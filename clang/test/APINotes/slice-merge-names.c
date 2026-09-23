// A redeclaration's Swift name, once API notes apply.
//
//   nmRedecl    a redeclaration that repeats the header's name inherits the
//               notes' Swift 4 name, and keeps its own as the name those
//               notes replaced.
//   nmChain     a name that the first of three declarations' notes give, at
//               each version, reaches the third through the second. The
//               producer's name check collapses both and puts them back.
//   nmConflict  two different names in the header are one error, reported
//               once, whatever the notes do at each version.

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump-all -ast-dump-filter nm -x c | FileCheck --check-prefixes=CHECK,V4 --allow-unused-prefixes %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump-all -ast-dump-filter nm -x c | FileCheck --check-prefixes=CHECK,V4 --allow-unused-prefixes --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump-all -ast-dump-filter nm -x c | FileCheck --check-prefixes=CHECK,V5 --allow-unused-prefixes %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump-all -ast-dump-filter nm -x c | FileCheck --check-prefixes=CHECK,V5 --allow-unused-prefixes --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: not %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use-conflict.c -fapinotes-swift-version=5 -fmodules-cache-path=%t/conflict-default -fsyntax-only -x c 2>&1 | FileCheck --check-prefix=CONFLICT %s
// RUN: not %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use-conflict.c -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/conflict-capture -fsyntax-only -x c 2>&1 | FileCheck --check-prefix=CONFLICT %s

// V4:      Dumping nmRedecl:
// V4-NEXT: FunctionDecl {{.*}} imported in NR nmRedecl 'void (void)' external-linkage
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 IsReplacedByActive 0
// V4-NEXT: SwiftNameAttr {{.*}} "hdr()"
// V4-NEXT: SwiftNameAttr {{.*}} "v4()"
// V4-EMPTY:
// V5:      Dumping nmRedecl:
// V5-NEXT: FunctionDecl {{.*}} imported in NR nmRedecl 'void (void)' external-linkage
// V5-NEXT: SwiftNameAttr {{.*}} "hdr()"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 0
// V5-NEXT: SwiftNameAttr {{.*}} "v4()"
// V5-EMPTY:
// V4-NEXT: Dumping nmRedecl:
// V4-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in NR nmRedecl 'void (void)' external-linkage
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "v4()"
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 IsReplacedByActive 0
// V4-NEXT: SwiftNameAttr {{.*}} "hdr()"
// V4-EMPTY:
// V5-NEXT: Dumping nmRedecl:
// V5-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in NR nmRedecl 'void (void)' external-linkage
// V5-NEXT: SwiftNameAttr {{.*}} Inherited "hdr()"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 0
// V5-NEXT: SwiftNameAttr {{.*}} "v4()"
// V5-EMPTY:
// V4-NEXT: Dumping nmChain:
// V4-NEXT: FunctionDecl {{.*}} imported in TLA nmChain 'void (void)' external-linkage
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 IsReplacedByActive 0
// V4-NEXT: SwiftNameAttr {{.*}} "tlRoot()"
// V4-NEXT: SwiftNameAttr {{.*}} "tlRoot4()"
// V4-EMPTY:
// V5-NEXT: Dumping nmChain:
// V5-NEXT: FunctionDecl {{.*}} imported in TLA nmChain 'void (void)' external-linkage
// V5-NEXT: SwiftNameAttr {{.*}} "tlRoot()"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 0
// V5-NEXT: SwiftNameAttr {{.*}} "tlRoot4()"
// V5-EMPTY:
// V4-NEXT: Dumping nmChain:
// V4-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in TLB nmChain 'void (void)' external-linkage
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "tlRoot4()"
// V4-EMPTY:
// V5-NEXT: Dumping nmChain:
// V5-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in TLB nmChain 'void (void)' external-linkage
// V5-NEXT: SwiftNameAttr {{.*}} Inherited "tlRoot()"
// V5-EMPTY:
// V4-NEXT: Dumping nmChain:
// V4-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in TLC nmChain 'void (void)' external-linkage
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "tlRoot4()"
// V4-EMPTY:
// V5-NEXT: Dumping nmChain:
// V5-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in TLC nmChain 'void (void)' external-linkage
// V5-NEXT: SwiftNameAttr {{.*}} Inherited "tlRoot()"
// V5-EMPTY:

// CONFLICT:     error: 'swift_name' and 'swift_name' attributes are not compatible
// CONFLICT-NOT: attributes are not compatible

//--- module.modulemap
module NR { header "NR.h" export * }
module DN { header "DN.h" export * }
module TLA { header "TLA.h" export * }
module TLB { header "TLB.h" export * }
module TLC { header "TLC.h" export * }

//--- NR.h
void nmRedecl(void) __attribute__((swift_name("hdr()")));
void nmRedecl(void) __attribute__((swift_name("hdr()")));

//--- NR.apinotes
Name: NR
SwiftVersions:
- Version: 4
  Functions:
  - Name: nmRedecl
    SwiftName: 'v4()'

//--- DN.h
void nmConflict(void) __attribute__((swift_name("a()")));
void nmConflict(void) __attribute__((swift_name("b()")));

//--- DN.apinotes
Name: DN
Functions:
- Name: nmConflict
  SwiftPrivate: true
SwiftVersions:
- Version: 4
  Functions:
  - Name: nmConflict
    SwiftPrivate: false

//--- TLA.h
void nmChain(void);

//--- TLA.apinotes
Name: TLA
Functions:
- Name: nmChain
  SwiftName: 'tlRoot()'
SwiftVersions:
- Version: 4
  Functions:
  - Name: nmChain
    SwiftName: 'tlRoot4()'

//--- TLB.h
#include "TLA.h"
void nmChain(void);

//--- TLC.h
#include "TLB.h"
void nmChain(void);

//--- use.c
#include "NR.h"
#include "TLC.h"

//--- use-conflict.c
#include "DN.h"
