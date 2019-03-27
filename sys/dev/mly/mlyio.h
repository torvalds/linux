/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Michael Smith
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

/********************************************************************************
 * Control structures exchanged through the GAM interface with userland
 * management tools.
 *
 * The member naming here is consistent with the Linux driver, with which this
 * interface is basically compatible. 
 */
struct mly_user_command
{
    unsigned char		ControllerNumber;
    union mly_command_packet	CommandMailbox;
    int				DataTransferLength;
    int				RequestSenseLength;
    void			*DataTransferBuffer;
    void			*RequestSenseBuffer;
    int				CommandStatus;		/* not in the Linux structure */
};

#define MLYIO_COMMAND	_IOWR('M', 200, struct mly_user_command)

struct mly_user_health
{
    unsigned char	ControllerNumber;
    void		*HealthStatusBuffer;
};

#define MLYIO_HEALTH	_IOW('M', 201, struct mly_user_health)

/*
 * Command queue statistics
 */

#define MLYQ_FREE	0
#define MLYQ_BUSY	1
#define MLYQ_COMPLETE	2
#define MLYQ_COUNT	3

struct mly_qstat 
{
    u_int32_t	q_length;
    u_int32_t	q_max;
};
