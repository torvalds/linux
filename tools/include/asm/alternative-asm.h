/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _TOOLS_ASM_ALTERNATIVE_ASM_H
#define _TOOLS_ASM_ALTERNATIVE_ASM_H

/* Just disable it so we can build arch/x86/lib/memcpy_64.S for perf bench: */

#define altinstruction_entry #
#define ALTERNATIVE_2 #

#endif
