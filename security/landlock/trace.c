// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Tracepoint helpers
 *
 * Copyright © 2025 Microsoft Corporation
 * Copyright © 2026 Cloudflare, Inc.
 */

#include "ruleset.h"

/*
 * Generates the tracepoint definitions in this translation unit.  The trace
 * event header dereferences the traced objects in TP_fast_assign, so the full
 * struct definitions (e.g. ruleset.h) must be included before it.
 */
#define CREATE_TRACE_POINTS
#include <trace/events/landlock.h>
