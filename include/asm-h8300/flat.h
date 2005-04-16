/*
 * include/asm-h8300/flat.h -- uClinux flat-format executables
 */

#ifndef __H8300_FLAT_H__
#define __H8300_FLAT_H__

#define	flat_stack_align(sp)			/* nothing needed */
#define	flat_argvp_envp_on_stack()		1
#define	flat_old_ram_flag(flags)		1
#define	flat_reloc_valid(reloc, size)		((reloc) <= (size))

/*
 * on the H8 a couple of the relocations have an instruction in the
 * top byte.  As there can only be 24bits of address space,  we just
 * always preserve that 8bits at the top,  when it isn't an instruction
 * is is 0 (davidm@snapgear.com)
 */

#define	flat_get_relocate_addr(rel)		(rel)
#define flat_get_addr_from_rp(rp, relval, flags) \
        (get_unaligned(rp) & ((flags & FLAT_FLAG_GOTPIC) ? 0xffffffff: 0x00ffffff))
#define flat_put_addr_at_rp(rp, addr, rel) \
	put_unaligned (((*(char *)(rp)) << 24) | ((addr) & 0x00ffffff), rp)

#endif /* __H8300_FLAT_H__ */
