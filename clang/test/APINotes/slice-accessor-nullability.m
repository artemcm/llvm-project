// An implicit accessor's nullability comes from its property's, which API
// notes replace. A capture-mode producer synthesized the accessor from the
// property as written, a null_resettable one's included, so the collapse
// replaces what that synthesis derived rather than adding to it.
//
// Synthesis derives the accessor's type once, from the property after its
// notes apply, and a null_resettable property's getter returns nonnull and its
// setter takes nullable in place of unspecified. Nullability from API notes
// makes the property null_resettable only if it changes the property's type:
//
//   unspecProbe       null_unspecified with 'Nullability: U' changes nothing,
//                     so the accessors stay unspecified.
//   resetTypeProbe    null_resettable as written, with an unspecified 'Type:'.
//   plainUnspecProbe  'Nullability: U' on a property with none.
//   typeNullProbe     'Type:' and 'Nullability: U' together.
//   verProbe          nonnull, and unspecified at Swift 4.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump -ast-dump-filter Reset -x objective-c | FileCheck %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump -ast-dump-filter Reset -x objective-c | FileCheck --implicit-check-not=SwiftVersionedSliceAttr %s

// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump -ast-dump-filter Probe -x objective-c 2>/dev/null | FileCheck --check-prefixes=PROBE,PROBE4 %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump -ast-dump-filter Probe -x objective-c 2>/dev/null | FileCheck --check-prefixes=PROBE,PROBE4 --implicit-check-not=SwiftVersioned %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump -ast-dump-filter Probe -x objective-c 2>/dev/null | FileCheck --check-prefixes=PROBE,PROBE5 %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump -ast-dump-filter Probe -x objective-c 2>/dev/null | FileCheck --check-prefixes=PROBE,PROBE5 --implicit-check-not=SwiftVersioned %s

// CHECK:      ObjCMethodDecl {{.*}} implicit - arReset 'id _Nonnull':'id'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping AccObj::setArReset::
// CHECK-NEXT: ObjCMethodDecl {{.*}} implicit - setArReset: 'void'
// CHECK-NEXT: ParmVarDecl {{.*}} arReset 'id _Nonnull':'id'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping AccObj::arResetOpt:
// CHECK-NEXT: ObjCMethodDecl {{.*}} implicit - arResetOpt 'id _Nullable':'id'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping AccObj::setArResetOpt::
// CHECK-NEXT: ObjCMethodDecl {{.*}} implicit - setArResetOpt: 'void'
// CHECK-NEXT: ParmVarDecl {{.*}} arResetOpt 'id _Nullable':'id'
// CHECK-EMPTY:

// PROBE:      ObjCMethodDecl {{.*}} implicit - unspecProbe 'id _Null_unspecified':'id'
// PROBE:      ObjCMethodDecl {{.*}} implicit - setUnspecProbe: 'void'
// PROBE-NEXT: ParmVarDecl {{.*}} unspecProbe 'id _Null_unspecified':'id'
// PROBE:      ObjCMethodDecl {{.*}} implicit - resetTypeProbe 'id _Nonnull':'id'
// PROBE:      ObjCMethodDecl {{.*}} implicit - setResetTypeProbe: 'void'
// PROBE-NEXT: ParmVarDecl {{.*}} resetTypeProbe 'id _Nullable':'id'
// PROBE:      ObjCMethodDecl {{.*}} implicit - plainUnspecProbe 'id _Nonnull':'id'
// PROBE:      ObjCMethodDecl {{.*}} implicit - setPlainUnspecProbe: 'void'
// PROBE-NEXT: ParmVarDecl {{.*}} plainUnspecProbe 'id _Nullable':'id'
// PROBE:      ObjCMethodDecl {{.*}} implicit - typeNullProbe 'AccObj * _Nonnull':'AccObj *'
// PROBE:      ObjCMethodDecl {{.*}} implicit - setTypeNullProbe: 'void'
// PROBE-NEXT: ParmVarDecl {{.*}} typeNullProbe 'AccObj * _Nullable':'AccObj *'
// PROBE:      ObjCMethodDecl {{.*}} implicit - verProbe 'id _Nonnull':'id'
// PROBE:      ObjCMethodDecl {{.*}} implicit - setVerProbe: 'void'
// PROBE4-NEXT: ParmVarDecl {{.*}} verProbe 'id _Nullable':'id'
// PROBE5-NEXT: ParmVarDecl {{.*}} verProbe 'id _Nonnull':'id'

//--- module.modulemap
module Acc { header "Acc.h" export * }

//--- Acc.h
__attribute__((objc_root_class))
@interface AccObj
@property (null_resettable) id arReset;
@property (null_resettable) id arResetOpt;
@property (null_unspecified) id unspecProbe;
@property (null_resettable) id resetTypeProbe;
@property id plainUnspecProbe;
@property id typeNullProbe;
@property id verProbe;
@end

//--- Acc.apinotes
Name: Acc
Classes:
- Name: AccObj
  Properties:
  - Name: arReset
    PropertyKind: Instance
    Nullability: N
  - Name: arResetOpt
    PropertyKind: Instance
    Nullability: O
  - Name: unspecProbe
    PropertyKind: Instance
    Nullability: U
  - Name: resetTypeProbe
    PropertyKind: Instance
    Type: 'id _Null_unspecified'
  - Name: plainUnspecProbe
    PropertyKind: Instance
    Nullability: U
  - Name: typeNullProbe
    PropertyKind: Instance
    Type: 'AccObj *'
    Nullability: U
  - Name: verProbe
    PropertyKind: Instance
    Nullability: N
SwiftVersions:
- Version: 4
  Classes:
  - Name: AccObj
    Properties:
    - Name: verProbe
      PropertyKind: Instance
      Nullability: U

//--- use.m
@import Acc;
