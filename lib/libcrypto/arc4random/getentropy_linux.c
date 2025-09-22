/*	$OpenBSD: getentropy_linux.c,v 1.48 2021/10/24 21:24:20 deraadt Exp $	*/

/*
 * Copyright (c) 2014 Theo de Raadt <deraadt@openbsd.org>
 * Copyright (c) 2014 Bob Beck <beck@obtuse.com>
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
 *
 * Emulation of getentropy(2) as documented at:
 * http://man.openbsd.org/getentropy.2
 */

#define	_POSIX_C_SOURCE	199309L
#define	_GNU_SOURCE	1
#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#ifdef SYS__sysctl
#include <linux/sysctl.h>
#endif
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <link.h>
#include <termios.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <openssl/sha.h>

#include <linux/types.h>
#include <linux/random.h>
#ifdef HAVE_GETAUXVAL
#include <sys/auxv.h>
#endif
#include <sys/vfs.h>

#define REPEAT 5
#define MINIMUM(a, b) (((a) < (b)) ? (a) : (b))

#define HX(a, b) \
	do { \
		if ((a)) \
			HD(errno); \
		else \
			HD(b); \
	} while (0)

#define HR(x, l) (SHA512_Update(&ctx, (char *)(x), (l)))
#define HD(x)	 (SHA512_Update(&ctx, (char *)&(x), sizeof (x)))
#define HF(x)    (SHA512_Update(&ctx, (char *)&(x), sizeof (void*)))

int	getentropy(void *buf, size_t len);

#if defined(SYS_getrandom) && defined(GRND_NONBLOCK)
static int getentropy_getrandom(void *buf, size_t len);
#endif
static int getentropy_urandom(void *buf, size_t len);
#ifdef SYS__sysctl
static int getentropy_sysctl(void *buf, size_t len);
#endif
static int getentropy_fallback(void *buf, size_t len);
static int getentropy_phdr(struct dl_phdr_info *info, size_t size, void *data);

int
getentropy(void *buf, size_t len)
{
	int ret = -1;

	if (len > 256) {
		errno = EIO;
		return (-1);
	}

#if defined(SYS_getrandom) && defined(GRND_NONBLOCK)
	/*
	 * Try descriptor-less getrandom(), in non-blocking mode.
	 *
	 * The design of Linux getrandom is broken.  It has an
	 * uninitialized phase coupled with blocking behaviour, which
	 * is unacceptable from within a library at boot time without
	 * possible recovery. See http://bugs.python.org/issue26839#msg267745
	 */
	ret = getentropy_getrandom(buf, len);
	if (ret != -1)
		return (ret);
#endif

	/*
	 * Try to get entropy with /dev/urandom
	 *
	 * This can fail if the process is inside a chroot or if file
	 * descriptors are exhausted.
	 */
	ret = getentropy_urandom(buf, len);
	if (ret != -1)
		return (ret);

#ifdef SYS__sysctl
	/*
	 * Try to use sysctl CTL_KERN, KERN_RANDOM, RANDOM_UUID.
	 * sysctl is a failsafe API, so it guarantees a result.  This
	 * should work inside a chroot, or when file descriptors are
	 * exhausted.
	 *
	 * However this can fail if the Linux kernel removes support
	 * for sysctl.  Starting in 2007, there have been efforts to
	 * deprecate the sysctl API/ABI, and push callers towards use
	 * of the chroot-unavailable fd-using /proc mechanism --
	 * essentially the same problems as /dev/urandom.
	 *
	 * Numerous setbacks have been encountered in their deprecation
	 * schedule, so as of June 2014 the kernel ABI still exists on
	 * most Linux architectures. The sysctl() stub in libc is missing
	 * on some systems.  There are also reports that some kernels
	 * spew messages to the console.
	 */
	ret = getentropy_sysctl(buf, len);
	if (ret != -1)
		return (ret);
#endif /* SYS__sysctl */

	/*
	 * Entropy collection via /dev/urandom and sysctl have failed.
	 *
	 * No other API exists for collecting entropy.  See the large
	 * comment block above.
	 *
	 * We have very few options:
	 *     - Even syslog_r is unsafe to call at this low level, so
	 *	 there is no way to alert the user or program.
	 *     - Cannot call abort() because some systems have unsafe
	 *	 corefiles.
	 *     - Could raise(SIGKILL) resulting in silent program termination.
	 *     - Return EIO, to hint that arc4random's stir function
	 *       should raise(SIGKILL)
	 *     - Do the best under the circumstances....
	 *
	 * This code path exists to bring light to the issue that Linux
	 * still does not provide a failsafe API for entropy collection.
	 *
	 * We hope this demonstrates that Linux should either retain their
	 * sysctl ABI, or consider providing a new failsafe API which
	 * works in a chroot or when file descriptors are exhausted.
	 */
#undef FAIL_INSTEAD_OF_TRYING_FALLBACK
#ifdef FAIL_INSTEAD_OF_TRYING_FALLBACK
	raise(SIGKILL);
#endif
	ret = getentropy_fallback(buf, len);
	if (ret != -1)
		return (ret);

	errno = EIO;
	return (ret);
}

#if defined(SYS_getrandom) && defined(GRND_NONBLOCK)
static int
getentropy_getrandom(void *buf, size_t len)
{
	int pre_errno = errno;
	int ret;
	if (len > 256)
		return (-1);
	do {
		ret = syscall(SYS_getrandom, buf, len, GRND_NONBLOCK);
	} while (ret == -1 && errno == EINTR);

	if (ret != len)
		return (-1);
	errno = pre_errno;
	return (0);
}
#endif

static int
getentropy_urandom(void *buf, size_t len)
{
	struct stat st;
	size_t i;
	int fd, cnt, flags;
	int save_errno = errno;

start:

	flags = O_RDONLY;
#ifdef O_NOFOLLOW
	flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif
	fd = open("/dev/urandom", flags);
	if (fd == -1) {
		if (errno == EINTR)
			goto start;
		goto nodevrandom;
	}
#ifndef O_CLOEXEC
	fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
#endif

	/* Lightly verify that the device node looks sane */
	if (fstat(fd, &st) == -1 || !S_ISCHR(st.st_mode)) {
		close(fd);
		goto nodevrandom;
	}
	if (ioctl(fd, RNDGETENTCNT, &cnt) == -1) {
		close(fd);
		goto nodevrandom;
	}
	for (i = 0; i < len; ) {
		size_t wanted = len - i;
		ssize_t ret = read(fd, (char *)buf + i, wanted);

		if (ret == -1) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			close(fd);
			goto nodevrandom;
		}
		i += ret;
	}
	close(fd);
	errno = save_errno;
	return (0);		/* satisfied */
nodevrandom:
	errno = EIO;
	return (-1);
}

#ifdef SYS__sysctl
static int
getentropy_sysctl(void *buf, size_t len)
{
	static int mib[] = { CTL_KERN, KERN_RANDOM, RANDOM_UUID };
	size_t i;
	int save_errno = errno;

	for (i = 0; i < len; ) {
		size_t chunk = MINIMUM(len - i, 16);

		/* SYS__sysctl because some systems already removed sysctl() */
		struct __sysctl_args args = {
			.name = mib,
			.nlen = 3,
			.oldval = (char *)buf + i,
			.oldlenp = &chunk,
		};
		if (syscall(SYS__sysctl, &args) != 0)
			goto sysctlfailed;
		i += chunk;
	}
	errno = save_errno;
	return (0);			/* satisfied */
sysctlfailed:
	errno = EIO;
	return (-1);
}
#endif /* SYS__sysctl */

static const int cl[] = {
	CLOCK_REALTIME,
#ifdef CLOCK_MONOTONIC
	CLOCK_MONOTONIC,
#endif
#ifdef CLOCK_MONOTONIC_RAW
	CLOCK_MONOTONIC_RAW,
#endif
#ifdef CLOCK_TAI
	CLOCK_TAI,
#endif
#ifdef CLOCK_VIRTUAL
	CLOCK_VIRTUAL,
#endif
#ifdef CLOCK_UPTIME
	CLOCK_UPTIME,
#endif
#ifdef CLOCK_PROCESS_CPUTIME_ID
	CLOCK_PROCESS_CPUTIME_ID,
#endif
#ifdef CLOCK_THREAD_CPUTIME_ID
	CLOCK_THREAD_CPUTIME_ID,
#endif
};

static int
getentropy_phdr(struct dl_phdr_info *info, size_t size, void *data)
{
	SHA512_CTX *ctx = data;

	SHA512_Update(ctx, &info->dlpi_addr, sizeof (info->dlpi_addr));
	return (0);
}

static int
getentropy_fallback(void *buf, size_t len)
{
	uint8_t results[SHA512_DIGEST_LENGTH];
	int save_errno = errno, e, pgs = getpagesize(), faster = 0, repeat;
	static int cnt;
	struct timespec ts;
	struct timeval tv;
	struct rusage ru;
	sigset_t sigset;
	struct stat st;
	SHA512_CTX ctx;
	static pid_t lastpid;
	pid_t pid;
	size_t i, ii, m;
	char *p;

	pid = getpid();
	if (lastpid == pid) {
		faster = 1;
		repeat = 2;
	} else {
		faster = 0;
		lastpid = pid;
		repeat = REPEAT;
	}
	for (i = 0; i < len; ) {
		int j;
		SHA512_Init(&ctx);
		for (j = 0; j < repeat; j++) {
			HX((e = gettimeofday(&tv, NULL)) == -1, tv);
			if (e != -1) {
				cnt += (int)tv.tv_sec;
				cnt += (int)tv.tv_usec;
			}

			dl_iterate_phdr(getentropy_phdr, &ctx);

			for (ii = 0; ii < sizeof(cl)/sizeof(cl[0]); ii++)
				HX(clock_gettime(cl[ii], &ts) == -1, ts);

			HX((pid = getpid()) == -1, pid);
			HX((pid = getsid(pid)) == -1, pid);
			HX((pid = getppid()) == -1, pid);
			HX((pid = getpgid(0)) == -1, pid);
			HX((e = getpriority(0, 0)) == -1, e);

			if (!faster) {
				ts.tv_sec = 0;
				ts.tv_nsec = 1;
				(void) nanosleep(&ts, NULL);
			}

			HX(sigpending(&sigset) == -1, sigset);
			HX(sigprocmask(SIG_BLOCK, NULL, &sigset) == -1,
			    sigset);

			HF(getentropy);	/* an addr in this library */
			HF(printf);		/* an addr in libc */
			p = (char *)&p;
			HD(p);		/* an addr on stack */
			p = (char *)&errno;
			HD(p);		/* the addr of errno */

			if (i == 0) {
				struct sockaddr_storage ss;
				struct statvfs stvfs;
				struct termios tios;
				struct statfs stfs;
				socklen_t ssl;
				off_t off;

				/*
				 * Prime-sized mappings encourage fragmentation;
				 * thus exposing some address entropy.
				 */
				struct mm {
					size_t	npg;
					void	*p;
				} mm[] =	 {
					{ 17, MAP_FAILED }, { 3, MAP_FAILED },
					{ 11, MAP_FAILED }, { 2, MAP_FAILED },
					{ 5, MAP_FAILED }, { 3, MAP_FAILED },
					{ 7, MAP_FAILED }, { 1, MAP_FAILED },
					{ 57, MAP_FAILED }, { 3, MAP_FAILED },
					{ 131, MAP_FAILED }, { 1, MAP_FAILED },
				};

				for (m = 0; m < sizeof mm/sizeof(mm[0]); m++) {
					HX(mm[m].p = mmap(NULL,
					    mm[m].npg * pgs,
					    PROT_READ|PROT_WRITE,
					    MAP_PRIVATE|MAP_ANON, -1,
					    (off_t)0), mm[m].p);
					if (mm[m].p != MAP_FAILED) {
						size_t mo;

						/* Touch some memory... */
						p = mm[m].p;
						mo = cnt %
						    (mm[m].npg * pgs - 1);
						p[mo] = 1;
						cnt += (int)((long)(mm[m].p)
						    / pgs);
					}

					/* Check cnts and times... */
					for (ii = 0; ii < sizeof(cl)/sizeof(cl[0]);
					    ii++) {
						HX((e = clock_gettime(cl[ii],
						    &ts)) == -1, ts);
						if (e != -1)
							cnt += (int)ts.tv_nsec;
					}

					HX((e = getrusage(RUSAGE_SELF,
					    &ru)) == -1, ru);
					if (e != -1) {
						cnt += (int)ru.ru_utime.tv_sec;
						cnt += (int)ru.ru_utime.tv_usec;
					}
				}

				for (m = 0; m < sizeof mm/sizeof(mm[0]); m++) {
					if (mm[m].p != MAP_FAILED)
						munmap(mm[m].p, mm[m].npg * pgs);
					mm[m].p = MAP_FAILED;
				}

				HX(stat(".", &st) == -1, st);
				HX(statvfs(".", &stvfs) == -1, stvfs);
				HX(statfs(".", &stfs) == -1, stfs);

				HX(stat("/", &st) == -1, st);
				HX(statvfs("/", &stvfs) == -1, stvfs);
				HX(statfs("/", &stfs) == -1, stfs);

				HX((e = fstat(0, &st)) == -1, st);
				if (e == -1) {
					if (S_ISREG(st.st_mode) ||
					    S_ISFIFO(st.st_mode) ||
					    S_ISSOCK(st.st_mode)) {
						HX(fstatvfs(0, &stvfs) == -1,
						    stvfs);
						HX(fstatfs(0, &stfs) == -1,
						    stfs);
						HX((off = lseek(0, (off_t)0,
						    SEEK_CUR)) < 0, off);
					}
					if (S_ISCHR(st.st_mode)) {
						HX(tcgetattr(0, &tios) == -1,
						    tios);
					} else if (S_ISSOCK(st.st_mode)) {
						memset(&ss, 0, sizeof ss);
						ssl = sizeof(ss);
						HX(getpeername(0,
						    (void *)&ss, &ssl) == -1,
						    ss);
					}
				}

				HX((e = getrusage(RUSAGE_CHILDREN,
				    &ru)) == -1, ru);
				if (e != -1) {
					cnt += (int)ru.ru_utime.tv_sec;
					cnt += (int)ru.ru_utime.tv_usec;
				}
			} else {
				/* Subsequent hashes absorb previous result */
				HD(results);
			}

			HX((e = gettimeofday(&tv, NULL)) == -1, tv);
			if (e != -1) {
				cnt += (int)tv.tv_sec;
				cnt += (int)tv.tv_usec;
			}

			HD(cnt);
		}
#ifdef HAVE_GETAUXVAL
#ifdef AT_RANDOM
		/* Not as random as you think but we take what we are given */
		p = (char *) getauxval(AT_RANDOM);
		if (p)
			HR(p, 16);
#endif
#ifdef AT_SYSINFO_EHDR
		p = (char *) getauxval(AT_SYSINFO_EHDR);
		if (p)
			HR(p, pgs);
#endif
#ifdef AT_BASE
		p = (char *) getauxval(AT_BASE);
		if (p)
			HD(p);
#endif
#endif

		SHA512_Final(results, &ctx);
		memcpy((char *)buf + i, results, MINIMUM(sizeof(results), len - i));
		i += MINIMUM(sizeof(results), len - i);
	}
	explicit_bzero(&ctx, sizeof ctx);
	explicit_bzero(results, sizeof results);
	errno = save_errno;
	return (0);		/* satisfied */
}
