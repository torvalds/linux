/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2014 Intel Corporation. All rights reserved.
 * Copyright (c) 2014 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $FreeBSD$
 */

#ifndef _IWPM_UTIL_H
#define _IWPM_UTIL_H

#include <linux/module.h>
#include <linux/io.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/jhash.h>
#include <linux/kref.h>
#include <linux/errno.h>
#include <linux/rwsem.h>

#include <rdma/iw_portmap.h>

#define IWPM_PID_UNDEFINED     -1
#define IWPM_PID_UNAVAILABLE   -2

#define IWPM_REG_UNDEF          0x01
#define IWPM_REG_VALID          0x02
#define IWPM_REG_INCOMPL        0x04

/**
 * iwpm_compare_sockaddr - Compare two sockaddr storage structs
 *
 * Returns 0 if they are holding the same ip/tcp address info,
 * otherwise returns 1
 */
int iwpm_compare_sockaddr(struct sockaddr_storage *a_sockaddr,
			struct sockaddr_storage *b_sockaddr);

/**
 * iwpm_print_sockaddr - Print IPv4/IPv6 address and TCP port
 * @sockaddr: Socket address to print
 * @msg: Message to print
 */
void iwpm_print_sockaddr(struct sockaddr_storage *sockaddr, char *msg);
#endif
