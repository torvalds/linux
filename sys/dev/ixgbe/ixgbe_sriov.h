/******************************************************************************

  Copyright (c) 2001-2017, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. Neither the name of the Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/


#ifndef _IXGBE_SRIOV_H_
#define _IXGBE_SRIOV_H_

#ifdef PCI_IOV

#include <sys/nv.h>
#include <sys/iov_schema.h>
#include <dev/pci/pci_iov.h>
#include <net/iflib.h>
#include "ixgbe_mbx.h"

#define IXGBE_VF_CTS            (1 << 0) /* VF is clear to send. */
#define IXGBE_VF_CAP_MAC        (1 << 1) /* VF is permitted to change MAC. */
#define IXGBE_VF_CAP_VLAN       (1 << 2) /* VF is permitted to join vlans. */
#define IXGBE_VF_ACTIVE         (1 << 3) /* VF is active. */
#define IXGBE_VF_INDEX(vmdq)    ((vmdq) / 32)
#define IXGBE_VF_BIT(vmdq)      (1 << ((vmdq) % 32))

#define IXGBE_VT_MSG_MASK	0xFFFF

#define IXGBE_VT_MSGINFO(msg)	\
	(((msg) & IXGBE_VT_MSGINFO_MASK) >> IXGBE_VT_MSGINFO_SHIFT)

#define IXGBE_VF_GET_QUEUES_RESP_LEN	5

#define IXGBE_API_VER_1_0	0
#define IXGBE_API_VER_2_0	1	/* Solaris API.  Not supported. */
#define IXGBE_API_VER_1_1	2
#define IXGBE_API_VER_UNKNOWN	UINT16_MAX

#define IXGBE_NO_VM             0
#define IXGBE_32_VM             32
#define IXGBE_64_VM             64

int  ixgbe_if_iov_vf_add(if_ctx_t, u16, const nvlist_t *);
int  ixgbe_if_iov_init(if_ctx_t, u16, const nvlist_t *);
void ixgbe_if_iov_uninit(if_ctx_t);
void ixgbe_initialize_iov(struct adapter *);
void ixgbe_recalculate_max_frame(struct adapter *);
void ixgbe_ping_all_vfs(struct adapter *);
int  ixgbe_pci_iov_detach(device_t);
void ixgbe_define_iov_schemas(device_t, int *);
void ixgbe_align_all_queue_indices(struct adapter *);
int  ixgbe_vf_que_index(int, int, int);
u32  ixgbe_get_mtqc(int);
u32  ixgbe_get_mrqc(int);

/******************************************************************************/
#else  /* PCI_IOV */
/******************************************************************************/

#define ixgbe_add_vf(_a,_b,_c)
#define ixgbe_init_iov(_a,_b,_c)
#define ixgbe_uninit_iov(_a)
#define ixgbe_initialize_iov(_a)
#define ixgbe_recalculate_max_frame(_a)
#define ixgbe_ping_all_vfs(_a)
#define ixgbe_pci_iov_detach(_a) 0
#define ixgbe_define_iov_schemas(_a,_b)
#define ixgbe_align_all_queue_indices(_a)
#define ixgbe_vf_que_index(_a, _b, _c) (_c)
#define ixgbe_get_mtqc(_a) IXGBE_MTQC_64Q_1PB
#define ixgbe_get_mrqc(_a) 0

#endif /* PCI_IOV */

void ixgbe_if_init(if_ctx_t ctx);
void ixgbe_handle_mbx(void *);

#endif
