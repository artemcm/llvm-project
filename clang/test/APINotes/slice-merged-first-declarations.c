// Two capture-mode modules that include one textual header each declare its C
// function first, and the consumer's reader merges the two. The second is a
// redeclaration only in the consumer, so it keeps the type its own notes give
// it, as its module built in the default mode would.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump -ast-dump-filter txShared -x c 2>/dev/null | FileCheck %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump -ast-dump-filter txShared -x c 2>/dev/null | FileCheck --implicit-check-not=SwiftVersionedSliceAttr %s

// CHECK:      FunctionDecl {{.*}} imported in TwoA {{.*}}txShared 'int * _Nonnull (int * _Nonnull _Nonnull)'
// CHECK:      FunctionDecl {{.*}} prev {{.*}} imported in TwoB {{.*}}txShared 'int * _Nonnull (int * _Nonnull _Nonnull)'

//--- module.modulemap
module TwoA { header "TwoA.h" export * }
module TwoB { header "TwoB.h" export * }

//--- Shared.h
int *txShared(int *p);

//--- TwoA.h
#include "Shared.h"

//--- TwoB.h
#include "Shared.h"

//--- TwoA.apinotes
Name: TwoA
Functions:
- Name: txShared
  NullabilityOfRet: N
  Parameters:
  - Position: 0
    Nullability: N

//--- TwoB.apinotes
Name: TwoB
Functions:
- Name: txShared
  NullabilityOfRet: N
  Parameters:
  - Position: 0
    Nullability: N

//--- use.c
#include "TwoA.h"
#include "TwoB.h"
int *use(int *p) { return txShared(p); }
