/*	$OpenBSD: config.h,v 1.23 2015/10/23 18:42:55 tedu Exp $	*/

/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1997,2000 by Internet Software Consortium, Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * these are site-dependent
 */

			/*
			 * choose one of these mailer commands.  some use
			 * /bin/mail for speed; it makes biff bark but doesn't
			 * do aliasing.  sendmail does do aliasing but is
			 * a hog for short messages.  aliasing is not needed
			 * if you make use of the MAILTO= feature in crontabs.
			 * (hint: MAILTO= was added for this reason).
			 */

#define MAILFMT "%s -FCronDaemon -odi -oem -oi -t"	/*-*/
			/* -Fx	 = set full-name of sender
			 * -odi	 = Option Deliverymode Interactive
			 * -oem	 = Option Errors Mailedtosender
			 * -oi   = Ignore "." alone on a line
			 * -t    = Get recipient from headers
			 */
#define MAILARG _PATH_SENDMAIL				/*-*/

			/* maximum load at which batch jobs will still run */
#define BATCH_MAXLOAD	1.5		/*-*/

			/* Define this to run crontab setgid instead of
			 * setuid root.  Group access will be used to read
			 * the tabs/atjobs dirs and the allow/deny files.
			 * If this is not defined then crontab and at
			 * must be setuid root.
			 */
#define CRON_GROUP	"crontab"	/*-*/
