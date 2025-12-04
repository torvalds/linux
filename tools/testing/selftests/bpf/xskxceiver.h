/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2020 Intel Corporation.
 */

#ifndef XSKXCEIVER_H_
#define XSKXCEIVER_H_

#include <limits.h>

#include "xsk_xdp_progs.skel.h"
#include "xsk_xdp_common.h"

#ifndef SOL_XDP
#define SOL_XDP 283
#endif

#ifndef AF_XDP
#define AF_XDP 44
#endif

#ifndef PF_XDP
#define PF_XDP AF_XDP
#endif

#define MAX_TEARDOWN_ITER 10
#define MAX_ETH_JUMBO_SIZE 9000
#define SOCK_RECONF_CTR 10
#define RX_FULL_RXQSIZE 32
#define UMEM_HEADROOM_TEST_SIZE 128
#define XSK_UMEM__INVALID_FRAME_SIZE (MAX_ETH_JUMBO_SIZE + 1)
#define RUN_ALL_TESTS UINT_MAX
#define NUM_MAC_ADDRESSES 4

#endif				/* XSKXCEIVER_H_ */
