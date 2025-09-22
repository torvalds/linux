/* $OpenBSD: xpio.h,v 1.1 2016/12/17 05:22:34 aoyama Exp $ */
/* $NetBSD: xpio.h,v 1.1 2016/12/03 17:38:02 tsutsui Exp $ */

/*-
 * Copyright (c) 2016 Izumi Tsutsui.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * ioctl for LUNA's Hitachi HD647180 "XP" I/O processor driver
 */

#ifndef _LUNA88K_XPIO_H_
#define _LUNA88K_XPIO_H_

#include <sys/ioctl.h>
#include <sys/ioccom.h>

struct xp_download {
	u_int	size;		/* size to be downloaded to XP shared mem */
	u_char	*data;		/* pointer to code to be downloaded */
};
#define XPIOCDOWNLD	_IOW('x', 1, struct xp_download)

#endif /* _LUNA88K_XPIO_H_ */
