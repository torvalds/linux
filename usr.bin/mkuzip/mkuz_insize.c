/*
 * Copyright (c) 2004-2016 Maxim Sobolev <sobomax@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>

#include "mkuz_cfg.h"
#include "mkuz_insize.h"

off_t
mkuz_get_insize(struct mkuz_cfg *cfp)
{
	int ffd;
	off_t ms;
	struct stat sb;
	struct statfs statfsbuf;

	if (fstat(cfp->fdr, &sb) != 0) {
		warn("fstat(%s)", cfp->iname);
		return (-1);
	}
	if ((sb.st_flags & SF_SNAPSHOT) != 0) {
		if (fstatfs(cfp->fdr, &statfsbuf) != 0) {
			warn("fstatfs(%s)", cfp->iname);
			return (-1);
		}
		ffd = open(statfsbuf.f_mntfromname, O_RDONLY);
		if (ffd < 0) {
			warn("open(%s, O_RDONLY)", statfsbuf.f_mntfromname);
			return (-1);
		}
		if (ioctl(ffd, DIOCGMEDIASIZE, &ms) < 0) {
			warn("ioctl(DIOCGMEDIASIZE)");
			close(ffd);
			return (-1);
		}
		close(ffd);
		sb.st_size = ms;
	} else if (S_ISCHR(sb.st_mode)) {
		if (ioctl(cfp->fdr, DIOCGMEDIASIZE, &ms) < 0) {
			warn("ioctl(DIOCGMEDIASIZE)");
			return (-1);
		}
		sb.st_size = ms;
	} else if (!S_ISREG(sb.st_mode)) {
		warnx("%s: not a character device or regular file\n",
			cfp->iname);
		return (-1);
	}
	return (sb.st_size);
}
