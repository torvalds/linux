/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2009 Daniel Braniss <danny@cs.huji.ac.il>
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
 */
/*
 | $Id: config.c,v 2.1 2006/11/12 08:06:51 danny Exp danny $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <camlib.h>

#include <dev/iscsi_initiator/iscsi.h>
#include "iscontrol.h"

/*
 | ints
 */
#define OPT_port			1
#define OPT_tags			2

#define OPT_maxConnections		3
#define OPT_maxRecvDataSegmentLength	4
#define OPT_maxXmitDataSegmentLength	5
#define OPT_maxBurstLength		6
#define OPT_firstBurstLength		7
#define OPT_defaultTime2Wait		8
#define OPT_defaultTime2Retain		9
#define OPT_maxOutstandingR2T		10
#define OPT_errorRecoveryLevel		11
#define OPT_targetPortalGroupTag	12
#define OPT_headerDigest		13
#define OPT_dataDigest			14
/*
 | Booleans
 */
#define OPT_initialR2T			16
#define OPT_immediateData		17
#define OPT_dataPDUInOrder		18
#define OPT_dataSequenceInOrder		19
/*
 | strings
 */
#define OPT_sessionType			15

#define OPT_targetAddress		21
#define OPT_targetAlias			22
#define OPT_targetName			23
#define OPT_initiatorName		24
#define OPT_initiatorAlias		25
#define OPT_authMethod			26

#define OPT_chapSecret			27
#define OPT_chapIName			28
#define OPT_chapDigest			29
#define OPT_tgtChapName			30
#define OPT_tgtChapSecret		31
#define OPT_tgtChallengeLen		32
/*
 | private
 */
#define OPT_maxluns			33
#define OPT_iqn				34
#define OPT_sockbufsize			35

/*
 | sentinel
 */
#define OPT_end				0

#define _OFF(v)	((int)&((isc_opt_t *)NULL)->v)
#define _E(u, s, v) {.usage=u, .scope=s, .name=#v, .tokenID=OPT_##v}

textkey_t keyMap[] = {
     _E(U_PR, S_PR, port),
     _E(U_PR, S_PR, tags),
     _E(U_PR, S_PR, maxluns),
     _E(U_PR, S_PR, sockbufsize),

     _E(U_PR, S_PR, iqn),
     _E(U_PR, S_PR, chapSecret),
     _E(U_PR, S_PR, chapIName),
     _E(U_PR, S_PR, chapDigest),
     _E(U_PR, S_PR, tgtChapName),
     _E(U_PR, S_PR, tgtChapSecret),
     _E(U_PR, S_PR, tgtChallengeLen),

     _E(U_IO, S_CO, headerDigest),
     _E(U_IO, S_CO, dataDigest),

     _E(U_IO, S_CO, authMethod),

     _E(U_LO, S_SW, maxConnections),
     _E(U_IO, S_SW, targetName),

     _E(U_IO, S_SW, initiatorName),
     _E(U_ALL,S_SW, targetAlias),
     _E(U_ALL,S_SW, initiatorAlias),
     _E(U_ALL,S_SW, targetAddress),

     _E(U_ALL,S_SW, targetPortalGroupTag),

     _E(U_LO, S_SW, initialR2T),
     _E(U_LO, S_SW, immediateData),

     _E(U_ALL,S_CO, maxRecvDataSegmentLength),
     _E(U_ALL,S_CO, maxXmitDataSegmentLength),

     _E(U_LO, S_SW, maxBurstLength),
     _E(U_LO, S_SW, firstBurstLength),
     _E(U_LO, S_SW, defaultTime2Wait),
     _E(U_LO, S_SW, defaultTime2Retain),

     _E(U_LO, S_SW, maxOutstandingR2T),
     _E(U_LO, S_SW, dataPDUInOrder),
     _E(U_LO, S_SW, dataSequenceInOrder),
     
     _E(U_LO, S_SW, errorRecoveryLevel),
     
     _E(U_LO, S_SW, sessionType),

     _E(0, 0, end)
};

#define _OPT_INT(w)	strtol((char *)w, NULL, 0)
#define _OPT_STR(w)	(char *)(w)

static __inline  int
_OPT_BOOL(char *w)
{
     if(isalpha((unsigned char)*w))
	  return strcasecmp(w, "TRUE") == 0;
     else
	  return _OPT_INT(w);
}

#define _CASE(k, v)	case OPT_##k: op->k = v; break
static void
setOption(isc_opt_t *op, int which, void *rval)
{
     switch(which) {
	  _CASE(port, _OPT_INT(rval));
	  _CASE(tags, _OPT_INT(rval));
	  _CASE(maxluns, _OPT_INT(rval));
	  _CASE(iqn, _OPT_STR(rval));
	  _CASE(sockbufsize, _OPT_INT(rval));

	  _CASE(maxConnections, _OPT_INT(rval));
	  _CASE(maxRecvDataSegmentLength, _OPT_INT(rval));
	  _CASE(maxXmitDataSegmentLength, _OPT_INT(rval));
	  _CASE(maxBurstLength, _OPT_INT(rval));
	  _CASE(firstBurstLength, _OPT_INT(rval));
	  _CASE(defaultTime2Wait, _OPT_INT(rval));
	  _CASE(defaultTime2Retain, _OPT_INT(rval));
	  _CASE(maxOutstandingR2T, _OPT_INT(rval));
	  _CASE(errorRecoveryLevel, _OPT_INT(rval));
	  _CASE(targetPortalGroupTag, _OPT_INT(rval));
	  _CASE(headerDigest, _OPT_STR(rval));
	  _CASE(dataDigest, _OPT_STR(rval));

	  _CASE(targetAddress, _OPT_STR(rval));
	  _CASE(targetAlias, _OPT_STR(rval));
	  _CASE(targetName, _OPT_STR(rval));
	  _CASE(initiatorName, _OPT_STR(rval));
	  _CASE(initiatorAlias, _OPT_STR(rval));
	  _CASE(authMethod, _OPT_STR(rval));
	  _CASE(chapSecret, _OPT_STR(rval));
	  _CASE(chapIName, _OPT_STR(rval));
	  _CASE(chapDigest, _OPT_STR(rval));

	  _CASE(tgtChapName, _OPT_STR(rval));
	  _CASE(tgtChapSecret, _OPT_STR(rval));

	  _CASE(initialR2T, _OPT_BOOL(rval));
	  _CASE(immediateData, _OPT_BOOL(rval));
	  _CASE(dataPDUInOrder, _OPT_BOOL(rval));
	  _CASE(dataSequenceInOrder, _OPT_BOOL(rval));
     }
}

static char *
get_line(FILE *fd)
{
     static char	*sp, line[BUFSIZ];
     char		*lp, *p;

     do {
	  if(sp == NULL)
	       sp = fgets(line, sizeof line, fd);

	  if((lp = sp) == NULL)
	       break;
	  if((p = strchr(lp, '\n')) != NULL)
	       *p = 0;
	  if((p = strchr(lp, '#')) != NULL)
	       *p = 0;
	  if((p = strchr(lp, ';')) != NULL) {
	       *p++ = 0;
	       sp = p;
	  } else
	       sp = NULL;
	  if(*lp)
	       return lp;
     } while (feof(fd) == 0);
     return NULL;
}

static int
getConfig(FILE *fd, char *key, char **Ar, int *nargs)
{
     char	*lp, *p, **ar;
     int	state, len, n;

     ar = Ar;
     if(key)
	  len = strlen(key);
     else
	  len = 0;
     state = 0;
     while((lp = get_line(fd)) != NULL) {
	  for(; isspace((unsigned char)*lp); lp++)
	       ;
	  switch(state) {
	  case 0:
	       if((p = strchr(lp, '{')) != NULL) {
		    while((--p > lp) && *p && isspace((unsigned char)*p));
		    n = p - lp;
		    if(len && strncmp(lp, key, MAX(n, len)) == 0)
			 state = 2;
		    else
			 state = 1;
		    continue;
	       }
	       break;

	  case 1:
	       if(*lp == '}')
		    state = 0;
	       continue;

	  case 2:
	       if(*lp == '}')
		    goto done;
	       
	       break;
	  }

	  
	  for(p = &lp[strlen(lp)-1]; isspace((unsigned char)*p); p--)
	       *p = 0;
	  if((*nargs)-- > 0)
	       *ar++ = strdup(lp);
     }

 done:
     if(*nargs > 0)
	  *ar = 0;
     *nargs =  ar - Ar;
     return ar - Ar;
}

static textkey_t *
keyLookup(char *key)
{
     textkey_t	*tk;

     for(tk = keyMap; tk->name && strcmp(tk->name, "end"); tk++) {
	  if(strcasecmp(key, tk->name) == 0)
	       return tk;
     }
     return NULL;
}

static void
puke(isc_opt_t *op)
{
     printf("%24s = %d\n", "port", op->port);
     printf("%24s = %d\n", "tags", op->tags);
     printf("%24s = %d\n", "maxluns", op->maxluns);
     printf("%24s = %s\n", "iqn", op->iqn);

     printf("%24s = %d\n", "maxConnections", op->maxConnections);
     printf("%24s = %d\n", "maxRecvDataSegmentLength", op->maxRecvDataSegmentLength);
     printf("%24s = %d\n", "maxXmitDataSegmentLength", op->maxRecvDataSegmentLength);
     printf("%24s = %d\n", "maxBurstLength", op->maxBurstLength);
     printf("%24s = %d\n", "firstBurstLength", op->firstBurstLength);
     printf("%24s = %d\n", "defaultTime2Wait", op->defaultTime2Wait);
     printf("%24s = %d\n", "defaultTime2Retain", op->defaultTime2Retain);
     printf("%24s = %d\n", "maxOutstandingR2T", op->maxOutstandingR2T);
     printf("%24s = %d\n", "errorRecoveryLevel", op->errorRecoveryLevel);
     printf("%24s = %d\n", "targetPortalGroupTag", op->targetPortalGroupTag);

     printf("%24s = %s\n", "headerDigest", op->headerDigest);
     printf("%24s = %s\n", "dataDigest", op->dataDigest);

     printf("%24s = %d\n", "initialR2T", op->initialR2T);
     printf("%24s = %d\n", "immediateData", op->immediateData);
     printf("%24s = %d\n", "dataPDUInOrder", op->dataPDUInOrder);
     printf("%24s = %d\n", "dataSequenceInOrder", op->dataSequenceInOrder);

     printf("%24s = %s\n", "sessionType", op->sessionType);
     printf("%24s = %s\n", "targetAddress", op->targetAddress);
     printf("%24s = %s\n", "targetAlias", op->targetAlias);
     printf("%24s = %s\n", "targetName", op->targetName);
     printf("%24s = %s\n", "initiatorName", op->initiatorName);
     printf("%24s = %s\n", "initiatorAlias", op->initiatorAlias);
     printf("%24s = %s\n", "authMethod", op->authMethod);
     printf("%24s = %s\n", "chapSecret", op->chapSecret);
     printf("%24s = %s\n", "chapIName", op->chapIName);
     printf("%24s = %s\n", "tgtChapName", op->tgtChapName);
     printf("%24s = %s\n", "tgtChapSecret", op->tgtChapSecret);
     printf("%24s = %d\n", "tgttgtChallengeLen", op->tgtChallengeLen);
}

void
parseArgs(int nargs, char **args, isc_opt_t *op)
{
     char	**ar;
     char	*p, *v;
     textkey_t	*tk;

     for(ar = args; nargs > 0; nargs--, ar++) {
	  p = strchr(*ar, '=');
	  if(p == NULL)
	       continue;
	  *p = 0;
	  v = p + 1;
	  while(isspace((unsigned char)*--p))
	       *p = 0;
	  while(isspace((unsigned char)*v))
	       v++;
	  if((tk = keyLookup(*ar)) == NULL)
	       continue;
	  setOption(op, tk->tokenID, v);
     }
}

void
parseConfig(FILE *fd, char *key, isc_opt_t *op)
{
     char	*Ar[256];
     int	cc;

     cc = 256;
     if(getConfig(fd, key, Ar, &cc))
	  parseArgs(cc, Ar, op);
     if(vflag)
	  puke(op);
}
