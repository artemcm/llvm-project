// An Objective-C method merges with each method it overrides or implements,
// and an @implementation with its @interface, once their API notes apply.
//
//   newOvThing  OvBase's notes take away its header's ns_returns_not_retained,
//               so OvBase infers ns_returns_retained, which OvSub inherits
//               before its own notes' swift_private; its own inference then
//               adds nothing.
//   ovPriv      OvMSub merges with its superclass's method, whose notes give
//               swift_private, and with its protocol's, whose notes take the
//               header's away.
//   ivImplRet   an @implementation in another module takes its @interface's
//               result nullability, which the interface's notes give. Its
//               body is type-checked against the notes unapplied: design.md
//               Limits.

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -fobjc-arc -I %t %t/use.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump-all -ast-dump-filter Ov -x objective-c | FileCheck --check-prefixes=CHECK,V4 --allow-unused-prefixes %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -fobjc-arc -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump-all -ast-dump-filter Ov -x objective-c | FileCheck --check-prefixes=CHECK,V4 --allow-unused-prefixes --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -fobjc-arc -I %t %t/use.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump-all -ast-dump-filter Ov -x objective-c | FileCheck --check-prefixes=CHECK,V5 --allow-unused-prefixes %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -fobjc-arc -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump-all -ast-dump-filter Ov -x objective-c | FileCheck --check-prefixes=CHECK,V5 --allow-unused-prefixes --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -fobjc-arc -I %t %t/use.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump-all -ast-dump-filter IvImpl -x objective-c | FileCheck --check-prefix=IMPL %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -fobjc-arc -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump-all -ast-dump-filter IvImpl -x objective-c | FileCheck --check-prefix=IMPL %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -fobjc-arc -I %t %t/use.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/ir-default -emit-llvm -o - -x objective-c | FileCheck --check-prefix=IR %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -fobjc-arc -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/ir-capture -emit-llvm -o - -x objective-c | FileCheck --check-prefix=IR %s

// CHECK:      Dumping OvBase:
// CHECK-NEXT: ObjCInterfaceDecl {{.*}} imported in OA {{.*}} OvBase
// CHECK-NEXT: ObjCRootClassAttr {{.*}}
// CHECK-NEXT: ObjCMethodDecl {{.*}} imported in OA - newOvThing 'id'
// CHECK-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 0 IsReplacedByActive 0
// CHECK-NEXT: NSReturnsNotRetainedAttr {{.*}}
// CHECK-NEXT: NSReturnsRetainedAttr {{.*}} Implicit
// CHECK-EMPTY:
// CHECK-NEXT: Dumping OvSub:
// CHECK-NEXT: ObjCInterfaceDecl {{.*}} imported in OB {{.*}} OvSub
// CHECK-NEXT: super ObjCInterface {{.*}} 'OvBase'
// CHECK-NEXT: ObjCMethodDecl {{.*}} imported in OB - newOvThing 'id'
// CHECK-NEXT: NSReturnsRetainedAttr {{.*}} Inherited Implicit
// CHECK-NEXT: SwiftPrivateAttr {{.*}}
// CHECK-EMPTY:
// CHECK-NEXT: Dumping OvMBase:
// CHECK-NEXT: ObjCInterfaceDecl {{.*}} imported in MKit {{.*}} OvMBase
// CHECK-NEXT: ObjCRootClassAttr {{.*}}
// CHECK-NEXT: ObjCMethodDecl {{.*}} imported in MKit - ovPriv 'void'
// CHECK-NEXT: SwiftPrivateAttr {{.*}}
// CHECK-EMPTY:
// CHECK-NEXT: Dumping OvMProto:
// CHECK-NEXT: ObjCProtocolDecl {{.*}} imported in MKit {{.*}} OvMProto
// CHECK-NEXT: ObjCMethodDecl {{.*}} imported in MKit - ovPriv 'void'
// CHECK-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 0 IsReplacedByActive 0
// CHECK-NEXT: SwiftPrivateAttr {{.*}}
// CHECK-EMPTY:
// CHECK-NEXT: Dumping OvMSub:
// CHECK-NEXT: ObjCInterfaceDecl {{.*}} imported in MSub {{.*}} OvMSub
// CHECK-NEXT: super ObjCInterface {{.*}} 'OvMBase'
// CHECK-NEXT: ObjCProtocol {{.*}} 'OvMProto'
// CHECK-NEXT: ObjCMethodDecl {{.*}} imported in MSub - ovPriv 'void'
// CHECK-NEXT: SwiftPrivateAttr {{.*}} Inherited
// CHECK-EMPTY:

// IMPL:      ObjCImplementationDecl {{.*}} imported in OJ IvImpl
// IMPL-NEXT: ObjCInterface {{.*}} 'IvImpl'
// IMPL-NEXT: ObjCMethodDecl {{.*}} imported in OJ - ivImplRet 'id _Nonnull':'id'

// IR-LABEL: define ptr @useSub(
// IR:       %call = call ptr @objc_msgSend(
// IR-NEXT:  call void @llvm.objc.storeStrong(ptr %s.addr, ptr null)
// IR-NEXT:  tail call ptr @llvm.objc.autoreleaseReturnValue(ptr %call)
// IR-LABEL: define ptr @useBase(
// IR:       %call = call ptr @objc_msgSend(
// IR-NEXT:  call void @llvm.objc.storeStrong(ptr %b.addr, ptr null)
// IR-NEXT:  tail call ptr @llvm.objc.autoreleaseReturnValue(ptr %call)

//--- module.modulemap
module OA { header "OA.h" export * }
module OB { header "OB.h" export * }
module MKit { header "MKit.h" export * }
module MSub { header "MSub.h" export * }
module OI { header "OI.h" export * }
module OJ { header "OJ.h" export * }

//--- OA.h
__attribute__((objc_root_class))
@interface OvBase
- (id)newOvThing __attribute__((ns_returns_not_retained));
@end

//--- OA.apinotes
Name: OA
Classes:
- Name: OvBase
  Methods:
  - Selector: newOvThing
    MethodKind: Instance
    RetainCountConvention: none

//--- OB.h
#include "OA.h"
@interface OvSub : OvBase
- (id)newOvThing;
@end

//--- OB.apinotes
Name: OB
Classes:
- Name: OvSub
  Methods:
  - Selector: newOvThing
    MethodKind: Instance
    SwiftPrivate: true

//--- MKit.h
__attribute__((objc_root_class))
@interface OvMBase
- (void)ovPriv;
@end
@protocol OvMProto
- (void)ovPriv __attribute__((swift_private));
@end

//--- MKit.apinotes
Name: MKit
Classes:
  - Name: OvMBase
    Methods:
      - Selector: ovPriv
        MethodKind: Instance
        SwiftPrivate: true
Protocols:
  - Name: OvMProto
    Methods:
      - Selector: ovPriv
        MethodKind: Instance
        SwiftPrivate: false

//--- MSub.h
#include "MKit.h"
@interface OvMSub : OvMBase <OvMProto>
- (void)ovPriv;
@end

//--- OI.h
__attribute__((objc_root_class))
@interface IvImpl
- (id)ivImplRet;
@end

//--- OJ.h
#include "OI.h"
@implementation IvImpl
- (id)ivImplRet { return 0; }
@end

//--- OI.apinotes
Name: OI
Classes:
- Name: IvImpl
  Methods:
  - Selector: ivImplRet
    MethodKind: Instance
    NullabilityOfRet: N

//--- use.m
#include "OB.h"
#include "MSub.h"
#include "OJ.h"
id useSub(OvSub *s) { return [s newOvThing]; }
id useBase(OvBase *b) { return [b newOvThing]; }
void usePriv(OvMSub *s) { [s ovPriv]; }
