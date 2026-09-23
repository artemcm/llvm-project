// Parameters take their API notes from their function's entry, never from a
// lookup of their own. Under -fswift-version-independent-apinotes each
// parameter carries its function's slice markers, and it collapses with its
// function.
//
//   lbProbe      a redeclaration's parameter inherits lifetimebound, which
//                Clang's lifetime analysis reads off the most recent
//                declaration.
//   takesShadow  its parameter is named like an API-noted global. While its
//                attributes are processed, a parameter's DeclContext is still
//                the translation unit, and it must not be looked up as that
//                global.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default4 -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -ast-dump -ast-dump-filter Probe -x c++ | FileCheck --check-prefix=CHECK %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture4 -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -ast-dump -ast-dump-filter Probe -x c++ | FileCheck --check-prefix=CHECK --implicit-check-not=SwiftVersionedSliceAttr %s
// RUN: %clang_cc1 -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default4 -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -ast-dump -ast-dump-filter takesShadow -x c++ | FileCheck --check-prefix=SHADOW %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture4 -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -ast-dump -ast-dump-filter takesShadow -x c++ | FileCheck --check-prefix=SHADOW --implicit-check-not=SwiftVersionedSliceAttr %s

// RUN: %clang_cc1 -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default4 -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fsyntax-only -verify -x c++
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture4 -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fsyntax-only -verify -x c++

// CHECK: Dumping lbProbe:
// CHECK-NEXT: FunctionDecl {{.*}} imported in LbOwner {{.*}}lbProbe
// CHECK-NEXT: ParmVarDecl {{.*}} p 'int *'
// CHECK-NEXT: LifetimeBoundAttr
// CHECK-EMPTY:
// CHECK-NEXT: Dumping lbProbe:
// CHECK-NEXT: FunctionDecl {{.*}} imported in LbHeir {{.*}}lbProbe
// CHECK-NEXT: ParmVarDecl {{.*}} p 'int *'
// CHECK-NEXT: LifetimeBoundAttr {{.*}} Inherited
// CHECK-EMPTY:

// The parameter carries nothing: the lines after it are the function's.
// SHADOW: Dumping takesShadow:
// SHADOW-NEXT: FunctionDecl {{.*}} takesShadow
// SHADOW-NEXT: ParmVarDecl {{.*}} shadow 'int *'
// SHADOW-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// SHADOW-NEXT: SwiftNameAttr {{.*}} "takesShadow(_:)"
// SHADOW-NEXT: SwiftNameAttr {{.*}} "takesShadowFour(_:)"
// SHADOW-EMPTY:

//--- module.modulemap
module LbOwner { header "LbOwner.h" export * }
module LbHeir { header "LbHeir.h" export * }

//--- LbOwner.apinotes
Name: LbOwner
Functions:
  - Name: lbProbe
    Parameters:
      - Position: 0
        Lifetimebound: true
  - Name: takesShadow
    SwiftName: 'takesShadow(_:)'
Globals:
  - Name: shadow
    SwiftPrivate: true
SwiftVersions:
  - Version: 4.0
    Functions:
      - Name: takesShadow
        SwiftName: 'takesShadowFour(_:)'
    Globals:
      - Name: shadow
        SwiftPrivate: false

//--- LbOwner.h
int *lbProbe(int *p);
extern int *shadow;
void takesShadow(int *shadow);

//--- LbHeir.h
#include "LbOwner.h"
// No API notes cover this header.
int *lbProbe(int *p);

//--- use.cpp
#include "LbHeir.h"
int *escape() {
  int x = 0;
  return lbProbe(&x); // expected-warning {{address of stack memory associated with local variable 'x' returned}}
}
void callShadow() { takesShadow(shadow); }
