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
 * $FreeBSD$
 */
/*
 | $Id: iscontrol.h,v 2.3 2007/04/27 08:36:49 danny Exp danny $
 */
#ifdef DEBUG
int vflag;

# define debug(level, fmt, args...)	do {if (level <= vflag) printf("%s: " fmt "\n", __func__ , ##args);} while(0)
# define debug_called(level)		do {if (level <= vflag) printf("%s: called\n", __func__);} while(0)
#else
# define debug(level, fmt, args...)
# define debug_called(level)
#endif // DEBUG
#define xdebug(fmt, args...)	printf("%s: " fmt "\n", __func__ , ##args)

#define BIT(n)	(1 <<(n))

#define MAXREDIRECTS	2

typedef int auth_t(void *sess);

typedef struct {
     char      *address;
     int       port;
     int       pgt;
} target_t;

typedef struct isess {
     int	flags;
#define SESS_CONNECTED		BIT(0)
#define SESS_DISCONNECT		BIT(1)
#define SESS_LOGGEDIN		BIT(2)
#define SESS_RECONNECT		BIT(3)
#define SESS_REDIRECT		BIT(4)

#define SESS_NEGODONE		BIT(10)	// XXX: kludge

#define SESS_FULLFEATURE	BIT(29)
#define SESS_INITIALLOGIN1	BIT(30)
#define SESS_INITIALLOGIN	BIT(31)


     isc_opt_t	*op;		// operational values
     target_t  target;         // the Original target address
     int	fd;		// the session fd
     int	soc;		// the socket
     iscsi_cam_t	cam;
     struct cam_device	*camdev;

     time_t	open_time;
     int	redirect_cnt;
     time_t	redirect_time;
     int	reconnect_cnt;
     int	reconnect_cnt1;
     time_t	reconnect_time;
     char	isid[6+1];
     int	csg;		// current stage
     int	nsg;		// next stage
     // Phases/Stages	
#define	SN_PHASE	0	// Security Negotiation
#define LON_PHASE	1	// Login Operational Negotiation
#define FF_PHASE	3	// FuLL-Feature
     uint	tsih;
     sn_t	sn;
} isess_t;

typedef struct token {
     char	*name;
     int	val;
} token_t;

typedef enum {
     NONE	= 0,
     KRB5,
     SPKM1,
     SPKM2,
     SRP,
     CHAP
} authm_t;

extern token_t AuthMethods[];
extern token_t DigestMethods[];

typedef enum {
     SET,
     GET
} oper_t;

typedef enum {
     U_PR,	// private
     U_IO,	// Initialize Only -- during login
     U_LO,	// Leading Only -- when TSIH is zero
     U_FFPO,	// Full Feature Phase Only
     U_ALL	// in any phase
} usage_t;

typedef enum {
     S_PR,
     S_CO,	// Connect only
     S_SW	// Session Wide
} scope_t;

typedef void keyfun_t(isess_t *, oper_t);

typedef struct {
     usage_t	usage;
     scope_t	scope;
     char	*name;
     int	tokenID;
} textkey_t;

typedef int handler_t(isess_t *sess, pdu_t *pp);

int	authenticateLogin(isess_t *sess);
int	fsm(isc_opt_t *op);
int	sendPDU(isess_t *sess, pdu_t *pp, handler_t *hdlr);
int	addText(pdu_t *pp, char *fmt, ...);
void	freePDU(pdu_t *pp);
int	xmitpdu(isess_t *sess, pdu_t *pp);
int	recvpdu(isess_t *sess, pdu_t *pp);

int	lookup(token_t *tbl, char *m);

int	vflag;
char	*iscsidev;

void	parseArgs(int nargs, char **args, isc_opt_t *op);
void	parseConfig(FILE *fd, char *key, isc_opt_t *op);

char	*chapDigest(char *ap, char id, char *cp, char *chapSecret);
char	*genChapChallenge(char *encoding, uint len);

int	str2bin(char *str, char **rsp);
char	*bin2str(char *fmt, unsigned char *md, int blen);

int	negotiateOPV(isess_t *sess);
int	setOptions(isess_t *sess, int flag);

int	loginPhase(isess_t *sess);
