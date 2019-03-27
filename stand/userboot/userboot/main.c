/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 1998,2000 Doug Rabson <dfr@freebsd.org>
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

#include <stand.h>
#include <string.h>
#include <setjmp.h>
#include <sys/disk.h>

#include "bootstrap.h"
#include "disk.h"
#include "libuserboot.h"

#if defined(USERBOOT_ZFS_SUPPORT)
#include "libzfs.h"

static void userboot_zfs_probe(void);
static int userboot_zfs_found;
#endif

/* Minimum version required */
#define	USERBOOT_VERSION	USERBOOT_VERSION_3

#define	LOADER_PATH		"/boot/loader"
#define	INTERP_MARKER		"$Interpreter:"

#define	MALLOCSZ		(64*1024*1024)

struct loader_callbacks *callbacks;
void *callbacks_arg;

static jmp_buf jb;

struct arch_switch archsw;	/* MI/MD interface boundary */

static void	extract_currdev(void);
static void	check_interpreter(void);

void
delay(int usec)
{

	CALLBACK(delay, usec);
}

void
exit(int v)
{

	CALLBACK(exit, v);
	longjmp(jb, 1);
}

static void
check_interpreter(void)
{
	struct stat st;
	size_t marklen, rdsize;
	const char *guest_interp, *my_interp;
	char *buf;
	int fd;

	/*
	 * If we can't stat(2) or open(2) LOADER_PATH, then we'll fail by
	 * simply letting us roll on with whatever interpreter we were compiled
	 * with.  This is likely not going to be an issue in reality.
	 */
	buf =  NULL;
	if (stat(LOADER_PATH, &st) != 0)
		return;
	if ((fd = open(LOADER_PATH, O_RDONLY)) < 0)
		return;

	rdsize = st.st_size;
	buf = malloc(rdsize);
	if (buf == NULL)
		goto out;
	if (read(fd, buf, rdsize) < rdsize)
		goto out;

	marklen = strlen(INTERP_MARKER);
	my_interp = bootprog_interp + marklen;

	/*
	 * Here we make the assumption that a loader binary without the
	 * interpreter marker is a 4th one.  All loader binaries going forward
	 * should have this properly specified, so our assumption should always
	 * be a good one.
	 */
	if ((guest_interp = memmem(buf, rdsize, INTERP_MARKER,
	    marklen)) != NULL)
		guest_interp += marklen;
	else
		guest_interp = "4th";

	/*
	 * The guest interpreter may not have a version of loader that
	 * specifies the interpreter installed.  If that's the case, we'll
	 * assume it's legacy (4th) and request a swap to that if we're
	 * a Lua-userboot.
	 */
	if (strcmp(my_interp, guest_interp) != 0)
		CALLBACK(swap_interpreter, guest_interp);
out:
	free(buf);
	close(fd);
	return;
}

void
loader_main(struct loader_callbacks *cb, void *arg, int version, int ndisks)
{
	static char mallocbuf[MALLOCSZ];
	char *var;
	int i;

	if (version < USERBOOT_VERSION)
		abort();

	callbacks = cb;
	callbacks_arg = arg;
	userboot_disk_maxunit = ndisks;

	/*
	 * initialise the heap as early as possible.  Once this is done,
	 * alloc() is usable.
	 */
	setheap((void *)mallocbuf, (void *)(mallocbuf + sizeof(mallocbuf)));

	/*
	 * Hook up the console
	 */
	cons_probe();

	printf("\n%s", bootprog_info);
#if 0
	printf("Memory: %ld k\n", memsize() / 1024);
#endif

	setenv("LINES", "24", 1);	/* optional */

	/*
	 * Set custom environment variables
	 */
	i = 0;
	while (1) {
		var = CALLBACK(getenv, i++);
		if (var == NULL)
			break;
		putenv(var);
	}

	archsw.arch_autoload = userboot_autoload;
	archsw.arch_getdev = userboot_getdev;
	archsw.arch_copyin = userboot_copyin;
	archsw.arch_copyout = userboot_copyout;
	archsw.arch_readin = userboot_readin;
#if defined(USERBOOT_ZFS_SUPPORT)
	archsw.arch_zfs_probe = userboot_zfs_probe;
#endif

	/*
	 * Initialise the block cache. Set the upper limit.
	 */
	bcache_init(32768, 512);
	/*
	 * March through the device switch probing for things.
	 */
	for (i = 0; devsw[i] != NULL; i++)
		if (devsw[i]->dv_init != NULL)
			(devsw[i]->dv_init)();

	extract_currdev();

	/*
	 * Checking the interpreter isn't worth the overhead unless we
	 * actually have the swap_interpreter callback, so we actually version
	 * check here rather than later on.
	 */
	if (version >= USERBOOT_VERSION_5)
		check_interpreter();

	if (setjmp(jb))
		return;

	interact();			/* doesn't return */

	exit(0);
}

/*
 * Set the 'current device' by (if possible) recovering the boot device as 
 * supplied by the initial bootstrap.
 */
static void
extract_currdev(void)
{
	struct disk_devdesc dev;
	struct devdesc *dd;
#if defined(USERBOOT_ZFS_SUPPORT)
	struct zfs_devdesc zdev;

	if (userboot_zfs_found) {
	
		/* Leave the pool/root guid's unassigned */
		bzero(&zdev, sizeof(zdev));
		zdev.dd.d_dev = &zfs_dev;
		
		init_zfs_bootenv(zfs_fmtdev(&zdev));
		dd = &zdev.dd;
	} else
#endif

	if (userboot_disk_maxunit > 0) {
		dev.dd.d_dev = &userboot_disk;
		dev.dd.d_unit = 0;
		dev.d_slice = D_SLICEWILD;
		dev.d_partition = D_PARTWILD;
		/*
		 * If we cannot auto-detect the partition type then
		 * access the disk as a raw device.
		 */
		if (dev.dd.d_dev->dv_open(NULL, &dev)) {
			dev.d_slice = D_SLICENONE;
			dev.d_partition = D_PARTNONE;
		}
		dd = &dev.dd;
	} else {
		dev.dd.d_dev = &host_dev;
		dev.dd.d_unit = 0;
		dd = &dev.dd;
	}

	env_setenv("currdev", EV_VOLATILE, userboot_fmtdev(dd),
	    userboot_setcurrdev, env_nounset);
	env_setenv("loaddev", EV_VOLATILE, userboot_fmtdev(dd),
	    env_noset, env_nounset);
}

#if defined(USERBOOT_ZFS_SUPPORT)
static void
userboot_zfs_probe(void)
{
	char devname[32];
	uint64_t pool_guid;
	int unit;

	/*
	 * Open all the disks we can find and see if we can reconstruct
	 * ZFS pools from them. Record if any were found.
	 */
	for (unit = 0; unit < userboot_disk_maxunit; unit++) {
		sprintf(devname, "disk%d:", unit);
		pool_guid = 0;
		zfs_probe_dev(devname, &pool_guid);
		if (pool_guid != 0)
			userboot_zfs_found = 1;
	}
}
#endif

COMMAND_SET(quit, "quit", "exit the loader", command_quit);

static int
command_quit(int argc, char *argv[])
{

	exit(USERBOOT_EXIT_QUIT);
	return (CMD_OK);
}

COMMAND_SET(reboot, "reboot", "reboot the system", command_reboot);

static int
command_reboot(int argc, char *argv[])
{

	exit(USERBOOT_EXIT_REBOOT);
	return (CMD_OK);
}
