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
#include <linux/compiler.h>

/*
 * These will be re-linked against their real values
 * during the second link stage.
 */
extern const unsigned long kallsyms_addresses[] __weak;
extern const int kallsyms_offsets[] __weak;
extern const u8 kallsyms_names[] __weak;

/*
 * Tell the compiler that the count isn't in the small data section if the arch
 * has one (eg: FRV).
 */
extern const unsigned long kallsyms_num_syms
__attribute__((weak, section(".rodata")));

extern const unsigned long kallsyms_relative_base
__attribute__((weak, section(".rodata")));

extern const u8 kallsyms_token_table[] __weak;
extern const u16 kallsyms_token_index[] __weak;

extern const unsigned long kallsyms_markers[] __weak;

/*
 * Expand a compressed symbol data into the resulting uncompressed string,
 * if uncompressed string is too long (>= maxlen), it will be truncated,
 * given the offset to where the symbol is in the compressed stream.
 */
static unsigned int kallsyms_expand_symbol(unsigned int off,
					   char *result, size_t maxlen)
{
	int len, skipped_first = 0;
	const u8 *tptr, *data;

	/* Get the compressed symbol length from the first symbol byte. */
	data = &kallsyms_names[off];
	len = *data;
	data++;

	/*
	 * Update the offset to return the offset for the next symbol on
	 * the compressed stream.
	 */
	off += len + 1;

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
	int i;

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
	for (i = 0; i < (pos & 0xFF); i++)
		name = name + (*name) + 1;

	return name - kallsyms_names;
}

static unsigned long kallsyms_sym_address(int idx)
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

/* Lookup the address for this symbol. Returns 0 if not found. */
unsigned long kallsyms_lookup_name(const char *name)
{
	char namebuf[KSYM_NAME_LEN];
	unsigned long i;
	unsigned int off;

	for (i = 0, off = 0; i < kallsyms_num_syms; i++) {
		off = kallsyms_expand_symbol(off, namebuf, ARRAY_SIZE(namebuf));

		if (strcmp(namebuf, name) == 0)
			return kallsyms_sym_address(i);
	}
	return module_kallsyms_lookup_name(name);
}
EXPORT_SYMBOL_GPL(kallsyms_lookup_name);

int kallsyms_on_each_symbol(int (*fn)(void *, const char *, struct module *,
				      unsigned long),
			    void *data)
{
	char namebuf[KSYM_NAME_LEN];
	unsigned long i;
	unsigned int off;
	int ret;

	for (i = 0, off = 0; i < kallsyms_num_syms; i++) {
		off = kallsyms_expand_symbol(off, namebuf, ARRAY_SIZE(namebuf));
		ret = fn(data, namebuf, NULL, kallsyms_sym_address(i));
		if (ret != 0)
			return ret;
	}
	return module_kallsyms_on_each_symbol(fn, data);
}
EXPORT_SYMBOL_GPL(kallsyms_on_each_symbol);

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
	return !!module_address_lookup(addr, symbolsize, offset, NULL, namebuf) ||
	       !!__bpf_address_lookup(addr, symbolsize, offset, namebuf);
}

#ifdef CONFIG_CFI_CLANG
/*
 * LLVM appends .cfi to function names when CONFIG_CFI_CLANG is enabled,
 * which causes confusion and potentially breaks user space tools, so we
 * will strip the postfix from expanded symbol names.
 */
static inline void cleanup_symbol_name(char *s)
{
	char *res;

	res = strrchr(s, '.');
	if (res && !strcmp(res, ".cfi"))
		*res = '\0';
}
#else
static inline void cleanup_symbol_name(char *s) {}
#endif

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

		ret = namebuf;
		goto found;
	}

	/* See if it's in a module or a BPF JITed image. */
	ret = module_address_lookup(addr, symbolsize, offset,
				    modname, namebuf);
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

int lookup_symbol_attrs(unsigned long addr, unsigned long *size,
			unsigned long *offset, char *modname, char *name)
{
	int res;

	name[0] = '\0';
	name[KSYM_NAME_LEN - 1] = '\0';

	if (is_ksym_addr(addr)) {
		unsigned long pos;

		pos = get_symbol_pos(addr, size, offset);
		/* Grab name */
		kallsyms_expand_symbol(get_symbol_offset(pos),
				       name, KSYM_NAME_LEN);
		modname[0] = '\0';
		goto found;
	}
	/* See if it's in a module. */
	res = lookup_module_symbol_attrs(addr, size, offset, modname, name);
	if (res)
		return res;

found:
	cleanup_symbol_name(name);
	return 0;
}

/* Look up a kernel symbol and return it in a text buffer. */
static int __sprint_symbol(char *buffer, unsigned long address,
			   int symbol_offset, int add_offset)
{
	char *modname;
	const char *name;
	unsigned long offset, size;
	int len;

	address += symbol_offset;
	name = kallsyms_lookup(address, &size, &offset, &modname, buffer);
	if (!name)
		return sprintf(buffer, "0x%lx", address - symbol_offset);

	if (name != buffer)
		strcpy(buffer, name);
	len = strlen(buffer);
	offset -= symbol_offset;

	if (add_offset)
		len += sprintf(buffer + len, "+%#lx/%#lx", offset, size);

	if (modname)
		len += sprintf(buffer + len, " [%s]", modname);

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
	return __sprint_symbol(buffer, address, 0, 1);
}
EXPORT_SYMBOL_GPL(sprint_symbol);

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
	return __sprint_symbol(buffer, address, 0, 0);
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
	return __sprint_symbol(buffer, address, -1, 1);
}

/* To avoid using get_symbol_offset for every symbol, we carry prefix along. */
struct kallsym_iter {
	loff_t pos;
	loff_t pos_arch_end;
	loff_t pos_mod_end;
	loff_t pos_ftrace_mod_end;
	unsigned long value;
	unsigned int nameoff; /* If iterating in core kernel symbols. */
	char type;
	char name[KSYM_NAME_LEN];
	char module_name[MODULE_NAME_LEN];
	int exported;
	int show_value;
};

int __weak arch_get_kallsym(unsigned int symnum, unsigned long *value,
			    char *type, char *name)
{
	return -EINVAL;
}

static int get_ksymbol_arch(struct kallsym_iter *iter)
{
	int ret = arch_get_kallsym(iter->pos - kallsyms_num_syms,
				   &iter->value, &iter->type,
				   iter->name);

	if (ret < 0) {
		iter->pos_arch_end = iter->pos;
		return 0;
	}

	return 1;
}

static int get_ksymbol_mod(struct kallsym_iter *iter)
{
	int ret = module_get_kallsym(iter->pos - iter->pos_arch_end,
				     &iter->value, &iter->type,
				     iter->name, iter->module_name,
				     &iter->exported);
	if (ret < 0) {
		iter->pos_mod_end = iter->pos;
		return 0;
	}

	return 1;
}

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
	iter->module_name[0] = '\0';
	iter->exported = 0;
	return bpf_get_kallsym(iter->pos - iter->pos_ftrace_mod_end,
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
		iter->pos_arch_end = 0;
		iter->pos_mod_end = 0;
		iter->pos_ftrace_mod_end = 0;
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

	if ((!iter->pos_arch_end || iter->pos_arch_end > pos) &&
	    get_ksymbol_arch(iter))
		return 1;

	if ((!iter->pos_mod_end || iter->pos_mod_end > pos) &&
	    get_ksymbol_mod(iter))
		return 1;

	if ((!iter->pos_ftrace_mod_end || iter->pos_ftrace_mod_end > pos) &&
	    get_ksymbol_ftrace_mod(iter))
		return 1;

	return get_ksymbol_bpf(iter);
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

static inline int kallsyms_for_perf(void)
{
#ifdef CONFIG_PERF_EVENTS
	extern int sysctl_perf_event_paranoid;
	if (sysctl_perf_event_paranoid <= 1)
		return 1;
#endif
	return 0;
}

/*
 * We show kallsyms information even to normal users if we've enabled
 * kernel profiling and are explicitly not paranoid (so kptr_restrict
 * is clear, and sysctl_perf_event_paranoid isn't set).
 *
 * Otherwise, require CAP_SYSLOG (assuming kptr_restrict isn't set to
 * block even that).
 */
int kallsyms_show_value(void)
{
	switch (kptr_restrict) {
	case 0:
		if (kallsyms_for_perf())
			return 1;
	/* fallthrough */
	case 1:
		if (has_capability_noaudit(current, CAP_SYSLOG))
			return 1;
	/* fallthrough */
	default:
		return 0;
	}
}

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

	iter->show_value = kallsyms_show_value();
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

static const struct file_operations kallsyms_operations = {
	.open = kallsyms_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

static int __init kallsyms_init(void)
{
	proc_create("kallsyms", 0444, NULL, &kallsyms_operations);
	return 0;
}
device_initcall(kallsyms_init);
