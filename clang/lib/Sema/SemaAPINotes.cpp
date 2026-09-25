//===--- SemaAPINotes.cpp - API Notes Handling ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the mapping from API notes to declaration attributes.
//
//===----------------------------------------------------------------------===//

#include "SemaAPINotesInternal.h"
#include "TypeLocBuilder.h"
#include "clang/APINotes/APINotesReader.h"
#include "clang/APINotes/Types.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Analysis/Analyses/LifetimeSafety/LifetimeAnnotations.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Lexer.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaSwift.h"
#include "llvm/ADT/STLExtras.h"
#include <stack>

using namespace clang;

namespace {
enum class IsActive_t : bool { Inactive, Active };
enum class IsSubstitution_t : bool { Original, Replacement };

struct VersionedInfoMetadata {
  /// An empty version refers to unversioned metadata.
  VersionTuple Version;
  /// Which lookup group this slice came from.
  /// See the SwiftVersionedAddition comment in Attr.td.
  unsigned SliceGroup;
  unsigned IsActive : 1;
  unsigned IsReplacement : 1;

  VersionedInfoMetadata(VersionTuple Version, unsigned SliceGroup,
                        IsActive_t Active, IsSubstitution_t Replacement)
      : Version(Version), SliceGroup(SliceGroup),
        IsActive(Active == IsActive_t::Active),
        IsReplacement(Replacement == IsSubstitution_t::Replacement) {}
};
} // end anonymous namespace

/// The slice lookup groups a declaration can receive, numbered so that
/// ascending group order is the order Sema applies them in.
///
/// Sema makes up to two lookups per API notes reader. The broad lookup matches
/// on the declaration's name alone. The parameter-selector lookup matches on
/// the name plus a `Where: Parameters:` entry, and runs only for a declaration
/// that has a parameter selector. Sema applies them reader by reader, broad
/// first, so giving each reader an adjacent pair keeps the ordinals in
/// application order. A consumer needs that order to resolve two groups whose
/// winners set the same key.
///
/// \p Base is one above any group the declaration already carries, as a tag
/// does that inherited groups from a previous declaration before its own
/// lookup ran. Groups then number in the order they were attached.
static unsigned broadSliceGroup(unsigned Base, unsigned ReaderIndex) {
  return Base + 2 * ReaderIndex;
}

static unsigned parameterSelectorSliceGroup(unsigned Base,
                                            unsigned ReaderIndex) {
  return Base + 2 * ReaderIndex + 1;
}

namespace {
/// What an API notes addition or removal wrapper records: the group and
/// version of its slice, and the attribute it adds or the kind it removes.
struct CapturedSlice {
  unsigned Group;
  VersionTuple Version;
  attr::Kind Kind;
  /// The attribute an addition adds; null for a removal.
  Attr *Payload;
};
} // namespace

/// What \p A records, if it is an API notes addition or removal wrapper.
static std::optional<CapturedSlice> getCapturedSlice(const Attr *A) {
  if (const auto *Addition = dyn_cast<SwiftVersionedAdditionAttr>(A)) {
    Attr *Payload = Addition->getAdditionalAttr();
    return CapturedSlice{Addition->getSliceGroup(), Addition->getVersion(),
                         Payload->getKind(), Payload};
  }
  if (const auto *Removal = dyn_cast<SwiftVersionedRemovalAttr>(A))
    return CapturedSlice{Removal->getSliceGroup(), Removal->getVersion(),
                         Removal->getAttrKindToRemove(), nullptr};
  return std::nullopt;
}

/// The slice group an API notes wrapper or slice marker belongs to, if \p A is
/// one.
static std::optional<unsigned> apiNotesSliceGroup(const Attr *A) {
  if (std::optional<CapturedSlice> Slice = getCapturedSlice(A))
    return Slice->Group;
  if (const auto *Marker = dyn_cast<SwiftVersionedSliceAttr>(A))
    return Marker->getSliceGroup();
  return std::nullopt;
}

/// One above the highest slice group \p D carries, or zero.
static unsigned nextSliceGroup(const Decl *D) {
  unsigned Next = 0;
  for (const auto *A : D->attrs())
    if (std::optional<unsigned> Group = apiNotesSliceGroup(A))
      Next = std::max(Next, *Group + 1);
  return Next;
}

/// Determine whether this is a multi-level pointer type.
static bool isIndirectPointerType(QualType Type) {
  QualType Pointee = Type->getPointeeType();
  if (Pointee.isNull())
    return false;

  return Pointee->isAnyPointerType() || Pointee->isObjCObjectPointerType() ||
         Pointee->isMemberPointerType();
}

/// A replacement type from API notes, parsed, adjusted as the declaration it
/// replaces the type of requires, and checked. For a function or method, the
/// type replaced is its result type.
struct ParsedAPINotesType {
  /// What the declaration's type becomes.
  QualType Type;
  /// What its type source info becomes: the type as parsed.
  TypeSourceInfo *TypeInfo;
};

static std::optional<ParsedAPINotesType>
parseAPINotesType(Sema &S, Decl *D, StringRef TypeString);

/// Parse a 'Type:' or 'ResultType:' for capture, and wrap it as a slice.
/// Capture parses every slice's type, where the default mode parses only the
/// selected one, because whoever applies it later may have no parser; so it
/// also diagnoses every slice's.
static void captureAPINotesType(Sema &S, Decl *D, StringRef TypeString,
                                std::optional<ParsedAPINotesType> Parsed,
                                VersionedInfoMetadata Metadata) {
  if (!Parsed)
    return;
  TypeSourceInfo *Adjusted =
      Parsed->Type == Parsed->TypeInfo->getType()
          ? nullptr
          : S.Context.getTrivialTypeSourceInfo(Parsed->Type, D->getLocation());
  auto *TypeAttr = SwiftTypeAttr::CreateImplicit(S.Context, TypeString,
                                                 Parsed->TypeInfo, Adjusted);
  D->addAttr(SwiftVersionedAdditionAttr::CreateImplicit(
      S.Context, Metadata.Version, TypeAttr, Metadata.IsReplacement,
      Metadata.SliceGroup));
}

static void applyAPINotesType(Sema &S, Decl *decl, StringRef typeString,
                              VersionedInfoMetadata metadata) {
  if (typeString.empty())

    return;

  // Version-independent APINotes add "type" annotations
  // with a versioned attribute for the client to select and apply.
  if (S.captureSwiftVersionIndependentAPINotes()) {
    captureAPINotesType(S, decl, typeString,
                        parseAPINotesType(S, decl, typeString), metadata);
  } else {
    if (!metadata.IsActive)
      return;
    S.ApplyAPINotesType(decl, typeString);
  }
}

static NullabilityKind getNullabilityKind(const SwiftNullabilityAttr *A) {
  switch (A->getKind()) {
  case SwiftNullabilityAttr::Kind::NonNull:
    return NullabilityKind::NonNull;
  case SwiftNullabilityAttr::Kind::Nullable:
    return NullabilityKind::Nullable;
  case SwiftNullabilityAttr::Kind::Unspecified:
    return NullabilityKind::Unspecified;
  case SwiftNullabilityAttr::Kind::NullableResult:
    return NullabilityKind::NullableResult;
  }
  llvm_unreachable("unknown nullability");
}

/// Apply nullability to the given declaration.
static void applyNullability(Sema &S, Decl *decl, NullabilityKind nullability,
                             VersionedInfoMetadata metadata) {
  // Version-independent APINotes add "nullability" annotations
  // with a versioned attribute for the client to select and apply.
  if (S.captureSwiftVersionIndependentAPINotes()) {
    SwiftNullabilityAttr::Kind attrNullabilityKind;
    switch (nullability) {
    case NullabilityKind::NonNull:
      attrNullabilityKind = SwiftNullabilityAttr::Kind::NonNull;
      break;
    case NullabilityKind::Nullable:
      attrNullabilityKind = SwiftNullabilityAttr::Kind::Nullable;
      break;
    case NullabilityKind::Unspecified:
      attrNullabilityKind = SwiftNullabilityAttr::Kind::Unspecified;
      break;
    case NullabilityKind::NullableResult:
      attrNullabilityKind = SwiftNullabilityAttr::Kind::NullableResult;
      break;
    }
    auto *nullabilityAttr =
        SwiftNullabilityAttr::CreateImplicit(S.Context, attrNullabilityKind);
    auto *versioned = SwiftVersionedAdditionAttr::CreateImplicit(
        S.Context, metadata.Version, nullabilityAttr, metadata.IsReplacement,
        metadata.SliceGroup);
    decl->addAttr(versioned);
    return;
  } else {
    if (!metadata.IsActive)
      return;

    S.ApplyNullability(decl, nullability);
  }
}

/// Copy a string into ASTContext-allocated memory.
static StringRef ASTAllocateString(ASTContext &Ctx, StringRef String) {
  void *mem = Ctx.Allocate(String.size(), alignof(char *));
  memcpy(mem, String.data(), String.size());
  return StringRef(static_cast<char *>(mem), String.size());
}

static AttributeCommonInfo getPlaceholderAttrInfo() {
  return AttributeCommonInfo(SourceRange(),
                             AttributeCommonInfo::UnknownAttribute,
                             {AttributeCommonInfo::AS_GNU,
                              /*Spelling*/ 0, /*IsAlignas*/ false,
                              /*IsRegularKeywordAttribute*/ false});
}

namespace {
template <typename A> struct AttrKindFor {};

#define ATTR(X)                                                                \
  template <> struct AttrKindFor<X##Attr> {                                    \
    static const attr::Kind value = attr::X;                                   \
  };
#include "clang/Basic/AttrList.inc"
} // namespace

//===----------------------------------------------------------------------===//
// Displacement predicates
//
// Most API notes keys displace "the first attribute of the same kind". These
// three do not: isDisplacedBy, which every application goes through,
// dispatches to them by kind.
//===----------------------------------------------------------------------===//

/// A Swift-platform availability attribute, which `UnavailableInSwift`
/// displaces. A non-Swift availability attribute is untouched.
static bool isSwiftAvailabilityAttr(const Attr *A) {
  if (const auto *AA = dyn_cast<AvailabilityAttr>(A))
    if (const auto *II = AA->getPlatform())
      return II->isStr("swift");
  return false;
}

/// A `swift_attr` carrying a safety verdict, which `SwiftSafety` displaces.
static bool isSwiftSafetyAttr(const Attr *A) {
  if (const auto *SA = dyn_cast<SwiftAttrAttr>(A))
    return SA->getAttribute() == "safe" || SA->getAttribute() == "unsafe";
  return false;
}

/// Any retain-count convention attribute. The conventions are mutually
/// exclusive, so setting one displaces whichever of the five is present.
static bool isRetainCountConventionAttr(const Attr *A) {
  return isa<CFReturnsRetainedAttr, CFReturnsNotRetainedAttr,
             NSReturnsRetainedAttr, NSReturnsNotRetainedAttr,
             CFAuditedTransferAttr>(A);
}

/// Whether an API note of kind \p Kind displaces the existing attribute \p A.
///
/// For everything API notes produce, the kind decides the rule: only
/// UnavailableInSwift produces an availability attribute, only SwiftSafety a
/// swift_attr through this path, and only RetainCountConvention the
/// retain-count kinds. A new key that breaks this has to be told apart here.
static bool isDisplacedBy(const Attr *A, attr::Kind Kind) {
  switch (Kind) {
  case attr::Availability:
    return isSwiftAvailabilityAttr(A);
  case attr::SwiftAttr:
    return isSwiftSafetyAttr(A);
  case attr::CFUnknownTransfer:
  case attr::CFReturnsRetained:
  case attr::CFReturnsNotRetained:
  case attr::NSReturnsRetained:
  case attr::NSReturnsNotRetained:
    return isRetainCountConventionAttr(A);
  default:
    return A->getKind() == Kind;
  }
}

/// The existing attribute an API note of kind \p Kind displaces, which is the
/// first one it displaces.
static Decl::attr_iterator findAttrDisplacedBy(const Decl *D, attr::Kind Kind) {
  return llvm::find_if(D->attrs(), [Kind](const Attr *Next) {
    return isDisplacedBy(Next, Kind);
  });
}

/// Handle an attribute introduced by API notes.
/// The static kind-erased implementation.
///
/// \param IsAddition Whether we should add a new attribute
/// (otherwise, we might remove an existing attribute).
/// \param Kind The kind being added or removed. A removal records it, and it
/// decides which existing attribute an active slice displaces.
/// \param CreateAttr Create the new attribute to be added.
static void
handleAPINotedAttributeImpl(ASTContext &Ctx, Decl *D, bool IsAddition,
                            VersionedInfoMetadata Metadata, attr::Kind Kind,
                            llvm::function_ref<Attr *()> CreateAttr) {
  if (Metadata.IsActive) {
    const auto *Existing = findAttrDisplacedBy(D, Kind);
    if (Existing != D->attr_end()) {
      // Remove the existing attribute, and treat it as a superseded
      // non-versioned attribute.
      auto *Versioned = SwiftVersionedAdditionAttr::CreateImplicit(
          Ctx, Metadata.Version, *Existing, /*IsReplacedByActive*/ true,
          Metadata.SliceGroup);

      D->getAttrs().erase(Existing);
      D->addAttr(Versioned);
    }

    // If we're supposed to add a new attribute, do so.
    if (IsAddition) {
      if (auto *Attr = CreateAttr())
        D->addAttr(Attr);
    }

    return;
  }
  if (IsAddition) {
    if (auto *Attr = CreateAttr()) {
      auto *Versioned = SwiftVersionedAdditionAttr::CreateImplicit(
          Ctx, Metadata.Version, Attr,
          /*IsReplacedByActive*/ Metadata.IsReplacement, Metadata.SliceGroup);
      D->addAttr(Versioned);
    }
  } else {
    // FIXME: This isn't preserving enough information for things like
    // availability, where we're trying to remove a /specific/ kind of
    // attribute.
    auto *Versioned = SwiftVersionedRemovalAttr::CreateImplicit(
        Ctx, Metadata.Version, Kind,
        /*IsReplacedByActive*/ Metadata.IsReplacement, Metadata.SliceGroup);
    D->addAttr(Versioned);
  }
}

/// Handle an attribute introduced by API notes, naming it statically.
template <typename A>
static void handleAPINotedAttribute(Sema &S, Decl *D, bool IsAddition,
                                    VersionedInfoMetadata Metadata,
                                    llvm::function_ref<A *()> CreateAttr) {
  handleAPINotedAttributeImpl(S.Context, D, IsAddition, Metadata,
                              AttrKindFor<A>::value, CreateAttr);
}

template <typename A>
static void handleAPINotedRetainCountAttribute(Sema &S, Decl *D,
                                               bool ShouldAddAttribute,
                                               VersionedInfoMetadata Metadata) {
  // The template argument has a default to make the "removal" case more
  // concise; it doesn't matter /which/ attribute is being removed.
  handleAPINotedAttribute<A>(S, D, ShouldAddAttribute, Metadata, [&] {
    return new (S.Context) A(S.Context, getPlaceholderAttrInfo());
  });
}

static void handleAPINotedRetainCountConvention(
    Sema &S, Decl *D, VersionedInfoMetadata Metadata,
    std::optional<api_notes::RetainCountConventionKind> Convention) {
  if (!Convention)
    return;
  switch (*Convention) {
  case api_notes::RetainCountConventionKind::None:
    if (isa<FunctionDecl>(D)) {
      handleAPINotedRetainCountAttribute<CFUnknownTransferAttr>(
          S, D, /*shouldAddAttribute*/ true, Metadata);
    } else {
      handleAPINotedRetainCountAttribute<CFReturnsRetainedAttr>(
          S, D, /*shouldAddAttribute*/ false, Metadata);
    }
    break;
  case api_notes::RetainCountConventionKind::CFReturnsRetained:
    handleAPINotedRetainCountAttribute<CFReturnsRetainedAttr>(
        S, D, /*shouldAddAttribute*/ true, Metadata);
    break;
  case api_notes::RetainCountConventionKind::CFReturnsNotRetained:
    handleAPINotedRetainCountAttribute<CFReturnsNotRetainedAttr>(
        S, D, /*shouldAddAttribute*/ true, Metadata);
    break;
  case api_notes::RetainCountConventionKind::NSReturnsRetained:
    handleAPINotedRetainCountAttribute<NSReturnsRetainedAttr>(
        S, D, /*shouldAddAttribute*/ true, Metadata);
    break;
  case api_notes::RetainCountConventionKind::NSReturnsNotRetained:
    handleAPINotedRetainCountAttribute<NSReturnsNotRetainedAttr>(
        S, D, /*shouldAddAttribute*/ true, Metadata);
    break;
  }
}

/// Whether \p D carries a 'swift_attr' with exactly \p Attribute.
static bool hasSwiftAttr(const Decl *D, StringRef Attribute) {
  return llvm::any_of(
      D->specific_attrs<SwiftAttrAttr>(),
      [&](const SwiftAttrAttr *A) { return A->getAttribute() == Attribute; });
}

/// Add a 'swift_attr' unless \p D already carries that exact annotation.
static void addSwiftAttrIfAbsent(Sema &S, Decl *D, StringRef Attribute) {
  if (!hasSwiftAttr(D, Attribute))
    D->addAttr(SwiftAttrAttr::Create(S.Context, Attribute));
}

static void ProcessAPINotes(Sema &S, Decl *D,
                            const api_notes::CommonEntityInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Availability
  if (Info.Unavailable) {
    handleAPINotedAttribute<UnavailableAttr>(S, D, true, Metadata, [&] {
      return new (S.Context)
          UnavailableAttr(S.Context, getPlaceholderAttrInfo(),
                          ASTAllocateString(S.Context, Info.UnavailableMsg));
    });
  }

  if (Info.UnavailableInSwift) {
    handleAPINotedAttribute<AvailabilityAttr>(S, D, true, Metadata, [&] {
      return new (S.Context) AvailabilityAttr(
          S.Context, getPlaceholderAttrInfo(), &S.Context.Idents.get("swift"),
          VersionTuple(), VersionTuple(), VersionTuple(),
          /*Unavailable=*/true,
          ASTAllocateString(S.Context, Info.UnavailableMsg),
          /*Strict=*/false,
          /*Replacement=*/StringRef(),
          /*Priority=*/Sema::AP_Explicit,
          /*Environment=*/nullptr);
    });
  }

  // swift_private
  if (auto SwiftPrivate = Info.isSwiftPrivate()) {
    handleAPINotedAttribute<SwiftPrivateAttr>(
        S, D, *SwiftPrivate, Metadata, [&] {
          return new (S.Context)
              SwiftPrivateAttr(S.Context, getPlaceholderAttrInfo());
        });
  }

  // swift_safety
  if (auto SafetyKind = Info.getSwiftSafety()) {
    bool Addition = *SafetyKind != api_notes::SwiftSafetyKind::Unspecified;
    handleAPINotedAttribute<SwiftAttrAttr>(S, D, Addition, Metadata, [&] {
      return SwiftAttrAttr::Create(
          S.Context,
          *SafetyKind == api_notes::SwiftSafetyKind::Safe ? "safe" : "unsafe");
    });
  }

  // swift_name
  if (!Info.SwiftName.empty()) {
    AttributeFactory AF{};
    AttributePool AP{AF};
    auto &C = S.getASTContext();
    ParsedAttr *SNA = AP.create(&C.Idents.get("swift_name"), SourceRange(),
                                AttributeScopeInfo(), nullptr, nullptr, nullptr,
                                ParsedAttr::Form::GNU());
    const bool IsValid = S.Swift().DiagnoseName(
        D, Info.SwiftName, D->getLocation(), *SNA, /*IsAsync=*/false);

    // A rejected name adds nothing, but its slice still displaces the
    // declaration's own Swift name when selected. Capture has no attribute to
    // wrap for it, so it records a removal instead, which the collapse
    // replays as exactly that.
    handleAPINotedAttribute<SwiftNameAttr>(
        S, D, IsValid || !S.captureSwiftVersionIndependentAPINotes(), Metadata,
        [&]() -> SwiftNameAttr * {
          if (!IsValid)
            return nullptr;
          return new (S.Context)
              SwiftNameAttr(S.Context, getPlaceholderAttrInfo(),
                            ASTAllocateString(S.Context, Info.SwiftName));
        });
  }
}

static void ProcessAPINotes(Sema &S, Decl *D,
                            const api_notes::CommonTypeInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // swift_bridge
  if (auto SwiftBridge = Info.getSwiftBridge()) {
    handleAPINotedAttribute<SwiftBridgeAttr>(
        S, D, !SwiftBridge->empty(), Metadata, [&] {
          return new (S.Context)
              SwiftBridgeAttr(S.Context, getPlaceholderAttrInfo(),
                              ASTAllocateString(S.Context, *SwiftBridge));
        });
  }

  // ns_error_domain
  if (auto NSErrorDomain = Info.getNSErrorDomain()) {
    handleAPINotedAttribute<NSErrorDomainAttr>(
        S, D, !NSErrorDomain->empty(), Metadata, [&] {
          return new (S.Context)
              NSErrorDomainAttr(S.Context, getPlaceholderAttrInfo(),
                                &S.Context.Idents.get(*NSErrorDomain));
        });
  }

  if (auto ConformsTo = Info.getSwiftConformance())
    addSwiftAttrIfAbsent(S, D, "conforms_to:" + ConformsTo.value());

  ProcessAPINotes(S, D, static_cast<const api_notes::CommonEntityInfo &>(Info),
                  Metadata);
}

/// Check that the replacement type provided by API notes is reasonable.
///
/// This is a very weak form of ABI check.
static bool checkAPINotesReplacementType(Sema &S, SourceLocation Loc,
                                         QualType OrigType,
                                         QualType ReplacementType) {
  if (S.Context.getTypeSize(OrigType) !=
      S.Context.getTypeSize(ReplacementType)) {
    S.Diag(Loc, diag::err_incompatible_replacement_type)
        << ReplacementType << OrigType;
    return true;
  }

  return false;
}

/// Rebuild \p FD's type from its parameters' types, and from \p ResultType if
/// given, as ProcessAPINotes does once API notes changed either. An
/// unprototyped function takes no parameter types, so it is rebuilt only for a
/// new result type.
static void rebuildFunctionType(ASTContext &Ctx, FunctionDecl *FD,
                                std::optional<QualType> ResultType) {
  if (const auto *Proto = FD->getType()->getAs<FunctionProtoType>()) {
    SmallVector<QualType, 4> ParamTypes;
    for (const ParmVarDecl *Param : FD->parameters())
      ParamTypes.push_back(Param->getType());
    FD->setType(Ctx.getFunctionType(ResultType.value_or(Proto->getReturnType()),
                                    ParamTypes, Proto->getExtProtoInfo()));
  } else if (ResultType) {
    FD->setType(Ctx.getFunctionNoProtoType(
        *ResultType,
        FD->getType()->castAs<FunctionNoProtoType>()->getExtInfo()));
  }
}

/// Install a replacement type on \p D, as ProcessAPINotes does once it has
/// parsed, adjusted and checked it; for a function or method, its result type.
/// Needs no Sema.
static void applyTypeToDecl(ASTContext &Ctx, Decl *D, QualType Type,
                            TypeSourceInfo *TypeInfo) {
  if (auto *Var = dyn_cast<VarDecl>(D)) {
    Var->setType(Type);
    Var->setTypeSourceInfo(TypeInfo);
  } else if (auto *Property = dyn_cast<ObjCPropertyDecl>(D)) {
    Property->setType(Type, TypeInfo);
  } else if (auto *Field = dyn_cast<FieldDecl>(D)) {
    Field->setType(Type);
    Field->setTypeSourceInfo(TypeInfo);
  } else if (auto *Method = dyn_cast<ObjCMethodDecl>(D)) {
    Method->setReturnType(Type);
    Method->setReturnTypeSourceInfo(TypeInfo);
  } else if (auto *Function = dyn_cast<FunctionDecl>(D)) {
    rebuildFunctionType(Ctx, Function, Type);
  } else {
    llvm_unreachable("API notes allowed a type on an unknown declaration");
  }
}

static std::optional<ParsedAPINotesType>
parseAPINotesType(Sema &S, Decl *D, StringRef TypeString) {
  if (TypeString.empty() || !S.ParseTypeFromStringCallback)
    return std::nullopt;
  auto ParsedType = S.ParseTypeFromStringCallback(TypeString, "<API Notes>",
                                                  D->getLocation());
  if (!ParsedType.isUsable())
    return std::nullopt;
  QualType Type = Sema::GetTypeFromParser(ParsedType.get());
  auto *TypeInfo = S.Context.getTrivialTypeSourceInfo(Type, D->getLocation());
  QualType OrigType;
  if (auto *Var = dyn_cast<VarDecl>(D)) {
    // Make adjustments to parameter types.
    if (isa<ParmVarDecl>(Var)) {
      Type = S.ObjC().AdjustParameterTypeForObjCAutoRefCount(
          Type, D->getLocation(), TypeInfo);
      Type = S.Context.getAdjustedParameterType(Type);
    }
    OrigType = Var->getType();
  } else if (auto *Property = dyn_cast<ObjCPropertyDecl>(D)) {
    OrigType = Property->getType();
  } else if (auto *Field = dyn_cast<FieldDecl>(D)) {
    OrigType = Field->getType();
  } else if (auto *Method = dyn_cast<ObjCMethodDecl>(D)) {
    OrigType = Method->getReturnType();
  } else if (auto *Function = dyn_cast<FunctionDecl>(D)) {
    OrigType = Function->getReturnType();
  } else {
    llvm_unreachable("API notes allowed a type on an unknown declaration");
  }
  if (checkAPINotesReplacementType(S, D->getLocation(), OrigType, Type))
    return std::nullopt;
  return ParsedAPINotesType{Type, TypeInfo};
}

void Sema::ApplyAPINotesType(Decl *D, StringRef TypeString) {
  if (auto Parsed = parseAPINotesType(*this, D, TypeString))
    applyTypeToDecl(Context, D, Parsed->Type, Parsed->TypeInfo);
}

/// Apply 'Nullability:' to \p D, as Sema::ApplyNullability does. Needs no
/// Sema.
static void applyNullabilityToDecl(ASTContext &Context, Decl *D,
                                   NullabilityKind Nullability) {
  auto GetModified =
      [&](class Decl *D, QualType QT,
          NullabilityKind Nullability) -> std::optional<QualType> {
    QualType Original = QT;
    Sema::OverrideImplicitNullability(Context, QT, Nullability,
                                      isa<ParmVarDecl>(D));
    return (QT.getTypePtr() != Original.getTypePtr()) ? std::optional(QT)
                                                      : std::nullopt;
  };

  if (auto Function = dyn_cast<FunctionDecl>(D)) {
    if (auto Modified =
            GetModified(D, Function->getReturnType(), Nullability)) {
      const FunctionType *FnType = Function->getType()->castAs<FunctionType>();
      if (const FunctionProtoType *proto = dyn_cast<FunctionProtoType>(FnType))
        Function->setType(Context.getFunctionType(
            *Modified, proto->getParamTypes(), proto->getExtProtoInfo()));
      else
        Function->setType(
            Context.getFunctionNoProtoType(*Modified, FnType->getExtInfo()));
    }
  } else if (auto Method = dyn_cast<ObjCMethodDecl>(D)) {
    if (auto Modified = GetModified(D, Method->getReturnType(), Nullability)) {
      Method->setReturnType(*Modified);

      // Make it a context-sensitive keyword if we can.
      if (!isIndirectPointerType(*Modified))
        Method->setObjCDeclQualifier(Decl::ObjCDeclQualifier(
            Method->getObjCDeclQualifier() | Decl::OBJC_TQ_CSNullability));
    }
  } else if (auto Value = dyn_cast<ValueDecl>(D)) {
    if (auto Modified = GetModified(D, Value->getType(), Nullability)) {
      Value->setType(*Modified);

      // Make it a context-sensitive keyword if we can.
      if (auto Parm = dyn_cast<ParmVarDecl>(D)) {
        if (Parm->isObjCMethodParameter() && !isIndirectPointerType(*Modified))
          Parm->setObjCDeclQualifier(Decl::ObjCDeclQualifier(
              Parm->getObjCDeclQualifier() | Decl::OBJC_TQ_CSNullability));
      }
    }
  } else if (auto Property = dyn_cast<ObjCPropertyDecl>(D)) {
    if (auto Modified = GetModified(D, Property->getType(), Nullability)) {
      Property->setType(*Modified, Property->getTypeSourceInfo());

      // Make it a property attribute if we can.
      if (!isIndirectPointerType(*Modified))
        Property->setPropertyAttributes(
            ObjCPropertyAttribute::kind_null_resettable);
    }
  }
}

void Sema::ApplyNullability(Decl *D, NullabilityKind Nullability) {
  applyNullabilityToDecl(Context, D, Nullability);
}

/// Process API notes for a variable or property.
static void ProcessAPINotes(Sema &S, Decl *D,
                            const api_notes::VariableInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Type override.
  applyAPINotesType(S, D, Info.getType(), Metadata);

  // Nullability.
  if (auto Nullability = Info.getNullability())
    applyNullability(S, D, *Nullability, Metadata);

  // Handle common entity information.
  ProcessAPINotes(S, D, static_cast<const api_notes::CommonEntityInfo &>(Info),
                  Metadata);
}

/// Process API notes for a parameter.
static void ProcessAPINotes(Sema &S, ParmVarDecl *D,
                            const api_notes::ParamInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // noescape
  if (auto NoEscape = Info.isNoEscape())
    handleAPINotedAttribute<NoEscapeAttr>(S, D, *NoEscape, Metadata, [&] {
      return new (S.Context) NoEscapeAttr(S.Context, getPlaceholderAttrInfo());
    });

  if (auto Lifetimebound = Info.isLifetimebound())
    handleAPINotedAttribute<LifetimeBoundAttr>(
        S, D, *Lifetimebound, Metadata, [&] {
          return new (S.Context)
              LifetimeBoundAttr(S.Context, getPlaceholderAttrInfo());
        });

  // Retain count convention
  handleAPINotedRetainCountConvention(S, D, Metadata,
                                      Info.getRetainCountConvention());

  // Handle common entity information.
  ProcessAPINotes(S, D, static_cast<const api_notes::VariableInfo &>(Info),
                  Metadata);
}

/// Process API notes for a global variable.
static void ProcessAPINotes(Sema &S, VarDecl *D,
                            const api_notes::GlobalVariableInfo &Info,
                            VersionedInfoMetadata metadata) {
  // Handle common entity information.
  ProcessAPINotes(S, D, static_cast<const api_notes::VariableInfo &>(Info),
                  metadata);
}

/// Process API notes for a C field.
static void ProcessAPINotes(Sema &S, FieldDecl *D,
                            const api_notes::FieldInfo &Info,
                            VersionedInfoMetadata metadata) {
  // Handle common entity information.
  ProcessAPINotes(S, D, static_cast<const api_notes::VariableInfo &>(Info),
                  metadata);
}

/// Process API notes for an Objective-C property.
static void ProcessAPINotes(Sema &S, ObjCPropertyDecl *D,
                            const api_notes::ObjCPropertyInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Handle common entity information.
  ProcessAPINotes(S, D, static_cast<const api_notes::VariableInfo &>(Info),
                  Metadata);

  if (auto AsAccessors = Info.getSwiftImportAsAccessors()) {
    handleAPINotedAttribute<SwiftImportPropertyAsAccessorsAttr>(
        S, D, *AsAccessors, Metadata, [&] {
          return new (S.Context) SwiftImportPropertyAsAccessorsAttr(
              S.Context, getPlaceholderAttrInfo());
        });
  }
}

namespace {
typedef llvm::PointerUnion<FunctionDecl *, ObjCMethodDecl *> FunctionOrMethod;
}

/// Process API notes for a function or method.
static void ProcessAPINotes(Sema &S, FunctionOrMethod AnyFunc,
                            const api_notes::FunctionInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Find the declaration itself.
  FunctionDecl *FD = dyn_cast<FunctionDecl *>(AnyFunc);
  Decl *D = FD;
  ObjCMethodDecl *MD = nullptr;
  if (!D) {
    MD = cast<ObjCMethodDecl *>(AnyFunc);
    D = MD;
  }

  assert((FD || MD) && "Expecting Function or ObjCMethod");

  // Nullability of return type.
  if (Info.NullabilityAudited)
    applyNullability(S, D, Info.getReturnTypeInfo(), Metadata);

  // Add [[clang::unsafe_buffer_usage]]
  if (Info.UnsafeBufferUsage && !D->getAttr<UnsafeBufferUsageAttr>()) {
    handleAPINotedAttribute<UnsafeBufferUsageAttr>(S, D, true, Metadata, [&]() {
      return UnsafeBufferUsageAttr::Create(S.getASTContext(),
                                           getPlaceholderAttrInfo());
    });
  }

  // Parameters.
  unsigned NumParams = FD ? FD->getNumParams() : MD->param_size();

  bool AnyTypeChanged = false;
  for (unsigned I = 0; I != NumParams; ++I) {
    ParmVarDecl *Param = FD ? FD->getParamDecl(I) : MD->param_begin()[I];
    QualType ParamTypeBefore = Param->getType();

    if (I < Info.Params.size())
      ProcessAPINotes(S, Param, Info.Params[I], Metadata);

    // Nullability.
    if (Info.NullabilityAudited)
      applyNullability(S, Param, Info.getParamTypeInfo(I), Metadata);

    if (ParamTypeBefore.getAsOpaquePtr() != Param->getType().getAsOpaquePtr())
      AnyTypeChanged = true;
  }

  // returns_(un)retained
  if (!Info.SwiftReturnOwnership.empty())
    addSwiftAttrIfAbsent(S, D, "returns_" + Info.SwiftReturnOwnership);

  // Result type override. Capture parses every slice's, for whoever applies
  // it later. A function's type is rebuilt once, below.
  std::optional<QualType> OverriddenResultType;
  if (S.captureSwiftVersionIndependentAPINotes()) {
    captureAPINotesType(S, D, Info.ResultType,
                        parseAPINotesType(S, D, Info.ResultType), Metadata);
  } else if (Metadata.IsActive) {
    if (auto Parsed = parseAPINotesType(S, D, Info.ResultType)) {
      if (MD) {
        applyTypeToDecl(S.Context, MD, Parsed->Type, Parsed->TypeInfo);
      } else {
        OverriddenResultType = Parsed->Type;
        AnyTypeChanged = true;
      }
    }
  }

  // If the result type or any of the parameter types changed for a function
  // declaration, we have to rebuild the type.
  if (FD && AnyTypeChanged)
    rebuildFunctionType(S.Context, FD, OverriddenResultType);

  // Retain count convention
  handleAPINotedRetainCountConvention(S, D, Metadata,
                                      Info.getRetainCountConvention());

  // Handle common entity information.
  ProcessAPINotes(S, D, static_cast<const api_notes::CommonEntityInfo &>(Info),
                  Metadata);
}

/// Process API notes for a C++ method.
static void ProcessAPINotes(Sema &S, CXXMethodDecl *Method,
                            const api_notes::CXXMethodInfo &Info,
                            VersionedInfoMetadata Metadata) {
  if (Info.This && Info.This->isLifetimebound() &&
      !lifetimes::implicitObjectParamIsLifetimeBound(Method)) {
    auto MethodType = Method->getType();
    auto *attr = ::new (S.Context)
        LifetimeBoundAttr(S.Context, getPlaceholderAttrInfo());
    QualType AttributedType =
        S.Context.getAttributedType(attr, MethodType, MethodType);
    TypeLocBuilder TLB;
    TLB.pushFullCopy(Method->getTypeSourceInfo()->getTypeLoc());
    AttributedTypeLoc TyLoc = TLB.push<AttributedTypeLoc>(AttributedType);
    TyLoc.setAttr(attr);
    Method->setType(AttributedType);
    Method->setTypeSourceInfo(TLB.getTypeSourceInfo(S.Context, AttributedType));
  }

  ProcessAPINotes(S, (FunctionOrMethod)Method, Info, Metadata);
}

/// Process API notes for a global function.
static void ProcessAPINotes(Sema &S, FunctionDecl *D,
                            const api_notes::GlobalFunctionInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Handle common function information.
  ProcessAPINotes(S, FunctionOrMethod(D),
                  static_cast<const api_notes::FunctionInfo &>(Info), Metadata);
}

/// Process API notes for an enumerator.
static void ProcessAPINotes(Sema &S, EnumConstantDecl *D,
                            const api_notes::EnumConstantInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Handle common information.
  ProcessAPINotes(S, D, static_cast<const api_notes::CommonEntityInfo &>(Info),
                  Metadata);
}

/// Process API notes for an Objective-C method.
static void ProcessAPINotes(Sema &S, ObjCMethodDecl *D,
                            const api_notes::ObjCMethodInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Designated initializers.
  if (Info.DesignatedInit) {
    handleAPINotedAttribute<ObjCDesignatedInitializerAttr>(
        S, D, true, Metadata, [&] {
          if (ObjCInterfaceDecl *IFace = D->getClassInterface())
            IFace->setHasDesignatedInitializers();

          return new (S.Context) ObjCDesignatedInitializerAttr(
              S.Context, getPlaceholderAttrInfo());
        });
  }

  // Handle common function information.
  ProcessAPINotes(S, FunctionOrMethod(D),
                  static_cast<const api_notes::FunctionInfo &>(Info), Metadata);
}

/// Process API notes for a tag.
static void ProcessAPINotes(Sema &S, TagDecl *D, const api_notes::TagInfo &Info,
                            VersionedInfoMetadata Metadata) {
  if (auto ImportAs = Info.SwiftImportAs)
    addSwiftAttrIfAbsent(S, D, "import_" + ImportAs.value());

  if (auto RetainOp = Info.SwiftRetainOp)
    addSwiftAttrIfAbsent(S, D, "retain:" + RetainOp.value());

  if (auto ReleaseOp = Info.SwiftReleaseOp)
    addSwiftAttrIfAbsent(S, D, "release:" + ReleaseOp.value());
  if (auto DestroyOp = Info.SwiftDestroyOp)
    addSwiftAttrIfAbsent(S, D, "destroy:" + DestroyOp.value());
  if (auto DefaultOwnership = Info.SwiftDefaultOwnership)
    addSwiftAttrIfAbsent(
        S, D, "returned_as_" + DefaultOwnership.value() + "_by_default");

  if (auto Copyable = Info.isSwiftCopyable()) {
    if (!*Copyable)
      addSwiftAttrIfAbsent(S, D, "~Copyable");
  }

  if (auto Escapable = Info.isSwiftEscapable()) {
    addSwiftAttrIfAbsent(S, D, *Escapable ? "Escapable" : "~Escapable");
  }

  if (auto Extensibility = Info.EnumExtensibility) {
    using api_notes::EnumExtensibilityKind;
    bool ShouldAddAttribute = (*Extensibility != EnumExtensibilityKind::None);
    handleAPINotedAttribute<EnumExtensibilityAttr>(
        S, D, ShouldAddAttribute, Metadata, [&] {
          EnumExtensibilityAttr::Kind kind;
          switch (*Extensibility) {
          case EnumExtensibilityKind::None:
            llvm_unreachable("remove only");
          case EnumExtensibilityKind::Open:
            kind = EnumExtensibilityAttr::Open;
            break;
          case EnumExtensibilityKind::Closed:
            kind = EnumExtensibilityAttr::Closed;
            break;
          }
          return new (S.Context)
              EnumExtensibilityAttr(S.Context, getPlaceholderAttrInfo(), kind);
        });
  }

  if (auto FlagEnum = Info.isFlagEnum()) {
    handleAPINotedAttribute<FlagEnumAttr>(S, D, *FlagEnum, Metadata, [&] {
      return new (S.Context) FlagEnumAttr(S.Context, getPlaceholderAttrInfo());
    });
  }

  // Handle common type information.
  ProcessAPINotes(S, D, static_cast<const api_notes::CommonTypeInfo &>(Info),
                  Metadata);
}

/// Process API notes for a typedef.
static void ProcessAPINotes(Sema &S, TypedefNameDecl *D,
                            const api_notes::TypedefInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // swift_wrapper
  using SwiftWrapperKind = api_notes::SwiftNewTypeKind;

  if (auto SwiftWrapper = Info.SwiftWrapper) {
    handleAPINotedAttribute<SwiftNewTypeAttr>(
        S, D, *SwiftWrapper != SwiftWrapperKind::None, Metadata, [&] {
          SwiftNewTypeAttr::NewtypeKind Kind;
          switch (*SwiftWrapper) {
          case SwiftWrapperKind::None:
            llvm_unreachable("Shouldn't build an attribute");

          case SwiftWrapperKind::Struct:
            Kind = SwiftNewTypeAttr::NK_Struct;
            break;

          case SwiftWrapperKind::Enum:
            Kind = SwiftNewTypeAttr::NK_Enum;
            break;
          }
          AttributeCommonInfo SyntaxInfo{
              SourceRange(),
              AttributeCommonInfo::AT_SwiftNewType,
              {AttributeCommonInfo::AS_GNU, SwiftNewTypeAttr::GNU_swift_wrapper,
               /*IsAlignas*/ false, /*IsRegularKeywordAttribute*/ false}};
          return new (S.Context) SwiftNewTypeAttr(S.Context, SyntaxInfo, Kind);
        });
  }

  // Handle common type information.
  ProcessAPINotes(S, D, static_cast<const api_notes::CommonTypeInfo &>(Info),
                  Metadata);
}

/// Process API notes for an Objective-C class or protocol.
static void ProcessAPINotes(Sema &S, ObjCContainerDecl *D,
                            const api_notes::ContextInfo &Info,
                            VersionedInfoMetadata Metadata) {
  // Handle common type information.
  ProcessAPINotes(S, D, static_cast<const api_notes::CommonTypeInfo &>(Info),
                  Metadata);
}

/// Process API notes for an Objective-C class.
static void ProcessAPINotes(Sema &S, ObjCInterfaceDecl *D,
                            const api_notes::ContextInfo &Info,
                            VersionedInfoMetadata Metadata) {
  if (auto AsNonGeneric = Info.getSwiftImportAsNonGeneric()) {
    handleAPINotedAttribute<SwiftImportAsNonGenericAttr>(
        S, D, *AsNonGeneric, Metadata, [&] {
          return new (S.Context)
              SwiftImportAsNonGenericAttr(S.Context, getPlaceholderAttrInfo());
        });
  }

  if (auto ObjcMembers = Info.getSwiftObjCMembers()) {
    handleAPINotedAttribute<SwiftObjCMembersAttr>(
        S, D, *ObjcMembers, Metadata, [&] {
          return new (S.Context)
              SwiftObjCMembersAttr(S.Context, getPlaceholderAttrInfo());
        });
  }

  // Handle information common to Objective-C classes and protocols.
  ProcessAPINotes(S, static_cast<clang::ObjCContainerDecl *>(D), Info,
                  Metadata);
}

/// If we're applying API notes with an active, non-default version, and the
/// versioned API notes have a SwiftName but the declaration normally wouldn't
/// have one, add a removal attribute to make it clear that the new SwiftName
/// attribute only applies to the active version of \p D, not to all versions.
///
/// This must be run \em before processing API notes for \p D, because otherwise
/// any existing SwiftName attribute will have been packaged up in a
/// SwiftVersionedAdditionAttr.
///
/// \param Selected The selected slice's version: empty if the unversioned
/// slice is selected, or if none is.
/// \param SetsSwiftNameAt Whether the slice of \p SliceGroup at a given
/// version sets a Swift name.
static void maybeAttachUnversionedSwiftName(
    ASTContext &Ctx, Decl *D, VersionTuple Selected, unsigned SliceGroup,
    llvm::function_ref<bool(VersionTuple)> SetsSwiftNameAt) {
  // Is the active slice versioned, and does it set a Swift name that neither
  // the declaration nor the unversioned slice has?
  if (Selected.empty() || D->hasAttr<SwiftNameAttr>() ||
      !SetsSwiftNameAt(Selected) || SetsSwiftNameAt(VersionTuple()))
    return;

  // Then explicitly call that out with a removal, replaced by the selected
  // slice's name.
  D->addAttr(SwiftVersionedRemovalAttr::CreateImplicit(
      Ctx, Selected, attr::SwiftName, /*IsReplacedByActive=*/true, SliceGroup));
}

template <typename SpecificInfo>
static void maybeAttachUnversionedSwiftName(
    Sema &S, Decl *D,
    const api_notes::APINotesReader::VersionedInfo<SpecificInfo> &Info,
    unsigned SliceGroup) {
  if (!Info.getSelected())
    return;
  maybeAttachUnversionedSwiftName(
      S.Context, D, Info[*Info.getSelected()].first, SliceGroup,
      [&](VersionTuple Version) {
        return llvm::any_of(Info, [&](const auto &VersionAndInfoSlice) {
          return VersionAndInfoSlice.first == Version &&
                 !VersionAndInfoSlice.second.SwiftName.empty();
        });
      });
}

/// The parameters that take API notes from \p D's entry, if any.
static ArrayRef<ParmVarDecl *> getAPINotedParams(Decl *D) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    return FD->parameters();
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D))
    return MD->parameters();
  return {};
}

/// Drop \p D's markers for \p Group if no slice of that group left anything
/// else on it. A parameter is marked for every slice of its function's lookup,
/// in case that slice annotates it, and most slices do not.
static void dropUnusedSliceMarkers(Decl *D, unsigned Group) {
  if (!D->hasAttrs())
    return;
  AttrVec &Attrs = D->getAttrs();
  if (llvm::any_of(Attrs, [&](const Attr *A) {
        return !isa<SwiftVersionedSliceAttr>(A) &&
               apiNotesSliceGroup(A) == Group;
      }))
    return;
  llvm::erase_if(Attrs,
                 [&](const Attr *A) { return apiNotesSliceGroup(A) == Group; });
  if (Attrs.empty())
    D->dropAttrs();
}

/// Processes all versions of versioned API notes.
///
/// Just dispatches to the various ProcessAPINotes functions in this file.
///
/// \param SliceGroup Which group the slices in \p Info form. Selection runs
/// independently per group, so this has to travel with every slice.
template <typename SpecificDecl, typename SpecificInfo>
static void ProcessVersionedAPINotes(
    Sema &S, SpecificDecl *D,
    const api_notes::APINotesReader::VersionedInfo<SpecificInfo> &Info,
    unsigned SliceGroup) {

  if (!S.captureSwiftVersionIndependentAPINotes())
    maybeAttachUnversionedSwiftName(S, D, Info, SliceGroup);

  unsigned Selected = Info.getSelected().value_or(Info.size());

  VersionTuple Version;
  SpecificInfo InfoSlice;
  for (unsigned i = 0, e = Info.size(); i != e; ++i) {
    std::tie(Version, InfoSlice) = Info[i];
    auto Active = (i == Selected) ? IsActive_t::Active : IsActive_t::Inactive;
    auto Replacement = IsSubstitution_t::Original;

    // When collecting all APINotes as version-independent,
    // capture all as inactive and defer to the client to select the
    // right one.
    if (S.captureSwiftVersionIndependentAPINotes()) {
      Active = IsActive_t::Inactive;

      // Record that this slice exists, independently of whether it goes on to
      // set any key. A slice that sets nothing still wins selection for the
      // versions it covers, and winning suppresses every other slice, so a
      // client recomputing the selection cannot infer the slice set from the
      // addition and removal wrappers alone.
      //
      // A parameter's notes come from this same lookup and take its
      // selection, so each parameter is marked too. It collapses with its
      // function, but selects from its own markers.
      D->addAttr(SwiftVersionedSliceAttr::CreateImplicit(
          S.Context, Version, SliceGroup,
          SwiftVersionedSliceAttr::FromOwnLookup));
      for (ParmVarDecl *Param : getAPINotedParams(D))
        Param->addAttr(SwiftVersionedSliceAttr::CreateImplicit(
            S.Context, Version, SliceGroup,
            SwiftVersionedSliceAttr::FromOwnLookup));
    } else if (Active == IsActive_t::Inactive && Version.empty()) {
      Replacement = IsSubstitution_t::Replacement;
      Version = Info[Selected].first;
    }

    ProcessAPINotes(
        S, D, InfoSlice,
        VersionedInfoMetadata(Version, SliceGroup, Active, Replacement));
  }

  if (S.captureSwiftVersionIndependentAPINotes())
    for (ParmVarDecl *Param : getAPINotedParams(D))
      dropUnusedSliceMarkers(Param, SliceGroup);
}

static std::optional<api_notes::Context>
UnwindNamespaceContext(DeclContext *DC, api_notes::APINotesManager &APINotes) {
  if (auto NamespaceContext = dyn_cast<NamespaceDecl>(DC)) {
    for (auto Reader : APINotes.findAPINotes(NamespaceContext->getLocation())) {
      // Retrieve the context ID for the parent namespace of the decl.
      std::stack<NamespaceDecl *> NamespaceStack;
      {
        for (auto CurrentNamespace = NamespaceContext; CurrentNamespace;
             CurrentNamespace =
                 dyn_cast<NamespaceDecl>(CurrentNamespace->getParent())) {
          if (!CurrentNamespace->isInlineNamespace())
            NamespaceStack.push(CurrentNamespace);
        }
      }
      std::optional<api_notes::ContextID> NamespaceID;
      while (!NamespaceStack.empty()) {
        auto CurrentNamespace = NamespaceStack.top();
        NamespaceStack.pop();
        NamespaceID =
            Reader->lookupNamespaceID(CurrentNamespace->getName(), NamespaceID);
        if (!NamespaceID)
          return std::nullopt;
      }
      if (NamespaceID)
        return api_notes::Context(*NamespaceID,
                                  api_notes::ContextKind::Namespace);
    }
  }
  return std::nullopt;
}

static std::optional<api_notes::Context>
UnwindTagContext(TagDecl *DC, api_notes::APINotesManager &APINotes) {
  assert(DC && "tag context must not be null");
  for (auto Reader : APINotes.findAPINotes(DC->getLocation())) {
    // Retrieve the context ID for the parent tag of the decl.
    std::stack<TagDecl *> TagStack;
    {
      for (auto CurrentTag = DC; CurrentTag;
           CurrentTag = dyn_cast<TagDecl>(CurrentTag->getParent()))
        TagStack.push(CurrentTag);
    }
    assert(!TagStack.empty());
    std::optional<api_notes::Context> Ctx =
        UnwindNamespaceContext(TagStack.top()->getDeclContext(), APINotes);
    while (!TagStack.empty()) {
      auto CurrentTag = TagStack.top();
      TagStack.pop();
      auto CtxID = Reader->lookupTagID(CurrentTag->getName(), Ctx);
      if (!CtxID)
        return std::nullopt;
      Ctx = api_notes::Context(*CtxID, api_notes::ContextKind::Tag);
    }
    return Ctx;
  }
  return std::nullopt;
}

namespace clang {
struct APINotesParameterSelector {
  SmallVector<std::string, 4> Parameters;

  bool operator==(const APINotesParameterSelector &Other) const {
    return Parameters == Other.Parameters;
  }

  bool operator!=(const APINotesParameterSelector &Other) const {
    return !(*this == Other);
  }
};

struct APINotesParameterSelectorCandidates {
  APINotesParameterSelector Source;
  std::optional<APINotesParameterSelector> Desugared;
};
} // namespace clang

static PrintingPolicy
getAPINotesParameterSelectorPrintingPolicy(const ASTContext &Context) {
  PrintingPolicy Policy(Context.getLangOpts());
  Policy.PrintAsCanonical = false;
  Policy.FullyQualifiedName = false;
  Policy.SuppressScope = false;
  Policy.UsePreferredNames = false;
  Policy.MSVCFormatting = false;
  Policy.SplitTemplateClosers = false;
  Policy.IncludeNewlines = false;
  return Policy;
}

// Print the APINotes selector spelling for one parameter. The source-spelled
// selector is tried first. The desugared spelling is only a permissive
// fallback.
static std::string getAPINotesParameterSelectorSpelling(
    QualType ParamType, const ASTContext &Context, const PrintingPolicy &Policy,
    bool Desugar) {
  if (Desugar)
    ParamType = ParamType.getDesugaredType(Context);

  ParamType.removeLocalConst();
  ParamType.removeLocalVolatile();
  ParamType = ParamType.stripNullability(Context);

  return ParamType.getAsString(Policy);
}

static std::optional<APINotesParameterSelectorCandidates>
getAPINotesParameterSelectorCandidates(const Sema &S, const FunctionDecl *FD) {
  const auto *FPT = FD->getType()->getAs<FunctionProtoType>();
  if (!FPT)
    return std::nullopt;

  APINotesParameterSelectorCandidates Candidates;
  APINotesParameterSelector Desugared;
  Candidates.Source.Parameters.reserve(FPT->getNumParams());
  Desugared.Parameters.reserve(FPT->getNumParams());

  const PrintingPolicy Policy =
      getAPINotesParameterSelectorPrintingPolicy(S.Context);
  for (QualType ParamType : FPT->param_types()) {
    Candidates.Source.Parameters.push_back(
        getAPINotesParameterSelectorSpelling(ParamType, S.Context, Policy,
                                             /*Desugar=*/false));
    Desugared.Parameters.push_back(getAPINotesParameterSelectorSpelling(
        ParamType, S.Context, Policy, /*Desugar=*/true));
  }

  if (Candidates.Source != Desugared)
    Candidates.Desugared = std::move(Desugared);

  return Candidates;
}

APINotesSelectorDiagnosticReaderState &
APINotesSelectorDiagnosticState::getOrCreateReaderState(
    api_notes::APINotesReader &Reader) {
  auto [StateIt, Inserted] = Readers.try_emplace(&Reader);
  APINotesSelectorDiagnosticReaderState &State = StateIt->second;
  if (!Inserted)
    return State;

  SmallVector<api_notes::APINotesFunctionSelectorKey, 4> Selectors;
  Reader.collectExactFunctionParameterSelectors(Selectors);
  State.addSelectors(Selectors);
  return State;
}

static APINotesSelectorDiagnosticReaderState &
getAPINotesSelectorDiagnosticState(Sema &S, api_notes::APINotesReader *Reader) {
  if (!S.APINotesSelectorDiagnostics)
    S.APINotesSelectorDiagnostics =
        std::make_unique<APINotesSelectorDiagnosticState>();

  return S.APINotesSelectorDiagnostics->getOrCreateReaderState(*Reader);
}

void APINotesSelectorDiagnosticReaderState::markCandidatesUsed(
    llvm::function_ref<std::optional<api_notes::APINotesFunctionSelectorKey>(
        ArrayRef<std::string>)>
        GetSelectorKey,
    const APINotesParameterSelectorCandidates &Candidates) {
  if (auto Key = GetSelectorKey(Candidates.Source.Parameters))
    markUsed(*Key);
  if (Candidates.Desugared) {
    if (auto Key = GetSelectorKey(Candidates.Desugared->Parameters))
      markUsed(*Key);
  }
}

/// Apply the first exact selector entry found. This preserves source-spelling
/// precedence over the desugared fallback and avoids applying multiple exact
/// entries for the same declaration.
///
/// \param SliceGroup Which group the slices from \p LookupExact form. This
/// lookup runs its own version selection, so it is a group distinct from the
/// broad lookup beside it even though both read the same reader. Pass
/// parameterSelectorSliceGroup() for the reader being read.
template <typename SpecificInfo, typename SpecificDecl>
static void processExactAPINotes(
    Sema &S, SpecificDecl *D,
    const APINotesParameterSelectorCandidates &ParameterSelectorCandidates,
    llvm::function_ref<api_notes::APINotesReader::VersionedInfo<SpecificInfo>(
        ArrayRef<std::string>)>
        LookupExact,
    unsigned SliceGroup) {
  auto ProcessSelector = [&](const APINotesParameterSelector &Selector) {
    auto Info = LookupExact(Selector.Parameters);
    if (Info.size() == 0)
      return false;

    ProcessVersionedAPINotes(S, D, Info, SliceGroup);
    return true;
  };

  if (ProcessSelector(ParameterSelectorCandidates.Source))
    return;

  if (ParameterSelectorCandidates.Desugared)
    ProcessSelector(*ParameterSelectorCandidates.Desugared);
}

/// Process API notes that are associated with this declaration, mapping them
/// to attributes as appropriate.
void Sema::ProcessAPINotes(Decl *D) {
  if (!D)
    return;
  // A parameter's notes come from its function's entry. Its DeclContext is
  // not yet the function when its own attributes are processed, so it would
  // otherwise be looked up as a global variable of the same name.
  if (isa<ParmVarDecl>(D))
    return;
  if (!APINotes.hasAPINotes())
    return;
  auto Readers = APINotes.findAPINotes(D->getLocation());
  if (Readers.empty())
    return;
  const unsigned GroupBase = nextSliceGroup(D);

  auto *DC = D->getDeclContext();
  // Globals.
  if (DC->isFileContext() || DC->isNamespace() ||
      DC->getDeclKind() == Decl::LinkageSpec) {
    std::optional<api_notes::Context> APINotesContext =
        UnwindNamespaceContext(DC, APINotes);
    // Global variables.
    if (auto VD = dyn_cast<VarDecl>(D)) {
      for (auto [ReaderIndex, Reader] : llvm::enumerate(Readers)) {
        auto Info =
            Reader->lookupGlobalVariable(VD->getName(), APINotesContext);
        ProcessVersionedAPINotes(*this, VD, Info,
                                 broadSliceGroup(GroupBase, ReaderIndex));
      }

      return;
    }

    // Global functions.
    if (auto FD = dyn_cast<FunctionDecl>(D)) {
      if (FD->getDeclName().isIdentifier()) {
        auto ParameterSelectorCandidates =
            getAPINotesParameterSelectorCandidates(*this, FD);

        for (auto [ReaderIndex, ReaderRef] : llvm::enumerate(Readers)) {
          // Capturing a structured binding is a C++20 extension, so the
          // lambdas below capture a plain local instead.
          auto *Reader = ReaderRef;

          auto Info =
              Reader->lookupGlobalFunction(FD->getName(), APINotesContext);
          ProcessVersionedAPINotes(*this, FD, Info,
                                   broadSliceGroup(GroupBase, ReaderIndex));

          if (ParameterSelectorCandidates)
            processExactAPINotes<api_notes::GlobalFunctionInfo>(
                *this, FD, *ParameterSelectorCandidates,
                [&](ArrayRef<std::string> Parameters) {
                  return Reader->lookupGlobalFunction(FD->getName(), Parameters,
                                                      APINotesContext);
                },
                parameterSelectorSliceGroup(GroupBase, ReaderIndex));

          if (ParameterSelectorCandidates) {
            auto &DiagnosticState =
                getAPINotesSelectorDiagnosticState(*this, Reader);
            if (auto BroadKey = Reader->getGlobalFunctionSelectorKey(
                    FD->getName(), APINotesContext))
              DiagnosticState.noteSeenDeclaration(*BroadKey, FD->getName(),
                                                  FD->getLocation());
            DiagnosticState.markCandidatesUsed(
                [&](ArrayRef<std::string> Parameters) {
                  return Reader->getGlobalFunctionSelectorKey(
                      FD->getName(), Parameters, APINotesContext);
                },
                *ParameterSelectorCandidates);
          }
        }
      }

      return;
    }

    // Objective-C classes.
    if (auto Class = dyn_cast<ObjCInterfaceDecl>(D)) {
      for (auto [ReaderIndex, Reader] : llvm::enumerate(Readers)) {
        auto Info = Reader->lookupObjCClassInfo(Class->getName());
        ProcessVersionedAPINotes(*this, Class, Info,
                                 broadSliceGroup(GroupBase, ReaderIndex));
      }

      return;
    }

    // Objective-C protocols.
    if (auto Protocol = dyn_cast<ObjCProtocolDecl>(D)) {
      for (auto [ReaderIndex, Reader] : llvm::enumerate(Readers)) {
        auto Info = Reader->lookupObjCProtocolInfo(Protocol->getName());
        ProcessVersionedAPINotes(*this, Protocol, Info,
                                 broadSliceGroup(GroupBase, ReaderIndex));
      }

      return;
    }

    // Tags
    if (auto Tag = dyn_cast<TagDecl>(D)) {
      // Determine the name of the entity to search for. If this is an
      // anonymous tag that gets its linked name from a typedef, look for the
      // typedef name. This allows tag-specific information to be added
      // to the declaration.
      std::string LookupName;
      if (auto typedefName = Tag->getTypedefNameForAnonDecl())
        LookupName = typedefName->getName().str();
      else
        LookupName = Tag->getName().str();

      // Use the source location to discern if this Tag is an OPTIONS macro.
      // For now we would like to limit this trick of looking up the APINote tag
      // using the EnumDecl's QualType in the case where the enum is anonymous.
      // This is only being used to support APINotes lookup for C++
      // NS/CF_OPTIONS when C++-Interop is enabled.
      std::string MacroName =
          LookupName.empty() && Tag->getOuterLocStart().isMacroID()
              ? clang::Lexer::getImmediateMacroName(
                    Tag->getOuterLocStart(),
                    Tag->getASTContext().getSourceManager(), LangOpts)
                    .str()
              : "";

      if (LookupName.empty() && isa<clang::EnumDecl>(Tag) &&
          (MacroName == "CF_OPTIONS" || MacroName == "NS_OPTIONS" ||
           MacroName == "OBJC_OPTIONS" || MacroName == "SWIFT_OPTIONS")) {

        clang::QualType T = llvm::cast<clang::EnumDecl>(Tag)->getIntegerType();
        LookupName = clang::QualType::getAsString(
            T.split(), getASTContext().getPrintingPolicy());
      }

      for (auto [ReaderIndex, Reader] : llvm::enumerate(Readers)) {
        if (auto ParentTag = dyn_cast<TagDecl>(Tag->getDeclContext()))
          APINotesContext = UnwindTagContext(ParentTag, APINotes);
        auto Info = Reader->lookupTag(LookupName, APINotesContext);
        ProcessVersionedAPINotes(*this, Tag, Info,
                                 broadSliceGroup(GroupBase, ReaderIndex));
      }

      return;
    }

    // Typedefs
    if (auto Typedef = dyn_cast<TypedefNameDecl>(D)) {
      for (auto [ReaderIndex, Reader] : llvm::enumerate(Readers)) {
        auto Info = Reader->lookupTypedef(Typedef->getName(), APINotesContext);
        ProcessVersionedAPINotes(*this, Typedef, Info,
                                 broadSliceGroup(GroupBase, ReaderIndex));
      }

      return;
    }
  }

  // Enumerators.
  if (DC->getRedeclContext()->isFileContext() ||
      DC->getRedeclContext()->isExternCContext()) {
    if (auto EnumConstant = dyn_cast<EnumConstantDecl>(D)) {
      for (auto [ReaderIndex, Reader] : llvm::enumerate(Readers)) {
        auto Info = Reader->lookupEnumConstant(EnumConstant->getName());
        ProcessVersionedAPINotes(*this, EnumConstant, Info,
                                 broadSliceGroup(GroupBase, ReaderIndex));
      }

      return;
    }
  }

  if (auto ObjCContainer = dyn_cast<ObjCContainerDecl>(DC)) {
    // Location function that looks up an Objective-C context.
    auto GetContext = [&](api_notes::APINotesReader *Reader)
        -> std::optional<api_notes::ContextID> {
      if (auto Protocol = dyn_cast<ObjCProtocolDecl>(ObjCContainer)) {
        if (auto Found = Reader->lookupObjCProtocolID(Protocol->getName()))
          return *Found;

        return std::nullopt;
      }

      if (auto Impl = dyn_cast<ObjCCategoryImplDecl>(ObjCContainer)) {
        if (auto Cat = Impl->getCategoryDecl())
          ObjCContainer = Cat->getClassInterface();
        else
          return std::nullopt;
      }

      if (auto Category = dyn_cast<ObjCCategoryDecl>(ObjCContainer)) {
        if (Category->getClassInterface())
          ObjCContainer = Category->getClassInterface();
        else
          return std::nullopt;
      }

      if (auto Impl = dyn_cast<ObjCImplDecl>(ObjCContainer)) {
        if (Impl->getClassInterface())
          ObjCContainer = Impl->getClassInterface();
        else
          return std::nullopt;
      }

      if (auto Class = dyn_cast<ObjCInterfaceDecl>(ObjCContainer)) {
        if (auto Found = Reader->lookupObjCClassID(Class->getName()))
          return *Found;

        return std::nullopt;
      }

      return std::nullopt;
    };

    // Objective-C methods.
    if (auto Method = dyn_cast<ObjCMethodDecl>(D)) {
      for (auto [ReaderIndex, Reader] : llvm::enumerate(Readers)) {
        if (auto Context = GetContext(Reader)) {
          // Map the selector.
          Selector Sel = Method->getSelector();
          SmallVector<StringRef, 2> SelPieces;
          if (Sel.isUnarySelector()) {
            SelPieces.push_back(Sel.getNameForSlot(0));
          } else {
            for (unsigned i = 0, n = Sel.getNumArgs(); i != n; ++i)
              SelPieces.push_back(Sel.getNameForSlot(i));
          }

          api_notes::ObjCSelectorRef SelectorRef;
          SelectorRef.NumArgs = Sel.getNumArgs();
          SelectorRef.Identifiers = SelPieces;

          auto Info = Reader->lookupObjCMethod(*Context, SelectorRef,
                                               Method->isInstanceMethod());
          ProcessVersionedAPINotes(*this, Method, Info,
                                   broadSliceGroup(GroupBase, ReaderIndex));
        }
      }
    }

    // Objective-C properties.
    if (auto Property = dyn_cast<ObjCPropertyDecl>(D)) {
      for (auto [ReaderIndex, Reader] : llvm::enumerate(Readers)) {
        if (auto Context = GetContext(Reader)) {
          bool isInstanceProperty =
              (Property->getPropertyAttributesAsWritten() &
               ObjCPropertyAttribute::kind_class) == 0;
          auto Info = Reader->lookupObjCProperty(*Context, Property->getName(),
                                                 isInstanceProperty);
          ProcessVersionedAPINotes(*this, Property, Info,
                                   broadSliceGroup(GroupBase, ReaderIndex));
        }
      }

      return;
    }
  }

  if (auto TagContext = dyn_cast<TagDecl>(DC)) {
    if (auto CXXMethod = dyn_cast<CXXMethodDecl>(D)) {
      if (!isa<CXXConstructorDecl>(CXXMethod) &&
          !isa<CXXDestructorDecl>(CXXMethod) &&
          !isa<CXXConversionDecl>(CXXMethod)) {
        auto ParameterSelectorCandidates =
            getAPINotesParameterSelectorCandidates(*this, CXXMethod);
        for (auto [ReaderIndex, ReaderRef] : llvm::enumerate(Readers)) {
          // Capturing a structured binding is a C++20 extension, so the
          // lambdas below capture a plain local instead.
          auto *Reader = ReaderRef;

          if (auto Context = UnwindTagContext(TagContext, APINotes)) {
            std::string MethodName;
            if (CXXMethod->isOverloadedOperator())
              MethodName =
                  std::string("operator") +
                  getOperatorSpelling(CXXMethod->getOverloadedOperator());
            else
              MethodName = CXXMethod->getName();

            auto Info = Reader->lookupCXXMethod(Context->id, MethodName);
            ProcessVersionedAPINotes(*this, CXXMethod, Info,
                                     broadSliceGroup(GroupBase, ReaderIndex));

            if (ParameterSelectorCandidates)
              processExactAPINotes<api_notes::CXXMethodInfo>(
                  *this, CXXMethod, *ParameterSelectorCandidates,
                  [&](ArrayRef<std::string> Parameters) {
                    return Reader->lookupCXXMethod(Context->id, MethodName,
                                                   Parameters);
                  },
                  parameterSelectorSliceGroup(GroupBase, ReaderIndex));

            if (ParameterSelectorCandidates) {
              auto &DiagnosticState =
                  getAPINotesSelectorDiagnosticState(*this, Reader);
              if (auto BroadKey =
                      Reader->getCXXMethodSelectorKey(Context->id, MethodName))
                DiagnosticState.noteSeenDeclaration(*BroadKey, MethodName,
                                                    CXXMethod->getLocation());
              DiagnosticState.markCandidatesUsed(
                  [&](ArrayRef<std::string> Parameters) {
                    return Reader->getCXXMethodSelectorKey(
                        Context->id, MethodName, Parameters);
                  },
                  *ParameterSelectorCandidates);
            }
          }
        }
      }
    }

    if (auto Field = dyn_cast<FieldDecl>(D)) {
      if (!Field->isUnnamedBitField() && !Field->isAnonymousStructOrUnion()) {
        for (auto [ReaderIndex, Reader] : llvm::enumerate(Readers)) {
          if (auto Context = UnwindTagContext(TagContext, APINotes)) {
            auto Info = Reader->lookupField(Context->id, Field->getName());
            ProcessVersionedAPINotes(*this, Field, Info,
                                     broadSliceGroup(GroupBase, ReaderIndex));
          }
        }
      }
    }

    if (auto Tag = dyn_cast<TagDecl>(D)) {
      for (auto [ReaderIndex, Reader] : llvm::enumerate(Readers)) {
        if (auto Context = UnwindTagContext(TagContext, APINotes)) {
          auto Info = Reader->lookupTag(Tag->getName(), Context);
          ProcessVersionedAPINotes(*this, Tag, Info,
                                   broadSliceGroup(GroupBase, ReaderIndex));
        }
      }
    }
  }
}

void APINotesSelectorDiagnosticReaderState::diagnoseUnused(
    Sema &S, api_notes::APINotesReader &Reader) const {
  for (const auto &Selector : SelectorUsed) {
    if (Selector.second)
      continue;

    auto SeenName =
        SeenNames.find(Selector.first.getWithoutParameterSelector());
    if (SeenName == SeenNames.end())
      continue;

    std::optional<SmallVector<std::string, 4>> ParameterSpellings =
        Reader.getParameterSelectorSpellingsForDiagnostics(Selector.first);
    if (!ParameterSpellings)
      continue;

    S.Diag(SeenName->second.Loc, diag::warn_apinotes_message)
        << (llvm::Twine("API notes entry for '") + SeenName->second.Name +
            "' has unmatched Where.Parameters " +
            api_notes::formatAPINotesParameterSelector(*ParameterSpellings))
               .str();
  }
}

void APINotesSelectorDiagnosticState::diagnoseUnused(Sema &S) const {
  for (const auto &ReaderSelectors : Readers)
    ReaderSelectors.second.diagnoseUnused(S, *ReaderSelectors.first);
}

void Sema::DiagnoseUnusedAPINotesSelectors() {
  if (!APINotesSelectorDiagnostics)
    return;

  if (!Diags.isIgnored(diag::warn_apinotes_message, SourceLocation()))
    APINotesSelectorDiagnostics->diagnoseUnused(*this);
  APINotesSelectorDiagnostics.reset();
}

//===----------------------------------------------------------------------===//
// Consumer-side collapse
//
// A module built with -fswift-version-independent-apinotes carries every API
// notes slice unapplied, wrapped. A consumer of that module has one Swift
// version, so on reading a declaration it runs the selection the default mode
// would have run and applies the winner, reproducing the default mode's
// attribute list exactly, order included. See ProcessVersionedAPINotes above
// for the shape being reproduced.
//===----------------------------------------------------------------------===//

/// The winning slice version of each slice group that has a winner, empty for
/// an unversioned winner.
using SelectedSlices = llvm::SmallDenseMap<unsigned, VersionTuple, 8>;

/// Selection, once per slice group, as APINotesReader::VersionedInfo's
/// constructor makes it: the lowest slice at or above \p Requested, else the
/// unversioned slice. The match is gated on a non-empty requested version
/// there, so with none requested only an unversioned slice can win.
static SelectedSlices selectCapturedSlices(const Decl *D,
                                           VersionTuple Requested) {
  SelectedSlices Selected;
  for (const auto *Marker : D->specific_attrs<SwiftVersionedSliceAttr>()) {
    const VersionTuple Version = Marker->getVersion();
    const bool Matches = !Requested.empty() && Version >= Requested;
    if (!Matches && !Version.empty())
      continue;
    auto [It, Inserted] =
        Selected.try_emplace(Marker->getSliceGroup(), Version);
    // A match beats the unversioned slice, and a lower match a higher one.
    if (!Inserted && Matches && (It->second.empty() || Version < It->second))
      It->second = Version;
  }
  return Selected;
}

/// Whether \p A is a captured Swift name from the slice of \p Group at
/// \p Version: an addition of one, or, for a name the producer rejected, a
/// removal. The default mode tests only that the slice names something.
static bool isCapturedSwiftName(const Attr *A, unsigned Group,
                                VersionTuple Version) {
  std::optional<CapturedSlice> Slice = getCapturedSlice(A);
  return Slice && Slice->Group == Group && Slice->Version == Version &&
         Slice->Kind == attr::SwiftName;
}

/// Apply the winner of a slice group that \p D received from another
/// declaration, the way the attribute that winner leaves live there reaches
/// \p D in the default mode: inherited by a redeclaration, under
/// mergeDeclAttribute's rules for the kinds API notes produce, or copied to a
/// property's implicit accessor.
///
/// \param Payload The winner's attribute, or null for a removal.
static void applyReceivedWinner(ASTContext &Ctx, Decl *D, const Attr *Payload,
                                attr::Kind Kind,
                                SwiftVersionedSliceAttr::OriginKind Origin) {
  const bool IsInherited = Origin == SwiftVersionedSliceAttr::FromRedeclaration;

  // On the other declaration the slice displaced an attribute, so the default
  // mode never passes that one on, but D received it all the same, as a plain
  // copy. The copy goes. A redeclaration's own attributes stay: in the default
  // mode they never meet the previous declaration's slices. An accessor has
  // none of its own yet when it receives the property's.
  auto Displaced = llvm::find_if(D->attrs(), [&](const Attr *A) {
    return (!IsInherited || A->isInherited()) && isDisplacedBy(A, Kind);
  });
  if (Displaced != D->attr_end())
    D->getAttrs().erase(Displaced);
  if (!Payload)
    return;

  // AddPropertyAttrs copies unconditionally.
  if (!IsInherited) {
    D->addAttr(Payload->clone(Ctx));
    return;
  }

  switch (Payload->getKind()) {
  case attr::SwiftName:
    // mergeNameAttr replaces D's own name. A different own name is an error,
    // which the producer has already diagnosed.
    D->dropAttr<SwiftNameAttr>();
    break;
  case attr::SwiftAttr:
    // mergeAttrAttr: only an identical swift_attr is a duplicate.
    if (hasSwiftAttr(D, cast<SwiftAttrAttr>(Payload)->getAttribute()))
      return;
    break;
  case attr::Availability:
    // An approximation of mergeAvailabilityAttr, which merges versions: D's
    // own Swift availability wins.
    if (llvm::any_of(D->attrs(), isSwiftAvailabilityAttr))
      return;
    break;
  default:
    // DeclHasAttr.
    if (llvm::any_of(D->attrs(), [&](const Attr *A) {
          return A->getKind() == Payload->getKind();
        }))
      return;
  }
  auto *Inherited = cast<InheritableAttr>(Payload->clone(Ctx));
  Inherited->setInherited(true);
  D->addAttr(Inherited);
}

bool clang::isAPINotesInferenceSuppressed(const Decl *D, attr::Kind Kind) {
  switch (Kind) {
  case attr::NSReturnsRetained:
    // An init method only avoids a second copy.
    if (cast<ObjCMethodDecl>(D)->getMethodFamily() == OMF_init)
      return D->hasAttr<NSReturnsRetainedAttr>();
    return D->hasAttr<NSReturnsRetainedAttr>() ||
           D->hasAttr<NSReturnsNotRetainedAttr>() ||
           D->hasAttr<NSReturnsAutoreleasedAttr>();
  case attr::CFAuditedTransfer:
    return D->hasAttr<CFAuditedTransferAttr>() ||
           D->hasAttr<CFUnknownTransferAttr>();
  default:
    llvm_unreachable("Sema infers no other attribute after API notes");
  }
}

static Attr *createInferredAttr(ASTContext &Ctx, attr::Kind Kind) {
  if (Kind == attr::NSReturnsRetained)
    return NSReturnsRetainedAttr::CreateImplicit(Ctx);
  return CFAuditedTransferAttr::CreateImplicit(Ctx);
}

/// The type API notes rewrite on \p D: a method's return type, or its own.
static QualType getAPINotedType(const Decl *D) {
  if (const auto *Method = dyn_cast<ObjCMethodDecl>(D))
    return Method->getReturnType();
  if (const auto *Property = dyn_cast<ObjCPropertyDecl>(D))
    return Property->getType();
  return cast<ValueDecl>(D)->getType();
}

/// Apply the winning slice's 'Type:', 'ResultType:' or nullability to \p D,
/// as ProcessAPINotes applies its own slices'. For a slice \p D received from
/// another declaration, apply what that declaration's type passes on: its
/// nullability to a redeclaration, as type merging does, and its type to a
/// property's implicit accessor, which synthesis derives from it.
///
/// \returns Whether \p D's type changed.
static bool applyCapturedType(ASTContext &Ctx, Decl *D, const Attr *Payload,
                              SwiftVersionedSliceAttr::OriginKind Origin) {
  const auto *Type = dyn_cast<SwiftTypeAttr>(Payload);
  const auto *Nullability = dyn_cast<SwiftNullabilityAttr>(Payload);
  auto *Method = dyn_cast<ObjCMethodDecl>(D);
  auto *FD = dyn_cast<FunctionDecl>(D);
  auto *Value = dyn_cast<ValueDecl>(D);
  const QualType Before = getAPINotedType(D);

  switch (Origin) {
  case SwiftVersionedSliceAttr::FromOwnLookup:
    if (Nullability)
      applyNullabilityToDecl(Ctx, D, getNullabilityKind(Nullability));
    else
      applyTypeToDecl(Ctx, D,
                      Type->getAdjustedTypeLoc() ? Type->getAdjustedType()
                                                 : Type->getParsedType(),
                      Type->getParsedTypeLoc());
    break;

  case SwiftVersionedSliceAttr::FromRedeclaration: {
    // mergeParamDeclTypes, and for a C function mergeTypes: nullability, where
    // the redeclaration has none.
    const QualType Current = FD ? FD->getReturnType() : Value->getType();
    if (!Nullability || Current->getNullability())
      break;
    const NullabilityKind Kind = getNullabilityKind(Nullability);
    if (FD)
      applyNullabilityToDecl(Ctx, D, Kind);
    else
      Value->setType(Ctx.getAttributedType(Kind, Current, Current));
    break;
  }

  case SwiftVersionedSliceAttr::FromProperty: {
    // The getter returns the property's type and the setter takes it, with
    // qualifiers removed. A null_resettable property, which nullability from
    // API notes makes one, returns nonnull and takes nullable in place of
    // unspecified.
    QualType Current = Method ? Method->getReturnType() : Value->getType();
    if (Type) {
      Current = Method ? Type->getParsedType().getAtomicUnqualifiedType()
                       : Type->getParsedType()
                             .getUnqualifiedType()
                             .getAtomicUnqualifiedType();
    } else {
      NullabilityKind Kind = getNullabilityKind(Nullability);
      if (!Sema::OverrideImplicitNullability(Ctx, Current, Kind,
                                             /*AllowArrayTypes=*/false) &&
          Kind == NullabilityKind::Unspecified &&
          !isIndirectPointerType(Current)) {
        AttributedType::stripOuterNullability(Current);
        Kind = Method ? NullabilityKind::NonNull : NullabilityKind::Nullable;
        Current = Ctx.getAttributedType(Kind, Current, Current);
      }
    }
    if (Method)
      Method->setReturnType(Current);
    else
      Value->setType(Current);
    break;
  }
  }

  return Before.getAsOpaquePtr() != getAPINotedType(D).getAsOpaquePtr();
}

/// Rebuild \p D's attribute list from the slices captured on it, as the
/// default mode would have left it: a source attribute stays in place, a
/// winning slice is applied, and a losing slice stays wrapped. A group
/// received from another declaration contributes its winner alone. An
/// attribute Sema infers after API notes apply is inferred again.
///
/// The list is rebuilt in one walk, in stored order. That is the order the
/// default mode applies slices in, ascending group and then emission order,
/// and the default mode interleaves real attributes and wrappers as it goes,
/// so walking the same way reproduces its attribute order. Rebuilding rather
/// than editing also leaves the captured attributes untouched, so a producer
/// that collapses to precompute something for its importers can put them back
/// afterwards.
///
/// \returns Whether \p D is a parameter whose type changed in a way its
/// function's type has to follow.
static bool replayCapturedSlices(ASTContext &Context, Decl *D,
                                 const SelectedSlices &Selected) {
  bool FunctionTypeFollows = false;

  // The groups D received from another declaration, and how.
  llvm::SmallDenseMap<unsigned, SwiftVersionedSliceAttr::OriginKind, 8>
      ReceivedGroups;
  for (const auto *Marker : D->specific_attrs<SwiftVersionedSliceAttr>())
    if (Marker->getOrigin() != SwiftVersionedSliceAttr::FromOwnLookup)
      ReceivedGroups[Marker->getSliceGroup()] = Marker->getOrigin();

  AttrVec &Rebuilt = D->getAttrs();
  AttrVec Captured;
  std::swap(Captured, Rebuilt);

  std::optional<unsigned> CurrentGroup;
  bool DropInferred = false;
  for (Attr *A : Captured) {
    // The producer's own inference, which the record before it overruled.
    if (std::exchange(DropInferred, false)) {
      assert(A->isImplicit() && "expected the producer's inferred attribute");
      continue;
    }

    // Where Sema inferred an attribute after API notes applied, or declined
    // to, decide again now that they have.
    if (const auto *Inference = dyn_cast<SwiftVersionedInferenceAttr>(A)) {
      const attr::Kind Kind = Inference->getInferredKind();
      const bool Suppressed = isAPINotesInferenceSuppressed(D, Kind);
      if (!Inference->getInferred() && !Suppressed)
        Rebuilt.push_back(createInferredAttr(Context, Kind));
      DropInferred = Inference->getInferred() && Suppressed;
      continue;
    }

    // A marker leads its slice, so a group's first marker is where the default
    // mode runs maybeAttachUnversionedSwiftName, before the group's slices. It
    // does not for a parameter, or for a group received from elsewhere.
    // Markers themselves have done their job, and the default mode has none.
    if (const auto *Marker = dyn_cast<SwiftVersionedSliceAttr>(A)) {
      const unsigned Group = Marker->getSliceGroup();
      if (CurrentGroup != Group && !isa<ParmVarDecl>(D) &&
          !ReceivedGroups.contains(Group)) {
        CurrentGroup = Group;
        maybeAttachUnversionedSwiftName(
            Context, D, Selected.lookup(Group), Group,
            [&](VersionTuple Version) {
              return llvm::any_of(Captured, [&](const Attr *Other) {
                return isCapturedSwiftName(Other, Group, Version);
              });
            });
      }
      continue;
    }

    std::optional<CapturedSlice> Slice = getCapturedSlice(A);
    if (!Slice) {
      Rebuilt.push_back(A);
      continue;
    }
    const unsigned Group = Slice->Group;
    const VersionTuple Version = Slice->Version;
    Attr *Payload = Slice->Payload;
    const attr::Kind Kind = Slice->Kind;

    auto Winner = Selected.find(Group);
    const bool IsWinner = Winner != Selected.end() && Version == Winner->second;
    auto Received = ReceivedGroups.find(Group);
    const auto Origin = Received == ReceivedGroups.end()
                            ? SwiftVersionedSliceAttr::FromOwnLookup
                            : Received->second;

    // A type is no attribute. The winner rewrites the declaration's type, and
    // every other slice leaves nothing, as in the default mode. A C function's
    // redeclaration takes its type from the previous one's, parameters
    // included; a C++ one keeps its own.
    if (isa_and_nonnull<SwiftTypeAttr, SwiftNullabilityAttr>(Payload)) {
      if (IsWinner && applyCapturedType(Context, D, Payload, Origin) &&
          isa<ParmVarDecl>(D))
        FunctionTypeFollows |=
            Origin != SwiftVersionedSliceAttr::FromRedeclaration ||
            !Context.getLangOpts().CPlusPlus;
      continue;
    }

    if (Origin != SwiftVersionedSliceAttr::FromOwnLookup) {
      if (IsWinner)
        applyReceivedWinner(Context, D, Payload, Kind, Origin);
      continue;
    }

    // ProcessAPINotes skips an UnsafeBufferUsage slice, winner or not, once
    // the attribute is live, as it is when a slice applied before won.
    if (isa_and_nonnull<UnsafeBufferUsageAttr>(Payload) &&
        D->hasAttr<UnsafeBufferUsageAttr>())
      continue;

    // A rejected Swift name leaves nothing behind unless it is selected.
    if (!IsWinner && !Payload && Kind == attr::SwiftName)
      continue;

    // A losing versioned slice, or any slice of a group with no winner, stays
    // exactly as captured: the default mode wraps it the same way.
    if (Winner == Selected.end() ||
        (!Version.empty() && Version != Winner->second)) {
      Rebuilt.push_back(A);
      continue;
    }

    // Otherwise this is either the winner, applied as the active slice, or the
    // losing unversioned slice. The default mode records that as replaced, at
    // the winner's version, which is what a compatibility alias is obsoleted
    // at.
    const VersionedInfoMetadata Metadata(
        Winner->second, Group,
        IsWinner ? IsActive_t::Active : IsActive_t::Inactive,
        IsWinner ? IsSubstitution_t::Original : IsSubstitution_t::Replacement);
    handleAPINotedAttributeImpl(Context, D, /*IsAddition=*/Payload != nullptr,
                                Metadata, Kind,
                                [&] { return Payload->clone(Context); });
  }

  if (Rebuilt.empty())
    D->dropAttrs();
  return FunctionTypeFollows;
}

void Sema::CollapseVersionedAPINotes(ASTContext &Context, Decl *D,
                                     VersionTuple Requested,
                                     APINotesCollapseUndo *Undo) {
  // A parameter collapses with its function, which builds its type from the
  // parameters' types.
  if (!D || isa<ParmVarDecl>(D))
    return;

  // Only a capture-mode declaration carries slice markers. The default mode
  // also leaves addition wrappers behind, for the slices that lost, and
  // re-selecting over those would corrupt an already-applied declaration.
  // A parameter can carry markers its function does not: those of groups
  // only its own annotations inherited.
  auto Replay = [&](Decl *D) {
    if (!D->hasAttr<SwiftVersionedSliceAttr>())
      return false;
    if (Undo)
      Undo->save(D);
    replayCapturedSlices(Context, D, selectCapturedSlices(D, Requested));
    return true;
  };
  const bool Replayed = Replay(D);

  // ProcessAPINotes rebuilds a function's type whenever it changes a
  // parameter's.
  bool FunctionTypeFollows = false;
  for (ParmVarDecl *Param : getAPINotedParams(D)) {
    if (!Param->hasAttr<SwiftVersionedSliceAttr>())
      continue;
    if (Undo)
      Undo->save(Param);
    FunctionTypeFollows |= replayCapturedSlices(
        Context, Param, selectCapturedSlices(Param, Requested));
  }

  auto *FD = dyn_cast<FunctionDecl>(D);
  if (!FD || !FunctionTypeFollows)
    return;
  if (Undo && !Replayed)
    Undo->save(FD);
  rebuildFunctionType(Context, FD, std::nullopt);
}

void APINotesCollapseUndo::save(Decl *D) {
  Saved S{};
  S.D = D;
  if (D->hasAttrs())
    S.Attrs = D->getAttrs();
  if (auto *Method = dyn_cast<ObjCMethodDecl>(D)) {
    S.Type = Method->getReturnType();
    S.TypeInfo = Method->getReturnTypeSourceInfo();
    S.Qualifiers = Method->getObjCDeclQualifier();
  } else if (auto *Property = dyn_cast<ObjCPropertyDecl>(D)) {
    S.Type = Property->getType();
    S.TypeInfo = Property->getTypeSourceInfo();
    S.Qualifiers = Property->getPropertyAttributes();
  } else if (auto *Declarator = dyn_cast<DeclaratorDecl>(D)) {
    S.Type = Declarator->getType();
    S.TypeInfo = Declarator->getTypeSourceInfo();
    // Only an Objective-C method's parameter has qualifiers to set.
    if (auto *Param = dyn_cast<ParmVarDecl>(D);
        Param && Param->isObjCMethodParameter())
      S.Qualifiers = Param->getObjCDeclQualifier();
  }
  Decls.push_back(std::move(S));
}

void APINotesCollapseUndo::restore() {
  for (Saved &S : llvm::reverse(Decls)) {
    Decl *D = S.D;
    if (!S.Attrs)
      D->dropAttrs();
    else if (D->hasAttrs())
      D->getAttrs() = std::move(*S.Attrs);
    else
      D->setAttrs(*S.Attrs);
    if (auto *Method = dyn_cast<ObjCMethodDecl>(D)) {
      Method->setReturnType(S.Type);
      Method->setReturnTypeSourceInfo(S.TypeInfo);
      Method->setObjCDeclQualifier(Decl::ObjCDeclQualifier(S.Qualifiers));
    } else if (auto *Property = dyn_cast<ObjCPropertyDecl>(D)) {
      Property->setType(S.Type, S.TypeInfo);
      Property->overwritePropertyAttributes(S.Qualifiers);
    } else if (auto *Declarator = dyn_cast<DeclaratorDecl>(D)) {
      Declarator->setType(S.Type);
      Declarator->setTypeSourceInfo(S.TypeInfo);
      if (auto *Param = dyn_cast<ParmVarDecl>(D);
          Param && Param->isObjCMethodParameter())
        Param->setObjCDeclQualifier(Decl::ObjCDeclQualifier(S.Qualifiers));
    }
  }
  Decls.clear();
}

//===----------------------------------------------------------------------===//
// Redeclarations of a capture-mode declaration
//
// Under -fswift-version-independent-apinotes, API notes are not applied to a
// declaration, so the attributes they would add are not there for a
// redeclaration to inherit. Their slices are, as SwiftVersionedAdditionAttr,
// SwiftVersionedRemovalAttr and SwiftVersionedSliceAttr, but those are plain
// Attr, which attribute inheritance skips. So the redeclaration receives the
// previous declaration's slice groups, marked inherited, and the collapse
// applies each one's winner the way inheritance would have applied the
// attribute it left live.
//===----------------------------------------------------------------------===//

/// Copy an API notes wrapper or slice marker onto another declaration, under a
/// new slice group. A copied marker records how the group got there.
static Attr *
cloneReceivedAPINotesAttr(ASTContext &Ctx, const Attr *A, unsigned Group,
                          SwiftVersionedSliceAttr::OriginKind Origin) {
  if (const auto *Addition = dyn_cast<SwiftVersionedAdditionAttr>(A))
    return SwiftVersionedAdditionAttr::CreateImplicit(
        Ctx, Addition->getVersion(), Addition->getAdditionalAttr()->clone(Ctx),
        Addition->getIsReplacedByActive(), Group);
  if (const auto *Removal = dyn_cast<SwiftVersionedRemovalAttr>(A))
    return SwiftVersionedRemovalAttr::CreateImplicit(
        Ctx, Removal->getVersion(), Removal->getRawKind(),
        Removal->getIsReplacedByActive(), Group);
  return SwiftVersionedSliceAttr::CreateImplicit(
      Ctx, cast<SwiftVersionedSliceAttr>(A)->getVersion(), Group, Origin);
}

bool clang::propagateCapturedAPINotes(
    Sema &S, Decl *New, const Decl *Old,
    SwiftVersionedSliceAttr::OriginKind Origin,
    llvm::function_ref<bool(attr::Kind)> Inherits) {
  // The default mode applies the selected slice as a real attribute, which
  // inheritance already handles, and wraps the losing slices only as
  // per-declaration bookkeeping.
  if (!S.captureSwiftVersionIndependentAPINotes() || !Old->hasAttrs())
    return false;

  // Whether the default mode could inherit what this wrapper stands for. A
  // removal counts: the attribute it takes away is still live on Old, so
  // inheritance copied it to New, and the removal has to follow it there.
  auto WrapsInherited = [&](const Attr *A) {
    std::optional<CapturedSlice> Slice = getCapturedSlice(A);
    return Slice && Inherits(Slice->Kind);
  };

  // A group that wraps nothing inheritable stays behind entirely.
  llvm::SmallDenseSet<unsigned, 8> InheritingGroups;
  for (const auto *A : Old->attrs())
    if (WrapsInherited(A))
      InheritingGroups.insert(*apiNotesSliceGroup(A));
  if (InheritingGroups.empty())
    return false;

  // Old's group numbers are unrelated to New's, so they go above New's,
  // which keeps group order the order the default mode applies things in.
  const unsigned GroupOffset = nextSliceGroup(New);
  for (const auto *A : Old->attrs()) {
    std::optional<unsigned> Group = apiNotesSliceGroup(A);
    if (!Group || !InheritingGroups.contains(*Group))
      continue;
    // Markers travel whole, including those of slices whose own wrappers stay
    // behind. Selection picks the lowest slice at or above the requested
    // version, so dropping a marker can hand the group to a higher slice.
    if (isa<SwiftVersionedSliceAttr>(A) || WrapsInherited(A))
      New->addAttr(cloneReceivedAPINotesAttr(S.Context, A, *Group + GroupOffset,
                                             Origin));
  }
  return true;
}

/// The Swift name \p D has at Swift version \p Version, once its captured
/// slices are applied. \p D is left as it was. Only its own slices can name
/// it, so its parameters stay as they are.
static const SwiftNameAttr *getCapturedSwiftNameAt(ASTContext &Ctx, Decl *D,
                                                   VersionTuple Version) {
  if (!D->hasAttr<SwiftVersionedSliceAttr>())
    return D->getAttr<SwiftNameAttr>();
  APINotesCollapseUndo Undo;
  Undo.save(D);
  replayCapturedSlices(Ctx, D, selectCapturedSlices(D, Version));
  const auto *Name = D->getAttr<SwiftNameAttr>();
  Undo.restore();
  return Name;
}

void clang::diagnoseCapturedSwiftNameConflict(Sema &S, Decl *New, Decl *Old) {
  if (!S.captureSwiftVersionIndependentAPINotes())
    return;
  // Without captured slices on either side, attribute inheritance makes this
  // check itself.
  if (!New->hasAttr<SwiftVersionedSliceAttr>() &&
      !Old->hasAttr<SwiftVersionedSliceAttr>())
    return;
  auto MayBeNamed = [](const Decl *D) {
    return llvm::any_of(D->attrs(), [](const Attr *A) {
      if (const auto *Addition = dyn_cast<SwiftVersionedAdditionAttr>(A))
        return isa<SwiftNameAttr>(Addition->getAdditionalAttr());
      return isa<SwiftNameAttr>(A);
    });
  };
  if (!MayBeNamed(New) || !MayBeNamed(Old))
    return;

  // Selection changes only at a slice's version, so those versions, plus
  // none at all, reach every outcome.
  llvm::SmallVector<VersionTuple, 4> Versions = {VersionTuple()};
  for (const Decl *D : {New, Old})
    for (const auto *Marker : D->specific_attrs<SwiftVersionedSliceAttr>())
      if (!llvm::is_contained(Versions, Marker->getVersion()))
        Versions.push_back(Marker->getVersion());

  for (VersionTuple Version : Versions) {
    const auto *OldName = getCapturedSwiftNameAt(S.Context, Old, Version);
    const auto *NewName = getCapturedSwiftNameAt(S.Context, New, Version);
    // The check mergeNameAttr makes, at one version.
    if (!OldName || !NewName || OldName->getName() == NewName->getName() ||
        NewName->isImplicit())
      continue;
    S.Diag(New->getLocation(), diag::err_attributes_are_not_compatible)
        << NewName << OldName << /*IsRegularKeywordAttribute=*/false;
    S.Diag(Old->getLocation(), diag::note_conflicting_attribute);
    return;
  }
}

void clang::recordCapturedAPINotesInference(Sema &S, Decl *D, attr::Kind Kind,
                                            bool Inferred) {
  if (S.captureSwiftVersionIndependentAPINotes() &&
      D->hasAttr<SwiftVersionedSliceAttr>())
    D->addAttr(
        SwiftVersionedInferenceAttr::CreateImplicit(S.Context, Kind, Inferred));
}
