// Two lookups against a single API notes reader, which is the case that makes
// the slice group distinct from a reader index.
//
// Sema runs a broad lookup for a global function and, when the sidecar carries
// a `Where: Parameters:` entry, a second exact lookup beside it. Each call runs
// its own version selection and each winner is applied, so the two are separate
// competitions even though they read the same file. If both stamped the same
// group, a consumer that recomputes the selection would pool all four slices
// into one competition, pick a single winner, and silently drop either the
// broad annotation or the exact one.
//
// slice-groups.c covers the other way a second group arises, two readers via
// export_as. This one cannot be expressed with one lookup per reader, so it is
// the test that pins the group to the lookup rather than to the reader.
//
// Under -fswift-version-independent-apinotes the module build captures every
// slice and this translation unit collapses them at its own version. The bar
// is the default mode, so each check runs against both with the same
// expectations.

// RUN: rm -rf %t && mkdir -p %t

// With no requested version, only the unversioned slices can win.
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/def-none -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter sliceGroupExactProbe -x c | FileCheck --check-prefix=NOVERSION %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cap-none -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter sliceGroupExactProbe -x c | FileCheck --check-prefix=NOVERSION %s

// At Swift 3 each lookup's 3.0 slice wins its own group.
// RUN: %clang_cc1 -fapinotes-swift-version=3 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/def-v3 -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter sliceGroupExactProbe -x c | FileCheck --check-prefix=V3 %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=3 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cap-v3 -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter sliceGroupExactProbe -x c | FileCheck --check-prefix=V3 %s

#include "SliceGroupsExact.h"

// Both lookups' winners are applied: the broad one, then the exact one, which
// displaces it. The displaced name is re-wrapped under the exact lookup's
// group, 1, and each group's losing 3.0 slice keeps its own group.
// NOVERSION: Dumping sliceGroupExactProbe:
// NOVERSION: SwiftVersionedAdditionAttr {{.+}} Implicit 3.0 0{{$}}
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "broadV3(_:)"
// NOVERSION-NEXT: SwiftVersionedAdditionAttr {{.+}} Implicit 0 IsReplacedByActive 1{{$}}
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "broadUnversioned(_:)"
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "exactUnversioned(_:)"
// NOVERSION-NEXT: SwiftVersionedAdditionAttr {{.+}} Implicit 3.0 1{{$}}
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "exactV3(_:)"

// Two winners at 3.0, one per group. Pooled, the four slices would have
// elected a single one.
// V3: Dumping sliceGroupExactProbe:
// V3: SwiftVersionedAdditionAttr {{.+}} Implicit 3.0 IsReplacedByActive 0{{$}}
// V3-NEXT: SwiftNameAttr {{.+}} "broadUnversioned(_:)"
// V3-NEXT: SwiftVersionedAdditionAttr {{.+}} Implicit 3.0 IsReplacedByActive 1{{$}}
// V3-NEXT: SwiftNameAttr {{.+}} "exactUnversioned(_:)"
// V3-NEXT: SwiftVersionedAdditionAttr {{.+}} Implicit 3.0 IsReplacedByActive 1{{$}}
// V3-NEXT: SwiftNameAttr {{.+}} "broadV3(_:)"
// V3-NEXT: SwiftNameAttr {{.+}} "exactV3(_:)"
