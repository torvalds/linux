/* $OpenBSD: bioctl.c,v 1.159 2025/04/18 20:58:06 kn Exp $ */

/*
 * Copyright (c) 2004, 2005 Marco Peereboom
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>	/* NODEV */
#include <sys/ioctl.h>
#include <sys/dkio.h>
#include <sys/stat.h>
#include <dev/softraidvar.h>
#include <dev/biovar.h>

#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <util.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <vis.h>
#include <readpassphrase.h>

struct locator {
	int		channel;
	int		target;
	int		lun;
};

struct timing {
	int		interval;
	int		start;
};

static void __dead	usage(void);
const char 		*str2locator(const char *, struct locator *);
const char 		*str2patrol(const char *, struct timing *);
void			bio_status(struct bio_status *);
int			bio_parse_devlist(char *, dev_t *);
void			bio_kdf_derive(struct sr_crypto_kdfinfo *,
			    struct sr_crypto_pbkdf *, char *, int);
void			bio_kdf_generate(struct sr_crypto_kdfinfo *);
int			bcrypt_pbkdf_autorounds(void);
void			derive_key(u_int32_t, int, u_int8_t *, size_t,
			    u_int8_t *, size_t, char *, int);

void			bio_inq(char *);
void			bio_alarm(char *);
int			bio_getvolbyname(char *);
void			bio_setstate(char *, int, char *);
void			bio_setblink(char *, char *, int);
void			bio_blink(char *, int, int);
void			bio_createraid(u_int16_t, char *, char *);
void			bio_deleteraid(char *);
void			bio_changepass(char *);
u_int32_t		bio_createflags(char *);
char			*bio_vis(char *);
void			bio_diskinq(char *);
void			bio_patrol(char *);

int			devh = -1;
int			human;
int			verbose;
u_int32_t		cflags = 0;
int			rflag = -1;	/* auto */
char			*passfile;

void			*bio_cookie;

int interactive = 1;

int
main(int argc, char *argv[])
{
	struct bio_locate	bl;
	u_int64_t		func = 0;
	char			*devicename = NULL;
	char			*realname = NULL, *al_arg = NULL;
	char			*bl_arg = NULL, *dev_list = NULL;
	char			*key_disk = NULL;
	const char		*errstr;
	int			ch, blink = 0, changepass = 0, diskinq = 0;
	int			ss_func = 0;
	u_int16_t		cr_level = 0;
	int			biodev = 0;

	if (argc < 2)
		usage();

	while ((ch = getopt(argc, argv, "a:b:C:c:dH:hik:l:O:Pp:qr:R:st:u:v")) !=
	    -1) {
		switch (ch) {
		case 'a': /* alarm */
			func |= BIOC_ALARM;
			al_arg = optarg;
			break;
		case 'b': /* blink */
			func |= BIOC_BLINK;
			blink = BIOC_SBBLINK;
			bl_arg = optarg;
			break;
		case 'C': /* creation flags */
			cflags = bio_createflags(optarg);
			break;
		case 'c': /* create */
			func |= BIOC_CREATERAID;
			if (strcmp(optarg, "1C") == 0) {
				cr_level = 0x1C;
			} else if (isdigit((unsigned char)*optarg)) {
				cr_level = strtonum(optarg, 0, 10, &errstr);
				if (errstr != NULL)
					errx(1, "Invalid RAID level");
			} else if (strlen(optarg) == 1) {
				cr_level = *optarg;
			} else {
				errx(1, "Invalid RAID level");
			}
			break;
		case 'd':
			/* delete volume */
			func |= BIOC_DELETERAID;
			break;
		case 'u': /* unblink */
			func |= BIOC_BLINK;
			blink = BIOC_SBUNBLINK;
			bl_arg = optarg;
			break;
		case 'H': /* set hotspare */
			func |= BIOC_SETSTATE;
			ss_func = BIOC_SSHOTSPARE;
			al_arg = optarg;
			break;
		case 'h':
			human = 1;
			break;
		case 'i': /* inquiry */
			func |= BIOC_INQ;
			break;
		case 'k': /* Key disk. */
			key_disk = optarg;
			break;
		case 'l': /* device list */
			func |= BIOC_DEVLIST;
			dev_list = optarg;
			break;
		case 'P':
			/* Change passphrase. */
			changepass = 1;
			break;
		case 'p':
			passfile = optarg;
			break;
		case 'r':
			if (strcmp(optarg, "auto") == 0) {
				rflag = -1;
				break;
			}
			rflag = strtonum(optarg, 16, 1<<30, &errstr);
			if (errstr != NULL)
				errx(1, "number of KDF rounds is %s: %s",
				    errstr, optarg);
			break;
		case 'O':
			/* set a chunk to offline */
			func |= BIOC_SETSTATE;
			ss_func = BIOC_SSOFFLINE;
			al_arg = optarg;
			break;
		case 'R':
			/* rebuild to provided chunk/CTL */
			func |= BIOC_SETSTATE;
			ss_func = BIOC_SSREBUILD;
			al_arg = optarg;
			break;
		case 's':
			interactive = 0;
			break;
		case 't': /* patrol */
			func |= BIOC_PATROL;
			al_arg = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'q':
			diskinq = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1 || (changepass && func != 0))
		usage();

	if (func == 0)
		func |= BIOC_INQ;

	devicename = argv[0];
	if (devicename == NULL)
		errx(1, "need device");

	devh = opendev(devicename, O_RDWR, OPENDEV_PART, &realname);
	if (devh == -1) {
		devh = open("/dev/bio", O_RDWR);
		if (devh == -1)
			err(1, "Can't open %s", "/dev/bio");

		memset(&bl, 0, sizeof(bl));
		bl.bl_name = devicename;
		if (ioctl(devh, BIOCLOCATE, &bl) == -1)
			errx(1, "Can't locate %s device via %s",
			    bl.bl_name, "/dev/bio");

		bio_cookie = bl.bl_bio.bio_cookie;
		biodev = 1;
		devicename = NULL;
	}

	if (diskinq) {
		bio_diskinq(devicename);
	} else if (changepass && !biodev) {
		bio_changepass(devicename);
	} else if (func & BIOC_INQ) {
		bio_inq(devicename);
	} else if (func == BIOC_ALARM) {
		bio_alarm(al_arg);
	} else if (func == BIOC_BLINK) {
		bio_setblink(devicename, bl_arg, blink);
	} else if (func == BIOC_PATROL) {
		bio_patrol(al_arg);
	} else if (func == BIOC_SETSTATE) {
		bio_setstate(al_arg, ss_func, argv[0]);
	} else if (func == BIOC_DELETERAID && !biodev) {
		bio_deleteraid(devicename);
	} else if (func & BIOC_CREATERAID || func & BIOC_DEVLIST) {
		if (!(func & BIOC_CREATERAID))
			errx(1, "need -c parameter");
		if (!(func & BIOC_DEVLIST))
			errx(1, "need -l parameter");
		if (!biodev)
			errx(1, "must use bio device");
		bio_createraid(cr_level, dev_list, key_disk);
	}

	return (0);
}

static void __dead
usage(void)
{
	extern char		*__progname;

	fprintf(stderr,
		"usage: %s [-hiqv] [-a alarm-function] "
		"[-b channel:target[.lun]]\n"
		"\t[-H channel:target[.lun]] "
		"[-R chunk | channel:target[.lun]]\n"
		"\t[-t patrol-function] "
		"[-u channel:target[.lun]] "
		"device\n\n"
		"       %s [-dhiPqsv] "
		"[-C flag[,...]] [-c raidlevel] [-k keydisk]\n"
		"\t[-l chunk[,...]] "
		"[-O device | channel:target[.lun]] [-p passfile]\n"
		"\t[-R chunk | channel:target[.lun]] [-r rounds] "
		"device\n", __progname, __progname);

	exit(1);
}

const char *
str2locator(const char *string, struct locator *location)
{
	const char		*errstr;
	char			parse[80], *targ, *lun;

	strlcpy(parse, string, sizeof parse);
	targ = strchr(parse, ':');
	if (targ == NULL)
		return ("target not specified");
	*targ++ = '\0';

	lun = strchr(targ, '.');
	if (lun != NULL) {
		*lun++ = '\0';
		location->lun = strtonum(lun, 0, 256, &errstr);
		if (errstr)
			return (errstr);
	} else
		location->lun = 0;

	location->target = strtonum(targ, 0, 256, &errstr);
	if (errstr)
		return (errstr);
	location->channel = strtonum(parse, 0, 256, &errstr);
	if (errstr)
		return (errstr);
	return (NULL);
}

const char *
str2patrol(const char *string, struct timing *timing)
{
	const char		*errstr;
	char			parse[80], *interval = NULL, *start = NULL;

	timing->interval = 0;
	timing->start = 0;

	strlcpy(parse, string, sizeof parse);

	interval = strchr(parse, '.');
	if (interval != NULL) {
		*interval++ = '\0';
		start = strchr(interval, '.');
		if (start != NULL)
			*start++ = '\0';
	}
	if (interval != NULL) {
		/* -1 == continuously */
		timing->interval = strtonum(interval, -1, INT_MAX, &errstr);
		if (errstr)
			return (errstr);
	}
	if (start != NULL) {
		timing->start = strtonum(start, 0, INT_MAX, &errstr);
		if (errstr)
			return (errstr);
	}

	return (NULL);
}

void
bio_status(struct bio_status *bs)
{
	extern char		*__progname;
	char			*prefix;
	int			i;

	if (strlen(bs->bs_controller))
		prefix = bs->bs_controller;
	else
		prefix = __progname;

	for (i = 0; i < bs->bs_msg_count; i++)
		fprintf(bs->bs_msgs[i].bm_type == BIO_MSG_INFO ?
		    stdout : stderr, "%s: %s\n", prefix, bs->bs_msgs[i].bm_msg);

	if (bs->bs_status == BIO_STATUS_ERROR) {
		if (bs->bs_msg_count == 0)
			errx(1, "unknown error");
		else
			exit(1);
	}
}

void
bio_inq(char *name)
{
	char 			*status, *cache;
	char			size[64], scsiname[16], volname[32];
	char			percent[20], seconds[20];
	int			i, d, volheader, hotspare, unused;
	char			encname[16], serial[32];
	struct bioc_inq		bi;
	struct bioc_vol		bv;
	struct bioc_disk	bd;

	memset(&bi, 0, sizeof(bi));

	bi.bi_bio.bio_cookie = bio_cookie;

	if (ioctl(devh, BIOCINQ, &bi) == -1) {
		if (errno == ENOTTY)
			bio_diskinq(name);
		else
			err(1, "BIOCINQ");
		return;
	}

	bio_status(&bi.bi_bio.bio_status);

	volheader = 0;
	for (i = 0; i < bi.bi_novol; i++) {
		memset(&bv, 0, sizeof(bv));
		bv.bv_bio.bio_cookie = bio_cookie;
		bv.bv_volid = i;
		bv.bv_percent = -1;
		bv.bv_seconds = 0;

		if (ioctl(devh, BIOCVOL, &bv) == -1)
			err(1, "BIOCVOL");

		bio_status(&bv.bv_bio.bio_status);

		if (name && strcmp(name, bv.bv_dev) != 0)
			continue;

		if (!volheader) {
			volheader = 1;
			printf("%-11s %-10s %14s %-8s\n",
			    "Volume", "Status", "Size", "Device");
		}

		percent[0] = '\0';
		seconds[0] = '\0';
		if (bv.bv_percent != -1)
			snprintf(percent, sizeof percent,
			    " %d%% done", bv.bv_percent);
		if (bv.bv_seconds)
			snprintf(seconds, sizeof seconds,
			    " %u seconds", bv.bv_seconds);
		switch (bv.bv_status) {
		case BIOC_SVONLINE:
			status = BIOC_SVONLINE_S;
			break;
		case BIOC_SVOFFLINE:
			status = BIOC_SVOFFLINE_S;
			break;
		case BIOC_SVDEGRADED:
			status = BIOC_SVDEGRADED_S;
			break;
		case BIOC_SVBUILDING:
			status = BIOC_SVBUILDING_S;
			break;
		case BIOC_SVREBUILD:
			status = BIOC_SVREBUILD_S;
			break;
		case BIOC_SVSCRUB:
			status = BIOC_SVSCRUB_S;
			break;
		case BIOC_SVINVALID:
		default:
			status = BIOC_SVINVALID_S;
		}
		switch (bv.bv_cache) {
		case BIOC_CVWRITEBACK:
			cache = BIOC_CVWRITEBACK_S;
			break;
		case BIOC_CVWRITETHROUGH:
			cache = BIOC_CVWRITETHROUGH_S;
			break;
		case BIOC_CVUNKNOWN:
		default:
			cache = BIOC_CVUNKNOWN_S;
		}

		snprintf(volname, sizeof volname, "%s %u",
		    bi.bi_dev, bv.bv_volid);

		unused = 0;
		hotspare = 0;
		if (bv.bv_level == -1 && bv.bv_nodisk == 1)
			hotspare = 1;
		else if (bv.bv_level == -2 && bv.bv_nodisk == 1)
			unused = 1;
		else {
			if (human)
				fmt_scaled(bv.bv_size, size);
			else
				snprintf(size, sizeof size, "%14llu",
				    bv.bv_size);
			printf("%11s %-10s %14s %-7s ",
			    volname, status, size, bv.bv_dev);
			switch (bv.bv_level) {
			case 'C':
				printf("CRYPTO%s%s\n",
				    percent, seconds);
				break;
			case 'c':
				printf("CONCAT%s%s\n",
				    percent, seconds);
				break;
			case 0x1C:
			case 0x1E:
				printf("RAID%X%s%s %s\n",
				    bv.bv_level, percent, seconds, cache);
				break;
			default:
				printf("RAID%u%s%s %s\n",
				    bv.bv_level, percent, seconds, cache);
				break;
			}
			
		}

		for (d = 0; d < bv.bv_nodisk; d++) {
			memset(&bd, 0, sizeof(bd));
			bd.bd_bio.bio_cookie = bio_cookie;
			bd.bd_diskid = d;
			bd.bd_volid = i;
			bd.bd_patrol.bdp_percent = -1;
			bd.bd_patrol.bdp_seconds = 0;

			if (ioctl(devh, BIOCDISK, &bd) == -1)
				err(1, "BIOCDISK");
		
			bio_status(&bd.bd_bio.bio_status);

			switch (bd.bd_status) {
			case BIOC_SDONLINE:
				status = BIOC_SDONLINE_S;
				break;
			case BIOC_SDOFFLINE:
				status = BIOC_SDOFFLINE_S;
				break;
			case BIOC_SDFAILED:
				status = BIOC_SDFAILED_S;
				break;
			case BIOC_SDREBUILD:
				status = BIOC_SDREBUILD_S;
				break;
			case BIOC_SDHOTSPARE:
				status = BIOC_SDHOTSPARE_S;
				break;
			case BIOC_SDUNUSED:
				status = BIOC_SDUNUSED_S;
				break;
			case BIOC_SDSCRUB:
				status = BIOC_SDSCRUB_S;
				break;
			case BIOC_SDINVALID:
			default:
				status = BIOC_SDINVALID_S;
			}

			if (hotspare || unused)
				;	/* use volname from parent volume */
			else
				snprintf(volname, sizeof volname, "    %3u",
				    bd.bd_diskid);

			if ((bv.bv_level == 'C' || bv.bv_level == 0x1C) &&
			    bd.bd_size == 0)
				snprintf(size, sizeof size, "%14s", "key disk");
			else if (human)
				fmt_scaled(bd.bd_size, size);
			else
				snprintf(size, sizeof size, "%14llu",
				    bd.bd_size);
			snprintf(scsiname, sizeof scsiname,
			    "%u:%u.%u",
			    bd.bd_channel, bd.bd_target, bd.bd_lun);
			if (bd.bd_procdev[0])
				strlcpy(encname, bd.bd_procdev, sizeof encname);
			else
				strlcpy(encname, "noencl", sizeof encname);
			if (bd.bd_serial[0])
				strlcpy(serial, bd.bd_serial, sizeof serial);
			else
				strlcpy(serial, "unknown serial", sizeof serial);

			percent[0] = '\0';
			seconds[0] = '\0';
			if (bd.bd_patrol.bdp_percent != -1)
				snprintf(percent, sizeof percent,
				    " patrol %d%% done", bd.bd_patrol.bdp_percent);
			if (bd.bd_patrol.bdp_seconds)
				snprintf(seconds, sizeof seconds,
				    " %u seconds", bd.bd_patrol.bdp_seconds);

			printf("%11s %-10s %14s %-7s %-6s <%s>\n",
			    volname, status, size, scsiname, encname,
			    bd.bd_vendor);
			if (verbose)
				printf("%11s %-10s %14s %-7s %-6s '%s'%s%s\n",
				    "", "", "", "", "", serial, percent, seconds);
		}
	}
}

void
bio_alarm(char *arg)
{
	struct bioc_alarm	ba;

	memset(&ba, 0, sizeof(ba));
	ba.ba_bio.bio_cookie = bio_cookie;

	switch (arg[0]) {
	case 'q': /* silence alarm */
		/* FALLTHROUGH */
	case 's':
		ba.ba_opcode = BIOC_SASILENCE;
		break;

	case 'e': /* enable alarm */
		ba.ba_opcode = BIOC_SAENABLE;
		break;

	case 'd': /* disable alarm */
		ba.ba_opcode = BIOC_SADISABLE;
		break;

	case 't': /* test alarm */
		ba.ba_opcode = BIOC_SATEST;
		break;

	case 'g': /* get alarm state */
		ba.ba_opcode = BIOC_GASTATUS;
		break;

	default:
		errx(1, "invalid alarm function: %s", arg);
	}

	if (ioctl(devh, BIOCALARM, &ba) == -1)
		err(1, "BIOCALARM");

	bio_status(&ba.ba_bio.bio_status);

	if (arg[0] == 'g')
		printf("alarm is currently %s\n",
		    ba.ba_status ? "enabled" : "disabled");
}

int
bio_getvolbyname(char *name)
{
	int			id = -1, i;
	struct bioc_inq		bi;
	struct bioc_vol		bv;

	memset(&bi, 0, sizeof(bi));
	bi.bi_bio.bio_cookie = bio_cookie;
	if (ioctl(devh, BIOCINQ, &bi) == -1)
		err(1, "BIOCINQ");

	bio_status(&bi.bi_bio.bio_status);

	for (i = 0; i < bi.bi_novol; i++) {
		memset(&bv, 0, sizeof(bv));
		bv.bv_bio.bio_cookie = bio_cookie;
		bv.bv_volid = i;
		if (ioctl(devh, BIOCVOL, &bv) == -1)
			err(1, "BIOCVOL");

		bio_status(&bv.bv_bio.bio_status);

		if (name && strcmp(name, bv.bv_dev) != 0)
			continue;
		id = i;
		break;
	}

	return (id);
}

void
bio_setstate(char *arg, int status, char *devicename)
{
	struct bioc_setstate	bs;
	struct locator		location;
	struct stat		sb;
	const char		*errstr;

	memset(&bs, 0, sizeof(bs));
	if (stat(arg, &sb) == -1) {
		/* use CTL */
		errstr = str2locator(arg, &location);
		if (errstr)
			errx(1, "Target %s: %s", arg, errstr);
		bs.bs_channel = location.channel;
		bs.bs_target = location.target;
		bs.bs_lun = location.lun;
	} else {
		/* use other id */
		bs.bs_other_id = sb.st_rdev;
		bs.bs_other_id_type = BIOC_SSOTHER_DEVT;
	}

	bs.bs_bio.bio_cookie = bio_cookie;
	bs.bs_status = status;

	if (status != BIOC_SSHOTSPARE) {
		/* make sure user supplied a sd device */
		bs.bs_volid = bio_getvolbyname(devicename);
		if (bs.bs_volid == -1)
			errx(1, "invalid device %s", devicename);
	}

	if (ioctl(devh, BIOCSETSTATE, &bs) == -1)
		err(1, "BIOCSETSTATE");

	bio_status(&bs.bs_bio.bio_status);
}

void
bio_setblink(char *name, char *arg, int blink)
{
	struct locator		location;
	struct bioc_blink	bb;
	struct bioc_inq		bi;
	struct bioc_vol		bv;
	struct bioc_disk	bd;
	const char		*errstr;
	int			v, d, rv;

	errstr = str2locator(arg, &location);
	if (errstr)
		errx(1, "Target %s: %s", arg, errstr);

	/* try setting blink on the device directly */
	memset(&bb, 0, sizeof(bb));
	bb.bb_bio.bio_cookie = bio_cookie;
	bb.bb_status = blink;
	bb.bb_target = location.target;
	bb.bb_channel = location.channel;
	rv = ioctl(devh, BIOCBLINK, &bb);

	if (rv == 0 && bb.bb_bio.bio_status.bs_status == BIO_STATUS_UNKNOWN)
		return;

	if (rv == 0 && bb.bb_bio.bio_status.bs_status == BIO_STATUS_SUCCESS) {
		bio_status(&bb.bb_bio.bio_status);
		return;
	}

	/* if the blink didn't work, try to find something that will */

	memset(&bi, 0, sizeof(bi));
	bi.bi_bio.bio_cookie = bio_cookie;
	if (ioctl(devh, BIOCINQ, &bi) == -1)
		err(1, "BIOCINQ");

	bio_status(&bi.bi_bio.bio_status);

	for (v = 0; v < bi.bi_novol; v++) {
		memset(&bv, 0, sizeof(bv));
		bv.bv_bio.bio_cookie = bio_cookie;
		bv.bv_volid = v;
		if (ioctl(devh, BIOCVOL, &bv) == -1)
			err(1, "BIOCVOL");

		bio_status(&bv.bv_bio.bio_status);

		if (name && strcmp(name, bv.bv_dev) != 0)
			continue;

		for (d = 0; d < bv.bv_nodisk; d++) {
			memset(&bd, 0, sizeof(bd));
			bd.bd_bio.bio_cookie = bio_cookie;
			bd.bd_volid = v;
			bd.bd_diskid = d;

			if (ioctl(devh, BIOCDISK, &bd) == -1)
				err(1, "BIOCDISK");

			bio_status(&bd.bd_bio.bio_status);

			if (bd.bd_channel == location.channel &&
			    bd.bd_target == location.target &&
			    bd.bd_lun == location.lun) {
				if (bd.bd_procdev[0] != '\0')
					bio_blink(bd.bd_procdev,
					    location.target, blink);
				else
					warnx("Disk %s is not in an enclosure",
					    arg);
				return;
			}
		}
	}

	warnx("Disk %s does not exist", arg);
}

void
bio_blink(char *enclosure, int target, int blinktype)
{
	int			bioh;
	struct bio_locate	bl;
	struct bioc_blink	blink;

	bioh = open("/dev/bio", O_RDWR);
	if (bioh == -1)
		err(1, "Can't open %s", "/dev/bio");

	memset(&bl, 0, sizeof(bl));
	bl.bl_name = enclosure;
	if (ioctl(bioh, BIOCLOCATE, &bl) == -1)
		errx(1, "Can't locate %s device via %s", enclosure, "/dev/bio");

	memset(&blink, 0, sizeof(blink));
	blink.bb_bio.bio_cookie = bio_cookie;
	blink.bb_status = blinktype;
	blink.bb_target = target;

	if (ioctl(bioh, BIOCBLINK, &blink) == -1)
		err(1, "BIOCBLINK");

	bio_status(&blink.bb_bio.bio_status);

	close(bioh);
}

void
bio_createraid(u_int16_t level, char *dev_list, char *key_disk)
{
	struct bioc_createraid	create;
	struct sr_crypto_kdfinfo kdfinfo;
	struct sr_crypto_pbkdf	kdfhint;
	struct stat		sb;
	int			rv, no_dev, fd;
	dev_t			*dt;
	u_int16_t		min_disks = 0;

	if (!dev_list)
		errx(1, "no devices specified");

	dt = calloc(1, BIOC_CRMAXLEN);
	if (!dt)
		err(1, "not enough memory for dev_t list");

	no_dev = bio_parse_devlist(dev_list, dt);

	switch (level) {
	case 0:
		min_disks = 2;
		break;
	case 1:
		min_disks = 2;
		break;
	case 5:
		min_disks = 3;
		break;
	case 'C':
	case 0x1C:
		min_disks = 1;
		break;
	case 'c':
		min_disks = 1;
		break;
	default:
		errx(1, "unsupported RAID level");
	}

	if (no_dev < min_disks)
		errx(1, "not enough disks");

	/* for crypto raid we only allow one single chunk */
	if (level == 'C' && no_dev != min_disks)
		errx(1, "not exactly one partition");

	memset(&create, 0, sizeof(create));
	create.bc_bio.bio_cookie = bio_cookie;
	create.bc_level = level;
	create.bc_dev_list_len = no_dev * sizeof(dev_t);
	create.bc_dev_list = dt;
	create.bc_flags = BIOC_SCDEVT | cflags;
	create.bc_key_disk = NODEV;

	if ((level == 'C' || level == 0x1C) && key_disk == NULL) {

		memset(&kdfinfo, 0, sizeof(kdfinfo));
		memset(&kdfhint, 0, sizeof(kdfhint));

		create.bc_flags |= BIOC_SCNOAUTOASSEMBLE;

		create.bc_opaque = &kdfhint;
		create.bc_opaque_size = sizeof(kdfhint);
		create.bc_opaque_flags = BIOC_SOOUT;

		/* try to get KDF hint */
		if (ioctl(devh, BIOCCREATERAID, &create) == -1)
			err(1, "ioctl");

		bio_status(&create.bc_bio.bio_status);

		if (create.bc_opaque_status == BIOC_SOINOUT_OK) {
			bio_kdf_derive(&kdfinfo, &kdfhint, "Passphrase: ", 0);
			memset(&kdfhint, 0, sizeof(kdfhint));
		} else {
			bio_kdf_generate(&kdfinfo);
		}

		create.bc_opaque = &kdfinfo;
		create.bc_opaque_size = sizeof(kdfinfo);
		create.bc_opaque_flags = BIOC_SOIN;

	} else if ((level == 'C' || level == 0x1C) && key_disk != NULL) {

		/* Get device number for key disk. */
		fd = opendev(key_disk, O_RDONLY, OPENDEV_BLCK, NULL);
		if (fd == -1)
			err(1, "could not open %s", key_disk);
		if (fstat(fd, &sb) == -1) {
			int saved_errno = errno;
			close(fd);
			errc(1, saved_errno, "could not stat %s", key_disk);
		}
		close(fd);
		create.bc_key_disk = sb.st_rdev;

		memset(&kdfinfo, 0, sizeof(kdfinfo));

		kdfinfo.genkdf.len = sizeof(kdfinfo.genkdf);
		kdfinfo.genkdf.type = SR_CRYPTOKDFT_KEYDISK;
		kdfinfo.len = sizeof(kdfinfo);
		kdfinfo.flags = SR_CRYPTOKDF_HINT;

		create.bc_opaque = &kdfinfo;
		create.bc_opaque_size = sizeof(kdfinfo);
		create.bc_opaque_flags = BIOC_SOIN;

	}

	rv = ioctl(devh, BIOCCREATERAID, &create);
	explicit_bzero(&kdfinfo, sizeof(kdfinfo));
	if (rv == -1)
		err(1, "BIOCCREATERAID");

	bio_status(&create.bc_bio.bio_status);

	free(dt);
}

void
bio_kdf_derive(struct sr_crypto_kdfinfo *kdfinfo, struct sr_crypto_pbkdf
    *kdfhint, char* prompt, int verify)
{
	if (!kdfinfo)
		errx(1, "invalid KDF info");
	if (!kdfhint)
		errx(1, "invalid KDF hint");

	if (kdfhint->generic.len != sizeof(*kdfhint))
		errx(1, "KDF hint has invalid size");

	kdfinfo->flags = SR_CRYPTOKDF_KEY;
	kdfinfo->len = sizeof(*kdfinfo);

	derive_key(kdfhint->generic.type, kdfhint->rounds,
	    kdfinfo->maskkey, sizeof(kdfinfo->maskkey),
	    kdfhint->salt, sizeof(kdfhint->salt),
	    prompt, verify);
}

void
bio_kdf_generate(struct sr_crypto_kdfinfo *kdfinfo)
{
	if (!kdfinfo)
		errx(1, "invalid KDF info");

	if (rflag == -1)
		rflag = bcrypt_pbkdf_autorounds();

	kdfinfo->pbkdf.generic.len = sizeof(kdfinfo->pbkdf);
	kdfinfo->pbkdf.generic.type = SR_CRYPTOKDFT_BCRYPT_PBKDF;
	kdfinfo->pbkdf.rounds = rflag;

	kdfinfo->flags = SR_CRYPTOKDF_KEY | SR_CRYPTOKDF_HINT;
	kdfinfo->len = sizeof(*kdfinfo);

	/* generate salt */
	arc4random_buf(kdfinfo->pbkdf.salt, sizeof(kdfinfo->pbkdf.salt));

	derive_key(kdfinfo->pbkdf.generic.type, kdfinfo->pbkdf.rounds,
	    kdfinfo->maskkey, sizeof(kdfinfo->maskkey),
	    kdfinfo->pbkdf.salt, sizeof(kdfinfo->pbkdf.salt),
	    "New passphrase: ", interactive);
}

int
bio_parse_devlist(char *lst, dev_t *dt)
{
	char			*s, *e;
	u_int32_t		sz = 0;
	int			no_dev = 0, i, x;
	struct stat		sb;
	char			dev[PATH_MAX];
	int			fd;

	if (!lst)
		errx(1, "invalid device list");

	s = e = lst;
	/* make sure we have a valid device list like /dev/sdNa,/dev/sdNNa */
	while (*e != '\0') {
		if (*e == ',')
			s = e + 1;
		else if (*(e + 1) == '\0' || *(e + 1) == ',') {
			/* got one */
			sz = e - s + 1;
			strlcpy(dev, s, sz + 1);
			fd = opendev(dev, O_RDONLY, OPENDEV_BLCK, NULL);
			if (fd == -1)
				err(1, "could not open %s", dev);
			if (fstat(fd, &sb) == -1) {
				int saved_errno = errno;
				close(fd);
				errc(1, saved_errno, "could not stat %s", dev);
			}
			close(fd);
			dt[no_dev] = sb.st_rdev;
			no_dev++;
			if (no_dev > (int)(BIOC_CRMAXLEN / sizeof(dev_t)))
				errx(1, "too many devices on device list");
		}
		e++;
	}

	for (i = 0; i < no_dev; i++)
		for (x = 0; x < no_dev; x++)
			if (dt[i] == dt[x] && x != i)
				errx(1, "duplicate device in list");

	return (no_dev);
}

u_int32_t
bio_createflags(char *lst)
{
	char			*s, *e, fs[32];
	u_int32_t		sz = 0;
	u_int32_t		flags = 0;

	if (!lst)
		errx(1, "invalid flags list");

	s = e = lst;
	/* make sure we have a valid flags list like force,noassemeble */
	while (*e != '\0') {
		if (*e == ',')
			s = e + 1;
		else if (*(e + 1) == '\0' || *(e + 1) == ',') {
			/* got one */
			sz = e - s + 1;
			switch (s[0]) {
			case 'f':
				flags |= BIOC_SCFORCE;
				break;
			case 'n':
				flags |= BIOC_SCNOAUTOASSEMBLE;
				break;
			default:
				strlcpy(fs, s, sz + 1);
				errx(1, "invalid flag %s", fs);
			}
		}
		e++;
	}

	return (flags);
}

void
bio_deleteraid(char *dev)
{
	struct bioc_deleteraid	bd;
	memset(&bd, 0, sizeof(bd));

	bd.bd_bio.bio_cookie = bio_cookie;
	/* XXX make this a dev_t instead of a string */
	strlcpy(bd.bd_dev, dev, sizeof bd.bd_dev);
	if (ioctl(devh, BIOCDELETERAID, &bd) == -1)
		err(1, "BIOCDELETERAID");

	bio_status(&bd.bd_bio.bio_status);
}

void
bio_changepass(char *dev)
{
	struct bioc_discipline bd;
	struct sr_crypto_kdfpair kdfpair;
	struct sr_crypto_kdfinfo kdfinfo1, kdfinfo2;
	struct sr_crypto_pbkdf kdfhint;
	int rv;

	memset(&bd, 0, sizeof(bd));
	memset(&kdfhint, 0, sizeof(kdfhint));
	memset(&kdfinfo1, 0, sizeof(kdfinfo1));
	memset(&kdfinfo2, 0, sizeof(kdfinfo2));

	/* XXX use dev_t instead of string. */
	strlcpy(bd.bd_dev, dev, sizeof(bd.bd_dev));
	bd.bd_cmd = SR_IOCTL_GET_KDFHINT;
	bd.bd_size = sizeof(kdfhint);
	bd.bd_data = &kdfhint;

	if (ioctl(devh, BIOCDISCIPLINE, &bd) == -1)
		err(1, "BIOCDISCIPLINE");

	bio_status(&bd.bd_bio.bio_status);

	/* Current passphrase. */
	bio_kdf_derive(&kdfinfo1, &kdfhint, "Old passphrase: ", 0);

	if (rflag == -1) {
		rflag = bcrypt_pbkdf_autorounds();

		/* Use previous number of rounds for the same KDF if higher. */
		if (kdfhint.generic.type == SR_CRYPTOKDFT_BCRYPT_PBKDF &&
		    rflag < kdfhint.rounds)
			rflag = kdfhint.rounds;
	}

	/* New passphrase. */
	bio_kdf_generate(&kdfinfo2);

	kdfpair.kdfinfo1 = &kdfinfo1;
	kdfpair.kdfsize1 = sizeof(kdfinfo1);
	kdfpair.kdfinfo2 = &kdfinfo2;
	kdfpair.kdfsize2 = sizeof(kdfinfo2);

	bd.bd_cmd = SR_IOCTL_CHANGE_PASSPHRASE;
	bd.bd_size = sizeof(kdfpair);
	bd.bd_data = &kdfpair;

	rv = ioctl(devh, BIOCDISCIPLINE, &bd);

	memset(&kdfhint, 0, sizeof(kdfhint));
	explicit_bzero(&kdfinfo1, sizeof(kdfinfo1));
	explicit_bzero(&kdfinfo2, sizeof(kdfinfo2));

	if (rv == -1)
		err(1, "BIOCDISCIPLINE");

	bio_status(&bd.bd_bio.bio_status);
}

#define BIOCTL_VIS_NBUF		4
#define BIOCTL_VIS_BUFLEN	80

char *
bio_vis(char *s)
{
	static char	 rbuf[BIOCTL_VIS_NBUF][BIOCTL_VIS_BUFLEN];
	static uint	 idx = 0;
	char		*buf;

	buf = rbuf[idx++];
	if (idx == BIOCTL_VIS_NBUF)
		idx = 0;

	strnvis(buf, s, BIOCTL_VIS_BUFLEN, VIS_NL|VIS_CSTYLE);
	return (buf);
}

void
bio_diskinq(char *sd_dev)
{
	struct dk_inquiry	di;

	if (ioctl(devh, DIOCINQ, &di) == -1)
		err(1, "DIOCINQ");

	printf("%s: <%s, %s, %s>, serial %s\n", sd_dev, bio_vis(di.vendor),
	    bio_vis(di.product), bio_vis(di.revision), bio_vis(di.serial));
}

void
bio_patrol(char *arg)
{
	struct bioc_patrol	bp;
	struct timing		timing;
	const char		*errstr;

	memset(&bp, 0, sizeof(bp));
	bp.bp_bio.bio_cookie = bio_cookie;

	switch (arg[0]) {
	case 'a':
		bp.bp_opcode = BIOC_SPAUTO;
		break;

	case 'm':
		bp.bp_opcode = BIOC_SPMANUAL;
		break;

	case 'd':
		bp.bp_opcode = BIOC_SPDISABLE;
		break;

	case 'g': /* get patrol state */
		bp.bp_opcode = BIOC_GPSTATUS;
		break;

	case 's': /* start/stop patrol */
		if (strncmp("sta", arg, 3) == 0)
			bp.bp_opcode = BIOC_SPSTART;
		else
			bp.bp_opcode = BIOC_SPSTOP;
		break;

	default:
		errx(1, "invalid patrol function: %s", arg);
	}

	switch (arg[0]) {
	case 'a':
		errstr = str2patrol(arg, &timing);
		if (errstr)
			errx(1, "Patrol %s: %s", arg, errstr);
		bp.bp_autoival = timing.interval;
		bp.bp_autonext = timing.start;
		break;
	}

	if (ioctl(devh, BIOCPATROL, &bp) == -1)
		err(1, "BIOCPATROL");

	bio_status(&bp.bp_bio.bio_status);

	if (arg[0] == 'g') {
		const char *mode, *status;
		char interval[40];

		interval[0] = '\0';

		switch (bp.bp_mode) {
		case BIOC_SPMAUTO:
			mode = "auto";
			snprintf(interval, sizeof interval,
			    " interval=%d next=%d", bp.bp_autoival,
			    bp.bp_autonext - bp.bp_autonow);
			break;
		case BIOC_SPMMANUAL:
			mode = "manual";
			break;
		case BIOC_SPMDISABLED:
			mode = "disabled";
			break;
		default:
			mode = "unknown";
			break;
		}
		switch (bp.bp_status) {
		case BIOC_SPSSTOPPED:
			status = "stopped";
			break;
		case BIOC_SPSREADY:
			status = "ready";
			break;
		case BIOC_SPSACTIVE:
			status = "active";
			break;
		case BIOC_SPSABORTED:
			status = "aborted";
			break;
		default:
			status = "unknown";
			break;
		}
		printf("patrol mode: %s%s\n", mode, interval);
		printf("patrol status: %s\n", status);
	}
}

/*
 * Measure this system's performance by measuring the time for 100 rounds.
 * We are aiming for something that takes around 1s.
 */
int
bcrypt_pbkdf_autorounds(void)
{
	struct timespec before, after;
	char buf[SR_CRYPTO_MAXKEYBYTES], salt[128];
	int r = 100;
	int duration;

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &before);
	if (bcrypt_pbkdf("testpassword", strlen("testpassword"),
	    salt, sizeof(salt), buf, sizeof(buf), r) != 0)
		errx(1, "bcrypt pbkdf failed");
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &after);

	duration = after.tv_sec - before.tv_sec;
	duration *= 1000000;
	duration += (after.tv_nsec - before.tv_nsec) / 1000;

	duration /= r;
	r = 1000000 / duration;

	if (r < 16)
		r = 16;

	return r;
}

void
derive_key(u_int32_t type, int rounds, u_int8_t *key, size_t keysz,
    u_int8_t *salt, size_t saltsz, char *prompt, int verify)
{
	FILE		*f;
	size_t		pl;
	struct stat	sb;
	char		passphrase[1024], verifybuf[1024];
	int		rpp_flag = RPP_ECHO_OFF;

	if (!key)
		errx(1, "Invalid key");
	if (!salt)
		errx(1, "Invalid salt");

	if (type != SR_CRYPTOKDFT_PKCS5_PBKDF2 &&
	    type != SR_CRYPTOKDFT_BCRYPT_PBKDF)
		errx(1, "unknown KDF type %d", type);

	if (rounds < (type == SR_CRYPTOKDFT_PKCS5_PBKDF2 ? 1000 : 16))
		errx(1, "number of KDF rounds is too small: %d", rounds);

	/* get passphrase */
	if (passfile) {
		if ((f = fopen(passfile, "r")) == NULL)
			err(1, "invalid passphrase file");

		if (fstat(fileno(f), &sb) == -1)
			err(1, "can't stat passphrase file");
		if (sb.st_uid != 0)
			errx(1, "passphrase file must be owned by root");
		if ((sb.st_mode & ~S_IFMT) != (S_IRUSR | S_IWUSR))
			errx(1, "passphrase file has the wrong permissions");

		if (fgets(passphrase, sizeof(passphrase), f) == NULL)
			err(1, "can't read passphrase file");
		pl = strlen(passphrase);
		if (pl > 0 && passphrase[pl - 1] == '\n')
			passphrase[pl - 1] = '\0';
		else
			errx(1, "invalid passphrase length");

		fclose(f);
	} else {
		rpp_flag |= interactive ? RPP_REQUIRE_TTY : RPP_STDIN;

 retry:
		if (readpassphrase(prompt, passphrase, sizeof(passphrase),
		    rpp_flag) == NULL)
			err(1, "unable to read passphrase");
		if (*passphrase == '\0') {
			warnx("invalid passphrase length");
			if (interactive)
				goto retry;
			exit(1);
		}
	}

	if (verify && !passfile) {
		/* request user to re-type it */
		if (readpassphrase("Re-type passphrase: ", verifybuf,
		    sizeof(verifybuf), rpp_flag) == NULL) {
			explicit_bzero(passphrase, sizeof(passphrase));
			err(1, "unable to read passphrase");
		}
		if ((strlen(passphrase) != strlen(verifybuf)) ||
		    (strcmp(passphrase, verifybuf) != 0)) {
			explicit_bzero(passphrase, sizeof(passphrase));
			explicit_bzero(verifybuf, sizeof(verifybuf));
			if (interactive) {
				warnx("Passphrases did not match, try again");
				goto retry;
			}
			errx(1, "Passphrases did not match");
		}
		/* forget the re-typed one */
		explicit_bzero(verifybuf, sizeof(verifybuf));
	}

	/* derive key from passphrase */
	if (type == SR_CRYPTOKDFT_PKCS5_PBKDF2) {
		if (verbose)
			printf("Deriving key using PKCS#5 PBKDF2 with %i rounds...\n",
			    rounds);
		if (pkcs5_pbkdf2(passphrase, strlen(passphrase), salt, saltsz,
		    key, keysz, rounds) != 0)
			errx(1, "pkcs5_pbkdf2 failed");
	} else if (type == SR_CRYPTOKDFT_BCRYPT_PBKDF) {
		if (verbose)
			printf("Deriving key using bcrypt PBKDF with %i rounds...\n",
			    rounds);
		if (bcrypt_pbkdf(passphrase, strlen(passphrase), salt, saltsz,
		    key, keysz, rounds) != 0)
			errx(1, "bcrypt_pbkdf failed");
	} else {
		errx(1, "unknown KDF type %d", type);
	}

	/* forget passphrase */
	explicit_bzero(passphrase, sizeof(passphrase));
}
