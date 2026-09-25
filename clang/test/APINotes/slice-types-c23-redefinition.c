// C23 lets a tag be defined again, compatibly, and checks the two definitions
// for structural equivalence, captured API notes included. A captured 'Type:'
// has to compare, whether or not the declaration adjusts it.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -std=c23 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump -ast-dump-filter field -x c | FileCheck --check-prefix=V4 %s
// RUN: %clang_cc1 -std=c23 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump -ast-dump-filter field -x c | FileCheck --check-prefix=V4 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftTypeAttr %s
// RUN: %clang_cc1 -std=c23 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump -ast-dump-filter field -x c | FileCheck --check-prefix=V5 %s
// RUN: %clang_cc1 -std=c23 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump -ast-dump-filter field -x c | FileCheck --check-prefix=V5 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftTypeAttr %s

// V4: Dumping DupRec::field:
// V4-NEXT: FieldDecl {{.*}} field 'void *'
// V4-EMPTY:

// V5: Dumping DupRec::field:
// V5-NEXT: FieldDecl {{.*}} field 'char *'
// V5-EMPTY:

//--- module.modulemap
module Dup { header "Dup.h" export * }

//--- Dup.h
struct DupRec { int *field; };
struct DupRec { int *field; };

//--- Dup.apinotes
Name: Dup
Tags:
- Name: DupRec
  Fields:
  - Name: field
    Type: 'char *'
SwiftVersions:
- Version: 4
  Tags:
  - Name: DupRec
    Fields:
    - Name: field
      Type: 'void *'

//--- use.c
#include "Dup.h"
void useDup(struct DupRec *r) { (void)r->field; }
