/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for libpfm4 event encoding.
 *
 * Copyright 2020 Google LLC.
 */
#ifndef __PERF_PFM_H
#define __PERF_PFM_H

#include <subcmd/parse-options.h>

#ifdef HAVE_LIBPFM
int parse_libpfm_events_option(const struct option *opt, const char *str,
			int unset);

void print_libpfm_events(bool name_only, bool long_desc);

#else
#include <linux/compiler.h>

static inline int parse_libpfm_events_option(
	const struct option *opt __maybe_unused,
	const char *str __maybe_unused,
	int unset __maybe_unused)
{
	return 0;
}

static inline void print_libpfm_events(bool name_only __maybe_unused,
				       bool long_desc __maybe_unused)
{
}

#endif


#endif /* __PERF_PFM_H */
