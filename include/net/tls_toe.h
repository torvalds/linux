/*
 * Copyright (c) 2016-2017, Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016-2017, Dave Watson <davejwatson@fb.com>. All rights reserved.
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
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/kref.h>
#include <linux/list.h>

struct sock;

#define TLS_TOE_DEVICE_NAME_MAX		32

/*
 * This structure defines the routines for Inline TLS driver.
 * The following routines are optional and filled with a
 * null pointer if not defined.
 *
 * @name: Its the name of registered Inline tls device
 * @dev_list: Inline tls device list
 * int (*feature)(struct tls_toe_device *device);
 *     Called to return Inline TLS driver capability
 *
 * int (*hash)(struct tls_toe_device *device, struct sock *sk);
 *     This function sets Inline driver for listen and program
 *     device specific functioanlity as required
 *
 * void (*unhash)(struct tls_toe_device *device, struct sock *sk);
 *     This function cleans listen state set by Inline TLS driver
 *
 * void (*release)(struct kref *kref);
 *     Release the registered device and allocated resources
 * @kref: Number of reference to tls_toe_device
 */
struct tls_toe_device {
	char name[TLS_TOE_DEVICE_NAME_MAX];
	struct list_head dev_list;
	int  (*feature)(struct tls_toe_device *device);
	int  (*hash)(struct tls_toe_device *device, struct sock *sk);
	void (*unhash)(struct tls_toe_device *device, struct sock *sk);
	void (*release)(struct kref *kref);
	struct kref kref;
};

int tls_toe_bypass(struct sock *sk);
int tls_toe_hash(struct sock *sk);
void tls_toe_unhash(struct sock *sk);

void tls_toe_register_device(struct tls_toe_device *device);
void tls_toe_unregister_device(struct tls_toe_device *device);
