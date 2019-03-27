/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Netflix, Inc.
 * Copyright (C) 2018 Alexander Motin <mav@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ioccom.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nvmecontrol.h"

NVME_CMD_DECLARE(ns, struct nvme_function);

#define NS_USAGE							\
	"ns (create|delete|attach|detach)\n"

/* handles NVME_OPC_NAMESPACE_MANAGEMENT and ATTACHMENT admin cmds */

#define NSCREATE_USAGE							\
	"ns create -s size [-c cap] [-f fmt] [-m mset] [-n nmic] [-p pi] [-l pil] nvmeN\n"

#define NSDELETE_USAGE							\
	"ns delete -n nsid nvmeN\n"

#define NSATTACH_USAGE							\
	"ns attach -n nsid [-c ctrlrid] nvmeN \n"

#define NSDETACH_USAGE							\
	"ns detach -n nsid [-c ctrlrid] nvmeN\n"

static void nscreate(const struct nvme_function *nf, int argc, char *argv[]);
static void nsdelete(const struct nvme_function *nf, int argc, char *argv[]);
static void nsattach(const struct nvme_function *nf, int argc, char *argv[]);
static void nsdetach(const struct nvme_function *nf, int argc, char *argv[]);

NVME_COMMAND(ns, create, nscreate, NSCREATE_USAGE);
NVME_COMMAND(ns, delete, nsdelete, NSDELETE_USAGE);
NVME_COMMAND(ns, attach, nsattach, NSATTACH_USAGE);
NVME_COMMAND(ns, detach, nsdetach, NSDETACH_USAGE);

struct ns_result_str {
	uint16_t res;
	const char * str;
};

static struct ns_result_str ns_result[] = {
	{ 0x2,  "Invalid Field"},
	{ 0xa,  "Invalid Format"},
	{ 0xb,  "Invalid Namespace or format"},
	{ 0x15, "Namespace insufficent capacity"},
	{ 0x16, "Namespace ID unavaliable"},
	{ 0x18, "Namespace already attached"},
	{ 0x19, "Namespace is private"},
	{ 0x1a, "Namespace is not attached"},
	{ 0x1b, "Thin provisioning not supported"},
	{ 0x1c, "Controller list invalid"},
	{ 0xFFFF, "Unknown"}
};

static const char *
get_res_str(uint16_t res)
{
	struct ns_result_str *t = ns_result;

	while (t->res != 0xFFFF) {
		if (t->res == res)
			return (t->str);
		t++;
	}
	return t->str;
}

/*
 * NS MGMT Command specific status values:
 * 0xa = Invalid Format
 * 0x15 = Namespace Insuffience capacity
 * 0x16 = Namespace ID  unavailable (number namespaces exceeded)
 * 0xb = Thin Provisioning Not supported
 */
static void
nscreate(const struct nvme_function *nf, int argc, char *argv[])
{
	struct nvme_pt_command	pt;
	struct nvme_controller_data cd;
	struct nvme_namespace_data nsdata;
	int64_t	nsze = -1, cap = -1;
	int	ch, fd, result, lbaf = 0, mset = 0, nmic = -1, pi = 0, pil = 0;

	if (optind >= argc)
		usage(nf);

	while ((ch = getopt(argc, argv, "s:c:f:m:n:p:l:")) != -1) {
		switch (ch) {
		case 's':
			nsze = strtol(optarg, (char **)NULL, 0);
			break;
		case 'c':
			cap = strtol(optarg, (char **)NULL, 0);
			break;
		case 'f':
			lbaf = strtol(optarg, (char **)NULL, 0);
			break;
		case 'm':
			mset = strtol(optarg, NULL, 0);
			break;
		case 'n':
			nmic = strtol(optarg, NULL, 0);
			break;
		case 'p':
			pi = strtol(optarg, NULL, 0);
			break;
		case 'l':
			pil = strtol(optarg, NULL, 0);
			break;
		default:
			usage(nf);
		}
	}

	if (optind >= argc)
		usage(nf);

	if (cap == -1)
		cap = nsze;
	if (nsze == -1 || cap == -1)
		usage(nf);

	open_dev(argv[optind], &fd, 1, 1);
	read_controller_data(fd, &cd);

	/* Check that controller can execute this command. */
	if (((cd.oacs >> NVME_CTRLR_DATA_OACS_NSMGMT_SHIFT) &
	    NVME_CTRLR_DATA_OACS_NSMGMT_MASK) == 0)
		errx(1, "controller does not support namespace management");

	/* Allow namespaces sharing if Multi-Path I/O is supported. */
	if (nmic == -1) {
		nmic = cd.mic ? (NVME_NS_DATA_NMIC_MAY_BE_SHARED_MASK <<
		     NVME_NS_DATA_NMIC_MAY_BE_SHARED_SHIFT) : 0;
	}

	memset(&nsdata, 0, sizeof(nsdata));
	nsdata.nsze = (uint64_t)nsze;
	nsdata.ncap = (uint64_t)cap;
	nsdata.flbas = ((lbaf & NVME_NS_DATA_FLBAS_FORMAT_MASK)
	     << NVME_NS_DATA_FLBAS_FORMAT_SHIFT) |
	    ((mset & NVME_NS_DATA_FLBAS_EXTENDED_MASK)
	     << NVME_NS_DATA_FLBAS_EXTENDED_SHIFT);
	nsdata.dps = ((pi & NVME_NS_DATA_DPS_MD_START_MASK)
	     << NVME_NS_DATA_DPS_MD_START_SHIFT) |
	    ((pil & NVME_NS_DATA_DPS_PIT_MASK)
	     << NVME_NS_DATA_DPS_PIT_SHIFT);
	nsdata.nmic = nmic;
	nvme_namespace_data_swapbytes(&nsdata);

	memset(&pt, 0, sizeof(pt));
	pt.cmd.opc = NVME_OPC_NAMESPACE_MANAGEMENT;

	pt.cmd.cdw10 = 0; /* create */
	pt.buf = &nsdata;
	pt.len = sizeof(struct nvme_namespace_data);
	pt.is_read = 0; /* passthrough writes data to ctrlr */
	if ((result = ioctl(fd, NVME_PASSTHROUGH_CMD, &pt)) < 0)
		errx(1, "ioctl request to %s failed: %d", argv[optind], result);

	if (nvme_completion_is_error(&pt.cpl)) {
		errx(1, "namespace creation failed: %s",
		    get_res_str((pt.cpl.status >> NVME_STATUS_SC_SHIFT) &
		    NVME_STATUS_SC_MASK));
	}
	printf("namespace %d created\n", pt.cpl.cdw0);
	exit(0);
}

static void
nsdelete(const struct nvme_function *nf, int argc, char *argv[])
{
	struct nvme_pt_command	pt;
	struct nvme_controller_data cd;
	int	ch, fd, result, nsid = -2;
	char buf[2];

	if (optind >= argc)
		usage(nf);

	while ((ch = getopt(argc, argv, "n:")) != -1) {
		switch ((char)ch) {
		case  'n':
			nsid = strtol(optarg, (char **)NULL, 0);
			break;
		default:
			usage(nf);
		}
	}

	if (optind >= argc || nsid == -2)
		usage(nf);

	open_dev(argv[optind], &fd, 1, 1);
	read_controller_data(fd, &cd);

	/* Check that controller can execute this command. */
	if (((cd.oacs >> NVME_CTRLR_DATA_OACS_NSMGMT_SHIFT) &
	    NVME_CTRLR_DATA_OACS_NSMGMT_MASK) == 0)
		errx(1, "controller does not support namespace management");

	memset(&pt, 0, sizeof(pt));
	pt.cmd.opc = NVME_OPC_NAMESPACE_MANAGEMENT;
	pt.cmd.cdw10 = 1; /* delete */
	pt.buf = buf;
	pt.len = sizeof(buf);
	pt.is_read = 1;
	pt.cmd.nsid = (uint32_t)nsid;

	if ((result = ioctl(fd, NVME_PASSTHROUGH_CMD, &pt)) < 0)
		errx(1, "ioctl request to %s failed: %d", argv[optind], result);

	if (nvme_completion_is_error(&pt.cpl)) {
		errx(1, "namespace deletion failed: %s",
		    get_res_str((pt.cpl.status >> NVME_STATUS_SC_SHIFT) &
		    NVME_STATUS_SC_MASK));
	}
	printf("namespace %d deleted\n", nsid);
	exit(0);
}

/*
 * Attach and Detach use Dword 10, and a controller list (section 4.9)
 * This struct is 4096 bytes in size.
 * 0h = attach
 * 1h = detach
 *
 * Result values for both attach/detach:
 *
 * Completion 18h = Already attached
 *            19h = NS is private and already attached to a controller
 *            1Ah = Not attached, request could not be completed
 *            1Ch = Controller list invalid.
 *
 * 0x2 Invalid Field can occur if ctrlrid d.n.e in system.
 */
static void
nsattach(const struct nvme_function *nf, int argc, char *argv[])
{
	struct nvme_pt_command	pt;
	struct nvme_controller_data cd;
	int	ctrlrid = -2;
	int	fd, ch, result, nsid = -1;
	uint16_t clist[2048];

	if (optind >= argc)
		usage(nf);

	while ((ch = getopt(argc, argv, "n:c:")) != -1) {
		switch (ch) {
		case 'n':
			nsid = strtol(optarg, (char **)NULL, 0);
			break;
		case 'c':
			ctrlrid = strtol(optarg, (char **)NULL, 0);
			break;
		default:
			usage(nf);
		}
	}

	if (optind >= argc)
		usage(nf);

	if (nsid == -1 )
		usage(nf);

	open_dev(argv[optind], &fd, 1, 1);
	read_controller_data(fd, &cd);

	/* Check that controller can execute this command. */
	if (((cd.oacs >> NVME_CTRLR_DATA_OACS_NSMGMT_SHIFT) &
	    NVME_CTRLR_DATA_OACS_NSMGMT_MASK) == 0)
		errx(1, "controller does not support namespace management");

	if (ctrlrid == -1) {
		/* Get full list of controllers to attach to. */
		memset(&pt, 0, sizeof(pt));
		pt.cmd.opc = NVME_OPC_IDENTIFY;
		pt.cmd.cdw10 = htole32(0x13);
		pt.buf = clist;
		pt.len = sizeof(clist);
		pt.is_read = 1;
		if (ioctl(fd, NVME_PASSTHROUGH_CMD, &pt) < 0)
			err(1, "identify request failed");
		if (nvme_completion_is_error(&pt.cpl))
			errx(1, "identify request returned error");
	} else {
		/* By default attach to this controller. */
		if (ctrlrid == -2)
			ctrlrid = cd.ctrlr_id;
		memset(&clist, 0, sizeof(clist));
		clist[0] = htole16(1);
		clist[1] = htole16(ctrlrid);
	}

	memset(&pt, 0, sizeof(pt));
	pt.cmd.opc = NVME_OPC_NAMESPACE_ATTACHMENT;
	pt.cmd.cdw10 = 0; /* attach */
	pt.cmd.nsid = (uint32_t)nsid;
	pt.buf = &clist;
	pt.len = sizeof(clist);

	if ((result = ioctl(fd, NVME_PASSTHROUGH_CMD, &pt)) < 0)
		errx(1, "ioctl request to %s failed: %d", argv[optind], result);

	if (nvme_completion_is_error(&pt.cpl)) {
		errx(1, "namespace attach failed: %s",
		    get_res_str((pt.cpl.status >> NVME_STATUS_SC_SHIFT) &
		    NVME_STATUS_SC_MASK));
	}
	printf("namespace %d attached\n", nsid);
	exit(0);
}

static void
nsdetach(const struct nvme_function *nf, int argc, char *argv[])
{
	struct nvme_pt_command	pt;
	struct nvme_controller_data cd;
	int	ctrlrid = -2;
	int	fd, ch, result, nsid = -1;
	uint16_t clist[2048];

	if (optind >= argc)
		usage(nf);

	while ((ch = getopt(argc, argv, "n:c:")) != -1) {
		switch (ch) {
		case 'n':
			nsid = strtol(optarg, (char **)NULL, 0);
			break;
		case 'c':
			ctrlrid = strtol(optarg, (char **)NULL, 0);
			break;
		default:
			usage(nf);
		}
	}

	if (optind >= argc)
		usage(nf);

	if (nsid == -1)
		usage(nf);

	open_dev(argv[optind], &fd, 1, 1);
	read_controller_data(fd, &cd);

	/* Check that controller can execute this command. */
	if (((cd.oacs >> NVME_CTRLR_DATA_OACS_NSMGMT_SHIFT) &
	    NVME_CTRLR_DATA_OACS_NSMGMT_MASK) == 0)
		errx(1, "controller does not support namespace management");

	if (ctrlrid == -1) {
		/* Get list of controllers this namespace attached to. */
		memset(&pt, 0, sizeof(pt));
		pt.cmd.opc = NVME_OPC_IDENTIFY;
		pt.cmd.nsid = htole32(nsid);
		pt.cmd.cdw10 = htole32(0x12);
		pt.buf = clist;
		pt.len = sizeof(clist);
		pt.is_read = 1;
		if (ioctl(fd, NVME_PASSTHROUGH_CMD, &pt) < 0)
			err(1, "identify request failed");
		if (nvme_completion_is_error(&pt.cpl))
			errx(1, "identify request returned error");
		if (clist[0] == 0) {
			ctrlrid = cd.ctrlr_id;
			memset(&clist, 0, sizeof(clist));
			clist[0] = htole16(1);
			clist[1] = htole16(ctrlrid);
		}
	} else {
		/* By default detach from this controller. */
		if (ctrlrid == -2)
			ctrlrid = cd.ctrlr_id;
		memset(&clist, 0, sizeof(clist));
		clist[0] = htole16(1);
		clist[1] = htole16(ctrlrid);
	}

	memset(&pt, 0, sizeof(pt));
	pt.cmd.opc = NVME_OPC_NAMESPACE_ATTACHMENT;
	pt.cmd.cdw10 = 1; /* detach */
	pt.cmd.nsid = (uint32_t)nsid;
	pt.buf = &clist;
	pt.len = sizeof(clist);

	if ((result = ioctl(fd, NVME_PASSTHROUGH_CMD, &pt)) < 0)
		errx(1, "ioctl request to %s failed: %d", argv[optind], result);

	if (nvme_completion_is_error(&pt.cpl)) {
		errx(1, "namespace detach failed: %s",
		    get_res_str((pt.cpl.status >> NVME_STATUS_SC_SHIFT) &
		    NVME_STATUS_SC_MASK));
	}
	printf("namespace %d detached\n", nsid);
	exit(0);
}

static void
ns(const struct nvme_function *nf __unused, int argc, char *argv[])
{

	DISPATCH(argc, argv, ns);
}

NVME_COMMAND(top, ns, ns, NS_USAGE);
