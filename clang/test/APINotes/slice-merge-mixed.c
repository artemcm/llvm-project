// A capture-mode module redeclares a function of a module built without
// -fswift-version-independent-apinotes, at Swift 4. The collapse merges with
// that declaration as that module's build left it, as a default-mode build
// over the same module does, at every version (regression guard).

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -fmodules -fno-implicit-modules -fmodule-map-file=%t/module.modulemap -fapinotes-modules -I %t -x c -emit-module -fmodule-name=MA -fapinotes-swift-version=4 %t/module.modulemap -o %t/ma4.pcm
// RUN: %clang_cc1 -fmodules -fno-implicit-modules -fmodule-map-file=%t/module.modulemap -fapinotes-modules -I %t -x c -emit-module -fmodule-name=MB -fswift-version-independent-apinotes -fmodule-file=MA=%t/ma4.pcm %t/module.modulemap -o %t/mb-capture.pcm
// RUN: %clang_cc1 -fmodules -fno-implicit-modules -fmodule-map-file=%t/module.modulemap -fapinotes-modules -I %t -x c -emit-module -fmodule-name=MB -fapinotes-swift-version=4 -fmodule-file=MA=%t/ma4.pcm %t/module.modulemap -o %t/mb-default4.pcm
// RUN: %clang_cc1 -fmodules -fno-implicit-modules -fmodule-map-file=%t/module.modulemap -fapinotes-modules -I %t -x c -emit-module -fmodule-name=MB -fapinotes-swift-version=5 -fmodule-file=MA=%t/ma4.pcm %t/module.modulemap -o %t/mb-default5.pcm 2>/dev/null

// RUN: %clang_cc1 -fmodules -fno-implicit-modules -fmodule-map-file=%t/module.modulemap -fapinotes-modules -I %t -x c -fsyntax-only -fapinotes-swift-version=4 -fmodule-file=MA=%t/ma4.pcm -fmodule-file=MB=%t/mb-default4.pcm %t/use.c -ast-dump-all -ast-dump-filter mxFn | FileCheck %s
// RUN: %clang_cc1 -fmodules -fno-implicit-modules -fmodule-map-file=%t/module.modulemap -fapinotes-modules -I %t -x c -fsyntax-only -fapinotes-swift-version=4 -fmodule-file=MA=%t/ma4.pcm -fmodule-file=MB=%t/mb-capture.pcm %t/use.c -ast-dump-all -ast-dump-filter mxFn | FileCheck --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -fmodules -fno-implicit-modules -fmodule-map-file=%t/module.modulemap -fapinotes-modules -I %t -x c -fsyntax-only -fapinotes-swift-version=5 -fmodule-file=MA=%t/ma4.pcm -fmodule-file=MB=%t/mb-default5.pcm %t/use.c -ast-dump-all -ast-dump-filter mxFn 2>/dev/null | FileCheck %s
// RUN: %clang_cc1 -fmodules -fno-implicit-modules -fmodule-map-file=%t/module.modulemap -fapinotes-modules -I %t -x c -fsyntax-only -fapinotes-swift-version=5 -fmodule-file=MA=%t/ma4.pcm -fmodule-file=MB=%t/mb-capture.pcm %t/use.c -ast-dump-all -ast-dump-filter mxFn 2>/dev/null | FileCheck --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s

// CHECK:      Dumping mxFn:
// CHECK-NEXT: FunctionDecl {{.*}} imported in MA mxFn 'void (int *)' external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in MA p 'int *'
// CHECK-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 IsReplacedByActive 0
// CHECK-NEXT: SwiftNameAttr {{.*}} "mxNew(_:)"
// CHECK-NEXT: SwiftNameAttr {{.*}} "mxOld(_:)"
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mxFn:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in MB mxFn 'void (int *)' external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in MB p 'int *'
// CHECK-NEXT: SwiftNameAttr {{.*}} Inherited "mxOld(_:)"
// CHECK-NEXT: SwiftPrivateAttr {{.*}}
// CHECK-EMPTY:

//--- module.modulemap
module MA { header "MA.h" export * }
module MB { header "MB.h" export * }

//--- MA.h
void mxFn(int *p);

//--- MA.apinotes
Name: MA
Functions:
- Name: mxFn
  SwiftName: 'mxNew(_:)'
  Parameters:
  - Position: 0
    Nullability: N
SwiftVersions:
- Version: 4
  Functions:
  - Name: mxFn
    SwiftName: 'mxOld(_:)'

//--- MB.h
#include "MA.h"
void mxFn(int *p);

//--- MB.apinotes
Name: MB
Functions:
- Name: mxFn
  SwiftPrivate: true

//--- use.c
#include "MB.h"
