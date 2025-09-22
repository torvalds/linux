/*	$OpenBSD: conf.h,v 1.7 2022/06/28 14:43:50 visa Exp $	*/
/*
 * Copyright (c) 2004, Miodrag Vallat.
 * All rights reserved.
 *
 * Permission to redistribute, use, copy, and modify this software
 * is hereby granted without fee, provided that the following
 * conditions are met:
 *
 * 1. This entire notice is included in all source code copies of any
 *    software which is or includes a copy or modification of this
 *    software.
 * 2. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/conf.h>

#define	mmread	mmrw
#define	mmwrite	mmrw
cdev_decl(mm);

cdev_decl(sio);

cdev_decl(lcd);

cdev_decl(pcex);

cdev_decl(xp);

/* devices on PCMCIA */
/* block devices */
bdev_decl(wd);
/* character devices */
cdev_decl(com);
cdev_decl(wd);

/* open, close, write, ioctl */
#define	cdev_lcd_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), \
	(dev_type_read((*))) enodev, dev_init(c,n,write), \
	dev_init(c,n,ioctl), (dev_type_stop((*))) enodev, \
	0, (dev_type_mmap((*))) enodev, 0, 0, seltrue_kqfilter }

/* open, close, ioctl, mmap */
#define cdev_pcex_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	dev_init(c,n,mmap) }

/* open, close, ioctl, mmap */
#define cdev_xp_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	dev_init(c,n,mmap) }
