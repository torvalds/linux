/*	$OpenBSD: i2c_bitbang.h,v 1.3 2006/01/13 23:56:46 grange Exp $	*/
/*	$NetBSD: i2c_bitbang.h,v 1.1 2003/09/30 00:35:31 thorpej Exp $	*/

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

#ifndef _DEV_I2C_I2C_BITBANG_H_
#define	_DEV_I2C_I2C_BITBANG_H_

#define	I2C_BIT_SDA		0	/* SDA signal */
#define	I2C_BIT_SCL		1	/* SCL signal */
#define	I2C_BIT_OUTPUT		2	/* controller: SDA is output */
#define	I2C_BIT_INPUT		3	/* controller: SDA is input */
#define	I2C_NBITS		4

struct i2c_bitbang_ops {
	void		(*ibo_set_bits)(void *, uint32_t);
	void		(*ibo_set_dir)(void *, uint32_t);
	uint32_t	(*ibo_read_bits)(void *);
	uint32_t	ibo_bits[I2C_NBITS];
};

typedef const struct i2c_bitbang_ops *i2c_bitbang_ops_t;

int	i2c_bitbang_send_start(void *, int, i2c_bitbang_ops_t);
int	i2c_bitbang_send_stop(void *, int, i2c_bitbang_ops_t);
int	i2c_bitbang_initiate_xfer(void *, i2c_addr_t, int, i2c_bitbang_ops_t);
int	i2c_bitbang_read_byte(void *, uint8_t *, int, i2c_bitbang_ops_t);
int	i2c_bitbang_write_byte(void *, uint8_t, int, i2c_bitbang_ops_t);

#endif /* _DEV_I2C_I2C_BITBANG_H_ */
