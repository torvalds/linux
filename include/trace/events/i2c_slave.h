/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * I2C slave tracepoints
 *
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM i2c_slave

#if !defined(_TRACE_I2C_SLAVE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_I2C_SLAVE_H

#include <linux/i2c.h>
#include <linux/tracepoint.h>

TRACE_DEFINE_ENUM(I2C_SLAVE_READ_REQUESTED);
TRACE_DEFINE_ENUM(I2C_SLAVE_WRITE_REQUESTED);
TRACE_DEFINE_ENUM(I2C_SLAVE_READ_PROCESSED);
TRACE_DEFINE_ENUM(I2C_SLAVE_WRITE_RECEIVED);
TRACE_DEFINE_ENUM(I2C_SLAVE_STOP);

#define show_event_type(type)						\
	__print_symbolic(type,						\
		{ I2C_SLAVE_READ_REQUESTED,	"RD_REQ" },		\
		{ I2C_SLAVE_WRITE_REQUESTED,	"WR_REQ" },		\
		{ I2C_SLAVE_READ_PROCESSED,	"RD_PRO" },		\
		{ I2C_SLAVE_WRITE_RECEIVED,	"WR_RCV" },		\
		{ I2C_SLAVE_STOP,		"  STOP" })

TRACE_EVENT(i2c_slave,
	TP_PROTO(const struct i2c_client *client, enum i2c_slave_event event,
		 __u8 *val, int cb_ret),
	TP_ARGS(client, event, val, cb_ret),
	TP_STRUCT__entry(
		__field(int,				adapter_nr	)
		__field(int,				ret		)
		__field(__u16,				addr		)
		__field(__u16,				len		)
		__field(enum i2c_slave_event,		event		)
		__array(__u8,				buf,	1)	),

	TP_fast_assign(
		__entry->adapter_nr = client->adapter->nr;
		__entry->addr = client->addr;
		__entry->event = event;
		__entry->ret = cb_ret;
		switch (event) {
		case I2C_SLAVE_READ_REQUESTED:
		case I2C_SLAVE_READ_PROCESSED:
		case I2C_SLAVE_WRITE_RECEIVED:
			__entry->len = 1;
			memcpy(__entry->buf, val, __entry->len);
			break;
		default:
			__entry->len = 0;
			break;
		}
		),
	TP_printk("i2c-%d a=%03x ret=%d %s [%*phD]",
		__entry->adapter_nr, __entry->addr, __entry->ret,
		show_event_type(__entry->event), __entry->len, __entry->buf
		));

#endif /* _TRACE_I2C_SLAVE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
