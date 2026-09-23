// A redeclaration in a module that owns no API notes of its own.
//
// API notes readers resolve by source location, so a redeclaration in a scope
// no sidecar covers gets no slices of its own. Under
// -fswift-version-independent-apinotes the module build captures every slice
// unapplied, and those captured slices are plain Attr, so without help the
// inheritable loop in mergeDeclAttributes skips them and the redeclaration
// silently loses the annotation.
//
// mergeDeclAttributes therefore propagates the captured slices itself, per slice
// group rather than per attribute: selection picks the lowest slice at or above
// the requested version, so a group arriving without one of its markers can be
// handed to a slice the default mode never selects. The consumer then applies
// only each inherited group's winner, the way inheritance applies the
// attribute that winner left live, so the redeclaration gets exactly what the
// default mode gives it: that attribute, marked Inherited, and no wrappers.
//
// Every check runs against both modes: the bar is the default mode.

// RUN: rm -rf %t && mkdir -p %t

// RUN: %clang_cc1 -fapinotes-swift-version=3 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default3 -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter slice -x c | FileCheck --check-prefix=V3 %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=3 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture3 -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter slice -x c | FileCheck --check-prefix=V3 --implicit-check-not=SwiftVersionedSliceAttr %s
// RUN: %clang_cc1 -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default4 -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter slice -x c | FileCheck --check-prefix=V4 %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture4 -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers %s -ast-dump -ast-dump-filter slice -x c | FileCheck --check-prefix=V4 --implicit-check-not=SwiftVersionedSliceAttr %s

#include "SliceInheritanceClient.h"

// Swift 3 selects the versioned slice. The redeclaration inherits the live name
// alone.
// V3: imported in SliceInheritanceClient sliceInheritanceProbe
// V3-NEXT: SwiftNameAttr {{.*}} Inherited "renamedV3()"
// V3-EMPTY:

// Swift 4 is above every versioned slice, so the unversioned one wins.
// V4: imported in SliceInheritanceClient sliceInheritanceProbe
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "renamedUnversioned()"
// V4-EMPTY:

// One group, two slices, and only the unversioned one carries a payload that
// inherits. Both markers still had to travel for selection to come out right:
// at Swift 3 the versioned slice wins and supplies no name, so none appears.
// Had its marker been dropped, the unversioned rename would have won instead.
// V3: imported in SliceInheritanceClient sliceMixedProbe
// V3-EMPTY:
// V4: imported in SliceInheritanceClient sliceMixedProbe
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "mixedUnversioned()"
// V4-EMPTY:

// No slice in the group carries an attribute that inherits, so nothing
// travels at all.
//
// Known gap: the default mode rewrites the declaration's type with the
// selected nullability, and function type merging carries that type to the
// redeclaration. Capture mode doesn't apply types yet, so the types printed
// here differ between the modes and aren't checked.
// V3: imported in SliceInheritanceClient sliceNoInheritProbe
// V3-EMPTY:
// V4: imported in SliceInheritanceClient sliceNoInheritProbe
// V4-EMPTY:
