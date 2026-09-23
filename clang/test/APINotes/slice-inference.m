// Attributes Sema infers after API notes apply, unless one that API notes can
// add or remove is present. A capture-mode module has applied no API notes when
// the inference runs, so its consumer has to decide again once it has, in
// both directions.
//
//   newProbe        a new-family method under ARC is inferred to return
//                   retained, unless the 4.0 slice says it does not.
//   newHeaderProbe  the header says not-retained, and the 4.0 slice removes
//                   that, so at 4 the inference the producer skipped happens.
//   auditedProbe    inside an audited region a function is inferred to be
//                   audited, unless the 4.0 slice makes it cf_unknown_transfer.
//
// Everything runs against both modes. The IR checks are the ones that matter:
// a caller retains or not by the attributes it sees.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default4 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -ast-dump -ast-dump-filter Probe -x objective-c | FileCheck --check-prefix=V4 %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture4 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -ast-dump -ast-dump-filter Probe -x objective-c | FileCheck --check-prefix=V4 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedInferenceAttr %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default5 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -ast-dump -ast-dump-filter Probe -x objective-c | FileCheck --check-prefix=V5 %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture5 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -ast-dump -ast-dump-filter Probe -x objective-c | FileCheck --check-prefix=V5 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedInferenceAttr %s

// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default4 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -emit-llvm -o - -x objective-c | FileCheck --check-prefix=IR4 %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture4 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -emit-llvm -o - -x objective-c | FileCheck --check-prefix=IR4 %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default5 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -emit-llvm -o - -x objective-c | FileCheck --check-prefix=IR5 %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture5 -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -emit-llvm -o - -x objective-c | FileCheck --check-prefix=IR5 %s

// V4: Dumping auditedProbe:
// V4-NEXT: FunctionDecl {{.*}} auditedProbe
// V4-NEXT: CFUnknownTransferAttr
// V4-EMPTY:
// V4-NEXT: Dumping InfThing::newProbe:
// V4-NEXT: ObjCMethodDecl {{.*}} + newProbe
// V4-NEXT: NSReturnsNotRetainedAttr
// V4-EMPTY:
// V4-NEXT: Dumping InfThing::newHeaderProbe:
// V4-NEXT: ObjCMethodDecl {{.*}} + newHeaderProbe
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: NSReturnsNotRetainedAttr
// V4-NEXT: NSReturnsRetainedAttr {{.*}} Implicit
// V4-EMPTY:

// V5: Dumping auditedProbe:
// V5-NEXT: FunctionDecl {{.*}} auditedProbe
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 0{{$}}
// V5-NEXT: CFUnknownTransferAttr
// V5-NEXT: CFAuditedTransferAttr {{.*}} Implicit
// V5-EMPTY:
// V5-NEXT: Dumping InfThing::newProbe:
// V5-NEXT: ObjCMethodDecl {{.*}} + newProbe
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 0{{$}}
// V5-NEXT: NSReturnsNotRetainedAttr
// V5-NEXT: NSReturnsRetainedAttr {{.*}} Implicit
// V5-EMPTY:
// V5-NEXT: Dumping InfThing::newHeaderProbe:
// V5-NEXT: ObjCMethodDecl {{.*}} + newHeaderProbe
// V5-NEXT: NSReturnsNotRetainedAttr
// V5-NEXT: SwiftVersionedRemovalAttr {{.*}} Implicit 4.0 {{[0-9]+}} 0{{$}}
// V5-EMPTY:

// At 4 newProbe returns +0 and newHeaderProbe +1; at 5 the other way round.
// IR4-LABEL: define ptr @useNew()
// IR4-NOT: autoreleaseReturnValue
// IR4: ret ptr
// IR4-LABEL: define ptr @useNewHeader()
// IR4: call ptr @llvm.objc.autoreleaseReturnValue
// IR5-LABEL: define ptr @useNew()
// IR5: call ptr @llvm.objc.autoreleaseReturnValue
// IR5-LABEL: define ptr @useNewHeader()
// IR5-NOT: autoreleaseReturnValue
// IR5: ret ptr

//--- module.modulemap
module InfKit { header "InfKit.h" export * }

//--- InfKit.h
#pragma clang arc_cf_code_audited begin
void *auditedProbe(void);
#pragma clang arc_cf_code_audited end

__attribute__((objc_root_class))
@interface InfThing
+ (id)newProbe;
+ (id)newHeaderProbe __attribute__((ns_returns_not_retained));
@end

//--- InfKit.apinotes
Name: InfKit
SwiftVersions:
  - Version: 4.0
    Functions:
      - Name: auditedProbe
        RetainCountConvention: none
    Classes:
      - Name: InfThing
        Methods:
          - Selector: newProbe
            MethodKind: Class
            RetainCountConvention: NSReturnsNotRetained
          - Selector: newHeaderProbe
            MethodKind: Class
            RetainCountConvention: none

//--- use.m
@import InfKit;
id useNew(void) { return [InfThing newProbe]; }
id useNewHeader(void) { return [InfThing newHeaderProbe]; }
void *useAudited(void) { return auditedProbe(); }
