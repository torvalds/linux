/* $OpenBSD: log.h,v 1.25 2008/12/22 14:30:04 hshoexer Exp $	 */
/* $EOM: log.h,v 1.19 2000/03/30 14:27:23 ho Exp $	 */

/*
 * Copyright (c) 1998, 1999 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 2001, 2002, 2003 Håkan Olsson.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#ifndef _LOG_H_
#define _LOG_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <stdio.h>

extern int      verbose_logging;

/*
 * We cannot do the log strings dynamically sizeable as out of memory is one
 * of the situations we need to report about.
 */
#define LOG_SIZE	200

enum log_classes {
	LOG_MISC, LOG_TRANSPORT, LOG_MESSAGE, LOG_CRYPTO, LOG_TIMER, LOG_SYSDEP,
	LOG_SA, LOG_EXCHANGE, LOG_NEGOTIATION, LOG_POLICY, LOG_UI, LOG_ENDCLASS
};
#define LOG_CLASSES_TEXT \
  { "Misc", "Trpt", "Mesg", "Cryp", "Timr", "Sdep", "SA  ", "Exch", "Negt", \
    "Plcy", "UI  " }

/*
 * "Class" LOG_REPORT will always be logged to the current log channel,
 * regardless of level.
 */
#define LOG_PRINT  -1
#define LOG_REPORT -2

#define LOG_DBG(x)	log_debug x
#define LOG_DBG_BUF(x)	log_debug_buf x

extern void	log_debug(int, int, const char *,...)
		    __attribute__((__format__(__printf__, 3, 4)));
extern void     log_debug_buf(int, int, const char *, const u_int8_t *, size_t);
extern void     log_debug_cmd(int, int);
extern void     log_debug_toggle(void);

#define PCAP_FILE_DEFAULT "/var/run/isakmpd.pcap"
extern void     log_packet_init(char *);
extern void     log_packet_iov(struct sockaddr *, struct sockaddr *,
				                       struct iovec *, int);
extern void     log_packet_restart(char *);
extern void     log_packet_stop(void);

extern FILE    *log_current(void);
extern void	log_error(const char *,...)
		    __attribute__((__format__(__printf__, 1, 2)));
extern void	log_errorx(const char *,...)
		    __attribute__((__format__(__printf__, 1, 2)));
extern void     log_fatal(const char *,...)
		    __attribute__((__format__(__printf__, 1, 2))) __dead;
extern void     log_fatalx(const char *,...)
		    __attribute__((__format__(__printf__, 1, 2)));
extern void     log_print(const char *,...)
		    __attribute__((__format__(__printf__, 1, 2)));
extern void     log_verbose(const char *,...)
		    __attribute__((__format__(__printf__, 1, 2)));
extern void     log_to(FILE *);
extern void     log_init(int);
extern void     log_reinit(void);

#endif				/* _LOG_H_ */
