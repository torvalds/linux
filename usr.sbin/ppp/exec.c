/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Brian Somers <brian@Awfulhak.org>
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
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <termios.h>
#include <unistd.h>

#include "layer.h"
#include "defs.h"
#include "mbuf.h"
#include "log.h"
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
#include "mp.h"
#include "chat.h"
#include "command.h"
#include "auth.h"
#include "chap.h"
#include "cbcp.h"
#include "datalink.h"
#include "id.h"
#include "main.h"
#include "exec.h"


struct execdevice {
  struct device dev;		/* What struct physical knows about */
  int fd_out;			/* output descriptor */
};

#define device2exec(d) ((d)->type == EXEC_DEVICE ? (struct execdevice *)d : NULL)

unsigned
exec_DeviceSize(void)
{
  return sizeof(struct execdevice);
}

static void
exec_Free(struct physical *p)
{
  struct execdevice *dev = device2exec(p->handler);

  if (dev->fd_out != -1)
    close(dev->fd_out);
  free(dev);
}

static void
exec_device2iov(struct device *d, struct iovec *iov, int *niov,
               int maxiov __unused, int *auxfd, int *nauxfd)
{
  struct execdevice *dev;
  int sz = physical_MaxDeviceSize();

  iov[*niov].iov_base = d = realloc(d, sz);
  if (d == NULL) {
    log_Printf(LogALERT, "Failed to allocate memory: %d\n", sz);
    AbortProgram(EX_OSERR);
  }
  iov[*niov].iov_len = sz;
  (*niov)++;

  dev = device2exec(d);
  if (dev->fd_out >= 0) {
    *auxfd = dev->fd_out;
    (*nauxfd)++;
  }
}

static int
exec_RemoveFromSet(struct physical *p, fd_set *r, fd_set *w, fd_set *e)
{
  struct execdevice *dev = device2exec(p->handler);
  int sets;

  p->handler->removefromset = NULL;
  sets = physical_RemoveFromSet(p, r, w, e);
  p->handler->removefromset = exec_RemoveFromSet;

  if (dev->fd_out >= 0) {
    if (w && FD_ISSET(dev->fd_out, w)) {
      FD_CLR(dev->fd_out, w);
      log_Printf(LogTIMER, "%s: fdunset(w) %d\n", p->link.name, dev->fd_out);
      sets++;
    }
    if (e && FD_ISSET(dev->fd_out, e)) {
      FD_CLR(dev->fd_out, e);
      log_Printf(LogTIMER, "%s: fdunset(e) %d\n", p->link.name, dev->fd_out);
      sets++;
    }
  }

  return sets;
}

static ssize_t
exec_Write(struct physical *p, const void *v, size_t n)
{
  struct execdevice *dev = device2exec(p->handler);
  int fd = dev->fd_out == -1 ? p->fd : dev->fd_out;

  return write(fd, v, n);
}

static struct device baseexecdevice = {
  EXEC_DEVICE,
  "exec",
  0,
  { CD_NOTREQUIRED, 0 },
  NULL,
  exec_RemoveFromSet,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  exec_Free,
  NULL,
  exec_Write,
  exec_device2iov,
  NULL,
  NULL,
  NULL
};

struct device *
exec_iov2device(int type, struct physical *p, struct iovec *iov,
                int *niov, int maxiov __unused, int *auxfd, int *nauxfd)
{
  if (type == EXEC_DEVICE) {
    struct execdevice *dev = (struct execdevice *)iov[(*niov)++].iov_base;

    dev = realloc(dev, sizeof *dev);	/* Reduce to the correct size */
    if (dev == NULL) {
      log_Printf(LogALERT, "Failed to allocate memory: %d\n",
                 (int)(sizeof *dev));
      AbortProgram(EX_OSERR);
    }

    if (*nauxfd) {
      dev->fd_out = *auxfd;
      (*nauxfd)--;
    } else
      dev->fd_out = -1;

    /* Refresh function pointers etc */
    memcpy(&dev->dev, &baseexecdevice, sizeof dev->dev);

    physical_SetupStack(p, dev->dev.name, PHYSICAL_NOFORCE);
    return &dev->dev;
  }

  return NULL;
}

static int
exec_UpdateSet(struct fdescriptor *d, fd_set *r, fd_set *w, fd_set *e, int *n)
{
  struct physical *p = descriptor2physical(d);
  struct execdevice *dev = device2exec(p->handler);
  int result = 0;

  if (w && dev->fd_out >= 0) {
    FD_SET(dev->fd_out, w);
    log_Printf(LogTIMER, "%s: fdset(w) %d\n", p->link.name, dev->fd_out);
    result++;
    w = NULL;
  }

  if (e && dev->fd_out >= 0) {
    FD_SET(dev->fd_out, e);
    log_Printf(LogTIMER, "%s: fdset(e) %d\n", p->link.name, dev->fd_out);
    result++;
  }

  if (result && *n <= dev->fd_out)
    *n = dev->fd_out + 1;

  return result + physical_doUpdateSet(d, r, w, e, n, 0);
}

static int
exec_IsSet(struct fdescriptor *d, const fd_set *fdset)
{
  struct physical *p = descriptor2physical(d);
  struct execdevice *dev = device2exec(p->handler);
  int result = dev->fd_out >= 0 && FD_ISSET(dev->fd_out, fdset);
  result += physical_IsSet(d, fdset);

  return result;
}

struct device *
exec_Create(struct physical *p)
{
  struct execdevice *dev;

  dev = NULL;
  if (p->fd < 0) {
    if (*p->name.full == '!') {
      int fids[2], type;
  
      if ((dev = malloc(sizeof *dev)) == NULL) {
        log_Printf(LogWARN, "%s: Cannot allocate an exec device: %s\n",
                   p->link.name, strerror(errno));
        return NULL;
      }
      dev->fd_out = -1;
  
      p->fd--;	/* We own the device but maybe can't use it - change fd */
      type = physical_IsSync(p) ? SOCK_DGRAM : SOCK_STREAM;
  
      if (socketpair(AF_UNIX, type, PF_UNSPEC, fids) < 0) {
        log_Printf(LogPHASE, "Unable to create pipe for line exec: %s\n",
                   strerror(errno));
        free(dev);
        dev = NULL;
      } else {
        static int child_status;		/* This variable is abused ! */
        int stat, argc, i, ret, wret, pidpipe[2];
        pid_t pid, realpid;
        char *argv[MAXARGS];
  
        stat = fcntl(fids[0], F_GETFL, 0);
        if (stat > 0) {
          stat |= O_NONBLOCK;
          fcntl(fids[0], F_SETFL, stat);
        }
        realpid = getpid();
        if (pipe(pidpipe) == -1) {
          log_Printf(LogPHASE, "Unable to pipe for line exec: %s\n",
                     strerror(errno));
          close(fids[1]);
          close(fids[0]);
          free(dev);
          dev = NULL;
        } else switch ((pid = fork())) {
          case -1:
            log_Printf(LogPHASE, "Unable to fork for line exec: %s\n",
                       strerror(errno));
            close(pidpipe[0]);
            close(pidpipe[1]);
            close(fids[1]);
            close(fids[0]);
            break;
  
          case 0:
            close(pidpipe[0]);
            close(fids[0]);
            timer_TermService();
  #ifndef NOSUID
            setuid(ID0realuid());
  #endif
  
            child_status = 0;
            switch ((pid = vfork())) {
              case 0:
                close(pidpipe[1]);
                break;
  
              case -1:
                ret = errno;
                log_Printf(LogPHASE, "Unable to vfork to drop parent: %s\n",
                           strerror(errno));
                close(pidpipe[1]);
                _exit(ret);
  
              default:
                write(pidpipe[1], &pid, sizeof pid);
                close(pidpipe[1]);
                _exit(child_status);	/* The error from exec() ! */
            }
  
            log_Printf(LogDEBUG, "Exec'ing ``%s''\n", p->name.base);
  
            if ((argc = MakeArgs(p->name.base, argv, VECSIZE(argv),
                                 PARSE_REDUCE|PARSE_NOHASH)) < 0) {
              log_Printf(LogWARN, "Syntax error in exec command\n");
              _exit(ESRCH);
            }
  
            command_Expand(argv, argc, (char const *const *)argv,
                           p->dl->bundle, 0, realpid);
  
            dup2(fids[1], STDIN_FILENO);
            dup2(fids[1], STDOUT_FILENO);
            dup2(fids[1], STDERR_FILENO);
            for (i = getdtablesize(); i > STDERR_FILENO; i--)
              fcntl(i, F_SETFD, 1);
  
            execvp(*argv, argv);
            child_status = errno;		/* Only works for vfork() */
            printf("execvp failed: %s: %s\r\n", *argv, strerror(child_status));
            _exit(child_status);
            break;
  
          default:
            close(pidpipe[1]);
            close(fids[1]);
            if (read(pidpipe[0], &p->session_owner, sizeof p->session_owner) !=
                sizeof p->session_owner)
              p->session_owner = (pid_t)-1;
            close(pidpipe[0]);
            while ((wret = waitpid(pid, &stat, 0)) == -1 && errno == EINTR)
              ;
            if (wret == -1) {
              log_Printf(LogWARN, "Waiting for child process: %s\n",
                         strerror(errno));
              close(fids[0]);
              p->session_owner = (pid_t)-1;
              break;
            } else if (WIFSIGNALED(stat)) {
              log_Printf(LogWARN, "Child process received sig %d !\n",
                         WTERMSIG(stat));
              close(fids[0]);
              p->session_owner = (pid_t)-1;
              break;
            } else if (WIFSTOPPED(stat)) {
              log_Printf(LogWARN, "Child process received stop sig %d !\n",
                         WSTOPSIG(stat));
              /* I guess that's ok.... */
            } else if ((ret = WEXITSTATUS(stat))) {
              log_Printf(LogWARN, "Cannot exec \"%s\": %s\n", p->name.base,
                         strerror(ret));
              close(fids[0]);
              p->session_owner = (pid_t)-1;
              break;
            }
            p->fd = fids[0];
            log_Printf(LogDEBUG, "Using descriptor %d for child\n", p->fd);
        }
      }
    }
  } else {
    struct stat st;

    if (fstat(p->fd, &st) != -1 && (st.st_mode & S_IFIFO)) {
      if ((dev = malloc(sizeof *dev)) == NULL)
        log_Printf(LogWARN, "%s: Cannot allocate an exec device: %s\n",
                   p->link.name, strerror(errno));
      else if (p->fd == STDIN_FILENO) {
        log_Printf(LogPHASE, "%s: Using stdin/stdout to communicate with "
                   "parent (pipe mode)\n", p->link.name);
        dev->fd_out = dup(STDOUT_FILENO);

        /* Hook things up so that we monitor dev->fd_out */
        p->desc.UpdateSet = exec_UpdateSet;
        p->desc.IsSet = exec_IsSet;
      } else
        dev->fd_out = -1;
    }
  }

  if (dev) {
    memcpy(&dev->dev, &baseexecdevice, sizeof dev->dev);
    physical_SetupStack(p, dev->dev.name, PHYSICAL_NOFORCE);
    if (p->cfg.cd.necessity != CD_DEFAULT)
      log_Printf(LogWARN, "Carrier settings ignored\n");
    return &dev->dev;
  }

  return NULL;
}
