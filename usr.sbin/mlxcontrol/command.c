/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Michael Smith
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
 *
 *	$FreeBSD$
 */

#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

#include <dev/mlx/mlxio.h>
#include <dev/mlx/mlxreg.h>

#include "mlxcontrol.h"

static int	cmd_status(int argc, char *argv[]);
static int	cmd_rescan(int argc, char *argv[]);
static int	cmd_detach(int argc, char *argv[]);
static int	cmd_check(int argc, char *argv[]);
static int	cmd_rebuild(int argc, char *argv[]);
#ifdef SUPPORT_PAUSE
static int	cmd_pause(int argc, char *argv[]);
#endif
static int	cmd_help(int argc, char *argv[]);

extern int	cmd_config(int argc, char *argv[]);


struct 
{
    char	*cmd;
    int		(*func)(int argc, char *argv[]);
    char	*desc;
    char	*text;
} commands[] = {
    {"status",	cmd_status, 
     "displays device status",
     "  status [-qv] [<drive>...]\n"
     "      Display status for <drive> or all drives if none is listed\n"
     "  -q    Suppress output.\n"
     "  -v    Display verbose information.\n"
     "  Returns 0 if all drives tested are online, 1 if one or more are\n"
     "  critical, and 2 if one or more are offline."},
    {"rescan",	cmd_rescan, 
     "scan for new system drives",
     "  rescan <controller> [<controller>...]\n"
     "      Rescan <controller> for system drives.\n"
     "  rescan -a\n"
     "      Rescan all controllers for system drives."},
    {"detach",	cmd_detach,
     "detach system drives",
     "  detach <drive> [<drive>...]\n"
     "      Detaches <drive> from the controller.\n"
     "  detach -a <controller>\n"
     "      Detaches all drives on <controller>."},
    {"check",	cmd_check,
     "consistency-check a system drive",
     "  check <drive>\n"
     "      Requests a check and rebuild of the parity information on <drive>.\n"
     "      Note that each controller can only check one system drive at a time."},
    {"rebuild",	cmd_rebuild,
     "initiate a rebuild of a dead physical drive",
     "  rebuild <controller> <physdrive>\n"
     "      All system drives using space on the physical drive <physdrive>\n"
     "      are rebuilt, reconstructing all data on the drive.\n"
     "      Note that each controller can only perform one rebuild at a time."},
#ifdef SUPPORT_PAUSE
    {"pause",	cmd_pause,
     "pauses controller channels",
     "  pause [-t <howlong>] [-d <delay>] <controller> [<channel>...]\n"
     "      Pauses SCSI I/O on <channel> and <controller>.  If no channel is specified,\n"
     "      all channels are paused.\n"
     "  <howlong>   How long (seconds) to pause for (default 30).\n"
     "  <delay>     How long (seconds) to wait before pausing (default 30).\n"
     "  pause <controller> -c\n"
     "      Cancels any pending pause operation on <controller>."},
#endif
    {"config",	cmd_config,
     "examine and update controller configuration",
     "  config <controller>\n"
     "      Print configuration for <controller>."},
    {"help",	cmd_help,   
     "give help on usage",
     ""},
    {NULL, NULL, NULL, NULL}
};

/********************************************************************************
 * Command dispatch and global options parsing.
 */

int
main(int argc, char *argv[])
{
    int		ch, i, oargc;
    char	**oargv;
    
    oargc = argc;
    oargv = argv;
    while ((ch = getopt(argc, argv, "")) != -1)
	switch(ch) {
	default:
	    return(cmd_help(0, NULL));
	}

    argc -= optind;
    argv += optind;
    
    if (argc > 0)
	for (i = 0; commands[i].cmd != NULL; i++)
	    if (!strcmp(argv[0], commands[i].cmd))
		return(commands[i].func(argc, argv));

    return(cmd_help(oargc, oargv));
}

/********************************************************************************
 * Helptext output
 */
static int
cmd_help(int argc, char *argv[]) 
{
    int		i;
    
    if (argc > 1)
	for (i = 0; commands[i].cmd != NULL; i++)
	    if (!strcmp(argv[1], commands[i].cmd)) {
		fprintf(stderr, "%s\n", commands[i].text);
		fflush(stderr);
		return(0);
	    }

    if (argv != NULL)
	fprintf(stderr, "Unknown command '%s'.\n", argv[1]);    
    fprintf(stderr, "Valid commands are:\n");
    for (i = 0; commands[i].cmd != NULL; i++)
	fprintf(stderr, "  %-20s %s\n", commands[i].cmd, commands[i].desc);
    fflush(stderr);
    return(0);
}

/********************************************************************************
 * Status output
 *
 * status [-qv] [<device> ...]
 *		Prints status for <device>, or all if none listed.
 *
 * -q	Suppresses output, command returns 0 if devices are OK, 1 if one or
 *	more devices are critical, 2 if one or more devices are offline.
 */
static struct mlx_rebuild_status	rs;
static int				rs_ctrlr = -1;
static int				status_result = 0;

/* XXX more verbosity! */
static void
status_print(int unit, void *arg)
{
    int				verbosity = *(int *)arg;
    int				fd, result, ctrlr, sysdrive, statvalid;
    
    /* Find which controller and what system drive we are */
    statvalid = 0;
    if (mlxd_find_ctrlr(unit, &ctrlr, &sysdrive)) {
	warnx("couldn't get controller/drive for %s", drivepath(unit));
    } else {
	/* If we don't have rebuild stats for this controller, get them */
	if (rs_ctrlr == ctrlr) {
	    statvalid = 1;
	} else {
	    if ((fd = open(ctrlrpath(ctrlr), 0)) < 0) {
		warn("can't open %s", ctrlrpath(ctrlr));
	    } else {
		if (ioctl(fd, MLX_REBUILDSTAT, &rs) < 0) {
		    warn("ioctl MLX_REBUILDSTAT");
		} else {
		    rs_ctrlr = ctrlr;
		    statvalid = 1;
		}
		close(fd);
	    }
	}
    }

    /* Get the device */
    if ((fd = open(drivepath(unit), 0)) < 0) {
	warn("can't open %s", drivepath(unit));
	return;
    }

    /* Get its status */
    if (ioctl(fd, MLXD_STATUS, &result) < 0) {
	warn("ioctl MLXD_STATUS");
    } else {
	switch(result) {
	case MLX_SYSD_ONLINE:
	    if (verbosity > 0)
		printf("%s: online", drivename(unit));
	    break;
	case MLX_SYSD_CRITICAL:
	    if (verbosity > 0)
		printf("%s: critical", drivename(unit));
	    if (status_result < 1)
		status_result = 1;
	    break;
	case MLX_SYSD_OFFLINE:
	    if (verbosity > 0)
		printf("%s: offline", drivename(unit));
	    if (status_result < 2)
		status_result = 2;
	    break;
	default:
	    if (verbosity > 0) {
		printf("%s: unknown status 0x%x", drivename(unit), result);
	    }
	}
	if (verbosity > 0) {
	    /* rebuild/check in progress on this drive? */
	    if (statvalid && (rs_ctrlr == ctrlr) && 
		(rs.rs_drive == sysdrive) && (rs.rs_code != MLX_REBUILDSTAT_IDLE)) {
		switch(rs.rs_code) {
		case MLX_REBUILDSTAT_REBUILDCHECK:
		    printf(" [consistency check");
		    break;
		case MLX_REBUILDSTAT_ADDCAPACITY:
		    printf(" [add capacity");
		    break;
		case MLX_REBUILDSTAT_ADDCAPACITYINIT:
		    printf(" [add capacity init");
		    break;
		default:
		    printf(" [unknown operation");
		}
		printf(": %d/%d, %d%% complete]",
		       rs.rs_remaining, rs.rs_size, 
		       ((rs.rs_size - rs.rs_remaining) / (rs.rs_size / 100)));
	    }
	    printf("\n");
	}
    }
    close(fd);
}

static struct 
{
    int		hwid;
    char	*name;
} mlx_controller_names[] = {
    {0x01,	"960P/PD"},
    {0x02,	"960PL"},
    {0x10,	"960PG"},
    {0x11,	"960PJ"},
    {0x12,	"960PR"},
    {0x13,	"960PT"},
    {0x14,	"960PTL0"},
    {0x15,	"960PRL"},
    {0x16,	"960PTL1"},
    {0x20,	"1100PVX"},
    {-1, NULL}
};

static void
controller_print(int unit, void *arg)
{
    struct mlx_enquiry2	enq;
    struct mlx_phys_drv	pd;
    int			verbosity = *(int *)arg;
    static char		buf[80];
    char		*model;
    int			i, channel, target;

    if (verbosity == 0)
	return;

    /* fetch and print controller data */
    if (mlx_enquiry(unit, &enq)) {
	printf("mlx%d: error submitting ENQUIRY2\n", unit);
    } else {
	
	for (i = 0, model = NULL; mlx_controller_names[i].name != NULL; i++) {
	    if ((enq.me_hardware_id & 0xff) == mlx_controller_names[i].hwid) {
		model = mlx_controller_names[i].name;
		break;
	    }
	}
	if (model == NULL) {
	    sprintf(buf, " model 0x%x", enq.me_hardware_id & 0xff);
	    model = buf;
	}

	printf("mlx%d: DAC%s, %d channel%s, firmware %d.%02d-%c-%02d, %dMB RAM\n",
	       unit, model, 
	       enq.me_actual_channels, 
	       enq.me_actual_channels > 1 ? "s" : "",
	       enq.me_firmware_id & 0xff,
	       (enq.me_firmware_id >> 8) & 0xff,
	       (enq.me_firmware_id >> 16),
	       (enq.me_firmware_id >> 24) & 0xff,
	       enq.me_mem_size / (1024 * 1024));

	if (verbosity > 1) {
	    printf("  Hardware ID                 0x%08x\n", enq.me_hardware_id);
	    printf("  Firmware ID                 0x%08x\n", enq.me_firmware_id);
	    printf("  Configured/Actual channels  %d/%d\n", enq.me_configured_channels,
		      enq.me_actual_channels);
	    printf("  Max Targets                 %d\n", enq.me_max_targets);
	    printf("  Max Tags                    %d\n", enq.me_max_tags);
	    printf("  Max System Drives           %d\n", enq.me_max_sys_drives);
	    printf("  Max Arms                    %d\n", enq.me_max_arms);
	    printf("  Max Spans                   %d\n", enq.me_max_spans);
	    printf("  DRAM/cache/flash/NVRAM size %d/%d/%d/%d\n", enq.me_mem_size,
		      enq.me_cache_size, enq.me_flash_size, enq.me_nvram_size);
	    printf("  DRAM type                   %d\n", enq.me_mem_type);
	    printf("  Clock Speed                 %dns\n", enq.me_clock_speed);
	    printf("  Hardware Speed              %dns\n", enq.me_hardware_speed);
	    printf("  Max Commands                %d\n", enq.me_max_commands);
	    printf("  Max SG Entries              %d\n", enq.me_max_sg);
	    printf("  Max DP                      %d\n", enq.me_max_dp);
	    printf("  Max IOD                     %d\n", enq.me_max_iod);
	    printf("  Max Comb                    %d\n", enq.me_max_comb);
	    printf("  Latency                     %ds\n", enq.me_latency);
	    printf("  SCSI Timeout                %ds\n", enq.me_scsi_timeout);
	    printf("  Min Free Lines              %d\n", enq.me_min_freelines);
	    printf("  Rate Constant               %d\n", enq.me_rate_const);
	    printf("  MAXBLK                      %d\n", enq.me_maxblk);
	    printf("  Blocking Factor             %d sectors\n", enq.me_blocking_factor);
	    printf("  Cache Line Size             %d blocks\n", enq.me_cacheline);
	    printf("  SCSI Capability             %s%dMHz, %d bit\n", 
		      enq.me_scsi_cap & (1<<4) ? "differential " : "",
		      (1 << ((enq.me_scsi_cap >> 2) & 3)) * 10,
		      8 << (enq.me_scsi_cap & 0x3));
	    printf("  Firmware Build Number       %d\n", enq.me_firmware_build);
	    printf("  Fault Management Type       %d\n", enq.me_fault_mgmt_type);
#if 0
	    printf("  Features                    %b\n", enq.me_firmware_features,
		      "\20\4Background Init\3Read Ahead\2MORE\1Cluster\n");
#endif
	}

	/* fetch and print physical drive data */
	for (channel = 0; channel < enq.me_configured_channels; channel++) {
	    for (target = 0; target < enq.me_max_targets; target++) {
		if ((mlx_get_device_state(unit, channel, target, &pd) == 0) &&
		    (pd.pd_flags1 & MLX_PHYS_DRV_PRESENT)) {
		    mlx_print_phys_drv(&pd, channel, target, "  ", verbosity - 1);
		    if (verbosity > 1) {
			/* XXX print device statistics? */
		    }
		}
	    }
	}
    }
}

static int
cmd_status(int argc, char *argv[])
{
    int		ch, verbosity = 1, i, unit;

    optreset = 1;
    optind = 1;
    while ((ch = getopt(argc, argv, "qv")) != -1)
	switch(ch) {
	case 'q':
	    verbosity = 0;
	    break;
	case 'v':
	    verbosity = 2;
	    break;
	default:
	    return(cmd_help(argc, argv));
	}
    argc -= optind;
    argv += optind;

    if (argc < 1) {
	mlx_foreach(controller_print, &verbosity);
	mlxd_foreach(status_print, &verbosity);
    } else {
	for (i = 0; i < argc; i++) {
	    if ((unit = driveunit(argv[i])) == -1) {
		warnx("'%s' is not a valid drive", argv[i]);
	    } else {
		status_print(unit, &verbosity);
	    }
	}
    }
    return(status_result);
}

/********************************************************************************
 * Recscan for system drives on one or more controllers.
 *
 * rescan <controller> [<controller>...]
 * rescan -a
 */
static void
rescan_ctrlr(int unit, void *junk)
{
    int		fd;
    
    /* Get the device */
    if ((fd = open(ctrlrpath(unit), 0)) < 0) {
	warn("can't open %s", ctrlrpath(unit));
	return;
    }

    if (ioctl(fd, MLX_RESCAN_DRIVES) < 0)
	warn("can't rescan %s", ctrlrname(unit));
    close(fd);
}

static int
cmd_rescan(int argc, char *argv[]) 
{
    int		all = 0, i, ch, unit;

    optreset = 1;
    optind = 1;
    while ((ch = getopt(argc, argv, "a")) != -1)
	switch(ch) {
	case 'a':
	    all = 1;
	    break;
	default:
	    return(cmd_help(argc, argv));
	}
    argc -= optind;
    argv += optind;

    if (all) {
	mlx_foreach(rescan_ctrlr, NULL);
    } else {
	for (i = 0; i < argc; i++) {
	    if ((unit = ctrlrunit(argv[i])) == -1) {
		warnx("'%s' is not a valid controller", argv[i]);
	    } else {
		rescan_ctrlr(unit, NULL);
	    }
	}
    }
    return(0);
}

/********************************************************************************
 * Detach one or more system drives from a controller.
 *
 * detach <drive> [<drive>...]
 *		Detach <drive>.
 *
 * detach -a <controller> [<controller>...]
 *		Detach all drives on <controller>.
 *
 */
static void
detach_drive(int unit, void *arg)
{
    int		fd;
    
    /* Get the device */
    if ((fd = open(ctrlrpath(unit), 0)) < 0) {
	warn("can't open %s", ctrlrpath(unit));
	return;
    }

    if (ioctl(fd, MLX_DETACH_DRIVE, &unit) < 0)
	warn("can't detach %s", drivename(unit));
    close(fd);
}

static int
cmd_detach(int argc, char *argv[]) 
{
    struct mlxd_foreach_action	ma;
    int				all = 0, i, ch, unit;

    optreset = 1;
    optind = 1;
    while ((ch = getopt(argc, argv, "a")) != -1)
	switch(ch) {
	case 'a':
	    all = 1;
	    break;
	default:
	    return(cmd_help(argc, argv));
	}
    argc -= optind;
    argv += optind;

    if (all) {
	ma.func = detach_drive;
	ma.arg = &unit;
	for (i = 0; i < argc; i++) {
	    if ((unit = ctrlrunit(argv[i])) == -1) {
		warnx("'%s' is not a valid controller", argv[i]);
	    } else {
		mlxd_foreach_ctrlr(unit, &ma);
	    }
	}
    } else {
	for (i = 0; i < argc; i++) {
	    if ((unit = driveunit(argv[i])) == -1) {
		warnx("'%s' is not a valid drive", argv[i]);
	    } else {
		/* run across all controllers to find this drive */
		mlx_foreach(detach_drive, &unit);
	    }
	}
    }
    return(0);
}

/********************************************************************************
 * Initiate a consistency check on a system drive.
 *
 * check [<drive>]
 *	Start a check of <drive>
 *
 */
static int
cmd_check(int argc, char *argv[])
{
    int		unit, fd, result;

    if (argc != 2)
	return(cmd_help(argc, argv));

    if ((unit = driveunit(argv[1])) == -1) {
	warnx("'%s' is not a valid drive", argv[1]);
    } else {
	
	/* Get the device */
	if ((fd = open(drivepath(unit), 0)) < 0) {
	    warn("can't open %s", drivepath(unit));
	} else {
	    /* Try to start the check */
	    if ((ioctl(fd, MLXD_CHECKASYNC, &result)) < 0) {
		switch(result) {
		case 0x0002:
		    warnx("one or more of the SCSI disks on which the drive '%s' depends is DEAD", argv[1]);
		    break;
		case 0x0105:
		    warnx("drive %s is invalid, or not a drive which can be checked", argv[1]);
		    break;
		case 0x0106:
		    warnx("drive rebuild or consistency check is already in progress on this controller");
		    break;
		default:
		    warn("ioctl MLXD_CHECKASYNC");
		}
	    }
	}
    }
    return(0);
}

/********************************************************************************
 * Initiate a physical drive rebuild
 *
 * rebuild <controller> <channel>:<target>
 *	Start a rebuild of <controller>:<channel>:<target>
 *
 */
static int
cmd_rebuild(int argc, char *argv[])
{
    struct mlx_rebuild_request	rb;
    int				unit, fd;

    if (argc != 3)
	return(cmd_help(argc, argv));

    /* parse arguments */
    if ((unit = ctrlrunit(argv[1])) == -1) {
	warnx("'%s' is not a valid controller", argv[1]);
	return(1);
    }
    /* try diskXXXX and unknownXXXX as we report the latter for a dead drive ... */
    if ((sscanf(argv[2], "disk%2d%2d", &rb.rr_channel, &rb.rr_target) != 2) &&
	(sscanf(argv[2], "unknown%2d%2d", &rb.rr_channel, &rb.rr_target) != 2)) {	
	warnx("'%s' is not a valid physical drive", argv[2]);
	return(1);
    }
    /* get the device */
    if ((fd = open(ctrlrpath(unit), 0)) < 0) {
	warn("can't open %s", ctrlrpath(unit));
	return(1);
    }
    /* try to start the rebuild */
    if ((ioctl(fd, MLX_REBUILDASYNC, &rb)) < 0) {
	switch(rb.rr_status) {
	case 0x0002:
	    warnx("the drive at %d:%d is already ONLINE", rb.rr_channel, rb.rr_target);
	    break;
	case 0x0004:
	    warnx("drive failed during rebuild");
	    break;
	case 0x0105:
	    warnx("there is no drive at channel %d, target %d", rb.rr_channel, rb.rr_target);
	    break;
	case 0x0106:
	    warnx("drive rebuild or consistency check is already in progress on this controller");
	    break;
	default:
	    warn("ioctl MLXD_REBUILDASYNC");
	}
    }
    return(0);
}

#ifdef SUPPORT_PAUSE
/********************************************************************************
 * Pause one or more channels on a controller
 *
 * pause [-d <delay>] [-t <time>] <controller> [<channel>...]
 *		Pauses <channel> (or all channels) for <time> seconds after a
 *		delay of <delay> seconds.
 * pause <controller> -c
 *		Cancels pending pause
 */
static int
cmd_pause(int argc, char *argv[]) 
{
    struct mlx_pause	mp;
    int			unit, i, ch, fd, cancel = 0;
    char		*cp;
    int			oargc = argc;
    char		**oargv = argv;

    mp.mp_which = 0;
    mp.mp_when = 30;
    mp.mp_howlong = 30;
    optreset = 1;
    optind = 1;
    while ((ch = getopt(argc, argv, "cd:t:")) != -1)
	switch(ch) {
	case 'c':
	    cancel = 1;
	    break;
	case 'd':
	    mp.mp_when = strtol(optarg, &cp, 0);
	    if (*cp != 0)
		return(cmd_help(argc, argv));
	    break;
	case 't':
	    mp.mp_howlong = strtol(optarg, &cp, 0);
	    if (*cp != 0)
		return(cmd_help(argc, argv));
	    break;
	default:
	    return(cmd_help(argc, argv));
	}
    argc -= optind;
    argv += optind;

    /* get controller unit number that we're working on */
    if ((argc < 1) || ((unit = ctrlrunit(argv[0])) == -1))
	return(cmd_help(oargc, oargv));

    /* Get the device */
    if ((fd = open(ctrlrpath(unit), 0)) < 0) {
	warn("can't open %s", ctrlrpath(unit));
	return(1);
    }

    if (argc == 1) {
	/* controller-wide pause/cancel */
	mp.mp_which = cancel ? MLX_PAUSE_CANCEL : MLX_PAUSE_ALL;
    } else {
	for (i = 1; i < argc; i++) {
	    ch = strtol(argv[i], &cp, 0);
	    if (*cp != 0) {
		warnx("bad channel number '%s'", argv[i]);
		continue;
	    } else {
		mp.mp_which |= (1 << ch);
	    }
	}
    }
    if ((ioctl(fd, MLX_PAUSE_CHANNEL, &mp)) < 0)
	warn("couldn't %s %s", cancel ? "cancel pause on" : "pause", ctrlrname(unit));
    close(fd);
    return(0);
}
#endif	/* SUPPORT_PAUSE */

