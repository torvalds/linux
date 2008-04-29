#ifndef _ASM_M68K_UNALIGNED_H
#define _ASM_M68K_UNALIGNED_H

/*
 * The m68k can do unaligned accesses itself.
 */
#include <linux/unaligned/access_ok.h>
#include <linux/unaligned/generic.h>

#define get_unaligned	__get_unaligned_be
#define put_unaligned	__put_unaligned_be

#endif /* _ASM_M68K_UNALIGNED_H */
