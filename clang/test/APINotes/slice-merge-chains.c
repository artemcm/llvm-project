// A redeclaration takes what the default mode's merge gives it once its
// previous declaration's API notes apply. A capture-mode producer records
// each merge, and the collapse merges again, against the previous
// declaration collapsed, where the producer merged.
//
//   strchr      the redeclaration of a builtin, declared twice, has the
//               builtin's type: its own notes reach its parameter only.
//   mrgChainRet a C function's third declaration has the second's type,
//               '_Nullable' as written, which the first's notes never reach.
//   mrgChainParam  a parameter takes the nullability its previous
//               declaration writes, over what the notes two declarations up
//               give...
//   mrgNotesParam  ...or that its previous declaration's own notes give it.
//   mrgAvail    the second declaration's own Swift availability keeps the
//               first's notes from reaching the third.
//   mrgRcFn, mrgRcInh  a convention the previous declaration writes passes on
//               beside the one its notes give or take away.
//   mrgSwRet, mrgSwParam  a redeclaration in the same module gets the type
//               its previous declaration's notes and its own both give.
//   mrgChainFn  five modules deep, each declaration merges with the one
//               before it as the default mode does, attribute order included.
//   mrgTag      a tag merges before its own notes apply, and its own slices
//               number from zero, as in the default mode.

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump-all -ast-dump-filter mrg -x c 2>/dev/null | FileCheck --check-prefixes=CHECK,V4 --allow-unused-prefixes %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump-all -ast-dump-filter mrg -x c 2>/dev/null | FileCheck --check-prefixes=CHECK,V4 --allow-unused-prefixes --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump-all -ast-dump-filter mrg -x c 2>/dev/null | FileCheck --check-prefixes=CHECK,V5 --allow-unused-prefixes %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump-all -ast-dump-filter mrg -x c 2>/dev/null | FileCheck --check-prefixes=CHECK,V5 --allow-unused-prefixes --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fapinotes-swift-version=6 -fmodules-cache-path=%t/default6 -ast-dump-all -ast-dump-filter mrg -x c 2>/dev/null | FileCheck --check-prefixes=CHECK,V6 --allow-unused-prefixes %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fswift-version-independent-apinotes -fapinotes-swift-version=6 -fmodules-cache-path=%t/capture6 -ast-dump-all -ast-dump-filter mrg -x c 2>/dev/null | FileCheck --check-prefixes=CHECK,V6 --allow-unused-prefixes --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump-all -ast-dump-filter strchr -x c 2>/dev/null | FileCheck --check-prefix=BUILTIN %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump-all -ast-dump-filter strchr -x c 2>/dev/null | FileCheck --check-prefix=BUILTIN --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s

// CHECK:      Dumping mrgChainRet:
// CHECK-NEXT: FunctionDecl {{.*}} imported in CA mrgChainRet 'int * _Nullable (void)' external-linkage
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgChainParam:
// CHECK-NEXT: FunctionDecl {{.*}} imported in CA mrgChainParam 'void (int * _Null_unspecified)' external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in CA p 'int * _Null_unspecified':'int *'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgChainRet:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in CB mrgChainRet 'int * _Nullable (void)' external-linkage
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgChainRet:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in CB mrgChainRet 'int * _Nullable (void)' external-linkage
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgChainParam:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in CB mrgChainParam 'void (int * _Null_unspecified)' external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in CB p 'int * _Nullable':'int *'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgChainParam:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in CB mrgChainParam 'void (int * _Null_unspecified)' external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in CB p 'int * _Nullable':'int *'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgNotesParam:
// CHECK-NEXT: FunctionDecl {{.*}} imported in NA mrgNotesParam 'void (int * _Null_unspecified)' external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in NA p 'int * _Null_unspecified':'int *'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgNotesParam:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in NB mrgNotesParam 'void (int * _Null_unspecified)' external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in NB p 'int * _Nonnull':'int *'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgNotesParam:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in NC mrgNotesParam 'void (int * _Null_unspecified)' external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in NC p 'int * _Nonnull':'int *'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgAvail:
// CHECK-NEXT: FunctionDecl {{.*}} imported in AA mrgAvail 'void (void)' external-linkage
// CHECK-NEXT: AvailabilityAttr {{.*}} swift 0 0 0 Unavailable "fromNotes" "" 0
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgAvail:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in AB mrgAvail 'void (void)' external-linkage
// CHECK-NEXT: AvailabilityAttr {{.*}} swift 0 0 0 Unavailable "fromHeader" "" 0
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgAvail:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in AB mrgAvail 'void (void)' external-linkage
// CHECK-NEXT: AvailabilityAttr {{.*}} Inherited swift 0 0 0 Unavailable "fromHeader" "" 0
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgRcFn:
// CHECK-NEXT: FunctionDecl {{.*}} imported in RA mrgRcFn 'CFThingRef (void)' external-linkage
// CHECK-NEXT: CFReturnsNotRetainedAttr {{.*}}
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgRcInh:
// CHECK-NEXT: FunctionDecl {{.*}} imported in RA mrgRcInh 'CFThingRef (void)' external-linkage
// CHECK-NEXT: CFReturnsRetainedAttr {{.*}}
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgRcFn:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in RB mrgRcFn 'CFThingRef (void)' external-linkage
// CHECK-NEXT: CFReturnsNotRetainedAttr {{.*}} Inherited
// CHECK-NEXT: CFReturnsRetainedAttr {{.*}}
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgRcFn:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in RB mrgRcFn 'CFThingRef (void)' external-linkage
// CHECK-NEXT: CFReturnsNotRetainedAttr {{.*}} Inherited
// CHECK-NEXT: CFReturnsRetainedAttr {{.*}} Inherited
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgRcInh:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in RB mrgRcInh 'CFThingRef (void)' external-linkage
// CHECK-NEXT: CFReturnsRetainedAttr {{.*}} Inherited
// CHECK-NEXT: CFReturnsNotRetainedAttr {{.*}}
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgRcInh:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in RB mrgRcInh 'CFThingRef (void)' external-linkage
// CHECK-NEXT: CFReturnsRetainedAttr {{.*}} Inherited
// CHECK-NEXT: CFReturnsNotRetainedAttr {{.*}}
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgSwRet:
// CHECK-NEXT: FunctionDecl {{.*}} imported in SW mrgSwRet 'int *(void)' external-linkage
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgSwRet:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in SW mrgSwRet 'int *(void)' external-linkage
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgSwParam:
// CHECK-NEXT: FunctionDecl {{.*}} imported in SW mrgSwParam 'void (int *)' external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in SW p 'int *'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping mrgSwParam:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in SW mrgSwParam 'void (int *)' external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in SW p 'int *'
// CHECK-EMPTY:
// V4-NEXT: Dumping mrgChainFn:
// V4-NEXT: FunctionDecl {{.*}} imported in C1 mrgChainFn 'int *(int *)' external-linkage
// V4-NEXT: ParmVarDecl {{.*}} imported in C1 p 'int *'
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 IsReplacedByActive 0
// V4-NEXT: SwiftNameAttr {{.*}} "chain(_:)"
// V4-NEXT: SwiftNameAttr {{.*}} "chainOld(_:)"
// V4-EMPTY:
// V5-NEXT: Dumping mrgChainFn:
// V5-NEXT: FunctionDecl {{.*}} imported in C1 mrgChainFn 'int * _Nonnull (int * _Nonnull)' external-linkage
// V5-NEXT: ParmVarDecl {{.*}} imported in C1 p 'int * _Nonnull':'int *'
// V5-NEXT: SwiftNameAttr {{.*}} "chain(_:)"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 0
// V5-NEXT: SwiftNameAttr {{.*}} "chainOld(_:)"
// V5-EMPTY:
// V6-NEXT: Dumping mrgChainFn:
// V6-NEXT: FunctionDecl {{.*}} imported in C1 mrgChainFn 'int * _Nonnull (int * _Nonnull)' external-linkage
// V6-NEXT: ParmVarDecl {{.*}} imported in C1 p 'int * _Nonnull':'int *'
// V6-NEXT: SwiftNameAttr {{.*}} "chain(_:)"
// V6-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 0
// V6-NEXT: SwiftNameAttr {{.*}} "chainOld(_:)"
// V6-EMPTY:
// V4-NEXT: Dumping mrgChainFn:
// V4-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in C2 mrgChainFn 'int *(int *)' external-linkage
// V4-NEXT: ParmVarDecl {{.*}} imported in C2 p 'int *'
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "chainOld(_:)"
// V4-NEXT: SwiftPrivateAttr {{.*}}
// V4-EMPTY:
// V5-NEXT: Dumping mrgChainFn:
// V5-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in C2 mrgChainFn 'int * _Nonnull (int * _Nonnull)' external-linkage
// V5-NEXT: ParmVarDecl {{.*}} imported in C2 p 'int * _Nonnull':'int *'
// V5-NEXT: SwiftNameAttr {{.*}} Inherited "chain(_:)"
// V5-NEXT: SwiftPrivateAttr {{.*}}
// V5-EMPTY:
// V6-NEXT: Dumping mrgChainFn:
// V6-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in C2 mrgChainFn 'int * _Nonnull (int * _Nonnull)' external-linkage
// V6-NEXT: ParmVarDecl {{.*}} imported in C2 p 'int * _Nonnull':'int *'
// V6-NEXT: SwiftNameAttr {{.*}} Inherited "chain(_:)"
// V6-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 5 0
// V6-NEXT: SwiftPrivateAttr {{.*}}
// V6-EMPTY:
// V4-NEXT: Dumping mrgChainFn:
// V4-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in C3 mrgChainFn 'int *(int *)' external-linkage
// V4-NEXT: ParmVarDecl {{.*}} imported in C3 p 'int *'
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "chainOld(_:)"
// V4-NEXT: SwiftPrivateAttr {{.*}} Inherited
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 IsReplacedByActive 0
// V4-NEXT: SwiftAttrAttr {{.*}} "safe"
// V4-NEXT: AvailabilityAttr {{.*}} swift 0 0 0 Unavailable "" "" 0
// V4-EMPTY:
// V5-NEXT: Dumping mrgChainFn:
// V5-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in C3 mrgChainFn 'int * _Nonnull (int * _Nonnull)' external-linkage
// V5-NEXT: ParmVarDecl {{.*}} imported in C3 p 'int * _Nonnull':'int *'
// V5-NEXT: SwiftNameAttr {{.*}} Inherited "chain(_:)"
// V5-NEXT: SwiftPrivateAttr {{.*}} Inherited
// V5-NEXT: SwiftAttrAttr {{.*}} "safe"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 0
// V5-NEXT: AvailabilityAttr {{.*}} swift 0 0 0 Unavailable "" "" 0
// V5-EMPTY:
// V6-NEXT: Dumping mrgChainFn:
// V6-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in C3 mrgChainFn 'int * _Nonnull (int * _Nonnull)' external-linkage
// V6-NEXT: ParmVarDecl {{.*}} imported in C3 p 'int * _Nonnull':'int *'
// V6-NEXT: SwiftNameAttr {{.*}} Inherited "chain(_:)"
// V6-NEXT: SwiftAttrAttr {{.*}} "safe"
// V6-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 0
// V6-NEXT: AvailabilityAttr {{.*}} swift 0 0 0 Unavailable "" "" 0
// V6-EMPTY:
// V4-NEXT: Dumping mrgChainFn:
// V4-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in C4 mrgChainFn 'int *(int *)' external-linkage
// V4-NEXT: ParmVarDecl {{.*}} imported in C4 p 'int *'
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "chainOld(_:)"
// V4-NEXT: SwiftPrivateAttr {{.*}} Inherited
// V4-NEXT: AvailabilityAttr {{.*}} Inherited swift 0 0 0 Unavailable "" "" 0
// V4-EMPTY:
// V5-NEXT: Dumping mrgChainFn:
// V5-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in C4 mrgChainFn 'int * _Nonnull (int * _Nonnull)' external-linkage
// V5-NEXT: ParmVarDecl {{.*}} imported in C4 p 'int * _Nonnull':'int *'
// V5-NEXT: SwiftNameAttr {{.*}} Inherited "chain(_:)"
// V5-NEXT: SwiftPrivateAttr {{.*}} Inherited
// V5-NEXT: SwiftAttrAttr {{.*}} Inherited "safe"
// V5-EMPTY:
// V6-NEXT: Dumping mrgChainFn:
// V6-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in C4 mrgChainFn 'int * _Nonnull (int * _Nonnull)' external-linkage
// V6-NEXT: ParmVarDecl {{.*}} imported in C4 p 'int * _Nonnull':'int *'
// V6-NEXT: SwiftNameAttr {{.*}} Inherited "chain(_:)"
// V6-NEXT: SwiftAttrAttr {{.*}} Inherited "safe"
// V6-EMPTY:
// V4-NEXT: Dumping mrgChainFn:
// V4-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in C5 mrgChainFn 'int *(int *)' external-linkage
// V4-NEXT: ParmVarDecl {{.*}} imported in C5 p 'int * _Null_unspecified':'int *'
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "chainOld(_:)"
// V4-NEXT: SwiftPrivateAttr {{.*}} Inherited
// V4-NEXT: AvailabilityAttr {{.*}} Inherited swift 0 0 0 Unavailable "" "" 0
// V4-EMPTY:
// V5-NEXT: Dumping mrgChainFn:
// V5-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in C5 mrgChainFn 'int * _Nonnull (int * _Nonnull)' external-linkage
// V5-NEXT: ParmVarDecl {{.*}} imported in C5 p 'int * _Null_unspecified':'int *'
// V5-NEXT: SwiftNameAttr {{.*}} Inherited "chain(_:)"
// V5-NEXT: SwiftPrivateAttr {{.*}} Inherited
// V5-NEXT: SwiftAttrAttr {{.*}} Inherited "safe"
// V5-EMPTY:
// V6-NEXT: Dumping mrgChainFn:
// V6-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in C5 mrgChainFn 'int * _Nonnull (int * _Nonnull)' external-linkage
// V6-NEXT: ParmVarDecl {{.*}} imported in C5 p 'int * _Nonnull':'int *'
// V6-NEXT: SwiftNameAttr {{.*}} Inherited "chain(_:)"
// V6-NEXT: SwiftAttrAttr {{.*}} Inherited "safe"
// V6-EMPTY:
// V4-NEXT: Dumping mrgTag:
// V4-NEXT: RecordDecl {{.*}} imported in TGA struct mrgTag external-linkage
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 IsReplacedByActive 0
// V4-NEXT: SwiftNameAttr {{.*}} "TagNew"
// V4-NEXT: SwiftNameAttr {{.*}} "TagOld"
// V4-EMPTY:
// V5-NEXT: Dumping mrgTag:
// V5-NEXT: RecordDecl {{.*}} imported in TGA struct mrgTag external-linkage
// V5-NEXT: SwiftNameAttr {{.*}} "TagNew"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 0
// V5-NEXT: SwiftNameAttr {{.*}} "TagOld"
// V5-EMPTY:
// V6-NEXT: Dumping mrgTag:
// V6-NEXT: RecordDecl {{.*}} imported in TGA struct mrgTag external-linkage
// V6-NEXT: SwiftNameAttr {{.*}} "TagNew"
// V6-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 0
// V6-NEXT: SwiftNameAttr {{.*}} "TagOld"
// V6-EMPTY:
// V4-NEXT: Dumping mrgTag:
// V4-NEXT: RecordDecl {{.*}} prev {{.*}} imported in TGB {{.*}} struct mrgTag definition external-linkage
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "TagOld"
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4 IsReplacedByActive 0
// V4-NEXT: SwiftPrivateAttr {{.*}}
// V4-NEXT: FieldDecl {{.*}} imported in TGB x 'int'
// V4-EMPTY:
// V5-NEXT: Dumping mrgTag:
// V5-NEXT: RecordDecl {{.*}} prev {{.*}} imported in TGB {{.*}} struct mrgTag definition external-linkage
// V5-NEXT: SwiftNameAttr {{.*}} Inherited "TagNew"
// V5-NEXT: SwiftPrivateAttr {{.*}}
// V5-NEXT: SwiftVersionedRemovalAttr {{.*}} Implicit 4 406 0
// V5-NEXT: FieldDecl {{.*}} imported in TGB x 'int'
// V5-EMPTY:
// V6-NEXT: Dumping mrgTag:
// V6-NEXT: RecordDecl {{.*}} prev {{.*}} imported in TGB {{.*}} struct mrgTag definition external-linkage
// V6-NEXT: SwiftNameAttr {{.*}} Inherited "TagNew"
// V6-NEXT: SwiftPrivateAttr {{.*}}
// V6-NEXT: SwiftVersionedRemovalAttr {{.*}} Implicit 4 406 0
// V6-NEXT: FieldDecl {{.*}} imported in TGB x 'int'
// V6-EMPTY:

// BUILTIN:      Dumping strchr:
// BUILTIN-NEXT: FunctionDecl {{.*}} imported in BI2 implicit strchr 'char *(const char *, int)' extern external-linkage
// BUILTIN-NEXT: ParmVarDecl {{.*}} imported in BI2 'const char *'
// BUILTIN-NEXT: ParmVarDecl {{.*}} imported in BI2 'int'
// BUILTIN-NEXT: BuiltinAttr {{.*}} Implicit {{[0-9]+}}
// BUILTIN-NEXT: NoThrowAttr {{.*}} Implicit
// BUILTIN-EMPTY:
// BUILTIN-NEXT: Dumping strchr:
// BUILTIN-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in BI2 strchr 'char *(const char *, int)' external-linkage
// BUILTIN-NEXT: ParmVarDecl {{.*}} imported in BI2 s 'const char * _Nonnull':'const char *'
// BUILTIN-NEXT: ParmVarDecl {{.*}} imported in BI2 c 'int'
// BUILTIN-NEXT: BuiltinAttr {{.*}} Inherited Implicit {{[0-9]+}}
// BUILTIN-NEXT: NoThrowAttr {{.*}} Inherited Implicit
// BUILTIN-EMPTY:
// BUILTIN-NEXT: Dumping strchr:
// BUILTIN-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in BI2 strchr 'char *(const char *, int)' external-linkage
// BUILTIN-NEXT: ParmVarDecl {{.*}} imported in BI2 s 'const char * _Nonnull':'const char *'
// BUILTIN-NEXT: ParmVarDecl {{.*}} imported in BI2 c 'int'
// BUILTIN-NEXT: BuiltinAttr {{.*}} Inherited Implicit {{[0-9]+}}
// BUILTIN-NEXT: NoThrowAttr {{.*}} Inherited Implicit
// BUILTIN-EMPTY:

//--- module.modulemap
module BI2 { header "BI2.h" export * }
module CA { header "CA.h" export * }
module CB { header "CB.h" export * }
module NA { header "NA.h" export * }
module NB { header "NB.h" export * }
module NC { header "NC.h" export * }
module AA { header "AA.h" export * }
module AB { header "AB.h" export * }
module RA { header "RA.h" export * }
module RB { header "RB.h" export * }
module SW { header "SW.h" export * }
module C1 { header "C1.h" export * }
module C2 { header "C2.h" export * }
module C3 { header "C3.h" export * }
module C4 { header "C4.h" export * }
module C5 { header "C5.h" export * }
module TGA { header "TGA.h" export * }
module TGB { header "TGB.h" export * }

//--- BI2.h
char *strchr(const char *s, int c);
char *strchr(const char *s, int c);

//--- BI2.apinotes
Name: BI2
Functions:
- Name: strchr
  NullabilityOfRet: O
  Nullability: [N, S]

//--- CA.h
int * _Nullable mrgChainRet(void);
void mrgChainParam(int * _Nullable p);

//--- CA.apinotes
Name: CA
Functions:
- Name: mrgChainParam
  Parameters:
  - Position: 0
    Nullability: U

//--- CB.h
#include "CA.h"
int *mrgChainRet(void);
int *mrgChainRet(void);
void mrgChainParam(int * _Nullable p);
void mrgChainParam(int *p);

//--- CB.apinotes
Name: CB
Functions:
- Name: mrgChainRet
  NullabilityOfRet: N

//--- NA.h
void mrgNotesParam(int *p);

//--- NA.apinotes
Name: NA
Functions:
- Name: mrgNotesParam
  Parameters:
  - Position: 0
    Nullability: U

//--- NB.h
#include "NA.h"
void mrgNotesParam(int *p);

//--- NB.apinotes
Name: NB
Functions:
- Name: mrgNotesParam
  Parameters:
  - Position: 0
    Nullability: N

//--- NC.h
#include "NB.h"
void mrgNotesParam(int *p);

//--- AA.h
void mrgAvail(void);

//--- AA.apinotes
Name: AA
Functions:
- Name: mrgAvail
  Availability: nonswift
  AvailabilityMsg: 'fromNotes'

//--- AB.h
#include "AA.h"
void mrgAvail(void) __attribute__((availability(swift, unavailable, message="fromHeader")));
void mrgAvail(void);

//--- RA.h
typedef const struct __attribute__((objc_bridge(id))) __CFThing *CFThingRef;
CFThingRef mrgRcFn(void);
CFThingRef mrgRcInh(void) __attribute__((cf_returns_retained));

//--- RA.apinotes
Name: RA
Functions:
- Name: mrgRcFn
  RetainCountConvention: CFReturnsNotRetained

//--- RB.h
#include "RA.h"
CFThingRef mrgRcFn(void) __attribute__((cf_returns_retained));
CFThingRef mrgRcFn(void);
CFThingRef mrgRcInh(void);
CFThingRef mrgRcInh(void);

//--- RB.apinotes
Name: RB
Functions:
- Name: mrgRcInh
  RetainCountConvention: CFReturnsNotRetained

//--- SW.h
void *mrgSwRet(void);
void *mrgSwRet(void);
void mrgSwParam(void *p);
void mrgSwParam(void *p);

//--- SW.apinotes
Name: SW
Functions:
- Name: mrgSwRet
  ResultType: 'int *'
- Name: mrgSwParam
  Parameters:
  - Position: 0
    Type: 'int *'

//--- C1.h
int *mrgChainFn(int *p);

//--- C1.apinotes
Name: C1
Functions:
- Name: mrgChainFn
  SwiftName: 'chain(_:)'
  NullabilityOfRet: N
SwiftVersions:
- Version: 4
  Functions:
  - Name: mrgChainFn
    SwiftName: 'chainOld(_:)'

//--- C2.h
#include "C1.h"
int *mrgChainFn(int *p);

//--- C2.apinotes
Name: C2
SwiftVersions:
- Version: 5
  Functions:
  - Name: mrgChainFn
    SwiftPrivate: true

//--- C3.h
#include "C2.h"
int *mrgChainFn(int *p);

//--- C3.apinotes
Name: C3
Functions:
- Name: mrgChainFn
  SwiftSafety: safe
SwiftVersions:
- Version: 4
  Functions:
  - Name: mrgChainFn
    Availability: nonswift

//--- C4.h
#include "C3.h"
int *mrgChainFn(int *p);

//--- C4.apinotes
Name: C4
SwiftVersions:
- Version: 6
  Functions:
  - Name: mrgChainFn
    SwiftPrivate: false

//--- C5.h
#include "C4.h"
int *mrgChainFn(int *p);

//--- C5.apinotes
Name: C5
Functions:
- Name: mrgChainFn
  Parameters:
  - Position: 0
    Nullability: N
SwiftVersions:
- Version: 5
  Functions:
  - Name: mrgChainFn
    Parameters:
    - Position: 0
      Nullability: U

//--- TGA.h
struct mrgTag;

//--- TGA.apinotes
Name: TGA
Tags:
- Name: mrgTag
  SwiftName: TagNew
SwiftVersions:
- Version: 4
  Tags:
  - Name: mrgTag
    SwiftName: TagOld

//--- TGB.h
#include "TGA.h"
struct mrgTag { int x; };

//--- TGB.apinotes
Name: TGB
Tags:
- Name: mrgTag
  SwiftPrivate: true
SwiftVersions:
- Version: 4
  Tags:
  - Name: mrgTag
    SwiftPrivate: false

//--- use.c
#include "BI2.h"
#include "CB.h"
#include "NC.h"
#include "AB.h"
#include "RB.h"
#include "SW.h"
#include "C5.h"
#include "TGB.h"
