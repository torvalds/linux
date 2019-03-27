/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2003 Paul Saab
 * Copyright (c) 2003 Vinod Kashyap
 * Copyright (c) 2000 BSDi
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


/*
 * User-space command
 *
 * Note that the command's scatter/gather list will be computed by the
 * driver, and cannot be filled in by the consumer.
 */
struct twe_usercommand {
    TWE_Command	tu_command;	/* command ready for the controller */
    void	*tu_data;	/* pointer to data in userspace */
    size_t	tu_size;	/* userspace data length */
};

#define TWEIO_COMMAND		_IOWR('T', 100, struct twe_usercommand)

/*
 * Command queue statistics
 */
#define TWEQ_FREE	0
#define TWEQ_BIO	1
#define TWEQ_READY	2
#define TWEQ_BUSY	3
#define TWEQ_COMPLETE	4
#define TWEQ_COUNT	5	/* total number of queues */

struct twe_qstat {
    u_int32_t	q_length;
    u_int32_t	q_max;
    u_int32_t	q_min;
};

/*
 * Statistics request
 */
union twe_statrequest {
    u_int32_t		ts_item;
    struct twe_qstat	ts_qstat;
};

#define TWEIO_STATS		_IOWR('T', 101, union twe_statrequest)

/*
 * AEN listen
 */
#define TWEIO_AEN_POLL		_IOR('T', 102, u_int16_t)
#define TWEIO_AEN_WAIT		_IOR('T', 103, u_int16_t)

/*
 * Controller parameter access
 */
struct twe_paramcommand {
    u_int16_t	tp_table_id;
    u_int8_t	tp_param_id;
    void	*tp_data;
    u_int8_t	tp_size;
};

#define TWEIO_SET_PARAM		_IOW ('T', 104, struct twe_paramcommand)
#define TWEIO_GET_PARAM		_IOW ('T', 105, struct twe_paramcommand)

/*
 * Request a controller soft-reset
 */
#define TWEIO_RESET		_IO  ('T', 106)

/*
 * Request a drive addition or deletion
 */
struct twe_drivecommand {
    int		td_unit;
};

#define TWEIO_ADD_UNIT		_IOW ('U', 107, int)
#define TWEIO_DEL_UNIT		_IOW ('U', 108, int)
