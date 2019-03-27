/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997, 1998, 2000, 2001  Kenneth D. Merry
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $FreeBSD$
 */
/*
 * Parts of this program are derived from the original FreeBSD iostat
 * program:
 */
/*-
 * Copyright (c) 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Ideas for the new iostat statistics output modes taken from the NetBSD
 * version of iostat:
 */
/*
 * Copyright (c) 1996 John M. Vinopal
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project
 *      by John M. Vinopal.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#include <sys/param.h>
#include <sys/errno.h>
#include <sys/resource.h>
#include <sys/sysctl.h>

#include <ctype.h>
#include <devstat.h>
#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <kvm.h>
#include <limits.h>
#include <math.h>
#include <nlist.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static struct nlist namelist[] = {
#define X_TTY_NIN	0
	{ .n_name = "_tty_nin",
	  .n_type = 0, .n_other = 0, .n_desc = 0, .n_value = 0 },
#define X_TTY_NOUT	1
	{ .n_name = "_tty_nout",
	  .n_type = 0, .n_other = 0, .n_desc = 0, .n_value = 0 },
#define X_BOOTTIME	2
	{ .n_name = "_boottime",
	  .n_type = 0, .n_other = 0, .n_desc = 0, .n_value = 0 },
#define X_END		2
	{ .n_name = NULL,
	  .n_type = 0, .n_other = 0, .n_desc = 0, .n_value = 0 },
};

#define	IOSTAT_DEFAULT_ROWS	20	/* Traditional default `wrows' */

static struct statinfo cur, last;
static int num_devices;
static struct device_selection *dev_select;
static int maxshowdevs;
static volatile sig_atomic_t headercount;
static volatile sig_atomic_t wresized;	/* Tty resized, when non-zero. */
static volatile sig_atomic_t alarm_rang;
static volatile sig_atomic_t return_requested;
static unsigned short wrows;		/* Current number of tty rows. */
static int dflag = 0, Iflag = 0, Cflag = 0, Tflag = 0, oflag = 0, Kflag = 0;
static int xflag = 0, zflag = 0;

/* local function declarations */
static void usage(void);
static void needhdr(int signo);
static void needresize(int signo);
static void needreturn(int signo);
static void alarm_clock(int signo);
static void doresize(void);
static void phdr(void);
static void devstats(int perf_select, long double etime, int havelast);
static void cpustats(void);
static int readvar(kvm_t *kd, const char *name, int nlid, void *ptr,
		   size_t len);

static void
usage(void)
{
	/*
	 * We also support the following 'traditional' syntax:
	 * iostat [drives] [wait [count]]
	 * This isn't mentioned in the man page, or the usage statement,
	 * but it is supported.
	 */
	fprintf(stderr, "usage: iostat [-CdhIKoTxz?] [-c count] [-M core]"
		" [-n devs] [-N system]\n"
		"\t      [-t type,if,pass] [-w wait] [drives]\n");
}

int
main(int argc, char **argv)
{
	int c, i;
	int tflag = 0, hflag = 0, cflag = 0, wflag = 0, nflag = 0;
	int count = 0, waittime = 0;
	char *memf = NULL, *nlistf = NULL;
	struct devstat_match *matches;
	struct itimerval alarmspec;
	int num_matches = 0;
	char errbuf[_POSIX2_LINE_MAX];
	kvm_t *kd = NULL;
	long generation;
	int num_devices_specified;
	int num_selected, num_selections;
	long select_generation;
	char **specified_devices;
	devstat_select_mode select_mode;
	float f;
	int havelast = 0;

	matches = NULL;
	maxshowdevs = 3;

	while ((c = getopt(argc, argv, "c:CdhIKM:n:N:ot:Tw:xz?")) != -1) {
		switch(c) {
			case 'c':
				cflag++;
				count = atoi(optarg);
				if (count < 1)
					errx(1, "count %d is < 1", count);
				break;
			case 'C':
				Cflag++;
				break;
			case 'd':
				dflag++;
				break;
			case 'h':
				hflag++;
				break;
			case 'I':
				Iflag++;
				break;
			case 'K':
				Kflag++;
				break;
			case 'M':
				memf = optarg;
				break;
			case 'n':
				nflag++;
				maxshowdevs = atoi(optarg);
				if (maxshowdevs < 0)
					errx(1, "number of devices %d is < 0",
					     maxshowdevs);
				break;
			case 'N':
				nlistf = optarg;
				break;
			case 'o':
				oflag++;
				break;
			case 't':
				tflag++;
				if (devstat_buildmatch(optarg, &matches,
						       &num_matches) != 0)
					errx(1, "%s", devstat_errbuf);
				break;
			case 'T':
				Tflag++;
				break;
			case 'w':
				wflag++;
				f = atof(optarg);
				waittime = f * 1000;
				if (waittime < 1)
					errx(1, "wait time is < 1ms");
				break;
			case 'x':
				xflag++;
				break;
			case 'z':
				zflag++;
				break;
			default:
				usage();
				exit(1);
				break;
		}
	}

	argc -= optind;
	argv += optind;

	if (nlistf != NULL || memf != NULL) {
		kd = kvm_openfiles(nlistf, memf, NULL, O_RDONLY, errbuf);

		if (kd == NULL)
			errx(1, "kvm_openfiles: %s", errbuf);

		if (kvm_nlist(kd, namelist) == -1)
			errx(1, "kvm_nlist: %s", kvm_geterr(kd));
	}

	/*
	 * Make sure that the userland devstat version matches the kernel
	 * devstat version.  If not, exit and print a message informing
	 * the user of his mistake.
	 */
	if (devstat_checkversion(kd) < 0)
		errx(1, "%s", devstat_errbuf);

	/*
	 * Make sure Tflag and/or Cflag are set if dflag == 0.  If dflag is
	 * greater than 0, they may be 0 or non-zero.
	 */
	if (dflag == 0 && xflag == 0) {
		Cflag = 1;
		Tflag = 1;
	}

	/* find out how many devices we have */
	if ((num_devices = devstat_getnumdevs(kd)) < 0)
		err(1, "can't get number of devices");

	/*
	 * Figure out how many devices we should display.
	 */
	if (nflag == 0) {
		if (xflag > 0)
			maxshowdevs = num_devices;
		else if (oflag > 0) {
			if ((dflag > 0) && (Cflag == 0) && (Tflag == 0))
				maxshowdevs = 5;
			else if ((dflag > 0) && (Tflag > 0) && (Cflag == 0))
				maxshowdevs = 5;
			else
				maxshowdevs = 4;
		} else {
			if ((dflag > 0) && (Cflag == 0))
				maxshowdevs = 4;
			else
				maxshowdevs = 3;
		}
	}

	cur.dinfo = (struct devinfo *)calloc(1, sizeof(struct devinfo));
	if (cur.dinfo == NULL)
		err(1, "calloc failed");

	last.dinfo = (struct devinfo *)calloc(1, sizeof(struct devinfo));
	if (last.dinfo == NULL)
		err(1, "calloc failed");

	/*
	 * Grab all the devices.  We don't look to see if the list has
	 * changed here, since it almost certainly has.  We only look for
	 * errors.
	 */
	if (devstat_getdevs(kd, &cur) == -1)
		errx(1, "%s", devstat_errbuf);

	num_devices = cur.dinfo->numdevs;
	generation = cur.dinfo->generation;

	/*
	 * If the user specified any devices on the command line, see if
	 * they are in the list of devices we have now.
	 */
	specified_devices = (char **)malloc(sizeof(char *));
	if (specified_devices == NULL)
		err(1, "malloc failed");

	for (num_devices_specified = 0; *argv; ++argv) {
		if (isdigit(**argv))
			break;
		num_devices_specified++;
		specified_devices = (char **)realloc(specified_devices,
						     sizeof(char *) *
						     num_devices_specified);
		if (specified_devices == NULL)
			err(1, "realloc failed");

		specified_devices[num_devices_specified - 1] = *argv;

	}
	if (nflag == 0 && maxshowdevs < num_devices_specified)
		maxshowdevs = num_devices_specified;

	dev_select = NULL;

	if ((num_devices_specified == 0) && (num_matches == 0))
		select_mode = DS_SELECT_ADD;
	else
		select_mode = DS_SELECT_ONLY;

	/*
	 * At this point, selectdevs will almost surely indicate that the
	 * device list has changed, so we don't look for return values of 0
	 * or 1.  If we get back -1, though, there is an error.
	 */
	if (devstat_selectdevs(&dev_select, &num_selected,
			       &num_selections, &select_generation, generation,
			       cur.dinfo->devices, num_devices, matches,
			       num_matches, specified_devices,
			       num_devices_specified, select_mode, maxshowdevs,
			       hflag) == -1)
		errx(1, "%s", devstat_errbuf);

	/*
	 * Look for the traditional wait time and count arguments.
	 */
	if (*argv) {
		f = atof(*argv);
		waittime = f * 1000;

		/* Let the user know he goofed, but keep going anyway */
		if (wflag != 0)
			warnx("discarding previous wait interval, using"
			      " %g instead", waittime / 1000.0);
		wflag++;

		if (*++argv) {
			count = atoi(*argv);
			if (cflag != 0)
				warnx("discarding previous count, using %d"
				      " instead", count);
			cflag++;
		} else
			count = -1;
	}

	/*
	 * If the user specified a count, but not an interval, we default
	 * to an interval of 1 second.
	 */
	if ((wflag == 0) && (cflag > 0))
		waittime = 1 * 1000;

	/*
	 * If the user specified a wait time, but not a count, we want to
	 * go on ad infinitum.  This can be redundant if the user uses the
	 * traditional method of specifying the wait, since in that case we
	 * already set count = -1 above.  Oh well.
	 */
	if ((wflag > 0) && (cflag == 0))
		count = -1;

	bzero(cur.cp_time, sizeof(cur.cp_time));
	cur.tk_nout = 0;
	cur.tk_nin = 0;

	/*
	 * Set the snap time to the system boot time (ie: zero), so the
	 * stats are calculated since system boot.
	 */
	cur.snap_time = 0;

	/*
	 * If the user stops the program (control-Z) and then resumes it,
	 * print out the header again.
	 */
	(void)signal(SIGCONT, needhdr);

	/*
	 * If our standard output is a tty, then install a SIGWINCH handler
	 * and set wresized so that our first iteration through the main
	 * iostat loop will peek at the terminal's current rows to find out
	 * how many lines can fit in a screenful of output.
	 */
	if (isatty(fileno(stdout)) != 0) {
		wresized = 1;
		(void)signal(SIGWINCH, needresize);
	} else {
		wresized = 0;
		wrows = IOSTAT_DEFAULT_ROWS;
	}

	/*
	 * Register a SIGINT handler so that we can print out final statistics
	 * when we get that signal
	 */
	(void)signal(SIGINT, needreturn);

	/*
	 * Register a SIGALRM handler to implement sleeps if the user uses the
	 * -c or -w options
	 */
	(void)signal(SIGALRM, alarm_clock);
	alarmspec.it_interval.tv_sec = waittime / 1000;
	alarmspec.it_interval.tv_usec = 1000 * (waittime % 1000);
	alarmspec.it_value.tv_sec = waittime / 1000;
	alarmspec.it_value.tv_usec = 1000 * (waittime % 1000);
	setitimer(ITIMER_REAL, &alarmspec, NULL);

	for (headercount = 1;;) {
		struct devinfo *tmp_dinfo;
		long tmp;
		long double etime;
		sigset_t sigmask, oldsigmask;

		if (Tflag > 0) {
			if ((readvar(kd, "kern.tty_nin", X_TTY_NIN, &cur.tk_nin,
			     sizeof(cur.tk_nin)) != 0)
			 || (readvar(kd, "kern.tty_nout", X_TTY_NOUT,
			     &cur.tk_nout, sizeof(cur.tk_nout))!= 0)) {
				Tflag = 0;
				warnx("disabling TTY statistics");
			}
		 }

		if (Cflag > 0) {
			if (kd == NULL) {
				if (readvar(kd, "kern.cp_time", 0,
				    &cur.cp_time, sizeof(cur.cp_time)) != 0)
					Cflag = 0;
			} else {
				if (kvm_getcptime(kd, cur.cp_time) < 0) {
					warnx("kvm_getcptime: %s",
					    kvm_geterr(kd));
					Cflag = 0;
				}
			}
			if (Cflag == 0)
				warnx("disabling CPU time statistics");
		}

		if (!--headercount) {
			phdr();
			if (wresized != 0)
				doresize();
			headercount = wrows;
		}

		tmp_dinfo = last.dinfo;
		last.dinfo = cur.dinfo;
		cur.dinfo = tmp_dinfo;

		last.snap_time = cur.snap_time;

		/*
		 * Here what we want to do is refresh our device stats.
		 * devstat_getdevs() returns 1 when the device list has changed.
		 * If the device list has changed, we want to go through
		 * the selection process again, in case a device that we
		 * were previously displaying has gone away.
		 */
		switch (devstat_getdevs(kd, &cur)) {
		case -1:
			errx(1, "%s", devstat_errbuf);
			break;
		case 1: {
			int retval;

			num_devices = cur.dinfo->numdevs;
			generation = cur.dinfo->generation;
			retval = devstat_selectdevs(&dev_select, &num_selected,
						    &num_selections,
						    &select_generation,
						    generation,
						    cur.dinfo->devices,
						    num_devices, matches,
						    num_matches,
						    specified_devices,
						    num_devices_specified,
						    select_mode, maxshowdevs,
						    hflag);
			switch(retval) {
			case -1:
				errx(1, "%s", devstat_errbuf);
				break;
			case 1:
				phdr();
				if (wresized != 0)
					doresize();
				headercount = wrows;
				break;
			default:
				break;
			}
			break;
		}
		default:
			break;
		}

		/*
		 * We only want to re-select devices if we're in 'top'
		 * mode.  This is the only mode where the devices selected
		 * could actually change.
		 */
		if (hflag > 0) {
			int retval;
			retval = devstat_selectdevs(&dev_select, &num_selected,
						    &num_selections,
						    &select_generation,
						    generation,
						    cur.dinfo->devices,
						    num_devices, matches,
						    num_matches,
						    specified_devices,
						    num_devices_specified,
						    select_mode, maxshowdevs,
						    hflag);
			switch(retval) {
			case -1:
				errx(1,"%s", devstat_errbuf);
				break;
			case 1:
				phdr();
				if (wresized != 0)
					doresize();
				headercount = wrows;
				break;
			default:
				break;
			}
		}

		if (Tflag > 0) {
			tmp = cur.tk_nin;
			cur.tk_nin -= last.tk_nin;
			last.tk_nin = tmp;
			tmp = cur.tk_nout;
			cur.tk_nout -= last.tk_nout;
			last.tk_nout = tmp;
		}

		etime = cur.snap_time - last.snap_time;

		if (etime == 0.0)
			etime = 1.0;

		for (i = 0; i < CPUSTATES; i++) {
			tmp = cur.cp_time[i];
			cur.cp_time[i] -= last.cp_time[i];
			last.cp_time[i] = tmp;
		}

		if (xflag == 0 && Tflag > 0)
			printf("%4.0Lf %5.0Lf", cur.tk_nin / etime,
			    cur.tk_nout / etime);

		devstats(hflag, etime, havelast);

		if (xflag == 0) {
			if (Cflag > 0)
				cpustats();

			printf("\n");
		}
		fflush(stdout);

		if ((count >= 0 && --count <= 0) || return_requested)
			break;

		/*
		 * Use sigsuspend to safely sleep until either signal is
		 * received
		 */
		alarm_rang = 0;
		sigemptyset(&sigmask);
		sigaddset(&sigmask, SIGINT);
		sigaddset(&sigmask, SIGALRM);
		sigprocmask(SIG_BLOCK, &sigmask, &oldsigmask);
		while (! (alarm_rang || return_requested) ) {
			sigsuspend(&oldsigmask);
		}
		sigprocmask(SIG_UNBLOCK, &sigmask, NULL);

		havelast = 1;
	}

	exit(0);
}

/*
 * Force a header to be prepended to the next output.
 */
void
needhdr(int signo __unused)
{

	headercount = 1;
}

/*
 * When the terminal is resized, force an update of the maximum number of rows
 * printed between each header repetition.  Then force a new header to be
 * prepended to the next output.
 */
void
needresize(int signo __unused)
{

	wresized = 1;
	headercount = 1;
}

/*
 * Record the alarm so the main loop can break its sleep
 */
void
alarm_clock(int signo __unused)
{
	alarm_rang = 1;
}

/*
 * Request that the main loop exit soon
 */
void
needreturn(int signo __unused)
{
	return_requested = 1;
}

/*
 * Update the global `wrows' count of terminal rows.
 */
void
doresize(void)
{
	int status;
	struct winsize w;

	for (;;) {
		status = ioctl(fileno(stdout), TIOCGWINSZ, &w);
		if (status == -1 && errno == EINTR)
			continue;
		else if (status == -1)
			err(1, "ioctl");
		if (w.ws_row > 3)
			wrows = w.ws_row - 3;
		else
			wrows = IOSTAT_DEFAULT_ROWS;
		break;
	}

	/*
	 * Inhibit doresize() calls until we are rescheduled by SIGWINCH.
	 */
	wresized = 0;
}

static void
phdr(void)
{
	int i, printed;
	char devbuf[256];

	/*
	 * If xflag is set, we need a per-loop header, not a page header, so
	 * just return.  We'll print the header in devstats().
	 */
	if (xflag > 0)
		return;

	if (Tflag > 0)
		(void)printf("       tty");
	for (i = 0, printed=0;(i < num_devices) && (printed < maxshowdevs);i++){
		int di;
		if ((dev_select[i].selected != 0)
		 && (dev_select[i].selected <= maxshowdevs)) {
			di = dev_select[i].position;
			snprintf(devbuf, sizeof(devbuf), "%s%d", 
					    cur.dinfo->devices[di].device_name,
					    cur.dinfo->devices[di].unit_number);
			if (oflag > 0)
				(void)printf("%13.6s ", devbuf);
			else
				printf("%16.6s ", devbuf);
			printed++;
		}
	}
	if (Cflag > 0)
		(void)printf("            cpu\n");
	else
		(void)printf("\n");

	if (Tflag > 0)
		(void)printf(" tin  tout");

	for (i=0, printed = 0;(i < num_devices) && (printed < maxshowdevs);i++){
		if ((dev_select[i].selected != 0)
		 && (dev_select[i].selected <= maxshowdevs)) {
			if (oflag > 0) {
				if (Iflag == 0)
					(void)printf(" sps tps msps ");
				else
					(void)printf(" blk xfr msps ");
			} else {
				if (Iflag == 0)
					printf("  KB/t tps  MB/s ");
				else
					printf("  KB/t xfrs   MB ");
			}
			printed++;
		}
	}
	if (Cflag > 0)
		(void)printf(" us ni sy in id\n");
	else
		printf("\n");

}

static void
devstats(int perf_select, long double etime, int havelast)
{
	int dn;
	long double transfers_per_second, transfers_per_second_read;
	long double transfers_per_second_write;
	long double kb_per_transfer, mb_per_second, mb_per_second_read;
	long double mb_per_second_write;
	u_int64_t total_bytes, total_transfers, total_blocks;
	u_int64_t total_bytes_read, total_transfers_read;
	u_int64_t total_bytes_write, total_transfers_write;
	long double busy_pct, busy_time;
	u_int64_t queue_len;
	long double total_mb, blocks_per_second, total_duration;
	long double ms_per_other, ms_per_read, ms_per_write, ms_per_transaction;
	int firstline = 1;
	char *devicename;

	if (xflag > 0) {
		if (Cflag > 0) {
			printf("      cpu\n");
			printf(" us ni sy in id\n");
			cpustats();
			printf("\n");
		}
		printf("                        extended device statistics  ");
		if (Tflag > 0)
			printf("      tty ");
		printf("\n");
		if (Iflag == 0) {
			printf("device       r/s     w/s     kr/s     kw/s "
			    " ms/r  ms/w  ms/o  ms/t qlen  %%b  ");
		} else {
			printf("device           r/i         w/i         kr/i"
			    "         kw/i qlen   tsvc_t/i      sb/i  ");
		}
		if (Tflag > 0)
			printf("tin  tout ");
		printf("\n");
	}

	for (dn = 0; dn < num_devices; dn++) {
		int di;

		if (((perf_select == 0) && (dev_select[dn].selected == 0))
		 || (dev_select[dn].selected > maxshowdevs))
			continue;

		di = dev_select[dn].position;

		if (devstat_compute_statistics(&cur.dinfo->devices[di],
		    havelast ? &last.dinfo->devices[di] : NULL, etime,
		    DSM_TOTAL_BYTES, &total_bytes,
		    DSM_TOTAL_BYTES_READ, &total_bytes_read,
		    DSM_TOTAL_BYTES_WRITE, &total_bytes_write,
		    DSM_TOTAL_TRANSFERS, &total_transfers,
		    DSM_TOTAL_TRANSFERS_READ, &total_transfers_read,
		    DSM_TOTAL_TRANSFERS_WRITE, &total_transfers_write,
		    DSM_TOTAL_BLOCKS, &total_blocks,
		    DSM_KB_PER_TRANSFER, &kb_per_transfer,
		    DSM_TRANSFERS_PER_SECOND, &transfers_per_second,
		    DSM_TRANSFERS_PER_SECOND_READ, &transfers_per_second_read,
		    DSM_TRANSFERS_PER_SECOND_WRITE, &transfers_per_second_write,
		    DSM_MB_PER_SECOND, &mb_per_second,
		    DSM_MB_PER_SECOND_READ, &mb_per_second_read,
		    DSM_MB_PER_SECOND_WRITE, &mb_per_second_write,
		    DSM_BLOCKS_PER_SECOND, &blocks_per_second,
		    DSM_MS_PER_TRANSACTION, &ms_per_transaction,
		    DSM_MS_PER_TRANSACTION_READ, &ms_per_read,
		    DSM_MS_PER_TRANSACTION_WRITE, &ms_per_write,
		    DSM_MS_PER_TRANSACTION_OTHER, &ms_per_other,
		    DSM_BUSY_PCT, &busy_pct,
		    DSM_QUEUE_LENGTH, &queue_len,
		    DSM_TOTAL_DURATION, &total_duration,
		    DSM_TOTAL_BUSY_TIME, &busy_time,
		    DSM_NONE) != 0)
			errx(1, "%s", devstat_errbuf);

		if (perf_select != 0) {
			dev_select[dn].bytes = total_bytes;
			if ((dev_select[dn].selected == 0)
			 || (dev_select[dn].selected > maxshowdevs))
				continue;
		}

		if (Kflag > 0 || xflag > 0) {
			int block_size = cur.dinfo->devices[di].block_size;
			total_blocks = total_blocks * (block_size ?
						       block_size : 512) / 1024;
		}

		if (xflag > 0) {
			if (asprintf(&devicename, "%s%d",
			    cur.dinfo->devices[di].device_name,
			    cur.dinfo->devices[di].unit_number) == -1)
				err(1, "asprintf");
			/*
			 * If zflag is set, skip any devices with zero I/O.
			 */
			if (zflag == 0 || transfers_per_second_read > 0.05 ||
			    transfers_per_second_write > 0.05 ||
			    mb_per_second_read > ((long double).0005)/1024 ||
			    mb_per_second_write > ((long double).0005)/1024 ||
			    busy_pct > 0.5) {
				if (Iflag == 0)
					printf("%-8.8s %7d %7d %8.1Lf "
					    "%8.1Lf %5d %5d %5d %5d "
					    "%4" PRIu64 " %3.0Lf ",
					    devicename,
					    (int)transfers_per_second_read,
					    (int)transfers_per_second_write,
					    mb_per_second_read * 1024,
					    mb_per_second_write * 1024,
					    (int)ms_per_read, (int)ms_per_write,
					    (int)ms_per_other,
					    (int)ms_per_transaction,
					    queue_len, busy_pct);
				else
					printf("%-8.8s %11.1Lf %11.1Lf "
					    "%12.1Lf %12.1Lf %4" PRIu64
					    " %10.1Lf %9.1Lf ",
					    devicename,
					    (long double)total_transfers_read,
					    (long double)total_transfers_write,
					    (long double)
					        total_bytes_read / 1024,
					    (long double)
					        total_bytes_write / 1024,
					    queue_len,
					    total_duration, busy_time);
				if (firstline) {
					/*
					 * If this is the first device
					 * we're printing, also print
					 * CPU or TTY stats if requested.
					 */
					firstline = 0;
					if (Tflag > 0)
						printf("%4.0Lf%5.0Lf",
						    cur.tk_nin / etime,
						    cur.tk_nout / etime);
				}
				printf("\n");
			}
			free(devicename);
		} else if (oflag > 0) {
			int msdig = (ms_per_transaction < 100.0) ? 1 : 0;

			if (Iflag == 0)
				printf("%4.0Lf%4.0Lf%5.*Lf ",
				       blocks_per_second,
				       transfers_per_second,
				       msdig,
				       ms_per_transaction);
			else
				printf("%4.1" PRIu64 "%4.1" PRIu64 "%5.*Lf ",
				       total_blocks,
				       total_transfers,
				       msdig,
				       ms_per_transaction);
		} else {
			if (Iflag == 0)
				printf(" %5.2Lf %3.0Lf %5.2Lf ",
				       kb_per_transfer,
				       transfers_per_second,
				       mb_per_second);
			else {
				total_mb = total_bytes;
				total_mb /= 1024 * 1024;

				printf(" %5.2Lf %3.1" PRIu64 " %5.2Lf ",
				       kb_per_transfer,
				       total_transfers,
				       total_mb);
			}
		}
	}
	if (xflag > 0 && zflag > 0 && firstline == 1 &&
	    (Tflag > 0 || Cflag > 0)) {
		/*
		 * If zflag is set and we did not print any device
		 * lines I/O because they were all zero,
		 * print TTY/CPU stats.
		 */
		printf("%52s","");
		if (Tflag > 0)
			printf("%4.0Lf %5.0Lf", cur.tk_nin / etime,
			    cur.tk_nout / etime);
		if (Cflag > 0)
			cpustats();
		printf("\n");
	}
}

static void
cpustats(void)
{
	int state;
	double cptime;

	cptime = 0.0;

	for (state = 0; state < CPUSTATES; ++state)
		cptime += cur.cp_time[state];
	for (state = 0; state < CPUSTATES; ++state)
		printf(" %2.0f",
		       rint(100. * cur.cp_time[state] / (cptime ? cptime : 1)));
}

static int
readvar(kvm_t *kd, const char *name, int nlid, void *ptr, size_t len)
{
	if (kd != NULL) {
		ssize_t nbytes;

		nbytes = kvm_read(kd, namelist[nlid].n_value, ptr, len);

		if (nbytes < 0) {
			warnx("kvm_read(%s): %s", namelist[nlid].n_name,
			    kvm_geterr(kd));
			return (1);
		} else if ((size_t)nbytes != len) {
			warnx("kvm_read(%s): expected %zu bytes, got %zd bytes",
			      namelist[nlid].n_name, len, nbytes);
			return (1);
		}
	} else {
		size_t nlen = len;

		if (sysctlbyname(name, ptr, &nlen, NULL, 0) == -1) {
			warn("sysctl(%s...) failed", name);
			return (1);
		}
		if (nlen != len) {
			warnx("sysctl(%s...): expected %lu, got %lu", name,
			      (unsigned long)len, (unsigned long)nlen);
			return (1);
		}
	}
	return (0);
}
