// A declaration can receive its attributes from an update record rather than
// from its own record. Here TplA owns the specialization Tpl<int> but never
// instantiates it; TplB does, and its update record carries the definition
// together with the attributes the instantiation copied from the pattern,
// captured API notes slices included. Those have to collapse too.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default4 -fapinotes-modules -I %t %t/use.cpp -ast-dump -ast-dump-filter Tpl -x c++ | FileCheck --check-prefix=V4 %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=4 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture4 -fapinotes-modules -I %t %t/use.cpp -ast-dump -ast-dump-filter Tpl -x c++ | FileCheck --check-prefix=V4 --implicit-check-not=SwiftVersionedSliceAttr %s
// RUN: %clang_cc1 -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/default5 -fapinotes-modules -I %t %t/use.cpp -ast-dump -ast-dump-filter Tpl -x c++ | FileCheck --check-prefix=V5 %s
// RUN: %clang_cc1 -fswift-version-independent-apinotes -fapinotes-swift-version=5 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/capture5 -fapinotes-modules -I %t %t/use.cpp -ast-dump -ast-dump-filter Tpl -x c++ | FileCheck --check-prefix=V5 --implicit-check-not=SwiftVersionedSliceAttr %s

// V4: ClassTemplateSpecializationDecl {{.*}} struct Tpl definition
// V4: TemplateArgument type 'int'
// V4-NEXT: BuiltinType {{.*}} 'int'
// V4-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 IsReplacedByActive 0{{$}}
// V4-NEXT: SwiftNameAttr {{.*}} "TplUnversioned"
// V4-NEXT: SwiftNameAttr {{.*}} "TplV4"
// V4-EMPTY:

// V5: ClassTemplateSpecializationDecl {{.*}} struct Tpl definition
// V5: TemplateArgument type 'int'
// V5-NEXT: BuiltinType {{.*}} 'int'
// V5-NEXT: SwiftNameAttr {{.*}} "TplUnversioned"
// V5-NEXT: SwiftVersionedAdditionAttr {{.*}} Implicit 4.0 0{{$}}
// V5-NEXT: SwiftNameAttr {{.*}} "TplV4"
// V5-EMPTY:

//--- module.modulemap
module TplA {
  header "TplA.h"
}
module TplB {
  header "TplB.h"
  export *
}

//--- TplA.apinotes
Name: TplA
Tags:
  - Name: Tpl
    SwiftName: TplUnversioned
SwiftVersions:
  - Version: 4.0
    Tags:
      - Name: Tpl
        SwiftName: TplV4

//--- TplA.h
template <class T> struct Tpl { T x; };
struct TplHolder { Tpl<int> *p; };

//--- TplB.h
#include "TplA.h"
inline int tplUse() { return sizeof(Tpl<int>); }

//--- use.cpp
#include "TplB.h"
int z = tplUse();
