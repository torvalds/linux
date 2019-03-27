/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
__RCSID("$FreeBSD$");

#include <sys/param.h>
#include <sys/errno.h>
#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "mptutil.h"

static CONFIG_PAGE_LOG_0 *
mpt_get_events(int fd, U16 *IOCStatus)
{

	return (mpt_read_extended_config_page(fd, MPI_CONFIG_EXTPAGETYPE_LOG,
	    0, 0, 0, IOCStatus));
}

/*
 *          1         2         3         4         5         6         7
 * 1234567890123456789012345678901234567890123456789012345678901234567890
 * < ID> < time > <ty> <X XX XX XX XX XX XX XX XX XX XX XX XX XX |..............|
 *  ID     Time   Type Log Data
 */
static void
mpt_print_event(MPI_LOG_0_ENTRY *entry, int verbose)
{
	int i;

	printf("%5d %7ds %4x ", entry->LogSequence, entry->TimeStamp,
	    entry->LogEntryQualifier);
	for (i = 0; i < 14; i++)
		printf("%02x ", entry->LogData[i]);
	printf("|");
	for (i = 0; i < 14; i++)
		printf("%c", isprint(entry->LogData[i]) ? entry->LogData[i] :
		    '.');
	printf("|\n");
	printf("                    ");
	for (i = 0; i < 14; i++)
		printf("%02x ", entry->LogData[i + 14]);
	printf("|");
	for (i = 0; i < 14; i++)
		printf("%c", isprint(entry->LogData[i + 14]) ?
		    entry->LogData[i + 14] : '.');
	printf("|\n");
}

static int
event_compare(const void *first, const void *second)
{
	MPI_LOG_0_ENTRY * const *one;
	MPI_LOG_0_ENTRY * const *two;

	one = first;
	two = second;
	return ((*one)->LogSequence - ((*two)->LogSequence));
}

static int
show_events(int ac, char **av)
{
	CONFIG_PAGE_LOG_0 *log;
	MPI_LOG_0_ENTRY **entries;
	int ch, error, fd, i, num_events, verbose;

	fd = mpt_open(mpt_unit);
	if (fd < 0) {
		error = errno;
		warn("mpt_open");
		return (error);
	}

	log = mpt_get_events(fd, NULL);
	if (log == NULL) {
		error = errno;
		warn("Failed to get event log info");
		return (error);
	}

	/* Default settings. */
	verbose = 0;

	/* Parse any options. */
	optind = 1;
	while ((ch = getopt(ac, av, "v")) != -1) {
		switch (ch) {
		case 'v':
			verbose = 1;
			break;
		case '?':
		default:
			free(log);
			close(fd);
			return (EINVAL);
		}
	}
	ac -= optind;
	av += optind;

	/* Build a list of valid entries and sort them by sequence. */
	entries = malloc(sizeof(MPI_LOG_0_ENTRY *) * log->NumLogEntries);
	if (entries == NULL) {
		free(log);
		close(fd);
		return (ENOMEM);
	}
	num_events = 0;
	for (i = 0; i < log->NumLogEntries; i++) {
		if (log->LogEntry[i].LogEntryQualifier ==
		    MPI_LOG_0_ENTRY_QUAL_ENTRY_UNUSED)
			continue;
		entries[num_events] = &log->LogEntry[i];
		num_events++;
	}

	qsort(entries, num_events, sizeof(MPI_LOG_0_ENTRY *), event_compare);

	if (num_events == 0)
		printf("Event log is empty\n");
	else {
		printf(" ID     Time   Type Log Data\n");
		for (i = 0; i < num_events; i++)
			mpt_print_event(entries[i], verbose);
	}
	
	free(entries);
	free(log);
	close(fd);

	return (0);
}
MPT_COMMAND(show, events, show_events);
