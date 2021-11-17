/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM spmi

#if !defined(_TRACE_SPMI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SPMI_H

#include <linux/spmi.h>
#include <linux/tracepoint.h>

/*
 * drivers/spmi/spmi.c
 */

TRACE_EVENT(spmi_write_begin,
	TP_PROTO(u8 opcode, u8 sid, u16 addr, u8 len, const u8 *buf),
	TP_ARGS(opcode, sid, addr, len, buf),

	TP_STRUCT__entry(
		__field		( u8,         opcode    )
		__field		( u8,         sid       )
		__field		( u16,        addr      )
		__field		( u8,         len       )
		__dynamic_array	( u8,   buf,  len + 1   )
	),

	TP_fast_assign(
		__entry->opcode = opcode;
		__entry->sid    = sid;
		__entry->addr   = addr;
		__entry->len    = len + 1;
		memcpy(__get_dynamic_array(buf), buf, len + 1);
	),

	TP_printk("opc=%d sid=%02d addr=0x%04x len=%d buf=0x[%*phD]",
		  (int)__entry->opcode, (int)__entry->sid,
		  (int)__entry->addr, (int)__entry->len,
		  (int)__entry->len, __get_dynamic_array(buf))
);

TRACE_EVENT(spmi_write_end,
	TP_PROTO(u8 opcode, u8 sid, u16 addr, int ret),
	TP_ARGS(opcode, sid, addr, ret),

	TP_STRUCT__entry(
		__field		( u8,         opcode    )
		__field		( u8,         sid       )
		__field		( u16,        addr      )
		__field		( int,        ret       )
	),

	TP_fast_assign(
		__entry->opcode = opcode;
		__entry->sid    = sid;
		__entry->addr   = addr;
		__entry->ret    = ret;
	),

	TP_printk("opc=%d sid=%02d addr=0x%04x ret=%d",
		  (int)__entry->opcode, (int)__entry->sid,
		  (int)__entry->addr, __entry->ret)
);

TRACE_EVENT(spmi_read_begin,
	TP_PROTO(u8 opcode, u8 sid, u16 addr),
	TP_ARGS(opcode, sid, addr),

	TP_STRUCT__entry(
		__field		( u8,         opcode    )
		__field		( u8,         sid       )
		__field		( u16,        addr      )
	),

	TP_fast_assign(
		__entry->opcode = opcode;
		__entry->sid    = sid;
		__entry->addr   = addr;
	),

	TP_printk("opc=%d sid=%02d addr=0x%04x",
		  (int)__entry->opcode, (int)__entry->sid,
		  (int)__entry->addr)
);

TRACE_EVENT(spmi_read_end,
	TP_PROTO(u8 opcode, u8 sid, u16 addr, int ret, u8 len, const u8 *buf),
	TP_ARGS(opcode, sid, addr, ret, len, buf),

	TP_STRUCT__entry(
		__field		( u8,         opcode    )
		__field		( u8,         sid       )
		__field		( u16,        addr      )
		__field		( int,        ret       )
		__field		( u8,         len       )
		__dynamic_array	( u8,   buf,  len + 1   )
	),

	TP_fast_assign(
		__entry->opcode = opcode;
		__entry->sid    = sid;
		__entry->addr   = addr;
		__entry->ret    = ret;
		__entry->len    = len + 1;
		memcpy(__get_dynamic_array(buf), buf, len + 1);
	),

	TP_printk("opc=%d sid=%02d addr=0x%04x ret=%d len=%02d buf=0x[%*phD]",
		  (int)__entry->opcode, (int)__entry->sid,
		  (int)__entry->addr, __entry->ret, (int)__entry->len,
		  (int)__entry->len, __get_dynamic_array(buf))
);

TRACE_EVENT(spmi_cmd,
	TP_PROTO(u8 opcode, u8 sid, int ret),
	TP_ARGS(opcode, sid, ret),

	TP_STRUCT__entry(
		__field		( u8,         opcode    )
		__field		( u8,         sid       )
		__field		( int,        ret       )
	),

	TP_fast_assign(
		__entry->opcode = opcode;
		__entry->sid    = sid;
		__entry->ret    = ret;
	),

	TP_printk("opc=%d sid=%02d ret=%d", (int)__entry->opcode,
		  (int)__entry->sid, ret)
);

#endif /* _TRACE_SPMI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
