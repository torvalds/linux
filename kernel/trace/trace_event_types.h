#undef TRACE_SYSTEM
#define TRACE_SYSTEM	ftrace

/*
 * We cheat and use the proto type field as the ID
 * and args as the entry type (minus 'struct')
 */
TRACE_EVENT_FORMAT(function, TRACE_FN, ftrace_entry, ignore,
	TRACE_STRUCT(
		TRACE_FIELD(unsigned long, ip, ip)
		TRACE_FIELD(unsigned long, parent_ip, parent_ip)
	),
	TP_RAW_FMT(" %lx <-- %lx")
);

TRACE_EVENT_FORMAT(funcgraph_entry, TRACE_GRAPH_ENT,
		   ftrace_graph_ent_entry, ignore,
	TRACE_STRUCT(
		TRACE_FIELD(unsigned long, graph_ent.func, func)
		TRACE_FIELD(int, graph_ent.depth, depth)
	),
	TP_RAW_FMT("--> %lx (%d)")
);

TRACE_EVENT_FORMAT(funcgraph_exit, TRACE_GRAPH_RET,
		   ftrace_graph_ret_entry, ignore,
	TRACE_STRUCT(
		TRACE_FIELD(unsigned long, ret.func, func)
		TRACE_FIELD(unsigned long long, ret.calltime, calltime)
		TRACE_FIELD(unsigned long long, ret.rettime, rettime)
		TRACE_FIELD(unsigned long, ret.overrun, overrun)
		TRACE_FIELD(int, ret.depth, depth)
	),
	TP_RAW_FMT("<-- %lx (%d)")
);

TRACE_EVENT_FORMAT(wakeup, TRACE_WAKE, ctx_switch_entry, ignore,
	TRACE_STRUCT(
		TRACE_FIELD(unsigned int, prev_pid, prev_pid)
		TRACE_FIELD(unsigned char, prev_prio, prev_prio)
		TRACE_FIELD(unsigned char, prev_state, prev_state)
		TRACE_FIELD(unsigned int, next_pid, next_pid)
		TRACE_FIELD(unsigned char, next_prio, next_prio)
		TRACE_FIELD(unsigned char, next_state, next_state)
		TRACE_FIELD(unsigned int, next_cpu, next_cpu)
	),
	TP_RAW_FMT("%u:%u:%u  ==+ %u:%u:%u [%03u]")
);

TRACE_EVENT_FORMAT(context_switch, TRACE_CTX, ctx_switch_entry, ignore,
	TRACE_STRUCT(
		TRACE_FIELD(unsigned int, prev_pid, prev_pid)
		TRACE_FIELD(unsigned char, prev_prio, prev_prio)
		TRACE_FIELD(unsigned char, prev_state, prev_state)
		TRACE_FIELD(unsigned int, next_pid, next_pid)
		TRACE_FIELD(unsigned char, next_prio, next_prio)
		TRACE_FIELD(unsigned char, next_state, next_state)
		TRACE_FIELD(unsigned int, next_cpu, next_cpu)
	),
	TP_RAW_FMT("%u:%u:%u  ==+ %u:%u:%u [%03u]")
);

TRACE_EVENT_FORMAT_NOFILTER(special, TRACE_SPECIAL, special_entry, ignore,
	TRACE_STRUCT(
		TRACE_FIELD(unsigned long, arg1, arg1)
		TRACE_FIELD(unsigned long, arg2, arg2)
		TRACE_FIELD(unsigned long, arg3, arg3)
	),
	TP_RAW_FMT("(%08lx) (%08lx) (%08lx)")
);

/*
 * Stack-trace entry:
 */

/* #define FTRACE_STACK_ENTRIES   8 */

TRACE_EVENT_FORMAT(kernel_stack, TRACE_STACK, stack_entry, ignore,
	TRACE_STRUCT(
		TRACE_FIELD(unsigned long, caller[0], stack0)
		TRACE_FIELD(unsigned long, caller[1], stack1)
		TRACE_FIELD(unsigned long, caller[2], stack2)
		TRACE_FIELD(unsigned long, caller[3], stack3)
		TRACE_FIELD(unsigned long, caller[4], stack4)
		TRACE_FIELD(unsigned long, caller[5], stack5)
		TRACE_FIELD(unsigned long, caller[6], stack6)
		TRACE_FIELD(unsigned long, caller[7], stack7)
	),
	TP_RAW_FMT("\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n"
		 "\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n")
);

TRACE_EVENT_FORMAT(user_stack, TRACE_USER_STACK, userstack_entry, ignore,
	TRACE_STRUCT(
		TRACE_FIELD(unsigned long, caller[0], stack0)
		TRACE_FIELD(unsigned long, caller[1], stack1)
		TRACE_FIELD(unsigned long, caller[2], stack2)
		TRACE_FIELD(unsigned long, caller[3], stack3)
		TRACE_FIELD(unsigned long, caller[4], stack4)
		TRACE_FIELD(unsigned long, caller[5], stack5)
		TRACE_FIELD(unsigned long, caller[6], stack6)
		TRACE_FIELD(unsigned long, caller[7], stack7)
	),
	TP_RAW_FMT("\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n"
		 "\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n\t=> (%08lx)\n")
);

TRACE_EVENT_FORMAT(bprint, TRACE_BPRINT, bprint_entry, ignore,
	TRACE_STRUCT(
		TRACE_FIELD(unsigned long, ip, ip)
		TRACE_FIELD(char *, fmt, fmt)
		TRACE_FIELD_ZERO_CHAR(buf)
	),
	TP_RAW_FMT("%08lx (%d) fmt:%p %s")
);

TRACE_EVENT_FORMAT(print, TRACE_PRINT, print_entry, ignore,
	TRACE_STRUCT(
		TRACE_FIELD(unsigned long, ip, ip)
		TRACE_FIELD_ZERO_CHAR(buf)
	),
	TP_RAW_FMT("%08lx (%d) fmt:%p %s")
);

TRACE_EVENT_FORMAT(branch, TRACE_BRANCH, trace_branch, ignore,
	TRACE_STRUCT(
		TRACE_FIELD(unsigned int, line, line)
		TRACE_FIELD_SPECIAL(char func[TRACE_FUNC_SIZE+1], func,
				    TRACE_FUNC_SIZE+1, func)
		TRACE_FIELD_SPECIAL(char file[TRACE_FUNC_SIZE+1], file,
				    TRACE_FUNC_SIZE+1, file)
		TRACE_FIELD(char, correct, correct)
	),
	TP_RAW_FMT("%u:%s:%s (%u)")
);

TRACE_EVENT_FORMAT(hw_branch, TRACE_HW_BRANCHES, hw_branch_entry, ignore,
	TRACE_STRUCT(
		TRACE_FIELD(u64, from, from)
		TRACE_FIELD(u64, to, to)
	),
	TP_RAW_FMT("from: %llx to: %llx")
);

TRACE_EVENT_FORMAT(power, TRACE_POWER, trace_power, ignore,
	TRACE_STRUCT(
		TRACE_FIELD_SIGN(ktime_t, state_data.stamp, stamp, 1)
		TRACE_FIELD_SIGN(ktime_t, state_data.end, end, 1)
		TRACE_FIELD(int, state_data.type, type)
		TRACE_FIELD(int, state_data.state, state)
	),
	TP_RAW_FMT("%llx->%llx type:%u state:%u")
);

TRACE_EVENT_FORMAT(kmem_alloc, TRACE_KMEM_ALLOC, kmemtrace_alloc_entry, ignore,
	TRACE_STRUCT(
		TRACE_FIELD(enum kmemtrace_type_id, type_id, type_id)
		TRACE_FIELD(unsigned long, call_site, call_site)
		TRACE_FIELD(const void *, ptr, ptr)
		TRACE_FIELD(size_t, bytes_req, bytes_req)
		TRACE_FIELD(size_t, bytes_alloc, bytes_alloc)
		TRACE_FIELD(gfp_t, gfp_flags, gfp_flags)
		TRACE_FIELD(int, node, node)
	),
	TP_RAW_FMT("type:%u call_site:%lx ptr:%p req:%lu alloc:%lu"
		 " flags:%x node:%d")
);

TRACE_EVENT_FORMAT(kmem_free, TRACE_KMEM_FREE, kmemtrace_free_entry, ignore,
	TRACE_STRUCT(
		TRACE_FIELD(enum kmemtrace_type_id, type_id, type_id)
		TRACE_FIELD(unsigned long, call_site, call_site)
		TRACE_FIELD(const void *, ptr, ptr)
	),
	TP_RAW_FMT("type:%u call_site:%lx ptr:%p")
);

#undef TRACE_SYSTEM
