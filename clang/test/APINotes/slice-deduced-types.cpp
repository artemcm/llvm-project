// A deduced type is read after the declaration that has it: the reader defers
// it until the declaration is loaded, since it can name something declared
// inside that declaration. The collapse waits for it, then does what the
// default mode did. That applies API notes to a declaration as written, before
// deduction replaces its type, so notes on a deduced type, or a deduced return
// type, leave nothing, and notes on a parameter shape the function's type.
//
//   dcAuto, dcAutoPtr     'Nullability:' on __auto_type globals.
//   dxAuto, dxAutoPtr     ...and on auto ones.
//   dxFloat               'Type:' on a parameter of a function returning auto,
//                         which its callers must pass a float to...
//   dxNull                ...and 'Nullability:' on one, at two versions.
//   dxRet, dxRetPtr       'NullabilityOfRet:' on auto and auto * returns, which
//                         also audits the parameters.
//
// A type already deduced when the notes apply takes them as any other does.
// The explicit instantiation of a specialization that an implicit one
// deduced first is processed again, and the collapse has to run before the
// reader propagates the deduced return type along the redeclaration chain.
//
//   dxVarT                'Nullability:' on such a variable template...
//   dxFnT                 ...and 'NullabilityOfRet:' on a function template.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fapinotes-swift-version=4 -fmodules-cache-path=%t/c-default4 -ast-dump -ast-dump-filter dc -x c | FileCheck --check-prefix=C %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/c-capture4 -ast-dump -ast-dump-filter dc -x c | FileCheck --check-prefix=C --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftNullabilityAttr %s

// RUN: %clang_cc1 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump -ast-dump-filter dx -x c++ | FileCheck --check-prefixes=CXX,CXX4 %s
// RUN: %clang_cc1 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump -ast-dump-filter dx -x c++ | FileCheck --check-prefixes=CXX,CXX4 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftTypeAttr --implicit-check-not=SwiftNullabilityAttr %s
// RUN: %clang_cc1 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump -ast-dump-filter dx -x c++ | FileCheck --check-prefixes=CXX,CXX5 %s
// RUN: %clang_cc1 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump -ast-dump-filter dx -x c++ | FileCheck --check-prefixes=CXX,CXX5 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftTypeAttr --implicit-check-not=SwiftNullabilityAttr %s

// RUN: %clang_cc1 -triple arm64-apple-macosx14 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fapinotes-swift-version=5 -fmodules-cache-path=%t/ir-default5 -emit-llvm -o - -x c++ | FileCheck --check-prefix=IR %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/ir-capture5 -emit-llvm -o - -x c++ | FileCheck --check-prefix=IR %s

// C: Dumping dcAuto:
// C-NEXT: VarDecl {{.*}} dcAuto 'int *' static cinit
// C: Dumping dcAutoPtr:
// C-NEXT: VarDecl {{.*}} dcAutoPtr 'int *' static cinit

// CXX: Dumping dxAuto:
// CXX-NEXT: VarDecl {{.*}} dxAuto 'int *' inline cinit
// CXX: Dumping dxAutoPtr:
// CXX-NEXT: VarDecl {{.*}} dxAutoPtr 'int *' inline cinit
// CXX: Dumping dxFloat:
// CXX-NEXT: FunctionDecl {{.*}} dxFloat 'int (float)' inline
// CXX-NEXT: ParmVarDecl {{.*}} v 'float'
// CXX: Dumping dxNull:
// CXX4-NEXT: FunctionDecl {{.*}} dxNull 'int (int * _Nullable)' inline
// CXX4-NEXT: ParmVarDecl {{.*}} p 'int * _Nullable':'int *'
// CXX5-NEXT: FunctionDecl {{.*}} dxNull 'int (int * _Nonnull)' inline
// CXX5-NEXT: ParmVarDecl {{.*}} p 'int * _Nonnull':'int *'
// CXX: Dumping dxRet:
// CXX-NEXT: FunctionDecl {{.*}} dxRet 'int *(int * _Nonnull)' inline
// CXX-NEXT: ParmVarDecl {{.*}} p 'int * _Nonnull':'int *'
// CXX: Dumping dxRetPtr:
// CXX-NEXT: FunctionDecl {{.*}} dxRetPtr 'int *(int * _Nonnull)' inline
// CXX-NEXT: ParmVarDecl {{.*}} p 'int * _Nonnull':'int *'

// CXX: VarTemplateSpecializationDecl {{.*}} dxVarT 'int * _Nonnull':'int *' explicit_instantiation_definition
// CXX: FunctionDecl {{.*}} dxFnT 'int * _Nonnull (int * _Nonnull)' explicit_instantiation_definition

// IR: call noundef i32 @_Z7dxFloatf(float noundef 1.500000e+00)

//--- module.modulemap
module DedC { header "DedC.h" export * }
module DedCXX { header "DedCXX.h" export * }

//--- DedC.h
static int dcTarget;
static __auto_type dcAuto = &dcTarget;
static __auto_type *dcAutoPtr = &dcTarget;

//--- DedC.apinotes
Name: DedC
Globals:
- Name: dcAuto
  Nullability: N
- Name: dcAutoPtr
  Nullability: N

//--- DedCXX.h
inline int dxTarget = 0;
inline auto dxAuto = &dxTarget;
inline auto *dxAutoPtr = &dxTarget;
inline auto dxFloat(int v) { return 0; }
inline auto dxNull(int *p) { return 0; }
inline auto dxRet(int *p) { return (int *)nullptr; }
inline auto *dxRetPtr(int *p) { return (int *)nullptr; }
template <typename T> inline auto dxVarT = (T *)nullptr;
inline int *useVarT() { return dxVarT<int>; }
template int *dxVarT<int>;
template <typename T> inline auto dxFnT(T *p) { return p; }
inline int *useFnT() { return dxFnT<int>(nullptr); }
template auto dxFnT<int>(int *);

//--- DedCXX.apinotes
Name: DedCXX
Globals:
- Name: dxAuto
  Nullability: N
- Name: dxAutoPtr
  Nullability: N
- Name: dxVarT
  Nullability: N
Functions:
- Name: dxFloat
  Parameters:
  - Position: 0
    Type: 'float'
- Name: dxNull
  Parameters:
  - Position: 0
    Nullability: N
- Name: dxRet
  NullabilityOfRet: N
- Name: dxRetPtr
  NullabilityOfRet: N
- Name: dxFnT
  NullabilityOfRet: N
SwiftVersions:
- Version: 4
  Functions:
  - Name: dxNull
    Parameters:
    - Position: 0
      Nullability: O

//--- use.c
#include "DedC.h"

//--- use.cpp
#include "DedCXX.h"
int callFloat() { return dxFloat(1.5f); }
