/******************************************************************************

  Copyright (c) 2013-2018, Intel Corporation
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


#include "ixl_pf.h"

#ifndef _IXL_PF_QMGR_H_
#define _IXL_PF_QMGR_H_

/*
 * Primarily manages the queues that need to be allocated to VSIs.
 * 
 * Cardinality: There should only be one of these in a PF.
 * Lifetime: Created and initialized in attach(); destroyed in detach().
 */

#define IXL_MAX_SCATTERED_QUEUES	16
#define IXL_MAX_CONTIGUOUS_QUEUES_XL710	64
#define IXL_MAX_CONTIGUOUS_QUEUES_X722	128

/* Structures */

/* Manager */
struct ixl_pf_qmgr_qinfo {
	u8 allocated;
	u8 tx_enabled;
	u8 rx_enabled;
	u8 tx_configured;
	u8 rx_configured;
};

struct ixl_pf_qmgr {
	u16	num_queues;
	struct ixl_pf_qmgr_qinfo *qinfo;
};

/* Tag */
enum ixl_pf_qmgr_qalloc_type {
	IXL_PF_QALLOC_CONTIGUOUS,
	IXL_PF_QALLOC_SCATTERED
};

struct ixl_pf_qtag {
	struct ixl_pf_qmgr *qmgr;
	enum ixl_pf_qmgr_qalloc_type type;
	union {
		u16 qidx[IXL_MAX_SCATTERED_QUEUES];
		u16 first_qidx;
	};
	u16 num_allocated;
	u16 num_active;
};

/* Public manager functions */
int	ixl_pf_qmgr_init(struct ixl_pf_qmgr *qmgr, u16 num_queues);
void	ixl_pf_qmgr_destroy(struct ixl_pf_qmgr *qmgr);

int	ixl_pf_qmgr_get_num_queues(struct ixl_pf_qmgr *qmgr);
int	ixl_pf_qmgr_get_first_free(struct ixl_pf_qmgr *qmgr, u16 start);
int	ixl_pf_qmgr_get_num_free(struct ixl_pf_qmgr *qmgr);

/* Allocate queues for a VF VSI */
int	ixl_pf_qmgr_alloc_scattered(struct ixl_pf_qmgr *qmgr, u16 num, struct ixl_pf_qtag *qtag);
/* Allocate queues for the LAN VSIs, or X722 VF VSIs */
int	ixl_pf_qmgr_alloc_contiguous(struct ixl_pf_qmgr *qmgr, u16 num, struct ixl_pf_qtag *qtag);
/* Release a queue allocation */
int	ixl_pf_qmgr_release(struct ixl_pf_qmgr *qmgr, struct ixl_pf_qtag *qtag);

/* Help manage queues used in VFs */
/* Typically hardware refers to RX as 0 and TX as 1, so continue that convention here */
void	ixl_pf_qmgr_mark_queue_enabled(struct ixl_pf_qtag *qtag, u16 vsi_qidx, bool tx);
void	ixl_pf_qmgr_mark_queue_disabled(struct ixl_pf_qtag *qtag, u16 vsi_qidx, bool tx);
void	ixl_pf_qmgr_mark_queue_configured(struct ixl_pf_qtag *qtag, u16 vsi_qidx, bool tx);
bool	ixl_pf_qmgr_is_queue_enabled(struct ixl_pf_qtag *qtag, u16 vsi_qidx, bool tx);
bool	ixl_pf_qmgr_is_queue_configured(struct ixl_pf_qtag *qtag, u16 vsi_qidx, bool tx);
void	ixl_pf_qmgr_clear_queue_flags(struct ixl_pf_qtag *qtag);

/* Public tag functions */
u16	ixl_pf_qidx_from_vsi_qidx(struct ixl_pf_qtag *qtag, u16 index);

#endif /* _IXL_PF_QMGR_H_ */

