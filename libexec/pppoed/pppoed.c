/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999-2001 Brian Somers <brian@Awfulhak.org>
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netgraph.h>
#include <net/ethernet.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netgraph/ng_ether.h>
#include <netgraph/ng_message.h>
#include <netgraph/ng_pppoe.h>
#include <netgraph/ng_socket.h>

#include <errno.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <sys/fcntl.h>
#ifndef NOKLDLOAD
#include <sys/linker.h>
#include <sys/module.h>
#endif
#include <sys/uio.h>
#include <sys/wait.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>


#define	DEFAULT_EXEC_PREFIX	"exec /usr/sbin/ppp -direct "
#define	HISMACADDR		"HISMACADDR"
#define	SESSION_ID		"SESSION_ID"

static void nglogx(const char *, ...) __printflike(1, 2);

static int ReceivedSignal;

static int
usage(const char *prog)
{
  fprintf(stderr, "usage: %s [-Fd] [-P pidfile] [-a name] [-e exec | -l label]"
          " [-n ngdebug] [-p provider] interface\n", prog);
  return EX_USAGE;
}

static void
Farewell(int sig)
{
  ReceivedSignal = sig;
}

static int
ConfigureNode(const char *prog, const char *iface, const char *provider,
              int cs, int ds, int debug, struct ngm_connect *ngc)
{
  /*
   * We're going to do this with the passed `ds' & `cs' descriptors:
   *
   * .---------.
   * |  ether  |
   * | <iface> |
   * `---------'
   *  (orphan)                                     ds    cs
   *     |                                         |     |
   *     |                                         |     |
   * (ethernet)                                    |     |
   * .---------.                                .-----------.
   * |  pppoe  |                                |  socket   |
   * | <iface> |(pppoe-<pid>)<---->(pppoe-<pid>)| <unnamed> |
   * `---------                                 `-----------'
   * (exec-<pid>)
   *     ^                .-----------.      .-------------.
   *     |                |   socket  |      | ppp -direct |
   *     `--->(exec-<pid>)| <unnamed> |--fd--|  provider   |
   *                      `-----------'      `-------------'
   *
   * where there are potentially many ppp processes running off of the
   * same PPPoE node.
   * The exec-<pid> hook isn't made 'till we Spawn().
   */

  char *epath, *spath;
  struct ngpppoe_init_data *data;
  const struct hooklist *hlist;
  const struct nodeinfo *ninfo;
  const struct linkinfo *nlink;
  struct ngm_mkpeer mkp;
  struct ng_mesg *resp;
  u_char rbuf[2048];
  int f, plen;

  /*
   * Ask for a list of hooks attached to the "ether" node.  This node should
   * magically exist as a way of hooking stuff onto an ethernet device
   */
  epath = (char *)alloca(strlen(iface) + 2);
  sprintf(epath, "%s:", iface);

  if (debug)
    fprintf(stderr, "Sending NGM_LISTHOOKS to %s\n", epath);

  if (NgSendMsg(cs, epath, NGM_GENERIC_COOKIE, NGM_LISTHOOKS, NULL, 0) < 0) {
    if (errno == ENOENT)
      fprintf(stderr, "%s Cannot send a netgraph message: Invalid interface\n",
              epath);
    else
      fprintf(stderr, "%s Cannot send a netgraph message: %s\n",
              epath, strerror(errno));
    return EX_UNAVAILABLE;
  }

  /* Get our list back */
  resp = (struct ng_mesg *)rbuf;
  if (NgRecvMsg(cs, resp, sizeof rbuf, NULL) <= 0) {
    perror("Cannot get netgraph response");
    return EX_UNAVAILABLE;
  }

  hlist = (const struct hooklist *)resp->data;
  ninfo = &hlist->nodeinfo;

  if (debug)
    fprintf(stderr, "Got reply from id [%x]: Type %s with %d hooks\n",
            ninfo->id, ninfo->type, ninfo->hooks);

  /* Make sure we've got the right type of node */
  if (strncmp(ninfo->type, NG_ETHER_NODE_TYPE, sizeof NG_ETHER_NODE_TYPE - 1)) {
    fprintf(stderr, "%s Unexpected node type ``%s'' (wanted ``"
            NG_ETHER_NODE_TYPE "'')\n", epath, ninfo->type);
    return EX_DATAERR;
  }

  /* look for a hook already attached.  */
  for (f = 0; f < ninfo->hooks; f++) {
    nlink = &hlist->link[f];

    if (debug)
      fprintf(stderr, "  Got [%x]:%s -> [%x]:%s\n", ninfo->id,
              nlink->ourhook, nlink->nodeinfo.id, nlink->peerhook);

    if (!strcmp(nlink->ourhook, NG_ETHER_HOOK_ORPHAN) ||
        !strcmp(nlink->ourhook, NG_ETHER_HOOK_DIVERT)) {
      /*
       * Something is using the data coming out of this `ether' node.
       * If it's a PPPoE node, we use that node, otherwise we complain that
       * someone else is using the node.
       */
      if (strcmp(nlink->nodeinfo.type, NG_PPPOE_NODE_TYPE)) {
        fprintf(stderr, "%s Node type %s is currently active\n",
                epath, nlink->nodeinfo.type);
        return EX_UNAVAILABLE;
      }
      break;
    }
  }

  if (f == ninfo->hooks) {
    /*
     * Create a new PPPoE node connected to the `ether' node using
     * the magic `orphan' and `ethernet' hooks
     */
    snprintf(mkp.type, sizeof mkp.type, "%s", NG_PPPOE_NODE_TYPE);
    snprintf(mkp.ourhook, sizeof mkp.ourhook, "%s", NG_ETHER_HOOK_ORPHAN);
    snprintf(mkp.peerhook, sizeof mkp.peerhook, "%s", NG_PPPOE_HOOK_ETHERNET);

    if (debug)
      fprintf(stderr, "Send MKPEER: %s%s -> [type %s]:%s\n", epath,
              mkp.ourhook, mkp.type, mkp.peerhook);

    if (NgSendMsg(cs, epath, NGM_GENERIC_COOKIE,
                  NGM_MKPEER, &mkp, sizeof mkp) < 0) {
      fprintf(stderr, "%s Cannot create a peer PPPoE node: %s\n",
              epath, strerror(errno));
      return EX_OSERR;
    }
  }

  /* Connect the PPPoE node to our socket node.  */
  snprintf(ngc->path, sizeof ngc->path, "%s%s", epath, NG_ETHER_HOOK_ORPHAN);
  snprintf(ngc->ourhook, sizeof ngc->ourhook, "pppoe-%ld", (long)getpid());
  memcpy(ngc->peerhook, ngc->ourhook, sizeof ngc->peerhook);

  if (NgSendMsg(cs, ".:", NGM_GENERIC_COOKIE,
                NGM_CONNECT, ngc, sizeof *ngc) < 0) {
    perror("Cannot CONNECT PPPoE and socket nodes");
    return EX_OSERR;
  }

  plen = strlen(provider);

  data = (struct ngpppoe_init_data *)alloca(sizeof *data + plen);
  snprintf(data->hook, sizeof data->hook, "%s", ngc->peerhook);
  memcpy(data->data, provider, plen);
  data->data_len = plen;

  spath = (char *)alloca(strlen(ngc->peerhook) + 3);
  strcpy(spath, ".:");
  strcpy(spath + 2, ngc->ourhook);

  if (debug) {
    if (provider)
      fprintf(stderr, "Sending PPPOE_LISTEN to %s, provider %s\n",
              spath, provider);
    else
      fprintf(stderr, "Sending PPPOE_LISTEN to %s\n", spath);
  }

  if (NgSendMsg(cs, spath, NGM_PPPOE_COOKIE, NGM_PPPOE_LISTEN,
                data, sizeof *data + plen) == -1) {
    fprintf(stderr, "%s: Cannot LISTEN on netgraph node: %s\n",
            spath, strerror(errno));
    return EX_OSERR;
  }

  return 0;
}

static void
Spawn(const char *prog, const char *acname, const char *provider,
      const char *exec, struct ngm_connect ngc, int cs, int ds, void *request,
      int sz, int debug)
{
  char msgbuf[sizeof(struct ng_mesg) + sizeof(struct ngpppoe_sts)];
  struct ng_mesg *rep = (struct ng_mesg *)msgbuf;
  struct ngpppoe_sts *sts = (struct ngpppoe_sts *)(msgbuf + sizeof *rep);
  struct ngpppoe_init_data *data;
  char env[18], unknown[14], sessionid[5], *path;
  unsigned char *macaddr;
  const char *msg;
  int ret, slen;

  switch ((ret = fork())) {
    case -1:
      syslog(LOG_ERR, "fork: %m");
      break;

    case 0:
      switch (fork()) {
        case 0:
          break;
        case -1:
          _exit(errno);
        default:
          _exit(0);
      }
      close(cs);
      close(ds);

      /* Create a new socket node */
      if (debug)
        syslog(LOG_INFO, "Creating a new socket node");

      if (NgMkSockNode(NULL, &cs, &ds) == -1) {
        syslog(LOG_ERR, "Cannot create netgraph socket node: %m");
        _exit(EX_CANTCREAT);
      }

      /* Connect the PPPoE node to our new socket node.  */
      snprintf(ngc.ourhook, sizeof ngc.ourhook, "exec-%ld", (long)getpid());
      memcpy(ngc.peerhook, ngc.ourhook, sizeof ngc.peerhook);

      if (debug)
        syslog(LOG_INFO, "Sending CONNECT from .:%s -> %s.%s",
               ngc.ourhook, ngc.path, ngc.peerhook);
      if (NgSendMsg(cs, ".:", NGM_GENERIC_COOKIE,
                    NGM_CONNECT, &ngc, sizeof ngc) < 0) {
        syslog(LOG_ERR, "Cannot CONNECT PPPoE and socket nodes: %m");
        _exit(EX_OSERR);
      }

      /*
       * If we tell the socket node not to LINGER, it will go away when
       * the last hook is removed.
       */
      if (debug)
        syslog(LOG_INFO, "Sending NGM_SOCK_CMD_NOLINGER to socket");
      if (NgSendMsg(cs, ".:", NGM_SOCKET_COOKIE,
                    NGM_SOCK_CMD_NOLINGER, NULL, 0) < 0) {
        syslog(LOG_ERR, "Cannot send NGM_SOCK_CMD_NOLINGER: %m");
        _exit(EX_OSERR);
      }

      /* Put the PPPoE node into OFFER mode */
      slen = strlen(acname);
      data = (struct ngpppoe_init_data *)alloca(sizeof *data + slen);
      snprintf(data->hook, sizeof data->hook, "%s", ngc.ourhook);
      memcpy(data->data, acname, slen);
      data->data_len = slen;

      path = (char *)alloca(strlen(ngc.ourhook) + 3);
      strcpy(path, ".:");
      strcpy(path + 2, ngc.ourhook);

      syslog(LOG_INFO, "Offering to %s as access concentrator %s",
             path, acname);
      if (NgSendMsg(cs, path, NGM_PPPOE_COOKIE, NGM_PPPOE_OFFER,
                    data, sizeof *data + slen) == -1) {
        syslog(LOG_INFO, "%s: Cannot OFFER on netgraph node: %m", path);
        _exit(EX_OSERR);
      }
      /* If we have a provider code, set it */
      if (provider) {
        slen = strlen(provider);
        data = (struct ngpppoe_init_data *)alloca(sizeof *data + slen);
        snprintf(data->hook, sizeof data->hook, "%s", ngc.ourhook);
        memcpy(data->data, provider, slen);
        data->data_len = slen;

        syslog(LOG_INFO, "adding to %s as offered service %s",
             path, acname);
        if (NgSendMsg(cs, path, NGM_PPPOE_COOKIE, NGM_PPPOE_SERVICE,
                    data, sizeof *data + slen) == -1) {
          syslog(LOG_INFO, "%s: Cannot add service on netgraph node: %m", path);
          _exit(EX_OSERR);
        }
      }

      /* Put the peer's MAC address in the environment */
      if (sz >= sizeof(struct ether_header)) {
        macaddr = ((struct ether_header *)request)->ether_shost;
        snprintf(env, sizeof(env), "%x:%x:%x:%x:%x:%x",
                 macaddr[0], macaddr[1], macaddr[2], macaddr[3], macaddr[4],
                 macaddr[5]);
        if (setenv(HISMACADDR, env, 1) != 0)
          syslog(LOG_INFO, "setenv: cannot set %s: %m", HISMACADDR);
      }

      /* And send our request data to the waiting node */
      if (debug)
        syslog(LOG_INFO, "Sending original request to %s (%d bytes)", path, sz);
      if (NgSendData(ds, ngc.ourhook, request, sz) == -1) {
        syslog(LOG_ERR, "Cannot send original request to %s: %m", path);
        _exit(EX_OSERR);
      }

      /* Then wait for a success indication */

      if (debug)
        syslog(LOG_INFO, "Waiting for a SUCCESS reply %s", path);

      do {
        if ((ret = NgRecvMsg(cs, rep, sizeof msgbuf, NULL)) < 0) {
          syslog(LOG_ERR, "%s: Cannot receive a message: %m", path);
          _exit(EX_OSERR);
        }

        if (ret == 0) {
          /* The socket has been closed */
          syslog(LOG_INFO, "%s: Client timed out", path);
          _exit(EX_TEMPFAIL);
        }

        if (rep->header.version != NG_VERSION) {
          syslog(LOG_ERR, "%ld: Unexpected netgraph version, expected %ld",
                 (long)rep->header.version, (long)NG_VERSION);
          _exit(EX_PROTOCOL);
        }

        if (rep->header.typecookie != NGM_PPPOE_COOKIE) {
          syslog(LOG_INFO, "%ld: Unexpected netgraph cookie, expected %ld",
                 (long)rep->header.typecookie, (long)NGM_PPPOE_COOKIE);
          continue;
        }

        switch (rep->header.cmd) {
          case NGM_PPPOE_SET_FLAG:	msg = "SET_FLAG";	break;
          case NGM_PPPOE_CONNECT:	msg = "CONNECT";	break;
          case NGM_PPPOE_LISTEN:	msg = "LISTEN";		break;
          case NGM_PPPOE_OFFER:		msg = "OFFER";		break;
          case NGM_PPPOE_SUCCESS:	msg = "SUCCESS";	break;
          case NGM_PPPOE_FAIL:		msg = "FAIL";		break;
          case NGM_PPPOE_CLOSE:		msg = "CLOSE";		break;
          case NGM_PPPOE_GET_STATUS:	msg = "GET_STATUS";	break;
          case NGM_PPPOE_ACNAME:
            msg = "ACNAME";
            if (setenv("ACNAME", sts->hook, 1) != 0)
              syslog(LOG_WARNING, "setenv: cannot set ACNAME=%s: %m",
                     sts->hook);
            break;
          case NGM_PPPOE_SESSIONID:
            msg = "SESSIONID";
            snprintf(sessionid, sizeof sessionid, "%04x", *(u_int16_t *)sts);
            if (setenv("SESSIONID", sessionid, 1) != 0)
              syslog(LOG_WARNING, "setenv: cannot set SESSIONID=%s: %m",
                     sessionid);
            break;
          default:
            snprintf(unknown, sizeof unknown, "<%d>", (int)rep->header.cmd);
            msg = unknown;
            break;
        }

        switch (rep->header.cmd) {
          case NGM_PPPOE_FAIL:
          case NGM_PPPOE_CLOSE:
            syslog(LOG_ERR, "Received NGM_PPPOE_%s (hook \"%s\")",
                   msg, sts->hook);
            _exit(0);
        }

        syslog(LOG_INFO, "Received NGM_PPPOE_%s (hook \"%s\")", msg, sts->hook);
      } while (rep->header.cmd != NGM_PPPOE_SUCCESS);

      dup2(ds, STDIN_FILENO);
      dup2(ds, STDOUT_FILENO);
      close(ds);
      close(cs);

      setsid();
      syslog(LOG_INFO, "Executing: %s", exec);
      execlp(_PATH_BSHELL, _PATH_BSHELL, "-c", exec, (char *)NULL);
      syslog(LOG_ERR, "execlp failed: %m");
      _exit(EX_OSFILE);

    default:
      wait(&ret);
      errno = ret;
      if (errno)
        syslog(LOG_ERR, "Second fork failed: %m");
      break;
  }
}

#ifndef NOKLDLOAD
static int
LoadModules(void)
{
  const char *module[] = { "netgraph", "ng_socket", "ng_ether", "ng_pppoe" };
  int f;

  for (f = 0; f < sizeof module / sizeof *module; f++)
    if (modfind(module[f]) == -1 && kldload(module[f]) == -1) {
      fprintf(stderr, "kldload: %s: %s\n", module[f], strerror(errno));
      return 0;
    }

  return 1;
}
#endif

static void
nglog(const char *fmt, ...)
{
  char nfmt[256];
  va_list ap;

  snprintf(nfmt, sizeof nfmt, "%s: %s", fmt, strerror(errno));
  va_start(ap, fmt);
  vsyslog(LOG_INFO, nfmt, ap);
  va_end(ap);
}

static void
nglogx(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
}

int
main(int argc, char *argv[])
{
  char hostname[MAXHOSTNAMELEN], *exec, rhook[NG_HOOKSIZ];
  unsigned char response[1024];
  const char *label, *prog, *provider, *acname;
  struct ngm_connect ngc;
  struct sigaction act;
  int ch, cs, ds, ret, optF, optd, optn, sz, f;
  const char *pidfile;

  prog = strrchr(argv[0], '/');
  prog = prog ? prog + 1 : argv[0];
  pidfile = NULL;
  exec = NULL;
  label = NULL;
  acname = NULL;
  provider = "";
  optF = optd = optn = 0;

  while ((ch = getopt(argc, argv, "FP:a:de:l:n:p:")) != -1) {
    switch (ch) {
      case 'F':
        optF = 1;
        break;

      case 'P':
        pidfile = optarg;
        break;

      case 'a':
        acname = optarg;
        break;

      case 'd':
        optd = 1;
        break;

      case 'e':
        exec = optarg;
        break;

      case 'l':
        label = optarg;
        break;

      case 'n':
        optn = 1;
        NgSetDebug(atoi(optarg));
        break;

      case 'p':
        provider = optarg;
        break;

      default:
        return usage(prog);
    }
  }

  if (optind >= argc || optind + 2 < argc)
    return usage(prog);

  if (exec != NULL && label != NULL)
    return usage(prog);

  if (exec == NULL) {
    if (label == NULL)
      label = provider;
    if (label == NULL) {
      fprintf(stderr, "%s: Either a provider, a label or an exec command"
              " must be given\n", prog);
      return usage(prog);
    }
    exec = (char *)alloca(sizeof DEFAULT_EXEC_PREFIX + strlen(label));
    if (exec == NULL) {
      fprintf(stderr, "%s: Cannot allocate %zu bytes\n", prog,
              sizeof DEFAULT_EXEC_PREFIX + strlen(label));
      return EX_OSERR;
    }
    strcpy(exec, DEFAULT_EXEC_PREFIX);
    strcpy(exec + sizeof DEFAULT_EXEC_PREFIX - 1, label);
  }

  if (acname == NULL) {
    char *dot;

    if (gethostname(hostname, sizeof hostname))
      strcpy(hostname, "localhost");
    else if ((dot = strchr(hostname, '.')))
      *dot = '\0';

    acname = hostname;
  }

#ifndef NOKLDLOAD
  if (!LoadModules())
    return EX_UNAVAILABLE;
#endif

  /* Create a socket node */
  if (NgMkSockNode(NULL, &cs, &ds) == -1) {
    perror("Cannot create netgraph socket node");
    return EX_CANTCREAT;
  }

  /* Connect it up (and fill in `ngc') */
  if ((ret = ConfigureNode(prog, argv[optind], provider, cs, ds,
                           optd, &ngc)) != 0) {
    close(cs);
    close(ds);
    return ret;
  }

  if (!optF && daemon(1, 0) == -1) {
    perror("daemon()");
    close(cs);
    close(ds);
    return EX_OSERR;
  }


  if (pidfile != NULL) {
    FILE *fp;

    if ((fp = fopen(pidfile, "w")) == NULL) {
      perror(pidfile);
      close(cs);
      close(ds);
      return EX_CANTCREAT;
    } else {
      fprintf(fp, "%d\n", (int)getpid());
      fclose(fp);
    }
  }

  openlog(prog, LOG_PID | (optF ? LOG_PERROR : 0), LOG_DAEMON);
  if (!optF && optn)
    NgSetErrLog(nglog, nglogx);

  memset(&act, '\0', sizeof act);
  act.sa_handler = Farewell;
  act.sa_flags = 0;
  sigemptyset(&act.sa_mask);
  sigaction(SIGHUP, &act, NULL);
  sigaction(SIGINT, &act, NULL);
  sigaction(SIGQUIT, &act, NULL);
  sigaction(SIGTERM, &act, NULL);

  while (!ReceivedSignal) {
    if (*provider)
      syslog(LOG_INFO, "Listening as provider %s", provider);
    else
      syslog(LOG_INFO, "Listening");

    switch (sz = NgRecvData(ds, response, sizeof response, rhook)) {
      case -1:
        syslog(LOG_INFO, "NgRecvData: %m");
        break;
      case 0:
        syslog(LOG_INFO, "NgRecvData: socket closed");
        break;
      default:
        if (optd) {
          char *dbuf, *ptr;

          ptr = dbuf = alloca(sz * 2 + 1);
          for (f = 0; f < sz; f++, ptr += 2)
            sprintf(ptr, "%02x", (u_char)response[f]);
          *ptr = '\0';
          syslog(LOG_INFO, "Got %d bytes of data: %s", sz, dbuf);
        }
    }
    if (sz <= 0) {
      ret = EX_UNAVAILABLE;
      break;
    }
    Spawn(prog, acname, provider, exec, ngc, cs, ds, response, sz, optd);
  }

  if (pidfile)
    remove(pidfile);

  if (ReceivedSignal) {
    syslog(LOG_INFO, "Received signal %d, exiting", ReceivedSignal);

    signal(ReceivedSignal, SIG_DFL);
    raise(ReceivedSignal);

    /* NOTREACHED */

    ret = -ReceivedSignal;
  }

  return ret;
}
