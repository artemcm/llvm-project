// The consumer-side collapse: a module built with
// -fswift-version-independent-apinotes carries every API notes slice unapplied,
// and a consumer of it applies the slices for its own Swift version on read.
//
// The bar is the default mode, so every check below runs twice against the same
// expectations: once against a module built and consumed the default way, and
// once against the capture-mode module. The two have to agree, attribute order
// included, because the Swift importer reads the leftover wrappers in order to
// build compatibility aliases.
//
// Fourteen probes, each covering something the collapse has to get right:
//   collapseName     a losing unversioned slice is recorded superseded, and
//                    takes the *winner's* version rather than its own empty
//                    one. That version is what an alias is obsoleted at.
//   collapseSafety   swift_safety displaces a swift_attr carrying a safety
//                    verdict, not the first swift_attr of any kind.
//   collapseUnavail  a group with no winner at all: everything stays wrapped
//                    and no slice markers survive.
//   collapsePrivate  a slice that *removes* a key rather than adding one.
//   collapseRetain   RetainCountConvention: none on a function creates
//                    cf_unknown_transfer, which displaces whichever retain-count
//                    convention the header wrote, not only its own kind.
//   collapseParam    a parameter's notes come from its function's lookup, so
//                    it carries that lookup's markers and collapses with its
//                    function, taking the same selection.
//   collapseMethod   the same for an Objective-C method's parameter.
//   collapseOrder    an attribute API notes add without a wrapper keeps its
//                    place after the wrapped ones applied before it.
//   collapseBad*     a Swift name that Sema rejects adds nothing, but when
//                    selected it still displaces the header's name (BadName),
//                    and it counts as naming the declaration when deciding
//                    whether a versioned name needs an unversioned removal
//                    (BadAlias, BadBase).
//   collapseUbu      UnsafeBufferUsage is skipped once live, so a slice after
//                    the winner leaves nothing, not even a wrapper.
//   collapseSafetyOther  swift_safety passes over an unrelated swift_attr.
//   collapseAvailPlatform  UnavailableInSwift leaves a platform's
//                    availability alone.

// RUN: rm -rf %t && mkdir -p %t

// Build the module both ways. The capture build takes no version; that is the
// point of it.
// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=SliceCollapse -o %t/capture.pcm -fapinotes-modules -fswift-version-independent-apinotes -I %S/Inputs/Headers %S/Inputs/Headers/module.modulemap -x objective-c
// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=SliceCollapse -o %t/default4.pcm -fapinotes-modules -fapinotes-swift-version=4 -I %S/Inputs/Headers %S/Inputs/Headers/module.modulemap -x objective-c
// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=SliceCollapse -o %t/default5.pcm -fapinotes-modules -fapinotes-swift-version=5 -I %S/Inputs/Headers %S/Inputs/Headers/module.modulemap -x objective-c

// Swift 4 selects the versioned slice.
// RUN: %clang_cc1 -fmodules -fmodule-file=SliceCollapse=%t/default4.pcm -fapinotes-modules -fapinotes-swift-version=4 %s -ast-dump -ast-dump-filter collapse -x objective-c | FileCheck --check-prefixes=V4,COMMON %s
// RUN: %clang_cc1 -fmodules -fmodule-file=SliceCollapse=%t/capture.pcm  -fapinotes-modules -fapinotes-swift-version=4 %s -ast-dump -ast-dump-filter collapse -x objective-c | FileCheck --check-prefixes=V4,COMMON --implicit-check-not=SwiftVersionedSliceAttr %s

// A module file named without its module is loaded up front, before Sema
// exists, rather than at the import. The collapse does not depend on Sema.
// RUN: %clang_cc1 -fmodules -fmodule-file=%t/capture.pcm -fapinotes-modules -fapinotes-swift-version=4 %s -ast-dump -ast-dump-filter collapse -x objective-c | FileCheck --check-prefixes=V4,COMMON --implicit-check-not=SwiftVersionedSliceAttr %s

// Swift 5 is above every versioned slice, so the unversioned one wins.
// RUN: %clang_cc1 -fmodules -fmodule-file=SliceCollapse=%t/default5.pcm -fapinotes-modules -fapinotes-swift-version=5 %s -ast-dump -ast-dump-filter collapse -x objective-c | FileCheck --check-prefixes=V5,COMMON %s
// RUN: %clang_cc1 -fmodules -fmodule-file=SliceCollapse=%t/capture.pcm  -fapinotes-modules -fapinotes-swift-version=5 %s -ast-dump -ast-dump-filter collapse -x objective-c | FileCheck --check-prefixes=V5,COMMON --implicit-check-not=SwiftVersionedSliceAttr %s

// No slice markers may survive the collapse, which is what every capture run's
// --implicit-check-not asserts. The default mode never has any.

@import SliceCollapse;
void caller(void) {
  collapseName();
  collapseSafety();
  collapseUnavail();
  collapsePrivate();
  collapseRetain();
  collapseParam(0);
  collapseBadName();
  collapseBadAlias();
  collapseBadBase();
  collapseUbu();
  collapseSafetyOther();
  collapseAvailPlatform();
  (void)sizeof(struct collapseOrder);
  [(CollapseBox *)0 collapseMethod:0];
}

// V4: Dumping collapseName:
// V4: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: SwiftNameAttr {{.*}} "nameUnversioned()"
// V4-NEXT: SwiftNameAttr {{.*}} "nameV4()"

// V4: Dumping collapseSafety:
// V4: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: SwiftAttrAttr {{.*}} "safe"
// V4-NEXT: SwiftAttrAttr {{.*}} "unsafe"

// V4: Dumping collapseUnavail:
// V4: FunctionDecl {{.*}} collapseUnavail
// V4-NEXT: SwiftPrivateAttr

// V4: Dumping collapsePrivate:
// V4: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: SwiftPrivateAttr

// V4: Dumping collapseRetain:
// V4: FunctionDecl {{.*}} collapseRetain
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: NSReturnsRetainedAttr
// V4-NEXT: CFUnknownTransferAttr

// V4: Dumping collapseParam:
// V4: ParmVarDecl {{.*}} p 'int *'
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: NoEscapeAttr
// V4-EMPTY:

// V4: Dumping collapseBadName:
// V4-NEXT: FunctionDecl {{.*}} collapseBadName
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: SwiftNameAttr {{.*}} "header()"
// V4-EMPTY:

// V4: Dumping collapseBadAlias:
// V4-NEXT: FunctionDecl {{.*}} collapseBadAlias
// V4-NEXT: SwiftVersionedRemovalAttr {{.*}} Implicit 4.0 {{[0-9]+}} IsReplacedByActive 0{{$}}
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: SwiftPrivateAttr
// V4-EMPTY:

// V4: Dumping collapseBadBase:
// V4-NEXT: FunctionDecl {{.*}} collapseBadBase
// V4-NEXT: SwiftNameAttr {{.*}} "goodFour()"
// V4-EMPTY:

// V4: Dumping collapseUbu:
// V4-NEXT: FunctionDecl {{.*}} collapseUbu
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: UnsafeBufferUsageAttr
// V4-NEXT: UnsafeBufferUsageAttr
// V4-EMPTY:

// V4: Dumping collapseSafetyOther:
// V4-NEXT: FunctionDecl {{.*}} collapseSafetyOther
// V4-NEXT: SwiftAttrAttr {{.*}} "unrelated"
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: SwiftAttrAttr {{.*}} "safe"
// V4-NEXT: SwiftAttrAttr {{.*}} "unsafe"
// V4-EMPTY:

// V4: Dumping collapseAvailPlatform:
// V4-NEXT: FunctionDecl {{.*}} collapseAvailPlatform
// V4-NEXT: AvailabilityAttr {{.*}} macos 10.10
// V4-NEXT: AvailabilityAttr {{.*}} swift 0 0 0 Unavailable
// V4-EMPTY:

// V5: Dumping collapseName:
// V5: SwiftNameAttr {{.*}} "nameUnversioned()"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 0{{$}}
// V5-NEXT: SwiftNameAttr {{.*}} "nameV4()"

// V5: Dumping collapseSafety:
// V5: SwiftAttrAttr {{.*}} "safe"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 0{{$}}
// V5-NEXT: SwiftAttrAttr {{.*}} "unsafe"

// V5: Dumping collapseUnavail:
// V5: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 0{{$}}
// V5-NEXT: SwiftPrivateAttr

// V5: Dumping collapsePrivate:
// V5: SwiftPrivateAttr
// V5-NEXT: SwiftVersionedRemovalAttr {{.*}} Implicit 4.0 {{[0-9]+}} 0{{$}}

// V5: Dumping collapseRetain:
// V5: FunctionDecl {{.*}} collapseRetain
// V5-NEXT: NSReturnsRetainedAttr
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 0{{$}}
// V5-NEXT: CFUnknownTransferAttr

// V5: Dumping collapseParam:
// V5: ParmVarDecl {{.*}} p 'int *'
// V5-NEXT: NoEscapeAttr
// V5-NEXT: SwiftVersionedRemovalAttr {{.*}} Implicit 4.0 {{[0-9]+}} 0{{$}}
// V5-EMPTY:

// V5: Dumping collapseBadName:
// V5-NEXT: FunctionDecl {{.*}} collapseBadName
// V5-NEXT: SwiftNameAttr {{.*}} "header()"
// V5-EMPTY:

// V5: Dumping collapseBadAlias:
// V5-NEXT: FunctionDecl {{.*}} collapseBadAlias
// V5-NEXT: SwiftPrivateAttr
// V5-EMPTY:

// V5: Dumping collapseBadBase:
// V5-NEXT: FunctionDecl {{.*}} collapseBadBase
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 0{{$}}
// V5-NEXT: SwiftNameAttr {{.*}} "goodFour()"
// V5-EMPTY:

// V5: Dumping collapseUbu:
// V5-NEXT: FunctionDecl {{.*}} collapseUbu
// V5-NEXT: UnsafeBufferUsageAttr
// V5-EMPTY:

// V5: Dumping collapseSafetyOther:
// V5-NEXT: FunctionDecl {{.*}} collapseSafetyOther
// V5-NEXT: SwiftAttrAttr {{.*}} "unrelated"
// V5-NEXT: SwiftAttrAttr {{.*}} "safe"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 0{{$}}
// V5-NEXT: SwiftAttrAttr {{.*}} "unsafe"
// V5-EMPTY:

// V5: Dumping collapseAvailPlatform:
// V5-NEXT: FunctionDecl {{.*}} collapseAvailPlatform
// V5-NEXT: AvailabilityAttr {{.*}} macos 10.10
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 0{{$}}
// V5-NEXT: AvailabilityAttr {{.*}} swift 0 0 0 Unavailable
// V5-EMPTY:

// The last two probes read the same at both versions.

// COMMON: Dumping collapseOrder:
// COMMON-NEXT: RecordDecl {{.*}} struct collapseOrder definition
// COMMON-NEXT: SwiftBridgeAttr {{.*}} "Bridged"
// COMMON-NEXT: SwiftAttrAttr {{.*}} "conforms_to:Swift.Equatable"

// COMMON: Dumping CollapseBox::collapseMethod::
// COMMON-NEXT: ObjCMethodDecl {{.*}} - collapseMethod: 'void'
// COMMON-NEXT: ParmVarDecl {{.*}} p 'int *'
// COMMON-NEXT: NoEscapeAttr
// COMMON-EMPTY:
