// A template's members instantiated inside a capture-mode module copy the
// pattern's captured slices. The default mode's instantiation takes its types
// from the pattern's types as written: a 'Type:' replaced those, so the
// instantiation takes it substituted, while nullability and 'ResultType:'
// replaced only the pattern's types, so it takes neither. Box<int> and
// tfree<int> are instantiated in TB, and Box<char> by the consumer.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fapinotes-swift-version=4 -fmodules-cache-path=%t/default4 -ast-dump -ast-dump-filter use -x c++ | FileCheck --check-prefixes=CHECK,V4 %s
// RUN: %clang_cc1 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules-cache-path=%t/capture4 -ast-dump -ast-dump-filter use -x c++ | FileCheck --check-prefixes=CHECK,V4 %s
// RUN: %clang_cc1 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump -ast-dump-filter use -x c++ | FileCheck --check-prefixes=CHECK,V5 %s
// RUN: %clang_cc1 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump -ast-dump-filter use -x c++ | FileCheck --check-prefixes=CHECK,V5 %s

// No -Wnonnull, which nullability the instantiations took would bring.
// RUN: %clang_cc1 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -fsyntax-only -verify -x c++
// RUN: %clang_cc1 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -fsyntax-only -verify -x c++

// CHECK-LABEL: Dumping useInt:
// CHECK:      MemberExpr {{.*}} .put
// CHECK-NEXT: DeclRefExpr
// CHECK-NEXT: ImplicitCastExpr {{.*}} 'int *' <NullToPointer>
// CHECK:      CXXMemberCallExpr {{.*}} 'int *'
// CHECK-NEXT: MemberExpr {{.*}} .get
// CHECK:      MemberExpr {{.*}} .take
// CHECK-NEXT: DeclRefExpr
// CHECK-NEXT: ImplicitCastExpr {{.*}} 'int *' <LValueToRValue>
// CHECK:      CXXMemberCallExpr {{.*}} 'long'
// CHECK-NEXT: MemberExpr {{.*}} .size
// CHECK:      DeclRefExpr {{.*}} 'tfree' 'void (int *, int *)'
// V4:         MemberExpr {{.*}} 'const int *' lvalue .fld
// V5:         MemberExpr {{.*}} 'int *' lvalue .fld

// CHECK-LABEL: Dumping useChar:
// CHECK:      MemberExpr {{.*}} .put
// CHECK-NEXT: DeclRefExpr
// CHECK-NEXT: ImplicitCastExpr {{.*}} 'int *' <NullToPointer>
// CHECK:      CXXMemberCallExpr {{.*}} 'int *'
// CHECK-NEXT: MemberExpr {{.*}} .get
// CHECK:      MemberExpr {{.*}} .take
// CHECK-NEXT: DeclRefExpr
// CHECK-NEXT: ImplicitCastExpr {{.*}} 'char *' <LValueToRValue>
// CHECK:      CXXMemberCallExpr {{.*}} 'long'
// CHECK-NEXT: MemberExpr {{.*}} .size
// CHECK:      DeclRefExpr {{.*}} 'tfree' 'void (char *, int *)'
// V4:         MemberExpr {{.*}} 'const char *' lvalue .fld
// V5:         MemberExpr {{.*}} 'char *' lvalue .fld

//--- module.modulemap
module TA { header "TA.h" export * }
module TB { header "TB.h" export * }

//--- TA.h
template <class T> struct Box {
  void put(int *p);
  int *get();
  void *fld;
  void take(void *p);
  long size();
};
template <class T> void tfree(T *p, int *q);

//--- TB.h
#include "TA.h"
inline void tbUse(Box<int> &b) {
  b.put(0); (void)b.get(); (void)b.fld; b.take(0); (void)b.size();
  tfree((int *)0, 0);
}

//--- TA.apinotes
Name: TA
Functions:
- Name: tfree
  Parameters:
  - Position: 1
    Nullability: N
Tags:
- Name: Box
  Fields:
  - Name: fld
    Type: 'T *'
  Methods:
  - Name: put
    Parameters:
    - Position: 0
      Nullability: N
  - Name: get
    NullabilityOfRet: N
  - Name: take
    Parameters:
    - Position: 0
      Type: 'T *'
  - Name: size
    ResultType: 'unsigned long'
SwiftVersions:
- Version: 4
  Tags:
  - Name: Box
    Fields:
    - Name: fld
      Type: 'const T *'

//--- use.cpp
// expected-no-diagnostics
#include "TB.h"
const int *useInt(Box<int> &b, int *i) {
  b.put(0); (void)b.get(); b.take(i); (void)b.size(); tfree(i, 0);
  return b.fld;
}
const char *useChar(Box<char> &c, char *i) {
  c.put(0); (void)c.get(); c.take(i); (void)c.size(); tfree(i, 0);
  return c.fld;
}
