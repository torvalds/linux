/*	$OpenBSD: dump_tables.c,v 1.8 2023/05/11 22:28:38 guenther Exp $	*/
/*
 * Copyright (c) 2019,2023 Philip Guenther <guenther@openbsd.org>
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

/*
 * Dump amd64 page tables to text for analysis
 * Requires "kern.allowkmem=1" sysctl
 */

#include <sys/param.h>	/* PAGE_SIZE and other things */
#include <sys/sysctl.h>
#include <sys/time.h>
#include <uvm/uvm_extern.h>
#include <machine/pmap.h>
#include <machine/pcb.h>

/*
 * Getting struct pmap from <machine/pmap.h> is too hard right now.
 * Just extract it and go.
 */
#include "struct_pmap.h"

#define PG_1GFRAME	0x000fffffc0000000UL	/* should be in pmap.h */

#define PG_PK_SHIFT	59


#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <nlist.h>
#include <stdlib.h>
#include <unistd.h>

void
usage(int status)
{
	printf("\
Usage: dump_tables [-1234dlmpr]\n\
 -1234\tShow the specified levels in the page tables.\n\
 -d\tHide the entries in the direct-map.\n\
 -h\tShow this usage message.\n\
 -l\tShow the leaf entries, whether 4kB, 2MB, or 1GB.\n\
 -m\tShow the Meltdown U-K page tables instead.\n\
 -p\tHide the entries through the recursive PTE mapping.\n\
 -r\tSuppress the 'U'sed and 'M'odified attributes to increase.\n\
\treproducibility.\n\
 -u\tShow the page tables for PID 1 instead of those for kernel threads.\n\
\n\
Dump the page tables, including intermediate levels, showing for\n\
each valid entry the virtual-address (VA) it applies to, the level\n\
of page table, the index of the entry within its page, the physical\n\
address (PA) it points to, the size of leaf page it points to, the\n\
attributes on the entry, the effective attributes for those affected\n\
by higher levels of page table, and the slot type for those which have\n\
a particular name.\n\n\
If none of the options -1234l are used, then all levels will be shown.\n\
");
	exit(status);
}


kvm_t *k;
pd_entry_t *pt[5];
int meltdown, hide_direct, hide_pte, reproducible, show[5], show_leaves;
int user_proc;

struct nlist proc0[] = { { "_proc0paddr" }, { NULL } };

#define KGET(addr, var)							\
	KGETRET(addr, &var, sizeof var, #var)
#define KGETRET(addr, p, s, msg)	do {				\
	if (kvm_read(k, addr, p, s) != s)				\
		errx(1, "cannot read %s: %s", msg, kvm_geterr(k));	\
} while (0)
#define KGETPT_PA(addr, level)						\
	KGETPT_VA(PMAP_DIRECT_MAP(addr), level)
#define KGETPT_VA(addr, level)						\
	KGETRET(addr, pt[level], PAGE_SIZE, ptname[level])

const int shift[] = {
    [3] = L3_SHIFT,
    [2] = L2_SHIFT,
    [1] = L1_SHIFT,
};
const char * const ptname[] = {
    [4] = "pml4",
    [3] = "pt3",
    [2] = "pt2",
    [1] = "pt1",
};

/* Not currently used */
const pd_entry_t ign_normal[] = {
    [4] =		0x0000000000000f40UL,
#define	IGN_1GFRAME	0x0000000000000e00UL
    [3] =		0x0000000000000f40UL,
#define	IGN_LGFRAME	0x0000000000000e00UL
    [2] =		0x0000000000000f40UL,
    [1] =		0x0000000000000e00UL,
};

const pd_entry_t mbz_normal[] = {
    [5] =		0x0000000000000fe7UL,
    [4] =		0x0000000000000080UL,
#define	MBZ_1GFRAME	0x000000003fffe000UL
    [3] =		0x0000000000000000UL,
#define	MBZ_LGFRAME	0x00000000001fe000UL
    [2] =		0x0000000000000000UL,
    [1] =		0x0000000000000000UL,
};

static inline void
check_mbz(pd_entry_t e, pd_entry_t mbz)
{
	if ((e & mbz) != 0)
		errx(1, "non-zero mbz: %016llx in %016llx", e & mbz, e);
}

enum l4_type { T_NORMAL = 0, T_DIRECT, T_PTE, T_KERNBASE, };

static inline enum l4_type
l4type(int i)
{
	if (i >= L4_SLOT_DIRECT && i < L4_SLOT_DIRECT + NUM_L4_SLOT_DIRECT)
		return T_DIRECT;
	if (i == L4_SLOT_PTE)
		return T_PTE;
	if (i == L4_SLOT_KERNBASE)
		return T_KERNBASE;
	return T_NORMAL;
}

const char pk_name[16] = "R-23456789abcdef";
void
pflags(pd_entry_t e, pd_entry_t inherited)
{
	int pk = (e & PG_PKMASK) >> PG_PK_SHIFT;
	if (reproducible)
		e &= ~(PG_M|PG_U);
	inherited &= e;
	printf("[%c%c%c%c%c""%c%c%c%c][%c%c%c%c]",
	    e & PG_NX ? 'X' : '-',	/* reversed */
	    pk_name[pk],
	    e & PG_G  ? 'G' : '-',
	    e & PG_M  ? 'M' : '-',
	    e & PG_U  ? 'U' : '-',
	    e & PG_N  ? 'N' : '-',
	    e & PG_WT ? 'w' : '-',
	    e & PG_u  ? 'u' : '-',
	    e & PG_RW ? 'W' : '-',
	    inherited & PG_u  ? 'u' : '-',
	    pk_name[pk],
	    inherited & PG_RW ? 'W' : '-',
	    inherited & PG_NX ? 'X' : '-');	/* reversed */
}

const char * const prefix[] = {
    [4] = "4   ",
    [3] = " 3  ",
    [2] = "  2 ",
    [1] = "   1",
};

void
pent(int level, int idx, vaddr_t va, pd_entry_t e, pd_entry_t inherited,
    enum l4_type l4_type)
{
	if ((e & PG_V) == 0)
		return;

	/* have an actual mapping */
	pd_entry_t pa, mbz;
	char type;
	if ((e & PG_PS) && level == 2) {
		pa = e & PG_LGFRAME;
		mbz = MBZ_LGFRAME;
		type = 'M';
	} else if ((e & PG_PS) && level == 3) {
		pa = e & PG_1GFRAME;
		mbz = MBZ_1GFRAME;
		type = 'G';
	} else {
		pa = e & PG_FRAME;
		mbz = mbz_normal[level];
		type = level == 1 ? 'k' : ' ';
	}
	check_mbz(e, mbz);

	e ^= PG_NX;
	inherited &= e;
	if (show[level] || (show_leaves && type != ' ')) {
		printf("%016lx %s% 4d -> ", va, prefix[level], idx);

		printf("%016llx %c ", pa, type);
		pflags(e, inherited);
		switch (l4_type) {
		case T_NORMAL:		putchar('\n'); break;
		case T_DIRECT:		puts(" direct"); break;
		case T_PTE:		puts(" pte"); break;
		case T_KERNBASE:	puts(" kernbase"); break;
		}
	}

	if (type != ' ')
		return;
	level--;
	KGETPT_PA(pa, level);
	for (u_long i = 0; i < PAGE_SIZE / 8; i++) {
		pent(level, i, (i << shift[level]) + va, pt[level][i],
		    inherited, l4_type == T_PTE ? l4type(i) : T_NORMAL);
	}
}


int
main(int argc, char **argv)
{
	u_long paddr;
	struct pcb pcb;
	pd_entry_t cr3;
	u_long i;
	int ch;

	while ((ch = getopt(argc, argv, "1234dhlmpru")) != -1) {
		switch (ch) {
		case '1': case '2': case '3': case '4':
			show[ch - '0'] = 1;
			break;
		case 'd':
			hide_direct = 1;
			break;
		case 'h':
			usage(0);
			break;
		case 'l':
			show_leaves = 1;
			break;
		case 'm':
			meltdown = 1;
			break;
		case 'p':
			hide_pte = 1;
			break;
		case 'r':
			reproducible = 1;
			break;
		case 'u':
			user_proc = 1;
			break;
		default:
			usage(1);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 0)
		usage(1);

	if (!show[1] && !show[2] && !show[3] && !show[4] && !show_leaves)
		show[1] = show[2] = show[3] = show[4] = 1;

	if ((pt[4] = malloc(PAGE_SIZE)) == NULL ||
	    (pt[3] = malloc(PAGE_SIZE)) == NULL ||
	    (pt[2] = malloc(PAGE_SIZE)) == NULL ||
	    (pt[1] = malloc(PAGE_SIZE)) == NULL)
		err(1, "malloc");

	k = kvm_open(NULL, NULL, NULL, O_RDONLY, "foo");
	if (k == NULL)
		return 1;

	if (user_proc) {
		int cnt;
		struct kinfo_proc *kp = kvm_getprocs(k, KERN_PROC_PID, 1,
			sizeof *kp, &cnt);
		paddr = kp->p_addr;
	} else {
		if (kvm_nlist(k, proc0) != 0)
			err(1, "nlist");
		KGET(proc0[0].n_value, paddr);
	}

	KGET(paddr, pcb);

	cr3 = pcb.pcb_cr3 & ~0xfff;		/* mask off PCID */
	if (meltdown) {
		struct pmap pmap;
		KGET((u_long)pcb.pcb_pmap, pmap);
		if (cr3 != pmap.pm_pdirpa)
			errx(1, "cr3 != pm_pdir: %016llx != %016lx",
			    cr3, pmap.pm_pdirpa);

		cr3 = (u_long)pmap.pm_pdir_intel;	/* VA */
		if (cr3 == 0)
			errx(1, "meltdown mitigation not enabled");
		KGETPT_VA(cr3, 4);
	} else {
		KGETPT_PA(cr3, 4);
		/*printf("PML4 @ %016llx\n", cr3);*/
		check_mbz(cr3, mbz_normal[5]);
	}
	printf("\
VA               lvl  idx    PA              sz entry-attr  eff   L4-slot\
\n");
	for (i = 0; i < PAGE_SIZE / sizeof(pd_entry_t); i++) {
		enum l4_type l4_type = l4type(i);
		if ((l4_type == T_DIRECT && hide_direct) ||
		    (l4_type == T_PTE && hide_pte))
			continue;
		u_long va = i << L4_SHIFT;
		if (i > 255)
			va |= VA_SIGN_MASK;
		pent(4, i, va, pt[4][i], ~0UL, l4_type);
	}
	return 0;
}
