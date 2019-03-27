/*-
 * log.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: log.c,v 1.1 2004/01/07 23:15:00 max Exp $
 * $FreeBSD$
 */

#include <sys/types.h>
#include <stdarg.h>
#include <syslog.h>

void
log_open(char const *prog, int32_t log2stderr)
{
	openlog(prog, LOG_PID|LOG_NDELAY|(log2stderr? LOG_PERROR:0), LOG_USER);
}

void
log_close(void)
{
	closelog();
}

void
log_emerg(char const *message, ...)
{
	va_list	ap;

	va_start(ap, message);
	vsyslog(LOG_EMERG, message, ap);
	va_end(ap);
}

void
log_alert(char const *message, ...)
{
	va_list	ap;

	va_start(ap, message);
	vsyslog(LOG_ALERT, message, ap);
	va_end(ap);
}

void
log_crit(char const *message, ...)
{
	va_list	ap;

	va_start(ap, message);
	vsyslog(LOG_CRIT, message, ap);
	va_end(ap);
}

void
log_err(char const *message, ...)
{
	va_list	ap;

	va_start(ap, message);
	vsyslog(LOG_ERR, message, ap);
	va_end(ap);
}

void
log_warning(char const *message, ...)
{
	va_list	ap;

	va_start(ap, message);
	vsyslog(LOG_WARNING, message, ap);
	va_end(ap);
}

void
log_notice(char const *message, ...)
{
	va_list	ap;

	va_start(ap, message);
	vsyslog(LOG_NOTICE, message, ap);
	va_end(ap);
}

void
log_info(char const *message, ...)
{
	va_list	ap;

	va_start(ap, message);
	vsyslog(LOG_INFO, message, ap);
	va_end(ap);
}

void
log_debug(char const *message, ...)
{
	va_list	ap;

	va_start(ap, message);
	vsyslog(LOG_DEBUG, message, ap);
	va_end(ap);
}

