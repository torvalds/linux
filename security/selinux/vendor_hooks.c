// SPDX-License-Identifier: GPL-2.0-only
/* vendor_hook.c
 *
 * Copyright 2022 Google LLC
 */

#ifndef __GENKSYMS__
#include "security.h"
#endif

#define CREATE_TRACE_POINTS
#include <trace/hooks/vendor_hooks.h>
#include <linux/tracepoint.h>
#include <trace/hooks/selinux.h>

EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_selinux_is_initialized);

/*
 * For type visibility
 */
struct selinux_state *GKI_struct_selinux_state;
EXPORT_SYMBOL_GPL(GKI_struct_selinux_state);
