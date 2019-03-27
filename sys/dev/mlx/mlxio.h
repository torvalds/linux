/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
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
 *	$FreeBSD$
 */

#include <sys/ioccom.h>

/*
 * System Disk ioctls
 */

/* system disk status values */
#define MLX_SYSD_ONLINE		0x03
#define MLX_SYSD_CRITICAL	0x04
#define MLX_SYSD_OFFLINE	0xff

#define MLXD_STATUS		_IOR ('M', 100, int)
#define MLXD_CHECKASYNC		_IOR ('M', 101, int)	/* command result returned in argument */

/*
 * Controller ioctls
 */
struct mlx_pause 
{
    int		mp_which;
#define MLX_PAUSE_ALL		0xff
#define MLX_PAUSE_CANCEL	0x00
    int		mp_when;
    int		mp_howlong;
};

struct mlx_usercommand
{
    /* data buffer */
    size_t	mu_datasize;	/* size of databuffer */
    void	*mu_buf;	/* address in userspace of databuffer */
    int		mu_bufptr;	/* offset into command mailbox to place databuffer address */

    /* command */
    u_int16_t	mu_status;	/* command status returned */
    u_int8_t	mu_command[16];	/* command mailbox contents */

    /* wrapper */
    int		mu_error;	/* result of submission to driver */

};

struct mlx_rebuild_request
{
    int		rr_channel;
    int		rr_target;
    int		rr_status;
};

struct mlx_rebuild_status
{
    u_int16_t	rs_code;
#define MLX_REBUILDSTAT_REBUILDCHECK	0x0000
#define MLX_REBUILDSTAT_ADDCAPACITY	0x0400
#define MLX_REBUILDSTAT_ADDCAPACITYINIT	0x0500
#define MLX_REBUILDSTAT_IDLE		0xffff
    u_int16_t	rs_drive;
    int		rs_size;
    int		rs_remaining;
};

#define MLX_NEXT_CHILD		_IOWR('M', 0, int)
#define MLX_RESCAN_DRIVES	_IO  ('M', 1)
#define MLX_DETACH_DRIVE	_IOW ('M', 2, int)
#define MLX_PAUSE_CHANNEL	_IOW ('M', 3, struct mlx_pause)
#define MLX_COMMAND		_IOWR('M', 4, struct mlx_usercommand)
#define MLX_REBUILDASYNC	_IOWR('M', 5, struct mlx_rebuild_request)
#define MLX_REBUILDSTAT		_IOR ('M', 6, struct mlx_rebuild_status)
#define MLX_GET_SYSDRIVE	_IOWR('M', 7, int)
