#define memcpy MEMCPY /* don't hide glibc's memcpy() */
#define altinstr_replacement text
#define globl p2align 4; .globl
#include "../../../arch/x86/lib/memcpy_64.S"
/*
 * We need to provide note.GNU-stack section, saying that we want
 * NOT executable stack. Otherwise the final linking will assume that
 * the ELF stack should not be restricted at all and set it RWX.
 */
.section .note.GNU-stack,"",@progbits
