/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
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
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_scsi_all.h#2 $
 */

__FBSDID("$FreeBSD$");

__BEGIN_DECLS
const char *	ctl_scsi_status_string(struct ctl_scsiio *ctsio);
#ifdef _KERNEL
void		ctl_scsi_sense_print(struct ctl_scsiio *ctsio,
				     struct scsi_inquiry_data *inq_data);
#else /* _KERNEL */
void		ctl_scsi_sense_print(struct ctl_scsiio *ctsio,
				     struct scsi_inquiry_data *inq_data,
				     FILE *ofile);
#endif /* _KERNEL */
int ctl_scsi_command_string(struct ctl_scsiio *ctsio,
			    struct scsi_inquiry_data *inq_data,struct sbuf *sb);
int ctl_scsi_sense_sbuf(struct ctl_scsiio *ctsio,
			struct scsi_inquiry_data *inq_data, struct sbuf *sb,
			scsi_sense_string_flags flags);
void ctl_scsi_path_string(union ctl_io *io, char *path_str, int strlen);
char *ctl_scsi_sense_string(struct ctl_scsiio *ctsio,
			    struct scsi_inquiry_data *inq_data, char *str,
			    int str_len);

__END_DECLS
