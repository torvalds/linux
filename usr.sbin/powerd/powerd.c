/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Colin Percival
 * Copyright (c) 2005 Nate Lawson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#ifdef __i386__
#define USE_APM
#endif

#ifdef USE_APM
#include <machine/apm_bios.h>
#endif

#define DEFAULT_ACTIVE_PERCENT	75
#define DEFAULT_IDLE_PERCENT	50
#define DEFAULT_POLL_INTERVAL	250	/* Poll interval in milliseconds */

typedef enum {
	MODE_MIN,
	MODE_ADAPTIVE,
	MODE_HIADAPTIVE,
	MODE_MAX,
} modes_t;

typedef enum {
	SRC_AC,
	SRC_BATTERY,
	SRC_UNKNOWN,
} power_src_t;

static const char *modes[] = {
	"AC",
	"battery",
	"unknown"
};

#define ACPIAC		"hw.acpi.acline"
#define PMUAC		"dev.pmu.0.acline"
#define APMDEV		"/dev/apm"
#define DEVDPIPE	"/var/run/devd.pipe"
#define DEVCTL_MAXBUF	1024

static int	read_usage_times(int *load);
static int	read_freqs(int *numfreqs, int **freqs, int **power,
		    int minfreq, int maxfreq);
static int	set_freq(int freq);
static void	acline_init(void);
static void	acline_read(void);
static int	devd_init(void);
static void	devd_close(void);
static void	handle_sigs(int sig);
static void	parse_mode(char *arg, int *mode, int ch);
static void	usage(void);

/* Sysctl data structures. */
static int	cp_times_mib[2];
static int	freq_mib[4];
static int	levels_mib[4];
static int	acline_mib[4];
static size_t	acline_mib_len;

/* Configuration */
static int	cpu_running_mark;
static int	cpu_idle_mark;
static int	poll_ival;
static int	vflag;

static volatile sig_atomic_t exit_requested;
static power_src_t acline_status;
typedef enum {
	ac_none,
	ac_sysctl,
	ac_acpi_devd,
#ifdef USE_APM
	ac_apm,
#endif
} acline_mode_t;
static acline_mode_t acline_mode;
static acline_mode_t acline_mode_user = ac_none;
#ifdef USE_APM
static int	apm_fd = -1;
#endif
static int	devd_pipe = -1;

#define DEVD_RETRY_INTERVAL 60 /* seconds */
static struct timeval tried_devd;

/*
 * This function returns summary load of all CPUs.  It was made so
 * intentionally to not reduce performance in scenarios when several
 * threads are processing requests as a pipeline -- running one at
 * a time on different CPUs and waiting for each other.
 */
static int
read_usage_times(int *load)
{
	static long *cp_times = NULL, *cp_times_old = NULL;
	static int ncpus = 0;
	size_t cp_times_len;
	int error, cpu, i, total;

	if (cp_times == NULL) {
		cp_times_len = 0;
		error = sysctl(cp_times_mib, 2, NULL, &cp_times_len, NULL, 0);
		if (error)
			return (error);
		if ((cp_times = malloc(cp_times_len)) == NULL)
			return (errno);
		if ((cp_times_old = malloc(cp_times_len)) == NULL) {
			free(cp_times);
			cp_times = NULL;
			return (errno);
		}
		ncpus = cp_times_len / (sizeof(long) * CPUSTATES);
	}

	cp_times_len = sizeof(long) * CPUSTATES * ncpus;
	error = sysctl(cp_times_mib, 2, cp_times, &cp_times_len, NULL, 0);
	if (error)
		return (error);

	if (load) {
		*load = 0;
		for (cpu = 0; cpu < ncpus; cpu++) {
			total = 0;
			for (i = 0; i < CPUSTATES; i++) {
			    total += cp_times[cpu * CPUSTATES + i] -
				cp_times_old[cpu * CPUSTATES + i];
			}
			if (total == 0)
				continue;
			*load += 100 - (cp_times[cpu * CPUSTATES + CP_IDLE] -
			    cp_times_old[cpu * CPUSTATES + CP_IDLE]) * 100 / total;
		}
	}

	memcpy(cp_times_old, cp_times, cp_times_len);

	return (0);
}

static int
read_freqs(int *numfreqs, int **freqs, int **power, int minfreq, int maxfreq)
{
	char *freqstr, *p, *q;
	int i, j;
	size_t len = 0;

	if (sysctl(levels_mib, 4, NULL, &len, NULL, 0))
		return (-1);
	if ((freqstr = malloc(len)) == NULL)
		return (-1);
	if (sysctl(levels_mib, 4, freqstr, &len, NULL, 0))
		return (-1);

	*numfreqs = 1;
	for (p = freqstr; *p != '\0'; p++)
		if (*p == ' ')
			(*numfreqs)++;

	if ((*freqs = malloc(*numfreqs * sizeof(int))) == NULL) {
		free(freqstr);
		return (-1);
	}
	if ((*power = malloc(*numfreqs * sizeof(int))) == NULL) {
		free(freqstr);
		free(*freqs);
		return (-1);
	}
	for (i = 0, j = 0, p = freqstr; i < *numfreqs; i++) {
		q = strchr(p, ' ');
		if (q != NULL)
			*q = '\0';
		if (sscanf(p, "%d/%d", &(*freqs)[j], &(*power)[i]) != 2) {
			free(freqstr);
			free(*freqs);
			free(*power);
			return (-1);
		}
		if (((*freqs)[j] >= minfreq || minfreq == -1) &&
		    ((*freqs)[j] <= maxfreq || maxfreq == -1))
			j++;
		p = q + 1;
	}

	*numfreqs = j;
	if ((*freqs = realloc(*freqs, *numfreqs * sizeof(int))) == NULL) {
		free(freqstr);
		free(*freqs);
		free(*power);
		return (-1);
	}

	free(freqstr);
	return (0);
}

static int
get_freq(void)
{
	size_t len;
	int curfreq;

	len = sizeof(curfreq);
	if (sysctl(freq_mib, 4, &curfreq, &len, NULL, 0) != 0) {
		if (vflag)
			warn("error reading current CPU frequency");
		curfreq = 0;
	}
	return (curfreq);
}

static int
set_freq(int freq)
{

	if (sysctl(freq_mib, 4, NULL, NULL, &freq, sizeof(freq))) {
		if (errno != EPERM)
			return (-1);
	}

	return (0);
}

static int
get_freq_id(int freq, int *freqs, int numfreqs)
{
	int i = 1;

	while (i < numfreqs) {
		if (freqs[i] < freq)
			break;
		i++;
	}
	return (i - 1);
}

/*
 * Try to use ACPI to find the AC line status.  If this fails, fall back
 * to APM.  If nothing succeeds, we'll just run in default mode.
 */
static void
acline_init(void)
{
	int skip_source_check;

	acline_mib_len = 4;
	acline_status = SRC_UNKNOWN;
	skip_source_check = (acline_mode_user == ac_none ||
			     acline_mode_user == ac_acpi_devd);

	if ((skip_source_check || acline_mode_user == ac_sysctl) &&
	    sysctlnametomib(ACPIAC, acline_mib, &acline_mib_len) == 0) {
		acline_mode = ac_sysctl;
		if (vflag)
			warnx("using sysctl for AC line status");
#ifdef __powerpc__
	} else if ((skip_source_check || acline_mode_user == ac_sysctl) &&
		   sysctlnametomib(PMUAC, acline_mib, &acline_mib_len) == 0) {
		acline_mode = ac_sysctl;
		if (vflag)
			warnx("using sysctl for AC line status");
#endif
#ifdef USE_APM
	} else if ((skip_source_check || acline_mode_user == ac_apm) &&
		   (apm_fd = open(APMDEV, O_RDONLY)) >= 0) {
		if (vflag)
			warnx("using APM for AC line status");
		acline_mode = ac_apm;
#endif
	} else {
		warnx("unable to determine AC line status");
		acline_mode = ac_none;
	}
}

static void
acline_read(void)
{
	if (acline_mode == ac_acpi_devd) {
		char buf[DEVCTL_MAXBUF], *ptr;
		ssize_t rlen;
		int notify;

		rlen = read(devd_pipe, buf, sizeof(buf));
		if (rlen == 0 || (rlen < 0 && errno != EWOULDBLOCK)) {
			if (vflag)
				warnx("lost devd connection, switching to sysctl");
			devd_close();
			acline_mode = ac_sysctl;
			/* FALLTHROUGH */
		}
		if (rlen > 0 &&
		    (ptr = strstr(buf, "system=ACPI")) != NULL &&
		    (ptr = strstr(ptr, "subsystem=ACAD")) != NULL &&
		    (ptr = strstr(ptr, "notify=")) != NULL &&
		    sscanf(ptr, "notify=%x", &notify) == 1)
			acline_status = (notify ? SRC_AC : SRC_BATTERY);
	}
	if (acline_mode == ac_sysctl) {
		int acline;
		size_t len;

		len = sizeof(acline);
		if (sysctl(acline_mib, acline_mib_len, &acline, &len,
		    NULL, 0) == 0)
			acline_status = (acline ? SRC_AC : SRC_BATTERY);
		else
			acline_status = SRC_UNKNOWN;
	}
#ifdef USE_APM
	if (acline_mode == ac_apm) {
		struct apm_info info;

		if (ioctl(apm_fd, APMIO_GETINFO, &info) == 0) {
			acline_status = (info.ai_acline ? SRC_AC : SRC_BATTERY);
		} else {
			close(apm_fd);
			apm_fd = -1;
			acline_mode = ac_none;
			acline_status = SRC_UNKNOWN;
		}
	}
#endif
	/* try to (re)connect to devd */
#ifdef USE_APM
	if ((acline_mode == ac_sysctl &&
	    (acline_mode_user == ac_none ||
	     acline_mode_user == ac_acpi_devd)) ||
	    (acline_mode == ac_apm &&
	     acline_mode_user == ac_acpi_devd)) {
#else
	if (acline_mode == ac_sysctl &&
	    (acline_mode_user == ac_none ||
	     acline_mode_user == ac_acpi_devd)) {
#endif
		struct timeval now;

		gettimeofday(&now, NULL);
		if (now.tv_sec > tried_devd.tv_sec + DEVD_RETRY_INTERVAL) {
			if (devd_init() >= 0) {
				if (vflag)
					warnx("using devd for AC line status");
				acline_mode = ac_acpi_devd;
			}
			tried_devd = now;
		}
	}
}

static int
devd_init(void)
{
	struct sockaddr_un devd_addr;

	bzero(&devd_addr, sizeof(devd_addr));
	if ((devd_pipe = socket(PF_LOCAL, SOCK_STREAM|SOCK_NONBLOCK, 0)) < 0) {
		if (vflag)
			warn("%s(): socket()", __func__);
		return (-1);
	}

	devd_addr.sun_family = PF_LOCAL;
	strlcpy(devd_addr.sun_path, DEVDPIPE, sizeof(devd_addr.sun_path));
	if (connect(devd_pipe, (struct sockaddr *)&devd_addr,
	    sizeof(devd_addr)) == -1) {
		if (vflag)
			warn("%s(): connect()", __func__);
		close(devd_pipe);
		devd_pipe = -1;
		return (-1);
	}

	return (devd_pipe);
}

static void
devd_close(void)
{

	close(devd_pipe);
	devd_pipe = -1;
}

static void
parse_mode(char *arg, int *mode, int ch)
{

	if (strcmp(arg, "minimum") == 0 || strcmp(arg, "min") == 0)
		*mode = MODE_MIN;
	else if (strcmp(arg, "maximum") == 0 || strcmp(arg, "max") == 0)
		*mode = MODE_MAX;
	else if (strcmp(arg, "adaptive") == 0 || strcmp(arg, "adp") == 0)
		*mode = MODE_ADAPTIVE;
	else if (strcmp(arg, "hiadaptive") == 0 || strcmp(arg, "hadp") == 0)
		*mode = MODE_HIADAPTIVE;
	else
		errx(1, "bad option: -%c %s", (char)ch, optarg);
}

static void
parse_acline_mode(char *arg, int ch)
{
	if (strcmp(arg, "sysctl") == 0)
		acline_mode_user = ac_sysctl;
	else if (strcmp(arg, "devd") == 0)
		acline_mode_user = ac_acpi_devd;
#ifdef USE_APM
	else if (strcmp(arg, "apm") == 0)
		acline_mode_user = ac_apm;
#endif
	else
		errx(1, "bad option: -%c %s", (char)ch, optarg);
}

static void
handle_sigs(int __unused sig)
{

	exit_requested = 1;
}

static void
usage(void)
{

	fprintf(stderr,
"usage: powerd [-v] [-a mode] [-b mode] [-i %%] [-m freq] [-M freq] [-n mode] [-p ival] [-r %%] [-s source] [-P pidfile]\n");
	exit(1);
}

int
main(int argc, char * argv[])
{
	struct timeval timeout;
	fd_set fdset;
	int nfds;
	struct pidfh *pfh = NULL;
	const char *pidfile = NULL;
	int freq, curfreq, initfreq, *freqs, i, j, *mwatts, numfreqs, load;
	int minfreq = -1, maxfreq = -1;
	int ch, mode, mode_ac, mode_battery, mode_none, idle, to;
	uint64_t mjoules_used;
	size_t len;

	/* Default mode for all AC states is adaptive. */
	mode_ac = mode_none = MODE_HIADAPTIVE;
	mode_battery = MODE_ADAPTIVE;
	cpu_running_mark = DEFAULT_ACTIVE_PERCENT;
	cpu_idle_mark = DEFAULT_IDLE_PERCENT;
	poll_ival = DEFAULT_POLL_INTERVAL;
	mjoules_used = 0;
	vflag = 0;

	/* User must be root to control frequencies. */
	if (geteuid() != 0)
		errx(1, "must be root to run");

	while ((ch = getopt(argc, argv, "a:b:i:m:M:n:p:P:r:s:v")) != -1)
		switch (ch) {
		case 'a':
			parse_mode(optarg, &mode_ac, ch);
			break;
		case 'b':
			parse_mode(optarg, &mode_battery, ch);
			break;
		case 's':
			parse_acline_mode(optarg, ch);
			break;
		case 'i':
			cpu_idle_mark = atoi(optarg);
			if (cpu_idle_mark < 0 || cpu_idle_mark > 100) {
				warnx("%d is not a valid percent",
				    cpu_idle_mark);
				usage();
			}
			break;
		case 'm':
			minfreq = atoi(optarg);
			if (minfreq < 0) {
				warnx("%d is not a valid CPU frequency",
				    minfreq);
				usage();
			}
			break;
		case 'M':
			maxfreq = atoi(optarg);
			if (maxfreq < 0) {
				warnx("%d is not a valid CPU frequency",
				    maxfreq);
				usage();
			}
			break;
		case 'n':
			parse_mode(optarg, &mode_none, ch);
			break;
		case 'p':
			poll_ival = atoi(optarg);
			if (poll_ival < 5) {
				warnx("poll interval is in units of ms");
				usage();
			}
			break;
		case 'P':
			pidfile = optarg;
			break;
		case 'r':
			cpu_running_mark = atoi(optarg);
			if (cpu_running_mark <= 0 || cpu_running_mark > 100) {
				warnx("%d is not a valid percent",
				    cpu_running_mark);
				usage();
			}
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			usage();
		}

	mode = mode_none;

	/* Poll interval is in units of ms. */
	poll_ival *= 1000;

	/* Look up various sysctl MIBs. */
	len = 2;
	if (sysctlnametomib("kern.cp_times", cp_times_mib, &len))
		err(1, "lookup kern.cp_times");
	len = 4;
	if (sysctlnametomib("dev.cpu.0.freq", freq_mib, &len))
		err(EX_UNAVAILABLE, "no cpufreq(4) support -- aborting");
	len = 4;
	if (sysctlnametomib("dev.cpu.0.freq_levels", levels_mib, &len))
		err(1, "lookup freq_levels");

	/* Check if we can read the load and supported freqs. */
	if (read_usage_times(NULL))
		err(1, "read_usage_times");
	if (read_freqs(&numfreqs, &freqs, &mwatts, minfreq, maxfreq))
		err(1, "error reading supported CPU frequencies");
	if (numfreqs == 0)
		errx(1, "no CPU frequencies in user-specified range");

	/* Run in the background unless in verbose mode. */
	if (!vflag) {
		pid_t otherpid;

		pfh = pidfile_open(pidfile, 0600, &otherpid);
		if (pfh == NULL) {
			if (errno == EEXIST) {
				errx(1, "powerd already running, pid: %d",
				    otherpid);
			}
			warn("cannot open pid file");
		}
		if (daemon(0, 0) != 0) {
			warn("cannot enter daemon mode, exiting");
			pidfile_remove(pfh);
			exit(EXIT_FAILURE);

		}
		pidfile_write(pfh);
	}

	/* Decide whether to use ACPI or APM to read the AC line status. */
	acline_init();

	/*
	 * Exit cleanly on signals.
	 */
	signal(SIGINT, handle_sigs);
	signal(SIGTERM, handle_sigs);

	freq = initfreq = curfreq = get_freq();
	i = get_freq_id(curfreq, freqs, numfreqs);
	if (freq < 1)
		freq = 1;

	/*
	 * If we are in adaptive mode and the current frequency is outside the
	 * user-defined range, adjust it to be within the user-defined range.
	 */
	acline_read();
	if (acline_status > SRC_UNKNOWN)
		errx(1, "invalid AC line status %d", acline_status);
	if ((acline_status == SRC_AC &&
	    (mode_ac == MODE_ADAPTIVE || mode_ac == MODE_HIADAPTIVE)) ||
	    (acline_status == SRC_BATTERY &&
	    (mode_battery == MODE_ADAPTIVE || mode_battery == MODE_HIADAPTIVE)) ||
	    (acline_status == SRC_UNKNOWN &&
	    (mode_none == MODE_ADAPTIVE || mode_none == MODE_HIADAPTIVE))) {
		/* Read the current frequency. */
		len = sizeof(curfreq);
		if (sysctl(freq_mib, 4, &curfreq, &len, NULL, 0) != 0) {
			if (vflag)
				warn("error reading current CPU frequency");
		}
		if (curfreq < freqs[numfreqs - 1]) {
			if (vflag) {
				printf("CPU frequency is below user-defined "
				    "minimum; changing frequency to %d "
				    "MHz\n", freqs[numfreqs - 1]);
			}
			if (set_freq(freqs[numfreqs - 1]) != 0) {
				warn("error setting CPU freq %d",
				    freqs[numfreqs - 1]);
			}
		} else if (curfreq > freqs[0]) {
			if (vflag) {
				printf("CPU frequency is above user-defined "
				    "maximum; changing frequency to %d "
				    "MHz\n", freqs[0]);
			}
			if (set_freq(freqs[0]) != 0) {
				warn("error setting CPU freq %d",
				    freqs[0]);
			}
		}
	}

	idle = 0;
	/* Main loop. */
	for (;;) {
		FD_ZERO(&fdset);
		if (devd_pipe >= 0) {
			FD_SET(devd_pipe, &fdset);
			nfds = devd_pipe + 1;
		} else {
			nfds = 0;
		}
		if (mode == MODE_HIADAPTIVE || idle < 120)
			to = poll_ival;
		else if (idle < 360)
			to = poll_ival * 2;
		else
			to = poll_ival * 4;
		timeout.tv_sec = to / 1000000;
		timeout.tv_usec = to % 1000000;
		select(nfds, &fdset, NULL, &fdset, &timeout);

		/* If the user requested we quit, print some statistics. */
		if (exit_requested) {
			if (vflag && mjoules_used != 0)
				printf("total joules used: %u.%03u\n",
				    (u_int)(mjoules_used / 1000),
				    (int)mjoules_used % 1000);
			break;
		}

		/* Read the current AC status and record the mode. */
		acline_read();
		switch (acline_status) {
		case SRC_AC:
			mode = mode_ac;
			break;
		case SRC_BATTERY:
			mode = mode_battery;
			break;
		case SRC_UNKNOWN:
			mode = mode_none;
			break;
		default:
			errx(1, "invalid AC line status %d", acline_status);
		}

		/* Read the current frequency. */
		if (idle % 32 == 0) {
			if ((curfreq = get_freq()) == 0)
				continue;
			i = get_freq_id(curfreq, freqs, numfreqs);
		}
		idle++;
		if (vflag) {
			/* Keep a sum of all power actually used. */
			if (mwatts[i] != -1)
				mjoules_used +=
				    (mwatts[i] * (poll_ival / 1000)) / 1000;
		}

		/* Always switch to the lowest frequency in min mode. */
		if (mode == MODE_MIN) {
			freq = freqs[numfreqs - 1];
			if (curfreq != freq) {
				if (vflag) {
					printf("now operating on %s power; "
					    "changing frequency to %d MHz\n",
					    modes[acline_status], freq);
				}
				idle = 0;
				if (set_freq(freq) != 0) {
					warn("error setting CPU freq %d",
					    freq);
					continue;
				}
			}
			continue;
		}

		/* Always switch to the highest frequency in max mode. */
		if (mode == MODE_MAX) {
			freq = freqs[0];
			if (curfreq != freq) {
				if (vflag) {
					printf("now operating on %s power; "
					    "changing frequency to %d MHz\n",
					    modes[acline_status], freq);
				}
				idle = 0;
				if (set_freq(freq) != 0) {
					warn("error setting CPU freq %d",
					    freq);
					continue;
				}
			}
			continue;
		}

		/* Adaptive mode; get the current CPU usage times. */
		if (read_usage_times(&load)) {
			if (vflag)
				warn("read_usage_times() failed");
			continue;
		}

		if (mode == MODE_ADAPTIVE) {
			if (load > cpu_running_mark) {
				if (load > 95 || load > cpu_running_mark * 2)
					freq *= 2;
				else
					freq = freq * load / cpu_running_mark;
				if (freq > freqs[0])
					freq = freqs[0];
			} else if (load < cpu_idle_mark &&
			    curfreq * load < freqs[get_freq_id(
			    freq * 7 / 8, freqs, numfreqs)] *
			    cpu_running_mark) {
				freq = freq * 7 / 8;
				if (freq < freqs[numfreqs - 1])
					freq = freqs[numfreqs - 1];
			}
		} else { /* MODE_HIADAPTIVE */
			if (load > cpu_running_mark / 2) {
				if (load > 95 || load > cpu_running_mark)
					freq *= 4;
				else
					freq = freq * load * 2 / cpu_running_mark;
				if (freq > freqs[0] * 2)
					freq = freqs[0] * 2;
			} else if (load < cpu_idle_mark / 2 &&
			    curfreq * load < freqs[get_freq_id(
			    freq * 31 / 32, freqs, numfreqs)] *
			    cpu_running_mark / 2) {
				freq = freq * 31 / 32;
				if (freq < freqs[numfreqs - 1])
					freq = freqs[numfreqs - 1];
			}
		}
		if (vflag) {
		    printf("load %3d%%, current freq %4d MHz (%2d), wanted freq %4d MHz\n",
			load, curfreq, i, freq);
		}
		j = get_freq_id(freq, freqs, numfreqs);
		if (i != j) {
			if (vflag) {
				printf("changing clock"
				    " speed from %d MHz to %d MHz\n",
				    freqs[i], freqs[j]);
			}
			idle = 0;
			if (set_freq(freqs[j]))
				warn("error setting CPU frequency %d",
				    freqs[j]);
		}
	}
	if (set_freq(initfreq))
		warn("error setting CPU frequency %d", initfreq);
	free(freqs);
	free(mwatts);
	devd_close();
	if (!vflag)
		pidfile_remove(pfh);

	exit(0);
}
