/*-
 * Copyright (c) 2017 Marius Strobl <marius@FreeBSD.org>
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
 * $FreeBSD$
 */

#ifndef _DEV_MMC_MMC_IOCTL_H_
#define	_DEV_MMC_MMC_IOCTL_H_

struct mmc_ioc_cmd {
	int		write_flag; /* 0: RD, 1: WR, (1 << 31): reliable WR */
	int		is_acmd;    /* 0: normal, 1: use CMD55 */
	uint32_t	opcode;
	uint32_t	arg;
	uint32_t	response[4];
	u_int		flags;
	u_int		blksz;
	u_int		blocks;
	u_int		__spare[4];
	uint32_t	__pad;
	uint64_t	data_ptr;
};

#define	mmc_ioc_cmd_set_data(mic, ptr)					\
    (mic).data_ptr = (uint64_t)(uintptr_t)(ptr)

struct mmc_ioc_multi_cmd {
	uint64_t		num_of_cmds;
	struct mmc_ioc_cmd	cmds[0];
};

#define	MMC_IOC_BASE		'M'

#define	MMC_IOC_CMD		_IOWR(MMC_IOC_BASE, 0, struct mmc_ioc_cmd)
#define	MMC_IOC_MULTI_CMD	_IOWR(MMC_IOC_BASE, 1, struct mmc_ioc_multi_cmd)

/* Maximum accepted data transfer size */
#define	MMC_IOC_MAX_BYTES	(512  * 256)
/* Maximum accepted number of commands */
#define	MMC_IOC_MAX_CMDS	255

#endif /* _DEV_MMC_MMC_IOCTL_H_ */
