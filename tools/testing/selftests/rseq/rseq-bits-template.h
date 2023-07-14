/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * rseq-bits-template.h
 *
 * (C) Copyright 2016-2022 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 */

#ifdef RSEQ_TEMPLATE_CPU_ID
# define RSEQ_TEMPLATE_CPU_ID_OFFSET	RSEQ_CPU_ID_OFFSET
# define RSEQ_TEMPLATE_CPU_ID_FIELD	cpu_id
# ifdef RSEQ_TEMPLATE_MO_RELEASE
#  define RSEQ_TEMPLATE_SUFFIX		_release_cpu_id
# elif defined (RSEQ_TEMPLATE_MO_RELAXED)
#  define RSEQ_TEMPLATE_SUFFIX		_relaxed_cpu_id
# else
#  error "Never use <rseq-bits-template.h> directly; include <rseq.h> instead."
# endif
#elif defined(RSEQ_TEMPLATE_MM_CID)
# define RSEQ_TEMPLATE_CPU_ID_OFFSET	RSEQ_MM_CID_OFFSET
# define RSEQ_TEMPLATE_CPU_ID_FIELD	mm_cid
# ifdef RSEQ_TEMPLATE_MO_RELEASE
#  define RSEQ_TEMPLATE_SUFFIX		_release_mm_cid
# elif defined (RSEQ_TEMPLATE_MO_RELAXED)
#  define RSEQ_TEMPLATE_SUFFIX		_relaxed_mm_cid
# else
#  error "Never use <rseq-bits-template.h> directly; include <rseq.h> instead."
# endif
#elif defined (RSEQ_TEMPLATE_CPU_ID_NONE)
# ifdef RSEQ_TEMPLATE_MO_RELEASE
#  define RSEQ_TEMPLATE_SUFFIX		_release
# elif defined (RSEQ_TEMPLATE_MO_RELAXED)
#  define RSEQ_TEMPLATE_SUFFIX		_relaxed
# else
#  error "Never use <rseq-bits-template.h> directly; include <rseq.h> instead."
# endif
#else
# error "Never use <rseq-bits-template.h> directly; include <rseq.h> instead."
#endif

#define RSEQ_TEMPLATE_IDENTIFIER(x)	RSEQ_COMBINE_TOKENS(x, RSEQ_TEMPLATE_SUFFIX)

