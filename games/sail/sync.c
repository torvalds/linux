/*	$OpenBSD: sync.c,v 1.16 2019/06/28 13:32:52 deraadt Exp $	*/
/*	$NetBSD: sync.c,v 1.9 1998/08/30 09:19:40 veego Exp $	*/

/*
 * Copyright (c) 1983, 1993
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
 * 3. Neither the name of the University nor the names of its contributors
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

#include <sys/stat.h>

#include <errno.h>
#ifdef LOCK_EX
#include <fcntl.h>
#endif
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "extern.h"
#include "machdep.h"
#include "pathnames.h"
#include "player.h"

#define BUFSIZE 4096

static const char SF[] = _PATH_SYNC;
static const char LF[] = _PATH_LOCK;
static char sync_buf[BUFSIZE];
static char *sync_bp = sync_buf;
static char sync_lock[sizeof SF];
static char sync_file[sizeof LF];
static long sync_seek;
static FILE *sync_fp;

void
fmtship(char *buf, size_t len, const char *fmt, struct ship *ship)
{
	if (len == 0)
		abort();	/* XXX */

	while (*fmt && len > 1) {
		if (*fmt == '$' && fmt[1] == '$') {
			size_t l;
			snprintf(buf, len, "%s (%c%c)",
			    ship->shipname, colours(ship), sterncolour(ship));
			l = strlen(buf);
			buf += l;
			len -= l;
			fmt += 2;
		} else {
			*buf++ = *fmt++;
			len--;
		}
	}

	*buf = '\0';
}


void
makesignal(struct ship *from, const char *fmt, struct ship *ship, ...)
{
	char message[BUFSIZ];
	char format[BUFSIZ];
	va_list ap;

	va_start(ap, ship);
	fmtship(format, sizeof(format), fmt, ship);
	(void) vsnprintf(message, sizeof message, format, ap);
	va_end(ap);
	Writestr(W_SIGNAL, from, message);
}

void
makemsg(struct ship *from, const char *fmt, ...)
{
	char message[BUFSIZ];
	va_list ap;

	va_start(ap, fmt);
	(void) vsnprintf(message, sizeof message, fmt, ap);
	va_end(ap);
	Writestr(W_SIGNAL, from, message);
}

int
sync_exists(int game)
{
	char buf[sizeof sync_file];
	struct stat s;
	time_t t;

	(void) snprintf(buf, sizeof buf, SF, game);
	(void) time(&t);
	setegid(egid);
	if (stat(buf, &s) == -1) {
		setegid(gid);
		return 0;
	}
	if (s.st_mtime < t - 60*60*2) {		/* 2 hours */
		(void) unlink(buf);
		(void) snprintf(buf, sizeof buf, LF, game);
		(void) unlink(buf);
		setegid(gid);
		return 0;
	}
	setegid(gid);
	return 1;
}

int
sync_open(void)
{
	struct stat tmp;

	if (sync_fp != NULL)
		(void) fclose(sync_fp);
	(void) snprintf(sync_lock, sizeof sync_lock, LF, game);
	(void) snprintf(sync_file, sizeof sync_file, SF, game);
	setegid(egid);
	if (stat(sync_file, &tmp) == -1) {
		mode_t omask = umask(002);
		sync_fp = fopen(sync_file, "w+");
		(void) umask(omask);
	} else
		sync_fp = fopen(sync_file, "r+");
	setegid(gid);
	if (sync_fp == NULL)
		return -1;
	sync_seek = 0;
	return 0;
}

void
sync_close(int remove)
{
	if (sync_fp != 0)
		(void) fclose(sync_fp);
	if (remove) {
		setegid(egid);
		(void) unlink(sync_file);
		setegid(gid);
	}
}

void
Write(int type, struct ship *ship, long a, long b, long c, long d)
{
	(void) snprintf(sync_bp, sync_buf + sizeof sync_buf - sync_bp,
		"%d %d 0 %ld %ld %ld %ld\n",
	       type, ship->file->index, a, b, c, d);
	while (*sync_bp++)
		;
	sync_bp--;
	if (sync_bp >= &sync_buf[sizeof sync_buf])
		abort();
	(void) sync_update(type, ship, NULL, a, b, c, d);
}

void
Writestr(int type, struct ship *ship, const char *a)
{
	(void) snprintf(sync_bp, sync_buf + sizeof sync_buf - sync_bp,
		"%d %d 1 %s\n",
		type, ship->file->index, a);
	while (*sync_bp++)
		;
	sync_bp--;
	if (sync_bp >= &sync_buf[sizeof sync_buf])
		abort();
	(void) sync_update(type, ship, a, 0, 0, 0, 0);
}

int
Sync(void)
{
	sig_t sighup, sigint;
	int n;
	int type, shipnum, isstr;
	char *astr;
	long a, b, c, d;
	char buf[80];
	char erred = 0;

	sighup = signal(SIGHUP, SIG_IGN);
	sigint = signal(SIGINT, SIG_IGN);
	for (n = TIMEOUT; --n >= 0;) {
#ifdef LOCK_EX
		if (flock(fileno(sync_fp), LOCK_EX|LOCK_NB) >= 0)
			break;
		if (errno != EWOULDBLOCK)
			return -1;
#else
		setegid(egid);
		if (link(sync_file, sync_lock) >= 0) {
			setegid(gid);
			break;
		}
		setegid(gid);
		if (errno != EEXIST)
			return -1;
#endif
		sleep(1);
	}
	if (n <= 0)
		return -1;
	(void) fseek(sync_fp, sync_seek, SEEK_SET);
	for (;;) {
		switch (fscanf(sync_fp, "%d%d%d", &type, &shipnum, &isstr)) {
		case 3:
			break;
		case EOF:
			goto out;
		default:
			goto bad;
		}
		if (shipnum < 0 || shipnum >= cc->vessels)
			goto bad;
		if (isstr != 0 && isstr != 1)
			goto bad;
		if (isstr) {
			int ch;
			char *p;

			for (p = buf;;) {
				ch = getc(sync_fp);
				switch (ch) {
				case '\n':
				case EOF:
					break;
				default:
					if (p < buf + sizeof buf)
						*p++ = ch;
					continue;
				}
				break;
			}
			*p = 0;
			for (p = buf; *p == ' '; p++)
				;
			astr = p;
			a = b = c = d = 0;
		} else {
			if (fscanf(sync_fp, "%ld%ld%ld%ld", &a, &b, &c, &d) != 4)
				goto bad;
			astr = NULL;
		}
		if (sync_update(type, SHIP(shipnum), astr, a, b, c, d) < 0)
			goto bad;
	}
bad:
	erred++;
out:
	if (!erred && sync_bp != sync_buf) {
		(void) fseek(sync_fp, 0L, SEEK_END);
		(void) fwrite(sync_buf, sizeof *sync_buf, sync_bp - sync_buf,
			sync_fp);
		(void) fflush(sync_fp);
		sync_bp = sync_buf;
	}
	sync_seek = ftell(sync_fp);
#ifdef LOCK_EX
	(void) flock(fileno(sync_fp), LOCK_UN);
#else
	setegid(egid);
	(void) unlink(sync_lock);
	setegid(gid);
#endif
	(void) signal(SIGHUP, sighup);
	(void) signal(SIGINT, sigint);
	return erred ? -1 : 0;
}

int
sync_update(int type, struct ship *ship, const char *astr, long a, long b,
    long c, long d)
{
	switch (type) {
	case W_DBP: {
		struct BP *p = &ship->file->DBP[a];
		p->turnsent = b;
		p->toship = SHIP(c);
		p->mensent = d;
		break;
		}
	case W_OBP: {
		struct BP *p = &ship->file->OBP[a];
		p->turnsent = b;
		p->toship = SHIP(c);
		p->mensent = d;
		break;
		}
	case W_FOUL: {
		struct snag *p = &ship->file->foul[a];
		if (SHIP(a)->file->dir == 0)
			break;
		if (p->sn_count++ == 0)
			p->sn_turn = turn;
		ship->file->nfoul++;
		break;
		}
	case W_GRAP: {
		struct snag *p = &ship->file->grap[a];
		if (SHIP(a)->file->dir == 0)
			break;
		if (p->sn_count++ == 0)
			p->sn_turn = turn;
		ship->file->ngrap++;
		break;
		}
	case W_UNFOUL: {
		struct snag *p = &ship->file->foul[a];
		if (p->sn_count > 0) {
			if (b) {
				ship->file->nfoul -= p->sn_count;
				p->sn_count = 0;
			} else {
				ship->file->nfoul--;
				p->sn_count--;
			}
		}
		break;
		}
	case W_UNGRAP: {
		struct snag *p = &ship->file->grap[a];
		if (p->sn_count > 0) {
			if (b) {
				ship->file->ngrap -= p->sn_count;
				p->sn_count = 0;
			} else {
				ship->file->ngrap--;
				p->sn_count--;
			}
		}
		break;
		}
	case W_SIGNAL:
		if (mode == MODE_PLAYER) {
			if (nobells)
				Signal("$$: %s", ship, astr);
			else
				Signal("\7$$: %s", ship, astr);
		}
		break;
	case W_CREW: {
		struct shipspecs *s = ship->specs;
		s->crew1 = a;
		s->crew2 = b;
		s->crew3 = c;
		break;
		}
	case W_CAPTAIN:
		(void) strlcpy(ship->file->captain, astr,
			sizeof ship->file->captain);
		break;
	case W_CAPTURED:
		if (a < 0)
			ship->file->captured = 0;
		else
			ship->file->captured = SHIP(a);
		break;
	case W_CLASS:
		ship->specs->class = a;
		break;
	case W_DRIFT:
		ship->file->drift = a;
		break;
	case W_EXPLODE:
		if ((ship->file->explode = a) == 2)
			ship->file->dir = 0;
		break;
	case W_FS:
		ship->file->FS = a;
		break;
	case W_GUNL: {
		struct shipspecs *s = ship->specs;
		s->gunL = a;
		s->carL = b;
		break;
		}
	case W_GUNR: {
		struct shipspecs *s = ship->specs;
		s->gunR = a;
		s->carR = b;
		break;
		}
	case W_HULL:
		ship->specs->hull = a;
		break;
	case W_MOVE:
		(void) strlcpy(ship->file->movebuf, astr,
			sizeof ship->file->movebuf);
		break;
	case W_PCREW:
		ship->file->pcrew = a;
		break;
	case W_POINTS:
		ship->file->points = a;
		break;
	case W_QUAL:
		ship->specs->qual = a;
		break;
	case W_RIGG: {
		struct shipspecs *s = ship->specs;
		s->rig1 = a;
		s->rig2 = b;
		s->rig3 = c;
		s->rig4 = d;
		break;
		}
	case W_RIG1:
		ship->specs->rig1 = a;
		break;
	case W_RIG2:
		ship->specs->rig2 = a;
		break;
	case W_RIG3:
		ship->specs->rig3 = a;
		break;
	case W_RIG4:
		ship->specs->rig4 = a;
		break;
	case W_COL:
		ship->file->col = a;
		break;
	case W_DIR:
		ship->file->dir = a;
		break;
	case W_ROW:
		ship->file->row = a;
		break;
	case W_SINK:
		if ((ship->file->sink = a) == 2)
			ship->file->dir = 0;
		break;
	case W_STRUCK:
		ship->file->struck = a;
		break;
	case W_TA:
		ship->specs->ta = a;
		break;
	case W_ALIVE:
		alive = 1;
		break;
	case W_TURN:
		turn = a;
		break;
	case W_WIND:
		winddir = a;
		windspeed = b;
		break;
	case W_BEGIN:
		(void) strlcpy(ship->file->captain, "begin",
		    sizeof ship->file->captain);
		people++;
		break;
	case W_END:
		*ship->file->captain = 0;
		ship->file->points = 0;
		people--;
		break;
	case W_DDEAD:
		hasdriver = 0;
		break;
	default:
		fprintf(stderr, "sync_update: unknown type %d\r\n", type);
		return -1;
	}
	return 0;
}
