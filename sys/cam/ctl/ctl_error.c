/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2009 Silicon Graphics International Corp.
 * Copyright (c) 2011 Spectra Logic Corporation
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
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_error.c#2 $
 */
/*
 * CAM Target Layer error reporting routines.
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/stddef.h>
#include <sys/ctype.h>
#include <sys/sysctl.h>
#include <machine/stdarg.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_frontend.h>
#include <cam/ctl/ctl_backend.h>
#include <cam/ctl/ctl_ioctl.h>
#include <cam/ctl/ctl_error.h>
#include <cam/ctl/ctl_ha.h>
#include <cam/ctl/ctl_private.h>

void
ctl_set_sense_data_va(struct scsi_sense_data *sense_data, u_int *sense_len,
    void *lunptr, scsi_sense_data_type sense_format, int current_error,
    int sense_key, int asc, int ascq, va_list ap)
{
	struct ctl_lun *lun;

	lun = (struct ctl_lun *)lunptr;

	/*
	 * Determine whether to return fixed or descriptor format sense
	 * data.
	 */
	if (sense_format == SSD_TYPE_NONE) {
		/*
		 * If the format isn't specified, we only return descriptor
		 * sense if the LUN exists and descriptor sense is turned
		 * on for that LUN.
		 */
		if ((lun != NULL) && (lun->MODE_CTRL.rlec & SCP_DSENSE))
			sense_format = SSD_TYPE_DESC;
		else
			sense_format = SSD_TYPE_FIXED;
	}

	/*
	 * Determine maximum sense data length to return.
	 */
	if (*sense_len == 0) {
		if ((lun != NULL) && (lun->MODE_CTRLE.max_sense != 0))
			*sense_len = lun->MODE_CTRLE.max_sense;
		else
			*sense_len = SSD_FULL_SIZE;
	}

	scsi_set_sense_data_va(sense_data, sense_len, sense_format,
	    current_error, sense_key, asc, ascq, ap);
}

void
ctl_set_sense_data(struct scsi_sense_data *sense_data, u_int *sense_len,
    void *lunptr, scsi_sense_data_type sense_format, int current_error,
    int sense_key, int asc, int ascq, ...)
{
	va_list ap;

	va_start(ap, ascq);
	ctl_set_sense_data_va(sense_data, sense_len, lunptr, sense_format,
	    current_error, sense_key, asc, ascq, ap);
	va_end(ap);
}

void
ctl_set_sense(struct ctl_scsiio *ctsio, int current_error, int sense_key,
	      int asc, int ascq, ...)
{
	va_list ap;
	struct ctl_lun *lun;
	u_int sense_len;

	/*
	 * The LUN can't go away until all of the commands have been
	 * completed.  Therefore we can safely access the LUN structure and
	 * flags without the lock.
	 */
	lun = CTL_LUN(ctsio);

	va_start(ap, ascq);
	sense_len = 0;
	ctl_set_sense_data_va(&ctsio->sense_data, &sense_len,
			      lun,
			      SSD_TYPE_NONE,
			      current_error,
			      sense_key,
			      asc,
			      ascq,
			      ap);
	va_end(ap);

	ctsio->scsi_status = SCSI_STATUS_CHECK_COND;
	ctsio->sense_len = sense_len;
	ctsio->io_hdr.status = CTL_SCSI_ERROR | CTL_AUTOSENSE;
}

/*
 * Transform fixed sense data into descriptor sense data.
 * 
 * For simplicity's sake, we assume that both sense structures are
 * SSD_FULL_SIZE.  Otherwise, the logic gets more complicated.
 */
void
ctl_sense_to_desc(struct scsi_sense_data_fixed *sense_src,
		  struct scsi_sense_data_desc *sense_dest)
{
	struct scsi_sense_stream stream_sense;
	int current_error;
	u_int sense_len;
	uint8_t stream_bits;

	bzero(sense_dest, sizeof(*sense_dest));

	if ((sense_src->error_code & SSD_ERRCODE) == SSD_DEFERRED_ERROR)
		current_error = 0;
	else
		current_error = 1;

	bzero(&stream_sense, sizeof(stream_sense));

	/*
	 * Check to see whether any of the tape-specific bits are set.  If
	 * so, we'll need a stream sense descriptor.
	 */
	if (sense_src->flags & (SSD_ILI|SSD_EOM|SSD_FILEMARK))
		stream_bits = sense_src->flags & ~SSD_KEY;
	else
		stream_bits = 0;

	/*
	 * Utilize our sense setting routine to do the transform.  If a
	 * value is set in the fixed sense data, set it in the descriptor
	 * data.  Otherwise, skip it.
	 */
	sense_len = SSD_FULL_SIZE;
	ctl_set_sense_data((struct scsi_sense_data *)sense_dest, &sense_len,
			   /*lun*/ NULL,
			   /*sense_format*/ SSD_TYPE_DESC,
			   current_error,
			   /*sense_key*/ sense_src->flags & SSD_KEY,
			   /*asc*/ sense_src->add_sense_code,
			   /*ascq*/ sense_src->add_sense_code_qual,

			   /* Information Bytes */
			   (sense_src->error_code & SSD_ERRCODE_VALID) ?
			   SSD_ELEM_INFO : SSD_ELEM_SKIP,
			   sizeof(sense_src->info),
			   sense_src->info,

			   /* Command specific bytes */
			   (scsi_4btoul(sense_src->cmd_spec_info) != 0) ?
			   SSD_ELEM_COMMAND : SSD_ELEM_SKIP,
			   sizeof(sense_src->cmd_spec_info),
			   sense_src->cmd_spec_info,

			   /* FRU */
			   (sense_src->fru != 0) ?
			   SSD_ELEM_FRU : SSD_ELEM_SKIP,
			   sizeof(sense_src->fru),
			   &sense_src->fru,

			   /* Sense Key Specific */
			   (sense_src->sense_key_spec[0] & SSD_SCS_VALID) ?
			   SSD_ELEM_SKS : SSD_ELEM_SKIP,
			   sizeof(sense_src->sense_key_spec),
			   sense_src->sense_key_spec,

			   /* Tape bits */
			   (stream_bits != 0) ?
			   SSD_ELEM_STREAM : SSD_ELEM_SKIP,
			   sizeof(stream_bits),
			   &stream_bits,

			   SSD_ELEM_NONE);
}

/*
 * Transform descriptor format sense data into fixed sense data.
 *
 * Some data may be lost in translation, because there are descriptors
 * thant can't be represented as fixed sense data.
 *
 * For simplicity's sake, we assume that both sense structures are
 * SSD_FULL_SIZE.  Otherwise, the logic gets more complicated.
 */
void
ctl_sense_to_fixed(struct scsi_sense_data_desc *sense_src,
		   struct scsi_sense_data_fixed *sense_dest)
{
	int current_error;
	uint8_t *info_ptr = NULL, *cmd_ptr = NULL, *fru_ptr = NULL;
	uint8_t *sks_ptr = NULL, *stream_ptr = NULL;
	int info_size = 0, cmd_size = 0, fru_size = 0;
	int sks_size = 0, stream_size = 0;
	int pos;
	u_int sense_len;

	if ((sense_src->error_code & SSD_ERRCODE) == SSD_DESC_CURRENT_ERROR)
		current_error = 1;
	else
		current_error = 0;

	for (pos = 0; pos < (int)(sense_src->extra_len - 1);) {
		struct scsi_sense_desc_header *header;

		header = (struct scsi_sense_desc_header *)
		    &sense_src->sense_desc[pos];

		/*
		 * See if this record goes past the end of the sense data.
		 * It shouldn't, but check just in case.
		 */
		if ((pos + header->length + sizeof(*header)) >
		     sense_src->extra_len)
			break;

		switch (sense_src->sense_desc[pos]) {
		case SSD_DESC_INFO: {
			struct scsi_sense_info *info;

			info = (struct scsi_sense_info *)header;

			info_ptr = info->info;
			info_size = sizeof(info->info);

			pos += info->length +
			    sizeof(struct scsi_sense_desc_header);
			break;
		}
		case SSD_DESC_COMMAND: {
			struct scsi_sense_command *cmd;

			cmd = (struct scsi_sense_command *)header;
			cmd_ptr = cmd->command_info;
			cmd_size = sizeof(cmd->command_info);

			pos += cmd->length + 
			    sizeof(struct scsi_sense_desc_header);
			break;
		}
		case SSD_DESC_FRU: {
			struct scsi_sense_fru *fru;

			fru = (struct scsi_sense_fru *)header;
			fru_ptr = &fru->fru;
			fru_size = sizeof(fru->fru);
			pos += fru->length +
			    sizeof(struct scsi_sense_desc_header);
			break;
		}
		case SSD_DESC_SKS: {
			struct scsi_sense_sks *sks;

			sks = (struct scsi_sense_sks *)header;
			sks_ptr = sks->sense_key_spec;
			sks_size = sizeof(sks->sense_key_spec);

			pos = sks->length +
			    sizeof(struct scsi_sense_desc_header);
			break;
		}
		case SSD_DESC_STREAM: {
			struct scsi_sense_stream *stream_sense;

			stream_sense = (struct scsi_sense_stream *)header;
			stream_ptr = &stream_sense->byte3;
			stream_size = sizeof(stream_sense->byte3);
			pos = stream_sense->length +
			    sizeof(struct scsi_sense_desc_header);
			break;
		}
		default:
			/*
			 * We don't recognize this particular sense
			 * descriptor type, so just skip it.
			 */
			pos += sizeof(*header) + header->length;
			break;
		}
	}

	sense_len = SSD_FULL_SIZE;
	ctl_set_sense_data((struct scsi_sense_data *)sense_dest, &sense_len,
			   /*lun*/ NULL,
			   /*sense_format*/ SSD_TYPE_FIXED,
			   current_error,
			   /*sense_key*/ sense_src->sense_key & SSD_KEY,
			   /*asc*/ sense_src->add_sense_code,
			   /*ascq*/ sense_src->add_sense_code_qual,

			   /* Information Bytes */ 
			   (info_ptr != NULL) ? SSD_ELEM_INFO : SSD_ELEM_SKIP,
			   info_size,
			   info_ptr,

			   /* Command specific bytes */
			   (cmd_ptr != NULL) ? SSD_ELEM_COMMAND : SSD_ELEM_SKIP,
			   cmd_size,
			   cmd_ptr,

			   /* FRU */
			   (fru_ptr != NULL) ? SSD_ELEM_FRU : SSD_ELEM_SKIP,
			   fru_size,
			   fru_ptr,

			   /* Sense Key Specific */
			   (sks_ptr != NULL) ? SSD_ELEM_SKS : SSD_ELEM_SKIP,
			   sks_size,
			   sks_ptr,

			   /* Tape bits */
			   (stream_ptr != NULL) ? SSD_ELEM_STREAM : SSD_ELEM_SKIP,
			   stream_size,
			   stream_ptr,

			   SSD_ELEM_NONE);
}

void
ctl_set_ua(struct ctl_scsiio *ctsio, int asc, int ascq)
{
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_UNIT_ATTENTION,
		      asc,
		      ascq,
		      SSD_ELEM_NONE);
}

static void
ctl_ua_to_ascq(struct ctl_lun *lun, ctl_ua_type ua_to_build, int *asc,
    int *ascq, ctl_ua_type *ua_to_clear, uint8_t **info)
{

	switch (ua_to_build) {
	case CTL_UA_POWERON:
		/* 29h/01h  POWER ON OCCURRED */
		*asc = 0x29;
		*ascq = 0x01;
		*ua_to_clear = ~0;
		break;
	case CTL_UA_BUS_RESET:
		/* 29h/02h  SCSI BUS RESET OCCURRED */
		*asc = 0x29;
		*ascq = 0x02;
		*ua_to_clear = ~0;
		break;
	case CTL_UA_TARG_RESET:
		/* 29h/03h  BUS DEVICE RESET FUNCTION OCCURRED*/
		*asc = 0x29;
		*ascq = 0x03;
		*ua_to_clear = ~0;
		break;
	case CTL_UA_I_T_NEXUS_LOSS:
		/* 29h/07h  I_T NEXUS LOSS OCCURRED */
		*asc = 0x29;
		*ascq = 0x07;
		*ua_to_clear = ~0;
		break;
	case CTL_UA_LUN_RESET:
		/* 29h/00h  POWER ON, RESET, OR BUS DEVICE RESET OCCURRED */
		/*
		 * Since we don't have a specific ASC/ASCQ pair for a LUN
		 * reset, just return the generic reset code.
		 */
		*asc = 0x29;
		*ascq = 0x00;
		break;
	case CTL_UA_LUN_CHANGE:
		/* 3Fh/0Eh  REPORTED LUNS DATA HAS CHANGED */
		*asc = 0x3F;
		*ascq = 0x0E;
		break;
	case CTL_UA_MODE_CHANGE:
		/* 2Ah/01h  MODE PARAMETERS CHANGED */
		*asc = 0x2A;
		*ascq = 0x01;
		break;
	case CTL_UA_LOG_CHANGE:
		/* 2Ah/02h  LOG PARAMETERS CHANGED */
		*asc = 0x2A;
		*ascq = 0x02;
		break;
	case CTL_UA_INQ_CHANGE:
		/* 3Fh/03h  INQUIRY DATA HAS CHANGED */
		*asc = 0x3F;
		*ascq = 0x03;
		break;
	case CTL_UA_RES_PREEMPT:
		/* 2Ah/03h  RESERVATIONS PREEMPTED */
		*asc = 0x2A;
		*ascq = 0x03;
		break;
	case CTL_UA_RES_RELEASE:
		/* 2Ah/04h  RESERVATIONS RELEASED */
		*asc = 0x2A;
		*ascq = 0x04;
		break;
	case CTL_UA_REG_PREEMPT:
		/* 2Ah/05h  REGISTRATIONS PREEMPTED */
		*asc = 0x2A;
		*ascq = 0x05;
		break;
	case CTL_UA_ASYM_ACC_CHANGE:
		/* 2Ah/06h  ASYMMETRIC ACCESS STATE CHANGED */
		*asc = 0x2A;
		*ascq = 0x06;
		break;
	case CTL_UA_CAPACITY_CHANGE:
		/* 2Ah/09h  CAPACITY DATA HAS CHANGED */
		*asc = 0x2A;
		*ascq = 0x09;
		break;
	case CTL_UA_THIN_PROV_THRES:
		/* 38h/07h  THIN PROVISIONING SOFT THRESHOLD REACHED */
		*asc = 0x38;
		*ascq = 0x07;
		*info = lun->ua_tpt_info;
		break;
	case CTL_UA_MEDIUM_CHANGE:
		/* 28h/00h  NOT READY TO READY CHANGE, MEDIUM MAY HAVE CHANGED */
		*asc = 0x28;
		*ascq = 0x00;
		break;
	case CTL_UA_IE:
		/* Informational exception */
		*asc = lun->ie_asc;
		*ascq = lun->ie_ascq;
		break;
	default:
		panic("%s: Unknown UA %x", __func__, ua_to_build);
	}
}

ctl_ua_type
ctl_build_qae(struct ctl_lun *lun, uint32_t initidx, uint8_t *resp)
{
	ctl_ua_type ua;
	ctl_ua_type ua_to_build, ua_to_clear;
	uint8_t *info;
	int asc, ascq;
	uint32_t p, i;

	mtx_assert(&lun->lun_lock, MA_OWNED);
	p = initidx / CTL_MAX_INIT_PER_PORT;
	i = initidx % CTL_MAX_INIT_PER_PORT;
	if (lun->pending_ua[p] == NULL)
		ua = CTL_UA_POWERON;
	else
		ua = lun->pending_ua[p][i];
	if (ua == CTL_UA_NONE)
		return (CTL_UA_NONE);

	ua_to_build = (1 << (ffs(ua) - 1));
	ua_to_clear = ua_to_build;
	info = NULL;
	ctl_ua_to_ascq(lun, ua_to_build, &asc, &ascq, &ua_to_clear, &info);

	resp[0] = SSD_KEY_UNIT_ATTENTION;
	if (ua_to_build == ua)
		resp[0] |= 0x10;
	else
		resp[0] |= 0x20;
	resp[1] = asc;
	resp[2] = ascq;
	return (ua_to_build);
}

ctl_ua_type
ctl_build_ua(struct ctl_lun *lun, uint32_t initidx,
    struct scsi_sense_data *sense, u_int *sense_len,
    scsi_sense_data_type sense_format)
{
	ctl_ua_type *ua;
	ctl_ua_type ua_to_build, ua_to_clear;
	uint8_t *info;
	int asc, ascq;
	uint32_t p, i;

	mtx_assert(&lun->lun_lock, MA_OWNED);
	mtx_assert(&lun->ctl_softc->ctl_lock, MA_NOTOWNED);
	p = initidx / CTL_MAX_INIT_PER_PORT;
	if ((ua = lun->pending_ua[p]) == NULL) {
		mtx_unlock(&lun->lun_lock);
		ua = malloc(sizeof(ctl_ua_type) * CTL_MAX_INIT_PER_PORT,
		    M_CTL, M_WAITOK);
		mtx_lock(&lun->lun_lock);
		if (lun->pending_ua[p] == NULL) {
			lun->pending_ua[p] = ua;
			for (i = 0; i < CTL_MAX_INIT_PER_PORT; i++)
				ua[i] = CTL_UA_POWERON;
		} else {
			free(ua, M_CTL);
			ua = lun->pending_ua[p];
		}
	}
	i = initidx % CTL_MAX_INIT_PER_PORT;
	if (ua[i] == CTL_UA_NONE)
		return (CTL_UA_NONE);

	ua_to_build = (1 << (ffs(ua[i]) - 1));
	ua_to_clear = ua_to_build;
	info = NULL;
	ctl_ua_to_ascq(lun, ua_to_build, &asc, &ascq, &ua_to_clear, &info);

	ctl_set_sense_data(sense, sense_len, lun, sense_format, 1,
	    /*sense_key*/ SSD_KEY_UNIT_ATTENTION, asc, ascq,
	    ((info != NULL) ? SSD_ELEM_INFO : SSD_ELEM_SKIP), 8, info,
	    SSD_ELEM_NONE);

	/* We're reporting this UA, so clear it */
	ua[i] &= ~ua_to_clear;

	if (ua_to_build == CTL_UA_LUN_CHANGE) {
		mtx_unlock(&lun->lun_lock);
		mtx_lock(&lun->ctl_softc->ctl_lock);
		ctl_clr_ua_allluns(lun->ctl_softc, initidx, ua_to_build);
		mtx_unlock(&lun->ctl_softc->ctl_lock);
		mtx_lock(&lun->lun_lock);
	} else if (ua_to_build == CTL_UA_THIN_PROV_THRES &&
	    (lun->MODE_LBP.main.flags & SLBPP_SITUA) != 0) {
		ctl_clr_ua_all(lun, -1, ua_to_build);
	}

	return (ua_to_build);
}

void
ctl_set_overlapped_cmd(struct ctl_scsiio *ctsio)
{
	/* OVERLAPPED COMMANDS ATTEMPTED */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		      /*asc*/ 0x4E,
		      /*ascq*/ 0x00,
		      SSD_ELEM_NONE);
}

void
ctl_set_overlapped_tag(struct ctl_scsiio *ctsio, uint8_t tag)
{
	/* TAGGED OVERLAPPED COMMANDS (NN = QUEUE TAG) */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		      /*asc*/ 0x4D,
		      /*ascq*/ tag,
		      SSD_ELEM_NONE);
}

/*
 * Tell the user that there was a problem with the command or data he sent.
 */
void
ctl_set_invalid_field(struct ctl_scsiio *ctsio, int sks_valid, int command,
		      int field, int bit_valid, int bit)
{
	uint8_t sks[3];
	int asc;

	if (command != 0) {
		/* "Invalid field in CDB" */
		asc = 0x24;
	} else {
		/* "Invalid field in parameter list" */
		asc = 0x26;
	}

	if (sks_valid) {
		sks[0] = SSD_SCS_VALID;
		if (command)
			sks[0] |= SSD_FIELDPTR_CMD;
		scsi_ulto2b(field, &sks[1]);

		if (bit_valid)
			sks[0] |= SSD_BITPTR_VALID | bit;
	}

	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		      asc,
		      /*ascq*/ 0x00,
		      /*type*/ (sks_valid != 0) ? SSD_ELEM_SKS : SSD_ELEM_SKIP,
		      /*size*/ sizeof(sks),
		      /*data*/ sks,
		      SSD_ELEM_NONE);
}
void
ctl_set_invalid_field_ciu(struct ctl_scsiio *ctsio)
{

	/* "Invalid field in command information unit" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_ABORTED_COMMAND,
		      /*ascq*/ 0x0E,
		      /*ascq*/ 0x03,
		      SSD_ELEM_NONE);
}

void
ctl_set_invalid_opcode(struct ctl_scsiio *ctsio)
{
	uint8_t sks[3];

	sks[0] = SSD_SCS_VALID | SSD_FIELDPTR_CMD;
	scsi_ulto2b(0, &sks[1]);

	/* "Invalid command operation code" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		      /*asc*/ 0x20,
		      /*ascq*/ 0x00,
		      /*type*/ SSD_ELEM_SKS,
		      /*size*/ sizeof(sks),
		      /*data*/ sks,
		      SSD_ELEM_NONE);
}

void
ctl_set_param_len_error(struct ctl_scsiio *ctsio)
{
	/* "Parameter list length error" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		      /*asc*/ 0x1a,
		      /*ascq*/ 0x00,
		      SSD_ELEM_NONE);
}

void
ctl_set_already_locked(struct ctl_scsiio *ctsio)
{
	/* Vendor unique "Somebody already is locked" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		      /*asc*/ 0x81,
		      /*ascq*/ 0x00,
		      SSD_ELEM_NONE);
}

void
ctl_set_unsupported_lun(struct ctl_scsiio *ctsio)
{
	/* "Logical unit not supported" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		      /*asc*/ 0x25,
		      /*ascq*/ 0x00,
		      SSD_ELEM_NONE);
}

void
ctl_set_internal_failure(struct ctl_scsiio *ctsio, int sks_valid,
			 uint16_t retry_count)
{
	uint8_t sks[3];

	if (sks_valid) {
		sks[0] = SSD_SCS_VALID;
		sks[1] = (retry_count >> 8) & 0xff;
		sks[2] = retry_count & 0xff;
	}

	/* "Internal target failure" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_HARDWARE_ERROR,
		      /*asc*/ 0x44,
		      /*ascq*/ 0x00,
		      /*type*/ (sks_valid != 0) ? SSD_ELEM_SKS : SSD_ELEM_SKIP,
		      /*size*/ sizeof(sks),
		      /*data*/ sks,
		      SSD_ELEM_NONE);
}

void
ctl_set_medium_error(struct ctl_scsiio *ctsio, int read)
{
	if (read) {
		/* "Unrecovered read error" */
		ctl_set_sense(ctsio,
			      /*current_error*/ 1,
			      /*sense_key*/ SSD_KEY_MEDIUM_ERROR,
			      /*asc*/ 0x11,
			      /*ascq*/ 0x00,
			      SSD_ELEM_NONE);
	} else {
		/* "Write error - auto reallocation failed" */
		ctl_set_sense(ctsio,
			      /*current_error*/ 1,
			      /*sense_key*/ SSD_KEY_MEDIUM_ERROR,
			      /*asc*/ 0x0C,
			      /*ascq*/ 0x02,
			      SSD_ELEM_NONE);
	}
}

void
ctl_set_aborted(struct ctl_scsiio *ctsio)
{
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_ABORTED_COMMAND,
		      /*asc*/ 0x45,
		      /*ascq*/ 0x00,
		      SSD_ELEM_NONE);
}

void
ctl_set_lba_out_of_range(struct ctl_scsiio *ctsio, uint64_t lba)
{
	uint8_t	info[8];

	scsi_u64to8b(lba, info);

	/* "Logical block address out of range" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		      /*asc*/ 0x21,
		      /*ascq*/ 0x00,
		      /*type*/ (lba != 0) ? SSD_ELEM_INFO : SSD_ELEM_SKIP,
		      /*size*/ sizeof(info), /*data*/ &info,
		      SSD_ELEM_NONE);
}

void
ctl_set_lun_stopped(struct ctl_scsiio *ctsio)
{
	/* "Logical unit not ready, initializing cmd. required" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_NOT_READY,
		      /*asc*/ 0x04,
		      /*ascq*/ 0x02,
		      SSD_ELEM_NONE);
}

void
ctl_set_lun_int_reqd(struct ctl_scsiio *ctsio)
{
	/* "Logical unit not ready, manual intervention required" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_NOT_READY,
		      /*asc*/ 0x04,
		      /*ascq*/ 0x03,
		      SSD_ELEM_NONE);
}

void
ctl_set_lun_ejected(struct ctl_scsiio *ctsio)
{
	/* "Medium not present - tray open" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_NOT_READY,
		      /*asc*/ 0x3A,
		      /*ascq*/ 0x02,
		      SSD_ELEM_NONE);
}

void
ctl_set_lun_no_media(struct ctl_scsiio *ctsio)
{
	/* "Medium not present - tray closed" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_NOT_READY,
		      /*asc*/ 0x3A,
		      /*ascq*/ 0x01,
		      SSD_ELEM_NONE);
}

void
ctl_set_illegal_pr_release(struct ctl_scsiio *ctsio)
{
	/* "Invalid release of persistent reservation" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		      /*asc*/ 0x26,
		      /*ascq*/ 0x04,
		      SSD_ELEM_NONE);
}

void
ctl_set_lun_transit(struct ctl_scsiio *ctsio)
{
	/* "Logical unit not ready, asymmetric access state transition" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_NOT_READY,
		      /*asc*/ 0x04,
		      /*ascq*/ 0x0a,
		      SSD_ELEM_NONE);
}

void
ctl_set_lun_standby(struct ctl_scsiio *ctsio)
{
	/* "Logical unit not ready, target port in standby state" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_NOT_READY,
		      /*asc*/ 0x04,
		      /*ascq*/ 0x0b,
		      SSD_ELEM_NONE);
}

void
ctl_set_lun_unavail(struct ctl_scsiio *ctsio)
{
	/* "Logical unit not ready, target port in unavailable state" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_NOT_READY,
		      /*asc*/ 0x04,
		      /*ascq*/ 0x0c,
		      SSD_ELEM_NONE);
}

void
ctl_set_medium_format_corrupted(struct ctl_scsiio *ctsio)
{
	/* "Medium format corrupted" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_MEDIUM_ERROR,
		      /*asc*/ 0x31,
		      /*ascq*/ 0x00,
		      SSD_ELEM_NONE);
}

void
ctl_set_medium_magazine_inaccessible(struct ctl_scsiio *ctsio)
{
	/* "Medium magazine not accessible" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_NOT_READY,
		      /*asc*/ 0x3b,
		      /*ascq*/ 0x11,
		      SSD_ELEM_NONE);
}

void
ctl_set_data_phase_error(struct ctl_scsiio *ctsio)
{
	/* "Data phase error" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_NOT_READY,
		      /*asc*/ 0x4b,
		      /*ascq*/ 0x00,
		      SSD_ELEM_NONE);
}

void
ctl_set_reservation_conflict(struct ctl_scsiio *ctsio)
{

	ctsio->scsi_status = SCSI_STATUS_RESERV_CONFLICT;
	ctsio->sense_len = 0;
	ctsio->io_hdr.status = CTL_SCSI_ERROR;
}

void
ctl_set_queue_full(struct ctl_scsiio *ctsio)
{

	ctsio->scsi_status = SCSI_STATUS_QUEUE_FULL;
	ctsio->sense_len = 0;
	ctsio->io_hdr.status = CTL_SCSI_ERROR;
}

void
ctl_set_busy(struct ctl_scsiio *ctsio)
{

	ctsio->scsi_status = SCSI_STATUS_BUSY;
	ctsio->sense_len = 0;
	ctsio->io_hdr.status = CTL_SCSI_ERROR;
}

void
ctl_set_task_aborted(struct ctl_scsiio *ctsio)
{

	ctsio->scsi_status = SCSI_STATUS_TASK_ABORTED;
	ctsio->sense_len = 0;
	ctsio->io_hdr.status = CTL_CMD_ABORTED;
}

void
ctl_set_hw_write_protected(struct ctl_scsiio *ctsio)
{
	/* "Hardware write protected" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_DATA_PROTECT,
		      /*asc*/ 0x27,
		      /*ascq*/ 0x01,
		      SSD_ELEM_NONE);
}

void
ctl_set_space_alloc_fail(struct ctl_scsiio *ctsio)
{
	/* "Space allocation failed write protect" */
	ctl_set_sense(ctsio,
		      /*current_error*/ 1,
		      /*sense_key*/ SSD_KEY_DATA_PROTECT,
		      /*asc*/ 0x27,
		      /*ascq*/ 0x07,
		      SSD_ELEM_NONE);
}

void
ctl_set_success(struct ctl_scsiio *ctsio)
{

	ctsio->scsi_status = SCSI_STATUS_OK;
	ctsio->sense_len = 0;
	ctsio->io_hdr.status = CTL_SUCCESS;
}
