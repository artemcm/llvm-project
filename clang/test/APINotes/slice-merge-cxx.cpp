// C++ redeclarations merge once API notes apply too.
//
//   cxMerge     a C++ redeclaration's type is built from its parameters as its
//               own notes leave them; the nullability its parameters then
//               take from the previous declaration doesn't reach it.
//   cxDeduced   a function with a deduced return type waits for that type
//               before it collapses, and so does its redeclaration, which
//               merges with it.
//   cxTM        a class template's member defined out of line merges with its
//               declaration in the class, and its instantiation takes what
//               that merge gives, as the default mode instantiates the merged
//               attributes.
//   cxFT        a function template's specialization takes its parameters'
//               types as written, which the nullability its definition merged
//               from the declaration doesn't reach.

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump-all -ast-dump-filter cx -x c++ | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump-all -ast-dump-filter cx -x c++ | FileCheck --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fapinotes-swift-version=5 -fmodules-cache-path=%t/default5 -ast-dump-all -ast-dump-filter CxTM -x c++ | FileCheck --check-prefix=TM %s
// RUN: %clang_cc1 -std=c++17 -fmodules -fimplicit-module-maps -fdisable-module-hash -fapinotes-modules -I %t %t/use.cpp -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules-cache-path=%t/capture5 -ast-dump-all -ast-dump-filter CxTM -x c++ | FileCheck --check-prefix=TM --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s

// CHECK:      Dumping cxMerge:
// CHECK-NEXT: FunctionDecl {{.*}} imported in XA used cxMerge 'void (int *, int * _Nonnull)' external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XA a 'int *'
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XA b 'int * _Nonnull':'int *'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping cxMerge:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in XB used cxMerge 'void (int * _Nonnull, int *)' external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XB a 'int * _Nonnull':'int *'
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XB b 'int * _Nonnull':'int *'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping cxDeduced:
// CHECK-NEXT: FunctionDecl {{.*}} imported in XD used cxDeduced 'int (int * _Nonnull)' inline external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XD p 'int * _Nonnull':'int *'
// CHECK-NEXT: SwiftNameAttr {{.*}} "deduced(_:)"
// CHECK-EMPTY:
// CHECK-NEXT: Dumping cxDeduced:
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in XD used cxDeduced 'int (int * _Nonnull)' inline external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XD p 'int * _Nonnull':'int *'
// CHECK-NEXT: CompoundStmt {{.*}}
// CHECK-NEXT: ReturnStmt {{.*}}
// CHECK-NEXT: IntegerLiteral {{.*}} 'int' 1
// CHECK-NEXT: SwiftNameAttr {{.*}} Inherited "deduced(_:)"
// CHECK-EMPTY:
// CHECK-NEXT: Dumping CxTM::cxTM:
// CHECK-NEXT: CXXMethodDecl {{.*}} imported in XT cxTM 'void (int *)' external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XT p 'int *'
// CHECK-NEXT: SwiftPrivateAttr {{.*}}
// CHECK-NEXT: SwiftNameAttr {{.*}} "tm(_:)"
// CHECK-EMPTY:
// CHECK-NEXT: Dumping CxTM::cxTM:
// CHECK-NEXT: CXXMethodDecl {{.*}} parent {{.*}} prev {{.*}} imported in XT cxTM 'void (int *)' external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XT p 'int *'
// CHECK-NEXT: CompoundStmt {{.*}}
// CHECK-NEXT: SwiftNameAttr {{.*}} Inherited "tm(_:)"
// CHECK-NEXT: SwiftPrivateAttr {{.*}}
// CHECK-EMPTY:
// CHECK-NEXT: Dumping cxUseTM:
// CHECK-NEXT: FunctionDecl {{.*}} imported in XT cxUseTM 'void (CxTM{{.*}} &)' inline external-linkage
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XT used t 'CxTM{{.*}} &'
// CHECK-NEXT: CompoundStmt {{.*}}
// CHECK-NEXT: CXXMemberCallExpr {{.*}} 'void'
// CHECK-NEXT: MemberExpr {{.*}} '{{.*}}' .cxTM {{.*}}
// CHECK-NEXT: DeclRefExpr {{.*}} 'CxTM{{.*}}' lvalue ParmVar {{.*}} 't' 'CxTM{{.*}} &'
// CHECK-NEXT: ImplicitCastExpr {{.*}} 'int *' {{.*}}
// CHECK-NEXT: CXXNullPtrLiteralExpr {{.*}} 'std::nullptr_t'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping cxFT:
// CHECK-NEXT: FunctionTemplateDecl {{.*}} imported in XF cxFT external-linkage
// CHECK-NEXT: TemplateTypeParmDecl {{.*}} imported in XF referenced class depth 0 index 0 T
// CHECK-NEXT: FunctionDecl {{.*}} imported in XF cxFT 'void (T *, int * _Nonnull)'
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XF p 'T *'
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XF q 'int * _Nonnull':'int *'
// CHECK-NEXT: SwiftNameAttr {{.*}} "ft(_:_:)"
// CHECK-NEXT: FunctionDecl {{.*}} imported in XF used cxFT 'void (int *, int *)' implicit_instantiation instantiated_from {{.*}} external-linkage
// CHECK-NEXT: TemplateArgument type 'int'
// CHECK-NEXT: BuiltinType {{.*}} 'int'
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XF p 'int *'
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XF q 'int *'
// CHECK-NEXT: CompoundStmt {{.*}}
// CHECK-NEXT: SwiftNameAttr {{.*}} Inherited "ft(_:_:)"
// CHECK-EMPTY:
// CHECK-NEXT: Dumping cxFT:
// CHECK-NEXT: FunctionTemplateDecl {{.*}} prev {{.*}} imported in XF cxFT external-linkage
// CHECK-NEXT: TemplateTypeParmDecl {{.*}} imported in XF referenced class depth 0 index 0 T
// CHECK-NEXT: FunctionDecl {{.*}} prev {{.*}} imported in XF cxFT 'void (T *, int * _Nonnull)'
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XF p 'T *'
// CHECK-NEXT: ParmVarDecl {{.*}} imported in XF q 'int * _Nonnull':'int *'
// CHECK-NEXT: CompoundStmt {{.*}}
// CHECK-NEXT: SwiftNameAttr {{.*}} Inherited "ft(_:_:)"
// CHECK-NEXT: Function {{.*}} 'cxFT' 'void (int *, int *)'
// CHECK-EMPTY:
// CHECK-NEXT: Dumping cxUseFT:
// CHECK-NEXT: FunctionDecl {{.*}} imported in XF cxUseFT 'void ()' inline external-linkage
// CHECK-NEXT: CompoundStmt {{.*}}
// CHECK-NEXT: CallExpr {{.*}} 'void'
// CHECK-NEXT: ImplicitCastExpr {{.*}} 'void (*)(int *, int *)' {{.*}}
// CHECK-NEXT: DeclRefExpr {{.*}} 'void (int *, int *)' lvalue Function {{.*}} 'cxFT' 'void (int *, int *)' (FunctionTemplate {{.*}} 'cxFT')
// CHECK-NEXT: ImplicitCastExpr {{.*}} 'int *' {{.*}}
// CHECK-NEXT: CXXNullPtrLiteralExpr {{.*}} 'std::nullptr_t'
// CHECK-NEXT: ImplicitCastExpr {{.*}} 'int *' {{.*}}
// CHECK-NEXT: CXXNullPtrLiteralExpr {{.*}} 'std::nullptr_t'
// CHECK-EMPTY:

// TM:      ClassTemplateSpecializationDecl {{.*}} struct CxTM definition
// TM:      CXXMethodDecl {{.*}} used cxTM 'void (int *)' implicit_instantiation
// TM-NEXT: ParmVarDecl {{.*}} p 'int *'
// TM-NEXT: CompoundStmt
// TM-NEXT: SwiftNameAttr {{.*}} Inherited "tm(_:)"
// TM-NEXT: SwiftPrivateAttr
// TM-EMPTY:

//--- module.modulemap
module XA { header "XA.h" export * }
module XB { header "XB.h" export * }
module XD { header "XD.h" export * }
module XT { header "XT.h" export * }
module XF { header "XF.h" export * }

//--- XA.h
void cxMerge(int *a, int *b);

//--- XA.apinotes
Name: XA
Functions:
- Name: cxMerge
  Parameters:
  - Position: 1
    Nullability: N

//--- XB.h
#include "XA.h"
void cxMerge(int *a, int *b);

//--- XB.apinotes
Name: XB
Functions:
- Name: cxMerge
  Parameters:
  - Position: 0
    Nullability: N

//--- XD.h
inline auto cxDeduced(int *p);
inline auto cxDeduced(int *p) { return 1; }

//--- XD.apinotes
Name: XD
Functions:
- Name: cxDeduced
  SwiftName: 'deduced(_:)'
  Parameters:
  - Position: 0
    Nullability: N

//--- XT.h
template <class T> struct CxTM {
  void cxTM(int *p);
};
template <class T> void CxTM<T>::cxTM(int *p) {}
inline void cxUseTM(CxTM<int> &t) { t.cxTM(nullptr); }

//--- XT.apinotes
Name: XT
Tags:
- Name: CxTM
  Methods:
  - Name: cxTM
    SwiftName: 'tm(_:)'
    SwiftPrivate: true

//--- XF.h
template <class T> void cxFT(T *p, int *q);
template <class T> void cxFT(T *p, int *q) {}
inline void cxUseFT() { cxFT<int>(nullptr, nullptr); }

//--- XF.apinotes
Name: XF
Functions:
- Name: cxFT
  SwiftName: 'ft(_:_:)'
  Parameters:
  - Position: 1
    Nullability: N

//--- use.cpp
#include "XB.h"
#include "XD.h"
#include "XT.h"
#include "XF.h"
void use() { cxMerge(nullptr, nullptr); cxDeduced(nullptr); }
