/*
 * Kernel Debugger Architecture Independent Support Functions
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (c) 2009 Wind River Systems, Inc.  All Rights Reserved.
 * 03/02/13    added new 2.5 kallsyms <xavier.bru@bull.net>
 */

#include <stdarg.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kallsyms.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/ptrace.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/hardirq.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/kdb.h>
#include <linux/slab.h>
#include "kdb_private.h"

/*
 * kdbgetsymval - Return the address of the given symbol.
 *
 * Parameters:
 *	symname	Character string containing symbol name
 *      symtab  Structure to receive results
 * Returns:
 *	0	Symbol not found, symtab zero filled
 *	1	Symbol mapped to module/symbol/section, data in symtab
 */
int kdbgetsymval(const char *symname, kdb_symtab_t *symtab)
{
	if (KDB_DEBUG(AR))
		kdb_printf("kdbgetsymval: symname=%s, symtab=%p\n", symname,
			   symtab);
	memset(symtab, 0, sizeof(*symtab));
	symtab->sym_start = kallsyms_lookup_name(symname);
	if (symtab->sym_start) {
		if (KDB_DEBUG(AR))
			kdb_printf("kdbgetsymval: returns 1, "
				   "symtab->sym_start=0x%lx\n",
				   symtab->sym_start);
		return 1;
	}
	if (KDB_DEBUG(AR))
		kdb_printf("kdbgetsymval: returns 0\n");
	return 0;
}
EXPORT_SYMBOL(kdbgetsymval);

static char *kdb_name_table[100];	/* arbitrary size */

/*
 * kdbnearsym -	Return the name of the symbol with the nearest address
 *	less than 'addr'.
 *
 * Parameters:
 *	addr	Address to check for symbol near
 *	symtab  Structure to receive results
 * Returns:
 *	0	No sections contain this address, symtab zero filled
 *	1	Address mapped to module/symbol/section, data in symtab
 * Remarks:
 *	2.6 kallsyms has a "feature" where it unpacks the name into a
 *	string.  If that string is reused before the caller expects it
 *	then the caller sees its string change without warning.  To
 *	avoid cluttering up the main kdb code with lots of kdb_strdup,
 *	tests and kfree calls, kdbnearsym maintains an LRU list of the
 *	last few unique strings.  The list is sized large enough to
 *	hold active strings, no kdb caller of kdbnearsym makes more
 *	than ~20 later calls before using a saved value.
 */
int kdbnearsym(unsigned long addr, kdb_symtab_t *symtab)
{
	int ret = 0;
	unsigned long symbolsize;
	unsigned long offset;
#define knt1_size 128		/* must be >= kallsyms table size */
	char *knt1 = NULL;

	if (KDB_DEBUG(AR))
		kdb_printf("kdbnearsym: addr=0x%lx, symtab=%p\n", addr, symtab);
	memset(symtab, 0, sizeof(*symtab));

	if (addr < 4096)
		goto out;
	knt1 = debug_kmalloc(knt1_size, GFP_ATOMIC);
	if (!knt1) {
		kdb_printf("kdbnearsym: addr=0x%lx cannot kmalloc knt1\n",
			   addr);
		goto out;
	}
	symtab->sym_name = kallsyms_lookup(addr, &symbolsize , &offset,
				(char **)(&symtab->mod_name), knt1);
	if (offset > 8*1024*1024) {
		symtab->sym_name = NULL;
		addr = offset = symbolsize = 0;
	}
	symtab->sym_start = addr - offset;
	symtab->sym_end = symtab->sym_start + symbolsize;
	ret = symtab->sym_name != NULL && *(symtab->sym_name) != '\0';

	if (ret) {
		int i;
		/* Another 2.6 kallsyms "feature".  Sometimes the sym_name is
		 * set but the buffer passed into kallsyms_lookup is not used,
		 * so it contains garbage.  The caller has to work out which
		 * buffer needs to be saved.
		 *
		 * What was Rusty smoking when he wrote that code?
		 */
		if (symtab->sym_name != knt1) {
			strncpy(knt1, symtab->sym_name, knt1_size);
			knt1[knt1_size-1] = '\0';
		}
		for (i = 0; i < ARRAY_SIZE(kdb_name_table); ++i) {
			if (kdb_name_table[i] &&
			    strcmp(kdb_name_table[i], knt1) == 0)
				break;
		}
		if (i >= ARRAY_SIZE(kdb_name_table)) {
			debug_kfree(kdb_name_table[0]);
			memcpy(kdb_name_table, kdb_name_table+1,
			       sizeof(kdb_name_table[0]) *
			       (ARRAY_SIZE(kdb_name_table)-1));
		} else {
			debug_kfree(knt1);
			knt1 = kdb_name_table[i];
			memcpy(kdb_name_table+i, kdb_name_table+i+1,
			       sizeof(kdb_name_table[0]) *
			       (ARRAY_SIZE(kdb_name_table)-i-1));
		}
		i = ARRAY_SIZE(kdb_name_table) - 1;
		kdb_name_table[i] = knt1;
		symtab->sym_name = kdb_name_table[i];
		knt1 = NULL;
	}

	if (symtab->mod_name == NULL)
		symtab->mod_name = "kernel";
	if (KDB_DEBUG(AR))
		kdb_printf("kdbnearsym: returns %d symtab->sym_start=0x%lx, "
		   "symtab->mod_name=%p, symtab->sym_name=%p (%s)\n", ret,
		   symtab->sym_start, symtab->mod_name, symtab->sym_name,
		   symtab->sym_name);

out:
	debug_kfree(knt1);
	return ret;
}

void kdbnearsym_cleanup(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(kdb_name_table); ++i) {
		if (kdb_name_table[i]) {
			debug_kfree(kdb_name_table[i]);
			kdb_name_table[i] = NULL;
		}
	}
}

static char ks_namebuf[KSYM_NAME_LEN+1], ks_namebuf_prev[KSYM_NAME_LEN+1];

/*
 * kallsyms_symbol_complete
 *
 * Parameters:
 *	prefix_name	prefix of a symbol name to lookup
 *	max_len		maximum length that can be returned
 * Returns:
 *	Number of symbols which match the given prefix.
 * Notes:
 *	prefix_name is changed to contain the longest unique prefix that
 *	starts with this prefix (tab completion).
 */
int kallsyms_symbol_complete(char *prefix_name, int max_len)
{
	loff_t pos = 0;
	int prefix_len = strlen(prefix_name), prev_len = 0;
	int i, number = 0;
	const char *name;

	while ((name = kdb_walk_kallsyms(&pos))) {
		if (strncmp(name, prefix_name, prefix_len) == 0) {
			strcpy(ks_namebuf, name);
			/* Work out the longest name that matches the prefix */
			if (++number == 1) {
				prev_len = min_t(int, max_len-1,
						 strlen(ks_namebuf));
				memcpy(ks_namebuf_prev, ks_namebuf, prev_len);
				ks_namebuf_prev[prev_len] = '\0';
				continue;
			}
			for (i = 0; i < prev_len; i++) {
				if (ks_namebuf[i] != ks_namebuf_prev[i]) {
					prev_len = i;
					ks_namebuf_prev[i] = '\0';
					break;
				}
			}
		}
	}
	if (prev_len > prefix_len)
		memcpy(prefix_name, ks_namebuf_prev, prev_len+1);
	return number;
}

/*
 * kallsyms_symbol_next
 *
 * Parameters:
 *	prefix_name	prefix of a symbol name to lookup
 *	flag	0 means search from the head, 1 means continue search.
 * Returns:
 *	1 if a symbol matches the given prefix.
 *	0 if no string found
 */
int kallsyms_symbol_next(char *prefix_name, int flag)
{
	int prefix_len = strlen(prefix_name);
	static loff_t pos;
	const char *name;

	if (!flag)
		pos = 0;

	while ((name = kdb_walk_kallsyms(&pos))) {
		if (strncmp(name, prefix_name, prefix_len) == 0) {
			strncpy(prefix_name, name, strlen(name)+1);
			return 1;
		}
	}
	return 0;
}

/*
 * kdb_symbol_print - Standard method for printing a symbol name and offset.
 * Inputs:
 *	addr	Address to be printed.
 *	symtab	Address of symbol data, if NULL this routine does its
 *		own lookup.
 *	punc	Punctuation for string, bit field.
 * Remarks:
 *	The string and its punctuation is only printed if the address
 *	is inside the kernel, except that the value is always printed
 *	when requested.
 */
void kdb_symbol_print(unsigned long addr, const kdb_symtab_t *symtab_p,
		      unsigned int punc)
{
	kdb_symtab_t symtab, *symtab_p2;
	if (symtab_p) {
		symtab_p2 = (kdb_symtab_t *)symtab_p;
	} else {
		symtab_p2 = &symtab;
		kdbnearsym(addr, symtab_p2);
	}
	if (!(symtab_p2->sym_name || (punc & KDB_SP_VALUE)))
		return;
	if (punc & KDB_SP_SPACEB)
		kdb_printf(" ");
	if (punc & KDB_SP_VALUE)
		kdb_printf(kdb_machreg_fmt0, addr);
	if (symtab_p2->sym_name) {
		if (punc & KDB_SP_VALUE)
			kdb_printf(" ");
		if (punc & KDB_SP_PAREN)
			kdb_printf("(");
		if (strcmp(symtab_p2->mod_name, "kernel"))
			kdb_printf("[%s]", symtab_p2->mod_name);
		kdb_printf("%s", symtab_p2->sym_name);
		if (addr != symtab_p2->sym_start)
			kdb_printf("+0x%lx", addr - symtab_p2->sym_start);
		if (punc & KDB_SP_SYMSIZE)
			kdb_printf("/0x%lx",
				   symtab_p2->sym_end - symtab_p2->sym_start);
		if (punc & KDB_SP_PAREN)
			kdb_printf(")");
	}
	if (punc & KDB_SP_SPACEA)
		kdb_printf(" ");
	if (punc & KDB_SP_NEWLINE)
		kdb_printf("\n");
}

/*
 * kdb_strdup - kdb equivalent of strdup, for disasm code.
 * Inputs:
 *	str	The string to duplicate.
 *	type	Flags to kmalloc for the new string.
 * Returns:
 *	Address of the new string, NULL if storage could not be allocated.
 * Remarks:
 *	This is not in lib/string.c because it uses kmalloc which is not
 *	available when string.o is used in boot loaders.
 */
char *kdb_strdup(const char *str, gfp_t type)
{
	int n = strlen(str)+1;
	char *s = kmalloc(n, type);
	if (!s)
		return NULL;
	return strcpy(s, str);
}

/*
 * kdb_getarea_size - Read an area of data.  The kdb equivalent of
 *	copy_from_user, with kdb messages for invalid addresses.
 * Inputs:
 *	res	Pointer to the area to receive the result.
 *	addr	Address of the area to copy.
 *	size	Size of the area.
 * Returns:
 *	0 for success, < 0 for error.
 */
int kdb_getarea_size(void *res, unsigned long addr, size_t size)
{
	int ret = probe_kernel_read((char *)res, (char *)addr, size);
	if (ret) {
		if (!KDB_STATE(SUPPRESS)) {
			kdb_printf("kdb_getarea: Bad address 0x%lx\n", addr);
			KDB_STATE_SET(SUPPRESS);
		}
		ret = KDB_BADADDR;
	} else {
		KDB_STATE_CLEAR(SUPPRESS);
	}
	return ret;
}

/*
 * kdb_putarea_size - Write an area of data.  The kdb equivalent of
 *	copy_to_user, with kdb messages for invalid addresses.
 * Inputs:
 *	addr	Address of the area to write to.
 *	res	Pointer to the area holding the data.
 *	size	Size of the area.
 * Returns:
 *	0 for success, < 0 for error.
 */
int kdb_putarea_size(unsigned long addr, void *res, size_t size)
{
	int ret = probe_kernel_read((char *)addr, (char *)res, size);
	if (ret) {
		if (!KDB_STATE(SUPPRESS)) {
			kdb_printf("kdb_putarea: Bad address 0x%lx\n", addr);
			KDB_STATE_SET(SUPPRESS);
		}
		ret = KDB_BADADDR;
	} else {
		KDB_STATE_CLEAR(SUPPRESS);
	}
	return ret;
}

/*
 * kdb_getphys - Read data from a physical address. Validate the
 * 	address is in range, use kmap_atomic() to get data
 * 	similar to kdb_getarea() - but for phys addresses
 * Inputs:
 * 	res	Pointer to the word to receive the result
 * 	addr	Physical address of the area to copy
 * 	size	Size of the area
 * Returns:
 *	0 for success, < 0 for error.
 */
static int kdb_getphys(void *res, unsigned long addr, size_t size)
{
	unsigned long pfn;
	void *vaddr;
	struct page *page;

	pfn = (addr >> PAGE_SHIFT);
	if (!pfn_valid(pfn))
		return 1;
	page = pfn_to_page(pfn);
	vaddr = kmap_atomic(page, KM_KDB);
	memcpy(res, vaddr + (addr & (PAGE_SIZE - 1)), size);
	kunmap_atomic(vaddr, KM_KDB);

	return 0;
}

/*
 * kdb_getphysword
 * Inputs:
 *	word	Pointer to the word to receive the result.
 *	addr	Address of the area to copy.
 *	size	Size of the area.
 * Returns:
 *	0 for success, < 0 for error.
 */
int kdb_getphysword(unsigned long *word, unsigned long addr, size_t size)
{
	int diag;
	__u8  w1;
	__u16 w2;
	__u32 w4;
	__u64 w8;
	*word = 0;	/* Default value if addr or size is invalid */

	switch (size) {
	case 1:
		diag = kdb_getphys(&w1, addr, sizeof(w1));
		if (!diag)
			*word = w1;
		break;
	case 2:
		diag = kdb_getphys(&w2, addr, sizeof(w2));
		if (!diag)
			*word = w2;
		break;
	case 4:
		diag = kdb_getphys(&w4, addr, sizeof(w4));
		if (!diag)
			*word = w4;
		break;
	case 8:
		if (size <= sizeof(*word)) {
			diag = kdb_getphys(&w8, addr, sizeof(w8));
			if (!diag)
				*word = w8;
			break;
		}
		/* drop through */
	default:
		diag = KDB_BADWIDTH;
		kdb_printf("kdb_getphysword: bad width %ld\n", (long) size);
	}
	return diag;
}

/*
 * kdb_getword - Read a binary value.  Unlike kdb_getarea, this treats
 *	data as numbers.
 * Inputs:
 *	word	Pointer to the word to receive the result.
 *	addr	Address of the area to copy.
 *	size	Size of the area.
 * Returns:
 *	0 for success, < 0 for error.
 */
int kdb_getword(unsigned long *word, unsigned long addr, size_t size)
{
	int diag;
	__u8  w1;
	__u16 w2;
	__u32 w4;
	__u64 w8;
	*word = 0;	/* Default value if addr or size is invalid */
	switch (size) {
	case 1:
		diag = kdb_getarea(w1, addr);
		if (!diag)
			*word = w1;
		break;
	case 2:
		diag = kdb_getarea(w2, addr);
		if (!diag)
			*word = w2;
		break;
	case 4:
		diag = kdb_getarea(w4, addr);
		if (!diag)
			*word = w4;
		break;
	case 8:
		if (size <= sizeof(*word)) {
			diag = kdb_getarea(w8, addr);
			if (!diag)
				*word = w8;
			break;
		}
		/* drop through */
	default:
		diag = KDB_BADWIDTH;
		kdb_printf("kdb_getword: bad width %ld\n", (long) size);
	}
	return diag;
}

/*
 * kdb_putword - Write a binary value.  Unlike kdb_putarea, this
 *	treats data as numbers.
 * Inputs:
 *	addr	Address of the area to write to..
 *	word	The value to set.
 *	size	Size of the area.
 * Returns:
 *	0 for success, < 0 for error.
 */
int kdb_putword(unsigned long addr, unsigned long word, size_t size)
{
	int diag;
	__u8  w1;
	__u16 w2;
	__u32 w4;
	__u64 w8;
	switch (size) {
	case 1:
		w1 = word;
		diag = kdb_putarea(addr, w1);
		break;
	case 2:
		w2 = word;
		diag = kdb_putarea(addr, w2);
		break;
	case 4:
		w4 = word;
		diag = kdb_putarea(addr, w4);
		break;
	case 8:
		if (size <= sizeof(word)) {
			w8 = word;
			diag = kdb_putarea(addr, w8);
			break;
		}
		/* drop through */
	default:
		diag = KDB_BADWIDTH;
		kdb_printf("kdb_putword: bad width %ld\n", (long) size);
	}
	return diag;
}

/*
 * kdb_task_state_string - Convert a string containing any of the
 *	letters DRSTCZEUIMA to a mask for the process state field and
 *	return the value.  If no argument is supplied, return the mask
 *	that corresponds to environment variable PS, DRSTCZEU by
 *	default.
 * Inputs:
 *	s	String to convert
 * Returns:
 *	Mask for process state.
 * Notes:
 *	The mask folds data from several sources into a single long value, so
 *	be carefull not to overlap the bits.  TASK_* bits are in the LSB,
 *	special cases like UNRUNNABLE are in the MSB.  As of 2.6.10-rc1 there
 *	is no overlap between TASK_* and EXIT_* but that may not always be
 *	true, so EXIT_* bits are shifted left 16 bits before being stored in
 *	the mask.
 */

/* unrunnable is < 0 */
#define UNRUNNABLE	(1UL << (8*sizeof(unsigned long) - 1))
#define RUNNING		(1UL << (8*sizeof(unsigned long) - 2))
#define IDLE		(1UL << (8*sizeof(unsigned long) - 3))
#define DAEMON		(1UL << (8*sizeof(unsigned long) - 4))

unsigned long kdb_task_state_string(const char *s)
{
	long res = 0;
	if (!s) {
		s = kdbgetenv("PS");
		if (!s)
			s = "DRSTCZEU";	/* default value for ps */
	}
	while (*s) {
		switch (*s) {
		case 'D':
			res |= TASK_UNINTERRUPTIBLE;
			break;
		case 'R':
			res |= RUNNING;
			break;
		case 'S':
			res |= TASK_INTERRUPTIBLE;
			break;
		case 'T':
			res |= TASK_STOPPED;
			break;
		case 'C':
			res |= TASK_TRACED;
			break;
		case 'Z':
			res |= EXIT_ZOMBIE << 16;
			break;
		case 'E':
			res |= EXIT_DEAD << 16;
			break;
		case 'U':
			res |= UNRUNNABLE;
			break;
		case 'I':
			res |= IDLE;
			break;
		case 'M':
			res |= DAEMON;
			break;
		case 'A':
			res = ~0UL;
			break;
		default:
			  kdb_printf("%s: unknown flag '%c' ignored\n",
				     __func__, *s);
			  break;
		}
		++s;
	}
	return res;
}

/*
 * kdb_task_state_char - Return the character that represents the task state.
 * Inputs:
 *	p	struct task for the process
 * Returns:
 *	One character to represent the task state.
 */
char kdb_task_state_char (const struct task_struct *p)
{
	int cpu;
	char state;
	unsigned long tmp;

	if (!p || probe_kernel_read(&tmp, (char *)p, sizeof(unsigned long)))
		return 'E';

	cpu = kdb_process_cpu(p);
	state = (p->state == 0) ? 'R' :
		(p->state < 0) ? 'U' :
		(p->state & TASK_UNINTERRUPTIBLE) ? 'D' :
		(p->state & TASK_STOPPED) ? 'T' :
		(p->state & TASK_TRACED) ? 'C' :
		(p->exit_state & EXIT_ZOMBIE) ? 'Z' :
		(p->exit_state & EXIT_DEAD) ? 'E' :
		(p->state & TASK_INTERRUPTIBLE) ? 'S' : '?';
	if (p->pid == 0) {
		/* Idle task.  Is it really idle, apart from the kdb
		 * interrupt? */
		if (!kdb_task_has_cpu(p) || kgdb_info[cpu].irq_depth == 1) {
			if (cpu != kdb_initial_cpu)
				state = 'I';	/* idle task */
		}
	} else if (!p->mm && state == 'S') {
		state = 'M';	/* sleeping system daemon */
	}
	return state;
}

/*
 * kdb_task_state - Return true if a process has the desired state
 *	given by the mask.
 * Inputs:
 *	p	struct task for the process
 *	mask	mask from kdb_task_state_string to select processes
 * Returns:
 *	True if the process matches at least one criteria defined by the mask.
 */
unsigned long kdb_task_state(const struct task_struct *p, unsigned long mask)
{
	char state[] = { kdb_task_state_char(p), '\0' };
	return (mask & kdb_task_state_string(state)) != 0;
}

/*
 * kdb_print_nameval - Print a name and its value, converting the
 *	value to a symbol lookup if possible.
 * Inputs:
 *	name	field name to print
 *	val	value of field
 */
void kdb_print_nameval(const char *name, unsigned long val)
{
	kdb_symtab_t symtab;
	kdb_printf("  %-11.11s ", name);
	if (kdbnearsym(val, &symtab))
		kdb_symbol_print(val, &symtab,
				 KDB_SP_VALUE|KDB_SP_SYMSIZE|KDB_SP_NEWLINE);
	else
		kdb_printf("0x%lx\n", val);
}

/* Last ditch allocator for debugging, so we can still debug even when
 * the GFP_ATOMIC pool has been exhausted.  The algorithms are tuned
 * for space usage, not for speed.  One smallish memory pool, the free
 * chain is always in ascending address order to allow coalescing,
 * allocations are done in brute force best fit.
 */

struct debug_alloc_header {
	u32 next;	/* offset of next header from start of pool */
	u32 size;
	void *caller;
};

/* The memory returned by this allocator must be aligned, which means
 * so must the header size.  Do not assume that sizeof(struct
 * debug_alloc_header) is a multiple of the alignment, explicitly
 * calculate the overhead of this header, including the alignment.
 * The rest of this code must not use sizeof() on any header or
 * pointer to a header.
 */
#define dah_align 8
#define dah_overhead ALIGN(sizeof(struct debug_alloc_header), dah_align)

static u64 debug_alloc_pool_aligned[256*1024/dah_align];	/* 256K pool */
static char *debug_alloc_pool = (char *)debug_alloc_pool_aligned;
static u32 dah_first, dah_first_call = 1, dah_used, dah_used_max;

/* Locking is awkward.  The debug code is called from all contexts,
 * including non maskable interrupts.  A normal spinlock is not safe
 * in NMI context.  Try to get the debug allocator lock, if it cannot
 * be obtained after a second then give up.  If the lock could not be
 * previously obtained on this cpu then only try once.
 *
 * sparse has no annotation for "this function _sometimes_ acquires a
 * lock", so fudge the acquire/release notation.
 */
static DEFINE_SPINLOCK(dap_lock);
static int get_dap_lock(void)
	__acquires(dap_lock)
{
	static int dap_locked = -1;
	int count;
	if (dap_locked == smp_processor_id())
		count = 1;
	else
		count = 1000;
	while (1) {
		if (spin_trylock(&dap_lock)) {
			dap_locked = -1;
			return 1;
		}
		if (!count--)
			break;
		udelay(1000);
	}
	dap_locked = smp_processor_id();
	__acquire(dap_lock);
	return 0;
}

void *debug_kmalloc(size_t size, gfp_t flags)
{
	unsigned int rem, h_offset;
	struct debug_alloc_header *best, *bestprev, *prev, *h;
	void *p = NULL;
	if (!get_dap_lock()) {
		__release(dap_lock);	/* we never actually got it */
		return NULL;
	}
	h = (struct debug_alloc_header *)(debug_alloc_pool + dah_first);
	if (dah_first_call) {
		h->size = sizeof(debug_alloc_pool_aligned) - dah_overhead;
		dah_first_call = 0;
	}
	size = ALIGN(size, dah_align);
	prev = best = bestprev = NULL;
	while (1) {
		if (h->size >= size && (!best || h->size < best->size)) {
			best = h;
			bestprev = prev;
			if (h->size == size)
				break;
		}
		if (!h->next)
			break;
		prev = h;
		h = (struct debug_alloc_header *)(debug_alloc_pool + h->next);
	}
	if (!best)
		goto out;
	rem = best->size - size;
	/* The pool must always contain at least one header */
	if (best->next == 0 && bestprev == NULL && rem < dah_overhead)
		goto out;
	if (rem >= dah_overhead) {
		best->size = size;
		h_offset = ((char *)best - debug_alloc_pool) +
			   dah_overhead + best->size;
		h = (struct debug_alloc_header *)(debug_alloc_pool + h_offset);
		h->size = rem - dah_overhead;
		h->next = best->next;
	} else
		h_offset = best->next;
	best->caller = __builtin_return_address(0);
	dah_used += best->size;
	dah_used_max = max(dah_used, dah_used_max);
	if (bestprev)
		bestprev->next = h_offset;
	else
		dah_first = h_offset;
	p = (char *)best + dah_overhead;
	memset(p, POISON_INUSE, best->size - 1);
	*((char *)p + best->size - 1) = POISON_END;
out:
	spin_unlock(&dap_lock);
	return p;
}

void debug_kfree(void *p)
{
	struct debug_alloc_header *h;
	unsigned int h_offset;
	if (!p)
		return;
	if ((char *)p < debug_alloc_pool ||
	    (char *)p >= debug_alloc_pool + sizeof(debug_alloc_pool_aligned)) {
		kfree(p);
		return;
	}
	if (!get_dap_lock()) {
		__release(dap_lock);	/* we never actually got it */
		return;		/* memory leak, cannot be helped */
	}
	h = (struct debug_alloc_header *)((char *)p - dah_overhead);
	memset(p, POISON_FREE, h->size - 1);
	*((char *)p + h->size - 1) = POISON_END;
	h->caller = NULL;
	dah_used -= h->size;
	h_offset = (char *)h - debug_alloc_pool;
	if (h_offset < dah_first) {
		h->next = dah_first;
		dah_first = h_offset;
	} else {
		struct debug_alloc_header *prev;
		unsigned int prev_offset;
		prev = (struct debug_alloc_header *)(debug_alloc_pool +
						     dah_first);
		while (1) {
			if (!prev->next || prev->next > h_offset)
				break;
			prev = (struct debug_alloc_header *)
				(debug_alloc_pool + prev->next);
		}
		prev_offset = (char *)prev - debug_alloc_pool;
		if (prev_offset + dah_overhead + prev->size == h_offset) {
			prev->size += dah_overhead + h->size;
			memset(h, POISON_FREE, dah_overhead - 1);
			*((char *)h + dah_overhead - 1) = POISON_END;
			h = prev;
			h_offset = prev_offset;
		} else {
			h->next = prev->next;
			prev->next = h_offset;
		}
	}
	if (h_offset + dah_overhead + h->size == h->next) {
		struct debug_alloc_header *next;
		next = (struct debug_alloc_header *)
			(debug_alloc_pool + h->next);
		h->size += dah_overhead + next->size;
		h->next = next->next;
		memset(next, POISON_FREE, dah_overhead - 1);
		*((char *)next + dah_overhead - 1) = POISON_END;
	}
	spin_unlock(&dap_lock);
}

void debug_kusage(void)
{
	struct debug_alloc_header *h_free, *h_used;
#ifdef	CONFIG_IA64
	/* FIXME: using dah for ia64 unwind always results in a memory leak.
	 * Fix that memory leak first, then set debug_kusage_one_time = 1 for
	 * all architectures.
	 */
	static int debug_kusage_one_time;
#else
	static int debug_kusage_one_time = 1;
#endif
	if (!get_dap_lock()) {
		__release(dap_lock);	/* we never actually got it */
		return;
	}
	h_free = (struct debug_alloc_header *)(debug_alloc_pool + dah_first);
	if (dah_first == 0 &&
	    (h_free->size == sizeof(debug_alloc_pool_aligned) - dah_overhead ||
	     dah_first_call))
		goto out;
	if (!debug_kusage_one_time)
		goto out;
	debug_kusage_one_time = 0;
	kdb_printf("%s: debug_kmalloc memory leak dah_first %d\n",
		   __func__, dah_first);
	if (dah_first) {
		h_used = (struct debug_alloc_header *)debug_alloc_pool;
		kdb_printf("%s: h_used %p size %d\n", __func__, h_used,
			   h_used->size);
	}
	do {
		h_used = (struct debug_alloc_header *)
			  ((char *)h_free + dah_overhead + h_free->size);
		kdb_printf("%s: h_used %p size %d caller %p\n",
			   __func__, h_used, h_used->size, h_used->caller);
		h_free = (struct debug_alloc_header *)
			  (debug_alloc_pool + h_free->next);
	} while (h_free->next);
	h_used = (struct debug_alloc_header *)
		  ((char *)h_free + dah_overhead + h_free->size);
	if ((char *)h_used - debug_alloc_pool !=
	    sizeof(debug_alloc_pool_aligned))
		kdb_printf("%s: h_used %p size %d caller %p\n",
			   __func__, h_used, h_used->size, h_used->caller);
out:
	spin_unlock(&dap_lock);
}

/* Maintain a small stack of kdb_flags to allow recursion without disturbing
 * the global kdb state.
 */

static int kdb_flags_stack[4], kdb_flags_index;

void kdb_save_flags(void)
{
	BUG_ON(kdb_flags_index >= ARRAY_SIZE(kdb_flags_stack));
	kdb_flags_stack[kdb_flags_index++] = kdb_flags;
}

void kdb_restore_flags(void)
{
	BUG_ON(kdb_flags_index <= 0);
	kdb_flags = kdb_flags_stack[--kdb_flags_index];
}
