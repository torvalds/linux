/*	$OpenBSD: posix_pty.c,v 1.3 2019/01/25 00:19:25 millert Exp $	*/

/*
 * Copyright (c) 2012 Todd C. Miller <millert@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/tty.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
posix_openpt(int oflag)
{
	struct ptmget ptm;
	int fd, mfd = -1;

	/* User must specify O_RDWR in oflag. */
	if ((oflag & O_ACCMODE) != O_RDWR ||
	    (oflag & ~(O_ACCMODE | O_NOCTTY)) != 0) {
		errno = EINVAL;
		return -1;
	}

	/* Get pty master and slave (this API only uses the master). */
	fd = open(PATH_PTMDEV, O_RDWR);
	if (fd != -1) {
		if (ioctl(fd, PTMGET, &ptm) != -1) {
			close(ptm.sfd);
			mfd = ptm.cfd;
		}
		close(fd);
	}

	return mfd;
}

/*
 * Look up the name of the specified pty master fd.
 * Note that the name returned does *not* include the /dev/ prefix.
 * Returns the name on success and NULL on error, setting errno.
 */
static const char *
ptmname(int mfd)
{
	struct stat sb;
	const char *name;

	/* Make sure it is a pty master. */
	if (fstat(mfd, &sb) != 0)
		return NULL;
	if (!S_ISCHR(sb.st_mode)) {
		errno = EINVAL;
		return NULL;
	}
	name = devname(sb.st_rdev, S_IFCHR);
	if (strncmp(name, "pty", 3) != 0) {
		errno = EINVAL;
		return NULL;
	}
	return name;
}

/*
 * The PTMGET ioctl handles the mode and owner for us.
 */
int
grantpt(int mfd)
{
	return ptmname(mfd) ? 0 : -1;
}

/*
 * The PTMGET ioctl unlocks the pty master and slave for us.
 */
int
unlockpt(int mfd)
{
	return ptmname(mfd) ? 0 : -1;
}

/*
 * Look up the path of the slave pty that corresponds to the master fd.
 * Returns the path if successful or NULL on error.
 */
char *
ptsname(int mfd)
{
	const char *master;
	static char slave[sizeof(((struct ptmget *)NULL)->sn)];

	if ((master = ptmname(mfd)) == NULL)
		return NULL;

	/* Add /dev/ prefix and convert "pty" to "tty". */
	strlcpy(slave, _PATH_DEV, sizeof(slave));
	strlcat(slave, master, sizeof(slave));
	slave[sizeof(_PATH_DEV) - 1] = 't';

	return slave;
}
