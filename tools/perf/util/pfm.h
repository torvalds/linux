/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for libpfm4 event encoding.
 *
 * Copyright 2020 Google LLC.
 */
#ifndef __PERF_PFM_H
#define __PERF_PFM_H

#include "print-events.h"
#include <subcmd/parse-options.h>

#ifdef HAVE_LIBPFM
int parse_libpfm_events_option(const struct option *opt, const char *str,
			int unset);

void print_libpfm_events(const struct print_callbacks *print_cb, void *print_state);

#else
#include <linux/compiler.h>

static inline int parse_libpfm_events_option(
	const struct option *opt __maybe_unused,
	const char *str __maybe_unused,
	int unset __maybe_unused)
{
	return 0;
}

static inline void print_libpfm_events(const struct print_callbacks *print_cb __maybe_unused,
				       void *print_state __maybe_unused)
{
}

#endif


#endif /* __PERF_PFM_H */
