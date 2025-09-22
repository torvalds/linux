/*	$OpenBSD: conf.h,v 1.28 2024/03/29 21:06:14 miod Exp $	*/
/*	$NetBSD: conf.h,v 1.9 2001/03/26 12:33:26 lukem Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define mmread mmrw
#define mmwrite mmrw
cdev_decl(mm);

cdev_decl(openprom);

/* open, close, ioctl */
#define cdev_openprom_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) nullop, 0, \
	(dev_type_mmap((*))) enodev }

cdev_decl(uperf);

/* open, close, ioctl */
#define cdev_uperf_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) nullop, 0, \
	(dev_type_mmap((*))) enodev }

cdev_decl(vdsp);

/* open, close, ioctl */
#define cdev_vdsp_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) nullop, 0, \
	(dev_type_mmap((*))) enodev }

#define	cdev_gen_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) nullop, \
	0, (dev_type_mmap((*))) enodev, \
	0, 0, dev_init(c,n,kqfilter) }

cdev_decl(zs);

bdev_decl(fd);
cdev_decl(fd);

cdev_decl(bpp);
cdev_decl(lpt);

bdev_decl(wd);
cdev_decl(wd);

cdev_decl(sabtty);

cdev_decl(pcons);
cdev_decl(vcons);
cdev_decl(vcctty);
cdev_decl(sbbc);

cdev_decl(vldcp);

cdev_decl(com);

cdev_decl(mtty);
cdev_decl(mbpp);
cdev_decl(stty);
cdev_decl(sbpp);

/* open, close, write, ioctl */
#define	cdev_bpp_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) nullop, \
	0, (dev_type_mmap((*))) enodev, 0, 0, seltrue_kqfilter }
