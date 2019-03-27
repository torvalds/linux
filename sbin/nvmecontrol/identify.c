/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012-2013 Intel Corporation
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

#include <sys/param.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nvmecontrol.h"
#include "nvmecontrol_ext.h"

#define IDENTIFY_USAGE							       \
	"identify [-x [-v]] <controller id|namespace id>\n"

static void
print_namespace(struct nvme_namespace_data *nsdata)
{
	uint32_t	i;
	uint32_t	lbaf, lbads, ms, rp;
	uint8_t		thin_prov, ptype;
	uint8_t		flbas_fmt;

	thin_prov = (nsdata->nsfeat >> NVME_NS_DATA_NSFEAT_THIN_PROV_SHIFT) &
		NVME_NS_DATA_NSFEAT_THIN_PROV_MASK;

	flbas_fmt = (nsdata->flbas >> NVME_NS_DATA_FLBAS_FORMAT_SHIFT) &
		NVME_NS_DATA_FLBAS_FORMAT_MASK;

	printf("Size (in LBAs):              %lld (%lldM)\n",
		(long long)nsdata->nsze,
		(long long)nsdata->nsze / 1024 / 1024);
	printf("Capacity (in LBAs):          %lld (%lldM)\n",
		(long long)nsdata->ncap,
		(long long)nsdata->ncap / 1024 / 1024);
	printf("Utilization (in LBAs):       %lld (%lldM)\n",
		(long long)nsdata->nuse,
		(long long)nsdata->nuse / 1024 / 1024);
	printf("Thin Provisioning:           %s\n",
		thin_prov ? "Supported" : "Not Supported");
	printf("Number of LBA Formats:       %d\n", nsdata->nlbaf+1);
	printf("Current LBA Format:          LBA Format #%02d\n", flbas_fmt);
	printf("Data Protection Caps:        %s%s%s%s%s%s\n",
	    (nsdata->dpc == 0) ? "Not Supported" : "",
	    ((nsdata->dpc >> NVME_NS_DATA_DPC_MD_END_SHIFT) &
	     NVME_NS_DATA_DPC_MD_END_MASK) ? "Last Bytes, " : "",
	    ((nsdata->dpc >> NVME_NS_DATA_DPC_MD_START_SHIFT) &
	     NVME_NS_DATA_DPC_MD_START_MASK) ? "First Bytes, " : "",
	    ((nsdata->dpc >> NVME_NS_DATA_DPC_PIT3_SHIFT) &
	     NVME_NS_DATA_DPC_PIT3_MASK) ? "Type 3, " : "",
	    ((nsdata->dpc >> NVME_NS_DATA_DPC_PIT2_SHIFT) &
	     NVME_NS_DATA_DPC_PIT2_MASK) ? "Type 2, " : "",
	    ((nsdata->dpc >> NVME_NS_DATA_DPC_PIT2_MASK) &
	     NVME_NS_DATA_DPC_PIT1_MASK) ? "Type 1" : "");
	printf("Data Protection Settings:    ");
	ptype = (nsdata->dps >> NVME_NS_DATA_DPS_PIT_SHIFT) &
	    NVME_NS_DATA_DPS_PIT_MASK;
	if (ptype) {
		printf("Type %d, %s Bytes\n", ptype,
		    ((nsdata->dps >> NVME_NS_DATA_DPS_MD_START_SHIFT) &
		     NVME_NS_DATA_DPS_MD_START_MASK) ? "First" : "Last");
	} else {
		printf("Not Enabled\n");
	}
	printf("Multi-Path I/O Capabilities: %s%s\n",
	    (nsdata->nmic == 0) ? "Not Supported" : "",
	    ((nsdata->nmic >> NVME_NS_DATA_NMIC_MAY_BE_SHARED_SHIFT) &
	     NVME_NS_DATA_NMIC_MAY_BE_SHARED_MASK) ? "May be shared" : "");
	printf("Reservation Capabilities:    %s%s%s%s%s%s%s%s%s\n",
	    (nsdata->rescap == 0) ? "Not Supported" : "",
	    ((nsdata->rescap >> NVME_NS_DATA_RESCAP_IEKEY13_SHIFT) &
	     NVME_NS_DATA_RESCAP_IEKEY13_MASK) ? "IEKEY13, " : "",
	    ((nsdata->rescap >> NVME_NS_DATA_RESCAP_EX_AC_AR_SHIFT) &
	     NVME_NS_DATA_RESCAP_EX_AC_AR_MASK) ? "EX_AC_AR, " : "",
	    ((nsdata->rescap >> NVME_NS_DATA_RESCAP_WR_EX_AR_SHIFT) &
	     NVME_NS_DATA_RESCAP_WR_EX_AR_MASK) ? "WR_EX_AR, " : "",
	    ((nsdata->rescap >> NVME_NS_DATA_RESCAP_EX_AC_RO_SHIFT) &
	     NVME_NS_DATA_RESCAP_EX_AC_RO_MASK) ? "EX_AC_RO, " : "",
	    ((nsdata->rescap >> NVME_NS_DATA_RESCAP_WR_EX_RO_SHIFT) &
	     NVME_NS_DATA_RESCAP_WR_EX_RO_MASK) ? "WR_EX_RO, " : "",
	    ((nsdata->rescap >> NVME_NS_DATA_RESCAP_EX_AC_SHIFT) &
	     NVME_NS_DATA_RESCAP_EX_AC_MASK) ? "EX_AC, " : "",
	    ((nsdata->rescap >> NVME_NS_DATA_RESCAP_WR_EX_SHIFT) &
	     NVME_NS_DATA_RESCAP_WR_EX_MASK) ? "WR_EX, " : "",
	    ((nsdata->rescap >> NVME_NS_DATA_RESCAP_PTPL_SHIFT) &
	     NVME_NS_DATA_RESCAP_PTPL_MASK) ? "PTPL" : "");
	printf("Format Progress Indicator:   ");
	if ((nsdata->fpi >> NVME_NS_DATA_FPI_SUPP_SHIFT) &
	    NVME_NS_DATA_FPI_SUPP_MASK) {
		printf("%u%% remains\n",
		    (nsdata->fpi >> NVME_NS_DATA_FPI_PERC_SHIFT) &
		    NVME_NS_DATA_FPI_PERC_MASK);
	} else
		printf("Not Supported\n");
	printf("Optimal I/O Boundary (LBAs): %u\n", nsdata->noiob);
	printf("Globally Unique Identifier:  ");
	for (i = 0; i < sizeof(nsdata->nguid); i++)
		printf("%02x", nsdata->nguid[i]);
	printf("\n");
	printf("IEEE EUI64:                  ");
	for (i = 0; i < sizeof(nsdata->eui64); i++)
		printf("%02x", nsdata->eui64[i]);
	printf("\n");
	for (i = 0; i <= nsdata->nlbaf; i++) {
		lbaf = nsdata->lbaf[i];
		lbads = (lbaf >> NVME_NS_DATA_LBAF_LBADS_SHIFT) &
			NVME_NS_DATA_LBAF_LBADS_MASK;
		ms = (lbaf >> NVME_NS_DATA_LBAF_MS_SHIFT) &
			NVME_NS_DATA_LBAF_MS_MASK;
		rp = (lbaf >> NVME_NS_DATA_LBAF_RP_SHIFT) &
			NVME_NS_DATA_LBAF_RP_MASK;
		printf("LBA Format #%02d: Data Size: %5d  Metadata Size: %5d"
		    "  Performance: %s\n",
		    i, 1 << lbads, ms, (rp == 0) ? "Best" :
		    (rp == 1) ? "Better" : (rp == 2) ? "Good" : "Degraded");
	}
}

static void
identify_ctrlr(const struct nvme_function *nf, int argc, char *argv[])
{
	struct nvme_controller_data	cdata;
	int				ch, fd, hexflag = 0, hexlength;
	int				verboseflag = 0;

	while ((ch = getopt(argc, argv, "vx")) != -1) {
		switch ((char)ch) {
		case 'v':
			verboseflag = 1;
			break;
		case 'x':
			hexflag = 1;
			break;
		default:
			usage(nf);
		}
	}

	/* Check that a controller was specified. */
	if (optind >= argc)
		usage(nf);

	open_dev(argv[optind], &fd, 1, 1);
	read_controller_data(fd, &cdata);
	close(fd);

	if (hexflag == 1) {
		if (verboseflag == 1)
			hexlength = sizeof(struct nvme_controller_data);
		else
			hexlength = offsetof(struct nvme_controller_data,
			    reserved8);
		print_hex(&cdata, hexlength);
		exit(0);
	}

	if (verboseflag == 1) {
		fprintf(stderr, "-v not currently supported without -x\n");
		usage(nf);
	}

	nvme_print_controller(&cdata);
	exit(0);
}

static void
identify_ns(const struct nvme_function *nf,int argc, char *argv[])
{
	struct nvme_namespace_data	nsdata;
	char				path[64];
	int				ch, fd, hexflag = 0, hexlength;
	int				verboseflag = 0;
	uint32_t			nsid;

	while ((ch = getopt(argc, argv, "vx")) != -1) {
		switch ((char)ch) {
		case 'v':
			verboseflag = 1;
			break;
		case 'x':
			hexflag = 1;
			break;
		default:
			usage(nf);
		}
	}

	/* Check that a namespace was specified. */
	if (optind >= argc)
		usage(nf);

	/*
	 * Check if the specified device node exists before continuing.
	 *  This is a cleaner check for cases where the correct controller
	 *  is specified, but an invalid namespace on that controller.
	 */
	open_dev(argv[optind], &fd, 1, 1);
	close(fd);

	/*
	 * We send IDENTIFY commands to the controller, not the namespace,
	 *  since it is an admin cmd.  The namespace ID will be specified in
	 *  the IDENTIFY command itself.  So parse the namespace's device node
	 *  string to get the controller substring and namespace ID.
	 */
	parse_ns_str(argv[optind], path, &nsid);
	open_dev(path, &fd, 1, 1);
	read_namespace_data(fd, nsid, &nsdata);
	close(fd);

	if (hexflag == 1) {
		if (verboseflag == 1)
			hexlength = sizeof(struct nvme_namespace_data);
		else
			hexlength = offsetof(struct nvme_namespace_data,
			    reserved6);
		print_hex(&nsdata, hexlength);
		exit(0);
	}

	if (verboseflag == 1) {
		fprintf(stderr, "-v not currently supported without -x\n");
		usage(nf);
	}

	print_namespace(&nsdata);
	exit(0);
}

static void
identify(const struct nvme_function *nf, int argc, char *argv[])
{
	char	*target;

	if (argc < 2)
		usage(nf);

	while (getopt(argc, argv, "vx") != -1) ;

	/* Check that a controller or namespace was specified. */
	if (optind >= argc)
		usage(nf);

	target = argv[optind];

	optreset = 1;
	optind = 1;

	/*
	 * If device node contains "ns", we consider it a namespace,
	 *  otherwise, consider it a controller.
	 */
	if (strstr(target, NVME_NS_PREFIX) == NULL)
		identify_ctrlr(nf, argc, argv);
	else
		identify_ns(nf, argc, argv);
}

NVME_COMMAND(top, identify, identify, IDENTIFY_USAGE);
