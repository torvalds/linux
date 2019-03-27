/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2010 Daniel Braniss <danny@cs.huji.ac.il>
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
 | $Id: login.c,v 1.4 2007/04/27 07:40:40 danny Exp danny $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dev/iscsi_initiator/iscsi.h>
#include "iscontrol.h"

static char *status_class1[] = {
     "Initiator error",
     "Authentication failure",
     "Authorization failure",
     "Not found",
     "Target removed",
     "Unsupported version",
     "Too many connections",
     "Missing parameter",
     "Can't include in session",
     "Session type not suported",
     "Session does not exist",
     "Invalid during login",
};
#define CLASS1_ERRS ((sizeof status_class1) / sizeof(char *))

static char *status_class3[] = {
     "Target error",
     "Service unavailable",
     "Out of resources"
};
#define CLASS3_ERRS ((sizeof status_class3) / sizeof(char *))

static char *
selectFrom(char *str, token_t *list)
{
     char	*sep, *sp;
     token_t	*lp;
     int	n;

     sp = str;
     do {
	  sep = strchr(sp, ',');
	  if(sep != NULL)
	       n = sep - sp;
	  else
	       n = strlen(sp);
	  
	  for(lp = list; lp->name != NULL; lp++) {
	       if(strncasecmp(lp->name, sp, n) == 0)
		    return strdup(lp->name);
	  }
	  sp = sep + 1;
     } while(sep != NULL);

     return NULL;
}

static char *
getkeyval(char *key, pdu_t *pp)
{
    char	*ptr;
    int	klen, len, n;

    debug_called(3);

    len = pp->ds_len;
    ptr = (char *)pp->ds_addr;
    klen = strlen(key);
    while(len > klen) {
	 if(strncmp(key, ptr, klen) == 0)
	      return ptr+klen;
	 n = strlen(ptr) + 1;
	 len -= n;
	 ptr += n;
    }
    return 0;
}

static int
handleTgtResp(isess_t *sess, pdu_t *pp)
{
     isc_opt_t	*op = sess->op;
     char	*np, *rp, *d1, *d2;
     int	res, l1, l2;
     
     res = -1;
     if(((np = getkeyval("CHAP_N=", pp)) == NULL) ||
	((rp = getkeyval("CHAP_R=", pp)) == NULL))
	  goto out;
     if(strcmp(np, op->tgtChapName? op->tgtChapName: op->initiatorName) != 0) {
	  fprintf(stderr, "%s does not match\n", np);
	  goto out;
     }
     l1 = str2bin(op->tgtChapDigest, &d1);
     l2 = str2bin(rp, &d2);

     debug(3, "l1=%d '%s' l2=%d '%s'", l1, op->tgtChapDigest, l2, rp);
     if(l1 == l2 && memcmp(d1, d2, l1) == 0)
	res = 0;
     if(l1)
	  free(d1);
     if(l2)
	  free(d2);
 out:
     free(op->tgtChapDigest);
     op->tgtChapDigest = NULL;

     debug(3, "res=%d", res);

     return res;
}

static void
processParams(isess_t *sess, pdu_t *pp)
{
     isc_opt_t		*op = sess->op;
     int		len, klen, n;
     char		*eq, *ptr;

     debug_called(3);

     len = pp->ds_len;
     ptr = (char *)pp->ds_addr;
     while(len > 0) {
	  if(vflag > 1)
	       printf("got: len=%d %s\n", len, ptr);
	  klen = 0;
	  if((eq = strchr(ptr, '=')) != NULL)
	       klen = eq - ptr;
	  if(klen > 0) {
	       if(strncmp(ptr, "TargetAddress", klen) == 0) {
		    char	*p, *q, *ta = NULL;

		    // TargetAddress=domainname[:port][,portal-group-tag]
		    // XXX: if(op->targetAddress) free(op->targetAddress);
		    q = op->targetAddress = strdup(eq+1);
		    if(*q == '[') {
			 // bracketed IPv6
			 if((q = strchr(q, ']')) != NULL) {
			      *q++ = '\0';
			      ta = op->targetAddress;
			      op->targetAddress = strdup(ta+1);
			 } else
			      q = op->targetAddress;
		    }
		    if((p = strchr(q, ',')) != NULL) {
			 *p++ = 0;
			 op->targetPortalGroupTag = atoi(p);
		    }
		    if((p = strchr(q, ':')) != NULL) {
			 *p++ = 0;
			 op->port = atoi(p);
		    }
		    if(ta)
			 free(ta);
	       } else if(strncmp(ptr, "MaxRecvDataSegmentLength", klen) == 0) {
		    // danny's RFC
		    op->maxXmitDataSegmentLength = strtol(eq+1, (char **)NULL, 0);
	       } else  if(strncmp(ptr, "TargetPortalGroupTag", klen) == 0) {
		    op->targetPortalGroupTag = strtol(eq+1, (char **)NULL, 0);
	       } else if(strncmp(ptr, "HeaderDigest", klen) == 0) {
		    op->headerDigest = selectFrom(eq+1, DigestMethods);
	       } else if(strncmp(ptr, "DataDigest", klen) == 0) {
		    op->dataDigest = selectFrom(eq+1, DigestMethods);
	       } else if(strncmp(ptr, "MaxOutstandingR2T", klen) == 0)
		    op->maxOutstandingR2T = strtol(eq+1, (char **)NULL, 0);
#if 0
	       else
	       for(kp = keyMap; kp->name; kp++) {
		    if(strncmp(ptr, kp->name, kp->len) == 0 && ptr[kp->len] == '=')
			 mp->func(sess, ptr+kp->len+1, GET);
	       }
#endif
	  }
	  n = strlen(ptr) + 1;
	  len -= n;
	  ptr += n;
     }

}

static int
handleLoginResp(isess_t *sess, pdu_t *pp)
{
     login_rsp_t *lp = (login_rsp_t *)pp;
     uint	st_class, status = ntohs(lp->status);

     debug_called(3);
     debug(4, "Tbit=%d csg=%d nsg=%d status=%x", lp->T, lp->CSG, lp->NSG, status);

     st_class  = status >> 8;
     if(status) {
	  uint	st_detail = status & 0xff;

	  switch(st_class) {
	  case 1: // Redirect
	       switch(st_detail) {
		    // the ITN (iSCSI target Name) requests a: 
	       case 1: // temporary address change
	       case 2: // permanent address change
		    status = 0;
	       }
	       break;

	  case 2: // Initiator Error
	       if(st_detail < CLASS1_ERRS)
		    printf("0x%04x: %s\n", status, status_class1[st_detail]);
	       break;

	  case 3:
	       if(st_detail < CLASS3_ERRS)
		    printf("0x%04x: %s\n", status, status_class3[st_detail]);
	       break;
	  }
     }
	  
     if(status == 0) {
	  processParams(sess, pp);
	  setOptions(sess, 0); // XXX: just in case ...

	  if(lp->T) {
	       isc_opt_t	*op = sess->op;

	       if(sess->csg == SN_PHASE && (op->tgtChapDigest != NULL))
		    if(handleTgtResp(sess, pp) != 0)
			 return 1; // XXX: Authentication failure ...
	       sess->csg = lp->NSG;
	       if(sess->csg == FF_PHASE) {
		    // XXX: will need this when implementing reconnect.
		    sess->tsih = lp->tsih;
		    debug(2, "TSIH=%x", sess->tsih);
	       }
	  }
     }

     return st_class;
}

static int
handleChap(isess_t *sess, pdu_t *pp)
{
     pdu_t		spp;
     login_req_t	*lp;
     isc_opt_t		*op = sess->op;
     char		*ap, *ip, *cp, *digest; // MD5 is 128bits, SHA1 160bits

     debug_called(3);

     bzero(&spp, sizeof(pdu_t));
     lp = (login_req_t *)&spp.ipdu.bhs;
     lp->cmd = ISCSI_LOGIN_CMD | 0x40; // login request + Inmediate
     memcpy(lp->isid, sess->isid, 6);
     lp->tsih = sess->tsih;    // MUST be zero the first time!
     lp->CID = htons(1);
     lp->CSG = SN_PHASE;       // Security Negotiation
     lp->NSG = LON_PHASE;
     lp->T = 1;
    
     if(((ap = getkeyval("CHAP_A=", pp)) == NULL) ||
	((ip = getkeyval("CHAP_I=", pp)) == NULL) ||
	((cp = getkeyval("CHAP_C=", pp)) == NULL))
	  return -1;

     if((digest = chapDigest(ap, (char)strtol(ip, (char **)NULL, 0), cp, op->chapSecret)) == NULL)
	  return -1;

     addText(&spp, "CHAP_N=%s", op->chapIName? op->chapIName: op->initiatorName);
     addText(&spp, "CHAP_R=%s", digest);
     free(digest);

     if(op->tgtChapSecret != NULL) {
	  op->tgtChapID = (random() >> 24) % 255; // should be random enough ...
	  addText(&spp, "CHAP_I=%d", op->tgtChapID);
	  cp = genChapChallenge(cp, op->tgtChallengeLen? op->tgtChallengeLen: 8);
	  addText(&spp, "CHAP_C=%s", cp);
	  op->tgtChapDigest = chapDigest(ap, op->tgtChapID, cp, op->tgtChapSecret);
     }

     return sendPDU(sess, &spp, handleLoginResp);
}

static int
authenticate(isess_t *sess)
{
     pdu_t		spp;
     login_req_t	*lp;
     isc_opt_t	*op = sess->op;

     bzero(&spp, sizeof(pdu_t));
     lp = (login_req_t *)&spp.ipdu.bhs;
     lp->cmd = ISCSI_LOGIN_CMD | 0x40; // login request + Inmediate
     memcpy(lp->isid, sess->isid, 6);
     lp->tsih = sess->tsih;	// MUST be zero the first time!
     lp->CID = htons(1);
     lp->CSG = SN_PHASE;	// Security Negotiation
     lp->NSG = SN_PHASE;
     lp->T = 0;

     switch((authm_t)lookup(AuthMethods, op->authMethod)) {
     case NONE:
	  return 0;

     case KRB5:
     case SPKM1:
     case SPKM2:
     case SRP:
	  return 2;

     case CHAP:
	  if(op->chapDigest == 0)
	       addText(&spp, "CHAP_A=5");
	  else
	  if(strcmp(op->chapDigest, "MD5") == 0)
	       addText(&spp, "CHAP_A=5");
	  else
	  if(strcmp(op->chapDigest, "SHA1") == 0)
	       addText(&spp, "CHAP_A=7");
	  else
	       addText(&spp, "CHAP_A=5,7");
	  return sendPDU(sess, &spp, handleChap);
     }
     return 1;
}

int
loginPhase(isess_t *sess)
{
     pdu_t		spp, *sp = &spp;
     isc_opt_t  	*op = sess->op;
     login_req_t	*lp;
     int		status = 1;

     debug_called(3);

     bzero(sp, sizeof(pdu_t));
     lp = (login_req_t *)&spp.ipdu.bhs;
     lp->cmd = ISCSI_LOGIN_CMD | 0x40; // login request + Inmediate
     memcpy(lp->isid, sess->isid, 6);
     lp->tsih = sess->tsih;	// MUST be zero the first time!
     lp->CID = htons(1);	// sess->cid?

     if((lp->CSG = sess->csg) == LON_PHASE)
	  lp->NSG = FF_PHASE;	// lets try and go full feature ...
     else
	  lp->NSG = LON_PHASE;
     lp->T = 1;			// transit to next login stage

     if(sess->flags & SESS_INITIALLOGIN1) {
	  sess->flags &= ~SESS_INITIALLOGIN1;

	  addText(sp, "SessionType=%s", op->sessionType);
	  addText(sp, "InitiatorName=%s", op->initiatorName);
	  if(strcmp(op->sessionType, "Discovery") != 0) {
	       addText(sp, "TargetName=%s", op->targetName);
	  }
     }
     switch(sess->csg) {
     case SN_PHASE:	// Security Negotiation
	  addText(sp, "AuthMethod=%s", op->authMethod);
	  break;
	       
     case LON_PHASE:	// Login Operational Negotiation
	  if((sess->flags & SESS_NEGODONE) == 0) {
	       sess->flags |= SESS_NEGODONE;
	       addText(sp, "MaxBurstLength=%d", op->maxBurstLength);
	       addText(sp, "HeaderDigest=%s", op->headerDigest);
	       addText(sp, "DataDigest=%s", op->dataDigest);
	       addText(sp, "MaxRecvDataSegmentLength=%d", op->maxRecvDataSegmentLength);
	       addText(sp, "ErrorRecoveryLevel=%d", op->errorRecoveryLevel);
	       addText(sp, "DefaultTime2Wait=%d", op->defaultTime2Wait);
	       addText(sp, "DefaultTime2Retain=%d", op->defaultTime2Retain);
	       addText(sp, "DataPDUInOrder=%s", op->dataPDUInOrder? "Yes": "No");
	       addText(sp, "DataSequenceInOrder=%s", op->dataSequenceInOrder? "Yes": "No");
	       addText(sp, "MaxOutstandingR2T=%d", op->maxOutstandingR2T);

	       if(strcmp(op->sessionType, "Discovery") != 0) {
		    addText(sp, "MaxConnections=%d", op->maxConnections);
		    addText(sp, "FirstBurstLength=%d", op->firstBurstLength);
		    addText(sp, "InitialR2T=%s", op->initialR2T? "Yes": "No");
		    addText(sp, "ImmediateData=%s", op->immediateData? "Yes": "No");
	       }
	  }

	  break;
     }

     status = sendPDU(sess, &spp, handleLoginResp);

     switch(status) {
     case 0: // all is ok ...
	  if(sess->csg == SN_PHASE)
	       /*
		| if we are still here, then we need
		| to exchange some secrets ...
	        */
	       status = authenticate(sess);
     }

     return status;
}
