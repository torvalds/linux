/*
 * verify.h
 *
 * Copyright (c) 2020, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 */
#ifndef VERIFY_H
#define VERIFY_H

#ifndef USE_MINI_EVENT
#  ifdef HAVE_EVENT_H
#    include <event.h>
#  else
#    include <event2/event.h>
#    include "event2/event_struct.h"
#    include "event2/event_compat.h"
#  endif
#else
#  include "mini_event.h"
#endif

/*
 * Track position in zone to feed verifier more data as the input descriptor
 * becomes available.
 */
struct verifier_zone_feed {
	FILE *fh;
	struct event event;
	zone_rr_iter_type rriter;
	struct state_pretty_rr *rrprinter;
	struct region *region;
	struct buffer *buffer;
};

/* 40 is (estimated) space already used on each logline.
 * (time, pid, priority, etc)
 */
#define LOGLINELEN (MAXSYSLOGMSGLEN-40)

#define LOGBUFSIZE (LOGLINELEN * 2)

/*
 * STDOUT and STDERR are logged per line. Lines that exceed LOGLINELEN, are
 * split over multiple entries. Line breaks are indicated with "..." in the log
 * before and after the break.
 */
struct verifier_stream {
	int fd;
	struct event event;
	int priority;
	int cut;
	char buf[LOGBUFSIZE+1];
	size_t cnt;
	size_t off;
};

struct verifier {
	struct nsd *nsd;
	struct zone *zone;
	pid_t pid;
	int was_ok;
	struct timeval timeout;
	struct event timeout_event;
	struct verifier_zone_feed zone_feed;
	struct verifier_stream output_stream;
	struct verifier_stream error_stream;
};

struct zone *verify_next_zone(struct nsd *nsd, struct zone *zone);

void verify_zone(struct nsd *nsd, struct zone *zone);

void verify_handle_signal(int sig, short event, void *arg);

void verify_handle_exit(int fd, short event, void *arg);

void verify_handle_command(int fd, short event, void *arg);

#endif /* VERIFY_H */
