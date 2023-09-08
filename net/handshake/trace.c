// SPDX-License-Identifier: GPL-2.0
/*
 * Trace points for transport security layer handshakes.
 *
 * Author: Chuck Lever <chuck.lever@oracle.com>
 *
 * Copyright (c) 2023, Oracle and/or its affiliates.
 */

#include <linux/types.h>

#include <net/sock.h>
#include <net/netlink.h>
#include <net/genetlink.h>

#include "handshake.h"

#define CREATE_TRACE_POINTS

#include <trace/events/handshake.h>
