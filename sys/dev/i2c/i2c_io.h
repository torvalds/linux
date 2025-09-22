/*	$OpenBSD: i2c_io.h,v 1.2 2020/01/11 11:30:47 kettenis Exp $	*/
/*	$NetBSD: i2c_io.h,v 1.3 2012/04/22 14:10:36 pgoyette Exp $	*/

/*
 * Copyright (c) 2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
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
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_I2C_I2C_IO_H_
#define	_DEV_I2C_I2C_IO_H_

#include <sys/ioccom.h>

/* I2C bus address. */
typedef uint16_t i2c_addr_t;

/* High-level I2C operations. */
#define	I2C_OPMASK_STOP		1
#define	I2C_OPMASK_WRITE	2
#define	I2C_OPMASK_BLKMODE	4

#define	I2C_OP_STOP_P(x)	(((int)(x) & I2C_OPMASK_STOP) != 0)
#define	I2C_OP_WRITE_P(x)	(((int)(x) & I2C_OPMASK_WRITE) != 0)
#define	I2C_OP_READ_P(x)	(!I2C_OP_WRITE_P(x))
#define	I2C_OP_BLKMODE_P(x)	(((int)(x) & I2C_OPMASK_BLKMODE) != 0)

typedef enum {
	I2C_OP_READ		= 0,
	I2C_OP_READ_WITH_STOP	= I2C_OPMASK_STOP,
	I2C_OP_WRITE		= I2C_OPMASK_WRITE,
	I2C_OP_WRITE_WITH_STOP	= I2C_OPMASK_WRITE   | I2C_OPMASK_STOP,
	I2C_OP_READ_BLOCK	= I2C_OPMASK_BLKMODE | I2C_OPMASK_STOP,
	I2C_OP_WRITE_BLOCK	= I2C_OPMASK_BLKMODE | I2C_OPMASK_WRITE |
					I2C_OPMASK_STOP,
} i2c_op_t;

/*
 * This structure describes a single I2C control script fragment.
 *
 * Note that use of this scripted API allows for support of automated
 * SMBus controllers.  The following table maps SMBus operations to
 * script fragment configuration:
 *
 *	SMBus "write byte":		I2C_OP_WRITE_WITH_STOP
 *					cmdlen = 1
 *
 *	SMBus "read byte":		I2C_OP_READ_WITH_STOP
 *					cmdlen = 1
 *
 *	SMBus "receive byte":		I2C_OP_READ_WITH_STOP
 *					cmdlen = 0
 *
 * Note that while each of these 3 SMBus operations implies a STOP
 * (which an automated controller will likely perform automatically),
 * non-SMBus clients may continue to function even if they issue
 * I2C_OP_WRITE and I2C_OP_READ.
 */

#ifdef notyet
/*
 * I2C_IOCTL_EXEC:
 *
 *	User ioctl to execute an i2c operation.
 */
typedef struct i2c_ioctl_exec {
	i2c_op_t iie_op;		/* operation to perform */
	i2c_addr_t iie_addr;		/* address of device */
	const void *iie_cmd;		/* pointer to command */
	size_t iie_cmdlen;		/* length of command */
	void *iie_buf;			/* pointer to data buffer */
	size_t iie_buflen;		/* length of data buffer */
} i2c_ioctl_exec_t;
#define	I2C_EXEC_MAX_CMDLEN	32
#define	I2C_EXEC_MAX_BUFLEN	32

#define	I2C_IOCTL_EXEC		 _IOW('I', 0, i2c_ioctl_exec_t)
#endif

#endif /* _DEV_I2C_I2C_IO_H_ */
