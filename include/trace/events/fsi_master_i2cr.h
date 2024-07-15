/* SPDX-License-Identifier: GPL-2.0-or-later */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM fsi_master_i2cr

#if !defined(_TRACE_FSI_MASTER_I2CR_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FSI_MASTER_I2CR_H

#include <linux/tracepoint.h>

TRACE_EVENT(i2cr_i2c_error,
	TP_PROTO(const struct i2c_client *client, uint32_t command, int rc),
	TP_ARGS(client, command, rc),
	TP_STRUCT__entry(
		__field(int, bus)
		__field(int, rc)
		__array(unsigned char, command, sizeof(uint32_t))
		__field(unsigned short, addr)
	),
	TP_fast_assign(
		__entry->bus = client->adapter->nr;
		__entry->rc = rc;
		memcpy(__entry->command, &command, sizeof(uint32_t));
		__entry->addr = client->addr;
	),
	TP_printk("%d-%02x command:{ %*ph } rc:%d", __entry->bus, __entry->addr,
		  (int)sizeof(uint32_t), __entry->command, __entry->rc)
);

TRACE_EVENT(i2cr_read,
	TP_PROTO(const struct i2c_client *client, uint32_t command, uint64_t *data),
	TP_ARGS(client, command, data),
	TP_STRUCT__entry(
		__field(int, bus)
		__array(unsigned char, data, sizeof(uint64_t))
		__array(unsigned char, command, sizeof(uint32_t))
		__field(unsigned short, addr)
	),
	TP_fast_assign(
		__entry->bus = client->adapter->nr;
		memcpy(__entry->data, data, sizeof(uint64_t));
		memcpy(__entry->command, &command, sizeof(uint32_t));
		__entry->addr = client->addr;
	),
	TP_printk("%d-%02x command:{ %*ph } { %*ph }", __entry->bus, __entry->addr,
		  (int)sizeof(uint32_t), __entry->command, (int)sizeof(uint64_t), __entry->data)
);

TRACE_EVENT(i2cr_status,
	TP_PROTO(const struct i2c_client *client, uint64_t status),
	TP_ARGS(client, status),
	TP_STRUCT__entry(
		__field(uint64_t, status)
		__field(int, bus)
		__field(unsigned short, addr)
	),
	TP_fast_assign(
		__entry->status = status;
		__entry->bus = client->adapter->nr;
		__entry->addr = client->addr;
	),
	TP_printk("%d-%02x %016llx", __entry->bus, __entry->addr, __entry->status)
);

TRACE_EVENT(i2cr_status_error,
	TP_PROTO(const struct i2c_client *client, uint64_t status, uint64_t error, uint64_t log),
	TP_ARGS(client, status, error, log),
	TP_STRUCT__entry(
		__field(uint64_t, error)
		__field(uint64_t, log)
		__field(uint64_t, status)
		__field(int, bus)
		__field(unsigned short, addr)
	),
	TP_fast_assign(
		__entry->error = error;
		__entry->log = log;
		__entry->status = status;
		__entry->bus = client->adapter->nr;
		__entry->addr = client->addr;
	),
	TP_printk("%d-%02x status:%016llx error:%016llx log:%016llx", __entry->bus, __entry->addr,
		  __entry->status, __entry->error, __entry->log)
);

TRACE_EVENT(i2cr_write,
	TP_PROTO(const struct i2c_client *client, uint32_t command, uint64_t data),
	TP_ARGS(client, command, data),
	TP_STRUCT__entry(
		__field(int, bus)
		__array(unsigned char, data, sizeof(uint64_t))
		__array(unsigned char, command, sizeof(uint32_t))
		__field(unsigned short, addr)
	),
	TP_fast_assign(
		__entry->bus = client->adapter->nr;
		memcpy(__entry->data, &data, sizeof(uint64_t));
		memcpy(__entry->command, &command, sizeof(uint32_t));
		__entry->addr = client->addr;
	),
	TP_printk("%d-%02x command:{ %*ph } { %*ph }", __entry->bus, __entry->addr,
		  (int)sizeof(uint32_t), __entry->command, (int)sizeof(uint64_t), __entry->data)
);

#endif

#include <trace/define_trace.h>
