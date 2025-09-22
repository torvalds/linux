/*	$OpenBSD: grey.h,v 1.10 2013/08/21 16:13:29 millert Exp $	*/

/*
 * Copyright (c) 2004 Bob Beck.  All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define MAX_MAIL 1024 /* how big an email address will we consider */
#define PASSTIME (60 * 25) /* pass after first retry seen after 25 mins */
#define GREYEXP (60 * 60 * 4) /* remove grey entries after 4 hours */
#define WHITEEXP (60 * 60 * 24 * 36) /* remove white entries after 36 days */
#define TRAPEXP (60 * 60 * 24) /* hitting a spamtrap blacklists for a day */
#define PATH_PFCTL "/sbin/pfctl"
#define PATH_SPAMD_ALLOWEDDOMAINS "/etc/mail/spamd.alloweddomains"
#define DB_SCAN_INTERVAL 60
#define DB_TRAP_INTERVAL 60 * 10
#define PATH_SPAMD_DB "/var/db/spamd"

/* Obsolete grey data format. */
struct ogdata {
	int32_t first;  /* when did we see it first */
	int32_t pass;   /* when was it whitelisted */
	int32_t expire; /* when will we get rid of this entry */
	int bcount;     /* how many times have we blocked it */
	int pcount;     /* how many times passed, or -1 for spamtrap */
};

struct gdata {
	int64_t first;  /* when did we see it first */
	int64_t pass;   /* when was it whitelisted */
	int64_t expire; /* when will we get rid of this entry */
	int bcount;     /* how many times have we blocked it */
	int pcount;     /* how many times passed, or -1 for spamtrap */
};

extern int greywatcher(void);
extern int greyupdate(char *, char *, char *, char *, char *, int, char *);
extern int gdcopyin(const void *, struct gdata *);
