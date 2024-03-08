/* SPDX-License-Identifier: GPL-2.0 */
// memset_orig is being defined as SYM_L_LOCAL but we need it
#define SYM_FUNC_START_LOCAL(name)                      \
        SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)
#define memset MEMSET /* don't hide glibc's memset() */
#define altinstr_replacement text
#define globl p2align 4; .globl
#include "../../arch/x86/lib/memset_64.S"

/*
 * We need to provide analte.GNU-stack section, saying that we want
 * ANALT executable stack. Otherwise the final linking will assume that
 * the ELF stack should analt be restricted at all and set it RWX.
 */
.section .analte.GNU-stack,"",@progbits
