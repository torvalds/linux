#undef TRACE_SYSTEM
#define TRACE_SYSTEM 9p

#if !defined(_TRACE_9P_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_9P_H

#include <linux/tracepoint.h>

#define show_9p_op(type)						\
	__print_symbolic(type,						\
			 { P9_TLERROR,		"P9_TLERROR" },		\
			 { P9_RLERROR,		"P9_RLERROR" },		\
			 { P9_TSTATFS,		"P9_TSTATFS" },		\
			 { P9_RSTATFS,		"P9_RSTATFS" },		\
			 { P9_TLOPEN,		"P9_TLOPEN" },		\
			 { P9_RLOPEN,		"P9_RLOPEN" },		\
			 { P9_TLCREATE,		"P9_TLCREATE" },	\
			 { P9_RLCREATE,		"P9_RLCREATE" },	\
			 { P9_TSYMLINK,		"P9_TSYMLINK" },	\
			 { P9_RSYMLINK,		"P9_RSYMLINK" },	\
			 { P9_TMKNOD,		"P9_TMKNOD" },		\
			 { P9_RMKNOD,		"P9_RMKNOD" },		\
			 { P9_TRENAME,		"P9_TRENAME" },		\
			 { P9_RRENAME,		"P9_RRENAME" },		\
			 { P9_TREADLINK,	"P9_TREADLINK" },	\
			 { P9_RREADLINK,	"P9_RREADLINK" },	\
			 { P9_TGETATTR,		"P9_TGETATTR" },	\
			 { P9_RGETATTR,		"P9_RGETATTR" },	\
			 { P9_TSETATTR,		"P9_TSETATTR" },	\
			 { P9_RSETATTR,		"P9_RSETATTR" },	\
			 { P9_TXATTRWALK,	"P9_TXATTRWALK" },	\
			 { P9_RXATTRWALK,	"P9_RXATTRWALK" },	\
			 { P9_TXATTRCREATE,	"P9_TXATTRCREATE" },	\
			 { P9_RXATTRCREATE,	"P9_RXATTRCREATE" },	\
			 { P9_TREADDIR,		"P9_TREADDIR" },	\
			 { P9_RREADDIR,		"P9_RREADDIR" },	\
			 { P9_TFSYNC,		"P9_TFSYNC" },		\
			 { P9_RFSYNC,		"P9_RFSYNC" },		\
			 { P9_TLOCK,		"P9_TLOCK" },		\
			 { P9_RLOCK,		"P9_RLOCK" },		\
			 { P9_TGETLOCK,		"P9_TGETLOCK" },	\
			 { P9_RGETLOCK,		"P9_RGETLOCK" },	\
			 { P9_TLINK,		"P9_TLINK" },		\
			 { P9_RLINK,		"P9_RLINK" },		\
			 { P9_TMKDIR,		"P9_TMKDIR" },		\
			 { P9_RMKDIR,		"P9_RMKDIR" },		\
			 { P9_TRENAMEAT,	"P9_TRENAMEAT" },	\
			 { P9_RRENAMEAT,	"P9_RRENAMEAT" },	\
			 { P9_TUNLINKAT,	"P9_TUNLINKAT" },	\
			 { P9_RUNLINKAT,	"P9_RUNLINKAT" },	\
			 { P9_TVERSION,		"P9_TVERSION" },	\
			 { P9_RVERSION,		"P9_RVERSION" },	\
			 { P9_TAUTH,		"P9_TAUTH" },		\
			 { P9_RAUTH,		"P9_RAUTH" },		\
			 { P9_TATTACH,		"P9_TATTACH" },		\
			 { P9_RATTACH,		"P9_RATTACH" },		\
			 { P9_TERROR,		"P9_TERROR" },		\
			 { P9_RERROR,		"P9_RERROR" },		\
			 { P9_TFLUSH,		"P9_TFLUSH" },		\
			 { P9_RFLUSH,		"P9_RFLUSH" },		\
			 { P9_TWALK,		"P9_TWALK" },		\
			 { P9_RWALK,		"P9_RWALK" },		\
			 { P9_TOPEN,		"P9_TOPEN" },		\
			 { P9_ROPEN,		"P9_ROPEN" },		\
			 { P9_TCREATE,		"P9_TCREATE" },		\
			 { P9_RCREATE,		"P9_RCREATE" },		\
			 { P9_TREAD,		"P9_TREAD" },		\
			 { P9_RREAD,		"P9_RREAD" },		\
			 { P9_TWRITE,		"P9_TWRITE" },		\
			 { P9_RWRITE,		"P9_RWRITE" },		\
			 { P9_TCLUNK,		"P9_TCLUNK" },		\
			 { P9_RCLUNK,		"P9_RCLUNK" },		\
			 { P9_TREMOVE,		"P9_TREMOVE" },		\
			 { P9_RREMOVE,		"P9_RREMOVE" },		\
			 { P9_TSTAT,		"P9_TSTAT" },		\
			 { P9_RSTAT,		"P9_RSTAT" },		\
			 { P9_TWSTAT,		"P9_TWSTAT" },		\
			 { P9_RWSTAT,		"P9_RWSTAT" })

TRACE_EVENT(9p_client_req,
	    TP_PROTO(struct p9_client *clnt, int8_t type, int tag),

	    TP_ARGS(clnt, type, tag),

	    TP_STRUCT__entry(
		    __field(    void *,		clnt			     )
		    __field(	__u8,		type			     )
		    __field(	__u32,		tag			     )
		    ),

	    TP_fast_assign(
		    __entry->clnt    =  clnt;
		    __entry->type    =  type;
		    __entry->tag     =  tag;
		    ),

	    TP_printk("client %lu request %s tag  %d",
		    (long)__entry->clnt, show_9p_op(__entry->type),
		    __entry->tag)
 );

TRACE_EVENT(9p_client_res,
	    TP_PROTO(struct p9_client *clnt, int8_t type, int tag, int err),

	    TP_ARGS(clnt, type, tag, err),

	    TP_STRUCT__entry(
		    __field(    void *,		clnt			     )
		    __field(	__u8,		type			     )
		    __field(	__u32,		tag			     )
		    __field(	__u32,		err			     )
		    ),

	    TP_fast_assign(
		    __entry->clnt    =  clnt;
		    __entry->type    =  type;
		    __entry->tag     =  tag;
		    __entry->err     =  err;
		    ),

	    TP_printk("client %lu response %s tag  %d err %d",
		      (long)__entry->clnt, show_9p_op(__entry->type),
		      __entry->tag, __entry->err)
);

/* dump 32 bytes of protocol data */
#define P9_PROTO_DUMP_SZ 32
TRACE_EVENT(9p_protocol_dump,
	    TP_PROTO(struct p9_client *clnt, struct p9_fcall *pdu),

	    TP_ARGS(clnt, pdu),

	    TP_STRUCT__entry(
		    __field(	void *,		clnt				)
		    __field(	__u8,		type				)
		    __field(	__u16,		tag				)
		    __array(	unsigned char,	line,	P9_PROTO_DUMP_SZ	)
		    ),

	    TP_fast_assign(
		    __entry->clnt   =  clnt;
		    __entry->type   =  pdu->id;
		    __entry->tag    =  pdu->tag;
		    memcpy(__entry->line, pdu->sdata, P9_PROTO_DUMP_SZ);
		    ),
	    TP_printk("clnt %lu %s(tag = %d)\n%.3x: %16ph\n%.3x: %16ph\n",
		      (unsigned long)__entry->clnt, show_9p_op(__entry->type),
		      __entry->tag, 0, __entry->line, 16, __entry->line + 16)
 );

#endif /* _TRACE_9P_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
