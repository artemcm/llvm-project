// A translation unit that does not build a module applies API notes normally
// under -fswift-version-independent-apinotes. It has one Swift version, so
// there is no module file to serve several versions and nothing to capture for.
// The same checks run against the default mode.

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -fapinotes -fswift-version-independent-apinotes -fapinotes-swift-version=4 -I %t %t/tu.c -ast-dump -ast-dump-filter tuRenamed -x c | FileCheck %s
// RUN: %clang_cc1 -fapinotes -fapinotes-swift-version=4 -I %t %t/tu.c -ast-dump -ast-dump-filter tuRenamed -x c | FileCheck %s

// The 4.0 slice is applied and the unversioned one superseded. No slice
// markers, which only a capturing module build emits.
// CHECK: Dumping tuRenamed:
// CHECK: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// CHECK-NEXT: SwiftNameAttr {{.*}} "tuUnversioned()"
// CHECK-NEXT: SwiftNameAttr {{.*}} "tuV4()"
// CHECK-NOT: SwiftVersionedSliceAttr

//--- APINotes.apinotes
Name: TranslationUnit
Functions:
  - Name: tuRenamed
    SwiftName: 'tuUnversioned()'
SwiftVersions:
  - Version: 4.0
    Functions:
      - Name: tuRenamed
        SwiftName: 'tuV4()'

//--- TranslationUnit.h
void tuRenamed(void);

//--- tu.c
#include "TranslationUnit.h"
