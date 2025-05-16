// SPDX-License-Identifier: GPL-2.0
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
 *	internal structures are just tracing helpers, this is not
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
		__field_fn(	unsigned long,		ip		)
		__field_fn(	unsigned long,		parent_ip	)
		__dynamic_array( unsigned long,		args		)
	),

	F_printk(" %ps <-- %ps",
		 (void *)__entry->ip, (void *)__entry->parent_ip),

	perf_ftrace_event_register
);

/* Function call entry */
FTRACE_ENTRY(funcgraph_entry, ftrace_graph_ent_entry,

	TRACE_GRAPH_ENT,

	F_STRUCT(
		__field_struct(	struct ftrace_graph_ent,	graph_ent	)
		__field_packed(	unsigned long,	graph_ent,	func		)
		__field_packed(	unsigned int,	graph_ent,	depth		)
		__dynamic_array(unsigned long,	args				)
	),

	F_printk("--> %ps (%u)", (void *)__entry->func, __entry->depth)
);

#ifdef CONFIG_FUNCTION_GRAPH_RETADDR

/* Function call entry with a return address */
FTRACE_ENTRY_PACKED(fgraph_retaddr_entry, fgraph_retaddr_ent_entry,

	TRACE_GRAPH_RETADDR_ENT,

	F_STRUCT(
		__field_struct(	struct fgraph_retaddr_ent,	graph_ent	)
		__field_packed(	unsigned long,	graph_ent,	func		)
		__field_packed(	int,		graph_ent,	depth		)
		__field_packed(	unsigned long,	graph_ent,	retaddr		)
	),

	F_printk("--> %ps (%d) <- %ps", (void *)__entry->func, __entry->depth,
		(void *)__entry->retaddr)
);

#else

#ifndef fgraph_retaddr_ent_entry
#define fgraph_retaddr_ent_entry ftrace_graph_ent_entry
#endif

#endif

#ifdef CONFIG_FUNCTION_GRAPH_RETVAL

/* Function return entry */
FTRACE_ENTRY_PACKED(funcgraph_exit, ftrace_graph_ret_entry,

	TRACE_GRAPH_RET,

	F_STRUCT(
		__field_struct(	struct ftrace_graph_ret,	ret	)
		__field_packed(	unsigned long,	ret,		func	)
		__field_packed(	unsigned long,	ret,		retval	)
		__field_packed(	int,		ret,		depth	)
		__field_packed(	unsigned int,	ret,		overrun	)
		__field(unsigned long long,	calltime		)
		__field(unsigned long long,	rettime			)
	),

	F_printk("<-- %ps (%d) (start: %llx  end: %llx) over: %d retval: %lx",
		 (void *)__entry->func, __entry->depth,
		 __entry->calltime, __entry->rettime,
		 __entry->depth, __entry->retval)
);

#else

/* Function return entry */
FTRACE_ENTRY_PACKED(funcgraph_exit, ftrace_graph_ret_entry,

	TRACE_GRAPH_RET,

	F_STRUCT(
		__field_struct(	struct ftrace_graph_ret,	ret	)
		__field_packed(	unsigned long,	ret,		func	)
		__field_packed(	int,		ret,		depth	)
		__field_packed(	unsigned int,	ret,		overrun	)
		__field(unsigned long long,	calltime		)
		__field(unsigned long long,	rettime			)
	),

	F_printk("<-- %ps (%d) (start: %llx  end: %llx) over: %d",
		 (void *)__entry->func, __entry->depth,
		 __entry->calltime, __entry->rettime,
		 __entry->depth)
);

#endif

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
		 __entry->next_cpu)
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
		 __entry->next_cpu)
);

/*
 * Stack-trace entry:
 */

#define FTRACE_STACK_ENTRIES	8

FTRACE_ENTRY(kernel_stack, stack_entry,

	TRACE_STACK,

	F_STRUCT(
		__field(	int,		size	)
		__stack_array(	unsigned long,	caller,	FTRACE_STACK_ENTRIES, size)
	),

	F_printk("\t=> %ps\n\t=> %ps\n\t=> %ps\n"
		 "\t=> %ps\n\t=> %ps\n\t=> %ps\n"
		 "\t=> %ps\n\t=> %ps\n",
		 (void *)__entry->caller[0], (void *)__entry->caller[1],
		 (void *)__entry->caller[2], (void *)__entry->caller[3],
		 (void *)__entry->caller[4], (void *)__entry->caller[5],
		 (void *)__entry->caller[6], (void *)__entry->caller[7])
);

FTRACE_ENTRY(user_stack, userstack_entry,

	TRACE_USER_STACK,

	F_STRUCT(
		__field(	unsigned int,	tgid	)
		__array(	unsigned long,	caller, FTRACE_STACK_ENTRIES	)
	),

	F_printk("\t=> %ps\n\t=> %ps\n\t=> %ps\n"
		 "\t=> %ps\n\t=> %ps\n\t=> %ps\n"
		 "\t=> %ps\n\t=> %ps\n",
		 (void *)__entry->caller[0], (void *)__entry->caller[1],
		 (void *)__entry->caller[2], (void *)__entry->caller[3],
		 (void *)__entry->caller[4], (void *)__entry->caller[5],
		 (void *)__entry->caller[6], (void *)__entry->caller[7])
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

	F_printk("%ps: %s",
		 (void *)__entry->ip, __entry->fmt)
);

FTRACE_ENTRY_REG(print, print_entry,

	TRACE_PRINT,

	F_STRUCT(
		__field(	unsigned long,	ip	)
		__dynamic_array(	char,	buf	)
	),

	F_printk("%ps: %s",
		 (void *)__entry->ip, __entry->buf),

	ftrace_event_register
);

FTRACE_ENTRY(raw_data, raw_data_entry,

	TRACE_RAW_DATA,

	F_STRUCT(
		__field(	unsigned int,	id	)
		__dynamic_array(	char,	buf	)
	),

	F_printk("id:%04x %08x",
		 __entry->id, (int)__entry->buf[0])
);

FTRACE_ENTRY(bputs, bputs_entry,

	TRACE_BPUTS,

	F_STRUCT(
		__field(	unsigned long,	ip	)
		__field(	const char *,	str	)
	),

	F_printk("%ps: %s",
		 (void *)__entry->ip, __entry->str)
);

FTRACE_ENTRY(mmiotrace_rw, trace_mmiotrace_rw,

	TRACE_MMIO_RW,

	F_STRUCT(
		__field_struct(	struct mmiotrace_rw,	rw	)
		__field_desc(	resource_size_t, rw,	phys	)
		__field_desc(	unsigned long,	rw,	value	)
		__field_desc(	unsigned long,	rw,	pc	)
		__field_desc(	int,		rw,	map_id	)
		__field_desc(	unsigned char,	rw,	opcode	)
		__field_desc(	unsigned char,	rw,	width	)
	),

	F_printk("%lx %lx %lx %d %x %x",
		 (unsigned long)__entry->phys, __entry->value, __entry->pc,
		 __entry->map_id, __entry->opcode, __entry->width)
);

FTRACE_ENTRY(mmiotrace_map, trace_mmiotrace_map,

	TRACE_MMIO_MAP,

	F_STRUCT(
		__field_struct(	struct mmiotrace_map,	map	)
		__field_desc(	resource_size_t, map,	phys	)
		__field_desc(	unsigned long,	map,	virt	)
		__field_desc(	unsigned long,	map,	len	)
		__field_desc(	int,		map,	map_id	)
		__field_desc(	unsigned char,	map,	opcode	)
	),

	F_printk("%lx %lx %lx %d %x",
		 (unsigned long)__entry->phys, __entry->virt, __entry->len,
		 __entry->map_id, __entry->opcode)
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
		__field(	char,		constant			)
	),

	F_printk("%u:%s:%s (%u)%s",
		 __entry->line,
		 __entry->func, __entry->file, __entry->correct,
		 __entry->constant ? " CONSTANT" : "")
);


FTRACE_ENTRY(hwlat, hwlat_entry,

	TRACE_HWLAT,

	F_STRUCT(
		__field(	u64,			duration	)
		__field(	u64,			outer_duration	)
		__field(	u64,			nmi_total_ts	)
		__field_struct( struct timespec64,	timestamp	)
		__field_desc(	s64,	timestamp,	tv_sec		)
		__field_desc(	long,	timestamp,	tv_nsec		)
		__field(	unsigned int,		nmi_count	)
		__field(	unsigned int,		seqnum		)
		__field(	unsigned int,		count		)
	),

	F_printk("cnt:%u\tts:%010llu.%010lu\tinner:%llu\touter:%llu\tcount:%d\tnmi-ts:%llu\tnmi-count:%u\n",
		 __entry->seqnum,
		 __entry->tv_sec,
		 __entry->tv_nsec,
		 __entry->duration,
		 __entry->outer_duration,
		 __entry->count,
		 __entry->nmi_total_ts,
		 __entry->nmi_count)
);

#define FUNC_REPEATS_GET_DELTA_TS(entry)				\
	(((u64)(entry)->top_delta_ts << 32) | (entry)->bottom_delta_ts)	\

FTRACE_ENTRY(func_repeats, func_repeats_entry,

	TRACE_FUNC_REPEATS,

	F_STRUCT(
		__field(	unsigned long,	ip		)
		__field(	unsigned long,	parent_ip	)
		__field(	u16	,	count		)
		__field(	u16	,	top_delta_ts	)
		__field(	u32	,	bottom_delta_ts	)
	),

	F_printk(" %ps <-%ps\t(repeats:%u  delta: -%llu)",
		 (void *)__entry->ip,
		 (void *)__entry->parent_ip,
		 __entry->count,
		 FUNC_REPEATS_GET_DELTA_TS(__entry))
);

FTRACE_ENTRY(osnoise, osnoise_entry,

	TRACE_OSNOISE,

	F_STRUCT(
		__field(	u64,			noise		)
		__field(	u64,			runtime		)
		__field(	u64,			max_sample	)
		__field(	unsigned int,		hw_count	)
		__field(	unsigned int,		nmi_count	)
		__field(	unsigned int,		irq_count	)
		__field(	unsigned int,		softirq_count	)
		__field(	unsigned int,		thread_count	)
	),

	F_printk("noise:%llu\tmax_sample:%llu\thw:%u\tnmi:%u\tirq:%u\tsoftirq:%u\tthread:%u\n",
		 __entry->noise,
		 __entry->max_sample,
		 __entry->hw_count,
		 __entry->nmi_count,
		 __entry->irq_count,
		 __entry->softirq_count,
		 __entry->thread_count)
);

FTRACE_ENTRY(timerlat, timerlat_entry,

	TRACE_TIMERLAT,

	F_STRUCT(
		__field(	unsigned int,		seqnum		)
		__field(	int,			context		)
		__field(	u64,			timer_latency	)
	),

	F_printk("seq:%u\tcontext:%d\ttimer_latency:%llu\n",
		 __entry->seqnum,
		 __entry->context,
		 __entry->timer_latency)
);
