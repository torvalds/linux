/* SPDX-License-Identifier: GPL-2.0 */

/* Various wrappers to make the kernel .S file build in user-space: */

// memcpy_orig is being defined as SYM_L_LOCAL but we need it
#define SYM_FUNC_START_LOCAL(name)                      \
        SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)
#define memcpy MEMCPY /* don't hide glibc's memcpy() */
#define altinstr_replacement text
#define globl p2align 4; .globl
#define _ASM_EXTABLE_FAULT(x, y)
#define _ASM_EXTABLE(x, y)

#include "../../arch/x86/lib/memcpy_64.S"
/*
 * We need to provide note.GNU-stack section, saying that we want
 * NOT executable stack. Otherwise the final linking will assume that
 * the ELF stack should not be restricted at all and set it RWX.
 */
.section .note.GNU-stack,"",@progbits
