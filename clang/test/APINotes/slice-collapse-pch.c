// A PCH built with -fswift-version-independent-apinotes captures, like a
// module does, so one PCH serves every Swift version. Its reader applies the
// slices at the version it asks for, whether it includes the PCH or reads it
// as the main file with -x ast, which replaces the language options with the
// file's own. A PCH's user has to match its language options, flag included.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -fapinotes -fswift-version-independent-apinotes -I %t -x c-header %t/PCHHeader.h -emit-pch -o %t/capture.pch
// RUN: %clang_cc1 -fapinotes -fapinotes-swift-version=4 -I %t -x c-header %t/PCHHeader.h -emit-pch -o %t/default4.pch
// RUN: %clang_cc1 -fapinotes -fapinotes-swift-version=5 -I %t -x c-header %t/PCHHeader.h -emit-pch -o %t/default5.pch

// RUN: %clang_cc1 -fapinotes -fapinotes-swift-version=4 -I %t -include-pch %t/default4.pch %t/use.c -ast-dump -ast-dump-filter pchFunc | FileCheck --check-prefix=V4 %s
// RUN: %clang_cc1 -fapinotes -fswift-version-independent-apinotes -fapinotes-swift-version=4 -I %t -include-pch %t/capture.pch %t/use.c -ast-dump -ast-dump-filter pchFunc | FileCheck --check-prefix=V4 --implicit-check-not=SwiftVersionedSliceAttr %s
// RUN: %clang_cc1 -fapinotes -fapinotes-swift-version=5 -I %t -include-pch %t/default5.pch %t/use.c -ast-dump -ast-dump-filter pchFunc | FileCheck --check-prefix=V5 %s
// RUN: %clang_cc1 -fapinotes -fswift-version-independent-apinotes -fapinotes-swift-version=5 -I %t -include-pch %t/capture.pch %t/use.c -ast-dump -ast-dump-filter pchFunc | FileCheck --check-prefix=V5 --implicit-check-not=SwiftVersionedSliceAttr %s

// RUN: %clang_cc1 -fapinotes-swift-version=4 -ast-dump -ast-dump-filter pchFunc -x ast %t/default4.pch | FileCheck --check-prefix=V4 %s
// RUN: %clang_cc1 -fapinotes-swift-version=4 -ast-dump -ast-dump-filter pchFunc -x ast %t/capture.pch | FileCheck --check-prefix=V4 --implicit-check-not=SwiftVersionedSliceAttr %s
// RUN: %clang_cc1 -fapinotes-swift-version=5 -ast-dump -ast-dump-filter pchFunc -x ast %t/default5.pch | FileCheck --check-prefix=V5 %s
// RUN: %clang_cc1 -fapinotes-swift-version=5 -ast-dump -ast-dump-filter pchFunc -x ast %t/capture.pch | FileCheck --check-prefix=V5 --implicit-check-not=SwiftVersionedSliceAttr %s

// V4: Dumping pchFunc:
// V4-NEXT: FunctionDecl {{.*}} pchFunc
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: SwiftNameAttr {{.*}} "pchUnversioned()"
// V4-NEXT: SwiftNameAttr {{.*}} "pchV4()"
// V4-EMPTY:

// V5: Dumping pchFunc:
// V5-NEXT: FunctionDecl {{.*}} pchFunc
// V5-NEXT: SwiftNameAttr {{.*}} "pchUnversioned()"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 0{{$}}
// V5-NEXT: SwiftNameAttr {{.*}} "pchV4()"
// V5-EMPTY:

//--- APINotes.apinotes
Name: PCHNotes
Functions:
  - Name: pchFunc
    SwiftName: 'pchUnversioned()'
SwiftVersions:
  - Version: 4.0
    Functions:
      - Name: pchFunc
        SwiftName: 'pchV4()'

//--- PCHHeader.h
void pchFunc(void);

//--- use.c
void caller(void) { pchFunc(); }
