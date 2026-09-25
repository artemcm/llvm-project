// Under ARC, a method's family decides whether Sema infers ns_returns_retained
// and, for init, ns_consumes_self. For init, alloc, copy, mutableCopy and new
// the family also needs an object return type, which 'ResultType:' can
// rewrite. A capture-mode producer sees only the type as written, so it
// records those inferences wherever a captured 'ResultType:' could change
// the family, and its consumer decides them again once the winning type has
// applied, as the default mode would have.
//
//   newThing     'void *' as written, 'id' at Swift 4: new only at Swift 4.
//   newOther     'id' as written, 'void *' at Swift 4: new except at Swift 4.
//   initThing    the same for init, with ns_consumes_self too.
//   initOther    ...and the other way round.
//   allocThing   a class method.
//
// An init method whose captured 'ResultType:' is unrelated to its class is
// rejected by the default mode, which capture cannot reproduce for a client,
// so it warns instead.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump -ast-dump-filter FamBox:: -x objective-c | FileCheck --check-prefix=V4 %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump -ast-dump-filter FamBox:: -x objective-c | FileCheck --check-prefix=V4 --implicit-check-not=SwiftVersioned --implicit-check-not=SwiftTypeAttr %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump -ast-dump-filter FamBox:: -x objective-c | FileCheck --check-prefix=V5 %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump -ast-dump-filter FamBox:: -x objective-c | FileCheck --check-prefix=V5 --implicit-check-not=SwiftVersioned --implicit-check-not=SwiftTypeAttr %s

// The unrelated init: the default mode rejects it at Swift 4 only, and
// capture warns at every version.
// RUN: not %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use-unrelated.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/unrelated-default4 -fsyntax-only -x objective-c 2>&1 | FileCheck --check-prefix=UNRELATED4 %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use-unrelated.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/unrelated-default5 -fsyntax-only -x objective-c 2>&1 | FileCheck --allow-empty --check-prefix=CLEAN --implicit-check-not=warning: --implicit-check-not=error: %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fobjc-arc -fobjc-runtime=macosx-14 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use-unrelated.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/unrelated-capture5 -fsyntax-only -x objective-c 2>&1 | FileCheck --check-prefix=WARN --implicit-check-not=error: %s

// V4: ObjCMethodDecl {{.*}} - newThing 'id'
// V4-NEXT: NSReturnsRetainedAttr {{.*}} Implicit
// V4-EMPTY:
// V4: ObjCMethodDecl {{.*}} - newOther 'void *'
// V4-EMPTY:
// V4: ObjCMethodDecl {{.*}} - initThing 'FamBox *'
// V4-NEXT: NSConsumesSelfAttr {{.*}} Implicit
// V4-NEXT: NSReturnsRetainedAttr {{.*}} Implicit
// V4-EMPTY:
// V4: ObjCMethodDecl {{.*}} - initOther 'void *'
// V4-EMPTY:
// V4: ObjCMethodDecl {{.*}} + allocThing 'FamBox *'
// V4-NEXT: NSReturnsRetainedAttr {{.*}} Implicit

// V5: ObjCMethodDecl {{.*}} - newThing 'void *'
// V5-EMPTY:
// V5: ObjCMethodDecl {{.*}} - newOther 'id'
// V5-NEXT: NSReturnsRetainedAttr {{.*}} Implicit
// V5-EMPTY:
// V5: ObjCMethodDecl {{.*}} - initThing 'void *'
// V5-EMPTY:
// V5: ObjCMethodDecl {{.*}} - initOther 'id'
// V5-NEXT: NSConsumesSelfAttr {{.*}} Implicit
// V5-NEXT: NSReturnsRetainedAttr {{.*}} Implicit
// V5-EMPTY:
// V5: ObjCMethodDecl {{.*}} + allocThing 'void *'
// V5-NOT: Attr

// UNRELATED4: error: init methods must return a type related to the receiver type
// WARN: Unrelated.h:6:1: warning: API notes result type 'UnrelatedOther *' for Swift version 4 is unrelated to this init method's class; a client that selects it does not reject the method [-Wapinotes-version-independent]
// CLEAN-NOT: {{.}}

//--- module.modulemap
module Fam { header "Fam.h" export * }
module Unrelated { header "Unrelated.h" export * }

//--- Fam.h
__attribute__((objc_root_class))
@interface FamBox
- (void *)newThing;
- (id)newOther;
- (void *)initThing;
- (id)initOther;
+ (void *)allocThing;
@end

//--- Fam.apinotes
Name: Fam
SwiftVersions:
- Version: 4
  Classes:
  - Name: FamBox
    Methods:
    - Selector: newThing
      MethodKind: Instance
      ResultType: 'id'
    - Selector: newOther
      MethodKind: Instance
      ResultType: 'void *'
    - Selector: initThing
      MethodKind: Instance
      ResultType: 'FamBox *'
    - Selector: initOther
      MethodKind: Instance
      ResultType: 'void *'
    - Selector: allocThing
      MethodKind: Class
      ResultType: 'FamBox *'

//--- Unrelated.h
__attribute__((objc_root_class))
@interface UnrelatedOther
@end
__attribute__((objc_root_class))
@interface UnrelatedBox
- (id)initUnrelated;
@end

//--- Unrelated.apinotes
Name: Unrelated
SwiftVersions:
- Version: 4
  Classes:
  - Name: UnrelatedBox
    Methods:
    - Selector: initUnrelated
      MethodKind: Instance
      ResultType: 'UnrelatedOther *'

//--- use.m
#include "Fam.h"

//--- use-unrelated.m
#include "Unrelated.h"
