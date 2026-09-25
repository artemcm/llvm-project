// A module or PCH built without -fswift-version-independent-apinotes applied
// its API notes at one Swift version, and shows every importer that version's
// view of its declarations. Loading it at another version, or into a build
// that captures API notes for every version, gets a warning. Nothing depends
// on the version when no API notes differ by it, or when they were captured.

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=Versioned -o %t/versioned4.pcm -fapinotes-modules -fapinotes-swift-version=4 -I %t %t/module.modulemap -x c
// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=Versioned -o %t/versioned-capture.pcm -fapinotes-modules -fswift-version-independent-apinotes -I %t %t/module.modulemap -x c
// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=Unversioned -o %t/unversioned4.pcm -fapinotes-modules -fapinotes-swift-version=4 -I %t %t/module.modulemap -x c

// RUN: %clang_cc1 -fsyntax-only -fmodules -fmodule-file=Versioned=%t/versioned4.pcm -fmodule-map-file=%t/module.modulemap -fapinotes-modules -fapinotes-swift-version=5 -I %t %t/use.c 2>&1 | FileCheck --check-prefix=MISMATCH %s
// MISMATCH: warning: {{.*}}versioned4.pcm applied API notes for Swift 4, but this compilation requests Swift 5 [-Wapinotes-swift-version-mismatch]

// RUN: %clang_cc1 -emit-module -fmodules -fmodule-name=Client -fmodule-file=Versioned=%t/versioned4.pcm -o %t/client.pcm -fapinotes-modules -fswift-version-independent-apinotes -I %t %t/module.modulemap -x c 2>&1 | FileCheck --check-prefix=CAPTURING %s
// CAPTURING: warning: {{.*}}versioned4.pcm applied API notes for Swift 4, but this compilation captures API notes for every Swift version

// RUN: %clang_cc1 -fsyntax-only -Werror=apinotes-swift-version-mismatch -fmodules -fmodule-file=Versioned=%t/versioned4.pcm -fmodule-map-file=%t/module.modulemap -fapinotes-modules -fapinotes-swift-version=4 -I %t %t/use.c
// RUN: %clang_cc1 -fsyntax-only -Werror=apinotes-swift-version-mismatch -fmodules -fmodule-file=Versioned=%t/versioned-capture.pcm -fmodule-map-file=%t/module.modulemap -fapinotes-modules -fapinotes-swift-version=5 -I %t %t/use.c
// RUN: %clang_cc1 -fsyntax-only -Werror=apinotes-swift-version-mismatch -fmodules -fmodule-file=Unversioned=%t/unversioned4.pcm -fmodule-map-file=%t/module.modulemap -fapinotes-modules -fapinotes-swift-version=5 -I %t %t/use-unversioned.c

// The same holds for a PCH.
// RUN: %clang_cc1 -fapinotes -fapinotes-swift-version=4 -I %t/pch -x c-header %t/pch/PCHHeader.h -emit-pch -o %t/versioned4.pch
// RUN: %clang_cc1 -fsyntax-only -fapinotes -fapinotes-swift-version=5 -I %t/pch -include-pch %t/versioned4.pch %t/use-pch.c 2>&1 | FileCheck --check-prefix=PCH %s
// PCH: warning: {{.*}}versioned4.pch applied API notes for Swift 4, but this compilation requests Swift 5

//--- module.modulemap
module Versioned { header "Versioned.h" }
module Unversioned { header "Unversioned.h" }
module Client { header "Client.h" }

//--- Versioned.apinotes
Name: Versioned
Functions:
  - Name: versionedFunc
    SwiftName: 'versionedUnversioned()'
SwiftVersions:
  - Version: 4.0
    Functions:
      - Name: versionedFunc
        SwiftName: 'versionedFour()'

//--- Versioned.h
void versionedFunc(void);

//--- Unversioned.apinotes
Name: Unversioned
Functions:
  - Name: unversionedFunc
    SwiftName: 'unversioned()'

//--- Unversioned.h
void unversionedFunc(void);

//--- Client.h
#include "Versioned.h"

//--- use.c
#include "Versioned.h"
void caller(void) { versionedFunc(); }

//--- use-unversioned.c
#include "Unversioned.h"
void caller(void) { unversionedFunc(); }

//--- pch/APINotes.apinotes
Name: PCHNotes
SwiftVersions:
  - Version: 4.0
    Functions:
      - Name: pchFunc
        SwiftName: 'pchFour()'

//--- pch/PCHHeader.h
void pchFunc(void);

//--- use-pch.c
void caller(void) { pchFunc(); }
