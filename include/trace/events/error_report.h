/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Declarations for error reporting tracepoints.
 *
 * Copyright (C) 2021, Google LLC.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM error_report

#if !defined(_TRACE_ERROR_REPORT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ERROR_REPORT_H

#include <linux/tracepoint.h>

#ifndef __ERROR_REPORT_DECLARE_TRACE_ENUMS_ONCE_ONLY
#define __ERROR_REPORT_DECLARE_TRACE_ENUMS_ONCE_ONLY

enum error_detector {
	ERROR_DETECTOR_KFENCE,
	ERROR_DETECTOR_KASAN,
	ERROR_DETECTOR_WARN,
};

#endif /* __ERROR_REPORT_DECLARE_TRACE_ENUMS_ONCE_ONLY */

#define error_detector_list			\
	EM(ERROR_DETECTOR_KFENCE, "kfence")	\
	EM(ERROR_DETECTOR_KASAN, "kasan")	\
	EMe(ERROR_DETECTOR_WARN, "warning")
/* Always end the list with an EMe. */

#undef EM
#undef EMe

#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

error_detector_list

#undef EM
#undef EMe

#define EM(a, b) { a, b },
#define EMe(a, b) { a, b }

#define show_error_detector_list(val) \
	__print_symbolic(val, error_detector_list)

DECLARE_EVENT_CLASS(error_report_template,
		    TP_PROTO(enum error_detector error_detector, unsigned long id),
		    TP_ARGS(error_detector, id),
		    TP_STRUCT__entry(__field(enum error_detector, error_detector)
					     __field(unsigned long, id)),
		    TP_fast_assign(__entry->error_detector = error_detector;
				   __entry->id = id;),
		    TP_printk("[%s] %lx",
			      show_error_detector_list(__entry->error_detector),
			      __entry->id));

/**
 * error_report_end - called after printing the error report
 * @error_detector:	short string describing the error detection tool
 * @id:			pseudo-unique descriptor identifying the report
 *			(e.g. the memory access address)
 *
 * This event occurs right after a debugging tool finishes printing the error
 * report.
 */
DEFINE_EVENT(error_report_template, error_report_end,
	     TP_PROTO(enum error_detector error_detector, unsigned long id),
	     TP_ARGS(error_detector, id));

#endif /* _TRACE_ERROR_REPORT_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
