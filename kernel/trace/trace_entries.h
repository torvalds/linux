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
 *	structure is layed out. This allows the internal structure
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
 * Function trace entry - function address and parent function addres:
 */
FTRACE_ENTRY(function, ftrace_entry,

	TRACE_FN,

	F_STRUCT(
		__field(	unsigned long,	ip		)
		__field(	unsigned long,	parent_ip	)
	),

	F_printk(" %lx <-- %lx", __entry->ip, __entry->parent_ip)
);

/* Function call entry */
FTRACE_ENTRY(funcgraph_entry, ftrace_graph_ent_entry,

	TRACE_GRAPH_ENT,

	F_STRUCT(
		__field_struct(	struct ftrace_graph_ent,	graph_ent	)
		__field_desc(	unsigned long,	graph_ent,	func		)
		__field_desc(	int,		graph_ent,	depth		)
	),

	F_printk("--> %lx (%d)", __entry->func, __entry->depth)
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
		 __entry->depth)
);

/*
 * Context switch trace entry - which task (and prio) we switched from/to:
 *
 * This is used for both wakeup and context switches. We only want
 * to create one structure, but we need two outputs for it.
 */
#define FTRACE_CTX_FIELDS					\
	__field(	unsigned int,	prev_pid	)	\
	__field(	unsigned char,	prev_prio	)	\
	__field(	unsigned char,	prev_state	)	\
	__field(	unsigned int,	next_pid	)	\
	__field(	unsigned char,	next_prio	)	\
	__field(	unsigned char,	next_state	)	\
	__field(	unsigned int,	next_cpu	)

FTRACE_ENTRY(context_switch, ctx_switch_entry,

	TRACE_CTX,

	F_STRUCT(
		FTRACE_CTX_FIELDS
	),

	F_printk("%u:%u:%u  ==> %u:%u:%u [%03u]",
		 __entry->prev_pid, __entry->prev_prio, __entry->prev_state,
		 __entry->next_pid, __entry->next_prio, __entry->next_state,
		 __entry->next_cpu
		)
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
		 __entry->next_cpu
		)
);

/*
 * Special (free-form) trace entry:
 */
FTRACE_ENTRY(special, special_entry,

	TRACE_SPECIAL,

	F_STRUCT(
		__field(	unsigned long,	arg1	)
		__field(	unsigned long,	arg2	)
		__field(	unsigned long,	arg3	)
	),

	F_printk("(%08lx) (%08lx) (%08lx)",
		 __entry->arg1, __entry->arg2, __entry->arg3)
);

/*
 * Stack-trace entry:
 */

#define FTRACE_STACK_ENTRIES	8

FTRACE_ENTRY(kernel_stack, stack_entry,

	TRACE_STACK,

	F_STRUCT(
		__array(	unsigned long,	caller, FTRACE_STACK_ENTRIES	)
	),

	F_printk("\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n"
		 "\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n",
		 __entry->caller[0], __entry->caller[1], __entry->caller[2],
		 __entry->caller[3], __entry->caller[4], __entry->caller[5],
		 __entry->caller[6], __entry->caller[7])
);

FTRACE_ENTRY(user_stack, userstack_entry,

	TRACE_USER_STACK,

	F_STRUCT(
		__field(	unsigned int,	tgid	)
		__array(	unsigned long,	caller, FTRACE_STACK_ENTRIES	)
	),

	F_printk("\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n"
		 "\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n",
		 __entry->caller[0], __entry->caller[1], __entry->caller[2],
		 __entry->caller[3], __entry->caller[4], __entry->caller[5],
		 __entry->caller[6], __entry->caller[7])
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
		 __entry->ip, __entry->fmt)
);

FTRACE_ENTRY(print, print_entry,

	TRACE_PRINT,

	F_STRUCT(
		__field(	unsigned long,	ip	)
		__dynamic_array(	char,	buf	)
	),

	F_printk("%08lx %s",
		 __entry->ip, __entry->buf)
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
		 __entry->map_id, __entry->opcode, __entry->width)
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
		 __entry->map_id, __entry->opcode)
);

FTRACE_ENTRY(boot_call, trace_boot_call,

	TRACE_BOOT_CALL,

	F_STRUCT(
		__field_struct(	struct boot_trace_call,	boot_call	)
		__field_desc(	pid_t,	boot_call,	caller		)
		__array_desc(	char,	boot_call,	func,	KSYM_SYMBOL_LEN)
	),

	F_printk("%d  %s", __entry->caller, __entry->func)
);

FTRACE_ENTRY(boot_ret, trace_boot_ret,

	TRACE_BOOT_RET,

	F_STRUCT(
		__field_struct(	struct boot_trace_ret,	boot_ret	)
		__array_desc(	char,	boot_ret,	func,	KSYM_SYMBOL_LEN)
		__field_desc(	int,	boot_ret,	result		)
		__field_desc(	unsigned long, boot_ret, duration	)
	),

	F_printk("%s %d %lx",
		 __entry->func, __entry->result, __entry->duration)
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
		 __entry->func, __entry->file, __entry->correct)
);

FTRACE_ENTRY(kmem_alloc, kmemtrace_alloc_entry,

	TRACE_KMEM_ALLOC,

	F_STRUCT(
		__field(	enum kmemtrace_type_id,	type_id		)
		__field(	unsigned long,		call_site	)
		__field(	const void *,		ptr		)
		__field(	size_t,			bytes_req	)
		__field(	size_t,			bytes_alloc	)
		__field(	gfp_t,			gfp_flags	)
		__field(	int,			node		)
	),

	F_printk("type:%u call_site:%lx ptr:%p req:%zi alloc:%zi"
		 " flags:%x node:%d",
		 __entry->type_id, __entry->call_site, __entry->ptr,
		 __entry->bytes_req, __entry->bytes_alloc,
		 __entry->gfp_flags, __entry->node)
);

FTRACE_ENTRY(kmem_free, kmemtrace_free_entry,

	TRACE_KMEM_FREE,

	F_STRUCT(
		__field(	enum kmemtrace_type_id,	type_id		)
		__field(	unsigned long,		call_site	)
		__field(	const void *,		ptr		)
	),

	F_printk("type:%u call_site:%lx ptr:%p",
		 __entry->type_id, __entry->call_site, __entry->ptr)
);

FTRACE_ENTRY(ksym_trace, ksym_trace_entry,

	TRACE_KSYM,

	F_STRUCT(
		__field(	unsigned long,	ip			  )
		__field(	unsigned char,	type			  )
		__array(	char	     ,	cmd,	   TASK_COMM_LEN  )
		__field(	unsigned long,  addr			  )
	),

	F_printk("ip: %pF type: %d ksym_name: %pS cmd: %s",
		(void *)__entry->ip, (unsigned int)__entry->type,
		(void *)__entry->addr,  __entry->cmd)
);
