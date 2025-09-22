/*	$OpenBSD: dt_prov_kprobe.c,v 1.10 2025/08/14 13:04:48 mpi Exp $	*/

/*
 * Copyright (c) 2024 Martin Pieuchot <mpi@openbsd.org>
 * Copyright (c) 2020 Tom Rollet <tom.rollet@epita.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#if defined(DDBPROF) && (defined(__amd64__) || defined(__i386__))

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/exec_elf.h>

#include <ddb/db_elf.h>
#include <machine/db_machdep.h>
#include <ddb/db_extern.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_interface.h>

#include <dev/dt/dtvar.h>

#define KPROBE_ENTRY	0x1
#define KPROBE_RETURN	0x2

extern db_symtab_t	db_symtab;
extern char		__kutext_end[];
extern int		db_prof_on;

extern void	db_prof_count(struct trapframe *);
extern vaddr_t	db_get_probe_addr(struct trapframe *);

/* Lists of probes per ELF symbol. */
SLIST_HEAD(, dt_probe) *dtpf_entry;
SLIST_HEAD(, dt_probe) *dtpf_return;

int	dt_prov_kprobe_alloc(struct dt_probe *, struct dt_softc *,
	    struct dt_pcb_list *, struct dtioc_req *);
int	dt_prov_kprobe_hook(struct dt_provider *, ...);
int	dt_prov_kprobe_dealloc(struct dt_probe *, struct dt_softc *,
	    struct dtioc_req *);

#define DTEVT_PROV_KPROBE (DTEVT_COMMON|DTEVT_FUNCARGS)

struct dt_provider dt_prov_kprobe = {
	.dtpv_name    = "kprobe",
	.dtpv_alloc   = dt_prov_kprobe_alloc,
	.dtpv_enter   = dt_prov_kprobe_hook,
	.dtpv_leave   = NULL,
	.dtpv_dealloc = dt_prov_kprobe_dealloc,
};

/* Bob Jenkin's public domain 32-bit integer hashing function.
 * Original at https://burtleburtle.net/bob/hash/integer.html.
 */
uint32_t
ptr_hash(uint32_t a) {
	a = (a + 0x7ed55d16) + (a<<12);
	a = (a ^ 0xc761c23c) ^ (a>>19);
	a = (a + 0x165667b1) + (a<<5);
	a = (a + 0xd3a2646c) ^ (a<<9);
	a = (a + 0xfd7046c5) + (a<<3);
	a = (a ^ 0xb55a4f09) ^ (a>>16);
	return a;
}

#define	PPTSIZE		(PAGE_SIZE * 30) /* XXX */
#define	PPTMASK		((PPTSIZE / sizeof(struct dt_probe)) - 1)
#define	INSTTOIDX(inst)	(ptr_hash(inst) & PPTMASK)

#if defined(__amd64__)
#define	IBT_SIZE	4
#define	RTGD_MOV_SIZE	7
#define	RTGD_XOR_SIZE	4
#define	FRAME_SIZE	(IBT_SIZE + RTGD_XOR_SIZE + RTGD_XOR_SIZE + 1 + 3)

#define	RET_INST	0xc3
#define	RET_SIZE	1

/*
 * Validate that this prologue respect a well-known layout and return
 * the offset where the symbol can be patched.
 */
// XXX use db_get_value();
int
db_prologue_validate(Elf_Sym *symp)
{
	uint8_t *inst = (uint8_t *)symp->st_value;
	int off = symp->st_size - 1;

	if (off < IBT_SIZE + RTGD_MOV_SIZE + RTGD_XOR_SIZE)
		return -1;

	/* Check for IBT */
	if (inst[0] != 0xf3 || inst[1] != 0x0f ||
	    inst[2] != 0x1e || inst[3] != 0xfa)
		return -1;

	/* Check for retguard */
	off = IBT_SIZE;
	if (inst[off] != 0x4c || inst[off + 1] != 0x8b || inst[off + 2] != 0x1d)
	    	return -1;

	/* Check for `xorq off(%rsp), %reg' */
	off += RTGD_MOV_SIZE;
	if (inst[off] != 0x4c || inst[off + 1] != 0x33 || inst[off + 2] != 0x1c)
		return -1;

	/* Check for `pushq %rbp' */
	off += RTGD_XOR_SIZE;
	if (inst[off] != SSF_INST)
		return -1;

	/* Check for `movq %rsp, %rbp'  */
	if (inst[off + 1] != 0x48 || inst[off + 2] != 0x89 ||
	    inst[off + 3] != 0xe5)
	    	return -1;

	return off;
}

/*
 * Insert a breakpoint or restore `pushq %rbp'.
 */
void
db_prologue_patch(vaddr_t addr, int restore)
{
	uint8_t patch;
	size_t size;
	unsigned s;

	CTASSERT(SSF_SIZE == BKPT_SIZE);
	if (restore) {
		patch = SSF_INST;
		size = SSF_SIZE;
	} else {
		patch = BKPT_INST;
		size = BKPT_SIZE;
	}

	s = intr_disable();
	db_write_bytes(addr, size, &patch);
	intr_restore(s);
}

int
db_epilogue_validate(Elf_Sym *symp)
{
	uint8_t *inst = (uint8_t *)symp->st_value;
	int off = symp->st_size - 1;

	while (off > FRAME_SIZE + 2) {
		if (inst[off - 1] == 0xcc && inst[off] == RET_INST)
			return off;
		off--;
	}

	return -1;
}

void
db_epilogue_patch(vaddr_t addr, int restore)
{
	uint8_t patch;
	size_t size;
	unsigned s;

	CTASSERT(RET_SIZE == BKPT_SIZE);
	if (restore) {
		patch = RET_INST;
		size = RET_SIZE;
	} else {
		patch = BKPT_INST;
		size = BKPT_SIZE;
	}

	s = intr_disable();
	db_write_bytes(addr, size, &patch);
	intr_restore(s);
}

#elif defined(__i386__)
#define	POP_RBP_INST	0x5d
#define	POP_RBP_SIZE	1

int
db_prologue_validate(Elf_Sym *symp)
{
	uint8_t *inst = (uint8_t *)symp->st_value;

	/* No retguard or IBT on i386 */
	if (*inst != SSF_INST)
		return -1;

	return 0;
}

/*
 * Insert a breakpoint or restore
 */
void
db_prologue_patch(vaddr_t addr, int restore)
{
	uint8_t patch;
	size_t size;
	unsigned s;

	CTASSERT(SSF_SIZE == BKPT_SIZE);
	if (restore) {
		patch = SSF_INST;
		size = SSF_SIZE;
	} else {
		patch = BKPT_INST;
		size = BKPT_SIZE;
	}

	s = intr_disable();
	db_write_bytes(addr, size, &patch);
	intr_restore(s);
}

int
db_epilogue_validate(Elf_Sym *symp)
{
	vaddr_t limit = symp->st_value + symp->st_size;
#if 0
	/*
	 * Little temporary hack to find some return probe
	 *   => always int3 after 'pop %rpb; ret'
	 */
	while(*((uint8_t *)inst) == 0xcc)
		(*(uint8_t *)inst) -= 1;
#endif
	if (*(uint8_t *)(limit - 2) != POP_RBP)
		return -1;

	return symp->st_size - 2;
}

void
db_epilogue_patch(vaddr_t addr, int restore)
{
	uint8_t patch;
	size_t size;
	unsigned s;

	CTASSERT(SSF_SIZE == BKPT_SIZE);
	if (restore) {
		patch = POP_RBP_INST;
		size = POP_RBP_SIZE;
	} else {
		patch = BKPT_INST;
		size = BKPT_SIZE;
	}

	s = intr_disable();
	db_write_bytes(addr, size, &patch);
	intr_restore(s);
}
#endif /* defined(__i386__) */

/* Initialize all entry and return probes and store them in global arrays */
int
dt_prov_kprobe_init(void)
{
	struct dt_probe *dtp;
	Elf_Sym *symp, *symtab_start, *symtab_end;
	const char *strtab, *name;
	vaddr_t inst;
	int off, nb_sym, nb_probes = 0;

	nb_sym = (db_symtab.end - db_symtab.start) / sizeof (Elf_Sym);

	dtpf_entry = malloc(PPTSIZE, M_DT, M_NOWAIT|M_ZERO);
	if (dtpf_entry == NULL)
		return 0;

	dtpf_return = malloc(PPTSIZE, M_DT, M_NOWAIT|M_ZERO);
	if (dtpf_return == NULL) {
		free(dtpf_entry, M_DT, PPTSIZE);
		return 0;
	}

	db_symtab_t *stab = &db_symtab;

	symtab_start = STAB_TO_SYMSTART(stab);
	symtab_end = STAB_TO_SYMEND(stab);
	strtab = db_elf_find_strtab(stab);

	for (symp = symtab_start; symp < symtab_end; symp++) {
		if (ELF_ST_TYPE(symp->st_info) != STT_FUNC)
			continue;

		inst = symp->st_value;
		name = strtab + symp->st_name;

		/* Filter function that are not mapped in memory */
		if (inst < KERNBASE || inst >= (vaddr_t)&__kutext_end)
			continue;

		/* Remove some function to avoid recursive tracing */
		if (strncmp(name, "dt_", 3) == 0 ||
		    strncmp(name, "trap", 4) == 0 ||
		    strncmp(name, "db_", 3) == 0)
			continue;

		off = db_prologue_validate(symp);
		if (off < 0)
			continue;

		dtp = dt_dev_alloc_probe(name, "entry", &dt_prov_kprobe);
		if (dtp == NULL)
			break;

		dtp->dtp_addr = inst + off;
		dtp->dtp_type = KPROBE_ENTRY;
		dtp->dtp_nargs = db_ctf_func_numargs(symp);
		SLIST_INSERT_HEAD(&dtpf_entry[INSTTOIDX(dtp->dtp_addr)],
		    dtp, dtp_knext);
		dt_dev_register_probe(dtp);
		nb_probes++;

		off = db_epilogue_validate(symp);
		if (off < 0)
			continue;

		dtp = dt_dev_alloc_probe(name, "return", &dt_prov_kprobe);
		if (dtp == NULL)
			break;

		dtp->dtp_addr = inst + off;
		dtp->dtp_type = KPROBE_RETURN;
		SLIST_INSERT_HEAD(&dtpf_return[INSTTOIDX(dtp->dtp_addr)],
		    dtp, dtp_knext);
		dt_dev_register_probe(dtp);
		nb_probes++;
	}

	return nb_probes;
}

int
dt_prov_kprobe_alloc(struct dt_probe *dtp, struct dt_softc *sc,
    struct dt_pcb_list *plist, struct dtioc_req *dtrq)
{
	struct dt_pcb *dp;

	dp = dt_pcb_alloc(dtp, sc);
	if (dp == NULL)
		return ENOMEM;

	dtp->dtp_ref++;
	if (dtp->dtp_ref == 1) {
		switch (dtp->dtp_type) {
		case KPROBE_ENTRY:
			db_prologue_patch(dtp->dtp_addr, 0);
			break;
		case KPROBE_RETURN:
			db_epilogue_patch(dtp->dtp_addr, 0);
			break;
		default:
			panic("unknown probe type %d", dtp->dtp_type);
		}
	}

	dp->dp_evtflags = dtrq->dtrq_evtflags & DTEVT_PROV_KPROBE;
	TAILQ_INSERT_HEAD(plist, dp, dp_snext);
	return 0;
}

int
dt_prov_kprobe_dealloc(struct dt_probe *dtp, struct dt_softc *sc,
   struct dtioc_req *dtrq)
{
	dtp->dtp_ref--;
	if (dtp->dtp_ref > 0)
		return 0;

	switch (dtp->dtp_type) {
	case KPROBE_ENTRY:
		db_prologue_patch(dtp->dtp_addr, 1);
		break;
	case KPROBE_RETURN:
		db_epilogue_patch(dtp->dtp_addr, 1);
		break;
	default:
		panic("unknown probe type %d", dtp->dtp_type);
	}

	/* Deallocation of PCB is done by dt_pcb_purge when closing the dev */
	return 0;
}

int
dt_prov_kprobe_hook(struct dt_provider *dtpv, ...)
{
	struct dt_probe *dtp;
	struct dt_pcb *dp;
	struct trapframe *tf;
	va_list ap;
	int is_dt_bkpt = 0;
	int error;	/* Return values for return probes*/
	vaddr_t *args, addr;
	size_t argsize;
	register_t retval[2];

	KASSERT(dtpv == &dt_prov_kprobe);

	va_start(ap, dtpv);
	tf = va_arg(ap, struct trapframe*);
	va_end(ap);

	addr = db_get_probe_addr(tf);

	SLIST_FOREACH(dtp, &dtpf_entry[INSTTOIDX(addr)], dtp_knext) {
		if (dtp->dtp_addr != addr)
			continue;

		is_dt_bkpt = 1;
		if (db_prof_on)
			db_prof_count(tf);

		if (!dtp->dtp_recording)
			continue;

		smr_read_enter();
		SMR_SLIST_FOREACH(dp, &dtp->dtp_pcbs, dp_pnext) {
			struct dt_evt *dtev;

			dtev = dt_pcb_ring_get(dp, 0);
			if (dtev == NULL)
				continue;

#if defined(__amd64__)
			args = (vaddr_t *)tf->tf_rdi;
			/* XXX: use CTF to get the number of arguments. */
			argsize = 6;
#elif defined(__i386__)
			/* All args on stack */
			args = (vaddr_t *)(tf->tf_esp + 4);
			argsize = 10;
#endif

			if (ISSET(dp->dp_evtflags, DTEVT_FUNCARGS))
				memcpy(dtev->dtev_args, args, argsize);

			dt_pcb_ring_consume(dp, dtev);
		}
		smr_read_leave();
	}

	if (is_dt_bkpt)
		return is_dt_bkpt;

	SLIST_FOREACH(dtp, &dtpf_return[INSTTOIDX(addr)], dtp_knext) {
		if (dtp->dtp_addr != addr)
			continue;

		is_dt_bkpt = 2;

		if (!dtp->dtp_recording)
			continue;

		smr_read_enter();
		SMR_SLIST_FOREACH(dp, &dtp->dtp_pcbs, dp_pnext) {
			struct dt_evt *dtev;

			dtev = dt_pcb_ring_get(dp, 0);
			if (dtev == NULL)
				continue;

#if defined(__amd64__)
			retval[0] = tf->tf_rax;
			retval[1] = 0;
			error = 0;
#elif defined(__i386)
			retval[0] = tf->tf_eax;
			retval[1] = 0;
			error = 0;
#endif

			dtev->dtev_retval[0] = retval[0];
			dtev->dtev_retval[1] = retval[1];
			dtev->dtev_error = error;

			dt_pcb_ring_consume(dp, dtev);
		}
		smr_read_leave();
	}
	return is_dt_bkpt;
}

/* Called by ddb to patch all functions without allocating 1 pcb per probe */
void
dt_prov_kprobe_patch_all_entry(void)
{
	struct dt_probe *dtp;
	size_t i;

	for (i = 0; i < PPTMASK; ++i) {
		SLIST_FOREACH(dtp, &dtpf_entry[i], dtp_knext) {
			dtp->dtp_ref++;
			if (dtp->dtp_ref != 1)
				continue;

			db_prologue_patch(dtp->dtp_addr, 0);
		}
	}
}

/* Called by ddb to patch all functions without allocating 1 pcb per probe */
void
dt_prov_kprobe_depatch_all_entry(void)
{
	struct dt_probe *dtp;
	size_t i;

	for (i = 0; i < PPTMASK; ++i) {
		SLIST_FOREACH(dtp, &dtpf_entry[i], dtp_knext) {
			dtp->dtp_ref--;
			if (dtp->dtp_ref != 0)
				continue;

			db_prologue_patch(dtp->dtp_addr, 1);
		}

	}
}
#endif /* __amd64__ || __i386__ */
