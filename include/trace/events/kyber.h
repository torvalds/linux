/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM kyber

#if !defined(_TRACE_KYBER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KYBER_H

#include <linux/blkdev.h>
#include <linux/tracepoint.h>

#define DOMAIN_LEN		16
#define LATENCY_TYPE_LEN	8

TRACE_EVENT(kyber_latency,

	TP_PROTO(struct request_queue *q, const char *domain, const char *type,
		 unsigned int percentile, unsigned int numerator,
		 unsigned int denominator, unsigned int samples),

	TP_ARGS(q, domain, type, percentile, numerator, denominator, samples),

	TP_STRUCT__entry(
		__field(	dev_t,	dev				)
		__array(	char,	domain,	DOMAIN_LEN		)
		__array(	char,	type,	LATENCY_TYPE_LEN	)
		__field(	u8,	percentile			)
		__field(	u8,	numerator			)
		__field(	u8,	denominator			)
		__field(	unsigned int,	samples			)
	),

	TP_fast_assign(
		__entry->dev		= disk_devt(q->disk);
		strlcpy(__entry->domain, domain, sizeof(__entry->domain));
		strlcpy(__entry->type, type, sizeof(__entry->type));
		__entry->percentile	= percentile;
		__entry->numerator	= numerator;
		__entry->denominator	= denominator;
		__entry->samples	= samples;
	),

	TP_printk("%d,%d %s %s p%u %u/%u samples=%u",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->domain,
		  __entry->type, __entry->percentile, __entry->numerator,
		  __entry->denominator, __entry->samples)
);

TRACE_EVENT(kyber_adjust,

	TP_PROTO(struct request_queue *q, const char *domain,
		 unsigned int depth),

	TP_ARGS(q, domain, depth),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__array(	char,	domain,	DOMAIN_LEN	)
		__field(	unsigned int,	depth		)
	),

	TP_fast_assign(
		__entry->dev		= disk_devt(q->disk);
		strlcpy(__entry->domain, domain, sizeof(__entry->domain));
		__entry->depth		= depth;
	),

	TP_printk("%d,%d %s %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->domain,
		  __entry->depth)
);

TRACE_EVENT(kyber_throttled,

	TP_PROTO(struct request_queue *q, const char *domain),

	TP_ARGS(q, domain),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__array(	char,	domain,	DOMAIN_LEN	)
	),

	TP_fast_assign(
		__entry->dev		= disk_devt(q->disk);
		strlcpy(__entry->domain, domain, sizeof(__entry->domain));
	),

	TP_printk("%d,%d %s", MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->domain)
);

#define _TRACE_KYBER_H
#endif /* _TRACE_KYBER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
