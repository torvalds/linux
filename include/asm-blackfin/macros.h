/************************************************************************
 *
 * macros.h
 *
 * (c) Copyright 2001-2003 Analog Devices, Inc.  All rights reserved.
 *
 ************************************************************************/

/* Defines various assembly macros. */

#ifndef _MACROS_H
#define _MACROS_H

#define LO(con32) ((con32) & 0xFFFF)
#define lo(con32) ((con32) & 0xFFFF)
#define HI(con32) (((con32) >> 16) & 0xFFFF)
#define hi(con32) (((con32) >> 16) & 0xFFFF)

/*
 * Set the corresponding bits in a System Register (SR);
 * All bits set in "mask" will be set in the system register
 * specified by "sys_reg" bitset_SR(sys_reg, mask), where
 * sys_reg is the system register and mask are the bits to be set.
 */
#define bitset_SR(sys_reg, mask)\
		[--SP] = (R7:6);\
		r7 = sys_reg;\
		r6.l = (mask) & 0xffff;\
		r6.h = (mask) >> 16;\
		r7 = r7 | r6;\
		sys_reg = r7;\
		csync;\
		(R7:6) = [SP++]

/*
 * Clear the corresponding bits in a System Register (SR);
 * All bits set in "mask" will be cleared in the SR
 * specified by "sys_reg" bitclr_SR(sys_reg, mask), where
 * sys_reg is the SR and mask are the bits to be cleared.
 */
#define bitclr_SR(sys_reg, mask)\
		[--SP] = (R7:6);\
		r7 = sys_reg;\
		r7 =~ r7;\
		r6.l = (mask) & 0xffff;\
		r6.h = (mask) >> 16;\
		r7 = r7 | r6;\
		r7 =~ r7;\
		sys_reg = r7;\
		csync;\
		(R7:6) = [SP++]

/*
 * Set the corresponding bits in a Memory Mapped Register (MMR);
 * All bits set in "mask" will be set in the MMR specified by "mmr_reg"
 * bitset_MMR(mmr_reg, mask), where mmr_reg is the MMR and mask are
 * the bits to be set.
 */
#define bitset_MMR(mmr_reg, mask)\
		[--SP] = (R7:6);\
		[--SP] = P5;\
		p5.l = mmr_reg & 0xffff;\
		p5.h = mmr_reg >> 16;\
		r7 = [p5];\
		r6.l = (mask) & 0xffff;\
		r6.h = (mask) >> 16;\
		r7 = r7 | r6;\
		[p5] = r7;\
		csync;\
		p5 = [SP++];\
		(R7:6) = [SP++]

/*
 * Clear the corresponding bits in a Memory Mapped Register (MMR);
 * All bits set in "mask" will be cleared in the MMR specified by "mmr_reg"
 * bitclr_MMRreg(mmr_reg, mask), where sys_reg is the MMR and mask are
 * the bits to be cleared.
 */
#define bitclr_MMR(mmr_reg, mask)\
		[--SP] = (R7:6);\
		[--SP] = P5;\
		p5.l = mmr_reg & 0xffff;\
		p5.h = mmr_reg >> 16;\
		r7 = [p5];\
		r7 =~ r7;\
		r6.l = (mask) & 0xffff;\
		r6.h = (mask) >> 16;\
		r7 = r7 | r6;\
		r7 =~ r7;\
		[p5] = r7;\
		csync;\
		p5 = [SP++];\
		(R7:6) = [SP++]

#endif				/* _MACROS_H */
