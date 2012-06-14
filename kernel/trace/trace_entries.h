/*
 * This file defines the trace event structures that go into the ring
 * buffer directly. They are created via macros so that changes for them
 * appear in the format file. Using macros will automate this process.
 *
 * The macro used to create a ftrace data structure is:
 *
 * FTRACE_ENTRY( name, struct_name, id, structure, print )
 *
 * @name: the name used the event name, as well as the name of
 *   the directory that holds the format file.
 *
 * @struct_name: the name of the structure that is created.
 *
 * @id: The event identifier that is used to detect what event
 *    this is from the ring buffer.
 *
 * @structure: the structure layout
 *
 *  - __field(	type,	item	)
 *	  This is equivalent to declaring
 *		type	item;
 *	  in the structure.
 *  - __array(	type,	item,	size	)
 *	  This is equivalent to declaring
 *		type	item[size];
 *	  in the structure.
 *
 *   * for structures within structures, the format of the internal
 *	structure is laid out. This allows the internal structure
 *	to be deciphered for the format file. Although these macros
 *	may become out of sync with the internal structure, they
 *	will create a compile error if it happens. Since the
 *	internel structures are just tracing helpers, this is not
 *	an issue.
 *
 *	When an internal structure is used, it should use:
 *
 *	__field_struct(	type,	item	)
 *
 *	instead of __field. This will prevent it from being shown in
 *	the output file. The fields in the structure should use.
 *
 *	__field_desc(	type,	container,	item		)
 *	__array_desc(	type,	container,	item,	len	)
 *
 *	type, item and len are the same as __field and __array, but
 *	container is added. This is the name of the item in
 *	__field_struct that this is describing.
 *
 *
 * @print: the print format shown to users in the format file.
 */

/*
 * Function trace entry - function address and parent function address:
 */
FTRACE_ENTRY_REG(function, ftrace_entry,

	TRACE_FN,

	F_STRUCT(
		__field(	unsigned long,	ip		)
		__field(	unsigned long,	parent_ip	)
	),

	F_printk(" %lx <-- %lx", __entry->ip, __entry->parent_ip),

	FILTER_TRACE_FN,

	perf_ftrace_event_register
);

/* Function call entry */
FTRACE_ENTRY(funcgraph_entry, ftrace_graph_ent_entry,

	TRACE_GRAPH_ENT,

	F_STRUCT(
		__field_struct(	struct ftrace_graph_ent,	graph_ent	)
		__field_desc(	unsigned long,	graph_ent,	func		)
		__field_desc(	int,		graph_ent,	depth		)
	),

	F_printk("--> %lx (%d)", __entry->func, __entry->depth),

	FILTER_OTHER
);

/* Function return entry */
FTRACE_ENTRY(funcgraph_exit, ftrace_graph_ret_entry,

	TRACE_GRAPH_RET,

	F_STRUCT(
		__field_struct(	struct ftrace_graph_ret,	ret	)
		__field_desc(	unsigned long,	ret,		func	)
		__field_desc(	unsigned long long, ret,	calltime)
		__field_desc(	unsigned long long, ret,	rettime	)
		__field_desc(	unsigned long,	ret,		overrun	)
		__field_desc(	int,		ret,		depth	)
	),

	F_printk("<-- %lx (%d) (start: %llx  end: %llx) over: %d",
		 __entry->func, __entry->depth,
		 __entry->calltime, __entry->rettime,
		 __entry->depth),

	FILTER_OTHER
);

/*
 * Context switch trace entry - which task (and prio) we switched from/to:
 *
 * This is used for both wakeup and context switches. We only want
 * to create one structure, but we need two outputs for it.
 */
#define FTRACE_CTX_FIELDS					\
	__field(	unsigned int,	prev_pid	)	\
	__field(	unsigned int,	next_pid	)	\
	__field(	unsigned int,	next_cpu	)       \
	__field(	unsigned char,	prev_prio	)	\
	__field(	unsigned char,	prev_state	)	\
	__field(	unsigned char,	next_prio	)	\
	__field(	unsigned char,	next_state	)

FTRACE_ENTRY(context_switch, ctx_switch_entry,

	TRACE_CTX,

	F_STRUCT(
		FTRACE_CTX_FIELDS
	),

	F_printk("%u:%u:%u  ==> %u:%u:%u [%03u]",
		 __entry->prev_pid, __entry->prev_prio, __entry->prev_state,
		 __entry->next_pid, __entry->next_prio, __entry->next_state,
		 __entry->next_cpu),

	FILTER_OTHER
);

/*
 * FTRACE_ENTRY_DUP only creates the format file, it will not
 *  create another structure.
 */
FTRACE_ENTRY_DUP(wakeup, ctx_switch_entry,

	TRACE_WAKE,

	F_STRUCT(
		FTRACE_CTX_FIELDS
	),

	F_printk("%u:%u:%u  ==+ %u:%u:%u [%03u]",
		 __entry->prev_pid, __entry->prev_prio, __entry->prev_state,
		 __entry->next_pid, __entry->next_prio, __entry->next_state,
		 __entry->next_cpu),

	FILTER_OTHER
);

/*
 * Stack-trace entry:
 */

#define FTRACE_STACK_ENTRIES	8

#ifndef CONFIG_64BIT
# define IP_FMT "%08lx"
#else
# define IP_FMT "%016lx"
#endif

FTRACE_ENTRY(kernel_stack, stack_entry,

	TRACE_STACK,

	F_STRUCT(
		__field(	int,		size	)
		__dynamic_array(unsigned long,	caller	)
	),

	F_printk("\t=> (" IP_FMT ")\n\t=> (" IP_FMT ")\n\t=> (" IP_FMT ")\n"
		 "\t=> (" IP_FMT ")\n\t=> (" IP_FMT ")\n\t=> (" IP_FMT ")\n"
		 "\t=> (" IP_FMT ")\n\t=> (" IP_FMT ")\n",
		 __entry->caller[0], __entry->caller[1], __entry->caller[2],
		 __entry->caller[3], __entry->caller[4], __entry->caller[5],
		 __entry->caller[6], __entry->caller[7]),

	FILTER_OTHER
);

FTRACE_ENTRY(user_stack, userstack_entry,

	TRACE_USER_STACK,

	F_STRUCT(
		__field(	unsigned int,	tgid	)
		__array(	unsigned long,	caller, FTRACE_STACK_ENTRIES	)
	),

	F_printk("\t=> (" IP_FMT ")\n\t=> (" IP_FMT ")\n\t=> (" IP_FMT ")\n"
		 "\t=> (" IP_FMT ")\n\t=> (" IP_FMT ")\n\t=> (" IP_FMT ")\n"
		 "\t=> (" IP_FMT ")\n\t=> (" IP_FMT ")\n",
		 __entry->caller[0], __entry->caller[1], __entry->caller[2],
		 __entry->caller[3], __entry->caller[4], __entry->caller[5],
		 __entry->caller[6], __entry->caller[7]),

	FILTER_OTHER
);

/*
 * trace_printk entry:
 */
FTRACE_ENTRY(bprint, bprint_entry,

	TRACE_BPRINT,

	F_STRUCT(
		__field(	unsigned long,	ip	)
		__field(	const char *,	fmt	)
		__dynamic_array(	u32,	buf	)
	),

	F_printk("%08lx fmt:%p",
		 __entry->ip, __entry->fmt),

	FILTER_OTHER
);

FTRACE_ENTRY(print, print_entry,

	TRACE_PRINT,

	F_STRUCT(
		__field(	unsigned long,	ip	)
		__dynamic_array(	char,	buf	)
	),

	F_printk("%08lx %s",
		 __entry->ip, __entry->buf),

	FILTER_OTHER
);

FTRACE_ENTRY(mmiotrace_rw, trace_mmiotrace_rw,

	TRACE_MMIO_RW,

	F_STRUCT(
		__field_struct(	struct mmiotrace_rw,	rw	)
		__field_desc(	resource_size_t, rw,	phys	)
		__field_desc(	unsigned long,	rw,	value	)
		__field_desc(	unsigned long,	rw,	pc	)
		__field_desc(	int, 		rw,	map_id	)
		__field_desc(	unsigned char,	rw,	opcode	)
		__field_desc(	unsigned char,	rw,	width	)
	),

	F_printk("%lx %lx %lx %d %x %x",
		 (unsigned long)__entry->phys, __entry->value, __entry->pc,
		 __entry->map_id, __entry->opcode, __entry->width),

	FILTER_OTHER
);

FTRACE_ENTRY(mmiotrace_map, trace_mmiotrace_map,

	TRACE_MMIO_MAP,

	F_STRUCT(
		__field_struct(	struct mmiotrace_map,	map	)
		__field_desc(	resource_size_t, map,	phys	)
		__field_desc(	unsigned long,	map,	virt	)
		__field_desc(	unsigned long,	map,	len	)
		__field_desc(	int, 		map,	map_id	)
		__field_desc(	unsigned char,	map,	opcode	)
	),

	F_printk("%lx %lx %lx %d %x",
		 (unsigned long)__entry->phys, __entry->virt, __entry->len,
		 __entry->map_id, __entry->opcode),

	FILTER_OTHER
);


#define TRACE_FUNC_SIZE 30
#define TRACE_FILE_SIZE 20

FTRACE_ENTRY(branch, trace_branch,

	TRACE_BRANCH,

	F_STRUCT(
		__field(	unsigned int,	line				)
		__array(	char,		func,	TRACE_FUNC_SIZE+1	)
		__array(	char,		file,	TRACE_FILE_SIZE+1	)
		__field(	char,		correct				)
	),

	F_printk("%u:%s:%s (%u)",
		 __entry->line,
		 __entry->func, __entry->file, __entry->correct),

	FILTER_OTHER
);

