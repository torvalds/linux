/*	$OpenBSD: libscsi.h,v 1.3 2012/12/04 02:27:00 deraadt Exp $	*/

/* Copyright (c) 1994 HD Associates (hd@world.std.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 * This product includes software developed by HD Associates
 * 4. Neither the name of the HD Associaates nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY HD ASSOCIATES``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL HD ASSOCIATES OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SCSI_H_
#define _SCSI_H_

/* libscsi: Library header file for the SCSI user library.
 */

#include <sys/scsiio.h>
#include <stdio.h>

#define SCSIREQ_ERROR(SR) \
    (SR->senselen_used ||	/* Sent sense */ \
    SR->status ||      		/* Host adapter status */ \
    SR->retsts ||     		/* SCSI transfer status */ \
    SR->error)       		/* copy of errno */

scsireq_t *scsireq_reset(scsireq_t *);
scsireq_t *scsireq_new(void);

int scsireq_buff_decode_visit(u_char *, size_t, char *,
    void (*a)(void *, int, void *, int, char *), void *);

int scsireq_decode_visit(scsireq_t *, char *,
    void (*)(void *, int, void *, int, char *), void *);

int scsireq_encode_visit(scsireq_t *, char *,
    int (*)(void *, char *), void *);
int scsireq_buff_encode_visit(u_char *, size_t, char *,
    int (*)(void *, char *), void *);

scsireq_t *scsireq_build(scsireq_t *, u_long, caddr_t, u_long, char *, ...);

scsireq_t *scsireq_build_visit(scsireq_t *, u_long, caddr_t, u_long, char *,
    int (*)(void *, char *), void *);

int scsireq_enter(int, scsireq_t *);

void scsi_dump(FILE *, char *, u_char *, int, int, int );

int scsi_debug(FILE *, int, scsireq_t *);
FILE *scsi_debug_output(char *);
int scsi_open(const char *, int );

#endif /* _SCSI_H_ */
