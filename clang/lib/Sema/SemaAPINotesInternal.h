//===--- SemaAPINotesInternal.h - API Notes Sema Internals ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_SEMA_SEMAAPINOTESINTERNAL_H
#define LLVM_CLANG_LIB_SEMA_SEMAAPINOTESINTERNAL_H

#include "clang/APINotes/Types.h"
#include "clang/AST/Attr.h"
#include "clang/Basic/AttrKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <utility>

namespace clang {
class Decl;
class MultiLevelTemplateArgumentList;
class ParmVarDecl;
class Sema;
struct APINotesParameterSelectorCandidates;
namespace api_notes {
class APINotesReader;
} // namespace api_notes

/// Source name and location for a declaration seen by Sema.
struct APINotesSelectorDiagnosticName {
  SourceLocation Loc;
  std::string Name;
};

/// Tracks exact Where.Parameters selectors from one API notes reader.
///
/// Sema marks selectors as used when a visible declaration matches them. It
/// also records broad/name-only declarations seen in the translation unit, so
/// end-of-TU diagnostics can warn about exact selectors for known names that
/// were never matched.
struct APINotesSelectorDiagnosticReaderState {
  /// Exact Where.Parameters selector keys stored by API notes. The bool is
  /// true once Sema sees a declaration matching the exact selector.
  llvm::DenseMap<api_notes::APINotesFunctionSelectorKey, bool> SelectorUsed;

  /// Maps broad/name-only keys to a declaration location/name used for
  /// diagnostics.
  llvm::DenseMap<api_notes::APINotesFunctionSelectorKey,
                 APINotesSelectorDiagnosticName>
      SeenNames;

  void addSelector(const api_notes::APINotesFunctionSelectorKey &Key) {
    SelectorUsed.try_emplace(Key, false);
  }

  void addSelectors(
      llvm::ArrayRef<api_notes::APINotesFunctionSelectorKey> Selectors) {
    SelectorUsed.reserve(Selectors.size());
    SeenNames.reserve(Selectors.size());
    for (const auto &Selector : Selectors)
      addSelector(Selector);
  }

  void noteSeenDeclaration(const api_notes::APINotesFunctionSelectorKey &Key,
                           llvm::StringRef Name, SourceLocation Loc) {
    SeenNames.insert({Key.getWithoutParameterSelector(), {Loc, Name.str()}});
  }

  void markUsed(const api_notes::APINotesFunctionSelectorKey &Key) {
    auto KnownSelector = SelectorUsed.find(Key);
    if (KnownSelector != SelectorUsed.end())
      KnownSelector->second = true;
  }

  void markCandidatesUsed(
      llvm::function_ref<std::optional<api_notes::APINotesFunctionSelectorKey>(
          llvm::ArrayRef<std::string>)>
          GetSelectorKey,
      const APINotesParameterSelectorCandidates &Candidates);

  void diagnoseUnused(Sema &S, api_notes::APINotesReader &Reader) const;
};

/// Selector diagnostic state for all API notes readers used by one Sema.
struct APINotesSelectorDiagnosticState {
  llvm::DenseMap<api_notes::APINotesReader *,
                 APINotesSelectorDiagnosticReaderState>
      Readers;

  APINotesSelectorDiagnosticReaderState &
  getOrCreateReaderState(api_notes::APINotesReader &Reader);

  void diagnoseUnused(Sema &S) const;
};

/// Whether Sema should infer an attribute of kind \p Kind on \p D, now that
/// API notes have applied: not if D's attributes suppress it. Under
/// -fswift-version-independent-apinotes this also records the decision, which
/// the collapse makes again once it has applied the slices. Call it just
/// before adding the inferred attribute.
bool inferAfterAPINotes(Sema &S, Decl *D, attr::Kind Kind);

/// Instantiate \p A, if it is an API notes slice of a type or nullability
/// captured on a template pattern, onto the pattern's instantiation \p New, as
/// the default mode's instantiation would take what it stands for.
///
/// \returns Whether \p A was such a slice. Anything else instantiates as
/// usual.
bool instantiateCapturedAPINotesType(
    Sema &S, const MultiLevelTemplateArgumentList &TemplateArgs, const Attr *A,
    Decl *New);

} // namespace clang

#endif // LLVM_CLANG_LIB_SEMA_SEMAAPINOTESINTERNAL_H
