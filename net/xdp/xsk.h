/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2019 Intel Corporation. */

#ifndef XSK_H_
#define XSK_H_

static inline struct xdp_sock *xdp_sk(struct sock *sk)
{
	return (struct xdp_sock *)sk;
}

#endif /* XSK_H_ */
