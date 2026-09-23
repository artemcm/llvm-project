// Redeclarations of declarations whose API notes were captured, compared with
// the default mode. A redeclaration inherits what the default mode lets it
// inherit and no more: each inherited slice group contributes only its winner,
// by the rules attribute inheritance applies to the attribute that winner
// leaves live.
//
//   rdSame       the same module declares it twice, so the second declaration
//                has its own copy of the notes and inherits the first's too.
//   rdSafety     an inherited swift_attr joins one of the heir's own, as
//                mergeAttrAttr allows.
//   rdPrivate    the owner's notes remove swift_private from the owner, and
//                that must not remove the heir's own, written in its header.
//   RdWrap       the heir's own swift_wrapper wins over the inherited one.
//   RdTag        a tag inherits before its own lookup runs, so its own slices
//                apply after, and over, what it inherits.
//   RdSub, RdConf  an override or protocol implementation does not inherit
//                unavailability, so these calls compile.
//   rdConflict   the heir's own Swift name agrees with the owner's only below
//                Swift 4. The default mode diagnoses the conflict at 4; a
//                capture-mode module serves every version, so it always does.
//
// A leftover wrapper's slice group is not checked: a tag's own groups number
// above the ones it inherited, and after the collapse the number means nothing.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default4 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -ast-dump -ast-dump-filter rd -x objective-c | FileCheck --check-prefix=V4 %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture4 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -ast-dump -ast-dump-filter rd -x objective-c | FileCheck --check-prefix=V4 --implicit-check-not=SwiftVersionedSliceAttr %s
// RUN: %clang_cc1 -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default5 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -ast-dump -ast-dump-filter rd -x objective-c | FileCheck --check-prefix=V5 %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture5 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -ast-dump -ast-dump-filter rd -x objective-c | FileCheck --check-prefix=V5 --implicit-check-not=SwiftVersionedSliceAttr %s

// RUN: %clang_cc1 -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default4 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -ast-dump -ast-dump-filter Rd -x objective-c | FileCheck --check-prefix=TYPES4 %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture4 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -ast-dump -ast-dump-filter Rd -x objective-c | FileCheck --check-prefix=TYPES4 --implicit-check-not=SwiftVersionedSliceAttr %s
// RUN: %clang_cc1 -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default5 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -ast-dump -ast-dump-filter Rd -x objective-c | FileCheck --check-prefix=TYPES5 %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture5 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -ast-dump -ast-dump-filter Rd -x objective-c | FileCheck --check-prefix=TYPES5 --implicit-check-not=SwiftVersionedSliceAttr %s

// RUN: %clang_cc1 -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default5 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fsyntax-only -verify -x objective-c
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture5 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fsyntax-only -verify -x objective-c

// RUN: %clang_cc1 -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/conflict-default5 -fdisable-module-hash -fapinotes-modules -I %t %t/conflict.m -fsyntax-only -x objective-c
// RUN: not %clang_cc1 -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/conflict-default4 -fdisable-module-hash -fapinotes-modules -I %t %t/conflict.m -fsyntax-only -x objective-c 2>&1 | FileCheck --check-prefix=CONFLICT %s
// RUN: not %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/conflict-capture5 -fdisable-module-hash -fapinotes-modules -I %t %t/conflict.m -fsyntax-only -x objective-c 2>&1 | FileCheck --check-prefix=CONFLICT %s
// CONFLICT: error: 'swift_name' and 'swift_name' attributes are not compatible

// V4: Dumping rdSame:
// V4: Dumping rdSame:
// V4-NEXT: FunctionDecl {{.*}} prev {{.*}} rdSame
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "sameV4()"
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: SwiftNameAttr {{.*}} "sameUnversioned()"
// V4-EMPTY:
// V4: Dumping rdSafety:
// V4-NEXT: FunctionDecl {{.*}} imported in RdHeir rdSafety
// V4-NEXT: SwiftAttrAttr {{.*}} Inherited "safe"
// V4-NEXT: SwiftAttrAttr {{.*}} "unsafe"
// V4-EMPTY:
// V4-NEXT: Dumping rdPrivate:
// V4-NEXT: FunctionDecl {{.*}} imported in RdHeir rdPrivate
// V4-NEXT: SwiftPrivateAttr
// V4-EMPTY:
// V4-NEXT: Dumping RdSub::rdUnavail:
// V4-NEXT: ObjCMethodDecl {{.*}} rdUnavail
// V4-EMPTY:
// V4-NEXT: Dumping RdSub::rdNonSwift:
// V4-NEXT: ObjCMethodDecl {{.*}} rdNonSwift
// V4-EMPTY:
// V4-NEXT: Dumping RdConf::rdProtoReq:
// V4-NEXT: ObjCMethodDecl {{.*}} rdProtoReq
// V4-EMPTY:

// V5: Dumping rdSame:
// V5: Dumping rdSame:
// V5-NEXT: FunctionDecl {{.*}} prev {{.*}} rdSame
// V5-NEXT: SwiftNameAttr {{.*}} Inherited "sameUnversioned()"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 0{{$}}
// V5-NEXT: SwiftNameAttr {{.*}} "sameV4()"
// V5-EMPTY:
// V5: Dumping rdSafety:
// V5-NEXT: FunctionDecl {{.*}} imported in RdHeir rdSafety
// V5-NEXT: SwiftAttrAttr {{.*}} Inherited "safe"
// V5-NEXT: SwiftAttrAttr {{.*}} "unsafe"
// V5-EMPTY:
// V5-NEXT: Dumping rdPrivate:
// V5-NEXT: FunctionDecl {{.*}} imported in RdHeir rdPrivate
// V5-NEXT: SwiftPrivateAttr
// V5-EMPTY:
// V5-NEXT: Dumping RdSub::rdUnavail:
// V5-NEXT: ObjCMethodDecl {{.*}} rdUnavail
// V5-EMPTY:

// TYPES4: Dumping RdWrap:
// TYPES4: Dumping RdWrap:
// TYPES4-NEXT: TypedefDecl {{.*}} imported in RdHeir RdWrap
// TYPES4-NEXT: BuiltinType
// TYPES4-NEXT: SwiftNewTypeAttr {{.*}} NK_Enum
// TYPES4-EMPTY:
// TYPES4: Dumping RdTag:
// TYPES4-NEXT: RecordDecl {{.*}} imported in RdHeir {{.*}}struct RdTag definition
// TYPES4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 5.0 IsReplacedByActive {{[0-9]+}}{{$}}
// TYPES4-NEXT: SwiftNameAttr {{.*}} "HeirUnversioned"
// TYPES4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 5.0 IsReplacedByActive {{[0-9]+}}{{$}}
// TYPES4-NEXT: SwiftNameAttr {{.*}} Inherited "OwnerV4"
// TYPES4-NEXT: SwiftNameAttr {{.*}} "HeirFive"
// TYPES4-EMPTY:

// TYPES5: Dumping RdTag:
// TYPES5: Dumping RdTag:
// TYPES5-NEXT: RecordDecl {{.*}} imported in RdHeir {{.*}}struct RdTag definition
// TYPES5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 5.0 IsReplacedByActive {{[0-9]+}}{{$}}
// TYPES5-NEXT: SwiftNameAttr {{.*}} "HeirUnversioned"
// TYPES5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 5.0 IsReplacedByActive {{[0-9]+}}{{$}}
// TYPES5-NEXT: SwiftNameAttr {{.*}} Inherited "OwnerUnversioned"
// TYPES5-NEXT: SwiftNameAttr {{.*}} "HeirFive"
// TYPES5-EMPTY:

//--- module.modulemap
module RdOwner { header "RdOwner.h" header "RdOwnerAgain.h" export * }
module RdHeir { header "RdHeir.h" export * }
module RdConflict { header "RdConflict.h" export * }

//--- RdOwner.h
void rdSame(void);
typedef int RdWrap;
void rdSafety(void);
void rdPrivate(void) __attribute__((swift_private));
void rdConflict(void);
struct RdTag;
__attribute__((objc_root_class))
@interface RdBase
- (void)rdUnavail;
- (void)rdNonSwift;
@end
@protocol RdProto
- (void)rdProtoReq;
@end

//--- RdOwnerAgain.h
void rdSame(void);

//--- RdOwner.apinotes
Name: RdOwner
Functions:
  - Name: rdSame
    SwiftName: 'sameUnversioned()'
  - Name: rdSafety
    SwiftSafety: safe
  - Name: rdPrivate
    SwiftPrivate: false
  - Name: rdConflict
    SwiftName: 'ownerName()'
Typedefs:
  - Name: RdWrap
    SwiftWrapper: struct
Tags:
  - Name: RdTag
    SwiftName: OwnerUnversioned
Classes:
  - Name: RdBase
    Methods:
      - Selector: rdUnavail
        MethodKind: Instance
        Availability: none
      - Selector: rdNonSwift
        MethodKind: Instance
        Availability: nonswift
Protocols:
  - Name: RdProto
    Methods:
      - Selector: rdProtoReq
        MethodKind: Instance
        Availability: none
SwiftVersions:
  - Version: 4.0
    Functions:
      - Name: rdSame
        SwiftName: 'sameV4()'
      - Name: rdConflict
        SwiftName: 'ownerFour()'
    Tags:
      - Name: RdTag
        SwiftName: OwnerV4

//--- RdHeir.h
#include "RdOwner.h"
typedef int RdWrap;
void rdSafety(void);
void rdPrivate(void) __attribute__((swift_private));
struct RdTag {
  int x;
};
@interface RdSub : RdBase
- (void)rdUnavail;
- (void)rdNonSwift;
@end
@interface RdConf : RdBase <RdProto>
- (void)rdProtoReq;
@end

//--- RdHeir.apinotes
Name: RdHeir
Functions:
  - Name: rdSafety
    SwiftSafety: unsafe
Typedefs:
  - Name: RdWrap
    SwiftWrapper: enum
Tags:
  - Name: RdTag
    SwiftName: HeirUnversioned
SwiftVersions:
  - Version: 5.0
    Tags:
      - Name: RdTag
        SwiftName: HeirFive

//--- RdConflict.h
#include "RdOwner.h"
void rdConflict(void);

//--- RdConflict.apinotes
Name: RdConflict
Functions:
  - Name: rdConflict
    SwiftName: 'ownerName()'

//--- use.m
// expected-no-diagnostics
#include "RdHeir.h"
void use(RdSub *sub, RdConf *conf) {
  rdSame();
  [sub rdUnavail];
  [conf rdProtoReq];
}

//--- conflict.m
#include "RdConflict.h"
