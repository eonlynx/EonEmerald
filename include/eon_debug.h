#ifndef GUARD_EON_DEBUG_H
#define GUARD_EON_DEBUG_H

#include "global.h"

// Temporary diagnostics for the invalid-pointer crash. Set to FALSE to
// compile the calls out again once the corruption has been located.
#ifndef EON_DEBUG_MEMORY_LOG
#define EON_DEBUG_MEMORY_LOG FALSE
#endif

#if EON_DEBUG_MEMORY_LOG

void EonLog(const char *fmt, ...);
bool32 EonIsValidPointer(const void *ptr);

#else

#define EonLog(...) ((void)0)
#define EonIsValidPointer(ptr) TRUE

#endif // EON_DEBUG_MEMORY_LOG

#endif // GUARD_EON_DEBUG_H
