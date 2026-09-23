// A category method merges with itself, when the override search reaches it
// through its class: it inherits its own Swift name, once API notes apply.
// At Swift 4 that is the notes' name, and the header's is what they replaced.

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump-all -ast-dump-filter nmSelf -x objective-c | FileCheck --check-prefixes=CHECK,V4 --allow-unused-prefixes %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump-all -ast-dump-filter nmSelf -x objective-c | FileCheck --check-prefixes=CHECK,V4 --allow-unused-prefixes --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump-all -ast-dump-filter nmSelf -x objective-c | FileCheck --check-prefixes=CHECK,V5 --allow-unused-prefixes %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump-all -ast-dump-filter nmSelf -x objective-c | FileCheck --check-prefixes=CHECK,V5 --allow-unused-prefixes --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s

// CHECK:      Dumping NmBase::nmSelf:
// CHECK-NEXT: ObjCMethodDecl {{.*}} imported in SM - nmSelf 'void'
// CHECK-EMPTY:
// V4-NEXT: Dumping NmOther::nmSelf:
// V4-NEXT: ObjCMethodDecl {{.*}} imported in SM - nmSelf 'void'
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "v4Name()"
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 IsReplacedByActive 0
// V4-NEXT: SwiftNameAttr {{.*}} "hdrName()"
// V4-EMPTY:
// V5-NEXT: Dumping NmOther::nmSelf:
// V5-NEXT: ObjCMethodDecl {{.*}} imported in SM - nmSelf 'void'
// V5-NEXT: SwiftNameAttr {{.*}} Inherited "hdrName()"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 0
// V5-NEXT: SwiftNameAttr {{.*}} "v4Name()"
// V5-EMPTY:

//--- module.modulemap
module SM { header "SM.h" export * }

//--- SM.h
__attribute__((objc_root_class))
@interface NmBase
- (void)nmSelf;
@end
@interface NmOther
@end
@interface NmOther (Cat)
- (void)nmSelf __attribute__((swift_name("hdrName()")));
@end

//--- SM.apinotes
Name: SM
SwiftVersions:
- Version: 4
  Classes:
  - Name: NmOther
    Methods:
    - Selector: nmSelf
      MethodKind: Instance
      SwiftName: 'v4Name()'

//--- use.m
@import SM;
