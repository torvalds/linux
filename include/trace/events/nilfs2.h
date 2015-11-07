#undef TRACE_SYSTEM
#define TRACE_SYSTEM nilfs2

#if !defined(_TRACE_NILFS2_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NILFS2_H

#include <linux/tracepoint.h>

struct nilfs_sc_info;

#define show_collection_stage(type)					\
	__print_symbolic(type,						\
	{ NILFS_ST_INIT, "ST_INIT" },					\
	{ NILFS_ST_GC, "ST_GC" },					\
	{ NILFS_ST_FILE, "ST_FILE" },					\
	{ NILFS_ST_IFILE, "ST_IFILE" },					\
	{ NILFS_ST_CPFILE, "ST_CPFILE" },				\
	{ NILFS_ST_SUFILE, "ST_SUFILE" },				\
	{ NILFS_ST_DAT, "ST_DAT" },					\
	{ NILFS_ST_SR, "ST_SR" },					\
	{ NILFS_ST_DSYNC, "ST_DSYNC" },					\
	{ NILFS_ST_DONE, "ST_DONE"})

TRACE_EVENT(nilfs2_collection_stage_transition,

	    TP_PROTO(struct nilfs_sc_info *sci),

	    TP_ARGS(sci),

	    TP_STRUCT__entry(
		    __field(void *, sci)
		    __field(int, stage)
	    ),

	    TP_fast_assign(
			__entry->sci = sci;
			__entry->stage = sci->sc_stage.scnt;
		    ),

	    TP_printk("sci = %p stage = %s",
		      __entry->sci,
		      show_collection_stage(__entry->stage))
);

#endif /* _TRACE_NILFS2_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE nilfs2
#include <trace/define_trace.h>
