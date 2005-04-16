/*
 * include/asm-sh/flat.h
 *
 * uClinux flat-format executables
 *
 * Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive for
 * more details.
 */
#ifndef __ASM_SH_FLAT_H
#define __ASM_SH_FLAT_H

#define	flat_stack_align(sp)			/* nothing needed */
#define	flat_argvp_envp_on_stack()		1
#define	flat_old_ram_flag(flags)		(flags)
#define	flat_reloc_valid(reloc, size)		((reloc) <= (size))
#define	flat_get_addr_from_rp(rp, relval, flags)	get_unaligned(rp)
#define	flat_put_addr_at_rp(rp, val, relval)	put_unaligned(val,rp)
#define	flat_get_relocate_addr(rel)		(rel)

#endif /* __ASM_SH_FLAT_H */
