// Two API notes readers annotating one declaration across a redeclaration.
//
// Slice groups number readers within one lookup, and a lookup covers one source
// location. So the group 0 that SliceOwner's reader stamps and the group 0 that
// SliceHeir's reader stamps are unrelated lookups sharing a number. Propagating
// the owner's slices verbatim would pool them, and selection would then elect a
// single winner across two readers. At Swift 4 the owner's 4.0 slice would win
// the pooled group and suppress the heir's unversioned one, losing
// swift_private.
//
// So a declaration numbers the groups it receives above those it already
// carries, both when its own lookup runs and when it inherits.

// RUN: rm -rf %t && mkdir -p %t

// RUN: %clang_cc1 -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default4 -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter renumberProbe -x c | FileCheck --check-prefix=V4 %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture4 -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter renumberProbe -x c | FileCheck --check-prefix=V4 --implicit-check-not=SwiftVersionedSliceAttr %s
// RUN: %clang_cc1 -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default5 -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter renumberProbe -x c | FileCheck --check-prefix=V5 %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture5 -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter renumberProbe -x c | FileCheck --check-prefix=V5 --implicit-check-not=SwiftVersionedSliceAttr %s

#include "SliceHeir.h"

// Both annotations survive: swift_private is the heir's own, and the name is
// the one the owner's own selection gives it, inherited.
// V4: imported in SliceHeir renumberProbe
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "ownerV4()"
// V4-NEXT: SwiftPrivateAttr
// V4-EMPTY:

// V5: imported in SliceHeir renumberProbe
// V5-NEXT: SwiftNameAttr {{.*}} Inherited "ownerUnversioned()"
// V5-NEXT: SwiftPrivateAttr
// V5-EMPTY:
