/*
 * include/asm-v850/flat.h -- uClinux flat-format executables
 *
 *  Copyright (C) 2002,03  NEC Electronics Corporation
 *  Copyright (C) 2002,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_FLAT_H__
#define __V850_FLAT_H__

/* The amount by which a relocation can exceed the program image limits
   without being regarded as an error.  On the v850, the relocations of
   some base-pointers can be offset by 0x8000 (to allow better usage of the
   space offered by 16-bit signed offsets -- in most cases the offsets used
   with such a base-pointer will be negative).  */

#define	flat_reloc_valid(reloc, size)	((reloc) <= (size + 0x8000))

#define	flat_stack_align(sp)		/* nothing needed */
#define	flat_argvp_envp_on_stack()	0
#define	flat_old_ram_flag(flags)	(flags)

/* We store the type of relocation in the top 4 bits of the `relval.' */

/* Convert a relocation entry into an address.  */
static inline unsigned long
flat_get_relocate_addr (unsigned long relval)
{
	return relval & 0x0fffffff; /* Mask out top 4-bits */
}

#define flat_v850_get_reloc_type(relval) ((relval) >> 28)

#define FLAT_V850_R_32		0 /* Normal 32-bit reloc */
#define FLAT_V850_R_HI16S_LO15	1 /* High 16-bits + signed 15-bit low field */
#define FLAT_V850_R_HI16S_LO16	2 /* High 16-bits + signed 16-bit low field */

/* Extract the address to be relocated from the symbol reference at RP;
   RELVAL is the raw relocation-table entry from which RP is derived.
   For the v850, RP should always be half-word aligned.  */
static inline unsigned long flat_get_addr_from_rp (unsigned long *rp,
						   unsigned long relval,
						   unsigned long flags)
{
	short *srp = (short *)rp;

	switch (flat_v850_get_reloc_type (relval))
	{
	case FLAT_V850_R_32:
		/* Simple 32-bit address.  */
		return srp[0] | (srp[1] << 16);

	case FLAT_V850_R_HI16S_LO16:
		/* The high and low halves of the address are in the 16
		   bits at RP, and the 2nd word of the 32-bit instruction
		   following that, respectively.  The low half is _signed_
		   so we have to sign-extend it and add it to the upper
		   half instead of simply or-ing them together.

		   Unlike most relocated address, this one is stored in
		   native (little-endian) byte-order to avoid problems with
		   trashing the low-order bit, so we have to convert to
		   network-byte-order before returning, as that's what the
		   caller expects.  */
		return htonl ((srp[0] << 16) + srp[2]);

	case FLAT_V850_R_HI16S_LO15:
		/* The high and low halves of the address are in the 16
		   bits at RP, and the upper 15 bits of the 2nd word of the
		   32-bit instruction following that, respectively.  The
		   low half is _signed_ so we have to sign-extend it and
		   add it to the upper half instead of simply or-ing them
		   together.  The lowest bit is always zero.

		   Unlike most relocated address, this one is stored in
		   native (little-endian) byte-order to avoid problems with
		   trashing the low-order bit, so we have to convert to
		   network-byte-order before returning, as that's what the
		   caller expects.  */
		return htonl ((srp[0] << 16) + (srp[2] & ~0x1));

	default:
		return ~0;	/* bogus value */
	}
}

/* Insert the address ADDR into the symbol reference at RP;
   RELVAL is the raw relocation-table entry from which RP is derived.
   For the v850, RP should always be half-word aligned.  */
static inline void flat_put_addr_at_rp (unsigned long *rp, unsigned long addr,
					unsigned long relval)
{
	short *srp = (short *)rp;

	switch (flat_v850_get_reloc_type (relval)) {
	case FLAT_V850_R_32:
		/* Simple 32-bit address.  */
		srp[0] = addr & 0xFFFF;
		srp[1] = (addr >> 16);
		break;

	case FLAT_V850_R_HI16S_LO16:
		/* The high and low halves of the address are in the 16
		   bits at RP, and the 2nd word of the 32-bit instruction
		   following that, respectively.  The low half is _signed_
		   so we must carry its sign bit to the upper half before
		   writing the upper half.  */
		srp[0] = (addr >> 16) + ((addr >> 15) & 0x1);
		srp[2] = addr & 0xFFFF;
		break;

	case FLAT_V850_R_HI16S_LO15:
		/* The high and low halves of the address are in the 16
		   bits at RP, and the upper 15 bits of the 2nd word of the
		   32-bit instruction following that, respectively.  The
		   low half is _signed_ so we must carry its sign bit to
		   the upper half before writing the upper half.  The
		   lowest bit we preserve from the existing instruction.  */
		srp[0] = (addr >> 16) + ((addr >> 15) & 0x1);
		srp[2] = (addr & 0xFFFE) | (srp[2] & 0x1);
		break;
	}
}

#endif /* __V850_FLAT_H__ */
