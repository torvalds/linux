/*
 * Copyright © 2020 Alexey Gladkov <gladkov.alexey@gmail.com>
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
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <linux/mount.h>
#include <linux/unistd.h>

static inline int fsopen(const char *fsname, unsigned int flags)
{
	return syscall(__NR_fsopen, fsname, flags);
}

static inline int fsconfig(int fd, unsigned int cmd, const char *key, const void *val, int aux)
{
	return syscall(__NR_fsconfig, fd, cmd, key, val, aux);
}

int main(void)
{
	int fsfd, ret;
	int hidepid = 2;

	assert((fsfd = fsopen("proc", 0)) != -1);

	ret = fsconfig(fsfd, FSCONFIG_SET_BINARY, "hidepid", &hidepid, 0);
	assert(ret == -1);
	assert(errno == EINVAL);

	assert(!fsconfig(fsfd, FSCONFIG_SET_STRING, "hidepid", "2", 0));
	assert(!fsconfig(fsfd, FSCONFIG_SET_STRING, "hidepid", "invisible", 0));

	assert(!close(fsfd));

	return 0;
}
