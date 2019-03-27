/*-
 * Copyright (c) 2016 Netflix, Inc.
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

#include <sys/param.h>
#include <sys/ioccom.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nvmecontrol.h"

_Static_assert(sizeof(struct nvme_power_state) == 256 / NBBY,
	       "nvme_power_state size wrong");

#define POWER_USAGE							       \
	"power [-l] [-p new-state [-w workload-hint]] <controller id>\n"

static void
power_list_one(int i, struct nvme_power_state *nps)
{
	int mpower, apower, ipower;
	uint8_t mps, nops, aps, apw;

	mps = (nps->mps_nops >> NVME_PWR_ST_MPS_SHIFT) &
		NVME_PWR_ST_MPS_MASK;
	nops = (nps->mps_nops >> NVME_PWR_ST_NOPS_SHIFT) &
		NVME_PWR_ST_NOPS_MASK;
	apw = (nps->apw_aps >> NVME_PWR_ST_APW_SHIFT) &
		NVME_PWR_ST_APW_MASK;
	aps = (nps->apw_aps >> NVME_PWR_ST_APS_SHIFT) &
		NVME_PWR_ST_APS_MASK;

	mpower = nps->mp;
	if (mps == 0)
		mpower *= 100;
	ipower = nps->idlp;
	if (nps->ips == 1)
		ipower *= 100;
	apower = nps->actp;
	if (aps == 1)
		apower *= 100;
	printf("%2d: %2d.%04dW%c %3d.%03dms %3d.%03dms %2d %2d %2d %2d %2d.%04dW %2d.%04dW %d\n",
	       i, mpower / 10000, mpower % 10000,
	       nops ? '*' : ' ', nps->enlat / 1000, nps->enlat % 1000,
	       nps->exlat / 1000, nps->exlat % 1000, nps->rrt, nps->rrl,
	       nps->rwt, nps->rwl, ipower / 10000, ipower % 10000,
	       apower / 10000, apower % 10000, apw);
}

static void
power_list(struct nvme_controller_data *cdata)
{
	int i;

	printf("\nPower States Supported: %d\n\n", cdata->npss + 1);
	printf(" #   Max pwr  Enter Lat  Exit Lat RT RL WT WL Idle Pwr  Act Pwr Workloadd\n");
	printf("--  --------  --------- --------- -- -- -- -- -------- -------- --\n");
	for (i = 0; i <= cdata->npss; i++)
		power_list_one(i, &cdata->power_state[i]);
}

static void
power_set(int fd, int power_val, int workload, int perm)
{
	struct nvme_pt_command	pt;
	uint32_t p;

	p = perm ? (1u << 31) : 0;
	memset(&pt, 0, sizeof(pt));
	pt.cmd.opc = NVME_OPC_SET_FEATURES;
	pt.cmd.cdw10 = htole32(NVME_FEAT_POWER_MANAGEMENT | p);
	pt.cmd.cdw11 = htole32(power_val | (workload << 5));

	if (ioctl(fd, NVME_PASSTHROUGH_CMD, &pt) < 0)
		err(1, "set feature power mgmt request failed");

	if (nvme_completion_is_error(&pt.cpl))
		errx(1, "set feature power mgmt request returned error");
}

static void
power_show(int fd)
{
	struct nvme_pt_command	pt;

	memset(&pt, 0, sizeof(pt));
	pt.cmd.opc = NVME_OPC_GET_FEATURES;
	pt.cmd.cdw10 = htole32(NVME_FEAT_POWER_MANAGEMENT);

	if (ioctl(fd, NVME_PASSTHROUGH_CMD, &pt) < 0)
		err(1, "set feature power mgmt request failed");

	if (nvme_completion_is_error(&pt.cpl))
		errx(1, "set feature power mgmt request returned error");

	printf("Current Power Mode is %d\n", pt.cpl.cdw0);
}

static void
power(const struct nvme_function *nf, int argc, char *argv[])
{
	struct nvme_controller_data	cdata;
	int				ch, listflag = 0, powerflag = 0, power_val = 0, fd;
	int				workload = 0;
	char				*end;

	while ((ch = getopt(argc, argv, "lp:w:")) != -1) {
		switch ((char)ch) {
		case 'l':
			listflag = 1;
			break;
		case 'p':
			powerflag = 1;
			power_val = strtol(optarg, &end, 0);
			if (*end != '\0') {
				fprintf(stderr, "Invalid power state number: %s\n", optarg);
				usage(nf);
			}
			break;
		case 'w':
			workload = strtol(optarg, &end, 0);
			if (*end != '\0') {
				fprintf(stderr, "Invalid workload hint: %s\n", optarg);
				usage(nf);
			}
			break;
		default:
			usage(nf);
		}
	}

	/* Check that a controller was specified. */
	if (optind >= argc)
		usage(nf);

	if (listflag && powerflag) {
		fprintf(stderr, "Can't set power and list power states\n");
		usage(nf);
	}

	open_dev(argv[optind], &fd, 1, 1);
	read_controller_data(fd, &cdata);

	if (listflag) {
		power_list(&cdata);
		goto out;
	}

	if (powerflag) {
		power_set(fd, power_val, workload, 0);
		goto out;
	}
	power_show(fd);

out:
	close(fd);
	exit(0);
}

NVME_COMMAND(top, power, power, POWER_USAGE);
