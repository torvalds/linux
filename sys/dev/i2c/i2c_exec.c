/*	$OpenBSD: i2c_exec.c,v 1.3 2015/03/14 03:38:47 jsg Exp $	*/
/*	$NetBSD: i2c_exec.c,v 1.3 2003/10/29 00:34:58 mycroft Exp $	*/

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/event.h>

#define	_I2C_PRIVATE
#include <dev/i2c/i2cvar.h>

/*
 * iic_exec:
 *
 *	Simplified I2C client interface engine.
 *
 *	This and the SMBus routines are the preferred interface for
 *	client access to I2C/SMBus, since many automated controllers
 *	do not provide access to the low-level primitives of the I2C
 *	bus protocol.
 */
int
iic_exec(i2c_tag_t tag, i2c_op_t op, i2c_addr_t addr, const void *vcmd,
    size_t cmdlen, void *vbuf, size_t buflen, int flags)
{
	const uint8_t *cmd = vcmd;
	uint8_t *buf = vbuf;
	int error;
	size_t len;

	/*
	 * Defer to the controller if it provides an exec function.  Use
	 * it if it does.
	 */
	if (tag->ic_exec != NULL)
		return ((*tag->ic_exec)(tag->ic_cookie, op, addr, cmd,
					cmdlen, buf, buflen, flags));

	if ((len = cmdlen) != 0) {
		if ((error = iic_initiate_xfer(tag, addr, flags)) != 0)
			goto bad;
		while (len--) {
			if ((error = iic_write_byte(tag, *cmd++, flags)) != 0)
				goto bad;
		}
	}

	if (I2C_OP_READ_P(op))
		flags |= I2C_F_READ;

	len = buflen;
	while (len--) {
		if (len == 0 && I2C_OP_STOP_P(op))
			flags |= I2C_F_STOP;
		if (I2C_OP_READ_P(op)) {
			/* Send REPEATED START. */
			if ((len + 1) == buflen &&
			    (error = iic_initiate_xfer(tag, addr, flags)) != 0)
				goto bad;
			/* NACK on last byte. */
			if (len == 0)
				flags |= I2C_F_LAST;
			if ((error = iic_read_byte(tag, buf++, flags)) != 0)
				goto bad;
		} else  {
			/* Maybe send START. */
			if ((len + 1) == buflen && cmdlen == 0 &&
			    (error = iic_initiate_xfer(tag, addr, flags)) != 0)
				goto bad;
			if ((error = iic_write_byte(tag, *buf++, flags)) != 0)
				goto bad;
		}
	}

	return (0);
 bad:
	iic_send_stop(tag, flags);
	return (error);
}

/*
 * iic_smbus_write_byte:
 *
 *	Perform an SMBus "write byte" operation.
 */
int
iic_smbus_write_byte(i2c_tag_t tag, i2c_addr_t addr, uint8_t cmd,
    uint8_t val, int flags)
{

	return (iic_exec(tag, I2C_OP_WRITE_WITH_STOP, addr,
	    &cmd, sizeof cmd, &val, sizeof val, flags));
}

/*
 * iic_smbus_read_byte:
 *
 *	Perform an SMBus "read byte" operation.
 */
int
iic_smbus_read_byte(i2c_tag_t tag, i2c_addr_t addr, uint8_t cmd,
    uint8_t *valp, int flags)
{

	return (iic_exec(tag, I2C_OP_READ_WITH_STOP, addr,
	    &cmd, sizeof cmd, valp, sizeof (*valp), flags));
}

/*
 * iic_smbus_receive_byte:
 *
 *	Perform an SMBus "receive byte" operation.
 */
int
iic_smbus_receive_byte(i2c_tag_t tag, i2c_addr_t addr, uint8_t *valp,
    int flags)
{

	return (iic_exec(tag, I2C_OP_READ_WITH_STOP, addr,
	    NULL, 0, valp, sizeof (*valp), flags));
}
