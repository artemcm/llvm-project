// Under ARC, Sema infers a variable's or field's ownership once API notes have
// applied: '__strong' for a retainable type with none. A capture-mode producer
// infers it for the type as written, so its consumer takes an inferred
// ownership off before applying the winning 'Type:' or nullability, and
// infers it again after, as the default mode would have.
//
//   ownGlobal     'Type:' on a global, which stores through objc_storeStrong.
//   ownVersioned  ...at two versions.
//   ownExplicit   'Type:' replaces an explicit ownership too.
//   ownNullOnly   nullability alone, which the inferred ownership goes around.
//   ownBoth       'Type:' and nullability together.
//   OwnRec        the same for fields.
//   ownParam      a parameter, whose ownership its function type keeps.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump -ast-dump-filter own -x objective-c | FileCheck --check-prefixes=CHECK,V4 %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump -ast-dump-filter own -x objective-c | FileCheck --check-prefixes=CHECK,V4 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftTypeAttr --implicit-check-not=SwiftNullabilityAttr %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump -ast-dump-filter own -x objective-c | FileCheck --check-prefixes=CHECK,V5 %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump -ast-dump-filter own -x objective-c | FileCheck --check-prefixes=CHECK,V5 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftTypeAttr --implicit-check-not=SwiftNullabilityAttr %s

// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/ir-default5 -emit-llvm -o - -x objective-c | FileCheck --check-prefix=IR %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/ir-capture5 -emit-llvm -o - -x objective-c | FileCheck --check-prefix=IR %s

// CHECK: Dumping ownGlobal:
// CHECK-NEXT: VarDecl {{.*}} ownGlobal 'NSString *__strong' extern destroyed
// CHECK: Dumping ownVersioned:
// V4-NEXT: VarDecl {{.*}} ownVersioned 'NSArray *__strong' extern destroyed
// V5-NEXT: VarDecl {{.*}} ownVersioned 'NSString *__strong' extern destroyed
// CHECK: Dumping ownExplicit:
// CHECK-NEXT: VarDecl {{.*}} ownExplicit 'NSString *__strong' extern destroyed
// CHECK: Dumping ownNullOnly:
// CHECK-NEXT: VarDecl {{.*}} ownNullOnly 'id  _Nonnull __strong':'__strong id' extern destroyed
// CHECK: Dumping ownBoth:
// CHECK-NEXT: VarDecl {{.*}} ownBoth 'NSString * _Nullable __strong':'NSString *__strong' extern destroyed
// CHECK: Dumping OwnRec::ownField:
// CHECK-NEXT: FieldDecl {{.*}} ownField 'NSString *__strong'
// CHECK: Dumping OwnRec::ownFieldNull:
// CHECK-NEXT: FieldDecl {{.*}} ownFieldNull 'id  _Nonnull __strong':'__strong id'
// CHECK: Dumping ownParam:
// CHECK-NEXT: FunctionDecl {{.*}} ownParam 'void (NSString *__strong)'

// IR-LABEL: define{{.*}} void @setIt(
// IR: call void @llvm.objc.storeStrong(ptr @ownGlobal,

//--- module.modulemap
module Own { header "Own.h" export * }

//--- Own.h
@class NSString, NSArray;
extern id ownGlobal;
extern id ownVersioned;
extern id __unsafe_unretained ownExplicit;
extern id ownNullOnly;
extern id ownBoth;
struct OwnRec {
  id ownField;
  id ownFieldNull;
};
void ownParam(id x);

//--- Own.apinotes
Name: Own
Globals:
- Name: ownGlobal
  Type: 'NSString *'
- Name: ownVersioned
  Type: 'NSString *'
- Name: ownExplicit
  Type: 'NSString *'
- Name: ownNullOnly
  Nullability: N
- Name: ownBoth
  Type: 'NSString *'
  Nullability: O
Tags:
- Name: OwnRec
  Fields:
  - Name: ownField
    Type: 'NSString *'
  - Name: ownFieldNull
    Nullability: N
Functions:
- Name: ownParam
  Parameters:
  - Position: 0
    Type: 'NSString *'
SwiftVersions:
- Version: 4
  Globals:
  - Name: ownVersioned
    Type: 'NSArray *'

//--- use.m
#include "Own.h"
void setIt(NSString *x) { ownGlobal = x; }
void useAll(struct OwnRec *r) {
  (void)ownVersioned, (void)ownExplicit, (void)ownNullOnly, (void)ownBoth;
  (void)r->ownField, (void)r->ownFieldNull;
}
