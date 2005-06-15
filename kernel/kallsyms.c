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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>

#include <asm/sections.h>

#ifdef CONFIG_KALLSYMS_ALL
#define all_var 1
#else
#define all_var 0
#endif

/* These will be re-linked against their real values during the second link stage */
extern unsigned long kallsyms_addresses[] __attribute__((weak));
extern unsigned long kallsyms_num_syms __attribute__((weak,section("data")));
extern u8 kallsyms_names[] __attribute__((weak));

extern u8 kallsyms_token_table[] __attribute__((weak));
extern u16 kallsyms_token_index[] __attribute__((weak));

extern unsigned long kallsyms_markers[] __attribute__((weak));

static inline int is_kernel_inittext(unsigned long addr)
{
	if (addr >= (unsigned long)_sinittext
	    && addr <= (unsigned long)_einittext)
		return 1;
	return 0;
}

static inline int is_kernel_extratext(unsigned long addr)
{
	if (addr >= (unsigned long)_sextratext
	    && addr <= (unsigned long)_eextratext)
		return 1;
	return 0;
}

static inline int is_kernel_text(unsigned long addr)
{
	if (addr >= (unsigned long)_stext && addr <= (unsigned long)_etext)
		return 1;
	return in_gate_area_no_task(addr);
}

static inline int is_kernel(unsigned long addr)
{
	if (addr >= (unsigned long)_stext && addr <= (unsigned long)_end)
		return 1;
	return in_gate_area_no_task(addr);
}

/* expand a compressed symbol data into the resulting uncompressed string,
   given the offset to where the symbol is in the compressed stream */
static unsigned int kallsyms_expand_symbol(unsigned int off, char *result)
{
	int len, skipped_first = 0;
	u8 *tptr, *data;

	/* get the compressed symbol length from the first symbol byte */
	data = &kallsyms_names[off];
	len = *data;
	data++;

	/* update the offset to return the offset for the next symbol on
	 * the compressed stream */
	off += len + 1;

	/* for every byte on the compressed symbol data, copy the table
	   entry for that byte */
	while(len) {
		tptr = &kallsyms_token_table[ kallsyms_token_index[*data] ];
		data++;
		len--;

		while (*tptr) {
			if(skipped_first) {
				*result = *tptr;
				result++;
			} else
				skipped_first = 1;
			tptr++;
		}
	}

	*result = '\0';

	/* return to offset to the next symbol */
	return off;
}

/* get symbol type information. This is encoded as a single char at the
 * begining of the symbol name */
static char kallsyms_get_symbol_type(unsigned int off)
{
	/* get just the first code, look it up in the token table, and return the
	 * first char from this token */
	return kallsyms_token_table[ kallsyms_token_index[ kallsyms_names[off+1] ] ];
}


/* find the offset on the compressed stream given and index in the
 * kallsyms array */
static unsigned int get_symbol_offset(unsigned long pos)
{
	u8 *name;
	int i;

	/* use the closest marker we have. We have markers every 256 positions,
	 * so that should be close enough */
	name = &kallsyms_names[ kallsyms_markers[pos>>8] ];

	/* sequentially scan all the symbols up to the point we're searching for.
	 * Every symbol is stored in a [<len>][<len> bytes of data] format, so we
	 * just need to add the len to the current pointer for every symbol we
	 * wish to skip */
	for(i = 0; i < (pos&0xFF); i++)
		name = name + (*name) + 1;

	return name - kallsyms_names;
}

/* Lookup the address for this symbol. Returns 0 if not found. */
unsigned long kallsyms_lookup_name(const char *name)
{
	char namebuf[KSYM_NAME_LEN+1];
	unsigned long i;
	unsigned int off;

	for (i = 0, off = 0; i < kallsyms_num_syms; i++) {
		off = kallsyms_expand_symbol(off, namebuf);

		if (strcmp(namebuf, name) == 0)
			return kallsyms_addresses[i];
	}
	return module_kallsyms_lookup_name(name);
}
EXPORT_SYMBOL_GPL(kallsyms_lookup_name);

/*
 * Lookup an address
 * - modname is set to NULL if it's in the kernel
 * - we guarantee that the returned name is valid until we reschedule even if
 *   it resides in a module
 * - we also guarantee that modname will be valid until rescheduled
 */
const char *kallsyms_lookup(unsigned long addr,
			    unsigned long *symbolsize,
			    unsigned long *offset,
			    char **modname, char *namebuf)
{
	unsigned long i, low, high, mid;
	const char *msym;

	/* This kernel should never had been booted. */
	BUG_ON(!kallsyms_addresses);

	namebuf[KSYM_NAME_LEN] = 0;
	namebuf[0] = 0;

	if ((all_var && is_kernel(addr)) ||
	    (!all_var && (is_kernel_text(addr) || is_kernel_inittext(addr) ||
				is_kernel_extratext(addr)))) {
		unsigned long symbol_end = 0;

		/* do a binary search on the sorted kallsyms_addresses array */
		low = 0;
		high = kallsyms_num_syms;

		while (high-low > 1) {
			mid = (low + high) / 2;
			if (kallsyms_addresses[mid] <= addr) low = mid;
			else high = mid;
		}

		/* search for the first aliased symbol. Aliased symbols are
		   symbols with the same address */
		while (low && kallsyms_addresses[low - 1] == kallsyms_addresses[low])
			--low;

		/* Grab name */
		kallsyms_expand_symbol(get_symbol_offset(low), namebuf);

		/* Search for next non-aliased symbol */
		for (i = low + 1; i < kallsyms_num_syms; i++) {
			if (kallsyms_addresses[i] > kallsyms_addresses[low]) {
				symbol_end = kallsyms_addresses[i];
				break;
			}
		}

		/* if we found no next symbol, we use the end of the section */
		if (!symbol_end) {
			if (is_kernel_inittext(addr))
				symbol_end = (unsigned long)_einittext;
			else
				symbol_end = all_var ? (unsigned long)_end : (unsigned long)_etext;
		}

		*symbolsize = symbol_end - kallsyms_addresses[low];
		*modname = NULL;
		*offset = addr - kallsyms_addresses[low];
		return namebuf;
	}

	/* see if it's in a module */
	msym = module_address_lookup(addr, symbolsize, offset, modname);
	if (msym)
		return strncpy(namebuf, msym, KSYM_NAME_LEN);

	return NULL;
}

/* Replace "%s" in format with address, or returns -errno. */
void __print_symbol(const char *fmt, unsigned long address)
{
	char *modname;
	const char *name;
	unsigned long offset, size;
	char namebuf[KSYM_NAME_LEN+1];
	char buffer[sizeof("%s+%#lx/%#lx [%s]") + KSYM_NAME_LEN +
		    2*(BITS_PER_LONG*3/10) + MODULE_NAME_LEN + 1];

	name = kallsyms_lookup(address, &size, &offset, &modname, namebuf);

	if (!name)
		sprintf(buffer, "0x%lx", address);
	else {
		if (modname)
			sprintf(buffer, "%s+%#lx/%#lx [%s]", name, offset,
				size, modname);
		else
			sprintf(buffer, "%s+%#lx/%#lx", name, offset, size);
	}
	printk(fmt, buffer);
}

/* To avoid using get_symbol_offset for every symbol, we carry prefix along. */
struct kallsym_iter
{
	loff_t pos;
	struct module *owner;
	unsigned long value;
	unsigned int nameoff; /* If iterating in core kernel symbols */
	char type;
	char name[KSYM_NAME_LEN+1];
};

/* Only label it "global" if it is exported. */
static void upcase_if_global(struct kallsym_iter *iter)
{
	if (is_exported(iter->name, iter->owner))
		iter->type += 'A' - 'a';
}

static int get_ksymbol_mod(struct kallsym_iter *iter)
{
	iter->owner = module_get_kallsym(iter->pos - kallsyms_num_syms,
					 &iter->value,
					 &iter->type, iter->name);
	if (iter->owner == NULL)
		return 0;

	upcase_if_global(iter);
	return 1;
}

/* Returns space to next name. */
static unsigned long get_ksymbol_core(struct kallsym_iter *iter)
{
	unsigned off = iter->nameoff;

	iter->owner = NULL;
	iter->value = kallsyms_addresses[iter->pos];

	iter->type = kallsyms_get_symbol_type(off);

	off = kallsyms_expand_symbol(off, iter->name);

	return off - iter->nameoff;
}

static void reset_iter(struct kallsym_iter *iter, loff_t new_pos)
{
	iter->name[0] = '\0';
	iter->nameoff = get_symbol_offset(new_pos);
	iter->pos = new_pos;
}

/* Returns false if pos at or past end of file. */
static int update_iter(struct kallsym_iter *iter, loff_t pos)
{
	/* Module symbols can be accessed randomly. */
	if (pos >= kallsyms_num_syms) {
		iter->pos = pos;
		return get_ksymbol_mod(iter);
	}
	
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
	struct kallsym_iter *iter = m->private;

	/* Some debugging symbols have no name.  Ignore them. */ 
	if (!iter->name[0])
		return 0;

	if (iter->owner)
		seq_printf(m, "%0*lx %c %s\t[%s]\n",
			   (int)(2*sizeof(void*)),
			   iter->value, iter->type, iter->name,
			   module_name(iter->owner));
	else
		seq_printf(m, "%0*lx %c %s\n",
			   (int)(2*sizeof(void*)),
			   iter->value, iter->type, iter->name);
	return 0;
}

static struct seq_operations kallsyms_op = {
	.start = s_start,
	.next = s_next,
	.stop = s_stop,
	.show = s_show
};

static int kallsyms_open(struct inode *inode, struct file *file)
{
	/* We keep iterator in m->private, since normal case is to
	 * s_start from where we left off, so we avoid doing
	 * using get_symbol_offset for every symbol */
	struct kallsym_iter *iter;
	int ret;

	iter = kmalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return -ENOMEM;
	reset_iter(iter, 0);

	ret = seq_open(file, &kallsyms_op);
	if (ret == 0)
		((struct seq_file *)file->private_data)->private = iter;
	else
		kfree(iter);
	return ret;
}

static int kallsyms_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	kfree(m->private);
	return seq_release(inode, file);
}

static struct file_operations kallsyms_operations = {
	.open = kallsyms_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = kallsyms_release,
};

static int __init kallsyms_init(void)
{
	struct proc_dir_entry *entry;

	entry = create_proc_entry("kallsyms", 0444, NULL);
	if (entry)
		entry->proc_fops = &kallsyms_operations;
	return 0;
}
__initcall(kallsyms_init);

EXPORT_SYMBOL(__print_symbol);
