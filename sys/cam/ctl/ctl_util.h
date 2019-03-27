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
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_util.h#2 $
 * $FreeBSD$
 */
/*
 * CAM Target Layer SCSI library interface
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#ifndef	_CTL_UTIL_H
#define	_CTL_UTIL_H 1

__BEGIN_DECLS

void ctl_scsi_tur(union ctl_io *io, ctl_tag_type tag_type, uint8_t control);
void ctl_scsi_inquiry(union ctl_io *io, uint8_t *data_ptr, int32_t data_len,
		      uint8_t byte2, uint8_t page_code, ctl_tag_type tag_type,
		      uint8_t control);
void ctl_scsi_request_sense(union ctl_io *io, uint8_t *data_ptr,
			    int32_t data_len, uint8_t byte2,
			    ctl_tag_type tag_type, uint8_t control);
void ctl_scsi_report_luns(union ctl_io *io, uint8_t *data_ptr,
			  uint32_t data_len, uint8_t select_report,
			  ctl_tag_type tag_type, uint8_t control);
void ctl_scsi_read_write_buffer(union ctl_io *io, uint8_t *data_ptr,
				uint32_t data_len, int read_buffer,
				uint8_t mode, uint8_t buffer_id,
				uint32_t buffer_offset, ctl_tag_type tag_type,
				uint8_t control);
void ctl_scsi_read_write(union ctl_io *io, uint8_t *data_ptr,
			 uint32_t data_len, int read_op, uint8_t byte2,
			 int minimum_cdb_size, uint64_t lba,
			 uint32_t num_blocks, ctl_tag_type tag_type,
			 uint8_t control);
void ctl_scsi_write_same(union ctl_io *io, uint8_t *data_ptr,
			 uint32_t data_len, uint8_t byte2,
			 uint64_t lba, uint32_t num_blocks,
			 ctl_tag_type tag_type, uint8_t control);
void ctl_scsi_read_capacity(union ctl_io *io, uint8_t *data_ptr,
			    uint32_t data_len, uint32_t addr, int reladr,
			    int pmi, ctl_tag_type tag_type, uint8_t control);
void ctl_scsi_read_capacity_16(union ctl_io *io, uint8_t *data_ptr,
			       uint32_t data_len, uint64_t addr, int reladr,
			       int pmi, ctl_tag_type tag_type, uint8_t control);
void ctl_scsi_mode_sense(union ctl_io *io, uint8_t *data_ptr,
			 uint32_t data_len, int dbd, int llbaa,
			 uint8_t page_code, uint8_t pc, uint8_t subpage,
			 int minimum_cdb_size, ctl_tag_type tag_type,
			 uint8_t control);
void ctl_scsi_start_stop(union ctl_io *io, int start, int load_eject,
			 int immediate, int power_conditions,
			 ctl_tag_type tag_type, uint8_t control);
void ctl_scsi_sync_cache(union ctl_io *io, int immed, int reladr,
			 int minimum_cdb_size, uint64_t starting_lba,
			 uint32_t block_count, ctl_tag_type tag_type,
			 uint8_t control);
void ctl_scsi_persistent_res_in(union ctl_io *io, uint8_t *data_ptr, 
				uint32_t data_len, int action,
				ctl_tag_type tag_type, uint8_t control);
void ctl_scsi_persistent_res_out(union ctl_io *io, uint8_t *data_ptr, 
				 uint32_t data_len, int action, int type,
				 uint64_t key, uint64_t sa_key,
				 ctl_tag_type tag_type, uint8_t control);
void ctl_scsi_maintenance_in(union ctl_io *io, uint8_t *data_ptr, 
			     uint32_t data_len, uint8_t action, 
			     ctl_tag_type tag_type, uint8_t control);
#ifndef _KERNEL
union ctl_io *ctl_scsi_alloc_io(uint32_t initid);
void ctl_scsi_free_io(union ctl_io *io);
void ctl_scsi_zero_io(union ctl_io *io);
#else
#define	ctl_scsi_zero_io(io)	ctl_zero_io(io)
#endif /* !_KERNEL */
const char *ctl_scsi_task_string(struct ctl_taskio *taskio);
void ctl_io_sbuf(union ctl_io *io, struct sbuf *sb);
void ctl_io_error_sbuf(union ctl_io *io,
		       struct scsi_inquiry_data *inq_data, struct sbuf *sb);
char *ctl_io_string(union ctl_io *io, char *str, int str_len);
char *ctl_io_error_string(union ctl_io *io,
			  struct scsi_inquiry_data *inq_data, char *str,
			  int str_len);
#ifdef _KERNEL
void ctl_io_print(union ctl_io *io);
void ctl_io_error_print(union ctl_io *io, struct scsi_inquiry_data *inq_data);
void ctl_data_print(union ctl_io *io);
#else /* _KERNEL */
void ctl_io_error_print(union ctl_io *io, struct scsi_inquiry_data *inq_data,
		   FILE *ofile);
#endif /* _KERNEL */

__END_DECLS

#endif	/* _CTL_UTIL_H */

/*
 * vim: ts=8
 */
