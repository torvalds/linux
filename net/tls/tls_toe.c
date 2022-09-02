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

#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <net/inet_connection_sock.h>
#include <net/tls.h>
#include <net/tls_toe.h>

#include "tls.h"

static LIST_HEAD(device_list);
static DEFINE_SPINLOCK(device_spinlock);

static void tls_toe_sk_destruct(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct tls_context *ctx = tls_get_ctx(sk);

	ctx->sk_destruct(sk);
	/* Free ctx */
	rcu_assign_pointer(icsk->icsk_ulp_data, NULL);
	tls_ctx_free(sk, ctx);
}

int tls_toe_bypass(struct sock *sk)
{
	struct tls_toe_device *dev;
	struct tls_context *ctx;
	int rc = 0;

	spin_lock_bh(&device_spinlock);
	list_for_each_entry(dev, &device_list, dev_list) {
		if (dev->feature && dev->feature(dev)) {
			ctx = tls_ctx_create(sk);
			if (!ctx)
				goto out;

			ctx->sk_destruct = sk->sk_destruct;
			sk->sk_destruct = tls_toe_sk_destruct;
			ctx->rx_conf = TLS_HW_RECORD;
			ctx->tx_conf = TLS_HW_RECORD;
			update_sk_prot(sk, ctx);
			rc = 1;
			break;
		}
	}
out:
	spin_unlock_bh(&device_spinlock);
	return rc;
}

void tls_toe_unhash(struct sock *sk)
{
	struct tls_context *ctx = tls_get_ctx(sk);
	struct tls_toe_device *dev;

	spin_lock_bh(&device_spinlock);
	list_for_each_entry(dev, &device_list, dev_list) {
		if (dev->unhash) {
			kref_get(&dev->kref);
			spin_unlock_bh(&device_spinlock);
			dev->unhash(dev, sk);
			kref_put(&dev->kref, dev->release);
			spin_lock_bh(&device_spinlock);
		}
	}
	spin_unlock_bh(&device_spinlock);
	ctx->sk_proto->unhash(sk);
}

int tls_toe_hash(struct sock *sk)
{
	struct tls_context *ctx = tls_get_ctx(sk);
	struct tls_toe_device *dev;
	int err;

	err = ctx->sk_proto->hash(sk);
	spin_lock_bh(&device_spinlock);
	list_for_each_entry(dev, &device_list, dev_list) {
		if (dev->hash) {
			kref_get(&dev->kref);
			spin_unlock_bh(&device_spinlock);
			err |= dev->hash(dev, sk);
			kref_put(&dev->kref, dev->release);
			spin_lock_bh(&device_spinlock);
		}
	}
	spin_unlock_bh(&device_spinlock);

	if (err)
		tls_toe_unhash(sk);
	return err;
}

void tls_toe_register_device(struct tls_toe_device *device)
{
	spin_lock_bh(&device_spinlock);
	list_add_tail(&device->dev_list, &device_list);
	spin_unlock_bh(&device_spinlock);
}
EXPORT_SYMBOL(tls_toe_register_device);

void tls_toe_unregister_device(struct tls_toe_device *device)
{
	spin_lock_bh(&device_spinlock);
	list_del(&device->dev_list);
	spin_unlock_bh(&device_spinlock);
}
EXPORT_SYMBOL(tls_toe_unregister_device);
