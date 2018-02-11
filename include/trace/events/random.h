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
	TP_PROTO(const char *pool_name, int bytes, unsigned long IP),

	TP_ARGS(pool_name, bytes, IP),

	TP_STRUCT__entry(
		__field( const char *,	pool_name		)
		__field(	  int,	bytes			)
		__field(unsigned long,	IP			)
	),

	TP_fast_assign(
		__entry->pool_name	= pool_name;
		__entry->bytes		= bytes;
		__entry->IP		= IP;
	),

	TP_printk("%s pool: bytes %d caller %pS",
		  __entry->pool_name, __entry->bytes, (void *)__entry->IP)
);

DEFINE_EVENT(random__mix_pool_bytes, mix_pool_bytes,
	TP_PROTO(const char *pool_name, int bytes, unsigned long IP),

	TP_ARGS(pool_name, bytes, IP)
);

DEFINE_EVENT(random__mix_pool_bytes, mix_pool_bytes_nolock,
	TP_PROTO(const char *pool_name, int bytes, unsigned long IP),

	TP_ARGS(pool_name, bytes, IP)
);

TRACE_EVENT(credit_entropy_bits,
	TP_PROTO(const char *pool_name, int bits, int entropy_count,
		 int entropy_total, unsigned long IP),

	TP_ARGS(pool_name, bits, entropy_count, entropy_total, IP),

	TP_STRUCT__entry(
		__field( const char *,	pool_name		)
		__field(	  int,	bits			)
		__field(	  int,	entropy_count		)
		__field(	  int,	entropy_total		)
		__field(unsigned long,	IP			)
	),

	TP_fast_assign(
		__entry->pool_name	= pool_name;
		__entry->bits		= bits;
		__entry->entropy_count	= entropy_count;
		__entry->entropy_total	= entropy_total;
		__entry->IP		= IP;
	),

	TP_printk("%s pool: bits %d entropy_count %d entropy_total %d "
		  "caller %pS", __entry->pool_name, __entry->bits,
		  __entry->entropy_count, __entry->entropy_total,
		  (void *)__entry->IP)
);

TRACE_EVENT(push_to_pool,
	TP_PROTO(const char *pool_name, int pool_bits, int input_bits),

	TP_ARGS(pool_name, pool_bits, input_bits),

	TP_STRUCT__entry(
		__field( const char *,	pool_name		)
		__field(	  int,	pool_bits		)
		__field(	  int,	input_bits		)
	),

	TP_fast_assign(
		__entry->pool_name	= pool_name;
		__entry->pool_bits	= pool_bits;
		__entry->input_bits	= input_bits;
	),

	TP_printk("%s: pool_bits %d input_pool_bits %d",
		  __entry->pool_name, __entry->pool_bits,
		  __entry->input_bits)
);

TRACE_EVENT(debit_entropy,
	TP_PROTO(const char *pool_name, int debit_bits),

	TP_ARGS(pool_name, debit_bits),

	TP_STRUCT__entry(
		__field( const char *,	pool_name		)
		__field(	  int,	debit_bits		)
	),

	TP_fast_assign(
		__entry->pool_name	= pool_name;
		__entry->debit_bits	= debit_bits;
	),

	TP_printk("%s: debit_bits %d", __entry->pool_name,
		  __entry->debit_bits)
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

TRACE_EVENT(xfer_secondary_pool,
	TP_PROTO(const char *pool_name, int xfer_bits, int request_bits,
		 int pool_entropy, int input_entropy),

	TP_ARGS(pool_name, xfer_bits, request_bits, pool_entropy,
		input_entropy),

	TP_STRUCT__entry(
		__field( const char *,	pool_name		)
		__field(	  int,	xfer_bits		)
		__field(	  int,	request_bits		)
		__field(	  int,	pool_entropy		)
		__field(	  int,	input_entropy		)
	),

	TP_fast_assign(
		__entry->pool_name	= pool_name;
		__entry->xfer_bits	= xfer_bits;
		__entry->request_bits	= request_bits;
		__entry->pool_entropy	= pool_entropy;
		__entry->input_entropy	= input_entropy;
	),

	TP_printk("pool %s xfer_bits %d request_bits %d pool_entropy %d "
		  "input_entropy %d", __entry->pool_name, __entry->xfer_bits,
		  __entry->request_bits, __entry->pool_entropy,
		  __entry->input_entropy)
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
	TP_PROTO(const char *pool_name, int nbytes, int entropy_count,
		 unsigned long IP),

	TP_ARGS(pool_name, nbytes, entropy_count, IP),

	TP_STRUCT__entry(
		__field( const char *,	pool_name		)
		__field(	  int,	nbytes			)
		__field(	  int,	entropy_count		)
		__field(unsigned long,	IP			)
	),

	TP_fast_assign(
		__entry->pool_name	= pool_name;
		__entry->nbytes		= nbytes;
		__entry->entropy_count	= entropy_count;
		__entry->IP		= IP;
	),

	TP_printk("%s pool: nbytes %d entropy_count %d caller %pS",
		  __entry->pool_name, __entry->nbytes, __entry->entropy_count,
		  (void *)__entry->IP)
);


DEFINE_EVENT(random__extract_entropy, extract_entropy,
	TP_PROTO(const char *pool_name, int nbytes, int entropy_count,
		 unsigned long IP),

	TP_ARGS(pool_name, nbytes, entropy_count, IP)
);

DEFINE_EVENT(random__extract_entropy, extract_entropy_user,
	TP_PROTO(const char *pool_name, int nbytes, int entropy_count,
		 unsigned long IP),

	TP_ARGS(pool_name, nbytes, entropy_count, IP)
);

TRACE_EVENT(random_read,
	TP_PROTO(int got_bits, int need_bits, int pool_left, int input_left),

	TP_ARGS(got_bits, need_bits, pool_left, input_left),

	TP_STRUCT__entry(
		__field(	  int,	got_bits		)
		__field(	  int,	need_bits		)
		__field(	  int,	pool_left		)
		__field(	  int,	input_left		)
	),

	TP_fast_assign(
		__entry->got_bits	= got_bits;
		__entry->need_bits	= need_bits;
		__entry->pool_left	= pool_left;
		__entry->input_left	= input_left;
	),

	TP_printk("got_bits %d still_needed_bits %d "
		  "blocking_pool_entropy_left %d input_entropy_left %d",
		  __entry->got_bits, __entry->got_bits, __entry->pool_left,
		  __entry->input_left)
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

#endif /* _TRACE_RANDOM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
