// 'Type:', 'ResultType:' and nullability rewrite a declaration's type rather
// than add attributes. A capture-mode module parses every slice's type as it
// captures it, and its consumer applies the winner's to the type, as the
// default mode would have. Checked against the default mode for:
//
//   varProbe, paramProbe   'Nullability:' on a global and a parameter.
//   resultProbe            'ResultType:' on a function...
//   methodProbe            ...and on a method.
//   auditedProbe           'Nullability:' and 'NullabilityOfRet:' together.
//   RecProbe               'Type:' on a field.
//   propProbe              'Type:' and 'Nullability:' on a property, which its
//                          implicit getter returns and its setter takes.
//   TyHeir                 redeclarations, which take the parameters'
//                          nullability, and in C the whole function type.
//   namedProbe             a redeclaration with a Swift name of its own, which
//                          the producer checks against its previous
//                          declaration's at every version: it collapses that
//                          one, parameters included, and puts it back.
//
// The -Wnonnull diagnostics are what a consumer sees of all this.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -fblocks -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump -ast-dump-filter Probe -x objective-c 2>/dev/null | FileCheck --check-prefix=V4 %s
// RUN: %clang_cc1 -fblocks -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump -ast-dump-filter Probe -x objective-c 2>/dev/null | FileCheck --check-prefix=V4 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftTypeAttr --implicit-check-not=SwiftNullabilityAttr %s
// RUN: %clang_cc1 -fblocks -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump -ast-dump-filter Probe -x objective-c 2>/dev/null | FileCheck --check-prefix=V5 %s
// RUN: %clang_cc1 -fblocks -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump -ast-dump-filter Probe -x objective-c 2>/dev/null | FileCheck --check-prefix=V5 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftTypeAttr --implicit-check-not=SwiftNullabilityAttr %s

// RUN: %clang_cc1 -fblocks -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/cxx-default4 -ast-dump -ast-dump-filter Probe -x objective-c++ 2>/dev/null | FileCheck --check-prefix=CXX4 %s
// RUN: %clang_cc1 -fblocks -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/cxx-capture4 -ast-dump -ast-dump-filter Probe -x objective-c++ 2>/dev/null | FileCheck --check-prefix=CXX4 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftTypeAttr --implicit-check-not=SwiftNullabilityAttr %s
// RUN: %clang_cc1 -fblocks -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/cxx-default5 -ast-dump -ast-dump-filter Probe -x objective-c++ 2>/dev/null | FileCheck --check-prefix=CXX5 %s
// RUN: %clang_cc1 -fblocks -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/cxx-capture5 -ast-dump -ast-dump-filter Probe -x objective-c++ 2>/dev/null | FileCheck --check-prefix=CXX5 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftTypeAttr --implicit-check-not=SwiftNullabilityAttr %s

// RUN: %clang_cc1 -fblocks -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -fsyntax-only -verify=v4 -x objective-c
// RUN: %clang_cc1 -fblocks -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -fsyntax-only -verify=v4 -x objective-c
// RUN: %clang_cc1 -fblocks -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -fsyntax-only -verify=v5 -x objective-c
// RUN: %clang_cc1 -fblocks -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -fsyntax-only -verify=v5 -x objective-c

// V4: Dumping varProbe:
// V4: VarDecl {{.*}} varProbe 'int * _Nullable':'int *'
// V4: Dumping paramProbe:
// V4: FunctionDecl {{.*}} paramProbe 'void (int * _Nullable)'
// V4: ParmVarDecl {{.*}} p 'int * _Nullable':'int *'
// V4: Dumping resultProbe:
// V4: FunctionDecl {{.*}} resultProbe 'char *(void)'
// V4: Dumping auditedProbe:
// V4: FunctionDecl {{.*}} auditedProbe 'int * _Nullable (int * _Nullable, int * _Nonnull)'
// V4: ParmVarDecl {{.*}} a 'int * _Nullable':'int *'
// V4: ParmVarDecl {{.*}} b 'int * _Nonnull':'int *'
// V4: Dumping RecProbe:
// V4: FieldDecl {{.*}} field 'char *'
// V4: Dumping TyBox::methodProbe::
// V4: ObjCMethodDecl {{.*}} methodProbe: 'TyBox *'
// V4: ParmVarDecl {{.*}} p 'id'
// V4: Dumping TyBox::propProbe:
// V4: ObjCPropertyDecl {{.*}} propProbe 'TyBox *'
// V4: Dumping TyBox::propProbe:
// V4: ObjCMethodDecl {{.*}} propProbe 'TyBox *'
// V4: Dumping TyBox::setPropProbe::
// V4: ObjCMethodDecl {{.*}} setPropProbe: 'void'
// V4: ParmVarDecl {{.*}} propProbe 'TyBox *'
// V4: Dumping paramProbe:
// V4: FunctionDecl {{.*}} paramProbe 'void (int * _Nullable)'
// V4: ParmVarDecl {{.*}} p 'int * _Nullable':'int *'
// V4: Dumping auditedProbe:
// V4: FunctionDecl {{.*}} auditedProbe 'int * _Nullable (int * _Nullable, int * _Nonnull)'
// V4: ParmVarDecl {{.*}} a 'int * _Nullable':'int *'
// V4: ParmVarDecl {{.*}} b 'int * _Nonnull':'int *'

// V5: Dumping varProbe:
// V5: VarDecl {{.*}} varProbe 'int * _Nonnull':'int *'
// V5: Dumping paramProbe:
// V5: FunctionDecl {{.*}} paramProbe 'void (int * _Nonnull)'
// V5: ParmVarDecl {{.*}} p 'int * _Nonnull':'int *'
// V5: Dumping resultProbe:
// V5: FunctionDecl {{.*}} resultProbe 'void *(void)'
// V5: Dumping auditedProbe:
// V5: FunctionDecl {{.*}} auditedProbe 'int * _Nonnull (int * _Nonnull, int * _Nullable)'
// V5: ParmVarDecl {{.*}} a 'int * _Nonnull':'int *'
// V5: ParmVarDecl {{.*}} b 'int * _Nullable':'int *'
// V5: Dumping RecProbe:
// V5: FieldDecl {{.*}} field 'char *'
// V5: Dumping TyBox::methodProbe::
// V5: ObjCMethodDecl {{.*}} methodProbe: 'id _Nonnull':'id'
// V5: ParmVarDecl {{.*}} p 'id _Nonnull':'id'
// V5: Dumping TyBox::propProbe:
// V5: ObjCPropertyDecl {{.*}} propProbe 'id _Nonnull':'id'
// V5: Dumping TyBox::propProbe:
// V5: ObjCMethodDecl {{.*}} propProbe 'id _Nonnull':'id'
// V5: Dumping TyBox::setPropProbe::
// V5: ObjCMethodDecl {{.*}} setPropProbe: 'void'
// V5: ParmVarDecl {{.*}} propProbe 'id _Nonnull':'id'
// V5: Dumping paramProbe:
// V5: FunctionDecl {{.*}} paramProbe 'void (int * _Nonnull)'
// V5: ParmVarDecl {{.*}} p 'int * _Nonnull':'int *'
// V5: Dumping auditedProbe:
// V5: FunctionDecl {{.*}} auditedProbe 'int * _Nonnull (int * _Nonnull, int * _Nullable)'
// V5: ParmVarDecl {{.*}} a 'int * _Nonnull':'int *'
// V5: ParmVarDecl {{.*}} b 'int * _Nullable':'int *'

// CXX4: FunctionDecl {{.*}} imported in TyHeir {{.*}}paramProbe 'void (int *)'
// CXX4-NEXT: ParmVarDecl {{.*}} p 'int * _Nullable':'int *'
// CXX4: FunctionDecl {{.*}} imported in TyHeir {{.*}}auditedProbe 'int *(int *, int *)'
// CXX4-NEXT: ParmVarDecl {{.*}} a 'int * _Nullable':'int *'
// CXX4-NEXT: ParmVarDecl {{.*}} b 'int * _Nonnull':'int *'

// CXX5: FunctionDecl {{.*}} imported in TyHeir {{.*}}paramProbe 'void (int *)'
// CXX5-NEXT: ParmVarDecl {{.*}} p 'int * _Nonnull':'int *'
// CXX5: FunctionDecl {{.*}} imported in TyHeir {{.*}}auditedProbe 'int *(int *, int *)'
// CXX5-NEXT: ParmVarDecl {{.*}} a 'int * _Nonnull':'int *'
// CXX5-NEXT: ParmVarDecl {{.*}} b 'int * _Nullable':'int *'

// RUN: %clang_cc1 -fblocks -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump -ast-dump-filter namedProbe -x objective-c 2>/dev/null | FileCheck --check-prefix=NAMED %s
// RUN: %clang_cc1 -fblocks -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.m -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump -ast-dump-filter namedProbe -x objective-c 2>/dev/null | FileCheck --check-prefix=NAMED --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftNullabilityAttr %s

// NAMED: Dumping namedProbe:
// NAMED-NEXT: FunctionDecl {{.*}} imported in TyKit namedProbe 'void (int * _Nonnull)'
// NAMED-NEXT: ParmVarDecl {{.*}} p 'int * _Nonnull':'int *'
// NAMED-NEXT: SwiftNameAttr {{.*}} "namedProbe(_:)"
// NAMED-EMPTY:
// NAMED-NEXT: Dumping namedProbe:
// NAMED-NEXT: FunctionDecl {{.*}} imported in TyHeir namedProbe 'void (int * _Nonnull)'
// NAMED-NEXT: ParmVarDecl {{.*}} p 'int * _Nonnull':'int *'
// NAMED-NEXT: SwiftNameAttr {{.*}} Inherited "namedProbe(_:)"
// NAMED-EMPTY:

//--- module.modulemap
module TyKit { header "TyKit.h" export * }
module TyHeir { header "TyHeir.h" export * }

//--- TyKit.h
extern int *varProbe;
void paramProbe(int *p);
void *resultProbe(void);
int *auditedProbe(int *a, int *b);
void namedProbe(int *p);
struct RecProbe {
  void *field;
};
__attribute__((objc_root_class))
@interface TyBox
- (id)methodProbe:(id)p;
@property (nonatomic, strong) id propProbe;
@end

//--- TyKit.apinotes
Name: TyKit
Globals:
  - Name: varProbe
    Nullability: N
Functions:
  - Name: paramProbe
    Parameters:
      - Position: 0
        Nullability: N
  - Name: auditedProbe
    Nullability: [N, O]
    NullabilityOfRet: N
  - Name: namedProbe
    SwiftName: 'namedProbe(_:)'
    Parameters:
      - Position: 0
        Nullability: N
Tags:
  - Name: RecProbe
    Fields:
      - Name: field
        Type: 'char *'
Classes:
  - Name: TyBox
    Methods:
      - Selector: 'methodProbe:'
        MethodKind: Instance
        NullabilityOfRet: N
        Parameters:
          - Position: 0
            Nullability: O
    Properties:
      - Name: propProbe
        Nullability: N
SwiftVersions:
  - Version: 4.0
    Globals:
      - Name: varProbe
        Nullability: O
    Functions:
      - Name: paramProbe
        Parameters:
          - Position: 0
            Nullability: O
      - Name: resultProbe
        ResultType: 'char *'
      - Name: auditedProbe
        Nullability: [O, N]
        NullabilityOfRet: O
    Classes:
      - Name: TyBox
        Methods:
          - Selector: 'methodProbe:'
            MethodKind: Instance
            ResultType: 'TyBox *'
        Properties:
          - Name: propProbe
            Type: 'TyBox *'

//--- TyHeir.h
#include "TyKit.h"
void paramProbe(int *p);
int *auditedProbe(int *a, int *b);
void namedProbe(int *p) __attribute__((swift_name("namedProbe(_:)")));

//--- use.m
#include "TyHeir.h"
void use(TyBox *box) {
  paramProbe(0); // v5-warning {{null passed to a callee that requires a non-null argument}}
  (void)resultProbe();
  (void)[box methodProbe:0]; // v5-warning {{null passed to a callee that requires a non-null argument}}
  (void)box.propProbe;
  box.propProbe = 0; // v5-warning {{null passed to a callee that requires a non-null argument}}
  struct RecProbe r;
  (void)r;
}
// v4-no-diagnostics
