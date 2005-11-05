#ifndef __ASM_UM_LINKAGE_H
#define __ASM_UM_LINKAGE_H

#include "asm/arch/linkage.h"

#include <linux/config.h>

/* <linux/linkage.h> will pick sane defaults */
#ifdef CONFIG_GPROF
#undef FASTCALL
#undef fastcall
#endif

#endif
