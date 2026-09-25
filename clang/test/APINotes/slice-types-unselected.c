// Capture mode parses every slice's 'Type:' and 'ResultType:', where the
// default mode parses only the one it selects. A slice that would not be
// selected for this compilation's own Swift version must not fail the build
// the default mode would pass: it is warned about, and a client that selects
// it keeps the declaration's type, as the default mode does once it has
// diagnosed the type. The selected slice is diagnosed as the default mode
// would.
//
//   unknownProbe   'ResultType:' names a type that does not exist.
//   sizeProbe      'ResultType:' has a different size from the original.
//   varProbe       'Type:' names a type that does not exist.
//   goodProbe      a valid slice, which still applies.

// RUN: rm -rf %t && split-file %s %t

// The default mode never parses the Swift 3 slice at Swift 5.
// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=TyKit -o %t/default5.pcm -fapinotes-modules -fapinotes-swift-version=5 -I %t %t/module.modulemap -verify=default
// Nor does capture, at Swift 5 or at no version, but it warns.
// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=TyKit -o %t/capture5.pcm -fapinotes-modules -fswift-version-independent-apinotes -fapinotes-swift-version=5 -I %t %t/module.modulemap -verify=capture
// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=TyKit -o %t/capture.pcm -fapinotes-modules -fswift-version-independent-apinotes -I %t %t/module.modulemap -verify=capture
// Silencing the warning leaves nothing.
// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=TyKit -o %t/quiet.pcm -fapinotes-modules -fswift-version-independent-apinotes -Wno-apinotes-version-independent -I %t %t/module.modulemap -verify=default

// At Swift 3 both modes select the bad slice, and both diagnose it.
// RUN: not %clang_cc1 -emit-module -fmodules -fmodule-name=TyKit -o %t/default3.pcm -fapinotes-modules -fapinotes-swift-version=3 -I %t %t/module.modulemap 2>&1 | FileCheck --check-prefix=ERR3 --implicit-check-not=warning: %s
// RUN: not %clang_cc1 -emit-module -fmodules -fmodule-name=TyKit -o %t/capture3.pcm -fapinotes-modules -fswift-version-independent-apinotes -fapinotes-swift-version=3 -I %t %t/module.modulemap 2>&1 | FileCheck --check-prefix=ERR3 --implicit-check-not=warning: %s

// A Swift 3 client of the capture module keeps every bad slice's original
// type, and still takes the good slice's.
// RUN: %clang_cc1 -fmodules -fmodule-file=TyKit=%t/capture.pcm -fmodule-map-file=%t/module.modulemap -fapinotes-modules -fapinotes-swift-version=3 -I %t %t/use.c -ast-dump -ast-dump-filter Probe | FileCheck --check-prefix=V3 --implicit-check-not=SwiftVersionedSliceAttr %s
// RUN: %clang_cc1 -fmodules -fmodule-file=TyKit=%t/default5.pcm -fmodule-map-file=%t/module.modulemap -fapinotes-modules -fapinotes-swift-version=5 -I %t %t/use.c -ast-dump -ast-dump-filter Probe | FileCheck --check-prefix=V5 %s
// RUN: %clang_cc1 -fmodules -fmodule-file=TyKit=%t/capture.pcm -fmodule-map-file=%t/module.modulemap -fapinotes-modules -fapinotes-swift-version=5 -I %t %t/use.c -ast-dump -ast-dump-filter Probe | FileCheck --check-prefix=V5 --implicit-check-not=SwiftVersionedSliceAttr %s

// ERR3-DAG: error: unknown type name 'NoSuchType'
// ERR3-DAG: error: unknown type name 'NoSuchVarType'
// ERR3-DAG: error: API notes replacement type 'char' has a different size from original type 'void *'

// V3: Dumping unknownProbe:
// V3: FunctionDecl {{.*}} unknownProbe 'void *(void)'
// V3: Dumping sizeProbe:
// V3: FunctionDecl {{.*}} sizeProbe 'void *(void)'
// V3: Dumping varProbe:
// V3: VarDecl {{.*}} varProbe 'int *'
// V3: Dumping goodProbe:
// V3: FunctionDecl {{.*}} goodProbe 'char *(void)'

// V5: Dumping unknownProbe:
// V5: FunctionDecl {{.*}} unknownProbe 'void *(void)'
// V5: Dumping sizeProbe:
// V5: FunctionDecl {{.*}} sizeProbe 'void *(void)'
// V5: Dumping varProbe:
// V5: VarDecl {{.*}} varProbe 'int *'
// V5: Dumping goodProbe:
// V5: FunctionDecl {{.*}} goodProbe 'void *(void)'

//--- module.modulemap
module TyKit { header "TyKit.h" export * }

//--- TyKit.h
// default-no-diagnostics
void *unknownProbe(void); // capture-warning {{API notes type 'NoSuchType *' for Swift version 3 is not valid here; a client that selects it keeps the declaration's type}}
void *sizeProbe(void); // capture-warning {{API notes type 'char' for Swift version 3 is not valid here}}
extern int *varProbe; // capture-warning {{API notes type 'NoSuchVarType *' for Swift version 3 is not valid here}}
void *goodProbe(void);

//--- TyKit.apinotes
Name: TyKit
SwiftVersions:
  - Version: 3
    Functions:
      - Name: unknownProbe
        ResultType: 'NoSuchType *'
      - Name: sizeProbe
        ResultType: 'char'
      - Name: goodProbe
        ResultType: 'char *'
    Globals:
      - Name: varProbe
        Type: 'NoSuchVarType *'

//--- use.c
#include "TyKit.h"
