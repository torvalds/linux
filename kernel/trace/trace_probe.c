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
#define pr_fmt(fmt)	"trace_probe: " fmt

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

/* Printing  in basic type function template */
#define DEFINE_BASIC_PRINT_TYPE_FUNC(tname, type, fmt)			\
int PRINT_TYPE_FUNC_NAME(tname)(struct trace_seq *s, const char *name,	\
				void *data, void *ent)			\
{									\
	trace_seq_printf(s, " %s=" fmt, name, *(type *)data);		\
	return !trace_seq_has_overflowed(s);				\
}									\
const char PRINT_TYPE_FMT_NAME(tname)[] = fmt;				\
NOKPROBE_SYMBOL(PRINT_TYPE_FUNC_NAME(tname));

DEFINE_BASIC_PRINT_TYPE_FUNC(u8,  u8,  "%u")
DEFINE_BASIC_PRINT_TYPE_FUNC(u16, u16, "%u")
DEFINE_BASIC_PRINT_TYPE_FUNC(u32, u32, "%u")
DEFINE_BASIC_PRINT_TYPE_FUNC(u64, u64, "%Lu")
DEFINE_BASIC_PRINT_TYPE_FUNC(s8,  s8,  "%d")
DEFINE_BASIC_PRINT_TYPE_FUNC(s16, s16, "%d")
DEFINE_BASIC_PRINT_TYPE_FUNC(s32, s32, "%d")
DEFINE_BASIC_PRINT_TYPE_FUNC(s64, s64, "%Ld")
DEFINE_BASIC_PRINT_TYPE_FUNC(x8,  u8,  "0x%x")
DEFINE_BASIC_PRINT_TYPE_FUNC(x16, u16, "0x%x")
DEFINE_BASIC_PRINT_TYPE_FUNC(x32, u32, "0x%x")
DEFINE_BASIC_PRINT_TYPE_FUNC(x64, u64, "0x%Lx")

/* Print type function for string type */
int PRINT_TYPE_FUNC_NAME(string)(struct trace_seq *s, const char *name,
				 void *data, void *ent)
{
	int len = *(u32 *)data >> 16;

	if (!len)
		trace_seq_printf(s, " %s=(fault)", name);
	else
		trace_seq_printf(s, " %s=\"%s\"", name,
				 (const char *)get_loc_data(data, ent));
	return !trace_seq_has_overflowed(s);
}
NOKPROBE_SYMBOL(PRINT_TYPE_FUNC_NAME(string));

const char PRINT_TYPE_FMT_NAME(string)[] = "\\\"%s\\\"";

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
void FETCH_FUNC_NAME(reg, type)(struct pt_regs *regs, void *offset, void *dest)	\
{									\
	*(type *)dest = (type)regs_get_register(regs,			\
				(unsigned int)((unsigned long)offset));	\
}									\
NOKPROBE_SYMBOL(FETCH_FUNC_NAME(reg, type));
DEFINE_BASIC_FETCH_FUNCS(reg)
/* No string on the register */
#define fetch_reg_string	NULL
#define fetch_reg_string_size	NULL

#define DEFINE_FETCH_retval(type)					\
void FETCH_FUNC_NAME(retval, type)(struct pt_regs *regs,		\
				   void *dummy, void *dest)		\
{									\
	*(type *)dest = (type)regs_return_value(regs);			\
}									\
NOKPROBE_SYMBOL(FETCH_FUNC_NAME(retval, type));
DEFINE_BASIC_FETCH_FUNCS(retval)
/* No string on the retval */
#define fetch_retval_string		NULL
#define fetch_retval_string_size	NULL

/* Dereference memory access function */
struct deref_fetch_param {
	struct fetch_param	orig;
	long			offset;
	fetch_func_t		fetch;
	fetch_func_t		fetch_size;
};

#define DEFINE_FETCH_deref(type)					\
void FETCH_FUNC_NAME(deref, type)(struct pt_regs *regs,			\
				  void *data, void *dest)		\
{									\
	struct deref_fetch_param *dprm = data;				\
	unsigned long addr;						\
	call_fetch(&dprm->orig, regs, &addr);				\
	if (addr) {							\
		addr += dprm->offset;					\
		dprm->fetch(regs, (void *)addr, dest);			\
	} else								\
		*(type *)dest = 0;					\
}									\
NOKPROBE_SYMBOL(FETCH_FUNC_NAME(deref, type));
DEFINE_BASIC_FETCH_FUNCS(deref)
DEFINE_FETCH_deref(string)

void FETCH_FUNC_NAME(deref, string_size)(struct pt_regs *regs,
					 void *data, void *dest)
{
	struct deref_fetch_param *dprm = data;
	unsigned long addr;

	call_fetch(&dprm->orig, regs, &addr);
	if (addr && dprm->fetch_size) {
		addr += dprm->offset;
		dprm->fetch_size(regs, (void *)addr, dest);
	} else
		*(string_size *)dest = 0;
}
NOKPROBE_SYMBOL(FETCH_FUNC_NAME(deref, string_size));

static void update_deref_fetch_param(struct deref_fetch_param *data)
{
	if (CHECK_FETCH_FUNCS(deref, data->orig.fn))
		update_deref_fetch_param(data->orig.data);
	else if (CHECK_FETCH_FUNCS(symbol, data->orig.fn))
		update_symbol_cache(data->orig.data);
}
NOKPROBE_SYMBOL(update_deref_fetch_param);

static void free_deref_fetch_param(struct deref_fetch_param *data)
{
	if (CHECK_FETCH_FUNCS(deref, data->orig.fn))
		free_deref_fetch_param(data->orig.data);
	else if (CHECK_FETCH_FUNCS(symbol, data->orig.fn))
		free_symbol_cache(data->orig.data);
	kfree(data);
}
NOKPROBE_SYMBOL(free_deref_fetch_param);

/* Bitfield fetch function */
struct bitfield_fetch_param {
	struct fetch_param	orig;
	unsigned char		hi_shift;
	unsigned char		low_shift;
};

#define DEFINE_FETCH_bitfield(type)					\
void FETCH_FUNC_NAME(bitfield, type)(struct pt_regs *regs,		\
				     void *data, void *dest)		\
{									\
	struct bitfield_fetch_param *bprm = data;			\
	type buf = 0;							\
	call_fetch(&bprm->orig, regs, &buf);				\
	if (buf) {							\
		buf <<= bprm->hi_shift;					\
		buf >>= bprm->low_shift;				\
	}								\
	*(type *)dest = buf;						\
}									\
NOKPROBE_SYMBOL(FETCH_FUNC_NAME(bitfield, type));
DEFINE_BASIC_FETCH_FUNCS(bitfield)
#define fetch_bitfield_string		NULL
#define fetch_bitfield_string_size	NULL

static void
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

static void
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

void FETCH_FUNC_NAME(comm, string)(struct pt_regs *regs,
					  void *data, void *dest)
{
	int maxlen = get_rloc_len(*(u32 *)dest);
	u8 *dst = get_rloc_data(dest);
	long ret;

	if (!maxlen)
		return;

	ret = strlcpy(dst, current->comm, maxlen);
	*(u32 *)dest = make_data_rloc(ret, get_rloc_offs(*(u32 *)dest));
}
NOKPROBE_SYMBOL(FETCH_FUNC_NAME(comm, string));

void FETCH_FUNC_NAME(comm, string_size)(struct pt_regs *regs,
					       void *data, void *dest)
{
	*(u32 *)dest = strlen(current->comm) + 1;
}
NOKPROBE_SYMBOL(FETCH_FUNC_NAME(comm, string_size));

static const struct fetch_type *find_fetch_type(const char *type,
						const struct fetch_type *ftbl)
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
			return find_fetch_type("u8", ftbl);
		case 16:
			return find_fetch_type("u16", ftbl);
		case 32:
			return find_fetch_type("u32", ftbl);
		case 64:
			return find_fetch_type("u64", ftbl);
		default:
			goto fail;
		}
	}

	for (i = 0; ftbl[i].name; i++) {
		if (strcmp(type, ftbl[i].name) == 0)
			return &ftbl[i];
	}

fail:
	return NULL;
}

/* Special function : only accept unsigned long */
static void fetch_kernel_stack_address(struct pt_regs *regs, void *dummy, void *dest)
{
	*(unsigned long *)dest = kernel_stack_pointer(regs);
}
NOKPROBE_SYMBOL(fetch_kernel_stack_address);

static void fetch_user_stack_address(struct pt_regs *regs, void *dummy, void *dest)
{
	*(unsigned long *)dest = user_stack_pointer(regs);
}
NOKPROBE_SYMBOL(fetch_user_stack_address);

static fetch_func_t get_fetch_size_function(const struct fetch_type *type,
					    fetch_func_t orig_fn,
					    const struct fetch_type *ftbl)
{
	int i;

	if (type != &ftbl[FETCH_TYPE_STRING])
		return NULL;	/* Only string type needs size function */

	for (i = 0; i < FETCH_MTD_END; i++)
		if (type->fetch[i] == orig_fn)
			return ftbl[FETCH_TYPE_STRSIZE].fetch[i];

	WARN_ON(1);	/* This should not happen */

	return NULL;
}

/* Split symbol and offset. */
int traceprobe_split_symbol_offset(char *symbol, long *offset)
{
	char *tmp;
	int ret;

	if (!offset)
		return -EINVAL;

	tmp = strpbrk(symbol, "+-");
	if (tmp) {
		ret = kstrtol(tmp, 0, offset);
		if (ret)
			return ret;
		*tmp = '\0';
	} else
		*offset = 0;

	return 0;
}

#define PARAM_MAX_STACK (THREAD_SIZE / sizeof(unsigned long))

static int parse_probe_vars(char *arg, const struct fetch_type *t,
			    struct fetch_param *f, bool is_return,
			    bool is_kprobe)
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
			if (strcmp(t->name, DEFAULT_FETCH_TYPE_STR))
				return -EINVAL;

			if (is_kprobe)
				f->fn = fetch_kernel_stack_address;
			else
				f->fn = fetch_user_stack_address;
		} else if (isdigit(arg[5])) {
			ret = kstrtoul(arg + 5, 10, &param);
			if (ret || (is_kprobe && param > PARAM_MAX_STACK))
				ret = -EINVAL;
			else {
				f->fn = t->fetch[FETCH_MTD_stack];
				f->data = (void *)param;
			}
		} else
			ret = -EINVAL;
	} else if (strcmp(arg, "comm") == 0) {
		if (strcmp(t->name, "string") != 0 &&
		    strcmp(t->name, "string_size") != 0)
			return -EINVAL;
		f->fn = t->fetch[FETCH_MTD_comm];
	} else
		ret = -EINVAL;

	return ret;
}

/* Recursive argument parser */
static int parse_probe_arg(char *arg, const struct fetch_type *t,
		     struct fetch_param *f, bool is_return, bool is_kprobe,
		     const struct fetch_type *ftbl)
{
	unsigned long param;
	long offset;
	char *tmp;
	int ret = 0;

	switch (arg[0]) {
	case '$':
		ret = parse_probe_vars(arg + 1, t, f, is_return, is_kprobe);
		break;

	case '%':	/* named register */
		ret = regs_query_register_offset(arg + 1);
		if (ret >= 0) {
			f->fn = t->fetch[FETCH_MTD_reg];
			f->data = (void *)(unsigned long)ret;
			ret = 0;
		}
		break;

	case '@':	/* memory, file-offset or symbol */
		if (isdigit(arg[1])) {
			ret = kstrtoul(arg + 1, 0, &param);
			if (ret)
				break;

			f->fn = t->fetch[FETCH_MTD_memory];
			f->data = (void *)param;
		} else if (arg[1] == '+') {
			/* kprobes don't support file offsets */
			if (is_kprobe)
				return -EINVAL;

			ret = kstrtol(arg + 2, 0, &offset);
			if (ret)
				break;

			f->fn = t->fetch[FETCH_MTD_file_offset];
			f->data = (void *)offset;
		} else {
			/* uprobes don't support symbols */
			if (!is_kprobe)
				return -EINVAL;

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

			t2 = find_fetch_type(NULL, ftbl);
			*tmp = '\0';
			dprm = kzalloc(sizeof(struct deref_fetch_param), GFP_KERNEL);

			if (!dprm)
				return -ENOMEM;

			dprm->offset = offset;
			dprm->fetch = t->fetch[FETCH_MTD_memory];
			dprm->fetch_size = get_fetch_size_function(t,
							dprm->fetch, ftbl);
			ret = parse_probe_arg(arg, t2, &dprm->orig, is_return,
							is_kprobe, ftbl);
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
		struct probe_arg *parg, bool is_return, bool is_kprobe,
		const struct fetch_type *ftbl)
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
	/*
	 * The default type of $comm should be "string", and it can't be
	 * dereferenced.
	 */
	if (!t && strcmp(arg, "$comm") == 0)
		t = "string";
	parg->type = find_fetch_type(t, ftbl);
	if (!parg->type) {
		pr_info("Unsupported type: %s\n", t);
		return -EINVAL;
	}
	parg->offset = *size;
	*size += parg->type->size;
	ret = parse_probe_arg(arg, parg->type, &parg->fetch, is_return,
			      is_kprobe, ftbl);

	if (ret >= 0 && t != NULL)
		ret = __parse_bitfield_probe_arg(t, parg->type, &parg->fetch);

	if (ret >= 0) {
		parg->fetch_size.fn = get_fetch_size_function(parg->type,
							      parg->fetch.fn,
							      ftbl);
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
	char *kbuf, *buf, *tmp;
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
		buf = kbuf;
		do {
			tmp = strchr(buf, '\n');
			if (tmp) {
				*tmp = '\0';
				size = tmp - buf + 1;
			} else {
				size = strlen(buf);
				if (done + size < count) {
					if (buf != kbuf)
						break;
					/* This can accept WRITE_BUFSIZE - 2 ('\n' + '\0') */
					pr_warn("Line length is too long: Should be less than %d\n",
						WRITE_BUFSIZE - 2);
					ret = -EINVAL;
					goto out;
				}
			}
			done += size;

			/* Remove comments */
			tmp = strchr(buf, '#');

			if (tmp)
				*tmp = '\0';

			ret = traceprobe_command(buf, createfn);
			if (ret)
				goto out;
			buf += size;

		} while (done < count);
	}
	ret = done;

out:
	kfree(kbuf);

	return ret;
}

static int __set_print_fmt(struct trace_probe *tp, char *buf, int len,
			   bool is_return)
{
	int i;
	int pos = 0;

	const char *fmt, *arg;

	if (!is_return) {
		fmt = "(%lx)";
		arg = "REC->" FIELD_STRING_IP;
	} else {
		fmt = "(%lx <- %lx)";
		arg = "REC->" FIELD_STRING_FUNC ", REC->" FIELD_STRING_RETIP;
	}

	/* When len=0, we just calculate the needed length */
#define LEN_OR_ZERO (len ? len - pos : 0)

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"%s", fmt);

	for (i = 0; i < tp->nr_args; i++) {
		pos += snprintf(buf + pos, LEN_OR_ZERO, " %s=%s",
				tp->args[i].name, tp->args[i].type->fmt);
	}

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\", %s", arg);

	for (i = 0; i < tp->nr_args; i++) {
		if (strcmp(tp->args[i].type->name, "string") == 0)
			pos += snprintf(buf + pos, LEN_OR_ZERO,
					", __get_str(%s)",
					tp->args[i].name);
		else
			pos += snprintf(buf + pos, LEN_OR_ZERO, ", REC->%s",
					tp->args[i].name);
	}

#undef LEN_OR_ZERO

	/* return the length of print_fmt */
	return pos;
}

int set_print_fmt(struct trace_probe *tp, bool is_return)
{
	int len;
	char *print_fmt;

	/* First: called with 0 length to calculate the needed length */
	len = __set_print_fmt(tp, NULL, 0, is_return);
	print_fmt = kmalloc(len + 1, GFP_KERNEL);
	if (!print_fmt)
		return -ENOMEM;

	/* Second: actually write the @print_fmt */
	__set_print_fmt(tp, print_fmt, len + 1, is_return);
	tp->call.print_fmt = print_fmt;

	return 0;
}
