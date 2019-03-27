/* 
 * $FreeBSD$
 *
 * Copyright (c) 2011, 2012, 2013, 2015, Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/mac.h>
#include <sys/stat.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <paths.h>

#include <security/mac_veriexec/mac_veriexec.h>

#include "libveriexec.h"


static int
check_fd_mode(int fd, unsigned int mask)
{
	struct stat st;

	if (fstat(fd, &st) < 0)
		return errno;

	if ((st.st_mode & mask) == 0)
		return EAUTH;

	return 0;
}

int
veriexec_check_fd_mode(int fd, unsigned int mask)
{
	int error;

	if (fd < 0) {
		errno = EINVAL;
		return -1;
	}

	error = mac_syscall(MAC_VERIEXEC_NAME, MAC_VERIEXEC_CHECK_FD_SYSCALL,
	    (void *)(intptr_t)fd);
	if (error == -1) {
		switch (errno) {
		case ENOSYS:	/* veriexec not loaded */
			error = 0;	/* ignore */
			break;
		}
	}
	if (mask && error == 0)
	    error = check_fd_mode(fd, mask);

	return (error);
}

int
veriexec_check_path_mode(const char *file, unsigned int mask)
{
	int error;

	if (!file) {
		errno = EINVAL;
		return -1;
	}

	if (mask) {
		int fd;

		if ((fd = open(file, O_RDONLY)) < 0)
			return errno;

		error = veriexec_check_fd_mode(fd, mask);
		close(fd);
		return error;
	}

	error = mac_syscall(MAC_VERIEXEC_NAME, MAC_VERIEXEC_CHECK_PATH_SYSCALL,
	    __DECONST(void *, file));
	if (error == -1) {
		switch (errno) {
		case ENOSYS:	/* veriexec not loaded */
			error = 0;	/* ignore */
			break;
		}
	}
	return (error);
}

int
veriexec_check_fd(int fd)
{
	return veriexec_check_fd_mode(fd, 0);
}

int
veriexec_check_path(const char *file)
{
	return veriexec_check_path_mode(file, 0);
}

#if defined(MAIN) || defined(UNIT_TEST)
int
main(int argc __unused, char *argv[] __unused)
{
	int error;
	int rc = 0;
    
	while (*++argv) {
		error = veriexec_check_path(*argv);
		if (error == -1) {
			rc = 1;
			warn("%s", *argv);
		}
	}
	exit(rc);
}
#endif
