/*	$OpenBSD: i2c_bitbang.c,v 1.5 2023/03/08 04:43:08 guenther Exp $	*/
/*	$NetBSD: i2c_bitbang.c,v 1.1 2003/09/30 00:35:31 thorpej Exp $	*/

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

/*
 * Common module for bit-bang'ing an I2C bus.
 */

#include <sys/param.h>

#include <dev/i2c/i2cvar.h>
#include <dev/i2c/i2c_bitbang.h>

#define	BB_SET(x)	ops->ibo_set_bits(v, (x))
#define	BB_DIR(x)	ops->ibo_set_dir(v, (x))
#define	BB_READ		ops->ibo_read_bits(v)

#define	SDA		ops->ibo_bits[I2C_BIT_SDA]	/* i2c signal */
#define	SCL		ops->ibo_bits[I2C_BIT_SCL]	/* i2c signal */
#define	OUTPUT		ops->ibo_bits[I2C_BIT_OUTPUT]	/* SDA is output */
#define	INPUT		ops->ibo_bits[I2C_BIT_INPUT]	/* SDA is input */

#define SCL_BAIL_COUNT	1000

int i2c_wait_for_scl(void *, i2c_bitbang_ops_t);

int
i2c_wait_for_scl(void *v, i2c_bitbang_ops_t ops)
{
	int bail = 0;

	while(((BB_READ & SCL) == 0) && bail < SCL_BAIL_COUNT) {
		delay(1);
		bail++;
	}
	if (bail == SCL_BAIL_COUNT) {
		i2c_bitbang_send_stop(v, 0, ops);
		return (EIO);
	}

	return (0);
}

int
i2c_bitbang_send_start(void *v, int flags, i2c_bitbang_ops_t ops)
{

	BB_DIR(OUTPUT);

	BB_SET(SDA | SCL);
	delay(5);		/* bus free time (4.7 uS) */
	BB_SET(      SCL);
	if (i2c_wait_for_scl(v, ops) != 0)
		return (EIO);
	delay(4);		/* start hold time (4.0 uS) */
	BB_SET(        0);
	delay(5);		/* clock low time (4.7 uS) */

	return (0);
}

int
i2c_bitbang_send_stop(void *v, int flags, i2c_bitbang_ops_t ops)
{

	BB_DIR(OUTPUT);

	BB_SET(      SCL);
	delay(4);		/* stop setup time (4.0 uS) */
	BB_SET(SDA | SCL);

	return (0);
}

int
i2c_bitbang_initiate_xfer(void *v, i2c_addr_t addr, int flags,
    i2c_bitbang_ops_t ops)
{
	int i2caddr;

	/* XXX Only support 7-bit addressing for now. */
	if ((addr & 0x78) == 0x78)
		return (EINVAL);

	i2caddr = (addr << 1) | ((flags & I2C_F_READ) ? 1 : 0);

	(void) i2c_bitbang_send_start(v, flags, ops);
	return (i2c_bitbang_write_byte(v, i2caddr, flags & ~I2C_F_STOP, ops));
}

int
i2c_bitbang_read_byte(void *v, uint8_t *valp, int flags,
    i2c_bitbang_ops_t ops)
{
	int i;
	uint8_t val = 0;
	uint32_t bit;

	BB_DIR(INPUT);
	BB_SET(SDA      );

	for (i = 0; i < 8; i++) {
		val <<= 1;
		BB_SET(SDA | SCL);
		if (i2c_wait_for_scl(v, ops) != 0)
			return (EIO);
		delay(4);	/* clock high time (4.0 uS) */
		if (BB_READ & SDA)
			val |= 1;
		BB_SET(SDA      );
		delay(5);	/* clock low time (4.7 uS) */
	}

	bit = (flags & I2C_F_LAST) ? SDA : 0;
	BB_DIR(OUTPUT);
	BB_SET(bit      );
	delay(1);	/* data setup time (250 nS) */
	BB_SET(bit | SCL);
	if (i2c_wait_for_scl(v, ops) != 0)
		return (EIO);
	delay(4);	/* clock high time (4.0 uS) */
	BB_SET(bit      );
	delay(5);	/* clock low time (4.7 uS) */

	BB_DIR(INPUT);
	BB_SET(SDA      );
	delay(5);

	if ((flags & (I2C_F_STOP | I2C_F_LAST)) == (I2C_F_STOP | I2C_F_LAST))
		(void) i2c_bitbang_send_stop(v, flags, ops);

	*valp = val;
	return (0);
}

int
i2c_bitbang_write_byte(void *v, uint8_t val, int flags,
    i2c_bitbang_ops_t ops)
{
	uint32_t bit;
	uint8_t mask;
	int error;

	BB_DIR(OUTPUT);

	for (mask = 0x80; mask != 0; mask >>= 1) {
		bit = (val & mask) ? SDA : 0;
		BB_SET(bit      );
		delay(1);	/* data setup time (250 nS) */
		BB_SET(bit | SCL);
		if (i2c_wait_for_scl(v, ops) != 0)
			return (EIO);
		delay(4);	/* clock high time (4.0 uS) */
		BB_SET(bit      );
		delay(5);	/* clock low time (4.7 uS) */
	}

	BB_DIR(INPUT);

	BB_SET(SDA      );
	delay(5);
	BB_SET(SDA | SCL);
	if (i2c_wait_for_scl(v, ops) != 0)
		return (EIO);
	delay(4);
	error = (BB_READ & SDA) ? EIO : 0;
	BB_SET(SDA      );
	delay(5);

	if (flags & I2C_F_STOP)
		(void) i2c_bitbang_send_stop(v, flags, ops);

	return (error);
}
