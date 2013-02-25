/*
 * Common code for probe-based Dynamic events.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This code was copied from kernel/trace/trace_kprobe.c written by
 * Masami Hiramatsu <masami.hiramatsu.pt@hitachi.com>
 *
 * Updates to make this generic:
 * Copyright (C) IBM Corporation, 2010-2011
 * Author:     Srikar Dronamraju
 */

#include "trace_probe.h"

const char *reserved_field_names[] = {
	"common_type",
	"common_flags",
	"common_preempt_count",
	"common_pid",
	"common_tgid",
	FIELD_STRING_IP,
	FIELD_STRING_RETIP,
	FIELD_STRING_FUNC,
};

/* Printing function type */
#define PRINT_TYPE_FUNC_NAME(type)	print_type_##type
#define PRINT_TYPE_FMT_NAME(type)	print_type_format_##type

/* Printing  in basic type function template */
#define DEFINE_BASIC_PRINT_TYPE_FUNC(type, fmt, cast)			\
static __kprobes int PRINT_TYPE_FUNC_NAME(type)(struct trace_seq *s,	\
						const char *name,	\
						void *data, void *ent)\
{									\
	return trace_seq_printf(s, " %s=" fmt, name, (cast)*(type *)data);\
}									\
static const char PRINT_TYPE_FMT_NAME(type)[] = fmt;

DEFINE_BASIC_PRINT_TYPE_FUNC(u8, "%x", unsigned int)
DEFINE_BASIC_PRINT_TYPE_FUNC(u16, "%x", unsigned int)
DEFINE_BASIC_PRINT_TYPE_FUNC(u32, "%lx", unsigned long)
DEFINE_BASIC_PRINT_TYPE_FUNC(u64, "%llx", unsigned long long)
DEFINE_BASIC_PRINT_TYPE_FUNC(s8, "%d", int)
DEFINE_BASIC_PRINT_TYPE_FUNC(s16, "%d", int)
DEFINE_BASIC_PRINT_TYPE_FUNC(s32, "%ld", long)
DEFINE_BASIC_PRINT_TYPE_FUNC(s64, "%lld", long long)

static inline void *get_rloc_data(u32 *dl)
{
	return (u8 *)dl + get_rloc_offs(*dl);
}

/* For data_loc conversion */
static inline void *get_loc_data(u32 *dl, void *ent)
{
	return (u8 *)ent + get_rloc_offs(*dl);
}

/* For defining macros, define string/string_size types */
typedef u32 string;
typedef u32 string_size;

/* Print type function for string type */
static __kprobes int PRINT_TYPE_FUNC_NAME(string)(struct trace_seq *s,
						  const char *name,
						  void *data, void *ent)
{
	int len = *(u32 *)data >> 16;

	if (!len)
		return trace_seq_printf(s, " %s=(fault)", name);
	else
		return trace_seq_printf(s, " %s=\"%s\"", name,
					(const char *)get_loc_data(data, ent));
}

static const char PRINT_TYPE_FMT_NAME(string)[] = "\\\"%s\\\"";

#define FETCH_FUNC_NAME(method, type)	fetch_##method##_##type
/*
 * Define macro for basic types - we don't need to define s* types, because
 * we have to care only about bitwidth at recording time.
 */
#define DEFINE_BASIC_FETCH_FUNCS(method) \
DEFINE_FETCH_##method(u8)		\
DEFINE_FETCH_##method(u16)		\
DEFINE_FETCH_##method(u32)		\
DEFINE_FETCH_##method(u64)

#define CHECK_FETCH_FUNCS(method, fn)			\
	(((FETCH_FUNC_NAME(method, u8) == fn) ||	\
	  (FETCH_FUNC_NAME(method, u16) == fn) ||	\
	  (FETCH_FUNC_NAME(method, u32) == fn) ||	\
	  (FETCH_FUNC_NAME(method, u64) == fn) ||	\
	  (FETCH_FUNC_NAME(method, string) == fn) ||	\
	  (FETCH_FUNC_NAME(method, string_size) == fn)) \
	 && (fn != NULL))

/* Data fetch function templates */
#define DEFINE_FETCH_reg(type)						\
static __kprobes void FETCH_FUNC_NAME(reg, type)(struct pt_regs *regs,	\
					void *offset, void *dest)	\
{									\
	*(type *)dest = (type)regs_get_register(regs,			\
				(unsigned int)((unsigned long)offset));	\
}
DEFINE_BASIC_FETCH_FUNCS(reg)
/* No string on the register */
#define fetch_reg_string	NULL
#define fetch_reg_string_size	NULL

#define DEFINE_FETCH_stack(type)					\
static __kprobes void FETCH_FUNC_NAME(stack, type)(struct pt_regs *regs,\
					  void *offset, void *dest)	\
{									\
	*(type *)dest = (type)regs_get_kernel_stack_nth(regs,		\
				(unsigned int)((unsigned long)offset));	\
}
DEFINE_BASIC_FETCH_FUNCS(stack)
/* No string on the stack entry */
#define fetch_stack_string	NULL
#define fetch_stack_string_size	NULL

#define DEFINE_FETCH_retval(type)					\
static __kprobes void FETCH_FUNC_NAME(retval, type)(struct pt_regs *regs,\
					  void *dummy, void *dest)	\
{									\
	*(type *)dest = (type)regs_return_value(regs);			\
}
DEFINE_BASIC_FETCH_FUNCS(retval)
/* No string on the retval */
#define fetch_retval_string		NULL
#define fetch_retval_string_size	NULL

#define DEFINE_FETCH_memory(type)					\
static __kprobes void FETCH_FUNC_NAME(memory, type)(struct pt_regs *regs,\
					  void *addr, void *dest)	\
{									\
	type retval;							\
	if (probe_kernel_address(addr, retval))				\
		*(type *)dest = 0;					\
	else								\
		*(type *)dest = retval;					\
}
DEFINE_BASIC_FETCH_FUNCS(memory)
/*
 * Fetch a null-terminated string. Caller MUST set *(u32 *)dest with max
 * length and relative data location.
 */
static __kprobes void FETCH_FUNC_NAME(memory, string)(struct pt_regs *regs,
						      void *addr, void *dest)
{
	long ret;
	int maxlen = get_rloc_len(*(u32 *)dest);
	u8 *dst = get_rloc_data(dest);
	u8 *src = addr;
	mm_segment_t old_fs = get_fs();

	if (!maxlen)
		return;

	/*
	 * Try to get string again, since the string can be changed while
	 * probing.
	 */
	set_fs(KERNEL_DS);
	pagefault_disable();

	do
		ret = __copy_from_user_inatomic(dst++, src++, 1);
	while (dst[-1] && ret == 0 && src - (u8 *)addr < maxlen);

	dst[-1] = '\0';
	pagefault_enable();
	set_fs(old_fs);

	if (ret < 0) {	/* Failed to fetch string */
		((u8 *)get_rloc_data(dest))[0] = '\0';
		*(u32 *)dest = make_data_rloc(0, get_rloc_offs(*(u32 *)dest));
	} else {
		*(u32 *)dest = make_data_rloc(src - (u8 *)addr,
					      get_rloc_offs(*(u32 *)dest));
	}
}

/* Return the length of string -- including null terminal byte */
static __kprobes void FETCH_FUNC_NAME(memory, string_size)(struct pt_regs *regs,
							void *addr, void *dest)
{
	mm_segment_t old_fs;
	int ret, len = 0;
	u8 c;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pagefault_disable();

	do {
		ret = __copy_from_user_inatomic(&c, (u8 *)addr + len, 1);
		len++;
	} while (c && ret == 0 && len < MAX_STRING_SIZE);

	pagefault_enable();
	set_fs(old_fs);

	if (ret < 0)	/* Failed to check the length */
		*(u32 *)dest = 0;
	else
		*(u32 *)dest = len;
}

/* Memory fetching by symbol */
struct symbol_cache {
	char		*symbol;
	long		offset;
	unsigned long	addr;
};

static unsigned long update_symbol_cache(struct symbol_cache *sc)
{
	sc->addr = (unsigned long)kallsyms_lookup_name(sc->symbol);

	if (sc->addr)
		sc->addr += sc->offset;

	return sc->addr;
}

static void free_symbol_cache(struct symbol_cache *sc)
{
	kfree(sc->symbol);
	kfree(sc);
}

static struct symbol_cache *alloc_symbol_cache(const char *sym, long offset)
{
	struct symbol_cache *sc;

	if (!sym || strlen(sym) == 0)
		return NULL;

	sc = kzalloc(sizeof(struct symbol_cache), GFP_KERNEL);
	if (!sc)
		return NULL;

	sc->symbol = kstrdup(sym, GFP_KERNEL);
	if (!sc->symbol) {
		kfree(sc);
		return NULL;
	}
	sc->offset = offset;
	update_symbol_cache(sc);

	return sc;
}

#define DEFINE_FETCH_symbol(type)					\
static __kprobes void FETCH_FUNC_NAME(symbol, type)(struct pt_regs *regs,\
					  void *data, void *dest)	\
{									\
	struct symbol_cache *sc = data;					\
	if (sc->addr)							\
		fetch_memory_##type(regs, (void *)sc->addr, dest);	\
	else								\
		*(type *)dest = 0;					\
}
DEFINE_BASIC_FETCH_FUNCS(symbol)
DEFINE_FETCH_symbol(string)
DEFINE_FETCH_symbol(string_size)

/* Dereference memory access function */
struct deref_fetch_param {
	struct fetch_param	orig;
	long			offset;
};

#define DEFINE_FETCH_deref(type)					\
static __kprobes void FETCH_FUNC_NAME(deref, type)(struct pt_regs *regs,\
					    void *data, void *dest)	\
{									\
	struct deref_fetch_param *dprm = data;				\
	unsigned long addr;						\
	call_fetch(&dprm->orig, regs, &addr);				\
	if (addr) {							\
		addr += dprm->offset;					\
		fetch_memory_##type(regs, (void *)addr, dest);		\
	} else								\
		*(type *)dest = 0;					\
}
DEFINE_BASIC_FETCH_FUNCS(deref)
DEFINE_FETCH_deref(string)
DEFINE_FETCH_deref(string_size)

static __kprobes void update_deref_fetch_param(struct deref_fetch_param *data)
{
	if (CHECK_FETCH_FUNCS(deref, data->orig.fn))
		update_deref_fetch_param(data->orig.data);
	else if (CHECK_FETCH_FUNCS(symbol, data->orig.fn))
		update_symbol_cache(data->orig.data);
}

static __kprobes void free_deref_fetch_param(struct deref_fetch_param *data)
{
	if (CHECK_FETCH_FUNCS(deref, data->orig.fn))
		free_deref_fetch_param(data->orig.data);
	else if (CHECK_FETCH_FUNCS(symbol, data->orig.fn))
		free_symbol_cache(data->orig.data);
	kfree(data);
}

/* Bitfield fetch function */
struct bitfield_fetch_param {
	struct fetch_param	orig;
	unsigned char		hi_shift;
	unsigned char		low_shift;
};

#define DEFINE_FETCH_bitfield(type)					\
static __kprobes void FETCH_FUNC_NAME(bitfield, type)(struct pt_regs *regs,\
					    void *data, void *dest)	\
{									\
	struct bitfield_fetch_param *bprm = data;			\
	type buf = 0;							\
	call_fetch(&bprm->orig, regs, &buf);				\
	if (buf) {							\
		buf <<= bprm->hi_shift;					\
		buf >>= bprm->low_shift;				\
	}								\
	*(type *)dest = buf;						\
}

DEFINE_BASIC_FETCH_FUNCS(bitfield)
#define fetch_bitfield_string		NULL
#define fetch_bitfield_string_size	NULL

static __kprobes void
update_bitfield_fetch_param(struct bitfield_fetch_param *data)
{
	/*
	 * Don't check the bitfield itself, because this must be the
	 * last fetch function.
	 */
	if (CHECK_FETCH_FUNCS(deref, data->orig.fn))
		update_deref_fetch_param(data->orig.data);
	else if (CHECK_FETCH_FUNCS(symbol, data->orig.fn))
		update_symbol_cache(data->orig.data);
}

static __kprobes void
free_bitfield_fetch_param(struct bitfield_fetch_param *data)
{
	/*
	 * Don't check the bitfield itself, because this must be the
	 * last fetch function.
	 */
	if (CHECK_FETCH_FUNCS(deref, data->orig.fn))
		free_deref_fetch_param(data->orig.data);
	else if (CHECK_FETCH_FUNCS(symbol, data->orig.fn))
		free_symbol_cache(data->orig.data);

	kfree(data);
}

/* Default (unsigned long) fetch type */
#define __DEFAULT_FETCH_TYPE(t) u##t
#define _DEFAULT_FETCH_TYPE(t) __DEFAULT_FETCH_TYPE(t)
#define DEFAULT_FETCH_TYPE _DEFAULT_FETCH_TYPE(BITS_PER_LONG)
#define DEFAULT_FETCH_TYPE_STR __stringify(DEFAULT_FETCH_TYPE)

#define ASSIGN_FETCH_FUNC(method, type)	\
	[FETCH_MTD_##method] = FETCH_FUNC_NAME(method, type)

#define __ASSIGN_FETCH_TYPE(_name, ptype, ftype, _size, sign, _fmttype)	\
	{.name = _name,				\
	 .size = _size,					\
	 .is_signed = sign,				\
	 .print = PRINT_TYPE_FUNC_NAME(ptype),		\
	 .fmt = PRINT_TYPE_FMT_NAME(ptype),		\
	 .fmttype = _fmttype,				\
	 .fetch = {					\
ASSIGN_FETCH_FUNC(reg, ftype),				\
ASSIGN_FETCH_FUNC(stack, ftype),			\
ASSIGN_FETCH_FUNC(retval, ftype),			\
ASSIGN_FETCH_FUNC(memory, ftype),			\
ASSIGN_FETCH_FUNC(symbol, ftype),			\
ASSIGN_FETCH_FUNC(deref, ftype),			\
ASSIGN_FETCH_FUNC(bitfield, ftype),			\
	  }						\
	}

#define ASSIGN_FETCH_TYPE(ptype, ftype, sign)			\
	__ASSIGN_FETCH_TYPE(#ptype, ptype, ftype, sizeof(ftype), sign, #ptype)

#define FETCH_TYPE_STRING	0
#define FETCH_TYPE_STRSIZE	1

/* Fetch type information table */
static const struct fetch_type fetch_type_table[] = {
	/* Special types */
	[FETCH_TYPE_STRING] = __ASSIGN_FETCH_TYPE("string", string, string,
					sizeof(u32), 1, "__data_loc char[]"),
	[FETCH_TYPE_STRSIZE] = __ASSIGN_FETCH_TYPE("string_size", u32,
					string_size, sizeof(u32), 0, "u32"),
	/* Basic types */
	ASSIGN_FETCH_TYPE(u8,  u8,  0),
	ASSIGN_FETCH_TYPE(u16, u16, 0),
	ASSIGN_FETCH_TYPE(u32, u32, 0),
	ASSIGN_FETCH_TYPE(u64, u64, 0),
	ASSIGN_FETCH_TYPE(s8,  u8,  1),
	ASSIGN_FETCH_TYPE(s16, u16, 1),
	ASSIGN_FETCH_TYPE(s32, u32, 1),
	ASSIGN_FETCH_TYPE(s64, u64, 1),
};

static const struct fetch_type *find_fetch_type(const char *type)
{
	int i;

	if (!type)
		type = DEFAULT_FETCH_TYPE_STR;

	/* Special case: bitfield */
	if (*type == 'b') {
		unsigned long bs;

		type = strchr(type, '/');
		if (!type)
			goto fail;

		type++;
		if (kstrtoul(type, 0, &bs))
			goto fail;

		switch (bs) {
		case 8:
			return find_fetch_type("u8");
		case 16:
			return find_fetch_type("u16");
		case 32:
			return find_fetch_type("u32");
		case 64:
			return find_fetch_type("u64");
		default:
			goto fail;
		}
	}

	for (i = 0; i < ARRAY_SIZE(fetch_type_table); i++)
		if (strcmp(type, fetch_type_table[i].name) == 0)
			return &fetch_type_table[i];

fail:
	return NULL;
}

/* Special function : only accept unsigned long */
static __kprobes void fetch_stack_address(struct pt_regs *regs,
					void *dummy, void *dest)
{
	*(unsigned long *)dest = kernel_stack_pointer(regs);
}

static fetch_func_t get_fetch_size_function(const struct fetch_type *type,
					fetch_func_t orig_fn)
{
	int i;

	if (type != &fetch_type_table[FETCH_TYPE_STRING])
		return NULL;	/* Only string type needs size function */

	for (i = 0; i < FETCH_MTD_END; i++)
		if (type->fetch[i] == orig_fn)
			return fetch_type_table[FETCH_TYPE_STRSIZE].fetch[i];

	WARN_ON(1);	/* This should not happen */

	return NULL;
}

/* Split symbol and offset. */
int traceprobe_split_symbol_offset(char *symbol, unsigned long *offset)
{
	char *tmp;
	int ret;

	if (!offset)
		return -EINVAL;

	tmp = strchr(symbol, '+');
	if (tmp) {
		/* skip sign because kstrtoul doesn't accept '+' */
		ret = kstrtoul(tmp + 1, 0, offset);
		if (ret)
			return ret;

		*tmp = '\0';
	} else
		*offset = 0;

	return 0;
}

#define PARAM_MAX_STACK (THREAD_SIZE / sizeof(unsigned long))

static int parse_probe_vars(char *arg, const struct fetch_type *t,
			    struct fetch_param *f, bool is_return)
{
	int ret = 0;
	unsigned long param;

	if (strcmp(arg, "retval") == 0) {
		if (is_return)
			f->fn = t->fetch[FETCH_MTD_retval];
		else
			ret = -EINVAL;
	} else if (strncmp(arg, "stack", 5) == 0) {
		if (arg[5] == '\0') {
			if (strcmp(t->name, DEFAULT_FETCH_TYPE_STR) == 0)
				f->fn = fetch_stack_address;
			else
				ret = -EINVAL;
		} else if (isdigit(arg[5])) {
			ret = kstrtoul(arg + 5, 10, &param);
			if (ret || param > PARAM_MAX_STACK)
				ret = -EINVAL;
			else {
				f->fn = t->fetch[FETCH_MTD_stack];
				f->data = (void *)param;
			}
		} else
			ret = -EINVAL;
	} else
		ret = -EINVAL;

	return ret;
}

/* Recursive argument parser */
static int parse_probe_arg(char *arg, const struct fetch_type *t,
		     struct fetch_param *f, bool is_return, bool is_kprobe)
{
	unsigned long param;
	long offset;
	char *tmp;
	int ret;

	ret = 0;

	/* Until uprobe_events supports only reg arguments */
	if (!is_kprobe && arg[0] != '%')
		return -EINVAL;

	switch (arg[0]) {
	case '$':
		ret = parse_probe_vars(arg + 1, t, f, is_return);
		break;

	case '%':	/* named register */
		ret = regs_query_register_offset(arg + 1);
		if (ret >= 0) {
			f->fn = t->fetch[FETCH_MTD_reg];
			f->data = (void *)(unsigned long)ret;
			ret = 0;
		}
		break;

	case '@':	/* memory or symbol */
		if (isdigit(arg[1])) {
			ret = kstrtoul(arg + 1, 0, &param);
			if (ret)
				break;

			f->fn = t->fetch[FETCH_MTD_memory];
			f->data = (void *)param;
		} else {
			ret = traceprobe_split_symbol_offset(arg + 1, &offset);
			if (ret)
				break;

			f->data = alloc_symbol_cache(arg + 1, offset);
			if (f->data)
				f->fn = t->fetch[FETCH_MTD_symbol];
		}
		break;

	case '+':	/* deref memory */
		arg++;	/* Skip '+', because kstrtol() rejects it. */
	case '-':
		tmp = strchr(arg, '(');
		if (!tmp)
			break;

		*tmp = '\0';
		ret = kstrtol(arg, 0, &offset);

		if (ret)
			break;

		arg = tmp + 1;
		tmp = strrchr(arg, ')');

		if (tmp) {
			struct deref_fetch_param	*dprm;
			const struct fetch_type		*t2;

			t2 = find_fetch_type(NULL);
			*tmp = '\0';
			dprm = kzalloc(sizeof(struct deref_fetch_param), GFP_KERNEL);

			if (!dprm)
				return -ENOMEM;

			dprm->offset = offset;
			ret = parse_probe_arg(arg, t2, &dprm->orig, is_return,
							is_kprobe);
			if (ret)
				kfree(dprm);
			else {
				f->fn = t->fetch[FETCH_MTD_deref];
				f->data = (void *)dprm;
			}
		}
		break;
	}
	if (!ret && !f->fn) {	/* Parsed, but do not find fetch method */
		pr_info("%s type has no corresponding fetch method.\n", t->name);
		ret = -EINVAL;
	}

	return ret;
}

#define BYTES_TO_BITS(nb)	((BITS_PER_LONG * (nb)) / sizeof(long))

/* Bitfield type needs to be parsed into a fetch function */
static int __parse_bitfield_probe_arg(const char *bf,
				      const struct fetch_type *t,
				      struct fetch_param *f)
{
	struct bitfield_fetch_param *bprm;
	unsigned long bw, bo;
	char *tail;

	if (*bf != 'b')
		return 0;

	bprm = kzalloc(sizeof(*bprm), GFP_KERNEL);
	if (!bprm)
		return -ENOMEM;

	bprm->orig = *f;
	f->fn = t->fetch[FETCH_MTD_bitfield];
	f->data = (void *)bprm;
	bw = simple_strtoul(bf + 1, &tail, 0);	/* Use simple one */

	if (bw == 0 || *tail != '@')
		return -EINVAL;

	bf = tail + 1;
	bo = simple_strtoul(bf, &tail, 0);

	if (tail == bf || *tail != '/')
		return -EINVAL;

	bprm->hi_shift = BYTES_TO_BITS(t->size) - (bw + bo);
	bprm->low_shift = bprm->hi_shift + bo;

	return (BYTES_TO_BITS(t->size) < (bw + bo)) ? -EINVAL : 0;
}

/* String length checking wrapper */
int traceprobe_parse_probe_arg(char *arg, ssize_t *size,
		struct probe_arg *parg, bool is_return, bool is_kprobe)
{
	const char *t;
	int ret;

	if (strlen(arg) > MAX_ARGSTR_LEN) {
		pr_info("Argument is too long.: %s\n",  arg);
		return -ENOSPC;
	}
	parg->comm = kstrdup(arg, GFP_KERNEL);
	if (!parg->comm) {
		pr_info("Failed to allocate memory for command '%s'.\n", arg);
		return -ENOMEM;
	}
	t = strchr(parg->comm, ':');
	if (t) {
		arg[t - parg->comm] = '\0';
		t++;
	}
	parg->type = find_fetch_type(t);
	if (!parg->type) {
		pr_info("Unsupported type: %s\n", t);
		return -EINVAL;
	}
	parg->offset = *size;
	*size += parg->type->size;
	ret = parse_probe_arg(arg, parg->type, &parg->fetch, is_return, is_kprobe);

	if (ret >= 0 && t != NULL)
		ret = __parse_bitfield_probe_arg(t, parg->type, &parg->fetch);

	if (ret >= 0) {
		parg->fetch_size.fn = get_fetch_size_function(parg->type,
							      parg->fetch.fn);
		parg->fetch_size.data = parg->fetch.data;
	}

	return ret;
}

/* Return 1 if name is reserved or already used by another argument */
int traceprobe_conflict_field_name(const char *name,
			       struct probe_arg *args, int narg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(reserved_field_names); i++)
		if (strcmp(reserved_field_names[i], name) == 0)
			return 1;

	for (i = 0; i < narg; i++)
		if (strcmp(args[i].name, name) == 0)
			return 1;

	return 0;
}

void traceprobe_update_arg(struct probe_arg *arg)
{
	if (CHECK_FETCH_FUNCS(bitfield, arg->fetch.fn))
		update_bitfield_fetch_param(arg->fetch.data);
	else if (CHECK_FETCH_FUNCS(deref, arg->fetch.fn))
		update_deref_fetch_param(arg->fetch.data);
	else if (CHECK_FETCH_FUNCS(symbol, arg->fetch.fn))
		update_symbol_cache(arg->fetch.data);
}

void traceprobe_free_probe_arg(struct probe_arg *arg)
{
	if (CHECK_FETCH_FUNCS(bitfield, arg->fetch.fn))
		free_bitfield_fetch_param(arg->fetch.data);
	else if (CHECK_FETCH_FUNCS(deref, arg->fetch.fn))
		free_deref_fetch_param(arg->fetch.data);
	else if (CHECK_FETCH_FUNCS(symbol, arg->fetch.fn))
		free_symbol_cache(arg->fetch.data);

	kfree(arg->name);
	kfree(arg->comm);
}

int traceprobe_command(const char *buf, int (*createfn)(int, char **))
{
	char **argv;
	int argc, ret;

	argc = 0;
	ret = 0;
	argv = argv_split(GFP_KERNEL, buf, &argc);
	if (!argv)
		return -ENOMEM;

	if (argc)
		ret = createfn(argc, argv);

	argv_free(argv);

	return ret;
}

#define WRITE_BUFSIZE  4096

ssize_t traceprobe_probes_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *ppos,
				int (*createfn)(int, char **))
{
	char *kbuf, *tmp;
	int ret = 0;
	size_t done = 0;
	size_t size;

	kbuf = kmalloc(WRITE_BUFSIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	while (done < count) {
		size = count - done;

		if (size >= WRITE_BUFSIZE)
			size = WRITE_BUFSIZE - 1;

		if (copy_from_user(kbuf, buffer + done, size)) {
			ret = -EFAULT;
			goto out;
		}
		kbuf[size] = '\0';
		tmp = strchr(kbuf, '\n');

		if (tmp) {
			*tmp = '\0';
			size = tmp - kbuf + 1;
		} else if (done + size < count) {
			pr_warning("Line length is too long: "
				   "Should be less than %d.", WRITE_BUFSIZE);
			ret = -EINVAL;
			goto out;
		}
		done += size;
		/* Remove comments */
		tmp = strchr(kbuf, '#');

		if (tmp)
			*tmp = '\0';

		ret = traceprobe_command(kbuf, createfn);
		if (ret)
			goto out;
	}
	ret = done;

out:
	kfree(kbuf);

	return ret;
}
