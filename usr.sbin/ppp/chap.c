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
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#ifndef NODES
#include <md4.h>
#endif
#include <md5.h>
#include <paths.h>
#include <signal.h>
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
#include "fsm.h"
#include "proto.h"
#include "lqr.h"
#include "hdlc.h"
#include "lcp.h"
#include "auth.h"
#include "async.h"
#include "throughput.h"
#include "descriptor.h"
#include "chap.h"
#include "iplist.h"
#include "slcompress.h"
#include "ncpaddr.h"
#include "ipcp.h"
#include "filter.h"
#include "ccp.h"
#include "link.h"
#include "physical.h"
#include "mp.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"
#include "chat.h"
#include "cbcp.h"
#include "command.h"
#include "datalink.h"
#ifndef NODES
#include "chap_ms.h"
#include "mppe.h"
#endif
#include "id.h"

static const char * const chapcodes[] = {
  "???", "CHALLENGE", "RESPONSE", "SUCCESS", "FAILURE"
};
#define MAXCHAPCODE (sizeof chapcodes / sizeof chapcodes[0] - 1)

static void
ChapOutput(struct physical *physical, u_int code, u_int id,
	   const u_char *ptr, int count, const char *text)
{
  int plen;
  struct fsmheader lh;
  struct mbuf *bp;

  plen = sizeof(struct fsmheader) + count;
  lh.code = code;
  lh.id = id;
  lh.length = htons(plen);
  bp = m_get(plen, MB_CHAPOUT);
  memcpy(MBUF_CTOP(bp), &lh, sizeof(struct fsmheader));
  if (count)
    memcpy(MBUF_CTOP(bp) + sizeof(struct fsmheader), ptr, count);
  log_DumpBp(LogDEBUG, "ChapOutput", bp);
  if (text == NULL)
    log_Printf(LogPHASE, "Chap Output: %s\n", chapcodes[code]);
  else
    log_Printf(LogPHASE, "Chap Output: %s (%s)\n", chapcodes[code], text);
  link_PushPacket(&physical->link, bp, physical->dl->bundle,
                  LINK_QUEUES(&physical->link) - 1, PROTO_CHAP);
}

static char *
chap_BuildAnswer(char *name, char *key, u_char id, char *challenge
#ifndef NODES
		 , u_char type, char *peerchallenge, char *authresponse,
		 int lanman
#endif
                )
{
  char *result, *digest;
  size_t nlen, klen;

  nlen = strlen(name);
  klen = strlen(key);

#ifndef NODES
  if (type == 0x80) {
    char expkey[AUTHLEN << 2];
    MD4_CTX MD4context;
    size_t f;

    if ((result = malloc(1 + nlen + MS_CHAP_RESPONSE_LEN)) == NULL)
      return result;

    digest = result;					/* the response */
    *digest++ = MS_CHAP_RESPONSE_LEN;			/* 49 */
    memcpy(digest + MS_CHAP_RESPONSE_LEN, name, nlen);
    if (lanman) {
      memset(digest + 24, '\0', 25);
      mschap_LANMan(digest, challenge + 1, key);	/* LANMan response */
    } else {
      memset(digest, '\0', 25);
      digest += 24;

      for (f = 0; f < klen; f++) {
        expkey[2*f] = key[f];
        expkey[2*f+1] = '\0';
      }
      /*
       *           -----------
       * expkey = | k\0e\0y\0 |
       *           -----------
       */
      MD4Init(&MD4context);
      MD4Update(&MD4context, expkey, klen << 1);
      MD4Final(digest, &MD4context);

      /*
       *           ---- -------- ---------------- ------- ------
       * result = | 49 | LANMan | 16 byte digest | 9 * ? | name |
       *           ---- -------- ---------------- ------- ------
       */
      mschap_NT(digest, challenge + 1);
    }
    /*
     *           ---- -------- ------------- ----- ------
     *          |    |  struct MS_ChapResponse24  |      |
     * result = | 49 | LANMan  |  NT digest | 0/1 | name |
     *           ---- -------- ------------- ----- ------
     * where only one of LANMan & NT digest are set.
     */
  } else if (type == 0x81) {
    char expkey[AUTHLEN << 2];
    char pwdhash[CHAP81_HASH_LEN];
    char pwdhashhash[CHAP81_HASH_LEN];
    char *ntresponse;
    size_t f;

    if ((result = malloc(1 + nlen + CHAP81_RESPONSE_LEN)) == NULL)
      return result;

    memset(result, 0, 1 + nlen + CHAP81_RESPONSE_LEN);

    digest = result;
    *digest++ = CHAP81_RESPONSE_LEN;		/* value size */

    /* Copy our challenge */
    memcpy(digest, peerchallenge + 1, CHAP81_CHALLENGE_LEN);

    /* Expand password to Unicode XXX */
    for (f = 0; f < klen; f++) {
      expkey[2*f] = key[f];
      expkey[2*f+1] = '\0';
    }

    ntresponse = digest + CHAP81_NTRESPONSE_OFF;

    /* Get some needed hashes */
    NtPasswordHash(expkey, klen * 2, pwdhash);
    HashNtPasswordHash(pwdhash, pwdhashhash);

    /* Generate NTRESPONSE to respond on challenge call */
    GenerateNTResponse(challenge + 1, peerchallenge + 1, name,
                       expkey, klen * 2, ntresponse);

    /* Generate MPPE MASTERKEY */
    GetMasterKey(pwdhashhash, ntresponse, MPPE_MasterKey);    /* XXX Global ! */

    /* Generate AUTHRESPONSE to verify on auth success */
    GenerateAuthenticatorResponse(expkey, klen * 2, ntresponse,
                                  peerchallenge + 1, challenge + 1, name,
                                  authresponse);

    authresponse[CHAP81_AUTHRESPONSE_LEN] = 0;

    memcpy(digest + CHAP81_RESPONSE_LEN, name, nlen);
  } else
#endif
  if ((result = malloc(nlen + 17)) != NULL) {
    /* Normal MD5 stuff */
    MD5_CTX MD5context;

    digest = result;
    *digest++ = 16;				/* value size */

    MD5Init(&MD5context);
    MD5Update(&MD5context, &id, 1);
    MD5Update(&MD5context, key, klen);
    MD5Update(&MD5context, challenge + 1, *challenge);
    MD5Final(digest, &MD5context);

    memcpy(digest + 16, name, nlen);
    /*
     *           ---- -------- ------
     * result = | 16 | digest | name |
     *           ---- -------- ------
     */
  }

  return result;
}

static void
chap_StartChild(struct chap *chap, char *prog, const char *name)
{
  char *argv[MAXARGS], *nargv[MAXARGS];
  int argc, fd;
  int in[2], out[2];
  pid_t pid;

  if (chap->child.fd != -1) {
    log_Printf(LogWARN, "Chap: %s: Program already running\n", prog);
    return;
  }

  if (pipe(in) == -1) {
    log_Printf(LogERROR, "Chap: pipe: %s\n", strerror(errno));
    return;
  }

  if (pipe(out) == -1) {
    log_Printf(LogERROR, "Chap: pipe: %s\n", strerror(errno));
    close(in[0]);
    close(in[1]);
    return;
  }

  pid = getpid();
  switch ((chap->child.pid = fork())) {
    case -1:
      log_Printf(LogERROR, "Chap: fork: %s\n", strerror(errno));
      close(in[0]);
      close(in[1]);
      close(out[0]);
      close(out[1]);
      chap->child.pid = 0;
      return;

    case 0:
      timer_TermService();

      if ((argc = command_Interpret(prog, strlen(prog), argv)) <= 0) {
        if (argc < 0) {
          log_Printf(LogWARN, "CHAP: Invalid command syntax\n");
          _exit(255);
        }
        _exit(0);
      }

      close(in[1]);
      close(out[0]);
      if (out[1] == STDIN_FILENO)
        out[1] = dup(out[1]);
      dup2(in[0], STDIN_FILENO);
      dup2(out[1], STDOUT_FILENO);
      close(STDERR_FILENO);
      if (open(_PATH_DEVNULL, O_RDWR) != STDERR_FILENO) {
        log_Printf(LogALERT, "Chap: Failed to open %s: %s\n",
                  _PATH_DEVNULL, strerror(errno));
        exit(1);
      }
      for (fd = getdtablesize(); fd > STDERR_FILENO; fd--)
        fcntl(fd, F_SETFD, 1);
#ifndef NOSUID
      setuid(ID0realuid());
#endif
      command_Expand(nargv, argc, (char const *const *)argv,
                     chap->auth.physical->dl->bundle, 0, pid);
      execvp(nargv[0], nargv);
      printf("exec() of %s failed: %s\n", nargv[0], strerror(errno));
      _exit(255);

    default:
      close(in[0]);
      close(out[1]);
      chap->child.fd = out[0];
      chap->child.buf.len = 0;
      write(in[1], chap->auth.in.name, strlen(chap->auth.in.name));
      write(in[1], "\n", 1);
      write(in[1], chap->challenge.peer + 1, *chap->challenge.peer);
      write(in[1], "\n", 1);
      write(in[1], name, strlen(name));
      write(in[1], "\n", 1);
      close(in[1]);
      break;
  }
}

static void
chap_Cleanup(struct chap *chap, int sig)
{
  if (chap->child.pid) {
    int status;

    close(chap->child.fd);
    chap->child.fd = -1;
    if (sig)
      kill(chap->child.pid, SIGTERM);
    chap->child.pid = 0;
    chap->child.buf.len = 0;

    if (wait(&status) == -1)
      log_Printf(LogERROR, "Chap: wait: %s\n", strerror(errno));
    else if (WIFSIGNALED(status))
      log_Printf(LogWARN, "Chap: Child received signal %d\n", WTERMSIG(status));
    else if (WIFEXITED(status) && WEXITSTATUS(status))
      log_Printf(LogERROR, "Chap: Child exited %d\n", WEXITSTATUS(status));
  }
  *chap->challenge.local = *chap->challenge.peer = '\0';
#ifndef NODES
  chap->peertries = 0;
#endif
}

static void
chap_Respond(struct chap *chap, char *name, char *key
#ifndef NODES
             , u_char type, int lm
#endif
            )
{
  u_char *ans;

  ans = chap_BuildAnswer(name, key, chap->auth.id, chap->challenge.peer
#ifndef NODES
                         , type, chap->challenge.local, chap->authresponse, lm
#endif
                        );

  if (ans) {
    ChapOutput(chap->auth.physical, CHAP_RESPONSE, chap->auth.id,
               ans, *ans + 1 + strlen(name), name);
#ifndef NODES
    chap->NTRespSent = !lm;
    MPPE_IsServer = 0;		/* XXX Global ! */
#endif
    free(ans);
  } else
    ChapOutput(chap->auth.physical, CHAP_FAILURE, chap->auth.id,
               "Out of memory!", 14, NULL);
}

static int
chap_UpdateSet(struct fdescriptor *d, fd_set *r, fd_set *w __unused,
	       fd_set *e __unused, int *n)
{
  struct chap *chap = descriptor2chap(d);

  if (r && chap && chap->child.fd != -1) {
    FD_SET(chap->child.fd, r);
    if (*n < chap->child.fd + 1)
      *n = chap->child.fd + 1;
    log_Printf(LogTIMER, "Chap: fdset(r) %d\n", chap->child.fd);
    return 1;
  }

  return 0;
}

static int
chap_IsSet(struct fdescriptor *d, const fd_set *fdset)
{
  struct chap *chap = descriptor2chap(d);

  return chap && chap->child.fd != -1 && FD_ISSET(chap->child.fd, fdset);
}

static void
chap_Read(struct fdescriptor *d, struct bundle *bundle __unused,
	  const fd_set *fdset __unused)
{
  struct chap *chap = descriptor2chap(d);
  int got;

  got = read(chap->child.fd, chap->child.buf.ptr + chap->child.buf.len,
             sizeof chap->child.buf.ptr - chap->child.buf.len - 1);
  if (got == -1) {
    log_Printf(LogERROR, "Chap: Read: %s\n", strerror(errno));
    chap_Cleanup(chap, SIGTERM);
  } else if (got == 0) {
    log_Printf(LogWARN, "Chap: Read: Child terminated connection\n");
    chap_Cleanup(chap, SIGTERM);
  } else {
    char *name, *key, *end;

    chap->child.buf.len += got;
    chap->child.buf.ptr[chap->child.buf.len] = '\0';
    name = chap->child.buf.ptr;
    name += strspn(name, " \t");
    if ((key = strchr(name, '\n')) == NULL)
      end = NULL;
    else
      end = strchr(++key, '\n');

    if (end == NULL) {
      if (chap->child.buf.len == sizeof chap->child.buf.ptr - 1) {
        log_Printf(LogWARN, "Chap: Read: Input buffer overflow\n");
        chap_Cleanup(chap, SIGTERM);
      }
    } else {
#ifndef NODES
      int lanman = chap->auth.physical->link.lcp.his_authtype == 0x80 &&
                   ((chap->NTRespSent &&
                     IsAccepted(chap->auth.physical->link.lcp.cfg.chap80lm)) ||
                    !IsAccepted(chap->auth.physical->link.lcp.cfg.chap80nt));
#endif

      while (end >= name && strchr(" \t\r\n", *end))
        *end-- = '\0';
      end = key - 1;
      while (end >= name && strchr(" \t\r\n", *end))
        *end-- = '\0';
      key += strspn(key, " \t");

      chap_Respond(chap, name, key
#ifndef NODES
                   , chap->auth.physical->link.lcp.his_authtype, lanman
#endif
                  );
      chap_Cleanup(chap, 0);
    }
  }
}

static int
chap_Write(struct fdescriptor *d __unused, struct bundle *bundle __unused,
	   const fd_set *fdset __unused)
{
  /* We never want to write here ! */
  log_Printf(LogALERT, "chap_Write: Internal error: Bad call !\n");
  return 0;
}

static void
chap_ChallengeInit(struct authinfo *authp)
{
  struct chap *chap = auth2chap(authp);
  int len, i;
  char *cp;

  len = strlen(authp->physical->dl->bundle->cfg.auth.name);

  if (!*chap->challenge.local) {
    randinit();
    cp = chap->challenge.local;

#ifndef NORADIUS
    if (*authp->physical->dl->bundle->radius.cfg.file) {
      /* For radius, our challenge is 16 readable NUL terminated bytes :*/
      *cp++ = 16;
      for (i = 0; i < 16; i++)
        *cp++ = (random() % 10) + '0';
    } else
#endif
    {
#ifndef NODES
      if (authp->physical->link.lcp.want_authtype == 0x80)
        *cp++ = 8;	/* MS does 8 byte callenges :-/ */
      else if (authp->physical->link.lcp.want_authtype == 0x81)
        *cp++ = 16;	/* MS-CHAP-V2 does 16 bytes challenges */
      else
#endif
        *cp++ = random() % (CHAPCHALLENGELEN-16) + 16;
      for (i = 0; i < *chap->challenge.local; i++)
        *cp++ = random() & 0xff;
    }
    memcpy(cp, authp->physical->dl->bundle->cfg.auth.name, len);
  }
}

static void
chap_Challenge(struct authinfo *authp)
{
  struct chap *chap = auth2chap(authp);
  int len;

  log_Printf(LogDEBUG, "CHAP%02X: Challenge\n",
             authp->physical->link.lcp.want_authtype);

  len = strlen(authp->physical->dl->bundle->cfg.auth.name);

  /* Generate new local challenge value */
  if (!*chap->challenge.local)
    chap_ChallengeInit(authp);

#ifndef NODES
  if (authp->physical->link.lcp.want_authtype == 0x81)
    ChapOutput(authp->physical, CHAP_CHALLENGE, authp->id,
             chap->challenge.local, 1 + *chap->challenge.local, NULL);
  else
#endif
    ChapOutput(authp->physical, CHAP_CHALLENGE, authp->id,
             chap->challenge.local, 1 + *chap->challenge.local + len, NULL);
}

static void
chap_Success(struct authinfo *authp)
{
  struct bundle *bundle = authp->physical->dl->bundle;
  const char *msg;

  datalink_GotAuthname(authp->physical->dl, authp->in.name);
#ifndef NODES
  if (authp->physical->link.lcp.want_authtype == 0x81) {
#ifndef NORADIUS
    if (*bundle->radius.cfg.file && bundle->radius.msrepstr)
      msg = bundle->radius.msrepstr;
    else
#endif
      msg = auth2chap(authp)->authresponse;
    MPPE_MasterKeyValid = 1;		/* XXX Global ! */
  } else
#endif
#ifndef NORADIUS
  if (*bundle->radius.cfg.file && bundle->radius.repstr)
    msg = bundle->radius.repstr;
  else
#endif
    msg = "Welcome!!";

  ChapOutput(authp->physical, CHAP_SUCCESS, authp->id, msg, strlen(msg),
             NULL);

  authp->physical->link.lcp.auth_ineed = 0;
  if (Enabled(bundle, OPT_UTMP))
    physical_Login(authp->physical, authp->in.name);

  if (authp->physical->link.lcp.auth_iwait == 0)
    /*
     * Either I didn't need to authenticate, or I've already been
     * told that I got the answer right.
     */
    datalink_AuthOk(authp->physical->dl);
}

static void
chap_Failure(struct authinfo *authp)
{
#ifndef NODES
  char buf[1024], *ptr;
#endif
  const char *msg;

#ifndef NORADIUS
  struct bundle *bundle = authp->physical->link.lcp.fsm.bundle;
  if (*bundle->radius.cfg.file && bundle->radius.errstr)
    msg = bundle->radius.errstr;
  else
#endif
#ifndef NODES
  if (authp->physical->link.lcp.want_authtype == 0x80) {
    sprintf(buf, "E=691 R=1 M=Invalid!");
    msg = buf;
  } else if (authp->physical->link.lcp.want_authtype == 0x81) {
    int i;

    ptr = buf;
    ptr += sprintf(buf, "E=691 R=0 C=");
    for (i=0; i<16; i++)
      ptr += sprintf(ptr, "%02X", *(auth2chap(authp)->challenge.local+1+i));

    sprintf(ptr, " V=3 M=Invalid!");
    msg = buf;
  } else
#endif
    msg = "Invalid!!";

  ChapOutput(authp->physical, CHAP_FAILURE, authp->id, msg, strlen(msg) + 1,
             NULL);
  datalink_AuthNotOk(authp->physical->dl);
}

static int
chap_Cmp(char *myans, int mylen, char *hisans, int hislen
#ifndef NODES
         , u_char type, int lm
#endif
        )
{
  int off;

  if (mylen != hislen)
    return 0;

  off = 0;

#ifndef NODES
  if (type == 0x80) {
    off = lm ? 0 : 24;
    mylen = 24;
  }
#endif

  for (; mylen; off++, mylen--)
    if (toupper(myans[off]) != toupper(hisans[off]))
      return 0;

  return 1;
}

#ifndef NODES
static int
chap_HaveAnotherGo(struct chap *chap)
{
  if (++chap->peertries < 3) {
    /* Give the peer another shot */
    *chap->challenge.local = '\0';
    chap_Challenge(&chap->auth);
    return 1;
  }

  return 0;
}
#endif

void
chap_Init(struct chap *chap, struct physical *p)
{
  chap->desc.type = CHAP_DESCRIPTOR;
  chap->desc.UpdateSet = chap_UpdateSet;
  chap->desc.IsSet = chap_IsSet;
  chap->desc.Read = chap_Read;
  chap->desc.Write = chap_Write;
  chap->child.pid = 0;
  chap->child.fd = -1;
  auth_Init(&chap->auth, p, chap_Challenge, chap_Success, chap_Failure);
  *chap->challenge.local = *chap->challenge.peer = '\0';
#ifndef NODES
  chap->NTRespSent = 0;
  chap->peertries = 0;
#endif
}

void
chap_ReInit(struct chap *chap)
{
  chap_Cleanup(chap, SIGTERM);
}

struct mbuf *
chap_Input(struct bundle *bundle, struct link *l, struct mbuf *bp)
{
  struct physical *p = link2physical(l);
  struct chap *chap = &p->dl->chap;
  char *name, *key, *ans;
  int len;
  size_t nlen;
  u_char alen;
#ifndef NODES
  int lanman;
#endif

  if (p == NULL) {
    log_Printf(LogERROR, "chap_Input: Not a physical link - dropped\n");
    m_freem(bp);
    return NULL;
  }

  if (bundle_Phase(bundle) != PHASE_NETWORK &&
      bundle_Phase(bundle) != PHASE_AUTHENTICATE) {
    log_Printf(LogPHASE, "Unexpected chap input - dropped !\n");
    m_freem(bp);
    return NULL;
  }

  m_settype(bp, MB_CHAPIN);
  if ((bp = auth_ReadHeader(&chap->auth, bp)) == NULL &&
      ntohs(chap->auth.in.hdr.length) == 0)
    log_Printf(LogWARN, "Chap Input: Truncated header !\n");
  else if (chap->auth.in.hdr.code == 0 || chap->auth.in.hdr.code > MAXCHAPCODE)
    log_Printf(LogPHASE, "Chap Input: %d: Bad CHAP code !\n",
               chap->auth.in.hdr.code);
  else {
    len = m_length(bp);
    ans = NULL;

    if (chap->auth.in.hdr.code != CHAP_CHALLENGE &&
        chap->auth.id != chap->auth.in.hdr.id &&
        Enabled(bundle, OPT_IDCHECK)) {
      /* Wrong conversation dude ! */
      log_Printf(LogPHASE, "Chap Input: %s dropped (got id %d, not %d)\n",
                 chapcodes[chap->auth.in.hdr.code], chap->auth.in.hdr.id,
                 chap->auth.id);
      m_freem(bp);
      return NULL;
    }
    chap->auth.id = chap->auth.in.hdr.id;	/* We respond with this id */

#ifndef NODES
    lanman = 0;
#endif
    switch (chap->auth.in.hdr.code) {
      case CHAP_CHALLENGE:
        bp = mbuf_Read(bp, &alen, 1);
        len -= alen + 1;
        if (len < 0) {
          log_Printf(LogERROR, "Chap Input: Truncated challenge !\n");
          m_freem(bp);
          return NULL;
        }
        *chap->challenge.peer = alen;
        bp = mbuf_Read(bp, chap->challenge.peer + 1, alen);
        bp = auth_ReadName(&chap->auth, bp, len);
#ifndef NODES
        lanman = p->link.lcp.his_authtype == 0x80 &&
                 ((chap->NTRespSent && IsAccepted(p->link.lcp.cfg.chap80lm)) ||
                  !IsAccepted(p->link.lcp.cfg.chap80nt));

        /* Generate local challenge value */
        chap_ChallengeInit(&chap->auth);
#endif
        break;

      case CHAP_RESPONSE:
        auth_StopTimer(&chap->auth);
        bp = mbuf_Read(bp, &alen, 1);
        len -= alen + 1;
        if (len < 0) {
          log_Printf(LogERROR, "Chap Input: Truncated response !\n");
          m_freem(bp);
          return NULL;
        }
        if ((ans = malloc(alen + 1)) == NULL) {
          log_Printf(LogERROR, "Chap Input: Out of memory !\n");
          m_freem(bp);
          return NULL;
        }
        *ans = chap->auth.id;
        bp = mbuf_Read(bp, ans + 1, alen);
        bp = auth_ReadName(&chap->auth, bp, len);
#ifndef NODES
        lanman = p->link.lcp.want_authtype == 0x80 &&
                 alen == 49 && ans[alen] == 0;
#endif
        break;

      case CHAP_SUCCESS:
      case CHAP_FAILURE:
        /* chap->auth.in.name is already set up at CHALLENGE time */
        if ((ans = malloc(len + 1)) == NULL) {
          log_Printf(LogERROR, "Chap Input: Out of memory !\n");
          m_freem(bp);
          return NULL;
        }
        bp = mbuf_Read(bp, ans, len);
        ans[len] = '\0';
        break;
    }

    switch (chap->auth.in.hdr.code) {
      case CHAP_CHALLENGE:
      case CHAP_RESPONSE:
        if (*chap->auth.in.name)
          log_Printf(LogPHASE, "Chap Input: %s (%d bytes from %s%s)\n",
                     chapcodes[chap->auth.in.hdr.code], alen,
                     chap->auth.in.name,
#ifndef NODES
                     lanman && chap->auth.in.hdr.code == CHAP_RESPONSE ?
                     " - lanman" :
#endif
                     "");
        else
          log_Printf(LogPHASE, "Chap Input: %s (%d bytes%s)\n",
                     chapcodes[chap->auth.in.hdr.code], alen,
#ifndef NODES
                     lanman && chap->auth.in.hdr.code == CHAP_RESPONSE ?
                     " - lanman" :
#endif
                     "");
        break;

      case CHAP_SUCCESS:
      case CHAP_FAILURE:
        if (*ans)
          log_Printf(LogPHASE, "Chap Input: %s (%s)\n",
                     chapcodes[chap->auth.in.hdr.code], ans);
        else
          log_Printf(LogPHASE, "Chap Input: %s\n",
                     chapcodes[chap->auth.in.hdr.code]);
        break;
    }

    switch (chap->auth.in.hdr.code) {
      case CHAP_CHALLENGE:
        if (*bundle->cfg.auth.key == '!' && bundle->cfg.auth.key[1] != '!')
          chap_StartChild(chap, bundle->cfg.auth.key + 1,
                          bundle->cfg.auth.name);
        else
          chap_Respond(chap, bundle->cfg.auth.name, bundle->cfg.auth.key +
                       (*bundle->cfg.auth.key == '!' ? 1 : 0)
                       
#ifndef NODES
                       , p->link.lcp.his_authtype, lanman
#endif
                      );
        break;

      case CHAP_RESPONSE:
        name = chap->auth.in.name;
        nlen = strlen(name);
#ifndef NODES
        if (p->link.lcp.want_authtype == 0x81) {
          struct MSCHAPv2_resp *resp = (struct MSCHAPv2_resp *)(ans + 1);

          chap->challenge.peer[0] = sizeof resp->PeerChallenge;
          memcpy(chap->challenge.peer + 1, resp->PeerChallenge,
                 sizeof resp->PeerChallenge);
        }
#endif

#ifndef NORADIUS
        if (*bundle->radius.cfg.file) {
          if (!radius_Authenticate(&bundle->radius, &chap->auth,
                                   chap->auth.in.name, ans, alen + 1,
                                   chap->challenge.local + 1,
                                   *chap->challenge.local))
            chap_Failure(&chap->auth);
        } else
#endif
        {
          if (p->link.lcp.want_authtype == 0x81 && ans[alen] != '\0' &&
              alen == sizeof(struct MSCHAPv2_resp)) {
            struct MSCHAPv2_resp *resp = (struct MSCHAPv2_resp *)(ans + 1);

            log_Printf(LogWARN, "%s: Compensating for corrupt (Win98/WinME?) "
                       "CHAP81 RESPONSE\n", l->name);
            resp->Flags = '\0';	/* rfc2759 says it *MUST* be zero */
          }
          key = auth_GetSecret(name, nlen);
          if (key) {
#ifndef NODES
            if (p->link.lcp.want_authtype == 0x80 &&
                lanman && !IsEnabled(p->link.lcp.cfg.chap80lm)) {
              log_Printf(LogPHASE, "Auth failure: LANMan not enabled\n");
              if (chap_HaveAnotherGo(chap))
                break;
              key = NULL;
            } else if (p->link.lcp.want_authtype == 0x80 &&
                !lanman && !IsEnabled(p->link.lcp.cfg.chap80nt)) {
              log_Printf(LogPHASE, "Auth failure: mschap not enabled\n");
              if (chap_HaveAnotherGo(chap))
                break;
              key = NULL;
            } else if (p->link.lcp.want_authtype == 0x81 &&
                !IsEnabled(p->link.lcp.cfg.chap81)) {
              log_Printf(LogPHASE, "Auth failure: CHAP81 not enabled\n");
              key = NULL;
            } else
#endif
            {
              char *myans = chap_BuildAnswer(name, key, chap->auth.id,
                                             chap->challenge.local
#ifndef NODES
					     , p->link.lcp.want_authtype,
					     chap->challenge.peer,
					     chap->authresponse, lanman);
              MPPE_IsServer = 1;		/* XXX Global ! */
#else
                                      );
#endif
              if (myans == NULL)
                key = NULL;
              else {
                if (!chap_Cmp(myans + 1, *myans, ans + 1, alen
#ifndef NODES
                              , p->link.lcp.want_authtype, lanman
#endif
                             ))
                  key = NULL;
                free(myans);
              }
            }
          }

          if (key)
            chap_Success(&chap->auth);
          else
            chap_Failure(&chap->auth);
        }

        break;

      case CHAP_SUCCESS:
        if (p->link.lcp.auth_iwait == PROTO_CHAP) {
          p->link.lcp.auth_iwait = 0;
          if (p->link.lcp.auth_ineed == 0) {
#ifndef NODES
            if (p->link.lcp.his_authtype == 0x81) {
              if (strncasecmp(ans, chap->authresponse, 42)) {
                datalink_AuthNotOk(p->dl);
	        log_Printf(LogWARN, "CHAP81: AuthenticatorResponse: (%.42s)"
                           " != ans: (%.42s)\n", chap->authresponse, ans);

              } else {
                /* Successful login */
                MPPE_MasterKeyValid = 1;		/* XXX Global ! */
                datalink_AuthOk(p->dl);
              }
            } else
#endif
            /*
             * We've succeeded in our ``login''
             * If we're not expecting  the peer to authenticate (or he already
             * has), proceed to network phase.
             */
            datalink_AuthOk(p->dl);
          }
        }
        break;

      case CHAP_FAILURE:
        datalink_AuthNotOk(p->dl);
        break;
    }
    free(ans);
  }

  m_freem(bp);
  return NULL;
}
