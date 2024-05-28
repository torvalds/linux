/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM 9p

#if !defined(_TRACE_9P_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_9P_H

#include <linux/tracepoint.h>

#define P9_MSG_T							\
		EM( P9_TLERROR,		"P9_TLERROR" )			\
		EM( P9_RLERROR,		"P9_RLERROR" )			\
		EM( P9_TSTATFS,		"P9_TSTATFS" )			\
		EM( P9_RSTATFS,		"P9_RSTATFS" )			\
		EM( P9_TLOPEN,		"P9_TLOPEN" )			\
		EM( P9_RLOPEN,		"P9_RLOPEN" )			\
		EM( P9_TLCREATE,	"P9_TLCREATE" )			\
		EM( P9_RLCREATE,	"P9_RLCREATE" )			\
		EM( P9_TSYMLINK,	"P9_TSYMLINK" )			\
		EM( P9_RSYMLINK,	"P9_RSYMLINK" )			\
		EM( P9_TMKNOD,		"P9_TMKNOD" )			\
		EM( P9_RMKNOD,		"P9_RMKNOD" )			\
		EM( P9_TRENAME,		"P9_TRENAME" )			\
		EM( P9_RRENAME,		"P9_RRENAME" )			\
		EM( P9_TREADLINK,	"P9_TREADLINK" )		\
		EM( P9_RREADLINK,	"P9_RREADLINK" )		\
		EM( P9_TGETATTR,	"P9_TGETATTR" )			\
		EM( P9_RGETATTR,	"P9_RGETATTR" )			\
		EM( P9_TSETATTR,	"P9_TSETATTR" )			\
		EM( P9_RSETATTR,	"P9_RSETATTR" )			\
		EM( P9_TXATTRWALK,	"P9_TXATTRWALK" )		\
		EM( P9_RXATTRWALK,	"P9_RXATTRWALK" )		\
		EM( P9_TXATTRCREATE,	"P9_TXATTRCREATE" )		\
		EM( P9_RXATTRCREATE,	"P9_RXATTRCREATE" )		\
		EM( P9_TREADDIR,	"P9_TREADDIR" )			\
		EM( P9_RREADDIR,	"P9_RREADDIR" )			\
		EM( P9_TFSYNC,		"P9_TFSYNC" )			\
		EM( P9_RFSYNC,		"P9_RFSYNC" )			\
		EM( P9_TLOCK,		"P9_TLOCK" )			\
		EM( P9_RLOCK,		"P9_RLOCK" )			\
		EM( P9_TGETLOCK,	"P9_TGETLOCK" )			\
		EM( P9_RGETLOCK,	"P9_RGETLOCK" )			\
		EM( P9_TLINK,		"P9_TLINK" )			\
		EM( P9_RLINK,		"P9_RLINK" )			\
		EM( P9_TMKDIR,		"P9_TMKDIR" )			\
		EM( P9_RMKDIR,		"P9_RMKDIR" )			\
		EM( P9_TRENAMEAT,	"P9_TRENAMEAT" )		\
		EM( P9_RRENAMEAT,	"P9_RRENAMEAT" )		\
		EM( P9_TUNLINKAT,	"P9_TUNLINKAT" )		\
		EM( P9_RUNLINKAT,	"P9_RUNLINKAT" )		\
		EM( P9_TVERSION,	"P9_TVERSION" )			\
		EM( P9_RVERSION,	"P9_RVERSION" )			\
		EM( P9_TAUTH,		"P9_TAUTH" )			\
		EM( P9_RAUTH,		"P9_RAUTH" )			\
		EM( P9_TATTACH,		"P9_TATTACH" )			\
		EM( P9_RATTACH,		"P9_RATTACH" )			\
		EM( P9_TERROR,		"P9_TERROR" )			\
		EM( P9_RERROR,		"P9_RERROR" )			\
		EM( P9_TFLUSH,		"P9_TFLUSH" )			\
		EM( P9_RFLUSH,		"P9_RFLUSH" )			\
		EM( P9_TWALK,		"P9_TWALK" )			\
		EM( P9_RWALK,		"P9_RWALK" )			\
		EM( P9_TOPEN,		"P9_TOPEN" )			\
		EM( P9_ROPEN,		"P9_ROPEN" )			\
		EM( P9_TCREATE,		"P9_TCREATE" )			\
		EM( P9_RCREATE,		"P9_RCREATE" )			\
		EM( P9_TREAD,		"P9_TREAD" )			\
		EM( P9_RREAD,		"P9_RREAD" )			\
		EM( P9_TWRITE,		"P9_TWRITE" )			\
		EM( P9_RWRITE,		"P9_RWRITE" )			\
		EM( P9_TCLUNK,		"P9_TCLUNK" )			\
		EM( P9_RCLUNK,		"P9_RCLUNK" )			\
		EM( P9_TREMOVE,		"P9_TREMOVE" )			\
		EM( P9_RREMOVE,		"P9_RREMOVE" )			\
		EM( P9_TSTAT,		"P9_TSTAT" )			\
		EM( P9_RSTAT,		"P9_RSTAT" )			\
		EM( P9_TWSTAT,		"P9_TWSTAT" )			\
		EMe(P9_RWSTAT,		"P9_RWSTAT" )


#define P9_FID_REFTYPE							\
		EM( P9_FID_REF_CREATE,	"create " )			\
		EM( P9_FID_REF_GET,	"get    " )			\
		EM( P9_FID_REF_PUT,	"put    " )			\
		EMe(P9_FID_REF_DESTROY,	"destroy" )

/* Define EM() to export the enums to userspace via TRACE_DEFINE_ENUM() */
#undef EM
#undef EMe
#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

P9_MSG_T
P9_FID_REFTYPE

/* And also use EM/EMe to define helper enums -- once */
#ifndef __9P_DECLARE_TRACE_ENUMS_ONLY_ONCE
#define __9P_DECLARE_TRACE_ENUMS_ONLY_ONCE
#undef EM
#undef EMe
#define EM(a, b)	a,
#define EMe(a, b)	a

enum p9_fid_reftype {
	P9_FID_REFTYPE
} __mode(byte);

#endif

/*
 * Now redefine the EM() and EMe() macros to map the enums to the strings
 * that will be printed in the output.
 */
#undef EM
#undef EMe
#define EM(a, b)	{ a, b },
#define EMe(a, b)	{ a, b }

#define show_9p_op(type)						\
	__print_symbolic(type, P9_MSG_T)
#define show_9p_fid_reftype(type)					\
	__print_symbolic(type, P9_FID_REFTYPE)

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
		    __dynamic_array(unsigned char, line,
				min_t(size_t, pdu->capacity, P9_PROTO_DUMP_SZ))
		    ),

	    TP_fast_assign(
		    __entry->clnt   =  clnt;
		    __entry->type   =  pdu->id;
		    __entry->tag    =  pdu->tag;
		    memcpy(__get_dynamic_array(line), pdu->sdata,
				__get_dynamic_array_len(line));
		    ),
	    TP_printk("clnt %lu %s(tag = %d)\n%*ph\n",
		      (unsigned long)__entry->clnt, show_9p_op(__entry->type),
		      __entry->tag, __get_dynamic_array_len(line),
		      __get_dynamic_array(line))
 );


TRACE_EVENT(9p_fid_ref,
	    TP_PROTO(struct p9_fid *fid, __u8 type),

	    TP_ARGS(fid, type),

	    TP_STRUCT__entry(
		    __field(	int,	fid		)
		    __field(	int,	refcount	)
		    __field(	__u8, type	)
		    ),

	    TP_fast_assign(
		    __entry->fid = fid->fid;
		    __entry->refcount = refcount_read(&fid->count);
		    __entry->type = type;
		    ),

	    TP_printk("%s fid %d, refcount %d",
		      show_9p_fid_reftype(__entry->type),
		      __entry->fid, __entry->refcount)
);


#endif /* _TRACE_9P_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
