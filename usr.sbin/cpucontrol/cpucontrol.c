/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2011 Stanislav Sedov <stas@FreeBSD.org>.
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This utility provides userland access to the cpuctl(4) pseudo-device
 * features.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>

#include <sys/queue.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/cpuctl.h>

#include "cpucontrol.h"
#include "amd.h"
#include "intel.h"
#include "via.h"

int	verbosity_level = 0;

#define	DEFAULT_DATADIR	"/usr/local/share/cpucontrol"

#define	FLAG_I	0x01
#define	FLAG_M	0x02
#define	FLAG_U	0x04
#define	FLAG_N	0x08
#define	FLAG_E	0x10

#define	OP_INVAL	0x00
#define	OP_READ		0x01
#define	OP_WRITE	0x02
#define	OP_OR		0x04
#define	OP_AND		0x08

#define	HIGH(val)	(uint32_t)(((val) >> 32) & 0xffffffff)
#define	LOW(val)	(uint32_t)((val) & 0xffffffff)

struct datadir {
	const char		*path;
	SLIST_ENTRY(datadir)	next;
};
static SLIST_HEAD(, datadir) datadirs = SLIST_HEAD_INITIALIZER(datadirs);

static struct ucode_handler {
	ucode_probe_t *probe;
	ucode_update_t *update;
} handlers[] = {
	{ intel_probe, intel_update },
	{ amd10h_probe, amd10h_update },
	{ amd_probe, amd_update },
	{ via_probe, via_update },
};
#define NHANDLERS (sizeof(handlers) / sizeof(*handlers))

static void	usage(void);
static int	do_cpuid(const char *cmdarg, const char *dev);
static int	do_cpuid_count(const char *cmdarg, const char *dev);
static int	do_msr(const char *cmdarg, const char *dev);
static int	do_update(const char *dev);
static void	datadir_add(const char *path);

static void __dead2
usage(void)
{
	const char *name;

	name = getprogname();
	if (name == NULL)
		name = "cpuctl";
	fprintf(stderr, "Usage: %s [-vh] [-d datadir] [-m msr[=value] | "
	    "-i level | -i level,level_type | -e | -u] device\n", name);
	exit(EX_USAGE);
}

static int
do_cpuid(const char *cmdarg, const char *dev)
{
	unsigned int level;
	cpuctl_cpuid_args_t args;
	int fd, error;
	char *endptr;

	assert(cmdarg != NULL);
	assert(dev != NULL);

	level = strtoul(cmdarg, &endptr, 16);
	if (*cmdarg == '\0' || *endptr != '\0') {
		WARNX(0, "incorrect operand: %s", cmdarg);
		usage();
		/* NOTREACHED */
	}

	/*
	 * Fill ioctl argument structure.
	 */
	args.level = level;
	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		WARN(0, "error opening %s for reading", dev);
		return (1);
	}
	error = ioctl(fd, CPUCTL_CPUID, &args);
	if (error < 0) {
		WARN(0, "ioctl(%s, CPUCTL_CPUID)", dev);
		close(fd);
		return (error);
	}
	fprintf(stdout, "cpuid level 0x%x: 0x%.8x 0x%.8x 0x%.8x 0x%.8x\n",
	    level, args.data[0], args.data[1], args.data[2], args.data[3]);
	close(fd);
	return (0);
}

static int
do_cpuid_count(const char *cmdarg, const char *dev)
{
	char *cmdarg1, *endptr, *endptr1;
	unsigned int level, level_type;
	cpuctl_cpuid_count_args_t args;
	int fd, error;

	assert(cmdarg != NULL);
	assert(dev != NULL);

	level = strtoul(cmdarg, &endptr, 16);
	if (*cmdarg == '\0' || *endptr == '\0') {
		WARNX(0, "incorrect or missing operand: %s", cmdarg);
		usage();
		/* NOTREACHED */
	}
	/* Locate the comma... */
	cmdarg1 = strstr(endptr, ",");
	/* ... and skip past it */
	cmdarg1 += 1;
	level_type = strtoul(cmdarg1, &endptr1, 16);
	if (*cmdarg1 == '\0' || *endptr1 != '\0') {
		WARNX(0, "incorrect or missing operand: %s", cmdarg);
		usage();
		/* NOTREACHED */
	}

	/*
	 * Fill ioctl argument structure.
	 */
	args.level = level;
	args.level_type = level_type;
	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		WARN(0, "error opening %s for reading", dev);
		return (1);
	}
	error = ioctl(fd, CPUCTL_CPUID_COUNT, &args);
	if (error < 0) {
		WARN(0, "ioctl(%s, CPUCTL_CPUID_COUNT)", dev);
		close(fd);
		return (error);
	}
	fprintf(stdout, "cpuid level 0x%x, level_type 0x%x: 0x%.8x 0x%.8x "
	    "0x%.8x 0x%.8x\n", level, level_type, args.data[0], args.data[1],
	    args.data[2], args.data[3]);
	close(fd);
	return (0);
}

static int
do_msr(const char *cmdarg, const char *dev)
{
	unsigned int msr;
	cpuctl_msr_args_t args;
	size_t len;
	uint64_t data = 0;
	unsigned long command;
	int do_invert = 0, op;
	int fd, error;
	const char *command_name;
	char *endptr;
	char *p;

	assert(cmdarg != NULL);
	assert(dev != NULL);
	len = strlen(cmdarg);
	if (len == 0) {
		WARNX(0, "MSR register expected");
		usage();
		/* NOTREACHED */
	}

	/*
	 * Parse command string.
	 */
	msr = strtoul(cmdarg, &endptr, 16);
	switch (*endptr) {
	case '\0':
		op = OP_READ;
		break;
	case '=':
		op = OP_WRITE;
		break;
	case '&':
		op = OP_AND;
		endptr++;
		break;
	case '|':
		op = OP_OR;
		endptr++;
		break;
	default:
		op = OP_INVAL;
	}
	if (op != OP_READ) {	/* Complex operation. */
		if (*endptr != '=')
			op = OP_INVAL;
		else {
			p = ++endptr;
			if (*p == '~') {
				do_invert = 1;
				p++;
			}
			data = strtoull(p, &endptr, 16);
			if (*p == '\0' || *endptr != '\0') {
				WARNX(0, "argument required: %s", cmdarg);
				usage();
				/* NOTREACHED */
			}
		}
	}
	if (op == OP_INVAL) {
		WARNX(0, "invalid operator: %s", cmdarg);
		usage();
		/* NOTREACHED */
	}

	/*
	 * Fill ioctl argument structure.
	 */
	args.msr = msr;
	if ((do_invert != 0) ^ (op == OP_AND))
		args.data = ~data;
	else
		args.data = data;
	switch (op) {
	case OP_READ:
		command = CPUCTL_RDMSR;
		command_name = "RDMSR";
		break;
	case OP_WRITE:
		command = CPUCTL_WRMSR;
		command_name = "WRMSR";
		break;
	case OP_OR:
		command = CPUCTL_MSRSBIT;
		command_name = "MSRSBIT";
		break;
	case OP_AND:
		command = CPUCTL_MSRCBIT;
		command_name = "MSRCBIT";
		break;
	default:
		abort();
	}
	fd = open(dev, op == OP_READ ? O_RDONLY : O_WRONLY);
	if (fd < 0) {
		WARN(0, "error opening %s for %s", dev,
		    op == OP_READ ? "reading" : "writing");
		return (1);
	}
	error = ioctl(fd, command, &args);
	if (error < 0) {
		WARN(0, "ioctl(%s, CPUCTL_%s (%lu))", dev, command_name, command);
		close(fd);
		return (1);
	}
	if (op == OP_READ)
		fprintf(stdout, "MSR 0x%x: 0x%.8x 0x%.8x\n", msr,
		    HIGH(args.data), LOW(args.data));
	close(fd);
	return (0);
}

static int
do_eval_cpu_features(const char *dev)
{
	int fd, error;

	assert(dev != NULL);

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		WARN(0, "error opening %s for writing", dev);
		return (1);
	}
	error = ioctl(fd, CPUCTL_EVAL_CPU_FEATURES, NULL);
	if (error < 0)
		WARN(0, "ioctl(%s, CPUCTL_EVAL_CPU_FEATURES)", dev);
	close(fd);
	return (error);
}

static int
try_a_fw_image(const char *dev_path, int devfd, int fwdfd, const char *dpath,
    const char *fname, struct ucode_handler *handler)
{
	struct ucode_update_params parm;
	struct stat st;
	char *fw_path;
	void *fw_map;
	int fwfd, rc;

	rc = 0;
	fw_path = NULL;
	fw_map = MAP_FAILED;
	fwfd = openat(fwdfd, fname, O_RDONLY);
	if (fwfd < 0) {
		WARN(0, "openat(%s, %s)", dpath, fname);
		goto out;
	}

	rc = asprintf(&fw_path, "%s/%s", dpath, fname);
	if (rc == -1) {
		WARNX(0, "out of memory");
		rc = ENOMEM;
		goto out;
	}

	rc = fstat(fwfd, &st);
	if (rc != 0) {
		WARN(0, "fstat(%s)", fw_path);
		rc = 0;
		goto out;
	}
	if (!S_ISREG(st.st_mode))
		goto out;
	if (st.st_size <= 0) {
		WARN(0, "%s: empty", fw_path);
		goto out;
	}

	fw_map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fwfd, 0);
	if (fw_map == MAP_FAILED) {
		WARN(0, "mmap(%s)", fw_path);
		goto out;
	}


	memset(&parm, 0, sizeof(parm));
	parm.devfd = devfd;
	parm.fwimage = fw_map;
	parm.fwsize = st.st_size;
	parm.dev_path = dev_path;
	parm.fw_path = fw_path;

	handler->update(&parm);

out:
	if (fw_map != MAP_FAILED)
		munmap(fw_map, st.st_size);
	free(fw_path);
	if (fwfd >= 0)
		close(fwfd);
	return (rc);
}

static int
do_update(const char *dev)
{
	int fd, fwdfd;
	unsigned int i;
	int error;
	struct ucode_handler *handler;
	struct datadir *dir;
	DIR *dirp;
	struct dirent *direntry;

	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		WARN(0, "error opening %s for reading", dev);
		return (1);
	}

	/*
	 * Find the appropriate handler for CPU.
	 */
	for (i = 0; i < NHANDLERS; i++)
		if (handlers[i].probe(fd) == 0)
			break;
	if (i < NHANDLERS)
		handler = &handlers[i];
	else {
		WARNX(0, "cannot find the appropriate handler for %s", dev);
		close(fd);
		return (1);
	}
	close(fd);

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		WARN(0, "error opening %s for writing", dev);
		return (1);
	}

	/*
	 * Process every image in specified data directories.
	 */
	SLIST_FOREACH(dir, &datadirs, next) {
		fwdfd = open(dir->path, O_RDONLY);
		if (fwdfd < 0) {
			WARN(1, "skipping directory %s: not accessible", dir->path);
			continue;
		}
		dirp = fdopendir(fwdfd);
		if (dirp == NULL) {
			WARNX(0, "out of memory");
			close(fwdfd);
			close(fd);
			return (1);
		}

		while ((direntry = readdir(dirp)) != NULL) {
			if (direntry->d_namlen == 0)
				continue;
			if (direntry->d_type == DT_DIR)
				continue;

			error = try_a_fw_image(dev, fd, fwdfd, dir->path,
			    direntry->d_name, handler);
			if (error != 0) {
				closedir(dirp);
				close(fd);
				return (1);
			}
		}
		error = closedir(dirp);
		if (error != 0)
			WARN(0, "closedir(%s)", dir->path);
	}
	close(fd);
	return (0);
}

/*
 * Add new data directory to the search list.
 */
static void
datadir_add(const char *path)
{
	struct datadir *newdir;

	newdir = (struct datadir *)malloc(sizeof(*newdir));
	if (newdir == NULL)
		err(EX_OSERR, "cannot allocate memory");
	newdir->path = path;
	SLIST_INSERT_HEAD(&datadirs, newdir, next);
}

int
main(int argc, char *argv[])
{
	struct datadir *elm;
	int c, flags;
	const char *cmdarg;
	const char *dev;
	int error;

	flags = 0;
	error = 0;
	cmdarg = "";	/* To keep gcc3 happy. */

	while ((c = getopt(argc, argv, "d:ehi:m:nuv")) != -1) {
		switch (c) {
		case 'd':
			datadir_add(optarg);
			break;
		case 'e':
			flags |= FLAG_E;
			break;
		case 'i':
			flags |= FLAG_I;
			cmdarg = optarg;
			break;
		case 'm':
			flags |= FLAG_M;
			cmdarg = optarg;
			break;
		case 'n':
			flags |= FLAG_N;
			break;
		case 'u':
			flags |= FLAG_U;
			break;
		case 'v':
			verbosity_level++;
			break;
		case 'h':
			/* FALLTHROUGH */
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;
	if (argc < 1) {
		usage();
		/* NOTREACHED */
	}
	if ((flags & FLAG_N) == 0)
		datadir_add(DEFAULT_DATADIR);
	dev = argv[0];
	c = flags & (FLAG_E | FLAG_I | FLAG_M | FLAG_U);
	switch (c) {
	case FLAG_I:
		if (strstr(cmdarg, ",") != NULL)
			error = do_cpuid_count(cmdarg, dev);
		else
			error = do_cpuid(cmdarg, dev);
		break;
	case FLAG_M:
		error = do_msr(cmdarg, dev);
		break;
	case FLAG_U:
		error = do_update(dev);
		break;
	case FLAG_E:
		error = do_eval_cpu_features(dev);
		break;
	default:
		usage();	/* Only one command can be selected. */
	}
	while ((elm = SLIST_FIRST(&datadirs)) != NULL) {
		SLIST_REMOVE_HEAD(&datadirs, next);
		free(elm);
	}
	return (error == 0 ? 0 : 1);
}
