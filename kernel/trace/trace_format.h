/*
 * Setup the showing format of trace point.
 *
 * int
 * ftrace_format_##call(struct trace_seq *s)
 * {
 *	struct ftrace_raw_##call field;
 *	int ret;
 *
 *	ret = trace_seq_printf(s, #type " " #item ";"
 *			       " size:%d; offset:%d;\n",
 *			       sizeof(field.type),
 *			       offsetof(struct ftrace_raw_##call,
 *					item));
 *
 * }
 */

#undef TRACE_STRUCT
#define TRACE_STRUCT(args...) args

#undef TRACE_FIELD
#define TRACE_FIELD(type, item, assign)					\
	ret = trace_seq_printf(s, "\tfield:" #type " " #item ";\t"	\
			       "offset:%lu;\tsize:%lu;\n",		\
			       offsetof(typeof(field), item),		\
			       sizeof(field.item));			\
	if (!ret)							\
		return 0;


#undef TRACE_FIELD_SPECIAL
#define TRACE_FIELD_SPECIAL(type_item, item, cmd)			\
	ret = trace_seq_printf(s, "\tfield special:" #type_item ";\t"	\
			       "offset:%lu;\tsize:%lu;\n",		\
			       offsetof(typeof(field), item),		\
			       sizeof(field.item));			\
	if (!ret)							\
		return 0;

#undef TRACE_EVENT_FORMAT
#define TRACE_EVENT_FORMAT(call, proto, args, fmt, tstruct, tpfmt)	\
int									\
ftrace_format_##call(struct trace_seq *s)				\
{									\
	struct ftrace_raw_##call field;					\
	int ret;							\
									\
	tstruct;							\
									\
	trace_seq_printf(s, "\nprint fmt: \"%s\"\n", tpfmt);		\
									\
	return ret;							\
}

