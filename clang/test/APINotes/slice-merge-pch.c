// A PCH built with -fswift-version-independent-apinotes records the merge of
// a redeclaration with its previous declaration, and every way of reading the
// PCH merges again at the version it asks for: included, read as the main
// file with -x ast, and merged with -ast-merge (regression guard).

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -fapinotes -fswift-version-independent-apinotes -I %t -x c-header %t/PCHHeader.h -emit-pch -o %t/capture.pch
// RUN: %clang_cc1 -fapinotes -fapinotes-swift-version=4 -I %t -x c-header %t/PCHHeader.h -emit-pch -o %t/default4.pch
// RUN: %clang_cc1 -fapinotes -fapinotes-swift-version=5 -I %t -x c-header %t/PCHHeader.h -emit-pch -o %t/default5.pch

// RUN: %clang_cc1 -fapinotes -fapinotes-swift-version=4 -I %t -include-pch %t/default4.pch %t/use.c -ast-dump -ast-dump-filter pchTwice | FileCheck --check-prefix=V4 %s
// RUN: %clang_cc1 -fapinotes -fswift-version-independent-apinotes -fapinotes-swift-version=4 -I %t -include-pch %t/capture.pch %t/use.c -ast-dump -ast-dump-filter pchTwice | FileCheck --check-prefix=V4 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -fapinotes -fapinotes-swift-version=5 -I %t -include-pch %t/default5.pch %t/use.c -ast-dump -ast-dump-filter pchTwice | FileCheck --check-prefix=V5 %s
// RUN: %clang_cc1 -fapinotes -fswift-version-independent-apinotes -fapinotes-swift-version=5 -I %t -include-pch %t/capture.pch %t/use.c -ast-dump -ast-dump-filter pchTwice | FileCheck --check-prefix=V5 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s

// RUN: %clang_cc1 -fapinotes-swift-version=4 -ast-dump -ast-dump-filter pchTwice -x ast %t/capture.pch | FileCheck --check-prefix=V4 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -fapinotes-swift-version=5 -ast-dump -ast-dump-filter pchTwice -x ast %t/capture.pch | FileCheck --check-prefix=V5 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s

// RUN: %clang_cc1 -fapinotes -fswift-version-independent-apinotes -fapinotes-swift-version=4 -I %t -ast-merge %t/capture.pch %t/empty.c -ast-dump -ast-dump-filter pchTwice | FileCheck --check-prefix=V4 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s
// RUN: %clang_cc1 -fapinotes -fswift-version-independent-apinotes -fapinotes-swift-version=5 -I %t -ast-merge %t/capture.pch %t/empty.c -ast-dump -ast-dump-filter pchTwice | FileCheck --check-prefix=V5 --implicit-check-not=SwiftVersionedSliceAttr --implicit-check-not=SwiftVersionedMergeAttr %s

// V4:      Dumping pchTwice:
// V4-NEXT: FunctionDecl {{.*}} pchTwice 'void (int *)'
// V4-NEXT: ParmVarDecl {{.*}} p 'int *'
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: SwiftNameAttr {{.*}} "pchTwice(_:)"
// V4-NEXT: SwiftNameAttr {{.*}} "pchTwice4(_:)"
// V4-EMPTY:
// V4-NEXT: Dumping pchTwice:
// V4-NEXT: FunctionDecl {{.*}} prev {{.*}} pchTwice 'void (int *)'
// V4-NEXT: ParmVarDecl {{.*}} p 'int *'
// V4-NEXT: SwiftNameAttr {{.*}} Inherited "pchTwice4(_:)"
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: SwiftNameAttr {{.*}} "pchTwice(_:)"
// V4-EMPTY:

// V5:      Dumping pchTwice:
// V5-NEXT: FunctionDecl {{.*}} pchTwice 'void (int * _Nonnull)'
// V5-NEXT: ParmVarDecl {{.*}} p 'int * _Nonnull':'int *'
// V5-NEXT: SwiftNameAttr {{.*}} "pchTwice(_:)"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 0{{$}}
// V5-NEXT: SwiftNameAttr {{.*}} "pchTwice4(_:)"
// V5-EMPTY:
// V5-NEXT: Dumping pchTwice:
// V5-NEXT: FunctionDecl {{.*}} prev {{.*}} pchTwice 'void (int * _Nonnull)'
// V5-NEXT: ParmVarDecl {{.*}} p 'int * _Nonnull':'int *'
// V5-NEXT: SwiftNameAttr {{.*}} Inherited "pchTwice(_:)"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 0{{$}}
// V5-NEXT: SwiftNameAttr {{.*}} "pchTwice4(_:)"
// V5-EMPTY:

//--- APINotes.apinotes
Name: PCHNotes
Functions:
  - Name: pchTwice
    SwiftName: 'pchTwice(_:)'
    Parameters:
      - Position: 0
        Nullability: N
SwiftVersions:
  - Version: 4.0
    Functions:
      - Name: pchTwice
        SwiftName: 'pchTwice4(_:)'

//--- PCHHeader.h
void pchTwice(int *p);
void pchTwice(int *p);

//--- use.c
void caller(void) { pchTwice(0); }

//--- empty.c
