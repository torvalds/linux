// SPDX-License-Identifier: GPL-2.0-only
/*
 * kallsyms.c: in-kernel printing of symbolic oopses and stack traces.
 *
 * Rewritten and vastly simplified by Rusty Russell for in-kernel
 * module loader:
 *   Copyright 2002 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
 *
 * ChangeLog:
 *
 * (25/Aug/2004) Paulo Marques <pmarques@grupopie.com>
 *      Changed the compression method from stem compression to "table lookup"
 *      compression (see scripts/kallsyms.c for a more complete description)
 */
#include <linux/kallsyms.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/kdb.h>
#include <linux/err.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>	/* for cond_resched */
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/filter.h>
#include <linux/ftrace.h>
#include <linux/kprobes.h>
#include <linux/build_bug.h>
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bsearch.h>
#include <linux/btf_ids.h>

#include "kallsyms_internal.h"

/*
 * Expand a compressed symbol data into the resulting uncompressed string,
 * if uncompressed string is too long (>= maxlen), it will be truncated,
 * given the offset to where the symbol is in the compressed stream.
 */
static unsigned int kallsyms_expand_symbol(unsigned int off,
					   char *result, size_t maxlen)
{
	int len, skipped_first = 0;
	const char *tptr;
	const u8 *data;

	/* Get the compressed symbol length from the first symbol byte. */
	data = &kallsyms_names[off];
	len = *data;
	data++;
	off++;

	/* If MSB is 1, it is a "big" symbol, so needs an additional byte. */
	if ((len & 0x80) != 0) {
		len = (len & 0x7F) | (*data << 7);
		data++;
		off++;
	}

	/*
	 * Update the offset to return the offset for the next symbol on
	 * the compressed stream.
	 */
	off += len;

	/*
	 * For every byte on the compressed symbol data, copy the table
	 * entry for that byte.
	 */
	while (len) {
		tptr = &kallsyms_token_table[kallsyms_token_index[*data]];
		data++;
		len--;

		while (*tptr) {
			if (skipped_first) {
				if (maxlen <= 1)
					goto tail;
				*result = *tptr;
				result++;
				maxlen--;
			} else
				skipped_first = 1;
			tptr++;
		}
	}

tail:
	if (maxlen)
		*result = '\0';

	/* Return to offset to the next symbol. */
	return off;
}

/*
 * Get symbol type information. This is encoded as a single char at the
 * beginning of the symbol name.
 */
static char kallsyms_get_symbol_type(unsigned int off)
{
	/*
	 * Get just the first code, look it up in the token table,
	 * and return the first char from this token.
	 */
	return kallsyms_token_table[kallsyms_token_index[kallsyms_names[off + 1]]];
}


/*
 * Find the offset on the compressed stream given and index in the
 * kallsyms array.
 */
static unsigned int get_symbol_offset(unsigned long pos)
{
	const u8 *name;
	int i, len;

	/*
	 * Use the closest marker we have. We have markers every 256 positions,
	 * so that should be close enough.
	 */
	name = &kallsyms_names[kallsyms_markers[pos >> 8]];

	/*
	 * Sequentially scan all the symbols up to the point we're searching
	 * for. Every symbol is stored in a [<len>][<len> bytes of data] format,
	 * so we just need to add the len to the current pointer for every
	 * symbol we wish to skip.
	 */
	for (i = 0; i < (pos & 0xFF); i++) {
		len = *name;

		/*
		 * If MSB is 1, it is a "big" symbol, so we need to look into
		 * the next byte (and skip it, too).
		 */
		if ((len & 0x80) != 0)
			len = ((len & 0x7F) | (name[1] << 7)) + 1;

		name = name + len + 1;
	}

	return name - kallsyms_names;
}

unsigned long kallsyms_sym_address(int idx)
{
	if (!IS_ENABLED(CONFIG_KALLSYMS_BASE_RELATIVE))
		return kallsyms_addresses[idx];

	/* values are unsigned offsets if --absolute-percpu is not in effect */
	if (!IS_ENABLED(CONFIG_KALLSYMS_ABSOLUTE_PERCPU))
		return kallsyms_relative_base + (u32)kallsyms_offsets[idx];

	/* ...otherwise, positive offsets are absolute values */
	if (kallsyms_offsets[idx] >= 0)
		return kallsyms_offsets[idx];

	/* ...and negative offsets are relative to kallsyms_relative_base - 1 */
	return kallsyms_relative_base - 1 - kallsyms_offsets[idx];
}

static bool cleanup_symbol_name(char *s)
{
	char *res;

	if (!IS_ENABLED(CONFIG_LTO_CLANG))
		return false;

	/*
	 * LLVM appends various suffixes for local functions and variables that
	 * must be promoted to global scope as part of LTO.  This can break
	 * hooking of static functions with kprobes. '.' is not a valid
	 * character in an identifier in C. Suffixes only in LLVM LTO observed:
	 * - foo.llvm.[0-9a-f]+
	 */
	res = strstr(s, ".llvm.");
	if (res) {
		*res = '\0';
		return true;
	}

	return false;
}

static int compare_symbol_name(const char *name, char *namebuf)
{
	int ret;

	ret = strcmp(name, namebuf);
	if (!ret)
		return ret;

	if (cleanup_symbol_name(namebuf) && !strcmp(name, namebuf))
		return 0;

	return ret;
}

static unsigned int get_symbol_seq(int index)
{
	unsigned int i, seq = 0;

	for (i = 0; i < 3; i++)
		seq = (seq << 8) | kallsyms_seqs_of_names[3 * index + i];

	return seq;
}

static int kallsyms_lookup_names(const char *name,
				 unsigned int *start,
				 unsigned int *end)
{
	int ret;
	int low, mid, high;
	unsigned int seq, off;
	char namebuf[KSYM_NAME_LEN];

	low = 0;
	high = kallsyms_num_syms - 1;

	while (low <= high) {
		mid = low + (high - low) / 2;
		seq = get_symbol_seq(mid);
		off = get_symbol_offset(seq);
		kallsyms_expand_symbol(off, namebuf, ARRAY_SIZE(namebuf));
		ret = compare_symbol_name(name, namebuf);
		if (ret > 0)
			low = mid + 1;
		else if (ret < 0)
			high = mid - 1;
		else
			break;
	}

	if (low > high)
		return -ESRCH;

	low = mid;
	while (low) {
		seq = get_symbol_seq(low - 1);
		off = get_symbol_offset(seq);
		kallsyms_expand_symbol(off, namebuf, ARRAY_SIZE(namebuf));
		if (compare_symbol_name(name, namebuf))
			break;
		low--;
	}
	*start = low;

	if (end) {
		high = mid;
		while (high < kallsyms_num_syms - 1) {
			seq = get_symbol_seq(high + 1);
			off = get_symbol_offset(seq);
			kallsyms_expand_symbol(off, namebuf, ARRAY_SIZE(namebuf));
			if (compare_symbol_name(name, namebuf))
				break;
			high++;
		}
		*end = high;
	}

	return 0;
}

/* Lookup the address for this symbol. Returns 0 if not found. */
unsigned long kallsyms_lookup_name(const char *name)
{
	int ret;
	unsigned int i;

	/* Skip the search for empty string. */
	if (!*name)
		return 0;

	ret = kallsyms_lookup_names(name, &i, NULL);
	if (!ret)
		return kallsyms_sym_address(get_symbol_seq(i));

	return module_kallsyms_lookup_name(name);
}

/*
 * Iterate over all symbols in vmlinux.  For symbols from modules use
 * module_kallsyms_on_each_symbol instead.
 */
int kallsyms_on_each_symbol(int (*fn)(void *, const char *, unsigned long),
			    void *data)
{
	char namebuf[KSYM_NAME_LEN];
	unsigned long i;
	unsigned int off;
	int ret;

	for (i = 0, off = 0; i < kallsyms_num_syms; i++) {
		off = kallsyms_expand_symbol(off, namebuf, ARRAY_SIZE(namebuf));
		ret = fn(data, namebuf, kallsyms_sym_address(i));
		if (ret != 0)
			return ret;
		cond_resched();
	}
	return 0;
}

int kallsyms_on_each_match_symbol(int (*fn)(void *, unsigned long),
				  const char *name, void *data)
{
	int ret;
	unsigned int i, start, end;

	ret = kallsyms_lookup_names(name, &start, &end);
	if (ret)
		return 0;

	for (i = start; !ret && i <= end; i++) {
		ret = fn(data, kallsyms_sym_address(get_symbol_seq(i)));
		cond_resched();
	}

	return ret;
}

static unsigned long get_symbol_pos(unsigned long addr,
				    unsigned long *symbolsize,
				    unsigned long *offset)
{
	unsigned long symbol_start = 0, symbol_end = 0;
	unsigned long i, low, high, mid;

	/* This kernel should never had been booted. */
	if (!IS_ENABLED(CONFIG_KALLSYMS_BASE_RELATIVE))
		BUG_ON(!kallsyms_addresses);
	else
		BUG_ON(!kallsyms_offsets);

	/* Do a binary search on the sorted kallsyms_addresses array. */
	low = 0;
	high = kallsyms_num_syms;

	while (high - low > 1) {
		mid = low + (high - low) / 2;
		if (kallsyms_sym_address(mid) <= addr)
			low = mid;
		else
			high = mid;
	}

	/*
	 * Search for the first aliased symbol. Aliased
	 * symbols are symbols with the same address.
	 */
	while (low && kallsyms_sym_address(low-1) == kallsyms_sym_address(low))
		--low;

	symbol_start = kallsyms_sym_address(low);

	/* Search for next non-aliased symbol. */
	for (i = low + 1; i < kallsyms_num_syms; i++) {
		if (kallsyms_sym_address(i) > symbol_start) {
			symbol_end = kallsyms_sym_address(i);
			break;
		}
	}

	/* If we found no next symbol, we use the end of the section. */
	if (!symbol_end) {
		if (is_kernel_inittext(addr))
			symbol_end = (unsigned long)_einittext;
		else if (IS_ENABLED(CONFIG_KALLSYMS_ALL))
			symbol_end = (unsigned long)_end;
		else
			symbol_end = (unsigned long)_etext;
	}

	if (symbolsize)
		*symbolsize = symbol_end - symbol_start;
	if (offset)
		*offset = addr - symbol_start;

	return low;
}

/*
 * Lookup an address but don't bother to find any names.
 */
int kallsyms_lookup_size_offset(unsigned long addr, unsigned long *symbolsize,
				unsigned long *offset)
{
	char namebuf[KSYM_NAME_LEN];

	if (is_ksym_addr(addr)) {
		get_symbol_pos(addr, symbolsize, offset);
		return 1;
	}
	return !!module_address_lookup(addr, symbolsize, offset, NULL, NULL, namebuf) ||
	       !!__bpf_address_lookup(addr, symbolsize, offset, namebuf);
}

static const char *kallsyms_lookup_buildid(unsigned long addr,
			unsigned long *symbolsize,
			unsigned long *offset, char **modname,
			const unsigned char **modbuildid, char *namebuf)
{
	const char *ret;

	namebuf[KSYM_NAME_LEN - 1] = 0;
	namebuf[0] = 0;

	if (is_ksym_addr(addr)) {
		unsigned long pos;

		pos = get_symbol_pos(addr, symbolsize, offset);
		/* Grab name */
		kallsyms_expand_symbol(get_symbol_offset(pos),
				       namebuf, KSYM_NAME_LEN);
		if (modname)
			*modname = NULL;
		if (modbuildid)
			*modbuildid = NULL;

		ret = namebuf;
		goto found;
	}

	/* See if it's in a module or a BPF JITed image. */
	ret = module_address_lookup(addr, symbolsize, offset,
				    modname, modbuildid, namebuf);
	if (!ret)
		ret = bpf_address_lookup(addr, symbolsize,
					 offset, modname, namebuf);

	if (!ret)
		ret = ftrace_mod_address_lookup(addr, symbolsize,
						offset, modname, namebuf);

found:
	cleanup_symbol_name(namebuf);
	return ret;
}

/*
 * Lookup an address
 * - modname is set to NULL if it's in the kernel.
 * - We guarantee that the returned name is valid until we reschedule even if.
 *   It resides in a module.
 * - We also guarantee that modname will be valid until rescheduled.
 */
const char *kallsyms_lookup(unsigned long addr,
			    unsigned long *symbolsize,
			    unsigned long *offset,
			    char **modname, char *namebuf)
{
	return kallsyms_lookup_buildid(addr, symbolsize, offset, modname,
				       NULL, namebuf);
}

int lookup_symbol_name(unsigned long addr, char *symname)
{
	int res;

	symname[0] = '\0';
	symname[KSYM_NAME_LEN - 1] = '\0';

	if (is_ksym_addr(addr)) {
		unsigned long pos;

		pos = get_symbol_pos(addr, NULL, NULL);
		/* Grab name */
		kallsyms_expand_symbol(get_symbol_offset(pos),
				       symname, KSYM_NAME_LEN);
		goto found;
	}
	/* See if it's in a module. */
	res = lookup_module_symbol_name(addr, symname);
	if (res)
		return res;

found:
	cleanup_symbol_name(symname);
	return 0;
}

/* Look up a kernel symbol and return it in a text buffer. */
static int __sprint_symbol(char *buffer, unsigned long address,
			   int symbol_offset, int add_offset, int add_buildid)
{
	char *modname;
	const unsigned char *buildid;
	const char *name;
	unsigned long offset, size;
	int len;

	address += symbol_offset;
	name = kallsyms_lookup_buildid(address, &size, &offset, &modname, &buildid,
				       buffer);
	if (!name)
		return sprintf(buffer, "0x%lx", address - symbol_offset);

	if (name != buffer)
		strcpy(buffer, name);
	len = strlen(buffer);
	offset -= symbol_offset;

	if (add_offset)
		len += sprintf(buffer + len, "+%#lx/%#lx", offset, size);

	if (modname) {
		len += sprintf(buffer + len, " [%s", modname);
#if IS_ENABLED(CONFIG_STACKTRACE_BUILD_ID)
		if (add_buildid && buildid) {
			/* build ID should match length of sprintf */
#if IS_ENABLED(CONFIG_MODULES)
			static_assert(sizeof(typeof_member(struct module, build_id)) == 20);
#endif
			len += sprintf(buffer + len, " %20phN", buildid);
		}
#endif
		len += sprintf(buffer + len, "]");
	}

	return len;
}

/**
 * sprint_symbol - Look up a kernel symbol and return it in a text buffer
 * @buffer: buffer to be stored
 * @address: address to lookup
 *
 * This function looks up a kernel symbol with @address and stores its name,
 * offset, size and module name to @buffer if possible. If no symbol was found,
 * just saves its @address as is.
 *
 * This function returns the number of bytes stored in @buffer.
 */
int sprint_symbol(char *buffer, unsigned long address)
{
	return __sprint_symbol(buffer, address, 0, 1, 0);
}
EXPORT_SYMBOL_GPL(sprint_symbol);

/**
 * sprint_symbol_build_id - Look up a kernel symbol and return it in a text buffer
 * @buffer: buffer to be stored
 * @address: address to lookup
 *
 * This function looks up a kernel symbol with @address and stores its name,
 * offset, size, module name and module build ID to @buffer if possible. If no
 * symbol was found, just saves its @address as is.
 *
 * This function returns the number of bytes stored in @buffer.
 */
int sprint_symbol_build_id(char *buffer, unsigned long address)
{
	return __sprint_symbol(buffer, address, 0, 1, 1);
}
EXPORT_SYMBOL_GPL(sprint_symbol_build_id);

/**
 * sprint_symbol_no_offset - Look up a kernel symbol and return it in a text buffer
 * @buffer: buffer to be stored
 * @address: address to lookup
 *
 * This function looks up a kernel symbol with @address and stores its name
 * and module name to @buffer if possible. If no symbol was found, just saves
 * its @address as is.
 *
 * This function returns the number of bytes stored in @buffer.
 */
int sprint_symbol_no_offset(char *buffer, unsigned long address)
{
	return __sprint_symbol(buffer, address, 0, 0, 0);
}
EXPORT_SYMBOL_GPL(sprint_symbol_no_offset);

/**
 * sprint_backtrace - Look up a backtrace symbol and return it in a text buffer
 * @buffer: buffer to be stored
 * @address: address to lookup
 *
 * This function is for stack backtrace and does the same thing as
 * sprint_symbol() but with modified/decreased @address. If there is a
 * tail-call to the function marked "noreturn", gcc optimized out code after
 * the call so that the stack-saved return address could point outside of the
 * caller. This function ensures that kallsyms will find the original caller
 * by decreasing @address.
 *
 * This function returns the number of bytes stored in @buffer.
 */
int sprint_backtrace(char *buffer, unsigned long address)
{
	return __sprint_symbol(buffer, address, -1, 1, 0);
}

/**
 * sprint_backtrace_build_id - Look up a backtrace symbol and return it in a text buffer
 * @buffer: buffer to be stored
 * @address: address to lookup
 *
 * This function is for stack backtrace and does the same thing as
 * sprint_symbol() but with modified/decreased @address. If there is a
 * tail-call to the function marked "noreturn", gcc optimized out code after
 * the call so that the stack-saved return address could point outside of the
 * caller. This function ensures that kallsyms will find the original caller
 * by decreasing @address. This function also appends the module build ID to
 * the @buffer if @address is within a kernel module.
 *
 * This function returns the number of bytes stored in @buffer.
 */
int sprint_backtrace_build_id(char *buffer, unsigned long address)
{
	return __sprint_symbol(buffer, address, -1, 1, 1);
}

/* To avoid using get_symbol_offset for every symbol, we carry prefix along. */
struct kallsym_iter {
	loff_t pos;
	loff_t pos_mod_end;
	loff_t pos_ftrace_mod_end;
	loff_t pos_bpf_end;
	unsigned long value;
	unsigned int nameoff; /* If iterating in core kernel symbols. */
	char type;
	char name[KSYM_NAME_LEN];
	char module_name[MODULE_NAME_LEN];
	int exported;
	int show_value;
};

static int get_ksymbol_mod(struct kallsym_iter *iter)
{
	int ret = module_get_kallsym(iter->pos - kallsyms_num_syms,
				     &iter->value, &iter->type,
				     iter->name, iter->module_name,
				     &iter->exported);
	if (ret < 0) {
		iter->pos_mod_end = iter->pos;
		return 0;
	}

	return 1;
}

/*
 * ftrace_mod_get_kallsym() may also get symbols for pages allocated for ftrace
 * purposes. In that case "__builtin__ftrace" is used as a module name, even
 * though "__builtin__ftrace" is not a module.
 */
static int get_ksymbol_ftrace_mod(struct kallsym_iter *iter)
{
	int ret = ftrace_mod_get_kallsym(iter->pos - iter->pos_mod_end,
					 &iter->value, &iter->type,
					 iter->name, iter->module_name,
					 &iter->exported);
	if (ret < 0) {
		iter->pos_ftrace_mod_end = iter->pos;
		return 0;
	}

	return 1;
}

static int get_ksymbol_bpf(struct kallsym_iter *iter)
{
	int ret;

	strscpy(iter->module_name, "bpf", MODULE_NAME_LEN);
	iter->exported = 0;
	ret = bpf_get_kallsym(iter->pos - iter->pos_ftrace_mod_end,
			      &iter->value, &iter->type,
			      iter->name);
	if (ret < 0) {
		iter->pos_bpf_end = iter->pos;
		return 0;
	}

	return 1;
}

/*
 * This uses "__builtin__kprobes" as a module name for symbols for pages
 * allocated for kprobes' purposes, even though "__builtin__kprobes" is not a
 * module.
 */
static int get_ksymbol_kprobe(struct kallsym_iter *iter)
{
	strscpy(iter->module_name, "__builtin__kprobes", MODULE_NAME_LEN);
	iter->exported = 0;
	return kprobe_get_kallsym(iter->pos - iter->pos_bpf_end,
				  &iter->value, &iter->type,
				  iter->name) < 0 ? 0 : 1;
}

/* Returns space to next name. */
static unsigned long get_ksymbol_core(struct kallsym_iter *iter)
{
	unsigned off = iter->nameoff;

	iter->module_name[0] = '\0';
	iter->value = kallsyms_sym_address(iter->pos);

	iter->type = kallsyms_get_symbol_type(off);

	off = kallsyms_expand_symbol(off, iter->name, ARRAY_SIZE(iter->name));

	return off - iter->nameoff;
}

static void reset_iter(struct kallsym_iter *iter, loff_t new_pos)
{
	iter->name[0] = '\0';
	iter->nameoff = get_symbol_offset(new_pos);
	iter->pos = new_pos;
	if (new_pos == 0) {
		iter->pos_mod_end = 0;
		iter->pos_ftrace_mod_end = 0;
		iter->pos_bpf_end = 0;
	}
}

/*
 * The end position (last + 1) of each additional kallsyms section is recorded
 * in iter->pos_..._end as each section is added, and so can be used to
 * determine which get_ksymbol_...() function to call next.
 */
static int update_iter_mod(struct kallsym_iter *iter, loff_t pos)
{
	iter->pos = pos;

	if ((!iter->pos_mod_end || iter->pos_mod_end > pos) &&
	    get_ksymbol_mod(iter))
		return 1;

	if ((!iter->pos_ftrace_mod_end || iter->pos_ftrace_mod_end > pos) &&
	    get_ksymbol_ftrace_mod(iter))
		return 1;

	if ((!iter->pos_bpf_end || iter->pos_bpf_end > pos) &&
	    get_ksymbol_bpf(iter))
		return 1;

	return get_ksymbol_kprobe(iter);
}

/* Returns false if pos at or past end of file. */
static int update_iter(struct kallsym_iter *iter, loff_t pos)
{
	/* Module symbols can be accessed randomly. */
	if (pos >= kallsyms_num_syms)
		return update_iter_mod(iter, pos);

	/* If we're not on the desired position, reset to new position. */
	if (pos != iter->pos)
		reset_iter(iter, pos);

	iter->nameoff += get_ksymbol_core(iter);
	iter->pos++;

	return 1;
}

static void *s_next(struct seq_file *m, void *p, loff_t *pos)
{
	(*pos)++;

	if (!update_iter(m->private, *pos))
		return NULL;
	return p;
}

static void *s_start(struct seq_file *m, loff_t *pos)
{
	if (!update_iter(m->private, *pos))
		return NULL;
	return m->private;
}

static void s_stop(struct seq_file *m, void *p)
{
}

static int s_show(struct seq_file *m, void *p)
{
	void *value;
	struct kallsym_iter *iter = m->private;

	/* Some debugging symbols have no name.  Ignore them. */
	if (!iter->name[0])
		return 0;

	value = iter->show_value ? (void *)iter->value : NULL;

	if (iter->module_name[0]) {
		char type;

		/*
		 * Label it "global" if it is exported,
		 * "local" if not exported.
		 */
		type = iter->exported ? toupper(iter->type) :
					tolower(iter->type);
		seq_printf(m, "%px %c %s\t[%s]\n", value,
			   type, iter->name, iter->module_name);
	} else
		seq_printf(m, "%px %c %s\n", value,
			   iter->type, iter->name);
	return 0;
}

static const struct seq_operations kallsyms_op = {
	.start = s_start,
	.next = s_next,
	.stop = s_stop,
	.show = s_show
};

#ifdef CONFIG_BPF_SYSCALL

struct bpf_iter__ksym {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct kallsym_iter *, ksym);
};

static int ksym_prog_seq_show(struct seq_file *m, bool in_stop)
{
	struct bpf_iter__ksym ctx;
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;

	meta.seq = m;
	prog = bpf_iter_get_info(&meta, in_stop);
	if (!prog)
		return 0;

	ctx.meta = &meta;
	ctx.ksym = m ? m->private : NULL;
	return bpf_iter_run_prog(prog, &ctx);
}

static int bpf_iter_ksym_seq_show(struct seq_file *m, void *p)
{
	return ksym_prog_seq_show(m, false);
}

static void bpf_iter_ksym_seq_stop(struct seq_file *m, void *p)
{
	if (!p)
		(void) ksym_prog_seq_show(m, true);
	else
		s_stop(m, p);
}

static const struct seq_operations bpf_iter_ksym_ops = {
	.start = s_start,
	.next = s_next,
	.stop = bpf_iter_ksym_seq_stop,
	.show = bpf_iter_ksym_seq_show,
};

static int bpf_iter_ksym_init(void *priv_data, struct bpf_iter_aux_info *aux)
{
	struct kallsym_iter *iter = priv_data;

	reset_iter(iter, 0);

	/* cache here as in kallsyms_open() case; use current process
	 * credentials to tell BPF iterators if values should be shown.
	 */
	iter->show_value = kallsyms_show_value(current_cred());

	return 0;
}

DEFINE_BPF_ITER_FUNC(ksym, struct bpf_iter_meta *meta, struct kallsym_iter *ksym)

static const struct bpf_iter_seq_info ksym_iter_seq_info = {
	.seq_ops		= &bpf_iter_ksym_ops,
	.init_seq_private	= bpf_iter_ksym_init,
	.fini_seq_private	= NULL,
	.seq_priv_size		= sizeof(struct kallsym_iter),
};

static struct bpf_iter_reg ksym_iter_reg_info = {
	.target                 = "ksym",
	.feature		= BPF_ITER_RESCHED,
	.ctx_arg_info_size	= 1,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__ksym, ksym),
		  PTR_TO_BTF_ID_OR_NULL },
	},
	.seq_info		= &ksym_iter_seq_info,
};

BTF_ID_LIST(btf_ksym_iter_id)
BTF_ID(struct, kallsym_iter)

static int __init bpf_ksym_iter_register(void)
{
	ksym_iter_reg_info.ctx_arg_info[0].btf_id = *btf_ksym_iter_id;
	return bpf_iter_reg_target(&ksym_iter_reg_info);
}

late_initcall(bpf_ksym_iter_register);

#endif /* CONFIG_BPF_SYSCALL */

static int kallsyms_open(struct inode *inode, struct file *file)
{
	/*
	 * We keep iterator in m->private, since normal case is to
	 * s_start from where we left off, so we avoid doing
	 * using get_symbol_offset for every symbol.
	 */
	struct kallsym_iter *iter;
	iter = __seq_open_private(file, &kallsyms_op, sizeof(*iter));
	if (!iter)
		return -ENOMEM;
	reset_iter(iter, 0);

	/*
	 * Instead of checking this on every s_show() call, cache
	 * the result here at open time.
	 */
	iter->show_value = kallsyms_show_value(file->f_cred);
	return 0;
}

#ifdef	CONFIG_KGDB_KDB
const char *kdb_walk_kallsyms(loff_t *pos)
{
	static struct kallsym_iter kdb_walk_kallsyms_iter;
	if (*pos == 0) {
		memset(&kdb_walk_kallsyms_iter, 0,
		       sizeof(kdb_walk_kallsyms_iter));
		reset_iter(&kdb_walk_kallsyms_iter, 0);
	}
	while (1) {
		if (!update_iter(&kdb_walk_kallsyms_iter, *pos))
			return NULL;
		++*pos;
		/* Some debugging symbols have no name.  Ignore them. */
		if (kdb_walk_kallsyms_iter.name[0])
			return kdb_walk_kallsyms_iter.name;
	}
}
#endif	/* CONFIG_KGDB_KDB */

static const struct proc_ops kallsyms_proc_ops = {
	.proc_open	= kallsyms_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release_private,
};

static int __init kallsyms_init(void)
{
	proc_create("kallsyms", 0444, NULL, &kallsyms_proc_ops);
	return 0;
}
device_initcall(kallsyms_init);
