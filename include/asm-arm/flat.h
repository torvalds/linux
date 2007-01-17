/*
 * include/asm-arm/flat.h -- uClinux flat-format executables
 */

#ifndef __ARM_FLAT_H__
#define __ARM_FLAT_H__

/* An odd number of words will be pushed after this alignment, so
   deliberately misalign the value.  */
#define	flat_stack_align(sp)	sp = (void *)(((unsigned long)(sp) - 4) | 4)
#define	flat_argvp_envp_on_stack()		1
#define	flat_old_ram_flag(flags)		(flags)
#define	flat_reloc_valid(reloc, size)		((reloc) <= (size))
#define	flat_get_addr_from_rp(rp, relval, flags) get_unaligned(rp)
#define	flat_put_addr_at_rp(rp, val, relval)	put_unaligned(val,rp)
#define	flat_get_relocate_addr(rel)		(rel)

#endif /* __ARM_FLAT_H__ */
