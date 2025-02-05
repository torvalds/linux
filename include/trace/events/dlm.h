/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dlm

#if !defined(_TRACE_DLM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DLM_H

#include <linux/dlm.h>
#include <linux/dlmconstants.h>
#include <uapi/linux/dlm_plock.h>
#include <linux/tracepoint.h>

#include "../../../fs/dlm/dlm_internal.h"

#define show_lock_flags(flags) __print_flags(flags, "|",	\
	{ DLM_LKF_NOQUEUE,	"NOQUEUE" },			\
	{ DLM_LKF_CANCEL,	"CANCEL" },			\
	{ DLM_LKF_CONVERT,	"CONVERT" },			\
	{ DLM_LKF_VALBLK,	"VALBLK" },			\
	{ DLM_LKF_QUECVT,	"QUECVT" },			\
	{ DLM_LKF_IVVALBLK,	"IVVALBLK" },			\
	{ DLM_LKF_CONVDEADLK,	"CONVDEADLK" },			\
	{ DLM_LKF_PERSISTENT,	"PERSISTENT" },			\
	{ DLM_LKF_NODLCKWT,	"NODLCKWT" },			\
	{ DLM_LKF_NODLCKBLK,	"NODLCKBLK" },			\
	{ DLM_LKF_EXPEDITE,	"EXPEDITE" },			\
	{ DLM_LKF_NOQUEUEBAST,	"NOQUEUEBAST" },		\
	{ DLM_LKF_HEADQUE,	"HEADQUE" },			\
	{ DLM_LKF_NOORDER,	"NOORDER" },			\
	{ DLM_LKF_ORPHAN,	"ORPHAN" },			\
	{ DLM_LKF_ALTPR,	"ALTPR" },			\
	{ DLM_LKF_ALTCW,	"ALTCW" },			\
	{ DLM_LKF_FORCEUNLOCK,	"FORCEUNLOCK" },		\
	{ DLM_LKF_TIMEOUT,	"TIMEOUT" })

#define show_lock_mode(mode) __print_symbolic(mode,		\
	{ DLM_LOCK_IV,		"IV"},				\
	{ DLM_LOCK_NL,		"NL"},				\
	{ DLM_LOCK_CR,		"CR"},				\
	{ DLM_LOCK_CW,		"CW"},				\
	{ DLM_LOCK_PR,		"PR"},				\
	{ DLM_LOCK_PW,		"PW"},				\
	{ DLM_LOCK_EX,		"EX"})

#define show_dlm_sb_flags(flags) __print_flags(flags, "|",	\
	{ DLM_SBF_DEMOTED,	"DEMOTED" },			\
	{ DLM_SBF_VALNOTVALID,	"VALNOTVALID" },		\
	{ DLM_SBF_ALTMODE,	"ALTMODE" })

#define show_lkb_flags(flags) __print_flags(flags, "|",		\
	{ BIT(DLM_DFL_USER_BIT), "USER" },			\
	{ BIT(DLM_DFL_ORPHAN_BIT), "ORPHAN" })

#define show_header_cmd(cmd) __print_symbolic(cmd,		\
	{ DLM_MSG,		"MSG"},				\
	{ DLM_RCOM,		"RCOM"},			\
	{ DLM_OPTS,		"OPTS"},			\
	{ DLM_ACK,		"ACK"},				\
	{ DLM_FIN,		"FIN"})

#define show_message_version(version) __print_symbolic(version,	\
	{ DLM_VERSION_3_1,	"3.1"},				\
	{ DLM_VERSION_3_2,	"3.2"})

#define show_message_type(type) __print_symbolic(type,		\
	{ DLM_MSG_REQUEST,	"REQUEST"},			\
	{ DLM_MSG_CONVERT,	"CONVERT"},			\
	{ DLM_MSG_UNLOCK,	"UNLOCK"},			\
	{ DLM_MSG_CANCEL,	"CANCEL"},			\
	{ DLM_MSG_REQUEST_REPLY, "REQUEST_REPLY"},		\
	{ DLM_MSG_CONVERT_REPLY, "CONVERT_REPLY"},		\
	{ DLM_MSG_UNLOCK_REPLY,	"UNLOCK_REPLY"},		\
	{ DLM_MSG_CANCEL_REPLY,	"CANCEL_REPLY"},		\
	{ DLM_MSG_GRANT,	"GRANT"},			\
	{ DLM_MSG_BAST,		"BAST"},			\
	{ DLM_MSG_LOOKUP,	"LOOKUP"},			\
	{ DLM_MSG_REMOVE,	"REMOVE"},			\
	{ DLM_MSG_LOOKUP_REPLY,	"LOOKUP_REPLY"},		\
	{ DLM_MSG_PURGE,	"PURGE"})

#define show_rcom_type(type) __print_symbolic(type,            \
	{ DLM_RCOM_STATUS,              "STATUS"},              \
	{ DLM_RCOM_NAMES,               "NAMES"},               \
	{ DLM_RCOM_LOOKUP,              "LOOKUP"},              \
	{ DLM_RCOM_LOCK,                "LOCK"},                \
	{ DLM_RCOM_STATUS_REPLY,        "STATUS_REPLY"},        \
	{ DLM_RCOM_NAMES_REPLY,         "NAMES_REPLY"},         \
	{ DLM_RCOM_LOOKUP_REPLY,        "LOOKUP_REPLY"},        \
	{ DLM_RCOM_LOCK_REPLY,          "LOCK_REPLY"})


/* note: we begin tracing dlm_lock_start() only if ls and lkb are found */
TRACE_EVENT(dlm_lock_start,

	TP_PROTO(struct dlm_ls *ls, struct dlm_lkb *lkb, const void *name,
		 unsigned int namelen, int mode, __u32 flags),

	TP_ARGS(ls, lkb, name, namelen, mode, flags),

	TP_STRUCT__entry(
		__field(__u32, ls_id)
		__field(__u32, lkb_id)
		__field(int, mode)
		__field(__u32, flags)
		__dynamic_array(unsigned char, res_name,
				lkb->lkb_resource ? lkb->lkb_resource->res_length : namelen)
	),

	TP_fast_assign(
		struct dlm_rsb *r;

		__entry->ls_id = ls->ls_global_id;
		__entry->lkb_id = lkb->lkb_id;
		__entry->mode = mode;
		__entry->flags = flags;

		r = lkb->lkb_resource;
		if (r)
			memcpy(__get_dynamic_array(res_name), r->res_name,
			       __get_dynamic_array_len(res_name));
		else if (name)
			memcpy(__get_dynamic_array(res_name), name,
			       __get_dynamic_array_len(res_name));
	),

	TP_printk("ls_id=%u lkb_id=%x mode=%s flags=%s res_name=%s",
		  __entry->ls_id, __entry->lkb_id,
		  show_lock_mode(__entry->mode),
		  show_lock_flags(__entry->flags),
		  __print_hex_str(__get_dynamic_array(res_name),
				  __get_dynamic_array_len(res_name)))

);

TRACE_EVENT(dlm_lock_end,

	TP_PROTO(struct dlm_ls *ls, struct dlm_lkb *lkb, const void *name,
		 unsigned int namelen, int mode, __u32 flags, int error,
		 bool kernel_lock),

	TP_ARGS(ls, lkb, name, namelen, mode, flags, error, kernel_lock),

	TP_STRUCT__entry(
		__field(__u32, ls_id)
		__field(__u32, lkb_id)
		__field(int, mode)
		__field(__u32, flags)
		__field(int, error)
		__dynamic_array(unsigned char, res_name,
				lkb->lkb_resource ? lkb->lkb_resource->res_length : namelen)
	),

	TP_fast_assign(
		struct dlm_rsb *r;

		__entry->ls_id = ls->ls_global_id;
		__entry->lkb_id = lkb->lkb_id;
		__entry->mode = mode;
		__entry->flags = flags;
		__entry->error = error;

		r = lkb->lkb_resource;
		if (r)
			memcpy(__get_dynamic_array(res_name), r->res_name,
			       __get_dynamic_array_len(res_name));
		else if (name)
			memcpy(__get_dynamic_array(res_name), name,
			       __get_dynamic_array_len(res_name));

		if (kernel_lock) {
			/* return value will be zeroed in those cases by dlm_lock()
			 * we do it here again to not introduce more overhead if
			 * trace isn't running and error reflects the return value.
			 */
			if (error == -EAGAIN || error == -EDEADLK)
				__entry->error = 0;
		}

	),

	TP_printk("ls_id=%u lkb_id=%x mode=%s flags=%s error=%d res_name=%s",
		  __entry->ls_id, __entry->lkb_id,
		  show_lock_mode(__entry->mode),
		  show_lock_flags(__entry->flags), __entry->error,
		  __print_hex_str(__get_dynamic_array(res_name),
				  __get_dynamic_array_len(res_name)))

);

TRACE_EVENT(dlm_bast,

	TP_PROTO(__u32 ls_id, __u32 lkb_id, int mode,
		 const char *res_name, size_t res_length),

	TP_ARGS(ls_id, lkb_id, mode, res_name, res_length),

	TP_STRUCT__entry(
		__field(__u32, ls_id)
		__field(__u32, lkb_id)
		__field(int, mode)
		__dynamic_array(unsigned char, res_name, res_length)
	),

	TP_fast_assign(
		__entry->ls_id = ls_id;
		__entry->lkb_id = lkb_id;
		__entry->mode = mode;

		memcpy(__get_dynamic_array(res_name), res_name,
		       __get_dynamic_array_len(res_name));
	),

	TP_printk("ls_id=%u lkb_id=%x mode=%s res_name=%s",
		  __entry->ls_id, __entry->lkb_id,
		  show_lock_mode(__entry->mode),
		  __print_hex_str(__get_dynamic_array(res_name),
				  __get_dynamic_array_len(res_name)))

);

TRACE_EVENT(dlm_ast,

	TP_PROTO(__u32 ls_id, __u32 lkb_id, __u8 sb_flags, int sb_status,
		 const char *res_name, size_t res_length),

	TP_ARGS(ls_id, lkb_id, sb_flags, sb_status, res_name, res_length),

	TP_STRUCT__entry(
		__field(__u32, ls_id)
		__field(__u32, lkb_id)
		__field(__u8, sb_flags)
		__field(int, sb_status)
		__dynamic_array(unsigned char, res_name, res_length)
	),

	TP_fast_assign(
		__entry->ls_id = ls_id;
		__entry->lkb_id = lkb_id;
		__entry->sb_flags = sb_flags;
		__entry->sb_status = sb_status;

		memcpy(__get_dynamic_array(res_name), res_name,
		       __get_dynamic_array_len(res_name));
	),

	TP_printk("ls_id=%u lkb_id=%x sb_flags=%s sb_status=%d res_name=%s",
		  __entry->ls_id, __entry->lkb_id,
		  show_dlm_sb_flags(__entry->sb_flags), __entry->sb_status,
		  __print_hex_str(__get_dynamic_array(res_name),
				  __get_dynamic_array_len(res_name)))

);

/* note: we begin tracing dlm_unlock_start() only if ls and lkb are found */
TRACE_EVENT(dlm_unlock_start,

	TP_PROTO(struct dlm_ls *ls, struct dlm_lkb *lkb, __u32 flags),

	TP_ARGS(ls, lkb, flags),

	TP_STRUCT__entry(
		__field(__u32, ls_id)
		__field(__u32, lkb_id)
		__field(__u32, flags)
		__dynamic_array(unsigned char, res_name,
				lkb->lkb_resource ? lkb->lkb_resource->res_length : 0)
	),

	TP_fast_assign(
		struct dlm_rsb *r;

		__entry->ls_id = ls->ls_global_id;
		__entry->lkb_id = lkb->lkb_id;
		__entry->flags = flags;

		r = lkb->lkb_resource;
		if (r)
			memcpy(__get_dynamic_array(res_name), r->res_name,
			       __get_dynamic_array_len(res_name));
	),

	TP_printk("ls_id=%u lkb_id=%x flags=%s res_name=%s",
		  __entry->ls_id, __entry->lkb_id,
		  show_lock_flags(__entry->flags),
		  __print_hex_str(__get_dynamic_array(res_name),
				  __get_dynamic_array_len(res_name)))

);

TRACE_EVENT(dlm_unlock_end,

	TP_PROTO(struct dlm_ls *ls, struct dlm_lkb *lkb, __u32 flags,
		 int error),

	TP_ARGS(ls, lkb, flags, error),

	TP_STRUCT__entry(
		__field(__u32, ls_id)
		__field(__u32, lkb_id)
		__field(__u32, flags)
		__field(int, error)
		__dynamic_array(unsigned char, res_name,
				lkb->lkb_resource ? lkb->lkb_resource->res_length : 0)
	),

	TP_fast_assign(
		struct dlm_rsb *r;

		__entry->ls_id = ls->ls_global_id;
		__entry->lkb_id = lkb->lkb_id;
		__entry->flags = flags;
		__entry->error = error;

		r = lkb->lkb_resource;
		if (r)
			memcpy(__get_dynamic_array(res_name), r->res_name,
			       __get_dynamic_array_len(res_name));
	),

	TP_printk("ls_id=%u lkb_id=%x flags=%s error=%d res_name=%s",
		  __entry->ls_id, __entry->lkb_id,
		  show_lock_flags(__entry->flags), __entry->error,
		  __print_hex_str(__get_dynamic_array(res_name),
				  __get_dynamic_array_len(res_name)))

);

DECLARE_EVENT_CLASS(dlm_rcom_template,

	TP_PROTO(uint32_t dst, uint32_t h_seq, const struct dlm_rcom *rc),

	TP_ARGS(dst, h_seq, rc),

	TP_STRUCT__entry(
		__field(uint32_t, dst)
		__field(uint32_t, h_seq)
		__field(uint32_t, h_version)
		__field(uint32_t, h_lockspace)
		__field(uint32_t, h_nodeid)
		__field(uint16_t, h_length)
		__field(uint8_t, h_cmd)
		__field(uint32_t, rc_type)
		__field(int32_t, rc_result)
		__field(uint64_t, rc_id)
		__field(uint64_t, rc_seq)
		__field(uint64_t, rc_seq_reply)
		__dynamic_array(unsigned char, rc_buf,
				le16_to_cpu(rc->rc_header.h_length) - sizeof(*rc))
	),

	TP_fast_assign(
		__entry->dst = dst;
		__entry->h_seq = h_seq;
		__entry->h_version = le32_to_cpu(rc->rc_header.h_version);
		__entry->h_lockspace = le32_to_cpu(rc->rc_header.u.h_lockspace);
		__entry->h_nodeid = le32_to_cpu(rc->rc_header.h_nodeid);
		__entry->h_length = le16_to_cpu(rc->rc_header.h_length);
		__entry->h_cmd = rc->rc_header.h_cmd;
		__entry->rc_type = le32_to_cpu(rc->rc_type);
		__entry->rc_result = le32_to_cpu(rc->rc_result);
		__entry->rc_id = le64_to_cpu(rc->rc_id);
		__entry->rc_seq = le64_to_cpu(rc->rc_seq);
		__entry->rc_seq_reply = le64_to_cpu(rc->rc_seq_reply);
		memcpy(__get_dynamic_array(rc_buf), rc->rc_buf,
		       __get_dynamic_array_len(rc_buf));
	),

	TP_printk("dst=%u h_seq=%u h_version=%s h_lockspace=%u h_nodeid=%u "
		  "h_length=%u h_cmd=%s rc_type=%s rc_result=%d "
		  "rc_id=%llu rc_seq=%llu rc_seq_reply=%llu "
		  "rc_buf=0x%s", __entry->dst, __entry->h_seq,
		  show_message_version(__entry->h_version),
		  __entry->h_lockspace, __entry->h_nodeid, __entry->h_length,
		  show_header_cmd(__entry->h_cmd),
		  show_rcom_type(__entry->rc_type),
		  __entry->rc_result, __entry->rc_id, __entry->rc_seq,
		  __entry->rc_seq_reply,
		  __print_hex_str(__get_dynamic_array(rc_buf),
				  __get_dynamic_array_len(rc_buf)))

);

DEFINE_EVENT(dlm_rcom_template, dlm_send_rcom,
	     TP_PROTO(uint32_t dst, uint32_t h_seq, const struct dlm_rcom *rc),
	     TP_ARGS(dst, h_seq, rc));

DEFINE_EVENT(dlm_rcom_template, dlm_recv_rcom,
	     TP_PROTO(uint32_t dst, uint32_t h_seq, const struct dlm_rcom *rc),
	     TP_ARGS(dst, h_seq, rc));

TRACE_EVENT(dlm_send_message,

	TP_PROTO(uint32_t dst, uint32_t h_seq, const struct dlm_message *ms,
		 const void *name, int namelen),

	TP_ARGS(dst, h_seq, ms, name, namelen),

	TP_STRUCT__entry(
		__field(uint32_t, dst)
		__field(uint32_t, h_seq)
		__field(uint32_t, h_version)
		__field(uint32_t, h_lockspace)
		__field(uint32_t, h_nodeid)
		__field(uint16_t, h_length)
		__field(uint8_t, h_cmd)
		__field(uint32_t, m_type)
		__field(uint32_t, m_nodeid)
		__field(uint32_t, m_pid)
		__field(uint32_t, m_lkid)
		__field(uint32_t, m_remid)
		__field(uint32_t, m_parent_lkid)
		__field(uint32_t, m_parent_remid)
		__field(uint32_t, m_exflags)
		__field(uint32_t, m_sbflags)
		__field(uint32_t, m_flags)
		__field(uint32_t, m_lvbseq)
		__field(uint32_t, m_hash)
		__field(int32_t, m_status)
		__field(int32_t, m_grmode)
		__field(int32_t, m_rqmode)
		__field(int32_t, m_bastmode)
		__field(int32_t, m_asts)
		__field(int32_t, m_result)
		__dynamic_array(unsigned char, m_extra,
				le16_to_cpu(ms->m_header.h_length) - sizeof(*ms))
		__dynamic_array(unsigned char, res_name, namelen)
	),

	TP_fast_assign(
		__entry->dst = dst;
		__entry->h_seq = h_seq;
		__entry->h_version = le32_to_cpu(ms->m_header.h_version);
		__entry->h_lockspace = le32_to_cpu(ms->m_header.u.h_lockspace);
		__entry->h_nodeid = le32_to_cpu(ms->m_header.h_nodeid);
		__entry->h_length = le16_to_cpu(ms->m_header.h_length);
		__entry->h_cmd = ms->m_header.h_cmd;
		__entry->m_type = le32_to_cpu(ms->m_type);
		__entry->m_nodeid = le32_to_cpu(ms->m_nodeid);
		__entry->m_pid = le32_to_cpu(ms->m_pid);
		__entry->m_lkid = le32_to_cpu(ms->m_lkid);
		__entry->m_remid = le32_to_cpu(ms->m_remid);
		__entry->m_parent_lkid = le32_to_cpu(ms->m_parent_lkid);
		__entry->m_parent_remid = le32_to_cpu(ms->m_parent_remid);
		__entry->m_exflags = le32_to_cpu(ms->m_exflags);
		__entry->m_sbflags = le32_to_cpu(ms->m_sbflags);
		__entry->m_flags = le32_to_cpu(ms->m_flags);
		__entry->m_lvbseq = le32_to_cpu(ms->m_lvbseq);
		__entry->m_hash = le32_to_cpu(ms->m_hash);
		__entry->m_status = le32_to_cpu(ms->m_status);
		__entry->m_grmode = le32_to_cpu(ms->m_grmode);
		__entry->m_rqmode = le32_to_cpu(ms->m_rqmode);
		__entry->m_bastmode = le32_to_cpu(ms->m_bastmode);
		__entry->m_asts = le32_to_cpu(ms->m_asts);
		__entry->m_result = le32_to_cpu(ms->m_result);
		memcpy(__get_dynamic_array(m_extra), ms->m_extra,
		       __get_dynamic_array_len(m_extra));
		memcpy(__get_dynamic_array(res_name), name,
		       __get_dynamic_array_len(res_name));
	),

	TP_printk("dst=%u h_seq=%u h_version=%s h_lockspace=%u h_nodeid=%u "
		  "h_length=%u h_cmd=%s m_type=%s m_nodeid=%u "
		  "m_pid=%u m_lkid=%u m_remid=%u m_parent_lkid=%u "
		  "m_parent_remid=%u m_exflags=%s m_sbflags=%s m_flags=%s "
		  "m_lvbseq=%u m_hash=%u m_status=%d m_grmode=%s "
		  "m_rqmode=%s m_bastmode=%s m_asts=%d m_result=%d "
		  "m_extra=0x%s res_name=0x%s", __entry->dst,
		  __entry->h_seq, show_message_version(__entry->h_version),
		  __entry->h_lockspace, __entry->h_nodeid, __entry->h_length,
		  show_header_cmd(__entry->h_cmd),
		  show_message_type(__entry->m_type),
		  __entry->m_nodeid, __entry->m_pid, __entry->m_lkid,
		  __entry->m_remid, __entry->m_parent_lkid,
		  __entry->m_parent_remid, show_lock_flags(__entry->m_exflags),
		  show_dlm_sb_flags(__entry->m_sbflags),
		  show_lkb_flags(__entry->m_flags), __entry->m_lvbseq,
		  __entry->m_hash, __entry->m_status,
		  show_lock_mode(__entry->m_grmode),
		  show_lock_mode(__entry->m_rqmode),
		  show_lock_mode(__entry->m_bastmode),
		  __entry->m_asts, __entry->m_result,
		  __print_hex_str(__get_dynamic_array(m_extra),
				  __get_dynamic_array_len(m_extra)),
		  __print_hex_str(__get_dynamic_array(res_name),
				  __get_dynamic_array_len(res_name)))

);

TRACE_EVENT(dlm_recv_message,

	TP_PROTO(uint32_t dst, uint32_t h_seq, const struct dlm_message *ms),

	TP_ARGS(dst, h_seq, ms),

	TP_STRUCT__entry(
		__field(uint32_t, dst)
		__field(uint32_t, h_seq)
		__field(uint32_t, h_version)
		__field(uint32_t, h_lockspace)
		__field(uint32_t, h_nodeid)
		__field(uint16_t, h_length)
		__field(uint8_t, h_cmd)
		__field(uint32_t, m_type)
		__field(uint32_t, m_nodeid)
		__field(uint32_t, m_pid)
		__field(uint32_t, m_lkid)
		__field(uint32_t, m_remid)
		__field(uint32_t, m_parent_lkid)
		__field(uint32_t, m_parent_remid)
		__field(uint32_t, m_exflags)
		__field(uint32_t, m_sbflags)
		__field(uint32_t, m_flags)
		__field(uint32_t, m_lvbseq)
		__field(uint32_t, m_hash)
		__field(int32_t, m_status)
		__field(int32_t, m_grmode)
		__field(int32_t, m_rqmode)
		__field(int32_t, m_bastmode)
		__field(int32_t, m_asts)
		__field(int32_t, m_result)
		__dynamic_array(unsigned char, m_extra,
				le16_to_cpu(ms->m_header.h_length) - sizeof(*ms))
	),

	TP_fast_assign(
		__entry->dst = dst;
		__entry->h_seq = h_seq;
		__entry->h_version = le32_to_cpu(ms->m_header.h_version);
		__entry->h_lockspace = le32_to_cpu(ms->m_header.u.h_lockspace);
		__entry->h_nodeid = le32_to_cpu(ms->m_header.h_nodeid);
		__entry->h_length = le16_to_cpu(ms->m_header.h_length);
		__entry->h_cmd = ms->m_header.h_cmd;
		__entry->m_type = le32_to_cpu(ms->m_type);
		__entry->m_nodeid = le32_to_cpu(ms->m_nodeid);
		__entry->m_pid = le32_to_cpu(ms->m_pid);
		__entry->m_lkid = le32_to_cpu(ms->m_lkid);
		__entry->m_remid = le32_to_cpu(ms->m_remid);
		__entry->m_parent_lkid = le32_to_cpu(ms->m_parent_lkid);
		__entry->m_parent_remid = le32_to_cpu(ms->m_parent_remid);
		__entry->m_exflags = le32_to_cpu(ms->m_exflags);
		__entry->m_sbflags = le32_to_cpu(ms->m_sbflags);
		__entry->m_flags = le32_to_cpu(ms->m_flags);
		__entry->m_lvbseq = le32_to_cpu(ms->m_lvbseq);
		__entry->m_hash = le32_to_cpu(ms->m_hash);
		__entry->m_status = le32_to_cpu(ms->m_status);
		__entry->m_grmode = le32_to_cpu(ms->m_grmode);
		__entry->m_rqmode = le32_to_cpu(ms->m_rqmode);
		__entry->m_bastmode = le32_to_cpu(ms->m_bastmode);
		__entry->m_asts = le32_to_cpu(ms->m_asts);
		__entry->m_result = le32_to_cpu(ms->m_result);
		memcpy(__get_dynamic_array(m_extra), ms->m_extra,
		       __get_dynamic_array_len(m_extra));
	),

	TP_printk("dst=%u h_seq=%u h_version=%s h_lockspace=%u h_nodeid=%u "
		  "h_length=%u h_cmd=%s m_type=%s m_nodeid=%u "
		  "m_pid=%u m_lkid=%u m_remid=%u m_parent_lkid=%u "
		  "m_parent_remid=%u m_exflags=%s m_sbflags=%s m_flags=%s "
		  "m_lvbseq=%u m_hash=%u m_status=%d m_grmode=%s "
		  "m_rqmode=%s m_bastmode=%s m_asts=%d m_result=%d "
		  "m_extra=0x%s", __entry->dst,
		  __entry->h_seq, show_message_version(__entry->h_version),
		  __entry->h_lockspace, __entry->h_nodeid, __entry->h_length,
		  show_header_cmd(__entry->h_cmd),
		  show_message_type(__entry->m_type),
		  __entry->m_nodeid, __entry->m_pid, __entry->m_lkid,
		  __entry->m_remid, __entry->m_parent_lkid,
		  __entry->m_parent_remid, show_lock_flags(__entry->m_exflags),
		  show_dlm_sb_flags(__entry->m_sbflags),
		  show_lkb_flags(__entry->m_flags), __entry->m_lvbseq,
		  __entry->m_hash, __entry->m_status,
		  show_lock_mode(__entry->m_grmode),
		  show_lock_mode(__entry->m_rqmode),
		  show_lock_mode(__entry->m_bastmode),
		  __entry->m_asts, __entry->m_result,
		  __print_hex_str(__get_dynamic_array(m_extra),
				  __get_dynamic_array_len(m_extra)))

);

DECLARE_EVENT_CLASS(dlm_plock_template,

	TP_PROTO(const struct dlm_plock_info *info),

	TP_ARGS(info),

	TP_STRUCT__entry(
		__field(uint8_t, optype)
		__field(uint8_t, ex)
		__field(uint8_t, wait)
		__field(uint8_t, flags)
		__field(uint32_t, pid)
		__field(int32_t, nodeid)
		__field(int32_t, rv)
		__field(uint32_t, fsid)
		__field(uint64_t, number)
		__field(uint64_t, start)
		__field(uint64_t, end)
		__field(uint64_t, owner)
	),

	TP_fast_assign(
		__entry->optype = info->optype;
		__entry->ex = info->ex;
		__entry->wait = info->wait;
		__entry->flags = info->flags;
		__entry->pid = info->pid;
		__entry->nodeid = info->nodeid;
		__entry->rv = info->rv;
		__entry->fsid = info->fsid;
		__entry->number = info->number;
		__entry->start = info->start;
		__entry->end = info->end;
		__entry->owner = info->owner;
	),

	TP_printk("fsid=%u number=%llx owner=%llx optype=%d ex=%d wait=%d flags=%x pid=%u nodeid=%d rv=%d start=%llx end=%llx",
		  __entry->fsid, __entry->number, __entry->owner,
		  __entry->optype, __entry->ex, __entry->wait,
		  __entry->flags, __entry->pid, __entry->nodeid,
		  __entry->rv, __entry->start, __entry->end)

);

DEFINE_EVENT(dlm_plock_template, dlm_plock_read,
	     TP_PROTO(const struct dlm_plock_info *info), TP_ARGS(info));

DEFINE_EVENT(dlm_plock_template, dlm_plock_write,
	     TP_PROTO(const struct dlm_plock_info *info), TP_ARGS(info));

TRACE_EVENT(dlm_send,

	TP_PROTO(int nodeid, int ret),

	TP_ARGS(nodeid, ret),

	TP_STRUCT__entry(
		__field(int, nodeid)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->nodeid = nodeid;
		__entry->ret = ret;
	),

	TP_printk("nodeid=%d ret=%d", __entry->nodeid, __entry->ret)

);

TRACE_EVENT(dlm_recv,

	TP_PROTO(int nodeid, int ret),

	TP_ARGS(nodeid, ret),

	TP_STRUCT__entry(
		__field(int, nodeid)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->nodeid = nodeid;
		__entry->ret = ret;
	),

	TP_printk("nodeid=%d ret=%d", __entry->nodeid, __entry->ret)

);

#endif /* if !defined(_TRACE_DLM_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>
