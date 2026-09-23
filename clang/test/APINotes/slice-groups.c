// A declaration annotated by two API notes readers at once, which is what makes
// the slice group observable. ExportAsCore is `export_as ExportAs`, so
// tryAPINotes loads ExportAsCore.apinotes and then ExportAs.apinotes from the
// same directory. Both are public readers.
//
// Clang selects one slice per group and applies every group's winner, so the
// two readers are two groups. The group number is what tells a consumer which
// slices were candidates against each other; pooling them would let one
// reader's slice suppress the other's.
//
// This is the sibling of versioned-version-independent.m, which covers the
// keyless-slice case on a single reader, and of slice-groups-order.c, which
// covers two readers and a parameter selector at once.
//
// Under -fswift-version-independent-apinotes the module build captures every
// slice and this translation unit collapses them at its own version, so the
// check is behavioural: *both* readers' winners have to land. Pooling would
// elect one winner across the two and drop the other. The group numbers stay
// directly visible too, on the wrappers the losing slices leave behind.

// RUN: rm -rf %t && mkdir -p %t

// With no requested version, only an unversioned slice can win — in each group.
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/none -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter sliceGroupProbe -x c | FileCheck --check-prefix=NOVERSION %s

// At Swift 4 group 2's versioned slice wins and takes swift_private away, while
// group 0 still falls back to its unversioned slice. One group moving and the
// other not is only possible if the two were never pooled.
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/v4 -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter sliceGroupProbe -x c | FileCheck --check-prefix=V4 %s

#include "ExportAs.h"

// Group 0 is ExportAsCore.apinotes: an unversioned slice and a 3.0 slice.
// Group 2 is ExportAs.apinotes, reached through export_as. Reader 1's broad
// lookup is group 2, not 1, because each reader reserves an odd number for its
// parameter-selector lookup.
// NOVERSION: Dumping sliceGroupProbe:
// NOVERSION: SwiftNameAttr {{.*}} "fromCoreUnversioned"
// NOVERSION-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 3.0 0{{$}}
// NOVERSION-NEXT: SwiftNameAttr {{.*}} "fromCoreV3"
// NOVERSION-NEXT: SwiftPrivateAttr
// NOVERSION-NEXT: SwiftVersionedRemovalAttr {{.*}} Implicit 4.0 {{[0-9]+}} 2{{$}}

// V4: Dumping sliceGroupProbe:
// V4: SwiftNameAttr {{.*}} "fromCoreUnversioned"
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 3.0 0{{$}}
// V4-NEXT: SwiftNameAttr {{.*}} "fromCoreV3"
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 2{{$}}
// V4-NEXT: SwiftPrivateAttr
// V4-NOT: SwiftPrivateAttr
