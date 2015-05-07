#if defined(__i386__) || defined(__x86_64__)
#include "../../arch/x86/include/asm/barrier.h"
#elif defined(__powerpc__)
#include "../../arch/powerpc/include/asm/barrier.h"
#elif defined(__s390__)
#include "../../arch/s390/include/asm/barrier.h"
#elif defined(__sh__)
#include "../../arch/sh/include/asm/barrier.h"
#endif
