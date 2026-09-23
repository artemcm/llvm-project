#include "SliceOwner.h"

// Redeclared in a module that owns a sidecar of its own, so this declaration
// gets its own slice group before it inherits the owner's.
void renumberProbe(void);
