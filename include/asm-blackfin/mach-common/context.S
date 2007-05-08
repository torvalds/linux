/*
 * File:         arch/blackfin/kernel/context.S
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * Code to save processor context.
 *  We even save the register which are preserved by a function call
 *	 - r4, r5, r6, r7, p3, p4, p5
 */
.macro save_context_with_interrupts
	[--sp] = SYSCFG;

	[--sp] = P0;	/*orig_p0*/
	[--sp] = R0;	/*orig_r0*/

	[--sp] = ( R7:0, P5:0 );
	[--sp] = fp;
	[--sp] = usp;

	[--sp] = i0;
	[--sp] = i1;
	[--sp] = i2;
	[--sp] = i3;

	[--sp] = m0;
	[--sp] = m1;
	[--sp] = m2;
	[--sp] = m3;

	[--sp] = l0;
	[--sp] = l1;
	[--sp] = l2;
	[--sp] = l3;

	[--sp] = b0;
	[--sp] = b1;
	[--sp] = b2;
	[--sp] = b3;
	[--sp] = a0.x;
	[--sp] = a0.w;
	[--sp] = a1.x;
	[--sp] = a1.w;

	[--sp] = LC0;
	[--sp] = LC1;
	[--sp] = LT0;
	[--sp] = LT1;
	[--sp] = LB0;
	[--sp] = LB1;

	[--sp] = ASTAT;

	[--sp] = r0;	/* Skip reserved */
	[--sp] = RETS;
	r0 = RETI;
	[--sp] = r0;
	[--sp] = RETX;
	[--sp] = RETN;
	[--sp] = RETE;
	[--sp] = SEQSTAT;
	[--sp] = r0;	/* Skip IPEND as well. */
	/* Switch to other method of keeping interrupts disabled.  */
#ifdef CONFIG_DEBUG_HWERR
	r0 = 0x3f;
	sti r0;
#else
	cli r0;
#endif
	[--sp] = RETI;  /*orig_pc*/
	/* Clear all L registers.  */
	r0 = 0 (x);
	l0 = r0;
	l1 = r0;
	l2 = r0;
	l3 = r0;
.endm

.macro save_context_syscall
	[--sp] = SYSCFG;

	[--sp] = P0;	/*orig_p0*/
	[--sp] = R0;	/*orig_r0*/
	[--sp] = ( R7:0, P5:0 );
	[--sp] = fp;
	[--sp] = usp;

	[--sp] = i0;
	[--sp] = i1;
	[--sp] = i2;
	[--sp] = i3;

	[--sp] = m0;
	[--sp] = m1;
	[--sp] = m2;
	[--sp] = m3;

	[--sp] = l0;
	[--sp] = l1;
	[--sp] = l2;
	[--sp] = l3;

	[--sp] = b0;
	[--sp] = b1;
	[--sp] = b2;
	[--sp] = b3;
	[--sp] = a0.x;
	[--sp] = a0.w;
	[--sp] = a1.x;
	[--sp] = a1.w;

	[--sp] = LC0;
	[--sp] = LC1;
	[--sp] = LT0;
	[--sp] = LT1;
	[--sp] = LB0;
	[--sp] = LB1;

	[--sp] = ASTAT;

	[--sp] = r0;	/* Skip reserved */
	[--sp] = RETS;
	r0 = RETI;
	[--sp] = r0;
	[--sp] = RETX;
	[--sp] = RETN;
	[--sp] = RETE;
	[--sp] = SEQSTAT;
	[--sp] = r0;	/* Skip IPEND as well. */
	[--sp] = RETI;  /*orig_pc*/
	/* Clear all L registers.  */
	r0 = 0 (x);
	l0 = r0;
	l1 = r0;
	l2 = r0;
	l3 = r0;
.endm

.macro save_context_no_interrupts
	[--sp] = SYSCFG;
	[--sp] = P0;	/* orig_p0 */
	[--sp] = R0;	/* orig_r0 */
	[--sp] = ( R7:0, P5:0 );
	[--sp] = fp;
	[--sp] = usp;

	[--sp] = i0;
	[--sp] = i1;
	[--sp] = i2;
	[--sp] = i3;

	[--sp] = m0;
	[--sp] = m1;
	[--sp] = m2;
	[--sp] = m3;

	[--sp] = l0;
	[--sp] = l1;
	[--sp] = l2;
	[--sp] = l3;

	[--sp] = b0;
	[--sp] = b1;
	[--sp] = b2;
	[--sp] = b3;
	[--sp] = a0.x;
	[--sp] = a0.w;
	[--sp] = a1.x;
	[--sp] = a1.w;

	[--sp] = LC0;
	[--sp] = LC1;
	[--sp] = LT0;
	[--sp] = LT1;
	[--sp] = LB0;
	[--sp] = LB1;

	[--sp] = ASTAT;

#ifdef CONFIG_KGDB
	fp     = 0(Z);
	r1     = sp;
	r1    += 60;
	r1    += 60;
	r1    += 60;
	[--sp] = r1;
#else
	[--sp] = r0;	/* Skip reserved */
#endif
	[--sp] = RETS;
	r0 = RETI;
	[--sp] = r0;
	[--sp] = RETX;
	[--sp] = RETN;
	[--sp] = RETE;
	[--sp] = SEQSTAT;
#ifdef CONFIG_KGDB
	r1.l = lo(IPEND);
	r1.h = hi(IPEND);
	[--sp] = r1;
#else
	[--sp] = r0;	/* Skip IPEND as well. */
#endif
	[--sp] = r0;  /*orig_pc*/
	/* Clear all L registers.  */
	r0 = 0 (x);
	l0 = r0;
	l1 = r0;
	l2 = r0;
	l3 = r0;
.endm

.macro restore_context_no_interrupts
	sp += 4;	/* Skip orig_pc */
	sp += 4;	/* Skip IPEND */
	SEQSTAT = [sp++];
	RETE = [sp++];
	RETN = [sp++];
	RETX = [sp++];
	r0 = [sp++];
	RETI = r0;	/* Restore RETI indirectly when in exception */
	RETS = [sp++];

	sp += 4;	/* Skip Reserved */

	ASTAT = [sp++];

	LB1 = [sp++];
	LB0 = [sp++];
	LT1 = [sp++];
	LT0 = [sp++];
	LC1 = [sp++];
	LC0 = [sp++];

	a1.w = [sp++];
	a1.x = [sp++];
	a0.w = [sp++];
	a0.x = [sp++];
	b3 = [sp++];
	b2 = [sp++];
	b1 = [sp++];
	b0 = [sp++];

	l3 = [sp++];
	l2 = [sp++];
	l1 = [sp++];
	l0 = [sp++];

	m3 = [sp++];
	m2 = [sp++];
	m1 = [sp++];
	m0 = [sp++];

	i3 = [sp++];
	i2 = [sp++];
	i1 = [sp++];
	i0 = [sp++];

	sp += 4;
	fp = [sp++];

	( R7 : 0, P5 : 0) = [ SP ++ ];
	sp += 8;	/* Skip orig_r0/orig_p0 */
	SYSCFG = [sp++];
.endm

.macro restore_context_with_interrupts
	sp += 4;	/* Skip orig_pc */
	sp += 4;	/* Skip IPEND */
	SEQSTAT = [sp++];
	RETE = [sp++];
	RETN = [sp++];
	RETX = [sp++];
	RETI = [sp++];
	RETS = [sp++];

	p0.h = _irq_flags;
	p0.l = _irq_flags;
	r0 = [p0];
	sti r0;

	sp += 4;	/* Skip Reserved */

	ASTAT = [sp++];

	LB1 = [sp++];
	LB0 = [sp++];
	LT1 = [sp++];
	LT0 = [sp++];
	LC1 = [sp++];
	LC0 = [sp++];

	a1.w = [sp++];
	a1.x = [sp++];
	a0.w = [sp++];
	a0.x = [sp++];
	b3 = [sp++];
	b2 = [sp++];
	b1 = [sp++];
	b0 = [sp++];

	l3 = [sp++];
	l2 = [sp++];
	l1 = [sp++];
	l0 = [sp++];

	m3 = [sp++];
	m2 = [sp++];
	m1 = [sp++];
	m0 = [sp++];

	i3 = [sp++];
	i2 = [sp++];
	i1 = [sp++];
	i0 = [sp++];

	sp += 4;
	fp = [sp++];

	( R7 : 0, P5 : 0) = [ SP ++ ];
	sp += 8;	/* Skip orig_r0/orig_p0 */
	csync;
	SYSCFG = [sp++];
	csync;
.endm

