// Two API notes readers and a parameter selector on one declaration, which is
// the only configuration where the slice group numbering's ordering property is
// observable.
//
// Sema makes two lookups per reader: a broad one, and a parameter-selector one
// for a `Where: Parameters:` entry. It applies them reader by reader, broad
// first, so each reader owns an adjacent pair of groups. ExportAsCore takes 0
// and 1, ExportAs takes 2 and 3, and that is also the order Sema applied them.
//
// The ordering matters because a group's winner can collide with another
// group's winner on the same key, as all four do here on SwiftName. Clang
// resolves that by last-applied-wins, so a consumer has to replay the groups in
// ascending order. Numbering the readers 0 and 1 and the selector lookups
// 2 and 3 would apply them in the order 0, 2, 1, 3, and a consumer sorting on
// the ordinal would pick the wrong winner.
//
// slice-groups.c covers two readers with no selector, and slice-groups-exact.c
// covers one reader with a selector. Neither pins the order, because with a
// single pair any numbering is ascending.
//
// Under -fswift-version-independent-apinotes the module build captures every
// slice and this translation unit collapses them at its own version. The bar
// is the default mode, so each check runs against both with the same
// expectations.

// RUN: rm -rf %t && mkdir -p %t

// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/def-none -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter sliceGroupOrderProbe -x c | FileCheck --check-prefix=NOVERSION %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cap-none -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter sliceGroupOrderProbe -x c | FileCheck --check-prefix=NOVERSION %s

// At Swift 3 group 0's 3.0 slice wins, and is displaced in turn.
// RUN: %clang_cc1 -fapinotes-swift-version=3 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/def-v3 -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter sliceGroupOrderProbe -x c | FileCheck --check-prefix=V3 %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=3 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cap-v3 -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter sliceGroupOrderProbe -x c | FileCheck --check-prefix=V3 %s

#include "ExportAs.h"

// Each group's winner displaces the previous group's, and each displaced name
// is re-wrapped under the group that displaced it: 0, 1, 2 and 3 in turn. The
// live name is group 3's, ExportAs's parameter-selector lookup, because it was
// applied last.
// NOVERSION: Dumping sliceGroupOrderProbe:
// NOVERSION: SwiftVersionedAdditionAttr {{.+}} Implicit 3.0 0{{$}}
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "coreBroadV3(_:)"
// NOVERSION-NEXT: SwiftVersionedAdditionAttr {{.+}} Implicit 0 IsReplacedByActive 1{{$}}
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "coreBroad(_:)"
// NOVERSION-NEXT: SwiftVersionedAdditionAttr {{.+}} Implicit 0 IsReplacedByActive 2{{$}}
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "coreExact(_:)"
// NOVERSION-NEXT: SwiftVersionedAdditionAttr {{.+}} Implicit 0 IsReplacedByActive 3{{$}}
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "exportBroad(_:)"
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "exportExact(_:)"

// V3: Dumping sliceGroupOrderProbe:
// V3: SwiftVersionedAdditionAttr {{.+}} Implicit 3.0 IsReplacedByActive 0{{$}}
// V3-NEXT: SwiftNameAttr {{.+}} "coreBroad(_:)"
// V3-NEXT: SwiftVersionedAdditionAttr {{.+}} Implicit 0 IsReplacedByActive 1{{$}}
// V3-NEXT: SwiftNameAttr {{.+}} "coreBroadV3(_:)"
// V3-NEXT: SwiftVersionedAdditionAttr {{.+}} Implicit 0 IsReplacedByActive 2{{$}}
// V3-NEXT: SwiftNameAttr {{.+}} "coreExact(_:)"
// V3-NEXT: SwiftVersionedAdditionAttr {{.+}} Implicit 0 IsReplacedByActive 3{{$}}
// V3-NEXT: SwiftNameAttr {{.+}} "exportBroad(_:)"
// V3-NEXT: SwiftNameAttr {{.+}} "exportExact(_:)"
