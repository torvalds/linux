/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _NET_CORE_DATAGRAM_H_
#define _NET_CORE_DATAGRAM_H_

#include <linux/types.h>

struct sock;
struct sk_buff;
struct iov_iter;

int __zerocopy_sg_from_iter(struct sock *sk, struct sk_buff *skb,
			    struct iov_iter *from, size_t length);

#endif /* _NET_CORE_DATAGRAM_H_ */
