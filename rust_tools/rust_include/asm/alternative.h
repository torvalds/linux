/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_ASM_ALTERNATIVE_ASM_H
#define _TOOLS_ASM_ALTERNATIVE_ASM_H

#if defined(__s390x__)
#ifdef __ASSEMBLY__
.macro ALTERNATIVE oldinstr, newinstr, feature
	\oldinstr
.endm
#endif
#else

/* Just disable it so we can build arch/x86/lib/memcpy_64.S for perf bench: */

#define ALTERNATIVE #

#endif

#endif
