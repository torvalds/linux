/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @file
 * OCS Linux SCSI API base driver implementation.
 */

/**
 * @defgroup scsi_api_base SCSI Base Target/Initiator
 */
 
 
#include "ocs.h"
#include "ocs_els.h"
#include "ocs_scsi.h"
#if defined(OCS_ENABLE_VPD_SUPPORT)
#include "ocs_vpd.h"
#endif
#include "ocs_utils.h"
#include "ocs_device.h"

#define SCSI_IOFMT "[%04x][i:%0*x t:%0*x h:%04x]"
#define SCSI_ITT_SIZE(ocs)	((ocs->ocs_xport == OCS_XPORT_FC) ? 4 : 8)

#define SCSI_IOFMT_ARGS(io) io->instance_index, SCSI_ITT_SIZE(io->ocs), io->init_task_tag, SCSI_ITT_SIZE(io->ocs), io->tgt_task_tag, io->hw_tag

#define enable_tsend_auto_resp(ocs)		((ocs->ctrlmask & OCS_CTRLMASK_XPORT_DISABLE_AUTORSP_TSEND) == 0)
#define enable_treceive_auto_resp(ocs)	((ocs->ctrlmask & OCS_CTRLMASK_XPORT_DISABLE_AUTORSP_TRECEIVE) == 0)

#define scsi_io_printf(io, fmt, ...) ocs_log_info(io->ocs, "[%s]" SCSI_IOFMT fmt, \
	io->node->display_name, SCSI_IOFMT_ARGS(io), ##__VA_ARGS__)

#define scsi_io_trace(io, fmt, ...) \
	do { \
		if (OCS_LOG_ENABLE_SCSI_TRACE(io->ocs)) \
			scsi_io_printf(io, fmt, ##__VA_ARGS__); \
	} while (0)

#define scsi_log(ocs, fmt, ...) \
	do { \
		if (OCS_LOG_ENABLE_SCSI_TRACE(ocs)) \
			ocs_log_info(ocs, fmt, ##__VA_ARGS__); \
	} while (0)

static int32_t ocs_target_send_bls_resp(ocs_io_t *io, ocs_scsi_io_cb_t cb, void *arg);
static int32_t ocs_scsi_abort_io_cb(struct ocs_hw_io_s *hio, ocs_remote_node_t *rnode, uint32_t len, int32_t status,
	uint32_t ext, void *arg);

static void ocs_scsi_io_free_ovfl(ocs_io_t *io);
static uint32_t ocs_scsi_count_sgls(ocs_hw_dif_info_t *hw_dif, ocs_scsi_sgl_t *sgl, uint32_t sgl_count);
static int ocs_scsi_dif_guard_is_crc(uint8_t direction, ocs_hw_dif_info_t *dif_info);
static ocs_scsi_io_status_e ocs_scsi_dif_check_unknown(ocs_io_t *io, uint32_t length, uint32_t check_length, int is_crc);
static uint32_t ocs_scsi_dif_check_guard(ocs_hw_dif_info_t *dif_info, ocs_scsi_vaddr_len_t addrlen[],
	uint32_t addrlen_count, ocs_dif_t *dif, int is_crc);
static uint32_t ocs_scsi_dif_check_app_tag(ocs_t *ocs, ocs_hw_dif_info_t *dif_info, uint16_t exp_app_tag, ocs_dif_t *dif);
static uint32_t ocs_scsi_dif_check_ref_tag(ocs_t *ocs, ocs_hw_dif_info_t *dif_info, uint32_t exp_ref_tag, ocs_dif_t *dif);
static int32_t ocs_scsi_convert_dif_info(ocs_t *ocs, ocs_scsi_dif_info_t *scsi_dif_info,
	ocs_hw_dif_info_t *hw_dif_info);
static int32_t ocs_scsi_io_dispatch_hw_io(ocs_io_t *io, ocs_hw_io_t *hio);
static int32_t ocs_scsi_io_dispatch_no_hw_io(ocs_io_t *io);
static void _ocs_scsi_io_free(void *arg);


/**
 * @ingroup scsi_api_base
 * @brief Returns a big-endian 32-bit value given a pointer.
 *
 * @param p Pointer to the 32-bit big-endian location.
 *
 * @return Returns the byte-swapped 32-bit value.
 */

static inline uint32_t
ocs_fc_getbe32(void *p)
{
	return ocs_be32toh(*((uint32_t*)p));
}

/**
 * @ingroup scsi_api_base
 * @brief Enable IO allocation.
 *
 * @par Description
 * The SCSI and Transport IO allocation functions are enabled. If the allocation functions
 * are not enabled, then calls to ocs_scsi_io_alloc() (and ocs_els_io_alloc() for FC) will
 * fail.
 *
 * @param node Pointer to node object.
 *
 * @return None.
 */
void
ocs_scsi_io_alloc_enable(ocs_node_t *node)
{
	ocs_assert(node != NULL);
	ocs_lock(&node->active_ios_lock);
		node->io_alloc_enabled = TRUE;
	ocs_unlock(&node->active_ios_lock);
}

/**
 * @ingroup scsi_api_base
 * @brief Disable IO allocation
 *
 * @par Description
 * The SCSI and Transport IO allocation functions are disabled. If the allocation functions
 * are not enabled, then calls to ocs_scsi_io_alloc() (and ocs_els_io_alloc() for FC) will
 * fail.
 *
 * @param node Pointer to node object
 *
 * @return None.
 */
void
ocs_scsi_io_alloc_disable(ocs_node_t *node)
{
	ocs_assert(node != NULL);
	ocs_lock(&node->active_ios_lock);
		node->io_alloc_enabled = FALSE;
	ocs_unlock(&node->active_ios_lock);
}

/**
 * @ingroup scsi_api_base
 * @brief Allocate a SCSI IO context.
 *
 * @par Description
 * A SCSI IO context is allocated and associated with a @c node. This function
 * is called by an initiator-client when issuing SCSI commands to remote
 * target devices. On completion, ocs_scsi_io_free() is called.
 * @n @n
 * The returned ocs_io_t structure has an element of type ocs_scsi_ini_io_t named
 * &quot;ini_io&quot; that is declared and used by an initiator-client for private information.
 *
 * @param node Pointer to the associated node structure.
 * @param role Role for IO (originator/responder).
 *
 * @return Returns the pointer to the IO context, or NULL.
 *
 */

ocs_io_t *
ocs_scsi_io_alloc(ocs_node_t *node, ocs_scsi_io_role_e role)
{
	ocs_t *ocs;
	ocs_xport_t *xport;
	ocs_io_t *io;

	ocs_assert(node, NULL);
	ocs_assert(node->ocs, NULL);

	ocs = node->ocs;
	ocs_assert(ocs->xport, NULL);
	xport = ocs->xport;

	ocs_lock(&node->active_ios_lock);

		if (!node->io_alloc_enabled) {
			ocs_unlock(&node->active_ios_lock);
			return NULL;
		}

		io = ocs_io_alloc(ocs);
		if (io == NULL) {
			ocs_atomic_add_return(&xport->io_alloc_failed_count, 1);
			ocs_unlock(&node->active_ios_lock);
			return NULL;
		}

		/* initialize refcount */
		ocs_ref_init(&io->ref, _ocs_scsi_io_free, io);

		if (io->hio != NULL) {
			ocs_log_err(node->ocs, "assertion failed: io->hio is not NULL\n");
			ocs_unlock(&node->active_ios_lock);
			return NULL;
		}

		/* set generic fields */
		io->ocs = ocs;
		io->node = node;

		/* set type and name */
		io->io_type = OCS_IO_TYPE_IO;
		io->display_name = "scsi_io";

		switch (role) {
		case OCS_SCSI_IO_ROLE_ORIGINATOR:
			io->cmd_ini = TRUE;
			io->cmd_tgt = FALSE;
			break;
		case OCS_SCSI_IO_ROLE_RESPONDER:
			io->cmd_ini = FALSE;
			io->cmd_tgt = TRUE;
			break;
		}

		/* Add to node's active_ios list */
		ocs_list_add_tail(&node->active_ios, io);

	ocs_unlock(&node->active_ios_lock);

	return io;
}

/**
 * @ingroup scsi_api_base
 * @brief Free a SCSI IO context (internal).
 *
 * @par Description
 * The IO context previously allocated using ocs_scsi_io_alloc()
 * is freed. This is called from within the transport layer,
 * when the reference count goes to zero.
 *
 * @param arg Pointer to the IO context.
 *
 * @return None.
 */
static void
_ocs_scsi_io_free(void *arg)
{
	ocs_io_t *io = (ocs_io_t *)arg;
	ocs_t *ocs = io->ocs;
	ocs_node_t *node = io->node;
	int send_empty_event;

	ocs_assert(io != NULL);

	scsi_io_trace(io, "freeing io 0x%p %s\n", io, io->display_name);

	ocs_assert(ocs_io_busy(io));

	ocs_lock(&node->active_ios_lock);
		ocs_list_remove(&node->active_ios, io);
		send_empty_event = (!node->io_alloc_enabled) && ocs_list_empty(&node->active_ios);
	ocs_unlock(&node->active_ios_lock);

	if (send_empty_event) {
		ocs_node_post_event(node, OCS_EVT_NODE_ACTIVE_IO_LIST_EMPTY, NULL);
	}

	io->node = NULL;
	ocs_io_free(ocs, io);

}

/**
 * @ingroup scsi_api_base
 * @brief Free a SCSI IO context.
 *
 * @par Description
 * The IO context previously allocated using ocs_scsi_io_alloc() is freed.
 *
 * @param io Pointer to the IO context.
 *
 * @return None.
 */
void
ocs_scsi_io_free(ocs_io_t *io)
{
	scsi_io_trace(io, "freeing io 0x%p %s\n", io, io->display_name);
	ocs_assert(ocs_ref_read_count(&io->ref) > 0);
	ocs_ref_put(&io->ref); /* ocs_ref_get(): ocs_scsi_io_alloc() */
}



static int32_t
ocs_scsi_send_io(ocs_hw_io_type_e type, ocs_node_t *node, ocs_io_t *io, uint64_t lun,
	ocs_scsi_tmf_cmd_e tmf, uint8_t *cdb, uint32_t cdb_len,
	ocs_scsi_dif_info_t *dif_info,
	ocs_scsi_sgl_t *sgl, uint32_t sgl_count, uint32_t wire_len, uint32_t first_burst,
	ocs_scsi_rsp_io_cb_t cb, void *arg);

/**
 * @brief Target response completion callback.
 *
 * @par Description
 * Function is called upon the completion of a target IO request.
 *
 * @param hio Pointer to the HW IO structure.
 * @param rnode Remote node associated with the IO that is completing.
 * @param length Length of the response payload.
 * @param status Completion status.
 * @param ext_status Extended completion status.
 * @param app Application-specific data (generally a pointer to the IO context).
 *
 * @return None.
 */

static void
ocs_target_io_cb(ocs_hw_io_t *hio, ocs_remote_node_t *rnode, uint32_t length,
	int32_t status, uint32_t ext_status, void *app)
{
	ocs_io_t *io = app;
	ocs_t *ocs;
	ocs_scsi_io_status_e scsi_status = OCS_SCSI_STATUS_GOOD;
	uint16_t additional_length;
	uint8_t edir;
	uint8_t tdpv;
	ocs_hw_dif_info_t *dif_info = &io->hw_dif;
	int is_crc;

	ocs_assert(io);

	scsi_io_trace(io, "status x%x ext_status x%x\n", status, ext_status);

	ocs = io->ocs;
	ocs_assert(ocs);

	ocs_scsi_io_free_ovfl(io);

	io->transferred += length;

	/* Call target server completion */
	if (io->scsi_tgt_cb) {
		ocs_scsi_io_cb_t cb = io->scsi_tgt_cb;
		uint32_t flags = 0;

		/* Clear the callback before invoking the callback */
		io->scsi_tgt_cb = NULL;

		/* if status was good, and auto-good-response was set, then callback
		 * target-server with IO_CMPL_RSP_SENT, otherwise send IO_CMPL
		 */
		if ((status == 0) && (io->auto_resp))
			flags |= OCS_SCSI_IO_CMPL_RSP_SENT;
		else
			flags |= OCS_SCSI_IO_CMPL;

		switch (status) {
		case SLI4_FC_WCQE_STATUS_SUCCESS:
			scsi_status = OCS_SCSI_STATUS_GOOD;
			break;
		case SLI4_FC_WCQE_STATUS_DI_ERROR:
			if (ext_status & SLI4_FC_DI_ERROR_GE) {
				scsi_status = OCS_SCSI_STATUS_DIF_GUARD_ERROR;
			} else if (ext_status & SLI4_FC_DI_ERROR_AE) {
				scsi_status = OCS_SCSI_STATUS_DIF_APP_TAG_ERROR;
			} else if (ext_status & SLI4_FC_DI_ERROR_RE) {
				scsi_status = OCS_SCSI_STATUS_DIF_REF_TAG_ERROR;
			} else {
				additional_length = ((ext_status >> 16) & 0xFFFF);

				/* Capture the EDIR and TDPV bits as 0 or 1 for easier printing. */
				edir = !!(ext_status & SLI4_FC_DI_ERROR_EDIR);
				tdpv = !!(ext_status & SLI4_FC_DI_ERROR_TDPV);

				is_crc = ocs_scsi_dif_guard_is_crc(edir, dif_info);

				if (edir == 0) {
					/* For reads, we have everything in memory.  Start checking from beginning. */
					scsi_status = ocs_scsi_dif_check_unknown(io, 0, io->wire_len, is_crc);
				} else {
					/* For writes, use the additional length to determine where to look for the error.
					 * The additional_length field is set to 0 if it is not supported.
					 * The additional length field is valid if:
					 *    . additional_length is not zero
					 *    . Total Data Placed is valid
					 *    . Error Direction is RX (1)
					 *    . Operation is a pass thru (CRC or CKSUM on IN, and CRC or CHKSUM on OUT) (all pass-thru cases except raw)
					 */
					if ((additional_length != 0) && (tdpv != 0) &&
					    (dif_info->dif == SLI4_DIF_PASS_THROUGH) && (dif_info->dif_oper != OCS_HW_SGE_DIF_OP_IN_RAW_OUT_RAW) ) {
						scsi_status = ocs_scsi_dif_check_unknown(io, length, additional_length, is_crc);
					} else {
						/* If we can't do additional checking, then fall-back to guard error */
						scsi_status = OCS_SCSI_STATUS_DIF_GUARD_ERROR;
					}
				}
			}
			break;
		case SLI4_FC_WCQE_STATUS_LOCAL_REJECT:
			switch (ext_status) {
			case SLI4_FC_LOCAL_REJECT_INVALID_RELOFFSET:
			case SLI4_FC_LOCAL_REJECT_ABORT_REQUESTED:
				scsi_status = OCS_SCSI_STATUS_ABORTED;
				break;
			case SLI4_FC_LOCAL_REJECT_INVALID_RPI:
				scsi_status = OCS_SCSI_STATUS_NEXUS_LOST;
				break;
			case SLI4_FC_LOCAL_REJECT_NO_XRI:
				scsi_status = OCS_SCSI_STATUS_NO_IO;
				break;
			default:
				/* TODO: we have seen 0x0d (TX_DMA_FAILED error) */
				scsi_status = OCS_SCSI_STATUS_ERROR;
				break;
			}
			break;

		case SLI4_FC_WCQE_STATUS_TARGET_WQE_TIMEOUT:
			/* target IO timed out */
			scsi_status = OCS_SCSI_STATUS_TIMEDOUT_AND_ABORTED;
			break;

		case SLI4_FC_WCQE_STATUS_SHUTDOWN:
			/* Target IO cancelled by HW */
			scsi_status = OCS_SCSI_STATUS_SHUTDOWN;
			break;

		default:
			scsi_status = OCS_SCSI_STATUS_ERROR;
			break;
		}

		cb(io, scsi_status, flags, io->scsi_tgt_cb_arg);

	}
	ocs_scsi_check_pending(ocs);
}

/**
 * @brief Determine if an IO is using CRC for DIF guard format.
 *
 * @param direction IO direction: 1 for write, 0 for read.
 * @param dif_info Pointer to HW DIF info data.
 *
 * @return Returns TRUE if using CRC, FALSE if not.
 */
static int
ocs_scsi_dif_guard_is_crc(uint8_t direction, ocs_hw_dif_info_t *dif_info)
{
	int is_crc;

	if (direction) {
		/* For writes, check if operation is "OUT_CRC" or not */
		switch(dif_info->dif_oper) {
			case OCS_HW_SGE_DIF_OP_IN_NODIF_OUT_CRC:
			case OCS_HW_SGE_DIF_OP_IN_CRC_OUT_CRC:
			case OCS_HW_SGE_DIF_OP_IN_CHKSUM_OUT_CRC:
				is_crc = TRUE;
				break;
			default:
				is_crc = FALSE;
				break;
		}
	} else {
		/* For reads, check if operation is "IN_CRC" or not */
		switch(dif_info->dif_oper) {
			case OCS_HW_SGE_DIF_OP_IN_CRC_OUT_NODIF:
			case OCS_HW_SGE_DIF_OP_IN_CRC_OUT_CRC:
			case OCS_HW_SGE_DIF_OP_IN_CRC_OUT_CHKSUM:
				is_crc = TRUE;
				break;
			default:
				is_crc = FALSE;
				break;
		}
	}

	return is_crc;
}

/**
 * @brief Check a block and DIF data, computing the appropriate SCSI status
 *
 * @par Description
 * This function is used to check blocks and DIF when given an unknown DIF
 * status using the following logic:
 *
 * Given the address of the last good block, and a length of bytes that includes
 * the block with the DIF error, find the bad block. If a block is found with an
 * app_tag or ref_tag error, then return the appropriate error. No block is expected
 * to have a block guard error since hardware "fixes" the crc. So if no block in the
 * range of blocks has an error, then it is presumed to be a BLOCK GUARD error.
 *
 * @param io Pointer to the IO object.
 * @param length Length of bytes covering the good blocks.
 * @param check_length Length of bytes that covers the bad block.
 * @param is_crc True if guard is using CRC format.
 *
 * @return Returns SCSI status.
 */

static ocs_scsi_io_status_e
ocs_scsi_dif_check_unknown(ocs_io_t *io, uint32_t length, uint32_t check_length, int is_crc)
{
	uint32_t i;
	ocs_t *ocs = io->ocs;
	ocs_hw_dif_info_t *dif_info = &io->hw_dif;
	ocs_scsi_io_status_e scsi_status = OCS_SCSI_STATUS_DIF_GUARD_ERROR;
	uint32_t blocksize;			/* data block size */
	uint64_t first_check_block;		/* first block following total data placed */
	uint64_t last_check_block;		/* last block to check */
	uint32_t check_count;			/* count of blocks to check */
	ocs_scsi_vaddr_len_t addrlen[4];	/* address-length pairs returned from target */
	int32_t addrlen_count;			/* count of address-length pairs */
	ocs_dif_t *dif;				/* pointer to DIF block returned from target */
	ocs_scsi_dif_info_t scsi_dif_info = io->scsi_dif_info;

	blocksize = ocs_hw_dif_mem_blocksize(&io->hw_dif, TRUE);
	first_check_block = length / blocksize;
	last_check_block = ((length + check_length) / blocksize);
	check_count = last_check_block - first_check_block;

	ocs_log_debug(ocs, "blocksize %d first check_block %" PRId64 " last_check_block %" PRId64 " check_count %d\n",
		blocksize, first_check_block, last_check_block, check_count);

	for (i = first_check_block; i < last_check_block; i++) {
		addrlen_count = ocs_scsi_get_block_vaddr(io, (scsi_dif_info.lba + i), addrlen, ARRAY_SIZE(addrlen), (void**) &dif);
		if (addrlen_count < 0) {
			ocs_log_test(ocs, "ocs_scsi_get_block_vaddr() failed: %d\n", addrlen_count);
			scsi_status = OCS_SCSI_STATUS_DIF_UNKNOWN_ERROR;
			break;
		}

		if (! ocs_scsi_dif_check_guard(dif_info, addrlen, addrlen_count, dif, is_crc)) {
			ocs_log_debug(ocs, "block guard check error, lba %" PRId64 "\n", scsi_dif_info.lba + i);
			scsi_status = OCS_SCSI_STATUS_DIF_GUARD_ERROR;
			break;
		}
		if (! ocs_scsi_dif_check_app_tag(ocs, dif_info, scsi_dif_info.app_tag, dif)) {
			ocs_log_debug(ocs, "app tag check error, lba %" PRId64 "\n", scsi_dif_info.lba + i);
			scsi_status = OCS_SCSI_STATUS_DIF_APP_TAG_ERROR;
			break;
		}
		if (! ocs_scsi_dif_check_ref_tag(ocs, dif_info, (scsi_dif_info.ref_tag + i), dif)) {
			ocs_log_debug(ocs, "ref tag check error, lba %" PRId64 "\n", scsi_dif_info.lba + i);
			scsi_status = OCS_SCSI_STATUS_DIF_REF_TAG_ERROR;
			break;
		}

	}
	return scsi_status;
}

/**
 * @brief Check the block guard of block data
 *
 * @par Description
 * Using the dif_info for the transfer, check the block guard value.
 *
 * @param dif_info Pointer to HW DIF info data.
 * @param addrlen Array of address length pairs.
 * @param addrlen_count Number of entries in the addrlen[] array.
 * @param dif Pointer to the DIF data block being checked.
 * @param is_crc True if guard is using CRC format.
 *
 * @return Returns TRUE if block guard check is ok.
 */
static uint32_t
ocs_scsi_dif_check_guard(ocs_hw_dif_info_t *dif_info, ocs_scsi_vaddr_len_t addrlen[], uint32_t addrlen_count,
	ocs_dif_t *dif, int is_crc)
{
	uint16_t crc = dif_info->dif_seed;
	uint32_t i;
	uint16_t checksum;

	if ((dif == NULL)  || !dif_info->check_guard) {
		return TRUE;
	}

	if (is_crc) {
		for (i = 0; i < addrlen_count; i++) {
			crc = ocs_scsi_dif_calc_crc(addrlen[i].vaddr, addrlen[i].length, crc);
		}
		return (crc == ocs_be16toh(dif->crc));
	} else {
		checksum = ocs_scsi_dif_calc_checksum(addrlen, addrlen_count);

		return (checksum == dif->crc);
	}
}

/**
 * @brief Check the app tag of dif data
 *
 * @par Description
 * Using the dif_info for the transfer, check the app tag.
 *
 * @param ocs Pointer to the ocs structure for logging.
 * @param dif_info Pointer to HW DIF info data.
 * @param exp_app_tag The value the app tag is expected to be.
 * @param dif Pointer to the DIF data block being checked.
 *
 * @return Returns TRUE if app tag check is ok.
 */
static uint32_t
ocs_scsi_dif_check_app_tag(ocs_t *ocs, ocs_hw_dif_info_t *dif_info, uint16_t exp_app_tag, ocs_dif_t *dif)
{
	if ((dif == NULL)  || !dif_info->check_app_tag) {
		return TRUE;
	}

	ocs_log_debug(ocs, "expected app tag 0x%x,  actual 0x%x\n",
		exp_app_tag, ocs_be16toh(dif->app_tag));

	return (exp_app_tag == ocs_be16toh(dif->app_tag));
}

/**
 * @brief Check the ref tag of dif data
 *
 * @par Description
 * Using the dif_info for the transfer, check the app tag.
 *
 * @param ocs Pointer to the ocs structure for logging.
 * @param dif_info Pointer to HW DIF info data.
 * @param exp_ref_tag The value the ref tag is expected to be.
 * @param dif Pointer to the DIF data block being checked.
 *
 * @return Returns TRUE if ref tag check is ok.
 */
static uint32_t
ocs_scsi_dif_check_ref_tag(ocs_t *ocs, ocs_hw_dif_info_t *dif_info, uint32_t exp_ref_tag, ocs_dif_t *dif)
{
	if ((dif == NULL)  || !dif_info->check_ref_tag) {
		return TRUE;
	}

	if (exp_ref_tag != ocs_be32toh(dif->ref_tag)) {
		ocs_log_debug(ocs, "expected ref tag 0x%x, actual 0x%x\n",
			exp_ref_tag, ocs_be32toh(dif->ref_tag));
		return FALSE;
	} else {
		return TRUE;
	}
}

/**
 * @brief Return count of SGE's required for request
 *
 * @par Description
 * An accurate count of SGEs is computed and returned.
 *
 * @param hw_dif Pointer to HW dif information.
 * @param sgl Pointer to SGL from back end.
 * @param sgl_count Count of SGEs in SGL.
 *
 * @return Count of SGEs.
 */
static uint32_t
ocs_scsi_count_sgls(ocs_hw_dif_info_t *hw_dif, ocs_scsi_sgl_t *sgl, uint32_t sgl_count)
{
	uint32_t count = 0;
	uint32_t i;

	/* Convert DIF Information */
	if (hw_dif->dif_oper != OCS_HW_DIF_OPER_DISABLED) {

		/* If we're not DIF separate, then emit a seed SGE */
		if (!hw_dif->dif_separate) {
			count++;
		}

		for (i = 0; i < sgl_count; i++) {
			/* If DIF is enabled, and DIF is separate, then append a SEED then DIF SGE */
			if (hw_dif->dif_separate) {
				count += 2;
			}

			count++;
		}
	} else {
		count = sgl_count;
	}
	return count;
}

static int32_t
ocs_scsi_build_sgls(ocs_hw_t *hw, ocs_hw_io_t *hio, ocs_hw_dif_info_t *hw_dif, ocs_scsi_sgl_t *sgl, uint32_t sgl_count, ocs_hw_io_type_e type)
{
	int32_t rc;
	uint32_t i;
	ocs_t *ocs = hw->os;
	uint32_t blocksize = 0;
	uint32_t blockcount;

	ocs_assert(hio, -1);

	/* Initialize HW SGL */
	rc = ocs_hw_io_init_sges(hw, hio, type);
	if (rc) {
		ocs_log_err(ocs, "ocs_hw_io_init_sges failed: %d\n", rc);
		return -1;
	}

	/* Convert DIF Information */
	if (hw_dif->dif_oper != OCS_HW_DIF_OPER_DISABLED) {

		/* If we're not DIF separate, then emit a seed SGE */
		if (!hw_dif->dif_separate) {
			rc = ocs_hw_io_add_seed_sge(hw, hio, hw_dif);
			if (rc) {
				return rc;
			}
		}

		/* if we are doing DIF separate, then figure out the block size so that we
		 * can update the ref tag in the DIF seed SGE.   Also verify that the
		 * the sgl lengths are all multiples of the blocksize
		 */
		if (hw_dif->dif_separate) {
			switch(hw_dif->blk_size) {
			case OCS_HW_DIF_BK_SIZE_512:	blocksize = 512; break;
			case OCS_HW_DIF_BK_SIZE_1024:	blocksize = 1024; break;
			case OCS_HW_DIF_BK_SIZE_2048:	blocksize = 2048; break;
			case OCS_HW_DIF_BK_SIZE_4096:	blocksize = 4096; break;
			case OCS_HW_DIF_BK_SIZE_520:	blocksize = 520; break;
			case OCS_HW_DIF_BK_SIZE_4104:	blocksize = 4104; break;
			default:
				ocs_log_test(hw->os, "Inavlid hw_dif blocksize %d\n", hw_dif->blk_size);
				return -1;
			}
			for (i = 0; i < sgl_count; i++) {
				if ((sgl[i].len % blocksize) != 0) {
					ocs_log_test(hw->os, "sgl[%d] len of %ld is not multiple of blocksize\n",
						     i, sgl[i].len);
					return -1;
				}
			}
		}

		for (i = 0; i < sgl_count; i++) {
			ocs_assert(sgl[i].addr, -1);
			ocs_assert(sgl[i].len, -1);

			/* If DIF is enabled, and DIF is separate, then append a SEED then DIF SGE */
			if (hw_dif->dif_separate) {
				rc = ocs_hw_io_add_seed_sge(hw, hio, hw_dif);
				if (rc) {
					return rc;
				}
				rc = ocs_hw_io_add_dif_sge(hw, hio, sgl[i].dif_addr);
				if (rc) {
					return rc;
				}
				/* Update the ref_tag for the next DIF seed SGE */
				blockcount = sgl[i].len / blocksize;
				if (hw_dif->dif_oper == OCS_HW_DIF_OPER_INSERT) {
					hw_dif->ref_tag_repl += blockcount;
				} else {
					hw_dif->ref_tag_cmp += blockcount;
				}
			}

			/* Add data SGE */
			rc = ocs_hw_io_add_sge(hw, hio, sgl[i].addr, sgl[i].len);
			if (rc) {
				ocs_log_err(ocs, "ocs_hw_io_add_sge failed: count=%d rc=%d\n",
						sgl_count, rc);
				return rc;
			}
		}
	} else {
		for (i = 0; i < sgl_count; i++) {
			ocs_assert(sgl[i].addr, -1);
			ocs_assert(sgl[i].len, -1);

			/* Add data SGE */
			rc = ocs_hw_io_add_sge(hw, hio, sgl[i].addr, sgl[i].len);
			if (rc) {
				ocs_log_err(ocs, "ocs_hw_io_add_sge failed: count=%d rc=%d\n",
						sgl_count, rc);
				return rc;
			}

		}
	}
	return 0;
}

/**
 * @ingroup scsi_api_base
 * @brief Convert SCSI API T10 DIF information into the FC HW format.
 *
 * @param ocs Pointer to the ocs structure for logging.
 * @param scsi_dif_info Pointer to the SCSI API T10 DIF fields.
 * @param hw_dif_info Pointer to the FC HW API T10 DIF fields.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */

static int32_t
ocs_scsi_convert_dif_info(ocs_t *ocs, ocs_scsi_dif_info_t *scsi_dif_info, ocs_hw_dif_info_t *hw_dif_info)
{
	uint32_t dif_seed;
	ocs_memset(hw_dif_info, 0, sizeof(ocs_hw_dif_info_t));

	if (scsi_dif_info == NULL) {
		hw_dif_info->dif_oper = OCS_HW_DIF_OPER_DISABLED;
		hw_dif_info->blk_size =  OCS_HW_DIF_BK_SIZE_NA;
		return 0;
	}

	/* Convert the DIF operation */
	switch(scsi_dif_info->dif_oper) {
	case OCS_SCSI_DIF_OPER_IN_NODIF_OUT_CRC:
		hw_dif_info->dif_oper = OCS_HW_SGE_DIF_OP_IN_NODIF_OUT_CRC;
		hw_dif_info->dif = SLI4_DIF_INSERT;
		break;
	case OCS_SCSI_DIF_OPER_IN_CRC_OUT_NODIF:
		hw_dif_info->dif_oper = OCS_HW_SGE_DIF_OP_IN_CRC_OUT_NODIF;
		hw_dif_info->dif = SLI4_DIF_STRIP;
		break;
	case OCS_SCSI_DIF_OPER_IN_NODIF_OUT_CHKSUM:
		hw_dif_info->dif_oper = OCS_HW_SGE_DIF_OP_IN_NODIF_OUT_CHKSUM;
		hw_dif_info->dif = SLI4_DIF_INSERT;
		break;
	case OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_NODIF:
		hw_dif_info->dif_oper = OCS_HW_SGE_DIF_OP_IN_CHKSUM_OUT_NODIF;
		hw_dif_info->dif = SLI4_DIF_STRIP;
		break;
	case OCS_SCSI_DIF_OPER_IN_CRC_OUT_CRC:
		hw_dif_info->dif_oper = OCS_HW_SGE_DIF_OP_IN_CRC_OUT_CRC;
		hw_dif_info->dif = SLI4_DIF_PASS_THROUGH;
		break;
	case OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_CHKSUM:
		hw_dif_info->dif_oper = OCS_HW_SGE_DIF_OP_IN_CHKSUM_OUT_CHKSUM;
		hw_dif_info->dif = SLI4_DIF_PASS_THROUGH;
		break;
	case OCS_SCSI_DIF_OPER_IN_CRC_OUT_CHKSUM:
		hw_dif_info->dif_oper = OCS_HW_SGE_DIF_OP_IN_CRC_OUT_CHKSUM;
		hw_dif_info->dif = SLI4_DIF_PASS_THROUGH;
		break;
	case OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_CRC:
		hw_dif_info->dif_oper = OCS_HW_SGE_DIF_OP_IN_CHKSUM_OUT_CRC;
		hw_dif_info->dif = SLI4_DIF_PASS_THROUGH;
		break;
	case OCS_SCSI_DIF_OPER_IN_RAW_OUT_RAW:
		hw_dif_info->dif_oper = OCS_HW_SGE_DIF_OP_IN_RAW_OUT_RAW;
		hw_dif_info->dif = SLI4_DIF_PASS_THROUGH;
		break;
	default:
		ocs_log_test(ocs, "unhandled SCSI DIF operation %d\n",
			     scsi_dif_info->dif_oper);
		return -1;
	}

	switch(scsi_dif_info->blk_size) {
	case OCS_SCSI_DIF_BK_SIZE_512:
		hw_dif_info->blk_size = OCS_HW_DIF_BK_SIZE_512;
		break;
	case OCS_SCSI_DIF_BK_SIZE_1024:
		hw_dif_info->blk_size = OCS_HW_DIF_BK_SIZE_1024;
		break;
	case OCS_SCSI_DIF_BK_SIZE_2048:
		hw_dif_info->blk_size = OCS_HW_DIF_BK_SIZE_2048;
		break;
	case OCS_SCSI_DIF_BK_SIZE_4096:
		hw_dif_info->blk_size = OCS_HW_DIF_BK_SIZE_4096;
		break;
	case OCS_SCSI_DIF_BK_SIZE_520:
		hw_dif_info->blk_size = OCS_HW_DIF_BK_SIZE_520;
		break;
	case OCS_SCSI_DIF_BK_SIZE_4104:
		hw_dif_info->blk_size = OCS_HW_DIF_BK_SIZE_4104;
		break;
	default:
		ocs_log_test(ocs, "unhandled SCSI DIF block size %d\n",
			     scsi_dif_info->blk_size);
		return -1;
	}

	/* If the operation is an INSERT the tags provided are the ones that should be
	 * inserted, otherwise they're the ones to be checked against. */
	if (hw_dif_info->dif == SLI4_DIF_INSERT ) {
		hw_dif_info->ref_tag_repl = scsi_dif_info->ref_tag;
		hw_dif_info->app_tag_repl = scsi_dif_info->app_tag;
	} else {
		hw_dif_info->ref_tag_cmp = scsi_dif_info->ref_tag;
		hw_dif_info->app_tag_cmp = scsi_dif_info->app_tag;
	}

	hw_dif_info->check_ref_tag = scsi_dif_info->check_ref_tag;
	hw_dif_info->check_app_tag = scsi_dif_info->check_app_tag;
	hw_dif_info->check_guard = scsi_dif_info->check_guard;
	hw_dif_info->auto_incr_ref_tag = 1;
	hw_dif_info->dif_separate = scsi_dif_info->dif_separate;
	hw_dif_info->disable_app_ffff = scsi_dif_info->disable_app_ffff;
	hw_dif_info->disable_app_ref_ffff = scsi_dif_info->disable_app_ref_ffff;

	ocs_hw_get(&ocs->hw, OCS_HW_DIF_SEED, &dif_seed);
	hw_dif_info->dif_seed = dif_seed;

	return 0;
}

/**
 * @ingroup scsi_api_base
 * @brief This function logs the SGLs for an IO.
 *
 * @param io Pointer to the IO context.
 */
static void ocs_log_sgl(ocs_io_t *io)
{
	ocs_hw_io_t *hio = io->hio;
	sli4_sge_t *data = NULL;
	uint32_t *dword = NULL;
	uint32_t i;
	uint32_t n_sge;

	scsi_io_trace(io, "def_sgl at 0x%x 0x%08x\n",
		      ocs_addr32_hi(hio->def_sgl.phys),
		      ocs_addr32_lo(hio->def_sgl.phys));
	n_sge = (hio->sgl == &hio->def_sgl ? hio->n_sge : hio->def_sgl_count);
	for (i = 0, data = hio->def_sgl.virt; i < n_sge; i++, data++) {
		dword = (uint32_t*)data;

		scsi_io_trace(io, "SGL %2d 0x%08x 0x%08x 0x%08x 0x%08x\n",
			 i, dword[0], dword[1], dword[2], dword[3]);

		if (dword[2] & (1U << 31)) {
			break;
		}
	}

	if (hio->ovfl_sgl != NULL &&
		hio->sgl == hio->ovfl_sgl) {
		scsi_io_trace(io, "Overflow at 0x%x 0x%08x\n",
			      ocs_addr32_hi(hio->ovfl_sgl->phys),
			      ocs_addr32_lo(hio->ovfl_sgl->phys));
		for (i = 0, data = hio->ovfl_sgl->virt; i < hio->n_sge; i++, data++) {
			dword = (uint32_t*)data;

			scsi_io_trace(io, "SGL %2d 0x%08x 0x%08x 0x%08x 0x%08x\n",
				 i, dword[0], dword[1], dword[2], dword[3]);
			if (dword[2] & (1U << 31)) {
				break;
			}
		}
	}

}


/**
 * @brief Check pending error asynchronous callback function.
 *
 * @par Description
 * Invoke the HW callback function for a given IO. This function is called
 * from the NOP mailbox completion context.
 *
 * @param hw Pointer to HW object.
 * @param status Completion status.
 * @param mqe Mailbox completion queue entry.
 * @param arg General purpose argument.
 *
 * @return Returns 0.
 */
static int32_t
ocs_scsi_check_pending_async_cb(ocs_hw_t *hw, int32_t status, uint8_t *mqe, void *arg)
{
	ocs_io_t *io = arg;

	if (io != NULL) {
		if (io->hw_cb != NULL) {
			ocs_hw_done_t cb = io->hw_cb;

			io->hw_cb = NULL;
			cb(io->hio, NULL, 0, SLI4_FC_WCQE_STATUS_DISPATCH_ERROR, 0, io);
		}
	}
	return 0;
}

/**
 * @brief Check for pending IOs to dispatch.
 *
 * @par Description
 * If there are IOs on the pending list, and a HW IO is available, then
 * dispatch the IOs.
 *
 * @param ocs Pointer to the OCS structure.
 *
 * @return None.
 */

void
ocs_scsi_check_pending(ocs_t *ocs)
{
	ocs_xport_t *xport = ocs->xport;
	ocs_io_t *io;
	ocs_hw_io_t *hio;
	int32_t status;
	int count = 0;
	int dispatch;

	/* Guard against recursion */
	if (ocs_atomic_add_return(&xport->io_pending_recursing, 1)) {
		/* This function is already running.  Decrement and return. */
		ocs_atomic_sub_return(&xport->io_pending_recursing, 1);
		return;
	}

	do {
		ocs_lock(&xport->io_pending_lock);
			status = 0;
			hio = NULL;
			io = ocs_list_remove_head(&xport->io_pending_list);
			if (io != NULL) {
				if (io->io_type == OCS_IO_TYPE_ABORT) {
					hio = NULL;
				} else {
					hio = ocs_hw_io_alloc(&ocs->hw);
					if (hio == NULL) {
						/*
						 * No HW IO available.
						 * Put IO back on the front of pending list
						 */
						ocs_list_add_head(&xport->io_pending_list, io);
						io = NULL;
					} else {
						hio->eq = io->hw_priv;
					}
				}
			}
		/* Must drop the lock before dispatching the IO */
		ocs_unlock(&xport->io_pending_lock);

		if (io != NULL) {
			count++;

			/*
			 * We pulled an IO off the pending list,
			 * and either got an HW IO or don't need one
			 */
			ocs_atomic_sub_return(&xport->io_pending_count, 1);
			if (hio == NULL) {
				status = ocs_scsi_io_dispatch_no_hw_io(io);
			} else {
				status = ocs_scsi_io_dispatch_hw_io(io, hio);
			}
			if (status) {
				/*
				 * Invoke the HW callback, but do so in the separate execution context,
				 * provided by the NOP mailbox completion processing context by using
				 * ocs_hw_async_call()
				 */
				if (ocs_hw_async_call(&ocs->hw, ocs_scsi_check_pending_async_cb, io)) {
					ocs_log_test(ocs, "call to ocs_hw_async_call() failed\n");
				}
			}
		}
	} while (io != NULL);


	/*
	 * If nothing was removed from the list,
	 * we might be in a case where we need to abort an
	 * active IO and the abort is on the pending list.
	 * Look for an abort we can dispatch.
	 */
	if (count == 0 ) {
		dispatch = 0;

		ocs_lock(&xport->io_pending_lock);
			ocs_list_foreach(&xport->io_pending_list, io) {
				if (io->io_type == OCS_IO_TYPE_ABORT) {
					if (io->io_to_abort->hio != NULL) {
						/* This IO has a HW IO, so it is active.  Dispatch the abort. */
						dispatch = 1;
					} else {
						/* Leave this abort on the pending list and keep looking */
						dispatch = 0;
					}
				}
				if (dispatch) {
					ocs_list_remove(&xport->io_pending_list, io);
					ocs_atomic_sub_return(&xport->io_pending_count, 1);
					break;
				}
			}
		ocs_unlock(&xport->io_pending_lock);

		if (dispatch) {
			status = ocs_scsi_io_dispatch_no_hw_io(io);
			if (status) {
				if (ocs_hw_async_call(&ocs->hw, ocs_scsi_check_pending_async_cb, io)) {
					ocs_log_test(ocs, "call to ocs_hw_async_call() failed\n");
				}
			}
		}
	}

	ocs_atomic_sub_return(&xport->io_pending_recursing, 1);
	return;
}

/**
 * @brief Attempt to dispatch a non-abort IO
 *
 * @par Description
 * An IO is dispatched:
 * - if the pending list is not empty, add IO to pending list
 *   and call a function to process the pending list.
 * - if pending list is empty, try to allocate a HW IO. If none
 *   is available, place this IO at the tail of the pending IO
 *   list.
 * - if HW IO is available, attach this IO to the HW IO and
 *   submit it.
 *
 * @param io Pointer to IO structure.
 * @param cb Callback function.
 *
 * @return Returns 0 on success, a negative error code value on failure.
 */

int32_t
ocs_scsi_io_dispatch(ocs_io_t *io, void *cb)
{
	ocs_hw_io_t *hio;
	ocs_t *ocs = io->ocs;
	ocs_xport_t *xport = ocs->xport;

	ocs_assert(io->cmd_tgt || io->cmd_ini, -1);
	ocs_assert((io->io_type != OCS_IO_TYPE_ABORT), -1);
	io->hw_cb = cb;

	/*
	 * if this IO already has a HW IO, then this is either not the first phase of
	 * the IO. Send it to the HW.
	 */
	if (io->hio != NULL) {
		return ocs_scsi_io_dispatch_hw_io(io, io->hio);
	}

	/*
	 * We don't already have a HW IO associated with the IO. First check
	 * the pending list. If not empty, add IO to the tail and process the
	 * pending list.
	 */
	ocs_lock(&xport->io_pending_lock);
		if (!ocs_list_empty(&xport->io_pending_list)) {
			/*
			 * If this is a low latency request, the put at the front of the IO pending
			 * queue, otherwise put it at the end of the queue.
			 */
			if (io->low_latency) {
				ocs_list_add_head(&xport->io_pending_list, io);
			} else {
				ocs_list_add_tail(&xport->io_pending_list, io);
			}
			ocs_unlock(&xport->io_pending_lock);
			ocs_atomic_add_return(&xport->io_pending_count, 1);
			ocs_atomic_add_return(&xport->io_total_pending, 1);

			/* process pending list */
			ocs_scsi_check_pending(ocs);
			return 0;
		}
	ocs_unlock(&xport->io_pending_lock);

	/*
	 * We don't have a HW IO associated with the IO and there's nothing
	 * on the pending list. Attempt to allocate a HW IO and dispatch it.
	 */
	hio = ocs_hw_io_alloc(&io->ocs->hw);
	if (hio == NULL) {

		/* Couldn't get a HW IO. Save this IO on the pending list */
		ocs_lock(&xport->io_pending_lock);
			ocs_list_add_tail(&xport->io_pending_list, io);
		ocs_unlock(&xport->io_pending_lock);

		ocs_atomic_add_return(&xport->io_total_pending, 1);
		ocs_atomic_add_return(&xport->io_pending_count, 1);
		return 0;
	}

	/* We successfully allocated a HW IO; dispatch to HW */
	return ocs_scsi_io_dispatch_hw_io(io, hio);
}

/**
 * @brief Attempt to dispatch an Abort IO.
 *
 * @par Description
 * An Abort IO is dispatched:
 * - if the pending list is not empty, add IO to pending list
 *   and call a function to process the pending list.
 * - if pending list is empty, send abort to the HW.
 *
 * @param io Pointer to IO structure.
 * @param cb Callback function.
 *
 * @return Returns 0 on success, a negative error code value on failure.
 */

int32_t
ocs_scsi_io_dispatch_abort(ocs_io_t *io, void *cb)
{
	ocs_t *ocs = io->ocs;
	ocs_xport_t *xport = ocs->xport;

	ocs_assert((io->io_type == OCS_IO_TYPE_ABORT), -1);
	io->hw_cb = cb;

	/*
	 * For aborts, we don't need a HW IO, but we still want to pass through
	 * the pending list to preserve ordering. Thus, if the pending list is
	 * not empty, add this abort to the pending list and process the pending list.
	 */
	ocs_lock(&xport->io_pending_lock);
		if (!ocs_list_empty(&xport->io_pending_list)) {
			ocs_list_add_tail(&xport->io_pending_list, io);
			ocs_unlock(&xport->io_pending_lock);
			ocs_atomic_add_return(&xport->io_pending_count, 1);
			ocs_atomic_add_return(&xport->io_total_pending, 1);

			/* process pending list */
			ocs_scsi_check_pending(ocs);
			return 0;
		}
	ocs_unlock(&xport->io_pending_lock);

	/* nothing on pending list, dispatch abort */
	return ocs_scsi_io_dispatch_no_hw_io(io);

}

/**
 * @brief Dispatch IO
 *
 * @par Description
 * An IO and its associated HW IO is dispatched to the HW.
 *
 * @param io Pointer to IO structure.
 * @param hio Pointer to HW IO structure from which IO will be
 * dispatched.
 *
 * @return Returns 0 on success, a negative error code value on failure.
 */

static int32_t
ocs_scsi_io_dispatch_hw_io(ocs_io_t *io, ocs_hw_io_t *hio)
{
	int32_t rc;
	ocs_t *ocs = io->ocs;

	/* Got a HW IO; update ini/tgt_task_tag with HW IO info and dispatch */
	io->hio = hio;
	if (io->cmd_tgt) {
		io->tgt_task_tag = hio->indicator;
	} else if (io->cmd_ini) {
		io->init_task_tag = hio->indicator;
	}
	io->hw_tag = hio->reqtag;

	hio->eq = io->hw_priv;

	/* Copy WQ steering */
	switch(io->wq_steering) {
	case OCS_SCSI_WQ_STEERING_CLASS >> OCS_SCSI_WQ_STEERING_SHIFT:
		hio->wq_steering = OCS_HW_WQ_STEERING_CLASS;
		break;
	case OCS_SCSI_WQ_STEERING_REQUEST >> OCS_SCSI_WQ_STEERING_SHIFT:
		hio->wq_steering = OCS_HW_WQ_STEERING_REQUEST;
		break;
	case OCS_SCSI_WQ_STEERING_CPU >> OCS_SCSI_WQ_STEERING_SHIFT:
		hio->wq_steering = OCS_HW_WQ_STEERING_CPU;
		break;
	}


	switch (io->io_type) {
	case OCS_IO_TYPE_IO: {
		uint32_t max_sgl;
		uint32_t total_count;
		uint32_t host_allocated;

		ocs_hw_get(&ocs->hw, OCS_HW_N_SGL, &max_sgl);
		ocs_hw_get(&ocs->hw, OCS_HW_SGL_CHAINING_HOST_ALLOCATED, &host_allocated);

		/*
		 * If the requested SGL is larger than the default size, then we can allocate
		 * an overflow SGL.
		 */
		total_count = ocs_scsi_count_sgls(&io->hw_dif, io->sgl, io->sgl_count);

		/*
		 * Lancer requires us to allocate the chained memory area, but
		 * Skyhawk must use the SGL list associated with another XRI.
		 */
		if (host_allocated && total_count > max_sgl) {
			/* Compute count needed, the number extra plus 1 for the link sge */
			uint32_t count = total_count - max_sgl + 1;
			rc = ocs_dma_alloc(ocs, &io->ovfl_sgl, count*sizeof(sli4_sge_t), 64);
			if (rc) {
				ocs_log_err(ocs, "ocs_dma_alloc overflow sgl failed\n");
				break;
			}
			rc = ocs_hw_io_register_sgl(&ocs->hw, io->hio, &io->ovfl_sgl, count);
			if (rc) {
				ocs_scsi_io_free_ovfl(io);
				ocs_log_err(ocs, "ocs_hw_io_register_sgl() failed\n");
				break;
			}
			/* EVT: update chained_io_count */
			io->node->chained_io_count++;
		}

		rc = ocs_scsi_build_sgls(&ocs->hw, io->hio, &io->hw_dif, io->sgl, io->sgl_count, io->hio_type);
		if (rc) {
			ocs_scsi_io_free_ovfl(io);
			break;
		}

		if (OCS_LOG_ENABLE_SCSI_TRACE(ocs)) {
			ocs_log_sgl(io);
		}

		if (io->app_id) {
			io->iparam.fcp_tgt.app_id = io->app_id;
		}

		rc = ocs_hw_io_send(&io->ocs->hw, io->hio_type, io->hio, io->wire_len, &io->iparam, &io->node->rnode,
			io->hw_cb, io);
		break;
	}
	case OCS_IO_TYPE_ELS:
	case OCS_IO_TYPE_CT: {
		rc = ocs_hw_srrs_send(&ocs->hw, io->hio_type, io->hio,
			&io->els_req, io->wire_len,
			&io->els_rsp, &io->node->rnode, &io->iparam,
			io->hw_cb, io);
		break;
	}
	case OCS_IO_TYPE_CT_RESP: {
		rc = ocs_hw_srrs_send(&ocs->hw, io->hio_type, io->hio,
			&io->els_rsp, io->wire_len,
			NULL, &io->node->rnode, &io->iparam,
			io->hw_cb, io);
		break;
	}
	case OCS_IO_TYPE_BLS_RESP: {
		/* no need to update tgt_task_tag for BLS response since the RX_ID
		 * will be specified by the payload, not the XRI */
		rc = ocs_hw_srrs_send(&ocs->hw, io->hio_type, io->hio,
			NULL, 0, NULL, &io->node->rnode, &io->iparam, io->hw_cb, io);
		break;
	}
	default:
		scsi_io_printf(io, "Unknown IO type=%d\n", io->io_type);
		rc = -1;
		break;
	}
	return rc;
}

/**
 * @brief Dispatch IO
 *
 * @par Description
 * An IO that does require a HW IO is dispatched to the HW.
 *
 * @param io Pointer to IO structure.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */

static int32_t
ocs_scsi_io_dispatch_no_hw_io(ocs_io_t *io)
{
	int32_t rc;

	switch (io->io_type) {
	case OCS_IO_TYPE_ABORT: {
		ocs_hw_io_t *hio_to_abort = NULL;
		ocs_assert(io->io_to_abort, -1);
		hio_to_abort = io->io_to_abort->hio;

		if (hio_to_abort == NULL) {
			/*
			 * If "IO to abort" does not have an associated HW IO, immediately
			 * make callback with success. The command must have been sent to
			 * the backend, but the data phase has not yet started, so we don't
			 * have a HW IO.
			 *
			 * Note: since the backend shims should be taking a reference
			 * on io_to_abort, it should not be possible to have been completed
			 * and freed by the backend before the abort got here.
			 */
			scsi_io_printf(io, "IO: " SCSI_IOFMT " not active\n",
				       SCSI_IOFMT_ARGS(io->io_to_abort));
			((ocs_hw_done_t)io->hw_cb)(io->hio, NULL, 0, SLI4_FC_WCQE_STATUS_SUCCESS, 0, io);
			rc = 0;
		} else {
			/* HW IO is valid, abort it */
			scsi_io_printf(io, "aborting " SCSI_IOFMT "\n", SCSI_IOFMT_ARGS(io->io_to_abort));
			rc = ocs_hw_io_abort(&io->ocs->hw, hio_to_abort, io->send_abts,
					      io->hw_cb, io);
			if (rc) {
				int status = SLI4_FC_WCQE_STATUS_SUCCESS;
				if ((rc != OCS_HW_RTN_IO_NOT_ACTIVE) &&
				    (rc != OCS_HW_RTN_IO_ABORT_IN_PROGRESS)) {
					status = -1;
					scsi_io_printf(io, "Failed to abort IO: " SCSI_IOFMT " status=%d\n",
						       SCSI_IOFMT_ARGS(io->io_to_abort), rc);
				}
				((ocs_hw_done_t)io->hw_cb)(io->hio, NULL, 0, status, 0, io);
				rc = 0;
			}
		}

		break;
	}
	default:
		scsi_io_printf(io, "Unknown IO type=%d\n", io->io_type);
		rc = -1;
		break;
	}
	return rc;
}

/**
 * @ingroup scsi_api_base
 * @brief Send read/write data.
 *
 * @par Description
 * This call is made by a target-server to initiate a SCSI read or write data phase, transferring
 * data between the target to the remote initiator. The payload is specified by the
 * scatter-gather list @c sgl of length @c sgl_count. The @c wire_len argument
 * specifies the payload length (independent of the scatter-gather list cumulative length).
 * @n @n
 * The @c flags argument has one bit, OCS_SCSI_LAST_DATAPHASE, which is a hint to the base
 * driver that it may use auto SCSI response features if the hardware supports it.
 * @n @n
 * Upon completion, the callback function @b cb is called with flags indicating that the
 * IO has completed (OCS_SCSI_IO_COMPL) and another data phase or response may be sent;
 * that the IO has completed and no response needs to be sent (OCS_SCSI_IO_COMPL_NO_RSP);
 * or that the IO was aborted (OCS_SCSI_IO_ABORTED).
 *
 * @param io Pointer to the IO context.
 * @param flags Flags controlling the sending of data.
 * @param dif_info Pointer to T10 DIF fields, or NULL if no DIF.
 * @param sgl Pointer to the payload scatter-gather list.
 * @param sgl_count Count of the scatter-gather list elements.
 * @param xwire_len Length of the payload on wire, in bytes.
 * @param type HW IO type.
 * @param enable_ar Enable auto-response if true.
 * @param cb Completion callback.
 * @param arg Application-supplied callback data.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */

static inline int32_t
ocs_scsi_xfer_data(ocs_io_t *io, uint32_t flags,
	ocs_scsi_dif_info_t *dif_info,
	ocs_scsi_sgl_t *sgl, uint32_t sgl_count, uint32_t xwire_len,
	ocs_hw_io_type_e type, int enable_ar,
	ocs_scsi_io_cb_t cb, void *arg)
{
	int32_t rc;
	ocs_t *ocs;
	uint32_t disable_ar_tgt_dif = FALSE;
	size_t residual = 0;

	if ((dif_info != NULL) && (dif_info->dif_oper == OCS_SCSI_DIF_OPER_DISABLED)) {
		dif_info = NULL;
	}

	ocs_assert(io, -1);

	if (dif_info != NULL) {
		ocs_hw_get(&io->ocs->hw, OCS_HW_DISABLE_AR_TGT_DIF, &disable_ar_tgt_dif);
		if (disable_ar_tgt_dif) {
			enable_ar = FALSE;
		}
	}

	io->sgl_count = sgl_count;

	/* If needed, copy SGL */
	if (sgl && (sgl != io->sgl)) {
		ocs_assert(sgl_count <= io->sgl_allocated, -1);
		ocs_memcpy(io->sgl, sgl, sgl_count*sizeof(*io->sgl));
	}

	ocs = io->ocs;
	ocs_assert(ocs, -1);
	ocs_assert(io->node, -1);

	scsi_io_trace(io, "%s wire_len %d\n", (type == OCS_HW_IO_TARGET_READ) ? "send" : "recv", xwire_len);

	ocs_assert(sgl, -1);
	ocs_assert(sgl_count > 0, -1);
	ocs_assert(io->exp_xfer_len > io->transferred, -1);

	io->hio_type = type;

	io->scsi_tgt_cb = cb;
	io->scsi_tgt_cb_arg = arg;

	rc = ocs_scsi_convert_dif_info(ocs, dif_info, &io->hw_dif);
	if (rc) {
		return rc;
	}

	/* If DIF is used, then save lba for error recovery */
	if (dif_info) {
		io->scsi_dif_info = *dif_info;
	}

	io->wire_len = MIN(xwire_len, io->exp_xfer_len - io->transferred);
	residual = (xwire_len - io->wire_len);

	ocs_memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.fcp_tgt.ox_id = io->init_task_tag;
	io->iparam.fcp_tgt.offset = io->transferred;
	io->iparam.fcp_tgt.dif_oper = io->hw_dif.dif;
	io->iparam.fcp_tgt.blk_size = io->hw_dif.blk_size;
	io->iparam.fcp_tgt.cs_ctl = io->cs_ctl;
	io->iparam.fcp_tgt.timeout = io->timeout;

	/* if this is the last data phase and there is no residual, enable
	 * auto-good-response
	 */
	if (enable_ar && (flags & OCS_SCSI_LAST_DATAPHASE) &&
		(residual == 0) && ((io->transferred + io->wire_len) == io->exp_xfer_len) && (!(flags & OCS_SCSI_NO_AUTO_RESPONSE))) {
		io->iparam.fcp_tgt.flags |= SLI4_IO_AUTO_GOOD_RESPONSE;
		io->auto_resp = TRUE;
	} else {
		io->auto_resp = FALSE;
	}

	/* save this transfer length */
	io->xfer_req = io->wire_len;

	/* Adjust the transferred count to account for overrun
	 * when the residual is calculated in ocs_scsi_send_resp
	 */
	io->transferred += residual;

	/* Adjust the SGL size if there is overrun */

	if (residual) {
		ocs_scsi_sgl_t  *sgl_ptr = &io->sgl[sgl_count-1];

		while (residual) {
			size_t len = sgl_ptr->len;
			if ( len > residual) {
				sgl_ptr->len = len - residual;
				residual = 0;
			} else {
				sgl_ptr->len = 0;
				residual -= len;
				io->sgl_count--;
			}
			sgl_ptr--;
		}
	}

	/* Set latency and WQ steering */
	io->low_latency = (flags & OCS_SCSI_LOW_LATENCY) != 0;
	io->wq_steering = (flags & OCS_SCSI_WQ_STEERING_MASK) >> OCS_SCSI_WQ_STEERING_SHIFT;
	io->wq_class = (flags & OCS_SCSI_WQ_CLASS_MASK) >> OCS_SCSI_WQ_CLASS_SHIFT;

	return ocs_scsi_io_dispatch(io, ocs_target_io_cb);
}


int32_t
ocs_scsi_send_rd_data(ocs_io_t *io, uint32_t flags,
	ocs_scsi_dif_info_t *dif_info,
	ocs_scsi_sgl_t *sgl, uint32_t sgl_count, uint32_t len,
	ocs_scsi_io_cb_t cb, void *arg)
{
	return ocs_scsi_xfer_data(io, flags, dif_info, sgl, sgl_count, len, OCS_HW_IO_TARGET_READ,
				  enable_tsend_auto_resp(io->ocs), cb, arg);
}

int32_t
ocs_scsi_recv_wr_data(ocs_io_t *io, uint32_t flags,
	ocs_scsi_dif_info_t *dif_info,
	ocs_scsi_sgl_t *sgl, uint32_t sgl_count, uint32_t len,
	ocs_scsi_io_cb_t cb, void *arg)
{
	return ocs_scsi_xfer_data(io, flags, dif_info, sgl, sgl_count, len, OCS_HW_IO_TARGET_WRITE,
				  enable_treceive_auto_resp(io->ocs), cb, arg);
}

/**
 * @ingroup scsi_api_base
 * @brief Free overflow SGL.
 *
 * @par Description
 * Free the overflow SGL if it is present.
 *
 * @param io Pointer to IO object.
 *
 * @return None.
 */
static void
ocs_scsi_io_free_ovfl(ocs_io_t *io) {
	if (io->ovfl_sgl.size) {
		ocs_dma_free(io->ocs, &io->ovfl_sgl);
	}
}

/**
 * @ingroup scsi_api_base
 * @brief Send response data.
 *
 * @par Description
 * This function is used by a target-server to send the SCSI response data to a remote
 * initiator node. The target-server populates the @c ocs_scsi_cmd_resp_t
 * argument with scsi status, status qualifier, sense data, and response data, as
 * needed.
 * @n @n
 * Upon completion, the callback function @c cb is invoked. The target-server will generally
 * clean up its IO context resources and call ocs_scsi_io_complete().
 *
 * @param io Pointer to the IO context.
 * @param flags Flags to control sending of the SCSI response.
 * @param rsp Pointer to the response data populated by the caller.
 * @param cb Completion callback.
 * @param arg Application-specified completion callback argument.

 * @return Returns 0 on success, or a negative error code value on failure.
 */
int32_t
ocs_scsi_send_resp(ocs_io_t *io, uint32_t flags, ocs_scsi_cmd_resp_t *rsp, ocs_scsi_io_cb_t cb, void *arg)
{
	ocs_t *ocs;
	int32_t residual;
	int auto_resp = TRUE;		/* Always try auto resp */
	uint8_t scsi_status = 0;
	uint16_t scsi_status_qualifier = 0;
	uint8_t *sense_data = NULL;
	uint32_t sense_data_length = 0;

	ocs_assert(io, -1);

	ocs = io->ocs;
	ocs_assert(ocs, -1);

	ocs_assert(io->node, -1);

	ocs_scsi_convert_dif_info(ocs, NULL, &io->hw_dif);

	if (rsp) {
		scsi_status = rsp->scsi_status;
		scsi_status_qualifier = rsp->scsi_status_qualifier;
		sense_data = rsp->sense_data;
		sense_data_length = rsp->sense_data_length;
		residual = rsp->residual;
	} else {
		residual = io->exp_xfer_len - io->transferred;
	}

	io->wire_len = 0;
	io->hio_type = OCS_HW_IO_TARGET_RSP;

	io->scsi_tgt_cb = cb;
	io->scsi_tgt_cb_arg = arg;

	ocs_memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.fcp_tgt.ox_id = io->init_task_tag;
	io->iparam.fcp_tgt.offset = 0;
	io->iparam.fcp_tgt.cs_ctl = io->cs_ctl;
	io->iparam.fcp_tgt.timeout = io->timeout;

	/* Set low latency queueing request */
	io->low_latency = (flags & OCS_SCSI_LOW_LATENCY) != 0;
	io->wq_steering = (flags & OCS_SCSI_WQ_STEERING_MASK) >> OCS_SCSI_WQ_STEERING_SHIFT;
	io->wq_class = (flags & OCS_SCSI_WQ_CLASS_MASK) >> OCS_SCSI_WQ_CLASS_SHIFT;

	if ((scsi_status != 0) || residual || sense_data_length) {
		fcp_rsp_iu_t *fcprsp = io->rspbuf.virt;

		if (!fcprsp) {
			ocs_log_err(ocs, "NULL response buffer\n");
			return -1;
		}

		auto_resp = FALSE;

		ocs_memset(fcprsp, 0, sizeof(*fcprsp));

		io->wire_len += (sizeof(*fcprsp) - sizeof(fcprsp->data));

		fcprsp->scsi_status = scsi_status;
		*((uint16_t*)fcprsp->status_qualifier) = ocs_htobe16(scsi_status_qualifier);

		/* set residual status if necessary */
		if (residual != 0) {
			/* FCP: if data transferred is less than the amount expected, then this is an
			 * underflow.  If data transferred would have been greater than the amount expected
			 * then this is an overflow
			 */
			if (residual > 0) {
				fcprsp->flags |= FCP_RESID_UNDER;
				*((uint32_t *)fcprsp->fcp_resid) = ocs_htobe32(residual);
			} else {
				fcprsp->flags |= FCP_RESID_OVER;
				*((uint32_t *)fcprsp->fcp_resid) = ocs_htobe32(-residual);
			}
		}

		if (sense_data && sense_data_length) {
			ocs_assert(sense_data_length <= sizeof(fcprsp->data), -1);
			fcprsp->flags |= FCP_SNS_LEN_VALID;
			ocs_memcpy(fcprsp->data, sense_data, sense_data_length);
			*((uint32_t*)fcprsp->fcp_sns_len) = ocs_htobe32(sense_data_length);
			io->wire_len += sense_data_length;
		}

		io->sgl[0].addr = io->rspbuf.phys;
		io->sgl[0].dif_addr = 0;
		io->sgl[0].len = io->wire_len;
		io->sgl_count = 1;
	}

	if (auto_resp) {
		io->iparam.fcp_tgt.flags |= SLI4_IO_AUTO_GOOD_RESPONSE;
	}

	return ocs_scsi_io_dispatch(io, ocs_target_io_cb);
}

/**
 * @ingroup scsi_api_base
 * @brief Send TMF response data.
 *
 * @par Description
 * This function is used by a target-server to send SCSI TMF response data to a remote
 * initiator node.
 * Upon completion, the callback function @c cb is invoked. The target-server will generally
 * clean up its IO context resources and call ocs_scsi_io_complete().
 *
 * @param io Pointer to the IO context.
 * @param rspcode TMF response code.
 * @param addl_rsp_info Additional TMF response information (may be NULL for zero data).
 * @param cb Completion callback.
 * @param arg Application-specified completion callback argument.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */
int32_t
ocs_scsi_send_tmf_resp(ocs_io_t *io, ocs_scsi_tmf_resp_e rspcode, uint8_t addl_rsp_info[3],
		ocs_scsi_io_cb_t cb, void *arg)
{
	int32_t rc = -1;
	ocs_t *ocs = NULL;
	fcp_rsp_iu_t *fcprsp = NULL;
	fcp_rsp_info_t *rspinfo = NULL;
	uint8_t fcp_rspcode;

	ocs_assert(io, -1);
	ocs_assert(io->ocs, -1);
	ocs_assert(io->node, -1);

	ocs = io->ocs;

	io->wire_len = 0;
	ocs_scsi_convert_dif_info(ocs, NULL, &io->hw_dif);

	switch(rspcode) {
	case OCS_SCSI_TMF_FUNCTION_COMPLETE:
		fcp_rspcode = FCP_TMF_COMPLETE;
		break;
	case OCS_SCSI_TMF_FUNCTION_SUCCEEDED:
	case OCS_SCSI_TMF_FUNCTION_IO_NOT_FOUND:
		fcp_rspcode = FCP_TMF_SUCCEEDED;
		break;
	case OCS_SCSI_TMF_FUNCTION_REJECTED:
		fcp_rspcode = FCP_TMF_REJECTED;
		break;
	case OCS_SCSI_TMF_INCORRECT_LOGICAL_UNIT_NUMBER:
		fcp_rspcode = FCP_TMF_INCORRECT_LUN;
		break;
	case OCS_SCSI_TMF_SERVICE_DELIVERY:
		fcp_rspcode = FCP_TMF_FAILED;
		break;
	default:
		fcp_rspcode = FCP_TMF_REJECTED;
		break;
	}

	io->hio_type = OCS_HW_IO_TARGET_RSP;

	io->scsi_tgt_cb = cb;
	io->scsi_tgt_cb_arg = arg;

	if (io->tmf_cmd == OCS_SCSI_TMF_ABORT_TASK) {
		rc = ocs_target_send_bls_resp(io, cb, arg);
		return rc;
	}

	/* populate the FCP TMF response */
	fcprsp = io->rspbuf.virt;
	ocs_memset(fcprsp, 0, sizeof(*fcprsp));

	fcprsp->flags |= FCP_RSP_LEN_VALID;

	rspinfo = (fcp_rsp_info_t*) fcprsp->data;
	if (addl_rsp_info != NULL) {
		ocs_memcpy(rspinfo->addl_rsp_info, addl_rsp_info, sizeof(rspinfo->addl_rsp_info));
	}
	rspinfo->rsp_code = fcp_rspcode;

	io->wire_len = sizeof(*fcprsp) - sizeof(fcprsp->data) + sizeof(*rspinfo);

	*((uint32_t*)fcprsp->fcp_rsp_len) = ocs_htobe32(sizeof(*rspinfo));

	io->sgl[0].addr = io->rspbuf.phys;
	io->sgl[0].dif_addr = 0;
	io->sgl[0].len = io->wire_len;
	io->sgl_count = 1;

	ocs_memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.fcp_tgt.ox_id = io->init_task_tag;
	io->iparam.fcp_tgt.offset = 0;
	io->iparam.fcp_tgt.cs_ctl = io->cs_ctl;
	io->iparam.fcp_tgt.timeout = io->timeout;

	rc = ocs_scsi_io_dispatch(io, ocs_target_io_cb);

	return rc;
}


/**
 * @brief Process target abort callback.
 *
 * @par Description
 * Accepts HW abort requests.
 *
 * @param hio HW IO context.
 * @param rnode Remote node.
 * @param length Length of response data.
 * @param status Completion status.
 * @param ext_status Extended completion status.
 * @param app Application-specified callback data.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */

static int32_t
ocs_target_abort_cb(ocs_hw_io_t *hio, ocs_remote_node_t *rnode, uint32_t length, int32_t status, uint32_t ext_status, void *app)
{
	ocs_io_t *io = app;
	ocs_t *ocs;
	ocs_scsi_io_status_e scsi_status;

	ocs_assert(io, -1);
	ocs_assert(io->ocs, -1);

	ocs = io->ocs;

	if (io->abort_cb) {
		ocs_scsi_io_cb_t abort_cb = io->abort_cb;
		void *abort_cb_arg = io->abort_cb_arg;

		io->abort_cb = NULL;
		io->abort_cb_arg = NULL;

		switch (status) {
		case SLI4_FC_WCQE_STATUS_SUCCESS:
			scsi_status = OCS_SCSI_STATUS_GOOD;
			break;
		case SLI4_FC_WCQE_STATUS_LOCAL_REJECT:
			switch (ext_status) {
			case SLI4_FC_LOCAL_REJECT_NO_XRI:
				scsi_status = OCS_SCSI_STATUS_NO_IO;
				break;
			case SLI4_FC_LOCAL_REJECT_ABORT_IN_PROGRESS:
				scsi_status = OCS_SCSI_STATUS_ABORT_IN_PROGRESS;
				break;
			default:
				/* TODO: we have seen 0x15 (abort in progress) */
				scsi_status = OCS_SCSI_STATUS_ERROR;
				break;
			}
			break;
		case SLI4_FC_WCQE_STATUS_FCP_RSP_FAILURE:
			scsi_status = OCS_SCSI_STATUS_CHECK_RESPONSE;
			break;
		default:
			scsi_status = OCS_SCSI_STATUS_ERROR;
			break;
		}
		/* invoke callback */
		abort_cb(io->io_to_abort, scsi_status, 0, abort_cb_arg);
	}

	ocs_assert(io != io->io_to_abort, -1);

	/* done with IO to abort */
	ocs_ref_put(&io->io_to_abort->ref); /* ocs_ref_get(): ocs_scsi_tgt_abort_io() */

	ocs_io_free(ocs, io);

	ocs_scsi_check_pending(ocs);
	return 0;
}

/**
 * @ingroup scsi_api_base
 * @brief Abort a target IO.
 *
 * @par Description
 * This routine is called from a SCSI target-server. It initiates an abort of a
 * previously-issued target data phase or response request.
 *
 * @param io IO context.
 * @param cb SCSI target server callback.
 * @param arg SCSI target server supplied callback argument.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
ocs_scsi_tgt_abort_io(ocs_io_t *io, ocs_scsi_io_cb_t cb, void *arg)
{
	ocs_t *ocs;
	ocs_xport_t *xport;
	int32_t rc;

	ocs_io_t *abort_io = NULL;
	ocs_assert(io, -1);
	ocs_assert(io->node, -1);
	ocs_assert(io->ocs, -1);

	ocs = io->ocs;
	xport = ocs->xport;

	/* take a reference on IO being aborted */
	if ((ocs_ref_get_unless_zero(&io->ref) == 0)) {
		/* command no longer active */
		scsi_io_printf(io, "command no longer active\n");
		return -1;
	}

	/*
	 * allocate a new IO to send the abort request. Use ocs_io_alloc() directly, as
	 * we need an IO object that will not fail allocation due to allocations being
	 * disabled (in ocs_scsi_io_alloc())
	 */
	abort_io = ocs_io_alloc(ocs);
	if (abort_io == NULL) {
		ocs_atomic_add_return(&xport->io_alloc_failed_count, 1);
		ocs_ref_put(&io->ref); /* ocs_ref_get(): same function */
		return -1;
	}

	/* Save the target server callback and argument */
	ocs_assert(abort_io->hio == NULL, -1);

	/* set generic fields */
	abort_io->cmd_tgt = TRUE;
	abort_io->node = io->node;

	/* set type and abort-specific fields */
	abort_io->io_type = OCS_IO_TYPE_ABORT;
	abort_io->display_name = "tgt_abort";
	abort_io->io_to_abort = io;
	abort_io->send_abts = FALSE;
	abort_io->abort_cb = cb;
	abort_io->abort_cb_arg = arg;

	/* now dispatch IO */
	rc = ocs_scsi_io_dispatch_abort(abort_io, ocs_target_abort_cb);
	if (rc) {
		ocs_ref_put(&io->ref); /* ocs_ref_get(): same function */
	}
	return rc;
}

/**
 * @brief Process target BLS response callback.
 *
 * @par Description
 * Accepts HW abort requests.
 *
 * @param hio HW IO context.
 * @param rnode Remote node.
 * @param length Length of response data.
 * @param status Completion status.
 * @param ext_status Extended completion status.
 * @param app Application-specified callback data.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */

static int32_t
ocs_target_bls_resp_cb(ocs_hw_io_t *hio, ocs_remote_node_t *rnode, uint32_t length, int32_t status, uint32_t ext_status, void *app)
{
	ocs_io_t *io = app;
	ocs_t *ocs;
	ocs_scsi_io_status_e bls_status;

	ocs_assert(io, -1);
	ocs_assert(io->ocs, -1);

	ocs = io->ocs;

	/* BLS isn't really a "SCSI" concept, but use SCSI status */
	if (status) {
		io_error_log(io, "s=%#x x=%#x\n", status, ext_status);
		bls_status = OCS_SCSI_STATUS_ERROR;
	} else {
		bls_status = OCS_SCSI_STATUS_GOOD;
	}

	if (io->bls_cb) {
		ocs_scsi_io_cb_t bls_cb = io->bls_cb;
		void *bls_cb_arg = io->bls_cb_arg;

		io->bls_cb = NULL;
		io->bls_cb_arg = NULL;

		/* invoke callback */
		bls_cb(io, bls_status, 0, bls_cb_arg);
	}

	ocs_scsi_check_pending(ocs);
	return 0;
}

/**
 * @brief Complete abort request.
 *
 * @par Description
 * An abort request is completed by posting a BA_ACC for the IO that requested the abort.
 *
 * @param io Pointer to the IO context.
 * @param cb Callback function to invoke upon completion.
 * @param arg Application-specified completion callback argument.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */

static int32_t
ocs_target_send_bls_resp(ocs_io_t *io, ocs_scsi_io_cb_t cb, void *arg)
{
	int32_t rc;
	fc_ba_acc_payload_t *acc;

	ocs_assert(io, -1);

	/* fill out IO structure with everything needed to send BA_ACC */
	ocs_memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.bls.ox_id = io->init_task_tag;
	io->iparam.bls.rx_id = io->abort_rx_id;

	acc = (void *)io->iparam.bls.payload;

	ocs_memset(io->iparam.bls.payload, 0, sizeof(io->iparam.bls.payload));
	acc->ox_id = io->iparam.bls.ox_id;
	acc->rx_id = io->iparam.bls.rx_id;
	acc->high_seq_cnt = UINT16_MAX;

	/* generic io fields have already been populated */

	/* set type and BLS-specific fields */
	io->io_type = OCS_IO_TYPE_BLS_RESP;
	io->display_name = "bls_rsp";
	io->hio_type = OCS_HW_BLS_ACC;
	io->bls_cb = cb;
	io->bls_cb_arg = arg;

	/* dispatch IO */
	rc = ocs_scsi_io_dispatch(io, ocs_target_bls_resp_cb);
	return rc;
}

/**
 * @ingroup scsi_api_base
 * @brief Notify the base driver that the IO is complete.
 *
 * @par Description
 * This function is called by a target-server to notify the base driver that an IO
 * has completed, allowing for the base driver to free resources.
 * @n
 * @n @b Note: This function is not called by initiator-clients.
 *
 * @param io Pointer to IO context.
 *
 * @return None.
 */
void
ocs_scsi_io_complete(ocs_io_t *io)
{
	ocs_assert(io);

	if (!ocs_io_busy(io)) {
		ocs_log_test(io->ocs, "Got completion for non-busy io with tag 0x%x\n", io->tag);
		return;
	}

	scsi_io_trace(io, "freeing io 0x%p %s\n", io, io->display_name);
	ocs_assert(ocs_ref_read_count(&io->ref) > 0);
	ocs_ref_put(&io->ref); /* ocs_ref_get(): ocs_scsi_io_alloc() */
}


/**
 * @brief Handle initiator IO completion.
 *
 * @par Description
 * This callback is made upon completion of an initiator operation (initiator read/write command).
 *
 * @param hio HW IO context.
 * @param rnode Remote node.
 * @param length Length of completion data.
 * @param status Completion status.
 * @param ext_status Extended completion status.
 * @param app Application-specified callback data.
 *
 * @return None.
 */

static void
ocs_initiator_io_cb(ocs_hw_io_t *hio, ocs_remote_node_t *rnode, uint32_t length,
	int32_t status, uint32_t ext_status, void *app)
{
	ocs_io_t *io = app;
	ocs_t *ocs;
	ocs_scsi_io_status_e scsi_status;

	ocs_assert(io);
	ocs_assert(io->scsi_ini_cb);

	scsi_io_trace(io, "status x%x ext_status x%x\n", status, ext_status);

	ocs = io->ocs;
	ocs_assert(ocs);

	ocs_scsi_io_free_ovfl(io);

	/* Call target server completion */
	if (io->scsi_ini_cb) {
		fcp_rsp_iu_t *fcprsp = io->rspbuf.virt;
		ocs_scsi_cmd_resp_t rsp;
		ocs_scsi_rsp_io_cb_t cb = io->scsi_ini_cb;
		uint32_t flags = 0;
		uint8_t *pd = fcprsp->data;

		/* Clear the callback before invoking the callback */
		io->scsi_ini_cb = NULL;

		ocs_memset(&rsp, 0, sizeof(rsp));

		/* Unless status is FCP_RSP_FAILURE, fcprsp is not filled in */
		switch (status) {
		case SLI4_FC_WCQE_STATUS_SUCCESS:
			scsi_status = OCS_SCSI_STATUS_GOOD;
			break;
		case SLI4_FC_WCQE_STATUS_FCP_RSP_FAILURE:
			scsi_status = OCS_SCSI_STATUS_CHECK_RESPONSE;
			rsp.scsi_status = fcprsp->scsi_status;
			rsp.scsi_status_qualifier = ocs_be16toh(*((uint16_t*)fcprsp->status_qualifier));

			if (fcprsp->flags & FCP_RSP_LEN_VALID) {
				rsp.response_data = pd;
				rsp.response_data_length = ocs_fc_getbe32(fcprsp->fcp_rsp_len);
				pd += rsp.response_data_length;
			}
			if (fcprsp->flags & FCP_SNS_LEN_VALID) {
				uint32_t sns_len = ocs_fc_getbe32(fcprsp->fcp_sns_len);
				rsp.sense_data = pd;
				rsp.sense_data_length = sns_len;
				pd += sns_len;
			}
			/* Set residual */
			if (fcprsp->flags & FCP_RESID_OVER) {
				rsp.residual = -ocs_fc_getbe32(fcprsp->fcp_resid);
				rsp.response_wire_length = length;
			} else	if (fcprsp->flags & FCP_RESID_UNDER) {
				rsp.residual = ocs_fc_getbe32(fcprsp->fcp_resid);
				rsp.response_wire_length = length;
			}

			/*
			 * Note: The FCP_RSP_FAILURE can be returned for initiator IOs when the total data
			 * placed does not match the requested length even if the status is good. If
			 * the status is all zeroes, then we have to assume that a frame(s) were
			 * dropped and change the status to LOCAL_REJECT/OUT_OF_ORDER_DATA
			 */
			if (length != io->wire_len) {
				uint32_t rsp_len = ext_status;
				uint8_t *rsp_bytes = io->rspbuf.virt;
				uint32_t i;
				uint8_t all_zeroes = (rsp_len > 0);
				/* Check if the rsp is zero */
				for (i = 0; i < rsp_len; i++) {
					if (rsp_bytes[i] != 0) {
						all_zeroes = FALSE;
						break;
					}
				}
				if (all_zeroes) {
					scsi_status = OCS_SCSI_STATUS_ERROR;
					ocs_log_test(io->ocs, "[%s]" SCSI_IOFMT "local reject=0x%02x\n",
						     io->node->display_name, SCSI_IOFMT_ARGS(io),
						     SLI4_FC_LOCAL_REJECT_OUT_OF_ORDER_DATA);
				}
			}
			break;
		case SLI4_FC_WCQE_STATUS_LOCAL_REJECT:
			if (ext_status == SLI4_FC_LOCAL_REJECT_SEQUENCE_TIMEOUT) {
				scsi_status = OCS_SCSI_STATUS_COMMAND_TIMEOUT;
			} else {
				scsi_status = OCS_SCSI_STATUS_ERROR;
			}
			break;
		case SLI4_FC_WCQE_STATUS_DI_ERROR:
			if (ext_status & 0x01) {
				scsi_status = OCS_SCSI_STATUS_DIF_GUARD_ERROR;
			} else if (ext_status & 0x02) {
				scsi_status = OCS_SCSI_STATUS_DIF_APP_TAG_ERROR;
			} else if (ext_status & 0x04) {
				scsi_status = OCS_SCSI_STATUS_DIF_REF_TAG_ERROR;
			} else {
				scsi_status = OCS_SCSI_STATUS_DIF_UNKNOWN_ERROR;
			}
			break;
		default:
			scsi_status = OCS_SCSI_STATUS_ERROR;
			break;
		}

		cb(io, scsi_status, &rsp, flags, io->scsi_ini_cb_arg);

	}
	ocs_scsi_check_pending(ocs);
}

/**
 * @ingroup scsi_api_base
 * @brief Initiate initiator read IO.
 *
 * @par Description
 * This call is made by an initiator-client to send a SCSI read command. The payload
 * for the command is given by a scatter-gather list @c sgl for @c sgl_count
 * entries.
 * @n @n
 * Upon completion, the callback @b cb is invoked and passed request status.
 * If the command completed successfully, the callback is given SCSI response data.
 *
 * @param node Pointer to the node.
 * @param io Pointer to the IO context.
 * @param lun LUN value.
 * @param cdb Pointer to the CDB.
 * @param cdb_len Length of the CDB.
 * @param dif_info Pointer to the T10 DIF fields, or NULL if no DIF.
 * @param sgl Pointer to the scatter-gather list.
 * @param sgl_count Count of the scatter-gather list elements.
 * @param wire_len Length of the payload.
 * @param cb Completion callback.
 * @param arg Application-specified completion callback argument.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */
int32_t
ocs_scsi_send_rd_io(ocs_node_t *node, ocs_io_t *io, uint64_t lun, void *cdb, uint32_t cdb_len,
	ocs_scsi_dif_info_t *dif_info,
	ocs_scsi_sgl_t *sgl, uint32_t sgl_count, uint32_t wire_len,
	ocs_scsi_rsp_io_cb_t cb, void *arg)
{
	int32_t rc;

	rc = ocs_scsi_send_io(OCS_HW_IO_INITIATOR_READ, node, io, lun, 0, cdb, cdb_len, dif_info, sgl, sgl_count,
			      wire_len, 0, cb, arg);

	return rc;
}

/**
 * @ingroup scsi_api_base
 * @brief Initiate initiator write IO.
 *
 * @par Description
 * This call is made by an initiator-client to send a SCSI write command. The payload
 * for the command is given by a scatter-gather list @c sgl for @c sgl_count
 * entries.
 * @n @n
 * Upon completion, the callback @c cb is invoked and passed request status. If the command
 * completed successfully, the callback is given SCSI response data.
 *
 * @param node Pointer to the node.
 * @param io Pointer to IO context.
 * @param lun LUN value.
 * @param cdb Pointer to the CDB.
 * @param cdb_len Length of the CDB.
 * @param dif_info Pointer to the T10 DIF fields, or NULL if no DIF.
 * @param sgl Pointer to the scatter-gather list.
 * @param sgl_count Count of the scatter-gather list elements.
 * @param wire_len Length of the payload.
 * @param cb Completion callback.
 * @param arg Application-specified completion callback argument.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */
int32_t ocs_scsi_send_wr_io(ocs_node_t *node, ocs_io_t *io, uint64_t lun, void *cdb, uint32_t cdb_len,
	ocs_scsi_dif_info_t *dif_info,
	ocs_scsi_sgl_t *sgl, uint32_t sgl_count, uint32_t wire_len,
	ocs_scsi_rsp_io_cb_t cb, void *arg)
{
	int32_t rc;

	rc = ocs_scsi_send_io(OCS_HW_IO_INITIATOR_WRITE, node, io, lun, 0, cdb, cdb_len, dif_info, sgl, sgl_count,
			      wire_len, 0, cb, arg);

	return rc;
}

/**
 * @ingroup scsi_api_base
 * @brief Initiate initiator write IO.
 *
 * @par Description
 * This call is made by an initiator-client to send a SCSI write command. The payload
 * for the command is given by a scatter-gather list @c sgl for @c sgl_count
 * entries.
 * @n @n
 * Upon completion, the callback @c cb is invoked and passed request status. If the command
 * completed successfully, the callback is given SCSI response data.
 *
 * @param node Pointer to the node.
 * @param io Pointer to IO context.
 * @param lun LUN value.
 * @param cdb Pointer to the CDB.
 * @param cdb_len Length of the CDB.
 * @param dif_info Pointer to the T10 DIF fields, or NULL if no DIF.
 * @param sgl Pointer to the scatter-gather list.
 * @param sgl_count Count of the scatter-gather list elements.
 * @param wire_len Length of the payload.
 * @param first_burst Number of first burst bytes to send.
 * @param cb Completion callback.
 * @param arg Application-specified completion callback argument.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */
int32_t
ocs_scsi_send_wr_io_first_burst(ocs_node_t *node, ocs_io_t *io, uint64_t lun, void *cdb, uint32_t cdb_len,
	ocs_scsi_dif_info_t *dif_info,
	ocs_scsi_sgl_t *sgl, uint32_t sgl_count, uint32_t wire_len, uint32_t first_burst,
	ocs_scsi_rsp_io_cb_t cb, void *arg)
{
	int32_t rc;

	rc = ocs_scsi_send_io(OCS_HW_IO_INITIATOR_WRITE, node, io, lun, 0, cdb, cdb_len, dif_info, sgl, sgl_count,
			      wire_len, 0, cb, arg);

	return rc;
}

/**
 * @ingroup scsi_api_base
 * @brief Initiate initiator SCSI command with no data.
 *
 * @par Description
 * This call is made by an initiator-client to send a SCSI command with no data.
 * @n @n
 * Upon completion, the callback @c cb is invoked and passed request status. If the command
 * completed successfully, the callback is given SCSI response data.
 *
 * @param node Pointer to the node.
 * @param io Pointer to the IO context.
 * @param lun LUN value.
 * @param cdb Pointer to the CDB.
 * @param cdb_len Length of the CDB.
 * @param cb Completion callback.
 * @param arg Application-specified completion callback argument.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */
int32_t ocs_scsi_send_nodata_io(ocs_node_t *node, ocs_io_t *io, uint64_t lun, void *cdb, uint32_t cdb_len,
	ocs_scsi_rsp_io_cb_t cb, void *arg)
{
	int32_t rc;

	rc = ocs_scsi_send_io(OCS_HW_IO_INITIATOR_NODATA, node, io, lun, 0, cdb, cdb_len, NULL, NULL, 0, 0, 0, cb, arg);

	return rc;
}
/**
 * @ingroup scsi_api_base
 * @brief Initiate initiator task management operation.
 *
 * @par Description
 * This command is used to send a SCSI task management function command. If the command
 * requires it (QUERY_TASK_SET for example), a payload may be associated with the command.
 * If no payload is required, then @c sgl_count may be zero and @c sgl is ignored.
 * @n @n
 * Upon completion @c cb is invoked with status and SCSI response data.
 *
 * @param node Pointer to the node.
 * @param io Pointer to the IO context.
 * @param io_to_abort Pointer to the IO context to abort in the
 * case of OCS_SCSI_TMF_ABORT_TASK. Note: this can point to the
 * same the same ocs_io_t as @c io, provided that @c io does not
 * have any outstanding work requests.
 * @param lun LUN value.
 * @param tmf Task management command.
 * @param sgl Pointer to the scatter-gather list.
 * @param sgl_count Count of the scatter-gather list elements.
 * @param len Length of the payload.
 * @param cb Completion callback.
 * @param arg Application-specified completion callback argument.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */
int32_t
ocs_scsi_send_tmf(ocs_node_t *node, ocs_io_t *io, ocs_io_t *io_to_abort, uint64_t lun, ocs_scsi_tmf_cmd_e tmf,
	ocs_scsi_sgl_t *sgl, uint32_t sgl_count, uint32_t len, ocs_scsi_rsp_io_cb_t cb, void *arg)
{
	int32_t rc;
	ocs_assert(io, -1);

	if (tmf == OCS_SCSI_TMF_ABORT_TASK) {
		ocs_assert(io_to_abort, -1);

		/* take a reference on IO being aborted */
		if ((ocs_ref_get_unless_zero(&io_to_abort->ref) == 0)) {
			/* command no longer active */
			scsi_io_printf(io, "command no longer active\n");
			return -1;
		}
		/* generic io fields have already been populated */

		/* abort-specific fields */
		io->io_type = OCS_IO_TYPE_ABORT;
		io->display_name = "abort_task";
		io->io_to_abort = io_to_abort;
		io->send_abts = TRUE;
		io->scsi_ini_cb = cb;
		io->scsi_ini_cb_arg = arg;

		/* now dispatch IO */
		rc = ocs_scsi_io_dispatch_abort(io, ocs_scsi_abort_io_cb);
		if (rc) {
			scsi_io_printf(io, "Failed to dispatch abort\n");
			ocs_ref_put(&io->ref); /* ocs_ref_get(): same function */
		}
	} else {
		io->display_name = "tmf";
		rc = ocs_scsi_send_io(OCS_HW_IO_INITIATOR_READ, node, io, lun, tmf, NULL, 0, NULL,
				      sgl, sgl_count, len, 0, cb, arg);
	}

	return rc;
}

/**
 * @ingroup scsi_api_base
 * @brief Send an FCP IO.
 *
 * @par Description
 * An FCP read/write IO command, with optional task management flags, is sent to @c node.
 *
 * @param type HW IO type to send.
 * @param node Pointer to the node destination of the IO.
 * @param io Pointer to the IO context.
 * @param lun LUN value.
 * @param tmf Task management command.
 * @param cdb Pointer to the SCSI CDB.
 * @param cdb_len Length of the CDB, in bytes.
 * @param dif_info Pointer to the T10 DIF fields, or NULL if no DIF.
 * @param sgl Pointer to the scatter-gather list.
 * @param sgl_count Number of SGL entries in SGL.
 * @param wire_len Payload length, in bytes, of data on wire.
 * @param first_burst Number of first burst bytes to send.
 * @param cb Completion callback.
 * @param arg Application-specified completion callback argument.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */

/* tc: could elminiate LUN, as it's part of the IO structure */

static int32_t ocs_scsi_send_io(ocs_hw_io_type_e type, ocs_node_t *node, ocs_io_t *io, uint64_t lun,
	ocs_scsi_tmf_cmd_e tmf, uint8_t *cdb, uint32_t cdb_len,
	ocs_scsi_dif_info_t *dif_info,
	ocs_scsi_sgl_t *sgl, uint32_t sgl_count, uint32_t wire_len, uint32_t first_burst,
	ocs_scsi_rsp_io_cb_t cb, void *arg)
{
	int32_t rc;
	ocs_t *ocs;
	fcp_cmnd_iu_t *cmnd;
	uint32_t cmnd_bytes = 0;
	uint32_t *fcp_dl;
	uint8_t tmf_flags = 0;

	ocs_assert(io->node, -1);
	ocs_assert(io->node == node, -1);
	ocs_assert(io, -1);
	ocs = io->ocs;
	ocs_assert(cb, -1);

	io->sgl_count = sgl_count;

	/* Copy SGL if needed */
	if (sgl != io->sgl) {
		ocs_assert(sgl_count <= io->sgl_allocated, -1);
		ocs_memcpy(io->sgl, sgl, sizeof(*io->sgl) * sgl_count);
	}

	/* save initiator and target task tags for debugging */
	io->tgt_task_tag = 0xffff;

	io->wire_len = wire_len;
	io->hio_type = type;

	if (OCS_LOG_ENABLE_SCSI_TRACE(ocs)) {
		char buf[80];
		ocs_textbuf_t txtbuf;
		uint32_t i;

		ocs_textbuf_init(ocs, &txtbuf, buf, sizeof(buf));

		ocs_textbuf_printf(&txtbuf, "cdb%d: ", cdb_len);
		for (i = 0; i < cdb_len; i ++) {
			ocs_textbuf_printf(&txtbuf, "%02X%s", cdb[i], (i == (cdb_len-1)) ? "" : " ");
		}
		scsi_io_printf(io, "%s len %d, %s\n", (io->hio_type == OCS_HW_IO_INITIATOR_READ) ? "read" :
			(io->hio_type == OCS_HW_IO_INITIATOR_WRITE) ? "write" : "",  io->wire_len,
			ocs_textbuf_get_buffer(&txtbuf));
	}


	ocs_assert(io->cmdbuf.virt, -1);

	cmnd = io->cmdbuf.virt;

	ocs_assert(sizeof(*cmnd) <= io->cmdbuf.size, -1);

	ocs_memset(cmnd, 0, sizeof(*cmnd));

	/* Default FCP_CMND IU doesn't include additional CDB bytes but does include FCP_DL */
	cmnd_bytes = sizeof(fcp_cmnd_iu_t) - sizeof(cmnd->fcp_cdb_and_dl) + sizeof(uint32_t);

	fcp_dl = (uint32_t*)(&(cmnd->fcp_cdb_and_dl));

	if (cdb) {
		if (cdb_len <= 16) {
			ocs_memcpy(cmnd->fcp_cdb, cdb, cdb_len);
		} else {
			uint32_t addl_cdb_bytes;

			ocs_memcpy(cmnd->fcp_cdb, cdb, 16);
			addl_cdb_bytes = cdb_len - 16;
			ocs_memcpy(cmnd->fcp_cdb_and_dl, &(cdb[16]), addl_cdb_bytes);
			/* additional_fcp_cdb_length is in words, not bytes */
			cmnd->additional_fcp_cdb_length = (addl_cdb_bytes + 3) / 4;
			fcp_dl += cmnd->additional_fcp_cdb_length;

			/* Round up additional CDB bytes */
			cmnd_bytes += (addl_cdb_bytes + 3) & ~0x3;
		}
	}

	be64enc(cmnd->fcp_lun, CAM_EXTLUN_BYTE_SWIZZLE(lun));
	
	if (node->fcp2device) {
		if(ocs_get_crn(node, &cmnd->command_reference_number,
					lun)) {
			return -1;
		}
	}

	switch (tmf) {
	case OCS_SCSI_TMF_QUERY_TASK_SET:
		tmf_flags = FCP_QUERY_TASK_SET;
		break;
	case OCS_SCSI_TMF_ABORT_TASK_SET:
		tmf_flags = FCP_ABORT_TASK_SET;
		break;
	case OCS_SCSI_TMF_CLEAR_TASK_SET:
		tmf_flags = FCP_CLEAR_TASK_SET;
		break;
	case OCS_SCSI_TMF_QUERY_ASYNCHRONOUS_EVENT:
		tmf_flags = FCP_QUERY_ASYNCHRONOUS_EVENT;
		break;
	case OCS_SCSI_TMF_LOGICAL_UNIT_RESET:
		tmf_flags = FCP_LOGICAL_UNIT_RESET;
		break;
	case OCS_SCSI_TMF_CLEAR_ACA:
		tmf_flags = FCP_CLEAR_ACA;
		break;
	case OCS_SCSI_TMF_TARGET_RESET:
		tmf_flags = FCP_TARGET_RESET;
		break;
	default:
		tmf_flags = 0;
	}
	cmnd->task_management_flags = tmf_flags;

	*fcp_dl = ocs_htobe32(io->wire_len);

	switch (io->hio_type) {
	case OCS_HW_IO_INITIATOR_READ:
		cmnd->rddata = 1;
		break;
	case OCS_HW_IO_INITIATOR_WRITE:
		cmnd->wrdata = 1;
		break;
	case  OCS_HW_IO_INITIATOR_NODATA:
		/* sets neither */
		break;
	default:
		ocs_log_test(ocs, "bad IO type %d\n", io->hio_type);
		return -1;
	}

	rc = ocs_scsi_convert_dif_info(ocs, dif_info, &io->hw_dif);
	if (rc) {
		return rc;
	}

	io->scsi_ini_cb = cb;
	io->scsi_ini_cb_arg = arg;

	/* set command and response buffers in the iparam */
	io->iparam.fcp_ini.cmnd = &io->cmdbuf;
	io->iparam.fcp_ini.cmnd_size = cmnd_bytes;
	io->iparam.fcp_ini.rsp = &io->rspbuf;
	io->iparam.fcp_ini.flags = 0;
	io->iparam.fcp_ini.dif_oper = io->hw_dif.dif;
	io->iparam.fcp_ini.blk_size = io->hw_dif.blk_size;
	io->iparam.fcp_ini.timeout = io->timeout;
	io->iparam.fcp_ini.first_burst = first_burst;

	return ocs_scsi_io_dispatch(io, ocs_initiator_io_cb);
}

/**
 * @ingroup scsi_api_base
 * @brief Callback for an aborted IO.
 *
 * @par Description
 * Callback function invoked upon completion of an IO abort request.
 *
 * @param hio HW IO context.
 * @param rnode Remote node.
 * @param len Response length.
 * @param status Completion status.
 * @param ext_status Extended completion status.
 * @param arg Application-specific callback, usually IO context.

 * @return Returns 0 on success, or a negative error code value on failure.
 */

static int32_t
ocs_scsi_abort_io_cb(struct ocs_hw_io_s *hio, ocs_remote_node_t *rnode, uint32_t len, int32_t status,
	uint32_t ext_status, void *arg)
{
	ocs_io_t *io = arg;
	ocs_t *ocs;
	ocs_scsi_io_status_e scsi_status = OCS_SCSI_STATUS_GOOD;

	ocs_assert(io, -1);
	ocs_assert(ocs_io_busy(io), -1);
	ocs_assert(io->ocs, -1);
	ocs_assert(io->io_to_abort, -1);
	ocs = io->ocs;

	ocs_log_debug(ocs, "status %d ext %d\n", status, ext_status);

	/* done with IO to abort */
	ocs_ref_put(&io->io_to_abort->ref); /* ocs_ref_get(): ocs_scsi_send_tmf() */

	ocs_scsi_io_free_ovfl(io);

	switch (status) {
	case SLI4_FC_WCQE_STATUS_SUCCESS:
		scsi_status = OCS_SCSI_STATUS_GOOD;
		break;
	case SLI4_FC_WCQE_STATUS_LOCAL_REJECT:
		if (ext_status == SLI4_FC_LOCAL_REJECT_ABORT_REQUESTED) {
			scsi_status = OCS_SCSI_STATUS_ABORTED;
		} else if (ext_status == SLI4_FC_LOCAL_REJECT_NO_XRI) {
			scsi_status = OCS_SCSI_STATUS_NO_IO;
		} else if (ext_status == SLI4_FC_LOCAL_REJECT_ABORT_IN_PROGRESS) {
			scsi_status = OCS_SCSI_STATUS_ABORT_IN_PROGRESS;
		} else {
			ocs_log_test(ocs, "Unhandled local reject 0x%x/0x%x\n", status, ext_status);
			scsi_status = OCS_SCSI_STATUS_ERROR;
		}
		break;
	default:
		scsi_status = OCS_SCSI_STATUS_ERROR;
		break;
	}

	if (io->scsi_ini_cb) {
		(*io->scsi_ini_cb)(io, scsi_status, NULL, 0, io->scsi_ini_cb_arg);
	} else {
		ocs_scsi_io_free(io);
	}

	ocs_scsi_check_pending(ocs);
	return 0;
}

/**
 * @ingroup scsi_api_base
 * @brief Return SCSI API integer valued property.
 *
 * @par Description
 * This function is called by a target-server or initiator-client to
 * retrieve an integer valued property.
 *
 * @param ocs Pointer to the ocs.
 * @param prop Property value to return.
 *
 * @return Returns a value, or 0 if invalid property was requested.
 */
uint32_t
ocs_scsi_get_property(ocs_t *ocs, ocs_scsi_property_e prop)
{
	ocs_xport_t *xport = ocs->xport;
	uint32_t	val;

	switch (prop) {
	case OCS_SCSI_MAX_SGE:
		if (0 == ocs_hw_get(&ocs->hw, OCS_HW_MAX_SGE, &val)) {
			return val;
		}
		break;
	case OCS_SCSI_MAX_SGL:
		if (ocs->ctrlmask & OCS_CTRLMASK_TEST_CHAINED_SGLS) {
			/*
			 * If chain SGL test-mode is enabled, the number of HW SGEs
			 * has been limited; report back original max.
			 */
			return (OCS_FC_MAX_SGL);
		}
		if (0 == ocs_hw_get(&ocs->hw, OCS_HW_N_SGL, &val)) {
			return val;
		}
		break;
	case OCS_SCSI_MAX_IOS:
		return ocs_io_pool_allocated(xport->io_pool);
	case OCS_SCSI_DIF_CAPABLE:
	        if (0 == ocs_hw_get(&ocs->hw, OCS_HW_DIF_CAPABLE, &val)) {
	                return val;
	        }
		break;
	case OCS_SCSI_MAX_FIRST_BURST:
		return 0;
	case OCS_SCSI_DIF_MULTI_SEPARATE:
	        if (ocs_hw_get(&ocs->hw, OCS_HW_DIF_MULTI_SEPARATE, &val) == 0) {
	                return val;
	        }
		break;
	case OCS_SCSI_ENABLE_TASK_SET_FULL:
		/* Return FALSE if we are send frame capable */
		if (ocs_hw_get(&ocs->hw, OCS_HW_SEND_FRAME_CAPABLE, &val) == 0) {
			return ! val;
		}
		break;
	default:
		break;
	}

	ocs_log_debug(ocs, "invalid property request %d\n", prop);
	return 0;
}

/**
 * @ingroup scsi_api_base
 * @brief Return a property pointer.
 *
 * @par Description
 * This function is called by a target-server or initiator-client to
 * retrieve a pointer to the requested property.
 *
 * @param ocs Pointer to the ocs.
 * @param prop Property value to return.
 *
 * @return Returns pointer to the requested property, or NULL otherwise.
 */
void *ocs_scsi_get_property_ptr(ocs_t *ocs, ocs_scsi_property_e prop)
{
	void *rc = NULL;

	switch (prop) {
	case OCS_SCSI_WWNN:
		rc = ocs_hw_get_ptr(&ocs->hw, OCS_HW_WWN_NODE);
		break;
	case OCS_SCSI_WWPN:
		rc = ocs_hw_get_ptr(&ocs->hw, OCS_HW_WWN_PORT);
		break;
	case OCS_SCSI_PORTNUM:
		rc = ocs_hw_get_ptr(&ocs->hw, OCS_HW_PORTNUM);
		break;
	case OCS_SCSI_BIOS_VERSION_STRING:
		rc = ocs_hw_get_ptr(&ocs->hw, OCS_HW_BIOS_VERSION_STRING);
		break;
#if defined(OCS_ENABLE_VPD_SUPPORT)
	case OCS_SCSI_SERIALNUMBER:
	{
		uint8_t *pvpd;
		uint32_t vpd_len;

		if (ocs_hw_get(&ocs->hw, OCS_HW_VPD_LEN, &vpd_len)) {
			ocs_log_test(ocs, "Can't get VPD length\n");
			rc = "\012sn-unknown";
			break;
		}

		pvpd = ocs_hw_get_ptr(&ocs->hw, OCS_HW_VPD);
		if (pvpd) {
			rc = ocs_find_vpd(pvpd, vpd_len, "SN");
		}

		if (rc == NULL ||
		    ocs_strlen(rc) == 0) {
			/* Note: VPD is missing, using wwnn for serial number */
			scsi_log(ocs, "Note: VPD is missing, using wwnn for serial number\n");
			/* Use the last 32 bits of the WWN */
			if ((ocs == NULL) || (ocs->domain == NULL) || (ocs->domain->sport == NULL)) {
				rc = "\011(Unknown)";
			} else {
				rc = &ocs->domain->sport->wwnn_str[8];
			}
		}
		break;
	}
	case OCS_SCSI_PARTNUMBER:
	{
		uint8_t *pvpd;
		uint32_t vpd_len;

		if (ocs_hw_get(&ocs->hw, OCS_HW_VPD_LEN, &vpd_len)) {
			ocs_log_test(ocs, "Can't get VPD length\n");
			rc = "\012pn-unknown";
			break;
		}
		pvpd = ocs_hw_get_ptr(&ocs->hw, OCS_HW_VPD);
		if (pvpd) {
			rc = ocs_find_vpd(pvpd, vpd_len, "PN");
			if (rc == NULL) {
				rc = "\012pn-unknown";
			}
		} else {
			rc = "\012pn-unknown";
		}
		break;
	}
#endif
	default:
		break;
	}

	if (rc == NULL) {
		ocs_log_debug(ocs, "invalid property request %d\n", prop);
	}
	return rc;
}

/**
 * @ingroup scsi_api_base
 * @brief Notify that delete initiator is complete.
 *
 * @par Description
 * Sent by the target-server to notify the base driver that the work started from
 * ocs_scsi_del_initiator() is now complete and that it is safe for the node to
 * release the rest of its resources.
 *
 * @param node Pointer to the node.
 *
 * @return None.
 */
void
ocs_scsi_del_initiator_complete(ocs_node_t *node)
{
	/* Notify the node to resume */
	ocs_node_post_event(node, OCS_EVT_NODE_DEL_INI_COMPLETE, NULL);
}


/**
 * @ingroup scsi_api_base
 * @brief Notify that delete target is complete.
 *
 * @par Description
 * Sent by the initiator-client to notify the base driver that the work started from
 * ocs_scsi_del_target() is now complete and that it is safe for the node to
 * release the rest of its resources.
 *
 * @param node Pointer to the node.
 *
 * @return None.
 */
void
ocs_scsi_del_target_complete(ocs_node_t *node)
{
	/* Notify the node to resume */
	ocs_node_post_event(node, OCS_EVT_NODE_DEL_TGT_COMPLETE, NULL);
}


/**
 * @brief Update transferred count
 *
 * @par Description
 * Updates io->transferred, as required when using first burst, when the amount
 * of first burst data processed differs from the amount of first burst
 * data received.
 *
 * @param io Pointer to the io object.
 * @param transferred Number of bytes transferred out of first burst buffers.
 *
 * @return None.
 */
void
ocs_scsi_update_first_burst_transferred(ocs_io_t *io, uint32_t transferred)
{
	io->transferred = transferred;
}

/**
 * @brief Register bounce callback for multi-threading.
 *
 * @par Description
 * Register the back end bounce function.
 *
 * @param ocs Pointer to device object.
 * @param fctn Function pointer of bounce function.
 *
 * @return None.
 */
void
ocs_scsi_register_bounce(ocs_t *ocs, void(*fctn)(void(*fctn)(void *arg), void *arg, uint32_t s_id, uint32_t d_id,
						 uint32_t ox_id))
{
	ocs_hw_rtn_e rc;

	rc = ocs_hw_callback(&ocs->hw, OCS_HW_CB_BOUNCE, fctn, NULL);
	if (rc) {
		ocs_log_test(ocs, "ocs_hw_callback(OCS_HW_CB_BOUNCE) failed: %d\n", rc);
	}
}
