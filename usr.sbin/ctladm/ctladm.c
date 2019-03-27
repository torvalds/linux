/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003, 2004 Silicon Graphics International Corp.
 * Copyright (c) 1997-2007 Kenneth D. Merry
 * Copyright (c) 2012 The FreeBSD Foundation
 * Copyright (c) 2018 Marcelo Araujo <araujo@FreeBSD.org>
 * All rights reserved.
 *
 * Portions of this software were developed by Edward Tomasz Napierala
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/users/kenm/FreeBSD-test2/usr.sbin/ctladm/ctladm.c#4 $
 */
/*
 * CAM Target Layer exercise program.
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/callout.h>
#include <sys/ioctl.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/nv.h>
#include <sys/stat.h>
#include <bsdxml.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl_backend.h>
#include <cam/ctl/ctl_ioctl.h>
#include <cam/ctl/ctl_util.h>
#include <cam/ctl/ctl_scsi_all.h>
#include <camlib.h>
#include <libutil.h>
#include "ctladm.h"

#ifdef min
#undef min
#endif
#define min(x,y) (x < y) ? x : y

typedef enum {
	CTLADM_CMD_TUR,
	CTLADM_CMD_INQUIRY,
	CTLADM_CMD_REQ_SENSE,
	CTLADM_CMD_ARRAYLIST,
	CTLADM_CMD_REPORT_LUNS,
	CTLADM_CMD_HELP,
	CTLADM_CMD_DEVLIST,
	CTLADM_CMD_ADDDEV,
	CTLADM_CMD_RM,
	CTLADM_CMD_CREATE,
	CTLADM_CMD_READ,
	CTLADM_CMD_WRITE,
	CTLADM_CMD_PORT,
	CTLADM_CMD_PORTLIST,
	CTLADM_CMD_READCAPACITY,
	CTLADM_CMD_MODESENSE,
	CTLADM_CMD_DUMPOOA,
	CTLADM_CMD_DUMPSTRUCTS,
	CTLADM_CMD_START,
	CTLADM_CMD_STOP,
	CTLADM_CMD_SYNC_CACHE,
	CTLADM_CMD_LUNLIST,
	CTLADM_CMD_DELAY,
	CTLADM_CMD_ERR_INJECT,
	CTLADM_CMD_PRES_IN,
	CTLADM_CMD_PRES_OUT,
	CTLADM_CMD_INQ_VPD_DEVID,
	CTLADM_CMD_RTPG,
	CTLADM_CMD_MODIFY,
	CTLADM_CMD_ISLIST,
	CTLADM_CMD_ISLOGOUT,
	CTLADM_CMD_ISTERMINATE,
	CTLADM_CMD_LUNMAP
} ctladm_cmdfunction;

typedef enum {
	CTLADM_ARG_NONE		= 0x0000000,
	CTLADM_ARG_AUTOSENSE	= 0x0000001,
	CTLADM_ARG_DEVICE	= 0x0000002,
	CTLADM_ARG_ARRAYSIZE	= 0x0000004,
	CTLADM_ARG_BACKEND	= 0x0000008,
	CTLADM_ARG_CDBSIZE	= 0x0000010,
	CTLADM_ARG_DATALEN	= 0x0000020,
	CTLADM_ARG_FILENAME	= 0x0000040,
	CTLADM_ARG_LBA		= 0x0000080,
	CTLADM_ARG_PC		= 0x0000100,
	CTLADM_ARG_PAGE_CODE	= 0x0000200,
	CTLADM_ARG_PAGE_LIST	= 0x0000400,
	CTLADM_ARG_SUBPAGE	= 0x0000800,
	CTLADM_ARG_PAGELIST	= 0x0001000,
	CTLADM_ARG_DBD		= 0x0002000,
	CTLADM_ARG_TARG_LUN	= 0x0004000,
	CTLADM_ARG_BLOCKSIZE	= 0x0008000,
	CTLADM_ARG_IMMED	= 0x0010000,
	CTLADM_ARG_RELADR	= 0x0020000,
	CTLADM_ARG_RETRIES	= 0x0040000,
	CTLADM_ARG_ONOFFLINE	= 0x0080000,
	CTLADM_ARG_ONESHOT	= 0x0100000,
	CTLADM_ARG_TIMEOUT	= 0x0200000,
	CTLADM_ARG_INITIATOR	= 0x0400000,
	CTLADM_ARG_NOCOPY	= 0x0800000,
	CTLADM_ARG_NEED_TL	= 0x1000000
} ctladm_cmdargs;

struct ctladm_opts {
	const char	*optname;
	uint32_t	cmdnum;
	ctladm_cmdargs	argnum;
	const char	*subopt;
};

typedef enum {
	CC_OR_NOT_FOUND,
	CC_OR_AMBIGUOUS,
	CC_OR_FOUND
} ctladm_optret;

static const char rw_opts[] = "Nb:c:d:f:l:";
static const char startstop_opts[] = "i";

static struct ctladm_opts option_table[] = {
	{"adddev", CTLADM_CMD_ADDDEV, CTLADM_ARG_NONE, NULL},
	{"create", CTLADM_CMD_CREATE, CTLADM_ARG_NONE, "b:B:d:l:o:s:S:t:"},
	{"delay", CTLADM_CMD_DELAY, CTLADM_ARG_NEED_TL, "T:l:t:"},
	{"devid", CTLADM_CMD_INQ_VPD_DEVID, CTLADM_ARG_NEED_TL, NULL},
	{"devlist", CTLADM_CMD_DEVLIST, CTLADM_ARG_NONE, "b:vx"},
	{"dumpooa", CTLADM_CMD_DUMPOOA, CTLADM_ARG_NONE, NULL},
	{"dumpstructs", CTLADM_CMD_DUMPSTRUCTS, CTLADM_ARG_NONE, NULL},
	{"help", CTLADM_CMD_HELP, CTLADM_ARG_NONE, NULL},
	{"inject", CTLADM_CMD_ERR_INJECT, CTLADM_ARG_NEED_TL, "cd:i:p:r:s:"},
	{"inquiry", CTLADM_CMD_INQUIRY, CTLADM_ARG_NEED_TL, NULL},
	{"islist", CTLADM_CMD_ISLIST, CTLADM_ARG_NONE, "vx"},
	{"islogout", CTLADM_CMD_ISLOGOUT, CTLADM_ARG_NONE, "ac:i:p:"},
	{"isterminate", CTLADM_CMD_ISTERMINATE, CTLADM_ARG_NONE, "ac:i:p:"},
	{"lunlist", CTLADM_CMD_LUNLIST, CTLADM_ARG_NONE, NULL},
	{"lunmap", CTLADM_CMD_LUNMAP, CTLADM_ARG_NONE, "p:l:L:"},
	{"modesense", CTLADM_CMD_MODESENSE, CTLADM_ARG_NEED_TL, "P:S:dlm:c:"},
	{"modify", CTLADM_CMD_MODIFY, CTLADM_ARG_NONE, "b:l:o:s:"},
	{"port", CTLADM_CMD_PORT, CTLADM_ARG_NONE, "lo:O:d:crp:qt:w:W:x"},
	{"portlist", CTLADM_CMD_PORTLIST, CTLADM_ARG_NONE, "f:ilp:qvx"},
	{"prin", CTLADM_CMD_PRES_IN, CTLADM_ARG_NEED_TL, "a:"},
	{"prout", CTLADM_CMD_PRES_OUT, CTLADM_ARG_NEED_TL, "a:k:r:s:"},
	{"read", CTLADM_CMD_READ, CTLADM_ARG_NEED_TL, rw_opts},
	{"readcapacity", CTLADM_CMD_READCAPACITY, CTLADM_ARG_NEED_TL, "c:"},
	{"remove", CTLADM_CMD_RM, CTLADM_ARG_NONE, "b:l:o:"},
	{"reportluns", CTLADM_CMD_REPORT_LUNS, CTLADM_ARG_NEED_TL, NULL},
	{"reqsense", CTLADM_CMD_REQ_SENSE, CTLADM_ARG_NEED_TL, NULL},
	{"rtpg", CTLADM_CMD_RTPG, CTLADM_ARG_NEED_TL, NULL},
	{"start", CTLADM_CMD_START, CTLADM_ARG_NEED_TL, startstop_opts},
	{"stop", CTLADM_CMD_STOP, CTLADM_ARG_NEED_TL, startstop_opts},
	{"synccache", CTLADM_CMD_SYNC_CACHE, CTLADM_ARG_NEED_TL, "b:c:il:r"},
	{"tur", CTLADM_CMD_TUR, CTLADM_ARG_NEED_TL, NULL},
	{"write", CTLADM_CMD_WRITE, CTLADM_ARG_NEED_TL, rw_opts},
	{"-?", CTLADM_CMD_HELP, CTLADM_ARG_NONE, NULL},
	{"-h", CTLADM_CMD_HELP, CTLADM_ARG_NONE, NULL},
	{NULL, 0, 0, NULL}
};


ctladm_optret getoption(struct ctladm_opts *table, char *arg, uint32_t *cmdnum,
			ctladm_cmdargs *argnum, const char **subopt);
static int cctl_dump_ooa(int fd, int argc, char **argv);
static int cctl_port(int fd, int argc, char **argv, char *combinedopt);
static int cctl_do_io(int fd, int retries, union ctl_io *io, const char *func);
static int cctl_delay(int fd, int lun, int argc, char **argv,
		      char *combinedopt);
static int cctl_lunlist(int fd);
static int cctl_sync_cache(int fd, int lun, int iid, int retries,
			   int argc, char **argv, char *combinedopt);
static int cctl_start_stop(int fd, int lun, int iid, int retries,
			   int start, int argc, char **argv, char *combinedopt);
static int cctl_mode_sense(int fd, int lun, int iid, int retries,
			   int argc, char **argv, char *combinedopt);
static int cctl_read_capacity(int fd, int lun, int iid,
			      int retries, int argc, char **argv,
			      char *combinedopt);
static int cctl_read_write(int fd, int lun, int iid, int retries,
			   int argc, char **argv, char *combinedopt,
			   ctladm_cmdfunction command);
static int cctl_get_luns(int fd, int lun, int iid, int retries,
			 struct scsi_report_luns_data **lun_data,
			 uint32_t *num_luns);
static int cctl_report_luns(int fd, int lun, int iid, int retries);
static int cctl_tur(int fd, int lun, int iid, int retries);
static int cctl_get_inquiry(int fd, int lun, int iid, int retries,
			    char *path_str, int path_len,
			    struct scsi_inquiry_data *inq_data);
static int cctl_inquiry(int fd, int lun, int iid, int retries);
static int cctl_req_sense(int fd, int lun, int iid, int retries);
static int cctl_persistent_reserve_in(int fd, int lun,
				      int initiator, int argc, char **argv,
				      char *combinedopt, int retry_count);
static int cctl_persistent_reserve_out(int fd, int lun,
				       int initiator, int argc, char **argv,
				       char *combinedopt, int retry_count);
static int cctl_create_lun(int fd, int argc, char **argv, char *combinedopt);
static int cctl_inquiry_vpd_devid(int fd, int lun, int initiator);
static int cctl_report_target_port_group(int fd, int lun, int initiator);
static int cctl_modify_lun(int fd, int argc, char **argv, char *combinedopt);
static int cctl_portlist(int fd, int argc, char **argv, char *combinedopt);

ctladm_optret
getoption(struct ctladm_opts *table, char *arg, uint32_t *cmdnum,
	  ctladm_cmdargs *argnum, const char **subopt)
{
	struct ctladm_opts *opts;
	int num_matches = 0;

	for (opts = table; (opts != NULL) && (opts->optname != NULL);
	     opts++) {
		if (strncmp(opts->optname, arg, strlen(arg)) == 0) {
			*cmdnum = opts->cmdnum;
			*argnum = opts->argnum;
			*subopt = opts->subopt;

			if (strcmp(opts->optname, arg) == 0)
				return (CC_OR_FOUND);

			if (++num_matches > 1)
				return(CC_OR_AMBIGUOUS);
		}
	}

	if (num_matches > 0)
		return(CC_OR_FOUND);
	else
		return(CC_OR_NOT_FOUND);
}

static int
cctl_dump_ooa(int fd, int argc, char **argv)
{
	struct ctl_ooa ooa;
	long double cmd_latency;
	int num_entries, len, lun = -1, retval = 0;
	unsigned int i;

	num_entries = 104;

	if ((argc > 2) && (isdigit(argv[2][0])))
		lun = strtol(argv[2], NULL, 0);
retry:

	len = num_entries * sizeof(struct ctl_ooa_entry);
	bzero(&ooa, sizeof(ooa));
	ooa.entries = malloc(len);
	if (ooa.entries == NULL) {
		warn("%s: error mallocing %d bytes", __func__, len);
		return (1);
	}
	if (lun >= 0) {
		ooa.lun_num = lun;
	} else
		ooa.flags |= CTL_OOA_FLAG_ALL_LUNS;
	ooa.alloc_len = len;
	ooa.alloc_num = num_entries;
	if (ioctl(fd, CTL_GET_OOA, &ooa) == -1) {
		warn("%s: CTL_GET_OOA ioctl failed", __func__);
		retval = 1;
		goto bailout;
	}

	if (ooa.status == CTL_OOA_NEED_MORE_SPACE) {
		num_entries = num_entries * 2;
		free(ooa.entries);
		ooa.entries = NULL;
		goto retry;
	}

	if (ooa.status != CTL_OOA_OK) {
		warnx("%s: CTL_GET_OOA ioctl returned error %d", __func__,
		      ooa.status);
		retval = 1;
		goto bailout;
	}

	fprintf(stdout, "Dumping OOA queues\n");
	for (i = 0; i < ooa.fill_num; i++) {
		struct ctl_ooa_entry *entry;
		char cdb_str[(SCSI_MAX_CDBLEN * 3) +1];
		struct bintime delta_bt;
		struct timespec ts;

		entry = &ooa.entries[i];

		delta_bt = ooa.cur_bt;
		bintime_sub(&delta_bt, &entry->start_bt);
		bintime2timespec(&delta_bt, &ts);
		cmd_latency = ts.tv_sec * 1000;
		if (ts.tv_nsec > 0)
			cmd_latency += ts.tv_nsec / 1000000;

		fprintf(stdout, "LUN %jd tag 0x%04x%s%s%s%s%s: %s. CDB: %s "
			"(%0.0Lf ms)\n",
			(intmax_t)entry->lun_num, entry->tag_num,
			(entry->cmd_flags & CTL_OOACMD_FLAG_BLOCKED) ?
			 " BLOCKED" : "",
			(entry->cmd_flags & CTL_OOACMD_FLAG_DMA) ? " DMA" : "",
			(entry->cmd_flags & CTL_OOACMD_FLAG_DMA_QUEUED) ?
			 " DMAQUEUED" : "",
			(entry->cmd_flags & CTL_OOACMD_FLAG_ABORT) ?
			 " ABORT" : "",
			(entry->cmd_flags & CTL_OOACMD_FLAG_RTR) ? " RTR" :"",
			scsi_op_desc(entry->cdb[0], NULL),
			scsi_cdb_string(entry->cdb, cdb_str, sizeof(cdb_str)),
			cmd_latency);
	}
	fprintf(stdout, "OOA queues dump done\n");

bailout:
	free(ooa.entries);
	return (retval);
}

static int
cctl_dump_structs(int fd, ctladm_cmdargs cmdargs __unused)
{
	if (ioctl(fd, CTL_DUMP_STRUCTS) == -1) {
		warn(__func__);
		return (1);
	}
	return (0);
}

typedef enum {
	CCTL_PORT_MODE_NONE,
	CCTL_PORT_MODE_LIST,
	CCTL_PORT_MODE_SET,
	CCTL_PORT_MODE_ON,
	CCTL_PORT_MODE_OFF,
	CCTL_PORT_MODE_CREATE,
	CCTL_PORT_MODE_REMOVE
} cctl_port_mode;

static struct ctladm_opts cctl_fe_table[] = {
	{"fc", CTL_PORT_FC, CTLADM_ARG_NONE, NULL},
	{"scsi", CTL_PORT_SCSI, CTLADM_ARG_NONE, NULL},
	{"internal", CTL_PORT_INTERNAL, CTLADM_ARG_NONE, NULL},
	{"iscsi", CTL_PORT_ISCSI, CTLADM_ARG_NONE, NULL},
	{"sas", CTL_PORT_SAS, CTLADM_ARG_NONE, NULL},
	{"all", CTL_PORT_ALL, CTLADM_ARG_NONE, NULL},
	{NULL, 0, 0, NULL}
};

static int
cctl_port(int fd, int argc, char **argv, char *combinedopt)
{
	int c;
	int32_t targ_port = -1;
	int retval = 0;
	int wwnn_set = 0, wwpn_set = 0;
	uint64_t wwnn = 0, wwpn = 0;
	cctl_port_mode port_mode = CCTL_PORT_MODE_NONE;
	struct ctl_port_entry entry;
	struct ctl_req req;
	char *driver = NULL;
	nvlist_t *option_list;
	ctl_port_type port_type = CTL_PORT_NONE;
	int quiet = 0, xml = 0;

	option_list = nvlist_create(0);
	if (option_list == NULL)
		err(1, "%s: unable to allocate nvlist", __func__);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'l':
			if (port_mode != CCTL_PORT_MODE_NONE)
				goto bailout_badarg;

			port_mode = CCTL_PORT_MODE_LIST;
			break;
		case 'c':
			port_mode = CCTL_PORT_MODE_CREATE;
			break;
		case 'r':
			port_mode = CCTL_PORT_MODE_REMOVE;
			break;
		case 'o':
			if (port_mode != CCTL_PORT_MODE_NONE)
				goto bailout_badarg;

			if (strcasecmp(optarg, "on") == 0)
				port_mode = CCTL_PORT_MODE_ON;
			else if (strcasecmp(optarg, "off") == 0)
				port_mode = CCTL_PORT_MODE_OFF;
			else {
				warnx("Invalid -o argument %s, \"on\" or "
				      "\"off\" are the only valid args",
				      optarg);
				retval = 1;
				goto bailout;
			}
			break;
		case 'O': {
			char *tmpstr;
			char *name, *value;

			tmpstr = strdup(optarg);
			name = strsep(&tmpstr, "=");
			if (name == NULL) {
				warnx("%s: option -O takes \"name=value\""
				      "argument", __func__);
				retval = 1;
				goto bailout;
			}
			value = strsep(&tmpstr, "=");
			if (value == NULL) {
				warnx("%s: option -O takes \"name=value\""
				      "argument", __func__);
				retval = 1;
				goto bailout;
			}

			free(tmpstr);
			nvlist_add_string(option_list, name, value);
			break;
		}
		case 'd':
			if (driver != NULL) {
				warnx("%s: option -d cannot be specified twice",
				    __func__);
				retval = 1;
				goto bailout;
			}

			driver = strdup(optarg);
			break;
		case 'p':
			targ_port = strtol(optarg, NULL, 0);
			break;
		case 'q':
			quiet = 1;
			break;
		case 't': {
			ctladm_optret optret;
			ctladm_cmdargs argnum;
			const char *subopt;
			ctl_port_type tmp_port_type;

			optret = getoption(cctl_fe_table, optarg, &tmp_port_type,
					   &argnum, &subopt);
			if (optret == CC_OR_AMBIGUOUS) {
				warnx("%s: ambiguous frontend type %s",
				      __func__, optarg);
				retval = 1;
				goto bailout;
			} else if (optret == CC_OR_NOT_FOUND) {
				warnx("%s: invalid frontend type %s",
				      __func__, optarg);
				retval = 1;
				goto bailout;
			}

			port_type |= tmp_port_type;
			break;
		}
		case 'w':
			if ((port_mode != CCTL_PORT_MODE_NONE)
			 && (port_mode != CCTL_PORT_MODE_SET))
				goto bailout_badarg;

			port_mode = CCTL_PORT_MODE_SET;

			wwnn = strtoull(optarg, NULL, 0);
			wwnn_set = 1;
			break;
		case 'W':
			if ((port_mode != CCTL_PORT_MODE_NONE)
			 && (port_mode != CCTL_PORT_MODE_SET))
				goto bailout_badarg;

			port_mode = CCTL_PORT_MODE_SET;

			wwpn = strtoull(optarg, NULL, 0);
			wwpn_set = 1;
			break;
		case 'x':
			xml = 1;
			break;
		}
	}

	if (driver == NULL)
		driver = strdup("ioctl");

	/*
	 * The user can specify either one or more frontend types (-t), or
	 * a specific frontend, but not both.
	 *
	 * If the user didn't specify a frontend type or number, set it to
	 * all.  This is primarily needed for the enable/disable ioctls.
	 * This will be a no-op for the listing code.  For the set ioctl,
	 * we'll throw an error, since that only works on one port at a time.
	 */
	if ((port_type != CTL_PORT_NONE) && (targ_port != -1)) {
		warnx("%s: can only specify one of -t or -n", __func__);
		retval = 1;
		goto bailout;
	} else if ((targ_port == -1) && (port_type == CTL_PORT_NONE))
		port_type = CTL_PORT_ALL;

	bzero(&entry, sizeof(entry));

	/*
	 * These are needed for all but list/dump mode.
	 */
	entry.port_type = port_type;
	entry.targ_port = targ_port;

	switch (port_mode) {
	case CCTL_PORT_MODE_LIST: {
		char opts[] = "xq";
		char argx[] = "-x";
		char argq[] = "-q";
		char *argvx[2];
		int argcx = 0;

		optind = 0;
		optreset = 1;
		if (xml)
			argvx[argcx++] = argx;
		if (quiet)
			argvx[argcx++] = argq;
		cctl_portlist(fd, argcx, argvx, opts);
		break;
	}
	case CCTL_PORT_MODE_REMOVE:
		if (targ_port == -1) {
			warnx("%s: -r require -p", __func__);
			retval = 1;
			goto bailout;
		}
	case CCTL_PORT_MODE_CREATE: {
		bzero(&req, sizeof(req));
		strlcpy(req.driver, driver, sizeof(req.driver));

		if (port_mode == CCTL_PORT_MODE_REMOVE) {
			req.reqtype = CTL_REQ_REMOVE;
			nvlist_add_stringf(option_list, "port_id", "%d",
			    targ_port);
		} else
			req.reqtype = CTL_REQ_CREATE;

		req.args = nvlist_pack(option_list, &req.args_len);
		if (req.args == NULL) {
			warn("%s: error packing nvlist", __func__);
			retval = 1;
			goto bailout;
		}

		retval = ioctl(fd, CTL_PORT_REQ, &req);
		free(req.args);
		if (retval == -1) {
			warn("%s: CTL_PORT_REQ ioctl failed", __func__);
			retval = 1;
			goto bailout;
		}

		switch (req.status) {
		case CTL_LUN_ERROR:
			warnx("error: %s", req.error_str);
			retval = 1;
			goto bailout;
		case CTL_LUN_WARNING:
			warnx("warning: %s", req.error_str);
			break;
		case CTL_LUN_OK:
			break;
		default:
			warnx("unknown status: %d", req.status);
			retval = 1;
			goto bailout;
		}

		break;
	}
	case CCTL_PORT_MODE_SET:
		if (targ_port == -1) {
			warnx("%s: -w and -W require -n", __func__);
			retval = 1;
			goto bailout;
		}

		if (wwnn_set) {
			entry.flags |= CTL_PORT_WWNN_VALID;
			entry.wwnn = wwnn;
		}
		if (wwpn_set) {
			entry.flags |= CTL_PORT_WWPN_VALID;
			entry.wwpn = wwpn;
		}

		if (ioctl(fd, CTL_SET_PORT_WWNS, &entry) == -1) {
			warn("%s: CTL_SET_PORT_WWNS ioctl failed", __func__);
			retval = 1;
			goto bailout;
		}
		break;
	case CCTL_PORT_MODE_ON:
		if (ioctl(fd, CTL_ENABLE_PORT, &entry) == -1) {
			warn("%s: CTL_ENABLE_PORT ioctl failed", __func__);
			retval = 1;
			goto bailout;
		}
		fprintf(stdout, "Front End Ports enabled\n");
		break;
	case CCTL_PORT_MODE_OFF:
		if (ioctl(fd, CTL_DISABLE_PORT, &entry) == -1) {
			warn("%s: CTL_DISABLE_PORT ioctl failed", __func__);
			retval = 1;
			goto bailout;
		}
		fprintf(stdout, "Front End Ports disabled\n");
		break;
	default:
		warnx("%s: one of -l, -o or -w/-W must be specified", __func__);
		retval = 1;
		goto bailout;
		break;
	}

bailout:
	nvlist_destroy(option_list);
	free(driver);
	return (retval);

bailout_badarg:
	warnx("%s: only one of -l, -o or -w/-W may be specified", __func__);
	return (1);
}

static int
cctl_do_io(int fd, int retries, union ctl_io *io, const char *func)
{
	do {
		if (ioctl(fd, CTL_IO, io) == -1) {
			warn("%s: error sending CTL_IO ioctl", func);
			return (-1);
		}
	} while (((io->io_hdr.status & CTL_STATUS_MASK) != CTL_SUCCESS)
	      && (retries-- > 0));

	return (0);
}

static int
cctl_delay(int fd, int lun, int argc, char **argv,
	   char *combinedopt)
{
	struct ctl_io_delay_info delay_info;
	char *delayloc = NULL;
	char *delaytype = NULL;
	int delaytime = -1;
	int retval;
	int c;

	retval = 0;

	memset(&delay_info, 0, sizeof(delay_info));

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'T':
			delaytype = strdup(optarg);
			break;
		case 'l':
			delayloc = strdup(optarg);
			break;
		case 't':
			delaytime = strtoul(optarg, NULL, 0);
			break;
		}
	}

	if (delaytime == -1) {
		warnx("%s: you must specify the delaytime with -t", __func__);
		retval = 1;
		goto bailout;
	}

	if (strcasecmp(delayloc, "datamove") == 0)
		delay_info.delay_loc = CTL_DELAY_LOC_DATAMOVE;
	else if (strcasecmp(delayloc, "done") == 0)
		delay_info.delay_loc = CTL_DELAY_LOC_DONE;
	else {
		warnx("%s: invalid delay location %s", __func__, delayloc);
		retval = 1;
		goto bailout;
	}

	if ((delaytype == NULL)
	 || (strcmp(delaytype, "oneshot") == 0))
		delay_info.delay_type = CTL_DELAY_TYPE_ONESHOT;
	else if (strcmp(delaytype, "cont") == 0)
		delay_info.delay_type = CTL_DELAY_TYPE_CONT;
	else {
		warnx("%s: invalid delay type %s", __func__, delaytype);
		retval = 1;
		goto bailout;
	}

	delay_info.lun_id = lun;
	delay_info.delay_secs = delaytime;

	if (ioctl(fd, CTL_DELAY_IO, &delay_info) == -1) {
		warn("%s: CTL_DELAY_IO ioctl failed", __func__);
		retval = 1;
		goto bailout;
	}
	switch (delay_info.status) {
	case CTL_DELAY_STATUS_NONE:
		warnx("%s: no delay status??", __func__);
		retval = 1;
		break;
	case CTL_DELAY_STATUS_OK:
		break;
	case CTL_DELAY_STATUS_INVALID_LUN:
		warnx("%s: invalid lun %d", __func__, lun);
		retval = 1;
		break;
	case CTL_DELAY_STATUS_INVALID_TYPE:
		warnx("%s: invalid delay type %d", __func__,
		      delay_info.delay_type);
		retval = 1;
		break;
	case CTL_DELAY_STATUS_INVALID_LOC:
		warnx("%s: delay location %s not implemented?", __func__,
		      delayloc);
		retval = 1;
		break;
	case CTL_DELAY_STATUS_NOT_IMPLEMENTED:
		warnx("%s: delay not implemented in the kernel", __func__);
		warnx("%s: recompile with the CTL_IO_DELAY flag set", __func__);
		retval = 1;
		break;
	default:
		warnx("%s: unknown delay return status %d", __func__,
		      delay_info.status);
		retval = 1;
		break;
	}

bailout:
	free(delayloc);
	free(delaytype);
	return (retval);
}

static struct ctladm_opts cctl_err_types[] = {
	{"aborted", CTL_LUN_INJ_ABORTED, CTLADM_ARG_NONE, NULL},
	{"mediumerr", CTL_LUN_INJ_MEDIUM_ERR, CTLADM_ARG_NONE, NULL},
	{"ua", CTL_LUN_INJ_UA, CTLADM_ARG_NONE, NULL},
	{"custom", CTL_LUN_INJ_CUSTOM, CTLADM_ARG_NONE, NULL},
	{NULL, 0, 0, NULL}

};

static struct ctladm_opts cctl_err_patterns[] = {
	{"read", CTL_LUN_PAT_READ, CTLADM_ARG_NONE, NULL},
	{"write", CTL_LUN_PAT_WRITE, CTLADM_ARG_NONE, NULL},
	{"rw", CTL_LUN_PAT_READWRITE, CTLADM_ARG_NONE, NULL},
	{"readwrite", CTL_LUN_PAT_READWRITE, CTLADM_ARG_NONE, NULL},
	{"readcap", CTL_LUN_PAT_READCAP, CTLADM_ARG_NONE, NULL},
	{"tur", CTL_LUN_PAT_TUR, CTLADM_ARG_NONE, NULL},
	{"any", CTL_LUN_PAT_ANY, CTLADM_ARG_NONE, NULL},
#if 0
	{"cmd", CTL_LUN_PAT_CMD,  CTLADM_ARG_NONE, NULL},
#endif
	{NULL, 0, 0, NULL}
};

static int
cctl_error_inject(int fd, uint32_t lun, int argc, char **argv,
		  char *combinedopt)
{
	int retval = 0;
	struct ctl_error_desc err_desc;
	uint64_t lba = 0;
	uint32_t len = 0;
	uint64_t delete_id = 0;
	int delete_id_set = 0;
	int continuous = 0;
	int sense_len = 0;
	int fd_sense = 0;
	int c;

	bzero(&err_desc, sizeof(err_desc));
	err_desc.lun_id = lun;

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'c':
			continuous = 1;
			break;
		case 'd':
			delete_id = strtoull(optarg, NULL, 0);
			delete_id_set = 1;
			break;
		case 'i':
		case 'p': {
			ctladm_optret optret;
			ctladm_cmdargs argnum;
			const char *subopt;

			if (c == 'i') {
				ctl_lun_error err_type;

				if (err_desc.lun_error != CTL_LUN_INJ_NONE) {
					warnx("%s: can't specify multiple -i "
					      "arguments", __func__);
					retval = 1;
					goto bailout;
				}
				optret = getoption(cctl_err_types, optarg,
						   &err_type, &argnum, &subopt);
				err_desc.lun_error = err_type;
			} else {
				ctl_lun_error_pattern pattern;

				optret = getoption(cctl_err_patterns, optarg,
						   &pattern, &argnum, &subopt);
				err_desc.error_pattern |= pattern;
			}

			if (optret == CC_OR_AMBIGUOUS) {
				warnx("%s: ambiguous argument %s", __func__,
				      optarg);
				retval = 1;
				goto bailout;
			} else if (optret == CC_OR_NOT_FOUND) {
				warnx("%s: argument %s not found", __func__,
				      optarg);
				retval = 1;
				goto bailout;
			}
			break;
		}
		case 'r': {
			char *tmpstr, *tmpstr2;

			tmpstr = strdup(optarg);
			if (tmpstr == NULL) {
				warn("%s: error duplicating string %s",
				     __func__, optarg);
				retval = 1;
				goto bailout;
			}

			tmpstr2 = strsep(&tmpstr, ",");
			if (tmpstr2 == NULL) {
				warnx("%s: invalid -r argument %s", __func__,
				      optarg);
				retval = 1;
				free(tmpstr);
				goto bailout;
			}
			lba = strtoull(tmpstr2, NULL, 0);
			tmpstr2 = strsep(&tmpstr, ",");
			if (tmpstr2 == NULL) {
				warnx("%s: no len argument for -r lba,len, got"
				      " %s", __func__, optarg);
				retval = 1;
				free(tmpstr);
				goto bailout;
			}
			len = strtoul(tmpstr2, NULL, 0);
			free(tmpstr);
			break;
		}
		case 's': {
			struct get_hook hook;
			char *sensestr;

			sense_len = strtol(optarg, NULL, 0);
			if (sense_len <= 0) {
				warnx("invalid number of sense bytes %d",
				      sense_len);
				retval = 1;
				goto bailout;
			}

			sense_len = MIN(sense_len, SSD_FULL_SIZE);

			hook.argc = argc - optind;
			hook.argv = argv + optind;
			hook.got = 0;

			sensestr = cget(&hook, NULL);
			if ((sensestr != NULL)
			 && (sensestr[0] == '-')) {
				fd_sense = 1;
			} else {
				buff_encode_visit(
				    (uint8_t *)&err_desc.custom_sense,
				    sense_len, sensestr, iget, &hook);
			}
			optind += hook.got;
			break;
		}
		default:
			break;
		}
	}

	if (delete_id_set != 0) {
		err_desc.serial = delete_id;
		if (ioctl(fd, CTL_ERROR_INJECT_DELETE, &err_desc) == -1) {
			warn("%s: error issuing CTL_ERROR_INJECT_DELETE ioctl",
			     __func__);
			retval = 1;
		}
		goto bailout;
	}

	if (err_desc.lun_error == CTL_LUN_INJ_NONE) {
		warnx("%s: error injection command (-i) needed",
		      __func__);
		retval = 1;
		goto bailout;
	} else if ((err_desc.lun_error == CTL_LUN_INJ_CUSTOM)
		&& (sense_len == 0)) {
		warnx("%s: custom error requires -s", __func__);
		retval = 1;
		goto bailout;
	}

	if (continuous != 0)
		err_desc.lun_error |= CTL_LUN_INJ_CONTINUOUS;

	/*
	 * If fd_sense is set, we need to read the sense data the user
	 * wants returned from stdin.
	 */
        if (fd_sense == 1) {
		ssize_t amt_read;
		int amt_to_read = sense_len;
		u_int8_t *buf_ptr = (uint8_t *)&err_desc.custom_sense;

		for (amt_read = 0; amt_to_read > 0;
		     amt_read = read(STDIN_FILENO, buf_ptr, amt_to_read)) {
			if (amt_read == -1) {
				warn("error reading sense data from stdin");
				retval = 1;
				goto bailout;
			}
			amt_to_read -= amt_read;
			buf_ptr += amt_read;
		}
	}

	if (err_desc.error_pattern == CTL_LUN_PAT_NONE) {
		warnx("%s: command pattern (-p) needed", __func__);
		retval = 1;
		goto bailout;
	}

	if (len != 0) {
		err_desc.error_pattern |= CTL_LUN_PAT_RANGE;
		/*
		 * We could check here to see whether it's a read/write
		 * command, but that will be pointless once we allow
		 * custom patterns.  At that point, the user could specify
		 * a READ(6) CDB type, and we wouldn't have an easy way here
		 * to verify whether range checking is possible there.  The
		 * user will just figure it out when his error never gets
		 * executed.
		 */
#if 0
		if ((err_desc.pattern & CTL_LUN_PAT_READWRITE) == 0) {
			warnx("%s: need read and/or write pattern if range "
			      "is specified", __func__);
			retval = 1;
			goto bailout;
		}
#endif
		err_desc.lba_range.lba = lba;
		err_desc.lba_range.len = len;
	}

	if (ioctl(fd, CTL_ERROR_INJECT, &err_desc) == -1) {
		warn("%s: error issuing CTL_ERROR_INJECT ioctl", __func__);
		retval = 1;
	} else {
		printf("Error injection succeeded, serial number is %ju\n",
		       (uintmax_t)err_desc.serial);
	}
bailout:

	return (retval);
}

static int
cctl_lunlist(int fd)
{
	struct scsi_report_luns_data *lun_data;
	struct scsi_inquiry_data *inq_data;
	uint32_t num_luns;
	int initid;
	unsigned int i;
	int retval;

	inq_data = NULL;
	initid = 7;

	/*
	 * XXX KDM assuming LUN 0 is fine, but we may need to change this
	 * if we ever acquire the ability to have multiple targets.
	 */
	if ((retval = cctl_get_luns(fd, /*lun*/ 0, initid,
				    /*retries*/ 2, &lun_data, &num_luns)) != 0)
		goto bailout;

	inq_data = malloc(sizeof(*inq_data));
	if (inq_data == NULL) {
		warn("%s: couldn't allocate memory for inquiry data\n",
		     __func__);
		retval = 1;
		goto bailout;
	}
	for (i = 0; i < num_luns; i++) {
		char scsi_path[40];
		int lun_val;

		switch (lun_data->luns[i].lundata[0] & RPL_LUNDATA_ATYP_MASK) {
		case RPL_LUNDATA_ATYP_PERIPH:
			lun_val = lun_data->luns[i].lundata[1];
			break;
		case RPL_LUNDATA_ATYP_FLAT:
			lun_val = (lun_data->luns[i].lundata[0] &
				RPL_LUNDATA_FLAT_LUN_MASK) |
				(lun_data->luns[i].lundata[1] <<
				RPL_LUNDATA_FLAT_LUN_BITS);
			break;
		case RPL_LUNDATA_ATYP_LUN:
		case RPL_LUNDATA_ATYP_EXTLUN:
		default:
			fprintf(stdout, "Unsupported LUN format %d\n",
				lun_data->luns[i].lundata[0] &
				RPL_LUNDATA_ATYP_MASK);
			lun_val = -1;
			break;
		}
		if (lun_val == -1)
			continue;

		if ((retval = cctl_get_inquiry(fd, lun_val, initid,
					       /*retries*/ 2, scsi_path,
					       sizeof(scsi_path),
					       inq_data)) != 0) {
			goto bailout;
		}
		printf("%s", scsi_path);
		scsi_print_inquiry(inq_data);
	}
bailout:

	if (lun_data != NULL)
		free(lun_data);

	if (inq_data != NULL)
		free(inq_data);

	return (retval);
}

static int
cctl_sync_cache(int fd, int lun, int iid, int retries,
		int argc, char **argv, char *combinedopt)
{
	union ctl_io *io;
	int cdb_size = -1;
	int retval;
	uint64_t our_lba = 0;
	uint32_t our_block_count = 0;
	int reladr = 0, immed = 0;
	int c;

	retval = 0;

	io = ctl_scsi_alloc_io(iid);
	if (io == NULL) {
		warnx("%s: can't allocate memory", __func__);
		return (1);
	}

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'b':
			our_block_count = strtoul(optarg, NULL, 0);
			break;
		case 'c':
			cdb_size = strtol(optarg, NULL, 0);
			break;
		case 'i':
			immed = 1;
			break;
		case 'l':
			our_lba = strtoull(optarg, NULL, 0);
			break;
		case 'r':
			reladr = 1;
			break;
		default:
			break;
		}
	}

	if (cdb_size != -1) {
		switch (cdb_size) {
		case 10:
		case 16:
			break;
		default:
			warnx("%s: invalid cdbsize %d, valid sizes are 10 "
			      "and 16", __func__, cdb_size);
			retval = 1;
			goto bailout;
			break; /* NOTREACHED */
		}
	} else
		cdb_size = 10;

	ctl_scsi_sync_cache(/*io*/ io,
			    /*immed*/ immed,
			    /*reladr*/ reladr,
			    /*minimum_cdb_size*/ cdb_size,
			    /*starting_lba*/ our_lba,
			    /*block_count*/ our_block_count,
			    /*tag_type*/ CTL_TAG_SIMPLE,
			    /*control*/ 0);

	io->io_hdr.nexus.targ_lun = lun;
	io->io_hdr.nexus.initid = iid;

	if (cctl_do_io(fd, retries, io, __func__) != 0) {
		retval = 1;
		goto bailout;
	}

	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS) {
		fprintf(stdout, "Cache synchronized successfully\n");
	} else
		ctl_io_error_print(io, NULL, stderr);
bailout:
	ctl_scsi_free_io(io);

	return (retval);
}

static int
cctl_start_stop(int fd, int lun, int iid, int retries, int start,
		int argc, char **argv, char *combinedopt)
{
	union ctl_io *io;
	char scsi_path[40];
	int immed = 0;
	int retval, c;

	retval = 0;

	io = ctl_scsi_alloc_io(iid);
	if (io == NULL) {
		warnx("%s: can't allocate memory", __func__);
		return (1);
	}

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'i':
			immed = 1;
			break;
		default:
			break;
		}
	}
	/*
	 * Use an ordered tag for the stop command, to guarantee that any
	 * pending I/O will finish before the stop command executes.  This
	 * would normally be the case anyway, since CTL will basically
	 * treat the start/stop command as an ordered command with respect
	 * to any other command except an INQUIRY.  (See ctl_ser_table.c.)
	 */
	ctl_scsi_start_stop(/*io*/ io,
			    /*start*/ start,
			    /*load_eject*/ 0,
			    /*immediate*/ immed,
			    /*power_conditions*/ SSS_PC_START_VALID,
			    /*ctl_tag_type*/ start ? CTL_TAG_SIMPLE :
						     CTL_TAG_ORDERED,
			    /*control*/ 0);

	io->io_hdr.nexus.targ_lun = lun;
	io->io_hdr.nexus.initid = iid;

	if (cctl_do_io(fd, retries, io, __func__) != 0) {
		retval = 1;
		goto bailout;
	}

	ctl_scsi_path_string(io, scsi_path, sizeof(scsi_path));
	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS) {
		fprintf(stdout, "%s LUN %s successfully\n", scsi_path,
			(start) ?  "started" : "stopped");
	} else
		ctl_io_error_print(io, NULL, stderr);

bailout:
	ctl_scsi_free_io(io);

	return (retval);
}

static int
cctl_mode_sense(int fd, int lun, int iid, int retries,
		int argc, char **argv, char *combinedopt)
{
	union ctl_io *io;
	uint32_t datalen;
	uint8_t *dataptr;
	int pc = -1, cdbsize, retval, dbd = 0, subpage = -1;
	int list = 0;
	int page_code = -1;
	int c;

	cdbsize = 0;
	retval = 0;
	dataptr = NULL;

	io = ctl_scsi_alloc_io(iid);
	if (io == NULL) {
		warn("%s: can't allocate memory", __func__);
		return (1);
	}

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'P':
			pc = strtoul(optarg, NULL, 0);
			break;
		case 'S':
			subpage = strtoul(optarg, NULL, 0);
			break;
		case 'd':
			dbd = 1;
			break;
		case 'l':
			list = 1;
			break;
		case 'm':
			page_code = strtoul(optarg, NULL, 0);
			break;
		case 'c':
			cdbsize = strtol(optarg, NULL, 0);
			break;
		default:
			break;
		}
	}

	if (((list == 0) && (page_code == -1))
	 || ((list != 0) && (page_code != -1))) {
		warnx("%s: you must specify either a page code (-m) or -l",
		      __func__);
		retval = 1;
		goto bailout;
	}

	if ((page_code != -1)
	 && ((page_code > SMS_ALL_PAGES_PAGE)
	  || (page_code < 0))) {
		warnx("%s: page code %d is out of range", __func__,
		      page_code);
		retval = 1;
		goto bailout;
	}

	if (list == 1) {
		page_code = SMS_ALL_PAGES_PAGE;
		if (pc != -1) {
			warnx("%s: arg -P makes no sense with -l",
			      __func__);
			retval = 1;
			goto bailout;
		}
		if (subpage != -1) {
			warnx("%s: arg -S makes no sense with -l", __func__);
			retval = 1;
			goto bailout;
		}
	}

	if (pc == -1)
		pc = SMS_PAGE_CTRL_CURRENT;
	else {
		if ((pc > 3)
		 || (pc < 0)) {
			warnx("%s: page control value %d is out of range: 0-3",
			      __func__, pc);
			retval = 1;
			goto bailout;
		}
	}


	if ((subpage != -1)
	 && ((subpage > 255)
	  || (subpage < 0))) {
		warnx("%s: subpage code %d is out of range: 0-255", __func__,
		      subpage);
		retval = 1;
		goto bailout;
	}
	if (cdbsize != 0) {
		switch (cdbsize) {
		case 6:
		case 10:
			break;
		default:
			warnx("%s: invalid cdbsize %d, valid sizes are 6 "
			      "and 10", __func__, cdbsize);
			retval = 1;
			goto bailout;
			break;
		}
	} else
		cdbsize = 6;

	if (subpage == -1)
		subpage = 0;

	if (cdbsize == 6)
		datalen = 255;
	else
		datalen = 65535;

	dataptr = (uint8_t *)malloc(datalen);
	if (dataptr == NULL) {
		warn("%s: can't allocate %d bytes", __func__, datalen);
		retval = 1;
		goto bailout;
	}

	memset(dataptr, 0, datalen);

	ctl_scsi_mode_sense(io,
			    /*data_ptr*/ dataptr,
			    /*data_len*/ datalen,
			    /*dbd*/ dbd,
			    /*llbaa*/ 0,
			    /*page_code*/ page_code,
			    /*pc*/ pc << 6,
			    /*subpage*/ subpage,
			    /*minimum_cdb_size*/ cdbsize,
			    /*tag_type*/ CTL_TAG_SIMPLE,
			    /*control*/ 0);

	io->io_hdr.nexus.targ_lun = lun;
	io->io_hdr.nexus.initid = iid;

	if (cctl_do_io(fd, retries, io, __func__) != 0) {
		retval = 1;
		goto bailout;
	}

	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS) {
		int pages_len, used_len;
		uint32_t returned_len;
		uint8_t *ndataptr;

		if (io->scsiio.cdb[0] == MODE_SENSE_6) {
			struct scsi_mode_hdr_6 *hdr6;
			int bdlen;

			hdr6 = (struct scsi_mode_hdr_6 *)dataptr;

			returned_len = hdr6->datalen + 1;
			bdlen = hdr6->block_descr_len;

			ndataptr = (uint8_t *)((uint8_t *)&hdr6[1] + bdlen);
		} else {
			struct scsi_mode_hdr_10 *hdr10;
			int bdlen;

			hdr10 = (struct scsi_mode_hdr_10 *)dataptr;

			returned_len = scsi_2btoul(hdr10->datalen) + 2;
			bdlen = scsi_2btoul(hdr10->block_descr_len);

			ndataptr = (uint8_t *)((uint8_t *)&hdr10[1] + bdlen);
		}
		/* just in case they can give us more than we allocated for */
		returned_len = min(returned_len, datalen);
		pages_len = returned_len - (ndataptr - dataptr);
#if 0
		fprintf(stdout, "returned_len = %d, pages_len = %d\n",
			returned_len, pages_len);
#endif
		if (list == 1) {
			fprintf(stdout, "Supported mode pages:\n");
			for (used_len = 0; used_len < pages_len;) {
				struct scsi_mode_page_header *header;

				header = (struct scsi_mode_page_header *)
					&ndataptr[used_len];
				fprintf(stdout, "%d\n", header->page_code);
				used_len += header->page_length + 2;
			}
		} else {
			for (used_len = 0; used_len < pages_len; used_len++) {
				fprintf(stdout, "0x%x ", ndataptr[used_len]);
				if (((used_len+1) % 16) == 0)
					fprintf(stdout, "\n");
			}
			fprintf(stdout, "\n");
		}
	} else
		ctl_io_error_print(io, NULL, stderr);
bailout:

	ctl_scsi_free_io(io);

	if (dataptr != NULL)
		free(dataptr);

	return (retval);
}

static int
cctl_read_capacity(int fd, int lun, int iid, int retries,
		   int argc, char **argv, char *combinedopt)
{
	union ctl_io *io;
	struct scsi_read_capacity_data *data;
	struct scsi_read_capacity_data_long *longdata;
	int cdbsize = -1, retval;
	uint8_t *dataptr;
	int c;

	cdbsize = 10;
	dataptr = NULL;
	retval = 0;

	io = ctl_scsi_alloc_io(iid);
	if (io == NULL) {
		warn("%s: can't allocate memory\n", __func__);
		return (1);
	}

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'c':
			cdbsize = strtol(optarg, NULL, 0);
			break;
		default:
			break;
		}
	}
	if (cdbsize != -1) {
		switch (cdbsize) {
		case 10:
		case 16:
			break;
		default:
			warnx("%s: invalid cdbsize %d, valid sizes are 10 "
			      "and 16", __func__, cdbsize);
			retval = 1;
			goto bailout;
			break; /* NOTREACHED */
		}
	} else
		cdbsize = 10;

	dataptr = (uint8_t *)malloc(sizeof(*longdata));
	if (dataptr == NULL) {
		warn("%s: can't allocate %zd bytes\n", __func__,
		     sizeof(*longdata));
		retval = 1;
		goto bailout;
	}
	memset(dataptr, 0, sizeof(*longdata));

retry:

	switch (cdbsize) {
	case 10:
		ctl_scsi_read_capacity(io,
				       /*data_ptr*/ dataptr,
				       /*data_len*/ sizeof(*longdata),
				       /*addr*/ 0,
				       /*reladr*/ 0,
				       /*pmi*/ 0,
				       /*tag_type*/ CTL_TAG_SIMPLE,
				       /*control*/ 0);
		break;
	case 16:
		ctl_scsi_read_capacity_16(io,
					  /*data_ptr*/ dataptr,
					  /*data_len*/ sizeof(*longdata),
					  /*addr*/ 0,
					  /*reladr*/ 0,
					  /*pmi*/ 0,
					  /*tag_type*/ CTL_TAG_SIMPLE,
					  /*control*/ 0);
		break;
	}

	io->io_hdr.nexus.initid = iid;
	io->io_hdr.nexus.targ_lun = lun;

	if (cctl_do_io(fd, retries, io, __func__) != 0) {
		retval = 1;
		goto bailout;
	}

	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS) {
		uint64_t maxlba;
		uint32_t blocksize;

		if (cdbsize == 10) {

			data = (struct scsi_read_capacity_data *)dataptr;

			maxlba = scsi_4btoul(data->addr);
			blocksize = scsi_4btoul(data->length);

			if (maxlba == 0xffffffff) {
				cdbsize = 16;
				goto retry;
			}
		} else {
			longdata=(struct scsi_read_capacity_data_long *)dataptr;

			maxlba = scsi_8btou64(longdata->addr);
			blocksize = scsi_4btoul(longdata->length);
		}

		fprintf(stdout, "Disk Capacity: %ju, Blocksize: %d\n",
			(uintmax_t)maxlba, blocksize);
	} else {
		ctl_io_error_print(io, NULL, stderr);
	}
bailout:
	ctl_scsi_free_io(io);

	if (dataptr != NULL)
		free(dataptr);

	return (retval);
}

static int
cctl_read_write(int fd, int lun, int iid, int retries,
		int argc, char **argv, char *combinedopt,
		ctladm_cmdfunction command)
{
	union ctl_io *io;
	int file_fd, do_stdio;
	int cdbsize = -1, databytes;
	uint8_t *dataptr;
	char *filename = NULL;
	int datalen = -1, blocksize = -1;
	uint64_t lba = 0;
	int lba_set = 0;
	int retval;
	int c;

	retval = 0;
	do_stdio = 0;
	dataptr = NULL;
	file_fd = -1;

	io = ctl_scsi_alloc_io(iid);
	if (io == NULL) {
		warn("%s: can't allocate memory\n", __func__);
		return (1);
	}

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'N':
			io->io_hdr.flags |= CTL_FLAG_NO_DATAMOVE;
			break;
		case 'b':
			blocksize = strtoul(optarg, NULL, 0);
			break;
		case 'c':
			cdbsize = strtoul(optarg, NULL, 0);
			break;
		case 'd':
			datalen = strtoul(optarg, NULL, 0);
			break;
		case 'f':
			filename = strdup(optarg);
			break;
		case 'l':
			lba = strtoull(optarg, NULL, 0);
			lba_set = 1;
			break;
		default:
			break;
		}
	}
	if (filename == NULL) {
		warnx("%s: you must supply a filename using -f", __func__);
		retval = 1;
		goto bailout;
	}

	if (datalen == -1) {
		warnx("%s: you must specify the data length with -d", __func__);
		retval = 1;
		goto bailout;
	}

	if (lba_set == 0) {
		warnx("%s: you must specify the LBA with -l", __func__);
		retval = 1;
		goto bailout;
	}

	if (blocksize == -1) {
		warnx("%s: you must specify the blocksize with -b", __func__);
		retval = 1;
		goto bailout;
	}

	if (cdbsize != -1) {
		switch (cdbsize) {
		case 6:
		case 10:
		case 12:
		case 16:
			break;
		default:
			warnx("%s: invalid cdbsize %d, valid sizes are 6, "
			      "10, 12 or 16", __func__, cdbsize);
			retval = 1;
			goto bailout;
			break; /* NOTREACHED */
		}
	} else
		cdbsize = 6;

	databytes = datalen * blocksize;
	dataptr = (uint8_t *)malloc(databytes);

	if (dataptr == NULL) {
		warn("%s: can't allocate %d bytes\n", __func__, databytes);
		retval = 1;
		goto bailout;
	}
	if (strcmp(filename, "-") == 0) {
		if (command == CTLADM_CMD_READ)
			file_fd = STDOUT_FILENO;
		else
			file_fd = STDIN_FILENO;
		do_stdio = 1;
	} else {
		file_fd = open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
		if (file_fd == -1) {
			warn("%s: can't open file %s", __func__, filename);
			retval = 1;
			goto bailout;
		}
	}

	memset(dataptr, 0, databytes);

	if (command == CTLADM_CMD_WRITE) {
		int bytes_read;

		bytes_read = read(file_fd, dataptr, databytes);
		if (bytes_read == -1) {
			warn("%s: error reading file %s", __func__, filename);
			retval = 1;
			goto bailout;
		}
		if (bytes_read != databytes) {
			warnx("%s: only read %d bytes from file %s",
			      __func__, bytes_read, filename);
			retval = 1;
			goto bailout;
		}
	}
	ctl_scsi_read_write(io,
			    /*data_ptr*/ dataptr,
			    /*data_len*/ databytes,
			    /*read_op*/ (command == CTLADM_CMD_READ) ? 1 : 0,
			    /*byte2*/ 0,
			    /*minimum_cdb_size*/ cdbsize,
			    /*lba*/ lba,
			    /*num_blocks*/ datalen,
			    /*tag_type*/ CTL_TAG_SIMPLE,
			    /*control*/ 0);

	io->io_hdr.nexus.targ_lun = lun;
	io->io_hdr.nexus.initid = iid;

	if (cctl_do_io(fd, retries, io, __func__) != 0) {
		retval = 1;
		goto bailout;
	}

	if (((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS)
	 && (command == CTLADM_CMD_READ)) {
		int bytes_written;

		bytes_written = write(file_fd, dataptr, databytes);
		if (bytes_written == -1) {
			warn("%s: can't write to %s", __func__, filename);
			goto bailout;
		}
	} else if ((io->io_hdr.status & CTL_STATUS_MASK) != CTL_SUCCESS)
		ctl_io_error_print(io, NULL, stderr);


bailout:

	ctl_scsi_free_io(io);

	if (dataptr != NULL)
		free(dataptr);

	if ((do_stdio == 0)
	 && (file_fd != -1))
		close(file_fd);

	return (retval);
}

static int
cctl_get_luns(int fd, int lun, int iid, int retries, struct
	      scsi_report_luns_data **lun_data, uint32_t *num_luns)
{
	union ctl_io *io;
	uint32_t nluns;
	int lun_datalen;
	int retval;

	retval = 0;

	io = ctl_scsi_alloc_io(iid);
	if (io == NULL) {
		warnx("%s: can't allocate memory", __func__);
		return (1);
	}

	/*
	 * lun_data includes space for 1 lun, allocate space for 4 initially.
	 * If that isn't enough, we'll allocate more.
	 */
	nluns = 4;
retry:
	lun_datalen = sizeof(*lun_data) +
		(nluns * sizeof(struct scsi_report_luns_lundata));
	*lun_data = malloc(lun_datalen);

	if (*lun_data == NULL) {
		warnx("%s: can't allocate memory", __func__);
		ctl_scsi_free_io(io);
		return (1);
	}

	ctl_scsi_report_luns(io,
			     /*data_ptr*/ (uint8_t *)*lun_data,
			     /*data_len*/ lun_datalen,
			     /*select_report*/ RPL_REPORT_ALL,
			     /*tag_type*/ CTL_TAG_SIMPLE,
			     /*control*/ 0);

	io->io_hdr.nexus.initid = iid;
	io->io_hdr.nexus.targ_lun = lun;

	if (cctl_do_io(fd, retries, io, __func__) != 0) {
		retval = 1;
		goto bailout;
	}

	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS) {
		uint32_t returned_len, returned_luns;

		returned_len = scsi_4btoul((*lun_data)->length);
		returned_luns = returned_len / 8;
		if (returned_luns > nluns) {
			nluns = returned_luns;
			free(*lun_data);
			goto retry;
		}
		/* These should be the same */
		*num_luns = MIN(returned_luns, nluns);
	} else {
		ctl_io_error_print(io, NULL, stderr);
		retval = 1;
	}
bailout:
	ctl_scsi_free_io(io);

	return (retval);
}

static int
cctl_report_luns(int fd, int lun, int iid, int retries)
{
	struct scsi_report_luns_data *lun_data;
	uint32_t num_luns, i;
	int retval;

	lun_data = NULL;

	if ((retval = cctl_get_luns(fd, lun, iid, retries, &lun_data,
				   &num_luns)) != 0)
		goto bailout;

	fprintf(stdout, "%u LUNs returned\n", num_luns);
	for (i = 0; i < num_luns; i++) {
		int lun_val;

		/*
		 * XXX KDM figure out a way to share this code with
		 * cctl_lunlist()?
		 */
		switch (lun_data->luns[i].lundata[0] & RPL_LUNDATA_ATYP_MASK) {
		case RPL_LUNDATA_ATYP_PERIPH:
			lun_val = lun_data->luns[i].lundata[1];
			break;
		case RPL_LUNDATA_ATYP_FLAT:
			lun_val = (lun_data->luns[i].lundata[0] &
				RPL_LUNDATA_FLAT_LUN_MASK) |
				(lun_data->luns[i].lundata[1] <<
				RPL_LUNDATA_FLAT_LUN_BITS);
			break;
		case RPL_LUNDATA_ATYP_LUN:
		case RPL_LUNDATA_ATYP_EXTLUN:
		default:
			fprintf(stdout, "Unsupported LUN format %d\n",
				lun_data->luns[i].lundata[0] &
				RPL_LUNDATA_ATYP_MASK);
			lun_val = -1;
			break;
		}
		if (lun_val == -1)
			continue;

		fprintf(stdout, "%d\n", lun_val);
	}

bailout:
	if (lun_data != NULL)
		free(lun_data);

	return (retval);
}

static int
cctl_tur(int fd, int lun, int iid, int retries)
{
	union ctl_io *io;

	io = ctl_scsi_alloc_io(iid);
	if (io == NULL) {
		fprintf(stderr, "can't allocate memory\n");
		return (1);
	}

	ctl_scsi_tur(io,
		     /* tag_type */ CTL_TAG_SIMPLE,
		     /* control */ 0);

	io->io_hdr.nexus.targ_lun = lun;
	io->io_hdr.nexus.initid = iid;

	if (cctl_do_io(fd, retries, io, __func__) != 0) {
		ctl_scsi_free_io(io);
		return (1);
	}

	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS)
		fprintf(stdout, "Unit is ready\n");
	else
		ctl_io_error_print(io, NULL, stderr);

	return (0);
}

static int
cctl_get_inquiry(int fd, int lun, int iid, int retries,
		 char *path_str, int path_len,
		 struct scsi_inquiry_data *inq_data)
{
	union ctl_io *io;
	int retval;

	retval = 0;

	io = ctl_scsi_alloc_io(iid);
	if (io == NULL) {
		warnx("cctl_inquiry: can't allocate memory\n");
		return (1);
	}

	ctl_scsi_inquiry(/*io*/ io,
			 /*data_ptr*/ (uint8_t *)inq_data,
			 /*data_len*/ sizeof(*inq_data),
			 /*byte2*/ 0,
			 /*page_code*/ 0,
			 /*tag_type*/ CTL_TAG_SIMPLE,
			 /*control*/ 0);

	io->io_hdr.nexus.targ_lun = lun;
	io->io_hdr.nexus.initid = iid;

	if (cctl_do_io(fd, retries, io, __func__) != 0) {
		retval = 1;
		goto bailout;
	}

	if ((io->io_hdr.status & CTL_STATUS_MASK) != CTL_SUCCESS) {
		retval = 1;
		ctl_io_error_print(io, NULL, stderr);
	} else if (path_str != NULL)
		ctl_scsi_path_string(io, path_str, path_len);

bailout:
	ctl_scsi_free_io(io);

	return (retval);
}

static int
cctl_inquiry(int fd, int lun, int iid, int retries)
{
	struct scsi_inquiry_data *inq_data;
	char scsi_path[40];
	int retval;

	inq_data = malloc(sizeof(*inq_data));
	if (inq_data == NULL) {
		warnx("%s: can't allocate inquiry data", __func__);
		retval = 1;
		goto bailout;
	}

	if ((retval = cctl_get_inquiry(fd, lun, iid, retries, scsi_path,
				       sizeof(scsi_path), inq_data)) != 0)
		goto bailout;

	printf("%s", scsi_path);
	scsi_print_inquiry(inq_data);

bailout:
	if (inq_data != NULL)
		free(inq_data);

	return (retval);
}

static int
cctl_req_sense(int fd, int lun, int iid, int retries)
{
	union ctl_io *io;
	struct scsi_sense_data *sense_data;
	int retval;

	retval = 0;

	io = ctl_scsi_alloc_io(iid);
	if (io == NULL) {
		warnx("cctl_req_sense: can't allocate memory\n");
		return (1);
	}
	sense_data = malloc(sizeof(*sense_data));
	memset(sense_data, 0, sizeof(*sense_data));

	ctl_scsi_request_sense(/*io*/ io,
			       /*data_ptr*/ (uint8_t *)sense_data,
			       /*data_len*/ sizeof(*sense_data),
			       /*byte2*/ 0,
			       /*tag_type*/ CTL_TAG_SIMPLE,
			       /*control*/ 0);

	io->io_hdr.nexus.targ_lun = lun;
	io->io_hdr.nexus.initid = iid;

	if (cctl_do_io(fd, retries, io, __func__) != 0) {
		retval = 1;
		goto bailout;
	}

	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS) {
		bcopy(sense_data, &io->scsiio.sense_data, sizeof(*sense_data));
		io->scsiio.sense_len = sizeof(*sense_data);
		ctl_scsi_sense_print(&io->scsiio, NULL, stdout);
	} else
		ctl_io_error_print(io, NULL, stderr);

bailout:

	ctl_scsi_free_io(io);
	free(sense_data);

	return (retval);
}

static int
cctl_report_target_port_group(int fd, int lun, int iid)
{
	union ctl_io *io;
	uint32_t datalen;
	uint8_t *dataptr;
	int retval;

	dataptr = NULL;
	retval = 0;

	io = ctl_scsi_alloc_io(iid);
	if (io == NULL) {
		warn("%s: can't allocate memory", __func__);
		return (1);
	}

	datalen = 64;
	dataptr = (uint8_t *)malloc(datalen);
	if (dataptr == NULL) {
		warn("%s: can't allocate %d bytes", __func__, datalen);
		retval = 1;
		goto bailout;
	}

	memset(dataptr, 0, datalen);

	ctl_scsi_maintenance_in(/*io*/ io,
				/*data_ptr*/ dataptr,
				/*data_len*/ datalen,
				/*action*/ SA_RPRT_TRGT_GRP,
				/*tag_type*/ CTL_TAG_SIMPLE,
				/*control*/ 0);

	io->io_hdr.nexus.targ_lun = lun;
	io->io_hdr.nexus.initid = iid;

	if (cctl_do_io(fd, 0, io, __func__) != 0) {
		retval = 1;
		goto bailout;
	}

	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS) {
		int returned_len, used_len;

		returned_len = scsi_4btoul(&dataptr[0]) + 4;

		for (used_len = 0; used_len < returned_len; used_len++) {
			fprintf(stdout, "0x%02x ", dataptr[used_len]);
			if (((used_len+1) % 8) == 0)
				fprintf(stdout, "\n");
		}
		fprintf(stdout, "\n");
	} else
		ctl_io_error_print(io, NULL, stderr);

bailout:
	ctl_scsi_free_io(io);

	if (dataptr != NULL)
		free(dataptr);

	return (retval);
}

static int
cctl_inquiry_vpd_devid(int fd, int lun, int iid)
{
	union ctl_io *io;
	uint32_t datalen;
	uint8_t *dataptr;
	int retval;

	retval = 0;
	dataptr = NULL;

	io = ctl_scsi_alloc_io(iid);
	if (io == NULL) {
		warn("%s: can't allocate memory", __func__);
		return (1);
	}

	datalen = 256;
	dataptr = (uint8_t *)malloc(datalen);
	if (dataptr == NULL) {
		warn("%s: can't allocate %d bytes", __func__, datalen);
		retval = 1;
		goto bailout;
	}

	memset(dataptr, 0, datalen);

	ctl_scsi_inquiry(/*io*/        io,
			 /*data_ptr*/  dataptr,
			 /*data_len*/  datalen,
			 /*byte2*/     SI_EVPD,
			 /*page_code*/ SVPD_DEVICE_ID,
			 /*tag_type*/  CTL_TAG_SIMPLE,
			 /*control*/   0);

	io->io_hdr.nexus.targ_lun = lun;
	io->io_hdr.nexus.initid = iid;

	if (cctl_do_io(fd, 0, io, __func__) != 0) {
		retval = 1;
		goto bailout;
	}

	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS) {
		int returned_len, used_len;

		returned_len = scsi_2btoul(&dataptr[2]) + 4;

		for (used_len = 0; used_len < returned_len; used_len++) {
			fprintf(stdout, "0x%02x ", dataptr[used_len]);
			if (((used_len+1) % 8) == 0)
				fprintf(stdout, "\n");
		}
		fprintf(stdout, "\n");
	} else
		ctl_io_error_print(io, NULL, stderr);

bailout:
	ctl_scsi_free_io(io);

	if (dataptr != NULL)
		free(dataptr);

	return (retval);
}

static int
cctl_persistent_reserve_in(int fd, int lun, int iid,
                           int argc, char **argv, char *combinedopt,
			   int retry_count)
{
	union ctl_io *io;
	uint32_t datalen;
	uint8_t *dataptr;
	int action = -1;
	int retval;
	int c;

	retval = 0;
	dataptr = NULL;

	io = ctl_scsi_alloc_io(iid);
	if (io == NULL) {
		warn("%s: can't allocate memory", __func__);
		return (1);
	}

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'a':
			action = strtol(optarg, NULL, 0);
			break;
		default:
			break;
		}
	}

	if (action < 0 || action > 2) {
		warn("action must be specified and in the range: 0-2");
		retval = 1;
		goto bailout;
	}


	datalen = 256;
	dataptr = (uint8_t *)malloc(datalen);
	if (dataptr == NULL) {
		warn("%s: can't allocate %d bytes", __func__, datalen);
		retval = 1;
		goto bailout;
	}

	memset(dataptr, 0, datalen);

	ctl_scsi_persistent_res_in(io,
				   /*data_ptr*/ dataptr,
				   /*data_len*/ datalen,
				   /*action*/   action,
				   /*tag_type*/ CTL_TAG_SIMPLE,
				   /*control*/  0);

	io->io_hdr.nexus.targ_lun = lun;
	io->io_hdr.nexus.initid = iid;

	if (cctl_do_io(fd, retry_count, io, __func__) != 0) {
		retval = 1;
		goto bailout;
	}

	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS) {
		int returned_len, used_len;

		switch (action) {
		case 0:
			returned_len = scsi_4btoul(&dataptr[4]) + 8;
			returned_len = min(returned_len, 256);
			break;
		case 1:
			returned_len = scsi_4btoul(&dataptr[4]) + 8;
			break;
		case 2:
			returned_len = 8;
			break;
		default:
			warnx("%s: invalid action %d", __func__, action);
			goto bailout;
			break; /* NOTREACHED */
		}

		for (used_len = 0; used_len < returned_len; used_len++) {
			fprintf(stdout, "0x%02x ", dataptr[used_len]);
			if (((used_len+1) % 8) == 0)
				fprintf(stdout, "\n");
		}
		fprintf(stdout, "\n");
	} else
		ctl_io_error_print(io, NULL, stderr);

bailout:
	ctl_scsi_free_io(io);

	if (dataptr != NULL)
		free(dataptr);

	return (retval);
}

static int
cctl_persistent_reserve_out(int fd, int lun, int iid,
			    int argc, char **argv, char *combinedopt,
			    int retry_count)
{
	union ctl_io *io;
	uint32_t datalen;
	uint64_t key = 0, sa_key = 0;
	int action = -1, restype = -1;
	uint8_t *dataptr;
	int retval;
	int c;

	retval = 0;
	dataptr = NULL;

	io = ctl_scsi_alloc_io(iid);
	if (io == NULL) {
		warn("%s: can't allocate memory", __func__);
		return (1);
	}

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'a':
			action = strtol(optarg, NULL, 0);
			break;
		case 'k':
			key = strtoull(optarg, NULL, 0);
			break;
		case 'r':
			restype = strtol(optarg, NULL, 0);
			break;
		case 's':
			sa_key = strtoull(optarg, NULL, 0);
			break;
		default:
			break;
		}
	}
	if (action < 0 || action > 5) {
		warn("action must be specified and in the range: 0-5");
		retval = 1;
		goto bailout;
	}

	if (restype < 0 || restype > 5) {
		if (action != 0 && action != 5 && action != 3) {
			warn("'restype' must specified and in the range: 0-5");
			retval = 1;
			goto bailout;
		}
	}

	datalen = 24;
	dataptr = (uint8_t *)malloc(datalen);
	if (dataptr == NULL) {
		warn("%s: can't allocate %d bytes", __func__, datalen);
		retval = 1;
		goto bailout;
	}

	memset(dataptr, 0, datalen);

	ctl_scsi_persistent_res_out(io,
				    /*data_ptr*/ dataptr,
				    /*data_len*/ datalen,
				    /*action*/   action,
				    /*type*/     restype,
				    /*key*/      key,
				    /*sa key*/   sa_key,
				    /*tag_type*/ CTL_TAG_SIMPLE,
				    /*control*/  0);

	io->io_hdr.nexus.targ_lun = lun;
	io->io_hdr.nexus.initid = iid;

	if (cctl_do_io(fd, retry_count, io, __func__) != 0) {
		retval = 1;
		goto bailout;
	}
	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS) {
		char scsi_path[40];
		ctl_scsi_path_string(io, scsi_path, sizeof(scsi_path));
		fprintf( stdout, "%sPERSISTENT RESERVE OUT executed "
			"successfully\n", scsi_path);
	} else
		ctl_io_error_print(io, NULL, stderr);

bailout:
	ctl_scsi_free_io(io);

	if (dataptr != NULL)
		free(dataptr);

	return (retval);
}

static int
cctl_create_lun(int fd, int argc, char **argv, char *combinedopt)
{
	struct ctl_lun_req req;
	int device_type = -1;
	uint64_t lun_size = 0;
	uint32_t blocksize = 0, req_lun_id = 0;
	char *serial_num = NULL;
	char *device_id = NULL;
	int lun_size_set = 0, blocksize_set = 0, lun_id_set = 0;
	char *backend_name = NULL;
	nvlist_t *option_list;
	int retval = 0, c;

	option_list = nvlist_create(0);
	if (option_list == NULL)
		err(1, "%s: unable to allocate nvlist", __func__);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'b':
			backend_name = strdup(optarg);
			break;
		case 'B':
			blocksize = strtoul(optarg, NULL, 0);
			blocksize_set = 1;
			break;
		case 'd':
			device_id = strdup(optarg);
			break;
		case 'l':
			req_lun_id = strtoul(optarg, NULL, 0);
			lun_id_set = 1;
			break;
		case 'o': {
			char *tmpstr;
			char *name, *value;

			tmpstr = strdup(optarg);
			name = strsep(&tmpstr, "=");
			if (name == NULL) {
				warnx("%s: option -o takes \"name=value\""
				      "argument", __func__);
				retval = 1;
				goto bailout;
			}
			value = strsep(&tmpstr, "=");
			if (value == NULL) {
				warnx("%s: option -o takes \"name=value\""
				      "argument", __func__);
				retval = 1;
				goto bailout;
			}
			free(tmpstr);
			nvlist_add_string(option_list, name, value);
			break;
		}
		case 's':
			if (strcasecmp(optarg, "auto") != 0) {
				retval = expand_number(optarg, &lun_size);
				if (retval != 0) {
					warn("%s: invalid -s argument",
					    __func__);
					retval = 1;
					goto bailout;
				}
			}
			lun_size_set = 1;
			break;
		case 'S':
			serial_num = strdup(optarg);
			break;
		case 't':
			device_type = strtoul(optarg, NULL, 0);
			break;
		default:
			break;
		}
	}

	if (backend_name == NULL) {
		warnx("%s: backend name (-b) must be specified", __func__);
		retval = 1;
		goto bailout;
	}

	bzero(&req, sizeof(req));

	strlcpy(req.backend, backend_name, sizeof(req.backend));
	req.reqtype = CTL_LUNREQ_CREATE;

	if (blocksize_set != 0)
		req.reqdata.create.blocksize_bytes = blocksize;

	if (lun_size_set != 0)
		req.reqdata.create.lun_size_bytes = lun_size;

	if (lun_id_set != 0) {
		req.reqdata.create.flags |= CTL_LUN_FLAG_ID_REQ;
		req.reqdata.create.req_lun_id = req_lun_id;
	}

	req.reqdata.create.flags |= CTL_LUN_FLAG_DEV_TYPE;

	if (device_type != -1)
		req.reqdata.create.device_type = device_type;
	else
		req.reqdata.create.device_type = T_DIRECT;

	if (serial_num != NULL) {
		strlcpy(req.reqdata.create.serial_num, serial_num,
			sizeof(req.reqdata.create.serial_num));
		req.reqdata.create.flags |= CTL_LUN_FLAG_SERIAL_NUM;
	}

	if (device_id != NULL) {
		strlcpy(req.reqdata.create.device_id, device_id,
			sizeof(req.reqdata.create.device_id));
		req.reqdata.create.flags |= CTL_LUN_FLAG_DEVID;
	}

	req.args = nvlist_pack(option_list, &req.args_len);
	if (req.args == NULL) {
		warn("%s: error packing nvlist", __func__);
		retval = 1;
		goto bailout;
	}

	retval = ioctl(fd, CTL_LUN_REQ, &req);
	free(req.args);
	if (retval == -1) {
		warn("%s: error issuing CTL_LUN_REQ ioctl", __func__);
		retval = 1;
		goto bailout;
	}

	switch (req.status) {
	case CTL_LUN_ERROR:
		warnx("LUN creation error: %s", req.error_str);
		retval = 1;
		goto bailout;
	case CTL_LUN_WARNING:
		warnx("LUN creation warning: %s", req.error_str);
		break;
	case CTL_LUN_OK:
		break;
	default:
		warnx("unknown LUN creation status: %d", req.status);
		retval = 1;
		goto bailout;
	}

	fprintf(stdout, "LUN created successfully\n");
	fprintf(stdout, "backend:       %s\n", req.backend);
	fprintf(stdout, "device type:   %d\n",req.reqdata.create.device_type);
	fprintf(stdout, "LUN size:      %ju bytes\n",
		(uintmax_t)req.reqdata.create.lun_size_bytes);
	fprintf(stdout, "blocksize      %u bytes\n",
		req.reqdata.create.blocksize_bytes);
	fprintf(stdout, "LUN ID:        %d\n", req.reqdata.create.req_lun_id);
	fprintf(stdout, "Serial Number: %s\n", req.reqdata.create.serial_num);
	fprintf(stdout, "Device ID:     %s\n", req.reqdata.create.device_id);

bailout:
	nvlist_destroy(option_list);
	return (retval);
}

static int
cctl_rm_lun(int fd, int argc, char **argv, char *combinedopt)
{
	struct ctl_lun_req req;
	uint32_t lun_id = 0;
	int lun_id_set = 0;
	char *backend_name = NULL;
	nvlist_t *option_list;
	int retval = 0, c;

	option_list = nvlist_create(0);
	if (option_list == NULL)
		err(1, "%s: unable to allocate nvlist", __func__);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'b':
			backend_name = strdup(optarg);
			break;
		case 'l':
			lun_id = strtoul(optarg, NULL, 0);
			lun_id_set = 1;
			break;
		case 'o': {
			char *tmpstr;
			char *name, *value;

			tmpstr = strdup(optarg);
			name = strsep(&tmpstr, "=");
			if (name == NULL) {
				warnx("%s: option -o takes \"name=value\""
				      "argument", __func__);
				retval = 1;
				goto bailout;
			}
			value = strsep(&tmpstr, "=");
			if (value == NULL) {
				warnx("%s: option -o takes \"name=value\""
				      "argument", __func__);
				retval = 1;
				goto bailout;
			}
			free(tmpstr);
			nvlist_add_string(option_list, name, value);
			break;
		}
		default:
			break;
		}
	}

	if (backend_name == NULL)
		errx(1, "%s: backend name (-b) must be specified", __func__);

	if (lun_id_set == 0)
		errx(1, "%s: LUN id (-l) must be specified", __func__);

	bzero(&req, sizeof(req));

	strlcpy(req.backend, backend_name, sizeof(req.backend));
	req.reqtype = CTL_LUNREQ_RM;

	req.reqdata.rm.lun_id = lun_id;
		
	req.args = nvlist_pack(option_list, &req.args_len);
	if (req.args == NULL) {
		warn("%s: error packing nvlist", __func__);
		retval = 1;
		goto bailout;
	}

	retval = ioctl(fd, CTL_LUN_REQ, &req);
	free(req.args);
	if (retval == -1) {
		warn("%s: error issuing CTL_LUN_REQ ioctl", __func__);
		retval = 1;
		goto bailout;
	}

	switch (req.status) {
	case CTL_LUN_ERROR:
		warnx("LUN removal error: %s", req.error_str);
		retval = 1;
		goto bailout;
	case CTL_LUN_WARNING:
		warnx("LUN removal warning: %s", req.error_str);
		break;
	case CTL_LUN_OK:
		break;
	default:
		warnx("unknown LUN removal status: %d", req.status);
		retval = 1;
		goto bailout;
	}

	printf("LUN %d removed successfully\n", lun_id);

bailout:
	nvlist_destroy(option_list);
	return (retval);
}

static int
cctl_modify_lun(int fd, int argc, char **argv, char *combinedopt)
{
	struct ctl_lun_req req;
	uint64_t lun_size = 0;
	uint32_t lun_id = 0;
	int lun_id_set = 0, lun_size_set = 0;
	char *backend_name = NULL;
	nvlist_t *option_list;
	int retval = 0, c;

	option_list = nvlist_create(0);
	if (option_list == NULL)
		err(1, "%s: unable to allocate nvlist", __func__);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'b':
			backend_name = strdup(optarg);
			break;
		case 'l':
			lun_id = strtoul(optarg, NULL, 0);
			lun_id_set = 1;
			break;
		case 'o': {
			char *tmpstr;
			char *name, *value;

			tmpstr = strdup(optarg);
			name = strsep(&tmpstr, "=");
			if (name == NULL) {
				warnx("%s: option -o takes \"name=value\""
				      "argument", __func__);
				retval = 1;
				goto bailout;
			}
			value = strsep(&tmpstr, "=");
			if (value == NULL) {
				warnx("%s: option -o takes \"name=value\""
				      "argument", __func__);
				retval = 1;
				goto bailout;
			}
			free(tmpstr);
			nvlist_add_string(option_list, name, value);
			break;
		}
		case 's':
			if (strcasecmp(optarg, "auto") != 0) {
				retval = expand_number(optarg, &lun_size);
				if (retval != 0) {
					warn("%s: invalid -s argument",
					    __func__);
					retval = 1;
					goto bailout;
				}
			}
			lun_size_set = 1;
			break;
		default:
			break;
		}
	}

	if (backend_name == NULL)
		errx(1, "%s: backend name (-b) must be specified", __func__);

	if (lun_id_set == 0)
		errx(1, "%s: LUN id (-l) must be specified", __func__);

	if (lun_size_set == 0 && nvlist_empty(option_list))
		errx(1, "%s: size (-s) or options (-o) must be specified",
		    __func__);

	bzero(&req, sizeof(req));

	strlcpy(req.backend, backend_name, sizeof(req.backend));
	req.reqtype = CTL_LUNREQ_MODIFY;

	req.reqdata.modify.lun_id = lun_id;
	req.reqdata.modify.lun_size_bytes = lun_size;

	req.args = nvlist_pack(option_list, &req.args_len);
	if (req.args == NULL) {
		warn("%s: error packing nvlist", __func__);
		retval = 1;
		goto bailout;
	}

	retval = ioctl(fd, CTL_LUN_REQ, &req);
	free(req.args);
	if (retval == -1) {
		warn("%s: error issuing CTL_LUN_REQ ioctl", __func__);
		retval = 1;
		goto bailout;
	}

	switch (req.status) {
	case CTL_LUN_ERROR:
		warnx("LUN modification error: %s", req.error_str);
		retval = 1;
		goto bailout;
	case CTL_LUN_WARNING:
		warnx("LUN modification warning: %s", req.error_str);
		break;
	case CTL_LUN_OK:
		break;
	default:
		warnx("unknown LUN modification status: %d", req.status);
		retval = 1;
		goto bailout;
	}

	printf("LUN %d modified successfully\n", lun_id);

bailout:
	nvlist_destroy(option_list);
	return (retval);
}

struct cctl_islist_conn {
	int connection_id;
	char *initiator;
	char *initiator_addr;
	char *initiator_alias;
	char *target;
	char *target_alias;
	char *header_digest;
	char *data_digest;
	char *max_recv_data_segment_length;
	char *max_send_data_segment_length;
	char *max_burst_length;
	char *first_burst_length;
	char *offload;
	int immediate_data;
	int iser;
	STAILQ_ENTRY(cctl_islist_conn) links;
};

struct cctl_islist_data {
	int num_conns;
	STAILQ_HEAD(,cctl_islist_conn) conn_list;
	struct cctl_islist_conn *cur_conn;
	int level;
	struct sbuf *cur_sb[32];
};

static void
cctl_islist_start_element(void *user_data, const char *name, const char **attr)
{
	int i;
	struct cctl_islist_data *islist;
	struct cctl_islist_conn *cur_conn;

	islist = (struct cctl_islist_data *)user_data;
	cur_conn = islist->cur_conn;
	islist->level++;
	if ((u_int)islist->level >= (sizeof(islist->cur_sb) /
	    sizeof(islist->cur_sb[0])))
		errx(1, "%s: too many nesting levels, %zd max", __func__,
		     sizeof(islist->cur_sb) / sizeof(islist->cur_sb[0]));

	islist->cur_sb[islist->level] = sbuf_new_auto();
	if (islist->cur_sb[islist->level] == NULL)
		err(1, "%s: Unable to allocate sbuf", __func__);

	if (strcmp(name, "connection") == 0) {
		if (cur_conn != NULL)
			errx(1, "%s: improper connection element nesting",
			    __func__);

		cur_conn = calloc(1, sizeof(*cur_conn));
		if (cur_conn == NULL)
			err(1, "%s: cannot allocate %zd bytes", __func__,
			    sizeof(*cur_conn));

		islist->num_conns++;
		islist->cur_conn = cur_conn;

		STAILQ_INSERT_TAIL(&islist->conn_list, cur_conn, links);

		for (i = 0; attr[i] != NULL; i += 2) {
			if (strcmp(attr[i], "id") == 0) {
				cur_conn->connection_id =
				    strtoull(attr[i+1], NULL, 0);
			} else {
				errx(1,
				    "%s: invalid connection attribute %s = %s",
				     __func__, attr[i], attr[i+1]);
			}
		}
	}
}

static void
cctl_islist_end_element(void *user_data, const char *name)
{
	struct cctl_islist_data *islist;
	struct cctl_islist_conn *cur_conn;
	char *str;

	islist = (struct cctl_islist_data *)user_data;
	cur_conn = islist->cur_conn;

	if ((cur_conn == NULL)
	 && (strcmp(name, "ctlislist") != 0))
		errx(1, "%s: cur_conn == NULL! (name = %s)", __func__, name);

	if (islist->cur_sb[islist->level] == NULL)
		errx(1, "%s: no valid sbuf at level %d (name %s)", __func__,
		     islist->level, name);

	sbuf_finish(islist->cur_sb[islist->level]);
	str = strdup(sbuf_data(islist->cur_sb[islist->level]));
	if (str == NULL)
		err(1, "%s can't allocate %zd bytes for string", __func__,
		    sbuf_len(islist->cur_sb[islist->level]));

	sbuf_delete(islist->cur_sb[islist->level]);
	islist->cur_sb[islist->level] = NULL;
	islist->level--;

	if (strcmp(name, "initiator") == 0) {
		cur_conn->initiator = str;
		str = NULL;
	} else if (strcmp(name, "initiator_addr") == 0) {
		cur_conn->initiator_addr = str;
		str = NULL;
	} else if (strcmp(name, "initiator_alias") == 0) {
		cur_conn->initiator_alias = str;
		str = NULL;
	} else if (strcmp(name, "target") == 0) {
		cur_conn->target = str;
		str = NULL;
	} else if (strcmp(name, "target_alias") == 0) {
		cur_conn->target_alias = str;
		str = NULL;
	} else if (strcmp(name, "target_portal_group_tag") == 0) {
	} else if (strcmp(name, "header_digest") == 0) {
		cur_conn->header_digest = str;
		str = NULL;
	} else if (strcmp(name, "data_digest") == 0) {
		cur_conn->data_digest = str;
		str = NULL;
	} else if (strcmp(name, "max_recv_data_segment_length") == 0) {
		cur_conn->max_recv_data_segment_length = str;
		str = NULL;
	} else if (strcmp(name, "max_send_data_segment_length") == 0) {
		cur_conn->max_send_data_segment_length = str;
		str = NULL;
	} else if (strcmp(name, "max_burst_length") == 0) {
		cur_conn->max_burst_length = str;
		str = NULL;
	} else if (strcmp(name, "first_burst_length") == 0) {
		cur_conn->first_burst_length = str;
		str = NULL;
	} else if (strcmp(name, "offload") == 0) {
		cur_conn->offload = str;
		str = NULL;
	} else if (strcmp(name, "immediate_data") == 0) {
		cur_conn->immediate_data = atoi(str);
	} else if (strcmp(name, "iser") == 0) {
		cur_conn->iser = atoi(str);
	} else if (strcmp(name, "connection") == 0) {
		islist->cur_conn = NULL;
	} else if (strcmp(name, "ctlislist") == 0) {
		/* Nothing. */
	} else {
		/*
		 * Unknown element; ignore it for forward compatibility.
		 */
	}

	free(str);
}

static void
cctl_islist_char_handler(void *user_data, const XML_Char *str, int len)
{
	struct cctl_islist_data *islist;

	islist = (struct cctl_islist_data *)user_data;

	sbuf_bcat(islist->cur_sb[islist->level], str, len);
}

static int
cctl_islist(int fd, int argc, char **argv, char *combinedopt)
{
	struct ctl_iscsi req;
	struct cctl_islist_data islist;
	struct cctl_islist_conn *conn;
	XML_Parser parser;
	char *conn_str;
	int conn_len;
	int dump_xml = 0;
	int c, retval, verbose = 0;

	retval = 0;
	conn_len = 4096;

	bzero(&islist, sizeof(islist));
	STAILQ_INIT(&islist.conn_list);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'v':
			verbose = 1;
			break;
		case 'x':
			dump_xml = 1;
			break;
		default:
			break;
		}
	}

retry:
	conn_str = malloc(conn_len);

	bzero(&req, sizeof(req));
	req.type = CTL_ISCSI_LIST;
	req.data.list.alloc_len = conn_len;
	req.data.list.conn_xml = conn_str;

	if (ioctl(fd, CTL_ISCSI, &req) == -1) {
		warn("%s: error issuing CTL_ISCSI ioctl", __func__);
		retval = 1;
		goto bailout;
	}

	if (req.status == CTL_ISCSI_ERROR) {
		warnx("%s: error returned from CTL_ISCSI ioctl:\n%s",
		      __func__, req.error_str);
	} else if (req.status == CTL_ISCSI_LIST_NEED_MORE_SPACE) {
		conn_len = conn_len << 1;
		goto retry;
	}

	if (dump_xml != 0) {
		printf("%s", conn_str);
		goto bailout;
	}

	parser = XML_ParserCreate(NULL);
	if (parser == NULL) {
		warn("%s: Unable to create XML parser", __func__);
		retval = 1;
		goto bailout;
	}

	XML_SetUserData(parser, &islist);
	XML_SetElementHandler(parser, cctl_islist_start_element,
	    cctl_islist_end_element);
	XML_SetCharacterDataHandler(parser, cctl_islist_char_handler);

	retval = XML_Parse(parser, conn_str, strlen(conn_str), 1);
	if (retval != 1) {
		warnx("%s: Unable to parse XML: Error %d", __func__,
		    XML_GetErrorCode(parser));
		XML_ParserFree(parser);
		retval = 1;
		goto bailout;
	}
	retval = 0;
	XML_ParserFree(parser);

	if (verbose != 0) {
		STAILQ_FOREACH(conn, &islist.conn_list, links) {
			printf("%-25s %d\n", "Session ID:", conn->connection_id);
			printf("%-25s %s\n", "Initiator name:", conn->initiator);
			printf("%-25s %s\n", "Initiator portal:", conn->initiator_addr);
			printf("%-25s %s\n", "Initiator alias:", conn->initiator_alias);
			printf("%-25s %s\n", "Target name:", conn->target);
			printf("%-25s %s\n", "Target alias:", conn->target_alias);
			printf("%-25s %s\n", "Header digest:", conn->header_digest);
			printf("%-25s %s\n", "Data digest:", conn->data_digest);
			printf("%-25s %s\n", "MaxRecvDataSegmentLength:", conn->max_recv_data_segment_length);
			printf("%-25s %s\n", "MaxSendDataSegmentLength:", conn->max_send_data_segment_length);
			printf("%-25s %s\n", "MaxBurstLen:", conn->max_burst_length);
			printf("%-25s %s\n", "FirstBurstLen:", conn->first_burst_length);
			printf("%-25s %s\n", "ImmediateData:", conn->immediate_data ? "Yes" : "No");
			printf("%-25s %s\n", "iSER (RDMA):", conn->iser ? "Yes" : "No");
			printf("%-25s %s\n", "Offload driver:", conn->offload);
			printf("\n");
		}
	} else {
		printf("%4s %-16s %-36s %-36s\n", "ID", "Portal", "Initiator name",
		    "Target name");
		STAILQ_FOREACH(conn, &islist.conn_list, links) {
			printf("%4u %-16s %-36s %-36s\n",
			    conn->connection_id, conn->initiator_addr, conn->initiator,
			    conn->target);
		}
	}
bailout:
	free(conn_str);

	return (retval);
}

static int
cctl_islogout(int fd, int argc, char **argv, char *combinedopt)
{
	struct ctl_iscsi req;
	int retval = 0, c;
	int all = 0, connection_id = -1, nargs = 0;
	char *initiator_name = NULL, *initiator_addr = NULL;

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'a':
			all = 1;
			nargs++;
			break;
		case 'c':
			connection_id = strtoul(optarg, NULL, 0);
			nargs++;
			break;
		case 'i':
			initiator_name = strdup(optarg);
			if (initiator_name == NULL)
				err(1, "%s: strdup", __func__);
			nargs++;
			break;
		case 'p':
			initiator_addr = strdup(optarg);
			if (initiator_addr == NULL)
				err(1, "%s: strdup", __func__);
			nargs++;
			break;
		default:
			break;
		}
	}

	if (nargs == 0)
		errx(1, "%s: either -a, -c, -i, or -p must be specified",
		    __func__);
	if (nargs > 1)
		errx(1, "%s: only one of -a, -c, -i, or -p may be specified",
		    __func__);

	bzero(&req, sizeof(req));
	req.type = CTL_ISCSI_LOGOUT;
	req.data.logout.connection_id = connection_id;
	if (initiator_addr != NULL)
		strlcpy(req.data.logout.initiator_addr,
		    initiator_addr, sizeof(req.data.logout.initiator_addr));
	if (initiator_name != NULL)
		strlcpy(req.data.logout.initiator_name,
		    initiator_name, sizeof(req.data.logout.initiator_name));
	if (all != 0)
		req.data.logout.all = 1;

	if (ioctl(fd, CTL_ISCSI, &req) == -1) {
		warn("%s: error issuing CTL_ISCSI ioctl", __func__);
		retval = 1;
		goto bailout;
	}

	if (req.status != CTL_ISCSI_OK) {
		warnx("%s: error returned from CTL iSCSI logout request:\n%s",
		      __func__, req.error_str);
		retval = 1;
		goto bailout;
	}

	printf("iSCSI logout requests submitted\n");

bailout:
	return (retval);
}

static int
cctl_isterminate(int fd, int argc, char **argv, char *combinedopt)
{
	struct ctl_iscsi req;
	int retval = 0, c;
	int all = 0, connection_id = -1, nargs = 0;
	char *initiator_name = NULL, *initiator_addr = NULL;

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'a':
			all = 1;
			nargs++;
			break;
		case 'c':
			connection_id = strtoul(optarg, NULL, 0);
			nargs++;
			break;
		case 'i':
			initiator_name = strdup(optarg);
			if (initiator_name == NULL)
				err(1, "%s: strdup", __func__);
			nargs++;
			break;
		case 'p':
			initiator_addr = strdup(optarg);
			if (initiator_addr == NULL)
				err(1, "%s: strdup", __func__);
			nargs++;
			break;
		default:
			break;
		}
	}

	if (nargs == 0)
		errx(1, "%s: either -a, -c, -i, or -p must be specified",
		    __func__);
	if (nargs > 1)
		errx(1, "%s: only one of -a, -c, -i, or -p may be specified",
		    __func__);

	bzero(&req, sizeof(req));
	req.type = CTL_ISCSI_TERMINATE;
	req.data.terminate.connection_id = connection_id;
	if (initiator_addr != NULL)
		strlcpy(req.data.terminate.initiator_addr,
		    initiator_addr, sizeof(req.data.terminate.initiator_addr));
	if (initiator_name != NULL)
		strlcpy(req.data.terminate.initiator_name,
		    initiator_name, sizeof(req.data.terminate.initiator_name));
	if (all != 0)
		req.data.terminate.all = 1;

	if (ioctl(fd, CTL_ISCSI, &req) == -1) {
		warn("%s: error issuing CTL_ISCSI ioctl", __func__);
		retval = 1;
		goto bailout;
	}

	if (req.status != CTL_ISCSI_OK) {
		warnx("%s: error returned from CTL iSCSI connection "
		    "termination request:\n%s", __func__, req.error_str);
		retval = 1;
		goto bailout;
	}

	printf("iSCSI connections terminated\n");

bailout:
	return (retval);
}

/*
 * Name/value pair used for per-LUN attributes.
 */
struct cctl_lun_nv {
	char *name;
	char *value;
	STAILQ_ENTRY(cctl_lun_nv) links;
};

/*
 * Backend LUN information.
 */
struct cctl_lun {
	uint64_t lun_id;
	char *backend_type;
	uint64_t size_blocks;
	uint32_t blocksize;
	char *serial_number;
	char *device_id;
	STAILQ_HEAD(,cctl_lun_nv) attr_list;
	STAILQ_ENTRY(cctl_lun) links;
};

struct cctl_devlist_data {
	int num_luns;
	STAILQ_HEAD(,cctl_lun) lun_list;
	struct cctl_lun *cur_lun;
	int level;
	struct sbuf *cur_sb[32];
};

static void
cctl_start_element(void *user_data, const char *name, const char **attr)
{
	int i;
	struct cctl_devlist_data *devlist;
	struct cctl_lun *cur_lun;

	devlist = (struct cctl_devlist_data *)user_data;
	cur_lun = devlist->cur_lun;
	devlist->level++;
	if ((u_int)devlist->level >= (sizeof(devlist->cur_sb) /
	    sizeof(devlist->cur_sb[0])))
		errx(1, "%s: too many nesting levels, %zd max", __func__,
		     sizeof(devlist->cur_sb) / sizeof(devlist->cur_sb[0]));

	devlist->cur_sb[devlist->level] = sbuf_new_auto();
	if (devlist->cur_sb[devlist->level] == NULL)
		err(1, "%s: Unable to allocate sbuf", __func__);

	if (strcmp(name, "lun") == 0) {
		if (cur_lun != NULL)
			errx(1, "%s: improper lun element nesting", __func__);

		cur_lun = calloc(1, sizeof(*cur_lun));
		if (cur_lun == NULL)
			err(1, "%s: cannot allocate %zd bytes", __func__,
			    sizeof(*cur_lun));

		devlist->num_luns++;
		devlist->cur_lun = cur_lun;

		STAILQ_INIT(&cur_lun->attr_list);
		STAILQ_INSERT_TAIL(&devlist->lun_list, cur_lun, links);

		for (i = 0; attr[i] != NULL; i += 2) {
			if (strcmp(attr[i], "id") == 0) {
				cur_lun->lun_id = strtoull(attr[i+1], NULL, 0);
			} else {
				errx(1, "%s: invalid LUN attribute %s = %s",
				     __func__, attr[i], attr[i+1]);
			}
		}
	}
}

static void
cctl_end_element(void *user_data, const char *name)
{
	struct cctl_devlist_data *devlist;
	struct cctl_lun *cur_lun;
	char *str;

	devlist = (struct cctl_devlist_data *)user_data;
	cur_lun = devlist->cur_lun;

	if ((cur_lun == NULL)
	 && (strcmp(name, "ctllunlist") != 0))
		errx(1, "%s: cur_lun == NULL! (name = %s)", __func__, name);

	if (devlist->cur_sb[devlist->level] == NULL)
		errx(1, "%s: no valid sbuf at level %d (name %s)", __func__,
		     devlist->level, name);

	if (sbuf_finish(devlist->cur_sb[devlist->level]) != 0)
		err(1, "%s: sbuf_finish", __func__);
	str = strdup(sbuf_data(devlist->cur_sb[devlist->level]));
	if (str == NULL)
		err(1, "%s can't allocate %zd bytes for string", __func__,
		    sbuf_len(devlist->cur_sb[devlist->level]));

	if (strlen(str) == 0) {
		free(str);
		str = NULL;
	}

	sbuf_delete(devlist->cur_sb[devlist->level]);
	devlist->cur_sb[devlist->level] = NULL;
	devlist->level--;

	if (strcmp(name, "backend_type") == 0) {
		cur_lun->backend_type = str;
		str = NULL;
	} else if (strcmp(name, "size") == 0) {
		cur_lun->size_blocks = strtoull(str, NULL, 0);
	} else if (strcmp(name, "blocksize") == 0) {
		cur_lun->blocksize = strtoul(str, NULL, 0);
	} else if (strcmp(name, "serial_number") == 0) {
		cur_lun->serial_number = str;
		str = NULL;
	} else if (strcmp(name, "device_id") == 0) {
		cur_lun->device_id = str;
		str = NULL;
	} else if (strcmp(name, "lun") == 0) {
		devlist->cur_lun = NULL;
	} else if (strcmp(name, "ctllunlist") == 0) {
		/* Nothing. */
	} else {
		struct cctl_lun_nv *nv;

		nv = calloc(1, sizeof(*nv));
		if (nv == NULL)
			err(1, "%s: can't allocate %zd bytes for nv pair",
			    __func__, sizeof(*nv));

		nv->name = strdup(name);
		if (nv->name == NULL)
			err(1, "%s: can't allocated %zd bytes for string",
			    __func__, strlen(name));

		nv->value = str;
		str = NULL;
		STAILQ_INSERT_TAIL(&cur_lun->attr_list, nv, links);
	}

	free(str);
}

static void
cctl_char_handler(void *user_data, const XML_Char *str, int len)
{
	struct cctl_devlist_data *devlist;

	devlist = (struct cctl_devlist_data *)user_data;

	sbuf_bcat(devlist->cur_sb[devlist->level], str, len);
}

static int
cctl_devlist(int fd, int argc, char **argv, char *combinedopt)
{
	struct ctl_lun_list list;
	struct cctl_devlist_data devlist;
	struct cctl_lun *lun;
	XML_Parser parser;
	char *lun_str;
	int lun_len;
	int dump_xml = 0;
	int retval, c;
	char *backend = NULL;
	int verbose = 0;

	retval = 0;
	lun_len = 4096;

	bzero(&devlist, sizeof(devlist));
	STAILQ_INIT(&devlist.lun_list);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'b':
			backend = strdup(optarg);
			break;
		case 'v':
			verbose++;
			break;
		case 'x':
			dump_xml = 1;
			break;
		default:
			break;
		}
	}

retry:
	lun_str = malloc(lun_len);

	bzero(&list, sizeof(list));
	list.alloc_len = lun_len;
	list.status = CTL_LUN_LIST_NONE;
	list.lun_xml = lun_str;

	if (ioctl(fd, CTL_LUN_LIST, &list) == -1) {
		warn("%s: error issuing CTL_LUN_LIST ioctl", __func__);
		retval = 1;
		goto bailout;
	}

	if (list.status == CTL_LUN_LIST_ERROR) {
		warnx("%s: error returned from CTL_LUN_LIST ioctl:\n%s",
		      __func__, list.error_str);
	} else if (list.status == CTL_LUN_LIST_NEED_MORE_SPACE) {
		lun_len = lun_len << 1;
		goto retry;
	}

	if (dump_xml != 0) {
		printf("%s", lun_str);
		goto bailout;
	}

	parser = XML_ParserCreate(NULL);
	if (parser == NULL) {
		warn("%s: Unable to create XML parser", __func__);
		retval = 1;
		goto bailout;
	}

	XML_SetUserData(parser, &devlist);
	XML_SetElementHandler(parser, cctl_start_element, cctl_end_element);
	XML_SetCharacterDataHandler(parser, cctl_char_handler);

	retval = XML_Parse(parser, lun_str, strlen(lun_str), 1);
	if (retval != 1) {
		warnx("%s: Unable to parse XML: Error %d", __func__,
		    XML_GetErrorCode(parser));
		XML_ParserFree(parser);
		retval = 1;
		goto bailout;
	}
	retval = 0;
	XML_ParserFree(parser);

	printf("LUN Backend  %18s %4s %-16s %-16s\n", "Size (Blocks)", "BS",
	       "Serial Number", "Device ID");
	STAILQ_FOREACH(lun, &devlist.lun_list, links) {
		struct cctl_lun_nv *nv;

		if ((backend != NULL)
		 && (strcmp(lun->backend_type, backend) != 0))
			continue;

		printf("%3ju %-8s %18ju %4u %-16s %-16s\n",
		       (uintmax_t)lun->lun_id,
		       lun->backend_type, (uintmax_t)lun->size_blocks,
		       lun->blocksize, lun->serial_number, lun->device_id);

		if (verbose == 0)
			continue;

		STAILQ_FOREACH(nv, &lun->attr_list, links) {
			printf("      %s=%s\n", nv->name, nv->value);
		}
	}
bailout:
	free(lun_str);

	return (retval);
}

/*
 * Port information.
 */
struct cctl_port {
	uint64_t port_id;
	char *online;
	char *frontend_type;
	char *name;
	int pp, vp;
	char *target, *port, *lun_map;
	STAILQ_HEAD(,cctl_lun_nv) init_list;
	STAILQ_HEAD(,cctl_lun_nv) lun_list;
	STAILQ_HEAD(,cctl_lun_nv) attr_list;
	STAILQ_ENTRY(cctl_port) links;
};

struct cctl_portlist_data {
	int num_ports;
	STAILQ_HEAD(,cctl_port) port_list;
	struct cctl_port *cur_port;
	int level;
	uint64_t cur_id;
	struct sbuf *cur_sb[32];
};

static void
cctl_start_pelement(void *user_data, const char *name, const char **attr)
{
	int i;
	struct cctl_portlist_data *portlist;
	struct cctl_port *cur_port;

	portlist = (struct cctl_portlist_data *)user_data;
	cur_port = portlist->cur_port;
	portlist->level++;
	if ((u_int)portlist->level >= (sizeof(portlist->cur_sb) /
	    sizeof(portlist->cur_sb[0])))
		errx(1, "%s: too many nesting levels, %zd max", __func__,
		     sizeof(portlist->cur_sb) / sizeof(portlist->cur_sb[0]));

	portlist->cur_sb[portlist->level] = sbuf_new_auto();
	if (portlist->cur_sb[portlist->level] == NULL)
		err(1, "%s: Unable to allocate sbuf", __func__);

	portlist->cur_id = 0;
	for (i = 0; attr[i] != NULL; i += 2) {
		if (strcmp(attr[i], "id") == 0) {
			portlist->cur_id = strtoull(attr[i+1], NULL, 0);
			break;
		}
	}

	if (strcmp(name, "targ_port") == 0) {
		if (cur_port != NULL)
			errx(1, "%s: improper port element nesting", __func__);

		cur_port = calloc(1, sizeof(*cur_port));
		if (cur_port == NULL)
			err(1, "%s: cannot allocate %zd bytes", __func__,
			    sizeof(*cur_port));

		portlist->num_ports++;
		portlist->cur_port = cur_port;

		STAILQ_INIT(&cur_port->init_list);
		STAILQ_INIT(&cur_port->lun_list);
		STAILQ_INIT(&cur_port->attr_list);
		cur_port->port_id = portlist->cur_id;
		STAILQ_INSERT_TAIL(&portlist->port_list, cur_port, links);
	}
}

static void
cctl_end_pelement(void *user_data, const char *name)
{
	struct cctl_portlist_data *portlist;
	struct cctl_port *cur_port;
	char *str;

	portlist = (struct cctl_portlist_data *)user_data;
	cur_port = portlist->cur_port;

	if ((cur_port == NULL)
	 && (strcmp(name, "ctlportlist") != 0))
		errx(1, "%s: cur_port == NULL! (name = %s)", __func__, name);

	if (portlist->cur_sb[portlist->level] == NULL)
		errx(1, "%s: no valid sbuf at level %d (name %s)", __func__,
		     portlist->level, name);

	if (sbuf_finish(portlist->cur_sb[portlist->level]) != 0)
		err(1, "%s: sbuf_finish", __func__);
	str = strdup(sbuf_data(portlist->cur_sb[portlist->level]));
	if (str == NULL)
		err(1, "%s can't allocate %zd bytes for string", __func__,
		    sbuf_len(portlist->cur_sb[portlist->level]));

	if (strlen(str) == 0) {
		free(str);
		str = NULL;
	}

	sbuf_delete(portlist->cur_sb[portlist->level]);
	portlist->cur_sb[portlist->level] = NULL;
	portlist->level--;

	if (strcmp(name, "frontend_type") == 0) {
		cur_port->frontend_type = str;
		str = NULL;
	} else if (strcmp(name, "port_name") == 0) {
		cur_port->name = str;
		str = NULL;
	} else if (strcmp(name, "online") == 0) {
		cur_port->online = str;
		str = NULL;
	} else if (strcmp(name, "physical_port") == 0) {
		cur_port->pp = strtoull(str, NULL, 0);
	} else if (strcmp(name, "virtual_port") == 0) {
		cur_port->vp = strtoull(str, NULL, 0);
	} else if (strcmp(name, "target") == 0) {
		cur_port->target = str;
		str = NULL;
	} else if (strcmp(name, "port") == 0) {
		cur_port->port = str;
		str = NULL;
	} else if (strcmp(name, "lun_map") == 0) {
		cur_port->lun_map = str;
		str = NULL;
	} else if (strcmp(name, "targ_port") == 0) {
		portlist->cur_port = NULL;
	} else if (strcmp(name, "ctlportlist") == 0) {
		/* Nothing. */
	} else {
		struct cctl_lun_nv *nv;

		nv = calloc(1, sizeof(*nv));
		if (nv == NULL)
			err(1, "%s: can't allocate %zd bytes for nv pair",
			    __func__, sizeof(*nv));

		if (strcmp(name, "initiator") == 0 ||
		    strcmp(name, "lun") == 0)
			asprintf(&nv->name, "%ju", portlist->cur_id);
		else
			nv->name = strdup(name);
		if (nv->name == NULL)
			err(1, "%s: can't allocated %zd bytes for string",
			    __func__, strlen(name));

		nv->value = str;
		str = NULL;
		if (strcmp(name, "initiator") == 0)
			STAILQ_INSERT_TAIL(&cur_port->init_list, nv, links);
		else if (strcmp(name, "lun") == 0)
			STAILQ_INSERT_TAIL(&cur_port->lun_list, nv, links);
		else
			STAILQ_INSERT_TAIL(&cur_port->attr_list, nv, links);
	}

	free(str);
}

static void
cctl_char_phandler(void *user_data, const XML_Char *str, int len)
{
	struct cctl_portlist_data *portlist;

	portlist = (struct cctl_portlist_data *)user_data;

	sbuf_bcat(portlist->cur_sb[portlist->level], str, len);
}

static int
cctl_portlist(int fd, int argc, char **argv, char *combinedopt)
{
	struct ctl_lun_list list;
	struct cctl_portlist_data portlist;
	struct cctl_port *port;
	XML_Parser parser;
	char *port_str = NULL;
	int port_len;
	int dump_xml = 0;
	int retval, c;
	char *frontend = NULL;
	uint64_t portarg = UINT64_MAX;
	int verbose = 0, init = 0, lun = 0, quiet = 0;

	retval = 0;
	port_len = 4096;

	bzero(&portlist, sizeof(portlist));
	STAILQ_INIT(&portlist.port_list);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'f':
			frontend = strdup(optarg);
			break;
		case 'i':
			init++;
			break;
		case 'l':
			lun++;
			break;
		case 'p':
			portarg = strtoll(optarg, NULL, 0);
			break;
		case 'q':
			quiet++;
			break;
		case 'v':
			verbose++;
			break;
		case 'x':
			dump_xml = 1;
			break;
		default:
			break;
		}
	}

retry:
	port_str = (char *)realloc(port_str, port_len);

	bzero(&list, sizeof(list));
	list.alloc_len = port_len;
	list.status = CTL_LUN_LIST_NONE;
	list.lun_xml = port_str;

	if (ioctl(fd, CTL_PORT_LIST, &list) == -1) {
		warn("%s: error issuing CTL_PORT_LIST ioctl", __func__);
		retval = 1;
		goto bailout;
	}

	if (list.status == CTL_LUN_LIST_ERROR) {
		warnx("%s: error returned from CTL_PORT_LIST ioctl:\n%s",
		      __func__, list.error_str);
	} else if (list.status == CTL_LUN_LIST_NEED_MORE_SPACE) {
		port_len = port_len << 1;
		goto retry;
	}

	if (dump_xml != 0) {
		printf("%s", port_str);
		goto bailout;
	}

	parser = XML_ParserCreate(NULL);
	if (parser == NULL) {
		warn("%s: Unable to create XML parser", __func__);
		retval = 1;
		goto bailout;
	}

	XML_SetUserData(parser, &portlist);
	XML_SetElementHandler(parser, cctl_start_pelement, cctl_end_pelement);
	XML_SetCharacterDataHandler(parser, cctl_char_phandler);

	retval = XML_Parse(parser, port_str, strlen(port_str), 1);
	if (retval != 1) {
		warnx("%s: Unable to parse XML: Error %d", __func__,
		    XML_GetErrorCode(parser));
		XML_ParserFree(parser);
		retval = 1;
		goto bailout;
	}
	retval = 0;
	XML_ParserFree(parser);

	if (quiet == 0)
		printf("Port Online Frontend Name     pp vp\n");
	STAILQ_FOREACH(port, &portlist.port_list, links) {
		struct cctl_lun_nv *nv;

		if ((frontend != NULL)
		 && (strcmp(port->frontend_type, frontend) != 0))
			continue;

		if ((portarg != UINT64_MAX) && (portarg != port->port_id))
			continue;

		printf("%-4ju %-6s %-8s %-8s %-2d %-2d %s\n",
		    (uintmax_t)port->port_id, port->online,
		    port->frontend_type, port->name, port->pp, port->vp,
		    port->port ? port->port : "");

		if (init || verbose) {
			if (port->target)
				printf("  Target: %s\n", port->target);
			STAILQ_FOREACH(nv, &port->init_list, links) {
				printf("  Initiator %s: %s\n",
				    nv->name, nv->value);
			}
		}

		if (lun || verbose) {
			if (port->lun_map) {
				STAILQ_FOREACH(nv, &port->lun_list, links)
					printf("  LUN %s: %s\n",
					    nv->name, nv->value);
				if (STAILQ_EMPTY(&port->lun_list))
					printf("  No LUNs mapped\n");
			} else
				printf("  All LUNs mapped\n");
		}

		if (verbose) {
			STAILQ_FOREACH(nv, &port->attr_list, links) {
				printf("      %s=%s\n", nv->name, nv->value);
			}
		}
	}
bailout:
	free(port_str);

	return (retval);
}

static int
cctl_lunmap(int fd, int argc, char **argv, char *combinedopt)
{
	struct ctl_lun_map lm;
	int retval = 0, c;

	retval = 0;
	lm.port = UINT32_MAX;
	lm.plun = UINT32_MAX;
	lm.lun = UINT32_MAX;

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'p':
			lm.port = strtoll(optarg, NULL, 0);
			break;
		case 'l':
			lm.plun = strtoll(optarg, NULL, 0);
			break;
		case 'L':
			lm.lun = strtoll(optarg, NULL, 0);
			break;
		default:
			break;
		}
	}

	if (ioctl(fd, CTL_LUN_MAP, &lm) == -1) {
		warn("%s: error issuing CTL_LUN_MAP ioctl", __func__);
		retval = 1;
	}

	return (retval);
}

void
usage(int error)
{
	fprintf(error ? stderr : stdout,
"Usage:\n"
"Primary commands:\n"
"         ctladm tur         [dev_id][general options]\n"
"         ctladm inquiry     [dev_id][general options]\n"
"         ctladm devid       [dev_id][general options]\n"
"         ctladm reqsense    [dev_id][general options]\n"
"         ctladm reportluns  [dev_id][general options]\n"
"         ctladm read        [dev_id][general options] <-l lba> <-d len>\n"
"                            <-f file|-> <-b blocksize> [-c cdbsize][-N]\n"
"         ctladm write       [dev_id][general options] <-l lba> <-d len>\n"
"                            <-f file|-> <-b blocksize> [-c cdbsize][-N]\n"
"         ctladm readcap     [dev_id][general options] [-c cdbsize]\n"
"         ctladm modesense   [dev_id][general options] <-m page|-l> [-P pc]\n"
"                            [-d] [-S subpage] [-c cdbsize]\n"
"         ctladm prin        [dev_id][general options] <-a action>\n"
"         ctladm prout       [dev_id][general options] <-a action>\n"
"                            <-r restype] [-k key] [-s sa_key]\n"
"         ctladm rtpg        [dev_id][general options]\n"
"         ctladm start       [dev_id][general options] [-i] [-o]\n"
"         ctladm stop        [dev_id][general options] [-i] [-o]\n"
"         ctladm synccache   [dev_id][general options] [-l lba]\n"
"                            [-b blockcount] [-r] [-i] [-c cdbsize]\n"
"         ctladm create      <-b backend> [-B blocksize] [-d device_id]\n"
"                            [-l lun_id] [-o name=value] [-s size_bytes]\n"
"                            [-S serial_num] [-t dev_type]\n"
"         ctladm remove      <-b backend> <-l lun_id> [-o name=value]\n"
"         ctladm modify      <-b backend> <-l lun_id> <-s size_bytes>\n"
"         ctladm devlist     [-b backend] [-v] [-x]\n"
"         ctladm lunlist\n"
"         ctladm lunmap      -p targ_port [-l pLUN] [-L cLUN]\n"
"         ctladm delay       [dev_id] <-l datamove|done> [-T oneshot|cont]\n"
"                            [-t secs]\n"
"         ctladm inject      [dev_id] <-i action> <-p pattern> [-r lba,len]\n"
"                            [-s len fmt [args]] [-c] [-d delete_id]\n"
"         ctladm port        <-o <on|off> | [-w wwnn][-W wwpn]>\n"
"                            [-p targ_port] [-t port_type]\n"
"                            <-c> [-d driver] [-O name=value]\n"
"                            <-r> <-p targ_port>\n"
"         ctladm portlist    [-f frontend] [-i] [-p targ_port] [-q] [-v] [-x]\n"
"         ctladm islist      [-v | -x]\n"
"         ctladm islogout    <-a | -c connection-id | -i name | -p portal>\n"
"         ctladm isterminate <-a | -c connection-id | -i name | -p portal>\n"
"         ctladm dumpooa\n"
"         ctladm dumpstructs\n"
"         ctladm help\n"
"General Options:\n"
"-I intiator_id           : defaults to 7, used to change the initiator id\n"
"-C retries               : specify the number of times to retry this command\n"
"-D devicename            : specify the device to operate on\n"
"                         : (default is %s)\n"
"read/write options:\n"
"-l lba                   : logical block address\n"
"-d len                   : read/write length, in blocks\n"
"-f file|-                : write/read data to/from file or stdout/stdin\n"
"-b blocksize             : block size, in bytes\n"
"-c cdbsize               : specify minimum cdb size: 6, 10, 12 or 16\n"
"-N                       : do not copy data to/from userland\n"
"readcapacity options:\n"
"-c cdbsize               : specify minimum cdb size: 10 or 16\n"
"modesense options:\n"
"-m page                  : specify the mode page to view\n"
"-l                       : request a list of supported pages\n"
"-P pc                    : specify the page control value: 0-3 (current,\n"
"                           changeable, default, saved, respectively)\n"
"-d                       : disable block descriptors for mode sense\n"
"-S subpage               : specify a subpage\n"
"-c cdbsize               : specify minimum cdb size: 6 or 10\n"
"persistent reserve in options:\n"
"-a action                : specify the action value: 0-2 (read key, read\n"
"                           reservation, read capabilities, respectively)\n"
"persistent reserve out options:\n"
"-a action                : specify the action value: 0-5 (register, reserve,\n"
"                           release, clear, preempt, register and ignore)\n"
"-k key                   : key value\n"
"-s sa_key                : service action value\n"
"-r restype               : specify the reservation type: 0-5(wr ex, ex ac,\n"
"                           wr ex ro, ex ac ro, wr ex ar, ex ac ar)\n"
"start/stop options:\n"
"-i                       : set the immediate bit (CTL does not support this)\n"
"-o                       : set the on/offline bit\n"
"synccache options:\n"
"-l lba                   : set the starting LBA\n"
"-b blockcount            : set the length to sync in blocks\n"
"-r                       : set the relative addressing bit\n"
"-i                       : set the immediate bit\n"
"-c cdbsize               : specify minimum cdb size: 10 or 16\n"
"create options:\n"
"-b backend               : backend name (\"block\", \"ramdisk\", etc.)\n"
"-B blocksize             : LUN blocksize in bytes (some backends)\n"
"-d device_id             : SCSI VPD page 0x83 ID\n"
"-l lun_id                : requested LUN number\n"
"-o name=value            : backend-specific options, multiple allowed\n"
"-s size_bytes            : LUN size in bytes (some backends)\n"
"-S serial_num            : SCSI VPD page 0x80 serial number\n"
"-t dev_type              : SCSI device type (0=disk, 3=processor)\n"
"remove options:\n"
"-b backend               : backend name (\"block\", \"ramdisk\", etc.)\n"
"-l lun_id                : LUN number to delete\n"
"-o name=value            : backend-specific options, multiple allowed\n"
"devlist options:\n"
"-b backend               : list devices from specified backend only\n"
"-v                       : be verbose, show backend attributes\n"
"-x                       : dump raw XML\n"
"delay options:\n"
"-l datamove|done         : delay command at datamove or done phase\n"
"-T oneshot               : delay one command, then resume normal completion\n"
"-T cont                  : delay all commands\n"
"-t secs                  : number of seconds to delay\n"
"inject options:\n"
"-i error_action          : action to perform\n"
"-p pattern               : command pattern to look for\n"
"-r lba,len               : LBA range for pattern\n"
"-s len fmt [args]        : sense data for custom sense action\n"
"-c                       : continuous operation\n"
"-d delete_id             : error id to delete\n"
"port options:\n"
"-c                       : create new ioctl or iscsi frontend port\n"
"-d                       : specify ioctl or iscsi frontend type\n"
"-l                       : list frontend ports\n"
"-o on|off                : turn frontend ports on or off\n"
"-O pp|vp                 : create new frontend port using pp and/or vp\n"
"-w wwnn                  : set WWNN for one frontend\n"
"-W wwpn                  : set WWPN for one frontend\n"
"-t port_type             : specify fc, scsi, ioctl, internal frontend type\n"
"-p targ_port             : specify target port number\n"
"-r                       : remove frontend port\n" 
"-q                       : omit header in list output\n"
"-x                       : output port list in XML format\n"
"portlist options:\n"
"-f frontend              : specify frontend type\n"
"-i                       : report target and initiators addresses\n"
"-l                       : report LUN mapping\n"
"-p targ_port             : specify target port number\n"
"-q                       : omit header in list output\n"
"-v                       : verbose output (report all port options)\n"
"-x                       : output port list in XML format\n"
"lunmap options:\n"
"-p targ_port             : specify target port number\n"
"-L pLUN                  : specify port-visible LUN\n"
"-L cLUN                  : specify CTL LUN\n",
CTL_DEFAULT_DEV);
}

int
main(int argc, char **argv)
{
	int c;
	ctladm_cmdfunction command;
	ctladm_cmdargs cmdargs;
	ctladm_optret optreturn;
	char *device;
	const char *mainopt = "C:D:I:";
	const char *subopt = NULL;
	char combinedopt[256];
	int lun;
	int optstart = 2;
	int retval, fd;
	int retries;
	int initid;
	int saved_errno;

	retval = 0;
	cmdargs = CTLADM_ARG_NONE;
	command = CTLADM_CMD_HELP;
	device = NULL;
	fd = -1;
	retries = 0;
	lun = 0;
	initid = 7;

	if (argc < 2) {
		usage(1);
		retval = 1;
		goto bailout;
	}

	/*
	 * Get the base option.
	 */
	optreturn = getoption(option_table,argv[1], &command, &cmdargs,&subopt);

	if (optreturn == CC_OR_AMBIGUOUS) {
		warnx("ambiguous option %s", argv[1]);
		usage(0);
		exit(1);
	} else if (optreturn == CC_OR_NOT_FOUND) {
		warnx("option %s not found", argv[1]);
		usage(0);
		exit(1);
	}

	if (cmdargs & CTLADM_ARG_NEED_TL) {
		if ((argc < 3) || (!isdigit(argv[2][0]))) {
			warnx("option %s requires a lun argument",
			      argv[1]);
			usage(0);
			exit(1);
		}
		lun = strtol(argv[2], NULL, 0);

		cmdargs |= CTLADM_ARG_TARG_LUN;
		optstart++;
	}

	/*
	 * Ahh, getopt(3) is a pain.
	 *
	 * This is a gross hack.  There really aren't many other good
	 * options (excuse the pun) for parsing options in a situation like
	 * this.  getopt is kinda braindead, so you end up having to run
	 * through the options twice, and give each invocation of getopt
	 * the option string for the other invocation.
	 *
	 * You would think that you could just have two groups of options.
	 * The first group would get parsed by the first invocation of
	 * getopt, and the second group would get parsed by the second
	 * invocation of getopt.  It doesn't quite work out that way.  When
	 * the first invocation of getopt finishes, it leaves optind pointing
	 * to the argument _after_ the first argument in the second group.
	 * So when the second invocation of getopt comes around, it doesn't
	 * recognize the first argument it gets and then bails out.
	 *
	 * A nice alternative would be to have a flag for getopt that says
	 * "just keep parsing arguments even when you encounter an unknown
	 * argument", but there isn't one.  So there's no real clean way to
	 * easily parse two sets of arguments without having one invocation
	 * of getopt know about the other.
	 *
	 * Without this hack, the first invocation of getopt would work as
	 * long as the generic arguments are first, but the second invocation
	 * (in the subfunction) would fail in one of two ways.  In the case
	 * where you don't set optreset, it would fail because optind may be
	 * pointing to the argument after the one it should be pointing at.
	 * In the case where you do set optreset, and reset optind, it would
	 * fail because getopt would run into the first set of options, which
	 * it doesn't understand.
	 *
	 * All of this would "sort of" work if you could somehow figure out
	 * whether optind had been incremented one option too far.  The
	 * mechanics of that, however, are more daunting than just giving
	 * both invocations all of the expect options for either invocation.
	 *
	 * Needless to say, I wouldn't mind if someone invented a better
	 * (non-GPL!) command line parsing interface than getopt.  I
	 * wouldn't mind if someone added more knobs to getopt to make it
	 * work better.  Who knows, I may talk myself into doing it someday,
	 * if the standards weenies let me.  As it is, it just leads to
	 * hackery like this and causes people to avoid it in some cases.
	 *
	 * KDM, September 8th, 1998
	 */
	if (subopt != NULL)
		sprintf(combinedopt, "%s%s", mainopt, subopt);
	else
		sprintf(combinedopt, "%s", mainopt);

	/*
	 * Start getopt processing at argv[2/3], since we've already
	 * accepted argv[1..2] as the command name, and as a possible
	 * device name.
	 */
	optind = optstart;

	/*
	 * Now we run through the argument list looking for generic
	 * options, and ignoring options that possibly belong to
	 * subfunctions.
	 */
	while ((c = getopt(argc, argv, combinedopt))!= -1){
		switch (c) {
		case 'C':
			cmdargs |= CTLADM_ARG_RETRIES;
			retries = strtol(optarg, NULL, 0);
			break;
		case 'D':
			device = strdup(optarg);
			cmdargs |= CTLADM_ARG_DEVICE;
			break;
		case 'I':
			cmdargs |= CTLADM_ARG_INITIATOR;
			initid = strtol(optarg, NULL, 0);
			break;
		default:
			break;
		}
	}

	if ((cmdargs & CTLADM_ARG_INITIATOR) == 0)
		initid = 7;

	optind = optstart;
	optreset = 1;

	/*
	 * Default to opening the CTL device for now.
	 */
	if (((cmdargs & CTLADM_ARG_DEVICE) == 0)
	 && (command != CTLADM_CMD_HELP)) {
		device = strdup(CTL_DEFAULT_DEV);
		cmdargs |= CTLADM_ARG_DEVICE;
	}

	if ((cmdargs & CTLADM_ARG_DEVICE)
	 && (command != CTLADM_CMD_HELP)) {
		fd = open(device, O_RDWR);
		if (fd == -1 && errno == ENOENT) {
			saved_errno = errno;
			retval = kldload("ctl");
			if (retval != -1)
				fd = open(device, O_RDWR);
			else
				errno = saved_errno;
		}
		if (fd == -1) {
			fprintf(stderr, "%s: error opening %s: %s\n",
				argv[0], device, strerror(errno));
			retval = 1;
			goto bailout;
		}
#ifdef	WANT_ISCSI
		else {
			if (modfind("cfiscsi") == -1 &&
			    kldload("cfiscsi") == -1)
				warn("couldn't load cfiscsi");
		}
#endif
	} else if ((command != CTLADM_CMD_HELP)
		&& ((cmdargs & CTLADM_ARG_DEVICE) == 0)) {
		fprintf(stderr, "%s: you must specify a device with the "
			"--device argument for this command\n", argv[0]);
		command = CTLADM_CMD_HELP;
		retval = 1;
	}

	switch (command) {
	case CTLADM_CMD_TUR:
		retval = cctl_tur(fd, lun, initid, retries);
		break;
	case CTLADM_CMD_INQUIRY:
		retval = cctl_inquiry(fd, lun, initid, retries);
		break;
	case CTLADM_CMD_REQ_SENSE:
		retval = cctl_req_sense(fd, lun, initid, retries);
		break;
	case CTLADM_CMD_REPORT_LUNS:
		retval = cctl_report_luns(fd, lun, initid, retries);
		break;
	case CTLADM_CMD_CREATE:
		retval = cctl_create_lun(fd, argc, argv, combinedopt);
		break;
	case CTLADM_CMD_RM:
		retval = cctl_rm_lun(fd, argc, argv, combinedopt);
		break;
	case CTLADM_CMD_DEVLIST:
		retval = cctl_devlist(fd, argc, argv, combinedopt);
		break;
	case CTLADM_CMD_READ:
	case CTLADM_CMD_WRITE:
		retval = cctl_read_write(fd, lun, initid, retries,
					 argc, argv, combinedopt, command);
		break;
	case CTLADM_CMD_PORT:
		retval = cctl_port(fd, argc, argv, combinedopt);
		break;
	case CTLADM_CMD_PORTLIST:
		retval = cctl_portlist(fd, argc, argv, combinedopt);
		break;
	case CTLADM_CMD_LUNMAP:
		retval = cctl_lunmap(fd, argc, argv, combinedopt);
		break;
	case CTLADM_CMD_READCAPACITY:
		retval = cctl_read_capacity(fd, lun, initid, retries,
					    argc, argv, combinedopt);
		break;
	case CTLADM_CMD_MODESENSE:
		retval = cctl_mode_sense(fd, lun, initid, retries,
					 argc, argv, combinedopt);
		break;
	case CTLADM_CMD_START:
	case CTLADM_CMD_STOP:
		retval = cctl_start_stop(fd, lun, initid, retries,
					 (command == CTLADM_CMD_START) ? 1 : 0,
					 argc, argv, combinedopt);
		break;
	case CTLADM_CMD_SYNC_CACHE:
		retval = cctl_sync_cache(fd, lun, initid, retries,
					 argc, argv, combinedopt);
		break;
	case CTLADM_CMD_LUNLIST:
		retval = cctl_lunlist(fd);
		break;
	case CTLADM_CMD_DELAY:
		retval = cctl_delay(fd, lun, argc, argv, combinedopt);
		break;
	case CTLADM_CMD_ERR_INJECT:
		retval = cctl_error_inject(fd, lun, argc, argv,
					   combinedopt);
		break;
	case CTLADM_CMD_DUMPOOA:
		retval = cctl_dump_ooa(fd, argc, argv);
		break;
	case CTLADM_CMD_DUMPSTRUCTS:
		retval = cctl_dump_structs(fd, cmdargs);
		break;
	case CTLADM_CMD_PRES_IN:
		retval = cctl_persistent_reserve_in(fd, lun, initid,
		                                    argc, argv, combinedopt,
						    retries);
		break;
	case CTLADM_CMD_PRES_OUT:
		retval = cctl_persistent_reserve_out(fd, lun, initid,
						     argc, argv, combinedopt,
						     retries);
		break;
	case CTLADM_CMD_INQ_VPD_DEVID:
	        retval = cctl_inquiry_vpd_devid(fd, lun, initid);
		break;
	case CTLADM_CMD_RTPG:
	        retval = cctl_report_target_port_group(fd, lun, initid);
		break;
	case CTLADM_CMD_MODIFY:
	        retval = cctl_modify_lun(fd, argc, argv, combinedopt);
		break;
	case CTLADM_CMD_ISLIST:
	        retval = cctl_islist(fd, argc, argv, combinedopt);
		break;
	case CTLADM_CMD_ISLOGOUT:
	        retval = cctl_islogout(fd, argc, argv, combinedopt);
		break;
	case CTLADM_CMD_ISTERMINATE:
	        retval = cctl_isterminate(fd, argc, argv, combinedopt);
		break;
	case CTLADM_CMD_HELP:
	default:
		usage(retval);
		break;
	}
bailout:

	if (fd != -1)
		close(fd);

	exit (retval);
}

/*
 * vim: ts=8
 */
