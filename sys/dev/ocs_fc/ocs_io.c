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
 * Provide IO object allocation.
 */

/*!
 * @defgroup io_alloc IO allocation
 */

#include "ocs.h"
#include "ocs_scsi.h"
#include "ocs_els.h"
#include "ocs_utils.h"

void ocs_mgmt_io_list(ocs_textbuf_t *textbuf, void *io);
void ocs_mgmt_io_get_all(ocs_textbuf_t *textbuf, void *io);
int ocs_mgmt_io_get(ocs_textbuf_t *textbuf, char *parent, char *name, void *io);

static ocs_mgmt_functions_t io_mgmt_functions = {
	.get_list_handler	=	ocs_mgmt_io_list,
	.get_handler		=	ocs_mgmt_io_get,
	.get_all_handler	=	ocs_mgmt_io_get_all,
};

/**
 * @brief IO pool.
 *
 * Structure encapsulating a pool of IO objects.
 *
 */

struct ocs_io_pool_s {
	ocs_t *ocs;			/* Pointer to device object */
	ocs_lock_t lock;		/* IO pool lock */
	uint32_t io_num_ios;		/* Total IOs allocated */
	ocs_pool_t *pool;
};

/**
 * @brief Create a pool of IO objects.
 *
 * @par Description
 * This function allocates memory in larger chucks called
 * "slabs" which are a fixed size. It calculates the number of IO objects that
 * fit within each "slab" and determines the number of "slabs" required to
 * allocate the number of IOs requested. Each of the slabs is allocated and
 * then it grabs each IO object within the slab and adds it to the free list.
 * Individual command, response and SGL DMA buffers are allocated for each IO.
 *
 *           "Slabs"
 *      +----------------+
 *      |                |
 *   +----------------+  |
 *   |    IO          |  |
 *   +----------------+  |
 *   |    ...         |  |
 *   +----------------+__+
 *   |    IO          |
 *   +----------------+
 *
 * @param ocs Driver instance's software context.
 * @param num_io Number of IO contexts to allocate.
 * @param num_sgl Number of SGL entries to allocate for each IO.
 *
 * @return Returns a pointer to a new ocs_io_pool_t on success,
 *         or NULL on failure.
 */

ocs_io_pool_t *
ocs_io_pool_create(ocs_t *ocs, uint32_t num_io, uint32_t num_sgl)
{
	uint32_t i = 0;
	int32_t	rc = -1;
	ocs_io_pool_t *io_pool;

	/* Allocate the IO pool */
	io_pool = ocs_malloc(ocs, sizeof(*io_pool), OCS_M_ZERO | OCS_M_NOWAIT);
	if (io_pool == NULL) {
		ocs_log_err(ocs, "allocate of IO pool failed\n");
		return NULL;;
	}

	io_pool->ocs = ocs;
	io_pool->io_num_ios = num_io;

	/* initialize IO pool lock */
	ocs_lock_init(ocs, &io_pool->lock, "io_pool lock[%d]", ocs->instance_index);

	io_pool->pool = ocs_pool_alloc(ocs, sizeof(ocs_io_t), io_pool->io_num_ios, FALSE);

	for (i = 0; i < io_pool->io_num_ios; i++) {
		ocs_io_t *io = ocs_pool_get_instance(io_pool->pool, i);

		io->tag = i;
		io->instance_index = i;
		io->ocs = ocs;

		/* allocate a command/response dma buffer */
		if (ocs->enable_ini) {
			rc = ocs_dma_alloc(ocs, &io->cmdbuf, SCSI_CMD_BUF_LENGTH, OCS_MIN_DMA_ALIGNMENT);
			if (rc) {
				ocs_log_err(ocs, "ocs_dma_alloc cmdbuf failed\n");
				ocs_io_pool_free(io_pool);
				return NULL;
			}
		}

		/* Allocate a response buffer */
		rc = ocs_dma_alloc(ocs, &io->rspbuf, SCSI_RSP_BUF_LENGTH, OCS_MIN_DMA_ALIGNMENT);
		if (rc) {
			ocs_log_err(ocs, "ocs_dma_alloc cmdbuf failed\n");
			ocs_io_pool_free(io_pool);
			return NULL;
		}

		/* Allocate SGL */
		io->sgl = ocs_malloc(ocs, sizeof(*io->sgl) * num_sgl, OCS_M_NOWAIT | OCS_M_ZERO);
		if (io->sgl == NULL) {
			ocs_log_err(ocs, "malloc sgl's failed\n");
			ocs_io_pool_free(io_pool);
			return NULL;
		}
		io->sgl_allocated = num_sgl;
		io->sgl_count = 0;

		/* Make IO backend call to initialize IO */
		ocs_scsi_tgt_io_init(io);
		ocs_scsi_ini_io_init(io);
		
		rc = ocs_dma_alloc(ocs, &io->els_req, OCS_ELS_REQ_LEN, OCS_MIN_DMA_ALIGNMENT);
		if (rc) {
			ocs_log_err(ocs, "ocs_dma_alloc els_req failed\n");
			ocs_io_pool_free(io_pool);
			return NULL;
 		}

		rc = ocs_dma_alloc(ocs, &io->els_rsp, OCS_ELS_GID_PT_RSP_LEN, OCS_MIN_DMA_ALIGNMENT);
		if (rc) {
			ocs_log_err(ocs, "ocs_dma_alloc els_rsp failed\n");
			ocs_io_pool_free(io_pool);
			return NULL;
 		}
	}

	return io_pool;
}

/**
 * @brief Free IO objects pool
 *
 * @par Description
 * The pool of IO objects are freed.
 *
 * @param io_pool Pointer to IO pool object.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */
int32_t
ocs_io_pool_free(ocs_io_pool_t *io_pool)
{
	ocs_t *ocs;
	uint32_t i;
	ocs_io_t *io;

	if (io_pool != NULL) {
		ocs = io_pool->ocs;
		for (i = 0; i < io_pool->io_num_ios; i++) {
			io = ocs_pool_get_instance(io_pool->pool, i);
			if (!io)
				continue;
			ocs_scsi_tgt_io_exit(io);
			ocs_scsi_ini_io_exit(io);
			if (io->sgl) {
				ocs_free(ocs, io->sgl, sizeof(*io->sgl) * io->sgl_allocated);
			}
			ocs_dma_free(ocs, &io->cmdbuf);
			ocs_dma_free(ocs, &io->rspbuf);
			ocs_dma_free(ocs, &io->els_req);
			ocs_dma_free(ocs, &io->els_rsp);
		}

		if (io_pool->pool != NULL) {
			ocs_pool_free(io_pool->pool);
		}
		ocs_lock_free(&io_pool->lock);
		ocs_free(ocs, io_pool, sizeof(*io_pool));
		ocs->xport->io_pool = NULL;
	}

	return 0;
}

uint32_t ocs_io_pool_allocated(ocs_io_pool_t *io_pool)
{
	return io_pool->io_num_ios;
}

/**
 * @ingroup io_alloc
 * @brief Allocate an object used to track an IO.
 *
 * @param io_pool Pointer to the IO pool.
 *
 * @return Returns the pointer to a new object, or NULL if none available.
 */
ocs_io_t *
ocs_io_pool_io_alloc(ocs_io_pool_t *io_pool)
{
	ocs_io_t *io = NULL;
	ocs_t *ocs;

	ocs_assert(io_pool, NULL);

	ocs = io_pool->ocs;

	ocs_lock(&io_pool->lock);
	if ((io = ocs_pool_get(io_pool->pool)) != NULL) {
		ocs_unlock(&io_pool->lock);

		io->io_type = OCS_IO_TYPE_MAX;
		io->hio_type = OCS_HW_IO_MAX;
		io->hio = NULL;
		io->transferred = 0;
		io->ocs = ocs;
		io->timeout = 0;
		io->sgl_count = 0;
		io->tgt_task_tag = 0;
		io->init_task_tag = 0;
		io->hw_tag = 0;
		io->display_name = "pending";
		io->seq_init = 0;
		io->els_req_free = 0;
		io->mgmt_functions = &io_mgmt_functions;
		io->io_free = 0;
		ocs_atomic_add_return(&ocs->xport->io_active_count, 1);
		ocs_atomic_add_return(&ocs->xport->io_total_alloc, 1);
	} else {
		ocs_unlock(&io_pool->lock);
	}
	return io;
}

/**
 * @ingroup io_alloc
 * @brief Free an object used to track an IO.
 *
 * @param io_pool Pointer to IO pool object.
 * @param io Pointer to the IO object.
 */
void
ocs_io_pool_io_free(ocs_io_pool_t *io_pool, ocs_io_t *io)
{
	ocs_t *ocs;
	ocs_hw_io_t *hio = NULL;

	ocs_assert(io_pool);

	ocs = io_pool->ocs;

	ocs_lock(&io_pool->lock);
		hio = io->hio;
		io->hio = NULL;
		ocs_pool_put(io_pool->pool, io);
	ocs_unlock(&io_pool->lock);

	if (hio) {
		ocs_hw_io_free(&ocs->hw, hio);
	}
	io->io_free = 1;
	ocs_atomic_sub_return(&ocs->xport->io_active_count, 1);
	ocs_atomic_add_return(&ocs->xport->io_total_free, 1);
}

/**
 * @ingroup io_alloc
 * @brief Find an I/O given it's node and ox_id.
 *
 * @param ocs Driver instance's software context.
 * @param node Pointer to node.
 * @param ox_id OX_ID to find.
 * @param rx_id RX_ID to find (0xffff for unassigned).
 */
ocs_io_t *
ocs_io_find_tgt_io(ocs_t *ocs, ocs_node_t *node, uint16_t ox_id, uint16_t rx_id)
{
	ocs_io_t	*io = NULL;

	ocs_lock(&node->active_ios_lock);
		ocs_list_foreach(&node->active_ios, io)
			if ((io->cmd_tgt && (io->init_task_tag == ox_id)) &&
			    ((rx_id == 0xffff) || (io->tgt_task_tag == rx_id))) {
				break;
			}
	ocs_unlock(&node->active_ios_lock);
	return io;
}

/**
 * @ingroup io_alloc
 * @brief Return IO context given the instance index.
 *
 * @par Description
 * Returns a pointer to the IO context given by the instance index.
 *
 * @param ocs Pointer to driver structure.
 * @param index IO instance index to return.
 *
 * @return Returns a pointer to the IO context, or NULL if not found.
 */
ocs_io_t *
ocs_io_get_instance(ocs_t *ocs, uint32_t index)
{
	ocs_xport_t *xport = ocs->xport;
	ocs_io_pool_t *io_pool = xport->io_pool;
	return ocs_pool_get_instance(io_pool->pool, index);
}

/**
 * @brief Generate IO context ddump data.
 *
 * The ddump data for an IO context is generated.
 *
 * @param textbuf Pointer to text buffer.
 * @param io Pointer to IO context.
 *
 * @return None.
 */

void
ocs_ddump_io(ocs_textbuf_t *textbuf, ocs_io_t *io)
{
	ocs_ddump_section(textbuf, "io", io->instance_index);
	ocs_ddump_value(textbuf, "display_name", "%s", io->display_name);
	ocs_ddump_value(textbuf, "node_name", "%s", io->node->display_name);

	ocs_ddump_value(textbuf, "ref_count", "%d", ocs_ref_read_count(&io->ref));
	ocs_ddump_value(textbuf, "io_type", "%d", io->io_type);
	ocs_ddump_value(textbuf, "hio_type", "%d", io->hio_type);
	ocs_ddump_value(textbuf, "cmd_tgt", "%d", io->cmd_tgt);
	ocs_ddump_value(textbuf, "cmd_ini", "%d", io->cmd_ini);
	ocs_ddump_value(textbuf, "send_abts", "%d", io->send_abts);
	ocs_ddump_value(textbuf, "init_task_tag", "0x%x", io->init_task_tag);
	ocs_ddump_value(textbuf, "tgt_task_tag", "0x%x", io->tgt_task_tag);
	ocs_ddump_value(textbuf, "hw_tag", "0x%x", io->hw_tag);
	ocs_ddump_value(textbuf, "tag", "0x%x", io->tag);
	ocs_ddump_value(textbuf, "timeout", "%d", io->timeout);
	ocs_ddump_value(textbuf, "tmf_cmd", "%d", io->tmf_cmd);
	ocs_ddump_value(textbuf, "abort_rx_id", "0x%x", io->abort_rx_id);

	ocs_ddump_value(textbuf, "busy", "%d", ocs_io_busy(io));
	ocs_ddump_value(textbuf, "transferred", "%zu", io->transferred);
	ocs_ddump_value(textbuf, "auto_resp", "%d", io->auto_resp);
	ocs_ddump_value(textbuf, "exp_xfer_len", "%d", io->exp_xfer_len);
	ocs_ddump_value(textbuf, "xfer_req", "%d", io->xfer_req);
	ocs_ddump_value(textbuf, "seq_init", "%d", io->seq_init);

	ocs_ddump_value(textbuf, "alloc_link", "%d", ocs_list_on_list(&io->io_alloc_link));
	ocs_ddump_value(textbuf, "pending_link", "%d", ocs_list_on_list(&io->io_pending_link));
	ocs_ddump_value(textbuf, "backend_link", "%d", ocs_list_on_list(&io->link));

	if (io->hio) {
		ocs_ddump_value(textbuf, "hw_tag", "%#x", io->hio->reqtag);
		ocs_ddump_value(textbuf, "hw_xri", "%#x", io->hio->indicator);
		ocs_ddump_value(textbuf, "hw_type", "%#x", io->hio->type);
	} else {
		ocs_ddump_value(textbuf, "hw_tag", "%s", "pending");
		ocs_ddump_value(textbuf, "hw_xri", "%s", "pending");
		ocs_ddump_value(textbuf, "hw_type", "%s", "pending");
	}

	ocs_scsi_ini_ddump(textbuf, OCS_SCSI_DDUMP_IO, io);
	ocs_scsi_tgt_ddump(textbuf, OCS_SCSI_DDUMP_IO, io);

	ocs_ddump_endsection(textbuf, "io", io->instance_index);
}


void
ocs_mgmt_io_list(ocs_textbuf_t *textbuf, void *object)
{

	/* Readonly values */
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "display_name");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "init_task_tag");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "tag");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "transferred");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "auto_resp");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "exp_xfer_len");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "xfer_req");
}

int
ocs_mgmt_io_get(ocs_textbuf_t *textbuf, char *parent, char *name, void *object)
{
	char qualifier[80];
	int retval = -1;
	ocs_io_t *io = (ocs_io_t *) object;

	snprintf(qualifier, sizeof(qualifier), "%s/io[%d]", parent, io->instance_index);

	/* If it doesn't start with my qualifier I don't know what to do with it */
	if (ocs_strncmp(name, qualifier, strlen(qualifier)) == 0) {
		char *unqualified_name = name + strlen(qualifier) +1;

		/* See if it's a value I can supply */
		if (ocs_strcmp(unqualified_name, "display_name") == 0) {
			ocs_mgmt_emit_string(textbuf, MGMT_MODE_RD, "display_name", io->display_name);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "init_task_tag") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "init_task_tag", "0x%x", io->init_task_tag);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "tgt_task_tag") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "tgt_task_tag", "0x%x", io->tgt_task_tag);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "hw_tag") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "hw_tag", "0x%x", io->hw_tag);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "tag") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "tag", "0x%x", io->tag);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "transferred") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "transferred", "%zu", io->transferred);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "auto_resp") == 0) {
			ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "auto_resp", io->auto_resp);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "exp_xfer_len") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "exp_xfer_len", "%d", io->exp_xfer_len);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "xfer_req") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "xfer_req", "%d", io->xfer_req);
			retval = 0;
		}
	}

	return retval;
}

void
ocs_mgmt_io_get_all(ocs_textbuf_t *textbuf, void *object)
{
	ocs_io_t *io = (ocs_io_t *) object;

	ocs_mgmt_emit_string(textbuf, MGMT_MODE_RD, "display_name", io->display_name);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "init_task_tag", "0x%x", io->init_task_tag);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "tgt_task_tag", "0x%x", io->tgt_task_tag);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "hw_tag", "0x%x", io->hw_tag);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "tag", "0x%x", io->tag);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "transferred", "%zu", io->transferred);
	ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "auto_resp", io->auto_resp);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "exp_xfer_len", "%d", io->exp_xfer_len);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "xfer_req", "%d", io->xfer_req);

}




