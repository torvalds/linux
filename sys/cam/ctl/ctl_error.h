/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Silicon Graphics International Corp.
 * Copyright (c) 2014-2015 Alexander Motin <mav@FreeBSD.org>
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
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_error.h#1 $
 * $FreeBSD$
 */
/*
 * Function definitions for various error reporting routines used both
 * within CTL and various CTL clients.
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#include <machine/stdarg.h>

#ifndef	_CTL_ERROR_H_
#define	_CTL_ERROR_H_

struct ctl_lun;

void ctl_set_sense_data_va(struct scsi_sense_data *sense_data, u_int *sense_len,
    void *lun, scsi_sense_data_type sense_format, int current_error,
    int sense_key, int asc, int ascq, va_list ap);
void ctl_set_sense_data(struct scsi_sense_data *sense_data, u_int *sense_len,
    void *lun, scsi_sense_data_type sense_format, int current_error,
    int sense_key, int asc, int ascq, ...);
void ctl_set_sense(struct ctl_scsiio *ctsio, int current_error, int sense_key,
		   int asc, int ascq, ...);
void ctl_sense_to_desc(struct scsi_sense_data_fixed *sense_src,
		      struct scsi_sense_data_desc *sense_dest);
void ctl_sense_to_fixed(struct scsi_sense_data_desc *sense_src,
			struct scsi_sense_data_fixed *sense_dest);
void ctl_set_ua(struct ctl_scsiio *ctsio, int asc, int ascq);
ctl_ua_type ctl_build_qae(struct ctl_lun *lun, uint32_t initidx, uint8_t *resp);
ctl_ua_type ctl_build_ua(struct ctl_lun *lun, uint32_t initidx,
    struct scsi_sense_data *sense, u_int *sense_len,
    scsi_sense_data_type sense_format);
void ctl_set_overlapped_cmd(struct ctl_scsiio *ctsio);
void ctl_set_overlapped_tag(struct ctl_scsiio *ctsio, uint8_t tag);
void ctl_set_invalid_field(struct ctl_scsiio *ctsio, int sks_valid, int command,
			   int field, int bit_valid, int bit);
void ctl_set_invalid_field_ciu(struct ctl_scsiio *ctsio);
void ctl_set_invalid_opcode(struct ctl_scsiio *ctsio);
void ctl_set_param_len_error(struct ctl_scsiio *ctsio);
void ctl_set_already_locked(struct ctl_scsiio *ctsio);
void ctl_set_unsupported_lun(struct ctl_scsiio *ctsio);
void ctl_set_lun_transit(struct ctl_scsiio *ctsio);
void ctl_set_lun_standby(struct ctl_scsiio *ctsio);
void ctl_set_lun_unavail(struct ctl_scsiio *ctsio);
void ctl_set_internal_failure(struct ctl_scsiio *ctsio, int sks_valid,
			      uint16_t retry_count);
void ctl_set_medium_error(struct ctl_scsiio *ctsio, int read);
void ctl_set_aborted(struct ctl_scsiio *ctsio);
void ctl_set_lba_out_of_range(struct ctl_scsiio *ctsio, uint64_t lba);
void ctl_set_lun_stopped(struct ctl_scsiio *ctsio);
void ctl_set_lun_int_reqd(struct ctl_scsiio *ctsio);
void ctl_set_lun_ejected(struct ctl_scsiio *ctsio);
void ctl_set_lun_no_media(struct ctl_scsiio *ctsio);
void ctl_set_illegal_pr_release(struct ctl_scsiio *ctsio);
void ctl_set_medium_format_corrupted(struct ctl_scsiio *ctsio);
void ctl_set_medium_magazine_inaccessible(struct ctl_scsiio *ctsio);
void ctl_set_data_phase_error(struct ctl_scsiio *ctsio);
void ctl_set_reservation_conflict(struct ctl_scsiio *ctsio);
void ctl_set_queue_full(struct ctl_scsiio *ctsio);
void ctl_set_busy(struct ctl_scsiio *ctsio);
void ctl_set_task_aborted(struct ctl_scsiio *ctsio);
void ctl_set_hw_write_protected(struct ctl_scsiio *ctsio);
void ctl_set_space_alloc_fail(struct ctl_scsiio *ctsio);
void ctl_set_success(struct ctl_scsiio *ctsio);

#endif	/* _CTL_ERROR_H_ */
