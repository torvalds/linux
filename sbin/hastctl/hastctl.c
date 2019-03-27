/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <err.h>
#include <libutil.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <activemap.h>

#include "hast.h"
#include "hast_proto.h"
#include "metadata.h"
#include "nv.h"
#include "pjdlog.h"
#include "proto.h"
#include "subr.h"

/* Path to configuration file. */
static const char *cfgpath = HAST_CONFIG;
/* Hastd configuration. */
static struct hastd_config *cfg;
/* Control connection. */
static struct proto_conn *controlconn;

enum {
	CMD_INVALID,
	CMD_CREATE,
	CMD_ROLE,
	CMD_STATUS,
	CMD_DUMP,
	CMD_LIST
};

static __dead2 void
usage(void)
{

	fprintf(stderr,
	    "usage: %s create [-d] [-c config] [-e extentsize] [-k keepdirty]\n"
	    "\t\t[-m mediasize] name ...\n",
	    getprogname());
	fprintf(stderr,
	    "       %s role [-d] [-c config] <init | primary | secondary> all | name ...\n",
	    getprogname());
	fprintf(stderr,
	    "       %s list [-d] [-c config] [all | name ...]\n",
	    getprogname());
	fprintf(stderr,
	    "       %s status [-d] [-c config] [all | name ...]\n",
	    getprogname());
	fprintf(stderr,
	    "       %s dump [-d] [-c config] [all | name ...]\n",
	    getprogname());
	exit(EX_USAGE);
}

static int
create_one(struct hast_resource *res, intmax_t mediasize, intmax_t extentsize,
    intmax_t keepdirty)
{
	unsigned char *buf;
	size_t mapsize;
	int ec;

	ec = 0;
	pjdlog_prefix_set("[%s] ", res->hr_name);

	if (provinfo(res, true) == -1) {
		ec = EX_NOINPUT;
		goto end;
	}
	if (mediasize == 0)
		mediasize = res->hr_local_mediasize;
	else if (mediasize > res->hr_local_mediasize) {
		pjdlog_error("Provided mediasize is larger than provider %s size.",
		    res->hr_localpath);
		ec = EX_DATAERR;
		goto end;
	}
	if (!powerof2(res->hr_local_sectorsize)) {
		pjdlog_error("Sector size of provider %s is not power of 2 (%u).",
		    res->hr_localpath, res->hr_local_sectorsize);
		ec = EX_DATAERR;
		goto end;
	}
	if (extentsize == 0)
		extentsize = HAST_EXTENTSIZE;
	if (extentsize < res->hr_local_sectorsize) {
		pjdlog_error("Extent size (%jd) is less than sector size (%u).",
		    (intmax_t)extentsize, res->hr_local_sectorsize);
		ec = EX_DATAERR;
		goto end;
	}
	if ((extentsize % res->hr_local_sectorsize) != 0) {
		pjdlog_error("Extent size (%jd) is not multiple of sector size (%u).",
		    (intmax_t)extentsize, res->hr_local_sectorsize);
		ec = EX_DATAERR;
		goto end;
	}
	mapsize = activemap_calc_ondisk_size(mediasize - METADATA_SIZE,
	    extentsize, res->hr_local_sectorsize);
	if (keepdirty == 0)
		keepdirty = HAST_KEEPDIRTY;
	res->hr_datasize = mediasize - METADATA_SIZE - mapsize;
	res->hr_extentsize = extentsize;
	res->hr_keepdirty = keepdirty;

	res->hr_localoff = METADATA_SIZE + mapsize;

	if (metadata_write(res) == -1) {
		ec = EX_IOERR;
		goto end;
	}
	buf = calloc(1, mapsize);
	if (buf == NULL) {
		pjdlog_error("Unable to allocate %zu bytes of memory for initial bitmap.",
		    mapsize);
		ec = EX_TEMPFAIL;
		goto end;
	}
	if (pwrite(res->hr_localfd, buf, mapsize, METADATA_SIZE) !=
	    (ssize_t)mapsize) {
		pjdlog_errno(LOG_ERR, "Unable to store initial bitmap on %s",
		    res->hr_localpath);
		free(buf);
		ec = EX_IOERR;
		goto end;
	}
	free(buf);
end:
	if (res->hr_localfd >= 0)
		close(res->hr_localfd);
	pjdlog_prefix_set("%s", "");
	return (ec);
}

static void
control_create(int argc, char *argv[], intmax_t mediasize, intmax_t extentsize,
    intmax_t keepdirty)
{
	struct hast_resource *res;
	int ec, ii, ret;

	/* Initialize the given resources. */
	if (argc < 1)
		usage();
	ec = 0;
	for (ii = 0; ii < argc; ii++) {
		TAILQ_FOREACH(res, &cfg->hc_resources, hr_next) {
			if (strcmp(argv[ii], res->hr_name) == 0)
				break;
		}
		if (res == NULL) {
			pjdlog_error("Unknown resource %s.", argv[ii]);
			if (ec == 0)
				ec = EX_DATAERR;
			continue;
		}
		ret = create_one(res, mediasize, extentsize, keepdirty);
		if (ret != 0 && ec == 0)
			ec = ret;
	}
	exit(ec);
}

static int
dump_one(struct hast_resource *res)
{
	int ret;

	ret = metadata_read(res, false);
	if (ret != 0)
		return (ret);

	printf("resource: %s\n", res->hr_name);
	printf("    datasize: %ju (%NB)\n", (uintmax_t)res->hr_datasize,
	    (intmax_t)res->hr_datasize);
	printf("    extentsize: %d (%NB)\n", res->hr_extentsize,
	    (intmax_t)res->hr_extentsize);
	printf("    keepdirty: %d\n", res->hr_keepdirty);
	printf("    localoff: %ju\n", (uintmax_t)res->hr_localoff);
	printf("    resuid: %ju\n", (uintmax_t)res->hr_resuid);
	printf("    localcnt: %ju\n", (uintmax_t)res->hr_primary_localcnt);
	printf("    remotecnt: %ju\n", (uintmax_t)res->hr_primary_remotecnt);
	printf("    prevrole: %s\n", role2str(res->hr_previous_role));

	return (0);
}

static void
control_dump(int argc, char *argv[])
{
	struct hast_resource *res;
	int ec, ret;

	/* Dump metadata of the given resource(s). */

	ec = 0;
	if (argc == 0 || (argc == 1 && strcmp(argv[0], "all") == 0)) {
		TAILQ_FOREACH(res, &cfg->hc_resources, hr_next) {
			ret = dump_one(res);
			if (ret != 0 && ec == 0)
				ec = ret;
		}
	} else {
		int ii;

		for (ii = 0; ii < argc; ii++) {
			TAILQ_FOREACH(res, &cfg->hc_resources, hr_next) {
				if (strcmp(argv[ii], res->hr_name) == 0)
					break;
			}
			if (res == NULL) {
				pjdlog_error("Unknown resource %s.", argv[ii]);
				if (ec == 0)
					ec = EX_DATAERR;
				continue;
			}
			ret = dump_one(res);
			if (ret != 0 && ec == 0)
				ec = ret;
		}
	}
	exit(ec);
}

static int
control_set_role(struct nv *nv, const char *newrole)
{
	const char *res, *oldrole;
	unsigned int ii;
	int error, ret;

	ret = 0;

	for (ii = 0; ; ii++) {
		res = nv_get_string(nv, "resource%u", ii);
		if (res == NULL)
			break;
		pjdlog_prefix_set("[%s] ", res);
		error = nv_get_int16(nv, "error%u", ii);
		if (error != 0) {
			if (ret == 0)
				ret = error;
			pjdlog_warning("Received error %d from hastd.", error);
			continue;
		}
		oldrole = nv_get_string(nv, "role%u", ii);
		if (strcmp(oldrole, newrole) == 0)
			pjdlog_debug(2, "Role unchanged (%s).", oldrole);
		else {
			pjdlog_debug(1, "Role changed from %s to %s.", oldrole,
			    newrole);
		}
	}
	pjdlog_prefix_set("%s", "");
	return (ret);
}

static int
control_list(struct nv *nv)
{
	pid_t pid;
	unsigned int ii;
	const char *str;
	int error, ret;

	ret = 0;

	for (ii = 0; ; ii++) {
		str = nv_get_string(nv, "resource%u", ii);
		if (str == NULL)
			break;
		printf("%s:\n", str);
		error = nv_get_int16(nv, "error%u", ii);
		if (error != 0) {
			if (ret == 0)
				ret = error;
			printf("  error: %d\n", error);
			continue;
		}
		printf("  role: %s\n", nv_get_string(nv, "role%u", ii));
		printf("  provname: %s\n",
		    nv_get_string(nv, "provname%u", ii));
		printf("  localpath: %s\n",
		    nv_get_string(nv, "localpath%u", ii));
		printf("  extentsize: %u (%NB)\n",
		    (unsigned int)nv_get_uint32(nv, "extentsize%u", ii),
		    (intmax_t)nv_get_uint32(nv, "extentsize%u", ii));
		printf("  keepdirty: %u\n",
		    (unsigned int)nv_get_uint32(nv, "keepdirty%u", ii));
		printf("  remoteaddr: %s\n",
		    nv_get_string(nv, "remoteaddr%u", ii));
		str = nv_get_string(nv, "sourceaddr%u", ii);
		if (str != NULL)
			printf("  sourceaddr: %s\n", str);
		printf("  replication: %s\n",
		    nv_get_string(nv, "replication%u", ii));
		str = nv_get_string(nv, "status%u", ii);
		if (str != NULL)
			printf("  status: %s\n", str);
		pid = nv_get_int32(nv, "workerpid%u", ii);
		if (pid != 0)
			printf("  workerpid: %d\n", pid);
		printf("  dirty: %ju (%NB)\n",
		    (uintmax_t)nv_get_uint64(nv, "dirty%u", ii),
		    (intmax_t)nv_get_uint64(nv, "dirty%u", ii));
		printf("  statistics:\n");
		printf("    reads: %ju\n",
		    (uintmax_t)nv_get_uint64(nv, "stat_read%u", ii));
		printf("    writes: %ju\n",
		    (uintmax_t)nv_get_uint64(nv, "stat_write%u", ii));
		printf("    deletes: %ju\n",
		    (uintmax_t)nv_get_uint64(nv, "stat_delete%u", ii));
		printf("    flushes: %ju\n",
		    (uintmax_t)nv_get_uint64(nv, "stat_flush%u", ii));
		printf("    activemap updates: %ju\n",
		    (uintmax_t)nv_get_uint64(nv, "stat_activemap_update%u", ii));
		printf("    local errors: "
		    "read: %ju, write: %ju, delete: %ju, flush: %ju\n",
		    (uintmax_t)nv_get_uint64(nv, "stat_read_error%u", ii),
		    (uintmax_t)nv_get_uint64(nv, "stat_write_error%u", ii),
		    (uintmax_t)nv_get_uint64(nv, "stat_delete_error%u", ii),
		    (uintmax_t)nv_get_uint64(nv, "stat_flush_error%u", ii));
		printf("    queues: "
		    "local: %ju, send: %ju, recv: %ju, done: %ju, idle: %ju\n",
		    (uintmax_t)nv_get_uint64(nv, "local_queue_size%u", ii),
		    (uintmax_t)nv_get_uint64(nv, "send_queue_size%u", ii),
		    (uintmax_t)nv_get_uint64(nv, "recv_queue_size%u", ii),
		    (uintmax_t)nv_get_uint64(nv, "done_queue_size%u", ii),
		    (uintmax_t)nv_get_uint64(nv, "idle_queue_size%u", ii));
	}
	return (ret);
}

static int
control_status(struct nv *nv)
{
	unsigned int ii;
	const char *str;
	int error, hprinted, ret;

	hprinted = 0;
	ret = 0;

	for (ii = 0; ; ii++) {
		str = nv_get_string(nv, "resource%u", ii);
		if (str == NULL)
			break;
		if (!hprinted) {
			printf("Name\tStatus\t Role\t\tComponents\n");
			hprinted = 1;
		}
		printf("%s\t", str);
		error = nv_get_int16(nv, "error%u", ii);
		if (error != 0) {
			if (ret == 0)
				ret = error;
			printf("ERR%d\n", error);
			continue;
		}
		str = nv_get_string(nv, "status%u", ii);
		printf("%-9s", (str != NULL) ? str : "-");
		printf("%-15s", nv_get_string(nv, "role%u", ii));
		printf("%s\t",
		    nv_get_string(nv, "localpath%u", ii));
		printf("%s\n",
		    nv_get_string(nv, "remoteaddr%u", ii));
	}
	return (ret);
}

int
main(int argc, char *argv[])
{
	struct nv *nv;
	int64_t mediasize, extentsize, keepdirty;
	int cmd, debug, error, ii;
	const char *optstr;

	debug = 0;
	mediasize = extentsize = keepdirty = 0;

	if (argc == 1)
		usage();

	if (strcmp(argv[1], "create") == 0) {
		cmd = CMD_CREATE;
		optstr = "c:de:k:m:h";
	} else if (strcmp(argv[1], "role") == 0) {
		cmd = CMD_ROLE;
		optstr = "c:dh";
	} else if (strcmp(argv[1], "list") == 0) {
		cmd = CMD_LIST;
		optstr = "c:dh";
	} else if (strcmp(argv[1], "status") == 0) {
		cmd = CMD_STATUS;
		optstr = "c:dh";
	} else if (strcmp(argv[1], "dump") == 0) {
		cmd = CMD_DUMP;
		optstr = "c:dh";
	} else
		usage();

	argc--;
	argv++;

	for (;;) {
		int ch;

		ch = getopt(argc, argv, optstr);
		if (ch == -1)
			break;
		switch (ch) {
		case 'c':
			cfgpath = optarg;
			break;
		case 'd':
			debug++;
			break;
		case 'e':
			if (expand_number(optarg, &extentsize) == -1)
				errx(EX_USAGE, "Invalid extentsize");
			break;
		case 'k':
			if (expand_number(optarg, &keepdirty) == -1)
				errx(EX_USAGE, "Invalid keepdirty");
			break;
		case 'm':
			if (expand_number(optarg, &mediasize) == -1)
				errx(EX_USAGE, "Invalid mediasize");
			break;
		case 'h':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	switch (cmd) {
	case CMD_CREATE:
	case CMD_ROLE:
		if (argc == 0)
			usage();
		break;
	}

	pjdlog_init(PJDLOG_MODE_STD);
	pjdlog_debug_set(debug);

	cfg = yy_config_parse(cfgpath, true);
	PJDLOG_ASSERT(cfg != NULL);

	switch (cmd) {
	case CMD_CREATE:
		control_create(argc, argv, mediasize, extentsize, keepdirty);
		/* NOTREACHED */
		PJDLOG_ABORT("What are we doing here?!");
		break;
	case CMD_DUMP:
		/* Dump metadata from local component of the given resource. */
		control_dump(argc, argv);
		/* NOTREACHED */
		PJDLOG_ABORT("What are we doing here?!");
		break;
	case CMD_ROLE:
		/* Change role for the given resources. */
		if (argc < 2)
			usage();
		nv = nv_alloc();
		nv_add_uint8(nv, HASTCTL_CMD_SETROLE, "cmd");
		if (strcmp(argv[0], "init") == 0)
			nv_add_uint8(nv, HAST_ROLE_INIT, "role");
		else if (strcmp(argv[0], "primary") == 0)
			nv_add_uint8(nv, HAST_ROLE_PRIMARY, "role");
		else if (strcmp(argv[0], "secondary") == 0)
			nv_add_uint8(nv, HAST_ROLE_SECONDARY, "role");
		else
			usage();
		for (ii = 0; ii < argc - 1; ii++)
			nv_add_string(nv, argv[ii + 1], "resource%d", ii);
		break;
	case CMD_LIST:
	case CMD_STATUS:
		/* Obtain status of the given resources. */
		nv = nv_alloc();
		nv_add_uint8(nv, HASTCTL_CMD_STATUS, "cmd");
		if (argc == 0)
			nv_add_string(nv, "all", "resource%d", 0);
		else {
			for (ii = 0; ii < argc; ii++)
				nv_add_string(nv, argv[ii], "resource%d", ii);
		}
		break;
	default:
		PJDLOG_ABORT("Impossible command!");
	}

	/* Setup control connection... */
	if (proto_client(NULL, cfg->hc_controladdr, &controlconn) == -1) {
		pjdlog_exit(EX_OSERR,
		    "Unable to setup control connection to %s",
		    cfg->hc_controladdr);
	}
	/* ...and connect to hastd. */
	if (proto_connect(controlconn, HAST_TIMEOUT) == -1) {
		pjdlog_exit(EX_OSERR, "Unable to connect to hastd via %s",
		    cfg->hc_controladdr);
	}

	if (drop_privs(NULL) != 0)
		exit(EX_CONFIG);

	/* Send the command to the server... */
	if (hast_proto_send(NULL, controlconn, nv, NULL, 0) == -1) {
		pjdlog_exit(EX_UNAVAILABLE,
		    "Unable to send command to hastd via %s",
		    cfg->hc_controladdr);
	}
	nv_free(nv);
	/* ...and receive reply. */
	if (hast_proto_recv_hdr(controlconn, &nv) == -1) {
		pjdlog_exit(EX_UNAVAILABLE,
		    "cannot receive reply from hastd via %s",
		    cfg->hc_controladdr);
	}

	error = nv_get_int16(nv, "error");
	if (error != 0) {
		pjdlog_exitx(EX_SOFTWARE, "Error %d received from hastd.",
		    error);
	}
	nv_set_error(nv, 0);

	switch (cmd) {
	case CMD_ROLE:
		error = control_set_role(nv, argv[0]);
		break;
	case CMD_LIST:
		error = control_list(nv);
		break;
	case CMD_STATUS:
		error = control_status(nv);
		break;
	default:
		PJDLOG_ABORT("Impossible command!");
	}

	exit(error);
}
