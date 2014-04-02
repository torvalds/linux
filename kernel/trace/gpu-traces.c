/*
 * GPU tracepoints
 *
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include <trace/events/gpu.h>

EXPORT_TRACEPOINT_SYMBOL(gpu_sched_switch);
EXPORT_TRACEPOINT_SYMBOL(gpu_job_enqueue);
