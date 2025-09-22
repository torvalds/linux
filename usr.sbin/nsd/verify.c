/*
 * verify.c -- running verifiers and serving the zone to be verified.
 *
 * Copyright (c) 2012-2020, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif /* HAVE_SYSLOG_H */
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "region-allocator.h"
#include "namedb.h"
#include "nsd.h"
#include "options.h"
#include "difffile.h"
#include "verify.h"
#include "popen3.h"

struct zone *verify_next_zone(struct nsd *nsd, struct zone *zone)
{
	int verify;
	struct radnode *node;

	if(zone != NULL) {
		node = radix_next(zone->node);
	} else {
		node = radix_first(nsd->db->zonetree);
	}

	while(node != NULL) {
		zone = (struct zone *)node->elem;
		verify = zone->opts->pattern->verify_zone;
		if(verify == VERIFY_ZONE_INHERIT) {
			verify = nsd->options->verify_zones;
		}
		if(verify && zone->is_updated && !zone->is_checked) {
			return zone;
		}
		node = radix_next(node);
	}

	return NULL;
}

static inline ssize_t fill_buffer(struct verifier_stream *stream)
{
	ssize_t cnt = 0;

	assert(stream);
	assert(stream->fd != -1);
	assert(stream->cnt <= LOGBUFSIZE);
	assert(stream->off <= stream->cnt);

	// move data to start of buffer assuming all complete lines are printed
	if (stream->off) {
		size_t len = stream->cnt - stream->off;
		memmove(stream->buf, stream->buf + stream->off, len);
		stream->off = 0;
		stream->cnt = len;
		stream->buf[stream->cnt] = '\0'; // always null-terminate
	}

	// read data if space is available
	cnt = read(stream->fd, stream->buf + stream->cnt, LOGBUFSIZE - stream->cnt);
	if (cnt > 0)
		stream->cnt += (size_t)cnt;
	assert(stream->cnt <= LOGBUFSIZE);
	assert(stream->off <= stream->cnt);
	stream->buf[stream->cnt] = '\0'; // always null-terminate

	return cnt;
}

static inline size_t print_line(struct verifier_stream *stream, int eof)
{
	char *eol = NULL;
	size_t len;
	const char *fmt;

	if (stream->cnt == 0)
		return 0;
	assert(stream->off <= stream->cnt);
	if (stream->off == stream->cnt)
		return 0;

	// try to locate natural line break
	assert(stream->buf[stream->cnt] == '\0');
	if ((eol = strchr(stream->buf + stream->off, '\n'))) {
		len = eol - (stream->buf + stream->off);
	} else {
		len = stream->cnt - stream->off;
	}

	assert(len <= (stream->cnt - stream->off));
	// wait for buffer to contain a full line except on eof
	if (len < LOGLINELEN && !eol && !eof)
		return 0;

	if (len > LOGLINELEN) {
		fmt = stream->cut ? "verifier: .. %.*s .." : "verifier: %.*s ..";
		len = LOGLINELEN; // remainder printed next iteration
		stream->cut = 1;
	} else {
		fmt = stream->cut ? "verifier: .. %.*s" : "verifier: %.*s";
		stream->cut = 0;
	}
	log_msg(stream->priority, fmt, len, stream->buf + stream->off);

	stream->off += len + (eol != NULL);
	assert(stream->off <= stream->cnt);
	return len;
}

/*
 * Log verifier output on STDOUT and STDERR. Lines longer than LOGLINELEN are
 * split over multiple lines. Line-breaks are indicated in the log with "...".
 */
static void verify_handle_stream(int fd, short event, void *arg)
{
	int eof = 0;
	ssize_t cnt;
	struct verifier *verifier;
	struct verifier_stream *stream;

	assert((event & EV_READ));
	assert(arg != NULL);

	verifier = (struct verifier *)arg;
	if (fd == verifier->output_stream.fd) {
		stream = &verifier->output_stream;
	} else {
		assert(fd == verifier->error_stream.fd);
		stream = &verifier->error_stream;
	}

	assert(stream);
	assert(stream->fd != -1);

	do {
		cnt = fill_buffer(stream);
		eof = !cnt || (cnt < 0 && errno != EAGAIN && errno != EINTR);
		while (print_line(stream, eof)) ;
	} while (cnt > 0);

	if(eof) {
		event_del(&stream->event);
		close(stream->fd);
		stream->fd = -1;
	}
}

static void kill_verifier(struct verifier *verifier)
{
	assert(verifier != NULL);
	assert(verifier->zone != NULL);

	if(kill(verifier->pid, SIGTERM) == -1) {
		log_msg(LOG_ERR, "verify: cannot kill verifier for "
		                 "zone %s (pid %d): %s",
		                 verifier->zone->opts->name,
		                 verifier->pid,
		                 strerror(errno));
	}
}

static void close_stream(struct verifier *verifier, struct verifier_stream *stream)
{
	if (stream->fd == -1)
		return;
	verify_handle_stream(stream->fd, EV_READ, verifier);
	if (stream->fd == -1)
		return;
	event_del(&stream->event);
	close(stream->fd);
	stream->fd = -1;
}

static void close_verifier(struct verifier *verifier)
{
	/* unregister events and close streams (in that order) */
	if(verifier->timeout.tv_sec > 0) {
		event_del(&verifier->timeout_event);
		verifier->timeout.tv_sec = 0;
		verifier->timeout.tv_usec = 0;
	}

	if(verifier->zone_feed.fh != NULL) {
		event_del(&verifier->zone_feed.event);
		fclose(verifier->zone_feed.fh);
		verifier->zone_feed.fh = NULL;
		region_destroy(verifier->zone_feed.region);
	}

	close_stream(verifier, &verifier->error_stream);
	close_stream(verifier, &verifier->output_stream);

	verifier->zone->is_ok = verifier->was_ok;
	verifier->pid = -1;
	verifier->zone = NULL;
}

/*
 * Feed zone to verifier over STDIN as it becomes available.
 */
static void verify_handle_feed(int fd, short event, void *arg)
{
	struct verifier *verifier;
	struct rr *rr;

	(void)fd;
	assert(event == EV_WRITE);
	assert(arg != NULL);

	verifier = (struct verifier *)arg;
	if((rr = zone_rr_iter_next(&verifier->zone_feed.rriter)) != NULL) {
		print_rr(verifier->zone_feed.fh,
		         verifier->zone_feed.rrprinter,
		         rr,
		         verifier->zone_feed.region,
		         verifier->zone_feed.buffer);
	} else {
		event_del(&verifier->zone_feed.event);
		fclose(verifier->zone_feed.fh);
		verifier->zone_feed.fh = NULL;
		region_destroy(verifier->zone_feed.region);
	}
}

/*
 * This handler will be called when a verifier-timeout alarm goes off. It just
 * kills the verifier. server_verify_zones will make sure the zone will be
 * considered bad.
 */
void verify_handle_timeout(int fd, short event, void *arg)
{
	struct verifier *verifier;

	(void)fd;
	assert((event & EV_TIMEOUT));
	assert(arg != NULL);

	verifier = (struct verifier *)arg;
	verifier->zone->is_bad = 1;

	log_msg(LOG_ERR, "verify: verifier for zone %s (pid %d) timed out",
	                 verifier->zone->opts->name, verifier->pid);

	/* kill verifier, process reaped by exit handler */
	kill_verifier(verifier);
}

void verify_handle_signal(int sig, short event, void *arg)
{
	char buf[1] = { '\0' };
	struct nsd *nsd;

	assert(sig == SIGCHLD);
	assert((event & EV_SIGNAL));
	assert(arg != NULL);

	nsd = (struct nsd *)arg;
	if(write(nsd->verifier_pipe[1], buf, sizeof(buf)) == -1) {
		log_msg(LOG_ERR, "verify_handle_signal: write failed: %s",
				strerror(errno));
	}
}

/*
 * Reap process and update status of respective zone based on the exit code
 * of a verifier. Everything from STDOUT and STDERR still available is read and
 * written to the log as it might contain valuable information.
 *
 * NOTE: A timeout might have caused the verifier to be terminated.
 */
void verify_handle_exit(int fd, short event, void *arg)
{
	int wstatus;
	pid_t pid;
	struct nsd *nsd;
	char buf[1];

	assert((event & EV_READ));
	assert(arg != NULL);

	nsd = (struct nsd *)arg;

	if(read(fd, buf, sizeof(buf)) == -1) {
		if(errno != EAGAIN && errno != EINTR && errno != EWOULDBLOCK)
			log_msg(LOG_ERR, "verify_handle_exit: read failed: %s",
				strerror(errno));
	}

	while(((pid = waitpid(-1, &wstatus, WNOHANG)) == -1 && errno == EINTR)
	    || (pid > 0))
	{
		struct verifier *verifier = NULL;

		for(size_t i = 0; !verifier && i < nsd->verifier_limit; i++) {
			if(nsd->verifiers[i].zone != NULL &&
			   nsd->verifiers[i].pid == pid)
			{
				verifier = &nsd->verifiers[i];
			}
		}

		if(verifier == NULL) {
			continue;
		}

		if(!WIFEXITED(wstatus)) {
			log_msg(LOG_ERR, "verify: verifier for zone %s "
			                 "(pid %d) exited abnormally",
			                 verifier->zone->opts->name, pid);
		} else {
			int priority = LOG_INFO;
			int status = WEXITSTATUS(wstatus);
			if(status != 0) {
				priority = LOG_ERR;
				verifier->zone->is_bad = 1;
			}
			log_msg(priority, "verify: verifier for zone %s "
			                  "(pid %d) exited with %d",
			                  verifier->zone->opts->name, pid, status);
		}

		close_verifier(verifier);
		nsd->verifier_count--;
	}

	while(nsd->mode == NSD_RUN &&
	      nsd->verifier_count < nsd->verifier_limit &&
	      nsd->next_zone_to_verify != NULL)
	{
		verify_zone(nsd, nsd->next_zone_to_verify);
		nsd->next_zone_to_verify
			= verify_next_zone(nsd, nsd->next_zone_to_verify);
	}

	if(nsd->next_zone_to_verify == NULL && nsd->verifier_count == 0) {
		event_base_loopexit(nsd->event_base, NULL);
		return;
	}
}

/*
 * A parent may be terminated (by the NSD_QUIT signal (nsdc stop command)).
 * When a reload server process is running, the parent will then send a
 * NSD_QUIT command to that server. This handler makes sure that this command
 * is not neglected and that the reload server process will exit (gracefully).
 */
void
verify_handle_command(int fd, short event, void *arg)
{
	struct nsd *nsd = (struct nsd *)arg;
	int len;
	sig_atomic_t mode;

	assert(nsd != NULL);
	assert((event & (EV_READ
#ifdef EV_CLOSED
	| EV_CLOSED
#endif
	)));

	if((len = read(fd, &mode, sizeof(mode))) == -1) {
		log_msg(LOG_ERR, "verify: verify_handle_command: read: %s",
		                 strerror(errno));
		return;
	} else if(len == 0) {
		log_msg(LOG_INFO, "verify: command channel closed");
		mode = NSD_QUIT;
	} else if(mode != NSD_QUIT) {
		log_msg(LOG_ERR, "verify: bad command: %d", (int)mode);
		return;
	}

	nsd->mode = mode;

	if(nsd->verifier_count == 0) {
		event_base_loopexit(nsd->event_base, NULL);
		return; /* exit early if no verifiers are executing */
	}

	/* kill verifiers, processes reaped elsewhere */
	for(size_t i = 0; i < nsd->verifier_limit; i++) {
		if(nsd->verifiers[i].zone != NULL) {
			kill_verifier(&nsd->verifiers[i]);
		}
	}
}

/*
 * A verifier is executed for the specified zone (if a verifier is configured
 * and the zone has not been verified before). If one of the verifiers exits
 * with non-zero, the zone is marked bad and nsd drops the zone update and
 * reloads again.
 */
void verify_zone(struct nsd *nsd, struct zone *zone)
{
	struct verifier *verifier = NULL;
	int32_t timeout;
	char **command;
	FILE *fin;
	int fdin, fderr, fdout, flags;

	assert(nsd != NULL);
	assert(nsd->verifier_count < nsd->verifier_limit);
	assert(zone != NULL);

	fin = NULL;
	fdin = fdout = fderr = -1;

	/* search for available verifier slot */
	for(size_t i = 0; i < nsd->verifier_limit && !verifier; i++) {
		if(nsd->verifiers[i].zone == NULL) {
			verifier = &nsd->verifiers[i];
		}
	}

	assert(verifier != NULL);

	if(zone->opts->pattern->verifier != NULL) {
		command = zone->opts->pattern->verifier;
	} else if (nsd->options->verifier != NULL) {
		command = nsd->options->verifier;
	} else {
		log_msg(LOG_ERR, "verify: no verifier for zone %s",
		                 zone->opts->name);
		return;
	}

	if(zone->opts->pattern->verifier_timeout
		!= VERIFIER_TIMEOUT_INHERIT)
	{
		timeout = zone->opts->pattern->verifier_timeout;
	} else {
		timeout = nsd->options->verifier_timeout;
	}

	if(zone->opts->pattern->verifier_feed_zone
		!= VERIFIER_FEED_ZONE_INHERIT)
	{
		fdin = zone->opts->pattern->verifier_feed_zone ? -2 : -1;
	} else {
		fdin = nsd->options->verifier_feed_zone ? -2 : -1;
	}

	assert(timeout >= 0);

	setenv("VERIFY_ZONE", zone->opts->name, 1);
	setenv("VERIFY_ZONE_ON_STDIN", fdin == -2 ? "yes" : "no", 1);

	verifier->pid = popen3(
		command, fdin == -2 ? &fdin : NULL, &fdout, &fderr);
	if(verifier->pid == -1) {
		log_msg(LOG_ERR, "verify: could not start verifier for zone "
				 "%s: %s", zone->opts->name, strerror(errno));
		goto fail_popen3;
	}
	flags = fcntl(fderr, F_GETFL, 0);
	if (fcntl(fderr, F_SETFL, flags | O_NONBLOCK) == -1) {
		log_msg(LOG_ERR, "verify: fcntl(stderr, ..., O_NONBLOCK) for "
		                 "zone %s: %s",
		                 zone->opts->name, strerror(errno));
		goto fail_fcntl;
	}
	flags = fcntl(fdout, F_GETFL, 0);
	if(fcntl(fdout, F_SETFL, flags | O_NONBLOCK) == -1) {
		log_msg(LOG_ERR, "verify: fcntl(stdout, ..., O_NONBLOCK) for "
		                 "zone %s: %s",
		                 zone->opts->name, strerror(errno));
		goto fail_fcntl;
	}
	if (fdin >= 0) {
		if ((fin = fdopen(fdin, "w")) == NULL) {
			log_msg(LOG_ERR, "verify: fdopen(stdin, ...) for "
			                 "zone %s: %s",
		                         zone->opts->name, strerror(errno));
			goto fail_fcntl;
		}
		/* write unbuffered */
		setbuf(fin, NULL);
	}

	verifier->zone = zone;
	verifier->was_ok = zone->is_ok;

	unsetenv("VERIFY_ZONE");
	unsetenv("VERIFY_ZONE_ON_STDIN");

	verifier->error_stream.fd = fderr;
	verifier->error_stream.cnt = 0;
	verifier->error_stream.off = 0;
	verifier->error_stream.buf[0] = '\0';
	event_set(&verifier->error_stream.event,
	          verifier->error_stream.fd,
	          EV_READ|EV_PERSIST,
	          verify_handle_stream,
		  verifier);
	event_base_set(nsd->event_base, &verifier->error_stream.event);
	if(event_add(&verifier->error_stream.event, NULL) != 0) {
		log_msg(LOG_ERR, "verify: could not add error event for "
		                 "zone %s", zone->opts->name);
		goto fail_stderr;
	}

	verifier->output_stream.fd = fdout;
	verifier->output_stream.cnt = 0;
	verifier->output_stream.off = 0;
	verifier->output_stream.buf[0] = '\0';
	event_set(&verifier->output_stream.event,
	          verifier->output_stream.fd,
	          EV_READ|EV_PERSIST,
	          verify_handle_stream,
	          verifier);
	event_base_set(nsd->event_base, &verifier->output_stream.event);
	if(event_add(&verifier->output_stream.event, NULL) != 0) {
		log_msg(LOG_ERR, "verify: could not add output event for "
		                 "zone %s", zone->opts->name);
		goto fail_stdout;
	}

	if(fin != NULL) {
		verifier->zone_feed.fh = fin;

		zone_rr_iter_init(&verifier->zone_feed.rriter, zone);

		verifier->zone_feed.rrprinter
			= create_pretty_rr(nsd->server_region);
		verifier->zone_feed.region
			= region_create(xalloc, free);
		verifier->zone_feed.buffer
			= buffer_create(nsd->server_region, MAX_RDLENGTH);

		event_set(&verifier->zone_feed.event,
		          fileno(verifier->zone_feed.fh),
			  EV_WRITE|EV_PERSIST,
			  &verify_handle_feed,
			  verifier);
		event_base_set(nsd->event_base, &verifier->zone_feed.event);
		if(event_add(&verifier->zone_feed.event, NULL) != 0) {
			log_msg(LOG_ERR, "verify: could not add input event "
			                 "for zone %s", zone->opts->name);
			goto fail_stdin;
		}
	}

	if(timeout > 0) {
		verifier->timeout.tv_sec = timeout;
		verifier->timeout.tv_usec = 0;
		event_set(&verifier->timeout_event,
		          -1,
		          EV_TIMEOUT,
		          verify_handle_timeout,
		          verifier);
		event_base_set(nsd->event_base, &verifier->timeout_event);
		if(event_add(&verifier->timeout_event, &verifier->timeout) != 0) {
			log_msg(LOG_ERR, "verify: could not add timeout event "
			                 "for zone %s", zone->opts->name);
			goto fail_timeout;
		}

		log_msg(LOG_INFO, "verify: started verifier for zone %s "
		                  "(pid %d), timeout is %d seconds",
		                  zone->opts->name, verifier->pid, timeout);
	} else {
		log_msg(LOG_INFO, "verify: started verifier for zone %s "
		                  "(pid %d)", zone->opts->name, verifier->pid);
	}

	zone->is_ok = 1;
	nsd->verifier_count++;
	return;

fail_timeout:
	verifier->timeout.tv_sec = 0;
	verifier->timeout.tv_usec = 0;
	if(fin != NULL) {
		event_del(&verifier->zone_feed.event);
	}
fail_stdin:
	verifier->zone_feed.fh = NULL;
	event_del(&verifier->output_stream.event);
fail_stdout:
	verifier->output_stream.fd = -1;
	event_del(&verifier->error_stream.event);
fail_stderr:
	verifier->error_stream.fd = -1;
fail_fcntl:
	kill_verifier(verifier);
	if(fin != NULL) {
		fclose(fin);
	} else if (fdin >= 0) {
		close(fdin);
	}
	close(fdout);
	close(fderr);
fail_popen3:
	zone->is_bad = 1;
	verifier->pid = -1;
	verifier->zone = NULL;
}
