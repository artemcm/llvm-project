// A redeclaration, override or protocol implementation applies its own API
// notes first, and only then inherits what its previous declaration has live:
// that declaration's attributes as written, then its notes' winners, then what
// Sema inferred once they had applied. A capture-mode consumer collapses in
// that order.
//
//   rdTake      inherits the notes' cf_returns_not_retained, and then the
//               cf_audited_transfer its audited previous declaration inferred.
//   rdRetained  the override's 'RetainCountConvention: none' has nothing of
//               its own to remove, so it still inherits ns_returns_retained.
//   rdTakes:    an override doesn't take its overridden method's parameter
//               nullability; only a redeclaration's type merge would...
//   rdImpl:     ...as an @implementation's does, whether its own notes give
//   rdTakes:    the nullability too or, in G2Impl's @implementation of
//               G2Root's method, it comes from the method it implements.
//   rdMulti     G2Proto's 'SwiftPrivate: false' removes nothing G2Sub gets
//               from G2Root.
//   setRdThing: the implicit setter's own lookup, whose Swift 4 slice wins at
//               4, is a group apart from the property nullability its
//               parameter received.
//
// Sema infers an ARC method family's ns_returns_retained only after the
// method has inherited, so an inherited convention suppresses it:
//
//   newRdThing  inherits ns_returns_not_retained, and infers nothing.
//   initRdThing inherits the inferred ns_returns_retained, and infers no copy.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump -ast-dump-filter rd -x objective-c | FileCheck %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump -ast-dump-filter rd -x objective-c | FileCheck --implicit-check-not=SwiftVersionedSliceAttr %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump -ast-dump-filter setRdThing -x objective-c | FileCheck --check-prefix=SETTER %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump -ast-dump-filter setRdThing -x objective-c | FileCheck --check-prefix=SETTER --implicit-check-not=SwiftVersionedSliceAttr %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump -ast-dump-filter RdThing -x objective-c | FileCheck --check-prefix=INFER %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump -ast-dump-filter RdThing -x objective-c | FileCheck --check-prefix=INFER %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump-all -ast-dump-filter G2Impl -x objective-c | FileCheck --check-prefix=IMPL %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump-all -ast-dump-filter G2Impl -x objective-c | FileCheck --check-prefix=IMPL %s

// CHECK:      FunctionDecl {{.*}} prev {{.*}} imported in G2Sub rdTake
// CHECK-NEXT: ParmVarDecl
// CHECK-NEXT: CFReturnsNotRetainedAttr {{.*}} Inherited
// CHECK-NEXT: SwiftNameAttr {{.*}} Inherited "take(_:)"
// CHECK-NEXT: CFAuditedTransferAttr {{.*}} Inherited Implicit
// CHECK-EMPTY:
// CHECK-NEXT: Dumping G2Sub::rdRetained:
// CHECK-NEXT: ObjCMethodDecl {{.*}} rdRetained 'id'
// CHECK-NEXT: NSReturnsRetainedAttr {{.*}} Inherited
// CHECK-EMPTY:
// CHECK-NEXT: Dumping G2Sub::rdTakes::
// CHECK-NEXT: ObjCMethodDecl {{.*}} rdTakes: 'void'
// CHECK-NEXT: ParmVarDecl {{.*}} x '__strong id' destroyed
// CHECK-EMPTY:
// CHECK-NEXT: Dumping G2Sub::rdMulti:
// CHECK-NEXT: ObjCMethodDecl {{.*}} rdMulti 'void'
// CHECK-NEXT: SwiftPrivateAttr {{.*}} Inherited
// CHECK-EMPTY:

// IMPL:      Dumping G2Impl:
// IMPL:      Dumping G2Impl:
// IMPL:      ObjCMethodDecl {{.*}} rdImpl: 'void'
// IMPL:      ParmVarDecl {{.*}} x '__strong id _Nonnull':'__strong id'
// IMPL:      ObjCMethodDecl {{.*}} rdTakes: 'void'
// IMPL:      ParmVarDecl {{.*}} x '__strong id _Nonnull':'__strong id'

// INFER:      Dumping G2Sub::newRdThing:
// INFER-NEXT: ObjCMethodDecl {{.*}} newRdThing 'id'
// INFER-NEXT: NSReturnsNotRetainedAttr {{.*}} Inherited
// INFER-NEXT: SwiftNameAttr {{.*}} "makeThing()"
// INFER-EMPTY:
// INFER-NEXT: Dumping G2Sub::initRdThing:
// INFER-NEXT: ObjCMethodDecl {{.*}} initRdThing 'instancetype':'id'
// INFER-NEXT: NSConsumesSelfAttr {{.*}} Inherited Implicit
// INFER-NEXT: NSReturnsRetainedAttr {{.*}} Inherited Implicit
// INFER-NEXT: SwiftNameAttr {{.*}} "init(rd:)"
// INFER-NEXT: NSConsumesSelfAttr {{.*}} Implicit
// INFER-EMPTY:

// SETTER:      Dumping G2Root::setRdThing::
// SETTER-NEXT: ObjCMethodDecl {{.*}} setRdThing: 'void'
// SETTER-NEXT: ParmVarDecl {{.*}} rdThing 'G2Root * _Nullable':'G2Root *'
// SETTER-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 IsReplacedByActive {{[0-9]+}}{{$}}
// SETTER-NEXT: SwiftNameAttr {{.*}} "setNewThing(_:)"
// SETTER-NEXT: SwiftNameAttr {{.*}} "setOldThing(_:)"
// SETTER-EMPTY:

//--- module.modulemap
module G2Base { header "G2Base.h" export * }
module G2Sub { header "G2Sub.h" export * }

//--- G2Base.h
typedef const struct __attribute__((objc_bridge(id))) __G2Thing *G2ThingRef;
#pragma clang arc_cf_code_audited begin
G2ThingRef rdTake(G2ThingRef x);
#pragma clang arc_cf_code_audited end
@protocol G2Proto
- (void)rdMulti;
@end
__attribute__((objc_root_class))
@interface G2Root
- (id)rdRetained __attribute__((ns_returns_retained));
- (void)rdTakes:(id)x;
- (void)rdMulti;
@property (nonatomic) G2Root *rdThing;
- (id)newRdThing;
- (instancetype)initRdThing;
@end

//--- G2Base.apinotes
Name: G2Base
Functions:
- Name: rdTake
  RetainCountConvention: CFReturnsNotRetained
  SwiftName: 'take(_:)'
Protocols:
- Name: G2Proto
  Methods:
  - Selector: rdMulti
    MethodKind: Instance
    SwiftPrivate: false
Classes:
- Name: G2Root
  Methods:
  - Selector: 'rdTakes:'
    MethodKind: Instance
    Parameters:
    - Position: 0
      Nullability: N
  - Selector: rdMulti
    MethodKind: Instance
    SwiftPrivate: true
  - Selector: 'setRdThing:'
    MethodKind: Instance
    SwiftName: 'setNewThing(_:)'
  - Selector: newRdThing
    MethodKind: Instance
    RetainCountConvention: NSReturnsNotRetained
  Properties:
  - Name: rdThing
    PropertyKind: Instance
    Nullability: O
SwiftVersions:
- Version: 4
  Classes:
  - Name: G2Root
    Methods:
    - Selector: 'setRdThing:'
      MethodKind: Instance
      SwiftName: 'setOldThing(_:)'

//--- G2Sub.h
#include "G2Base.h"
G2ThingRef rdTake(G2ThingRef x);
@interface G2Sub : G2Root <G2Proto>
- (id)rdRetained;
- (void)rdTakes:(id)x;
- (void)rdMulti;
- (id)newRdThing;
- (instancetype)initRdThing;
@end
@interface G2Impl : G2Root
- (void)rdImpl:(id)x;
@end
@implementation G2Impl
- (void)rdImpl:(id)x {}
- (void)rdTakes:(id)x {}
@end

//--- G2Sub.apinotes
Name: G2Sub
Classes:
- Name: G2Sub
  Methods:
  - Selector: rdRetained
    MethodKind: Instance
    RetainCountConvention: none
  - Selector: newRdThing
    MethodKind: Instance
    SwiftName: 'makeThing()'
  - Selector: initRdThing
    MethodKind: Instance
    SwiftName: 'init(rd:)'
- Name: G2Impl
  Methods:
  - Selector: 'rdImpl:'
    MethodKind: Instance
    Parameters:
    - Position: 0
      Nullability: N

//--- use.m
#include "G2Sub.h"
