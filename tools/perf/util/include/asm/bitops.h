#ifndef _PERF_ASM_BITOPS_H_
#define _PERF_ASM_BITOPS_H_

#include <sys/types.h>
#include "../../types.h"
#include <linux/compiler.h>

/* CHECKME: Not sure both always match */
#define BITS_PER_LONG	__WORDSIZE

#include "../../../../include/asm-generic/bitops/__fls.h"
#include "../../../../include/asm-generic/bitops/fls.h"
#include "../../../../include/asm-generic/bitops/fls64.h"
#include "../../../../include/asm-generic/bitops/__ffs.h"
#include "../../../../include/asm-generic/bitops/ffz.h"
#include "../../../../include/asm-generic/bitops/hweight.h"

#endif
