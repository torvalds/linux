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

#include <linux/bpf.h>
#include <linux/fs.h>

#include "trace_btf.h"
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
		trace_seq_puts(s, FAULT_STRING);
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
extern struct mutex dyn_event_ops_mutex;

void trace_probe_log_init(const char *subsystem, int argc, const char **argv)
{
	lockdep_assert_held(&dyn_event_ops_mutex);

	trace_probe_log.subsystem = subsystem;
	trace_probe_log.argc = argc;
	trace_probe_log.argv = argv;
	trace_probe_log.index = 0;
}

void trace_probe_log_clear(void)
{
	lockdep_assert_held(&dyn_event_ops_mutex);

	memset(&trace_probe_log, 0, sizeof(trace_probe_log));
}

void trace_probe_log_set_index(int index)
{
	lockdep_assert_held(&dyn_event_ops_mutex);

	trace_probe_log.index = index;
}

void __trace_probe_log_err(int offset, int err_type)
{
	char *command, *p;
	int i, len = 0, pos = 0;

	lockdep_assert_held(&dyn_event_ops_mutex);

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

/**
 * traceprobe_parse_event_name() - Parse a string into group and event names
 * @pevent: A pointer to the string to be parsed.
 * @pgroup: A pointer to the group name.
 * @buf:    A buffer to store the parsed group name.
 * @offset: The offset of the string in the original user command, for logging.
 *
 * This parses a string with the format `[GROUP/][EVENT]` or `[GROUP.][EVENT]`
 * (either GROUP or EVENT or both must be specified).
 * Since the parsed group name is stored in @buf, the caller must ensure @buf
 * is at least MAX_EVENT_NAME_LEN bytes.
 *
 * Return: 0 on success, or -EINVAL on failure.
 *
 * If success, *@pevent is updated to point to the event name part of the
 * original string, or NULL if there is no event name.
 * Also, *@pgroup is updated to point to the parsed group which is stored
 * in @buf, or NULL if there is no group name.
 */
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
	} else if (len >= MAX_EVENT_NAME_LEN) {
		trace_probe_log_err(offset, EVENT_TOO_LONG);
		return -EINVAL;
	}
	if (!is_good_name(event)) {
		trace_probe_log_err(offset, BAD_EVENT_NAME);
		return -EINVAL;
	}
	return 0;
}

static int parse_trace_event_arg(char *arg, struct fetch_insn *code,
				 struct traceprobe_parse_context *ctx)
{
	struct ftrace_event_field *field;
	struct list_head *head;

	head = trace_get_fields(ctx->event);
	list_for_each_entry(field, head, link) {
		if (!strcmp(arg, field->name)) {
			code->op = FETCH_OP_TP_ARG;
			code->data = field;
			return 0;
		}
	}
	return -ENOENT;
}

#ifdef CONFIG_PROBE_EVENTS_BTF_ARGS

static u32 btf_type_int(const struct btf_type *t)
{
	return *(u32 *)(t + 1);
}

static bool btf_type_is_char_ptr(struct btf *btf, const struct btf_type *type)
{
	const struct btf_type *real_type;
	u32 intdata;
	s32 tid;

	real_type = btf_type_skip_modifiers(btf, type->type, &tid);
	if (!real_type)
		return false;

	if (BTF_INFO_KIND(real_type->info) != BTF_KIND_INT)
		return false;

	intdata = btf_type_int(real_type);
	return !(BTF_INT_ENCODING(intdata) & BTF_INT_SIGNED)
		&& BTF_INT_BITS(intdata) == 8;
}

static bool btf_type_is_char_array(struct btf *btf, const struct btf_type *type)
{
	const struct btf_type *real_type;
	const struct btf_array *array;
	u32 intdata;
	s32 tid;

	if (BTF_INFO_KIND(type->info) != BTF_KIND_ARRAY)
		return false;

	array = (const struct btf_array *)(type + 1);

	real_type = btf_type_skip_modifiers(btf, array->type, &tid);

	intdata = btf_type_int(real_type);
	return !(BTF_INT_ENCODING(intdata) & BTF_INT_SIGNED)
		&& BTF_INT_BITS(intdata) == 8;
}

static int check_prepare_btf_string_fetch(char *typename,
				struct fetch_insn **pcode,
				struct traceprobe_parse_context *ctx)
{
	struct btf *btf = ctx->btf;

	if (!btf || !ctx->last_type)
		return 0;

	/* char [] does not need any change. */
	if (btf_type_is_char_array(btf, ctx->last_type))
		return 0;

	/* char * requires dereference the pointer. */
	if (btf_type_is_char_ptr(btf, ctx->last_type)) {
		struct fetch_insn *code = *pcode + 1;

		if (code->op == FETCH_OP_END) {
			trace_probe_log_err(ctx->offset, TOO_MANY_OPS);
			return -E2BIG;
		}
		if (typename[0] == 'u')
			code->op = FETCH_OP_UDEREF;
		else
			code->op = FETCH_OP_DEREF;
		code->offset = 0;
		*pcode = code;
		return 0;
	}
	/* Other types are not available for string */
	trace_probe_log_err(ctx->offset, BAD_TYPE4STR);
	return -EINVAL;
}

static const char *fetch_type_from_btf_type(struct btf *btf,
					const struct btf_type *type,
					struct traceprobe_parse_context *ctx)
{
	u32 intdata;

	/* TODO: const char * could be converted as a string */
	switch (BTF_INFO_KIND(type->info)) {
	case BTF_KIND_ENUM:
		/* enum is "int", so convert to "s32" */
		return "s32";
	case BTF_KIND_ENUM64:
		return "s64";
	case BTF_KIND_PTR:
		/* pointer will be converted to "x??" */
		if (IS_ENABLED(CONFIG_64BIT))
			return "x64";
		else
			return "x32";
	case BTF_KIND_INT:
		intdata = btf_type_int(type);
		if (BTF_INT_ENCODING(intdata) & BTF_INT_SIGNED) {
			switch (BTF_INT_BITS(intdata)) {
			case 8:
				return "s8";
			case 16:
				return "s16";
			case 32:
				return "s32";
			case 64:
				return "s64";
			}
		} else {	/* unsigned */
			switch (BTF_INT_BITS(intdata)) {
			case 8:
				return "u8";
			case 16:
				return "u16";
			case 32:
				return "u32";
			case 64:
				return "u64";
			}
			/* bitfield, size is encoded in the type */
			ctx->last_bitsize = BTF_INT_BITS(intdata);
			ctx->last_bitoffs += BTF_INT_OFFSET(intdata);
			return "u64";
		}
	}
	/* TODO: support other types */

	return NULL;
}

static int query_btf_context(struct traceprobe_parse_context *ctx)
{
	const struct btf_param *param;
	const struct btf_type *type;
	struct btf *btf;
	s32 nr;

	if (ctx->btf)
		return 0;

	if (!ctx->funcname)
		return -EINVAL;

	type = btf_find_func_proto(ctx->funcname, &btf);
	if (!type)
		return -ENOENT;

	ctx->btf = btf;
	ctx->proto = type;

	/* ctx->params is optional, since func(void) will not have params. */
	nr = 0;
	param = btf_get_func_param(type, &nr);
	if (!IS_ERR_OR_NULL(param)) {
		/* Hide the first 'data' argument of tracepoint */
		if (ctx->flags & TPARG_FL_TPOINT) {
			nr--;
			param++;
		}
	}

	if (nr > 0) {
		ctx->nr_params = nr;
		ctx->params = param;
	} else {
		ctx->nr_params = 0;
		ctx->params = NULL;
	}

	return 0;
}

static void clear_btf_context(struct traceprobe_parse_context *ctx)
{
	if (ctx->btf) {
		btf_put(ctx->btf);
		ctx->btf = NULL;
		ctx->proto = NULL;
		ctx->params = NULL;
		ctx->nr_params = 0;
	}
}

/* Return 1 if the field separater is arrow operator ('->') */
static int split_next_field(char *varname, char **next_field,
			    struct traceprobe_parse_context *ctx)
{
	char *field;
	int ret = 0;

	field = strpbrk(varname, ".-");
	if (field) {
		if (field[0] == '-' && field[1] == '>') {
			field[0] = '\0';
			field += 2;
			ret = 1;
		} else if (field[0] == '.') {
			field[0] = '\0';
			field += 1;
		} else {
			trace_probe_log_err(ctx->offset + field - varname, BAD_HYPHEN);
			return -EINVAL;
		}
		*next_field = field;
	}

	return ret;
}

/*
 * Parse the field of data structure. The @type must be a pointer type
 * pointing the target data structure type.
 */
static int parse_btf_field(char *fieldname, const struct btf_type *type,
			   struct fetch_insn **pcode, struct fetch_insn *end,
			   struct traceprobe_parse_context *ctx)
{
	struct fetch_insn *code = *pcode;
	const struct btf_member *field;
	u32 bitoffs, anon_offs;
	char *next;
	int is_ptr;
	s32 tid;

	do {
		/* Outer loop for solving arrow operator ('->') */
		if (BTF_INFO_KIND(type->info) != BTF_KIND_PTR) {
			trace_probe_log_err(ctx->offset, NO_PTR_STRCT);
			return -EINVAL;
		}
		/* Convert a struct pointer type to a struct type */
		type = btf_type_skip_modifiers(ctx->btf, type->type, &tid);
		if (!type) {
			trace_probe_log_err(ctx->offset, BAD_BTF_TID);
			return -EINVAL;
		}

		bitoffs = 0;
		do {
			/* Inner loop for solving dot operator ('.') */
			next = NULL;
			is_ptr = split_next_field(fieldname, &next, ctx);
			if (is_ptr < 0)
				return is_ptr;

			anon_offs = 0;
			field = btf_find_struct_member(ctx->btf, type, fieldname,
						       &anon_offs);
			if (IS_ERR(field)) {
				trace_probe_log_err(ctx->offset, BAD_BTF_TID);
				return PTR_ERR(field);
			}
			if (!field) {
				trace_probe_log_err(ctx->offset, NO_BTF_FIELD);
				return -ENOENT;
			}
			/* Add anonymous structure/union offset */
			bitoffs += anon_offs;

			/* Accumulate the bit-offsets of the dot-connected fields */
			if (btf_type_kflag(type)) {
				bitoffs += BTF_MEMBER_BIT_OFFSET(field->offset);
				ctx->last_bitsize = BTF_MEMBER_BITFIELD_SIZE(field->offset);
			} else {
				bitoffs += field->offset;
				ctx->last_bitsize = 0;
			}

			type = btf_type_skip_modifiers(ctx->btf, field->type, &tid);
			if (!type) {
				trace_probe_log_err(ctx->offset, BAD_BTF_TID);
				return -EINVAL;
			}

			ctx->offset += next - fieldname;
			fieldname = next;
		} while (!is_ptr && fieldname);

		if (++code == end) {
			trace_probe_log_err(ctx->offset, TOO_MANY_OPS);
			return -EINVAL;
		}
		code->op = FETCH_OP_DEREF;	/* TODO: user deref support */
		code->offset = bitoffs / 8;
		*pcode = code;

		ctx->last_bitoffs = bitoffs % 8;
		ctx->last_type = type;
	} while (fieldname);

	return 0;
}

static int __store_entry_arg(struct trace_probe *tp, int argnum);

static int parse_btf_arg(char *varname,
			 struct fetch_insn **pcode, struct fetch_insn *end,
			 struct traceprobe_parse_context *ctx)
{
	struct fetch_insn *code = *pcode;
	const struct btf_param *params;
	const struct btf_type *type;
	char *field = NULL;
	int i, is_ptr, ret;
	u32 tid;

	if (WARN_ON_ONCE(!ctx->funcname))
		return -EINVAL;

	is_ptr = split_next_field(varname, &field, ctx);
	if (is_ptr < 0)
		return is_ptr;
	if (!is_ptr && field) {
		/* dot-connected field on an argument is not supported. */
		trace_probe_log_err(ctx->offset + field - varname,
				    NOSUP_DAT_ARG);
		return -EOPNOTSUPP;
	}

	if (ctx->flags & TPARG_FL_RETURN && !strcmp(varname, "$retval")) {
		code->op = FETCH_OP_RETVAL;
		/* Check whether the function return type is not void */
		if (query_btf_context(ctx) == 0) {
			if (ctx->proto->type == 0) {
				trace_probe_log_err(ctx->offset, NO_RETVAL);
				return -ENOENT;
			}
			tid = ctx->proto->type;
			goto found;
		}
		if (field) {
			trace_probe_log_err(ctx->offset + field - varname,
					    NO_BTF_ENTRY);
			return -ENOENT;
		}
		return 0;
	}

	if (!ctx->btf) {
		ret = query_btf_context(ctx);
		if (ret < 0 || ctx->nr_params == 0) {
			trace_probe_log_err(ctx->offset, NO_BTF_ENTRY);
			return -ENOENT;
		}
	}
	params = ctx->params;

	for (i = 0; i < ctx->nr_params; i++) {
		const char *name = btf_name_by_offset(ctx->btf, params[i].name_off);

		if (name && !strcmp(name, varname)) {
			if (tparg_is_function_entry(ctx->flags)) {
				code->op = FETCH_OP_ARG;
				if (ctx->flags & TPARG_FL_TPOINT)
					code->param = i + 1;
				else
					code->param = i;
			} else if (tparg_is_function_return(ctx->flags)) {
				code->op = FETCH_OP_EDATA;
				ret = __store_entry_arg(ctx->tp, i);
				if (ret < 0) {
					/* internal error */
					return ret;
				}
				code->offset = ret;
			}
			tid = params[i].type;
			goto found;
		}
	}
	trace_probe_log_err(ctx->offset, NO_BTFARG);
	return -ENOENT;

found:
	type = btf_type_skip_modifiers(ctx->btf, tid, &tid);
	if (!type) {
		trace_probe_log_err(ctx->offset, BAD_BTF_TID);
		return -EINVAL;
	}
	/* Initialize the last type information */
	ctx->last_type = type;
	ctx->last_bitoffs = 0;
	ctx->last_bitsize = 0;
	if (field) {
		ctx->offset += field - varname;
		return parse_btf_field(field, type, pcode, end, ctx);
	}
	return 0;
}

static const struct fetch_type *find_fetch_type_from_btf_type(
					struct traceprobe_parse_context *ctx)
{
	struct btf *btf = ctx->btf;
	const char *typestr = NULL;

	if (btf && ctx->last_type)
		typestr = fetch_type_from_btf_type(btf, ctx->last_type, ctx);

	return find_fetch_type(typestr, ctx->flags);
}

static int parse_btf_bitfield(struct fetch_insn **pcode,
			      struct traceprobe_parse_context *ctx)
{
	struct fetch_insn *code = *pcode;

	if ((ctx->last_bitsize % 8 == 0) && ctx->last_bitoffs == 0)
		return 0;

	code++;
	if (code->op != FETCH_OP_NOP) {
		trace_probe_log_err(ctx->offset, TOO_MANY_OPS);
		return -EINVAL;
	}
	*pcode = code;

	code->op = FETCH_OP_MOD_BF;
	code->lshift = 64 - (ctx->last_bitsize + ctx->last_bitoffs);
	code->rshift = 64 - ctx->last_bitsize;
	code->basesize = 64 / 8;
	return 0;
}

#else
static void clear_btf_context(struct traceprobe_parse_context *ctx)
{
	ctx->btf = NULL;
}

static int query_btf_context(struct traceprobe_parse_context *ctx)
{
	return -EOPNOTSUPP;
}

static int parse_btf_arg(char *varname,
			 struct fetch_insn **pcode, struct fetch_insn *end,
			 struct traceprobe_parse_context *ctx)
{
	trace_probe_log_err(ctx->offset, NOSUP_BTFARG);
	return -EOPNOTSUPP;
}

static int parse_btf_bitfield(struct fetch_insn **pcode,
			      struct traceprobe_parse_context *ctx)
{
	trace_probe_log_err(ctx->offset, NOSUP_BTFARG);
	return -EOPNOTSUPP;
}

#define find_fetch_type_from_btf_type(ctx)		\
	find_fetch_type(NULL, ctx->flags)

static int check_prepare_btf_string_fetch(char *typename,
				struct fetch_insn **pcode,
				struct traceprobe_parse_context *ctx)
{
	return 0;
}

#endif

#ifdef CONFIG_HAVE_FUNCTION_ARG_ACCESS_API

static void store_entry_arg_at(struct fetch_insn *code, int argnum, int offset)
{
	code[0].op = FETCH_OP_ARG;
	code[0].param = argnum;
	code[1].op = FETCH_OP_ST_EDATA;
	code[1].offset = offset;
}

static int get_entry_arg_max_offset(struct probe_entry_arg *earg)
{
	int i, max_offset = 0;

	/*
	 * earg->code[] array has an operation sequence which is run in
	 * the entry handler.
	 * The sequence stopped by FETCH_OP_END and each data stored in
	 * the entry data buffer by FETCH_OP_ST_EDATA. The FETCH_OP_ST_EDATA
	 * stores the data at the data buffer + its offset, and all data are
	 * "unsigned long" size. The offset must be increased when a data is
	 * stored. Thus we need to find the last FETCH_OP_ST_EDATA in the
	 * code array.
	 */
	for (i = 0; i < earg->size - 1 && earg->code[i].op != FETCH_OP_END; i++) {
		if (earg->code[i].op == FETCH_OP_ST_EDATA)
			if (earg->code[i].offset > max_offset)
				max_offset = earg->code[i].offset;
	}
	return max_offset;
}

/*
 * Add the entry code to store the 'argnum'th parameter and return the offset
 * in the entry data buffer where the data will be stored.
 */
static int __store_entry_arg(struct trace_probe *tp, int argnum)
{
	struct probe_entry_arg *earg = tp->entry_arg;
	int i, offset, last_offset = 0;

	if (!earg) {
		earg = kzalloc(sizeof(*tp->entry_arg), GFP_KERNEL);
		if (!earg)
			return -ENOMEM;
		earg->size = 2 * tp->nr_args + 1;
		earg->code = kcalloc(earg->size, sizeof(struct fetch_insn),
				     GFP_KERNEL);
		if (!earg->code) {
			kfree(earg);
			return -ENOMEM;
		}
		/* Fill the code buffer with 'end' to simplify it */
		for (i = 0; i < earg->size; i++)
			earg->code[i].op = FETCH_OP_END;
		tp->entry_arg = earg;
		store_entry_arg_at(earg->code, argnum, 0);
		return 0;
	}

	/*
	 * NOTE: if anyone change the following rule, please rewrite this.
	 * The entry code array is filled with the pair of
	 *
	 * [FETCH_OP_ARG(argnum)]
	 * [FETCH_OP_ST_EDATA(offset of entry data buffer)]
	 *
	 * and the rest of entries are filled with [FETCH_OP_END].
	 * The offset should be incremented, thus the last pair should
	 * have the largest offset.
	 */

	/* Search the offset for the sprcified argnum. */
	for (i = 0; i < earg->size - 1 && earg->code[i].op != FETCH_OP_END; i += 2) {
		if (WARN_ON_ONCE(earg->code[i].op != FETCH_OP_ARG))
			return -EINVAL;

		if (earg->code[i].param != argnum)
			continue;

		if (WARN_ON_ONCE(earg->code[i + 1].op != FETCH_OP_ST_EDATA))
			return -EINVAL;

		return earg->code[i + 1].offset;
	}
	/* Not found, append new entry if possible. */
	if (i >= earg->size - 1)
		return -ENOSPC;

	/* The last entry must have the largest offset. */
	if (i != 0) {
		if (WARN_ON_ONCE(earg->code[i - 1].op != FETCH_OP_ST_EDATA))
			return -EINVAL;
		last_offset = earg->code[i - 1].offset;
	}

	offset = last_offset + sizeof(unsigned long);
	store_entry_arg_at(&earg->code[i], argnum, offset);
	return offset;
}

int traceprobe_get_entry_data_size(struct trace_probe *tp)
{
	struct probe_entry_arg *earg = tp->entry_arg;

	if (!earg)
		return 0;

	return get_entry_arg_max_offset(earg) + sizeof(unsigned long);
}

void store_trace_entry_data(void *edata, struct trace_probe *tp, struct pt_regs *regs)
{
	struct probe_entry_arg *earg = tp->entry_arg;
	unsigned long val = 0;
	int i;

	if (!earg)
		return;

	for (i = 0; i < earg->size; i++) {
		struct fetch_insn *code = &earg->code[i];

		switch (code->op) {
		case FETCH_OP_ARG:
			val = regs_get_kernel_argument(regs, code->param);
			break;
		case FETCH_OP_ST_EDATA:
			*(unsigned long *)((unsigned long)edata + code->offset) = val;
			break;
		case FETCH_OP_END:
			goto end;
		default:
			break;
		}
	}
end:
	return;
}
NOKPROBE_SYMBOL(store_trace_entry_data)
#endif

#define PARAM_MAX_STACK (THREAD_SIZE / sizeof(unsigned long))

/* Parse $vars. @orig_arg points '$', which syncs to @ctx->offset */
static int parse_probe_vars(char *orig_arg, const struct fetch_type *t,
			    struct fetch_insn **pcode,
			    struct fetch_insn *end,
			    struct traceprobe_parse_context *ctx)
{
	struct fetch_insn *code = *pcode;
	int err = TP_ERR_BAD_VAR;
	char *arg = orig_arg + 1;
	unsigned long param;
	int ret = 0;
	int len;

	if (ctx->flags & TPARG_FL_TEVENT) {
		if (code->data)
			return -EFAULT;
		ret = parse_trace_event_arg(arg, code, ctx);
		if (!ret)
			return 0;
		if (strcmp(arg, "comm") == 0 || strcmp(arg, "COMM") == 0) {
			code->op = FETCH_OP_COMM;
			return 0;
		}
		/* backward compatibility */
		ctx->offset = 0;
		goto inval;
	}

	if (str_has_prefix(arg, "retval")) {
		if (!(ctx->flags & TPARG_FL_RETURN)) {
			err = TP_ERR_RETVAL_ON_PROBE;
			goto inval;
		}
		if (!(ctx->flags & TPARG_FL_KERNEL) ||
		    !IS_ENABLED(CONFIG_PROBE_EVENTS_BTF_ARGS)) {
			code->op = FETCH_OP_RETVAL;
			return 0;
		}
		return parse_btf_arg(orig_arg, pcode, end, ctx);
	}

	len = str_has_prefix(arg, "stack");
	if (len) {

		if (arg[len] == '\0') {
			code->op = FETCH_OP_STACKP;
			return 0;
		}

		if (isdigit(arg[len])) {
			ret = kstrtoul(arg + len, 10, &param);
			if (ret)
				goto inval;

			if ((ctx->flags & TPARG_FL_KERNEL) &&
			    param > PARAM_MAX_STACK) {
				err = TP_ERR_BAD_STACK_NUM;
				goto inval;
			}
			code->op = FETCH_OP_STACK;
			code->param = (unsigned int)param;
			return 0;
		}
		goto inval;
	}

	if (strcmp(arg, "comm") == 0 || strcmp(arg, "COMM") == 0) {
		code->op = FETCH_OP_COMM;
		return 0;
	}

#ifdef CONFIG_HAVE_FUNCTION_ARG_ACCESS_API
	len = str_has_prefix(arg, "arg");
	if (len) {
		ret = kstrtoul(arg + len, 10, &param);
		if (ret)
			goto inval;

		if (!param || param > PARAM_MAX_STACK) {
			err = TP_ERR_BAD_ARG_NUM;
			goto inval;
		}
		param--; /* argN starts from 1, but internal arg[N] starts from 0 */

		if (tparg_is_function_entry(ctx->flags)) {
			code->op = FETCH_OP_ARG;
			code->param = (unsigned int)param;
			/*
			 * The tracepoint probe will probe a stub function, and the
			 * first parameter of the stub is a dummy and should be ignored.
			 */
			if (ctx->flags & TPARG_FL_TPOINT)
				code->param++;
		} else if (tparg_is_function_return(ctx->flags)) {
			/* function entry argument access from return probe */
			ret = __store_entry_arg(ctx->tp, param);
			if (ret < 0)	/* This error should be an internal error */
				return ret;

			code->op = FETCH_OP_EDATA;
			code->offset = ret;
		} else {
			err = TP_ERR_NOFENTRY_ARGS;
			goto inval;
		}
		return 0;
	}
#endif

inval:
	__trace_probe_log_err(ctx->offset, err);
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
		struct traceprobe_parse_context *ctx)
{
	struct fetch_insn *code = *pcode;
	unsigned long param;
	int deref = FETCH_OP_DEREF;
	long offset = 0;
	char *tmp;
	int ret = 0;

	switch (arg[0]) {
	case '$':
		ret = parse_probe_vars(arg, type, pcode, end, ctx);
		break;

	case '%':	/* named register */
		if (ctx->flags & (TPARG_FL_TEVENT | TPARG_FL_FPROBE)) {
			/* eprobe and fprobe do not handle registers */
			trace_probe_log_err(ctx->offset, BAD_VAR);
			break;
		}
		ret = regs_query_register_offset(arg + 1);
		if (ret >= 0) {
			code->op = FETCH_OP_REG;
			code->param = (unsigned int)ret;
			ret = 0;
		} else
			trace_probe_log_err(ctx->offset, BAD_REG_NAME);
		break;

	case '@':	/* memory, file-offset or symbol */
		if (isdigit(arg[1])) {
			ret = kstrtoul(arg + 1, 0, &param);
			if (ret) {
				trace_probe_log_err(ctx->offset, BAD_MEM_ADDR);
				break;
			}
			/* load address */
			code->op = FETCH_OP_IMM;
			code->immediate = param;
		} else if (arg[1] == '+') {
			/* kprobes don't support file offsets */
			if (ctx->flags & TPARG_FL_KERNEL) {
				trace_probe_log_err(ctx->offset, FILE_ON_KPROBE);
				return -EINVAL;
			}
			ret = kstrtol(arg + 2, 0, &offset);
			if (ret) {
				trace_probe_log_err(ctx->offset, BAD_FILE_OFFS);
				break;
			}

			code->op = FETCH_OP_FOFFS;
			code->immediate = (unsigned long)offset;  // imm64?
		} else {
			/* uprobes don't support symbols */
			if (!(ctx->flags & TPARG_FL_KERNEL)) {
				trace_probe_log_err(ctx->offset, SYM_ON_UPROBE);
				return -EINVAL;
			}
			/* Preserve symbol for updating */
			code->op = FETCH_NOP_SYMBOL;
			code->data = kstrdup(arg + 1, GFP_KERNEL);
			if (!code->data)
				return -ENOMEM;
			if (++code == end) {
				trace_probe_log_err(ctx->offset, TOO_MANY_OPS);
				return -EINVAL;
			}
			code->op = FETCH_OP_IMM;
			code->immediate = 0;
		}
		/* These are fetching from memory */
		if (++code == end) {
			trace_probe_log_err(ctx->offset, TOO_MANY_OPS);
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
			trace_probe_log_err(ctx->offset, DEREF_NEED_BRACE);
			return -EINVAL;
		}
		*tmp = '\0';
		ret = kstrtol(arg, 0, &offset);
		if (ret) {
			trace_probe_log_err(ctx->offset, BAD_DEREF_OFFS);
			break;
		}
		ctx->offset += (tmp + 1 - arg) + (arg[0] != '-' ? 1 : 0);
		arg = tmp + 1;
		tmp = strrchr(arg, ')');
		if (!tmp) {
			trace_probe_log_err(ctx->offset + strlen(arg),
					    DEREF_OPEN_BRACE);
			return -EINVAL;
		} else {
			const struct fetch_type *t2 = find_fetch_type(NULL, ctx->flags);
			int cur_offs = ctx->offset;

			*tmp = '\0';
			ret = parse_probe_arg(arg, t2, &code, end, ctx);
			if (ret)
				break;
			ctx->offset = cur_offs;
			if (code->op == FETCH_OP_COMM ||
			    code->op == FETCH_OP_DATA) {
				trace_probe_log_err(ctx->offset, COMM_CANT_DEREF);
				return -EINVAL;
			}
			if (++code == end) {
				trace_probe_log_err(ctx->offset, TOO_MANY_OPS);
				return -EINVAL;
			}
			*pcode = code;

			code->op = deref;
			code->offset = offset;
			/* Reset the last type if used */
			ctx->last_type = NULL;
		}
		break;
	case '\\':	/* Immediate value */
		if (arg[1] == '"') {	/* Immediate string */
			ret = __parse_imm_string(arg + 2, &tmp, ctx->offset + 2);
			if (ret)
				break;
			code->op = FETCH_OP_DATA;
			code->data = tmp;
		} else {
			ret = str_to_immediate(arg + 1, &code->immediate);
			if (ret)
				trace_probe_log_err(ctx->offset + 1, BAD_IMM);
			else
				code->op = FETCH_OP_IMM;
		}
		break;
	default:
		if (isalpha(arg[0]) || arg[0] == '_') {	/* BTF variable */
			if (!tparg_is_function_entry(ctx->flags) &&
			    !tparg_is_function_return(ctx->flags)) {
				trace_probe_log_err(ctx->offset, NOSUP_BTFARG);
				return -EINVAL;
			}
			ret = parse_btf_arg(arg, pcode, end, ctx);
			break;
		}
	}
	if (!ret && code->op == FETCH_OP_NOP) {
		/* Parsed, but do not find fetch method */
		trace_probe_log_err(ctx->offset, BAD_FETCH_ARG);
		ret = -EINVAL;
	}
	return ret;
}

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

/* Split type part from @arg and return it. */
static char *parse_probe_arg_type(char *arg, struct probe_arg *parg,
				  struct traceprobe_parse_context *ctx)
{
	char *t = NULL, *t2, *t3;
	int offs;

	t = strchr(arg, ':');
	if (t) {
		*t++ = '\0';
		t2 = strchr(t, '[');
		if (t2) {
			*t2++ = '\0';
			t3 = strchr(t2, ']');
			if (!t3) {
				offs = t2 + strlen(t2) - arg;

				trace_probe_log_err(ctx->offset + offs,
						    ARRAY_NO_CLOSE);
				return ERR_PTR(-EINVAL);
			} else if (t3[1] != '\0') {
				trace_probe_log_err(ctx->offset + t3 + 1 - arg,
						    BAD_ARRAY_SUFFIX);
				return ERR_PTR(-EINVAL);
			}
			*t3 = '\0';
			if (kstrtouint(t2, 0, &parg->count) || !parg->count) {
				trace_probe_log_err(ctx->offset + t2 - arg,
						    BAD_ARRAY_NUM);
				return ERR_PTR(-EINVAL);
			}
			if (parg->count > MAX_ARRAY_LEN) {
				trace_probe_log_err(ctx->offset + t2 - arg,
						    ARRAY_TOO_BIG);
				return ERR_PTR(-EINVAL);
			}
		}
	}
	offs = t ? t - arg : 0;

	/*
	 * Since $comm and immediate string can not be dereferenced,
	 * we can find those by strcmp. But ignore for eprobes.
	 */
	if (!(ctx->flags & TPARG_FL_TEVENT) &&
	    (strcmp(arg, "$comm") == 0 || strcmp(arg, "$COMM") == 0 ||
	     strncmp(arg, "\\\"", 2) == 0)) {
		/* The type of $comm must be "string", and not an array type. */
		if (parg->count || (t && strcmp(t, "string"))) {
			trace_probe_log_err(ctx->offset + offs, NEED_STRING_TYPE);
			return ERR_PTR(-EINVAL);
		}
		parg->type = find_fetch_type("string", ctx->flags);
	} else
		parg->type = find_fetch_type(t, ctx->flags);

	if (!parg->type) {
		trace_probe_log_err(ctx->offset + offs, BAD_TYPE);
		return ERR_PTR(-EINVAL);
	}

	return t;
}

/* After parsing, adjust the fetch_insn according to the probe_arg */
static int finalize_fetch_insn(struct fetch_insn *code,
			       struct probe_arg *parg,
			       char *type,
			       int type_offset,
			       struct traceprobe_parse_context *ctx)
{
	struct fetch_insn *scode;
	int ret;

	/* Store operation */
	if (parg->type->is_string) {
		/* Check bad combination of the type and the last fetch_insn. */
		if (!strcmp(parg->type->name, "symstr")) {
			if (code->op != FETCH_OP_REG && code->op != FETCH_OP_STACK &&
			    code->op != FETCH_OP_RETVAL && code->op != FETCH_OP_ARG &&
			    code->op != FETCH_OP_DEREF && code->op != FETCH_OP_TP_ARG) {
				trace_probe_log_err(ctx->offset + type_offset,
						    BAD_SYMSTRING);
				return -EINVAL;
			}
		} else {
			if (code->op != FETCH_OP_DEREF && code->op != FETCH_OP_UDEREF &&
			    code->op != FETCH_OP_IMM && code->op != FETCH_OP_COMM &&
			    code->op != FETCH_OP_DATA && code->op != FETCH_OP_TP_ARG) {
				trace_probe_log_err(ctx->offset + type_offset,
						    BAD_STRING);
				return -EINVAL;
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
				trace_probe_log_err(ctx->offset, TOO_MANY_OPS);
				return -EINVAL;
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
			trace_probe_log_err(ctx->offset, TOO_MANY_OPS);
			return -E2BIG;
		}
		code->op = FETCH_OP_ST_RAW;
		code->size = parg->type->size;
	}

	/* Save storing fetch_insn. */
	scode = code;

	/* Modify operation */
	if (type != NULL) {
		/* Bitfield needs a special fetch_insn. */
		ret = __parse_bitfield_probe_arg(type, parg->type, &code);
		if (ret) {
			trace_probe_log_err(ctx->offset + type_offset, BAD_BITFIELD);
			return ret;
		}
	} else if (IS_ENABLED(CONFIG_PROBE_EVENTS_BTF_ARGS) &&
		   ctx->last_type) {
		/* If user not specified the type, try parsing BTF bitfield. */
		ret = parse_btf_bitfield(&code, ctx);
		if (ret)
			return ret;
	}

	/* Loop(Array) operation */
	if (parg->count) {
		if (scode->op != FETCH_OP_ST_MEM &&
		    scode->op != FETCH_OP_ST_STRING &&
		    scode->op != FETCH_OP_ST_USTRING) {
			trace_probe_log_err(ctx->offset + type_offset, BAD_STRING);
			return -EINVAL;
		}
		code++;
		if (code->op != FETCH_OP_NOP) {
			trace_probe_log_err(ctx->offset, TOO_MANY_OPS);
			return -E2BIG;
		}
		code->op = FETCH_OP_LP_ARRAY;
		code->param = parg->count;
	}

	/* Finalize the fetch_insn array. */
	code++;
	code->op = FETCH_OP_END;

	return 0;
}

/* String length checking wrapper */
static int traceprobe_parse_probe_arg_body(const char *argv, ssize_t *size,
					   struct probe_arg *parg,
					   struct traceprobe_parse_context *ctx)
{
	struct fetch_insn *code, *tmp = NULL;
	char *type, *arg __free(kfree) = NULL;
	int ret, len;

	len = strlen(argv);
	if (len > MAX_ARGSTR_LEN) {
		trace_probe_log_err(ctx->offset, ARG_TOO_LONG);
		return -E2BIG;
	} else if (len == 0) {
		trace_probe_log_err(ctx->offset, NO_ARG_BODY);
		return -EINVAL;
	}

	arg = kstrdup(argv, GFP_KERNEL);
	if (!arg)
		return -ENOMEM;

	parg->comm = kstrdup(arg, GFP_KERNEL);
	if (!parg->comm)
		return -ENOMEM;

	type = parse_probe_arg_type(arg, parg, ctx);
	if (IS_ERR(type))
		return PTR_ERR(type);

	code = tmp = kcalloc(FETCH_INSN_MAX, sizeof(*code), GFP_KERNEL);
	if (!code)
		return -ENOMEM;
	code[FETCH_INSN_MAX - 1].op = FETCH_OP_END;

	ctx->last_type = NULL;
	ret = parse_probe_arg(arg, parg->type, &code, &code[FETCH_INSN_MAX - 1],
			      ctx);
	if (ret < 0)
		goto fail;

	/* Update storing type if BTF is available */
	if (IS_ENABLED(CONFIG_PROBE_EVENTS_BTF_ARGS) &&
	    ctx->last_type) {
		if (!type) {
			parg->type = find_fetch_type_from_btf_type(ctx);
		} else if (strstr(type, "string")) {
			ret = check_prepare_btf_string_fetch(type, &code, ctx);
			if (ret)
				goto fail;
		}
	}
	parg->offset = *size;
	*size += parg->type->size * (parg->count ?: 1);

	if (parg->count) {
		len = strlen(parg->type->fmttype) + 6;
		parg->fmt = kmalloc(len, GFP_KERNEL);
		if (!parg->fmt) {
			ret = -ENOMEM;
			goto fail;
		}
		snprintf(parg->fmt, len, "%s[%d]", parg->type->fmttype,
			 parg->count);
	}

	ret = finalize_fetch_insn(code, parg, type, type ? type - arg : 0, ctx);
	if (ret < 0)
		goto fail;

	for (; code < tmp + FETCH_INSN_MAX; code++)
		if (code->op == FETCH_OP_END)
			break;
	/* Shrink down the code buffer */
	parg->code = kcalloc(code - tmp + 1, sizeof(*code), GFP_KERNEL);
	if (!parg->code)
		ret = -ENOMEM;
	else
		memcpy(parg->code, tmp, sizeof(*code) * (code - tmp + 1));

fail:
	if (ret < 0) {
		for (code = tmp; code < tmp + FETCH_INSN_MAX; code++)
			if (code->op == FETCH_NOP_SYMBOL ||
			    code->op == FETCH_OP_DATA)
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

static char *generate_probe_arg_name(const char *arg, int idx)
{
	char *name = NULL;
	const char *end;

	/*
	 * If argument name is omitted, try arg as a name (BTF variable)
	 * or "argN".
	 */
	if (IS_ENABLED(CONFIG_PROBE_EVENTS_BTF_ARGS)) {
		end = strchr(arg, ':');
		if (!end)
			end = arg + strlen(arg);

		name = kmemdup_nul(arg, end - arg, GFP_KERNEL);
		if (!name || !is_good_name(name)) {
			kfree(name);
			name = NULL;
		}
	}

	if (!name)
		name = kasprintf(GFP_KERNEL, "arg%d", idx + 1);

	return name;
}

int traceprobe_parse_probe_arg(struct trace_probe *tp, int i, const char *arg,
			       struct traceprobe_parse_context *ctx)
{
	struct probe_arg *parg = &tp->args[i];
	const char *body;

	ctx->tp = tp;
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
		parg->name = generate_probe_arg_name(arg, i);
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
	ctx->offset = body - arg;
	/* Parse fetch argument */
	return traceprobe_parse_probe_arg_body(body, &tp->size, parg, ctx);
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

static int argv_has_var_arg(int argc, const char *argv[], int *args_idx,
			    struct traceprobe_parse_context *ctx)
{
	int i, found = 0;

	for (i = 0; i < argc; i++)
		if (str_has_prefix(argv[i], "$arg")) {
			trace_probe_log_set_index(i + 2);

			if (!tparg_is_function_entry(ctx->flags) &&
			    !tparg_is_function_return(ctx->flags)) {
				trace_probe_log_err(0, NOFENTRY_ARGS);
				return -EINVAL;
			}

			if (isdigit(argv[i][4])) {
				found = 1;
				continue;
			}

			if (argv[i][4] != '*') {
				trace_probe_log_err(0, BAD_VAR);
				return -EINVAL;
			}

			if (*args_idx >= 0 && *args_idx < argc) {
				trace_probe_log_err(0, DOUBLE_ARGS);
				return -EINVAL;
			}
			found = 1;
			*args_idx = i;
		}

	return found;
}

static int sprint_nth_btf_arg(int idx, const char *type,
			      char *buf, int bufsize,
			      struct traceprobe_parse_context *ctx)
{
	const char *name;
	int ret;

	if (idx >= ctx->nr_params) {
		trace_probe_log_err(0, NO_BTFARG);
		return -ENOENT;
	}
	name = btf_name_by_offset(ctx->btf, ctx->params[idx].name_off);
	if (!name) {
		trace_probe_log_err(0, NO_BTF_ENTRY);
		return -ENOENT;
	}
	ret = snprintf(buf, bufsize, "%s%s", name, type);
	if (ret >= bufsize) {
		trace_probe_log_err(0, ARGS_2LONG);
		return -E2BIG;
	}
	return ret;
}

/* Return new_argv which must be freed after use */
const char **traceprobe_expand_meta_args(int argc, const char *argv[],
					 int *new_argc, char *buf, int bufsize,
					 struct traceprobe_parse_context *ctx)
{
	const struct btf_param *params = NULL;
	int i, j, n, used, ret, args_idx = -1;
	const char **new_argv __free(kfree) = NULL;

	ret = argv_has_var_arg(argc, argv, &args_idx, ctx);
	if (ret < 0)
		return ERR_PTR(ret);

	if (!ret) {
		*new_argc = argc;
		return NULL;
	}

	ret = query_btf_context(ctx);
	if (ret < 0 || ctx->nr_params == 0) {
		if (args_idx != -1) {
			/* $arg* requires BTF info */
			trace_probe_log_err(0, NOSUP_BTFARG);
			return (const char **)params;
		}
		*new_argc = argc;
		return NULL;
	}

	if (args_idx >= 0)
		*new_argc = argc + ctx->nr_params - 1;
	else
		*new_argc = argc;

	new_argv = kcalloc(*new_argc, sizeof(char *), GFP_KERNEL);
	if (!new_argv)
		return ERR_PTR(-ENOMEM);

	used = 0;
	for (i = 0, j = 0; i < argc; i++) {
		trace_probe_log_set_index(i + 2);
		if (i == args_idx) {
			for (n = 0; n < ctx->nr_params; n++) {
				ret = sprint_nth_btf_arg(n, "", buf + used,
							 bufsize - used, ctx);
				if (ret < 0)
					return ERR_PTR(ret);

				new_argv[j++] = buf + used;
				used += ret + 1;
			}
			continue;
		}

		if (str_has_prefix(argv[i], "$arg")) {
			char *type = NULL;

			n = simple_strtoul(argv[i] + 4, &type, 10);
			if (type && !(*type == ':' || *type == '\0')) {
				trace_probe_log_err(0, BAD_VAR);
				return ERR_PTR(-ENOENT);
			}
			/* Note: $argN starts from $arg1 */
			ret = sprint_nth_btf_arg(n - 1, type, buf + used,
						 bufsize - used, ctx);
			if (ret < 0)
				return ERR_PTR(ret);
			new_argv[j++] = buf + used;
			used += ret + 1;
		} else
			new_argv[j++] = argv[i];
	}

	return_ptr(new_argv);
}

/* @buf: *buf must be equal to NULL. Caller must to free *buf */
int traceprobe_expand_dentry_args(int argc, const char *argv[], char **buf)
{
	int i, used, ret;
	const int bufsize = MAX_DENTRY_ARGS_LEN;
	char *tmpbuf __free(kfree) = NULL;

	if (*buf)
		return -EINVAL;

	used = 0;
	for (i = 0; i < argc; i++) {
		char *tmp __free(kfree) = NULL;
		char *equal;
		size_t arg_len;

		if (!glob_match("*:%p[dD]", argv[i]))
			continue;

		if (!tmpbuf) {
			tmpbuf = kmalloc(bufsize, GFP_KERNEL);
			if (!tmpbuf)
				return -ENOMEM;
		}

		tmp = kstrdup(argv[i], GFP_KERNEL);
		if (!tmp)
			return -ENOMEM;

		equal = strchr(tmp, '=');
		if (equal)
			*equal = '\0';
		arg_len = strlen(argv[i]);
		tmp[arg_len - 4] = '\0';
		if (argv[i][arg_len - 1] == 'd')
			ret = snprintf(tmpbuf + used, bufsize - used,
				       "%s%s+0x0(+0x%zx(%s)):string",
				       equal ? tmp : "", equal ? "=" : "",
				       offsetof(struct dentry, d_name.name),
				       equal ? equal + 1 : tmp);
		else
			ret = snprintf(tmpbuf + used, bufsize - used,
				       "%s%s+0x0(+0x%zx(+0x%zx(%s))):string",
				       equal ? tmp : "", equal ? "=" : "",
				       offsetof(struct dentry, d_name.name),
				       offsetof(struct file, f_path.dentry),
				       equal ? equal + 1 : tmp);

		if (ret >= bufsize - used)
			return -ENOMEM;
		argv[i] = tmpbuf + used;
		used += ret + 1;
	}

	*buf = no_free_ptr(tmpbuf);
	return 0;
}

void traceprobe_finish_parse(struct traceprobe_parse_context *ctx)
{
	clear_btf_context(ctx);
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

	if (tp->entry_arg) {
		kfree(tp->entry_arg->code);
		kfree(tp->entry_arg);
		tp->entry_arg = NULL;
	}

	if (tp->event)
		trace_probe_unlink(tp);
}

int trace_probe_init(struct trace_probe *tp, const char *event,
		     const char *group, bool alloc_filter, int nargs)
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

	tp->nr_args = nargs;
	/* Make sure pointers in args[] are NULL */
	if (nargs)
		memset(tp->args, 0, sizeof(tp->args[0]) * nargs);

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
