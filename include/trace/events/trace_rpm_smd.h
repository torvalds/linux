/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rpm_smd

#if !defined(_TRACE_RPM_SMD_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RPM_SMD_H

#include <linux/tracepoint.h>

TRACE_EVENT(rpm_smd_ack_recvd,

	TP_PROTO(unsigned int irq, unsigned int msg_id, int errno),

	TP_ARGS(irq, msg_id, errno),

	TP_STRUCT__entry(
		__field(int, irq)
		__field(int, msg_id)
		__field(int, errno)
	),

	TP_fast_assign(
		__entry->irq = irq;
		__entry->msg_id = msg_id;
		__entry->errno = errno;
	),

	TP_printk("ctx:%s msg_id:%d errno:%08x",
		__entry->irq ? "noslp" : "sleep",
		__entry->msg_id,
		__entry->errno)
);

TRACE_EVENT(rpm_smd_interrupt_notify,

	TP_PROTO(char *dummy),

	TP_ARGS(dummy),

	TP_STRUCT__entry(
		__field(char *, dummy)
	),

	TP_fast_assign(
		__entry->dummy = dummy;
	),

	TP_printk("%s", __entry->dummy)
);

DECLARE_EVENT_CLASS(rpm_send_msg,

	TP_PROTO(unsigned int msg_id, unsigned int rsc_type,
		unsigned int rsc_id),

	TP_ARGS(msg_id, rsc_type, rsc_id),

	TP_STRUCT__entry(
		__field(u32, msg_id)
		__field(u32, rsc_type)
		__field(u32, rsc_id)
		__array(char, name, 5)
	),

	TP_fast_assign(
		__entry->msg_id = msg_id;
		__entry->name[4] = 0;
		__entry->rsc_type = rsc_type;
		__entry->rsc_id = rsc_id;
		memcpy(__entry->name, &rsc_type, sizeof(uint32_t));

	),

	TP_printk("msg_id:%d, rsc_type:0x%08x(%s), rsc_id:0x%08x",
			__entry->msg_id,
			__entry->rsc_type, __entry->name,
			__entry->rsc_id)
);

DEFINE_EVENT(rpm_send_msg, rpm_smd_sleep_set,
	TP_PROTO(unsigned int msg_id, unsigned int rsc_type,
		unsigned int rsc_id),
	TP_ARGS(msg_id, rsc_type, rsc_id)
);

DEFINE_EVENT(rpm_send_msg, rpm_smd_send_sleep_set,
	TP_PROTO(unsigned int msg_id, unsigned int rsc_type,
		unsigned int rsc_id),
	TP_ARGS(msg_id, rsc_type, rsc_id)
);

DEFINE_EVENT(rpm_send_msg, rpm_smd_send_active_set,
	TP_PROTO(unsigned int msg_id, unsigned int rsc_type,
		unsigned int rsc_id),
	TP_ARGS(msg_id, rsc_type, rsc_id)
);

#endif
#define TRACE_INCLUDE_FILE trace_rpm_smd
#include <trace/define_trace.h>
