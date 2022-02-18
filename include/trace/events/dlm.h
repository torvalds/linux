/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dlm

#if !defined(_TRACE_DLM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DLM_H

#include <linux/dlm.h>
#include <linux/dlmconstants.h>
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

/* note: we begin tracing dlm_lock_start() only if ls and lkb are found */
TRACE_EVENT(dlm_lock_start,

	TP_PROTO(struct dlm_ls *ls, struct dlm_lkb *lkb, int mode,
		 __u32 flags),

	TP_ARGS(ls, lkb, mode, flags),

	TP_STRUCT__entry(
		__field(__u32, ls_id)
		__field(__u32, lkb_id)
		__field(int, mode)
		__field(__u32, flags)
	),

	TP_fast_assign(
		__entry->ls_id = ls->ls_global_id;
		__entry->lkb_id = lkb->lkb_id;
		__entry->mode = mode;
		__entry->flags = flags;
	),

	TP_printk("ls_id=%u lkb_id=%x mode=%s flags=%s",
		  __entry->ls_id, __entry->lkb_id,
		  show_lock_mode(__entry->mode),
		  show_lock_flags(__entry->flags))

);

TRACE_EVENT(dlm_lock_end,

	TP_PROTO(struct dlm_ls *ls, struct dlm_lkb *lkb, int mode, __u32 flags,
		 int error),

	TP_ARGS(ls, lkb, mode, flags, error),

	TP_STRUCT__entry(
		__field(__u32, ls_id)
		__field(__u32, lkb_id)
		__field(int, mode)
		__field(__u32, flags)
		__field(int, error)
	),

	TP_fast_assign(
		__entry->ls_id = ls->ls_global_id;
		__entry->lkb_id = lkb->lkb_id;
		__entry->mode = mode;
		__entry->flags = flags;

		/* return value will be zeroed in those cases by dlm_lock()
		 * we do it here again to not introduce more overhead if
		 * trace isn't running and error reflects the return value.
		 */
		if (error == -EAGAIN || error == -EDEADLK)
			__entry->error = 0;
		else
			__entry->error = error;
	),

	TP_printk("ls_id=%u lkb_id=%x mode=%s flags=%s error=%d",
		  __entry->ls_id, __entry->lkb_id,
		  show_lock_mode(__entry->mode),
		  show_lock_flags(__entry->flags), __entry->error)

);

TRACE_EVENT(dlm_bast,

	TP_PROTO(struct dlm_ls *ls, struct dlm_lkb *lkb, int mode),

	TP_ARGS(ls, lkb, mode),

	TP_STRUCT__entry(
		__field(__u32, ls_id)
		__field(__u32, lkb_id)
		__field(int, mode)
	),

	TP_fast_assign(
		__entry->ls_id = ls->ls_global_id;
		__entry->lkb_id = lkb->lkb_id;
		__entry->mode = mode;
	),

	TP_printk("ls_id=%u lkb_id=%x mode=%s", __entry->ls_id,
		  __entry->lkb_id, show_lock_mode(__entry->mode))

);

TRACE_EVENT(dlm_ast,

	TP_PROTO(struct dlm_ls *ls, struct dlm_lkb *lkb, struct dlm_lksb *lksb),

	TP_ARGS(ls, lkb, lksb),

	TP_STRUCT__entry(
		__field(__u32, ls_id)
		__field(__u32, lkb_id)
		__field(u8, sb_flags)
		__field(int, sb_status)
	),

	TP_fast_assign(
		__entry->ls_id = ls->ls_global_id;
		__entry->lkb_id = lkb->lkb_id;
		__entry->sb_flags = lksb->sb_flags;
		__entry->sb_status = lksb->sb_status;
	),

	TP_printk("ls_id=%u lkb_id=%x sb_flags=%s sb_status=%d",
		  __entry->ls_id, __entry->lkb_id,
		  show_dlm_sb_flags(__entry->sb_flags), __entry->sb_status)

);

/* note: we begin tracing dlm_unlock_start() only if ls and lkb are found */
TRACE_EVENT(dlm_unlock_start,

	TP_PROTO(struct dlm_ls *ls, struct dlm_lkb *lkb, __u32 flags),

	TP_ARGS(ls, lkb, flags),

	TP_STRUCT__entry(
		__field(__u32, ls_id)
		__field(__u32, lkb_id)
		__field(__u32, flags)
	),

	TP_fast_assign(
		__entry->ls_id = ls->ls_global_id;
		__entry->lkb_id = lkb->lkb_id;
		__entry->flags = flags;
	),

	TP_printk("ls_id=%u lkb_id=%x flags=%s",
		  __entry->ls_id, __entry->lkb_id,
		  show_lock_flags(__entry->flags))

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
	),

	TP_fast_assign(
		__entry->ls_id = ls->ls_global_id;
		__entry->lkb_id = lkb->lkb_id;
		__entry->flags = flags;
		__entry->error = error;
	),

	TP_printk("ls_id=%u lkb_id=%x flags=%s error=%d",
		  __entry->ls_id, __entry->lkb_id,
		  show_lock_flags(__entry->flags), __entry->error)

);

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
