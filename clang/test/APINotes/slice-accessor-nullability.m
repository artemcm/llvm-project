// An implicit accessor's nullability comes from its property's, which API
// notes replace. A capture-mode producer synthesized the accessor from the
// property as written, a null_resettable one's included, so the collapse
// replaces what that synthesis derived rather than adding to it.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump -ast-dump-filter Reset -x objective-c | FileCheck %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump -ast-dump-filter Reset -x objective-c | FileCheck --implicit-check-not=SwiftVersionedSliceAttr %s

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

//--- module.modulemap
module Acc { header "Acc.h" export * }

//--- Acc.h
__attribute__((objc_root_class))
@interface AccObj
@property (null_resettable) id arReset;
@property (null_resettable) id arResetOpt;
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

//--- use.m
@import Acc;
