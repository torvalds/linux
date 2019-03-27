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


#include "ixl_pf_qmgr.h"

static int	ixl_pf_qmgr_find_free_contiguous_block(struct ixl_pf_qmgr *qmgr, int num);

int
ixl_pf_qmgr_init(struct ixl_pf_qmgr *qmgr, u16 num_queues)
{
	if (num_queues < 1)
		return (EINVAL);

	qmgr->num_queues = num_queues;
	qmgr->qinfo = malloc(num_queues * sizeof(struct ixl_pf_qmgr_qinfo),
	    M_IXL, M_ZERO | M_NOWAIT);
	if (qmgr->qinfo == NULL)
		return ENOMEM;

	return (0);
}

int
ixl_pf_qmgr_alloc_contiguous(struct ixl_pf_qmgr *qmgr, u16 num, struct ixl_pf_qtag *qtag)
{
	int i;
	int avail;
	int block_start;
	u16 alloc_size;

	if (qtag == NULL || num < 1)
		return (EINVAL);
	
	/* We have to allocate in power-of-two chunks, so get next power of two */
	alloc_size = (u16)next_power_of_two(num);

	/* Don't try if there aren't enough queues */
	avail = ixl_pf_qmgr_get_num_free(qmgr);
	if (avail < alloc_size)
		return (ENOSPC);

	block_start = ixl_pf_qmgr_find_free_contiguous_block(qmgr, alloc_size);
	if (block_start < 0)
		return (ENOSPC);

	/* Mark queues as allocated */
	for (i = block_start; i < block_start + alloc_size; i++)
		qmgr->qinfo[i].allocated = true;

	bzero(qtag, sizeof(*qtag));
	qtag->qmgr = qmgr;
	qtag->type = IXL_PF_QALLOC_CONTIGUOUS;
	qtag->qidx[0] = block_start;
	qtag->num_allocated = alloc_size;
	qtag->num_active = num;

	return (0);
}

/*
 * NB: indices is u16 because this is the queue index width used in the Add VSI AQ command
 */
int
ixl_pf_qmgr_alloc_scattered(struct ixl_pf_qmgr *qmgr, u16 num, struct ixl_pf_qtag *qtag)
{
	int i;
	int avail, count = 0;
	u16 alloc_size;

	if (qtag == NULL || num < 1 || num > 16)
		return (EINVAL);

	/* We have to allocate in power-of-two chunks, so get next power of two */
	alloc_size = (u16)next_power_of_two(num);

	avail = ixl_pf_qmgr_get_num_free(qmgr);
	if (avail < alloc_size)
		return (ENOSPC);

	bzero(qtag, sizeof(*qtag));
	qtag->qmgr = qmgr;
	qtag->type = IXL_PF_QALLOC_SCATTERED;
	qtag->num_active = num;
	qtag->num_allocated = alloc_size;

	for (i = 0; i < qmgr->num_queues; i++) {
		if (!qmgr->qinfo[i].allocated) {
			qtag->qidx[count] = i;
			count++;
			qmgr->qinfo[i].allocated = true;
			if (count == alloc_size)
				return (0);
		}
	}

	// Shouldn't get here
	return (EDOOFUS);
}

int
ixl_pf_qmgr_release(struct ixl_pf_qmgr *qmgr, struct ixl_pf_qtag *qtag)
{
	u16 i, qidx;

	if (qtag == NULL)
		return (EINVAL);

	if (qtag->type == IXL_PF_QALLOC_SCATTERED) {
		for (i = 0; i < qtag->num_allocated; i++) {
			qidx = qtag->qidx[i];
			bzero(&qmgr->qinfo[qidx], sizeof(qmgr->qinfo[qidx]));
		}
	} else {
		u16 first_index = qtag->qidx[0];
		for (i = first_index; i < first_index + qtag->num_allocated; i++)
			bzero(&qmgr->qinfo[i], sizeof(qmgr->qinfo[qidx]));
	}

	qtag->qmgr = NULL;
	return (0);
}

int
ixl_pf_qmgr_get_num_queues(struct ixl_pf_qmgr *qmgr)
{
	return (qmgr->num_queues);
}

/*
 * ERJ: This assumes the info array isn't longer than INT_MAX.
 * This assumption might cause a y3k bug or something, I'm sure.
 */
int
ixl_pf_qmgr_get_num_free(struct ixl_pf_qmgr *qmgr)
{
	int count = 0;

	for (int i = 0; i < qmgr->num_queues; i++) {
		if (!qmgr->qinfo[i].allocated)
			count++;
	}

	return (count);
}

int
ixl_pf_qmgr_get_first_free(struct ixl_pf_qmgr *qmgr, u16 start)
{
	int i;

	if (start > qmgr->num_queues - 1)
		return (-EINVAL);

	for (i = start; i < qmgr->num_queues; i++) {
		if (qmgr->qinfo[i].allocated)
			continue;
		else
			return (i);
	}

	// No free queues
	return (-ENOSPC);
}

void
ixl_pf_qmgr_destroy(struct ixl_pf_qmgr *qmgr)
{
	free(qmgr->qinfo, M_IXL);
	qmgr->qinfo = NULL;
}

void
ixl_pf_qmgr_mark_queue_enabled(struct ixl_pf_qtag *qtag, u16 vsi_qidx, bool tx)
{
	MPASS(qtag != NULL);

	struct ixl_pf_qmgr *qmgr = qtag->qmgr;
	u16 pf_qidx = ixl_pf_qidx_from_vsi_qidx(qtag, vsi_qidx);
	if (tx)
		qmgr->qinfo[pf_qidx].tx_enabled = true;
	else
		qmgr->qinfo[pf_qidx].rx_enabled = true;
}

void
ixl_pf_qmgr_mark_queue_disabled(struct ixl_pf_qtag *qtag, u16 vsi_qidx, bool tx)
{
	MPASS(qtag != NULL);

	struct ixl_pf_qmgr *qmgr = qtag->qmgr;
	u16 pf_qidx = ixl_pf_qidx_from_vsi_qidx(qtag, vsi_qidx);
	if (tx)
		qmgr->qinfo[pf_qidx].tx_enabled = false;
	else
		qmgr->qinfo[pf_qidx].rx_enabled = false;
}

void
ixl_pf_qmgr_mark_queue_configured(struct ixl_pf_qtag *qtag, u16 vsi_qidx, bool tx)
{
	MPASS(qtag != NULL);

	struct ixl_pf_qmgr *qmgr = qtag->qmgr;
	u16 pf_qidx = ixl_pf_qidx_from_vsi_qidx(qtag, vsi_qidx);
	if (tx)
		qmgr->qinfo[pf_qidx].tx_configured = true;
	else
		qmgr->qinfo[pf_qidx].rx_configured = true;
}

bool
ixl_pf_qmgr_is_queue_enabled(struct ixl_pf_qtag *qtag, u16 vsi_qidx, bool tx)
{
	MPASS(qtag != NULL);

	struct ixl_pf_qmgr *qmgr = qtag->qmgr;
	u16 pf_qidx = ixl_pf_qidx_from_vsi_qidx(qtag, vsi_qidx);
	if (tx)
		return (qmgr->qinfo[pf_qidx].tx_enabled);
	else
		return (qmgr->qinfo[pf_qidx].rx_enabled);
}

bool
ixl_pf_qmgr_is_queue_configured(struct ixl_pf_qtag *qtag, u16 vsi_qidx, bool tx)
{
	MPASS(qtag != NULL);

	struct ixl_pf_qmgr *qmgr = qtag->qmgr;
	u16 pf_qidx = ixl_pf_qidx_from_vsi_qidx(qtag, vsi_qidx);
	if (tx)
		return (qmgr->qinfo[pf_qidx].tx_configured);
	else
		return (qmgr->qinfo[pf_qidx].rx_configured);
}

void
ixl_pf_qmgr_clear_queue_flags(struct ixl_pf_qtag *qtag)
{
	MPASS(qtag != NULL);

	struct ixl_pf_qmgr *qmgr = qtag->qmgr;
	for (u16 i = 0; i < qtag->num_allocated; i++) {
		u16 pf_qidx = ixl_pf_qidx_from_vsi_qidx(qtag, i);

		qmgr->qinfo[pf_qidx].tx_configured = 0;
		qmgr->qinfo[pf_qidx].rx_configured = 0;
		qmgr->qinfo[pf_qidx].rx_enabled = 0;
		qmgr->qinfo[pf_qidx].tx_enabled = 0;
	}
}

u16
ixl_pf_qidx_from_vsi_qidx(struct ixl_pf_qtag *qtag, u16 index)
{
	MPASS(index < qtag->num_allocated);

	if (qtag->type == IXL_PF_QALLOC_CONTIGUOUS)
		return qtag->first_qidx + index;
	else
		return qtag->qidx[index];
}

/* Static Functions */

static int
ixl_pf_qmgr_find_free_contiguous_block(struct ixl_pf_qmgr *qmgr, int num)
{
	int i;
	int count = 0;
	bool block_started = false;
	int possible_start;

	for (i = 0; i < qmgr->num_queues; i++) {
		if (!qmgr->qinfo[i].allocated) {
			if (!block_started) {
				block_started = true;
				possible_start = i;
			}
			count++;
			if (count == num)
				return (possible_start);
		} else { /* this queue is already allocated */
			block_started = false;
			count = 0;
		}
	}

	/* Can't find a contiguous block of the requested size */
	return (-1);
}

