/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

#include "lio_bsd.h"
#include "lio_common.h"
#include "lio_droq.h"
#include "lio_iq.h"
#include "lio_response_manager.h"
#include "lio_device.h"
#include "lio_ctrl.h"
#include "lio_main.h"

int
lio_send_data_pkt(struct octeon_device *oct, struct lio_data_pkt *ndata)
{
	int	ring_doorbell = 1;

	return (lio_send_command(oct, ndata->q_no, ring_doorbell, &ndata->cmd,
				 ndata->buf, ndata->datasize, ndata->reqtype));
}

static void
lio_ctrl_callback(struct octeon_device *oct, uint32_t status, void *sc_ptr)
{
	struct lio_soft_command	*sc = (struct lio_soft_command *)sc_ptr;
	struct lio_ctrl_pkt	*nctrl;

	nctrl = (struct lio_ctrl_pkt *)sc->ctxptr;

	/*
	 * Call the callback function if status is OK.
	 * Status is OK only if a response was expected and core returned
	 * success.
	 * If no response was expected, status is OK if the command was posted
	 * successfully.
	 */
	if (!status && nctrl->cb_fn)
		nctrl->cb_fn(nctrl);

	lio_free_soft_command(oct, sc);
}

static inline struct lio_soft_command *
lio_alloc_ctrl_pkt_sc(struct octeon_device *oct, struct lio_ctrl_pkt *nctrl)
{
	struct lio_soft_command	*sc = NULL;
	uint32_t		datasize = 0, rdatasize, uddsize = 0;
	uint8_t			*data;

	uddsize = (uint32_t)(nctrl->ncmd.s.more * 8);

	datasize = OCTEON_CMD_SIZE + uddsize;
	rdatasize = (nctrl->wait_time) ? 16 : 0;

	sc = lio_alloc_soft_command(oct, datasize, rdatasize,
				    sizeof(struct lio_ctrl_pkt));

	if (sc == NULL)
		return (NULL);

	memcpy(sc->ctxptr, nctrl, sizeof(struct lio_ctrl_pkt));

	data = (uint8_t *)sc->virtdptr;

	memcpy(data, &nctrl->ncmd, OCTEON_CMD_SIZE);

	lio_swap_8B_data((uint64_t *)data, (OCTEON_CMD_SIZE >> 3));

	if (uddsize) {
		/* Endian-Swap for UDD should have been done by caller. */
		memcpy(data + OCTEON_CMD_SIZE, nctrl->udd, uddsize);
	}
	sc->iq_no = (uint32_t)nctrl->iq_no;

	lio_prepare_soft_command(oct, sc, LIO_OPCODE_NIC, LIO_OPCODE_NIC_CMD, 0,
				 0, 0);

	sc->callback = lio_ctrl_callback;
	sc->callback_arg = sc;
	sc->wait_time = nctrl->wait_time;

	return (sc);
}

int
lio_send_ctrl_pkt(struct octeon_device *oct, struct lio_ctrl_pkt *nctrl)
{
	struct lio_soft_command	*sc = NULL;
	int	retval;

	mtx_lock(&oct->cmd_resp_wqlock);
	/*
	 * Allow only rx ctrl command to stop traffic on the chip
	 * during offline operations
	 */
	if ((oct->cmd_resp_state == LIO_DRV_OFFLINE) &&
	    (nctrl->ncmd.s.cmd != LIO_CMD_RX_CTL)) {
		mtx_unlock(&oct->cmd_resp_wqlock);
		lio_dev_err(oct, "%s cmd:%d not processed since driver offline\n",
			    __func__, nctrl->ncmd.s.cmd);
		return (-1);
	}

	sc = lio_alloc_ctrl_pkt_sc(oct, nctrl);
	if (sc == NULL) {
		lio_dev_err(oct, "%s soft command alloc failed\n", __func__);
		mtx_unlock(&oct->cmd_resp_wqlock);
		return (-1);
	}

	retval = lio_send_soft_command(oct, sc);
	if (retval == LIO_IQ_SEND_FAILED) {
		lio_free_soft_command(oct, sc);
		lio_dev_err(oct, "%s pf_num:%d soft command:%d send failed status: %x\n",
			    __func__, oct->pf_num, nctrl->ncmd.s.cmd, retval);
		mtx_unlock(&oct->cmd_resp_wqlock);
		return (-1);
	}

	mtx_unlock(&oct->cmd_resp_wqlock);
	return (retval);
}
