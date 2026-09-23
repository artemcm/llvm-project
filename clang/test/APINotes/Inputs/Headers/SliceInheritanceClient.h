#include "SliceInheritance.h"

// Redeclarations in a module that owns no API notes of its own. No reader
// covers this location, so these get no slices directly.
void sliceInheritanceProbe(void);
int *sliceMixedProbe(void);
int *sliceNoInheritProbe(void);
