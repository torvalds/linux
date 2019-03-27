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

#ifdef RSS

#include "lio_bsd.h"
#include "lio_common.h"
#include "lio_droq.h"
#include "lio_iq.h"
#include "lio_response_manager.h"
#include "lio_device.h"
#include "lio_ctrl.h"
#include "lio_main.h"
#include "lio_network.h"
#include "lio_rss.h"

int	lio_send_rss_param(struct lio *lio);

static void
lio_set_rss_callback(struct octeon_device *oct, uint32_t status, void *arg)
{
	struct lio_soft_command	*sc = (struct lio_soft_command *)arg;

	if (status)
		lio_dev_err(oct, "Failed to SET RSS params\n");
	else
		lio_dev_info(oct, "SET RSS params\n");

	lio_free_soft_command(oct, sc);
}

static void
lio_set_rss_info(struct lio *lio)
{
	struct octeon_device		*oct = lio->oct_dev;
	struct lio_rss_params_set	*rss_set = &lio->rss_set;
	uint32_t	rss_hash_config;
	int		i;
	uint8_t		queue_id;

	for (i = 0; i < LIO_RSS_TABLE_SZ; i++) {
		queue_id = rss_get_indirection_to_bucket(i);
		queue_id = queue_id % oct->num_oqs;
		rss_set->fw_itable[i] = queue_id;
	}

	rss_hash_config = rss_gethashconfig();
	if (rss_hash_config & RSS_HASHTYPE_RSS_IPV4)
		rss_set->hashinfo |= LIO_RSS_HASH_IPV4;

	if (rss_hash_config & RSS_HASHTYPE_RSS_TCP_IPV4)
		rss_set->hashinfo |= LIO_RSS_HASH_TCP_IPV4;

	if (rss_hash_config & RSS_HASHTYPE_RSS_IPV6)
		rss_set->hashinfo |= LIO_RSS_HASH_IPV6;

	if (rss_hash_config & RSS_HASHTYPE_RSS_TCP_IPV6)
		rss_set->hashinfo |= LIO_RSS_HASH_TCP_IPV6;

	if (rss_hash_config & RSS_HASHTYPE_RSS_IPV6_EX)
		rss_set->hashinfo |= LIO_RSS_HASH_IPV6_EX;

	if (rss_hash_config & RSS_HASHTYPE_RSS_TCP_IPV6_EX)
		rss_set->hashinfo |= LIO_RSS_HASH_TCP_IPV6_EX;
}

int
lio_send_rss_param(struct lio *lio)
{
	struct octeon_device	*oct = lio->oct_dev;
	struct lio_soft_command	*sc = NULL;
	union	octeon_cmd *cmd = NULL;
	struct lio_rss_params	*rss_param;
	int	i, retval;

	sc = lio_alloc_soft_command(oct,
			OCTEON_CMD_SIZE + sizeof(struct lio_rss_params), 0, 0);

	if (sc == NULL) {
		lio_dev_err(oct, "%s: Soft command allocation failed\n",
			    __func__);
		return (ENOMEM);
	}

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	lio_prepare_soft_command(oct, sc, LIO_OPCODE_NIC, LIO_OPCODE_NIC_CMD, 0,
				 0, 0);

	sc->callback = lio_set_rss_callback;
	sc->callback_arg = sc;
	sc->wait_time = 1000;

	cmd = (union octeon_cmd *)sc->virtdptr;
	cmd->cmd64 = 0;
	cmd->s.cmd = LIO_CMD_SET_RSS;

	rss_param = (struct lio_rss_params *)(cmd + 1);
	rss_param->param.flags = 0;
	rss_param->param.itablesize = LIO_RSS_TABLE_SZ;
	rss_param->param.hashkeysize = LIO_RSS_KEY_SZ;

	lio_set_rss_info(lio);
	rss_param->param.hashinfo = lio->rss_set.hashinfo;
	memcpy(rss_param->itable, (void *)lio->rss_set.fw_itable,
	       (size_t)rss_param->param.itablesize);

	lio_dev_info(oct, "RSS itable: Size %u\n", rss_param->param.itablesize);
	for (i = 0; i < rss_param->param.itablesize; i += 8) {
		lio_dev_dbg(oct, "   %03u:%2u, %03u:%2u, %03u:%2u, %03u:%2u, %03u:%2u, %03u:%2u, %03u:%2u, %03u:%2u\n",
			    i + 0, rss_param->itable[i + 0],
			    i + 1, rss_param->itable[i + 1],
			    i + 2, rss_param->itable[i + 2],
			    i + 3, rss_param->itable[i + 3],
			    i + 4, rss_param->itable[i + 4],
			    i + 5, rss_param->itable[i + 5],
			    i + 6, rss_param->itable[i + 6],
			    i + 7, rss_param->itable[i + 7]);
	}

	rss_getkey(lio->rss_set.key);

	memcpy(rss_param->key, (void *)lio->rss_set.key,
	       (size_t)rss_param->param.hashkeysize);

	/* swap cmd and rss params */
	lio_swap_8B_data((uint64_t *)cmd,
			 ((OCTEON_CMD_SIZE + LIO_RSS_PARAM_SIZE) >> 3));

	retval = lio_send_soft_command(oct, sc);
	if (retval == LIO_IQ_SEND_FAILED) {
		lio_dev_err(oct,
			    "%s: Sending soft command failed, status: %x\n",
			    __func__, retval);
		lio_free_soft_command(oct, sc);
		return (-1);
	}

	return (0);
}

#endif	/* RSS */
