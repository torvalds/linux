/* SMBUS message transfer tracepoints
 *
 * Copyright (C) 2013 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM smbus

#if !defined(_TRACE_SMBUS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SMBUS_H

#include <linux/i2c.h>
#include <linux/tracepoint.h>

/*
 * drivers/i2c/i2c-core-smbus.c
 */

/*
 * i2c_smbus_xfer() write data or procedure call request
 */
TRACE_EVENT_CONDITION(smbus_write,
	TP_PROTO(const struct i2c_adapter *adap,
		 u16 addr, unsigned short flags,
		 char read_write, u8 command, int protocol,
		 const union i2c_smbus_data *data),
	TP_ARGS(adap, addr, flags, read_write, command, protocol, data),
	TP_CONDITION(read_write == I2C_SMBUS_WRITE ||
		     protocol == I2C_SMBUS_PROC_CALL ||
		     protocol == I2C_SMBUS_BLOCK_PROC_CALL),
	TP_STRUCT__entry(
		__field(int,	adapter_nr		)
		__field(__u16,	addr			)
		__field(__u16,	flags			)
		__field(__u8,	command			)
		__field(__u8,	len			)
		__field(__u32,	protocol		)
		__array(__u8, buf, I2C_SMBUS_BLOCK_MAX + 2)	),
	TP_fast_assign(
		__entry->adapter_nr = adap->nr;
		__entry->addr = addr;
		__entry->flags = flags;
		__entry->command = command;
		__entry->protocol = protocol;

		switch (protocol) {
		case I2C_SMBUS_BYTE_DATA:
			__entry->len = 1;
			goto copy;
		case I2C_SMBUS_WORD_DATA:
		case I2C_SMBUS_PROC_CALL:
			__entry->len = 2;
			goto copy;
		case I2C_SMBUS_BLOCK_DATA:
		case I2C_SMBUS_BLOCK_PROC_CALL:
		case I2C_SMBUS_I2C_BLOCK_DATA:
			__entry->len = data->block[0] + 1;
		copy:
			memcpy(__entry->buf, data->block, __entry->len);
			break;
		case I2C_SMBUS_QUICK:
		case I2C_SMBUS_BYTE:
		case I2C_SMBUS_I2C_BLOCK_BROKEN:
		default:
			__entry->len = 0;
		}
		       ),
	TP_printk("i2c-%d a=%03x f=%04x c=%x %s l=%u [%*phD]",
		  __entry->adapter_nr,
		  __entry->addr,
		  __entry->flags,
		  __entry->command,
		  __print_symbolic(__entry->protocol,
				   { I2C_SMBUS_QUICK,		"QUICK"	},
				   { I2C_SMBUS_BYTE,		"BYTE"	},
				   { I2C_SMBUS_BYTE_DATA,		"BYTE_DATA" },
				   { I2C_SMBUS_WORD_DATA,		"WORD_DATA" },
				   { I2C_SMBUS_PROC_CALL,		"PROC_CALL" },
				   { I2C_SMBUS_BLOCK_DATA,		"BLOCK_DATA" },
				   { I2C_SMBUS_I2C_BLOCK_BROKEN,	"I2C_BLOCK_BROKEN" },
				   { I2C_SMBUS_BLOCK_PROC_CALL,	"BLOCK_PROC_CALL" },
				   { I2C_SMBUS_I2C_BLOCK_DATA,	"I2C_BLOCK_DATA" }),
		  __entry->len,
		  __entry->len, __entry->buf
		  ));

/*
 * i2c_smbus_xfer() read data request
 */
TRACE_EVENT_CONDITION(smbus_read,
	TP_PROTO(const struct i2c_adapter *adap,
		 u16 addr, unsigned short flags,
		 char read_write, u8 command, int protocol),
	TP_ARGS(adap, addr, flags, read_write, command, protocol),
	TP_CONDITION(!(read_write == I2C_SMBUS_WRITE ||
		       protocol == I2C_SMBUS_PROC_CALL ||
		       protocol == I2C_SMBUS_BLOCK_PROC_CALL)),
	TP_STRUCT__entry(
		__field(int,	adapter_nr		)
		__field(__u16,	flags			)
		__field(__u16,	addr			)
		__field(__u8,	command			)
		__field(__u32,	protocol		)
		__array(__u8, buf, I2C_SMBUS_BLOCK_MAX + 2)	),
	TP_fast_assign(
		__entry->adapter_nr = adap->nr;
		__entry->addr = addr;
		__entry->flags = flags;
		__entry->command = command;
		__entry->protocol = protocol;
		       ),
	TP_printk("i2c-%d a=%03x f=%04x c=%x %s",
		  __entry->adapter_nr,
		  __entry->addr,
		  __entry->flags,
		  __entry->command,
		  __print_symbolic(__entry->protocol,
				   { I2C_SMBUS_QUICK,		"QUICK"	},
				   { I2C_SMBUS_BYTE,		"BYTE"	},
				   { I2C_SMBUS_BYTE_DATA,		"BYTE_DATA" },
				   { I2C_SMBUS_WORD_DATA,		"WORD_DATA" },
				   { I2C_SMBUS_PROC_CALL,		"PROC_CALL" },
				   { I2C_SMBUS_BLOCK_DATA,		"BLOCK_DATA" },
				   { I2C_SMBUS_I2C_BLOCK_BROKEN,	"I2C_BLOCK_BROKEN" },
				   { I2C_SMBUS_BLOCK_PROC_CALL,	"BLOCK_PROC_CALL" },
				   { I2C_SMBUS_I2C_BLOCK_DATA,	"I2C_BLOCK_DATA" })
		  ));

/*
 * i2c_smbus_xfer() read data or procedure call reply
 */
TRACE_EVENT_CONDITION(smbus_reply,
	TP_PROTO(const struct i2c_adapter *adap,
		 u16 addr, unsigned short flags,
		 char read_write, u8 command, int protocol,
		 const union i2c_smbus_data *data, int res),
	TP_ARGS(adap, addr, flags, read_write, command, protocol, data, res),
	TP_CONDITION(res >= 0 && read_write == I2C_SMBUS_READ),
	TP_STRUCT__entry(
		__field(int,	adapter_nr		)
		__field(__u16,	addr			)
		__field(__u16,	flags			)
		__field(__u8,	command			)
		__field(__u8,	len			)
		__field(__u32,	protocol		)
		__array(__u8, buf, I2C_SMBUS_BLOCK_MAX + 2)	),
	TP_fast_assign(
		__entry->adapter_nr = adap->nr;
		__entry->addr = addr;
		__entry->flags = flags;
		__entry->command = command;
		__entry->protocol = protocol;

		switch (protocol) {
		case I2C_SMBUS_BYTE:
		case I2C_SMBUS_BYTE_DATA:
			__entry->len = 1;
			goto copy;
		case I2C_SMBUS_WORD_DATA:
		case I2C_SMBUS_PROC_CALL:
			__entry->len = 2;
			goto copy;
		case I2C_SMBUS_BLOCK_DATA:
		case I2C_SMBUS_BLOCK_PROC_CALL:
		case I2C_SMBUS_I2C_BLOCK_DATA:
			__entry->len = data->block[0] + 1;
		copy:
			memcpy(__entry->buf, data->block, __entry->len);
			break;
		case I2C_SMBUS_QUICK:
		case I2C_SMBUS_I2C_BLOCK_BROKEN:
		default:
			__entry->len = 0;
		}
		       ),
	TP_printk("i2c-%d a=%03x f=%04x c=%x %s l=%u [%*phD]",
		  __entry->adapter_nr,
		  __entry->addr,
		  __entry->flags,
		  __entry->command,
		  __print_symbolic(__entry->protocol,
				   { I2C_SMBUS_QUICK,		"QUICK"	},
				   { I2C_SMBUS_BYTE,		"BYTE"	},
				   { I2C_SMBUS_BYTE_DATA,		"BYTE_DATA" },
				   { I2C_SMBUS_WORD_DATA,		"WORD_DATA" },
				   { I2C_SMBUS_PROC_CALL,		"PROC_CALL" },
				   { I2C_SMBUS_BLOCK_DATA,		"BLOCK_DATA" },
				   { I2C_SMBUS_I2C_BLOCK_BROKEN,	"I2C_BLOCK_BROKEN" },
				   { I2C_SMBUS_BLOCK_PROC_CALL,	"BLOCK_PROC_CALL" },
				   { I2C_SMBUS_I2C_BLOCK_DATA,	"I2C_BLOCK_DATA" }),
		  __entry->len,
		  __entry->len, __entry->buf
		  ));

/*
 * i2c_smbus_xfer() result
 */
TRACE_EVENT(smbus_result,
	    TP_PROTO(const struct i2c_adapter *adap,
		     u16 addr, unsigned short flags,
		     char read_write, u8 command, int protocol,
		     int res),
	    TP_ARGS(adap, addr, flags, read_write, command, protocol, res),
	    TP_STRUCT__entry(
		    __field(int,	adapter_nr		)
		    __field(__u16,	addr			)
		    __field(__u16,	flags			)
		    __field(__u8,	read_write		)
		    __field(__u8,	command			)
		    __field(__s16,	res			)
		    __field(__u32,	protocol		)
			     ),
	    TP_fast_assign(
		    __entry->adapter_nr = adap->nr;
		    __entry->addr = addr;
		    __entry->flags = flags;
		    __entry->read_write = read_write;
		    __entry->command = command;
		    __entry->protocol = protocol;
		    __entry->res = res;
			   ),
	    TP_printk("i2c-%d a=%03x f=%04x c=%x %s %s res=%d",
		      __entry->adapter_nr,
		      __entry->addr,
		      __entry->flags,
		      __entry->command,
		      __print_symbolic(__entry->protocol,
				       { I2C_SMBUS_QUICK,		"QUICK"	},
				       { I2C_SMBUS_BYTE,		"BYTE"	},
				       { I2C_SMBUS_BYTE_DATA,		"BYTE_DATA" },
				       { I2C_SMBUS_WORD_DATA,		"WORD_DATA" },
				       { I2C_SMBUS_PROC_CALL,		"PROC_CALL" },
				       { I2C_SMBUS_BLOCK_DATA,		"BLOCK_DATA" },
				       { I2C_SMBUS_I2C_BLOCK_BROKEN,	"I2C_BLOCK_BROKEN" },
				       { I2C_SMBUS_BLOCK_PROC_CALL,	"BLOCK_PROC_CALL" },
				       { I2C_SMBUS_I2C_BLOCK_DATA,	"I2C_BLOCK_DATA" }),
		      __entry->read_write == I2C_SMBUS_WRITE ? "wr" : "rd",
		      __entry->res
		      ));

#endif /* _TRACE_SMBUS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
