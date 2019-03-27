/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Brian Somers <brian@Awfulhak.org>
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

#define LogLOG		(0)
#define LogMIN		(1)
#define LogASYNC	(1)	/* syslog(LOG_INFO, ....)	 */
#define LogCBCP		(2)
#define LogCCP		(3)
#define LogCHAT		(4)
#define LogCOMMAND	(5)
#define LogCONNECT	(6)
#define LogDEBUG	(7)	/* syslog(LOG_DEBUG, ....)	 */
#define LogDNS		(8)
#define LogFILTER       (9)
#define LogHDLC		(10)
#define LogID0		(11)
#define LogIPCP		(12)
#define LogIPV6CP	(13)
#define LogLCP		(14)
#define LogLQM		(15)
#define LogPHASE	(16)
#define LogPHYSICAL	(17)	/* syslog(LOG_INFO, ....)	 */
#define LogRADIUS	(18)	/* syslog(LOG_INFO, ....)	 */
#define LogSYNC		(19)	/* syslog(LOG_INFO, ....)	 */
#define LogTCPIP	(20)
#define LogTIMER	(21)	/* syslog(LOG_DEBUG, ....)	 */
#define LogTUN		(22)	/* If set, tun%d is output with each message */
#define LogWARN		(23)	/* Sent to VarTerm else syslog(LOG_WARNING, ) */
#define LogERROR	(24)	/* syslog(LOG_ERR, ....), + sent to VarTerm */
#define LogALERT	(25)	/* syslog(LOG_ALERT, ....)	 */

#define LogMAXCONF	(22)
#define LogMAX		(25)

struct mbuf;
struct cmdargs;
struct prompt;
struct server;
struct datalink;

/* The first int arg for all of the following is one of the above values */
extern const char *log_Name(int);
extern void log_Keep(int);
extern void log_KeepLocal(int, u_long *);
extern void log_Discard(int);
extern void log_DiscardLocal(int, u_long *);
extern void log_DiscardAll(void);
extern void log_DiscardAllLocal(u_long *);
#define LOG_KEPT_SYSLOG (1)	/* Results of log_IsKept() */
#define LOG_KEPT_LOCAL  (2)	/* Results of log_IsKept() */
extern int log_IsKept(int);
extern int log_IsKeptLocal(int, u_long);
extern void log_Open(const char *);
extern void log_SetTun(int, const char *);
extern void log_Close(void);
#ifdef __GNUC__
extern void log_Printf(int, const char *,...)
            __attribute__ ((format (printf, 2, 3)));
extern void log_WritePrompts(struct datalink *, const char *, ...)
            __attribute__ ((format (printf, 2, 3)));
#else
extern void log_Printf(int, const char *,...);
extern void log_WritePrompts(struct datalink *, const char *, ...);
#endif
extern void log_DumpBp(int, const char *, const struct mbuf *);
extern void log_DumpBuff(int, const char *, const u_char *, int);
extern int log_ShowLevel(struct cmdargs const *);
extern int log_SetLevel(struct cmdargs const *);
extern int log_ShowWho(struct cmdargs const *);

extern struct prompt *log_PromptContext;
extern int log_PromptListChanged;
extern void log_RegisterPrompt(struct prompt *);
extern void log_UnRegisterPrompt(struct prompt *);
extern void log_DestroyPrompts(struct server *);
extern void log_DisplayPrompts(void);
extern void log_ActivatePrompt(struct prompt *);
extern void log_DeactivatePrompt(struct prompt *);
extern void log_SetTtyCommandMode(struct datalink *);
extern struct prompt *log_PromptList(void);
