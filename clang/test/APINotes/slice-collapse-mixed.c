// A module built without -fswift-version-independent-apinotes is a consumer,
// even though it is a module build: it has one Swift version, so it applies
// the API notes captured in the modules it imports, as it applies its own. What
// it inherits from them, and what it diagnoses, then matches a module built
// against a default-mode import.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=MixA -o %t/A-capture.pcm -fapinotes-modules -fswift-version-independent-apinotes -I %t %t/module.modulemap -x c
// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=MixA -o %t/A-default.pcm -fapinotes-modules -fapinotes-swift-version=4 -I %t %t/module.modulemap -x c

// MixB redeclares mixFunc, so it inherits A's notes as selected at 4.
// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=MixB -fmodule-file=MixA=%t/A-capture.pcm -o %t/B-capture.pcm -fapinotes-modules -fapinotes-swift-version=4 -I %t %t/module.modulemap -x c
// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=MixB -fmodule-file=MixA=%t/A-default.pcm -o %t/B-default.pcm -fapinotes-modules -fapinotes-swift-version=4 -I %t %t/module.modulemap -x c

// RUN: %clang_cc1 -fmodules -fmodule-file=MixA=%t/A-default.pcm -fmodule-file=MixB=%t/B-default.pcm -fmodule-map-file=%t/module.modulemap -fapinotes-modules -fapinotes-swift-version=4 -I %t %t/use.c -ast-dump -ast-dump-filter mixFunc | FileCheck %s
// RUN: %clang_cc1 -fmodules -fmodule-file=MixA=%t/A-capture.pcm -fmodule-file=MixB=%t/B-capture.pcm -fmodule-map-file=%t/module.modulemap -fapinotes-modules -fapinotes-swift-version=4 -I %t %t/use.c -ast-dump -ast-dump-filter mixFunc | FileCheck --implicit-check-not=SwiftVersionedSliceAttr %s

// CHECK: Dumping mixFunc:
// CHECK-NEXT: FunctionDecl {{.*}} imported in MixA {{.*}}mixFunc
// CHECK-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// CHECK-NEXT: SwiftNameAttr {{.*}} "mixUnversioned()"
// CHECK-NEXT: SwiftNameAttr {{.*}} "mixFour()"
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mixFunc:
// CHECK-NEXT: FunctionDecl {{.*}} imported in MixB {{.*}}mixFunc
// CHECK-NEXT: SwiftNameAttr {{.*}} Inherited "mixFour()"
// CHECK-EMPTY:

// MixC uses an API that A's notes make unavailable.
// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=MixC -fmodule-file=MixA=%t/A-capture.pcm -o %t/C-capture.pcm -fapinotes-modules -fapinotes-swift-version=4 -I %t %t/module.modulemap -x c -verify
// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=MixC -fmodule-file=MixA=%t/A-default.pcm -o %t/C-default.pcm -fapinotes-modules -fapinotes-swift-version=4 -I %t %t/module.modulemap -x c -verify

//--- module.modulemap
module MixA { header "MixA.h" export * }
module MixB { header "MixB.h" export * }
module MixC { header "MixC.h" export * }

//--- MixA.apinotes
Name: MixA
Functions:
  - Name: mixFunc
    SwiftName: 'mixUnversioned()'
  - Name: mixUnavail
    Availability: none
    AvailabilityMsg: 'gone'
SwiftVersions:
  - Version: 4.0
    Functions:
      - Name: mixFunc
        SwiftName: 'mixFour()'

//--- MixA.h
void mixFunc(void);
void mixUnavail(void);

//--- MixB.h
#include "MixA.h"
void mixFunc(void);

//--- MixC.h
#include "MixA.h"
static inline void mixUser(void) {
  mixUnavail(); // expected-error {{'mixUnavail' is unavailable: gone}}
}
// expected-note@* {{'mixUnavail' has been explicitly marked unavailable here}}

//--- use.c
#include "MixB.h"
void caller(void) { mixFunc(); }
