/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Implementation of Utility functions for all SCSI device types.
 *
 * Copyright (c) 1997, 1998, 1999 Justin T. Gibbs.
 * Copyright (c) 1997, 1998, 2003 Kenneth D. Merry.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_scsi_all.c#2 $
 */

#include <sys/param.h>

__FBSDID("$FreeBSD$");

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/libkern.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#else
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#endif

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_queue.h>
#include <cam/cam_xpt.h>
#include <cam/scsi/scsi_all.h>

#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl_scsi_all.h>
#include <sys/sbuf.h>
#ifndef _KERNEL
#include <camlib.h>
#endif

const char *
ctl_scsi_status_string(struct ctl_scsiio *ctsio)
{
	switch(ctsio->scsi_status) {
	case SCSI_STATUS_OK:
		return("OK");
	case SCSI_STATUS_CHECK_COND:
		return("Check Condition");
	case SCSI_STATUS_BUSY:
		return("Busy");
	case SCSI_STATUS_INTERMED:
		return("Intermediate");
	case SCSI_STATUS_INTERMED_COND_MET:
		return("Intermediate-Condition Met");
	case SCSI_STATUS_RESERV_CONFLICT:
		return("Reservation Conflict");
	case SCSI_STATUS_CMD_TERMINATED:
		return("Command Terminated");
	case SCSI_STATUS_QUEUE_FULL:
		return("Queue Full");
	case SCSI_STATUS_ACA_ACTIVE:
		return("ACA Active");
	case SCSI_STATUS_TASK_ABORTED:
		return("Task Aborted");
	default: {
		static char unkstr[64];
		snprintf(unkstr, sizeof(unkstr), "Unknown %#x",
			 ctsio->scsi_status);
		return(unkstr);
	}
	}
}

/*
 * scsi_command_string() returns 0 for success and -1 for failure.
 */
int
ctl_scsi_command_string(struct ctl_scsiio *ctsio,
			struct scsi_inquiry_data *inq_data, struct sbuf *sb)
{
	char cdb_str[(SCSI_MAX_CDBLEN * 3) + 1];

	sbuf_printf(sb, "%s. CDB: %s",
		    scsi_op_desc(ctsio->cdb[0], inq_data),
		    scsi_cdb_string(ctsio->cdb, cdb_str, sizeof(cdb_str)));

	return(0);
}

void
ctl_scsi_path_string(union ctl_io *io, char *path_str, int len)
{

	snprintf(path_str, len, "(%u:%u:%u/%u): ",
	    io->io_hdr.nexus.initid, io->io_hdr.nexus.targ_port,
	    io->io_hdr.nexus.targ_lun, io->io_hdr.nexus.targ_mapped_lun);
}

/*
 * ctl_scsi_sense_sbuf() returns 0 for success and -1 for failure.
 */
int
ctl_scsi_sense_sbuf(struct ctl_scsiio *ctsio,
		    struct scsi_inquiry_data *inq_data, struct sbuf *sb,
		    scsi_sense_string_flags flags)
{
	char	  path_str[64];

	if ((ctsio == NULL) || (sb == NULL))
		return(-1);

	ctl_scsi_path_string((union ctl_io *)ctsio, path_str, sizeof(path_str));

	if (flags & SSS_FLAG_PRINT_COMMAND) {

		sbuf_cat(sb, path_str);

		ctl_scsi_command_string(ctsio, inq_data, sb);

		sbuf_printf(sb, "\n");
	}

	scsi_sense_only_sbuf(&ctsio->sense_data, ctsio->sense_len, sb,
			     path_str, inq_data, ctsio->cdb, ctsio->cdb_len);

	return(0);
}

char *
ctl_scsi_sense_string(struct ctl_scsiio *ctsio,
		      struct scsi_inquiry_data *inq_data, char *str,
		      int str_len)
{
	struct sbuf sb;

	sbuf_new(&sb, str, str_len, 0);

	ctl_scsi_sense_sbuf(ctsio, inq_data, &sb, SSS_FLAG_PRINT_COMMAND);

	sbuf_finish(&sb);

	return(sbuf_data(&sb));
}

#ifdef _KERNEL
void 
ctl_scsi_sense_print(struct ctl_scsiio *ctsio,
		     struct scsi_inquiry_data *inq_data)
{
	struct sbuf sb;
	char str[512];

	sbuf_new(&sb, str, sizeof(str), 0);

	ctl_scsi_sense_sbuf(ctsio, inq_data, &sb, SSS_FLAG_PRINT_COMMAND);

	sbuf_finish(&sb);

	printf("%s", sbuf_data(&sb));
}

#else /* _KERNEL */
void
ctl_scsi_sense_print(struct ctl_scsiio *ctsio,
		     struct scsi_inquiry_data *inq_data, FILE *ofile)
{
	struct sbuf sb;
	char str[512];

	if ((ctsio == NULL) || (ofile == NULL))
		return;

	sbuf_new(&sb, str, sizeof(str), 0);

	ctl_scsi_sense_sbuf(ctsio, inq_data, &sb, SSS_FLAG_PRINT_COMMAND);

	sbuf_finish(&sb);

	fprintf(ofile, "%s", sbuf_data(&sb));
}

#endif /* _KERNEL */

