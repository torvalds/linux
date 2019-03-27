/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Silicon Graphics International Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_util.c#2 $
 */
/*
 * CAM Target Layer SCSI library
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/malloc.h>
#else /* __KERNEL__ */
#include <sys/types.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif /* __KERNEL__ */
#include <sys/sbuf.h>
#include <sys/queue.h>
#include <sys/callout.h>
#include <cam/scsi/scsi_all.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl_scsi_all.h>
#include <cam/ctl/ctl_util.h>

struct ctl_status_desc {
	ctl_io_status status;
	const char *description;
};

struct ctl_task_desc {
	ctl_task_type	task_action;
	const char	*description;
};
static struct ctl_status_desc ctl_status_table[] = {
	{CTL_STATUS_NONE, "No Status"},
	{CTL_SUCCESS, "Command Completed Successfully"},
	{CTL_CMD_TIMEOUT, "Command Timed Out"},
	{CTL_SEL_TIMEOUT, "Selection Timeout"},
	{CTL_ERROR, "Command Failed"},
	{CTL_SCSI_ERROR, "SCSI Error"},
	{CTL_CMD_ABORTED, "Command Aborted"},
};

static struct ctl_task_desc ctl_task_table[] = {
	{CTL_TASK_ABORT_TASK, "Abort Task"},
	{CTL_TASK_ABORT_TASK_SET, "Abort Task Set"},
	{CTL_TASK_CLEAR_ACA, "Clear ACA"},
	{CTL_TASK_CLEAR_TASK_SET, "Clear Task Set"},
	{CTL_TASK_I_T_NEXUS_RESET, "I_T Nexus Reset"},
	{CTL_TASK_LUN_RESET, "LUN Reset"},
	{CTL_TASK_TARGET_RESET, "Target Reset"},
	{CTL_TASK_BUS_RESET, "Bus Reset"},
	{CTL_TASK_PORT_LOGIN, "Port Login"},
	{CTL_TASK_PORT_LOGOUT, "Port Logout"},
	{CTL_TASK_QUERY_TASK, "Query Task"},
	{CTL_TASK_QUERY_TASK_SET, "Query Task Set"},
	{CTL_TASK_QUERY_ASYNC_EVENT, "Query Async Event"}
};

void
ctl_scsi_tur(union ctl_io *io, ctl_tag_type tag_type, uint8_t control)
{
	struct ctl_scsiio *ctsio;
	struct scsi_test_unit_ready *cdb;

	ctl_scsi_zero_io(io);

	io->io_hdr.io_type = CTL_IO_SCSI;
	ctsio = &io->scsiio;
	cdb = (struct scsi_test_unit_ready *)ctsio->cdb;

	cdb->opcode = TEST_UNIT_READY;
	cdb->control = control;
	io->io_hdr.flags = CTL_FLAG_DATA_NONE;
	ctsio->tag_type = tag_type;
	ctsio->cdb_len = sizeof(*cdb);
	ctsio->ext_data_len = 0;
	ctsio->ext_data_ptr = NULL;
	ctsio->ext_sg_entries = 0;
	ctsio->ext_data_filled = 0;
	ctsio->sense_len = SSD_FULL_SIZE;
}

void
ctl_scsi_inquiry(union ctl_io *io, uint8_t *data_ptr, int32_t data_len,
		 uint8_t byte2, uint8_t page_code, ctl_tag_type tag_type,
		 uint8_t control)
{
	struct ctl_scsiio *ctsio;
	struct scsi_inquiry *cdb;

	ctl_scsi_zero_io(io);

	io->io_hdr.io_type = CTL_IO_SCSI;
	ctsio = &io->scsiio;
	cdb = (struct scsi_inquiry *)ctsio->cdb;

	cdb->opcode = INQUIRY;
	cdb->byte2 = byte2;
	cdb->page_code = page_code;
	cdb->control = control;
	scsi_ulto2b(data_len, cdb->length);
	io->io_hdr.io_type = CTL_IO_SCSI;
	io->io_hdr.flags = CTL_FLAG_DATA_IN;
	ctsio->tag_type = tag_type;
	ctsio->cdb_len = sizeof(*cdb);
	ctsio->ext_data_len = data_len;
	ctsio->ext_data_ptr = data_ptr;
	ctsio->ext_sg_entries = 0;
	ctsio->ext_data_filled = 0;
	ctsio->sense_len = SSD_FULL_SIZE;
}

void
ctl_scsi_request_sense(union ctl_io *io, uint8_t *data_ptr,
		       int32_t data_len, uint8_t byte2, ctl_tag_type tag_type,
		       uint8_t control)
{
	struct ctl_scsiio *ctsio;
	struct scsi_request_sense *cdb;

	ctl_scsi_zero_io(io);

	io->io_hdr.io_type = CTL_IO_SCSI;
	ctsio = &io->scsiio;
	cdb = (struct scsi_request_sense *)ctsio->cdb;

	cdb->opcode = REQUEST_SENSE;
	cdb->byte2 = byte2;
	cdb->control = control;
	cdb->length = data_len;
	io->io_hdr.io_type = CTL_IO_SCSI;
	io->io_hdr.flags = CTL_FLAG_DATA_IN;
	ctsio->tag_type = tag_type;
	ctsio->cdb_len = sizeof(*cdb);
	ctsio->ext_data_ptr = data_ptr;
	ctsio->ext_data_len = data_len;
	ctsio->ext_sg_entries = 0;
	ctsio->ext_data_filled = 0;
	ctsio->sense_len = SSD_FULL_SIZE;
}

void
ctl_scsi_report_luns(union ctl_io *io, uint8_t *data_ptr, uint32_t data_len,
		     uint8_t select_report, ctl_tag_type tag_type,
		     uint8_t control)
{
	struct ctl_scsiio *ctsio;
	struct scsi_report_luns *cdb;

	ctl_scsi_zero_io(io);

	io->io_hdr.io_type = CTL_IO_SCSI;
	ctsio = &io->scsiio;
	cdb = (struct scsi_report_luns *)ctsio->cdb;

	cdb->opcode = REPORT_LUNS;
	cdb->select_report = select_report;
	scsi_ulto4b(data_len, cdb->length);
	cdb->control = control;
	io->io_hdr.io_type = CTL_IO_SCSI;
	io->io_hdr.flags = CTL_FLAG_DATA_IN;
	ctsio->tag_type = tag_type;
	ctsio->cdb_len = sizeof(*cdb);
	ctsio->ext_data_ptr = data_ptr;
	ctsio->ext_data_len = data_len;
	ctsio->ext_sg_entries = 0;
	ctsio->ext_data_filled = 0;
	ctsio->sense_len = SSD_FULL_SIZE;
}

void
ctl_scsi_read_write_buffer(union ctl_io *io, uint8_t *data_ptr,
			   uint32_t data_len, int read_buffer, uint8_t mode,
			   uint8_t buffer_id, uint32_t buffer_offset,
			   ctl_tag_type tag_type, uint8_t control)
{
	struct ctl_scsiio *ctsio;
	struct scsi_write_buffer *cdb;

	ctl_scsi_zero_io(io);

	io->io_hdr.io_type = CTL_IO_SCSI;
	ctsio = &io->scsiio;
	cdb = (struct scsi_write_buffer *)ctsio->cdb;

	if (read_buffer != 0)
		cdb->opcode = READ_BUFFER;
	else
		cdb->opcode = WRITE_BUFFER;

	cdb->byte2 = mode & RWB_MODE;
	cdb->buffer_id = buffer_id;
	scsi_ulto3b(buffer_offset, cdb->offset);
	scsi_ulto3b(data_len, cdb->length);
	cdb->control = control;
	io->io_hdr.io_type = CTL_IO_SCSI;
	if (read_buffer != 0)
		io->io_hdr.flags = CTL_FLAG_DATA_IN;
	else
		io->io_hdr.flags = CTL_FLAG_DATA_OUT;
	ctsio->tag_type = tag_type;
	ctsio->cdb_len = sizeof(*cdb);
	ctsio->ext_data_ptr = data_ptr;
	ctsio->ext_data_len = data_len;
	ctsio->ext_sg_entries = 0;
	ctsio->ext_data_filled = 0;
	ctsio->sense_len = SSD_FULL_SIZE;
}

void
ctl_scsi_read_write(union ctl_io *io, uint8_t *data_ptr, uint32_t data_len,
		    int read_op, uint8_t byte2, int minimum_cdb_size,
		    uint64_t lba, uint32_t num_blocks, ctl_tag_type tag_type,
		    uint8_t control)
{
	struct ctl_scsiio *ctsio;

	ctl_scsi_zero_io(io);

	io->io_hdr.io_type = CTL_IO_SCSI;
	ctsio = &io->scsiio;

	/*
	 * Pick out the smallest CDB that will hold the user's request.
	 * minimum_cdb_size allows cranking the CDB size up, even for
	 * requests that would not normally need a large CDB.  This can be
	 * useful for testing (e.g. to make sure READ_16 support works without
	 * having an array larger than 2TB) and for compatibility -- e.g.
	 * if your device doesn't support READ_6.  (ATAPI drives don't.)
	 */
	if ((minimum_cdb_size < 10)
	 && ((lba & 0x1fffff) == lba)
	 && ((num_blocks & 0xff) == num_blocks)
	 && (byte2 == 0)) {
		struct scsi_rw_6 *cdb;

		/*
		 * Note that according to SBC-2, the target should return 256
		 * blocks if the transfer length in a READ(6) or WRITE(6) CDB
		 * is set to 0.  Since it's possible that some targets
		 * won't do the right thing, we only send a READ(6) or
		 * WRITE(6) for transfer sizes up to and including 255 blocks.
		 */
		cdb = (struct scsi_rw_6 *)ctsio->cdb;

		cdb->opcode = (read_op) ? READ_6 : WRITE_6;
		scsi_ulto3b(lba, cdb->addr);
		cdb->length = num_blocks & 0xff;
		cdb->control = control;

		ctsio->cdb_len = sizeof(*cdb);

	} else if ((minimum_cdb_size < 12)
		&& ((num_blocks & 0xffff) == num_blocks)
		&& ((lba & 0xffffffff) == lba)) {
		struct scsi_rw_10 *cdb;

		cdb = (struct scsi_rw_10 *)ctsio->cdb;

		cdb->opcode = (read_op) ? READ_10 : WRITE_10;
		cdb->byte2 = byte2;
		scsi_ulto4b(lba, cdb->addr);
		cdb->reserved = 0;
		scsi_ulto2b(num_blocks, cdb->length);
		cdb->control = control;

		ctsio->cdb_len = sizeof(*cdb);
	} else if ((minimum_cdb_size < 16)
		&& ((num_blocks & 0xffffffff) == num_blocks)
		&& ((lba & 0xffffffff) == lba)) {
		struct scsi_rw_12 *cdb;

		cdb = (struct scsi_rw_12 *)ctsio->cdb;

		cdb->opcode = (read_op) ? READ_12 : WRITE_12;
		cdb->byte2 = byte2;
		scsi_ulto4b(lba, cdb->addr);
		scsi_ulto4b(num_blocks, cdb->length);
		cdb->reserved = 0;
		cdb->control = control;

		ctsio->cdb_len = sizeof(*cdb);
	} else {
		struct scsi_rw_16 *cdb;

		cdb = (struct scsi_rw_16 *)ctsio->cdb;

		cdb->opcode = (read_op) ? READ_16 : WRITE_16;
		cdb->byte2 = byte2;
		scsi_u64to8b(lba, cdb->addr);
		scsi_ulto4b(num_blocks, cdb->length);
		cdb->reserved = 0;
		cdb->control = control;

		ctsio->cdb_len = sizeof(*cdb);
	}

	io->io_hdr.io_type = CTL_IO_SCSI;
	if (read_op != 0)
		io->io_hdr.flags = CTL_FLAG_DATA_IN;
	else
		io->io_hdr.flags = CTL_FLAG_DATA_OUT;
	ctsio->tag_type = tag_type;
	ctsio->ext_data_ptr = data_ptr;
	ctsio->ext_data_len = data_len;
	ctsio->ext_sg_entries = 0;
	ctsio->ext_data_filled = 0;
	ctsio->sense_len = SSD_FULL_SIZE;
}

void
ctl_scsi_write_same(union ctl_io *io, uint8_t *data_ptr, uint32_t data_len,
		    uint8_t byte2, uint64_t lba, uint32_t num_blocks,
		    ctl_tag_type tag_type, uint8_t control)
{
	struct ctl_scsiio *ctsio;
	struct scsi_write_same_16 *cdb;

	ctl_scsi_zero_io(io);

	io->io_hdr.io_type = CTL_IO_SCSI;
	ctsio = &io->scsiio;
	ctsio->cdb_len = sizeof(*cdb);
	cdb = (struct scsi_write_same_16 *)ctsio->cdb;
	cdb->opcode = WRITE_SAME_16;
	cdb->byte2 = byte2;
	scsi_u64to8b(lba, cdb->addr);
	scsi_ulto4b(num_blocks, cdb->length);
	cdb->group = 0;
	cdb->control = control;

	io->io_hdr.io_type = CTL_IO_SCSI;
	io->io_hdr.flags = CTL_FLAG_DATA_OUT;
	ctsio->tag_type = tag_type;
	ctsio->ext_data_ptr = data_ptr;
	ctsio->ext_data_len = data_len;
	ctsio->ext_sg_entries = 0;
	ctsio->ext_data_filled = 0;
	ctsio->sense_len = SSD_FULL_SIZE;
}

void
ctl_scsi_read_capacity(union ctl_io *io, uint8_t *data_ptr, uint32_t data_len,
		       uint32_t addr, int reladr, int pmi,
		       ctl_tag_type tag_type, uint8_t control)
{
	struct scsi_read_capacity *cdb;

	ctl_scsi_zero_io(io);

	io->io_hdr.io_type = CTL_IO_SCSI;
	cdb = (struct scsi_read_capacity *)io->scsiio.cdb;

	cdb->opcode = READ_CAPACITY;
	if (reladr)
		cdb->byte2 = SRC_RELADR;
	if (pmi)
		cdb->pmi = SRC_PMI;
	scsi_ulto4b(addr, cdb->addr);
	cdb->control = control;
	io->io_hdr.io_type = CTL_IO_SCSI;
	io->io_hdr.flags = CTL_FLAG_DATA_IN;
	io->scsiio.tag_type = tag_type;
	io->scsiio.ext_data_ptr = data_ptr;
	io->scsiio.ext_data_len = data_len;
	io->scsiio.ext_sg_entries = 0;
	io->scsiio.ext_data_filled = 0;
	io->scsiio.sense_len = SSD_FULL_SIZE;
}

void
ctl_scsi_read_capacity_16(union ctl_io *io, uint8_t *data_ptr,
			  uint32_t data_len, uint64_t addr, int reladr,
			  int pmi, ctl_tag_type tag_type, uint8_t control)
{
	struct scsi_read_capacity_16 *cdb;

	ctl_scsi_zero_io(io);

	io->io_hdr.io_type = CTL_IO_SCSI;
	cdb = (struct scsi_read_capacity_16 *)io->scsiio.cdb;

	cdb->opcode = SERVICE_ACTION_IN;
	cdb->service_action = SRC16_SERVICE_ACTION;
	if (reladr)
		cdb->reladr |= SRC16_RELADR;
	if (pmi)
		cdb->reladr |= SRC16_PMI;
	scsi_u64to8b(addr, cdb->addr);
	scsi_ulto4b(data_len, cdb->alloc_len);
	cdb->control = control;

	io->io_hdr.io_type = CTL_IO_SCSI;
	io->io_hdr.flags = CTL_FLAG_DATA_IN;
	io->scsiio.tag_type = tag_type;
	io->scsiio.ext_data_ptr = data_ptr;
	io->scsiio.ext_data_len = data_len;
	io->scsiio.ext_sg_entries = 0;
	io->scsiio.ext_data_filled = 0;
	io->scsiio.sense_len = SSD_FULL_SIZE;
}

void
ctl_scsi_mode_sense(union ctl_io *io, uint8_t *data_ptr, uint32_t data_len, 
		    int dbd, int llbaa, uint8_t page_code, uint8_t pc,
		    uint8_t subpage, int minimum_cdb_size,
		    ctl_tag_type tag_type, uint8_t control)
{
	ctl_scsi_zero_io(io);

	if ((minimum_cdb_size < 10)
	 && (llbaa == 0)
	 && (data_len < 256)) {
		struct scsi_mode_sense_6 *cdb;

		cdb = (struct scsi_mode_sense_6 *)io->scsiio.cdb;

		cdb->opcode = MODE_SENSE_6;
		if (dbd)
			cdb->byte2 |= SMS_DBD;
		cdb->page = page_code | pc;
		cdb->subpage = subpage;
		cdb->length = data_len;
		cdb->control = control;
	} else {
		struct scsi_mode_sense_10 *cdb;

		cdb = (struct scsi_mode_sense_10 *)io->scsiio.cdb;

		cdb->opcode = MODE_SENSE_10;
		if (dbd)
			cdb->byte2 |= SMS_DBD;
		if (llbaa)
			cdb->byte2 |= SMS10_LLBAA;
		cdb->page = page_code | pc;
		cdb->subpage = subpage;
		scsi_ulto2b(data_len, cdb->length);
		cdb->control = control;
	}

	io->io_hdr.io_type = CTL_IO_SCSI;
	io->io_hdr.flags = CTL_FLAG_DATA_IN;
	io->scsiio.tag_type = tag_type;
	io->scsiio.ext_data_ptr = data_ptr;
	io->scsiio.ext_data_len = data_len;
	io->scsiio.ext_sg_entries = 0;
	io->scsiio.ext_data_filled = 0;
	io->scsiio.sense_len = SSD_FULL_SIZE;
}

void
ctl_scsi_start_stop(union ctl_io *io, int start, int load_eject, int immediate,
    int power_conditions, ctl_tag_type tag_type, uint8_t control)
{
	struct scsi_start_stop_unit *cdb;

	cdb = (struct scsi_start_stop_unit *)io->scsiio.cdb;

	ctl_scsi_zero_io(io);

	cdb->opcode = START_STOP_UNIT;
	if (immediate)
		cdb->byte2 |= SSS_IMMED;
	cdb->how = power_conditions;
	if (load_eject)
		cdb->how |= SSS_LOEJ;
	if (start)
		cdb->how |= SSS_START;
	cdb->control = control;
	io->io_hdr.io_type = CTL_IO_SCSI;
	io->io_hdr.flags = CTL_FLAG_DATA_NONE;
	io->scsiio.tag_type = tag_type;
	io->scsiio.ext_data_ptr = NULL;
	io->scsiio.ext_data_len = 0;
	io->scsiio.ext_sg_entries = 0;
	io->scsiio.ext_data_filled = 0;
	io->scsiio.sense_len = SSD_FULL_SIZE;
}

void
ctl_scsi_sync_cache(union ctl_io *io, int immed, int reladr,
		    int minimum_cdb_size, uint64_t starting_lba,
		    uint32_t block_count, ctl_tag_type tag_type,
		    uint8_t control)
{
	ctl_scsi_zero_io(io);

	if ((minimum_cdb_size < 16)
	 && ((block_count & 0xffff) == block_count)
	 && ((starting_lba & 0xffffffff) == starting_lba)) {
		struct scsi_sync_cache *cdb;

		cdb = (struct scsi_sync_cache *)io->scsiio.cdb;

		cdb->opcode = SYNCHRONIZE_CACHE;
		if (reladr)
			cdb->byte2 |= SSC_RELADR;

		if (immed)
			cdb->byte2 |= SSC_IMMED;

		scsi_ulto4b(starting_lba, cdb->begin_lba);
		scsi_ulto2b(block_count, cdb->lb_count);
		cdb->control = control;
	} else {
		struct scsi_sync_cache_16 *cdb;

		cdb = (struct scsi_sync_cache_16 *)io->scsiio.cdb;

		cdb->opcode = SYNCHRONIZE_CACHE_16;
		if (reladr)
			cdb->byte2 |= SSC_RELADR;

		if (immed)
			cdb->byte2 |= SSC_IMMED;

		scsi_u64to8b(starting_lba, cdb->begin_lba);
		scsi_ulto4b(block_count, cdb->lb_count);
		cdb->control = control;
	}
	io->io_hdr.io_type = CTL_IO_SCSI;
	io->io_hdr.flags = CTL_FLAG_DATA_NONE;
	io->scsiio.tag_type = tag_type;
	io->scsiio.ext_data_ptr = NULL;
	io->scsiio.ext_data_len = 0;
	io->scsiio.ext_sg_entries = 0;
	io->scsiio.ext_data_filled = 0;
	io->scsiio.sense_len = SSD_FULL_SIZE;
}

void
ctl_scsi_persistent_res_in(union ctl_io *io, uint8_t *data_ptr,
			   uint32_t data_len, int action,
			   ctl_tag_type tag_type, uint8_t control)
{

	struct scsi_per_res_in *cdb;

	ctl_scsi_zero_io(io);

	cdb = (struct scsi_per_res_in *)io->scsiio.cdb;
	cdb->opcode = PERSISTENT_RES_IN;
	cdb->action = action;
	scsi_ulto2b(data_len, cdb->length);
	cdb->control = control;

	io->io_hdr.io_type = CTL_IO_SCSI;
	io->io_hdr.flags = CTL_FLAG_DATA_IN;
	io->scsiio.tag_type = tag_type;
	io->scsiio.ext_data_ptr = data_ptr;
	io->scsiio.ext_data_len = data_len;
	io->scsiio.ext_sg_entries = 0;
	io->scsiio.ext_data_filled = 0;
	io->scsiio.sense_len = SSD_FULL_SIZE;
}

void
ctl_scsi_persistent_res_out(union ctl_io *io, uint8_t *data_ptr,
			    uint32_t data_len, int action, int type,
			    uint64_t key, uint64_t sa_key,
			    ctl_tag_type tag_type, uint8_t control)
{

	struct scsi_per_res_out *cdb;
	struct scsi_per_res_out_parms *params;

	ctl_scsi_zero_io(io);

	cdb = (struct scsi_per_res_out *)io->scsiio.cdb;
	params = (struct scsi_per_res_out_parms *)data_ptr;

	cdb->opcode = PERSISTENT_RES_OUT;
	if (action == 5)
	    cdb->action = 6;
	else
	    cdb->action = action;
	switch(type)
	{
	    case 0:
		    cdb->scope_type = 1;
			break;
	    case 1:
		    cdb->scope_type = 3;
			break;
	    case 2:
		    cdb->scope_type = 5;
			break;
	    case 3:
		    cdb->scope_type = 6;
			break;
	    case 4:
		    cdb->scope_type = 7;
			break;
	    case 5:
		    cdb->scope_type = 8;
			break;
	}
	scsi_ulto4b(data_len, cdb->length);
	cdb->control = control;

	scsi_u64to8b(key, params->res_key.key);
	scsi_u64to8b(sa_key, params->serv_act_res_key);

	io->io_hdr.io_type = CTL_IO_SCSI;
	io->io_hdr.flags = CTL_FLAG_DATA_OUT;
	io->scsiio.tag_type = tag_type;
	io->scsiio.ext_data_ptr = data_ptr;
	io->scsiio.ext_data_len = data_len;
	io->scsiio.ext_sg_entries = 0;
	io->scsiio.ext_data_filled = 0;
	io->scsiio.sense_len = SSD_FULL_SIZE;

}

void
ctl_scsi_maintenance_in(union ctl_io *io, uint8_t *data_ptr, uint32_t data_len, 
			uint8_t action, ctl_tag_type tag_type, uint8_t control)
{
	struct scsi_maintenance_in *cdb;

	ctl_scsi_zero_io(io);

	cdb = (struct scsi_maintenance_in *)io->scsiio.cdb;
	cdb->opcode = MAINTENANCE_IN;
	cdb->byte2 = action;
	scsi_ulto4b(data_len, cdb->length);
	cdb->control = control;

	io->io_hdr.io_type = CTL_IO_SCSI;
	io->io_hdr.flags = CTL_FLAG_DATA_IN;
	io->scsiio.tag_type = tag_type;
	io->scsiio.ext_data_ptr = data_ptr;
	io->scsiio.ext_data_len = data_len;
	io->scsiio.ext_sg_entries = 0;
	io->scsiio.ext_data_filled = 0;
	io->scsiio.sense_len = SSD_FULL_SIZE;
}

#ifndef _KERNEL
union ctl_io *
ctl_scsi_alloc_io(uint32_t initid)
{
	union ctl_io *io;

	io = (union ctl_io *)malloc(sizeof(*io));
	if (io == NULL)
		goto bailout;

	io->io_hdr.nexus.initid = initid;

bailout:
	return (io);
}

void
ctl_scsi_free_io(union ctl_io *io)
{
	free(io);
}

void
ctl_scsi_zero_io(union ctl_io *io)
{
	void *pool_ref;

	if (io == NULL)
		return;

	pool_ref = io->io_hdr.pool;
	memset(io, 0, sizeof(*io));
	io->io_hdr.pool = pool_ref;
}
#endif /* !_KERNEL */

const char *
ctl_scsi_task_string(struct ctl_taskio *taskio)
{
	unsigned int i;

	for (i = 0; i < (sizeof(ctl_task_table)/sizeof(ctl_task_table[0]));
	     i++) {
		if (taskio->task_action == ctl_task_table[i].task_action) {
			return (ctl_task_table[i].description);
		}
	}

	return (NULL);
}

void
ctl_io_sbuf(union ctl_io *io, struct sbuf *sb)
{
	const char *task_desc;
	char path_str[64];

	ctl_scsi_path_string(io, path_str, sizeof(path_str));

	switch (io->io_hdr.io_type) {
	case CTL_IO_SCSI:
		sbuf_cat(sb, path_str);
		ctl_scsi_command_string(&io->scsiio, NULL, sb);
		sbuf_printf(sb, " Tag: %#x/%d\n",
			    io->scsiio.tag_num, io->scsiio.tag_type);
		break;
	case CTL_IO_TASK:
		sbuf_cat(sb, path_str);
		task_desc = ctl_scsi_task_string(&io->taskio);
		if (task_desc == NULL)
			sbuf_printf(sb, "Unknown Task Action %d (%#x)",
			    io->taskio.task_action, io->taskio.task_action);
		else
			sbuf_printf(sb, "Task Action: %s", task_desc);
		switch (io->taskio.task_action) {
		case CTL_TASK_ABORT_TASK:
			sbuf_printf(sb, " Tag: %#x/%d\n",
			    io->taskio.tag_num, io->taskio.tag_type);
			break;
		default:
			sbuf_printf(sb, "\n");
			break;
		}
		break;
	default:
		break;
	}
}

void
ctl_io_error_sbuf(union ctl_io *io, struct scsi_inquiry_data *inq_data,
		  struct sbuf *sb)
{
	struct ctl_status_desc *status_desc;
	char path_str[64];
	unsigned int i;

	ctl_io_sbuf(io, sb);

	status_desc = NULL;
	for (i = 0; i < (sizeof(ctl_status_table)/sizeof(ctl_status_table[0]));
	     i++) {
		if ((io->io_hdr.status & CTL_STATUS_MASK) ==
		     ctl_status_table[i].status) {
			status_desc = &ctl_status_table[i];
			break;
		}
	}

	ctl_scsi_path_string(io, path_str, sizeof(path_str));

	sbuf_cat(sb, path_str);
	if (status_desc == NULL)
		sbuf_printf(sb, "CTL Status: Unknown status %#x\n",
			    io->io_hdr.status);
	else
		sbuf_printf(sb, "CTL Status: %s\n", status_desc->description);

	if ((io->io_hdr.io_type == CTL_IO_SCSI)
	 && ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SCSI_ERROR)) {
		sbuf_cat(sb, path_str);
		sbuf_printf(sb, "SCSI Status: %s\n",
			    ctl_scsi_status_string(&io->scsiio));

		if (io->scsiio.scsi_status == SCSI_STATUS_CHECK_COND)
			ctl_scsi_sense_sbuf(&io->scsiio, inq_data,
					    sb, SSS_FLAG_NONE);
	}
}

char *
ctl_io_string(union ctl_io *io, char *str, int str_len)
{
	struct sbuf sb;

	sbuf_new(&sb, str, str_len, SBUF_FIXEDLEN);
	ctl_io_sbuf(io, &sb);
	sbuf_finish(&sb);
	return (sbuf_data(&sb));
}

char *
ctl_io_error_string(union ctl_io *io, struct scsi_inquiry_data *inq_data,
		    char *str, int str_len)
{
	struct sbuf sb;

	sbuf_new(&sb, str, str_len, SBUF_FIXEDLEN);
	ctl_io_error_sbuf(io, inq_data, &sb);
	sbuf_finish(&sb);
	return (sbuf_data(&sb));
}

#ifdef _KERNEL

void
ctl_io_print(union ctl_io *io)
{
	char str[512];

	printf("%s", ctl_io_string(io, str, sizeof(str)));
}

void
ctl_io_error_print(union ctl_io *io, struct scsi_inquiry_data *inq_data)
{
	char str[512];

	printf("%s", ctl_io_error_string(io, inq_data, str, sizeof(str)));

}

void
ctl_data_print(union ctl_io *io)
{
	char str[128];
	char path_str[64];
	struct sbuf sb;
	int i, j, len;

	if (io->io_hdr.io_type != CTL_IO_SCSI)
		return;
	if (io->io_hdr.flags & CTL_FLAG_BUS_ADDR)
		return;
	if (io->scsiio.ext_sg_entries > 0)	/* XXX: Implement */
		return;
	ctl_scsi_path_string(io, path_str, sizeof(path_str));
	len = min(io->scsiio.kern_data_len, 4096);
	for (i = 0; i < len; ) {
		sbuf_new(&sb, str, sizeof(str), SBUF_FIXEDLEN);
		sbuf_cat(&sb, path_str);
		sbuf_printf(&sb, " %#6x:%04x:", io->scsiio.tag_num, i);
		for (j = 0; j < 16 && i < len; i++, j++) {
			if (j == 8)
				sbuf_cat(&sb, " ");
			sbuf_printf(&sb, " %02x", io->scsiio.kern_data_ptr[i]);
		}
		sbuf_cat(&sb, "\n");
		sbuf_finish(&sb);
		printf("%s", sbuf_data(&sb));
	}
}

#else /* _KERNEL */

void
ctl_io_error_print(union ctl_io *io, struct scsi_inquiry_data *inq_data,
		   FILE *ofile)
{
	char str[512];

	fprintf(ofile, "%s", ctl_io_error_string(io, inq_data, str,
		sizeof(str)));
}

#endif /* _KERNEL */

/*
 * vim: ts=8
 */
