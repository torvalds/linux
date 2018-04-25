// SPDX-License-Identifier: GPL-2.0
/*
 * Common code for probe-based Dynamic events.
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
int PRINT_TYPE_FUNC_NAME(tname)(struct trace_seq *s, void *data, void *ent)\
{									\
	trace_seq_printf(s, fmt, *(type *)data);			\
	return !trace_seq_has_overflowed(s);				\
}									\
const char PRINT_TYPE_FMT_NAME(tname)[] = fmt;

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
int PRINT_TYPE_FUNC_NAME(string)(struct trace_seq *s, void *data, void *ent)
{
	int len = *(u32 *)data >> 16;

	if (!len)
		trace_seq_puts(s, "(fault)");
	else
		trace_seq_printf(s, "\"%s\"",
				 (const char *)get_loc_data(data, ent));
	return !trace_seq_has_overflowed(s);
}

const char PRINT_TYPE_FMT_NAME(string)[] = "\\\"%s\\\"";

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
			    struct fetch_insn *code, bool is_return,
			    bool is_kprobe)
{
	int ret = 0;
	unsigned long param;

	if (strcmp(arg, "retval") == 0) {
		if (is_return)
			code->op = FETCH_OP_RETVAL;
		else
			ret = -EINVAL;
	} else if (strncmp(arg, "stack", 5) == 0) {
		if (arg[5] == '\0') {
			code->op = FETCH_OP_STACKP;
		} else if (isdigit(arg[5])) {
			ret = kstrtoul(arg + 5, 10, &param);
			if (ret || (is_kprobe && param > PARAM_MAX_STACK))
				ret = -EINVAL;
			else {
				code->op = FETCH_OP_STACK;
				code->param = (unsigned int)param;
			}
		} else
			ret = -EINVAL;
	} else if (strcmp(arg, "comm") == 0) {
		code->op = FETCH_OP_COMM;
	} else
		ret = -EINVAL;

	return ret;
}

/* Recursive argument parser */
static int
parse_probe_arg(char *arg, const struct fetch_type *type,
		struct fetch_insn **pcode, struct fetch_insn *end,
		bool is_return, bool is_kprobe,
		const struct fetch_type *ftbl)
{
	struct fetch_insn *code = *pcode;
	unsigned long param;
	long offset;
	char *tmp;
	int ret = 0;

	switch (arg[0]) {
	case '$':
		ret = parse_probe_vars(arg + 1, type, code,
					is_return, is_kprobe);
		break;

	case '%':	/* named register */
		ret = regs_query_register_offset(arg + 1);
		if (ret >= 0) {
			code->op = FETCH_OP_REG;
			code->param = (unsigned int)ret;
			ret = 0;
		}
		break;

	case '@':	/* memory, file-offset or symbol */
		if (isdigit(arg[1])) {
			ret = kstrtoul(arg + 1, 0, &param);
			if (ret)
				break;
			/* load address */
			code->op = FETCH_OP_IMM;
			code->immediate = param;
		} else if (arg[1] == '+') {
			/* kprobes don't support file offsets */
			if (is_kprobe)
				return -EINVAL;

			ret = kstrtol(arg + 2, 0, &offset);
			if (ret)
				break;

			code->op = FETCH_OP_FOFFS;
			code->immediate = (unsigned long)offset;  // imm64?
		} else {
			/* uprobes don't support symbols */
			if (!is_kprobe)
				return -EINVAL;

			ret = traceprobe_split_symbol_offset(arg + 1, &offset);
			if (ret)
				break;

			code->op = FETCH_OP_IMM;
			code->immediate =
				(unsigned long)kallsyms_lookup_name(arg + 1);
			if (!code->immediate)
				return -ENOENT;
			code->immediate += offset;
		}
		/* These are fetching from memory */
		if (++code == end)
			return -E2BIG;
		*pcode = code;
		code->op = FETCH_OP_DEREF;
		code->offset = offset;
		break;

	case '+':	/* deref memory */
		arg++;	/* Skip '+', because kstrtol() rejects it. */
	case '-':
		tmp = strchr(arg, '(');
		if (!tmp)
			return -EINVAL;

		*tmp = '\0';
		ret = kstrtol(arg, 0, &offset);
		if (ret)
			break;

		arg = tmp + 1;
		tmp = strrchr(arg, ')');

		if (tmp) {
			const struct fetch_type *t2;

			t2 = find_fetch_type(NULL, ftbl);
			*tmp = '\0';
			ret = parse_probe_arg(arg, t2, &code, end, is_return,
					      is_kprobe, ftbl);
			if (ret)
				break;
			if (code->op == FETCH_OP_COMM)
				return -EINVAL;
			if (++code == end)
				return -E2BIG;
			*pcode = code;

			code->op = FETCH_OP_DEREF;
			code->offset = offset;
		}
		break;
	}
	if (!ret && code->op == FETCH_OP_NOP) {
		/* Parsed, but do not find fetch method */
		ret = -EINVAL;
	}
	return ret;
}

#define BYTES_TO_BITS(nb)	((BITS_PER_LONG * (nb)) / sizeof(long))

/* Bitfield type needs to be parsed into a fetch function */
static int __parse_bitfield_probe_arg(const char *bf,
				      const struct fetch_type *t,
				      struct fetch_insn **pcode)
{
	struct fetch_insn *code = *pcode;
	unsigned long bw, bo;
	char *tail;

	if (*bf != 'b')
		return 0;

	bw = simple_strtoul(bf + 1, &tail, 0);	/* Use simple one */

	if (bw == 0 || *tail != '@')
		return -EINVAL;

	bf = tail + 1;
	bo = simple_strtoul(bf, &tail, 0);

	if (tail == bf || *tail != '/')
		return -EINVAL;
	code++;
	if (code->op != FETCH_OP_NOP)
		return -E2BIG;
	*pcode = code;

	code->op = FETCH_OP_MOD_BF;
	code->lshift = BYTES_TO_BITS(t->size) - (bw + bo);
	code->rshift = BYTES_TO_BITS(t->size) - bw;
	code->basesize = t->size;

	return (BYTES_TO_BITS(t->size) < (bw + bo)) ? -EINVAL : 0;
}

/* String length checking wrapper */
int traceprobe_parse_probe_arg(char *arg, ssize_t *size,
		struct probe_arg *parg, bool is_return, bool is_kprobe,
		const struct fetch_type *ftbl)
{
	struct fetch_insn *code, *tmp = NULL;
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

	code = tmp = kzalloc(sizeof(*code) * FETCH_INSN_MAX, GFP_KERNEL);
	if (!code)
		return -ENOMEM;
	code[FETCH_INSN_MAX - 1].op = FETCH_OP_END;

	ret = parse_probe_arg(arg, parg->type, &code, &code[FETCH_INSN_MAX - 1],
			      is_return, is_kprobe, ftbl);
	if (ret)
		goto fail;

	/* Store operation */
	if (!strcmp(parg->type->name, "string")) {
		if (code->op != FETCH_OP_DEREF && code->op != FETCH_OP_IMM &&
		    code->op != FETCH_OP_COMM) {
			pr_info("string only accepts memory or address.\n");
			ret = -EINVAL;
			goto fail;
		}
		/* Since IMM or COMM must be the 1st insn, this is safe */
		if (code->op == FETCH_OP_IMM || code->op == FETCH_OP_COMM)
			code++;
		code->op = FETCH_OP_ST_STRING;	/* In DEREF case, replace it */
		parg->dynamic = true;
	} else if (code->op == FETCH_OP_DEREF) {
		code->op = FETCH_OP_ST_MEM;
		code->size = parg->type->size;
	} else {
		code++;
		if (code->op != FETCH_OP_NOP) {
			ret = -E2BIG;
			goto fail;
		}
		code->op = FETCH_OP_ST_RAW;
		code->size = parg->type->size;
	}
	/* Modify operation */
	if (t != NULL) {
		ret = __parse_bitfield_probe_arg(t, parg->type, &code);
		if (ret)
			goto fail;
	}
	code++;
	code->op = FETCH_OP_END;

	/* Shrink down the code buffer */
	parg->code = kzalloc(sizeof(*code) * (code - tmp + 1), GFP_KERNEL);
	if (!parg->code)
		ret = -ENOMEM;
	else
		memcpy(parg->code, tmp, sizeof(*code) * (code - tmp + 1));

fail:
	kfree(tmp);

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

void traceprobe_free_probe_arg(struct probe_arg *arg)
{
	kfree(arg->code);
	kfree(arg->name);
	kfree(arg->comm);
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

int traceprobe_define_arg_fields(struct trace_event_call *event_call,
				 size_t offset, struct trace_probe *tp)
{
	int ret, i;

	/* Set argument names as fields */
	for (i = 0; i < tp->nr_args; i++) {
		struct probe_arg *parg = &tp->args[i];

		ret = trace_define_field(event_call, parg->type->fmttype,
					 parg->name,
					 offset + parg->offset,
					 parg->type->size,
					 parg->type->is_signed,
					 FILTER_OTHER);
		if (ret)
			return ret;
	}
	return 0;
}
