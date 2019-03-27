/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008, 2009 Yahoo!, Inc.
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
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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

#include <sys/types.h>
#include <sys/errno.h>
#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include "mfiutil.h"

static int
mfi_event_get_info(int fd, struct mfi_evt_log_state *info, uint8_t *statusp)
{

	return (mfi_dcmd_command(fd, MFI_DCMD_CTRL_EVENT_GETINFO, info,
	    sizeof(struct mfi_evt_log_state), NULL, 0, statusp));
}

static int
mfi_get_events(int fd, struct mfi_evt_list *list, int num_events,
    union mfi_evt filter, uint32_t start_seq, uint8_t *statusp)
{
	uint32_t mbox[2];
	size_t size;

	mbox[0] = start_seq;
	mbox[1] = filter.word;
	size = sizeof(struct mfi_evt_list) + sizeof(struct mfi_evt_detail) *
	    (num_events - 1);
	return (mfi_dcmd_command(fd, MFI_DCMD_CTRL_EVENT_GET, list, size,
	    (uint8_t *)&mbox, sizeof(mbox), statusp));
}

static int
show_logstate(int ac, char **av __unused)
{
	struct mfi_evt_log_state info;
	int error, fd;

	if (ac != 1) {
		warnx("show logstate: extra arguments");
		return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (mfi_event_get_info(fd, &info, NULL) < 0) {
		error = errno;
		warn("Failed to get event log info");
		close(fd);
		return (error);
	}

	printf("mfi%d Event Log Sequence Numbers:\n", mfi_unit);
	printf("  Newest Seq #: %u\n", info.newest_seq_num);
	printf("  Oldest Seq #: %u\n", info.oldest_seq_num);
	printf("   Clear Seq #: %u\n", info.clear_seq_num);
	printf("Shutdown Seq #: %u\n", info.shutdown_seq_num);
	printf("    Boot Seq #: %u\n", info.boot_seq_num);
	
	close(fd);

	return (0);
}
MFI_COMMAND(show, logstate, show_logstate);

static int
parse_seq(struct mfi_evt_log_state *info, char *arg, uint32_t *seq)
{
	char *cp;
	long val;

	if (strcasecmp(arg, "newest") == 0) {
		*seq = info->newest_seq_num;
		return (0);
	}
	if (strcasecmp(arg, "oldest") == 0) {
		*seq = info->oldest_seq_num;
		return (0);
	}
	if (strcasecmp(arg, "clear") == 0) {
		*seq = info->clear_seq_num;
		return (0);
	}
	if (strcasecmp(arg, "shutdown") == 0) {
		*seq = info->shutdown_seq_num;
		return (0);
	}
	if (strcasecmp(arg, "boot") == 0) {
		*seq = info->boot_seq_num;
		return (0);
	}
	val = strtol(arg, &cp, 0);
	if (*cp != '\0' || val < 0) {
		errno = EINVAL;
		return (-1);
	}
	*seq = val;
	return (0);
}

static int
parse_locale(char *arg, uint16_t *locale)
{
	char *cp;
	long val;

	if (strncasecmp(arg, "vol", 3) == 0 || strcasecmp(arg, "ld") == 0) {
		*locale = MFI_EVT_LOCALE_LD;
		return (0);
	}
	if (strncasecmp(arg, "drive", 5) == 0 || strcasecmp(arg, "pd") == 0) {
		*locale = MFI_EVT_LOCALE_PD;
		return (0);
	}
	if (strncasecmp(arg, "encl", 4) == 0) {
		*locale = MFI_EVT_LOCALE_ENCL;
		return (0);
	}
	if (strncasecmp(arg, "batt", 4) == 0 ||
	    strncasecmp(arg, "bbu", 3) == 0) {
		*locale = MFI_EVT_LOCALE_BBU;
		return (0);
	}
	if (strcasecmp(arg, "sas") == 0) {
		*locale = MFI_EVT_LOCALE_SAS;
		return (0);
	}
	if (strcasecmp(arg, "ctrl") == 0 || strncasecmp(arg, "cont", 4) == 0) {
		*locale = MFI_EVT_LOCALE_CTRL;
		return (0);
	}
	if (strcasecmp(arg, "config") == 0) {
		*locale = MFI_EVT_LOCALE_CONFIG;
		return (0);
	}
	if (strcasecmp(arg, "cluster") == 0) {
		*locale = MFI_EVT_LOCALE_CLUSTER;
		return (0);
	}
	if (strcasecmp(arg, "all") == 0) {
		*locale = MFI_EVT_LOCALE_ALL;
		return (0);
	}
	val = strtol(arg, &cp, 0);
	if (*cp != '\0' || val < 0 || val > 0xffff) {
		errno = EINVAL;
		return (-1);
	}
	*locale = val;
	return (0);
}

static int
parse_class(char *arg, int8_t *class)
{
	char *cp;
	long val;

	if (strcasecmp(arg, "debug") == 0) {
		*class = MFI_EVT_CLASS_DEBUG;
		return (0);
	}
	if (strncasecmp(arg, "prog", 4) == 0) {
		*class = MFI_EVT_CLASS_PROGRESS;
		return (0);
	}
	if (strncasecmp(arg, "info", 4) == 0) {
		*class = MFI_EVT_CLASS_INFO;
		return (0);
	}
	if (strncasecmp(arg, "warn", 4) == 0) {
		*class = MFI_EVT_CLASS_WARNING;
		return (0);
	}
	if (strncasecmp(arg, "crit", 4) == 0) {
		*class = MFI_EVT_CLASS_CRITICAL;
		return (0);
	}
	if (strcasecmp(arg, "fatal") == 0) {
		*class = MFI_EVT_CLASS_FATAL;
		return (0);
	}
	if (strcasecmp(arg, "dead") == 0) {
		*class = MFI_EVT_CLASS_DEAD;
		return (0);
	}
	val = strtol(arg, &cp, 0);
	if (*cp != '\0' || val < -128 || val > 127) {
		errno = EINVAL;
		return (-1);
	}
	*class = val;
	return (0);
}

/*
 * The timestamp is the number of seconds since 00:00 Jan 1, 2000.  If
 * the bits in 24-31 are all set, then it is the number of seconds since
 * boot.
 */
static const char *
format_timestamp(uint32_t timestamp)
{
	static char buffer[32];
	static time_t base;
	time_t t;
	struct tm tm;

	if ((timestamp & 0xff000000) == 0xff000000) {
		snprintf(buffer, sizeof(buffer), "boot + %us", timestamp &
		    0x00ffffff);
		return (buffer);
	}

	if (base == 0) {
		/* Compute 00:00 Jan 1, 2000 offset. */
		bzero(&tm, sizeof(tm));
		tm.tm_mday = 1;
		tm.tm_year = (2000 - 1900);
		base = mktime(&tm);
	}
	if (base == -1) {
		snprintf(buffer, sizeof(buffer), "%us", timestamp);
		return (buffer);
	}
	t = base + timestamp;
	strftime(buffer, sizeof(buffer), "%+", localtime(&t));
	return (buffer);
}

static const char *
format_locale(uint16_t locale)
{
	static char buffer[8];

	switch (locale) {
	case MFI_EVT_LOCALE_LD:
		return ("VOLUME");
	case MFI_EVT_LOCALE_PD:
		return ("DRIVE");
	case MFI_EVT_LOCALE_ENCL:
		return ("ENCL");
	case MFI_EVT_LOCALE_BBU:
		return ("BATTERY");
	case MFI_EVT_LOCALE_SAS:
		return ("SAS");
	case MFI_EVT_LOCALE_CTRL:
		return ("CTRL");
	case MFI_EVT_LOCALE_CONFIG:
		return ("CONFIG");
	case MFI_EVT_LOCALE_CLUSTER:
		return ("CLUSTER");
	case MFI_EVT_LOCALE_ALL:
		return ("ALL");
	default:
		snprintf(buffer, sizeof(buffer), "0x%04x", locale);
		return (buffer);
	}
}

static const char *
format_class(int8_t class)
{
	static char buffer[6];

	switch (class) {
	case MFI_EVT_CLASS_DEBUG:
		return ("debug");
	case MFI_EVT_CLASS_PROGRESS:
		return ("progress");
	case MFI_EVT_CLASS_INFO:
		return ("info");
	case MFI_EVT_CLASS_WARNING:
		return ("WARN");
	case MFI_EVT_CLASS_CRITICAL:
		return ("CRIT");
	case MFI_EVT_CLASS_FATAL:
		return ("FATAL");
	case MFI_EVT_CLASS_DEAD:
		return ("DEAD");
	default:
		snprintf(buffer, sizeof(buffer), "%d", class);
		return (buffer);
	}
}

/* Simulates %D from kernel printf(9). */
static void
simple_hex(void *ptr, size_t length, const char *separator)
{
	unsigned char *cp;
	u_int i;

	if (length == 0)
		return;
	cp = ptr;
	printf("%02x", cp[0]);
	for (i = 1; i < length; i++)
		printf("%s%02x", separator, cp[i]);
}

static const char *
pdrive_location(struct mfi_evt_pd *pd)
{
	static char buffer[16];

	if (pd->enclosure_index == 0)
		snprintf(buffer, sizeof(buffer), "%02d(s%d)", pd->device_id,
		    pd->slot_number);
	else
		snprintf(buffer, sizeof(buffer), "%02d(e%d/s%d)", pd->device_id,
		    pd->enclosure_index, pd->slot_number);
	return (buffer);
}

static const char *
volume_name(int fd, struct mfi_evt_ld *ld)
{

	return (mfi_volume_name(fd, ld->target_id));
}

/* Ripped from sys/dev/mfi/mfi.c. */
static void
mfi_decode_evt(int fd, struct mfi_evt_detail *detail, int verbose)
{

	printf("%5d (%s/%s/%s) - ", detail->seq, format_timestamp(detail->time),
	    format_locale(detail->evt_class.members.locale),
	    format_class(detail->evt_class.members.evt_class));
	switch (detail->arg_type) {
	case MR_EVT_ARGS_NONE:
		break;
	case MR_EVT_ARGS_CDB_SENSE:
		if (verbose) {
			printf("PD %s CDB ",
			    pdrive_location(&detail->args.cdb_sense.pd)
			    );
			simple_hex(detail->args.cdb_sense.cdb,
			    detail->args.cdb_sense.cdb_len, ":");
			printf(" Sense ");
			simple_hex(detail->args.cdb_sense.sense,
			    detail->args.cdb_sense.sense_len, ":");
			printf(":\n ");
		}
		break;
	case MR_EVT_ARGS_LD:
		printf("VOL %s event: ", volume_name(fd, &detail->args.ld));
		break;
	case MR_EVT_ARGS_LD_COUNT:
		printf("VOL %s", volume_name(fd, &detail->args.ld_count.ld));
		if (verbose) {
			printf(" count %lld: ",
			    (long long)detail->args.ld_count.count);
		}
		printf(": ");
		break;
	case MR_EVT_ARGS_LD_LBA:
		printf("VOL %s", volume_name(fd, &detail->args.ld_count.ld));
		if (verbose) {
			printf(" lba %lld",
			    (long long)detail->args.ld_lba.lba);
		}
		printf(": ");
		break;
	case MR_EVT_ARGS_LD_OWNER:
		printf("VOL %s", volume_name(fd, &detail->args.ld_count.ld));
		if (verbose) {
			printf(" owner changed: prior %d, new %d",
			    detail->args.ld_owner.pre_owner,
			    detail->args.ld_owner.new_owner);
		}
		printf(": ");
		break;
	case MR_EVT_ARGS_LD_LBA_PD_LBA:
		printf("VOL %s", volume_name(fd, &detail->args.ld_count.ld));
		if (verbose) {
			printf(" lba %lld, physical drive PD %s lba %lld",
			    (long long)detail->args.ld_lba_pd_lba.ld_lba,
			    pdrive_location(&detail->args.ld_lba_pd_lba.pd),
			    (long long)detail->args.ld_lba_pd_lba.pd_lba);
		}
		printf(": ");
		break;
	case MR_EVT_ARGS_LD_PROG:
		printf("VOL %s", volume_name(fd, &detail->args.ld_prog.ld));
		if (verbose) {
			printf(" progress %d%% in %ds",
			    detail->args.ld_prog.prog.progress/655,
			    detail->args.ld_prog.prog.elapsed_seconds);
		}
		printf(": ");
		break;
	case MR_EVT_ARGS_LD_STATE:
		printf("VOL %s", volume_name(fd, &detail->args.ld_prog.ld));
		if (verbose) {
			printf(" state prior %s new %s",
			    mfi_ldstate(detail->args.ld_state.prev_state),
			    mfi_ldstate(detail->args.ld_state.new_state));
		}
		printf(": ");
		break;
	case MR_EVT_ARGS_LD_STRIP:
		printf("VOL %s", volume_name(fd, &detail->args.ld_strip.ld));
		if (verbose) {
			printf(" strip %lld",
			    (long long)detail->args.ld_strip.strip);
		}
		printf(": ");
		break;
	case MR_EVT_ARGS_PD:
		if (verbose) {
			printf("PD %s event: ",
			    pdrive_location(&detail->args.pd));
		}
		break;
	case MR_EVT_ARGS_PD_ERR:
		if (verbose) {
			printf("PD %s err %d: ",
			    pdrive_location(&detail->args.pd_err.pd),
			    detail->args.pd_err.err);
		}
		break;
	case MR_EVT_ARGS_PD_LBA:
		if (verbose) {
			printf("PD %s lba %lld: ",
			    pdrive_location(&detail->args.pd_lba.pd),
			    (long long)detail->args.pd_lba.lba);
		}
		break;
	case MR_EVT_ARGS_PD_LBA_LD:
		if (verbose) {
			printf("PD %s lba %lld VOL %s: ",
			    pdrive_location(&detail->args.pd_lba_ld.pd),
			    (long long)detail->args.pd_lba.lba,
			    volume_name(fd, &detail->args.pd_lba_ld.ld));
		}
		break;
	case MR_EVT_ARGS_PD_PROG:
		if (verbose) {
			printf("PD %s progress %d%% seconds %ds: ",
			    pdrive_location(&detail->args.pd_prog.pd),
			    detail->args.pd_prog.prog.progress/655,
			    detail->args.pd_prog.prog.elapsed_seconds);
		}
		break;
	case MR_EVT_ARGS_PD_STATE:
		if (verbose) {
			printf("PD %s state prior %s new %s: ",
			    pdrive_location(&detail->args.pd_prog.pd),
			    mfi_pdstate(detail->args.pd_state.prev_state),
			    mfi_pdstate(detail->args.pd_state.new_state));
		}
		break;
	case MR_EVT_ARGS_PCI:
		if (verbose) {
			printf("PCI 0x%04x 0x%04x 0x%04x 0x%04x: ",
			    detail->args.pci.venderId,
			    detail->args.pci.deviceId,
			    detail->args.pci.subVenderId,
			    detail->args.pci.subDeviceId);
		}
		break;
	case MR_EVT_ARGS_RATE:
		if (verbose) {
			printf("Rebuild rate %d: ", detail->args.rate);
		}
		break;
	case MR_EVT_ARGS_TIME:
		if (verbose) {
			printf("Adapter time %s; %d seconds since power on: ",
			    format_timestamp(detail->args.time.rtc),
			    detail->args.time.elapsedSeconds);
		}
		break;
	case MR_EVT_ARGS_ECC:
		if (verbose) {
			printf("Adapter ECC %x,%x: %s: ",
			    detail->args.ecc.ecar,
			    detail->args.ecc.elog,
			    detail->args.ecc.str);
		}
		break;
	default:
		if (verbose) {
			printf("Type %d: ", detail->arg_type);
		}
		break;
	}
	printf("%s\n", detail->description);
}

static int
show_events(int ac, char **av)
{
	struct mfi_evt_log_state info;
	struct mfi_evt_list *list;
	union mfi_evt filter;
	bool first;
	long val;
	char *cp;
	ssize_t size;
	uint32_t seq, start, stop;
	uint16_t locale;
	uint8_t status;
	int ch, error, fd, num_events, verbose;
	u_int i;

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (mfi_event_get_info(fd, &info, NULL) < 0) {
		error = errno;
		warn("Failed to get event log info");
		close(fd);
		return (error);
	}

	/* Default settings. */
	num_events = 15;
	filter.members.reserved = 0;
	filter.members.locale = MFI_EVT_LOCALE_ALL;
	filter.members.evt_class = MFI_EVT_CLASS_WARNING;
	start = info.boot_seq_num;
	stop = info.newest_seq_num;
	verbose = 0;

	/* Parse any options. */
	optind = 1;
	while ((ch = getopt(ac, av, "c:l:n:v")) != -1) {
		switch (ch) {
		case 'c':
			if (parse_class(optarg, &filter.members.evt_class) < 0) {
				error = errno;
				warn("Error parsing event class");
				close(fd);
				return (error);
			}
			break;
		case 'l':
			if (parse_locale(optarg, &locale) < 0) {
				error = errno;
				warn("Error parsing event locale");
				close(fd);
				return (error);
			}
			filter.members.locale = locale;
			break;
		case 'n':
			val = strtol(optarg, &cp, 0);
			if (*cp != '\0' || val <= 0) {
				warnx("Invalid event count");
				close(fd);
				return (EINVAL);
			}
			num_events = val;
			break;
		case 'v':
			verbose = 1;
			break;
		case '?':
		default:
			close(fd);
			return (EINVAL);
		}
	}
	ac -= optind;
	av += optind;

	/* Determine buffer size and validate it. */
	size = sizeof(struct mfi_evt_list) + sizeof(struct mfi_evt_detail) *
	    (num_events - 1);
	if (size > getpagesize()) {
		warnx("Event count is too high");
		close(fd);
		return (EINVAL);
	}

	/* Handle optional start and stop sequence numbers. */
	if (ac > 2) {
		warnx("show events: extra arguments");
		close(fd);
		return (EINVAL);
	}
	if (ac > 0 && parse_seq(&info, av[0], &start) < 0) {
		error = errno;
		warn("Error parsing starting sequence number");
		close(fd);
		return (error);
	}
	if (ac > 1 && parse_seq(&info, av[1], &stop) < 0) {
		error = errno;
		warn("Error parsing ending sequence number");
		close(fd);
		return (error);
	}

	list = malloc(size);
	if (list == NULL) {
		warnx("malloc failed");
		close(fd);
		return (ENOMEM);
	}
	first = true;
	seq = start;
	for (;;) {
		if (mfi_get_events(fd, list, num_events, filter, seq,
		    &status) < 0) {
			error = errno;
			warn("Failed to fetch events");
			free(list);
			close(fd);
			return (error);
		}
		if (status == MFI_STAT_NOT_FOUND) {
			break;
		}
		if (status != MFI_STAT_OK) {
			warnx("Error fetching events: %s", mfi_status(status));
			free(list);
			close(fd);
			return (EIO);
		}

		for (i = 0; i < list->count; i++) {
			/*
			 * If this event is newer than 'stop_seq' then
			 * break out of the loop.  Note that the log
			 * is a circular buffer so we have to handle
			 * the case that our stop point is earlier in
			 * the buffer than our start point.
			 */
			if (list->event[i].seq > stop) {
				if (start <= stop)
					goto finish;
				else if (list->event[i].seq < start)
					goto finish;
			}
			mfi_decode_evt(fd, &list->event[i], verbose);
			first = false;
		}

		/*
		 * XXX: If the event's seq # is the end of the buffer
		 * then this probably won't do the right thing.  We
		 * need to know the size of the buffer somehow.
		 */
		seq = list->event[list->count - 1].seq + 1;
			
	}
finish:
	if (first)
		warnx("No matching events found");

	free(list);
	close(fd);

	return (0);
}
MFI_COMMAND(show, events, show_events);
