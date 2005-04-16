#ifndef __UM_RWSEM_H__
#define __UM_RWSEM_H__

#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 96)
#define __builtin_expect(exp,c) (exp)
#endif

#include "asm/arch/rwsem.h"

#endif
