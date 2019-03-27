/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Doug Rabson
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

#ifndef _SYS_SPIGENIO_H_
#define _SYS_SPIGENIO_H_

#include <sys/_iovec.h>

struct spigen_transfer {
	struct iovec st_command; /* master to slave */
	struct iovec st_data;    /* slave to master and/or master to slave */
};

struct spigen_transfer_mmapped {
	size_t stm_command_length; /* at offset 0 in mmap(2) area */
	size_t stm_data_length;    /* at offset stm_command_length */
};

#define SPIGENIOC_BASE     'S'
#define SPIGENIOC_TRANSFER 	   _IOW(SPIGENIOC_BASE, 0, \
	    struct spigen_transfer)
#define SPIGENIOC_TRANSFER_MMAPPED _IOW(SPIGENIOC_BASE, 1, \
	    struct spigen_transfer_mmapped)
#define SPIGENIOC_GET_CLOCK_SPEED  _IOR(SPIGENIOC_BASE, 2, uint32_t)
#define SPIGENIOC_SET_CLOCK_SPEED  _IOW(SPIGENIOC_BASE, 3, uint32_t)
#define SPIGENIOC_GET_SPI_MODE     _IOR(SPIGENIOC_BASE, 4, uint32_t)
#define SPIGENIOC_SET_SPI_MODE     _IOW(SPIGENIOC_BASE, 5, uint32_t)

#endif /* !_SYS_SPIGENIO_H_ */
