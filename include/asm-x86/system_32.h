#ifndef __ASM_SYSTEM_H
#define __ASM_SYSTEM_H

#include <asm/segment.h>
#include <asm/cpufeature.h>
#include <asm/cmpxchg.h>

#include <linux/irqflags.h>

/*
 * disable hlt during certain critical i/o operations
 */
#define HAVE_DISABLE_HLT

#endif
