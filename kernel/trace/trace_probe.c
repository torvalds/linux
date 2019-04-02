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

#undef C
#define C(a, b)		b

static const char *trace_probe_err_text[] = { ERRORS };

static const char *reserved_field_names[] = {
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

int PRINT_TYPE_FUNC_NAME(symbol)(struct trace_seq *s, void *data, void *ent)
{
	trace_seq_printf(s, "%pS", (void *)*(unsigned long *)data);
	return !trace_seq_has_overflowed(s);
}
const char PRINT_TYPE_FMT_NAME(symbol)[] = "%pS";

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

/* Fetch type information table */
static const struct fetch_type probe_fetch_types[] = {
	/* Special types */
	__ASSIGN_FETCH_TYPE("string", string, string, sizeof(u32), 1,
			    "__data_loc char[]"),
	/* Basic types */
	ASSIGN_FETCH_TYPE(u8,  u8,  0),
	ASSIGN_FETCH_TYPE(u16, u16, 0),
	ASSIGN_FETCH_TYPE(u32, u32, 0),
	ASSIGN_FETCH_TYPE(u64, u64, 0),
	ASSIGN_FETCH_TYPE(s8,  u8,  1),
	ASSIGN_FETCH_TYPE(s16, u16, 1),
	ASSIGN_FETCH_TYPE(s32, u32, 1),
	ASSIGN_FETCH_TYPE(s64, u64, 1),
	ASSIGN_FETCH_TYPE_ALIAS(x8,  u8,  u8,  0),
	ASSIGN_FETCH_TYPE_ALIAS(x16, u16, u16, 0),
	ASSIGN_FETCH_TYPE_ALIAS(x32, u32, u32, 0),
	ASSIGN_FETCH_TYPE_ALIAS(x64, u64, u64, 0),
	ASSIGN_FETCH_TYPE_ALIAS(symbol, ADDR_FETCH_TYPE, ADDR_FETCH_TYPE, 0),

	ASSIGN_FETCH_TYPE_END
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

	for (i = 0; probe_fetch_types[i].name; i++) {
		if (strcmp(type, probe_fetch_types[i].name) == 0)
			return &probe_fetch_types[i];
	}

fail:
	return NULL;
}

static struct trace_probe_log trace_probe_log;

void trace_probe_log_init(const char *subsystem, int argc, const char **argv)
{
	trace_probe_log.subsystem = subsystem;
	trace_probe_log.argc = argc;
	trace_probe_log.argv = argv;
	trace_probe_log.index = 0;
}

void trace_probe_log_clear(void)
{
	memset(&trace_probe_log, 0, sizeof(trace_probe_log));
}

void trace_probe_log_set_index(int index)
{
	trace_probe_log.index = index;
}

void __trace_probe_log_err(int offset, int err_type)
{
	char *command, *p;
	int i, len = 0, pos = 0;

	if (!trace_probe_log.argv)
		return;

	/* Recalcurate the length and allocate buffer */
	for (i = 0; i < trace_probe_log.argc; i++) {
		if (i == trace_probe_log.index)
			pos = len;
		len += strlen(trace_probe_log.argv[i]) + 1;
	}
	command = kzalloc(len, GFP_KERNEL);
	if (!command)
		return;

	/* And make a command string from argv array */
	p = command;
	for (i = 0; i < trace_probe_log.argc; i++) {
		len = strlen(trace_probe_log.argv[i]);
		strcpy(p, trace_probe_log.argv[i]);
		p[len] = ' ';
		p += len + 1;
	}
	*(p - 1) = '\0';

	tracing_log_err(NULL, trace_probe_log.subsystem, command,
			trace_probe_err_text, err_type, pos + offset);

	kfree(command);
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

/* @buf must has MAX_EVENT_NAME_LEN size */
int traceprobe_parse_event_name(const char **pevent, const char **pgroup,
				char *buf, int offset)
{
	const char *slash, *event = *pevent;
	int len;

	slash = strchr(event, '/');
	if (slash) {
		if (slash == event) {
			trace_probe_log_err(offset, NO_GROUP_NAME);
			return -EINVAL;
		}
		if (slash - event + 1 > MAX_EVENT_NAME_LEN) {
			trace_probe_log_err(offset, GROUP_TOO_LONG);
			return -EINVAL;
		}
		strlcpy(buf, event, slash - event + 1);
		if (!is_good_name(buf)) {
			trace_probe_log_err(offset, BAD_GROUP_NAME);
			return -EINVAL;
		}
		*pgroup = buf;
		*pevent = slash + 1;
		offset += slash - event + 1;
		event = *pevent;
	}
	len = strlen(event);
	if (len == 0) {
		trace_probe_log_err(offset, NO_EVENT_NAME);
		return -EINVAL;
	} else if (len > MAX_EVENT_NAME_LEN) {
		trace_probe_log_err(offset, EVENT_TOO_LONG);
		return -EINVAL;
	}
	if (!is_good_name(event)) {
		trace_probe_log_err(offset, BAD_EVENT_NAME);
		return -EINVAL;
	}
	return 0;
}

#define PARAM_MAX_STACK (THREAD_SIZE / sizeof(unsigned long))

static int parse_probe_vars(char *arg, const struct fetch_type *t,
			struct fetch_insn *code, unsigned int flags, int offs)
{
	unsigned long param;
	int ret = 0;
	int len;

	if (strcmp(arg, "retval") == 0) {
		if (flags & TPARG_FL_RETURN) {
			code->op = FETCH_OP_RETVAL;
		} else {
			trace_probe_log_err(offs, RETVAL_ON_PROBE);
			ret = -EINVAL;
		}
	} else if ((len = str_has_prefix(arg, "stack"))) {
		if (arg[len] == '\0') {
			code->op = FETCH_OP_STACKP;
		} else if (isdigit(arg[len])) {
			ret = kstrtoul(arg + len, 10, &param);
			if (ret) {
				goto inval_var;
			} else if ((flags & TPARG_FL_KERNEL) &&
				    param > PARAM_MAX_STACK) {
				trace_probe_log_err(offs, BAD_STACK_NUM);
				ret = -EINVAL;
			} else {
				code->op = FETCH_OP_STACK;
				code->param = (unsigned int)param;
			}
		} else
			goto inval_var;
	} else if (strcmp(arg, "comm") == 0) {
		code->op = FETCH_OP_COMM;
#ifdef CONFIG_HAVE_FUNCTION_ARG_ACCESS_API
	} else if (((flags & TPARG_FL_MASK) ==
		    (TPARG_FL_KERNEL | TPARG_FL_FENTRY)) &&
		   (len = str_has_prefix(arg, "arg"))) {
		ret = kstrtoul(arg + len, 10, &param);
		if (ret) {
			goto inval_var;
		} else if (!param || param > PARAM_MAX_STACK) {
			trace_probe_log_err(offs, BAD_ARG_NUM);
			return -EINVAL;
		}
		code->op = FETCH_OP_ARG;
		code->param = (unsigned int)param - 1;
#endif
	} else
		goto inval_var;

	return ret;

inval_var:
	trace_probe_log_err(offs, BAD_VAR);
	return -EINVAL;
}

/* Recursive argument parser */
static int
parse_probe_arg(char *arg, const struct fetch_type *type,
		struct fetch_insn **pcode, struct fetch_insn *end,
		unsigned int flags, int offs)
{
	struct fetch_insn *code = *pcode;
	unsigned long param;
	long offset = 0;
	char *tmp;
	int ret = 0;

	switch (arg[0]) {
	case '$':
		ret = parse_probe_vars(arg + 1, type, code, flags, offs);
		break;

	case '%':	/* named register */
		ret = regs_query_register_offset(arg + 1);
		if (ret >= 0) {
			code->op = FETCH_OP_REG;
			code->param = (unsigned int)ret;
			ret = 0;
		} else
			trace_probe_log_err(offs, BAD_REG_NAME);
		break;

	case '@':	/* memory, file-offset or symbol */
		if (isdigit(arg[1])) {
			ret = kstrtoul(arg + 1, 0, &param);
			if (ret) {
				trace_probe_log_err(offs, BAD_MEM_ADDR);
				break;
			}
			/* load address */
			code->op = FETCH_OP_IMM;
			code->immediate = param;
		} else if (arg[1] == '+') {
			/* kprobes don't support file offsets */
			if (flags & TPARG_FL_KERNEL) {
				trace_probe_log_err(offs, FILE_ON_KPROBE);
				return -EINVAL;
			}
			ret = kstrtol(arg + 2, 0, &offset);
			if (ret) {
				trace_probe_log_err(offs, BAD_FILE_OFFS);
				break;
			}

			code->op = FETCH_OP_FOFFS;
			code->immediate = (unsigned long)offset;  // imm64?
		} else {
			/* uprobes don't support symbols */
			if (!(flags & TPARG_FL_KERNEL)) {
				trace_probe_log_err(offs, SYM_ON_UPROBE);
				return -EINVAL;
			}
			/* Preserve symbol for updating */
			code->op = FETCH_NOP_SYMBOL;
			code->data = kstrdup(arg + 1, GFP_KERNEL);
			if (!code->data)
				return -ENOMEM;
			if (++code == end) {
				trace_probe_log_err(offs, TOO_MANY_OPS);
				return -EINVAL;
			}
			code->op = FETCH_OP_IMM;
			code->immediate = 0;
		}
		/* These are fetching from memory */
		if (++code == end) {
			trace_probe_log_err(offs, TOO_MANY_OPS);
			return -EINVAL;
		}
		*pcode = code;
		code->op = FETCH_OP_DEREF;
		code->offset = offset;
		break;

	case '+':	/* deref memory */
		arg++;	/* Skip '+', because kstrtol() rejects it. */
		/* fall through */
	case '-':
		tmp = strchr(arg, '(');
		if (!tmp) {
			trace_probe_log_err(offs, DEREF_NEED_BRACE);
			return -EINVAL;
		}
		*tmp = '\0';
		ret = kstrtol(arg, 0, &offset);
		if (ret) {
			trace_probe_log_err(offs, BAD_DEREF_OFFS);
			break;
		}
		offs += (tmp + 1 - arg) + (arg[0] != '-' ? 1 : 0);
		arg = tmp + 1;
		tmp = strrchr(arg, ')');
		if (!tmp) {
			trace_probe_log_err(offs + strlen(arg),
					    DEREF_OPEN_BRACE);
			return -EINVAL;
		} else {
			const struct fetch_type *t2 = find_fetch_type(NULL);

			*tmp = '\0';
			ret = parse_probe_arg(arg, t2, &code, end, flags, offs);
			if (ret)
				break;
			if (code->op == FETCH_OP_COMM) {
				trace_probe_log_err(offs, COMM_CANT_DEREF);
				return -EINVAL;
			}
			if (++code == end) {
				trace_probe_log_err(offs, TOO_MANY_OPS);
				return -EINVAL;
			}
			*pcode = code;

			code->op = FETCH_OP_DEREF;
			code->offset = offset;
		}
		break;
	}
	if (!ret && code->op == FETCH_OP_NOP) {
		/* Parsed, but do not find fetch method */
		trace_probe_log_err(offs, BAD_FETCH_ARG);
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
		return -EINVAL;
	*pcode = code;

	code->op = FETCH_OP_MOD_BF;
	code->lshift = BYTES_TO_BITS(t->size) - (bw + bo);
	code->rshift = BYTES_TO_BITS(t->size) - bw;
	code->basesize = t->size;

	return (BYTES_TO_BITS(t->size) < (bw + bo)) ? -EINVAL : 0;
}

/* String length checking wrapper */
static int traceprobe_parse_probe_arg_body(char *arg, ssize_t *size,
		struct probe_arg *parg, unsigned int flags, int offset)
{
	struct fetch_insn *code, *scode, *tmp = NULL;
	char *t, *t2, *t3;
	int ret, len;

	len = strlen(arg);
	if (len > MAX_ARGSTR_LEN) {
		trace_probe_log_err(offset, ARG_TOO_LONG);
		return -EINVAL;
	} else if (len == 0) {
		trace_probe_log_err(offset, NO_ARG_BODY);
		return -EINVAL;
	}

	parg->comm = kstrdup(arg, GFP_KERNEL);
	if (!parg->comm)
		return -ENOMEM;

	t = strchr(arg, ':');
	if (t) {
		*t = '\0';
		t2 = strchr(++t, '[');
		if (t2) {
			*t2++ = '\0';
			t3 = strchr(t2, ']');
			if (!t3) {
				offset += t2 + strlen(t2) - arg;
				trace_probe_log_err(offset,
						    ARRAY_NO_CLOSE);
				return -EINVAL;
			} else if (t3[1] != '\0') {
				trace_probe_log_err(offset + t3 + 1 - arg,
						    BAD_ARRAY_SUFFIX);
				return -EINVAL;
			}
			*t3 = '\0';
			if (kstrtouint(t2, 0, &parg->count) || !parg->count) {
				trace_probe_log_err(offset + t2 - arg,
						    BAD_ARRAY_NUM);
				return -EINVAL;
			}
			if (parg->count > MAX_ARRAY_LEN) {
				trace_probe_log_err(offset + t2 - arg,
						    ARRAY_TOO_BIG);
				return -EINVAL;
			}
		}
	}
	/*
	 * The default type of $comm should be "string", and it can't be
	 * dereferenced.
	 */
	if (!t && strcmp(arg, "$comm") == 0)
		parg->type = find_fetch_type("string");
	else
		parg->type = find_fetch_type(t);
	if (!parg->type) {
		trace_probe_log_err(offset + (t ? (t - arg) : 0), BAD_TYPE);
		return -EINVAL;
	}
	parg->offset = *size;
	*size += parg->type->size * (parg->count ?: 1);

	if (parg->count) {
		len = strlen(parg->type->fmttype) + 6;
		parg->fmt = kmalloc(len, GFP_KERNEL);
		if (!parg->fmt)
			return -ENOMEM;
		snprintf(parg->fmt, len, "%s[%d]", parg->type->fmttype,
			 parg->count);
	}

	code = tmp = kzalloc(sizeof(*code) * FETCH_INSN_MAX, GFP_KERNEL);
	if (!code)
		return -ENOMEM;
	code[FETCH_INSN_MAX - 1].op = FETCH_OP_END;

	ret = parse_probe_arg(arg, parg->type, &code, &code[FETCH_INSN_MAX - 1],
			      flags, offset);
	if (ret)
		goto fail;

	/* Store operation */
	if (!strcmp(parg->type->name, "string")) {
		if (code->op != FETCH_OP_DEREF && code->op != FETCH_OP_IMM &&
		    code->op != FETCH_OP_COMM) {
			trace_probe_log_err(offset + (t ? (t - arg) : 0),
					    BAD_STRING);
			ret = -EINVAL;
			goto fail;
		}
		if (code->op != FETCH_OP_DEREF || parg->count) {
			/*
			 * IMM and COMM is pointing actual address, those must
			 * be kept, and if parg->count != 0, this is an array
			 * of string pointers instead of string address itself.
			 */
			code++;
			if (code->op != FETCH_OP_NOP) {
				trace_probe_log_err(offset, TOO_MANY_OPS);
				ret = -EINVAL;
				goto fail;
			}
		}
		code->op = FETCH_OP_ST_STRING;	/* In DEREF case, replace it */
		code->size = parg->type->size;
		parg->dynamic = true;
	} else if (code->op == FETCH_OP_DEREF) {
		code->op = FETCH_OP_ST_MEM;
		code->size = parg->type->size;
	} else {
		code++;
		if (code->op != FETCH_OP_NOP) {
			trace_probe_log_err(offset, TOO_MANY_OPS);
			ret = -EINVAL;
			goto fail;
		}
		code->op = FETCH_OP_ST_RAW;
		code->size = parg->type->size;
	}
	scode = code;
	/* Modify operation */
	if (t != NULL) {
		ret = __parse_bitfield_probe_arg(t, parg->type, &code);
		if (ret) {
			trace_probe_log_err(offset + t - arg, BAD_BITFIELD);
			goto fail;
		}
	}
	/* Loop(Array) operation */
	if (parg->count) {
		if (scode->op != FETCH_OP_ST_MEM &&
		    scode->op != FETCH_OP_ST_STRING) {
			trace_probe_log_err(offset + (t ? (t - arg) : 0),
					    BAD_STRING);
			ret = -EINVAL;
			goto fail;
		}
		code++;
		if (code->op != FETCH_OP_NOP) {
			trace_probe_log_err(offset, TOO_MANY_OPS);
			ret = -EINVAL;
			goto fail;
		}
		code->op = FETCH_OP_LP_ARRAY;
		code->param = parg->count;
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
	if (ret) {
		for (code = tmp; code < tmp + FETCH_INSN_MAX; code++)
			if (code->op == FETCH_NOP_SYMBOL)
				kfree(code->data);
	}
	kfree(tmp);

	return ret;
}

/* Return 1 if name is reserved or already used by another argument */
static int traceprobe_conflict_field_name(const char *name,
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

int traceprobe_parse_probe_arg(struct trace_probe *tp, int i, char *arg,
				unsigned int flags)
{
	struct probe_arg *parg = &tp->args[i];
	char *body;

	/* Increment count for freeing args in error case */
	tp->nr_args++;

	body = strchr(arg, '=');
	if (body) {
		if (body - arg > MAX_ARG_NAME_LEN) {
			trace_probe_log_err(0, ARG_NAME_TOO_LONG);
			return -EINVAL;
		} else if (body == arg) {
			trace_probe_log_err(0, NO_ARG_NAME);
			return -EINVAL;
		}
		parg->name = kmemdup_nul(arg, body - arg, GFP_KERNEL);
		body++;
	} else {
		/* If argument name is omitted, set "argN" */
		parg->name = kasprintf(GFP_KERNEL, "arg%d", i + 1);
		body = arg;
	}
	if (!parg->name)
		return -ENOMEM;

	if (!is_good_name(parg->name)) {
		trace_probe_log_err(0, BAD_ARG_NAME);
		return -EINVAL;
	}
	if (traceprobe_conflict_field_name(parg->name, tp->args, i)) {
		trace_probe_log_err(0, USED_ARG_NAME);
		return -EINVAL;
	}
	/* Parse fetch argument */
	return traceprobe_parse_probe_arg_body(body, &tp->size, parg, flags,
					       body - arg);
}

void traceprobe_free_probe_arg(struct probe_arg *arg)
{
	struct fetch_insn *code = arg->code;

	while (code && code->op != FETCH_OP_END) {
		if (code->op == FETCH_NOP_SYMBOL)
			kfree(code->data);
		code++;
	}
	kfree(arg->code);
	kfree(arg->name);
	kfree(arg->comm);
	kfree(arg->fmt);
}

int traceprobe_update_arg(struct probe_arg *arg)
{
	struct fetch_insn *code = arg->code;
	long offset;
	char *tmp;
	char c;
	int ret = 0;

	while (code && code->op != FETCH_OP_END) {
		if (code->op == FETCH_NOP_SYMBOL) {
			if (code[1].op != FETCH_OP_IMM)
				return -EINVAL;

			tmp = strpbrk(code->data, "+-");
			if (tmp)
				c = *tmp;
			ret = traceprobe_split_symbol_offset(code->data,
							     &offset);
			if (ret)
				return ret;

			code[1].immediate =
				(unsigned long)kallsyms_lookup_name(code->data);
			if (tmp)
				*tmp = c;
			if (!code[1].immediate)
				return -ENOENT;
			code[1].immediate += offset;
		}
		code++;
	}
	return 0;
}

/* When len=0, we just calculate the needed length */
#define LEN_OR_ZERO (len ? len - pos : 0)
static int __set_print_fmt(struct trace_probe *tp, char *buf, int len,
			   bool is_return)
{
	struct probe_arg *parg;
	int i, j;
	int pos = 0;
	const char *fmt, *arg;

	if (!is_return) {
		fmt = "(%lx)";
		arg = "REC->" FIELD_STRING_IP;
	} else {
		fmt = "(%lx <- %lx)";
		arg = "REC->" FIELD_STRING_FUNC ", REC->" FIELD_STRING_RETIP;
	}

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"%s", fmt);

	for (i = 0; i < tp->nr_args; i++) {
		parg = tp->args + i;
		pos += snprintf(buf + pos, LEN_OR_ZERO, " %s=", parg->name);
		if (parg->count) {
			pos += snprintf(buf + pos, LEN_OR_ZERO, "{%s",
					parg->type->fmt);
			for (j = 1; j < parg->count; j++)
				pos += snprintf(buf + pos, LEN_OR_ZERO, ",%s",
						parg->type->fmt);
			pos += snprintf(buf + pos, LEN_OR_ZERO, "}");
		} else
			pos += snprintf(buf + pos, LEN_OR_ZERO, "%s",
					parg->type->fmt);
	}

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\", %s", arg);

	for (i = 0; i < tp->nr_args; i++) {
		parg = tp->args + i;
		if (parg->count) {
			if (strcmp(parg->type->name, "string") == 0)
				fmt = ", __get_str(%s[%d])";
			else
				fmt = ", REC->%s[%d]";
			for (j = 0; j < parg->count; j++)
				pos += snprintf(buf + pos, LEN_OR_ZERO,
						fmt, parg->name, j);
		} else {
			if (strcmp(parg->type->name, "string") == 0)
				fmt = ", __get_str(%s)";
			else
				fmt = ", REC->%s";
			pos += snprintf(buf + pos, LEN_OR_ZERO,
					fmt, parg->name);
		}
	}

	/* return the length of print_fmt */
	return pos;
}
#undef LEN_OR_ZERO

int traceprobe_set_print_fmt(struct trace_probe *tp, bool is_return)
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
		const char *fmt = parg->type->fmttype;
		int size = parg->type->size;

		if (parg->fmt)
			fmt = parg->fmt;
		if (parg->count)
			size *= parg->count;
		ret = trace_define_field(event_call, fmt, parg->name,
					 offset + parg->offset, size,
					 parg->type->is_signed,
					 FILTER_OTHER);
		if (ret)
			return ret;
	}
	return 0;
}
