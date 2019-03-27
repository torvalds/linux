/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1985, 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)ftpcmd.y	8.3 (Berkeley) 4/6/94
 */

/*
 * Grammar for FTP commands.
 * See RFC 959.
 */

%{

#ifndef lint
#if 0
static char sccsid[] = "@(#)ftpcmd.y	8.3 (Berkeley) 4/6/94";
#endif
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/ftp.h>

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <libutil.h>
#include <limits.h>
#include <md5.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "extern.h"
#include "pathnames.h"

off_t	restart_point;

static	int cmd_type;
static	int cmd_form;
static	int cmd_bytesz;
static	int state;
char	cbuf[512];
char	*fromname = NULL;

%}

%union {
	struct {
		off_t	o;
		int	i;
	} u;
	char   *s;
}

%token
	A	B	C	E	F	I
	L	N	P	R	S	T
	ALL

	SP	CRLF	COMMA

	USER	PASS	ACCT	REIN	QUIT	PORT
	PASV	TYPE	STRU	MODE	RETR	STOR
	APPE	MLFL	MAIL	MSND	MSOM	MSAM
	MRSQ	MRCP	ALLO	REST	RNFR	RNTO
	ABOR	DELE	CWD	LIST	NLST	SITE
	STAT	HELP	NOOP	MKD	RMD	PWD
	CDUP	STOU	SMNT	SYST	SIZE	MDTM
	LPRT	LPSV	EPRT	EPSV	FEAT

	UMASK	IDLE	CHMOD	MDFIVE

	LEXERR	NOTIMPL

%token	<s> STRING
%token	<u> NUMBER

%type	<u.i> check_login octal_number byte_size
%type	<u.i> check_login_ro check_login_epsv
%type	<u.i> struct_code mode_code type_code form_code
%type	<s> pathstring pathname password username
%type	<s> ALL NOTIMPL

%start	cmd_list

%%

cmd_list
	: /* empty */
	| cmd_list cmd
		{
			if (fromname)
				free(fromname);
			fromname = NULL;
			restart_point = 0;
		}
	| cmd_list rcmd
	;

cmd
	: USER SP username CRLF
		{
			user($3);
			free($3);
		}
	| PASS SP password CRLF
		{
			pass($3);
			free($3);
		}
	| PASS CRLF
		{
			pass("");
		}
	| PORT check_login SP host_port CRLF
		{
			if (epsvall) {
				reply(501, "No PORT allowed after EPSV ALL.");
				goto port_done;
			}
			if (!$2)
				goto port_done;
			if (port_check("PORT") == 1)
				goto port_done;
#ifdef INET6
			if ((his_addr.su_family != AF_INET6 ||
			     !IN6_IS_ADDR_V4MAPPED(&his_addr.su_sin6.sin6_addr))) {
				/* shoud never happen */
				usedefault = 1;
				reply(500, "Invalid address rejected.");
				goto port_done;
			}
			port_check_v6("pcmd");
#endif
		port_done:
			;
		}
	| LPRT check_login SP host_long_port CRLF
		{
			if (epsvall) {
				reply(501, "No LPRT allowed after EPSV ALL.");
				goto lprt_done;
			}
			if (!$2)
				goto lprt_done;
			if (port_check("LPRT") == 1)
				goto lprt_done;
#ifdef INET6
			if (his_addr.su_family != AF_INET6) {
				usedefault = 1;
				reply(500, "Invalid address rejected.");
				goto lprt_done;
			}
			if (port_check_v6("LPRT") == 1)
				goto lprt_done;
#endif
		lprt_done:
			;
		}
	| EPRT check_login SP STRING CRLF
		{
			char delim;
			char *tmp = NULL;
			char *p, *q;
			char *result[3];
			struct addrinfo hints;
			struct addrinfo *res;
			int i;

			if (epsvall) {
				reply(501, "No EPRT allowed after EPSV ALL.");
				goto eprt_done;
			}
			if (!$2)
				goto eprt_done;

			memset(&data_dest, 0, sizeof(data_dest));
			tmp = strdup($4);
			if (ftpdebug)
				syslog(LOG_DEBUG, "%s", tmp);
			if (!tmp) {
				fatalerror("not enough core");
				/*NOTREACHED*/
			}
			p = tmp;
			delim = p[0];
			p++;
			memset(result, 0, sizeof(result));
			for (i = 0; i < 3; i++) {
				q = strchr(p, delim);
				if (!q || *q != delim) {
		parsefail:
					reply(500,
						"Invalid argument, rejected.");
					if (tmp)
						free(tmp);
					usedefault = 1;
					goto eprt_done;
				}
				*q++ = '\0';
				result[i] = p;
				if (ftpdebug)
					syslog(LOG_DEBUG, "%d: %s", i, p);
				p = q;
			}

			/* some more sanity check */
			p = result[0];
			while (*p) {
				if (!isdigit(*p))
					goto parsefail;
				p++;
			}
			p = result[2];
			while (*p) {
				if (!isdigit(*p))
					goto parsefail;
				p++;
			}

			/* grab address */
			memset(&hints, 0, sizeof(hints));
			if (atoi(result[0]) == 1)
				hints.ai_family = PF_INET;
#ifdef INET6
			else if (atoi(result[0]) == 2)
				hints.ai_family = PF_INET6;
#endif
			else
				hints.ai_family = PF_UNSPEC;	/*XXX*/
			hints.ai_socktype = SOCK_STREAM;
			i = getaddrinfo(result[1], result[2], &hints, &res);
			if (i)
				goto parsefail;
			memcpy(&data_dest, res->ai_addr, res->ai_addrlen);
#ifdef INET6
			if (his_addr.su_family == AF_INET6
			    && data_dest.su_family == AF_INET6) {
				/* XXX more sanity checks! */
				data_dest.su_sin6.sin6_scope_id =
					his_addr.su_sin6.sin6_scope_id;
			}
#endif
			free(tmp);
			tmp = NULL;

			if (port_check("EPRT") == 1)
				goto eprt_done;
#ifdef INET6
			if (his_addr.su_family != AF_INET6) {
				usedefault = 1;
				reply(500, "Invalid address rejected.");
				goto eprt_done;
			}
			if (port_check_v6("EPRT") == 1)
				goto eprt_done;
#endif
		eprt_done:
			free($4);
		}
	| PASV check_login CRLF
		{
			if (epsvall)
				reply(501, "No PASV allowed after EPSV ALL.");
			else if ($2)
				passive();
		}
	| LPSV check_login CRLF
		{
			if (epsvall)
				reply(501, "No LPSV allowed after EPSV ALL.");
			else if ($2)
				long_passive("LPSV", PF_UNSPEC);
		}
	| EPSV check_login_epsv SP NUMBER CRLF
		{
			if ($2) {
				int pf;
				switch ($4.i) {
				case 1:
					pf = PF_INET;
					break;
#ifdef INET6
				case 2:
					pf = PF_INET6;
					break;
#endif
				default:
					pf = -1;	/*junk value*/
					break;
				}
				long_passive("EPSV", pf);
			}
		}
	| EPSV check_login_epsv SP ALL CRLF
		{
			if ($2) {
				reply(200, "EPSV ALL command successful.");
				epsvall++;
			}
		}
	| EPSV check_login_epsv CRLF
		{
			if ($2)
				long_passive("EPSV", PF_UNSPEC);
		}
	| TYPE check_login SP type_code CRLF
		{
			if ($2) {
				switch (cmd_type) {

				case TYPE_A:
					if (cmd_form == FORM_N) {
						reply(200, "Type set to A.");
						type = cmd_type;
						form = cmd_form;
					} else
						reply(504, "Form must be N.");
					break;

				case TYPE_E:
					reply(504, "Type E not implemented.");
					break;

				case TYPE_I:
					reply(200, "Type set to I.");
					type = cmd_type;
					break;

				case TYPE_L:
#if CHAR_BIT == 8
					if (cmd_bytesz == 8) {
						reply(200,
						    "Type set to L (byte size 8).");
						type = cmd_type;
					} else
						reply(504, "Byte size must be 8.");
#else /* CHAR_BIT == 8 */
					UNIMPLEMENTED for CHAR_BIT != 8
#endif /* CHAR_BIT == 8 */
				}
			}
		}
	| STRU check_login SP struct_code CRLF
		{
			if ($2) {
				switch ($4) {

				case STRU_F:
					reply(200, "STRU F accepted.");
					break;

				default:
					reply(504, "Unimplemented STRU type.");
				}
			}
		}
	| MODE check_login SP mode_code CRLF
		{
			if ($2) {
				switch ($4) {

				case MODE_S:
					reply(200, "MODE S accepted.");
					break;
	
				default:
					reply(502, "Unimplemented MODE type.");
				}
			}
		}
	| ALLO check_login SP NUMBER CRLF
		{
			if ($2) {
				reply(202, "ALLO command ignored.");
			}
		}
	| ALLO check_login SP NUMBER SP R SP NUMBER CRLF
		{
			if ($2) {
				reply(202, "ALLO command ignored.");
			}
		}
	| RETR check_login SP pathname CRLF
		{
			if (noretr || (guest && noguestretr))
				reply(500, "RETR command disabled.");
			else if ($2 && $4 != NULL)
				retrieve(NULL, $4);

			if ($4 != NULL)
				free($4);
		}
	| STOR check_login_ro SP pathname CRLF
		{
			if ($2 && $4 != NULL)
				store($4, "w", 0);
			if ($4 != NULL)
				free($4);
		}
	| APPE check_login_ro SP pathname CRLF
		{
			if ($2 && $4 != NULL)
				store($4, "a", 0);
			if ($4 != NULL)
				free($4);
		}
	| NLST check_login CRLF
		{
			if ($2)
				send_file_list(".");
		}
	| NLST check_login SP pathstring CRLF
		{
			if ($2)
				send_file_list($4);
			free($4);
		}
	| LIST check_login CRLF
		{
			if ($2)
				retrieve(_PATH_LS " -lgA", "");
		}
	| LIST check_login SP pathstring CRLF
		{
			if ($2)
				retrieve(_PATH_LS " -lgA %s", $4);
			free($4);
		}
	| STAT check_login SP pathname CRLF
		{
			if ($2 && $4 != NULL)
				statfilecmd($4);
			if ($4 != NULL)
				free($4);
		}
	| STAT check_login CRLF
		{
			if ($2) {
				statcmd();
			}
		}
	| DELE check_login_ro SP pathname CRLF
		{
			if ($2 && $4 != NULL)
				delete($4);
			if ($4 != NULL)
				free($4);
		}
	| RNTO check_login_ro SP pathname CRLF
		{
			if ($2 && $4 != NULL) {
				if (fromname) {
					renamecmd(fromname, $4);
					free(fromname);
					fromname = NULL;
				} else {
					reply(503, "Bad sequence of commands.");
				}
			}
			if ($4 != NULL)
				free($4);
		}
	| ABOR check_login CRLF
		{
			if ($2)
				reply(225, "ABOR command successful.");
		}
	| CWD check_login CRLF
		{
			if ($2) {
				cwd(homedir);
			}
		}
	| CWD check_login SP pathname CRLF
		{
			if ($2 && $4 != NULL)
				cwd($4);
			if ($4 != NULL)
				free($4);
		}
	| HELP CRLF
		{
			help(cmdtab, NULL);
		}
	| HELP SP STRING CRLF
		{
			char *cp = $3;

			if (strncasecmp(cp, "SITE", 4) == 0) {
				cp = $3 + 4;
				if (*cp == ' ')
					cp++;
				if (*cp)
					help(sitetab, cp);
				else
					help(sitetab, NULL);
			} else
				help(cmdtab, $3);
			free($3);
		}
	| NOOP CRLF
		{
			reply(200, "NOOP command successful.");
		}
	| MKD check_login_ro SP pathname CRLF
		{
			if ($2 && $4 != NULL)
				makedir($4);
			if ($4 != NULL)
				free($4);
		}
	| RMD check_login_ro SP pathname CRLF
		{
			if ($2 && $4 != NULL)
				removedir($4);
			if ($4 != NULL)
				free($4);
		}
	| PWD check_login CRLF
		{
			if ($2)
				pwd();
		}
	| CDUP check_login CRLF
		{
			if ($2)
				cwd("..");
		}
	| SITE SP HELP CRLF
		{
			help(sitetab, NULL);
		}
	| SITE SP HELP SP STRING CRLF
		{
			help(sitetab, $5);
			free($5);
		}
	| SITE SP MDFIVE check_login SP pathname CRLF
		{
			char p[64], *q;

			if ($4 && $6) {
				q = MD5File($6, p);
				if (q != NULL)
					reply(200, "MD5(%s) = %s", $6, p);
				else
					perror_reply(550, $6);
			}
			if ($6)
				free($6);
		}
	| SITE SP UMASK check_login CRLF
		{
			int oldmask;

			if ($4) {
				oldmask = umask(0);
				(void) umask(oldmask);
				reply(200, "Current UMASK is %03o.", oldmask);
			}
		}
	| SITE SP UMASK check_login SP octal_number CRLF
		{
			int oldmask;

			if ($4) {
				if (($6 == -1) || ($6 > 0777)) {
					reply(501, "Bad UMASK value.");
				} else {
					oldmask = umask($6);
					reply(200,
					    "UMASK set to %03o (was %03o).",
					    $6, oldmask);
				}
			}
		}
	| SITE SP CHMOD check_login_ro SP octal_number SP pathname CRLF
		{
			if ($4 && ($8 != NULL)) {
				if (($6 == -1 ) || ($6 > 0777))
					reply(501, "Bad mode value.");
				else if (chmod($8, $6) < 0)
					perror_reply(550, $8);
				else
					reply(200, "CHMOD command successful.");
			}
			if ($8 != NULL)
				free($8);
		}
	| SITE SP check_login IDLE CRLF
		{
			if ($3)
				reply(200,
			    	    "Current IDLE time limit is %d seconds; max %d.",
				    timeout, maxtimeout);
		}
	| SITE SP check_login IDLE SP NUMBER CRLF
		{
			if ($3) {
				if ($6.i < 30 || $6.i > maxtimeout) {
					reply(501,
					    "Maximum IDLE time must be between 30 and %d seconds.",
					    maxtimeout);
				} else {
					timeout = $6.i;
					(void) alarm(timeout);
					reply(200,
					    "Maximum IDLE time set to %d seconds.",
					    timeout);
				}
			}
		}
	| STOU check_login_ro SP pathname CRLF
		{
			if ($2 && $4 != NULL)
				store($4, "w", 1);
			if ($4 != NULL)
				free($4);
		}
	| FEAT CRLF
		{
			lreply(211, "Extensions supported:");
#if 0
			/* XXX these two keywords are non-standard */
			printf(" EPRT\r\n");
			if (!noepsv)
				printf(" EPSV\r\n");
#endif
			printf(" MDTM\r\n");
			printf(" REST STREAM\r\n");
			printf(" SIZE\r\n");
			if (assumeutf8) {
				/* TVFS requires UTF8, see RFC 3659 */
				printf(" TVFS\r\n");
				printf(" UTF8\r\n");
			}
			reply(211, "End.");
		}
	| SYST check_login CRLF
		{
			if ($2) {
				if (hostinfo)
#ifdef BSD
					reply(215, "UNIX Type: L%d Version: BSD-%d",
					      CHAR_BIT, BSD);
#else /* BSD */
					reply(215, "UNIX Type: L%d", CHAR_BIT);
#endif /* BSD */
				else
					reply(215, "UNKNOWN Type: L%d", CHAR_BIT);
			}
		}

		/*
		 * SIZE is not in RFC959, but Postel has blessed it and
		 * it will be in the updated RFC.
		 *
		 * Return size of file in a format suitable for
		 * using with RESTART (we just count bytes).
		 */
	| SIZE check_login SP pathname CRLF
		{
			if ($2 && $4 != NULL)
				sizecmd($4);
			if ($4 != NULL)
				free($4);
		}

		/*
		 * MDTM is not in RFC959, but Postel has blessed it and
		 * it will be in the updated RFC.
		 *
		 * Return modification time of file as an ISO 3307
		 * style time. E.g. YYYYMMDDHHMMSS or YYYYMMDDHHMMSS.xxx
		 * where xxx is the fractional second (of any precision,
		 * not necessarily 3 digits)
		 */
	| MDTM check_login SP pathname CRLF
		{
			if ($2 && $4 != NULL) {
				struct stat stbuf;
				if (stat($4, &stbuf) < 0)
					perror_reply(550, $4);
				else if (!S_ISREG(stbuf.st_mode)) {
					reply(550, "%s: not a plain file.", $4);
				} else {
					struct tm *t;
					t = gmtime(&stbuf.st_mtime);
					reply(213,
					    "%04d%02d%02d%02d%02d%02d",
					    1900 + t->tm_year,
					    t->tm_mon+1, t->tm_mday,
					    t->tm_hour, t->tm_min, t->tm_sec);
				}
			}
			if ($4 != NULL)
				free($4);
		}
	| QUIT CRLF
		{
			reply(221, "Goodbye.");
			dologout(0);
		}
	| NOTIMPL
		{
			nack($1);
		}
	| error
		{
			yyclearin;		/* discard lookahead data */
			yyerrok;		/* clear error condition */
			state = CMD;		/* reset lexer state */
		}
	;
rcmd
	: RNFR check_login_ro SP pathname CRLF
		{
			restart_point = 0;
			if ($2 && $4) {
				if (fromname)
					free(fromname);
				fromname = NULL;
				if (renamefrom($4))
					fromname = $4;
				else
					free($4);
			} else if ($4) {
				free($4);
			}
		}
	| REST check_login SP NUMBER CRLF
		{
			if ($2) {
				if (fromname)
					free(fromname);
				fromname = NULL;
				restart_point = $4.o;
				reply(350, "Restarting at %jd. %s",
				    (intmax_t)restart_point,
				    "Send STORE or RETRIEVE to initiate transfer.");
			}
		}
	;

username
	: STRING
	;

password
	: /* empty */
		{
			$$ = (char *)calloc(1, sizeof(char));
		}
	| STRING
	;

byte_size
	: NUMBER
		{
			$$ = $1.i;
		}
	;

host_port
	: NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER COMMA NUMBER
		{
			char *a, *p;

			data_dest.su_len = sizeof(struct sockaddr_in);
			data_dest.su_family = AF_INET;
			p = (char *)&data_dest.su_sin.sin_port;
			p[0] = $9.i; p[1] = $11.i;
			a = (char *)&data_dest.su_sin.sin_addr;
			a[0] = $1.i; a[1] = $3.i; a[2] = $5.i; a[3] = $7.i;
		}
	;

host_long_port
	: NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER
		{
			char *a, *p;

			memset(&data_dest, 0, sizeof(data_dest));
			data_dest.su_len = sizeof(struct sockaddr_in6);
			data_dest.su_family = AF_INET6;
			p = (char *)&data_dest.su_port;
			p[0] = $39.i; p[1] = $41.i;
			a = (char *)&data_dest.su_sin6.sin6_addr;
			a[0] = $5.i; a[1] = $7.i; a[2] = $9.i; a[3] = $11.i;
			a[4] = $13.i; a[5] = $15.i; a[6] = $17.i; a[7] = $19.i;
			a[8] = $21.i; a[9] = $23.i; a[10] = $25.i; a[11] = $27.i;
			a[12] = $29.i; a[13] = $31.i; a[14] = $33.i; a[15] = $35.i;
			if (his_addr.su_family == AF_INET6) {
				/* XXX more sanity checks! */
				data_dest.su_sin6.sin6_scope_id =
					his_addr.su_sin6.sin6_scope_id;
			}
			if ($1.i != 6 || $3.i != 16 || $37.i != 2)
				memset(&data_dest, 0, sizeof(data_dest));
		}
	| NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER
		{
			char *a, *p;

			memset(&data_dest, 0, sizeof(data_dest));
			data_dest.su_sin.sin_len = sizeof(struct sockaddr_in);
			data_dest.su_family = AF_INET;
			p = (char *)&data_dest.su_port;
			p[0] = $15.i; p[1] = $17.i;
			a = (char *)&data_dest.su_sin.sin_addr;
			a[0] =  $5.i; a[1] = $7.i; a[2] = $9.i; a[3] = $11.i;
			if ($1.i != 4 || $3.i != 4 || $13.i != 2)
				memset(&data_dest, 0, sizeof(data_dest));
		}
	;

form_code
	: N
		{
			$$ = FORM_N;
		}
	| T
		{
			$$ = FORM_T;
		}
	| C
		{
			$$ = FORM_C;
		}
	;

type_code
	: A
		{
			cmd_type = TYPE_A;
			cmd_form = FORM_N;
		}
	| A SP form_code
		{
			cmd_type = TYPE_A;
			cmd_form = $3;
		}
	| E
		{
			cmd_type = TYPE_E;
			cmd_form = FORM_N;
		}
	| E SP form_code
		{
			cmd_type = TYPE_E;
			cmd_form = $3;
		}
	| I
		{
			cmd_type = TYPE_I;
		}
	| L
		{
			cmd_type = TYPE_L;
			cmd_bytesz = CHAR_BIT;
		}
	| L SP byte_size
		{
			cmd_type = TYPE_L;
			cmd_bytesz = $3;
		}
		/* this is for a bug in the BBN ftp */
	| L byte_size
		{
			cmd_type = TYPE_L;
			cmd_bytesz = $2;
		}
	;

struct_code
	: F
		{
			$$ = STRU_F;
		}
	| R
		{
			$$ = STRU_R;
		}
	| P
		{
			$$ = STRU_P;
		}
	;

mode_code
	: S
		{
			$$ = MODE_S;
		}
	| B
		{
			$$ = MODE_B;
		}
	| C
		{
			$$ = MODE_C;
		}
	;

pathname
	: pathstring
		{
			if (logged_in && $1) {
				char *p;

				/*
				 * Expand ~user manually since glob(3)
				 * will return the unexpanded pathname
				 * if the corresponding file/directory
				 * doesn't exist yet.  Using sole glob(3)
				 * would break natural commands like
				 * MKD ~user/newdir
				 * or
				 * RNTO ~/newfile
				 */
				if ((p = exptilde($1)) != NULL) {
					$$ = expglob(p);
					free(p);
				} else
					$$ = NULL;
				free($1);
			} else
				$$ = $1;
		}
	;

pathstring
	: STRING
	;

octal_number
	: NUMBER
		{
			int ret, dec, multby, digit;

			/*
			 * Convert a number that was read as decimal number
			 * to what it would be if it had been read as octal.
			 */
			dec = $1.i;
			multby = 1;
			ret = 0;
			while (dec) {
				digit = dec%10;
				if (digit > 7) {
					ret = -1;
					break;
				}
				ret += digit * multby;
				multby *= 8;
				dec /= 10;
			}
			$$ = ret;
		}
	;


check_login
	: /* empty */
		{
		$$ = check_login1();
		}
	;

check_login_epsv
	: /* empty */
		{
		if (noepsv) {
			reply(500, "EPSV command disabled.");
			$$ = 0;
		}
		else
			$$ = check_login1();
		}
	;

check_login_ro
	: /* empty */
		{
		if (readonly) {
			reply(550, "Permission denied.");
			$$ = 0;
		}
		else
			$$ = check_login1();
		}
	;

%%

#define	CMD	0	/* beginning of command */
#define	ARGS	1	/* expect miscellaneous arguments */
#define	STR1	2	/* expect SP followed by STRING */
#define	STR2	3	/* expect STRING */
#define	OSTR	4	/* optional SP then STRING */
#define	ZSTR1	5	/* optional SP then optional STRING */
#define	ZSTR2	6	/* optional STRING after SP */
#define	SITECMD	7	/* SITE command */
#define	NSTR	8	/* Number followed by a string */

#define	MAXGLOBARGS	1000

#define	MAXASIZE	10240	/* Deny ASCII SIZE on files larger than that */

struct tab {
	char	*name;
	short	token;
	short	state;
	short	implemented;	/* 1 if command is implemented */
	char	*help;
};

struct tab cmdtab[] = {		/* In order defined in RFC 765 */
	{ "USER", USER, STR1, 1,	"<sp> username" },
	{ "PASS", PASS, ZSTR1, 1,	"[<sp> [password]]" },
	{ "ACCT", ACCT, STR1, 0,	"(specify account)" },
	{ "SMNT", SMNT, ARGS, 0,	"(structure mount)" },
	{ "REIN", REIN, ARGS, 0,	"(reinitialize server state)" },
	{ "QUIT", QUIT, ARGS, 1,	"(terminate service)", },
	{ "PORT", PORT, ARGS, 1,	"<sp> b0, b1, b2, b3, b4, b5" },
	{ "LPRT", LPRT, ARGS, 1,	"<sp> af, hal, h1, h2, h3,..., pal, p1, p2..." },
	{ "EPRT", EPRT, STR1, 1,	"<sp> |af|addr|port|" },
	{ "PASV", PASV, ARGS, 1,	"(set server in passive mode)" },
	{ "LPSV", LPSV, ARGS, 1,	"(set server in passive mode)" },
	{ "EPSV", EPSV, ARGS, 1,	"[<sp> af|ALL]" },
	{ "TYPE", TYPE, ARGS, 1,	"<sp> { A | E | I | L }" },
	{ "STRU", STRU, ARGS, 1,	"(specify file structure)" },
	{ "MODE", MODE, ARGS, 1,	"(specify transfer mode)" },
	{ "RETR", RETR, STR1, 1,	"<sp> file-name" },
	{ "STOR", STOR, STR1, 1,	"<sp> file-name" },
	{ "APPE", APPE, STR1, 1,	"<sp> file-name" },
	{ "MLFL", MLFL, OSTR, 0,	"(mail file)" },
	{ "MAIL", MAIL, OSTR, 0,	"(mail to user)" },
	{ "MSND", MSND, OSTR, 0,	"(mail send to terminal)" },
	{ "MSOM", MSOM, OSTR, 0,	"(mail send to terminal or mailbox)" },
	{ "MSAM", MSAM, OSTR, 0,	"(mail send to terminal and mailbox)" },
	{ "MRSQ", MRSQ, OSTR, 0,	"(mail recipient scheme question)" },
	{ "MRCP", MRCP, STR1, 0,	"(mail recipient)" },
	{ "ALLO", ALLO, ARGS, 1,	"allocate storage (vacuously)" },
	{ "REST", REST, ARGS, 1,	"<sp> offset (restart command)" },
	{ "RNFR", RNFR, STR1, 1,	"<sp> file-name" },
	{ "RNTO", RNTO, STR1, 1,	"<sp> file-name" },
	{ "ABOR", ABOR, ARGS, 1,	"(abort operation)" },
	{ "DELE", DELE, STR1, 1,	"<sp> file-name" },
	{ "CWD",  CWD,  OSTR, 1,	"[ <sp> directory-name ]" },
	{ "XCWD", CWD,	OSTR, 1,	"[ <sp> directory-name ]" },
	{ "LIST", LIST, OSTR, 1,	"[ <sp> path-name ]" },
	{ "NLST", NLST, OSTR, 1,	"[ <sp> path-name ]" },
	{ "SITE", SITE, SITECMD, 1,	"site-cmd [ <sp> arguments ]" },
	{ "SYST", SYST, ARGS, 1,	"(get type of operating system)" },
	{ "FEAT", FEAT, ARGS, 1,	"(get extended features)" },
	{ "STAT", STAT, OSTR, 1,	"[ <sp> path-name ]" },
	{ "HELP", HELP, OSTR, 1,	"[ <sp> <string> ]" },
	{ "NOOP", NOOP, ARGS, 1,	"" },
	{ "MKD",  MKD,  STR1, 1,	"<sp> path-name" },
	{ "XMKD", MKD,  STR1, 1,	"<sp> path-name" },
	{ "RMD",  RMD,  STR1, 1,	"<sp> path-name" },
	{ "XRMD", RMD,  STR1, 1,	"<sp> path-name" },
	{ "PWD",  PWD,  ARGS, 1,	"(return current directory)" },
	{ "XPWD", PWD,  ARGS, 1,	"(return current directory)" },
	{ "CDUP", CDUP, ARGS, 1,	"(change to parent directory)" },
	{ "XCUP", CDUP, ARGS, 1,	"(change to parent directory)" },
	{ "STOU", STOU, STR1, 1,	"<sp> file-name" },
	{ "SIZE", SIZE, OSTR, 1,	"<sp> path-name" },
	{ "MDTM", MDTM, OSTR, 1,	"<sp> path-name" },
	{ NULL,   0,    0,    0,	0 }
};

struct tab sitetab[] = {
	{ "MD5", MDFIVE, STR1, 1,	"[ <sp> file-name ]" },
	{ "UMASK", UMASK, ARGS, 1,	"[ <sp> umask ]" },
	{ "IDLE", IDLE, ARGS, 1,	"[ <sp> maximum-idle-time ]" },
	{ "CHMOD", CHMOD, NSTR, 1,	"<sp> mode <sp> file-name" },
	{ "HELP", HELP, OSTR, 1,	"[ <sp> <string> ]" },
	{ NULL,   0,    0,    0,	0 }
};

static char	*copy(char *);
static char	*expglob(char *);
static char	*exptilde(char *);
static void	 help(struct tab *, char *);
static struct tab *
		 lookup(struct tab *, char *);
static int	 port_check(const char *);
#ifdef INET6
static int	 port_check_v6(const char *);
#endif
static void	 sizecmd(char *);
static void	 toolong(int);
#ifdef INET6
static void	 v4map_data_dest(void);
#endif
static int	 yylex(void);

static struct tab *
lookup(struct tab *p, char *cmd)
{

	for (; p->name != NULL; p++)
		if (strcmp(cmd, p->name) == 0)
			return (p);
	return (0);
}

#include <arpa/telnet.h>

/*
 * get_line - a hacked up version of fgets to ignore TELNET escape codes.
 */
int
get_line(char *s, int n, FILE *iop)
{
	int c;
	register char *cs;
	sigset_t sset, osset;

	cs = s;
/* tmpline may contain saved command from urgent mode interruption */
	for (c = 0; tmpline[c] != '\0' && --n > 0; ++c) {
		*cs++ = tmpline[c];
		if (tmpline[c] == '\n') {
			*cs++ = '\0';
			if (ftpdebug)
				syslog(LOG_DEBUG, "command: %s", s);
			tmpline[0] = '\0';
			return(0);
		}
		if (c == 0)
			tmpline[0] = '\0';
	}
	/* SIGURG would interrupt stdio if not blocked during the read loop */
	sigemptyset(&sset);
	sigaddset(&sset, SIGURG);
	sigprocmask(SIG_BLOCK, &sset, &osset);
	while ((c = getc(iop)) != EOF) {
		c &= 0377;
		if (c == IAC) {
			if ((c = getc(iop)) == EOF)
				goto got_eof;
			c &= 0377;
			switch (c) {
			case WILL:
			case WONT:
				if ((c = getc(iop)) == EOF)
					goto got_eof;
				printf("%c%c%c", IAC, DONT, 0377&c);
				(void) fflush(stdout);
				continue;
			case DO:
			case DONT:
				if ((c = getc(iop)) == EOF)
					goto got_eof;
				printf("%c%c%c", IAC, WONT, 0377&c);
				(void) fflush(stdout);
				continue;
			case IAC:
				break;
			default:
				continue;	/* ignore command */
			}
		}
		*cs++ = c;
		if (--n <= 0) {
			/*
			 * If command doesn't fit into buffer, discard the
			 * rest of the command and indicate truncation.
			 * This prevents the command to be split up into
			 * multiple commands.
			 */
			while (c != '\n' && (c = getc(iop)) != EOF)
				;
			return (-2);
		}
		if (c == '\n')
			break;
	}
got_eof:
	sigprocmask(SIG_SETMASK, &osset, NULL);
	if (c == EOF && cs == s)
		return (-1);
	*cs++ = '\0';
	if (ftpdebug) {
		if (!guest && strncasecmp("pass ", s, 5) == 0) {
			/* Don't syslog passwords */
			syslog(LOG_DEBUG, "command: %.5s ???", s);
		} else {
			register char *cp;
			register int len;

			/* Don't syslog trailing CR-LF */
			len = strlen(s);
			cp = s + len - 1;
			while (cp >= s && (*cp == '\n' || *cp == '\r')) {
				--cp;
				--len;
			}
			syslog(LOG_DEBUG, "command: %.*s", len, s);
		}
	}
	return (0);
}

static void
toolong(int signo)
{

	reply(421,
	    "Timeout (%d seconds): closing control connection.", timeout);
	if (logging)
		syslog(LOG_INFO, "User %s timed out after %d seconds",
		    (pw ? pw -> pw_name : "unknown"), timeout);
	dologout(1);
}

static int
yylex(void)
{
	static int cpos;
	char *cp, *cp2;
	struct tab *p;
	int n;
	char c;

	for (;;) {
		switch (state) {

		case CMD:
			(void) signal(SIGALRM, toolong);
			(void) alarm(timeout);
			n = get_line(cbuf, sizeof(cbuf)-1, stdin);
			if (n == -1) {
				reply(221, "You could at least say goodbye.");
				dologout(0);
			} else if (n == -2) {
				reply(500, "Command too long.");
				(void) alarm(0);
				continue;
			}
			(void) alarm(0);
#ifdef SETPROCTITLE
			if (strncasecmp(cbuf, "PASS", 4) != 0)
				setproctitle("%s: %s", proctitle, cbuf);
#endif /* SETPROCTITLE */
			if ((cp = strchr(cbuf, '\r'))) {
				*cp++ = '\n';
				*cp = '\0';
			}
			if ((cp = strpbrk(cbuf, " \n")))
				cpos = cp - cbuf;
			if (cpos == 0)
				cpos = 4;
			c = cbuf[cpos];
			cbuf[cpos] = '\0';
			upper(cbuf);
			p = lookup(cmdtab, cbuf);
			cbuf[cpos] = c;
			if (p != 0) {
				yylval.s = p->name;
				if (!p->implemented)
					return (NOTIMPL); /* state remains CMD */
				state = p->state;
				return (p->token);
			}
			break;

		case SITECMD:
			if (cbuf[cpos] == ' ') {
				cpos++;
				return (SP);
			}
			cp = &cbuf[cpos];
			if ((cp2 = strpbrk(cp, " \n")))
				cpos = cp2 - cbuf;
			c = cbuf[cpos];
			cbuf[cpos] = '\0';
			upper(cp);
			p = lookup(sitetab, cp);
			cbuf[cpos] = c;
			if (guest == 0 && p != 0) {
				yylval.s = p->name;
				if (!p->implemented) {
					state = CMD;
					return (NOTIMPL);
				}
				state = p->state;
				return (p->token);
			}
			state = CMD;
			break;

		case ZSTR1:
		case OSTR:
			if (cbuf[cpos] == '\n') {
				state = CMD;
				return (CRLF);
			}
			/* FALLTHROUGH */

		case STR1:
		dostr1:
			if (cbuf[cpos] == ' ') {
				cpos++;
				state = state == OSTR ? STR2 : state+1;
				return (SP);
			}
			break;

		case ZSTR2:
			if (cbuf[cpos] == '\n') {
				state = CMD;
				return (CRLF);
			}
			/* FALLTHROUGH */

		case STR2:
			cp = &cbuf[cpos];
			n = strlen(cp);
			cpos += n - 1;
			/*
			 * Make sure the string is nonempty and \n terminated.
			 */
			if (n > 1 && cbuf[cpos] == '\n') {
				cbuf[cpos] = '\0';
				yylval.s = copy(cp);
				cbuf[cpos] = '\n';
				state = ARGS;
				return (STRING);
			}
			break;

		case NSTR:
			if (cbuf[cpos] == ' ') {
				cpos++;
				return (SP);
			}
			if (isdigit(cbuf[cpos])) {
				cp = &cbuf[cpos];
				while (isdigit(cbuf[++cpos]))
					;
				c = cbuf[cpos];
				cbuf[cpos] = '\0';
				yylval.u.i = atoi(cp);
				cbuf[cpos] = c;
				state = STR1;
				return (NUMBER);
			}
			state = STR1;
			goto dostr1;

		case ARGS:
			if (isdigit(cbuf[cpos])) {
				cp = &cbuf[cpos];
				while (isdigit(cbuf[++cpos]))
					;
				c = cbuf[cpos];
				cbuf[cpos] = '\0';
				yylval.u.i = atoi(cp);
				yylval.u.o = strtoull(cp, NULL, 10);
				cbuf[cpos] = c;
				return (NUMBER);
			}
			if (strncasecmp(&cbuf[cpos], "ALL", 3) == 0
			 && !isalnum(cbuf[cpos + 3])) {
				cpos += 3;
				return ALL;
			}
			switch (cbuf[cpos++]) {

			case '\n':
				state = CMD;
				return (CRLF);

			case ' ':
				return (SP);

			case ',':
				return (COMMA);

			case 'A':
			case 'a':
				return (A);

			case 'B':
			case 'b':
				return (B);

			case 'C':
			case 'c':
				return (C);

			case 'E':
			case 'e':
				return (E);

			case 'F':
			case 'f':
				return (F);

			case 'I':
			case 'i':
				return (I);

			case 'L':
			case 'l':
				return (L);

			case 'N':
			case 'n':
				return (N);

			case 'P':
			case 'p':
				return (P);

			case 'R':
			case 'r':
				return (R);

			case 'S':
			case 's':
				return (S);

			case 'T':
			case 't':
				return (T);

			}
			break;

		default:
			fatalerror("Unknown state in scanner.");
		}
		state = CMD;
		return (LEXERR);
	}
}

void
upper(char *s)
{
	while (*s != '\0') {
		if (islower(*s))
			*s = toupper(*s);
		s++;
	}
}

static char *
copy(char *s)
{
	char *p;

	p = malloc(strlen(s) + 1);
	if (p == NULL)
		fatalerror("Ran out of memory.");
	(void) strcpy(p, s);
	return (p);
}

static void
help(struct tab *ctab, char *s)
{
	struct tab *c;
	int width, NCMDS;
	char *type;

	if (ctab == sitetab)
		type = "SITE ";
	else
		type = "";
	width = 0, NCMDS = 0;
	for (c = ctab; c->name != NULL; c++) {
		int len = strlen(c->name);

		if (len > width)
			width = len;
		NCMDS++;
	}
	width = (width + 8) &~ 7;
	if (s == 0) {
		int i, j, w;
		int columns, lines;

		lreply(214, "The following %scommands are recognized %s.",
		    type, "(* =>'s unimplemented)");
		columns = 76 / width;
		if (columns == 0)
			columns = 1;
		lines = (NCMDS + columns - 1) / columns;
		for (i = 0; i < lines; i++) {
			printf("   ");
			for (j = 0; j < columns; j++) {
				c = ctab + j * lines + i;
				printf("%s%c", c->name,
					c->implemented ? ' ' : '*');
				if (c + lines >= &ctab[NCMDS])
					break;
				w = strlen(c->name) + 1;
				while (w < width) {
					putchar(' ');
					w++;
				}
			}
			printf("\r\n");
		}
		(void) fflush(stdout);
		if (hostinfo)
			reply(214, "Direct comments to ftp-bugs@%s.", hostname);
		else
			reply(214, "End.");
		return;
	}
	upper(s);
	c = lookup(ctab, s);
	if (c == NULL) {
		reply(502, "Unknown command %s.", s);
		return;
	}
	if (c->implemented)
		reply(214, "Syntax: %s%s %s", type, c->name, c->help);
	else
		reply(214, "%s%-*s\t%s; unimplemented.", type, width,
		    c->name, c->help);
}

static void
sizecmd(char *filename)
{
	switch (type) {
	case TYPE_L:
	case TYPE_I: {
		struct stat stbuf;
		if (stat(filename, &stbuf) < 0)
			perror_reply(550, filename);
		else if (!S_ISREG(stbuf.st_mode))
			reply(550, "%s: not a plain file.", filename);
		else
			reply(213, "%jd", (intmax_t)stbuf.st_size);
		break; }
	case TYPE_A: {
		FILE *fin;
		int c;
		off_t count;
		struct stat stbuf;
		fin = fopen(filename, "r");
		if (fin == NULL) {
			perror_reply(550, filename);
			return;
		}
		if (fstat(fileno(fin), &stbuf) < 0) {
			perror_reply(550, filename);
			(void) fclose(fin);
			return;
		} else if (!S_ISREG(stbuf.st_mode)) {
			reply(550, "%s: not a plain file.", filename);
			(void) fclose(fin);
			return;
		} else if (stbuf.st_size > MAXASIZE) {
			reply(550, "%s: too large for type A SIZE.", filename);
			(void) fclose(fin);
			return;
		}

		count = 0;
		while((c=getc(fin)) != EOF) {
			if (c == '\n')	/* will get expanded to \r\n */
				count++;
			count++;
		}
		(void) fclose(fin);

		reply(213, "%jd", (intmax_t)count);
		break; }
	default:
		reply(504, "SIZE not implemented for type %s.",
		           typenames[type]);
	}
}

/* Return 1, if port check is done. Return 0, if not yet. */
static int
port_check(const char *pcmd)
{
	if (his_addr.su_family == AF_INET) {
		if (data_dest.su_family != AF_INET) {
			usedefault = 1;
			reply(500, "Invalid address rejected.");
			return 1;
		}
		if (paranoid &&
		    ((ntohs(data_dest.su_port) < IPPORT_RESERVED) ||
		     memcmp(&data_dest.su_sin.sin_addr,
			    &his_addr.su_sin.sin_addr,
			    sizeof(data_dest.su_sin.sin_addr)))) {
			usedefault = 1;
			reply(500, "Illegal PORT range rejected.");
		} else {
			usedefault = 0;
			if (pdata >= 0) {
				(void) close(pdata);
				pdata = -1;
			}
			reply(200, "%s command successful.", pcmd);
		}
		return 1;
	}
	return 0;
}

static int
check_login1(void)
{
	if (logged_in)
		return 1;
	else {
		reply(530, "Please login with USER and PASS.");
		return 0;
	}
}

/*
 * Replace leading "~user" in a pathname by the user's login directory.
 * Returned string will be in a freshly malloced buffer unless it's NULL.
 */
static char *
exptilde(char *s)
{
	char *p, *q;
	char *path, *user;
	struct passwd *ppw;

	if ((p = strdup(s)) == NULL)
		return (NULL);
	if (*p != '~')
		return (p);

	user = p + 1;	/* skip tilde */
	if ((path = strchr(p, '/')) != NULL)
		*(path++) = '\0'; /* separate ~user from the rest of path */
	if (*user == '\0') /* no user specified, use the current user */
		user = pw->pw_name;
	/* read passwd even for the current user since we may be chrooted */
	if ((ppw = getpwnam(user)) != NULL) {
		/* user found, substitute login directory for ~user */
		if (path)
			asprintf(&q, "%s/%s", ppw->pw_dir, path);
		else
			q = strdup(ppw->pw_dir);
		free(p);
		p = q;
	} else {
		/* user not found, undo the damage */
		if (path)
			path[-1] = '/';
	}
	return (p);
}

/*
 * Expand glob(3) patterns possibly present in a pathname.
 * Avoid expanding to a pathname including '\r' or '\n' in order to
 * not disrupt the FTP protocol.
 * The expansion found must be unique.
 * Return the result as a malloced string, or NULL if an error occurred.
 *
 * Problem: this production is used for all pathname
 * processing, but only gives a 550 error reply.
 * This is a valid reply in some cases but not in others.
 */
static char *
expglob(char *s)
{
	char *p, **pp, *rval;
	int flags = GLOB_BRACE | GLOB_NOCHECK;
	int n;
	glob_t gl;

	memset(&gl, 0, sizeof(gl));
	flags |= GLOB_LIMIT;
	gl.gl_matchc = MAXGLOBARGS;
	if (glob(s, flags, NULL, &gl) == 0 && gl.gl_pathc != 0) {
		for (pp = gl.gl_pathv, p = NULL, n = 0; *pp; pp++)
			if (*(*pp + strcspn(*pp, "\r\n")) == '\0') {
				p = *pp;
				n++;
			}
		if (n == 0)
			rval = strdup(s);
		else if (n == 1)
			rval = strdup(p);
		else {
			reply(550, "Wildcard is ambiguous.");
			rval = NULL;
		}
	} else {
		reply(550, "Wildcard expansion error.");
		rval = NULL;
	}
	globfree(&gl);
	return (rval);
}

#ifdef INET6
/* Return 1, if port check is done. Return 0, if not yet. */
static int
port_check_v6(const char *pcmd)
{
	if (his_addr.su_family == AF_INET6) {
		if (IN6_IS_ADDR_V4MAPPED(&his_addr.su_sin6.sin6_addr))
			/* Convert data_dest into v4 mapped sockaddr.*/
			v4map_data_dest();
		if (data_dest.su_family != AF_INET6) {
			usedefault = 1;
			reply(500, "Invalid address rejected.");
			return 1;
		}
		if (paranoid &&
		    ((ntohs(data_dest.su_port) < IPPORT_RESERVED) ||
		     memcmp(&data_dest.su_sin6.sin6_addr,
			    &his_addr.su_sin6.sin6_addr,
			    sizeof(data_dest.su_sin6.sin6_addr)))) {
			usedefault = 1;
			reply(500, "Illegal PORT range rejected.");
		} else {
			usedefault = 0;
			if (pdata >= 0) {
				(void) close(pdata);
				pdata = -1;
			}
			reply(200, "%s command successful.", pcmd);
		}
		return 1;
	}
	return 0;
}

static void
v4map_data_dest(void)
{
	struct in_addr savedaddr;
	int savedport;

	if (data_dest.su_family != AF_INET) {
		usedefault = 1;
		reply(500, "Invalid address rejected.");
		return;
	}

	savedaddr = data_dest.su_sin.sin_addr;
	savedport = data_dest.su_port;

	memset(&data_dest, 0, sizeof(data_dest));
	data_dest.su_sin6.sin6_len = sizeof(struct sockaddr_in6);
	data_dest.su_sin6.sin6_family = AF_INET6;
	data_dest.su_sin6.sin6_port = savedport;
	memset((caddr_t)&data_dest.su_sin6.sin6_addr.s6_addr[10], 0xff, 2);
	memcpy((caddr_t)&data_dest.su_sin6.sin6_addr.s6_addr[12],
	       (caddr_t)&savedaddr, sizeof(savedaddr));
}
#endif
