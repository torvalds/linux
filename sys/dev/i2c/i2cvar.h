/*	$OpenBSD: i2cvar.h,v 1.19 2022/08/31 15:14:01 kettenis Exp $	*/
/*	$NetBSD: i2cvar.h,v 1.1 2003/09/30 00:35:31 thorpej Exp $	*/

/*
 * Copyright (c) 2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Steve C. Woodford and Jason R. Thorpe for Wasabi Systems, Inc.
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

#ifndef _DEV_I2C_I2CVAR_H_
#define	_DEV_I2C_I2CVAR_H_

#include <dev/i2c/i2c_io.h>

struct device;

/* Flags passed to i2c routines. */
#define	I2C_F_WRITE		0x00	/* new transfer is a write */
#define	I2C_F_READ		0x01	/* new transfer is a read */
#define	I2C_F_LAST		0x02	/* last byte of read */
#define	I2C_F_STOP		0x04	/* send stop after byte */
#define	I2C_F_POLL		0x08	/* poll, don't sleep */

/*
 * This structure provides the interface between the i2c framework
 * and the underlying i2c controller.
 *
 * Note that this structure is designed specifically to allow us
 * to either use the autoconfiguration framework or not.  This
 * allows a driver for a board with a private i2c bus use generic
 * i2c client drivers for chips that might be on that board.
 */
typedef struct i2c_controller {
	void	*ic_cookie;		/* controller private */

	/*
	 * These provide synchronization in the presence of
	 * multiple users of the i2c bus.  When a device
	 * driver wishes to perform transfers on the i2c
	 * bus, the driver should acquire the bus.  When
	 * the driver is finished, it should release the
	 * bus.
	 *
	 * This is provided by the back-end since a single
	 * controller may present e.g. i2c and smbus views
	 * of the same set of i2c wires.
	 */
	int	(*ic_acquire_bus)(void *, int);
	void	(*ic_release_bus)(void *, int);

	/*
	 * The preferred API for clients of the i2c interface
	 * is the scripted API.  This handles i2c controllers
	 * that do not provide raw access to the i2c signals.
	 */
	int	(*ic_exec)(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
		    void *, size_t, int);

	int	(*ic_send_start)(void *, int);
	int	(*ic_send_stop)(void *, int);
	int	(*ic_initiate_xfer)(void *, i2c_addr_t, int);
	int	(*ic_read_byte)(void *, uint8_t *, int);
	int	(*ic_write_byte)(void *, uint8_t, int);

	void	*(*ic_intr_establish)(void *, void *, int, int (*)(void *),
		    void *, const char *);
	void	(*ic_intr_disestablish)(void *, void *);
	const char *(*ic_intr_string)(void *, void *);
} *i2c_tag_t;

/* Used to attach the i2c framework to the controller. */
struct i2cbus_attach_args {
	const char *iba_name;		/* bus name ("iic") */
	i2c_tag_t iba_tag;		/* the controller */
	void	(*iba_bus_scan)(struct device *, struct i2cbus_attach_args *,
		    void *);
	void	*iba_bus_scan_arg;
};

/* Used to attach devices on the i2c bus. */
struct i2c_attach_args {
	i2c_tag_t	ia_tag;		/* our controller */
	i2c_addr_t	ia_addr;	/* address of device */
	int		ia_size;	/* size (for EEPROMs) */
	char		*ia_name;	/* chip name */
	size_t		ia_namelen;	/* length of name concatenation */
	void		*ia_cookie;	/* pass extra info from bus to dev */
	void		*ia_intr;	/* interrupt info */
	int		ia_poll;	/* to force polling */
};

/*
 * API presented to i2c controllers.
 */
int	iicbus_print(void *, const char *);

#ifdef _I2C_PRIVATE
/*
 * Macros used internally by the i2c framework.
 */
#define	iic_send_start(ic, flags)					\
	(*(ic)->ic_send_start)((ic)->ic_cookie, (flags))
#define	iic_send_stop(ic, flags)					\
	(*(ic)->ic_send_stop)((ic)->ic_cookie, (flags))
#define	iic_initiate_xfer(ic, addr, flags)				\
	(*(ic)->ic_initiate_xfer)((ic)->ic_cookie, (addr), (flags))

#define	iic_read_byte(ic, bytep, flags)					\
	(*(ic)->ic_read_byte)((ic)->ic_cookie, (bytep), (flags))
#define	iic_write_byte(ic, byte, flags)					\
	(*(ic)->ic_write_byte)((ic)->ic_cookie, (byte), (flags))

void	iic_scan(struct device *, struct i2cbus_attach_args *);
int	iic_print(void *, const char *);
#endif /* _I2C_PRIVATE */

/*
 * Simplified API for clients of the i2c framework.  Definitions
 * in <dev/i2c/i2c_io.h>.
 */
#define	iic_acquire_bus(ic, flags)					\
	(*(ic)->ic_acquire_bus)((ic)->ic_cookie, (flags))
#define	iic_release_bus(ic, flags)					\
	(*(ic)->ic_release_bus)((ic)->ic_cookie, (flags))

int	iic_exec(i2c_tag_t, i2c_op_t, i2c_addr_t, const void *,
	    size_t, void *, size_t, int);

int	iic_smbus_write_byte(i2c_tag_t, i2c_addr_t, uint8_t, uint8_t, int);
int	iic_smbus_read_byte(i2c_tag_t, i2c_addr_t, uint8_t, uint8_t *, int);
int	iic_smbus_receive_byte(i2c_tag_t, i2c_addr_t, uint8_t *, int);

#define iic_intr_establish(ic, ih, level, func, arg, name)		\
	(*(ic)->ic_intr_establish)((ic)->ic_cookie, (ih), (level),	\
	    (func), (arg), (name))
#define iic_intr_disestablish(ic, ih)					\
	(*(ic)->ic_intr_disestablish)((ic)->ic_cookie, (ih))
#define iic_intr_string(ic, ih)						\
	(*(ic)->ic_intr_string)((ic)->ic_cookie, (ih))

void	iic_ignore_addr(u_int8_t addr);
int	iic_is_compatible(const struct i2c_attach_args *, const char *);

#endif /* _DEV_I2C_I2CVAR_H_ */
