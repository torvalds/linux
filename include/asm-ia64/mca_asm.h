/*
 * File:	mca_asm.h
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) Vijay Chander (vijay@engr.sgi.com)
 * Copyright (C) Srinivasa Thirumalachar <sprasad@engr.sgi.com>
 * Copyright (C) 2000 Hewlett-Packard Co.
 * Copyright (C) 2000 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2002 Intel Corp.
 * Copyright (C) 2002 Jenna Hall <jenna.s.hall@intel.com>
 */
#ifndef _ASM_IA64_MCA_ASM_H
#define _ASM_IA64_MCA_ASM_H

#define PSR_IC		13
#define PSR_I		14
#define	PSR_DT		17
#define PSR_RT		27
#define PSR_MC		35
#define PSR_IT		36
#define PSR_BN		44

/*
 * This macro converts a instruction virtual address to a physical address
 * Right now for simulation purposes the virtual addresses are
 * direct mapped to physical addresses.
 *	1. Lop off bits 61 thru 63 in the virtual address
 */
#define INST_VA_TO_PA(addr)							\
	dep	addr	= 0, addr, 61, 3
/*
 * This macro converts a data virtual address to a physical address
 * Right now for simulation purposes the virtual addresses are
 * direct mapped to physical addresses.
 *	1. Lop off bits 61 thru 63 in the virtual address
 */
#define DATA_VA_TO_PA(addr)							\
	tpa	addr	= addr
/*
 * This macro converts a data physical address to a virtual address
 * Right now for simulation purposes the virtual addresses are
 * direct mapped to physical addresses.
 *	1. Put 0x7 in bits 61 thru 63.
 */
#define DATA_PA_TO_VA(addr,temp)							\
	mov	temp	= 0x7	;;							\
	dep	addr	= temp, addr, 61, 3

#define GET_THIS_PADDR(reg, var)		\
	mov	reg = IA64_KR(PER_CPU_DATA);;	\
        addl	reg = THIS_CPU(var), reg

/*
 * This macro jumps to the instruction at the given virtual address
 * and starts execution in physical mode with all the address
 * translations turned off.
 *	1.	Save the current psr
 *	2.	Make sure that all the upper 32 bits are off
 *
 *	3.	Clear the interrupt enable and interrupt state collection bits
 *		in the psr before updating the ipsr and iip.
 *
 *	4.	Turn off the instruction, data and rse translation bits of the psr
 *		and store the new value into ipsr
 *		Also make sure that the interrupts are disabled.
 *		Ensure that we are in little endian mode.
 *		[psr.{rt, it, dt, i, be} = 0]
 *
 *	5.	Get the physical address corresponding to the virtual address
 *		of the next instruction bundle and put it in iip.
 *		(Using magic numbers 24 and 40 in the deposint instruction since
 *		 the IA64_SDK code directly maps to lower 24bits as physical address
 *		 from a virtual address).
 *
 *	6.	Do an rfi to move the values from ipsr to psr and iip to ip.
 */
#define  PHYSICAL_MODE_ENTER(temp1, temp2, start_addr, old_psr)				\
	mov	old_psr = psr;								\
	;;										\
	dep	old_psr = 0, old_psr, 32, 32;						\
											\
	mov	ar.rsc = 0 ;								\
	;;										\
	srlz.d;										\
	mov	temp2 = ar.bspstore;							\
	;;										\
	DATA_VA_TO_PA(temp2);								\
	;;										\
	mov	temp1 = ar.rnat;							\
	;;										\
	mov	ar.bspstore = temp2;							\
	;;										\
	mov	ar.rnat = temp1;							\
	mov	temp1 = psr;								\
	mov	temp2 = psr;								\
	;;										\
											\
	dep	temp2 = 0, temp2, PSR_IC, 2;						\
	;;										\
	mov	psr.l = temp2;								\
	;;										\
	srlz.d;										\
	dep	temp1 = 0, temp1, 32, 32;						\
	;;										\
	dep	temp1 = 0, temp1, PSR_IT, 1;						\
	;;										\
	dep	temp1 = 0, temp1, PSR_DT, 1;						\
	;;										\
	dep	temp1 = 0, temp1, PSR_RT, 1;						\
	;;										\
	dep	temp1 = 0, temp1, PSR_I, 1;						\
	;;										\
	dep	temp1 = 0, temp1, PSR_IC, 1;						\
	;;										\
	dep	temp1 = -1, temp1, PSR_MC, 1;						\
	;;										\
	mov	cr.ipsr = temp1;							\
	;;										\
	LOAD_PHYSICAL(p0, temp2, start_addr);						\
	;;										\
	mov	cr.iip = temp2;								\
	mov	cr.ifs = r0;								\
	DATA_VA_TO_PA(sp);								\
	DATA_VA_TO_PA(gp);								\
	;;										\
	srlz.i;										\
	;;										\
	nop	1;									\
	nop	2;									\
	nop	1;									\
	nop	2;									\
	rfi;										\
	;;

/*
 * This macro jumps to the instruction at the given virtual address
 * and starts execution in virtual mode with all the address
 * translations turned on.
 *	1.	Get the old saved psr
 *
 *	2.	Clear the interrupt state collection bit in the current psr.
 *
 *	3.	Set the instruction translation bit back in the old psr
 *		Note we have to do this since we are right now saving only the
 *		lower 32-bits of old psr.(Also the old psr has the data and
 *		rse translation bits on)
 *
 *	4.	Set ipsr to this old_psr with "it" bit set and "bn" = 1.
 *
 *	5.	Reset the current thread pointer (r13).
 *
 *	6.	Set iip to the virtual address of the next instruction bundle.
 *
 *	7.	Do an rfi to move ipsr to psr and iip to ip.
 */

#define VIRTUAL_MODE_ENTER(temp1, temp2, start_addr, old_psr)	\
	mov	temp2 = psr;					\
	;;							\
	mov	old_psr = temp2;				\
	;;							\
	dep	temp2 = 0, temp2, PSR_IC, 2;			\
	;;							\
	mov	psr.l = temp2;					\
	mov	ar.rsc = 0;					\
	;;							\
	srlz.d;							\
	mov	r13 = ar.k6;					\
	mov	temp2 = ar.bspstore;				\
	;;							\
	DATA_PA_TO_VA(temp2,temp1);				\
	;;							\
	mov	temp1 = ar.rnat;				\
	;;							\
	mov	ar.bspstore = temp2;				\
	;;							\
	mov	ar.rnat = temp1;				\
	;;							\
	mov	temp1 = old_psr;				\
	;;							\
	mov	temp2 = 1;					\
	;;							\
	dep	temp1 = temp2, temp1, PSR_IC, 1;		\
	;;							\
	dep	temp1 = temp2, temp1, PSR_IT, 1;		\
	;;							\
	dep	temp1 = temp2, temp1, PSR_DT, 1;		\
	;;							\
	dep	temp1 = temp2, temp1, PSR_RT, 1;		\
	;;							\
	dep	temp1 = temp2, temp1, PSR_BN, 1;		\
	;;							\
								\
	mov     cr.ipsr = temp1;				\
	movl	temp2 = start_addr;				\
	;;							\
	mov	cr.iip = temp2;					\
	;;							\
	DATA_PA_TO_VA(sp, temp1);				\
	DATA_PA_TO_VA(gp, temp2);				\
	srlz.i;							\
	;;							\
	nop	1;						\
	nop	2;						\
	nop	1;						\
	rfi							\
	;;

/*
 * The following offsets capture the order in which the
 * RSE related registers from the old context are
 * saved onto the new stack frame.
 *
 *	+-----------------------+
 *	|NDIRTY [BSP - BSPSTORE]|
 *	+-----------------------+
 *	|	RNAT		|
 *	+-----------------------+
 *	|	BSPSTORE	|
 *	+-----------------------+
 *	|	IFS		|
 *	+-----------------------+
 *	|	PFS		|
 *	+-----------------------+
 *	|	RSC		|
 *	+-----------------------+ <-------- Bottom of new stack frame
 */
#define  rse_rsc_offset		0
#define  rse_pfs_offset		(rse_rsc_offset+0x08)
#define  rse_ifs_offset		(rse_pfs_offset+0x08)
#define  rse_bspstore_offset	(rse_ifs_offset+0x08)
#define  rse_rnat_offset	(rse_bspstore_offset+0x08)
#define  rse_ndirty_offset	(rse_rnat_offset+0x08)

/*
 * rse_switch_context
 *
 *	1. Save old RSC onto the new stack frame
 *	2. Save PFS onto new stack frame
 *	3. Cover the old frame and start a new frame.
 *	4. Save IFS onto new stack frame
 *	5. Save the old BSPSTORE on the new stack frame
 *	6. Save the old RNAT on the new stack frame
 *	7. Write BSPSTORE with the new backing store pointer
 *	8. Read and save the new BSP to calculate the #dirty registers
 * NOTE: Look at pages 11-10, 11-11 in PRM Vol 2
 */
#define rse_switch_context(temp,p_stackframe,p_bspstore)			\
	;;									\
	mov     temp=ar.rsc;;							\
	st8     [p_stackframe]=temp,8;;					\
	mov     temp=ar.pfs;;							\
	st8     [p_stackframe]=temp,8;						\
	cover ;;								\
	mov     temp=cr.ifs;;							\
	st8     [p_stackframe]=temp,8;;						\
	mov     temp=ar.bspstore;;						\
	st8     [p_stackframe]=temp,8;;					\
	mov     temp=ar.rnat;;							\
	st8     [p_stackframe]=temp,8;						\
	mov     ar.bspstore=p_bspstore;;					\
	mov     temp=ar.bsp;;							\
	sub     temp=temp,p_bspstore;;						\
	st8     [p_stackframe]=temp,8;;

/*
 * rse_return_context
 *	1. Allocate a zero-sized frame
 *	2. Store the number of dirty registers RSC.loadrs field
 *	3. Issue a loadrs to insure that any registers from the interrupted
 *	   context which were saved on the new stack frame have been loaded
 *	   back into the stacked registers
 *	4. Restore BSPSTORE
 *	5. Restore RNAT
 *	6. Restore PFS
 *	7. Restore IFS
 *	8. Restore RSC
 *	9. Issue an RFI
 */
#define rse_return_context(psr_mask_reg,temp,p_stackframe)			\
	;;									\
	alloc   temp=ar.pfs,0,0,0,0;						\
	add     p_stackframe=rse_ndirty_offset,p_stackframe;;			\
	ld8     temp=[p_stackframe];;						\
	shl     temp=temp,16;;							\
	mov     ar.rsc=temp;;							\
	loadrs;;								\
	add     p_stackframe=-rse_ndirty_offset+rse_bspstore_offset,p_stackframe;;\
	ld8     temp=[p_stackframe];;						\
	mov     ar.bspstore=temp;;						\
	add     p_stackframe=-rse_bspstore_offset+rse_rnat_offset,p_stackframe;;\
	ld8     temp=[p_stackframe];;						\
	mov     ar.rnat=temp;;							\
	add     p_stackframe=-rse_rnat_offset+rse_pfs_offset,p_stackframe;;	\
	ld8     temp=[p_stackframe];;						\
	mov     ar.pfs=temp;;							\
	add     p_stackframe=-rse_pfs_offset+rse_ifs_offset,p_stackframe;;	\
	ld8     temp=[p_stackframe];;						\
	mov     cr.ifs=temp;;							\
	add     p_stackframe=-rse_ifs_offset+rse_rsc_offset,p_stackframe;;	\
	ld8     temp=[p_stackframe];;						\
	mov     ar.rsc=temp ;							\
	mov     temp=psr;;							\
	or      temp=temp,psr_mask_reg;;					\
	mov     cr.ipsr=temp;;							\
	mov     temp=ip;;							\
	add     temp=0x30,temp;;						\
	mov     cr.iip=temp;;							\
	srlz.i;;								\
	rfi;;

#endif /* _ASM_IA64_MCA_ASM_H */
