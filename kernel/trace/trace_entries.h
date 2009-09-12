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
		__field(	struct ftrace_graph_ent,	graph_ent	)
	),

	F_printk("--> %lx (%d)", __entry->graph_ent.func, __entry->depth)
);

/* Function return entry */
FTRACE_ENTRY(funcgraph_exit, ftrace_graph_ret_entry,

	TRACE_GRAPH_RET,

	F_STRUCT(
		__field(	struct ftrace_graph_ret,	ret	)
	),

	F_printk("<-- %lx (%d) (start: %llx  end: %llx) over: %d",
		 __entry->func, __entry->depth,
		 __entry->calltime, __entry->rettim,
		 __entrty->depth)
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

#if 0
FTRACE_ENTRY_STRUCT_ONLY(ctx_switch_entry,

	F_STRUCT(
		FTRACE_CTX_FIELDS
	)
);
#endif

FTRACE_ENTRY(context_switch, ctx_switch_entry,

	TRACE_CTX,

	F_STRUCT(
		FTRACE_CTX_FIELDS
	),

	F_printk(b"%u:%u:%u  ==> %u:%u:%u [%03u]",
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
		__field(	struct mmiotrace_rw,	rw	)
	),

	F_printk("%lx %lx %lx %d %lx %lx",
		 __entry->phs, __entry->value, __entry->pc,
		 __entry->map_id, __entry->opcode, __entry->width)
);

FTRACE_ENTRY(mmiotrace_map, trace_mmiotrace_map,

	TRACE_MMIO_MAP,

	F_STRUCT(
		__field(	struct mmiotrace_map,	map	)
	),

	F_printk("%lx %lx %lx %d %lx",
		 __entry->phs, __entry->virt, __entry->len,
		 __entry->map_id, __entry->opcode)
);

FTRACE_ENTRY(boot_call, trace_boot_call,

	TRACE_BOOT_CALL,

	F_STRUCT(
		__field(	struct boot_trace_call,	boot_call	)
	),

	F_printk("%d  %s", __entry->caller, __entry->func)
);

FTRACE_ENTRY(boot_ret, trace_boot_ret,

	TRACE_BOOT_RET,

	F_STRUCT(
		__field(	struct boot_trace_ret,	boot_ret	)
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

FTRACE_ENTRY(hw_branch, hw_branch_entry,

	TRACE_HW_BRANCHES,

	F_STRUCT(
		__field(	u64,	from	)
		__field(	u64,	to	)
	),

	F_printk("from: %llx to: %llx", __entry->from, __entry->to)
);

FTRACE_ENTRY(power, trace_power,

	TRACE_POWER,

	F_STRUCT(
		__field(	struct power_trace,	state_data	)
	),

	F_printk("%llx->%llx type:%u state:%u",
		 __entry->stamp, __entry->end,
		 __entry->type, __entry->state)
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

	F_printk("type:%u call_site:%lx ptr:%p req:%lu alloc:%lu"
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
