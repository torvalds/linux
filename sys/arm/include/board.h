/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 M. Warner Losh.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $FreeBSD$ */

#ifndef _ARM_INCLUDE_BOARD_H_
#define _ARM_INCLUDE_BOARD_H_

#include <sys/linker_set.h>

typedef long (arm_board_init_fn)(void);

struct arm_board {
	int		board_id;	/* Board ID from the boot loader */
	const char	*board_name;	/* Human readable name */
	arm_board_init_fn *board_init;	/* Board initialize code */
};

#if defined(ARM_MANY_BOARD)

#include "board_id.h"

#define ARM_BOARD(id, name)     \
	static struct arm_board this_board = { \
		.board_id = ARM_BOARD_ID_ ## id, \
		.board_name = name, \
		.board_init = board_init, \
	}; \
	DATA_SET(arm_boards, this_board);
#define BOARD_INIT static

#else /* !ARM_MANY_BOARD */

#define ARM_BOARD(id, name)
extern arm_board_init_fn board_init;
#define BOARD_INIT

#endif /* ARM_MANY_BOARD */

#endif /* _ARM_INCLUDE_BOARD_H_ */
