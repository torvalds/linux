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

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#ifndef NOPAM
#include <security/pam_appl.h>
#ifdef OPENPAM
#include <security/openpam.h>
#endif
#endif /* !NOPAM */

#include "layer.h"
#include "mbuf.h"
#include "defs.h"
#include "log.h"
#include "timer.h"
#include "fsm.h"
#include "iplist.h"
#include "throughput.h"
#include "slcompress.h"
#include "lqr.h"
#include "hdlc.h"
#include "ncpaddr.h"
#include "ipcp.h"
#include "auth.h"
#include "systems.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "descriptor.h"
#include "chat.h"
#include "proto.h"
#include "filter.h"
#include "mp.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "cbcp.h"
#include "chap.h"
#include "async.h"
#include "physical.h"
#include "datalink.h"
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"

const char *
Auth2Nam(u_short auth, u_char type)
{
  static char chap[10];

  switch (auth) {
  case PROTO_PAP:
    return "PAP";
  case PROTO_CHAP:
    snprintf(chap, sizeof chap, "CHAP 0x%02x", type);
    return chap;
  case 0:
    return "none";
  }
  return "unknown";
}

#if !defined(NOPAM) && !defined(OPENPAM)
static int
pam_conv(int n, const struct pam_message **msg, struct pam_response **resp,
  void *data)
{

  if (n != 1 || msg[0]->msg_style != PAM_PROMPT_ECHO_OFF)
    return (PAM_CONV_ERR);
  if ((*resp = malloc(sizeof(struct pam_response))) == NULL)
    return (PAM_CONV_ERR);
  (*resp)[0].resp = strdup((const char *)data);
  (*resp)[0].resp_retcode = 0;

  return ((*resp)[0].resp != NULL ? PAM_SUCCESS : PAM_CONV_ERR);
}
#endif /* !defined(NOPAM) && !defined(OPENPAM) */

static int
auth_CheckPasswd(const char *name, const char *data, const char *key)
{
  if (!strcmp(data, "*")) {
#ifdef NOPAM
    /* Then look up the real password database */
    struct passwd *pw;
    int result = 0;
    char *cryptpw;
    
    pw = getpwnam(name);

    if (pw) {
      cryptpw = crypt(key, pw->pw_passwd);

      result = (cryptpw != NULL) && !strcmp(cryptpw, pw->pw_passwd);
    }

    endpwent();

    return result;
#else /* !NOPAM */
    /* Then consult with PAM. */
    pam_handle_t *pamh;
    int status;

    struct pam_conv pamc = {
#ifdef OPENPAM
      &openpam_nullconv, NULL
#else
      &pam_conv, key
#endif
    };

    if (pam_start("ppp", name, &pamc, &pamh) != PAM_SUCCESS)
      return (0);
#ifdef OPENPAM
    if ((status = pam_set_item(pamh, PAM_AUTHTOK, key)) == PAM_SUCCESS)
#endif
      status = pam_authenticate(pamh, 0);
    pam_end(pamh, status);
    return (status == PAM_SUCCESS);
#endif /* !NOPAM */
  }

  return !strcmp(data, key);
}

int
auth_SetPhoneList(const char *name, char *phone, int phonelen)
{
  FILE *fp;
  int n, lineno;
  char *vector[6], buff[LINE_LEN];
  const char *slash;

  fp = OpenSecret(SECRETFILE);
  if (fp != NULL) {
again:
    lineno = 0;
    while (fgets(buff, sizeof buff, fp)) {
      lineno++;
      if (buff[0] == '#')
        continue;
      buff[strlen(buff) - 1] = '\0';
      memset(vector, '\0', sizeof vector);
      if ((n = MakeArgs(buff, vector, VECSIZE(vector), PARSE_REDUCE)) < 0)
        log_Printf(LogWARN, "%s: %d: Invalid line\n", SECRETFILE, lineno);
      if (n < 5)
        continue;
      if (strcmp(vector[0], name) == 0) {
        CloseSecret(fp);
        if (*vector[4] == '\0')
          return 0;
        strncpy(phone, vector[4], phonelen - 1);
        phone[phonelen - 1] = '\0';
        return 1;		/* Valid */
      }
    }

    if ((slash = strrchr(name, '\\')) != NULL && slash[1]) {
      /* Look for the name without the leading domain */
      name = slash + 1;
      rewind(fp);
      goto again;
    }

    CloseSecret(fp);
  }
  *phone = '\0';
  return 0;
}

int
auth_Select(struct bundle *bundle, const char *name)
{
  FILE *fp;
  int n, lineno;
  char *vector[5], buff[LINE_LEN];
  const char *slash;

  if (*name == '\0') {
    ipcp_Setup(&bundle->ncp.ipcp, INADDR_NONE);
    return 1;
  }

#ifndef NORADIUS
  if (bundle->radius.valid && bundle->radius.ip.s_addr != INADDR_NONE &&
	bundle->radius.ip.s_addr != RADIUS_INADDR_POOL) {
    /* We've got a radius IP - it overrides everything */
    if (!ipcp_UseHisIPaddr(bundle, bundle->radius.ip))
      return 0;
    ipcp_Setup(&bundle->ncp.ipcp, bundle->radius.mask.s_addr);
    /* Continue with ppp.secret in case we've got a new label */
  }
#endif

  fp = OpenSecret(SECRETFILE);
  if (fp != NULL) {
again:
    lineno = 0;
    while (fgets(buff, sizeof buff, fp)) {
      lineno++;
      if (buff[0] == '#')
        continue;
      buff[strlen(buff) - 1] = '\0';
      memset(vector, '\0', sizeof vector);
      if ((n = MakeArgs(buff, vector, VECSIZE(vector), PARSE_REDUCE)) < 0)
        log_Printf(LogWARN, "%s: %d: Invalid line\n", SECRETFILE, lineno);
      if (n < 2)
        continue;
      if (strcmp(vector[0], name) == 0) {
        CloseSecret(fp);
#ifndef NORADIUS
        if (!bundle->radius.valid || bundle->radius.ip.s_addr == INADDR_NONE) {
#endif
          if (n > 2 && *vector[2] && strcmp(vector[2], "*") &&
              !ipcp_UseHisaddr(bundle, vector[2], 1))
            return 0;
          ipcp_Setup(&bundle->ncp.ipcp, INADDR_NONE);
#ifndef NORADIUS
        }
#endif
        if (n > 3 && *vector[3] && strcmp(vector[3], "*"))
          bundle_SetLabel(bundle, vector[3]);
        return 1;		/* Valid */
      }
    }

    if ((slash = strrchr(name, '\\')) != NULL && slash[1]) {
      /* Look for the name without the leading domain */
      name = slash + 1;
      rewind(fp);
      goto again;
    }

    CloseSecret(fp);
  }

#ifndef NOPASSWDAUTH
  /* Let 'em in anyway - they must have been in the passwd file */
  ipcp_Setup(&bundle->ncp.ipcp, INADDR_NONE);
  return 1;
#else
#ifndef NORADIUS
  if (bundle->radius.valid)
    return 1;
#endif

  /* Disappeared from ppp.secret ??? */
  return 0;
#endif
}

int
auth_Validate(struct bundle *bundle, const char *name, const char *key)
{
  /* Used by PAP routines */

  FILE *fp;
  int n, lineno;
  char *vector[5], buff[LINE_LEN];
  const char *slash;

  fp = OpenSecret(SECRETFILE);
again:
  lineno = 0;
  if (fp != NULL) {
    while (fgets(buff, sizeof buff, fp)) {
      lineno++;
      if (buff[0] == '#')
        continue;
      buff[strlen(buff) - 1] = 0;
      memset(vector, '\0', sizeof vector);
      if ((n = MakeArgs(buff, vector, VECSIZE(vector), PARSE_REDUCE)) < 0)
        log_Printf(LogWARN, "%s: %d: Invalid line\n", SECRETFILE, lineno);
      if (n < 2)
        continue;
      if (strcmp(vector[0], name) == 0) {
        CloseSecret(fp);
        return auth_CheckPasswd(name, vector[1], key);
      }
    }
  }

  if ((slash = strrchr(name, '\\')) != NULL && slash[1]) {
    /* Look for the name without the leading domain */
    name = slash + 1;
    if (fp != NULL) {
      rewind(fp);
      goto again;
    }
  }

  if (fp != NULL)
    CloseSecret(fp);

#ifndef NOPASSWDAUTH
  if (Enabled(bundle, OPT_PASSWDAUTH))
    return auth_CheckPasswd(name, "*", key);
#endif

  return 0;			/* Invalid */
}

char *
auth_GetSecret(const char *name, size_t len)
{
  /* Used by CHAP routines */

  FILE *fp;
  int n, lineno;
  char *vector[5];
  const char *slash;
  static char buff[LINE_LEN];	/* vector[] will point here when returned */

  fp = OpenSecret(SECRETFILE);
  if (fp == NULL)
    return (NULL);

again:
  lineno = 0;
  while (fgets(buff, sizeof buff, fp)) {
    lineno++;
    if (buff[0] == '#')
      continue;
    n = strlen(buff) - 1;
    if (buff[n] == '\n')
      buff[n] = '\0';	/* Trim the '\n' */
    memset(vector, '\0', sizeof vector);
    if ((n = MakeArgs(buff, vector, VECSIZE(vector), PARSE_REDUCE)) < 0)
      log_Printf(LogWARN, "%s: %d: Invalid line\n", SECRETFILE, lineno);
    if (n < 2)
      continue;
    if (strlen(vector[0]) == len && strncmp(vector[0], name, len) == 0) {
      CloseSecret(fp);
      return vector[1];
    }
  }

  if ((slash = strrchr(name, '\\')) != NULL && slash[1]) {
    /* Go back and look for the name without the leading domain */
    len -= slash - name + 1;
    name = slash + 1;
    rewind(fp);
    goto again;
  }

  CloseSecret(fp);
  return (NULL);		/* Invalid */
}

static void
AuthTimeout(void *vauthp)
{
  struct authinfo *authp = (struct authinfo *)vauthp;

  timer_Stop(&authp->authtimer);
  if (--authp->retry > 0) {
    authp->id++;
    (*authp->fn.req)(authp);
    timer_Start(&authp->authtimer);
  } else {
    log_Printf(LogPHASE, "Auth: No response from server\n");
    datalink_AuthNotOk(authp->physical->dl);
  }
}

void
auth_Init(struct authinfo *authp, struct physical *p, auth_func req,
          auth_func success, auth_func failure)
{
  memset(authp, '\0', sizeof(struct authinfo));
  authp->cfg.fsm.timeout = DEF_FSMRETRY;
  authp->cfg.fsm.maxreq = DEF_FSMAUTHTRIES;
  authp->cfg.fsm.maxtrm = 0;	/* not used */
  authp->fn.req = req;
  authp->fn.success = success;
  authp->fn.failure = failure;
  authp->physical = p;
}

void
auth_StartReq(struct authinfo *authp)
{
  timer_Stop(&authp->authtimer);
  authp->authtimer.func = AuthTimeout;
  authp->authtimer.name = "auth";
  authp->authtimer.load = authp->cfg.fsm.timeout * SECTICKS;
  authp->authtimer.arg = (void *)authp;
  authp->retry = authp->cfg.fsm.maxreq;
  authp->id = 1;
  (*authp->fn.req)(authp);
  timer_Start(&authp->authtimer);
}

void
auth_StopTimer(struct authinfo *authp)
{
  timer_Stop(&authp->authtimer);
}

struct mbuf *
auth_ReadHeader(struct authinfo *authp, struct mbuf *bp)
{
  size_t len;

  len = m_length(bp);
  if (len >= sizeof authp->in.hdr) {
    bp = mbuf_Read(bp, (u_char *)&authp->in.hdr, sizeof authp->in.hdr);
    if (len >= ntohs(authp->in.hdr.length))
      return bp;
    authp->in.hdr.length = htons(0);
    log_Printf(LogWARN, "auth_ReadHeader: Short packet (%u > %zu) !\n",
               ntohs(authp->in.hdr.length), len);
  } else {
    authp->in.hdr.length = htons(0);
    log_Printf(LogWARN, "auth_ReadHeader: Short packet header (%u > %zu) !\n",
               (int)(sizeof authp->in.hdr), len);
  }

  m_freem(bp);
  return NULL;
}

struct mbuf *
auth_ReadName(struct authinfo *authp, struct mbuf *bp, size_t len)
{
  if (len > sizeof authp->in.name - 1)
    log_Printf(LogWARN, "auth_ReadName: Name too long (%zu) !\n", len);
  else {
    size_t mlen = m_length(bp);

    if (len > mlen)
      log_Printf(LogWARN, "auth_ReadName: Short packet (%zu > %zu) !\n",
                 len, mlen);
    else {
      bp = mbuf_Read(bp, (u_char *)authp->in.name, len);
      authp->in.name[len] = '\0';
      return bp;
    }
  }

  *authp->in.name = '\0';
  m_freem(bp);
  return NULL;
}
