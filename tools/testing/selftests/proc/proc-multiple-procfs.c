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
#include <stdlib.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>

int main(void)
{
	struct stat proc_st1, proc_st2;
	char procbuff[] = "/tmp/proc.XXXXXX/meminfo";
	char procdir1[] = "/tmp/proc.XXXXXX";
	char procdir2[] = "/tmp/proc.XXXXXX";

	assert(mkdtemp(procdir1) != NULL);
	assert(mkdtemp(procdir2) != NULL);

	assert(!mount("proc", procdir1, "proc", 0, "hidepid=1"));
	assert(!mount("proc", procdir2, "proc", 0, "hidepid=2"));

	snprintf(procbuff, sizeof(procbuff), "%s/meminfo", procdir1);
	assert(!stat(procbuff, &proc_st1));

	snprintf(procbuff, sizeof(procbuff), "%s/meminfo", procdir2);
	assert(!stat(procbuff, &proc_st2));

	umount(procdir1);
	umount(procdir2);

	assert(proc_st1.st_dev != proc_st2.st_dev);

	return 0;
}
