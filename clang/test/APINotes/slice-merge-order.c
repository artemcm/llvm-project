// A record names the declaration its producer merged, not the one the
// consumer's redeclaration chain happens to put before it.
//
//   ordSib      two modules each redeclare SA's function, with their own
//               notes, and a consumer imports them in either order. Each
//               declaration merges with SA's, not with its sibling
//               (regression guard; copying slices got this right too).
//   ordRetained RTC inherits both conventions RTB has live: the one its own
//               notes give it, and before it, the one RTB inherits from RTA.

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use1.c -fapinotes-swift-version=4 -fmodules-cache-path=%t/default1-4 -ast-dump-all -ast-dump-filter ord -x c | FileCheck --check-prefixes=USE1CHECK,USE1V4 --allow-unused-prefixes %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use1.c -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture1-4 -ast-dump-all -ast-dump-filter ord -x c | FileCheck --check-prefixes=USE1CHECK,USE1V4 --allow-unused-prefixes --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use1.c -fapinotes-swift-version=5 -fmodules-cache-path=%t/default1-5 -ast-dump-all -ast-dump-filter ord -x c | FileCheck --check-prefixes=USE1CHECK,USE1V5 --allow-unused-prefixes %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use1.c -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture1-5 -ast-dump-all -ast-dump-filter ord -x c | FileCheck --check-prefixes=USE1CHECK,USE1V5 --allow-unused-prefixes --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use2.c -fapinotes-swift-version=4 -fmodules-cache-path=%t/default2-4 -ast-dump-all -ast-dump-filter ord -x c | FileCheck --check-prefixes=USE2CHECK,USE2V4 --allow-unused-prefixes %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use2.c -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture2-4 -ast-dump-all -ast-dump-filter ord -x c | FileCheck --check-prefixes=USE2CHECK,USE2V4 --allow-unused-prefixes --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use2.c -fapinotes-swift-version=5 -fmodules-cache-path=%t/default2-5 -ast-dump-all -ast-dump-filter ord -x c | FileCheck --check-prefixes=USE2CHECK,USE2V5 --allow-unused-prefixes %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use2.c -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture2-5 -ast-dump-all -ast-dump-filter ord -x c | FileCheck --check-prefixes=USE2CHECK,USE2V5 --allow-unused-prefixes --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s

// USE1CHECK:      Dumping ordSib:
// USE1CHECK-NEXT: FunctionDecl {{.*}} imported in SA ordSib 'void (int * _Nonnull)' external-linkage
// USE1CHECK-NEXT: ParmVarDecl {{.*}} imported in SA p 'int * _Nonnull':'int *'
// USE1CHECK-NEXT: SwiftNameAttr {{.*}} "sibA(_:)"
// USE1CHECK-EMPTY:
// USE1CHECK-NEXT: Dumping ordSib:
// USE1CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in SB ordSib 'void (int * _Nonnull)' external-linkage
// USE1CHECK-NEXT: ParmVarDecl {{.*}} imported in SB p 'int * _Nonnull':'int *'
// USE1CHECK-NEXT: SwiftNameAttr {{.*}} Inherited "sibA(_:)"
// USE1CHECK-NEXT: SwiftPrivateAttr {{.*}}
// USE1CHECK-EMPTY:
// USE1V4-NEXT: Dumping ordSib:
// USE1V4-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in SC ordSib 'void (int * _Nonnull)' external-linkage
// USE1V4-NEXT: ParmVarDecl {{.*}} imported in SC p 'int * _Nullable':'int *'
// USE1V4-NEXT: SwiftNameAttr {{.*}} Inherited "sibA(_:)"
// USE1V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 IsReplacedByActive 0
// USE1V4-NEXT: AvailabilityAttr {{.*}} swift 0 0 0 Unavailable "" "" 0
// USE1V4-EMPTY:
// USE1V5-NEXT: Dumping ordSib:
// USE1V5-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in SC ordSib 'void (int * _Nonnull)' external-linkage
// USE1V5-NEXT: ParmVarDecl {{.*}} imported in SC p 'int * _Nonnull':'int *'
// USE1V5-NEXT: SwiftNameAttr {{.*}} Inherited "sibA(_:)"
// USE1V5-NEXT: AvailabilityAttr {{.*}} swift 0 0 0 Unavailable "" "" 0
// USE1V5-EMPTY:
// USE1CHECK-NEXT: Dumping ordRetained:
// USE1CHECK-NEXT: FunctionDecl {{.*}} imported in RTA ordRetained 'void *(void)' external-linkage
// USE1CHECK-NEXT: NSReturnsRetainedAttr {{.*}}
// USE1CHECK-EMPTY:
// USE1CHECK-NEXT: Dumping ordRetained:
// USE1CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in RTB ordRetained 'void *(void)' external-linkage
// USE1CHECK-NEXT: NSReturnsRetainedAttr {{.*}} Inherited
// USE1CHECK-NEXT: CFReturnsRetainedAttr {{.*}}
// USE1CHECK-EMPTY:
// USE1CHECK-NEXT: Dumping ordRetained:
// USE1CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in RTC ordRetained 'void *(void)' external-linkage
// USE1CHECK-NEXT: NSReturnsRetainedAttr {{.*}} Inherited
// USE1CHECK-NEXT: CFReturnsRetainedAttr {{.*}} Inherited
// USE1CHECK-EMPTY:

// USE2CHECK:      Dumping ordSib:
// USE2CHECK-NEXT: FunctionDecl {{.*}} imported in SA ordSib 'void (int * _Nonnull)' external-linkage
// USE2CHECK-NEXT: ParmVarDecl {{.*}} imported in SA p 'int * _Nonnull':'int *'
// USE2CHECK-NEXT: SwiftNameAttr {{.*}} "sibA(_:)"
// USE2CHECK-EMPTY:
// USE2V4-NEXT: Dumping ordSib:
// USE2V4-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in SC ordSib 'void (int * _Nonnull)' external-linkage
// USE2V4-NEXT: ParmVarDecl {{.*}} imported in SC p 'int * _Nullable':'int *'
// USE2V4-NEXT: SwiftNameAttr {{.*}} Inherited "sibA(_:)"
// USE2V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 IsReplacedByActive 0
// USE2V4-NEXT: AvailabilityAttr {{.*}} swift 0 0 0 Unavailable "" "" 0
// USE2V4-EMPTY:
// USE2V5-NEXT: Dumping ordSib:
// USE2V5-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in SC ordSib 'void (int * _Nonnull)' external-linkage
// USE2V5-NEXT: ParmVarDecl {{.*}} imported in SC p 'int * _Nonnull':'int *'
// USE2V5-NEXT: SwiftNameAttr {{.*}} Inherited "sibA(_:)"
// USE2V5-NEXT: AvailabilityAttr {{.*}} swift 0 0 0 Unavailable "" "" 0
// USE2V5-EMPTY:
// USE2V4-NEXT: Dumping ordSib:
// USE2V4-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in SB ordSib 'void (int * _Nonnull)' external-linkage
// USE2V4-NEXT: ParmVarDecl {{.*}} imported in SB p 'int * _Nonnull':'int *'
// USE2V4-NEXT: SwiftNameAttr {{.*}} Inherited "sibA(_:)"
// USE2V4-NEXT: SwiftPrivateAttr {{.*}}
// USE2V4-EMPTY:
// USE2V5-NEXT: Dumping ordSib:
// USE2V5-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in SB ordSib 'void (int * _Nonnull)' external-linkage
// USE2V5-NEXT: ParmVarDecl {{.*}} imported in SB p 'int * _Nonnull':'int *'
// USE2V5-NEXT: SwiftNameAttr {{.*}} Inherited "sibA(_:)"
// USE2V5-NEXT: AvailabilityAttr {{.*}} Inherited swift 0 0 0 Unavailable "" "" 0
// USE2V5-NEXT: SwiftPrivateAttr {{.*}}
// USE2V5-EMPTY:
// USE2CHECK-NEXT: Dumping ordRetained:
// USE2CHECK-NEXT: FunctionDecl {{.*}} imported in RTA ordRetained 'void *(void)' external-linkage
// USE2CHECK-NEXT: NSReturnsRetainedAttr {{.*}}
// USE2CHECK-EMPTY:
// USE2CHECK-NEXT: Dumping ordRetained:
// USE2CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in RTB ordRetained 'void *(void)' external-linkage
// USE2CHECK-NEXT: NSReturnsRetainedAttr {{.*}} Inherited
// USE2CHECK-NEXT: CFReturnsRetainedAttr {{.*}}
// USE2CHECK-EMPTY:
// USE2CHECK-NEXT: Dumping ordRetained:
// USE2CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in RTC ordRetained 'void *(void)' external-linkage
// USE2CHECK-NEXT: NSReturnsRetainedAttr {{.*}} Inherited
// USE2CHECK-NEXT: CFReturnsRetainedAttr {{.*}} Inherited
// USE2CHECK-EMPTY:

//--- module.modulemap
module SA { header "SA.h" export * }
module SB { header "SB.h" export * }
module SC { header "SC.h" export * }
module RTA { header "RTA.h" export * }
module RTB { header "RTB.h" export * }
module RTC { header "RTC.h" export * }

//--- SA.h
void ordSib(int *p) __attribute__((swift_name("sibA(_:)")));

//--- SA.apinotes
Name: SA
Functions:
- Name: ordSib
  Parameters:
  - Position: 0
    Nullability: N

//--- SB.h
#include "SA.h"
void ordSib(int *p);

//--- SB.apinotes
Name: SB
Functions:
- Name: ordSib
  SwiftPrivate: true

//--- SC.h
#include "SA.h"
void ordSib(int *p);

//--- SC.apinotes
Name: SC
Functions:
- Name: ordSib
  Availability: nonswift
SwiftVersions:
- Version: 4
  Functions:
  - Name: ordSib
    Parameters:
    - Position: 0
      Nullability: O

//--- RTA.h
void *ordRetained(void);

//--- RTA.apinotes
Name: RTA
Functions:
- Name: ordRetained
  RetainCountConvention: NSReturnsRetained

//--- RTB.h
#include "RTA.h"
void *ordRetained(void);

//--- RTB.apinotes
Name: RTB
Functions:
- Name: ordRetained
  RetainCountConvention: CFReturnsRetained

//--- RTC.h
#include "RTB.h"
void *ordRetained(void);

//--- use1.c
#include "SB.h"
#include "SC.h"
#include "RTC.h"

//--- use2.c
#include "SC.h"
#include "SB.h"
#include "RTC.h"
