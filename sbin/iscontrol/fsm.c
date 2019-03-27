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
 | $Id: fsm.c,v 2.8 2007/05/19 16:34:21 danny Exp danny $
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
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <syslog.h>
#include <stdarg.h>
#include <camlib.h>

#include <dev/iscsi_initiator/iscsi.h>
#include "iscontrol.h"

typedef enum {
     T1 = 1,
     T2, /*T3,*/ T4, T5, /*T6,*/ T7, T8, T9,
     T10, T11, T12, T13, T14, T15, T16, T18
} trans_t;

/*
 | now supports IPV6
 | thanks to:
 |	Hajimu UMEMOTO @ Internet Mutual Aid Society Yokohama, Japan
 |	ume@mahoroba.org  ume@{,jp.}FreeBSD.org
 |	http://www.imasy.org/~ume/
 */
static trans_t
tcpConnect(isess_t *sess)
{
     isc_opt_t *op = sess->op;
     int	val, sv_errno, soc;
     struct     addrinfo *res, *res0, hints;
     char	pbuf[10];

     debug_called(3);
     if(sess->flags & (SESS_RECONNECT|SESS_REDIRECT)) {
	  syslog(LOG_INFO, "%s", (sess->flags & SESS_RECONNECT)
		 ? "Reconnect": "Redirected");
	  
	  debug(1, "%s", (sess->flags & SESS_RECONNECT) ? "Reconnect": "Redirected");
	  shutdown(sess->soc, SHUT_RDWR);
	  //close(sess->soc);
	  sess->soc = -1;

	  sess->flags &= ~SESS_CONNECTED;
	  if(sess->flags & SESS_REDIRECT) {
	       sess->redirect_cnt++;
	       sess->flags |= SESS_RECONNECT;
	  } else
	       sleep(2); // XXX: actually should be ?
#ifdef notyet
	  {
	       time_t	sec;
	       // make sure we are not in a loop
	       // XXX: this code has to be tested
	       sec = time(0) - sess->reconnect_time;
	       if(sec > (5*60)) {
		    // if we've been connected for more that 5 minutes
		    // then just reconnect
		    sess->reconnect_time = sec;
		    sess->reconnect_cnt1 = 0;
	       }
	       else {
		    //
		    sess->reconnect_cnt1++;
		    if((sec / sess->reconnect_cnt1) < 2) {
			 // if less that 2 seconds from the last reconnect
			 // we are most probably looping
			 syslog(LOG_CRIT, "too many reconnects %d", sess->reconnect_cnt1);
			 return 0;
		    }
	       }
	  }
#endif
	  sess->reconnect_cnt++;
     }

     snprintf(pbuf, sizeof(pbuf), "%d", op->port);
     memset(&hints, 0, sizeof(hints));
     hints.ai_family	= PF_UNSPEC;
     hints.ai_socktype	= SOCK_STREAM;
     debug(1, "targetAddress=%s port=%d", op->targetAddress, op->port);
     if((val = getaddrinfo(op->targetAddress, pbuf, &hints, &res0)) != 0) {
          fprintf(stderr, "getaddrinfo(%s): %s\n", op->targetAddress, gai_strerror(val));
          return 0;
     }
     sess->flags &= ~SESS_CONNECTED;
     sv_errno = 0;
     soc = -1;
     for(res = res0; res; res = res->ai_next) {
	  soc = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	  if (soc == -1)
	       continue;

	  // from Patrick.Guelat@imp.ch:
	  // iscontrol can be called without waiting for the socket entry to time out
	  val = 1;
	  if(setsockopt(soc, SOL_SOCKET, SO_REUSEADDR, &val, (socklen_t)sizeof(val)) < 0) {
	       fprintf(stderr, "Cannot set socket SO_REUSEADDR %d: %s\n\n",
		       errno, strerror(errno));
	  }

	  if(connect(soc, res->ai_addr, res->ai_addrlen) == 0)
	       break;
	  sv_errno = errno;
	  close(soc);
	  soc = -1;
     }
     freeaddrinfo(res0);
     if(soc != -1) {
	  sess->soc = soc;

#if 0
	  struct	timeval timeout;

	  val = 1;
	  if(setsockopt(sess->soc, IPPROTO_TCP, TCP_KEEPALIVE, &val, sizeof(val)) < 0)
	       fprintf(stderr, "Cannot set socket KEEPALIVE option err=%d %s\n",
		       errno, strerror(errno));

	  if(setsockopt(sess->soc, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) < 0)
	       fprintf(stderr, "Cannot set socket NO delay option err=%d %s\n",
		       errno, strerror(errno));
	  
	  timeout.tv_sec = 10;
	  timeout.tv_usec = 0;
	  if((setsockopt(sess->soc, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
	     || (setsockopt(sess->soc, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)) {
	       fprintf(stderr, "Cannot set socket timeout to %ld err=%d %s\n",
		       timeout.tv_sec, errno, strerror(errno));
	  }
#endif
#ifdef CURIOUS
	  { 
	       int len = sizeof(val);
	       if(getsockopt(sess->soc, SOL_SOCKET, SO_SNDBUF, &val, &len) == 0)
		    fprintf(stderr, "was: SO_SNDBUF=%dK\n", val/1024);
	  }
#endif
	  if(sess->op->sockbufsize) {
	       val = sess->op->sockbufsize * 1024;
	       if((setsockopt(sess->soc, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val)) < 0)
		  || (setsockopt(sess->soc, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val)) < 0)) {
		    fprintf(stderr, "Cannot set socket sndbuf & rcvbuf to %d err=%d %s\n",
			    val, errno, strerror(errno));
		    return 0; 
	       }
	  }
	  sess->flags |= SESS_CONNECTED;
	  return T1;
     }

     fprintf(stderr, "errno=%d\n", sv_errno);
     perror("connect");
     switch(sv_errno) {
     case ECONNREFUSED:
     case EHOSTUNREACH:
     case ENETUNREACH:
     case ETIMEDOUT:
	  if((sess->flags & SESS_REDIRECT) == 0) {
	       if(strcmp(op->targetAddress, sess->target.address) != 0) {
		    syslog(LOG_INFO, "reconnecting to original target address");
		    free(op->targetAddress);
		    op->targetAddress           = sess->target.address;
		    op->port                    = sess->target.port;
		    op->targetPortalGroupTag    = sess->target.pgt;
		    return T1;
	       }
	  }
	  sleep(5); // for now ...
	  return T1;
     default:
	  return 0; // terminal error
     }
}

int
setOptions(isess_t *sess, int flag)
{
     isc_opt_t	oop;
     char	*sep;

     debug_called(3);

     bzero(&oop, sizeof(isc_opt_t));

     if((flag & SESS_FULLFEATURE) == 0) {
	  oop.initiatorName	= sess->op->initiatorName;
	  oop.targetAddress	= sess->op->targetAddress;
	  if(sess->op->targetName != 0)
	       oop.targetName = sess->op->targetName;
	  
	  oop.maxRecvDataSegmentLength = sess->op->maxRecvDataSegmentLength;
	  oop.maxXmitDataSegmentLength = sess->op->maxXmitDataSegmentLength; // XXX:
	  oop.maxBurstLength = sess->op->maxBurstLength;
	  oop.maxluns = sess->op->maxluns;
     }
     else {
	  /*
	   | turn on digestion only after login
	   */
	  if(sess->op->headerDigest != NULL) {
	       sep = strchr(sess->op->headerDigest, ',');
	       if(sep == NULL)
		    oop.headerDigest = sess->op->headerDigest;
	       debug(1, "oop.headerDigest=%s", oop.headerDigest);
	  }
	  if(sess->op->dataDigest != NULL) {
	       sep = strchr(sess->op->dataDigest, ',');
	       if(sep == NULL)
		    oop.dataDigest = sess->op->dataDigest;
	       debug(1, "oop.dataDigest=%s", oop.dataDigest);
	  }
     }

     if(ioctl(sess->fd, ISCSISETOPT, &oop)) {
	  perror("ISCSISETOPT");
	  return -1;
     }
     return 0;
}

static trans_t
startSession(isess_t *sess)
{
     
     int	n, fd, nfd;
     char	*dev;

     debug_called(3);

     if((sess->flags & SESS_CONNECTED) == 0) {
	  return T2;
     }
     if(sess->fd == -1) {
	  fd = open(iscsidev, O_RDWR);
	  if(fd < 0) {
	       perror(iscsidev);
	       return 0;
	  }
	  {
	       // XXX: this has to go
	       size_t	n;
	       n = sizeof(sess->isid);
	       if(sysctlbyname("net.iscsi_initiator.isid", (void *)sess->isid, (size_t *)&n, 0, 0) != 0)
		    perror("sysctlbyname");
	  }
	  if(ioctl(fd, ISCSISETSES, &n)) {
	       perror("ISCSISETSES");
	       return 0;
	  }
	  asprintf(&dev, "%s%d", iscsidev, n);
	  nfd = open(dev, O_RDWR);
	  if(nfd < 0) {
	       perror(dev);
	       free(dev);
	       return 0;
	  }
	  free(dev);
	  close(fd);
	  sess->fd = nfd;

	  if(setOptions(sess, 0) != 0)
	       return -1;
     }

     if(ioctl(sess->fd, ISCSISETSOC, &sess->soc)) {
	  perror("ISCSISETSOC");
	  return 0;
     }

     return T4;
}

isess_t *currsess;

static void
trap(int sig)
{
     syslog(LOG_NOTICE, "trapped signal %d", sig);
     fprintf(stderr, "trapped signal %d\n", sig);

     switch(sig) {
     case SIGHUP:
	  currsess->flags |= SESS_DISCONNECT;
	  break;

     case SIGUSR1:
	  currsess->flags |= SESS_RECONNECT;
	  break;

     case SIGINT: 
     case SIGTERM:
     default:
	  return; // ignore
     }
}

static int
doCAM(isess_t *sess)
{
     char	pathstr[1024];
     union ccb	*ccb;
     int	i, n;

     if(ioctl(sess->fd, ISCSIGETCAM, &sess->cam) != 0) {
	  syslog(LOG_WARNING, "ISCSIGETCAM failed: %d", errno);
	  return 0;
     }
     debug(1, "nluns=%d", sess->cam.target_nluns);
     /*
      | for now will do this for each lun ...
      */
     for(n = i = 0; i < sess->cam.target_nluns; i++) {
	  debug(2, "CAM path_id=%d target_id=%d",
		sess->cam.path_id, sess->cam.target_id);

	  sess->camdev = cam_open_btl(sess->cam.path_id, sess->cam.target_id,
				      i, O_RDWR, NULL);
	  if(sess->camdev == NULL) {
	       //syslog(LOG_WARNING, "%s", cam_errbuf);
	       debug(3, "%s", cam_errbuf);
	       continue;
	  }

	  cam_path_string(sess->camdev, pathstr, sizeof(pathstr));
	  debug(2, "pathstr=%s", pathstr);

	  ccb = cam_getccb(sess->camdev);
	  CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->crs);
	  ccb->ccb_h.func_code = XPT_REL_SIMQ;
	  ccb->crs.release_flags = RELSIM_ADJUST_OPENINGS;
	  ccb->crs.openings = sess->op->tags;
	  if(cam_send_ccb(sess->camdev, ccb) < 0)
	       debug(2, "%s", cam_errbuf);
	  else
	  if((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
	       syslog(LOG_WARNING, "XPT_REL_SIMQ CCB failed");
	       // cam_error_print(sess->camdev, ccb, CAM_ESF_ALL, CAM_EPF_ALL, stderr);
	  }
	  else {
	       n++;
	       syslog(LOG_INFO, "%s tagged openings now %d\n", pathstr, ccb->crs.openings);
	  }
	  cam_freeccb(ccb);
	  cam_close_device(sess->camdev);
     }
     return n;
}

static trans_t
supervise(isess_t *sess)
{
     int	sig, val;

     debug_called(3);

     if(strcmp(sess->op->sessionType, "Discovery") == 0) {
	  sess->flags |= SESS_DISCONNECT;
	  return T9;
     }

     if(vflag)
	  printf("ready to go scsi\n");

     if(setOptions(sess, SESS_FULLFEATURE) != 0)
	  return 0; // failure

     if((sess->flags & SESS_FULLFEATURE) == 0) {
	  if(daemon(0, 1) != 0) {
	       perror("daemon");
	       exit(1);
	  }
	  if(sess->op->pidfile != NULL) {
	       FILE *pidf;

	       pidf = fopen(sess->op->pidfile, "w");
	       if(pidf != NULL) { 
 		    fprintf(pidf, "%d\n", getpid());
		    fclose(pidf);
	       }
	  }
	  openlog("iscontrol", LOG_CONS|LOG_PERROR|LOG_PID|LOG_NDELAY, LOG_KERN);
	  syslog(LOG_INFO, "running");

	  currsess = sess;
	  if(ioctl(sess->fd, ISCSISTART)) {
	       perror("ISCSISTART");
	       return -1;
	  }
	  if(doCAM(sess) == 0) {
	       syslog(LOG_WARNING, "no device found");
	       ioctl(sess->fd, ISCSISTOP);
	       return T15;
	  }

     }
     else {
	  if(ioctl(sess->fd, ISCSIRESTART)) {
	       perror("ISCSIRESTART");
	       return -1;
	  }
     }
	  
     signal(SIGINT, trap);
     signal(SIGHUP, trap);
     signal(SIGTERM, trap);

     sig = SIGUSR1;
     signal(sig, trap);
     if(ioctl(sess->fd, ISCSISIGNAL, &sig)) {
	  perror("ISCSISIGNAL");
	  return -1;
     }
     sess->flags |= SESS_FULLFEATURE;

     sess->flags &= ~(SESS_REDIRECT | SESS_RECONNECT);
     if(vflag)
	  printf("iscontrol: supervise starting main loop\n");
     /*
      | the main loop - actually do nothing
      | all the work is done inside the kernel
      */
     while((sess->flags & (SESS_REDIRECT|SESS_RECONNECT|SESS_DISCONNECT)) == 0) {
	  // do something?
	  // like sending a nop_out?
	  sleep(60);
     }
     printf("iscontrol: supervise going down\n");
     syslog(LOG_INFO, "sess flags=%x", sess->flags);

     sig = 0;
     if(ioctl(sess->fd, ISCSISIGNAL, &sig)) {
	  perror("ISCSISIGNAL");
     }

     if(sess->flags & SESS_DISCONNECT) {
	  sess->flags &= ~SESS_FULLFEATURE;
	  return T9;
     } 
     else {
	  val = 0;
	  if(ioctl(sess->fd, ISCSISTOP, &val)) {
	       perror("ISCSISTOP");
	  }
	  sess->flags |= SESS_INITIALLOGIN1;
     }
     return T8;
}

static int
handledDiscoveryResp(isess_t *sess, pdu_t *pp)
{
     u_char	*ptr;
     int	len, n;

     debug_called(3);

     len = pp->ds_len;
     ptr = pp->ds_addr;
     while(len > 0) {
	  if(*ptr != 0)
	       printf("%s\n", ptr);
	  n = strlen((char *)ptr) + 1;
	  len -= n;
	  ptr += n;
     }
     return 0;
}

static int
doDiscovery(isess_t *sess)
{
     pdu_t	spp;
     text_req_t	*tp = (text_req_t *)&spp.ipdu.bhs;

     debug_called(3);

     bzero(&spp, sizeof(pdu_t));
     tp->cmd = ISCSI_TEXT_CMD /*| 0x40 */; // because of a bug in openiscsi-target
     tp->F = 1;
     tp->ttt = 0xffffffff;
     addText(&spp, "SendTargets=All");
     return sendPDU(sess, &spp, handledDiscoveryResp);
}

static trans_t
doLogin(isess_t *sess)
{
     isc_opt_t	*op = sess->op;
     int	status, count;

     debug_called(3);

     if(op->chapSecret == NULL && op->tgtChapSecret == NULL)
	  /*
	   | don't need any security negotiation
	   | or in other words: we don't have any secrets to exchange
	   */
	  sess->csg = LON_PHASE;
     else
	  sess->csg = SN_PHASE;

     if(sess->tsih) {
	  sess->tsih = 0;	// XXX: no 'reconnect' yet
	  sess->flags &= ~SESS_NEGODONE; // XXX: KLUDGE
     }
     count = 10; // should be more than enough
     do {
	  debug(3, "count=%d csg=%d", count, sess->csg);
	  status = loginPhase(sess);
	  if(count-- == 0)
	       // just in case we get into a loop
	       status = -1;
     } while(status == 0 && (sess->csg != FF_PHASE));

     sess->flags &= ~SESS_INITIALLOGIN;
     debug(3, "status=%d", status);

     switch(status) {
     case 0: // all is ok ...
	  sess->flags |= SESS_LOGGEDIN;
	  if(strcmp(sess->op->sessionType, "Discovery") == 0)
	       doDiscovery(sess);
	  return T5;

     case 1:	// redirect - temporary/permanent
	  /*
	   | start from scratch?
	   */
	  sess->flags &= ~SESS_NEGODONE;
	  sess->flags |= (SESS_REDIRECT | SESS_INITIALLOGIN1);
	  syslog(LOG_DEBUG, "target sent REDIRECT");
	  return T7;

     case 2: // initiator terminal error
	  return 0;
     case 3: // target terminal error -- could retry ...
	  sleep(5);
	  return T7; // lets try
     default:
	  return 0;
     }
}

static int
handleLogoutResp(isess_t *sess, pdu_t *pp)
{
     if(sess->flags & SESS_DISCONNECT) {
	  int val = 0;
	  if(ioctl(sess->fd, ISCSISTOP, &val)) {
	       perror("ISCSISTOP");
	  }
	  return 0;
     }
     return T13;
}

static trans_t
startLogout(isess_t *sess)
{
     pdu_t	spp;
     logout_req_t *p = (logout_req_t *)&spp.ipdu.bhs;

     bzero(&spp, sizeof(pdu_t));
     p->cmd = ISCSI_LOGOUT_CMD| 0x40;
     p->reason = BIT(7) | 0;
     p->CID = htons(1);

     return sendPDU(sess, &spp, handleLogoutResp);
}

static trans_t
inLogout(isess_t *sess)
{
     if(sess->flags & SESS_RECONNECT)
	  return T18;
     return 0;
}

typedef enum {
     S1, S2, /*S3,*/ S4, S5, S6, S7, S8
} state_t;

/**
      S1: FREE
      S2: XPT_WAIT
      S4: IN_LOGIN
      S5: LOGGED_IN
      S6: IN_LOGOUT
      S7: LOGOUT_REQUESTED
      S8: CLEANUP_WAIT

                     -------<-------------+
         +--------->/ S1    \<----+       |
      T13|       +->\       /<-+   \      |
         |      /    ---+---    \   \     |
         |     /        |     T2 \   |    |
         |  T8 |        |T1       |  |    |
         |     |        |        /   |T7  |
         |     |        |       /    |    |
         |     |        |      /     |    |
         |     |        V     /     /     |
         |     |     ------- /     /      |
         |     |    / S2    \     /       |
         |     |    \       /    /        |
         |     |     ---+---    /         |
         |     |        |T4    /          |
         |     |        V     /           | T18
         |     |     ------- /            |
         |     |    / S4    \             |
         |     |    \       /             |
         |     |     ---+---              |         T15
         |     |        |T5      +--------+---------+
         |     |        |       /T16+-----+------+  |
         |     |        |      /   -+-----+--+   |  |
         |     |        |     /   /  S7   \  |T12|  |
         |     |        |    / +->\       /<-+   V  V
         |     |        |   / /    -+-----       -------
         |     |        |  / /T11   |T10        /  S8   \
         |     |        V / /       V  +----+   \       /
         |     |      ---+-+-      ----+--  |    -------
         |     |     / S5    \T9  / S6    \<+    ^
         |     +-----\       /--->\       / T14  |
         |            -------      --+----+------+T17
         +---------------------------+
*/

int
fsm(isc_opt_t *op)
{
     state_t	state;
     isess_t	*sess;

     if((sess = calloc(1, sizeof(isess_t))) == NULL) {
	  // boy, is this a bad start ...
	  fprintf(stderr, "no memory!\n");
	  return -1;
     }

     state = S1;
     sess->op = op;
     sess->fd = -1;
     sess->soc = -1;
     sess->target.address = strdup(op->targetAddress);
     sess->target.port = op->port;
     sess->target.pgt = op->targetPortalGroupTag;

     sess->flags = SESS_INITIALLOGIN | SESS_INITIALLOGIN1;

     do {
	  switch(state) {

	  case S1:
	       switch(tcpConnect(sess)) {
	       case T1: state = S2; break;
	       default: state = S8; break;
	       }
	       break;

	  case S2:
	       switch(startSession(sess)) {
	       case T2: state = S1; break;
	       case T4: state = S4; break;
	       default: state = S8; break;
	       }
	       break;

	  case S4:
	       switch(doLogin(sess)) {
	       case T7:  state = S1; break;
	       case T5:  state = S5; break;
	       default: state = S8; break;
	       }
	       break;

	  case S5:
	       switch(supervise(sess)) {
	       case T8:  state = S1; break;
	       case T9:  state = S6; break;
	       case T11: state = S7; break;
	       case T15: state = S8; break;
	       default: state = S8; break;
	       }
	       break;

	  case S6:
	       switch(startLogout(sess)) {
	       case T13: state = S1; break;
	       case T14: state = S6; break;
	       case T16: state = S8; break;
	       default: state = S8; break;
	       }
	       break;
	  
	  case S7: 
	       switch(inLogout(sess)) {
	       case T18: state = S1; break;
	       case T10: state = S6; break;
	       case T12: state = S7; break;
	       case T16: state = S8; break;
	       default: state = S8; break;
	       }
	       break;

	  case S8:
	       // maybe do some clean up?
	       syslog(LOG_INFO, "terminated");
	       return 0;
	  }
     } while(1);
}
