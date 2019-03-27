/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Brian Somers <brian@Awfulhak.org>
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

#include <sys/param.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "layer.h"
#include "mbuf.h"
#include "log.h"
#include "defs.h"
#include "timer.h"
#include "lqr.h"
#include "hdlc.h"
#include "throughput.h"
#include "fsm.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "async.h"
#include "descriptor.h"
#include "physical.h"
#include "chat.h"
#include "mp.h"
#include "auth.h"
#include "chap.h"
#include "slcompress.h"
#include "iplist.h"
#include "ncpaddr.h"
#include "ipcp.h"
#include "filter.h"
#include "cbcp.h"
#include "command.h"
#include "datalink.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"
#include "id.h"

#define BUFLEFT(c) (sizeof (c)->buf - ((c)->bufend - (c)->buf))

static void ExecStr(struct physical *, char *, char *, int);
static char *ExpandString(struct chat *, const char *, char *, int, int);

static void
chat_PauseTimer(void *v)
{
  struct chat *c = (struct chat *)v;
  timer_Stop(&c->pause);
  c->pause.load = 0;
}

static void
chat_Pause(struct chat *c, u_long load)
{
  timer_Stop(&c->pause);
  c->pause.load += load;
  c->pause.func = chat_PauseTimer;
  c->pause.name = "chat pause";
  c->pause.arg = c;
  timer_Start(&c->pause);
}

static void
chat_TimeoutTimer(void *v)
{
  struct chat *c = (struct chat *)v;
  timer_Stop(&c->timeout);
  c->TimedOut = 1;
}

static void
chat_SetTimeout(struct chat *c)
{
  timer_Stop(&c->timeout);
  if (c->TimeoutSec > 0) {
    c->timeout.load = SECTICKS * c->TimeoutSec;
    c->timeout.func = chat_TimeoutTimer;
    c->timeout.name = "chat timeout";
    c->timeout.arg = c;
    timer_Start(&c->timeout);
  }
}

static char *
chat_NextChar(char *ptr, char ch)
{
  for (; *ptr; ptr++)
    if (*ptr == ch)
      return ptr;
    else if (*ptr == '\\')
      if (*++ptr == '\0')
        return NULL;

  return NULL;
}

static int
chat_UpdateSet(struct fdescriptor *d, fd_set *r, fd_set *w, fd_set *e, int *n)
{
  struct chat *c = descriptor2chat(d);
  int special, gotabort, gottimeout, needcr;
  int TimedOut = c->TimedOut;
  static char arg_term;		/* An empty string */

  if (c->pause.state == TIMER_RUNNING)
    return 0;

  if (TimedOut) {
    log_Printf(LogCHAT, "Expect timeout\n");
    if (c->nargptr == NULL)
      c->state = CHAT_FAILED;
    else {
      /* c->state = CHAT_EXPECT; */
      c->argptr = &arg_term;
      /*
	We have to clear the input buffer, because it contains output
	from the previous (timed out) command.
      */
      c->bufstart = c->bufend;
    }
    c->TimedOut = 0;
  }

  if (c->state != CHAT_EXPECT && c->state != CHAT_SEND)
    return 0;

  gottimeout = gotabort = 0;

  if (c->arg < c->argc && (c->arg < 0 || *c->argptr == '\0')) {
    /* Go get the next string */
    if (c->arg < 0 || c->state == CHAT_SEND)
      c->state = CHAT_EXPECT;
    else
      c->state = CHAT_SEND;

    special = 1;
    while (special && (c->nargptr || c->arg < c->argc - 1)) {
      if (c->arg < 0 || (!TimedOut && c->state == CHAT_SEND))
        c->nargptr = NULL;

      if (c->nargptr != NULL) {
        /* We're doing expect-send-expect.... */
        c->argptr = c->nargptr;
        /* Put the '-' back in case we ever want to rerun our script */
        c->nargptr[-1] = '-';
        c->nargptr = chat_NextChar(c->nargptr, '-');
        if (c->nargptr != NULL)
          *c->nargptr++ = '\0';
      } else {
        int minus;

        if ((c->argptr = c->argv[++c->arg]) == NULL) {
          /* End of script - all ok */
          c->state = CHAT_DONE;
          return 0;
        }

        if (c->state == CHAT_EXPECT) {
          /* Look for expect-send-expect sequence */
          c->nargptr = c->argptr;
          minus = 0;
          while ((c->nargptr = chat_NextChar(c->nargptr, '-'))) {
            c->nargptr++;
            minus++;
          }

          if (minus % 2)
            log_Printf(LogWARN, "chat_UpdateSet: \"%s\": Uneven number of"
                      " '-' chars, all ignored\n", c->argptr);
          else if (minus) {
            c->nargptr = chat_NextChar(c->argptr, '-');
            *c->nargptr++ = '\0';
          }
        }
      }

      /*
       * c->argptr now temporarily points into c->script (via c->argv)
       * If it's an expect-send-expect sequence, we've just got the correct
       * portion of that sequence.
       */

      needcr = c->state == CHAT_SEND &&
               (*c->argptr != '!' || c->argptr[1] == '!');

      /* We leave room for a potential HDLC header in the target string */
      ExpandString(c, c->argptr, c->exp + 2, sizeof c->exp - 2, needcr);

      /*
       * Now read our string.  If it's not a special string, we unset
       * ``special'' to break out of the loop.
       */
      if (gotabort) {
        if (c->abort.num < MAXABORTS) {
          int len, i;

          len = strlen(c->exp+2);
          for (i = 0; i < c->abort.num; i++)
            if (len > c->abort.string[i].len) {
              int last;

              for (last = c->abort.num; last > i; last--) {
                c->abort.string[last].data = c->abort.string[last-1].data;
                c->abort.string[last].len = c->abort.string[last-1].len;
              }
              break;
            }
          c->abort.string[i].len = len;
          if ((c->abort.string[i].data = (char *)malloc(len+1)) != NULL) {
            memcpy(c->abort.string[i].data, c->exp+2, len+1);
            c->abort.num++;
	  }
        } else
          log_Printf(LogERROR, "chat_UpdateSet: too many abort strings\n");
        gotabort = 0;
      } else if (gottimeout) {
        c->TimeoutSec = atoi(c->exp + 2);
        if (c->TimeoutSec <= 0)
          c->TimeoutSec = 30;
        gottimeout = 0;
      } else if (c->nargptr == NULL && !strcmp(c->exp+2, "ABORT"))
        gotabort = 1;
      else if (c->nargptr == NULL && !strcmp(c->exp+2, "TIMEOUT"))
        gottimeout = 1;
      else {
        if (c->exp[2] == '!' && c->exp[3] != '!')
          ExecStr(c->physical, c->exp + 3, c->exp + 3, sizeof c->exp - 3);

        if (c->exp[2] == '\0') {
          /* Empty string, reparse (this may be better as a `goto start') */
          c->argptr = &arg_term;
          return chat_UpdateSet(d, r, w, e, n);
        }

        special = 0;
      }
    }

    if (special) {
      if (gottimeout)
        log_Printf(LogWARN, "chat_UpdateSet: TIMEOUT: Argument expected\n");
      else if (gotabort)
        log_Printf(LogWARN, "chat_UpdateSet: ABORT: Argument expected\n");

      /* End of script - all ok */
      c->state = CHAT_DONE;
      return 0;
    }

    /* set c->argptr to point in the right place */
    c->argptr = c->exp + (c->exp[2] == '!' ? 3 : 2);
    c->arglen = strlen(c->argptr);

    if (c->state == CHAT_EXPECT) {
      /* We must check to see if the string's already been found ! */
      char *begin, *end;

      end = c->bufend - c->arglen + 1;
      if (end < c->bufstart)
        end = c->bufstart;
      for (begin = c->bufstart; begin < end; begin++)
        if (!strncmp(begin, c->argptr, c->arglen)) {
          c->bufstart = begin + c->arglen;
          c->argptr += c->arglen;
          c->arglen = 0;
          /* Continue - we've already read our expect string */
          return chat_UpdateSet(d, r, w, e, n);
        }

      log_Printf(LogCHAT, "Expect(%d): %s\n", c->TimeoutSec, c->argptr);
      chat_SetTimeout(c);
    }
  }

  /*
   * We now have c->argptr pointing at what we want to expect/send and
   * c->state saying what we want to do... we now know what to put in
   * the fd_set :-)
   */

  if (c->state == CHAT_EXPECT)
    return physical_doUpdateSet(&c->physical->desc, r, NULL, e, n, 1);
  else
    return physical_doUpdateSet(&c->physical->desc, NULL, w, e, n, 1);
}

static int
chat_IsSet(struct fdescriptor *d, const fd_set *fdset)
{
  struct chat *c = descriptor2chat(d);
  return c->argptr && physical_IsSet(&c->physical->desc, fdset);
}

static void
chat_UpdateLog(struct chat *c, int in)
{
  if (log_IsKept(LogCHAT) || log_IsKept(LogCONNECT)) {
    /*
     * If a linefeed appears in the last `in' characters of `c's input
     * buffer, output from there, all the way back to the last linefeed.
     * This is called for every read of `in' bytes.
     */
    char *ptr, *end, *stop, ch;
    int level;

    level = log_IsKept(LogCHAT) ? LogCHAT : LogCONNECT;
    if (in == -1)
      end = ptr = c->bufend;
    else {
      ptr = c->bufend - in;
      for (end = c->bufend - 1; end >= ptr; end--)
        if (*end == '\n')
          break;
    }

    if (end >= ptr) {
      for (ptr = c->bufend - (in == -1 ? 1 : in + 1); ptr >= c->bufstart; ptr--)
        if (*ptr == '\n')
          break;
      ptr++;
      stop = NULL;
      while (stop < end) {
        if ((stop = memchr(ptr, '\n', end - ptr)) == NULL)
          stop = end;
        ch = *stop;
        *stop = '\0';
        if (level == LogCHAT || strstr(ptr, "CONNECT"))
          log_Printf(level, "Received: %s\n", ptr);
        *stop = ch;
        ptr = stop + 1;
      }
    }
  }
}

static void
chat_Read(struct fdescriptor *d, struct bundle *bundle __unused,
	  const fd_set *fdset __unused)
{
  struct chat *c = descriptor2chat(d);

  if (c->state == CHAT_EXPECT) {
    ssize_t in;
    char *abegin, *ebegin, *begin, *aend, *eend, *end;
    int n;

    /*
     * XXX - should this read only 1 byte to guarantee that we don't
     * swallow any ppp talk from the peer ?
     */
    in = BUFLEFT(c);
    if (in > (ssize_t)sizeof c->buf / 2)
      in = sizeof c->buf / 2;

    in = physical_Read(c->physical, c->bufend, in);
    if (in <= 0)
      return;

    /* `begin' and `end' delimit where we're going to strncmp() from */
    ebegin = c->bufend - c->arglen + 1;
    eend = ebegin + in;
    if (ebegin < c->bufstart)
      ebegin = c->bufstart;

    if (c->abort.num) {
      abegin = c->bufend - c->abort.string[0].len + 1;
      aend = c->bufend - c->abort.string[c->abort.num-1].len + in + 1;
      if (abegin < c->bufstart)
        abegin = c->bufstart;
    } else {
      abegin = ebegin;
      aend = eend;
    }
    begin = abegin < ebegin ? abegin : ebegin;
    end = aend < eend ? eend : aend;

    c->bufend += in;

    chat_UpdateLog(c, in);

    if (c->bufend > c->buf + sizeof c->buf / 2) {
      /* Shuffle our receive buffer back a bit */
      int chop;

      for (chop = begin - c->buf; chop; chop--)
        if (c->buf[chop] == '\n')
          /* found some already-logged garbage to remove :-) */
          break;

      if (!chop)
        chop = begin - c->buf;

      if (chop) {
        char *from, *to;

        to = c->buf;
        from = to + chop;
        while (from < c->bufend)
          *to++ = *from++;
        c->bufstart -= chop;
        c->bufend -= chop;
        begin -= chop;
        end -= chop;
        abegin -= chop;
        aend -= chop;
        ebegin -= chop;
        eend -= chop;
      }
    }

    for (; begin < end; begin++)
      if (begin >= ebegin && begin < eend &&
          !strncmp(begin, c->argptr, c->arglen)) {
        /* Got it ! */
        timer_Stop(&c->timeout);
        if (memchr(begin + c->arglen - 1, '\n',
            c->bufend - begin - c->arglen + 1) == NULL) {
          /* force it into the log */
          end = c->bufend;
          c->bufend = begin + c->arglen;
          chat_UpdateLog(c, -1);
          c->bufend = end;
        }
        c->bufstart = begin + c->arglen;
        c->argptr += c->arglen;
        c->arglen = 0;
        break;
      } else if (begin >= abegin && begin < aend) {
        for (n = c->abort.num - 1; n >= 0; n--) {
          if (begin + c->abort.string[n].len > c->bufend)
            break;
          if (!strncmp(begin, c->abort.string[n].data,
                       c->abort.string[n].len)) {
            if (memchr(begin + c->abort.string[n].len - 1, '\n',
                c->bufend - begin - c->abort.string[n].len + 1) == NULL) {
              /* force it into the log */
              end = c->bufend;
              c->bufend = begin + c->abort.string[n].len;
              chat_UpdateLog(c, -1);
              c->bufend = end;
            }
            c->bufstart = begin + c->abort.string[n].len;
            c->state = CHAT_FAILED;
            return;
          }
        }
      }
  }
}

static int
chat_Write(struct fdescriptor *d, struct bundle *bundle __unused,
	   const fd_set *fdset __unused)
{
  struct chat *c = descriptor2chat(d);
  int result = 0;

  if (c->state == CHAT_SEND) {
    int wrote;

    if (strstr(c->argv[c->arg], "\\P"))            /* Don't log the password */
      log_Printf(LogCHAT, "Send: %s\n", c->argv[c->arg]);
    else {
      int sz;

      sz = c->arglen - 1;
      while (sz >= 0 && c->argptr[sz] == '\n')
        sz--;
      log_Printf(LogCHAT, "Send: %.*s\n", sz + 1, c->argptr);
    }

    if (physical_IsSync(c->physical)) {
      /*
       * XXX: Fix me
       * This data should be stuffed down through the link layers
       */
      /* There's always room for the HDLC header */
      c->argptr -= 2;
      c->arglen += 2;
      memcpy(c->argptr, "\377\003", 2);	/* Prepend HDLC header */
    }

    wrote = physical_Write(c->physical, c->argptr, c->arglen);
    result = wrote > 0 ? 1 : 0;
    if (wrote == -1) {
      if (errno != EINTR) {
        log_Printf(LogWARN, "chat_Write: %s\n", strerror(errno));
	result = -1;
      }
      if (physical_IsSync(c->physical)) {
        c->argptr += 2;
        c->arglen -= 2;
      }
    } else if (wrote < 2 && physical_IsSync(c->physical)) {
      /* Oops - didn't even write our HDLC header ! */
      c->argptr += 2;
      c->arglen -= 2;
    } else {
      c->argptr += wrote;
      c->arglen -= wrote;
    }
  }

  return result;
}

void
chat_Init(struct chat *c, struct physical *p)
{
  c->desc.type = CHAT_DESCRIPTOR;
  c->desc.UpdateSet = chat_UpdateSet;
  c->desc.IsSet = chat_IsSet;
  c->desc.Read = chat_Read;
  c->desc.Write = chat_Write;
  c->physical = p;
  *c->script = '\0';
  c->argc = 0;
  c->arg = -1;
  c->argptr = NULL;
  c->nargptr = NULL;
  c->bufstart = c->bufend = c->buf;

  memset(&c->pause, '\0', sizeof c->pause);
  memset(&c->timeout, '\0', sizeof c->timeout);
}

int
chat_Setup(struct chat *c, const char *data, const char *phone)
{
  c->state = CHAT_EXPECT;

  if (data == NULL) {
    *c->script = '\0';
    c->argc = 0;
  } else {
    strncpy(c->script, data, sizeof c->script - 1);
    c->script[sizeof c->script - 1] = '\0';
    c->argc = MakeArgs(c->script, c->argv, VECSIZE(c->argv), PARSE_NOHASH);
  }

  c->arg = -1;
  c->argptr = NULL;
  c->nargptr = NULL;

  c->TimeoutSec = 30;
  c->TimedOut = 0;
  c->phone = phone;
  c->abort.num = 0;

  timer_Stop(&c->pause);
  timer_Stop(&c->timeout);

  return c->argc >= 0;
}

void
chat_Finish(struct chat *c)
{
  timer_Stop(&c->pause);
  timer_Stop(&c->timeout);
  while (c->abort.num)
    free(c->abort.string[--c->abort.num].data);
  c->abort.num = 0;
}

void
chat_Destroy(struct chat *c)
{
  chat_Finish(c);
}

/*
 *  \c	don't add a cr
 *  \d  Sleep a little (delay 2 seconds
 *  \n  Line feed character
 *  \P  Auth Key password
 *  \p  pause 0.25 sec
 *  \r	Carrige return character
 *  \s  Space character
 *  \T  Telephone number(s) (defined via `set phone')
 *  \t  Tab character
 *  \U  Auth User
 */
static char *
ExpandString(struct chat *c, const char *str, char *result, int reslen, int cr)
{
  int len;

  result[--reslen] = '\0';
  while (*str && reslen > 0) {
    switch (*str) {
    case '\\':
      str++;
      switch (*str) {
      case 'c':
	cr = 0;
	break;
      case 'd':		/* Delay 2 seconds */
        chat_Pause(c, 2 * SECTICKS);
	break;
      case 'p':
        chat_Pause(c, SECTICKS / 4);
	break;		/* Delay 0.25 seconds */
      case 'n':
	*result++ = '\n';
	reslen--;
	break;
      case 'r':
	*result++ = '\r';
	reslen--;
	break;
      case 's':
	*result++ = ' ';
	reslen--;
	break;
      case 't':
	*result++ = '\t';
	reslen--;
	break;
      case 'P':
	strncpy(result, c->physical->dl->bundle->cfg.auth.key, reslen);
        len = strlen(result);
	reslen -= len;
	result += len;
	break;
      case 'T':
        if (c->phone) {
          strncpy(result, c->phone, reslen);
          len = strlen(result);
          reslen -= len;
          result += len;
        }
	break;
      case 'U':
	strncpy(result, c->physical->dl->bundle->cfg.auth.name, reslen);
        len = strlen(result);
	reslen -= len;
	result += len;
	break;
      default:
	reslen--;
	*result++ = *str;
	break;
      }
      if (*str)
	str++;
      break;
    case '^':
      str++;
      if (*str) {
	*result++ = *str++ & 0x1f;
	reslen--;
      }
      break;
    default:
      *result++ = *str++;
      reslen--;
      break;
    }
  }
  if (--reslen > 0) {
    if (cr)
      *result++ = '\r';
  }
  if (--reslen > 0)
    *result++ = '\0';
  return (result);
}

static void
ExecStr(struct physical *physical, char *command, char *out, int olen)
{
  pid_t pid;
  int fids[2];
  char *argv[MAXARGS], *vector[MAXARGS], *startout, *endout;
  int stat, nb, argc, i;

  log_Printf(LogCHAT, "Exec: %s\n", command);
  if ((argc = MakeArgs(command, vector, VECSIZE(vector),
                       PARSE_REDUCE|PARSE_NOHASH)) <= 0) {
    if (argc < 0)
      log_Printf(LogWARN, "Syntax error in exec command\n");
    *out = '\0';
    return;
  }

  if (pipe(fids) < 0) {
    log_Printf(LogCHAT, "Unable to create pipe in ExecStr: %s\n",
	      strerror(errno));
    *out = '\0';
    return;
  }
  if ((pid = fork()) == 0) {
    command_Expand(argv, argc, (char const *const *)vector,
                   physical->dl->bundle, 0, getpid());
    close(fids[0]);
    timer_TermService();
    if (fids[1] == STDIN_FILENO)
      fids[1] = dup(fids[1]);
    dup2(physical->fd, STDIN_FILENO);
    dup2(fids[1], STDERR_FILENO);
    dup2(STDIN_FILENO, STDOUT_FILENO);
    close(3);
    if (open(_PATH_TTY, O_RDWR) != 3)
      open(_PATH_DEVNULL, O_RDWR);	/* Leave it closed if it fails... */
    for (i = getdtablesize(); i > 3; i--)
      fcntl(i, F_SETFD, 1);
#ifndef NOSUID
    setuid(ID0realuid());
#endif
    execvp(argv[0], argv);
    fprintf(stderr, "execvp: %s: %s\n", argv[0], strerror(errno));
    _exit(127);
  } else {
    char *name = strdup(vector[0]);

    close(fids[1]);
    endout = out + olen - 1;
    startout = out;
    while (out < endout) {
      nb = read(fids[0], out, 1);
      if (nb <= 0)
	break;
      out++;
    }
    *out = '\0';
    close(fids[0]);
    close(fids[1]);
    waitpid(pid, &stat, WNOHANG);
    if (WIFSIGNALED(stat)) {
      log_Printf(LogWARN, "%s: signal %d\n", name, WTERMSIG(stat));
      free(name);
      *out = '\0';
      return;
    } else if (WIFEXITED(stat)) {
      switch (WEXITSTATUS(stat)) {
        case 0:
          free(name);
          break;
        case 127:
          log_Printf(LogWARN, "%s: %s\n", name, startout);
          free(name);
          *out = '\0';
          return;
          break;
        default:
          log_Printf(LogWARN, "%s: exit %d\n", name, WEXITSTATUS(stat));
          free(name);
          *out = '\0';
          return;
          break;
      }
    } else {
      log_Printf(LogWARN, "%s: Unexpected exit result\n", name);
      free(name);
      *out = '\0';
      return;
    }
  }
}
