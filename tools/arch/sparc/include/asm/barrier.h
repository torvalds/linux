#ifndef ___TOOLS_LINUX_ASM_SPARC_BARRIER_H
#define ___TOOLS_LINUX_ASM_SPARC_BARRIER_H
#if defined(__sparc__) && defined(__arch64__)
#include "barrier_64.h"
#else
#include "barrier_32.h"
#endif
#endif
