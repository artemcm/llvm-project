// A category method whose selector the global method pool has already seen is
// merged with itself: the override search reaches the category through its
// class and finds the method it started from. Capturing what that merge
// inherits must not grow the attribute list it is reading, and the result is
// what the default mode gives: its own Swift name, re-added as inherited.
//
//   selfName   SwiftName and SwiftPrivate at three versions and unversioned.
//   selfParam: a parameter with versioned nullability, merged with itself too.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump -ast-dump-filter self -x objective-c | FileCheck --check-prefix=V4 %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump -ast-dump-filter self -x objective-c | FileCheck --check-prefix=V4 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftNullabilityAttr %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump -ast-dump-filter self -x objective-c | FileCheck --check-prefix=V5 %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump -ast-dump-filter self -x objective-c | FileCheck --check-prefix=V5 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftNullabilityAttr %s

// V4: Dumping SelfOther::selfName:
// V4-NEXT: ObjCMethodDecl {{.*}} selfName 'void'
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "renamed4()"
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 IsReplacedByActive 0{{$}}
// V4-NEXT: AvailabilityAttr {{.*}} swift 0 0 0 Unavailable "no" "" 0
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 IsReplacedByActive 0{{$}}
// V4-NEXT: SwiftNameAttr {{.*}} "renamed()"
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 3 0{{$}}
// V4-NEXT: SwiftPrivateAttr
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 3 0{{$}}
// V4-NEXT: SwiftNameAttr {{.*}} "renamed3()"
// V4-NEXT: SwiftPrivateAttr
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.2 0{{$}}
// V4-NEXT: SwiftPrivateAttr
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.2 0{{$}}
// V4-NEXT: SwiftNameAttr {{.*}} "renamed42()"
// V4-EMPTY:
// V4-NEXT: Dumping SelfOther::selfParam::
// V4-NEXT: ObjCMethodDecl {{.*}} selfParam: 'void'
// V4-NEXT: ParmVarDecl {{.*}} x 'id _Nullable':'id'
// V4-EMPTY:

// V5: Dumping SelfOther::selfName:
// V5-NEXT: ObjCMethodDecl {{.*}} selfName 'void'
// V5-NEXT: SwiftNameAttr {{.*}} Inherited "renamed()"
// V5-NEXT: AvailabilityAttr {{.*}} swift 0 0 0 Unavailable "no" "" 0
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 3 0{{$}}
// V5-NEXT: SwiftPrivateAttr
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 3 0{{$}}
// V5-NEXT: SwiftNameAttr {{.*}} "renamed3()"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 0{{$}}
// V5-NEXT: SwiftPrivateAttr
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 0{{$}}
// V5-NEXT: SwiftNameAttr {{.*}} "renamed4()"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.2 0{{$}}
// V5-NEXT: SwiftPrivateAttr
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.2 0{{$}}
// V5-NEXT: SwiftNameAttr {{.*}} "renamed42()"
// V5-EMPTY:
// V5-NEXT: Dumping SelfOther::selfParam::
// V5-NEXT: ObjCMethodDecl {{.*}} selfParam: 'void'
// V5-NEXT: ParmVarDecl {{.*}} x 'id _Nonnull':'id'
// V5-EMPTY:

//--- module.modulemap
module SelfMerge { header "SelfMerge.h" export * }

//--- SelfMerge.h
@interface SelfBase
- (void)selfName;
- (void)selfParam:(id)x;
@end
@interface SelfOther
@end
@interface SelfOther (Cat)
- (void)selfName;
- (void)selfParam:(id)x;
@end

//--- SelfMerge.apinotes
Name: SelfMerge
Classes:
- Name: SelfOther
  Methods:
  - Selector: selfName
    MethodKind: Instance
    SwiftName: 'renamed()'
    Availability: nonswift
    AvailabilityMsg: 'no'
  - Selector: 'selfParam:'
    MethodKind: Instance
    Parameters:
    - Position: 0
      Nullability: N
SwiftVersions:
- Version: 3
  Classes:
  - Name: SelfOther
    Methods:
    - Selector: selfName
      MethodKind: Instance
      SwiftName: 'renamed3()'
      SwiftPrivate: true
- Version: 4
  Classes:
  - Name: SelfOther
    Methods:
    - Selector: selfName
      MethodKind: Instance
      SwiftName: 'renamed4()'
      SwiftPrivate: true
    - Selector: 'selfParam:'
      MethodKind: Instance
      Parameters:
      - Position: 0
        Nullability: O
- Version: 4.2
  Classes:
  - Name: SelfOther
    Methods:
    - Selector: selfName
      MethodKind: Instance
      SwiftName: 'renamed42()'
      SwiftPrivate: true

//--- use.m
@import SelfMerge;
