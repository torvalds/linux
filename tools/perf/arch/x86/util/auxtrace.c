/*
 * auxtrace.c: AUX area tracing support
 * Copyright (c) 2013-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include "../../util/header.h"
#include "../../util/auxtrace.h"
#include "../../util/intel-pt.h"

struct auxtrace_record *auxtrace_record__init(struct perf_evlist *evlist __maybe_unused,
					      int *err)
{
	char buffer[64];
	int ret;

	*err = 0;

	ret = get_cpuid(buffer, sizeof(buffer));
	if (ret) {
		*err = ret;
		return NULL;
	}

	if (!strncmp(buffer, "GenuineIntel,", 13))
		return intel_pt_recording_init(err);

	return NULL;
}
