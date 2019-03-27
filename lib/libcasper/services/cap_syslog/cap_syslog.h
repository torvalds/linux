/*-
 * Copyright (c) 2017 Mariusz Zaborski <oshogbo@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#ifndef	_CAP_SYSLOG_H_
#define	_CAP_SYSLOG_H_

#ifdef WITH_CASPER
void cap_syslog(cap_channel_t *chan, int pri,
    const char *fmt, ...) __printflike(3, 4);
void cap_vsyslog(cap_channel_t *chan, int priority, const char *fmt,
    va_list ap) __printflike(3, 0);

void cap_openlog(cap_channel_t *chan, const char *ident, int logopt,
    int facility);
void cap_closelog(cap_channel_t *chan);

int cap_setlogmask(cap_channel_t *chan, int maskpri);
#else
#define	cap_syslog(chan, pri, ...)	syslog(pri, __VA_ARGS__)
#define	cap_vsyslog(chan, pri, fmt, ap) vsyslog(pri, fmt, ap)

#define	cap_openlog(chan, ident, logopt, facility)				\
	openlog(ident, logopt, facility)
#define	cap_closelog(chan)				closelog()

#define	cap_setlogmask(chan, maskpri)			setlogmask(maskpri)
#endif /* !WITH_CASPER */

#endif	/* !_CAP_SYSLOG_H_ */
