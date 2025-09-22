/*	$OpenBSD: db_interface.c,v 1.16 2024/11/07 16:02:29 miod Exp $	*/
/*	$NetBSD: db_interface.c,v 1.37 2006/09/06 00:11:49 uwe Exp $	*/

/*-
 * Copyright (C) 2002 UCHIYAMA Yasushi.  All rights reserved.
 * Copyright (c) 2000 Tsubai Masanari.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>

#include <uvm/uvm_extern.h>

#include <dev/cons.h>

#include <machine/db_machdep.h>
#include <ddb/db_run.h>
#include <ddb/db_sym.h>

#include <sh/ubcreg.h>

db_regs_t ddb_regs;		/* register state */


#include <sh/cache.h>
#include <sh/cache_sh3.h>
#include <sh/cache_sh4.h>
#include <sh/mmu.h>
#include <sh/mmu_sh3.h>
#include <sh/mmu_sh4.h>

#include <ddb/db_command.h>
#include <ddb/db_extern.h>
#include <ddb/db_output.h>
#include <ddb/db_var.h>

void kdb_printtrap(u_int, int);

void db_tlbdump_cmd(db_expr_t, int, db_expr_t, char *);
void __db_tlbdump_page_size_sh4(uint32_t);
void __db_tlbdump_pfn(uint32_t);
void db_cachedump_cmd(db_expr_t, int, db_expr_t, char *);

void __db_cachedump_sh3(vaddr_t);
void __db_cachedump_sh4(vaddr_t);

void db_stackcheck_cmd(db_expr_t, int, db_expr_t, char *);
void db_frame_cmd(db_expr_t, int, db_expr_t, char *);
void __db_print_symbol(db_expr_t);
char *__db_procname_by_asid(int);

const struct db_command db_machine_command_table[] = {
	{ "tlb",	db_tlbdump_cmd,		0,	NULL },
	{ "cache",	db_cachedump_cmd,	0,	NULL },
	{ "frame",	db_frame_cmd,		0,	NULL },
#ifdef KSTACK_DEBUG
	{ "stack",	db_stackcheck_cmd,	0,	NULL },
#endif
	{ NULL }
};

void
db_machine_init(void)
{
}

void
kdb_printtrap(u_int type, int code)
{
	int i;
	i = type >> 5;

	db_printf("%s mode trap: ", type & 1 ? "user" : "kernel");
	if (i >= exp_types)
		db_printf("type 0x%03x", type & ~1);
	else
		db_printf("%s", exp_type[i]);

	db_printf(" code = 0x%x\n", code);
}

int
db_ktrap(int type, int code, db_regs_t *regs)
{
	extern label_t *db_recover;
	int s;

	switch (type) {
	case EXPEVT_TRAPA:	/* trapa instruction */
	case EXPEVT_BREAK:	/* UBC */
	case -1:		/* keyboard interrupt */
		break;
	default:
		if (!db_panic && db_recover == NULL)
			return 0;

		kdb_printtrap(type, code);
		if (db_recover != NULL) {
			db_error("Faulted in DDB; continuing...\n");
			/*NOTREACHED*/
		}
	}

	/* XXX Should switch to kdb's own stack here. */

	ddb_regs = *regs;

	s = splhigh();
	db_active++;
	cnpollc(1);
	db_trap(type, code);
	cnpollc(0);
	db_active--;
	splx(s);

	*regs = ddb_regs;

	return 1;
}

void
db_enter(void)
{
	__asm volatile("trapa %0" :: "i"(_SH_TRA_BREAK));
}

#define	M_BSR	0xf000
#define	I_BSR	0xb000
#define	M_BSRF	0xf0ff
#define	I_BSRF	0x0003
#define	M_JSR	0xf0ff
#define	I_JSR	0x400b
#define	M_RTS	0xffff
#define	I_RTS	0x000b
#define	M_RTE	0xffff
#define	I_RTE	0x002b

int
inst_call(int inst)
{
#if _BYTE_ORDER == BIG_ENDIAN
	inst >>= 16;
#endif
	return (inst & M_BSR) == I_BSR || (inst & M_BSRF) == I_BSRF ||
	       (inst & M_JSR) == I_JSR;
}

int
inst_return(int inst)
{
#if _BYTE_ORDER == BIG_ENDIAN
	inst >>= 16;
#endif
	return (inst & M_RTS) == I_RTS;
}

int
inst_trap_return(int inst)
{
#if _BYTE_ORDER == BIG_ENDIAN
	inst >>= 16;
#endif
	return (inst & M_RTE) == I_RTE;
}

void
db_set_single_step(db_regs_t *regs)
{

	_reg_write_2(SH_(BBRA), 0);		/* disable break */
	_reg_write_4(SH_(BARA), 0);		/* break address */
	_reg_write_1(SH_(BASRA), 0);		/* break ASID */
	_reg_write_1(SH_(BAMRA), 0x07);		/* break always */
	_reg_write_2(SH_(BRCR),  0x400);	/* break after each execution */

	regs->tf_ubc = 0x0014;	/* will be written to BBRA */
}

void
db_clear_single_step(db_regs_t *regs)
{

	regs->tf_ubc = 0;
}

#define	ON(x, c)	((x) & (c) ? '|' : '.')

/*
 * MMU
 */
void
db_tlbdump_cmd(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif)
{
	static const char *pr[] = { "_r", "_w", "rr", "ww" };
	static const char title[] =
	    "   VPN      ASID    PFN  AREA VDCGWtPR  SZ";
	static const char title2[] =
	    "          U/K                       U/K";
	uint32_t r, e;
	int i;
#ifdef SH3
	if (CPU_IS_SH3) {
		/* MMU configuration. */
		r = _reg_read_4(SH3_MMUCR);
		db_printf("%s-mode, %s virtual storage mode\n",
		    r & SH3_MMUCR_IX
		    ? "ASID + VPN" : "VPN only",
		    r & SH3_MMUCR_SV ? "single" : "multiple");
		i = _reg_read_4(SH3_PTEH) & SH3_PTEH_ASID_MASK;
		db_printf("ASID=%d (%s)", i, __db_procname_by_asid(i));

		db_printf("---TLB DUMP---\n%s\n%s\n", title, title2);
		for (i = 0; i < SH3_MMU_WAY; i++) {
			db_printf(" [way %d]\n", i);
			for (e = 0; e < SH3_MMU_ENTRY; e++) {
				uint32_t a;
				/* address/data array common offset. */
				a = (e << SH3_MMU_VPN_SHIFT) |
				    (i << SH3_MMU_WAY_SHIFT);

				r = _reg_read_4(SH3_MMUAA | a);
				if (r == 0) {
					db_printf("---------- - --- ----------"
					    " - ----x --  --\n");
				} else {
					vaddr_t va;
					int asid;
					asid = r & SH3_MMUAA_D_ASID_MASK;
					r &= SH3_MMUAA_D_VPN_MASK_1K;
					va = r | (e << SH3_MMU_VPN_SHIFT);
					db_printf("0x%08lx %c %3d", va,
					    (int)va < 0 ? 'K' : 'U', asid);

					r = _reg_read_4(SH3_MMUDA | a);
					__db_tlbdump_pfn(r);

					db_printf(" %c%c%c%cx %s %2dK\n",
					    ON(r, SH3_MMUDA_D_V),
					    ON(r, SH3_MMUDA_D_D),
					    ON(r, SH3_MMUDA_D_C),
					    ON(r, SH3_MMUDA_D_SH),
					    pr[(r & SH3_MMUDA_D_PR_MASK) >>
						SH3_MMUDA_D_PR_SHIFT],
					    r & SH3_MMUDA_D_SZ ? 4 : 1);
				}
			}
		}
	}
#endif /* SH3 */
#ifdef SH4
	if (CPU_IS_SH4) {
		/* MMU configuration */
		r = _reg_read_4(SH4_MMUCR);
		db_printf("%s virtual storage mode, SQ access: (kernel%s)\n",
		    r & SH3_MMUCR_SV ? "single" : "multiple",
		    r & SH4_MMUCR_SQMD ? "" : "/user");
		db_printf("random counter limit=%d\n",
		    (r & SH4_MMUCR_URB_MASK) >> SH4_MMUCR_URB_SHIFT);

		i = _reg_read_4(SH4_PTEH) & SH4_PTEH_ASID_MASK;
		db_printf("ASID=%d (%s)", i, __db_procname_by_asid(i));

		/* Dump ITLB */
		db_printf("---ITLB DUMP ---\n%s TC SA\n%s\n", title, title2);
		for (i = 0; i < 4; i++) {
			e = i << SH4_ITLB_E_SHIFT;

			r = _reg_read_4(SH4_ITLB_AA | e);
			db_printf("0x%08x   %3d",
			    r & SH4_ITLB_AA_VPN_MASK,
			    r & SH4_ITLB_AA_ASID_MASK);

			r = _reg_read_4(SH4_ITLB_DA1 | e);
			__db_tlbdump_pfn(r);
			db_printf(" %c_%c%c_ %s ",
			    ON(r, SH4_ITLB_DA1_V),
			    ON(r, SH4_ITLB_DA1_C),
			    ON(r, SH4_ITLB_DA1_SH),
			    pr[(r & SH4_ITLB_DA1_PR) >>
				SH4_UTLB_DA1_PR_SHIFT]);
			__db_tlbdump_page_size_sh4(r);

#if 0 /* XXX: causes weird effects on landisk */
			r = _reg_read_4(SH4_ITLB_DA2 | e);
			db_printf(" %c  %d\n",
			    ON(r, SH4_ITLB_DA2_TC),
			    r & SH4_ITLB_DA2_SA_MASK);
#else
			db_printf("\n");
#endif
		}

		/* Dump UTLB */
		db_printf("---UTLB DUMP---\n%s TC SA\n%s\n", title, title2);
		for (i = 0; i < 64; i++) {
			e = i << SH4_UTLB_E_SHIFT;

			r = _reg_read_4(SH4_UTLB_AA | e);
			db_printf("0x%08x   %3d",
			    r & SH4_UTLB_AA_VPN_MASK,
			    r & SH4_UTLB_AA_ASID_MASK);

			r = _reg_read_4(SH4_UTLB_DA1 | e);
			__db_tlbdump_pfn(r);
			db_printf(" %c%c%c%c%c %s ",
			    ON(r, SH4_UTLB_DA1_V),
			    ON(r, SH4_UTLB_DA1_D),
			    ON(r, SH4_UTLB_DA1_C),
			    ON(r, SH4_UTLB_DA1_SH),
			    ON(r, SH4_UTLB_DA1_WT),
			    pr[(r & SH4_UTLB_DA1_PR_MASK) >>
				SH4_UTLB_DA1_PR_SHIFT]
			    );
			__db_tlbdump_page_size_sh4(r);

#if 0 /* XXX: causes weird effects on landisk */
			r = _reg_read_4(SH4_UTLB_DA2 | e);
			db_printf(" %c  %d\n",
			    ON(r, SH4_UTLB_DA2_TC),
			    r & SH4_UTLB_DA2_SA_MASK);
#else
			db_printf("\n");
#endif
		}
	}
#endif /* SH4 */
}

void
__db_tlbdump_pfn(uint32_t r)
{
	uint32_t pa = (r & SH3_MMUDA_D_PPN_MASK);

	db_printf(" 0x%08x %d", pa, (pa >> 26) & 7);
}

char *
__db_procname_by_asid(int asid)
{
	static char notfound[] = "---";
	struct process *pr;

	LIST_FOREACH(pr, &allprocess, ps_list) {
		if (pr->ps_vmspace->vm_map.pmap->pm_asid == asid)
			return (pr->ps_comm);
	}

	return (notfound);
}

#ifdef SH4
void
__db_tlbdump_page_size_sh4(uint32_t r)
{
	switch (r & SH4_PTEL_SZ_MASK) {
	case SH4_PTEL_SZ_1K:
		db_printf(" 1K");
		break;
	case SH4_PTEL_SZ_4K:
		db_printf(" 4K");
		break;
	case SH4_PTEL_SZ_64K:
		db_printf("64K");
		break;
	case SH4_PTEL_SZ_1M:
		db_printf(" 1M");
		break;
	}
}
#endif /* SH4 */

/*
 * CACHE
 */
void
db_cachedump_cmd(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif)
{
#ifdef SH3
	if (CPU_IS_SH3)
		__db_cachedump_sh3(have_addr ? addr : 0);
#endif
#ifdef SH4
	if (CPU_IS_SH4)
		__db_cachedump_sh4(have_addr ? addr : 0);
#endif
}

#ifdef SH3
void
__db_cachedump_sh3(vaddr_t va_start)
{
	uint32_t r;
	vaddr_t va, va_end, cca;
	int entry, way;

	RUN_P2;
	/* disable cache */
	_reg_write_4(SH3_CCR,
	    _reg_read_4(SH3_CCR) & ~SH3_CCR_CE);

	if (va_start) {
		va = va_start & ~(sh_cache_line_size - 1);
		va_end = va + sh_cache_line_size;
	} else {
		va = 0;
		va_end = sh_cache_way_size;
	}

	db_printf("%d-way, way-size=%dB, way-shift=%d, entry-mask=%08x, "
	    "line-size=%dB \n", sh_cache_ways, sh_cache_way_size,
	    sh_cache_way_shift, sh_cache_entry_mask, sh_cache_line_size);
	db_printf("Entry  Way 0  UV   Way 1  UV   Way 2  UV   Way 3  UV\n");
	for (; va < va_end; va += sh_cache_line_size) {
		entry = va & sh_cache_entry_mask;
		cca = SH3_CCA | entry;
		db_printf(" %3d ", entry >> CCA_ENTRY_SHIFT);
		for (way = 0; way < sh_cache_ways; way++) {
			r = _reg_read_4(cca | (way << sh_cache_way_shift));
			db_printf("%08x %c%c ", r & CCA_TAGADDR_MASK,
			    ON(r, CCA_U), ON(r, CCA_V));
		}
		db_printf("\n");
	}

	/* enable cache */
	_reg_bset_4(SH3_CCR, SH3_CCR_CE);
	sh_icache_sync_all();

	RUN_P1;
}
#endif /* SH3 */

#ifdef SH4
void
__db_cachedump_sh4(vaddr_t va)
{
	uint32_t r, e;
	int i, istart, iend;

	RUN_P2; /* must access from P2 */

	/* disable I/D-cache */
	_reg_write_4(SH4_CCR,
	    _reg_read_4(SH4_CCR) & ~(SH4_CCR_ICE | SH4_CCR_OCE));

	if (va) {
		istart = ((va & CCIA_ENTRY_MASK) >> CCIA_ENTRY_SHIFT) & ~3;
		iend = istart + 4;
	} else {
		istart = 0;
		iend = SH4_ICACHE_SIZE / SH4_CACHE_LINESZ;
	}

	db_printf("[I-cache]\n");
	db_printf("  Entry             V           V           V           V\n");
	for (i = istart; i < iend; i++) {
		if ((i & 3) == 0)
			db_printf("\n[%3d-%3d] ", i, i + 3);
		r = _reg_read_4(SH4_CCIA | (i << CCIA_ENTRY_SHIFT));
		db_printf("%08x _%c ", r & CCIA_TAGADDR_MASK, ON(r, CCIA_V));
	}

	db_printf("\n[D-cache]\n");
	db_printf("  Entry            UV          UV          UV          UV\n");
	for (i = istart; i < iend; i++) {
		if ((i & 3) == 0)
			db_printf("\n[%3d-%3d] ", i, i + 3);
		e = (i << CCDA_ENTRY_SHIFT);
		r = _reg_read_4(SH4_CCDA | e);
		db_printf("%08x %c%c ", r & CCDA_TAGADDR_MASK, ON(r, CCDA_U),
		    ON(r, CCDA_V));

	}
	db_printf("\n");

	_reg_write_4(SH4_CCR,
	    _reg_read_4(SH4_CCR) | SH4_CCR_ICE | SH4_CCR_OCE);
	sh_icache_sync_all();

	RUN_P1;
}
#endif /* SH4 */

#undef ON

void
db_frame_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct switchframe *sf = &curpcb->pcb_sf;
	struct trapframe *tf, *tftop;

	/* Print switch frame */
	db_printf("[switch frame]\n");

#define	SF(x)	db_printf("sf_" #x "\t\t0x%08x\t", sf->sf_ ## x);	\
		__db_print_symbol(sf->sf_ ## x)

	SF(sr);
	SF(r15);
	SF(r14);
	SF(r13);
	SF(r12);
	SF(r11);
	SF(r10);
	SF(r9);
	SF(r8);
	SF(pr);
	db_printf("sf_r6_bank\t0x%08x\n", sf->sf_r6_bank);
	db_printf("sf_r7_bank\t0x%08x\n", sf->sf_r7_bank);


	/* Print trap frame stack */
	db_printf("[trap frame]\n");

	__asm("stc r6_bank, %0" : "=r"(tf));
	tftop = (struct trapframe *)((vaddr_t)curpcb + PAGE_SIZE);

	for (; tf != tftop; tf++) {
		db_printf("-- %p-%p --\n", tf, tf + 1);
		db_printf("tf_expevt\t0x%08x\n", tf->tf_expevt);

#define	TF(x)	db_printf("tf_" #x "\t\t0x%08x\t", tf->tf_ ## x);	\
		__db_print_symbol(tf->tf_ ## x)

		TF(ubc);
		TF(spc);
		TF(ssr);
		TF(gbr);
		TF(macl);
		TF(mach);
		TF(pr);
		TF(r13);
		TF(r12);
		TF(r11);
		TF(r10);
		TF(r9);
		TF(r8);
		TF(r7);
		TF(r6);
		TF(r5);
		TF(r4);
		TF(r3);
		TF(r2);
		TF(r1);
		TF(r0);
		TF(r15);
		TF(r14);
	}
#undef	SF
#undef	TF
}

void
__db_print_symbol(db_expr_t value)
{
	const char *name;
	db_expr_t offset;

	db_find_sym_and_offset((vaddr_t)value, &name, &offset);
	if (name != NULL && offset <= db_maxoff && offset != value)
		db_printsym(value, DB_STGY_ANY, db_printf);

	db_printf("\n");
}

#ifdef KSTACK_DEBUG
/*
 * Stack overflow check
 */
void
db_stackcheck_cmd(db_expr_t addr, int have_addr, db_expr_t count,
		  char *modif)
{
	struct proc *p;
	struct user *u;
	struct pcb *pcb;
	uint32_t *t32;
	uint8_t *t8;
	int i, j;

#define	MAX_STACK	(USPACE - PAGE_SIZE)
#define	MAX_FRAME	(PAGE_SIZE - sizeof(struct user))

	db_printf("stack max: %d byte, frame max %d byte,"
	    " sizeof(struct trapframe) %d byte\n", MAX_STACK, MAX_FRAME,
	    sizeof(struct trapframe));
	db_printf("   PID.LID    "
		  "stack top    max used    frame top     max used"
		  "  nest\n");

	LIST_FOREACH(p, &allproc, p_list) {
		u = p->p_addr;
		pcb = &u->u_pcb;
		/* stack */
		t32 = (uint32_t *)(pcb->pcb_sf.sf_r7_bank - MAX_STACK);
		for (i = 0; *t32++ == 0xa5a5a5a5; i++)
			continue;
		i = MAX_STACK - i * sizeof(int);

		/* frame */
		t8 = (uint8_t *)((vaddr_t)pcb + PAGE_SIZE - MAX_FRAME);
		for (j = 0; *t8++ == 0x5a; j++)
			continue;
		j = MAX_FRAME - j;

		db_printf("%6d 0x%08x %6d (%3d%%) 0x%08lx %6d (%3d%%) %d %s\n",
		    p->p_lid,
		    pcb->pcb_sf.sf_r7_bank, i, i * 100 / MAX_STACK,
		    (vaddr_t)pcb + PAGE_SIZE, j, j * 100 / MAX_FRAME,
		    j / sizeof(struct trapframe),
		    p->p_p->ps_comm);
	}
#undef	MAX_STACK
#undef	MAX_FRAME
}
#endif /* KSTACK_DEBUG */
