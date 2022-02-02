/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM random

#if !defined(_TRACE_RANDOM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RANDOM_H

#include <linux/writeback.h>
#include <linux/tracepoint.h>

TRACE_EVENT(add_device_randomness,
	TP_PROTO(int bytes, unsigned long IP),

	TP_ARGS(bytes, IP),

	TP_STRUCT__entry(
		__field(	  int,	bytes			)
		__field(unsigned long,	IP			)
	),

	TP_fast_assign(
		__entry->bytes		= bytes;
		__entry->IP		= IP;
	),

	TP_printk("bytes %d caller %pS",
		__entry->bytes, (void *)__entry->IP)
);

DECLARE_EVENT_CLASS(random__mix_pool_bytes,
	TP_PROTO(int bytes, unsigned long IP),

	TP_ARGS(bytes, IP),

	TP_STRUCT__entry(
		__field(	  int,	bytes			)
		__field(unsigned long,	IP			)
	),

	TP_fast_assign(
		__entry->bytes		= bytes;
		__entry->IP		= IP;
	),

	TP_printk("input pool: bytes %d caller %pS",
		  __entry->bytes, (void *)__entry->IP)
);

DEFINE_EVENT(random__mix_pool_bytes, mix_pool_bytes,
	TP_PROTO(int bytes, unsigned long IP),

	TP_ARGS(bytes, IP)
);

DEFINE_EVENT(random__mix_pool_bytes, mix_pool_bytes_nolock,
	TP_PROTO(int bytes, unsigned long IP),

	TP_ARGS(bytes, IP)
);

TRACE_EVENT(credit_entropy_bits,
	TP_PROTO(int bits, int entropy_count, unsigned long IP),

	TP_ARGS(bits, entropy_count, IP),

	TP_STRUCT__entry(
		__field(	  int,	bits			)
		__field(	  int,	entropy_count		)
		__field(unsigned long,	IP			)
	),

	TP_fast_assign(
		__entry->bits		= bits;
		__entry->entropy_count	= entropy_count;
		__entry->IP		= IP;
	),

	TP_printk("input pool: bits %d entropy_count %d caller %pS",
		  __entry->bits, __entry->entropy_count, (void *)__entry->IP)
);

TRACE_EVENT(add_input_randomness,
	TP_PROTO(int input_bits),

	TP_ARGS(input_bits),

	TP_STRUCT__entry(
		__field(	  int,	input_bits		)
	),

	TP_fast_assign(
		__entry->input_bits	= input_bits;
	),

	TP_printk("input_pool_bits %d", __entry->input_bits)
);

TRACE_EVENT(add_disk_randomness,
	TP_PROTO(dev_t dev, int input_bits),

	TP_ARGS(dev, input_bits),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	  int,	input_bits		)
	),

	TP_fast_assign(
		__entry->dev		= dev;
		__entry->input_bits	= input_bits;
	),

	TP_printk("dev %d,%d input_pool_bits %d", MAJOR(__entry->dev),
		  MINOR(__entry->dev), __entry->input_bits)
);

DECLARE_EVENT_CLASS(random__get_random_bytes,
	TP_PROTO(int nbytes, unsigned long IP),

	TP_ARGS(nbytes, IP),

	TP_STRUCT__entry(
		__field(	  int,	nbytes			)
		__field(unsigned long,	IP			)
	),

	TP_fast_assign(
		__entry->nbytes		= nbytes;
		__entry->IP		= IP;
	),

	TP_printk("nbytes %d caller %pS", __entry->nbytes, (void *)__entry->IP)
);

DEFINE_EVENT(random__get_random_bytes, get_random_bytes,
	TP_PROTO(int nbytes, unsigned long IP),

	TP_ARGS(nbytes, IP)
);

DEFINE_EVENT(random__get_random_bytes, get_random_bytes_arch,
	TP_PROTO(int nbytes, unsigned long IP),

	TP_ARGS(nbytes, IP)
);

DECLARE_EVENT_CLASS(random__extract_entropy,
	TP_PROTO(int nbytes, int entropy_count),

	TP_ARGS(nbytes, entropy_count),

	TP_STRUCT__entry(
		__field(	  int,	nbytes			)
		__field(	  int,	entropy_count		)
	),

	TP_fast_assign(
		__entry->nbytes		= nbytes;
		__entry->entropy_count	= entropy_count;
	),

	TP_printk("input pool: nbytes %d entropy_count %d",
		  __entry->nbytes, __entry->entropy_count)
);


DEFINE_EVENT(random__extract_entropy, extract_entropy,
	TP_PROTO(int nbytes, int entropy_count),

	TP_ARGS(nbytes, entropy_count)
);

TRACE_EVENT(urandom_read,
	TP_PROTO(int got_bits, int pool_left, int input_left),

	TP_ARGS(got_bits, pool_left, input_left),

	TP_STRUCT__entry(
		__field(	  int,	got_bits		)
		__field(	  int,	pool_left		)
		__field(	  int,	input_left		)
	),

	TP_fast_assign(
		__entry->got_bits	= got_bits;
		__entry->pool_left	= pool_left;
		__entry->input_left	= input_left;
	),

	TP_printk("got_bits %d nonblocking_pool_entropy_left %d "
		  "input_entropy_left %d", __entry->got_bits,
		  __entry->pool_left, __entry->input_left)
);

TRACE_EVENT(prandom_u32,

	TP_PROTO(unsigned int ret),

	TP_ARGS(ret),

	TP_STRUCT__entry(
		__field(   unsigned int, ret)
	),

	TP_fast_assign(
		__entry->ret = ret;
	),

	TP_printk("ret=%u" , __entry->ret)
);

#endif /* _TRACE_RANDOM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
