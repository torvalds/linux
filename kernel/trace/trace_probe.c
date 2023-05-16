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
DEFINE_BASIC_PRINT_TYPE_FUNC(char, u8, "'%c'")

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
	__ASSIGN_FETCH_TYPE("string", string, string, sizeof(u32), 1, 1,
			    "__data_loc char[]"),
	__ASSIGN_FETCH_TYPE("ustring", string, string, sizeof(u32), 1, 1,
			    "__data_loc char[]"),
	__ASSIGN_FETCH_TYPE("symstr", string, string, sizeof(u32), 1, 1,
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
	ASSIGN_FETCH_TYPE_ALIAS(char, u8, u8,  0),
	ASSIGN_FETCH_TYPE_ALIAS(symbol, ADDR_FETCH_TYPE, ADDR_FETCH_TYPE, 0),

	ASSIGN_FETCH_TYPE_END
};

static const struct fetch_type *find_fetch_type(const char *type, unsigned long flags)
{
	int i;

	/* Reject the symbol/symstr for uprobes */
	if (type && (flags & TPARG_FL_USER) &&
	    (!strcmp(type, "symbol") || !strcmp(type, "symstr")))
		return NULL;

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
			return find_fetch_type("u8", flags);
		case 16:
			return find_fetch_type("u16", flags);
		case 32:
			return find_fetch_type("u32", flags);
		case 64:
			return find_fetch_type("u64", flags);
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

	/* Recalculate the length and allocate buffer */
	for (i = 0; i < trace_probe_log.argc; i++) {
		if (i == trace_probe_log.index)
			pos = len;
		len += strlen(trace_probe_log.argv[i]) + 1;
	}
	command = kzalloc(len, GFP_KERNEL);
	if (!command)
		return;

	if (trace_probe_log.index >= trace_probe_log.argc) {
		/**
		 * Set the error position is next to the last arg + space.
		 * Note that len includes the terminal null and the cursor
		 * appears at pos + 1.
		 */
		pos = len;
		offset = 0;
	}

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
	if (!slash)
		slash = strchr(event, '.');

	if (slash) {
		if (slash == event) {
			trace_probe_log_err(offset, NO_GROUP_NAME);
			return -EINVAL;
		}
		if (slash - event + 1 > MAX_EVENT_NAME_LEN) {
			trace_probe_log_err(offset, GROUP_TOO_LONG);
			return -EINVAL;
		}
		strscpy(buf, event, slash - event + 1);
		if (!is_good_system_name(buf)) {
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
		if (slash) {
			*pevent = NULL;
			return 0;
		}
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

	if (flags & TPARG_FL_TPOINT) {
		if (code->data)
			return -EFAULT;
		code->data = kstrdup(arg, GFP_KERNEL);
		if (!code->data)
			return -ENOMEM;
		code->op = FETCH_OP_TP_ARG;
	} else if (strcmp(arg, "retval") == 0) {
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
	} else if (strcmp(arg, "comm") == 0 || strcmp(arg, "COMM") == 0) {
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

static int str_to_immediate(char *str, unsigned long *imm)
{
	if (isdigit(str[0]))
		return kstrtoul(str, 0, imm);
	else if (str[0] == '-')
		return kstrtol(str, 0, (long *)imm);
	else if (str[0] == '+')
		return kstrtol(str + 1, 0, (long *)imm);
	return -EINVAL;
}

static int __parse_imm_string(char *str, char **pbuf, int offs)
{
	size_t len = strlen(str);

	if (str[len - 1] != '"') {
		trace_probe_log_err(offs + len, IMMSTR_NO_CLOSE);
		return -EINVAL;
	}
	*pbuf = kstrndup(str, len - 1, GFP_KERNEL);
	if (!*pbuf)
		return -ENOMEM;
	return 0;
}

/* Recursive argument parser */
static int
parse_probe_arg(char *arg, const struct fetch_type *type,
		struct fetch_insn **pcode, struct fetch_insn *end,
		unsigned int flags, int offs)
{
	struct fetch_insn *code = *pcode;
	unsigned long param;
	int deref = FETCH_OP_DEREF;
	long offset = 0;
	char *tmp;
	int ret = 0;

	switch (arg[0]) {
	case '$':
		ret = parse_probe_vars(arg + 1, type, code, flags, offs);
		break;

	case '%':	/* named register */
		if (flags & TPARG_FL_TPOINT) {
			/* eprobes do not handle registers */
			trace_probe_log_err(offs, BAD_VAR);
			break;
		}
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
	case '-':
		if (arg[1] == 'u') {
			deref = FETCH_OP_UDEREF;
			arg[1] = arg[0];
			arg++;
		}
		if (arg[0] == '+')
			arg++;	/* Skip '+', because kstrtol() rejects it. */
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
			const struct fetch_type *t2 = find_fetch_type(NULL, flags);

			*tmp = '\0';
			ret = parse_probe_arg(arg, t2, &code, end, flags, offs);
			if (ret)
				break;
			if (code->op == FETCH_OP_COMM ||
			    code->op == FETCH_OP_DATA) {
				trace_probe_log_err(offs, COMM_CANT_DEREF);
				return -EINVAL;
			}
			if (++code == end) {
				trace_probe_log_err(offs, TOO_MANY_OPS);
				return -EINVAL;
			}
			*pcode = code;

			code->op = deref;
			code->offset = offset;
		}
		break;
	case '\\':	/* Immediate value */
		if (arg[1] == '"') {	/* Immediate string */
			ret = __parse_imm_string(arg + 2, &tmp, offs + 2);
			if (ret)
				break;
			code->op = FETCH_OP_DATA;
			code->data = tmp;
		} else {
			ret = str_to_immediate(arg + 1, &code->immediate);
			if (ret)
				trace_probe_log_err(offs + 1, BAD_IMM);
			else
				code->op = FETCH_OP_IMM;
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
static int traceprobe_parse_probe_arg_body(const char *argv, ssize_t *size,
		struct probe_arg *parg, unsigned int flags, int offset)
{
	struct fetch_insn *code, *scode, *tmp = NULL;
	char *t, *t2, *t3;
	char *arg;
	int ret, len;

	arg = kstrdup(argv, GFP_KERNEL);
	if (!arg)
		return -ENOMEM;

	ret = -EINVAL;
	len = strlen(arg);
	if (len > MAX_ARGSTR_LEN) {
		trace_probe_log_err(offset, ARG_TOO_LONG);
		goto out;
	} else if (len == 0) {
		trace_probe_log_err(offset, NO_ARG_BODY);
		goto out;
	}

	ret = -ENOMEM;
	parg->comm = kstrdup(arg, GFP_KERNEL);
	if (!parg->comm)
		goto out;

	ret = -EINVAL;
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
				goto out;
			} else if (t3[1] != '\0') {
				trace_probe_log_err(offset + t3 + 1 - arg,
						    BAD_ARRAY_SUFFIX);
				goto out;
			}
			*t3 = '\0';
			if (kstrtouint(t2, 0, &parg->count) || !parg->count) {
				trace_probe_log_err(offset + t2 - arg,
						    BAD_ARRAY_NUM);
				goto out;
			}
			if (parg->count > MAX_ARRAY_LEN) {
				trace_probe_log_err(offset + t2 - arg,
						    ARRAY_TOO_BIG);
				goto out;
			}
		}
	}

	/*
	 * Since $comm and immediate string can not be dereferenced,
	 * we can find those by strcmp. But ignore for eprobes.
	 */
	if (!(flags & TPARG_FL_TPOINT) &&
	    (strcmp(arg, "$comm") == 0 || strcmp(arg, "$COMM") == 0 ||
	     strncmp(arg, "\\\"", 2) == 0)) {
		/* The type of $comm must be "string", and not an array. */
		if (parg->count || (t && strcmp(t, "string")))
			goto out;
		parg->type = find_fetch_type("string", flags);
	} else
		parg->type = find_fetch_type(t, flags);
	if (!parg->type) {
		trace_probe_log_err(offset + (t ? (t - arg) : 0), BAD_TYPE);
		goto out;
	}
	parg->offset = *size;
	*size += parg->type->size * (parg->count ?: 1);

	ret = -ENOMEM;
	if (parg->count) {
		len = strlen(parg->type->fmttype) + 6;
		parg->fmt = kmalloc(len, GFP_KERNEL);
		if (!parg->fmt)
			goto out;
		snprintf(parg->fmt, len, "%s[%d]", parg->type->fmttype,
			 parg->count);
	}

	code = tmp = kcalloc(FETCH_INSN_MAX, sizeof(*code), GFP_KERNEL);
	if (!code)
		goto out;
	code[FETCH_INSN_MAX - 1].op = FETCH_OP_END;

	ret = parse_probe_arg(arg, parg->type, &code, &code[FETCH_INSN_MAX - 1],
			      flags, offset);
	if (ret)
		goto fail;

	ret = -EINVAL;
	/* Store operation */
	if (parg->type->is_string) {
		if (!strcmp(parg->type->name, "symstr")) {
			if (code->op != FETCH_OP_REG && code->op != FETCH_OP_STACK &&
			    code->op != FETCH_OP_RETVAL && code->op != FETCH_OP_ARG &&
			    code->op != FETCH_OP_DEREF && code->op != FETCH_OP_TP_ARG) {
				trace_probe_log_err(offset + (t ? (t - arg) : 0),
						    BAD_SYMSTRING);
				goto fail;
			}
		} else {
			if (code->op != FETCH_OP_DEREF && code->op != FETCH_OP_UDEREF &&
			    code->op != FETCH_OP_IMM && code->op != FETCH_OP_COMM &&
			    code->op != FETCH_OP_DATA && code->op != FETCH_OP_TP_ARG) {
				trace_probe_log_err(offset + (t ? (t - arg) : 0),
						    BAD_STRING);
				goto fail;
			}
		}
		if (!strcmp(parg->type->name, "symstr") ||
		    (code->op == FETCH_OP_IMM || code->op == FETCH_OP_COMM ||
		     code->op == FETCH_OP_DATA) || code->op == FETCH_OP_TP_ARG ||
		     parg->count) {
			/*
			 * IMM, DATA and COMM is pointing actual address, those
			 * must be kept, and if parg->count != 0, this is an
			 * array of string pointers instead of string address
			 * itself.
			 * For the symstr, it doesn't need to dereference, thus
			 * it just get the value.
			 */
			code++;
			if (code->op != FETCH_OP_NOP) {
				trace_probe_log_err(offset, TOO_MANY_OPS);
				goto fail;
			}
		}
		/* If op == DEREF, replace it with STRING */
		if (!strcmp(parg->type->name, "ustring") ||
		    code->op == FETCH_OP_UDEREF)
			code->op = FETCH_OP_ST_USTRING;
		else if (!strcmp(parg->type->name, "symstr"))
			code->op = FETCH_OP_ST_SYMSTR;
		else
			code->op = FETCH_OP_ST_STRING;
		code->size = parg->type->size;
		parg->dynamic = true;
	} else if (code->op == FETCH_OP_DEREF) {
		code->op = FETCH_OP_ST_MEM;
		code->size = parg->type->size;
	} else if (code->op == FETCH_OP_UDEREF) {
		code->op = FETCH_OP_ST_UMEM;
		code->size = parg->type->size;
	} else {
		code++;
		if (code->op != FETCH_OP_NOP) {
			trace_probe_log_err(offset, TOO_MANY_OPS);
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
	ret = -EINVAL;
	/* Loop(Array) operation */
	if (parg->count) {
		if (scode->op != FETCH_OP_ST_MEM &&
		    scode->op != FETCH_OP_ST_STRING &&
		    scode->op != FETCH_OP_ST_USTRING) {
			trace_probe_log_err(offset + (t ? (t - arg) : 0),
					    BAD_STRING);
			goto fail;
		}
		code++;
		if (code->op != FETCH_OP_NOP) {
			trace_probe_log_err(offset, TOO_MANY_OPS);
			goto fail;
		}
		code->op = FETCH_OP_LP_ARRAY;
		code->param = parg->count;
	}
	code++;
	code->op = FETCH_OP_END;

	ret = 0;
	/* Shrink down the code buffer */
	parg->code = kcalloc(code - tmp + 1, sizeof(*code), GFP_KERNEL);
	if (!parg->code)
		ret = -ENOMEM;
	else
		memcpy(parg->code, tmp, sizeof(*code) * (code - tmp + 1));

fail:
	if (ret) {
		for (code = tmp; code < tmp + FETCH_INSN_MAX; code++)
			if (code->op == FETCH_NOP_SYMBOL ||
			    code->op == FETCH_OP_DATA)
				kfree(code->data);
	}
	kfree(tmp);
out:
	kfree(arg);

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

int traceprobe_parse_probe_arg(struct trace_probe *tp, int i, const char *arg,
				unsigned int flags)
{
	struct probe_arg *parg = &tp->args[i];
	const char *body;

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
		if (code->op == FETCH_NOP_SYMBOL ||
		    code->op == FETCH_OP_DATA)
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
			   enum probe_print_type ptype)
{
	struct probe_arg *parg;
	int i, j;
	int pos = 0;
	const char *fmt, *arg;

	switch (ptype) {
	case PROBE_PRINT_NORMAL:
		fmt = "(%lx)";
		arg = ", REC->" FIELD_STRING_IP;
		break;
	case PROBE_PRINT_RETURN:
		fmt = "(%lx <- %lx)";
		arg = ", REC->" FIELD_STRING_FUNC ", REC->" FIELD_STRING_RETIP;
		break;
	case PROBE_PRINT_EVENT:
		fmt = "";
		arg = "";
		break;
	default:
		WARN_ON_ONCE(1);
		return 0;
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

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"%s", arg);

	for (i = 0; i < tp->nr_args; i++) {
		parg = tp->args + i;
		if (parg->count) {
			if (parg->type->is_string)
				fmt = ", __get_str(%s[%d])";
			else
				fmt = ", REC->%s[%d]";
			for (j = 0; j < parg->count; j++)
				pos += snprintf(buf + pos, LEN_OR_ZERO,
						fmt, parg->name, j);
		} else {
			if (parg->type->is_string)
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

int traceprobe_set_print_fmt(struct trace_probe *tp, enum probe_print_type ptype)
{
	struct trace_event_call *call = trace_probe_event_call(tp);
	int len;
	char *print_fmt;

	/* First: called with 0 length to calculate the needed length */
	len = __set_print_fmt(tp, NULL, 0, ptype);
	print_fmt = kmalloc(len + 1, GFP_KERNEL);
	if (!print_fmt)
		return -ENOMEM;

	/* Second: actually write the @print_fmt */
	__set_print_fmt(tp, print_fmt, len + 1, ptype);
	call->print_fmt = print_fmt;

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

static void trace_probe_event_free(struct trace_probe_event *tpe)
{
	kfree(tpe->class.system);
	kfree(tpe->call.name);
	kfree(tpe->call.print_fmt);
	kfree(tpe);
}

int trace_probe_append(struct trace_probe *tp, struct trace_probe *to)
{
	if (trace_probe_has_sibling(tp))
		return -EBUSY;

	list_del_init(&tp->list);
	trace_probe_event_free(tp->event);

	tp->event = to->event;
	list_add_tail(&tp->list, trace_probe_probe_list(to));

	return 0;
}

void trace_probe_unlink(struct trace_probe *tp)
{
	list_del_init(&tp->list);
	if (list_empty(trace_probe_probe_list(tp)))
		trace_probe_event_free(tp->event);
	tp->event = NULL;
}

void trace_probe_cleanup(struct trace_probe *tp)
{
	int i;

	for (i = 0; i < tp->nr_args; i++)
		traceprobe_free_probe_arg(&tp->args[i]);

	if (tp->event)
		trace_probe_unlink(tp);
}

int trace_probe_init(struct trace_probe *tp, const char *event,
		     const char *group, bool alloc_filter)
{
	struct trace_event_call *call;
	size_t size = sizeof(struct trace_probe_event);
	int ret = 0;

	if (!event || !group)
		return -EINVAL;

	if (alloc_filter)
		size += sizeof(struct trace_uprobe_filter);

	tp->event = kzalloc(size, GFP_KERNEL);
	if (!tp->event)
		return -ENOMEM;

	INIT_LIST_HEAD(&tp->event->files);
	INIT_LIST_HEAD(&tp->event->class.fields);
	INIT_LIST_HEAD(&tp->event->probes);
	INIT_LIST_HEAD(&tp->list);
	list_add(&tp->list, &tp->event->probes);

	call = trace_probe_event_call(tp);
	call->class = &tp->event->class;
	call->name = kstrdup(event, GFP_KERNEL);
	if (!call->name) {
		ret = -ENOMEM;
		goto error;
	}

	tp->event->class.system = kstrdup(group, GFP_KERNEL);
	if (!tp->event->class.system) {
		ret = -ENOMEM;
		goto error;
	}

	return 0;

error:
	trace_probe_cleanup(tp);
	return ret;
}

static struct trace_event_call *
find_trace_event_call(const char *system, const char *event_name)
{
	struct trace_event_call *tp_event;
	const char *name;

	list_for_each_entry(tp_event, &ftrace_events, list) {
		if (!tp_event->class->system ||
		    strcmp(system, tp_event->class->system))
			continue;
		name = trace_event_name(tp_event);
		if (!name || strcmp(event_name, name))
			continue;
		return tp_event;
	}

	return NULL;
}

int trace_probe_register_event_call(struct trace_probe *tp)
{
	struct trace_event_call *call = trace_probe_event_call(tp);
	int ret;

	lockdep_assert_held(&event_mutex);

	if (find_trace_event_call(trace_probe_group_name(tp),
				  trace_probe_name(tp)))
		return -EEXIST;

	ret = register_trace_event(&call->event);
	if (!ret)
		return -ENODEV;

	ret = trace_add_event_call(call);
	if (ret)
		unregister_trace_event(&call->event);

	return ret;
}

int trace_probe_add_file(struct trace_probe *tp, struct trace_event_file *file)
{
	struct event_file_link *link;

	link = kmalloc(sizeof(*link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	link->file = file;
	INIT_LIST_HEAD(&link->list);
	list_add_tail_rcu(&link->list, &tp->event->files);
	trace_probe_set_flag(tp, TP_FLAG_TRACE);
	return 0;
}

struct event_file_link *trace_probe_get_file_link(struct trace_probe *tp,
						  struct trace_event_file *file)
{
	struct event_file_link *link;

	trace_probe_for_each_link(link, tp) {
		if (link->file == file)
			return link;
	}

	return NULL;
}

int trace_probe_remove_file(struct trace_probe *tp,
			    struct trace_event_file *file)
{
	struct event_file_link *link;

	link = trace_probe_get_file_link(tp, file);
	if (!link)
		return -ENOENT;

	list_del_rcu(&link->list);
	kvfree_rcu_mightsleep(link);

	if (list_empty(&tp->event->files))
		trace_probe_clear_flag(tp, TP_FLAG_TRACE);

	return 0;
}

/*
 * Return the smallest index of different type argument (start from 1).
 * If all argument types and name are same, return 0.
 */
int trace_probe_compare_arg_type(struct trace_probe *a, struct trace_probe *b)
{
	int i;

	/* In case of more arguments */
	if (a->nr_args < b->nr_args)
		return a->nr_args + 1;
	if (a->nr_args > b->nr_args)
		return b->nr_args + 1;

	for (i = 0; i < a->nr_args; i++) {
		if ((b->nr_args <= i) ||
		    ((a->args[i].type != b->args[i].type) ||
		     (a->args[i].count != b->args[i].count) ||
		     strcmp(a->args[i].name, b->args[i].name)))
			return i + 1;
	}

	return 0;
}

bool trace_probe_match_command_args(struct trace_probe *tp,
				    int argc, const char **argv)
{
	char buf[MAX_ARGSTR_LEN + 1];
	int i;

	if (tp->nr_args < argc)
		return false;

	for (i = 0; i < argc; i++) {
		snprintf(buf, sizeof(buf), "%s=%s",
			 tp->args[i].name, tp->args[i].comm);
		if (strcmp(buf, argv[i]))
			return false;
	}
	return true;
}

int trace_probe_create(const char *raw_command, int (*createfn)(int, const char **))
{
	int argc = 0, ret = 0;
	char **argv;

	argv = argv_split(GFP_KERNEL, raw_command, &argc);
	if (!argv)
		return -ENOMEM;

	if (argc)
		ret = createfn(argc, (const char **)argv);

	argv_free(argv);

	return ret;
}

int trace_probe_print_args(struct trace_seq *s, struct probe_arg *args, int nr_args,
		 u8 *data, void *field)
{
	void *p;
	int i, j;

	for (i = 0; i < nr_args; i++) {
		struct probe_arg *a = args + i;

		trace_seq_printf(s, " %s=", a->name);
		if (likely(!a->count)) {
			if (!a->type->print(s, data + a->offset, field))
				return -ENOMEM;
			continue;
		}
		trace_seq_putc(s, '{');
		p = data + a->offset;
		for (j = 0; j < a->count; j++) {
			if (!a->type->print(s, p, field))
				return -ENOMEM;
			trace_seq_putc(s, j == a->count - 1 ? '}' : ',');
			p += a->type->size;
		}
	}
	return 0;
}
