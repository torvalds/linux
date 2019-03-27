/*-
 * Copyright (c) 2018, Mellanox Technologies, Ltd.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/ioctl.h>
#include <dev/mlx5/mlx5io.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* stolen from pciconf.c: parsesel() */
static int
parse_pci_addr(const char *addrstr, struct mlx5_fwdump_addr *addr)
{
	char *eppos;
	unsigned long selarr[4];
	int i;

	if (addrstr == NULL) {
		warnx("no pci address specified");
		return (1);
	}
	if (strncmp(addrstr, "pci", 3) == 0) {
		addrstr += 3;
		i = 0;
		while (isdigit(*addrstr) && i < 4) {
			selarr[i++] = strtoul(addrstr, &eppos, 10);
			addrstr = eppos;
			if (*addrstr == ':')
				addrstr++;
		}
		if (i > 0 && *addrstr == '\0') {
			addr->func = (i > 2) ? selarr[--i] : 0;
			addr->slot = (i > 0) ? selarr[--i] : 0;
			addr->bus = (i > 0) ? selarr[--i] : 0;
			addr->domain = (i > 0) ? selarr[--i] : 0;
			return (0);
		}
	}
	warnx("invalid pci address %s", addrstr);
	return (1);
}

static int
mlx5tool_save_dump(int ctldev, const struct mlx5_fwdump_addr *addr,
    const char *dumpname)
{
	struct mlx5_fwdump_get fdg;
	struct mlx5_fwdump_reg *rege;
	FILE *dump;
	size_t cnt;
	int error, res;

	if (dumpname == NULL)
		dump = stdout;
	else
		dump = fopen(dumpname, "w");
	if (dump == NULL) {
		warn("open %s", dumpname);
		return (1);
	}
	res = 1;
	memset(&fdg, 0, sizeof(fdg));
	fdg.devaddr = *addr;
	error = ioctl(ctldev, MLX5_FWDUMP_GET, &fdg);
	if (error != 0) {
		warn("MLX5_FWDUMP_GET dumpsize");
		goto out;
	}
	rege = calloc(fdg.reg_filled, sizeof(*rege));
	if (rege == NULL) {
		warn("alloc rege");
		goto out;
	}
	fdg.buf = rege;
	fdg.reg_cnt = fdg.reg_filled;
	error = ioctl(ctldev, MLX5_FWDUMP_GET, &fdg);
	if (error != 0) {
		if (errno == ENOENT)
			warnx("no dump recorded");
		else
			warn("MLX5_FWDUMP_GET dump fetch");
		goto out;
	}
	for (cnt = 0; cnt < fdg.reg_cnt; cnt++, rege++)
		fprintf(dump, "0x%08x\t0x%08x\n", rege->addr, rege->val);
	res = 0;
out:
	if (dump != stdout)
		fclose(dump);
	return (res);
}

static int
mlx5tool_dump_reset(int ctldev, const struct mlx5_fwdump_addr *addr)
{

	if (ioctl(ctldev, MLX5_FWDUMP_RESET, addr) == -1) {
		warn("MLX5_FWDUMP_RESET");
		return (1);
	}
	return (0);
}

static int
mlx5tool_dump_force(int ctldev, const struct mlx5_fwdump_addr *addr)
{

	if (ioctl(ctldev, MLX5_FWDUMP_FORCE, addr) == -1) {
		warn("MLX5_FWDUMP_FORCE");
		return (1);
	}
	return (0);
}

static void
usage(void)
{

	fprintf(stderr,
	    "Usage: mlx5tool -d pci<d:b:s:f> [-w -o dump.file | -r | -e]\n");
	fprintf(stderr, "\t-w - write firmware dump to the specified file\n");
	fprintf(stderr, "\t-r - reset dump\n");
	fprintf(stderr, "\t-e - force dump\n");
	exit(1);
}

enum mlx5_action {
	ACTION_DUMP_GET,
	ACTION_DUMP_RESET,
	ACTION_DUMP_FORCE,
	ACTION_NONE,
};

int
main(int argc, char *argv[])
{
	struct mlx5_fwdump_addr addr;
	char *dumpname;
	char *addrstr;
	int c, ctldev, res;
	enum mlx5_action act;

	act = ACTION_NONE;
	addrstr = NULL;
	dumpname = NULL;
	while ((c = getopt(argc, argv, "d:eho:rw")) != -1) {
		switch (c) {
		case 'd':
			addrstr = optarg;
			break;
		case 'w':
			act = ACTION_DUMP_GET;
			break;
		case 'e':
			act= ACTION_DUMP_FORCE;
			break;
		case 'o':
			dumpname = optarg;
			break;
		case 'r':
			act = ACTION_DUMP_RESET;
			break;
		case 'h':
		default:
			usage();
		}
	}
	if (act == ACTION_NONE || (dumpname != NULL && act != ACTION_DUMP_GET))
		usage();
	if (parse_pci_addr(addrstr, &addr) != 0)
		exit(1);

	ctldev = open(MLX5_DEV_PATH, O_RDWR);
	if (ctldev == -1)
		err(1, "open "MLX5_DEV_PATH);
	switch (act) {
	case ACTION_DUMP_GET:
		res = mlx5tool_save_dump(ctldev, &addr, dumpname);
		break;
	case ACTION_DUMP_RESET:
		res = mlx5tool_dump_reset(ctldev, &addr);
		break;
	case ACTION_DUMP_FORCE:
		res = mlx5tool_dump_force(ctldev, &addr);
		break;
	default:
		res = 0;
		break;
	}
	close(ctldev);
	exit(res);
}
