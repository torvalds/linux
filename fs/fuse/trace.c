// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "dev_uring_i.h"
#include "fuse_i.h"
#include "fuse_dev_i.h"

#include <linux/pagemap.h>

#define CREATE_TRACE_POINTS
#include "fuse_trace.h"
