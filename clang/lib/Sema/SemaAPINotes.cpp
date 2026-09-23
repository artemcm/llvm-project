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
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaSwift.h"
#include "clang/Sema/Template.h"
#include "llvm/Support/SaveAndRestore.h"
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
  /// Whether this is the slice selection picks for this compilation's own
  /// Swift version. Capture mode marks every slice inactive, but diagnoses
  /// this one's type as the default mode would.
  unsigned IsProducerSelected : 1;

  VersionedInfoMetadata(VersionTuple Version, unsigned SliceGroup,
                        IsActive_t Active, IsSubstitution_t Replacement,
                        bool ProducerSelected)
      : Version(Version), SliceGroup(SliceGroup),
        IsActive(Active == IsActive_t::Active),
        IsReplacement(Replacement == IsSubstitution_t::Replacement),
        IsProducerSelected(ProducerSelected) {}
};
} // end anonymous namespace

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

namespace {
/// A replacement type parsed and validated from an API note.
struct ParsedAPINotesType {
  QualType Type;
  TypeSourceInfo *TypeInfo;
};
} // namespace

static std::optional<ParsedAPINotesType>
parseAPINotesType(Sema &S, Decl *D, StringRef TypeString, bool Diagnose = true);

/// Whether \p D's type, or a function's return type, is still to be deduced.
/// The default mode applies API notes to the type as written, and deduction
/// then replaces it with the initializer's or the body's, notes and all. A type
/// already deduced, as on the explicit instantiation of a specialization that
/// an implicit one deduced first, takes them as any other does.
static bool isAPINotedTypeUndeduced(const Decl *D) {
  QualType Type;
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    Type = FD->getReturnType();
  else if (const auto *VD = dyn_cast<ValueDecl>(D))
    Type = VD->getType();
  return !Type.isNull() && Type->isUndeducedType();
}

/// Parse a 'Type:' or 'ResultType:' for capture, and wrap it as a slice.
/// Capture parses every slice's type, where the default mode parses only the
/// selected one, because whoever applies it later may have no parser. Only the
/// slice selected for this compilation's own Swift version is diagnosed as the
/// default mode would. Another slice's type that fails is only warned about:
/// the default mode would never parse it here, and a client that selects it
/// keeps the declaration's type, as the default mode does when it fails.
static void captureAPINotesType(Sema &S, Decl *D, StringRef TypeString,
                                VersionedInfoMetadata Metadata) {
  if (TypeString.empty() || isAPINotedTypeUndeduced(D))
    return;
  std::optional<ParsedAPINotesType> Parsed = parseAPINotesType(
      S, D, TypeString, /*Diagnose=*/Metadata.IsProducerSelected);
  if (!Parsed) {
    if (!Metadata.IsProducerSelected && S.ParseTypeFromStringCallback)
      S.Diag(D->getLocation(), diag::warn_apinotes_unselected_type_ignored)
          << TypeString << !Metadata.Version.empty()
          << Metadata.Version.getAsString();
    return;
  }
  TypeSourceInfo *Adjusted =
      Parsed->Type == Parsed->TypeInfo->getType()
          ? Parsed->TypeInfo
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
    captureAPINotesType(S, decl, typeString, metadata);
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
    if (isAPINotedTypeUndeduced(decl))
      return;
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

static bool isSwiftAvailabilityAttr(const Attr *A) {
  if (const auto *AA = dyn_cast<AvailabilityAttr>(A))
    if (const auto *II = AA->getPlatform())
      return II->isStr("swift");
  return false;
}

static bool isSwiftSafetyAttr(const Attr *A) {
  if (const auto *SA = dyn_cast<SwiftAttrAttr>(A))
    return SA->getAttribute() == "safe" || SA->getAttribute() == "unsafe";
  return false;
}

static bool isRetainCountConventionAttr(const Attr *A) {
  return isa<CFUnknownTransferAttr, CFReturnsRetainedAttr,
             CFReturnsNotRetainedAttr, NSReturnsRetainedAttr,
             NSReturnsNotRetainedAttr, CFAuditedTransferAttr>(A);
}

/// Whether an API note of kind \p Kind displaces the existing attribute \p A.
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

static Decl::attr_iterator findAttrDisplacedBy(const Decl *D, attr::Kind Kind) {
  return llvm::find_if(D->attrs(), [Kind](const Attr *Next) {
    return isDisplacedBy(Next, Kind);
  });
}

/// Handle an attribute introduced by API notes.
///
/// \param IsAddition Whether we should add a new attribute
/// (otherwise, we might remove an existing attribute).
/// \param Kind The attribute kind to add or remove.
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

/// Add a 'swift_attr' unless \p D already carries that exact annotation.
static void addSwiftAttrIfAbsent(Sema &S, Decl *D, StringRef Attribute) {
  for (const auto *A : D->specific_attrs<SwiftAttrAttr>())
    if (A->getAttribute() == Attribute)
      return;

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
                                         QualType ReplacementType,
                                         bool Diagnose = true) {
  if (S.Context.getTypeSize(OrigType) !=
      S.Context.getTypeSize(ReplacementType)) {
    if (Diagnose)
      S.Diag(Loc, diag::err_incompatible_replacement_type)
          << ReplacementType << OrigType;
    return true;
  }

  return false;
}

/// Rebuild \p FD's function type from its current parameter types and
/// \p ResultType.
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

/// Set \p D's type (or return type, for a function or method) to \p Type.
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

/// Parse \p TypeString as the type API notes give \p D.
///
/// \param Diagnose Whether a failure is an error. If not, the parse is silent,
/// and any error it would have reported makes it fail, recovery or not.
static std::optional<ParsedAPINotesType>
parseAPINotesType(Sema &S, Decl *D, StringRef TypeString, bool Diagnose) {
  if (TypeString.empty() || !S.ParseTypeFromStringCallback)
    return std::nullopt;
  TypeResult ParsedType;
  if (Diagnose) {
    ParsedType = S.ParseTypeFromStringCallback(TypeString, "<API Notes>",
                                               D->getLocation());
  } else {
    // The parser reports through the DiagnosticsEngine directly, so a
    // SFINAETrap would not catch its errors. An error trap still counts the
    // suppressed ones.
    DiagnosticsEngine &Diags = S.getDiagnostics();
    DiagnosticErrorTrap Trap(Diags);
    llvm::SaveAndRestore NoTypoCorrection(S.DisableTypoCorrection, true);
    const bool WasSuppressed = Diags.getSuppressAllDiagnostics();
    Diags.setSuppressAllDiagnostics(true);
    ParsedType = S.ParseTypeFromStringCallback(TypeString, "<API Notes>",
                                               D->getLocation());
    Diags.setSuppressAllDiagnostics(WasSuppressed);
    if (Trap.hasErrorOccurred())
      return std::nullopt;
  }
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
  if (checkAPINotesReplacementType(S, D->getLocation(), OrigType, Type,
                                   Diagnose))
    return std::nullopt;
  return ParsedAPINotesType{Type, TypeInfo};
}

void Sema::ApplyAPINotesType(Decl *D, StringRef TypeString) {
  if (auto Parsed = parseAPINotesType(*this, D, TypeString))
    applyTypeToDecl(Context, D, Parsed->Type, Parsed->TypeInfo);
}

static void applyNullabilityToDecl(ASTContext &Context, Decl *D,
                                   NullabilityKind Nullability) {
  auto GetModified = [&](QualType QT) -> std::optional<QualType> {
    QualType Original = QT;
    Sema::OverrideImplicitNullability(Context, QT, Nullability,
                                      isa<ParmVarDecl>(D));
    return (QT.getTypePtr() != Original.getTypePtr()) ? std::optional(QT)
                                                      : std::nullopt;
  };

  if (auto *Function = dyn_cast<FunctionDecl>(D)) {
    if (auto Modified = GetModified(Function->getReturnType()))
      rebuildFunctionType(Context, Function, *Modified);
  } else if (auto *Method = dyn_cast<ObjCMethodDecl>(D)) {
    if (auto Modified = GetModified(Method->getReturnType())) {
      Method->setReturnType(*Modified);

      // Make it a context-sensitive keyword if we can.
      if (!isIndirectPointerType(*Modified))
        Method->setObjCDeclQualifier(Decl::ObjCDeclQualifier(
            Method->getObjCDeclQualifier() | Decl::OBJC_TQ_CSNullability));
    }
  } else if (auto *Value = dyn_cast<ValueDecl>(D)) {
    if (auto Modified = GetModified(Value->getType())) {
      Value->setType(*Modified);

      // Make it a context-sensitive keyword if we can.
      if (auto *Parm = dyn_cast<ParmVarDecl>(D)) {
        if (Parm->isObjCMethodParameter() && !isIndirectPointerType(*Modified))
          Parm->setObjCDeclQualifier(Decl::ObjCDeclQualifier(
              Parm->getObjCDeclQualifier() | Decl::OBJC_TQ_CSNullability));
      }
    }
  } else if (auto *Property = dyn_cast<ObjCPropertyDecl>(D)) {
    if (auto Modified = GetModified(Property->getType())) {
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
      return UnsafeBufferUsageAttr::Create(S.getASTContext(), "",
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
    captureAPINotesType(S, D, Info.ResultType, Metadata);
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
    const api_notes::APINotesReader::VersionedInfo<SpecificInfo> Info,
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
    const bool ProducerSelected = i == Selected;

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

    ProcessAPINotes(S, D, InfoSlice,
                    VersionedInfoMetadata(Version, SliceGroup, Active,
                                          Replacement, ProducerSelected));
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
/// broad lookup beside it even though both read the same reader.
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
  // A parameter's notes come from its function's entry.
  if (isa<ParmVarDecl>(D))
    return;
  if (!APINotes.hasAPINotes())
    return;
  auto Readers = APINotes.findAPINotes(D->getLocation());
  if (Readers.empty())
    return;
  // Each lookup below is its own slice group, numbered in the order the
  // lookups run, which is the order Sema applies them in. A consumer needs that
  // order to resolve two groups whose winners set the same key.
  //
  // Numbering starts one above any group D, or one of its parameters, already
  // carries: a lookup's markers go on D's parameters too, and an implicit
  // setter's parameter has groups from the property.
  unsigned NextSliceGroup = nextSliceGroup(D);
  for (ParmVarDecl *Param : getAPINotedParams(D))
    NextSliceGroup = std::max(NextSliceGroup, nextSliceGroup(Param));

  auto *DC = D->getDeclContext();
  // Globals.
  if (DC->isFileContext() || DC->isNamespace() ||
      DC->getDeclKind() == Decl::LinkageSpec) {
    std::optional<api_notes::Context> APINotesContext =
        UnwindNamespaceContext(DC, APINotes);
    // Global variables.
    if (auto VD = dyn_cast<VarDecl>(D)) {
      for (auto Reader : Readers) {
        auto Info =
            Reader->lookupGlobalVariable(VD->getName(), APINotesContext);
        ProcessVersionedAPINotes(*this, VD, Info, NextSliceGroup++);
      }

      return;
    }

    // Global functions.
    if (auto FD = dyn_cast<FunctionDecl>(D)) {
      if (FD->getDeclName().isIdentifier()) {
        auto ParameterSelectorCandidates =
            getAPINotesParameterSelectorCandidates(*this, FD);

        for (auto Reader : Readers) {
          auto Info =
              Reader->lookupGlobalFunction(FD->getName(), APINotesContext);
          ProcessVersionedAPINotes(*this, FD, Info, NextSliceGroup++);

          if (ParameterSelectorCandidates)
            processExactAPINotes<api_notes::GlobalFunctionInfo>(
                *this, FD, *ParameterSelectorCandidates,
                [&](ArrayRef<std::string> Parameters) {
                  return Reader->lookupGlobalFunction(FD->getName(), Parameters,
                                                      APINotesContext);
                },
                NextSliceGroup++);

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
      for (auto Reader : Readers) {
        auto Info = Reader->lookupObjCClassInfo(Class->getName());
        ProcessVersionedAPINotes(*this, Class, Info, NextSliceGroup++);
      }

      return;
    }

    // Objective-C protocols.
    if (auto Protocol = dyn_cast<ObjCProtocolDecl>(D)) {
      for (auto Reader : Readers) {
        auto Info = Reader->lookupObjCProtocolInfo(Protocol->getName());
        ProcessVersionedAPINotes(*this, Protocol, Info, NextSliceGroup++);
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

      for (auto Reader : Readers) {
        if (auto ParentTag = dyn_cast<TagDecl>(Tag->getDeclContext()))
          APINotesContext = UnwindTagContext(ParentTag, APINotes);
        auto Info = Reader->lookupTag(LookupName, APINotesContext);
        ProcessVersionedAPINotes(*this, Tag, Info, NextSliceGroup++);
      }

      return;
    }

    // Typedefs
    if (auto Typedef = dyn_cast<TypedefNameDecl>(D)) {
      for (auto Reader : Readers) {
        auto Info = Reader->lookupTypedef(Typedef->getName(), APINotesContext);
        ProcessVersionedAPINotes(*this, Typedef, Info, NextSliceGroup++);
      }

      return;
    }
  }

  // Enumerators.
  if (DC->getRedeclContext()->isFileContext() ||
      DC->getRedeclContext()->isExternCContext()) {
    if (auto EnumConstant = dyn_cast<EnumConstantDecl>(D)) {
      for (auto Reader : Readers) {
        auto Info = Reader->lookupEnumConstant(EnumConstant->getName());
        ProcessVersionedAPINotes(*this, EnumConstant, Info, NextSliceGroup++);
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
      for (auto Reader : Readers) {
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
          ProcessVersionedAPINotes(*this, Method, Info, NextSliceGroup++);
        }
      }
    }

    // Objective-C properties.
    if (auto Property = dyn_cast<ObjCPropertyDecl>(D)) {
      for (auto Reader : APINotes.findAPINotes(D->getLocation())) {
        if (auto Context = GetContext(Reader)) {
          bool isInstanceProperty =
              (Property->getPropertyAttributesAsWritten() &
               ObjCPropertyAttribute::kind_class) == 0;
          auto Info = Reader->lookupObjCProperty(*Context, Property->getName(),
                                                 isInstanceProperty);
          ProcessVersionedAPINotes(*this, Property, Info, NextSliceGroup++);
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
        for (auto Reader : Readers) {
          if (auto Context = UnwindTagContext(TagContext, APINotes)) {
            std::string MethodName;
            if (CXXMethod->isOverloadedOperator())
              MethodName =
                  std::string("operator") +
                  getOperatorSpelling(CXXMethod->getOverloadedOperator());
            else
              MethodName = CXXMethod->getName();

            auto Info = Reader->lookupCXXMethod(Context->id, MethodName);
            ProcessVersionedAPINotes(*this, CXXMethod, Info, NextSliceGroup++);

            if (ParameterSelectorCandidates)
              processExactAPINotes<api_notes::CXXMethodInfo>(
                  *this, CXXMethod, *ParameterSelectorCandidates,
                  [&](ArrayRef<std::string> Parameters) {
                    return Reader->lookupCXXMethod(Context->id, MethodName,
                                                   Parameters);
                  },
                  NextSliceGroup++);

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
        for (auto Reader : Readers) {
          if (auto Context = UnwindTagContext(TagContext, APINotes)) {
            auto Info = Reader->lookupField(Context->id, Field->getName());
            ProcessVersionedAPINotes(*this, Field, Info, NextSliceGroup++);
          }
        }
      }
    }

    if (auto Tag = dyn_cast<TagDecl>(D)) {
      for (auto Reader : Readers) {
        if (auto Context = UnwindTagContext(TagContext, APINotes)) {
          auto Info = Reader->lookupTag(Tag->getName(), Context);
          ProcessVersionedAPINotes(*this, Tag, Info, NextSliceGroup++);
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
/// constructor makes it: see api_notes::isSliceSelectable.
static SelectedSlices selectCapturedSlices(const Decl *D,
                                           VersionTuple Requested) {
  SelectedSlices Selected;
  for (const auto *Marker : D->specific_attrs<SwiftVersionedSliceAttr>()) {
    const VersionTuple Version = Marker->getVersion();
    const bool Matches = api_notes::isSliceSelectable(Version, Requested);
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

/// Whether Sema infers attributes of kind \p Kind only once API notes have
/// applied: ns_returns_retained for a method of an ARC method family, and
/// ns_consumes_self for an init method, both of which depend on the return
/// type, and cf_audited_transfer inside an audited region.
static bool isAPINotesInferredKind(attr::Kind Kind) {
  return Kind == attr::NSReturnsRetained || Kind == attr::NSConsumesSelf ||
         Kind == attr::CFAuditedTransfer;
}

/// Whether \p Attrs suppress Sema's inference of an attribute of kind \p Kind,
/// for a declaration that is an init method if \p IsInit.
template <typename RangeT>
static bool suppressesInference(RangeT &&Attrs, attr::Kind Kind, bool IsInit) {
  auto Has = [&](std::initializer_list<attr::Kind> Kinds) {
    return llvm::any_of(Attrs, [&](const Attr *A) {
      return llvm::is_contained(Kinds, A->getKind());
    });
  };
  assert(isAPINotesInferredKind(Kind) && "not inferred after API notes");
  if (Kind == attr::CFAuditedTransfer)
    return Has({attr::CFAuditedTransfer, attr::CFUnknownTransfer});
  // Nothing suppresses ns_consumes_self, not even a second copy.
  if (Kind == attr::NSConsumesSelf)
    return false;
  // An init method only avoids a second copy.
  if (IsInit)
    return Has({attr::NSReturnsRetained});
  return Has({attr::NSReturnsRetained, attr::NSReturnsNotRetained,
              attr::NSReturnsAutoreleased});
}

static bool isInitMethod(const Decl *D) {
  const auto *Method = dyn_cast<ObjCMethodDecl>(D);
  return Method && Method->getMethodFamily() == OMF_init;
}

/// Whether Sema would consider inferring an attribute of kind \p Kind on \p D
/// at all, attributes aside. A method's family decides that for the ARC
/// conventions, and the family depends on the return type, which API notes
/// can rewrite.
static bool isAPINotesInferenceApplicable(const Decl *D, attr::Kind Kind) {
  const auto *Method = dyn_cast<ObjCMethodDecl>(D);
  if (!Method || Kind == attr::CFAuditedTransfer)
    return true;
  switch (Method->getMethodFamily()) {
  case OMF_init:
    return true;
  case OMF_alloc:
  case OMF_copy:
  case OMF_mutableCopy:
  case OMF_new:
    return Kind == attr::NSReturnsRetained;
  default:
    return false;
  }
}

/// Apply the winner of a slice group that \p D, a property's implicit accessor,
/// received from its property, as AddPropertyAttrs copies the attribute that
/// winner leaves live there.
///
/// On the property the slice displaced an attribute, so the default mode
/// never copies that one, but D received it all the same. The copy goes: the
/// first attribute the slice displaces.
///
/// \param Payload The winner's attribute, or null for a removal.
/// \param Take What D gets of \p Payload: it, or a copy.
static void applyReceivedWinner(Decl *D, Attr *Payload, attr::Kind Kind,
                                llvm::function_ref<Attr *(Attr *)> Take) {
  if (auto Displaced = findAttrDisplacedBy(D, Kind); Displaced != D->attr_end())
    D->getAttrs().erase(Displaced);
  if (Payload)
    D->addAttr(Take(Payload));
}

/// Whether \p D's attributes suppress an inference Sema makes after API notes
/// apply, of an attribute of kind \p Kind. Sema asks before it infers, and the
/// collapse of captured API notes asks again once it has applied the slices.
static bool isAPINotesInferenceSuppressed(const Decl *D, attr::Kind Kind) {
  return suppressesInference(D->attrs(), Kind, isInitMethod(D));
}

/// Infer the attribute \p Inference records, as Sema would have.
static Attr *createInferredAttr(ASTContext &Ctx,
                                const SwiftVersionedInferenceAttr *Inference) {
  const attr::Kind Kind = Inference->getInferredKind();
  assert(isAPINotesInferredKind(Kind) && "not inferred after API notes");
  if (Kind == attr::NSReturnsRetained)
    return NSReturnsRetainedAttr::CreateImplicit(Ctx);
  if (Kind == attr::NSConsumesSelf)
    return NSConsumesSelfAttr::CreateImplicit(Ctx);
  // Sema creates this one with the audited region's pragma, which the record
  // was created with too.
  return CFAuditedTransferAttr::CreateImplicit(Ctx, *Inference);
}

/// The type API notes rewrite on \p D: a method's return type, or its own.
static QualType getAPINotedType(const Decl *D) {
  if (const auto *Method = dyn_cast<ObjCMethodDecl>(D))
    return Method->getReturnType();
  if (const auto *Property = dyn_cast<ObjCPropertyDecl>(D))
    return Property->getType();
  return cast<ValueDecl>(D)->getType();
}

/// What replaying a declaration's slices did that the rest of its collapse
/// depends on.
struct ReplayResult {
  /// For a parameter, whether the function's type follows its new type.
  bool TypeFollows = false;
};

/// How replayCapturedSlices treats a declaration.
struct ReplayOptions {
  /// Whether the captured attributes have to stay intact, for an undo to put
  /// back. If not, a winner's attribute moves to D instead of being copied.
  bool KeepsCaptured = true;
};

/// Take off an ARC ownership Sema inferred for \p D, which its type as written
/// lacks.
static void dropInferredObjCLifetime(ASTContext &Ctx, DeclaratorDecl *D) {
  const QualType T = D->getType();
  Qualifiers Local = T.getLocalQualifiers();
  if (!Local.hasObjCLifetime() || !D->getTypeSourceInfo() ||
      D->getTypeSourceInfo()->getType().getObjCLifetime() !=
          Qualifiers::OCL_None)
    return;
  Local.removeObjCLifetime();
  D->setType(Ctx.getQualifiedType(T.getLocalUnqualifiedType(), Local));
}

/// Infer \p D's ARC ownership where it has none, as
/// SemaObjC::inferObjCARCLifetime does.
static void inferObjCLifetime(ASTContext &Ctx, ValueDecl *D) {
  const QualType T = D->getType();
  if (T.getObjCLifetime() == Qualifiers::OCL_None && T->isObjCLifetimeType())
    D->setType(
        Ctx.getLifetimeQualifiedType(T, T->getObjCARCImplicitLifetime()));
}

/// Apply the winning slice's 'Type:', 'ResultType:' or nullability to \p D,
/// as ProcessAPINotes applies its own slices'.
///
/// \returns Whether \p D's type changed.
static bool applyCapturedType(ASTContext &Ctx, Decl *D, const Attr *Payload) {
  const auto *Type = dyn_cast<SwiftTypeAttr>(Payload);
  const auto *Nullability = dyn_cast<SwiftNullabilityAttr>(Payload);
  const QualType Before = getAPINotedType(D);

  // Under ARC, Sema infers a variable's or field's ownership once API notes
  // have applied. The producer inferred it for the type as written, so it
  // comes off first, and is inferred again after.
  const bool InfersOwnership = Ctx.getLangOpts().ObjCAutoRefCount &&
                               isa<VarDecl, FieldDecl>(D) &&
                               !isa<ParmVarDecl>(D);
  if (InfersOwnership)
    dropInferredObjCLifetime(Ctx, cast<DeclaratorDecl>(D));
  if (Nullability)
    applyNullabilityToDecl(Ctx, D, getNullabilityKind(Nullability));
  else
    applyTypeToDecl(Ctx, D, Type->getAdjustedType(), Type->getParsedTypeLoc());
  if (InfersOwnership)
    inferObjCLifetime(Ctx, cast<ValueDecl>(D));

  const bool Changed =
      Before.getAsOpaquePtr() != getAPINotedType(D).getAsOpaquePtr();
  // A method's family depends on its return type.
  if (auto *Method = dyn_cast<ObjCMethodDecl>(D); Method && Changed)
    Method->resetMethodFamily();
  return Changed;
}

namespace {
/// The type of the property a property's implicit accessor takes its type
/// from, as the winners of the type slices it received from the property
/// rewrite it, and whether they made the property null_resettable.
struct ReceivedPropertyType {
  QualType Type;
  bool NullResettable;
  /// Whether a winner rewrote anything since the accessor's type was last set.
  bool Changed = false;
};
} // namespace

/// Apply a winning 'Type:' or nullability that a property's implicit accessor
/// received from its property to \p Property, as ProcessAPINotes applies it
/// to the property.
static void applyReceivedPropertyType(ASTContext &Ctx,
                                      ReceivedPropertyType &Property,
                                      const Attr *Payload) {
  if (const auto *Type = dyn_cast<SwiftTypeAttr>(Payload)) {
    Property.Type = Type->getAdjustedType();
    Property.Changed = true;
    return;
  }
  // As applyNullabilityToDecl does for a property: a nullability that
  // changes the type also makes the property null_resettable, if it can be.
  QualType Modified = Property.Type;
  Sema::OverrideImplicitNullability(
      Ctx, Modified, getNullabilityKind(cast<SwiftNullabilityAttr>(Payload)),
      /*AllowArrayTypes=*/false);
  if (Modified.getTypePtr() == Property.Type.getTypePtr())
    return;
  Property.Type = Modified;
  if (!isIndirectPointerType(Modified))
    Property.NullResettable = true;
  Property.Changed = true;
}

/// Set the type of \p D, a property's implicit getter or its setter's
/// parameter, to \p Type.
///
/// \returns Whether \p D's type changed.
static bool setImplicitAccessorType(Decl *D, QualType Type) {
  if (auto *Method = dyn_cast<ObjCMethodDecl>(D)) {
    if (Method->getReturnType() == Type)
      return false;
    Method->setReturnType(Type);
    // A method's family depends on its return type.
    Method->resetMethodFamily();
    return true;
  }
  auto *Param = cast<ParmVarDecl>(D);
  if (Param->getType() == Type)
    return false;
  Param->setType(Type);
  return true;
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
/// than editing also leaves the captured attributes untouched when
/// ReplayOptions::KeepsCaptured says to, so a producer that collapses to
/// precompute something for its importers can put them back afterwards.
static ReplayResult replayCapturedSlices(ASTContext &Context, Decl *D,
                                         const SelectedSlices &Selected,
                                         ReplayOptions Options) {
  ReplayResult Result;
  auto Take = [&](Attr *Payload) {
    return Options.KeepsCaptured ? Payload->clone(Context) : Payload;
  };

  // The groups D received from another declaration, and how.
  llvm::SmallDenseMap<unsigned, SwiftVersionedSliceAttr::OriginKind, 8>
      ReceivedGroups;
  for (const auto *Marker : D->specific_attrs<SwiftVersionedSliceAttr>())
    if (Marker->getOrigin() != SwiftVersionedSliceAttr::FromOwnLookup)
      ReceivedGroups[Marker->getSliceGroup()] = Marker->getOrigin();

  // A property's implicit accessor takes its type from the property once the
  // property's API notes have applied, and before its own do. The winners it
  // received from the property rewrite the property's type as written, and
  // the accessor's type is derived from the result before anything else sees
  // it: the accessor's own type slices, or an inference that depends on it.
  std::optional<ReceivedPropertyType> Property;
  if (const auto *Written = D->getAttr<SwiftVersionedPropertyTypeAttr>())
    Property = ReceivedPropertyType{Written->getPropertyType(),
                                    Written->getNullResettable()};
  auto SettleAccessorType = [&] {
    if (!Property || !std::exchange(Property->Changed, false))
      return;
    Result.TypeFollows |= setImplicitAccessorType(
        D, getImplicitAccessorType(Context, Property->Type,
                                   Property->NullResettable,
                                   /*IsGetter=*/isa<ObjCMethodDecl>(D)));
  };

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

    // The property's type as written has done its job by now too.
    if (isa<SwiftVersionedPropertyTypeAttr>(A))
      continue;

    // Where Sema inferred an attribute after API notes applied, or declined
    // to, decide again now that they have.
    if (const auto *Inference = dyn_cast<SwiftVersionedInferenceAttr>(A)) {
      SettleAccessorType();
      const attr::Kind Kind = Inference->getInferredKind();
      const bool Wanted = isAPINotesInferenceApplicable(D, Kind) &&
                          !isAPINotesInferenceSuppressed(D, Kind);
      if (!Inference->getInferred() && Wanted)
        Rebuilt.push_back(createInferredAttr(Context, Inference));
      DropInferred = Inference->getInferred() && !Wanted;
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
    // every other slice leaves nothing, as in the default mode.
    if (isa_and_nonnull<SwiftTypeAttr, SwiftNullabilityAttr>(Payload)) {
      if (!IsWinner)
        continue;
      if (Origin == SwiftVersionedSliceAttr::FromProperty) {
        assert(Property && "a property's type slice without its type");
        if (Property)
          applyReceivedPropertyType(Context, *Property, Payload);
        continue;
      }
      SettleAccessorType();
      Result.TypeFollows |= applyCapturedType(Context, D, Payload);
      continue;
    }

    // What a declaration receives is an accessor's, from its property.
    if (Origin == SwiftVersionedSliceAttr::FromProperty) {
      if (IsWinner)
        applyReceivedWinner(D, Payload, Kind, Take);
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
        IsWinner ? IsSubstitution_t::Original : IsSubstitution_t::Replacement,
        /*ProducerSelected=*/false);
    handleAPINotedAttributeImpl(Context, D, /*IsAddition=*/Payload != nullptr,
                                Metadata, Kind, [&] { return Take(Payload); });
  }
  SettleAccessorType();

  if (Rebuilt.empty())
    D->dropAttrs();
  return Result;
}

bool Sema::captureSwiftVersionIndependentAPINotes() {
  return getLangOpts().capturesVersionIndependentAPINotes(
      PP.getPreprocessorOpts().GeneratePreamble);
}

void Sema::CollapseVersionedAPINotes(ASTContext &Context, Decl *D,
                                     VersionTuple Requested,
                                     APINotesCollapseUndo *Undo) {
  // A parameter collapses with its function, which builds its type from the
  // parameters' types.
  if (!D || isa<ParmVarDecl>(D))
    return;
  auto *FD = dyn_cast<FunctionDecl>(D);

  // Only a capture-mode declaration carries slice markers. The default mode
  // also leaves addition wrappers behind, for the slices that lost, and
  // re-selecting over those would corrupt an already-applied declaration. A
  // parameter can carry markers its function does not: an implicit setter's
  // parameter receives its property's type.
  auto Replay = [&](Decl *D) -> ReplayResult {
    if (!D->hasAttr<SwiftVersionedSliceAttr>())
      return ReplayResult();
    if (Undo)
      Undo->save(D);
    return replayCapturedSlices(
        Context, D, selectCapturedSlices(D, Requested),
        ReplayOptions{/*KeepsCaptured=*/Undo != nullptr});
  };
  const bool Replayed = D->hasAttr<SwiftVersionedSliceAttr>();
  Replay(D);

  // ProcessAPINotes rebuilds a function's type whenever it changes a
  // parameter's.
  bool TypeChanges = false;
  for (ParmVarDecl *Param : getAPINotedParams(D))
    TypeChanges |= Replay(Param).TypeFollows;
  if (FD && TypeChanges) {
    if (Undo && !Replayed)
      Undo->save(FD);
    rebuildFunctionType(Context, FD, std::nullopt);
  }
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
      Method->resetMethodFamily();
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
// A property's accessors
//
// Under -fswift-version-independent-apinotes, API notes are not applied to a
// property, so AddPropertyAttrs would copy its attributes onto its implicit
// accessors as written. An accessor receives the property's slice groups
// instead, and the collapse applies each one's winner as the copy it would
// have got.
//===----------------------------------------------------------------------===//

/// Copy an API notes wrapper or slice marker onto a property's accessor, under
/// a new slice group, with a marker that says where the group came from.
static Attr *cloneReceivedAPINotesAttr(ASTContext &Ctx, const Attr *A,
                                       unsigned Group) {
  if (const auto *Addition = dyn_cast<SwiftVersionedAdditionAttr>(A))
    return SwiftVersionedAdditionAttr::CreateImplicit(
        Ctx, Addition->getVersion(), Addition->getAdditionalAttr()->clone(Ctx),
        Addition->getIsReplacedByActive(), Group);
  if (const auto *Removal = dyn_cast<SwiftVersionedRemovalAttr>(A))
    return SwiftVersionedRemovalAttr::CreateImplicit(
        Ctx, Removal->getVersion(), Removal->getRawKind(),
        Removal->getIsReplacedByActive(), Group);
  return SwiftVersionedSliceAttr::CreateImplicit(
      Ctx, cast<SwiftVersionedSliceAttr>(A)->getVersion(), Group,
      SwiftVersionedSliceAttr::FromProperty);
}

bool clang::propagateCapturedAPINotes(
    Sema &S, Decl *Accessor, const Decl *Property,
    llvm::function_ref<bool(attr::Kind)> Copies) {
  // The default mode applies the selected slice as a real attribute, which
  // AddPropertyAttrs copies, and wraps the losing slices only as
  // per-declaration bookkeeping.
  if (!S.captureSwiftVersionIndependentAPINotes() || !Property->hasAttrs())
    return false;

  // Only groups with markers travel: a module built without the flag wraps
  // the slices that lost, as bookkeeping of its own, which is never copied.
  llvm::SmallDenseSet<unsigned, 8> MarkedGroups;
  for (const auto *Marker : Property->specific_attrs<SwiftVersionedSliceAttr>())
    MarkedGroups.insert(Marker->getSliceGroup());

  // A group travels whole when it wraps an annotation the accessor would get.
  // A removal counts: the attribute it takes away is still live on the
  // property, so the accessor got a copy, and the removal has to follow it.
  llvm::SmallDenseSet<unsigned, 8> Groups;
  for (const auto *A : Property->attrs())
    if (std::optional<CapturedSlice> Slice = getCapturedSlice(A);
        Slice && MarkedGroups.contains(Slice->Group) && Copies(Slice->Kind))
      Groups.insert(Slice->Group);
  if (Groups.empty())
    return false;

  // The property's group numbers are unrelated to the accessor's, so they go
  // above the accessor's, which keeps group order the order the default mode
  // applies things in.
  const unsigned GroupOffset = nextSliceGroup(Accessor);
  bool CopiesType = false;
  for (const auto *A : Property->attrs()) {
    std::optional<unsigned> Group = apiNotesSliceGroup(A);
    if (!Group || !Groups.contains(*Group))
      continue;
    // Markers travel whole, including those of slices whose own wrappers stay
    // behind. Selection picks the lowest slice at or above the requested
    // version, so dropping a marker can hand the group to a higher slice.
    std::optional<CapturedSlice> Slice = getCapturedSlice(A);
    if (!Slice || Copies(Slice->Kind)) {
      Accessor->addAttr(
          cloneReceivedAPINotesAttr(S.Context, A, *Group + GroupOffset));
      CopiesType |= Slice && (Slice->Kind == attr::SwiftType ||
                              Slice->Kind == attr::SwiftNullability);
    }
  }

  // The accessor's type derives from the property's, so the collapse derives
  // it again from the property's as written once the winners apply.
  if (const auto *Prop = dyn_cast<ObjCPropertyDecl>(Property);
      Prop && CopiesType)
    Accessor->addAttr(SwiftVersionedPropertyTypeAttr::CreateImplicit(
        S.Context,
        S.Context.getTrivialTypeSourceInfo(Prop->getType(),
                                           Prop->getLocation()),
        Prop->getPropertyAttributes() &
            ObjCPropertyAttribute::kind_null_resettable));
  return true;
}

bool clang::instantiateCapturedAPINotesType(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs, const Attr *A,
    Decl *New) {
  const auto *Addition = dyn_cast<SwiftVersionedAdditionAttr>(A);
  const Attr *Payload = Addition ? Addition->getAdditionalAttr() : nullptr;
  if (!isa_and_nonnull<SwiftTypeAttr, SwiftNullabilityAttr>(Payload))
    return false;

  // An instantiation takes its types from the pattern's types as written.
  // Nullability, and a function's 'ResultType:', replace only the pattern's
  // types, so they don't reach it; any other 'Type:' replaces those as
  // written too, so the instantiation takes it substituted.
  const auto *Type = dyn_cast<SwiftTypeAttr>(Payload);
  if (!Type || isa<FunctionDecl>(New))
    return true;
  auto Substitute = [&](TypeSourceInfo *TypeInfo) {
    return S.SubstType(TypeInfo, TemplateArgs, New->getLocation(),
                       cast<NamedDecl>(New)->getDeclName());
  };
  TypeSourceInfo *Parsed = Substitute(Type->getParsedTypeLoc());
  TypeSourceInfo *Adjusted =
      Type->getAdjustedTypeLoc() == Type->getParsedTypeLoc()
          ? Parsed
          : Substitute(Type->getAdjustedTypeLoc());
  if (!Parsed || !Adjusted)
    return true;
  New->addAttr(SwiftVersionedAdditionAttr::CreateImplicit(
      S.Context, Addition->getVersion(),
      SwiftTypeAttr::CreateImplicit(S.Context, Type->getTypeString(), Parsed,
                                    Adjusted),
      Addition->getIsReplacedByActive(), Addition->getSliceGroup()));
  return true;
}

bool clang::inferAfterAPINotes(Sema &S, Decl *D, attr::Kind Kind,
                               bool Applicable,
                               const AttributeCommonInfo *Info) {
  const bool Inferred = Applicable && !isAPINotesInferenceSuppressed(D, Kind);
  if (S.captureSwiftVersionIndependentAPINotes() &&
      D->hasAttr<SwiftVersionedSliceAttr>())
    D->addAttr(Info ? SwiftVersionedInferenceAttr::CreateImplicit(
                          S.Context, Kind, Inferred, *Info)
                    : SwiftVersionedInferenceAttr::CreateImplicit(
                          S.Context, Kind, Inferred));
  return Inferred;
}
