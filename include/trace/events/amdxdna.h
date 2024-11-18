/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023-2024, Advanced Micro Devices, Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM amdxdna

#if !defined(_TRACE_AMDXDNA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_AMDXDNA_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(xdna_mbox_msg,
		    TP_PROTO(char *name, u8 chann_id, u32 opcode, u32 msg_id),

		    TP_ARGS(name, chann_id, opcode, msg_id),

		    TP_STRUCT__entry(__string(name, name)
				     __field(u32, chann_id)
				     __field(u32, opcode)
				     __field(u32, msg_id)),

		    TP_fast_assign(__assign_str(name);
				   __entry->chann_id = chann_id;
				   __entry->opcode = opcode;
				   __entry->msg_id = msg_id;),

		    TP_printk("%s.%d id 0x%x opcode 0x%x", __get_str(name),
			      __entry->chann_id, __entry->msg_id, __entry->opcode)
);

DEFINE_EVENT(xdna_mbox_msg, mbox_set_tail,
	     TP_PROTO(char *name, u8 chann_id, u32 opcode, u32 id),
	     TP_ARGS(name, chann_id, opcode, id)
);

DEFINE_EVENT(xdna_mbox_msg, mbox_set_head,
	     TP_PROTO(char *name, u8 chann_id, u32 opcode, u32 id),
	     TP_ARGS(name, chann_id, opcode, id)
);

TRACE_EVENT(mbox_irq_handle,
	    TP_PROTO(char *name, int irq),

	    TP_ARGS(name, irq),

	    TP_STRUCT__entry(__string(name, name)
			     __field(int, irq)),

	    TP_fast_assign(__assign_str(name);
			   __entry->irq = irq;),

	    TP_printk("%s.%d", __get_str(name), __entry->irq)
);

#endif /* !defined(_TRACE_AMDXDNA_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>
