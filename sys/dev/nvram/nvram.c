/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Peter Wemm
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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/sx.h>
#include <sys/uio.h>
#include <sys/module.h>

#include <isa/rtc.h>

/*
 * Linux-style /dev/nvram driver
 *
 * cmos ram starts at bytes 14 through 128, for a total of 114 bytes.
 * The driver exposes byte 14 as file offset 0.
 *
 * Offsets 2 through 31 are checksummed at offset 32, 33.
 * In order to avoid the possibility of making the machine unbootable at the
 * bios level (press F1 to continue!), we refuse to allow writes if we do
 * not see a pre-existing valid checksum.  If the existing sum is invalid,
 * then presumably we do not know how to make a sum that the bios will accept.
 */

#define NVRAM_FIRST	RTC_DIAG	/* 14 */
#define NVRAM_LAST	128

#define CKSUM_FIRST	2
#define CKSUM_LAST	31
#define CKSUM_MSB	32
#define CKSUM_LSB	33

static d_open_t		nvram_open;
static d_read_t		nvram_read;
static d_write_t	nvram_write;

static struct cdev *nvram_dev;
static struct sx nvram_lock;

static struct cdevsw nvram_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	nvram_open,
	.d_read =	nvram_read,
	.d_write =	nvram_write,
	.d_name =	"nvram",
};

static int
nvram_open(struct cdev *dev __unused, int flags, int fmt __unused,
    struct thread *td)
{
	int error = 0;

	if (flags & FWRITE)
		error = securelevel_gt(td->td_ucred, 0);

	return (error);
}

static int
nvram_read(struct cdev *dev, struct uio *uio, int flags)
{
	int nv_off;
	u_char v;
	int error = 0;

	while (uio->uio_resid > 0 && error == 0) {
		nv_off = uio->uio_offset + NVRAM_FIRST;
		if (nv_off < NVRAM_FIRST || nv_off >= NVRAM_LAST)
			return (0);	/* Signal EOF */
		/* Single byte at a time */
		v = rtcin(nv_off);
		error = uiomove(&v, 1, uio);
	}
	return (error);

}

static int
nvram_write(struct cdev *dev, struct uio *uio, int flags)
{
	int nv_off;
	u_char v;
	int error = 0;
	int i;
	uint16_t sum;

	sx_xlock(&nvram_lock);

	/* Assert that we understand the existing checksum first!  */
	sum = rtcin(NVRAM_FIRST + CKSUM_MSB) << 8 |
	      rtcin(NVRAM_FIRST + CKSUM_LSB);
	for (i = CKSUM_FIRST; i <= CKSUM_LAST; i++)
		sum -= rtcin(NVRAM_FIRST + i);
	if (sum != 0) {
		sx_xunlock(&nvram_lock);
		return (EIO);
	}
	/* Bring in user data and write */
	while (uio->uio_resid > 0 && error == 0) {
		nv_off = uio->uio_offset + NVRAM_FIRST;
		if (nv_off < NVRAM_FIRST || nv_off >= NVRAM_LAST) {
			sx_xunlock(&nvram_lock);
			return (0);	/* Signal EOF */
		}
		/* Single byte at a time */
		error = uiomove(&v, 1, uio);
		writertc(nv_off, v);
	}
	/* Recalculate checksum afterwards */
	sum = 0;
	for (i = CKSUM_FIRST; i <= CKSUM_LAST; i++)
		sum += rtcin(NVRAM_FIRST + i);
	writertc(NVRAM_FIRST + CKSUM_MSB, sum >> 8);
	writertc(NVRAM_FIRST + CKSUM_LSB, sum);
	sx_xunlock(&nvram_lock);
	return (error);
}

static int
nvram_modevent(module_t mod __unused, int type, void *data __unused)
{
	switch (type) {
	case MOD_LOAD:
		sx_init(&nvram_lock, "nvram");
		nvram_dev = make_dev(&nvram_cdevsw, 0,
		    UID_ROOT, GID_KMEM, 0640, "nvram");
		break;
	case MOD_UNLOAD:
	case MOD_SHUTDOWN:
		destroy_dev(nvram_dev);
		sx_destroy(&nvram_lock);
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}
DEV_MODULE(nvram, nvram_modevent, NULL);
