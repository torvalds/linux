/*	$OpenBSD: m88100_machdep.c,v 1.12 2014/05/31 11:19:06 miod Exp $	*/
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/asm_macro.h>
#include <m88k/m88100.h>

#ifdef MULTIPROCESSOR
uint32_t m88100_mp_atomic_begin(__cpu_simple_lock_t *, uint *);
void	m88100_mp_atomic_end(uint32_t, __cpu_simple_lock_t *, uint);
#endif

/*
 *  Data Access Emulation for M88100 exceptions
 */

#define DMT_BYTE	1
#define DMT_HALF	2
#define DMT_WORD	4

const struct {
	unsigned char    offset;
	unsigned char    size;
} dmt_en_info[16] = {
	{0, 0},
	{3, DMT_BYTE},
	{2, DMT_BYTE},
	{2, DMT_HALF},
	{1, DMT_BYTE},
	{0, 0},
	{0, 0},
	{0, 0},
	{0, DMT_BYTE},
	{0, 0},
	{0, 0},
	{0, 0},
	{0, DMT_HALF},
	{0, 0},
	{0, 0},
	{0, DMT_WORD}
};

#ifdef DATA_DEBUG
int data_access_emulation_debug = 0;
#define DAE_DEBUG(stuff) \
	do { \
		if (data_access_emulation_debug != 0) { \
			stuff; \
		} \
	} while (0)
#else
#define DAE_DEBUG(stuff)
#endif

void	dae_print_one(u_int, u_int, u_int, u_int);
void	dae_process(struct trapframe *, u_int, u_int, u_int, u_int);

void
dae_print(u_int *f)
{
	struct trapframe *eframe = (void *)f;

	if (!ISSET(eframe->tf_dmt0, DMT_VALID))
		return;

	dae_print_one(0, eframe->tf_dma0, eframe->tf_dmd0, eframe->tf_dmt0);
	dae_print_one(1, eframe->tf_dma1, eframe->tf_dmd1, eframe->tf_dmt1);
	dae_print_one(2, eframe->tf_dma2, eframe->tf_dmd2, eframe->tf_dmt2);
}

void
dae_print_one(u_int x, u_int dmax, u_int dmdx, u_int dmtx)
{
	u_int enbits;
	const char *width, *usr, *xmem;

	if (!ISSET(dmtx, DMT_VALID))
		return;

	enbits = DMT_ENBITS(dmtx);
	dmax += dmt_en_info[enbits].offset;

	if (dmtx & DMT_DOUB1)
		width = ".d";
	else {
		switch (dmt_en_info[enbits].size) {
		case DMT_BYTE:
			if (dmtx & DMT_SIGNED)
				width = ".b";
			else
				width = ".bu";
			break;
		case DMT_HALF:
			if (dmtx & DMT_SIGNED)
				width = ".h";
			else
				width = ".hu";
			break;
		case DMT_WORD:
			width = "";
			break;
		default:
			width = ".???";
			break;
		}
	}
	if (dmtx & DMT_DAS)
		usr = "";
	else
		usr = ".usr";
	if (dmtx & DMT_LOCKBAR)
		xmem = "(xmem)";
	else
		xmem = "";

	if (ISSET(dmtx, DMT_WRITE))
		printf("[DMT%d=%x: %sst%s%s %08x to %08x]\n",
		    x, dmtx, xmem, width, usr, dmdx, dmax);
	else
		printf("[DMT%d=%x: %sld%s%s r%d <- %x]\n",
		    x, dmtx, xmem, width, usr, DMT_DREGBITS(dmtx), dmax);
}

void
data_access_emulation(u_int *f)
{
	struct trapframe *eframe = (void *)f;

	if (!ISSET(eframe->tf_dmt0, DMT_VALID))
		return;

	dae_process(eframe, 0,
	    eframe->tf_dma0, eframe->tf_dmd0, eframe->tf_dmt0);
	dae_process(eframe, 1,
	    eframe->tf_dma1, eframe->tf_dmd1, eframe->tf_dmt1);
	dae_process(eframe, 2,
	    eframe->tf_dma2, eframe->tf_dmd2, eframe->tf_dmt2);

	eframe->tf_dmt0 = 0;
}

void
dae_process(struct trapframe *eframe, u_int x,
    u_int dmax, u_int dmdx, u_int dmtx)
{
	u_int v, reg, enbits;

	if (!ISSET(dmtx, DMT_VALID))
		return;

	DAE_DEBUG(dae_print_one(x, dmax, dmdx, dmtx));

	enbits = DMT_ENBITS(dmtx);
	dmax += dmt_en_info[enbits].offset;
	reg = DMT_DREGBITS(dmtx);

	if (!ISSET(dmtx, DMT_LOCKBAR)) {
		/* the fault is not during an XMEM */

		if (x == 2 && ISSET(dmtx, DMT_DOUB1)) {
			/* pipeline 2 (earliest stage) for a double */

			if (ISSET(dmtx, DMT_WRITE)) {
				/*
				 * STORE DOUBLE WILL BE REINITIATED BY rte
				 */
			} else {
				/* EMULATE ld.d INSTRUCTION */
				v = do_load_word(dmax, dmtx & DMT_DAS);
				if (reg != 0)
					eframe->tf_r[reg] = v;
				v = do_load_word(dmax ^ 4, dmtx & DMT_DAS);
				if (reg != 31)
					eframe->tf_r[reg + 1] = v;
			}
		} else {
			/* not pipeline #2 with a double */
			if (dmtx & DMT_WRITE) {
				switch (dmt_en_info[enbits].size) {
				case DMT_BYTE:
				DAE_DEBUG(
					printf("[byte %x -> %08x(%c)]\n",
					    dmdx & 0xff, dmax,
					    ISSET(dmtx, DMT_DAS) ? 's' : 'u')
				);
					do_store_byte(dmax, dmdx,
					    dmtx & DMT_DAS);
					break;
				case DMT_HALF:
				DAE_DEBUG(
					printf("[half %x -> %08x(%c)]\n",
					    dmdx & 0xffff, dmax,
					    ISSET(dmtx, DMT_DAS) ? 's' : 'u')
				);
					do_store_half(dmax, dmdx,
					    dmtx & DMT_DAS);
					break;
				case DMT_WORD:
				DAE_DEBUG(
					printf("[word %x -> %08x(%c)]\n",
					    dmdx, dmax,
					    ISSET(dmtx, DMT_DAS) ? 's' : 'u')
				);
					do_store_word(dmax, dmdx,
					    dmtx & DMT_DAS);
					break;
				}
			} else {
				/* else it's a read */
				switch (dmt_en_info[enbits].size) {
				case DMT_BYTE:
					v = do_load_byte(dmax, dmtx & DMT_DAS);
					if (!ISSET(dmtx, DMT_SIGNED))
						v &= 0x000000ff;
					break;
				case DMT_HALF:
					v = do_load_half(dmax, dmtx & DMT_DAS);
					if (!ISSET(dmtx, DMT_SIGNED))
						v &= 0x0000ffff;
					break;
				case DMT_WORD:
					v = do_load_word(dmax, dmtx & DMT_DAS);
					break;
				}
				DAE_DEBUG(
					if (reg == 0)
						printf("[no write to r0 done]\n");
					else
						printf("[r%d <- %08x]\n", reg, v);
				);
				if (reg != 0)
					eframe->tf_r[reg] = v;
			}
		}
	} else {
		/* if lockbar is set... it's part of an XMEM */
		/*
		 * According to Motorola's "General Information",
		 * the DMT_DOUB1 bit is never set in this case, as it
		 * should be.
		 * If lockbar is set (as it is if we're here) and if
		 * the write is not set, then it's the same as if DOUB1
		 * was set...
		 */
		if (!ISSET(dmtx, DMT_WRITE)) {
			if (x < 2) {
				/* RERUN xmem WITH DMD(x+1) */
				dmdx =
				    x == 0 ? eframe->tf_dmd1 : eframe->tf_dmd2;
			} else {
				/* RERUN xmem WITH DMD2 */
			}

			if (dmt_en_info[enbits].size == DMT_WORD) {
				v = do_xmem_word(dmax, dmdx, dmtx & DMT_DAS);
			} else {
				v = do_xmem_byte(dmax, dmdx, dmtx & DMT_DAS);
			}
			DAE_DEBUG(
				if (reg == 0)
					printf("[no write to r0 done]\n");
				else
					printf("[r%d <- %08x]\n", reg, v);
			);
			if (reg != 0)
				eframe->tf_r[reg] = v;
		} else {
			if (x == 0) {
				if (reg != 0)
					eframe->tf_r[reg] = dmdx;
				m88100_rewind_insn(&(eframe->tf_regs));
				/* xmem RERUN ON rte */
				eframe->tf_dmt0 = 0;
				return;
			}
		}
	}
}

/*
 * Routines to patch the kernel code on 88100 systems not affected by
 * the xxx.usr bug.
 */

void
m88100_apply_patches()
{
#ifdef ERRATA__XXX_USR
	if (((get_cpu_pid() & PID_VN) >> VN_SHIFT) > 10) {
		/*
		 * Patch DAE helpers.
		 *	    before		    after
		 *	branch			branch
		 *	NOP			jmp.n r1
		 *	xxx.usr			xxx.usr
		 *	NOP; NOP; NOP
		 *	jmp r1
		 */
		((u_int32_t *)(do_load_word))[1] = 0xf400c401;
		((u_int32_t *)(do_load_half))[1] = 0xf400c401;
		((u_int32_t *)(do_load_byte))[1] = 0xf400c401;
		((u_int32_t *)(do_store_word))[1] = 0xf400c401;
		((u_int32_t *)(do_store_half))[1] = 0xf400c401;
		((u_int32_t *)(do_store_byte))[1] = 0xf400c401;
	}
#endif
}

#ifdef MULTIPROCESSOR
void
m88100_smp_setup(struct cpu_info *ci)
{
	/*
	 * Setup function pointers for mplock operation.
	 */

	ci->ci_mp_atomic_begin = m88100_mp_atomic_begin;
	ci->ci_mp_atomic_end = m88100_mp_atomic_end;
}

uint32_t
m88100_mp_atomic_begin(__cpu_simple_lock_t *lock, uint *csr)
{
	uint32_t psr;

	psr = get_psr();
	set_psr(psr | PSR_IND);
	__cpu_simple_lock(lock);

	return psr;
}

void
m88100_mp_atomic_end(uint32_t psr, __cpu_simple_lock_t *lock, uint csr)
{
	__cpu_simple_unlock(lock);
	set_psr(psr);
}
#endif
