/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-3-Clause
 *
 * Copyright (c) 1999 Michael Smith
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (c) 2002 Eric Moore
 * Copyright (c) 2002 LSI Logic Corporation
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
 * 3. The party using or redistributing the source code and binary forms
 *    agrees to the disclaimer below and the terms and conditions set forth
 *    herein.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 *	$FreeBSD$
 */

/*
 * ioctl interface
 */

#include <sys/ioccom.h>
#include <sys/param.h>

/*
 * Fetch the driver's interface version.
 */
#define AMR_IO_VERSION_NUMBER	153
#define AMR_IO_VERSION	_IOR('A', 0x200, int)

/*
 * Pass a command from userspace through to the adapter.
 *
 * Note that in order to be code-compatible with the Linux
 * interface where possible, the formatting of the au_cmd field is
 * somewhat Interesting.
 *
 * For normal commands, the layout is (fields from struct amr_mailbox_ioctl):
 *
 * 0		mb_command
 * 1		mb_channel
 * 2		mb_param
 * 3		mb_pad[0]
 * 4		mb_drive
 *
 * For SCSI passthrough commands, the layout is:
 *
 * 0		AMR_CMD_PASS	(0x3)
 * 1		reserved, 0
 * 2		cdb length
 * 3		cdb data
 * 3+cdb_len	passthrough control byte (timeout, ars, islogical)
 * 4+cdb_len	reserved, 0
 * 5+cdb_len	channel
 * 6+cdb_len	target
 */

struct amr_user_ioctl {
    unsigned char	au_cmd[32];	/* command text from userspace */
    void		*au_buffer;	/* data buffer in userspace */
    unsigned long	au_length;	/* data buffer size (0 == no data) */
    int			au_direction;	/* data transfer direction */
#define AMR_IO_NODATA	0
#define AMR_IO_READ	1
#define AMR_IO_WRITE	2
    int			au_status;	/* command status returned by adapter */
};

#define AMR_IO_COMMAND	_IOWR('A', 0x201, struct amr_user_ioctl)

#if defined(__amd64__)

struct amr_user_ioctl32 {
    unsigned char	au_cmd[32];	/* command text from userspace */
    u_int32_t		au_buffer;	/* 32-bit pointer to uspace buf */
    u_int32_t		au_length;	/* length of the uspace buffer */
    int32_t		au_direction;	/* data transfer direction */
    int32_t		au_status;	/* command status returned by adapter */
};

#	define AMR_IO_COMMAND32	_IOWR('A', 0x201, struct amr_user_ioctl32)
#endif
