// RUN: rm -rf %t && mkdir -p %t

// Under -fswift-version-independent-apinotes the module build captures every
// slice unapplied and a consumer collapses them at its own Swift version. So
// the check is behavioural: compile the same translation unit against the same
// captured module at two versions, and require each to land on what the
// default mode applies at that version.
//
// Each slice an API notes lookup supplied is recorded by a
// SwiftVersionedSliceAttr, whether or not it went on to set a key. That marker
// is not visible here, because the collapse consumes it, but keylessSliceDUMP
// below is the case it exists for and that case is observable.

// Both runs share one module cache. The first builds the captured module, and
// the second, at another version, reuses it without building anything.
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fmodules -fblocks -fimplicit-module-maps -fmodules-cache-path=%t/cache -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers -F %S/Inputs/Frameworks %s -ast-dump -ast-dump-filter 'DUMP' | FileCheck -check-prefix=NOVERSION %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=3 -fmodules -fblocks -fimplicit-module-maps -fmodules-cache-path=%t/cache -fdisable-module-hash -fapinotes-modules -I %S/Inputs/Headers -F %S/Inputs/Frameworks %s -ast-dump -ast-dump-filter 'DUMP' -Rmodule-build 2>%t/v3.remarks | FileCheck -check-prefix=V3 %s
// RUN: FileCheck --check-prefix=REUSED --allow-empty %s < %t/v3.remarks
// REUSED-NOT: building module

#import <VersionedKit/VersionedKit.h>

// NOVERSION-LABEL: Dumping moveToPointDUMP
// NOVERSION: SwiftNameAttr {{.+}} "moveTo(x:y:)"
// NOVERSION-NEXT: SwiftVersionedAdditionAttr {{.+}} Implicit 3.0 0{{$}}
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "moveTo(a:b:)"

// V3-LABEL: Dumping moveToPointDUMP
// V3: SwiftVersionedAdditionAttr {{.+}} Implicit 3.0 IsReplacedByActive 0{{$}}
// V3-NEXT: SwiftNameAttr {{.+}} "moveTo(x:y:)"
// V3-NEXT: SwiftNameAttr {{.+}} "moveTo(a:b:)"

// An unversioned note supersedes the header's own swift_name at every version.
// NOVERSION-LABEL: Dumping unversionedRenameDUMP
// NOVERSION: SwiftVersionedAdditionAttr {{.+}} Implicit 0 IsReplacedByActive 0{{$}}
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "unversionedRename_HEADER()"
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "unversionedRename_NOTES()"

// V3-LABEL: Dumping unversionedRenameDUMP
// V3: SwiftVersionedAdditionAttr {{.+}} Implicit 0 IsReplacedByActive 0{{$}}
// V3-NEXT: SwiftNameAttr {{.+}} "unversionedRename_HEADER()"
// V3-NEXT: SwiftNameAttr {{.+}} "unversionedRename_NOTES()"

// The case the slice marker exists for. The 3.0 slice names the declaration and
// sets no key, so nothing but the marker records that the slice exists.
// Selecting it has to suppress the unversioned rename: at Swift 3 this
// declaration ends up with no live swift_name at all, and the unversioned one
// is superseded. Drop the marker and selection falls back to the unversioned
// slice, wrongly applying keylessSlice_NOTES here.
// NOVERSION-LABEL: Dumping keylessSliceDUMP
// NOVERSION: SwiftNameAttr {{.+}} "keylessSlice_NOTES()"

// V3-LABEL: Dumping keylessSliceDUMP
// V3: SwiftVersionedAdditionAttr {{.+}} Implicit 3.0 IsReplacedByActive 0{{$}}
// V3-NEXT: SwiftNameAttr {{.+}} "keylessSlice_NOTES()"
// V3-NOT: SwiftNameAttr

// NOVERSION-LABEL: Dumping TestGenericDUMP
// NOVERSION: SwiftVersionedAdditionAttr {{.+}} Implicit 3.0 0{{$}}
// NOVERSION-NEXT: SwiftImportAsNonGenericAttr

// V3-LABEL: Dumping TestGenericDUMP
// V3: SwiftImportAsNonGenericAttr

// A versioned slice that names a declaration the unversioned slice does not
// gets a removal wrapper, so the name stays scoped to that version rather than
// leaking to every later one.
// NOVERSION-LABEL: Dumping Swift3RenamedOnlyDUMP
// NOVERSION: SwiftVersionedAdditionAttr {{.+}} Implicit 3.0 0{{$}}
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "SpecialSwift3Name"

// V3-LABEL: Dumping Swift3RenamedOnlyDUMP
// V3: SwiftVersionedRemovalAttr {{.+}} Implicit 3.0 {{[0-9]+}} IsReplacedByActive 0{{$}}
// V3-NEXT: SwiftNameAttr {{.+}} "SpecialSwift3Name"

// NOVERSION-LABEL: Dumping Swift3RenamedAlsoDUMP
// NOVERSION: SwiftNameAttr {{.+}} "Swift4Name"
// NOVERSION-NEXT: SwiftVersionedAdditionAttr {{.+}} Implicit 3.0 0{{$}}
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "SpecialSwift3Also"

// V3-LABEL: Dumping Swift3RenamedAlsoDUMP
// V3: SwiftVersionedAdditionAttr {{.+}} Implicit 3.0 IsReplacedByActive 0{{$}}
// V3-NEXT: SwiftNameAttr {{.+}} "Swift4Name"
// V3-NEXT: SwiftNameAttr {{.+}} "SpecialSwift3Also"

// NOVERSION-LABEL: Dumping Swift4RenamedDUMP
// NOVERSION: SwiftVersionedAdditionAttr {{.+}} Implicit 4 0{{$}}
// NOVERSION-NEXT: SwiftNameAttr {{.+}} "SpecialSwift4Name"

// V3-LABEL: Dumping Swift4RenamedDUMP
// V3: SwiftVersionedRemovalAttr {{.+}} Implicit 4 {{[0-9]+}} IsReplacedByActive 0{{$}}
// V3-NEXT: SwiftNameAttr {{.+}} "SpecialSwift4Name"
