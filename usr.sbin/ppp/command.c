/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996 - 2001 Brian Somers <brian@Awfulhak.org>
 *          based on work by Toshiharu OHNO <tony-o@iij.ad.jp>
 *                           Internet Initiative Japan, Inc (IIJ)
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
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/route.h>
#include <netdb.h>
#include <sys/un.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#ifndef NONAT
#ifdef LOCALNAT
#include "alias.h"
#else
#include <alias.h>
#endif
#endif

#include "layer.h"
#include "defs.h"
#include "command.h"
#include "mbuf.h"
#include "log.h"
#include "timer.h"
#include "fsm.h"
#include "iplist.h"
#include "throughput.h"
#include "slcompress.h"
#include "lqr.h"
#include "hdlc.h"
#include "lcp.h"
#include "ncpaddr.h"
#include "ipcp.h"
#ifndef NONAT
#include "nat_cmd.h"
#endif
#include "systems.h"
#include "filter.h"
#include "descriptor.h"
#include "main.h"
#include "route.h"
#include "ccp.h"
#include "auth.h"
#include "async.h"
#include "link.h"
#include "physical.h"
#include "mp.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"
#include "server.h"
#include "prompt.h"
#include "chat.h"
#include "chap.h"
#include "cbcp.h"
#include "datalink.h"
#include "iface.h"
#include "id.h"
#include "probe.h"

/* ``set'' values */
#define	VAR_AUTHKEY	0
#define	VAR_DIAL	1
#define	VAR_LOGIN	2
#define	VAR_AUTHNAME	3
#define	VAR_AUTOLOAD	4
#define	VAR_WINSIZE	5
#define	VAR_DEVICE	6
#define	VAR_ACCMAP	7
#define	VAR_MRRU	8
#define	VAR_MRU		9
#define	VAR_MTU		10
#define	VAR_OPENMODE	11
#define	VAR_PHONE	12
#define	VAR_HANGUP	13
#define	VAR_IDLETIMEOUT	14
#define	VAR_LQRPERIOD	15
#define	VAR_LCPRETRY	16
#define	VAR_CHAPRETRY	17
#define	VAR_PAPRETRY	18
#define	VAR_CCPRETRY	19
#define	VAR_IPCPRETRY	20
#define	VAR_DNS		21
#define	VAR_NBNS	22
#define	VAR_MODE	23
#define	VAR_CALLBACK	24
#define	VAR_CBCP	25
#define	VAR_CHOKED	26
#define	VAR_SENDPIPE	27
#define	VAR_RECVPIPE	28
#define	VAR_RADIUS	29
#define	VAR_CD		30
#define	VAR_PARITY	31
#define VAR_CRTSCTS	32
#define VAR_URGENT	33
#define	VAR_LOGOUT	34
#define	VAR_IFQUEUE	35
#define	VAR_MPPE	36
#define	VAR_IPV6CPRETRY	37
#define	VAR_RAD_ALIVE	38
#define	VAR_PPPOE	39
#define	VAR_PORT_ID	40

/* ``accept|deny|disable|enable'' masks */
#define NEG_HISMASK (1)
#define NEG_MYMASK (2)

/* ``accept|deny|disable|enable'' values */
#define NEG_ACFCOMP	40
#define NEG_CHAP05	41
#define NEG_CHAP80	42
#define NEG_CHAP80LM	43
#define NEG_DEFLATE	44
#define NEG_DNS		45
#define NEG_ECHO	46
#define NEG_ENDDISC	47
#define NEG_LQR		48
#define NEG_PAP		49
#define NEG_PPPDDEFLATE	50
#define NEG_PRED1	51
#define NEG_PROTOCOMP	52
#define NEG_SHORTSEQ	53
#define NEG_VJCOMP	54
#define NEG_MPPE	55
#define NEG_CHAP81	56

const char Version[] = "3.4.2";

static int ShowCommand(struct cmdargs const *);
static int TerminalCommand(struct cmdargs const *);
static int QuitCommand(struct cmdargs const *);
static int OpenCommand(struct cmdargs const *);
static int CloseCommand(struct cmdargs const *);
static int DownCommand(struct cmdargs const *);
static int SetCommand(struct cmdargs const *);
static int LinkCommand(struct cmdargs const *);
static int AddCommand(struct cmdargs const *);
static int DeleteCommand(struct cmdargs const *);
static int NegotiateCommand(struct cmdargs const *);
static int ClearCommand(struct cmdargs const *);
static int RunListCommand(struct cmdargs const *);
static int IfaceNameCommand(struct cmdargs const *arg);
static int IfaceAddCommand(struct cmdargs const *);
static int IfaceDeleteCommand(struct cmdargs const *);
static int IfaceClearCommand(struct cmdargs const *);
static int SetProcTitle(struct cmdargs const *);
#ifndef NONAT
static int NatEnable(struct cmdargs const *);
static int NatOption(struct cmdargs const *);
#endif

extern struct libalias *la;

static const char *
showcx(struct cmdtab const *cmd)
{
  if (cmd->lauth & LOCAL_CX)
    return "(c)";
  else if (cmd->lauth & LOCAL_CX_OPT)
    return "(o)";

  return "";
}

static int
HelpCommand(struct cmdargs const *arg)
{
  struct cmdtab const *cmd;
  int n, cmax, dmax, cols, cxlen;
  const char *cx;

  if (!arg->prompt) {
    log_Printf(LogWARN, "help: Cannot help without a prompt\n");
    return 0;
  }

  if (arg->argc > arg->argn) {
    for (cmd = arg->cmdtab; cmd->name || cmd->alias; cmd++)
      if ((cmd->lauth & arg->prompt->auth) &&
          ((cmd->name && !strcasecmp(cmd->name, arg->argv[arg->argn])) ||
           (cmd->alias && !strcasecmp(cmd->alias, arg->argv[arg->argn])))) {
	prompt_Printf(arg->prompt, "%s %s\n", cmd->syntax, showcx(cmd));
	return 0;
      }
    return -1;
  }

  cmax = dmax = 0;
  for (cmd = arg->cmdtab; cmd->func; cmd++)
    if (cmd->name && (cmd->lauth & arg->prompt->auth)) {
      if ((n = strlen(cmd->name) + strlen(showcx(cmd))) > cmax)
        cmax = n;
      if ((n = strlen(cmd->helpmes)) > dmax)
        dmax = n;
    }

  cols = 80 / (dmax + cmax + 3);
  n = 0;
  prompt_Printf(arg->prompt, "(o) = Optional context,"
                " (c) = Context required\n");
  for (cmd = arg->cmdtab; cmd->func; cmd++)
    if (cmd->name && (cmd->lauth & arg->prompt->auth)) {
      cx = showcx(cmd);
      cxlen = cmax - strlen(cmd->name);
      if (n % cols != 0)
        prompt_Printf(arg->prompt, " ");
      prompt_Printf(arg->prompt, "%s%-*.*s: %-*.*s",
              cmd->name, cxlen, cxlen, cx, dmax, dmax, cmd->helpmes);
      if (++n % cols == 0)
        prompt_Printf(arg->prompt, "\n");
    }
  if (n % cols != 0)
    prompt_Printf(arg->prompt, "\n");

  return 0;
}

static int
IdentCommand(struct cmdargs const *arg)
{
  Concatinate(arg->cx->physical->link.lcp.cfg.ident,
              sizeof arg->cx->physical->link.lcp.cfg.ident,
              arg->argc - arg->argn, arg->argv + arg->argn);
  return 0;
}

static int
SendIdentification(struct cmdargs const *arg)
{
  if (arg->cx->state < DATALINK_LCP) {
    log_Printf(LogWARN, "sendident: link has not reached LCP\n");
    return 2;
  }
  return lcp_SendIdentification(&arg->cx->physical->link.lcp) ? 0 : 1;
}

static int
CloneCommand(struct cmdargs const *arg)
{
  char namelist[LINE_LEN];
  char *name;
  int f;

  if (arg->argc == arg->argn)
    return -1;

  namelist[sizeof namelist - 1] = '\0';
  for (f = arg->argn; f < arg->argc; f++) {
    strncpy(namelist, arg->argv[f], sizeof namelist - 1);
    for(name = strtok(namelist, ", "); name; name = strtok(NULL,", "))
      bundle_DatalinkClone(arg->bundle, arg->cx, name);
  }

  return 0;
}

static int
RemoveCommand(struct cmdargs const *arg)
{
  if (arg->argc != arg->argn)
    return -1;

  if (arg->cx->state != DATALINK_CLOSED) {
    log_Printf(LogWARN, "remove: Cannot delete links that aren't closed\n");
    return 2;
  }

  bundle_DatalinkRemove(arg->bundle, arg->cx);
  return 0;
}

static int
RenameCommand(struct cmdargs const *arg)
{
  if (arg->argc != arg->argn + 1)
    return -1;

  if (bundle_RenameDatalink(arg->bundle, arg->cx, arg->argv[arg->argn]))
    return 0;

  log_Printf(LogWARN, "%s -> %s: target name already exists\n",
             arg->cx->name, arg->argv[arg->argn]);
  return 1;
}

static int
LoadCommand(struct cmdargs const *arg)
{
  const char *err;
  int n, mode;

  mode = arg->bundle->phys_type.all;

  if (arg->argn < arg->argc) {
    for (n = arg->argn; n < arg->argc; n++)
      if ((err = system_IsValid(arg->argv[n], arg->prompt, mode)) != NULL) {
        log_Printf(LogWARN, "%s: %s\n", arg->argv[n], err);
        return 1;
      }

    for (n = arg->argn; n < arg->argc; n++) {
      bundle_SetLabel(arg->bundle, arg->argv[arg->argc - 1]);
      system_Select(arg->bundle, arg->argv[n], CONFFILE, arg->prompt, arg->cx);
    }
    bundle_SetLabel(arg->bundle, arg->argv[arg->argc - 1]);
  } else if ((err = system_IsValid("default", arg->prompt, mode)) != NULL) {
    log_Printf(LogWARN, "default: %s\n", err);
    return 1;
  } else {
    bundle_SetLabel(arg->bundle, "default");
    system_Select(arg->bundle, "default", CONFFILE, arg->prompt, arg->cx);
    bundle_SetLabel(arg->bundle, "default");
  }

  return 0;
}

static int
LogCommand(struct cmdargs const *arg)
{
  char buf[LINE_LEN];

  if (arg->argn < arg->argc) {
    char *argv[MAXARGS];
    int argc = arg->argc - arg->argn;

    if (argc >= (int)(sizeof argv / sizeof argv[0])) {
      argc = sizeof argv / sizeof argv[0] - 1;
      log_Printf(LogWARN, "Truncating log command to %d args\n", argc);
    }
    command_Expand(argv, argc, arg->argv + arg->argn, arg->bundle, 1, getpid());
    Concatinate(buf, sizeof buf, argc, (const char *const *)argv);
    log_Printf(LogLOG, "%s\n", buf);
    command_Free(argc, argv);
    return 0;
  }

  return -1;
}

static int
SaveCommand(struct cmdargs const *arg __unused)
{
  log_Printf(LogWARN, "save command is not yet implemented.\n");
  return 1;
}

static int
DialCommand(struct cmdargs const *arg)
{
  int res;

  if ((arg->cx && !(arg->cx->physical->type & (PHYS_INTERACTIVE|PHYS_AUTO)))
      || (!arg->cx &&
          (arg->bundle->phys_type.all & ~(PHYS_INTERACTIVE|PHYS_AUTO)))) {
    log_Printf(LogWARN, "Manual dial is only available for auto and"
              " interactive links\n");
    return 1;
  }

  if (arg->argc > arg->argn && (res = LoadCommand(arg)) != 0)
    return res;

  bundle_Open(arg->bundle, arg->cx ? arg->cx->name : NULL, PHYS_ALL, 1);

  return 0;
}

#define isinword(ch) (isalnum(ch) || (ch) == '_')

static char *
strstrword(char *big, const char *little)
{
  /* Get the first occurrence of the word ``little'' in ``big'' */
  char *pos;
  int len;

  pos = big;
  len = strlen(little);

  while ((pos = strstr(pos, little)) != NULL)
    if ((pos != big && isinword(pos[-1])) || isinword(pos[len]))
      pos++;
    else if (pos != big && pos[-1] == '\\')
      memmove(pos - 1, pos, strlen(pos) + 1);
    else
      break;

  return pos;
}

static char *
subst(char *tgt, const char *oldstr, const char *newstr)
{
  /* tgt is a malloc()d area... realloc() as necessary */
  char *word, *ntgt;
  int ltgt, loldstr, lnewstr, pos;

  if ((word = strstrword(tgt, oldstr)) == NULL)
    return tgt;

  ltgt = strlen(tgt) + 1;
  loldstr = strlen(oldstr);
  lnewstr = strlen(newstr);
  do {
    pos = word - tgt;
    if (loldstr > lnewstr)
      bcopy(word + loldstr, word + lnewstr, ltgt - pos - loldstr);
    if (loldstr != lnewstr) {
      ntgt = realloc(tgt, ltgt += lnewstr - loldstr);
      if (ntgt == NULL)
        break;			/* Oh wonderful ! */
      word = ntgt + pos;
      tgt = ntgt;
    }
    if (lnewstr > loldstr)
      bcopy(word + loldstr, word + lnewstr, ltgt - pos - lnewstr);
    bcopy(newstr, word, lnewstr);
  } while ((word = strstrword(word, oldstr)));

  return tgt;
}

static char *
substip(char *tgt, const char *oldstr, struct in_addr ip)
{
  return subst(tgt, oldstr, inet_ntoa(ip));
}

static char *
substlong(char *tgt, const char *oldstr, long l)
{
  char buf[23];

  snprintf(buf, sizeof buf, "%ld", l);

  return subst(tgt, oldstr, buf);
}

static char *
substull(char *tgt, const char *oldstr, unsigned long long ull)
{
  char buf[21];

  snprintf(buf, sizeof buf, "%llu", ull);

  return subst(tgt, oldstr, buf);
}


#ifndef NOINET6
static char *
substipv6(char *tgt, const char *oldstr, const struct ncpaddr *ip)
{
    return subst(tgt, oldstr, ncpaddr_ntoa(ip));
}

#ifndef NORADIUS
static char *
substipv6prefix(char *tgt, const char *oldstr, const uint8_t *ipv6prefix)
{
  uint8_t ipv6addr[INET6_ADDRSTRLEN];
  uint8_t prefix[INET6_ADDRSTRLEN + sizeof("/128") - 1];

  if (ipv6prefix) {
    inet_ntop(AF_INET6, &ipv6prefix[2], ipv6addr, sizeof(ipv6addr));
    snprintf(prefix, sizeof(prefix), "%s/%d", ipv6addr, ipv6prefix[1]);
  } else
    prefix[0] = '\0';
  return subst(tgt, oldstr, prefix);
}
#endif
#endif

void
command_Expand(char **nargv, int argc, char const *const *oargv,
               struct bundle *bundle, int inc0, pid_t pid)
{
  int arg, secs;
  char uptime[20];
  unsigned long long oin, oout, pin, pout;

  if (inc0)
    arg = 0;		/* Start at arg 0 */
  else {
    nargv[0] = strdup(oargv[0]);
    arg = 1;
  }

  secs = bundle_Uptime(bundle);
  snprintf(uptime, sizeof uptime, "%d:%02d:%02d",
           secs / 3600, (secs / 60) % 60, secs % 60);
  oin = bundle->ncp.ipcp.throughput.OctetsIn;
  oout = bundle->ncp.ipcp.throughput.OctetsOut;
  pin = bundle->ncp.ipcp.throughput.PacketsIn;
  pout = bundle->ncp.ipcp.throughput.PacketsOut;
#ifndef NOINET6
  oin += bundle->ncp.ipv6cp.throughput.OctetsIn;
  oout += bundle->ncp.ipv6cp.throughput.OctetsOut;
  pin += bundle->ncp.ipv6cp.throughput.PacketsIn;
  pout += bundle->ncp.ipv6cp.throughput.PacketsOut;
#endif

  for (; arg < argc; arg++) {
    nargv[arg] = strdup(oargv[arg]);
    nargv[arg] = subst(nargv[arg], "AUTHNAME", bundle->cfg.auth.name);
    nargv[arg] = substip(nargv[arg], "DNS0", bundle->ncp.ipcp.ns.dns[0]);
    nargv[arg] = substip(nargv[arg], "DNS1", bundle->ncp.ipcp.ns.dns[1]);
    nargv[arg] = subst(nargv[arg], "ENDDISC",
                       mp_Enddisc(bundle->ncp.mp.cfg.enddisc.class,
                                  bundle->ncp.mp.cfg.enddisc.address,
                                  bundle->ncp.mp.cfg.enddisc.len));
    nargv[arg] = substip(nargv[arg], "HISADDR", bundle->ncp.ipcp.peer_ip);
#ifndef NOINET6
    nargv[arg] = substipv6(nargv[arg], "HISADDR6", &bundle->ncp.ipv6cp.hisaddr);
#endif
    nargv[arg] = subst(nargv[arg], "INTERFACE", bundle->iface->name);
    nargv[arg] = substull(nargv[arg], "IPOCTETSIN",
                          bundle->ncp.ipcp.throughput.OctetsIn);
    nargv[arg] = substull(nargv[arg], "IPOCTETSOUT",
                          bundle->ncp.ipcp.throughput.OctetsOut);
    nargv[arg] = substull(nargv[arg], "IPPACKETSIN",
                          bundle->ncp.ipcp.throughput.PacketsIn);
    nargv[arg] = substull(nargv[arg], "IPPACKETSOUT",
                          bundle->ncp.ipcp.throughput.PacketsOut);
#ifndef NOINET6
    nargv[arg] = substull(nargv[arg], "IPV6OCTETSIN",
                          bundle->ncp.ipv6cp.throughput.OctetsIn);
    nargv[arg] = substull(nargv[arg], "IPV6OCTETSOUT",
                          bundle->ncp.ipv6cp.throughput.OctetsOut);
    nargv[arg] = substull(nargv[arg], "IPV6PACKETSIN",
                          bundle->ncp.ipv6cp.throughput.PacketsIn);
    nargv[arg] = substull(nargv[arg], "IPV6PACKETSOUT",
                          bundle->ncp.ipv6cp.throughput.PacketsOut);
#endif
    nargv[arg] = subst(nargv[arg], "LABEL", bundle_GetLabel(bundle));
    nargv[arg] = substip(nargv[arg], "MYADDR", bundle->ncp.ipcp.my_ip);
#ifndef NOINET6
    nargv[arg] = substipv6(nargv[arg], "MYADDR6", &bundle->ncp.ipv6cp.myaddr);
#ifndef NORADIUS
    nargv[arg] = substipv6prefix(nargv[arg], "IPV6PREFIX",
				 bundle->radius.ipv6prefix);
#endif
#endif
    nargv[arg] = substull(nargv[arg], "OCTETSIN", oin);
    nargv[arg] = substull(nargv[arg], "OCTETSOUT", oout);
    nargv[arg] = substull(nargv[arg], "PACKETSIN", pin);
    nargv[arg] = substull(nargv[arg], "PACKETSOUT", pout);
    nargv[arg] = subst(nargv[arg], "PEER_ENDDISC",
                       mp_Enddisc(bundle->ncp.mp.peer.enddisc.class,
                                  bundle->ncp.mp.peer.enddisc.address,
                                  bundle->ncp.mp.peer.enddisc.len));
    nargv[arg] = substlong(nargv[arg], "PROCESSID", pid);
    if (server.cfg.port)
      nargv[arg] = substlong(nargv[arg], "SOCKNAME", server.cfg.port);
    else
      nargv[arg] = subst(nargv[arg], "SOCKNAME", server.cfg.sockname);
    nargv[arg] = subst(nargv[arg], "UPTIME", uptime);
    nargv[arg] = subst(nargv[arg], "USER", bundle->ncp.mp.peer.authname);
    nargv[arg] = subst(nargv[arg], "VERSION", Version);
  }
  nargv[arg] = NULL;
}

void
command_Free(int argc, char **argv)
{
  while (argc) {
    free(*argv);
    argc--;
    argv++;
  }
}

static int
ShellCommand(struct cmdargs const *arg, int bg)
{
  const char *shell;
  pid_t shpid, pid;

#ifdef SHELL_ONLY_INTERACTIVELY
  /* we're only allowed to shell when we run ppp interactively */
  if (arg->prompt && arg->prompt->owner) {
    log_Printf(LogWARN, "Can't start a shell from a network connection\n");
    return 1;
  }
#endif

  if (arg->argc == arg->argn) {
    if (!arg->prompt) {
      log_Printf(LogWARN, "Can't start an interactive shell from"
                " a config file\n");
      return 1;
    } else if (arg->prompt->owner) {
      log_Printf(LogWARN, "Can't start an interactive shell from"
                " a socket connection\n");
      return 1;
    } else if (bg) {
      log_Printf(LogWARN, "Can only start an interactive shell in"
		" the foreground mode\n");
      return 1;
    }
  }

  pid = getpid();
  if ((shpid = fork()) == 0) {
    int i, fd;

    if ((shell = getenv("SHELL")) == NULL)
      shell = _PATH_BSHELL;

    timer_TermService();

    if (arg->prompt)
      fd = arg->prompt->fd_out;
    else if ((fd = open(_PATH_DEVNULL, O_RDWR)) == -1) {
      log_Printf(LogALERT, "Failed to open %s: %s\n",
                _PATH_DEVNULL, strerror(errno));
      exit(1);
    }
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    for (i = getdtablesize(); i > STDERR_FILENO; i--)
      fcntl(i, F_SETFD, 1);

#ifndef NOSUID
    setuid(ID0realuid());
#endif
    if (arg->argc > arg->argn) {
      /* substitute pseudo args */
      char *argv[MAXARGS];
      int argc = arg->argc - arg->argn;

      if (argc >= (int)(sizeof argv / sizeof argv[0])) {
        argc = sizeof argv / sizeof argv[0] - 1;
        log_Printf(LogWARN, "Truncating shell command to %d args\n", argc);
      }
      command_Expand(argv, argc, arg->argv + arg->argn, arg->bundle, 0, pid);
      if (bg) {
	pid_t p;

	p = getpid();
	if (daemon(1, 1) == -1) {
	  log_Printf(LogERROR, "%ld: daemon: %s\n", (long)p, strerror(errno));
	  exit(1);
	}
      } else if (arg->prompt)
        printf("ppp: Pausing until %s finishes\n", arg->argv[arg->argn]);
      execvp(argv[0], argv);
    } else {
      if (arg->prompt)
        printf("ppp: Pausing until %s finishes\n", shell);
      prompt_TtyOldMode(arg->prompt);
      execl(shell, shell, (char *)NULL);
    }

    log_Printf(LogWARN, "exec() of %s failed: %s\n",
              arg->argc > arg->argn ? arg->argv[arg->argn] : shell,
              strerror(errno));
    _exit(255);
  }

  if (shpid == (pid_t)-1)
    log_Printf(LogERROR, "Fork failed: %s\n", strerror(errno));
  else {
    int status;
    waitpid(shpid, &status, 0);
  }

  if (arg->prompt && !arg->prompt->owner)
    prompt_TtyCommandMode(arg->prompt);

  return 0;
}

static int
BgShellCommand(struct cmdargs const *arg)
{
  if (arg->argc == arg->argn)
    return -1;
  return ShellCommand(arg, 1);
}

static int
FgShellCommand(struct cmdargs const *arg)
{
  return ShellCommand(arg, 0);
}

static int
ResolvCommand(struct cmdargs const *arg)
{
  if (arg->argc == arg->argn + 1) {
    if (!strcasecmp(arg->argv[arg->argn], "reload"))
      ipcp_LoadDNS(&arg->bundle->ncp.ipcp);
    else if (!strcasecmp(arg->argv[arg->argn], "restore"))
      ipcp_RestoreDNS(&arg->bundle->ncp.ipcp);
    else if (!strcasecmp(arg->argv[arg->argn], "rewrite"))
      ipcp_WriteDNS(&arg->bundle->ncp.ipcp);
    else if (!strcasecmp(arg->argv[arg->argn], "readonly"))
      arg->bundle->ncp.ipcp.ns.writable = 0;
    else if (!strcasecmp(arg->argv[arg->argn], "writable"))
      arg->bundle->ncp.ipcp.ns.writable = 1;
    else
      return -1;

    return 0;
  }

  return -1;
}

#ifndef NONAT
static struct cmdtab const NatCommands[] =
{
  {"addr", NULL, nat_RedirectAddr, LOCAL_AUTH,
   "static address translation", "nat addr [addr_local addr_alias]", NULL},
  {"deny_incoming", NULL, NatOption, LOCAL_AUTH,
   "stop incoming connections", "nat deny_incoming yes|no",
   (const void *) PKT_ALIAS_DENY_INCOMING},
  {"enable", NULL, NatEnable, LOCAL_AUTH,
   "enable NAT", "nat enable yes|no", NULL},
  {"log", NULL, NatOption, LOCAL_AUTH,
   "log NAT link creation", "nat log yes|no",
   (const void *) PKT_ALIAS_LOG},
  {"port", NULL, nat_RedirectPort, LOCAL_AUTH, "port redirection",
   "nat port proto localaddr:port[-port] aliasport[-aliasport]", NULL},
  {"proto", NULL, nat_RedirectProto, LOCAL_AUTH, "protocol redirection",
   "nat proto proto localIP [publicIP [remoteIP]]", NULL},
  {"proxy", NULL, nat_ProxyRule, LOCAL_AUTH,
   "proxy control", "nat proxy server host[:port] ...", NULL},
#ifndef NO_FW_PUNCH
  {"punch_fw", NULL, nat_PunchFW, LOCAL_AUTH,
   "firewall control", "nat punch_fw [base count]", NULL},
#endif
  {"skinny_port", NULL, nat_SkinnyPort, LOCAL_AUTH,
   "TCP port used by Skinny Station protocol", "nat skinny_port [port]", NULL},
  {"same_ports", NULL, NatOption, LOCAL_AUTH,
   "try to leave port numbers unchanged", "nat same_ports yes|no",
   (const void *) PKT_ALIAS_SAME_PORTS},
  {"target", NULL, nat_SetTarget, LOCAL_AUTH,
   "Default address for incoming connections", "nat target addr", NULL},
  {"unregistered_only", NULL, NatOption, LOCAL_AUTH,
   "translate unregistered (private) IP address space only",
   "nat unregistered_only yes|no",
   (const void *) PKT_ALIAS_UNREGISTERED_ONLY},
  {"use_sockets", NULL, NatOption, LOCAL_AUTH,
   "allocate host sockets", "nat use_sockets yes|no",
   (const void *) PKT_ALIAS_USE_SOCKETS},
  {"help", "?", HelpCommand, LOCAL_AUTH | LOCAL_NO_AUTH,
   "Display this message", "nat help|? [command]", NatCommands},
  {NULL, NULL, NULL, 0, NULL, NULL, NULL},
};
#endif

static struct cmdtab const AllowCommands[] = {
  {"modes", "mode", AllowModes, LOCAL_AUTH,
  "Only allow certain ppp modes", "allow modes mode...", NULL},
  {"users", "user", AllowUsers, LOCAL_AUTH,
  "Only allow ppp access to certain users", "allow users logname...", NULL},
  {"help", "?", HelpCommand, LOCAL_AUTH | LOCAL_NO_AUTH,
  "Display this message", "allow help|? [command]", AllowCommands},
  {NULL, NULL, NULL, 0, NULL, NULL, NULL},
};

static struct cmdtab const IfaceCommands[] =
{
  {"add", NULL, IfaceAddCommand, LOCAL_AUTH,
   "Add iface address", "iface add addr[/bits| mask] peer", NULL},
  {NULL, "add!", IfaceAddCommand, LOCAL_AUTH,
   "Add or change an iface address", "iface add! addr[/bits| mask] peer",
   (void *)1},
  {"clear", NULL, IfaceClearCommand, LOCAL_AUTH,
   "Clear iface address(es)", "iface clear [INET | INET6]", NULL},
  {"delete", "rm", IfaceDeleteCommand, LOCAL_AUTH,
   "Delete iface address", "iface delete addr", NULL},
  {NULL, "rm!", IfaceDeleteCommand, LOCAL_AUTH,
   "Delete iface address", "iface delete addr", (void *)1},
  {NULL, "delete!", IfaceDeleteCommand, LOCAL_AUTH,
   "Delete iface address", "iface delete addr", (void *)1},
  {"name", NULL, IfaceNameCommand, LOCAL_AUTH,
    "Set iface name", "iface name name", NULL},
  {"description", NULL, iface_Descr, LOCAL_AUTH,
    "Set iface description", "iface description text", NULL},
  {"show", NULL, iface_Show, LOCAL_AUTH,
   "Show iface address(es)", "iface show", NULL},
  {"help", "?", HelpCommand, LOCAL_AUTH | LOCAL_NO_AUTH,
   "Display this message", "nat help|? [command]", IfaceCommands},
  {NULL, NULL, NULL, 0, NULL, NULL, NULL},
};

static struct cmdtab const Commands[] = {
  {"accept", NULL, NegotiateCommand, LOCAL_AUTH | LOCAL_CX_OPT,
  "accept option request", "accept option ..", NULL},
  {"add", NULL, AddCommand, LOCAL_AUTH,
  "add route", "add dest mask gateway", NULL},
  {NULL, "add!", AddCommand, LOCAL_AUTH,
  "add or change route", "add! dest mask gateway", (void *)1},
  {"allow", "auth", RunListCommand, LOCAL_AUTH,
  "Allow ppp access", "allow users|modes ....", AllowCommands},
  {"bg", "!bg", BgShellCommand, LOCAL_AUTH,
  "Run a background command", "[!]bg command", NULL},
  {"clear", NULL, ClearCommand, LOCAL_AUTH | LOCAL_CX_OPT,
  "Clear throughput statistics",
  "clear ipcp|ipv6cp|physical [current|overall|peak]...", NULL},
  {"clone", NULL, CloneCommand, LOCAL_AUTH | LOCAL_CX,
  "Clone a link", "clone newname...", NULL},
  {"close", NULL, CloseCommand, LOCAL_AUTH | LOCAL_CX_OPT,
  "Close an FSM", "close [lcp|ccp]", NULL},
  {"delete", NULL, DeleteCommand, LOCAL_AUTH,
  "delete route", "delete dest", NULL},
  {NULL, "delete!", DeleteCommand, LOCAL_AUTH,
  "delete a route if it exists", "delete! dest", (void *)1},
  {"deny", NULL, NegotiateCommand, LOCAL_AUTH | LOCAL_CX_OPT,
  "Deny option request", "deny option ..", NULL},
  {"dial", "call", DialCommand, LOCAL_AUTH | LOCAL_CX_OPT,
  "Dial and login", "dial|call [system ...]", NULL},
  {"disable", NULL, NegotiateCommand, LOCAL_AUTH | LOCAL_CX_OPT,
  "Disable option", "disable option ..", NULL},
  {"down", NULL, DownCommand, LOCAL_AUTH | LOCAL_CX_OPT,
  "Generate a down event", "down [ccp|lcp]", NULL},
  {"enable", NULL, NegotiateCommand, LOCAL_AUTH | LOCAL_CX_OPT,
  "Enable option", "enable option ..", NULL},
  {"ident", NULL, IdentCommand, LOCAL_AUTH | LOCAL_CX,
  "Set the link identity", "ident text...", NULL},
  {"iface", "interface", RunListCommand, LOCAL_AUTH,
  "interface control", "iface option ...", IfaceCommands},
  {"link", "datalink", LinkCommand, LOCAL_AUTH,
  "Link specific commands", "link name command ...", NULL},
  {"load", NULL, LoadCommand, LOCAL_AUTH | LOCAL_CX_OPT,
  "Load settings", "load [system ...]", NULL},
  {"log", NULL, LogCommand, LOCAL_AUTH | LOCAL_CX_OPT,
  "log information", "log word ...", NULL},
#ifndef NONAT
  {"nat", "alias", RunListCommand, LOCAL_AUTH,
  "NAT control", "nat option yes|no", NatCommands},
#endif
  {"open", NULL, OpenCommand, LOCAL_AUTH | LOCAL_CX_OPT,
  "Open an FSM", "open! [lcp|ccp|ipcp]", (void *)1},
  {"passwd", NULL, PasswdCommand, LOCAL_NO_AUTH,
  "Password for manipulation", "passwd LocalPassword", NULL},
  {"quit", "bye", QuitCommand, LOCAL_AUTH | LOCAL_NO_AUTH,
  "Quit PPP program", "quit|bye [all]", NULL},
  {"remove", "rm", RemoveCommand, LOCAL_AUTH | LOCAL_CX,
  "Remove a link", "remove", NULL},
  {"rename", "mv", RenameCommand, LOCAL_AUTH | LOCAL_CX,
  "Rename a link", "rename name", NULL},
  {"resolv", NULL, ResolvCommand, LOCAL_AUTH,
  "Manipulate resolv.conf", "resolv readonly|reload|restore|rewrite|writable",
  NULL},
  {"save", NULL, SaveCommand, LOCAL_AUTH,
  "Save settings", "save", NULL},
  {"sendident", NULL, SendIdentification, LOCAL_AUTH | LOCAL_CX,
  "Transmit the link identity", "sendident", NULL},
  {"set", "setup", SetCommand, LOCAL_AUTH | LOCAL_CX_OPT,
  "Set parameters", "set[up] var value", NULL},
  {"shell", "!", FgShellCommand, LOCAL_AUTH,
  "Run a subshell", "shell|! [sh command]", NULL},
  {"show", NULL, ShowCommand, LOCAL_AUTH | LOCAL_CX_OPT,
  "Show status and stats", "show var", NULL},
  {"term", NULL, TerminalCommand, LOCAL_AUTH | LOCAL_CX,
  "Enter terminal mode", "term", NULL},
  {"help", "?", HelpCommand, LOCAL_AUTH | LOCAL_NO_AUTH,
  "Display this message", "help|? [command]", Commands},
  {NULL, NULL, NULL, 0, NULL, NULL, NULL},
};

static int
ShowEscape(struct cmdargs const *arg)
{
  if (arg->cx->physical->async.cfg.EscMap[32]) {
    int code, bit;
    const char *sep = "";

    for (code = 0; code < 32; code++)
      if (arg->cx->physical->async.cfg.EscMap[code])
	for (bit = 0; bit < 8; bit++)
	  if (arg->cx->physical->async.cfg.EscMap[code] & (1 << bit)) {
	    prompt_Printf(arg->prompt, "%s0x%02x", sep, (code << 3) + bit);
            sep = ", ";
          }
    prompt_Printf(arg->prompt, "\n");
  }
  return 0;
}

static int
ShowTimerList(struct cmdargs const *arg)
{
  timer_Show(0, arg->prompt);
  return 0;
}

static int
ShowStopped(struct cmdargs const *arg)
{
  prompt_Printf(arg->prompt, " Stopped Timer:  LCP: ");
  if (!arg->cx->physical->link.lcp.fsm.StoppedTimer.load)
    prompt_Printf(arg->prompt, "Disabled");
  else
    prompt_Printf(arg->prompt, "%ld secs",
                  arg->cx->physical->link.lcp.fsm.StoppedTimer.load / SECTICKS);

  prompt_Printf(arg->prompt, ", CCP: ");
  if (!arg->cx->physical->link.ccp.fsm.StoppedTimer.load)
    prompt_Printf(arg->prompt, "Disabled");
  else
    prompt_Printf(arg->prompt, "%ld secs",
                  arg->cx->physical->link.ccp.fsm.StoppedTimer.load / SECTICKS);

  prompt_Printf(arg->prompt, "\n");

  return 0;
}

static int
ShowVersion(struct cmdargs const *arg)
{
  prompt_Printf(arg->prompt, "PPP Version %s\n", Version);
  return 0;
}

static int
ShowProtocolStats(struct cmdargs const *arg)
{
  struct link *l = command_ChooseLink(arg);

  prompt_Printf(arg->prompt, "%s:\n", l->name);
  link_ReportProtocolStatus(l, arg->prompt);
  return 0;
}

static struct cmdtab const ShowCommands[] = {
  {"bundle", NULL, bundle_ShowStatus, LOCAL_AUTH,
  "bundle details", "show bundle", NULL},
  {"ccp", NULL, ccp_ReportStatus, LOCAL_AUTH | LOCAL_CX_OPT,
  "CCP status", "show cpp", NULL},
  {"compress", NULL, sl_Show, LOCAL_AUTH,
  "VJ compression stats", "show compress", NULL},
  {"escape", NULL, ShowEscape, LOCAL_AUTH | LOCAL_CX,
  "escape characters", "show escape", NULL},
  {"filter", NULL, filter_Show, LOCAL_AUTH,
  "packet filters", "show filter [in|out|dial|alive]", NULL},
  {"hdlc", NULL, hdlc_ReportStatus, LOCAL_AUTH | LOCAL_CX,
  "HDLC errors", "show hdlc", NULL},
  {"iface", "interface", iface_Show, LOCAL_AUTH,
  "Interface status", "show iface", NULL},
  {"ipcp", NULL, ipcp_Show, LOCAL_AUTH,
  "IPCP status", "show ipcp", NULL},
#ifndef NOINET6
  {"ipv6cp", NULL, ipv6cp_Show, LOCAL_AUTH,
  "IPV6CP status", "show ipv6cp", NULL},
#endif
  {"layers", NULL, link_ShowLayers, LOCAL_AUTH | LOCAL_CX_OPT,
  "Protocol layers", "show layers", NULL},
  {"lcp", NULL, lcp_ReportStatus, LOCAL_AUTH | LOCAL_CX,
  "LCP status", "show lcp", NULL},
  {"link", "datalink", datalink_Show, LOCAL_AUTH | LOCAL_CX,
  "(high-level) link info", "show link", NULL},
  {"links", NULL, bundle_ShowLinks, LOCAL_AUTH,
  "available link names", "show links", NULL},
  {"log", NULL, log_ShowLevel, LOCAL_AUTH,
  "log levels", "show log", NULL},
  {"mem", NULL, mbuf_Show, LOCAL_AUTH,
  "mbuf allocations", "show mem", NULL},
  {"ncp", NULL, ncp_Show, LOCAL_AUTH,
  "NCP status", "show ncp", NULL},
  {"physical", NULL, physical_ShowStatus, LOCAL_AUTH | LOCAL_CX,
  "(low-level) link info", "show physical", NULL},
  {"mp", "multilink", mp_ShowStatus, LOCAL_AUTH,
  "multilink setup", "show mp", NULL},
  {"proto", NULL, ShowProtocolStats, LOCAL_AUTH | LOCAL_CX_OPT,
  "protocol summary", "show proto", NULL},
  {"route", NULL, route_Show, LOCAL_AUTH,
  "routing table", "show route", NULL},
  {"stopped", NULL, ShowStopped, LOCAL_AUTH | LOCAL_CX,
  "STOPPED timeout", "show stopped", NULL},
  {"timers", NULL, ShowTimerList, LOCAL_AUTH,
  "alarm timers", "show timers", NULL},
  {"version", NULL, ShowVersion, LOCAL_NO_AUTH | LOCAL_AUTH,
  "version string", "show version", NULL},
  {"who", NULL, log_ShowWho, LOCAL_AUTH,
  "client list", "show who", NULL},
  {"help", "?", HelpCommand, LOCAL_NO_AUTH | LOCAL_AUTH,
  "Display this message", "show help|? [command]", ShowCommands},
  {NULL, NULL, NULL, 0, NULL, NULL, NULL},
};

static struct cmdtab const *
FindCommand(struct cmdtab const *cmds, const char *str, int *pmatch)
{
  int nmatch;
  int len;
  struct cmdtab const *found;

  found = NULL;
  len = strlen(str);
  nmatch = 0;
  while (cmds->func) {
    if (cmds->name && strncasecmp(str, cmds->name, len) == 0) {
      if (cmds->name[len] == '\0') {
	*pmatch = 1;
	return cmds;
      }
      nmatch++;
      found = cmds;
    } else if (cmds->alias && strncasecmp(str, cmds->alias, len) == 0) {
      if (cmds->alias[len] == '\0') {
	*pmatch = 1;
	return cmds;
      }
      nmatch++;
      found = cmds;
    }
    cmds++;
  }
  *pmatch = nmatch;
  return found;
}

static const char *
mkPrefix(int argc, char const *const *argv, char *tgt, int sz)
{
  int f, tlen, len;

  tlen = 0;
  for (f = 0; f < argc && tlen < sz - 2; f++) {
    if (f)
      tgt[tlen++] = ' ';
    len = strlen(argv[f]);
    if (len > sz - tlen - 1)
      len = sz - tlen - 1;
    strncpy(tgt+tlen, argv[f], len);
    tlen += len;
  }
  tgt[tlen] = '\0';
  return tgt;
}

static int
FindExec(struct bundle *bundle, struct cmdtab const *cmds, int argc, int argn,
         char const *const *argv, struct prompt *prompt, struct datalink *cx)
{
  struct cmdtab const *cmd;
  int val = 1;
  int nmatch;
  struct cmdargs arg;
  char prefix[100];

  cmd = FindCommand(cmds, argv[argn], &nmatch);
  if (nmatch > 1)
    log_Printf(LogWARN, "%s: Ambiguous command\n",
              mkPrefix(argn+1, argv, prefix, sizeof prefix));
  else if (cmd && (!prompt || (cmd->lauth & prompt->auth))) {
    if ((cmd->lauth & LOCAL_CX) && !cx)
      /* We've got no context, but we require it */
      cx = bundle2datalink(bundle, NULL);

    if ((cmd->lauth & LOCAL_CX) && !cx)
      log_Printf(LogWARN, "%s: No context (use the `link' command)\n",
                mkPrefix(argn+1, argv, prefix, sizeof prefix));
    else {
      if (cx && !(cmd->lauth & (LOCAL_CX|LOCAL_CX_OPT))) {
        log_Printf(LogWARN, "%s: Redundant context (%s) ignored\n",
                  mkPrefix(argn+1, argv, prefix, sizeof prefix), cx->name);
        cx = NULL;
      }
      arg.cmdtab = cmds;
      arg.cmd = cmd;
      arg.argc = argc;
      arg.argn = argn+1;
      arg.argv = argv;
      arg.bundle = bundle;
      arg.cx = cx;
      arg.prompt = prompt;
      val = (*cmd->func) (&arg);
    }
  } else
    log_Printf(LogWARN, "%s: Invalid command\n",
              mkPrefix(argn+1, argv, prefix, sizeof prefix));

  if (val == -1)
    log_Printf(LogWARN, "usage: %s\n", cmd->syntax);
  else if (val)
    log_Printf(LogWARN, "%s: Failed %d\n",
              mkPrefix(argn+1, argv, prefix, sizeof prefix), val);

  return val;
}

int
command_Expand_Interpret(char *buff, int nb, char *argv[MAXARGS], int offset)
{
  char buff2[LINE_LEN-offset];

  InterpretArg(buff, buff2);
  strncpy(buff, buff2, LINE_LEN - offset - 1);
  buff[LINE_LEN - offset - 1] = '\0';

  return command_Interpret(buff, nb, argv);
}

int
command_Interpret(char *buff, int nb, char *argv[MAXARGS])
{
  char *cp;

  if (nb > 0) {
    cp = buff + strcspn(buff, "\r\n");
    if (cp)
      *cp = '\0';
    return MakeArgs(buff, argv, MAXARGS, PARSE_REDUCE);
  }
  return 0;
}

static int
arghidden(char const *const *argv, int n)
{
  /* Is arg n of the given command to be hidden from the log ? */

  /* set authkey xxxxx */
  /* set key xxxxx */
  if (n == 2 && !strncasecmp(argv[0], "se", 2) &&
      (!strncasecmp(argv[1], "authk", 5) || !strncasecmp(argv[1], "ke", 2)))
    return 1;

  /* passwd xxxxx */
  if (n == 1 && !strncasecmp(argv[0], "p", 1))
    return 1;

  /* set server port xxxxx .... */
  if (n == 3 && !strncasecmp(argv[0], "se", 2) &&
      !strncasecmp(argv[1], "se", 2))
    return 1;

  return 0;
}

void
command_Run(struct bundle *bundle, int argc, char const *const *argv,
           struct prompt *prompt, const char *label, struct datalink *cx)
{
  if (argc > 0) {
    if (log_IsKept(LogCOMMAND)) {
      char buf[LINE_LEN];
      int f;
      size_t n;

      if (label) {
        strncpy(buf, label, sizeof buf - 3);
        buf[sizeof buf - 3] = '\0';
        strcat(buf, ": ");
        n = strlen(buf);
      } else {
        *buf = '\0';
        n = 0;
      }
      buf[sizeof buf - 1] = '\0';	/* In case we run out of room in buf */

      for (f = 0; f < argc; f++) {
        if (n < sizeof buf - 1 && f)
          buf[n++] = ' ';
        if (arghidden(argv, f))
          strncpy(buf+n, "********", sizeof buf - n - 1);
        else
          strncpy(buf+n, argv[f], sizeof buf - n - 1);
        n += strlen(buf+n);
      }
      log_Printf(LogCOMMAND, "%s\n", buf);
    }
    FindExec(bundle, Commands, argc, 0, argv, prompt, cx);
  }
}

int
command_Decode(struct bundle *bundle, char *buff, int nb, struct prompt *prompt,
              const char *label)
{
  int argc;
  char *argv[MAXARGS];

  if ((argc = command_Expand_Interpret(buff, nb, argv, 0)) < 0)
    return 0;

  command_Run(bundle, argc, (char const *const *)argv, prompt, label, NULL);
  return 1;
}

static int
ShowCommand(struct cmdargs const *arg)
{
  if (!arg->prompt)
    log_Printf(LogWARN, "show: Cannot show without a prompt\n");
  else if (arg->argc > arg->argn)
    FindExec(arg->bundle, ShowCommands, arg->argc, arg->argn, arg->argv,
             arg->prompt, arg->cx);
  else
    prompt_Printf(arg->prompt, "Use ``show ?'' to get a list.\n");

  return 0;
}

static int
TerminalCommand(struct cmdargs const *arg)
{
  if (!arg->prompt) {
    log_Printf(LogWARN, "term: Need a prompt\n");
    return 1;
  }

  if (arg->cx->physical->link.lcp.fsm.state > ST_CLOSED) {
    prompt_Printf(arg->prompt, "LCP state is [%s]\n",
                  State2Nam(arg->cx->physical->link.lcp.fsm.state));
    return 1;
  }

  datalink_Up(arg->cx, 0, 0);
  prompt_TtyTermMode(arg->prompt, arg->cx);
  return 0;
}

static int
QuitCommand(struct cmdargs const *arg)
{
  if (!arg->prompt || prompt_IsController(arg->prompt) ||
      (arg->argc > arg->argn && !strcasecmp(arg->argv[arg->argn], "all") &&
       (arg->prompt->auth & LOCAL_AUTH)))
    Cleanup();
  if (arg->prompt)
    prompt_Destroy(arg->prompt, 1);

  return 0;
}

static int
OpenCommand(struct cmdargs const *arg)
{
  if (arg->argc == arg->argn)
    bundle_Open(arg->bundle, arg->cx ? arg->cx->name : NULL, PHYS_ALL, 1);
  else if (arg->argc == arg->argn + 1) {
    if (!strcasecmp(arg->argv[arg->argn], "lcp")) {
      struct datalink *cx = arg->cx ?
        arg->cx : bundle2datalink(arg->bundle, NULL);
      if (cx) {
        if (cx->physical->link.lcp.fsm.state == ST_OPENED)
          fsm_Reopen(&cx->physical->link.lcp.fsm);
        else
          bundle_Open(arg->bundle, cx->name, PHYS_ALL, 1);
      } else
        log_Printf(LogWARN, "open lcp: You must specify a link\n");
    } else if (!strcasecmp(arg->argv[arg->argn], "ccp")) {
      struct fsm *fp;

      fp = &command_ChooseLink(arg)->ccp.fsm;
      if (fp->link->lcp.fsm.state != ST_OPENED)
        log_Printf(LogWARN, "open: LCP must be open before opening CCP\n");
      else if (fp->state == ST_OPENED)
        fsm_Reopen(fp);
      else {
        fp->open_mode = 0;	/* Not passive any more */
        if (fp->state == ST_STOPPED) {
          fsm_Down(fp);
          fsm_Up(fp);
        } else {
          fsm_Up(fp);
          fsm_Open(fp);
        }
      }
    } else if (!strcasecmp(arg->argv[arg->argn], "ipcp")) {
      if (arg->cx)
        log_Printf(LogWARN, "open ipcp: You need not specify a link\n");
      if (arg->bundle->ncp.ipcp.fsm.state == ST_OPENED)
        fsm_Reopen(&arg->bundle->ncp.ipcp.fsm);
      else
        bundle_Open(arg->bundle, NULL, PHYS_ALL, 1);
    } else
      return -1;
  } else
    return -1;

  return 0;
}

static int
CloseCommand(struct cmdargs const *arg)
{
  if (arg->argc == arg->argn)
    bundle_Close(arg->bundle, arg->cx ? arg->cx->name : NULL, CLOSE_STAYDOWN);
  else if (arg->argc == arg->argn + 1) {
    if (!strcasecmp(arg->argv[arg->argn], "lcp"))
      bundle_Close(arg->bundle, arg->cx ? arg->cx->name : NULL, CLOSE_LCP);
    else if (!strcasecmp(arg->argv[arg->argn], "ccp") ||
             !strcasecmp(arg->argv[arg->argn], "ccp!")) {
      struct fsm *fp;

      fp = &command_ChooseLink(arg)->ccp.fsm;
      if (fp->state == ST_OPENED) {
        fsm_Close(fp);
        if (arg->argv[arg->argn][3] == '!')
          fp->open_mode = 0;		/* Stay ST_CLOSED */
        else
          fp->open_mode = OPEN_PASSIVE;	/* Wait for the peer to start */
      }
    } else
      return -1;
  } else
    return -1;

  return 0;
}

static int
DownCommand(struct cmdargs const *arg)
{
  if (arg->argc == arg->argn) {
      if (arg->cx)
        datalink_Down(arg->cx, CLOSE_STAYDOWN);
      else
        bundle_Down(arg->bundle, CLOSE_STAYDOWN);
  } else if (arg->argc == arg->argn + 1) {
    if (!strcasecmp(arg->argv[arg->argn], "lcp")) {
      if (arg->cx)
        datalink_Down(arg->cx, CLOSE_LCP);
      else
        bundle_Down(arg->bundle, CLOSE_LCP);
    } else if (!strcasecmp(arg->argv[arg->argn], "ccp")) {
      struct fsm *fp = arg->cx ? &arg->cx->physical->link.ccp.fsm :
                                 &arg->bundle->ncp.mp.link.ccp.fsm;
      fsm2initial(fp);
    } else
      return -1;
  } else
    return -1;

  return 0;
}

static int
SetModemSpeed(struct cmdargs const *arg)
{
  long speed;
  char *end;

  if (arg->argc > arg->argn && *arg->argv[arg->argn]) {
    if (arg->argc > arg->argn+1) {
      log_Printf(LogWARN, "SetModemSpeed: Too many arguments\n");
      return -1;
    }
    if (strcasecmp(arg->argv[arg->argn], "sync") == 0) {
      physical_SetSync(arg->cx->physical);
      return 0;
    }
    end = NULL;
    speed = strtol(arg->argv[arg->argn], &end, 10);
    if (*end || speed < 0) {
      log_Printf(LogWARN, "SetModemSpeed: Bad argument \"%s\"",
                arg->argv[arg->argn]);
      return -1;
    }
    if (physical_SetSpeed(arg->cx->physical, speed))
      return 0;
    log_Printf(LogWARN, "%s: Invalid speed\n", arg->argv[arg->argn]);
  } else
    log_Printf(LogWARN, "SetModemSpeed: No speed specified\n");

  return -1;
}

static int
SetStoppedTimeout(struct cmdargs const *arg)
{
  struct link *l = &arg->cx->physical->link;

  l->lcp.fsm.StoppedTimer.load = 0;
  l->ccp.fsm.StoppedTimer.load = 0;
  if (arg->argc <= arg->argn+2) {
    if (arg->argc > arg->argn) {
      l->lcp.fsm.StoppedTimer.load = atoi(arg->argv[arg->argn]) * SECTICKS;
      if (arg->argc > arg->argn+1)
        l->ccp.fsm.StoppedTimer.load = atoi(arg->argv[arg->argn+1]) * SECTICKS;
    }
    return 0;
  }
  return -1;
}

static int
SetServer(struct cmdargs const *arg)
{
  int res = -1;

  if (arg->argc > arg->argn && arg->argc < arg->argn+4) {
    const char *port, *passwd, *mask;
    size_t mlen;

    /* What's what ? */
    port = arg->argv[arg->argn];
    if (arg->argc == arg->argn + 2) {
      passwd = arg->argv[arg->argn+1];
      mask = NULL;
    } else if (arg->argc == arg->argn + 3) {
      passwd = arg->argv[arg->argn+1];
      mask = arg->argv[arg->argn+2];
      mlen = strlen(mask);
      if (mlen == 0 || mlen > 4 || strspn(mask, "01234567") != mlen ||
          (mlen == 4 && *mask != '0')) {
        log_Printf(LogWARN, "%s %s: %s: Invalid mask\n",
                   arg->argv[arg->argn - 2], arg->argv[arg->argn - 1], mask);
        return -1;
      }
    } else if (arg->argc != arg->argn + 1)
      return -1;
    else if (strcasecmp(port, "none") == 0) {
      if (server_Clear(arg->bundle))
        log_Printf(LogPHASE, "Disabled server socket\n");
      return 0;
    } else if (strcasecmp(port, "open") == 0) {
      switch (server_Reopen(arg->bundle)) {
        case SERVER_OK:
          return 0;
        case SERVER_FAILED:
          log_Printf(LogWARN, "Failed to reopen server port\n");
          return 1;
        case SERVER_UNSET:
          log_Printf(LogWARN, "Cannot reopen unset server socket\n");
          return 1;
        default:
          break;
      }
      return -1;
    } else if (strcasecmp(port, "closed") == 0) {
      if (server_Close(arg->bundle))
        log_Printf(LogPHASE, "Closed server socket\n");
      else
        log_Printf(LogWARN, "Server socket not open\n");

      return 0;
    } else
      return -1;

    strncpy(server.cfg.passwd, passwd, sizeof server.cfg.passwd - 1);
    server.cfg.passwd[sizeof server.cfg.passwd - 1] = '\0';

    if (*port == '/') {
      mode_t imask;
      char *ptr, name[LINE_LEN + 12];

      if (mask == NULL)
        imask = (mode_t)-1;
      else for (imask = mlen = 0; mask[mlen]; mlen++)
        imask = (imask * 8) + mask[mlen] - '0';

      ptr = strstr(port, "%d");
      if (ptr) {
        snprintf(name, sizeof name, "%.*s%d%s",
                 (int)(ptr - port), port, arg->bundle->unit, ptr + 2);
        port = name;
      }
      res = server_LocalOpen(arg->bundle, port, imask);
    } else {
      int iport, add = 0;

      if (mask != NULL)
        return -1;

      if (*port == '+') {
        port++;
        add = 1;
      }
      if (strspn(port, "0123456789") != strlen(port)) {
        struct servent *s;

        if ((s = getservbyname(port, "tcp")) == NULL) {
	  iport = 0;
	  log_Printf(LogWARN, "%s: Invalid port or service\n", port);
	} else
	  iport = ntohs(s->s_port);
      } else
        iport = atoi(port);

      if (iport) {
        if (add)
          iport += arg->bundle->unit;
        res = server_TcpOpen(arg->bundle, iport);
      } else
        res = -1;
    }
  }

  return res;
}

static int
SetEscape(struct cmdargs const *arg)
{
  int code;
  int argc = arg->argc - arg->argn;
  char const *const *argv = arg->argv + arg->argn;

  for (code = 0; code < 33; code++)
    arg->cx->physical->async.cfg.EscMap[code] = 0;

  while (argc-- > 0) {
    sscanf(*argv++, "%x", &code);
    code &= 0xff;
    arg->cx->physical->async.cfg.EscMap[code >> 3] |= (1 << (code & 7));
    arg->cx->physical->async.cfg.EscMap[32] = 1;
  }
  return 0;
}

static int
SetInterfaceAddr(struct cmdargs const *arg)
{
  struct ncp *ncp = &arg->bundle->ncp;
  struct ncpaddr ncpaddr;
  const char *hisaddr;

  if (arg->argc > arg->argn + 4)
    return -1;

  hisaddr = NULL;
  memset(&ncp->ipcp.cfg.my_range, '\0', sizeof ncp->ipcp.cfg.my_range);
  memset(&ncp->ipcp.cfg.peer_range, '\0', sizeof ncp->ipcp.cfg.peer_range);
  ncp->ipcp.cfg.HaveTriggerAddress = 0;
  ncp->ipcp.cfg.netmask.s_addr = INADDR_ANY;
  iplist_reset(&ncp->ipcp.cfg.peer_list);

  if (arg->argc > arg->argn) {
    if (!ncprange_aton(&ncp->ipcp.cfg.my_range, ncp, arg->argv[arg->argn]))
      return 1;
    if (arg->argc > arg->argn+1) {
      hisaddr = arg->argv[arg->argn+1];
      if (arg->argc > arg->argn+2) {
        ncp->ipcp.ifmask = ncp->ipcp.cfg.netmask =
          GetIpAddr(arg->argv[arg->argn+2]);
	if (arg->argc > arg->argn+3) {
	  ncp->ipcp.cfg.TriggerAddress = GetIpAddr(arg->argv[arg->argn+3]);
	  ncp->ipcp.cfg.HaveTriggerAddress = 1;
	}
      }
    }
  }

  /* 0.0.0.0 means any address (0 bits) */
  ncprange_getaddr(&ncp->ipcp.cfg.my_range, &ncpaddr);
  ncpaddr_getip4(&ncpaddr, &ncp->ipcp.my_ip);
  if (ncp->ipcp.my_ip.s_addr == INADDR_ANY)
    ncprange_setwidth(&ncp->ipcp.cfg.my_range, 0);
  bundle_AdjustFilters(arg->bundle, &ncpaddr, NULL);

  if (hisaddr && !ipcp_UseHisaddr(arg->bundle, hisaddr,
                                  arg->bundle->phys_type.all & PHYS_AUTO))
    return 4;

  return 0;
}

static int
SetRetry(int argc, char const *const *argv, u_int *timeout, u_int *maxreq,
          u_int *maxtrm, int def)
{
  if (argc == 0) {
    *timeout = DEF_FSMRETRY;
    *maxreq = def;
    if (maxtrm != NULL)
      *maxtrm = def;
  } else {
    long l = atol(argv[0]);

    if (l < MIN_FSMRETRY) {
      log_Printf(LogWARN, "%ld: Invalid FSM retry period - min %d\n",
                 l, MIN_FSMRETRY);
      return 1;
    } else
      *timeout = l;

    if (argc > 1) {
      l = atol(argv[1]);
      if (l < 1) {
        log_Printf(LogWARN, "%ld: Invalid FSM REQ tries - changed to 1\n", l);
        l = 1;
      }
      *maxreq = l;

      if (argc > 2 && maxtrm != NULL) {
        l = atol(argv[2]);
        if (l < 1) {
          log_Printf(LogWARN, "%ld: Invalid FSM TRM tries - changed to 1\n", l);
          l = 1;
        }
        *maxtrm = l;
      }
    }
  }

  return 0;
}

static int
SetVariable(struct cmdargs const *arg)
{
  long long_val, param = (long)arg->cmd->args;
  int mode, dummyint, f, first, res;
  u_short *change;
  const char *argp;
  struct datalink *cx = arg->cx;	/* LOCAL_CX uses this */
  struct link *l = command_ChooseLink(arg);	/* LOCAL_CX_OPT uses this */
  struct in_addr *ipaddr;
  struct ncpaddr ncpaddr[2];

  if (arg->argc > arg->argn)
    argp = arg->argv[arg->argn];
  else
    argp = "";

  res = 0;

  if ((arg->cmd->lauth & LOCAL_CX) && !cx) {
    log_Printf(LogWARN, "set %s: No context (use the `link' command)\n",
              arg->cmd->name);
    return 1;
  } else if (cx && !(arg->cmd->lauth & (LOCAL_CX|LOCAL_CX_OPT))) {
    log_Printf(LogWARN, "set %s: Redundant context (%s) ignored\n",
              arg->cmd->name, cx->name);
    cx = NULL;
  }

  switch (param) {
  case VAR_AUTHKEY:
    strncpy(arg->bundle->cfg.auth.key, argp,
            sizeof arg->bundle->cfg.auth.key - 1);
    arg->bundle->cfg.auth.key[sizeof arg->bundle->cfg.auth.key - 1] = '\0';
    break;

  case VAR_AUTHNAME:
    switch (bundle_Phase(arg->bundle)) {
      default:
        log_Printf(LogWARN, "Altering authname while at phase %s\n",
                   bundle_PhaseName(arg->bundle));
        /* drop through */
      case PHASE_DEAD:
      case PHASE_ESTABLISH:
        strncpy(arg->bundle->cfg.auth.name, argp,
                sizeof arg->bundle->cfg.auth.name - 1);
        arg->bundle->cfg.auth.name[sizeof arg->bundle->cfg.auth.name-1] = '\0';
        break;
    }
    break;

  case VAR_AUTOLOAD:
    if (arg->argc == arg->argn + 3) {
      int v1, v2, v3;
      char *end;

      v1 = strtol(arg->argv[arg->argn], &end, 0);
      if (v1 < 0 || *end) {
        log_Printf(LogWARN, "autoload: %s: Invalid min percentage\n",
                   arg->argv[arg->argn]);
        res = 1;
        break;
      }

      v2 = strtol(arg->argv[arg->argn + 1], &end, 0);
      if (v2 < 0 || *end) {
        log_Printf(LogWARN, "autoload: %s: Invalid max percentage\n",
                   arg->argv[arg->argn + 1]);
        res = 1;
        break;
      }
      if (v2 < v1) {
        v3 = v1;
        v1 = v2;
        v2 = v3;
      }

      v3 = strtol(arg->argv[arg->argn + 2], &end, 0);
      if (v3 <= 0 || *end) {
        log_Printf(LogWARN, "autoload: %s: Invalid throughput period\n",
                   arg->argv[arg->argn + 2]);
        res = 1;
        break;
      }

      arg->bundle->ncp.mp.cfg.autoload.min = v1;
      arg->bundle->ncp.mp.cfg.autoload.max = v2;
      arg->bundle->ncp.mp.cfg.autoload.period = v3;
      mp_RestartAutoloadTimer(&arg->bundle->ncp.mp);
    } else {
      log_Printf(LogWARN, "Set autoload requires three arguments\n");
      res = 1;
    }
    break;

  case VAR_DIAL:
    strncpy(cx->cfg.script.dial, argp, sizeof cx->cfg.script.dial - 1);
    cx->cfg.script.dial[sizeof cx->cfg.script.dial - 1] = '\0';
    break;

  case VAR_LOGIN:
    strncpy(cx->cfg.script.login, argp, sizeof cx->cfg.script.login - 1);
    cx->cfg.script.login[sizeof cx->cfg.script.login - 1] = '\0';
    break;

  case VAR_WINSIZE:
    if (arg->argc > arg->argn) {
      l->ccp.cfg.deflate.out.winsize = atoi(arg->argv[arg->argn]);
      if (l->ccp.cfg.deflate.out.winsize < 8 ||
          l->ccp.cfg.deflate.out.winsize > 15) {
          log_Printf(LogWARN, "%d: Invalid outgoing window size\n",
                    l->ccp.cfg.deflate.out.winsize);
          l->ccp.cfg.deflate.out.winsize = 15;
      }
      if (arg->argc > arg->argn+1) {
        l->ccp.cfg.deflate.in.winsize = atoi(arg->argv[arg->argn+1]);
        if (l->ccp.cfg.deflate.in.winsize < 8 ||
            l->ccp.cfg.deflate.in.winsize > 15) {
            log_Printf(LogWARN, "%d: Invalid incoming window size\n",
                      l->ccp.cfg.deflate.in.winsize);
            l->ccp.cfg.deflate.in.winsize = 15;
        }
      } else
        l->ccp.cfg.deflate.in.winsize = 0;
    } else {
      log_Printf(LogWARN, "No window size specified\n");
      res = 1;
    }
    break;

#ifndef NODES
  case VAR_MPPE:
    if (arg->argc > arg->argn + 2) {
      res = -1;
      break;
    }

    if (arg->argc == arg->argn) {
      l->ccp.cfg.mppe.keybits = 0;
      l->ccp.cfg.mppe.state = MPPE_ANYSTATE;
      l->ccp.cfg.mppe.required = 0;
      break;
    }

    if (!strcmp(argp, "*"))
      long_val = 0;
    else {
      long_val = atol(argp);
      if (long_val != 40 && long_val != 56 && long_val != 128) {
        log_Printf(LogWARN, "%s: Invalid bits value\n", argp);
        res = -1;
        break;
      }
    }

    if (arg->argc == arg->argn + 2) {
      if (!strcmp(arg->argv[arg->argn + 1], "*"))
        l->ccp.cfg.mppe.state = MPPE_ANYSTATE;
      else if (!strcasecmp(arg->argv[arg->argn + 1], "stateless"))
        l->ccp.cfg.mppe.state = MPPE_STATELESS;
      else if (!strcasecmp(arg->argv[arg->argn + 1], "stateful"))
        l->ccp.cfg.mppe.state = MPPE_STATEFUL;
      else {
        log_Printf(LogWARN, "%s: Invalid state value\n",
                   arg->argv[arg->argn + 1]);
        res = -1;
        break;
      }
    } else
      l->ccp.cfg.mppe.state = MPPE_ANYSTATE;
    l->ccp.cfg.mppe.keybits = long_val;
    l->ccp.cfg.mppe.required = 1;
    break;
#endif

  case VAR_DEVICE:
    physical_SetDeviceList(cx->physical, arg->argc - arg->argn,
                           arg->argv + arg->argn);
    break;

  case VAR_ACCMAP:
    if (arg->argc > arg->argn) {
      u_long ulong_val;
      sscanf(argp, "%lx", &ulong_val);
      cx->physical->link.lcp.cfg.accmap = (u_int32_t)ulong_val;
    } else {
      log_Printf(LogWARN, "No accmap specified\n");
      res = 1;
    }
    break;

  case VAR_MODE:
    mode = Nam2mode(argp);
    if (mode == PHYS_NONE || mode == PHYS_ALL) {
      log_Printf(LogWARN, "%s: Invalid mode\n", argp);
      res = -1;
      break;
    }
    bundle_SetMode(arg->bundle, cx, mode);
    break;

  case VAR_MRRU:
    switch (bundle_Phase(arg->bundle)) {
      case PHASE_DEAD:
        break;
      case PHASE_ESTABLISH:
        /* Make sure none of our links are DATALINK_LCP or greater */
        if (bundle_HighestState(arg->bundle) >= DATALINK_LCP) {
          log_Printf(LogWARN, "mrru: Only changeable before LCP negotiations\n");
          res = 1;
          break;
        }
        break;
      default:
        log_Printf(LogWARN, "mrru: Only changeable at phase DEAD/ESTABLISH\n");
        res = 1;
        break;
    }
    if (res != 0)
      break;
    long_val = atol(argp);
    if (long_val && long_val < MIN_MRU) {
      log_Printf(LogWARN, "MRRU %ld: too small - min %d\n", long_val, MIN_MRU);
      res = 1;
      break;
    } else if (long_val > MAX_MRU) {
      log_Printf(LogWARN, "MRRU %ld: too big - max %d\n", long_val, MAX_MRU);
      res = 1;
      break;
    } else
      arg->bundle->ncp.mp.cfg.mrru = long_val;
    break;

  case VAR_MRU:
    long_val = 0;	/* silence gcc */
    change = NULL;	/* silence gcc */
    switch(arg->argc - arg->argn) {
    case 1:
      if (argp[strspn(argp, "0123456789")] != '\0') {
        res = -1;
        break;
      }
      /*FALLTHRU*/
    case 0:
      long_val = atol(argp);
      change = &l->lcp.cfg.mru;
      if (long_val > l->lcp.cfg.max_mru) {
        log_Printf(LogWARN, "MRU %ld: too large - max set to %d\n", long_val,
                   l->lcp.cfg.max_mru);
        res = 1;
        break;
      }
      break;
    case 2:
      if (strcasecmp(argp, "max") && strcasecmp(argp, "maximum")) {
        res = -1;
        break;
      }
      long_val = atol(arg->argv[arg->argn + 1]);
      change = &l->lcp.cfg.max_mru;
      if (long_val > MAX_MRU) {
        log_Printf(LogWARN, "MRU %ld: too large - maximum is %d\n", long_val,
                   MAX_MRU);
        res = 1;
        break;
      }
      break;
    default:
      res = -1;
      break;
    }
    if (res != 0)
      break;

    if (long_val == 0)
      *change = 0;
    else if (long_val < MIN_MRU) {
      log_Printf(LogWARN, "MRU %ld: too small - min %d\n", long_val, MIN_MRU);
      res = 1;
      break;
    } else if (long_val > MAX_MRU) {
      log_Printf(LogWARN, "MRU %ld: too big - max %d\n", long_val, MAX_MRU);
      res = 1;
      break;
    } else
      *change = long_val;
    if (l->lcp.cfg.mru > *change)
      l->lcp.cfg.mru = *change;
    break;

  case VAR_MTU:
    long_val = 0;	/* silence gcc */
    change = NULL;	/* silence gcc */
    switch(arg->argc - arg->argn) {
    case 1:
      if (argp[strspn(argp, "0123456789")] != '\0') {
        res = -1;
        break;
      }
      /*FALLTHRU*/
    case 0:
      long_val = atol(argp);
      change = &l->lcp.cfg.mtu;
      if (long_val > l->lcp.cfg.max_mtu) {
        log_Printf(LogWARN, "MTU %ld: too large - max set to %d\n", long_val,
                   l->lcp.cfg.max_mtu);
        res = 1;
        break;
      }
      break;
    case 2:
      if (strcasecmp(argp, "max") && strcasecmp(argp, "maximum")) {
        res = -1;
        break;
      }
      long_val = atol(arg->argv[arg->argn + 1]);
      change = &l->lcp.cfg.max_mtu;
      if (long_val > MAX_MTU) {
        log_Printf(LogWARN, "MTU %ld: too large - maximum is %d\n", long_val,
                   MAX_MTU);
        res = 1;
        break;
      }
      break;
    default:
      res = -1;
      break;
    }

    if (res != 0)
      break;

    if (long_val && long_val < MIN_MTU) {
      log_Printf(LogWARN, "MTU %ld: too small - min %d\n", long_val, MIN_MTU);
      res = 1;
      break;
    } else if (long_val > MAX_MTU) {
      log_Printf(LogWARN, "MTU %ld: too big - max %d\n", long_val, MAX_MTU);
      res = 1;
      break;
    } else
      *change = long_val;
    if (l->lcp.cfg.mtu > *change)
      l->lcp.cfg.mtu = *change;
    break;

  case VAR_OPENMODE:
    if (strcasecmp(argp, "active") == 0)
      cx->physical->link.lcp.cfg.openmode = arg->argc > arg->argn+1 ?
        atoi(arg->argv[arg->argn+1]) : 1;
    else if (strcasecmp(argp, "passive") == 0)
      cx->physical->link.lcp.cfg.openmode = OPEN_PASSIVE;
    else {
      log_Printf(LogWARN, "%s: Invalid openmode\n", argp);
      res = 1;
    }
    break;

  case VAR_PHONE:
    strncpy(cx->cfg.phone.list, argp, sizeof cx->cfg.phone.list - 1);
    cx->cfg.phone.list[sizeof cx->cfg.phone.list - 1] = '\0';
    cx->phone.alt = cx->phone.next = NULL;
    break;

  case VAR_HANGUP:
    strncpy(cx->cfg.script.hangup, argp, sizeof cx->cfg.script.hangup - 1);
    cx->cfg.script.hangup[sizeof cx->cfg.script.hangup - 1] = '\0';
    break;

  case VAR_IFQUEUE:
    long_val = atol(argp);
    arg->bundle->cfg.ifqueue = long_val < 0 ? 0 : long_val;
    break;

  case VAR_LOGOUT:
    strncpy(cx->cfg.script.logout, argp, sizeof cx->cfg.script.logout - 1);
    cx->cfg.script.logout[sizeof cx->cfg.script.logout - 1] = '\0';
    break;

  case VAR_IDLETIMEOUT:
    if (arg->argc > arg->argn+2) {
      log_Printf(LogWARN, "Too many idle timeout values\n");
      res = 1;
    } else if (arg->argc == arg->argn) {
      log_Printf(LogWARN, "Too few idle timeout values\n");
      res = 1;
    } else {
      unsigned long timeout, min;

      timeout = strtoul(argp, NULL, 10);
      min = arg->bundle->cfg.idle.min_timeout;
      if (arg->argc == arg->argn + 2)
	min = strtoul(arg->argv[arg->argn + 1], NULL, 10);
      bundle_SetIdleTimer(arg->bundle, timeout, min);
    }
    break;
    
#ifndef NORADIUS
  case VAR_RAD_ALIVE:
    if (arg->argc > arg->argn + 2) {
      log_Printf(LogWARN, "Too many RADIUS alive interval values\n");
      res = 1;
    } else if (arg->argc == arg->argn) {
      log_Printf(LogWARN, "Too few RADIUS alive interval values\n");
      res = 1;
    } else {
      arg->bundle->radius.alive.interval = atoi(argp);
      if (arg->bundle->radius.alive.interval && !*arg->bundle->radius.cfg.file) {
        log_Printf(LogWARN, "rad_alive requires radius to be configured\n");
	res = 1;
      } else if (arg->bundle->ncp.ipcp.fsm.state == ST_OPENED) {
	if (arg->bundle->radius.alive.interval)
	  radius_StartTimer(arg->bundle);
	else
	  radius_StopTimer(&arg->bundle->radius);
      }
    }
    break;
#endif
   
  case VAR_LQRPERIOD:
    long_val = atol(argp);
    if (long_val < MIN_LQRPERIOD) {
      log_Printf(LogWARN, "%ld: Invalid lqr period - min %d\n",
                 long_val, MIN_LQRPERIOD);
      res = 1;
    } else
      l->lcp.cfg.lqrperiod = long_val;
    break;

  case VAR_LCPRETRY:
    res = SetRetry(arg->argc - arg->argn, arg->argv + arg->argn,
                   &cx->physical->link.lcp.cfg.fsm.timeout,
                   &cx->physical->link.lcp.cfg.fsm.maxreq,
                   &cx->physical->link.lcp.cfg.fsm.maxtrm, DEF_FSMTRIES);
    break;

  case VAR_CHAPRETRY:
    res = SetRetry(arg->argc - arg->argn, arg->argv + arg->argn,
                   &cx->chap.auth.cfg.fsm.timeout,
                   &cx->chap.auth.cfg.fsm.maxreq, NULL, DEF_FSMAUTHTRIES);
    break;

  case VAR_PAPRETRY:
    res = SetRetry(arg->argc - arg->argn, arg->argv + arg->argn,
                   &cx->pap.cfg.fsm.timeout, &cx->pap.cfg.fsm.maxreq,
                   NULL, DEF_FSMAUTHTRIES);
    break;

  case VAR_CCPRETRY:
    res = SetRetry(arg->argc - arg->argn, arg->argv + arg->argn,
                   &l->ccp.cfg.fsm.timeout, &l->ccp.cfg.fsm.maxreq,
                   &l->ccp.cfg.fsm.maxtrm, DEF_FSMTRIES);
    break;

  case VAR_IPCPRETRY:
    res = SetRetry(arg->argc - arg->argn, arg->argv + arg->argn,
                   &arg->bundle->ncp.ipcp.cfg.fsm.timeout,
                   &arg->bundle->ncp.ipcp.cfg.fsm.maxreq,
                   &arg->bundle->ncp.ipcp.cfg.fsm.maxtrm, DEF_FSMTRIES);
    break;

#ifndef NOINET6
  case VAR_IPV6CPRETRY:
    res = SetRetry(arg->argc - arg->argn, arg->argv + arg->argn,
                   &arg->bundle->ncp.ipv6cp.cfg.fsm.timeout,
                   &arg->bundle->ncp.ipv6cp.cfg.fsm.maxreq,
                   &arg->bundle->ncp.ipv6cp.cfg.fsm.maxtrm, DEF_FSMTRIES);
    break;
#endif

  case VAR_NBNS:
  case VAR_DNS:
    if (param == VAR_DNS) {
      ipaddr = arg->bundle->ncp.ipcp.cfg.ns.dns;
      ipaddr[0].s_addr = ipaddr[1].s_addr = INADDR_NONE;
    } else {
      ipaddr = arg->bundle->ncp.ipcp.cfg.ns.nbns;
      ipaddr[0].s_addr = ipaddr[1].s_addr = INADDR_ANY;
    }

    if (arg->argc > arg->argn) {
      ncpaddr_aton(ncpaddr, &arg->bundle->ncp, arg->argv[arg->argn]);
      if (!ncpaddr_getip4(ncpaddr, ipaddr))
        return -1;
      if (arg->argc > arg->argn+1) {
        ncpaddr_aton(ncpaddr + 1, &arg->bundle->ncp, arg->argv[arg->argn + 1]);
        if (!ncpaddr_getip4(ncpaddr + 1, ipaddr + 1))
          return -1;
      }

      if (ipaddr[0].s_addr == INADDR_ANY) {
        ipaddr[0] = ipaddr[1];
        ipaddr[1].s_addr = INADDR_ANY;
      }
      if (ipaddr[0].s_addr == INADDR_NONE) {
        ipaddr[0] = ipaddr[1];
        ipaddr[1].s_addr = INADDR_NONE;
      }
    }
    break;

  case VAR_CALLBACK:
    cx->cfg.callback.opmask = 0;
    for (dummyint = arg->argn; dummyint < arg->argc; dummyint++) {
      if (!strcasecmp(arg->argv[dummyint], "auth"))
        cx->cfg.callback.opmask |= CALLBACK_BIT(CALLBACK_AUTH);
      else if (!strcasecmp(arg->argv[dummyint], "cbcp"))
        cx->cfg.callback.opmask |= CALLBACK_BIT(CALLBACK_CBCP);
      else if (!strcasecmp(arg->argv[dummyint], "e.164")) {
        if (dummyint == arg->argc - 1)
          log_Printf(LogWARN, "No E.164 arg (E.164 ignored) !\n");
        else {
          cx->cfg.callback.opmask |= CALLBACK_BIT(CALLBACK_E164);
          strncpy(cx->cfg.callback.msg, arg->argv[++dummyint],
                  sizeof cx->cfg.callback.msg - 1);
          cx->cfg.callback.msg[sizeof cx->cfg.callback.msg - 1] = '\0';
        }
      } else if (!strcasecmp(arg->argv[dummyint], "none"))
        cx->cfg.callback.opmask |= CALLBACK_BIT(CALLBACK_NONE);
      else {
        res = -1;
        break;
      }
    }
    if (cx->cfg.callback.opmask == CALLBACK_BIT(CALLBACK_NONE))
      cx->cfg.callback.opmask = 0;
    break;

  case VAR_CBCP:
    cx->cfg.cbcp.delay = 0;
    *cx->cfg.cbcp.phone = '\0';
    cx->cfg.cbcp.fsmretry = DEF_FSMRETRY;
    if (arg->argc > arg->argn) {
      strncpy(cx->cfg.cbcp.phone, arg->argv[arg->argn],
              sizeof cx->cfg.cbcp.phone - 1);
      cx->cfg.cbcp.phone[sizeof cx->cfg.cbcp.phone - 1] = '\0';
      if (arg->argc > arg->argn + 1) {
        cx->cfg.cbcp.delay = atoi(arg->argv[arg->argn + 1]);
        if (arg->argc > arg->argn + 2) {
          long_val = atol(arg->argv[arg->argn + 2]);
          if (long_val < MIN_FSMRETRY)
            log_Printf(LogWARN, "%ld: Invalid CBCP FSM retry period - min %d\n",
                       long_val, MIN_FSMRETRY);
          else
            cx->cfg.cbcp.fsmretry = long_val;
        }
      }
    }
    break;

  case VAR_CHOKED:
    arg->bundle->cfg.choked.timeout = atoi(argp);
    if (arg->bundle->cfg.choked.timeout <= 0)
      arg->bundle->cfg.choked.timeout = CHOKED_TIMEOUT;
    break;

  case VAR_SENDPIPE:
    long_val = atol(argp);
    arg->bundle->ncp.cfg.sendpipe = long_val;
    break;

  case VAR_RECVPIPE:
    long_val = atol(argp);
    arg->bundle->ncp.cfg.recvpipe = long_val;
    break;

#ifndef NORADIUS
  case VAR_RADIUS:
    if (!*argp)
      *arg->bundle->radius.cfg.file = '\0';
    else if (access(argp, R_OK)) {
      log_Printf(LogWARN, "%s: %s\n", argp, strerror(errno));
      res = 1;
      break;
    } else {
      strncpy(arg->bundle->radius.cfg.file, argp,
              sizeof arg->bundle->radius.cfg.file - 1);
      arg->bundle->radius.cfg.file
        [sizeof arg->bundle->radius.cfg.file - 1] = '\0';
    }
    break;
#endif

  case VAR_CD:
    if (*argp) {
      if (strcasecmp(argp, "off")) {
        long_val = atol(argp);
        if (long_val < 0)
          long_val = 0;
        cx->physical->cfg.cd.delay = long_val;
        cx->physical->cfg.cd.necessity = argp[strlen(argp)-1] == '!' ?
          CD_REQUIRED : CD_VARIABLE;
      } else
        cx->physical->cfg.cd.necessity = CD_NOTREQUIRED;
    } else {
      cx->physical->cfg.cd.delay = 0;
      cx->physical->cfg.cd.necessity = CD_DEFAULT;
    }
    break;

  case VAR_PARITY:
    if (arg->argc == arg->argn + 1)
      res = physical_SetParity(arg->cx->physical, argp);
    else {
      log_Printf(LogWARN, "Parity value must be odd, even or none\n");
      res = 1;
    }
    break;

  case VAR_CRTSCTS:
    if (strcasecmp(argp, "on") == 0)
      physical_SetRtsCts(arg->cx->physical, 1);
    else if (strcasecmp(argp, "off") == 0)
      physical_SetRtsCts(arg->cx->physical, 0);
    else {
      log_Printf(LogWARN, "RTS/CTS value must be on or off\n");
      res = 1;
    }
    break;

  case VAR_URGENT:
    if (arg->argn == arg->argc) {
      ncp_SetUrgentTOS(&arg->bundle->ncp);
      ncp_ClearUrgentTcpPorts(&arg->bundle->ncp);
      ncp_ClearUrgentUdpPorts(&arg->bundle->ncp);
    } else if (!strcasecmp(arg->argv[arg->argn], "udp")) {
      ncp_SetUrgentTOS(&arg->bundle->ncp);
      if (arg->argn == arg->argc - 1)
        ncp_ClearUrgentUdpPorts(&arg->bundle->ncp);
      else for (f = arg->argn + 1; f < arg->argc; f++)
        if (*arg->argv[f] == '+')
          ncp_AddUrgentUdpPort(&arg->bundle->ncp, atoi(arg->argv[f] + 1));
        else if (*arg->argv[f] == '-')
          ncp_RemoveUrgentUdpPort(&arg->bundle->ncp, atoi(arg->argv[f] + 1));
        else {
          if (f == arg->argn)
            ncp_ClearUrgentUdpPorts(&arg->bundle->ncp);
          ncp_AddUrgentUdpPort(&arg->bundle->ncp, atoi(arg->argv[f]));
        }
    } else if (arg->argn == arg->argc - 1 &&
               !strcasecmp(arg->argv[arg->argn], "none")) {
      ncp_ClearUrgentTcpPorts(&arg->bundle->ncp);
      ncp_ClearUrgentUdpPorts(&arg->bundle->ncp);
      ncp_ClearUrgentTOS(&arg->bundle->ncp);
    } else if (!strcasecmp(arg->argv[arg->argn], "length")) {
      if (arg->argn == arg->argc - 1)
	ncp_SetUrgentTcpLen(&arg->bundle->ncp, 0);
      else
	ncp_SetUrgentTcpLen(&arg->bundle->ncp, atoi(arg->argv[arg->argn + 1]));
    } else {
      ncp_SetUrgentTOS(&arg->bundle->ncp);
      first = arg->argn;
      if (!strcasecmp(arg->argv[first], "tcp") && ++first == arg->argc)
        ncp_ClearUrgentTcpPorts(&arg->bundle->ncp);

      for (f = first; f < arg->argc; f++)
        if (*arg->argv[f] == '+')
          ncp_AddUrgentTcpPort(&arg->bundle->ncp, atoi(arg->argv[f] + 1));
        else if (*arg->argv[f] == '-')
          ncp_RemoveUrgentTcpPort(&arg->bundle->ncp, atoi(arg->argv[f] + 1));
        else {
          if (f == first)
            ncp_ClearUrgentTcpPorts(&arg->bundle->ncp);
          ncp_AddUrgentTcpPort(&arg->bundle->ncp, atoi(arg->argv[f]));
        }
    }
    break;

  case VAR_PPPOE:
    if (strcasecmp(argp, "3Com") == 0)
      physical_SetPPPoEnonstandard(arg->cx->physical, 1);
    else if (strcasecmp(argp, "standard") == 0)
      physical_SetPPPoEnonstandard(arg->cx->physical, 0);
    else {
      log_Printf(LogWARN, "PPPoE standard value must be \"standard\" or \"3Com\"\n");
      res = 1;
    }
    break;

#ifndef NORADIUS
  case VAR_PORT_ID:
    if (strcasecmp(argp, "default") == 0)
	    arg->bundle->radius.port_id_type = RPI_DEFAULT;
    else if (strcasecmp(argp, "pid") == 0)
	    arg->bundle->radius.port_id_type = RPI_PID;
    else if (strcasecmp(argp, "ifnum") == 0)
	    arg->bundle->radius.port_id_type = RPI_IFNUM; 
    else if (strcasecmp(argp, "tunnum") == 0)
	    arg->bundle->radius.port_id_type = RPI_TUNNUM;
    else {
	   log_Printf(LogWARN,
		"RADIUS port id must be one of \"default\", \"pid\", \"ifnum\" or \"tunnum\"\n");
	   res = 1;
    }

    if (arg->bundle->radius.port_id_type && !*arg->bundle->radius.cfg.file) {
	    log_Printf(LogWARN, "rad_port_id requires radius to be configured\n");
	    res = 1;
    }

    break;
#endif
  }

  return res;
}

static struct cmdtab const SetCommands[] = {
  {"accmap", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX,
  "accmap value", "set accmap hex-value", (const void *)VAR_ACCMAP},
  {"authkey", "key", SetVariable, LOCAL_AUTH,
  "authentication key", "set authkey|key key", (const void *)VAR_AUTHKEY},
  {"authname", NULL, SetVariable, LOCAL_AUTH,
  "authentication name", "set authname name", (const void *)VAR_AUTHNAME},
  {"autoload", NULL, SetVariable, LOCAL_AUTH,
  "auto link [de]activation", "set autoload maxtime maxload mintime minload",
  (const void *)VAR_AUTOLOAD},
  {"bandwidth", NULL, mp_SetDatalinkBandwidth, LOCAL_AUTH | LOCAL_CX,
  "datalink bandwidth", "set bandwidth value", NULL},
  {"callback", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX,
  "callback control", "set callback [none|auth|cbcp|"
  "E.164 *|number[,number]...]...", (const void *)VAR_CALLBACK},
  {"cbcp", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX,
  "CBCP control", "set cbcp [*|phone[,phone...] [delay [timeout]]]",
  (const void *)VAR_CBCP},
  {"ccpretry", "ccpretries", SetVariable, LOCAL_AUTH | LOCAL_CX_OPT,
   "CCP retries", "set ccpretry value [attempts]", (const void *)VAR_CCPRETRY},
  {"cd", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX, "Carrier delay requirement",
   "set cd value[!]", (const void *)VAR_CD},
  {"chapretry", "chapretries", SetVariable, LOCAL_AUTH | LOCAL_CX,
   "CHAP retries", "set chapretry value [attempts]",
   (const void *)VAR_CHAPRETRY},
  {"choked", NULL, SetVariable, LOCAL_AUTH,
  "choked timeout", "set choked [secs]", (const void *)VAR_CHOKED},
  {"ctsrts", "crtscts", SetVariable, LOCAL_AUTH | LOCAL_CX,
   "Use hardware flow control", "set ctsrts [on|off]",
   (const char *)VAR_CRTSCTS},
  {"deflate", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX_OPT,
  "deflate window sizes", "set deflate out-winsize in-winsize",
  (const void *) VAR_WINSIZE},
#ifndef NODES
  {"mppe", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX_OPT,
  "MPPE key size and state", "set mppe [40|56|128|* [stateful|stateless|*]]",
  (const void *) VAR_MPPE},
#endif
  {"device", "line", SetVariable, LOCAL_AUTH | LOCAL_CX,
  "physical device name", "set device|line device-name[,device-name]",
  (const void *) VAR_DEVICE},
  {"dial", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX,
  "dialing script", "set dial chat-script", (const void *) VAR_DIAL},
  {"dns", NULL, SetVariable, LOCAL_AUTH, "Domain Name Server",
  "set dns pri-addr [sec-addr]", (const void *)VAR_DNS},
  {"enddisc", NULL, mp_SetEnddisc, LOCAL_AUTH,
  "Endpoint Discriminator", "set enddisc [IP|magic|label|psn value]", NULL},
  {"escape", NULL, SetEscape, LOCAL_AUTH | LOCAL_CX,
  "escape characters", "set escape hex-digit ...", NULL},
  {"filter", NULL, filter_Set, LOCAL_AUTH,
  "packet filters", "set filter alive|dial|in|out rule-no permit|deny "
  "[src_addr[/width]] [dst_addr[/width]] [proto "
  "[src [lt|eq|gt port]] [dst [lt|eq|gt port]] [estab] [syn] [finrst]]", NULL},
  {"hangup", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX,
  "hangup script", "set hangup chat-script", (const void *) VAR_HANGUP},
  {"ifaddr", NULL, SetInterfaceAddr, LOCAL_AUTH, "destination address",
  "set ifaddr [src-addr [dst-addr [netmask [trg-addr]]]]", NULL},
  {"ifqueue", NULL, SetVariable, LOCAL_AUTH, "interface queue",
  "set ifqueue packets", (const void *)VAR_IFQUEUE},
  {"ipcpretry", "ipcpretries", SetVariable, LOCAL_AUTH, "IPCP retries",
   "set ipcpretry value [attempts]", (const void *)VAR_IPCPRETRY},
  {"ipv6cpretry", "ipv6cpretries", SetVariable, LOCAL_AUTH, "IPV6CP retries",
   "set ipv6cpretry value [attempts]", (const void *)VAR_IPV6CPRETRY},
  {"lcpretry", "lcpretries", SetVariable, LOCAL_AUTH | LOCAL_CX, "LCP retries",
   "set lcpretry value [attempts]", (const void *)VAR_LCPRETRY},
  {"log", NULL, log_SetLevel, LOCAL_AUTH, "log level",
  "set log [local] [+|-]all|async|cbcp|ccp|chat|command|connect|debug|dns|hdlc|"
  "id0|ipcp|lcp|lqm|phase|physical|radius|sync|tcp/ip|timer|tun...", NULL},
  {"login", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX,
  "login script", "set login chat-script", (const void *) VAR_LOGIN},
  {"logout", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX,
  "logout script", "set logout chat-script", (const void *) VAR_LOGOUT},
  {"lqrperiod", "echoperiod", SetVariable, LOCAL_AUTH | LOCAL_CX_OPT,
  "LQR period", "set lqr/echo period value", (const void *)VAR_LQRPERIOD},
  {"mode", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX, "mode value",
  "set mode interactive|auto|ddial|background", (const void *)VAR_MODE},
  {"mrru", NULL, SetVariable, LOCAL_AUTH, "MRRU value",
  "set mrru value", (const void *)VAR_MRRU},
  {"mru", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX,
  "MRU value", "set mru [max[imum]] [value]", (const void *)VAR_MRU},
  {"mtu", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX,
  "interface MTU value", "set mtu [max[imum]] [value]", (const void *)VAR_MTU},
  {"nbns", NULL, SetVariable, LOCAL_AUTH, "NetBIOS Name Server",
  "set nbns pri-addr [sec-addr]", (const void *)VAR_NBNS},
  {"openmode", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX, "open mode",
  "set openmode active|passive [secs]", (const void *)VAR_OPENMODE},
  {"papretry", "papretries", SetVariable, LOCAL_AUTH | LOCAL_CX, "PAP retries",
   "set papretry value [attempts]", (const void *)VAR_PAPRETRY},
  {"parity", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX, "serial parity",
   "set parity [odd|even|none]", (const void *)VAR_PARITY},
  {"phone", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX, "telephone number(s)",
  "set phone phone1[:phone2[...]]", (const void *)VAR_PHONE},
  {"proctitle", "title", SetProcTitle, LOCAL_AUTH,
  "Process title", "set proctitle [value]", NULL},
#ifndef NORADIUS
  {"radius", NULL, SetVariable, LOCAL_AUTH,
  "RADIUS Config", "set radius cfgfile", (const void *)VAR_RADIUS},
  {"rad_alive", NULL, SetVariable, LOCAL_AUTH,
  "Raduis alive interval", "set rad_alive value",
  (const void *)VAR_RAD_ALIVE},
  {"rad_port_id", NULL, SetVariable, LOCAL_AUTH,
  "NAS-Port-Id", "set rad_port_id [default|pid|ifnum|tunnum]", (const void *)VAR_PORT_ID},
#endif
  {"reconnect", NULL, datalink_SetReconnect, LOCAL_AUTH | LOCAL_CX,
  "Reconnect timeout", "set reconnect value ntries", NULL},
  {"recvpipe", NULL, SetVariable, LOCAL_AUTH,
  "RECVPIPE value", "set recvpipe value", (const void *)VAR_RECVPIPE},
  {"redial", NULL, datalink_SetRedial, LOCAL_AUTH | LOCAL_CX,
  "Redial timeout", "set redial secs[+inc[-incmax]][.next] [attempts]", NULL},
  {"sendpipe", NULL, SetVariable, LOCAL_AUTH,
  "SENDPIPE value", "set sendpipe value", (const void *)VAR_SENDPIPE},
  {"server", "socket", SetServer, LOCAL_AUTH, "diagnostic port",
  "set server|socket TcpPort|LocalName|none|open|closed [password [mask]]",
  NULL},
  {"speed", NULL, SetModemSpeed, LOCAL_AUTH | LOCAL_CX,
  "physical speed", "set speed value|sync", NULL},
  {"stopped", NULL, SetStoppedTimeout, LOCAL_AUTH | LOCAL_CX,
  "STOPPED timeouts", "set stopped [LCPseconds [CCPseconds]]", NULL},
  {"timeout", NULL, SetVariable, LOCAL_AUTH, "Idle timeout",
  "set timeout idletime", (const void *)VAR_IDLETIMEOUT},
  {"urgent", NULL, SetVariable, LOCAL_AUTH, "urgent traffic",
  "set urgent [[tcp|udp] [+|-]port...]|[length len]", (const void *)VAR_URGENT},
  {"vj", NULL, ipcp_vjset, LOCAL_AUTH,
  "vj values", "set vj slots|slotcomp [value]", NULL},
  {"help", "?", HelpCommand, LOCAL_AUTH | LOCAL_NO_AUTH,
  "Display this message", "set help|? [command]", SetCommands},
  {"pppoe", NULL, SetVariable, LOCAL_AUTH | LOCAL_CX,
   "Connect using standard/3Com mode", "set pppoe [standard|3Com]",
   (const char *)VAR_PPPOE},
  {NULL, NULL, NULL, 0, NULL, NULL, NULL},
};

static int
SetCommand(struct cmdargs const *arg)
{
  if (arg->argc > arg->argn)
    FindExec(arg->bundle, SetCommands, arg->argc, arg->argn, arg->argv,
             arg->prompt, arg->cx);
  else if (arg->prompt)
    prompt_Printf(arg->prompt, "Use `set ?' to get a list or `set ? <var>' for"
	          " syntax help.\n");
  else
    log_Printf(LogWARN, "set command must have arguments\n");

  return 0;
}

static int
AddCommand(struct cmdargs const *arg)
{
  struct ncpaddr gw;
  struct ncprange dest;
  struct in_addr host;
#ifndef NOINET6
  struct in6_addr host6;
#endif
  int dest_default, gw_arg, addrs;

  if (arg->argc != arg->argn+3 && arg->argc != arg->argn+2)
    return -1;

  addrs = 0;
  dest_default = 0;
  if (arg->argc == arg->argn + 2) {
    if (!strcasecmp(arg->argv[arg->argn], "default"))
      dest_default = 1;
    else {
      if (!ncprange_aton(&dest, &arg->bundle->ncp, arg->argv[arg->argn]))
        return -1;
      if (!strncasecmp(arg->argv[arg->argn], "MYADDR", 6))
        addrs = ROUTE_DSTMYADDR;
      else if (!strncasecmp(arg->argv[arg->argn], "MYADDR6", 7))
        addrs = ROUTE_DSTMYADDR6;
      else if (!strncasecmp(arg->argv[arg->argn], "HISADDR", 7))
        addrs = ROUTE_DSTHISADDR;
      else if (!strncasecmp(arg->argv[arg->argn], "HISADDR6", 8))
        addrs = ROUTE_DSTHISADDR6;
      else if (!strncasecmp(arg->argv[arg->argn], "DNS0", 4))
        addrs = ROUTE_DSTDNS0;
      else if (!strncasecmp(arg->argv[arg->argn], "DNS1", 4))
        addrs = ROUTE_DSTDNS1;
    }
    gw_arg = 1;
  } else {
    if (strcasecmp(arg->argv[arg->argn], "MYADDR") == 0) {
      addrs = ROUTE_DSTMYADDR;
      host = arg->bundle->ncp.ipcp.my_ip;
    } else if (strcasecmp(arg->argv[arg->argn], "HISADDR") == 0) {
      addrs = ROUTE_DSTHISADDR;
      host = arg->bundle->ncp.ipcp.peer_ip;
    } else if (strcasecmp(arg->argv[arg->argn], "DNS0") == 0) {
      addrs = ROUTE_DSTDNS0;
      host = arg->bundle->ncp.ipcp.ns.dns[0];
    } else if (strcasecmp(arg->argv[arg->argn], "DNS1") == 0) {
      addrs = ROUTE_DSTDNS1;
      host = arg->bundle->ncp.ipcp.ns.dns[1];
    } else {
      host = GetIpAddr(arg->argv[arg->argn]);
      if (host.s_addr == INADDR_NONE) {
        log_Printf(LogWARN, "%s: Invalid destination address\n",
                   arg->argv[arg->argn]);
        return -1;
      }
    }
    ncprange_setip4(&dest, host, GetIpAddr(arg->argv[arg->argn + 1]));
    gw_arg = 2;
  }

  if (strcasecmp(arg->argv[arg->argn + gw_arg], "HISADDR") == 0) {
    ncpaddr_setip4(&gw, arg->bundle->ncp.ipcp.peer_ip);
    addrs |= ROUTE_GWHISADDR;
#ifndef NOINET6
  } else if (strcasecmp(arg->argv[arg->argn + gw_arg], "HISADDR6") == 0) {
    if (!ncpaddr_getip6(&arg->bundle->ncp.ipv6cp.hisaddr, &host6))
      memset(&host6, '\0', sizeof host6);
    ncpaddr_setip6(&gw, &host6);
    addrs |= ROUTE_GWHISADDR6;
#endif
  } else {
    if (!ncpaddr_aton(&gw, &arg->bundle->ncp, arg->argv[arg->argn + gw_arg])) {
      log_Printf(LogWARN, "%s: Invalid gateway address\n",
                 arg->argv[arg->argn + gw_arg]);
      return -1;
    }
  }

  if (dest_default)
    ncprange_setdefault(&dest, ncpaddr_family(&gw));

  if (rt_Set(arg->bundle, RTM_ADD, &dest, &gw, arg->cmd->args ? 1 : 0,
             ((addrs & ROUTE_GWHISADDR) || (addrs & ROUTE_GWHISADDR6)) ? 1 : 0)
      && addrs != ROUTE_STATIC)
    route_Add(&arg->bundle->ncp.route, addrs, &dest, &gw);

  return 0;
}

static int
DeleteCommand(struct cmdargs const *arg)
{
  struct ncprange dest;
  int addrs;

  if (arg->argc == arg->argn+1) {
    if(strcasecmp(arg->argv[arg->argn], "all") == 0) {
      route_IfDelete(arg->bundle, 0);
      route_DeleteAll(&arg->bundle->ncp.route);
    } else {
      addrs = 0;
      if (strcasecmp(arg->argv[arg->argn], "MYADDR") == 0) {
        ncprange_setip4host(&dest, arg->bundle->ncp.ipcp.my_ip);
        addrs = ROUTE_DSTMYADDR;
#ifndef NOINET6
      } else if (strcasecmp(arg->argv[arg->argn], "MYADDR6") == 0) {
        ncprange_sethost(&dest, &arg->bundle->ncp.ipv6cp.myaddr);
        addrs = ROUTE_DSTMYADDR6;
#endif
      } else if (strcasecmp(arg->argv[arg->argn], "HISADDR") == 0) {
        ncprange_setip4host(&dest, arg->bundle->ncp.ipcp.peer_ip);
        addrs = ROUTE_DSTHISADDR;
#ifndef NOINET6
      } else if (strcasecmp(arg->argv[arg->argn], "HISADDR6") == 0) {
        ncprange_sethost(&dest, &arg->bundle->ncp.ipv6cp.hisaddr);
        addrs = ROUTE_DSTHISADDR6;
#endif
      } else if (strcasecmp(arg->argv[arg->argn], "DNS0") == 0) {
        ncprange_setip4host(&dest, arg->bundle->ncp.ipcp.ns.dns[0]);
        addrs = ROUTE_DSTDNS0;
      } else if (strcasecmp(arg->argv[arg->argn], "DNS1") == 0) {
        ncprange_setip4host(&dest, arg->bundle->ncp.ipcp.ns.dns[1]);
        addrs = ROUTE_DSTDNS1;
      } else {
        ncprange_aton(&dest, &arg->bundle->ncp, arg->argv[arg->argn]);
        addrs = ROUTE_STATIC;
      }
      rt_Set(arg->bundle, RTM_DELETE, &dest, NULL, arg->cmd->args ? 1 : 0, 0);
      route_Delete(&arg->bundle->ncp.route, addrs, &dest);
    }
  } else
    return -1;

  return 0;
}

#ifndef NONAT
static int
NatEnable(struct cmdargs const *arg)
{
  if (arg->argc == arg->argn+1) {
    if (strcasecmp(arg->argv[arg->argn], "yes") == 0) {
      if (!arg->bundle->NatEnabled) {
        if (arg->bundle->ncp.ipcp.fsm.state == ST_OPENED)
          LibAliasSetAddress(la, arg->bundle->ncp.ipcp.my_ip);
        arg->bundle->NatEnabled = 1;
      }
      return 0;
    } else if (strcasecmp(arg->argv[arg->argn], "no") == 0) {
      arg->bundle->NatEnabled = 0;
      opt_disable(arg->bundle, OPT_IFACEALIAS);
      /* Don't iface_Clear() - there may be manually configured addresses */
      return 0;
    }
  }

  return -1;
}


static int
NatOption(struct cmdargs const *arg)
{
  long param = (long)arg->cmd->args;

  if (arg->argc == arg->argn+1) {
    if (strcasecmp(arg->argv[arg->argn], "yes") == 0) {
      if (arg->bundle->NatEnabled) {
	LibAliasSetMode(la, param, param);
	return 0;
      }
      log_Printf(LogWARN, "nat not enabled\n");
    } else if (strcmp(arg->argv[arg->argn], "no") == 0) {
      if (arg->bundle->NatEnabled) {
	LibAliasSetMode(la, 0, param);
	return 0;
      }
      log_Printf(LogWARN, "nat not enabled\n");
    }
  }
  return -1;
}
#endif /* #ifndef NONAT */

static int
LinkCommand(struct cmdargs const *arg)
{
  if (arg->argc > arg->argn+1) {
    char namelist[LINE_LEN];
    struct datalink *cx;
    char *name;
    int result = 0;

    if (!strcmp(arg->argv[arg->argn], "*")) {
      struct datalink *dl;

      cx = arg->bundle->links;
      while (cx) {
        /* Watch it, the command could be a ``remove'' */
        dl = cx->next;
        FindExec(arg->bundle, Commands, arg->argc, arg->argn+1, arg->argv,
                 arg->prompt, cx);
        for (cx = arg->bundle->links; cx; cx = cx->next)
          if (cx == dl)
            break;		/* Pointer's still valid ! */
      }
    } else {
      strncpy(namelist, arg->argv[arg->argn], sizeof namelist - 1);
      namelist[sizeof namelist - 1] = '\0';
      for(name = strtok(namelist, ", "); name; name = strtok(NULL,", "))
        if (!bundle2datalink(arg->bundle, name)) {
          log_Printf(LogWARN, "link: %s: Invalid link name\n", name);
          return 1;
        }

      strncpy(namelist, arg->argv[arg->argn], sizeof namelist - 1);
      namelist[sizeof namelist - 1] = '\0';
      for(name = strtok(namelist, ", "); name; name = strtok(NULL,", ")) {
        cx = bundle2datalink(arg->bundle, name);
        if (cx)
          FindExec(arg->bundle, Commands, arg->argc, arg->argn+1, arg->argv,
                   arg->prompt, cx);
        else {
          log_Printf(LogWARN, "link: %s: Invalidated link name !\n", name);
          result++;
        }
      }
    }
    return result;
  }

  log_Printf(LogWARN, "usage: %s\n", arg->cmd->syntax);
  return 2;
}

struct link *
command_ChooseLink(struct cmdargs const *arg)
{
  if (arg->cx)
    return &arg->cx->physical->link;
  else if (!arg->bundle->ncp.mp.cfg.mrru) {
    struct datalink *dl = bundle2datalink(arg->bundle, NULL);
    if (dl)
      return &dl->physical->link;
  }
  return &arg->bundle->ncp.mp.link;
}

static const char *
ident_cmd(const char *cmd, unsigned *keep, unsigned *add)
{
  const char *result;

  switch (*cmd) {
    case 'A':
    case 'a':
      result = "accept";
      *keep = NEG_MYMASK;
      *add = NEG_ACCEPTED;
      break;
    case 'D':
    case 'd':
      switch (cmd[1]) {
        case 'E':
        case 'e':
          result = "deny";
          *keep = NEG_MYMASK;
          *add = 0;
          break;
        case 'I':
        case 'i':
          result = "disable";
          *keep = NEG_HISMASK;
          *add = 0;
          break;
        default:
          return NULL;
      }
      break;
    case 'E':
    case 'e':
      result = "enable";
      *keep = NEG_HISMASK;
      *add = NEG_ENABLED;
      break;
    default:
      return NULL;
  }

  return result;
}

static int
OptSet(struct cmdargs const *arg)
{
  int opt = (int)(long)arg->cmd->args;
  unsigned keep;			/* Keep this opt */
  unsigned add;				/* Add this opt */

  if (ident_cmd(arg->argv[arg->argn - 2], &keep, &add) == NULL)
    return 1;

#ifndef NOINET6
  if (add == NEG_ENABLED && opt == OPT_IPV6CP && !probe.ipv6_available) {
    log_Printf(LogWARN, "IPv6 is not available on this machine\n");
    return 1;
  }
#endif
  if (!add && ((opt == OPT_NAS_IP_ADDRESS &&
                !Enabled(arg->bundle, OPT_NAS_IDENTIFIER)) ||
               (opt == OPT_NAS_IDENTIFIER &&
                !Enabled(arg->bundle, OPT_NAS_IP_ADDRESS)))) {
    log_Printf(LogWARN,
               "Cannot disable both NAS-IP-Address and NAS-Identifier\n");
    return 1;
  }

  if (add)
    opt_enable(arg->bundle, opt);
  else
    opt_disable(arg->bundle, opt);

  return 0;
}

static int
IfaceAliasOptSet(struct cmdargs const *arg)
{
  unsigned long long save = arg->bundle->cfg.optmask;
  int result = OptSet(arg);

  if (result == 0)
    if (Enabled(arg->bundle, OPT_IFACEALIAS) && !arg->bundle->NatEnabled) {
      arg->bundle->cfg.optmask = save;
      log_Printf(LogWARN, "Cannot enable iface-alias without NAT\n");
      result = 2;
    }

  return result;
}

static int
NegotiateSet(struct cmdargs const *arg)
{
  long param = (long)arg->cmd->args;
  struct link *l = command_ChooseLink(arg);	/* LOCAL_CX_OPT uses this */
  struct datalink *cx = arg->cx;	/* LOCAL_CX uses this */
  const char *cmd;
  unsigned keep;			/* Keep these bits */
  unsigned add;				/* Add these bits */

  if ((cmd = ident_cmd(arg->argv[arg->argn-2], &keep, &add)) == NULL)
    return 1;

  if ((arg->cmd->lauth & LOCAL_CX) && !cx) {
    log_Printf(LogWARN, "%s %s: No context (use the `link' command)\n",
              cmd, arg->cmd->name);
    return 2;
  } else if (cx && !(arg->cmd->lauth & (LOCAL_CX|LOCAL_CX_OPT))) {
    log_Printf(LogWARN, "%s %s: Redundant context (%s) ignored\n",
              cmd, arg->cmd->name, cx->name);
    cx = NULL;
  }

  switch (param) {
    case NEG_ACFCOMP:
      cx->physical->link.lcp.cfg.acfcomp &= keep;
      cx->physical->link.lcp.cfg.acfcomp |= add;
      break;
    case NEG_CHAP05:
      cx->physical->link.lcp.cfg.chap05 &= keep;
      cx->physical->link.lcp.cfg.chap05 |= add;
      break;
#ifndef NODES
    case NEG_CHAP80:
      cx->physical->link.lcp.cfg.chap80nt &= keep;
      cx->physical->link.lcp.cfg.chap80nt |= add;
      break;
    case NEG_CHAP80LM:
      cx->physical->link.lcp.cfg.chap80lm &= keep;
      cx->physical->link.lcp.cfg.chap80lm |= add;
      break;
    case NEG_CHAP81:
      cx->physical->link.lcp.cfg.chap81 &= keep;
      cx->physical->link.lcp.cfg.chap81 |= add;
      break;
    case NEG_MPPE:
      l->ccp.cfg.neg[CCP_NEG_MPPE] &= keep;
      l->ccp.cfg.neg[CCP_NEG_MPPE] |= add;
      break;
#endif
    case NEG_DEFLATE:
      l->ccp.cfg.neg[CCP_NEG_DEFLATE] &= keep;
      l->ccp.cfg.neg[CCP_NEG_DEFLATE] |= add;
      break;
    case NEG_DNS:
      arg->bundle->ncp.ipcp.cfg.ns.dns_neg &= keep;
      arg->bundle->ncp.ipcp.cfg.ns.dns_neg |= add;
      break;
    case NEG_ECHO:	/* probably misplaced in this function ! */
      if (cx->physical->link.lcp.cfg.echo && !add) {
        cx->physical->link.lcp.cfg.echo = 0;
        cx->physical->hdlc.lqm.method &= ~LQM_ECHO;
        if (cx->physical->hdlc.lqm.method & LQM_ECHO &&
            !cx->physical->link.lcp.want_lqrperiod && 
            cx->physical->hdlc.lqm.timer.load) {
          cx->physical->hdlc.lqm.timer.load = 0;
          lqr_StopTimer(cx->physical);
        }
      } else if (!cx->physical->link.lcp.cfg.echo && add) {
        cx->physical->link.lcp.cfg.echo = 1;
        cx->physical->hdlc.lqm.method |= LQM_ECHO;
        cx->physical->hdlc.lqm.timer.load =
	    cx->physical->link.lcp.cfg.lqrperiod * SECTICKS;
        if (cx->physical->link.lcp.fsm.state == ST_OPENED)
          (*cx->physical->hdlc.lqm.timer.func)(&cx->physical->link.lcp);
      }
      break;
    case NEG_ENDDISC:
      arg->bundle->ncp.mp.cfg.negenddisc &= keep;
      arg->bundle->ncp.mp.cfg.negenddisc |= add;
      break;
    case NEG_LQR:
      cx->physical->link.lcp.cfg.lqr &= keep;
      cx->physical->link.lcp.cfg.lqr |= add;
      break;
    case NEG_PAP:
      cx->physical->link.lcp.cfg.pap &= keep;
      cx->physical->link.lcp.cfg.pap |= add;
      break;
    case NEG_PPPDDEFLATE:
      l->ccp.cfg.neg[CCP_NEG_DEFLATE24] &= keep;
      l->ccp.cfg.neg[CCP_NEG_DEFLATE24] |= add;
      break;
    case NEG_PRED1:
      l->ccp.cfg.neg[CCP_NEG_PRED1] &= keep;
      l->ccp.cfg.neg[CCP_NEG_PRED1] |= add;
      break;
    case NEG_PROTOCOMP:
      cx->physical->link.lcp.cfg.protocomp &= keep;
      cx->physical->link.lcp.cfg.protocomp |= add;
      break;
    case NEG_SHORTSEQ:
      switch (bundle_Phase(arg->bundle)) {
        case PHASE_DEAD:
          break;
        case PHASE_ESTABLISH:
          /* Make sure none of our links are DATALINK_LCP or greater */
          if (bundle_HighestState(arg->bundle) >= DATALINK_LCP) {
            log_Printf(LogWARN, "shortseq: Only changeable before"
                       " LCP negotiations\n");
            return 1;
          }
          break;
        default:
          log_Printf(LogWARN, "shortseq: Only changeable at phase"
                     " DEAD/ESTABLISH\n");
          return 1;
      }
      arg->bundle->ncp.mp.cfg.shortseq &= keep;
      arg->bundle->ncp.mp.cfg.shortseq |= add;
      break;
    case NEG_VJCOMP:
      arg->bundle->ncp.ipcp.cfg.vj.neg &= keep;
      arg->bundle->ncp.ipcp.cfg.vj.neg |= add;
      break;
  }

  return 0;
}

static struct cmdtab const NegotiateCommands[] = {
  {"echo", NULL, NegotiateSet, LOCAL_AUTH | LOCAL_CX, "Send echo requests",
  "disable|enable", (const void *)NEG_ECHO},
  {"filter-decapsulation", NULL, OptSet, LOCAL_AUTH,
  "filter on PPPoUDP payloads", "disable|enable",
  (const void *)OPT_FILTERDECAP},
  {"force-scripts", NULL, OptSet, LOCAL_AUTH,
   "Force execution of the configured chat scripts", "disable|enable",
   (const void *)OPT_FORCE_SCRIPTS},
  {"idcheck", NULL, OptSet, LOCAL_AUTH, "Check FSM reply ids",
  "disable|enable", (const void *)OPT_IDCHECK},
  {"iface-alias", NULL, IfaceAliasOptSet, LOCAL_AUTH,
  "retain interface addresses", "disable|enable",
  (const void *)OPT_IFACEALIAS},
#ifndef NOINET6
  {"ipcp", NULL, OptSet, LOCAL_AUTH, "IP Network Control Protocol",
  "disable|enable", (const void *)OPT_IPCP},
  {"ipv6cp", NULL, OptSet, LOCAL_AUTH, "IPv6 Network Control Protocol",
  "disable|enable", (const void *)OPT_IPV6CP},
#endif
  {"keep-session", NULL, OptSet, LOCAL_AUTH, "Retain device session leader",
  "disable|enable", (const void *)OPT_KEEPSESSION},
  {"loopback", NULL, OptSet, LOCAL_AUTH, "Loop packets for local iface",
  "disable|enable", (const void *)OPT_LOOPBACK},
  {"nas-ip-address", NULL, OptSet, LOCAL_AUTH, "Send NAS-IP-Address to RADIUS",
  "disable|enable", (const void *)OPT_NAS_IP_ADDRESS},
  {"nas-identifier", NULL, OptSet, LOCAL_AUTH, "Send NAS-Identifier to RADIUS",
  "disable|enable", (const void *)OPT_NAS_IDENTIFIER},
  {"passwdauth", NULL, OptSet, LOCAL_AUTH, "Use passwd file",
  "disable|enable", (const void *)OPT_PASSWDAUTH},
  {"proxy", NULL, OptSet, LOCAL_AUTH, "Create a proxy ARP entry",
  "disable|enable", (const void *)OPT_PROXY},
  {"proxyall", NULL, OptSet, LOCAL_AUTH, "Proxy ARP for all remote hosts",
  "disable|enable", (const void *)OPT_PROXYALL},
  {"sroutes", NULL, OptSet, LOCAL_AUTH, "Use sticky routes",
  "disable|enable", (const void *)OPT_SROUTES},
  {"tcpmssfixup", "mssfixup", OptSet, LOCAL_AUTH, "Modify MSS options",
  "disable|enable", (const void *)OPT_TCPMSSFIXUP},
  {"throughput", NULL, OptSet, LOCAL_AUTH, "Rolling throughput",
  "disable|enable", (const void *)OPT_THROUGHPUT},
  {"utmp", NULL, OptSet, LOCAL_AUTH, "Log connections in utmp",
  "disable|enable", (const void *)OPT_UTMP},

#ifndef NOINET6
#define NEG_OPT_MAX 17	/* accept/deny allowed below and not above */
#else
#define NEG_OPT_MAX 15
#endif

  {"acfcomp", NULL, NegotiateSet, LOCAL_AUTH | LOCAL_CX,
  "Address & Control field compression", "accept|deny|disable|enable",
  (const void *)NEG_ACFCOMP},
  {"chap", "chap05", NegotiateSet, LOCAL_AUTH | LOCAL_CX,
  "Challenge Handshake Authentication Protocol", "accept|deny|disable|enable",
  (const void *)NEG_CHAP05},
#ifndef NODES
  {"mschap", "chap80nt", NegotiateSet, LOCAL_AUTH | LOCAL_CX,
  "Microsoft (NT) CHAP", "accept|deny|disable|enable",
  (const void *)NEG_CHAP80},
  {"LANMan", "chap80lm", NegotiateSet, LOCAL_AUTH | LOCAL_CX,
  "Microsoft (NT) CHAP", "accept|deny|disable|enable",
  (const void *)NEG_CHAP80LM},
  {"mschapv2", "chap81", NegotiateSet, LOCAL_AUTH | LOCAL_CX,
  "Microsoft CHAP v2", "accept|deny|disable|enable",
  (const void *)NEG_CHAP81},
  {"mppe", NULL, NegotiateSet, LOCAL_AUTH | LOCAL_CX_OPT,
  "MPPE encryption", "accept|deny|disable|enable",
  (const void *)NEG_MPPE},
#endif
  {"deflate", NULL, NegotiateSet, LOCAL_AUTH | LOCAL_CX_OPT,
  "Deflate compression", "accept|deny|disable|enable",
  (const void *)NEG_DEFLATE},
  {"deflate24", NULL, NegotiateSet, LOCAL_AUTH | LOCAL_CX_OPT,
  "Deflate (type 24) compression", "accept|deny|disable|enable",
  (const void *)NEG_PPPDDEFLATE},
  {"dns", NULL, NegotiateSet, LOCAL_AUTH,
  "DNS specification", "accept|deny|disable|enable", (const void *)NEG_DNS},
  {"enddisc", NULL, NegotiateSet, LOCAL_AUTH, "ENDDISC negotiation",
  "accept|deny|disable|enable", (const void *)NEG_ENDDISC},
  {"lqr", NULL, NegotiateSet, LOCAL_AUTH | LOCAL_CX,
  "Link Quality Reports", "accept|deny|disable|enable",
  (const void *)NEG_LQR},
  {"pap", NULL, NegotiateSet, LOCAL_AUTH | LOCAL_CX,
  "Password Authentication protocol", "accept|deny|disable|enable",
  (const void *)NEG_PAP},
  {"pred1", "predictor1", NegotiateSet, LOCAL_AUTH | LOCAL_CX_OPT,
  "Predictor 1 compression", "accept|deny|disable|enable",
  (const void *)NEG_PRED1},
  {"protocomp", NULL, NegotiateSet, LOCAL_AUTH | LOCAL_CX,
  "Protocol field compression", "accept|deny|disable|enable",
  (const void *)NEG_PROTOCOMP},
  {"shortseq", NULL, NegotiateSet, LOCAL_AUTH,
  "MP Short Sequence Numbers", "accept|deny|disable|enable",
  (const void *)NEG_SHORTSEQ},
  {"vjcomp", NULL, NegotiateSet, LOCAL_AUTH,
  "Van Jacobson header compression", "accept|deny|disable|enable",
  (const void *)NEG_VJCOMP},
  {"help", "?", HelpCommand, LOCAL_AUTH | LOCAL_NO_AUTH,
  "Display this message", "accept|deny|disable|enable help|? [value]",
  NegotiateCommands},
  {NULL, NULL, NULL, 0, NULL, NULL, NULL},
};

static int
NegotiateCommand(struct cmdargs const *arg)
{
  if (arg->argc > arg->argn) {
    char const *argv[3];
    unsigned keep, add;
    int n;

    if ((argv[0] = ident_cmd(arg->argv[arg->argn-1], &keep, &add)) == NULL)
      return -1;
    argv[2] = NULL;

    for (n = arg->argn; n < arg->argc; n++) {
      argv[1] = arg->argv[n];
      FindExec(arg->bundle, NegotiateCommands + (keep == NEG_HISMASK ?
               0 : NEG_OPT_MAX), 2, 1, argv, arg->prompt, arg->cx);
    }
  } else if (arg->prompt)
    prompt_Printf(arg->prompt, "Use `%s ?' to get a list.\n",
	    arg->argv[arg->argn-1]);
  else
    log_Printf(LogWARN, "%s command must have arguments\n",
              arg->argv[arg->argn] );

  return 0;
}

const char *
command_ShowNegval(unsigned val)
{
  switch (val&3) {
    case 1: return "disabled & accepted";
    case 2: return "enabled & denied";
    case 3: return "enabled & accepted";
  }
  return "disabled & denied";
}

static int
ClearCommand(struct cmdargs const *arg)
{
  struct pppThroughput *t;
  struct datalink *cx;
  int i, clear_type;

  if (arg->argc < arg->argn + 1)
    return -1;

  if (strcasecmp(arg->argv[arg->argn], "physical") == 0) {
    cx = arg->cx;
    if (!cx)
      cx = bundle2datalink(arg->bundle, NULL);
    if (!cx) {
      log_Printf(LogWARN, "A link must be specified for ``clear physical''\n");
      return 1;
    }
    t = &cx->physical->link.stats.total;
  } else if (strcasecmp(arg->argv[arg->argn], "ipcp") == 0)
    t = &arg->bundle->ncp.ipcp.throughput;
#ifndef NOINET6
  else if (strcasecmp(arg->argv[arg->argn], "ipv6cp") == 0)
    t = &arg->bundle->ncp.ipv6cp.throughput;
#endif
  else
    return -1;

  if (arg->argc > arg->argn + 1) {
    clear_type = 0;
    for (i = arg->argn + 1; i < arg->argc; i++)
      if (strcasecmp(arg->argv[i], "overall") == 0)
        clear_type |= THROUGHPUT_OVERALL;
      else if (strcasecmp(arg->argv[i], "current") == 0)
        clear_type |= THROUGHPUT_CURRENT;
      else if (strcasecmp(arg->argv[i], "peak") == 0)
        clear_type |= THROUGHPUT_PEAK;
      else
        return -1;
  } else
    clear_type = THROUGHPUT_ALL;

  throughput_clear(t, clear_type, arg->prompt);
  return 0;
}

static int
RunListCommand(struct cmdargs const *arg)
{
  const char *cmd = arg->argc ? arg->argv[arg->argc - 1] : "???";

#ifndef NONAT
  if (arg->cmd->args == NatCommands &&
      tolower(*arg->argv[arg->argn - 1]) == 'a') {
    if (arg->prompt)
      prompt_Printf(arg->prompt, "The alias command is deprecated\n");
    else
      log_Printf(LogWARN, "The alias command is deprecated\n");
  }
#endif

  if (arg->argc > arg->argn)
    FindExec(arg->bundle, arg->cmd->args, arg->argc, arg->argn, arg->argv,
             arg->prompt, arg->cx);
  else if (arg->prompt)
    prompt_Printf(arg->prompt, "Use `%s help' to get a list or `%s help"
                  " <option>' for syntax help.\n", cmd, cmd);
  else
    log_Printf(LogWARN, "%s command must have arguments\n", cmd);

  return 0;
}

static int
IfaceNameCommand(struct cmdargs const *arg)
{
  int n = arg->argn;

  if (arg->argc != n + 1)
    return -1;

  if (!iface_Name(arg->bundle->iface, arg->argv[n]))
    return 1;

  log_SetTun(arg->bundle->unit, arg->bundle->iface->name);
  return 0;
}

static int
IfaceAddCommand(struct cmdargs const *arg)
{
  struct ncpaddr peer, addr;
  struct ncprange ifa;
  struct in_addr mask;
  int n, how;

  if (arg->argc == arg->argn + 1) {
    if (!ncprange_aton(&ifa, NULL, arg->argv[arg->argn]))
      return -1;
    ncpaddr_init(&peer);
  } else {
    if (arg->argc == arg->argn + 2) {
      if (!ncprange_aton(&ifa, NULL, arg->argv[arg->argn]))
        return -1;
      n = 1;
    } else if (arg->argc == arg->argn + 3) {
      if (!ncpaddr_aton(&addr, NULL, arg->argv[arg->argn]))
        return -1;
      if (ncpaddr_family(&addr) != AF_INET)
        return -1;
      ncprange_sethost(&ifa, &addr);
      if (!ncpaddr_aton(&addr, NULL, arg->argv[arg->argn + 1]))
        return -1;
      if (!ncpaddr_getip4(&addr, &mask))
        return -1;
      if (!ncprange_setip4mask(&ifa, mask))
        return -1;
      n = 2;
    } else
      return -1;

    if (!ncpaddr_aton(&peer, NULL, arg->argv[arg->argn + n]))
      return -1;

    if (ncprange_family(&ifa) != ncpaddr_family(&peer)) {
      log_Printf(LogWARN, "IfaceAddCommand: src and dst address families"
                 " differ\n");
      return -1;
    }
  }

  how = IFACE_ADD_LAST;
  if (arg->cmd->args)
    how |= IFACE_FORCE_ADD;

  return !iface_Add(arg->bundle->iface, &arg->bundle->ncp, &ifa, &peer, how);
}

static int
IfaceDeleteCommand(struct cmdargs const *arg)
{
  struct ncpaddr ifa;
  struct in_addr ifa4;
  int ok;

  if (arg->argc != arg->argn + 1)
    return -1;

  if (!ncpaddr_aton(&ifa, NULL, arg->argv[arg->argn]))
    return -1;

  if (arg->bundle->ncp.ipcp.fsm.state == ST_OPENED &&
      ncpaddr_getip4(&ifa, &ifa4) &&
      arg->bundle->ncp.ipcp.my_ip.s_addr == ifa4.s_addr) {
    log_Printf(LogWARN, "%s: Cannot remove active interface address\n",
               ncpaddr_ntoa(&ifa));
    return 1;
  }

  ok = iface_Delete(arg->bundle->iface, &arg->bundle->ncp, &ifa);
  if (!ok) {
    if (arg->cmd->args)
      ok = 1;
    else if (arg->prompt)
      prompt_Printf(arg->prompt, "%s: No such interface address\n",
                    ncpaddr_ntoa(&ifa));
    else
      log_Printf(LogWARN, "%s: No such interface address\n",
                 ncpaddr_ntoa(&ifa));
  }

  return !ok;
}

static int
IfaceClearCommand(struct cmdargs const *arg)
{
  int family, how;

  family = 0;
  if (arg->argc == arg->argn + 1) {
    if (strcasecmp(arg->argv[arg->argn], "inet") == 0)
      family = AF_INET;
#ifndef NOINET6
    else if (strcasecmp(arg->argv[arg->argn], "inet6") == 0)
      family = AF_INET6;
#endif
    else
      return -1;
  } else if (arg->argc != arg->argn)
    return -1;

  how = arg->bundle->ncp.ipcp.fsm.state == ST_OPENED ||
        arg->bundle->phys_type.all & PHYS_AUTO ?
        IFACE_CLEAR_ALIASES : IFACE_CLEAR_ALL;
  iface_Clear(arg->bundle->iface, &arg->bundle->ncp, family, how);

  return 0;
}

static int
SetProcTitle(struct cmdargs const *arg)
{
  static char title[LINE_LEN];
  char *argv[MAXARGS];
  int argc = arg->argc - arg->argn;

  if (arg->argc <= arg->argn) {
    SetTitle(NULL);
    return 0;
  }

  if ((unsigned)argc >= sizeof argv / sizeof argv[0]) {
    argc = sizeof argv / sizeof argv[0] - 1;
    log_Printf(LogWARN, "Truncating proc title to %d args\n", argc);
  }
  command_Expand(argv, argc, arg->argv + arg->argn, arg->bundle, 1, getpid());
  Concatinate(title, sizeof title, argc, (const char *const *)argv);
  SetTitle(title);
  command_Free(argc, argv);

  return 0;
}
