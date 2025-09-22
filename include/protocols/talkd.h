/*	$OpenBSD: talkd.h,v 1.5 2015/01/21 02:23:14 guenther Exp $	*/
/*	$NetBSD: talkd.h,v 1.5 1995/03/04 07:59:30 cgd Exp $	*/

/*
 * Copyright (c) 1983 Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)talkd.h	5.7 (Berkeley) 4/3/91
 */

#ifndef _TALKD_H_
#define	_TALKD_H_

/*
 * This describes the protocol used by the talk server and clients.
 *
 * The talk server acts a repository of invitations, responding to
 * requests by clients wishing to rendezvous for the purpose of
 * holding a conversation.  In normal operation, a client, the caller,
 * initiates a rendezvous by sending a CTL_MSG to the server of
 * type LOOK_UP.  This causes the server to search its invitation
 * tables to check if an invitation currently exists for the caller
 * (to speak to the callee specified in the message).  If the lookup
 * fails, the caller then sends an ANNOUNCE message causing the server
 * to broadcast an announcement on the callee's login ports requesting
 * contact.  When the callee responds, the local server uses the
 * recorded invitation to respond with the appropriate rendezvous
 * address and the caller and callee client programs establish a
 * stream connection through which the conversation takes place.
 */

struct osockaddr {
	unsigned short	sa_family;	/* address family */
	char		sa_data[14];	/* up to 14 bytes of direct address */
};

/*
 * Client->server request message format.
 */
typedef struct {
	unsigned char	  vers;			/* protocol version */
	unsigned char	  type;			/* request type, see below */
	unsigned char	  answer;		/* not used */
	unsigned char	  pad;
	u_int32_t 	  id_num;		/* message id */
	struct	  osockaddr addr;		/* old (4.3) style */
	struct	  osockaddr ctl_addr;		/* old (4.3) style */
	int32_t	  	  pid;			/* caller's process id */
#define	NAME_SIZE	12
	char	  	  l_name[NAME_SIZE];	/* caller's name */
	char	  	  r_name[NAME_SIZE];	/* callee's name */
#define	TTY_SIZE	16
	char	  	  r_tty[TTY_SIZE];	/* callee's tty name */
} CTL_MSG;

/*
 * Server->client response message format.
 */
typedef struct {
	unsigned char	  vers;		/* protocol version */
	unsigned char	  type;		/* type of request message, see below */
	unsigned char	  answer;	/* response to request message, 
					   see below */
	unsigned char	  pad;
	u_int32_t 	  id_num;	/* message id */
	struct	  osockaddr addr; 	/* address for establishing 
					   conversation */
} CTL_RESPONSE;

#define	TALK_VERSION	1		/* protocol version */

/* message type values */
#define LEAVE_INVITE	0	/* leave invitation with server */
#define LOOK_UP		1	/* check for invitation by callee */
#define DELETE		2	/* delete invitation by caller */
#define ANNOUNCE	3	/* announce invitation by caller */

/* answer values */
#define SUCCESS		0	/* operation completed properly */
#define NOT_HERE	1	/* callee not logged in */
#define FAILED		2	/* operation failed for unexplained reason */
#define MACHINE_UNKNOWN	3	/* caller's machine name unknown */
#define PERMISSION_DENIED 4	/* callee's tty doesn't permit announce */
#define UNKNOWN_REQUEST	5	/* request has invalid type value */
#define	BADVERSION	6	/* request has invalid protocol version */
#define	BADADDR		7	/* request has invalid addr value */
#define	BADCTLADDR	8	/* request has invalid ctl_addr value */

/*
 * Operational parameters.
 */
#define MAX_LIFE	60	/* max time daemon saves invitations */
/* RING_WAIT should be 10's of seconds less than MAX_LIFE */
#define RING_WAIT	30	/* time to wait before resending invitation */

#endif /* !_TALKD_H_ */
