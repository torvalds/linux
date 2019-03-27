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

#include <sys/types.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>

#include "defs.h"
#include "command.h"
#include "mbuf.h"
#include "log.h"
#include "descriptor.h"
#include "prompt.h"

static const char *const LogNames[] = {
  "Async",
  "CBCP",
  "CCP",
  "Chat",
  "Command",
  "Connect",
  "Debug",
  "DNS",
  "Filter",			/* Log discarded packets */
  "HDLC",
  "ID0",
  "IPCP",
  "IPV6CP",
  "LCP",
  "LQM",
  "Phase",
  "Physical",
  "Radius",
  "Sync",
  "TCP/IP",
  "Timer",
  "Tun",
  "Warning",
  "Error",
  "Alert"
};

#define MSK(n) (1<<((n)-1))

static u_long LogMask = MSK(LogPHASE);
static u_long LogMaskLocal = MSK(LogERROR) | MSK(LogALERT) | MSK(LogWARN);
static int LogTunno = -1;
static const char *LogIfaceName;
static struct prompt *promptlist;	/* Where to log local stuff */
struct prompt *log_PromptContext;
int log_PromptListChanged;

struct prompt *
log_PromptList()
{
  return promptlist;
}

void
log_RegisterPrompt(struct prompt *prompt)
{
  prompt->next = promptlist;
  promptlist = prompt;
  prompt->active = 1;
  log_DiscardAllLocal(&prompt->logmask);
}

void
log_ActivatePrompt(struct prompt *prompt)
{
  prompt->active = 1;
  LogMaskLocal |= prompt->logmask;
}

static void
LogSetMaskLocal(void)
{
  struct prompt *p;

  LogMaskLocal = MSK(LogERROR) | MSK(LogALERT) | MSK(LogWARN);
  for (p = promptlist; p; p = p->next)
    LogMaskLocal |= p->logmask;
}

void
log_DeactivatePrompt(struct prompt *prompt)
{
  if (prompt->active) {
    prompt->active = 0;
    LogSetMaskLocal();
  }
}

void
log_UnRegisterPrompt(struct prompt *prompt)
{
  if (prompt) {
    struct prompt **p;

    for (p = &promptlist; *p; p = &(*p)->next)
      if (*p == prompt) {
        *p = prompt->next;
        prompt->next = NULL;
        break;
      }
    LogSetMaskLocal();
    log_PromptListChanged++;
  }
}

void
log_DestroyPrompts(struct server *s)
{
  struct prompt *p, *pn, *pl;

  p = promptlist;
  pl = NULL;
  while (p) {
    pn = p->next;
    if (s && p->owner == s) {
      if (pl)
        pl->next = p->next;
      else
        promptlist = p->next;
      p->next = NULL;
      prompt_Destroy(p, 1);
    } else
      pl = p;
    p = pn;
  }
}

void
log_DisplayPrompts()
{
  struct prompt *p;

  for (p = promptlist; p; p = p->next)
    prompt_Required(p);
}

void
log_WritePrompts(struct datalink *dl, const char *fmt,...)
{
  va_list ap;
  struct prompt *p;

  va_start(ap, fmt);
  for (p = promptlist; p; p = p->next)
    if (prompt_IsTermMode(p, dl))
      prompt_vPrintf(p, fmt, ap);
  va_end(ap);
}

void
log_SetTtyCommandMode(struct datalink *dl)
{
  struct prompt *p;

  for (p = promptlist; p; p = p->next)
    if (prompt_IsTermMode(p, dl))
      prompt_TtyCommandMode(p);
}

static int
syslogLevel(int lev)
{
  switch (lev) {
  case LogLOG:
    return LOG_INFO;
  case LogDEBUG:
  case LogTIMER:
    return LOG_DEBUG;
  case LogWARN:
    return LOG_WARNING;
  case LogERROR:
    return LOG_ERR;
  case LogALERT:
    return LOG_ALERT;
  }
  return lev >= LogMIN && lev <= LogMAX ? LOG_INFO : 0;
}

const char *
log_Name(int id)
{
  if (id == LogLOG)
    return "LOG";
  return id < LogMIN || id > LogMAX ? "Unknown" : LogNames[id - 1];
}

void
log_Keep(int id)
{
  if (id >= LogMIN && id <= LogMAXCONF)
    LogMask |= MSK(id);
}

void
log_KeepLocal(int id, u_long *mask)
{
  if (id >= LogMIN && id <= LogMAXCONF) {
    LogMaskLocal |= MSK(id);
    *mask |= MSK(id);
  }
}

void
log_Discard(int id)
{
  if (id >= LogMIN && id <= LogMAXCONF)
    LogMask &= ~MSK(id);
}

void
log_DiscardLocal(int id, u_long *mask)
{
  if (id >= LogMIN && id <= LogMAXCONF) {
    *mask &= ~MSK(id);
    LogSetMaskLocal();
  }
}

void
log_DiscardAll()
{
  LogMask = 0;
}

void
log_DiscardAllLocal(u_long *mask)
{
  *mask = MSK(LogERROR) | MSK(LogALERT) | MSK(LogWARN);
  LogSetMaskLocal();
}

int
log_IsKept(int id)
{
  if (id == LogLOG)
    return LOG_KEPT_SYSLOG;
  if (id < LogMIN || id > LogMAX)
    return 0;
  if (id > LogMAXCONF)
    return LOG_KEPT_LOCAL | LOG_KEPT_SYSLOG;

  return ((LogMaskLocal & MSK(id)) ? LOG_KEPT_LOCAL : 0) |
    ((LogMask & MSK(id)) ? LOG_KEPT_SYSLOG : 0);
}

int
log_IsKeptLocal(int id, u_long mask)
{
  if (id < LogMIN || id > LogMAX)
    return 0;
  if (id > LogMAXCONF)
    return LOG_KEPT_LOCAL | LOG_KEPT_SYSLOG;

  return ((mask & MSK(id)) ? LOG_KEPT_LOCAL : 0) |
    ((LogMask & MSK(id)) ? LOG_KEPT_SYSLOG : 0);
}

void
log_Open(const char *Name)
{
  openlog(Name, LOG_PID, LOG_DAEMON);
}

void
log_SetTun(int tunno, const char *ifaceName)
{
  LogTunno = tunno;
  LogIfaceName = ifaceName;
}

void
log_Close()
{
  closelog();
  LogTunno = -1;
  LogIfaceName = NULL;
}

void
log_Printf(int lev, const char *fmt,...)
{
  va_list ap;
  struct prompt *prompt;

  if (log_IsKept(lev)) {
    char nfmt[200];

    va_start(ap, fmt);
    if (promptlist && (log_IsKept(lev) & LOG_KEPT_LOCAL)) {
      if ((log_IsKept(LogTUN) & LOG_KEPT_LOCAL) && LogTunno != -1) {
        if (LogIfaceName)
          snprintf(nfmt, sizeof nfmt, "%s%d(%s): %s: %s", TUN_NAME,
	         LogTunno, LogIfaceName, log_Name(lev), fmt);
        else
          snprintf(nfmt, sizeof nfmt, "%s%d: %s: %s", TUN_NAME,
	         LogTunno, log_Name(lev), fmt);
      } else
        snprintf(nfmt, sizeof nfmt, "%s: %s", log_Name(lev), fmt);

      if (log_PromptContext && lev == LogWARN)
        /* Warnings just go to the current prompt */
        prompt_vPrintf(log_PromptContext, nfmt, ap);
      else for (prompt = promptlist; prompt; prompt = prompt->next)
        if (lev > LogMAXCONF || (prompt->logmask & MSK(lev)))
          prompt_vPrintf(prompt, nfmt, ap);
    }
    va_end(ap);

    va_start(ap, fmt);
    if ((log_IsKept(lev) & LOG_KEPT_SYSLOG) &&
        (lev != LogWARN || !log_PromptContext)) {
      if ((log_IsKept(LogTUN) & LOG_KEPT_SYSLOG) && LogTunno != -1) {
        if (LogIfaceName)
          snprintf(nfmt, sizeof nfmt, "%s%d(%s): %s: %s", TUN_NAME,
	         LogTunno, LogIfaceName, log_Name(lev), fmt);
        else
          snprintf(nfmt, sizeof nfmt, "%s%d: %s: %s", TUN_NAME,
	         LogTunno, log_Name(lev), fmt);
      } else
        snprintf(nfmt, sizeof nfmt, "%s: %s", log_Name(lev), fmt);
      vsyslog(syslogLevel(lev), nfmt, ap);
    }
    va_end(ap);
  }
}

void
log_DumpBp(int lev, const char *hdr, const struct mbuf *bp)
{
  if (log_IsKept(lev)) {
    char buf[68];
    char *b, *c;
    const u_char *ptr;
    int f;

    if (hdr && *hdr)
      log_Printf(lev, "%s\n", hdr);

    b = buf;
    c = b + 50;
    do {
      f = bp->m_len;
      ptr = CONST_MBUF_CTOP(bp);
      while (f--) {
	sprintf(b, " %02x", (int) *ptr);
        *c++ = isprint(*ptr) ? *ptr : '.';
        ptr++;
        b += 3;
        if (b == buf + 48) {
          memset(b, ' ', 2);
          *c = '\0';
          log_Printf(lev, "%s\n", buf);
          b = buf;
          c = b + 50;
        }
      }
    } while ((bp = bp->m_next) != NULL);

    if (b > buf) {
      memset(b, ' ', 50 - (b - buf));
      *c = '\0';
      log_Printf(lev, "%s\n", buf);
    }
  }
}

void
log_DumpBuff(int lev, const char *hdr, const u_char *ptr, int n)
{
  if (log_IsKept(lev)) {
    char buf[68];
    char *b, *c;

    if (hdr && *hdr)
      log_Printf(lev, "%s\n", hdr);
    while (n > 0) {
      b = buf;
      c = b + 50;
      for (b = buf; b != buf + 48 && n--; b += 3, ptr++) {
	sprintf(b, " %02x", (int) *ptr);
        *c++ = isprint(*ptr) ? *ptr : '.';
      }
      memset(b, ' ', 50 - (b - buf));
      *c = '\0';
      log_Printf(lev, "%s\n", buf);
    }
  }
}

int
log_ShowLevel(struct cmdargs const *arg)
{
  int i;

  prompt_Printf(arg->prompt, "Log:  ");
  for (i = LogMIN; i <= LogMAX; i++)
    if (log_IsKept(i) & LOG_KEPT_SYSLOG)
      prompt_Printf(arg->prompt, " %s", log_Name(i));

  prompt_Printf(arg->prompt, "\nLocal:");
  for (i = LogMIN; i <= LogMAX; i++)
    if (log_IsKeptLocal(i, arg->prompt->logmask) & LOG_KEPT_LOCAL)
      prompt_Printf(arg->prompt, " %s", log_Name(i));

  prompt_Printf(arg->prompt, "\n");

  return 0;
}

int
log_SetLevel(struct cmdargs const *arg)
{
  int i, res, argc, local;
  char const *const *argv, *argp;

  argc = arg->argc - arg->argn;
  argv = arg->argv + arg->argn;
  res = 0;

  if (argc == 0 || strcasecmp(argv[0], "local"))
    local = 0;
  else {
    if (arg->prompt == NULL) {
      log_Printf(LogWARN, "set log local: Only available on the"
                 " command line\n");
      return 1;
    }
    argc--;
    argv++;
    local = 1;
  }

  if (argc == 0 || (argv[0][0] != '+' && argv[0][0] != '-')) {
    if (local)
      log_DiscardAllLocal(&arg->prompt->logmask);
    else
      log_DiscardAll();
  }

  while (argc--) {
    argp = **argv == '+' || **argv == '-' ? *argv + 1 : *argv;
    /* Special case 'all' */
    if (strcasecmp(argp, "all") == 0) {
        if (**argv == '-') {
          if (local)
            for (i = LogMIN; i <= LogMAX; i++)
              log_DiscardLocal(i, &arg->prompt->logmask);
          else
            for (i = LogMIN; i <= LogMAX; i++)
              log_Discard(i);
        } else if (local)
          for (i = LogMIN; i <= LogMAX; i++)
            log_KeepLocal(i, &arg->prompt->logmask);
        else
          for (i = LogMIN; i <= LogMAX; i++)
            log_Keep(i);
        argv++;
        continue;
    }
    for (i = LogMIN; i <= LogMAX; i++)
      if (strcasecmp(argp, log_Name(i)) == 0) {
	if (**argv == '-') {
          if (local)
            log_DiscardLocal(i, &arg->prompt->logmask);
          else
	    log_Discard(i);
	} else if (local)
          log_KeepLocal(i, &arg->prompt->logmask);
        else
          log_Keep(i);
	break;
      }
    if (i > LogMAX) {
      log_Printf(LogWARN, "%s: Invalid log value\n", argp);
      res = -1;
    }
    argv++;
  }
  return res;
}

int
log_ShowWho(struct cmdargs const *arg)
{
  struct prompt *p;

  for (p = promptlist; p; p = p->next) {
    prompt_Printf(arg->prompt, "%s (%s)", p->src.type, p->src.from);
    if (p == arg->prompt)
      prompt_Printf(arg->prompt, " *");
    if (!p->active)
      prompt_Printf(arg->prompt, " ^Z");
    prompt_Printf(arg->prompt, "\n");
  }

  return 0;
}
