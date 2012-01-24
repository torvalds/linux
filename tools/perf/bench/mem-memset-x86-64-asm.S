#define memset MEMSET /* don't hide glibc's memset() */
#define altinstr_replacement text
#define globl p2align 4; .globl
#define Lmemset_c globl memset_c; memset_c
#define Lmemset_c_e globl memset_c_e; memset_c_e
#include "../../../arch/x86/lib/memset_64.S"
