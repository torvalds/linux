/*-
 * Copyright (c) 2008-2009 Robert N. M. Watson
 * Copyright (c) 2011 Jonathan Anderson
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

/*
 * Test routines to make sure a variety of system calls are or are not
 * available in capability mode.  The goal is not to see if they work, just
 * whether or not they return the expected ECAPMODE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <machine/sysarch.h>

#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cap_test.h"

#define	CHECK_SYSCALL_VOID_NOT_ECAPMODE(syscall, ...)	do {		\
	errno = 0;							\
	(void)syscall(__VA_ARGS__);					\
	if (errno == ECAPMODE)						\
		FAIL("capmode: %s failed with ECAPMODE", #syscall);	\
} while (0)

int
test_capmode(void)
{
	struct statfs statfs;
	struct stat sb;
	long sysarch_arg = 0;
	int fd_close, fd_dir, fd_file, fd_socket, fd2[2];
	int success = PASSED;
	pid_t pid, wpid;
	char ch;

	/* Open some files to play with. */
	REQUIRE(fd_file = open("/tmp/cap_capmode", O_RDWR|O_CREAT, 0644));
	REQUIRE(fd_close = open("/dev/null", O_RDWR));
	REQUIRE(fd_dir = open("/tmp", O_RDONLY));
	REQUIRE(fd_socket = socket(PF_INET, SOCK_DGRAM, 0));

	/* Enter capability mode. */
	REQUIRE(cap_enter());

	/*
	 * System calls that are not permitted in capability mode.
	 */
	CHECK_CAPMODE(access, "/tmp/cap_capmode_access", F_OK);
	CHECK_CAPMODE(acct, "/tmp/cap_capmode_acct");
	CHECK_CAPMODE(bind, PF_INET, NULL, 0);
	CHECK_CAPMODE(chdir, "/tmp/cap_capmode_chdir");
	CHECK_CAPMODE(chflags, "/tmp/cap_capmode_chflags", UF_NODUMP);
	CHECK_CAPMODE(chmod, "/tmp/cap_capmode_chmod", 0644);
	CHECK_CAPMODE(chown, "/tmp/cap_capmode_chown", -1, -1);
	CHECK_CAPMODE(chroot, "/tmp/cap_capmode_chroot");
	CHECK_CAPMODE(connect, PF_INET, NULL, 0);
	CHECK_CAPMODE(creat, "/tmp/cap_capmode_creat", 0644);
	CHECK_CAPMODE(fchdir, fd_dir);
	CHECK_CAPMODE(getfsstat, &statfs, sizeof(statfs), MNT_NOWAIT);
	CHECK_CAPMODE(link, "/tmp/foo", "/tmp/bar");
	CHECK_CAPMODE(lstat, "/tmp/cap_capmode_lstat", &sb);
	CHECK_CAPMODE(mknod, "/tmp/capmode_mknod", 06440, 0);
	CHECK_CAPMODE(mount, "procfs", "/not_mounted", 0, NULL);
	CHECK_CAPMODE(open, "/dev/null", O_RDWR);
	CHECK_CAPMODE(readlink, "/tmp/cap_capmode_readlink", NULL, 0);
	CHECK_CAPMODE(revoke, "/tmp/cap_capmode_revoke");
	CHECK_CAPMODE(stat, "/tmp/cap_capmode_stat", &sb);
	CHECK_CAPMODE(symlink,
	    "/tmp/cap_capmode_symlink_from",
	    "/tmp/cap_capmode_symlink_to");
	CHECK_CAPMODE(unlink, "/tmp/cap_capmode_unlink");
	CHECK_CAPMODE(unmount, "/not_mounted", 0);

	/*
	 * System calls that are permitted in capability mode.
	 */
	CHECK_SYSCALL_SUCCEEDS(close, fd_close);
	CHECK_SYSCALL_SUCCEEDS(dup, fd_file);
	CHECK_SYSCALL_SUCCEEDS(fstat, fd_file, &sb);
	CHECK_SYSCALL_SUCCEEDS(lseek, fd_file, 0, SEEK_SET);
	CHECK_SYSCALL_SUCCEEDS(msync, &fd_file, 8192, MS_ASYNC);
	CHECK_SYSCALL_SUCCEEDS(profil, NULL, 0, 0, 0);
	CHECK_SYSCALL_SUCCEEDS(read, fd_file, &ch, sizeof(ch));
	CHECK_SYSCALL_SUCCEEDS(recvfrom, fd_socket, NULL, 0, 0, NULL, NULL);
	CHECK_SYSCALL_SUCCEEDS(setuid, getuid());
	CHECK_SYSCALL_SUCCEEDS(write, fd_file, &ch, sizeof(ch));

	/*
	 * These calls will fail for lack of e.g. a proper name to send to,
	 * but they are allowed in capability mode, so errno != ECAPMODE.
	 */
	CHECK_NOT_CAPMODE(accept, fd_socket, NULL, NULL);
	CHECK_NOT_CAPMODE(getpeername, fd_socket, NULL, NULL);
	CHECK_NOT_CAPMODE(getsockname, fd_socket, NULL, NULL);
	CHECK_NOT_CAPMODE(fchflags, fd_file, UF_NODUMP);
	CHECK_NOT_CAPMODE(recvmsg, fd_socket, NULL, 0);
	CHECK_NOT_CAPMODE(sendmsg, fd_socket, NULL, 0);
	CHECK_NOT_CAPMODE(sendto, fd_socket, NULL, 0, 0, NULL, 0);

	/*
	 * System calls which should be allowed in capability mode, but which
	 * don't return errors, and are thus difficult to check.
	 *
	 * We will try anyway, by checking errno.
	 */
	CHECK_SYSCALL_VOID_NOT_ECAPMODE(getegid);
	CHECK_SYSCALL_VOID_NOT_ECAPMODE(geteuid);
	CHECK_SYSCALL_VOID_NOT_ECAPMODE(getgid);
	CHECK_SYSCALL_VOID_NOT_ECAPMODE(getpid);
	CHECK_SYSCALL_VOID_NOT_ECAPMODE(getppid);
	CHECK_SYSCALL_VOID_NOT_ECAPMODE(getuid);

	/*
	 * Finally, tests for system calls that don't fit the pattern very well.
	 */
	pid = fork();
	if (pid >= 0) {
		if (pid == 0) {
			exit(0);
		} else if (pid > 0) {
			wpid = waitpid(pid, NULL, 0);
			if (wpid < 0) {
				if (errno != ECAPMODE)
					FAIL("capmode:waitpid");
			} else
				FAIL("capmode:waitpid succeeded");
		}
	} else
		FAIL("capmode:fork");

	if (getlogin() == NULL)
		FAIL("test_sycalls:getlogin %d", errno);

	if (getsockname(fd_socket, NULL, NULL) < 0) {
		if (errno == ECAPMODE)
			FAIL("capmode:getsockname");
	}

	/* XXXRW: ktrace */

	if (pipe(fd2) == 0) {
		close(fd2[0]);
		close(fd2[1]);
	} else if (errno == ECAPMODE)
		FAIL("capmode:pipe");

	/* XXXRW: ptrace. */

	/* sysarch() is, by definition, architecture-dependent */
#if defined (__amd64__) || defined (__i386__)
	CHECK_CAPMODE(sysarch, I386_SET_IOPERM, &sysarch_arg);
#else
	/* XXXJA: write a test for arm */
	FAIL("capmode:no sysarch() test for current architecture");
#endif

	/* XXXRW: No error return from sync(2) to test. */

	return (success);
}
