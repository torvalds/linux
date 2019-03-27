/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2012 Semihalf
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Control application for the NAND simulator.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <dev/nand/nandsim.h>
#include <dev/nand/nand_dev.h>

#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <sysexits.h>

#include "nandsim_cfgparse.h"

#define SIMDEVICE	"/dev/nandsim.ioctl"

#define error(fmt, args...) do { \
    printf("ERROR: " fmt "\n", ##args); } while (0)

#define warn(fmt, args...) do { \
    printf("WARNING: " fmt "\n", ##args); } while (0)

#define DEBUG
#undef DEBUG

#ifdef DEBUG
#define debug(fmt, args...) do { \
    printf("NANDSIM_CONF:" fmt "\n", ##args); } while (0)
#else
#define debug(fmt, args...) do {} while(0)
#endif

#define NANDSIM_RAM_LOG_SIZE 16384

#define MSG_NOTRUNNING		"Controller#%d is not running.Please start" \
    " it first."
#define MSG_RUNNING		"Controller#%d is already running!"
#define MSG_CTRLCHIPNEEDED	"You have to specify ctrl_no:cs_no pair!"
#define MSG_STATUSACQCTRLCHIP	"Could not acquire status for ctrl#%d chip#%d"
#define MSG_STATUSACQCTRL	"Could not acquire status for ctrl#%d"
#define MSG_NOCHIP		"There is no such chip configured (chip#%d "\
    "at ctrl#%d)!"

#define MSG_NOCTRL		"Controller#%d is not configured!"
#define MSG_NOTCONFIGDCTRLCHIP	"Chip connected to ctrl#%d at cs#%d " \
    "is not configured."

typedef int (commandfunc_t)(int , char **);

static struct nandsim_command *getcommand(char *);
static int parse_devstring(char *, int *, int *);
static void printchip(struct sim_chip *, uint8_t);
static void printctrl(struct sim_ctrl *);
static int opendev(int *);
static commandfunc_t cmdstatus;
static commandfunc_t cmdconf;
static commandfunc_t cmdstart;
static commandfunc_t cmdstop;
static commandfunc_t cmdmod;
static commandfunc_t cmderror;
static commandfunc_t cmdbb;
static commandfunc_t cmdfreeze;
static commandfunc_t cmdlog;
static commandfunc_t cmdstats;
static commandfunc_t cmddump;
static commandfunc_t cmdrestore;
static commandfunc_t cmddestroy;
static commandfunc_t cmdhelp;
static int checkusage(int, int, char **);
static int is_chip_created(int, int, int *);
static int is_ctrl_created(int, int *);
static int is_ctrl_running(int, int *);
static int assert_chip_connected(int , int);
static int printstats(int, int, uint32_t, int);

struct nandsim_command {
	const char	*cmd_name;	/* Command name */
	commandfunc_t	*commandfunc;	/* Ptr to command function */
	uint8_t		req_argc;	/* Mandatory arguments count */
	const char	*usagestring;	/* Usage string */
};

static struct nandsim_command commands[] = {
	{"status", cmdstatus, 1,
	    "status <ctl_no|--all|-a> [-v]\n" },
	{"conf", cmdconf, 1,
	    "conf <filename>\n" },
	{"start", cmdstart, 1,
	    "start <ctrl_no>\n" },
	{"mod", cmdmod, 2,
	    "mod [-l <loglevel>] | <ctl_no:cs_no> [-p <prog_time>]\n"
	    "\t[-e <erase_time>] [-r <read_time>]\n"
	    "\t[-E <error_ratio>] | [-h]\n" },
	{"stop", cmdstop, 1,
	    "stop <ctrl_no>\n" },
	{"error", cmderror, 5,
	    "error <ctrl_no:cs_no> <page_num> <column> <length> <pattern>\n" },
	{"bb", cmdbb, 2,
	    "bb <ctl_no:cs_no>  [blk_num1,blk_num2,..] [-U] [-L]\n" },
	{"freeze", cmdfreeze, 1,
	    "freeze [ctrl_no]\n" },
	{"log", cmdlog, 1,
	    "log <ctrl_no|--all|-a>\n" },
	{"stats", cmdstats, 2,
	    "stats <ctrl_no:cs_no> <pagenumber>\n" },
	{"dump", cmddump, 2,
	    "dump <ctrl_no:cs_no> <filename>\n" },
	{"restore", cmdrestore, 2,
	    "restore <ctrl_no:chip_no> <filename>\n" },
	{"destroy", cmddestroy, 1,
	    "destroy <ctrl_no[:cs_no]|--all|-a>\n" },
	{"help", cmdhelp, 0,
	    "help [-v]" },
	{NULL, NULL, 0, NULL},
};


/* Parse command name, and start appropriate function */
static struct nandsim_command*
getcommand(char *arg)
{
	struct nandsim_command *opts;

	for (opts = commands; (opts != NULL) &&
	    (opts->cmd_name != NULL); opts++) {
		if (strcmp(opts->cmd_name, arg) == 0)
			return (opts);
	}
	return (NULL);
}

/*
 * Parse given string in format <ctrl_no>:<cs_no>, if possible -- set
 * ctrl and/or cs, and return 0 (success) or 1 (in case of error).
 *
 * ctrl == 0xff && chip == 0xff  : '--all' flag specified
 * ctrl != 0xff && chip != 0xff  : both ctrl & chip were specified
 * ctrl != 0xff && chip == 0xff  : only ctrl was specified
 */
static int
parse_devstring(char *str, int *ctrl, int *cs)
{
	char *tmpstr;
	unsigned int num = 0;

	/* Ignore white spaces at the beginning */
	while (isspace(*str) && (*str != '\0'))
		str++;

	*ctrl = 0xff;
	*cs = 0xff;
	if (strcmp(str, "--all") == 0 ||
	    strcmp(str, "-a") == 0) {
		/* If --all or -a is specified, ctl==chip==0xff */
		debug("CTRL=%d CHIP=%d\n", *ctrl, *cs);
		return (0);
	}
	/* Separate token and try to convert it to int */
	tmpstr = (char *)strtok(str, ":");
	if ((tmpstr != NULL) && (*tmpstr != '\0')) {
		if (convert_arguint(tmpstr, &num) != 0)
			return (1);

		if (num > MAX_SIM_DEV - 1) {
			error("Invalid ctrl_no supplied: %s. Valid ctrl_no "
			    "value must lie between 0 and 3!", tmpstr);
			return (1);
		}

		*ctrl = num;
		tmpstr = (char *)strtok(NULL, ":");

		if ((tmpstr != NULL) && (*tmpstr != '\0')) {
			if (convert_arguint(tmpstr, &num) != 0)
				return (1);

			/* Check if chip_no is valid */
			if (num > MAX_CTRL_CS - 1) {
				error("Invalid chip_no supplied: %s. Valid "
				    "chip_no value must lie between 0 and 3!",
				    tmpstr);
				return (1);
			}
			*cs = num;
		}
	} else
		/* Empty devstring supplied */
		return (1);

	debug("CTRL=%d CHIP=%d\n", *ctrl, *cs);
	return (0);
}

static int
opendev(int *fd)
{

	*fd = open(SIMDEVICE, O_RDWR);
	if (*fd == -1) {
		error("Could not open simulator device file (%s)!",
		    SIMDEVICE);
		return (EX_OSFILE);
	}
	return (EX_OK);
}

static int
opencdev(int *cdevd, int ctrl, int chip)
{
	char fname[255];

	sprintf(fname, "/dev/nandsim%d.%d", ctrl, chip);
	*cdevd = open(fname, O_RDWR);
	if (*cdevd == -1)
		return (EX_NOINPUT);

	return (EX_OK);
}

/*
 * Check if given arguments count match requirements. If no, or
 * --help (-h) flag is specified -- return 1 (print usage)
 */
static int
checkusage(int gargc, int argsreqd, char **gargv)
{

	if (gargc < argsreqd + 2 || (gargc >= (argsreqd + 2) &&
	    (strcmp(gargv[1], "--help") == 0 ||
	    strcmp(gargv[1], "-h") == 0)))
		return (1);

	return (0);
}

static int
cmdstatus(int gargc, char **gargv)
{
	int chip = 0, ctl = 0, err = 0, fd, idx, idx2, start, stop;
	uint8_t verbose = 0;
	struct sim_ctrl ctrlconf;
	struct sim_chip chipconf;

	err = parse_devstring(gargv[2], &ctl, &chip);
	if (err) {
		return (EX_USAGE);
	} else if (ctl == 0xff) {
		/* Every controller */
		start = 0;
		stop = MAX_SIM_DEV-1;
	} else {
		/* Specified controller only */
		start = ctl;
		stop = ctl;
	}

	if (opendev(&fd) != EX_OK)
		return (EX_OSFILE);

	for (idx = 0; idx < gargc; idx ++)
		if (strcmp(gargv[idx], "-v") == 0 ||
		    strcmp(gargv[idx], "--verbose") == 0)
			verbose = 1;

	for (idx = start; idx <= stop; idx++) {
		ctrlconf.num = idx;
		err = ioctl(fd, NANDSIM_STATUS_CTRL, &ctrlconf);
		if (err) {
			err = EX_SOFTWARE;
			error(MSG_STATUSACQCTRL, idx);
			continue;
		}

		printctrl(&ctrlconf);

		for (idx2 = 0; idx2 < MAX_CTRL_CS; idx2++) {
			chipconf.num = idx2;
			chipconf.ctrl_num = idx;

			err = ioctl(fd, NANDSIM_STATUS_CHIP, &chipconf);
			if (err) {
				err = EX_SOFTWARE;
				error(MSG_STATUSACQCTRL, idx);
				continue;
			}

			printchip(&chipconf, verbose);
		}
	}
	close(fd);
	return (err);
}

static int
cmdconf(int gargc __unused, char **gargv)
{
	int err;

	err = parse_config(gargv[2], SIMDEVICE);
	if (err)
		return (EX_DATAERR);

	return (EX_OK);
}

static int
cmdstart(int gargc __unused, char **gargv)
{
	int chip = 0, ctl = 0, err = 0, fd, running, state;

	err = parse_devstring(gargv[2], &ctl, &chip);
	if (err)
		return (EX_USAGE);

	err = is_ctrl_created(ctl, &state);
	if (err) {
		return (EX_SOFTWARE);
	} else if (state == 0) {
		error(MSG_NOCTRL, ctl);
		return (EX_SOFTWARE);
	}

	err = is_ctrl_running(ctl, &running);
	if (err)
		return (EX_SOFTWARE);

	if (running) {
		warn(MSG_RUNNING, ctl);
	} else {
		if (opendev(&fd) != EX_OK)
			return (EX_OSFILE);

		err = ioctl(fd, NANDSIM_START_CTRL, &ctl);
		close(fd);
		if (err) {
			error("Cannot start controller#%d", ctl);
			err = EX_SOFTWARE;
		}
	}
	return (err);
}

static int
cmdstop(int gargc __unused, char **gargv)
{
	int chip = 0, ctl = 0, err = 0, fd, running;

	err = parse_devstring(gargv[2], &ctl, &chip);
	if (err)
		return (EX_USAGE);

	err = is_ctrl_running(ctl, &running);
	if (err)
		return (EX_SOFTWARE);

	if (!running) {
		error(MSG_NOTRUNNING, ctl);
	} else {
		if (opendev(&fd) != EX_OK)
			return (EX_OSFILE);

		err = ioctl(fd, NANDSIM_STOP_CTRL, &ctl);
		close(fd);
		if (err) {
			error("Cannot stop controller#%d", ctl);
			err = EX_SOFTWARE;
		}
	}

	return (err);
}

static int
cmdmod(int gargc __unused, char **gargv)
{
	int chip, ctl, err = 0, fd = -1, i;
	struct sim_mod mods;

	if (gargc >= 4) {
		if (strcmp(gargv[2], "--loglevel") == 0 || strcmp(gargv[2],
		    "-l") == 0) {
			/* Set loglevel (ctrl:chip pair independent) */
			mods.field = SIM_MOD_LOG_LEVEL;

			if (convert_arguint(gargv[3], &mods.new_value) != 0)
				return (EX_SOFTWARE);

			if (opendev(&fd) != EX_OK)
				return (EX_OSFILE);

			err = ioctl(fd, NANDSIM_MODIFY, &mods);
			if (err) {
				error("simulator parameter %s could not be "
				    "modified !", gargv[3]);
				close(fd);
				return (EX_SOFTWARE);
			}

			debug("request : loglevel = %d\n", mods.new_value);
			close(fd);
			return (EX_OK);
		}
	}

	err = parse_devstring(gargv[2], &ctl, &chip);
	if (err)
		return (EX_USAGE);

	else if (chip == 0xff) {
		error(MSG_CTRLCHIPNEEDED);
		return (EX_USAGE);
	}

	if (!assert_chip_connected(ctl, chip))
		return (EX_SOFTWARE);

	if (opendev(&fd) != EX_OK)
		return (EX_OSFILE);

	/* Find out which flags were passed */
	for (i = 3; i < gargc; i++) {

		if (convert_arguint(gargv[i + 1], &mods.new_value) != 0)
			continue;

		if (strcmp(gargv[i], "--prog-time") == 0 ||
		    strcmp(gargv[i], "-p") == 0) {

			mods.field = SIM_MOD_PROG_TIME;
			debug("request : progtime = %d\n", mods.new_value);

		} else if (strcmp(gargv[i], "--erase-time") == 0 ||
		    strcmp(gargv[i], "-e") == 0) {

			mods.field = SIM_MOD_ERASE_TIME;
			debug("request : eraseime = %d\n", mods.new_value);

		} else if (strcmp(gargv[i], "--read-time") == 0 ||
		    strcmp(gargv[i], "-r") == 0) {

			mods.field = SIM_MOD_READ_TIME;
			debug("request : read_time = %d\n", mods.new_value);

		} else if (strcmp(gargv[i], "--error-ratio") == 0 ||
		    strcmp(gargv[i], "-E") == 0) {

			mods.field = SIM_MOD_ERROR_RATIO;
			debug("request : error_ratio = %d\n", mods.new_value);

		} else {
			/* Flag not recognized, or nothing specified. */
			error("Unrecognized flag:%s\n", gargv[i]);
			if (fd >= 0)
				close(fd);
			return (EX_USAGE);
		}

		mods.chip_num = chip;
		mods.ctrl_num = ctl;

		/* Call appropriate ioctl */
		err = ioctl(fd, NANDSIM_MODIFY, &mods);
		if (err) {
			error("simulator parameter %s could not be modified! ",
			    gargv[i]);
			continue;
		}
		i++;
	}
	close(fd);
	return (EX_OK);
}

static int
cmderror(int gargc __unused, char **gargv)
{
	uint32_t page, column, len, pattern;
	int chip = 0, ctl = 0, err = 0, fd;
	struct sim_error sim_err;

	err = parse_devstring(gargv[2], &ctl, &chip);
	if (err)
		return (EX_USAGE);

	if (chip == 0xff) {
		error(MSG_CTRLCHIPNEEDED);
		return (EX_USAGE);
	}

	if (convert_arguint(gargv[3], &page) ||
	    convert_arguint(gargv[4], &column) ||
	    convert_arguint(gargv[5], &len) ||
	    convert_arguint(gargv[6], &pattern))
		return (EX_SOFTWARE);

	if (!assert_chip_connected(ctl, chip))
		return (EX_SOFTWARE);

	sim_err.page_num = page;
	sim_err.column = column;
	sim_err.len = len;
	sim_err.pattern = pattern;
	sim_err.ctrl_num = ctl;
	sim_err.chip_num = chip;

	if (opendev(&fd) != EX_OK)
		return (EX_OSFILE);

	err = ioctl(fd, NANDSIM_INJECT_ERROR, &sim_err);

	close(fd);
	if (err) {
		error("Could not inject error !");
		return (EX_SOFTWARE);
	}
	return (EX_OK);
}

static int
cmdbb(int gargc, char **gargv)
{
	struct sim_block_state bs;
	struct chip_param_io cparams;
	uint32_t blkidx;
	int c, cdevd, chip = 0, ctl = 0, err = 0, fd, idx;
	uint8_t flagL = 0, flagU = 0;
	int *badblocks = NULL;

	/* Check for --list/-L or --unmark/-U flags */
	for (idx = 3; idx < gargc; idx++) {
		if (strcmp(gargv[idx], "--list") == 0 ||
		    strcmp(gargv[idx], "-L") == 0)
			flagL = idx;
		if (strcmp(gargv[idx], "--unmark") == 0 ||
		    strcmp(gargv[idx], "-U") == 0)
			flagU = idx;
	}

	if (flagL == 2 || flagU == 2 || flagU == 3)
		return (EX_USAGE);

	err = parse_devstring(gargv[2], &ctl, &chip);
	if (err) {
		return (EX_USAGE);
	}
	if (chip == 0xff || ctl == 0xff) {
		error(MSG_CTRLCHIPNEEDED);
		return (EX_USAGE);
	}

	bs.ctrl_num = ctl;
	bs.chip_num = chip;

	if (!assert_chip_connected(ctl, chip))
		return (EX_SOFTWARE);

	if (opencdev(&cdevd, ctl, chip) != EX_OK)
		return (EX_OSFILE);

	err = ioctl(cdevd, NAND_IO_GET_CHIP_PARAM, &cparams);
	if (err)
		return (EX_SOFTWARE);

	close(cdevd);

	bs.ctrl_num = ctl;
	bs.chip_num = chip;

	if (opendev(&fd) != EX_OK)
		return (EX_OSFILE);

	if (flagL != 3) {
		/*
		 * Flag -L was specified either after blocklist or was not
		 * specified at all.
		 */
		c = parse_intarray(gargv[3], &badblocks);

		for (idx = 0; idx < c; idx++) {
			bs.block_num = badblocks[idx];
			/* Do not change wearout */
			bs.wearout = -1;
			bs.state = (flagU == 0) ? NANDSIM_BAD_BLOCK :
			    NANDSIM_GOOD_BLOCK;

			err = ioctl(fd, NANDSIM_SET_BLOCK_STATE, &bs);
			if (err) {
				error("Could not set bad block(%d) for "
				    "controller (%d)!",
				    badblocks[idx], ctl);
				err = EX_SOFTWARE;
				break;
			}
		}
	}
	if (flagL != 0) {
		/* If flag -L was specified (anywhere) */
		for (blkidx = 0; blkidx < cparams.blocks; blkidx++) {
			bs.block_num = blkidx;
			/* Do not change the wearout */
			bs.wearout = -1;
			err = ioctl(fd, NANDSIM_GET_BLOCK_STATE, &bs);
			if (err) {
				error("Could not acquire block state");
				err = EX_SOFTWARE;
				continue;
			}
			printf("Block#%d: wear count: %d %s\n", blkidx,
			    bs.wearout,
			    (bs.state == NANDSIM_BAD_BLOCK) ? "BAD":"GOOD");
		}
	}
	close(fd);
	return (err);
}

static int
cmdfreeze(int gargc __unused, char **gargv)
{
	int chip = 0, ctl = 0, err = 0, fd, i, start = 0, state, stop = 0;
	struct sim_ctrl_chip ctrlchip;

	err = parse_devstring(gargv[2], &ctl, &chip);
	if (err)
		return (EX_USAGE);

	if (ctl == 0xff) {
		error("You have to specify at least controller number");
		return (EX_USAGE);
	}

	if (ctl != 0xff && chip == 0xff) {
		start = 0;
		stop = MAX_CTRL_CS - 1;
	} else {
		start = chip;
		stop = chip;
	}

	ctrlchip.ctrl_num = ctl;

	err = is_ctrl_running(ctl, &state);
	if (err)
		return (EX_SOFTWARE);
	if (state == 0) {
		error(MSG_NOTRUNNING, ctl);
		return (EX_SOFTWARE);
	}

	if (opendev(&fd) != EX_OK)
		return (EX_OSFILE);

	for (i = start; i <= stop; i++) {
		err = is_chip_created(ctl, i, &state);
		if (err)
			return (EX_SOFTWARE);
		else if (state == 0) {
			continue;
		}

		ctrlchip.chip_num = i;
		err = ioctl(fd, NANDSIM_FREEZE, &ctrlchip);
		if (err) {
			error("Could not freeze ctrl#%d chip#%d", ctl, i);
			close(fd);
			return (EX_SOFTWARE);
		}
	}
	close(fd);
	return (EX_OK);
}

static int
cmdlog(int gargc __unused, char **gargv)
{
	struct sim_log log;
	int chip = 0, ctl = 0, err = 0, fd, idx, start = 0, stop = 0;
	char *logbuf;

	err = parse_devstring(gargv[2], &ctl, &chip);
	if (err)
		return (EX_USAGE);

	logbuf = (char *)malloc(sizeof(char) * NANDSIM_RAM_LOG_SIZE);
	if (logbuf == NULL) {
		error("Not enough memory to create log buffer");
		return (EX_SOFTWARE);
	}

	memset(logbuf, 0, NANDSIM_RAM_LOG_SIZE);
	log.log = logbuf;
	log.len = NANDSIM_RAM_LOG_SIZE;

	if (ctl == 0xff) {
		start = 0;
		stop = MAX_SIM_DEV-1;
	} else {
		start = ctl;
		stop = ctl;
	}

	if (opendev(&fd) != EX_OK) {
		free(logbuf);
		return (EX_OSFILE);
	}

	/* Print logs for selected controller(s) */
	for (idx = start; idx <= stop; idx++) {
		log.ctrl_num = idx;

		err = ioctl(fd, NANDSIM_PRINT_LOG, &log);
		if (err) {
			error("Could not get log for controller %d!", idx);
			continue;
		}

		printf("Logs for controller#%d:\n%s\n", idx, logbuf);
	}

	free(logbuf);
	close(fd);
	return (EX_OK);
}

static int
cmdstats(int gargc __unused, char **gargv)
{
	int cdevd, chip = 0, ctl = 0, err = 0;
	uint32_t pageno = 0;

	err = parse_devstring(gargv[2], &ctl, &chip);

	if (err)
		return (EX_USAGE);

	if (chip == 0xff) {
		error(MSG_CTRLCHIPNEEDED);
		return (EX_USAGE);
	}

	if (convert_arguint(gargv[3], &pageno) != 0)
		return (EX_USAGE);

	if (!assert_chip_connected(ctl, chip))
		return (EX_SOFTWARE);

	if (opencdev(&cdevd, ctl, chip) != EX_OK)
		return (EX_OSFILE);

	err = printstats(ctl, chip, pageno, cdevd);
	if (err) {
		close(cdevd);
		return (EX_SOFTWARE);
	}
	close(cdevd);
	return (EX_OK);
}

static int
cmddump(int gargc __unused, char **gargv)
{
	struct sim_dump dump;
	struct sim_block_state bs;
	struct chip_param_io cparams;
	int chip = 0, ctl = 0, err = EX_OK, fd, dumpfd;
	uint32_t blkidx, bwritten = 0, totalwritten = 0;
	void *buf;

	err = parse_devstring(gargv[2], &ctl, &chip);
	if (err)
		return (EX_USAGE);

	if (chip == 0xff || ctl == 0xff) {
		error(MSG_CTRLCHIPNEEDED);
		return (EX_USAGE);
	}

	if (!assert_chip_connected(ctl, chip))
		return (EX_SOFTWARE);

	if (opencdev(&fd, ctl, chip) != EX_OK)
		return (EX_OSFILE);

	err = ioctl(fd, NAND_IO_GET_CHIP_PARAM, &cparams);
	if (err) {
		error("Cannot get parameters for chip %d:%d", ctl, chip);
		close(fd);
		return (EX_SOFTWARE);
	}
	close(fd);

	dump.ctrl_num = ctl;
	dump.chip_num = chip;

	dump.len = cparams.pages_per_block * (cparams.page_size +
	    cparams.oob_size);

	buf = malloc(dump.len);
	if (buf == NULL) {
		error("Could not allocate memory!");
		return (EX_SOFTWARE);
	}
	dump.data = buf;

	errno = 0;
	dumpfd = open(gargv[3], O_WRONLY | O_CREAT, 0666);
	if (dumpfd == -1) {
		error("Cannot create dump file.");
		free(buf);
		return (EX_SOFTWARE);
	}

	if (opendev(&fd)) {
		close(dumpfd);
		free(buf);
		return (EX_SOFTWARE);
	}

	bs.ctrl_num = ctl;
	bs.chip_num = chip;

	/* First uint32_t in file shall contain block count */
	if (write(dumpfd, &cparams, sizeof(cparams)) < (int)sizeof(cparams)) {
		error("Error writing to dumpfile!");
		close(fd);
		close(dumpfd);
		free(buf);
		return (EX_SOFTWARE);
	}

	/*
	 * First loop acquires blocks states and writes them to
	 * the dump file.
	 */
	for (blkidx = 0; blkidx < cparams.blocks; blkidx++) {
		bs.block_num = blkidx;
		err = ioctl(fd, NANDSIM_GET_BLOCK_STATE, &bs);
		if (err) {
			error("Could not get bad block(%d) for "
			    "controller (%d)!", blkidx, ctl);
			close(fd);
			close(dumpfd);
			free(buf);
			return (EX_SOFTWARE);
		}

		bwritten = write(dumpfd, &bs, sizeof(bs));
		if (bwritten != sizeof(bs)) {
			error("Error writing to dumpfile");
			close(fd);
			close(dumpfd);
			free(buf);
			return (EX_SOFTWARE);
		}
	}

	/* Second loop dumps the data */
	for (blkidx = 0; blkidx < cparams.blocks; blkidx++) {
		debug("Block#%d...", blkidx);
		dump.block_num = blkidx;

		err = ioctl(fd, NANDSIM_DUMP, &dump);
		if (err) {
			error("Could not dump ctrl#%d chip#%d "
			    "block#%d", ctl, chip, blkidx);
			err = EX_SOFTWARE;
			break;
		}

		bwritten = write(dumpfd, dump.data, dump.len);
		if (bwritten != dump.len) {
			error("Error writing to dumpfile");
			err = EX_SOFTWARE;
			break;
		}
		debug("OK!\n");
		totalwritten += bwritten;
	}
	printf("%d out of %d B written.\n", totalwritten, dump.len * blkidx);

	close(fd);
	close(dumpfd);
	free(buf);
	return (err);
}

static int
cmdrestore(int gargc __unused, char **gargv)
{
	struct sim_dump dump;
	struct sim_block_state bs;
	struct stat filestat;
	int chip = 0, ctl = 0, err = 0, fd, dumpfd = -1;
	uint32_t blkidx, blksz, fsize = 0, expfilesz;
	void *buf;
	struct chip_param_io cparams, dumpcparams;

	err = parse_devstring(gargv[2], &ctl, &chip);
	if (err)
		return (EX_USAGE);
	else if (ctl == 0xff) {
		error(MSG_CTRLCHIPNEEDED);
		return (EX_USAGE);
	}

	if (!assert_chip_connected(ctl, chip))
		return (EX_SOFTWARE);

	/* Get chip geometry */
	if (opencdev(&fd, ctl, chip) != EX_OK)
		return (EX_OSFILE);

	err = ioctl(fd, NAND_IO_GET_CHIP_PARAM, &cparams);
	if (err) {
		error("Cannot get parameters for chip %d:%d", ctl, chip);
		close(fd);
		return (err);
	}
	close(fd);

	/* Obtain dump file size */
	errno = 0;
	if (stat(gargv[3], &filestat) != 0) {
		error("Could not acquire file size! : %s",
		    strerror(errno));
		return (EX_IOERR);
	}

	fsize = filestat.st_size;
	blksz = cparams.pages_per_block * (cparams.page_size +
	    cparams.oob_size);

	/* Expected dump file size for chip */
	expfilesz = cparams.blocks * (blksz + sizeof(bs)) + sizeof(cparams);

	if (fsize != expfilesz) {
		error("File size does not match chip geometry (file size: %d"
		    ", dump size: %d)", fsize, expfilesz);
		return (EX_SOFTWARE);
	}

	dumpfd = open(gargv[3], O_RDONLY);
	if (dumpfd == -1) {
		error("Could not open dump file!");
		return (EX_IOERR);
	}

	/* Read chip params saved in dumpfile */
	read(dumpfd, &dumpcparams, sizeof(dumpcparams));

	/* XXX */
	if (bcmp(&dumpcparams, &cparams, sizeof(cparams)) != 0) {
		error("Supplied dump is created for a chip with different "
		    "chip configuration!");
		close(dumpfd);
		return (EX_SOFTWARE);
	}

	if (opendev(&fd) != EX_OK) {
		close(dumpfd);
		return (EX_OSFILE);
	}

	buf = malloc(blksz);
	if (buf == NULL) {
		error("Could not allocate memory for block buffer");
		close(dumpfd);
		close(fd);
		return (EX_SOFTWARE);
	}

	dump.ctrl_num = ctl;
	dump.chip_num = chip;
	dump.data = buf;
	/* Restore block states and wearouts */
	for (blkidx = 0; blkidx < cparams.blocks; blkidx++) {
		dump.block_num = blkidx;
		if (read(dumpfd, &bs, sizeof(bs)) != sizeof(bs)) {
			error("Error reading dumpfile");
			close(dumpfd);
			close(fd);
			free(buf);
			return (EX_SOFTWARE);
		}
		bs.ctrl_num = ctl;
		bs.chip_num = chip;
		debug("BLKIDX=%d BLOCKS=%d CTRL=%d CHIP=%d STATE=%d\n"
		    "WEAROUT=%d BS.CTRL_NUM=%d BS.CHIP_NUM=%d\n",
		    blkidx, cparams.blocks, dump.ctrl_num, dump.chip_num,
		    bs.state, bs.wearout, bs.ctrl_num, bs.chip_num);

		err = ioctl(fd, NANDSIM_SET_BLOCK_STATE, &bs);
		if (err) {
			error("Could not set bad block(%d) for "
			    "controller: %d, chip: %d!", blkidx, ctl, chip);
			close(dumpfd);
			close(fd);
			free(buf);
			return (EX_SOFTWARE);
		}
	}
	/* Restore data */
	for (blkidx = 0; blkidx < cparams.blocks; blkidx++) {
		errno = 0;
		dump.len = read(dumpfd, buf, blksz);
		if (errno) {
			error("Failed to read block#%d from dumpfile.", blkidx);
			err = EX_SOFTWARE;
			break;
		}
		dump.block_num = blkidx;
		err = ioctl(fd, NANDSIM_RESTORE, &dump);
		if (err) {
			error("Could not restore block#%d of ctrl#%d chip#%d"
			    ": %s", blkidx, ctl, chip, strerror(errno));
			err = EX_SOFTWARE;
			break;
		}
	}

	free(buf);
	close(dumpfd);
	close(fd);
	return (err);

}

static int
cmddestroy(int gargc __unused, char **gargv)
{
	int chip = 0, ctl = 0, err = 0, fd, idx, idx2, state;
	int chipstart, chipstop, ctrlstart, ctrlstop;
	struct sim_chip_destroy chip_destroy;

	err = parse_devstring(gargv[2], &ctl, &chip);

	if (err)
		return (EX_USAGE);

	if (ctl == 0xff) {
		/* Every chip at every controller */
		ctrlstart = chipstart = 0;
		ctrlstop = MAX_SIM_DEV - 1;
		chipstop = MAX_CTRL_CS - 1;
	} else {
		ctrlstart = ctrlstop = ctl;
		if (chip == 0xff) {
			/* Every chip at selected controller */
			chipstart = 0;
			chipstop = MAX_CTRL_CS - 1;
		} else
			/* Selected chip at selected controller */
			chipstart = chipstop = chip;
	}
	debug("CTRLSTART=%d CTRLSTOP=%d CHIPSTART=%d CHIPSTOP=%d\n",
	    ctrlstart, ctrlstop, chipstart, chipstop);
	for (idx = ctrlstart; idx <= ctrlstop; idx++) {
		err = is_ctrl_created(idx, &state);
		if (err) {
			error("Could not acquire ctrl#%d state. Cannot "
			    "destroy controller.", idx);
			return (EX_SOFTWARE);
		}
		if (state == 0) {
			continue;
		}
		err = is_ctrl_running(idx, &state);
		if (err) {
			error(MSG_STATUSACQCTRL, idx);
			return (EX_SOFTWARE);
		}
		if (state != 0) {
			error(MSG_RUNNING, ctl);
			return (EX_SOFTWARE);
		}
		if (opendev(&fd) != EX_OK)
			return (EX_OSFILE);

		for (idx2 = chipstart; idx2 <= chipstop; idx2++) {
			err = is_chip_created(idx, idx2, &state);
			if (err) {
				error(MSG_STATUSACQCTRLCHIP, idx2, idx);
				continue;
			}
			if (state == 0)
				/* There is no such chip running */
				continue;
			chip_destroy.ctrl_num = idx;
			chip_destroy.chip_num = idx2;
			ioctl(fd, NANDSIM_DESTROY_CHIP,
			    &chip_destroy);
		}
		/* If chip isn't explicitly specified -- destroy ctrl */
		if (chip == 0xff) {
			err = ioctl(fd, NANDSIM_DESTROY_CTRL, &idx);
			if (err) {
				error("Could not destroy ctrl#%d", idx);
				continue;
			}
		}
		close(fd);
	}
	return (err);
}

int
main(int argc, char **argv)
{
	struct nandsim_command *cmdopts;
	int retcode = 0;

	if (argc < 2) {
		cmdhelp(argc, argv);
		retcode = EX_USAGE;
	} else {
		cmdopts = getcommand(argv[1]);
		if (cmdopts != NULL && cmdopts->commandfunc != NULL) {
			if (checkusage(argc, cmdopts->req_argc, argv) == 1) {
				/* Print command specific usage */
				printf("nandsim %s", cmdopts->usagestring);
				return (EX_USAGE);
			}
			retcode = cmdopts->commandfunc(argc, argv);

			if (retcode == EX_USAGE) {
				/* Print command-specific usage */
				printf("nandsim %s", cmdopts->usagestring);
			} else if (retcode == EX_OSFILE) {
				error("Could not open device file");
			}

		} else {
			error("Unknown command!");
			retcode = EX_USAGE;
		}
	}
	return (retcode);
}

static int
cmdhelp(int gargc __unused, char **gargv __unused)
{
	struct nandsim_command *opts;

	printf("usage:  nandsim <command> [command params] [params]\n\n");

	for (opts = commands; (opts != NULL) &&
	    (opts->cmd_name != NULL); opts++)
		printf("nandsim %s", opts->usagestring);

	printf("\n");
	return (EX_OK);
}

static void
printchip(struct sim_chip *chip, uint8_t verbose)
{

	if (chip->created == 0)
		return;
	if (verbose > 0) {
		printf("\n[Chip info]\n");
		printf("num= %d\nctrl_num=%d\ndevice_id=%02x"
		    "\tmanufacturer_id=%02x\ndevice_model=%s\nmanufacturer="
		    "%s\ncol_addr_cycles=%d\nrow_addr_cycles=%d"
		    "\npage_size=%d\noob_size=%d\npages_per_block=%d\n"
		    "blocks_per_lun=%d\nluns=%d\n\nprog_time=%d\n"
		    "erase_time=%d\nread_time=%d\n"
		    "error_ratio=%d\nwear_level=%d\nwrite_protect=%c\n"
		    "chip_width=%db\n", chip->num, chip->ctrl_num,
		    chip->device_id, chip->manufact_id,chip->device_model,
		    chip->manufacturer, chip->col_addr_cycles,
		    chip->row_addr_cycles, chip->page_size,
		    chip->oob_size, chip->pgs_per_blk, chip->blks_per_lun,
		    chip->luns,chip->prog_time, chip->erase_time,
		    chip->read_time, chip->error_ratio, chip->wear_level,
		    (chip->is_wp == 0) ? 'N':'Y', chip->width);
	} else {
		printf("[Chip info]\n");
		printf("\tnum=%d\n\tdevice_model=%s\n\tmanufacturer=%s\n"
		    "\tpage_size=%d\n\twrite_protect=%s\n",
		    chip->num, chip->device_model, chip->manufacturer,
		    chip->page_size, (chip->is_wp == 0) ? "NO":"YES");
	}
}

static void
printctrl(struct sim_ctrl *ctrl)
{
	int i;

	if (ctrl->created == 0) {
		printf(MSG_NOCTRL "\n", ctrl->num);
		return;
	}
	printf("\n[Controller info]\n");
	printf("\trunning: %s\n", ctrl->running ? "yes" : "no");
	printf("\tnum cs: %d\n", ctrl->num_cs);
	printf("\tecc: %d\n", ctrl->ecc);
	printf("\tlog_filename: %s\n", ctrl->filename);
	printf("\tecc_layout:");
	for (i = 0; i < MAX_ECC_BYTES; i++) {
		if (ctrl->ecc_layout[i] == 0xffff)
			break;
		else
			printf("%c%d", i%16 ? ' ' : '\n',
			    ctrl->ecc_layout[i]);
	}
	printf("\n");
}

static int
is_ctrl_running(int ctrl_no, int *running)
{
	struct sim_ctrl ctrl;
	int err, fd;

	ctrl.num = ctrl_no;
	if (opendev(&fd) != EX_OK)
		return (EX_OSFILE);

	err = ioctl(fd, NANDSIM_STATUS_CTRL, &ctrl);
	if (err) {
		error(MSG_STATUSACQCTRL, ctrl_no);
		close(fd);
		return (err);
	}
	*running = ctrl.running;
	close(fd);
	return (0);
}

static int
is_ctrl_created(int ctrl_no, int *created)
{
	struct sim_ctrl ctrl;
	int err, fd;

	ctrl.num = ctrl_no;

	if (opendev(&fd) != EX_OK)
		return (EX_OSFILE);

	err = ioctl(fd, NANDSIM_STATUS_CTRL, &ctrl);
	if (err) {
		error("Could not acquire conf for ctrl#%d", ctrl_no);
		close(fd);
		return (err);
	}
	*created = ctrl.created;
	close(fd);
	return (0);
}

static int
is_chip_created(int ctrl_no, int chip_no, int *created)
{
	struct sim_chip chip;
	int err, fd;

	chip.ctrl_num = ctrl_no;
	chip.num = chip_no;

	if (opendev(&fd) != EX_OK)
		return (EX_OSFILE);

	err = ioctl(fd, NANDSIM_STATUS_CHIP, &chip);
	if (err) {
		error("Could not acquire conf for chip#%d", chip_no);
		close(fd);
		return (err);
	}
	*created = chip.created;
	close(fd);
	return (0);
}

static int
assert_chip_connected(int ctrl_no, int chip_no)
{
	int created, running;

	if (is_ctrl_created(ctrl_no, &created))
		return (0);

	if (!created) {
		error(MSG_NOCTRL, ctrl_no);
		return (0);
	}

	if (is_chip_created(ctrl_no, chip_no, &created))
		return (0);

	if (!created) {
		error(MSG_NOTCONFIGDCTRLCHIP, ctrl_no, chip_no);
		return (0);
	}

	if (is_ctrl_running(ctrl_no, &running))
		return (0);

	if (!running) {
		error(MSG_NOTRUNNING, ctrl_no);
		return (0);
	}

	return (1);
}

static int
printstats(int ctrlno, int chipno, uint32_t pageno, int cdevd)
{
	struct page_stat_io pstats;
	struct block_stat_io bstats;
	struct chip_param_io cparams;
	uint32_t blkidx;
	int err;

	/* Gather information about chip */
	err = ioctl(cdevd, NAND_IO_GET_CHIP_PARAM, &cparams);

	if (err) {
		error("Could not acquire chip info for chip attached to cs#"
		    "%d, ctrl#%d", chipno, ctrlno);
		return (EX_SOFTWARE);
	}

	blkidx = (pageno / cparams.pages_per_block);
	bstats.block_num = blkidx;

	err = ioctl(cdevd, NAND_IO_BLOCK_STAT, &bstats);
	if (err) {
		error("Could not acquire block#%d statistics!", blkidx);
		return (ENXIO);
	}

	printf("Block #%d erased: %d\n", blkidx, bstats.block_erased);
	pstats.page_num = pageno;

	err = ioctl(cdevd, NAND_IO_PAGE_STAT, &pstats);
	if (err) {
		error("Could not acquire page statistics!");
		return (ENXIO);
	}

	debug("BLOCKIDX = %d PAGENO (REL. TO BLK) = %d\n", blkidx,
	    pstats.page_num);

	printf("Page#%d : reads:%d writes:%d \n\traw reads:%d raw writes:%d "
	    "\n\tecc_succeeded:%d ecc_corrected:%d ecc_failed:%d\n",
	    pstats.page_num, pstats.page_read, pstats.page_written,
	    pstats.page_raw_read, pstats.page_raw_written,
	    pstats.ecc_succeded, pstats.ecc_corrected, pstats.ecc_failed);
	return (0);
}
