#ifndef ASM_X86__STATFS_H
#define ASM_X86__STATFS_H

/*
 * We need compat_statfs64 to be packed, because the i386 ABI won't
 * add padding at the end to bring it to a multiple of 8 bytes, but
 * the x86_64 ABI will.
 */
#define ARCH_PACK_COMPAT_STATFS64 __attribute__((packed,aligned(4)))

#include <asm-generic/statfs.h>
#endif /* ASM_X86__STATFS_H */
