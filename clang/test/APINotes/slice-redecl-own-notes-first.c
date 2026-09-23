// A redeclaration applies its own API notes first, and only then inherits what
// its previous declaration has live: that declaration's attributes as written,
// then its notes' winners. A capture-mode consumer collapses in that order.
//
//   rdPrivate   G2B's 'SwiftPrivate: false' has nothing of its own to remove,
//               so the swift_private G2A's header wrote is still inherited.
//   rdChain     G2C inherits G2B's unavailability, which G2B's own notes gave
//               it and kept G2A's from reaching it.
//   rdNullable  a parameter takes its previous declaration's notes'
//               nullability when it writes none of its own, over the nullability
//               the previous declaration's header wrote...
//   rdAssumed   ...or assumed.
//
// A C function's redeclaration also takes its previous declaration's type,
// parameters included, whatever its own parameters and notes say:
//
//   rdOwn       its parameter keeps the nullability it writes, and its type
//               takes the previous declaration's notes'.
//   rdOwnNotes, rdRet  its own notes reach its parameter, but not its type.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump -ast-dump-filter rd -x c | FileCheck %s
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.c -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump -ast-dump-filter rd -x c | FileCheck --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedAdditionAttr --implicit-check-not=SwiftNullabilityAttr %s

// CHECK:      FunctionDecl {{.*}} prev {{.*}} imported in G2B rdPrivate
// CHECK-NEXT: SwiftPrivateAttr {{.*}} Inherited
// CHECK-EMPTY:
// CHECK:      FunctionDecl {{.*}} prev {{.*}} imported in G2B rdChain
// CHECK-NEXT: UnavailableAttr {{.*}} "goneB"
// CHECK-EMPTY:
// CHECK-NEXT: Dumping rdNullable:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} rdNullable 'void (int * _Nonnull)'
// CHECK-NEXT: ParmVarDecl {{.*}} p 'int * _Nonnull':'int *'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping rdAssumed:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} rdAssumed 'void (int * _Nullable)'
// CHECK-NEXT: ParmVarDecl {{.*}} p 'int * _Nullable':'int *'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping rdOwn:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} rdOwn 'void (int * _Nonnull)'
// CHECK-NEXT: ParmVarDecl {{.*}} p 'int * _Nullable':'int *'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping rdOwnNotes:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} rdOwnNotes 'void (int *)'
// CHECK-NEXT: ParmVarDecl {{.*}} p 'int * _Nonnull':'int *'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping rdRet:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} rdRet 'int *(void)'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping rdChain:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in G2C rdChain
// CHECK-NEXT: UnavailableAttr {{.*}} Inherited "goneB"
// CHECK-EMPTY:

//--- module.modulemap
module G2A { header "G2A.h" export * }
module G2B { header "G2B.h" export * }
module G2C { header "G2C.h" export * }

//--- G2A.h
void rdPrivate(void) __attribute__((swift_private));
void rdChain(void);
void rdNullable(int * _Nullable p);
#pragma clang assume_nonnull begin
void rdAssumed(int *p);
#pragma clang assume_nonnull end
void rdOwn(int *p);
void rdOwnNotes(int *p);
int *rdRet(void);

//--- G2A.apinotes
Name: G2A
Functions:
- Name: rdChain
  Availability: none
  AvailabilityMsg: 'goneA'
- Name: rdNullable
  Parameters:
  - Position: 0
    Nullability: N
- Name: rdAssumed
  Parameters:
  - Position: 0
    Nullability: O
- Name: rdOwn
  Parameters:
  - Position: 0
    Nullability: N

//--- G2B.h
#include "G2A.h"
void rdPrivate(void);
void rdChain(void);
void rdNullable(int *p);
void rdAssumed(int *p);
void rdOwn(int * _Nullable p);
void rdOwnNotes(int *p);
int *rdRet(void);

//--- G2B.apinotes
Name: G2B
Functions:
- Name: rdPrivate
  SwiftPrivate: false
- Name: rdChain
  Availability: none
  AvailabilityMsg: 'goneB'
- Name: rdOwnNotes
  Parameters:
  - Position: 0
    Nullability: N
- Name: rdRet
  NullabilityOfRet: N

//--- G2C.h
#include "G2B.h"
void rdChain(void);

//--- use.c
#include "G2C.h"
