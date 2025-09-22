/*	$OpenBSD: ftpcmd.y,v 1.75 2024/04/28 16:42:53 florian Exp $	*/
/*	$NetBSD: ftpcmd.y,v 1.7 1996/04/08 19:03:11 jtc Exp $	*/

/*
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/ftp.h>

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>
#include <limits.h>

#include "monitor.h"
#include "extern.h"

extern	union sockunion data_dest;
extern	int logged_in;
extern	struct passwd *pw;
extern	int guest;
extern	int logging;
extern	int type;
extern	int form;
extern	int debug;
extern	int timeout;
extern	int maxtimeout;
extern  int pdata;
extern	char hostname[], remotehost[];
extern	char proctitle[];
extern	int usedefault;
extern  int transflag;
extern  char tmpline[];
extern	int portcheck;
extern	union sockunion his_addr;
extern	int umaskchange;

off_t	restart_point;

static	int cmd_type;
static	int cmd_form;
static	int cmd_bytesz;
static	int state;
static	int quit;
char	cbuf[512];
char	*fromname;

%}

%union {
	int	i;
	off_t	o;
	char   *s;
}

%token
	A	B	C	E	F	I
	L	N	P	R	S	T

	SP	CRLF	COMMA	ALL

	USER	PASS	ACCT	REIN	QUIT	PORT
	PASV	TYPE	STRU	MODE	RETR	STOR
	APPE	MLFL	MAIL	MSND	MSOM	MSAM
	MRSQ	MRCP	ALLO	REST	RNFR	RNTO
	ABOR	DELE	CWD	LIST	NLST	SITE
	STAT	HELP	NOOP	MKD	RMD	PWD
	CDUP	STOU	SMNT	SYST	SIZE	MDTM

	LPRT	LPSV	EPRT	EPSV

	UMASK	IDLE	CHMOD

	LEXERR

%token	<s> STRING
%token	<i> NUMBER
%token	<o> BIGNUM

%type	<i> check_login check_login_epsvall octal_number byte_size
%type	<i> struct_code mode_code type_code form_code
%type	<i> host_port host_long_port4 host_long_port6
%type	<o> file_size
%type	<s> pathstring pathname password username

%start	cmd_list

%%

cmd_list
	: /* empty */
	| cmd_list cmd
		{
			if (fromname) {
				free(fromname);
				fromname = NULL;
			}
			restart_point = 0;
		}
	| cmd_list rcmd
	;

cmd
	: USER SP username CRLF
		{
			monitor_user($3);
			free($3);
		}
	| PASS SP password CRLF
		{
			quit = monitor_pass($3);
			explicit_bzero($3, strlen($3));
			free($3);

			/* Terminate unprivileged pre-auth slave */
			if (quit)
				_exit(0);
		}
	| PORT check_login_epsvall SP host_port CRLF
		{
			if ($2) {
				if ($4) {
					usedefault = 1;
					reply(500,
					    "Illegal PORT rejected (range errors).");
				} else if (portcheck &&
				    ntohs(data_dest.su_sin.sin_port) < IPPORT_RESERVED) {
					usedefault = 1;
					reply(500,
					    "Illegal PORT rejected (reserved port).");
				} else if (portcheck &&
				    memcmp(&data_dest.su_sin.sin_addr,
				    &his_addr.su_sin.sin_addr,
				    sizeof data_dest.su_sin.sin_addr)) {
					usedefault = 1;
					reply(500,
					    "Illegal PORT rejected (address wrong).");
				} else {
					usedefault = 0;
					if (pdata >= 0) {
						(void) close(pdata);
						pdata = -1;
					}
					reply(200, "PORT command successful.");
				}
			}
		}
	| LPRT check_login_epsvall SP host_long_port4 CRLF
		{
			if ($2) {
				/* reject invalid host_long_port4 */
				if ($4) {
					reply(500,
					    "Illegal LPRT command rejected");
					usedefault = 1;
				} else {
					usedefault = 0;
					if (pdata >= 0) {
						(void) close(pdata);
						pdata = -1;
					}
					reply(200, "LPRT command successful.");
				}
			}
		}

	| LPRT check_login_epsvall SP host_long_port6 CRLF
		{
			if ($2) {
				/* reject invalid host_long_port6 */
				if ($4) {
					reply(500,
					    "Illegal LPRT command rejected");
					usedefault = 1;
				} else {
					usedefault = 0;
					if (pdata >= 0) {
						(void) close(pdata);
						pdata = -1;
					}
					reply(200, "LPRT command successful.");
				}
			}
		}

	| EPRT check_login_epsvall SP STRING CRLF
		{
			if ($2)
				extended_port($4);
			free($4);
		}

	| PASV check_login_epsvall CRLF
		{
			if ($2)
				passive();
		}
	| LPSV check_login_epsvall CRLF
		{
			if ($2)
				long_passive("LPSV", PF_UNSPEC);
		}
	| EPSV check_login SP NUMBER CRLF
		{
			if ($2)
				long_passive("EPSV", epsvproto2af($4));
		}
	| EPSV check_login SP ALL CRLF
		{
			if ($2) {
				reply(200, "EPSV ALL command successful.");
				epsvall++;
			}
		}
	| EPSV check_login CRLF
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
					if (cmd_bytesz == 8) {
						reply(200,
						    "Type set to L (byte size 8).");
						    type = cmd_type;
					} else
						reply(504, "Byte size must be 8.");

				}
			}
		}
	| STRU check_login SP struct_code CRLF
		{
			if ($2) {
				switch ($4) {

				case STRU_F:
					reply(200, "STRU F ok.");
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
					reply(200, "MODE S ok.");
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
			if ($2 && $4 != NULL)
				retrieve(RET_FILE, $4);
			if ($4 != NULL)
				free($4);
		}
	| STOR check_login SP pathname CRLF
		{
			if ($2 && $4 != NULL)
				store($4, "w", 0);
			if ($4 != NULL)
				free($4);
		}
	| APPE check_login SP pathname CRLF
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
	| NLST check_login SP STRING CRLF
		{
			if ($2 && $4 != NULL)
				send_file_list($4);
			free($4);
		}
	| LIST check_login CRLF
		{
			if ($2)
				retrieve(RET_LIST, ".");
		}
	| LIST check_login SP pathname CRLF
		{
			if ($2 && $4 != NULL)
				retrieve(RET_LIST, $4);
			if ($4 != NULL)
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
			if ($2)
				statcmd();
		}
	| DELE check_login SP pathname CRLF
		{
			if ($2 && $4 != NULL)
				delete($4);
			if ($4 != NULL)
				free($4);
		}
	| RNTO check_login SP pathname CRLF
		{
			if ($2 && $4 != NULL) {
				if (fromname) {
					renamecmd(fromname, $4);
					free(fromname);
					fromname = NULL;
				} else {
					reply(503,
					  "Bad sequence of commands.");
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
			if ($2)
				cwd(pw->pw_dir);
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
			free ($3);
		}
	| NOOP CRLF
		{
			reply(200, "NOOP command successful.");
		}
	| MKD check_login SP pathname CRLF
		{
			if ($2 && $4 != NULL)
				makedir($4);
			if ($4 != NULL)
				free($4);
		}
	| RMD check_login SP pathname CRLF
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
			free ($5);
		}
	| SITE SP UMASK check_login CRLF
		{
			mode_t oldmask;

			if ($4) {
				oldmask = umask(0);
				(void) umask(oldmask);
				reply(200, "Current UMASK is %03o", oldmask);
			}
		}
	| SITE SP UMASK check_login SP octal_number CRLF
		{
			mode_t oldmask;

			if ($4) {
				if (($6 == -1) || ($6 > 0777)) {
					reply(501, "Bad UMASK value");
				} else if (!umaskchange) {
					reply(550,
					    "No permission to change umask.");
				} else {
					oldmask = umask($6);
					reply(200,
					    "UMASK set to %03o (was %03o)",
					    $6, oldmask);
				}
			}
		}
	| SITE SP CHMOD check_login SP octal_number SP pathname CRLF
		{
			if ($4 && ($8 != NULL)) {
				if (($6 == -1) || ($6 > 0777))
					reply(501,
					    "CHMOD: Mode value must be between "
					    "0 and 0777");
				else if (!umaskchange)
					reply(550,
					    "No permission to change mode of %s.",
					    $8);
				else if (chmod($8, $6) == -1)
					perror_reply(550, $8);
				else
					reply(200,
					    "CHMOD command successful.");
			}
			if ($8 != NULL)
				free($8);
		}
	| SITE SP check_login IDLE CRLF
		{
			if ($3)
				reply(200,
				    "Current IDLE time limit is %d "
				    "seconds; max %d",
				    timeout, maxtimeout);
		}
	| SITE SP check_login IDLE SP NUMBER CRLF
		{
			if ($3) {
				if ($6 < 30 || $6 > maxtimeout) {
					reply(501,
					    "Maximum IDLE time must be between "
					    "30 and %d seconds",
					    maxtimeout);
				} else {
					timeout = $6;
					(void) alarm((unsigned) timeout);
					reply(200,
					    "Maximum IDLE time set to %d seconds",
					    timeout);
				}
			}
		}
	| STOU check_login SP pathname CRLF
		{
			if ($2 && $4 != NULL)
				store($4, "w", 1);
			if ($4 != NULL)
				free($4);
		}
	| SYST check_login CRLF
		{
			if ($2)
			reply(215, "UNIX Type: L8");
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
				if (stat($4, &stbuf) == -1)
					reply(550, "%s: %s",
					    $4, strerror(errno));
				else if (!S_ISREG(stbuf.st_mode)) {
					reply(550, "%s: not a plain file.", $4);
				} else {
					struct tm *t;
					t = gmtime(&stbuf.st_mtime);
					if (t == NULL) {
						/* invalid time, use epoch */
						stbuf.st_mtime = 0;
						t = gmtime(&stbuf.st_mtime);
					}
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
	| error
		{
			yyclearin;		/* discard lookahead data */
			yyerrok;		/* clear error condition */
			state = 0;		/* reset lexer state */
		}
	;
rcmd
	: RNFR check_login SP pathname CRLF
		{
			restart_point = 0;
			if ($2 && $4) {
				if (fromname)
					free(fromname);
				fromname = renamefrom($4);
				if (fromname == NULL)
					free($4);
			} else if ($4) {
				free ($4);
			}
		}

	| REST check_login SP file_size CRLF
		{
			if ($2) {
				if (fromname) {
					free(fromname);
					fromname = NULL;
				}
				restart_point = $4;
				reply(350, "Restarting at %lld. %s",
				    (long long)restart_point,
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
			$$ = calloc(1, sizeof(char));
		}
	| STRING
	;

byte_size
	: NUMBER
	;

file_size
	: NUMBER
		{
			$$ = $1;
		}
	| BIGNUM
		{
			$$ = $1;
		}
	;

host_port
	: NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER COMMA NUMBER
		{
			char *a, *p;

			if ($1 < 0 || $1 > 255 || $3 < 0 || $3 > 255 ||
			    $5 < 0 || $5 > 255 || $7 < 0 || $7 > 255 ||
			    $9 < 0 || $9 > 255 || $11 < 0 || $11 > 255) {
				$$ = 1;
			} else {
				data_dest.su_sin.sin_len = sizeof(struct sockaddr_in);
				data_dest.su_sin.sin_family = AF_INET;
				p = (char *)&data_dest.su_sin.sin_port;
				p[0] = $9; p[1] = $11;
				a = (char *)&data_dest.su_sin.sin_addr;
				a[0] = $1; a[1] = $3; a[2] = $5; a[3] = $7;
				$$ = 0;
			}
		}
	;

host_long_port4
	: NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER
		{
			char *a, *p;

			/* reject invalid LPRT command */
			if ($1 != 4 || $3 != 4 ||
			    $5 < 0 || $5 > 255 || $7 < 0 || $7 > 255 ||
			    $9 < 0 || $9 > 255 || $11 < 0 || $11 > 255 ||
			    $13 != 2 ||
			    $15 < 0 || $15 > 255 || $17 < 0 || $17 > 255) {
				$$ = 1;
			} else {
				data_dest.su_sin.sin_len =
					sizeof(struct sockaddr_in);
				data_dest.su_family = AF_INET;
				p = (char *)&data_dest.su_port;
				p[0] = $15; p[1] = $17;
				a = (char *)&data_dest.su_sin.sin_addr;
				a[0] = $5; a[1] = $7; a[2] = $9; a[3] = $11;
				$$ = 0;
			}
		}
	;

host_long_port6
	: NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER
		{
			char *a, *p;

			/* reject invalid LPRT command */
			if ($1 != 6 || $3 != 16 ||
			    $5 < 0 || $5 > 255 || $7 < 0 || $7 > 255 ||
			    $9 < 0 || $9 > 255 || $11 < 0 || $11 > 255 ||
			    $13 < 0 || $13 > 255 || $15 < 0 || $15 > 255 ||
			    $17 < 0 || $17 > 255 || $19 < 0 || $19 > 255 ||
			    $21 < 0 || $21 > 255 || $23 < 0 || $23 > 255 ||
			    $25 < 0 || $25 > 255 || $27 < 0 || $27 > 255 ||
			    $29 < 0 || $29 > 255 || $31 < 0 || $31 > 255 ||
			    $33 < 0 || $33 > 255 || $35 < 0 || $35 > 255 ||
			    $37 != 2 ||
			    $39 < 0 || $39 > 255 || $41 < 0 || $41 > 255) {
				$$ = 1;
			} else {
				data_dest.su_sin6.sin6_len =
					sizeof(struct sockaddr_in6);
				data_dest.su_family = AF_INET6;
				p = (char *)&data_dest.su_port;
				p[0] = $39; p[1] = $41;
				a = (char *)&data_dest.su_sin6.sin6_addr;
				 a[0] =  $5;  a[1] =  $7;
				 a[2] =  $9;  a[3] = $11;
				 a[4] = $13;  a[5] = $15;
				 a[6] = $17;  a[7] = $19;
				 a[8] = $21;  a[9] = $23;
				a[10] = $25; a[11] = $27;
				a[12] = $29; a[13] = $31;
				a[14] = $33; a[15] = $35;
				if (his_addr.su_family == AF_INET6) {
					/* XXX more sanity checks! */
					data_dest.su_sin6.sin6_scope_id =
					    his_addr.su_sin6.sin6_scope_id;
				}

				$$ = 0;
			}
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
			cmd_bytesz = 8;
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
			/*
			 * Problem: this production is used for all pathname
			 * processing, but only gives a 550 error reply.
			 * This is a valid reply in some cases but not in others.
			 */
			if (logged_in && $1 && strchr($1, '~') != NULL) {
				glob_t gl;
				int flags =
				 GLOB_BRACE|GLOB_NOCHECK|GLOB_QUOTE|GLOB_TILDE;
				char *pptr = $1;

				/*
				 * glob() will only find a leading ~, but
				 * Netscape kindly puts a slash in front of
				 * it for publish URLs.  There needs to be
				 * a flag for glob() that expands tildes
				 * anywhere in the string.
				 */
				if ((pptr[0] == '/') && (pptr[1] == '~'))
					pptr++;

				memset(&gl, 0, sizeof(gl));
				if (glob(pptr, flags, NULL, &gl) ||
				    gl.gl_pathc == 0) {
					reply(550, "not found");
					$$ = NULL;
				} else {
					$$ = strdup(gl.gl_pathv[0]);
				}
				globfree(&gl);
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
			dec = $1;
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
			if (logged_in)
				$$ = 1;
			else {
				reply(530, "Please login with USER and PASS.");
				$$ = 0;
				state = 0;
				YYABORT;
			}
		}
	;

check_login_epsvall
	: /* empty */
		{
			if (!logged_in) {
				reply(530, "Please login with USER and PASS.");
				$$ = 0;
				state = 0;
				YYABORT;
			} else if (epsvall) {
				reply(501, "the command is disallowed "
				    "after EPSV ALL");
				usedefault = 1;
				$$ = 0;
			} else
				$$ = 1;
		}
	;

%%

#define	CMD	0	/* beginning of command */
#define	ARGS	1	/* expect miscellaneous arguments */
#define	STR1	2	/* expect SP followed by STRING */
#define	STR2	3	/* expect STRING */
#define	OSTR	4	/* optional SP then STRING */
#define	ZSTR1	5	/* SP then optional STRING */
#define	ZSTR2	6	/* optional STRING after SP */
#define	SITECMD	7	/* SITE command */
#define	NSTR	8	/* Number followed by a string */

struct tab {
	char	*name;
	short	token;
	short	state;
	short	implemented;	/* 1 if command is implemented */
	char	*help;
};

struct tab cmdtab[] = {		/* In order defined in RFC 765 */
	{ "USER", USER, STR1, 1,	"<sp> username" },
	{ "PASS", PASS, ZSTR1, 1,	"<sp> password" },
	{ "ACCT", ACCT, STR1, 0,	"(specify account)" },
	{ "SMNT", SMNT, ARGS, 0,	"(structure mount)" },
	{ "REIN", REIN, ARGS, 0,	"(reinitialize server state)" },
	{ "QUIT", QUIT, ARGS, 1,	"(terminate service)", },
	{ "PORT", PORT, ARGS, 1,	"<sp> b0, b1, b2, b3, b4" },
	{ "LPRT", LPRT, ARGS, 1,	"<sp> af, hal, h1, h2, h3,..., pal, p1, p2..." },
	{ "EPRT", EPRT, STR1, 1,	"<sp> |af|addr|port|" },
	{ "PASV", PASV, ARGS, 1,	"(set server in passive mode)" },
	{ "LPSV", LPSV, ARGS, 1,	"(set server in passive mode)" },
	{ "EPSV", EPSV, ARGS, 1,	"[<sp> af|ALL]" },
	{ "TYPE", TYPE, ARGS, 1,	"<sp> [ A | E | I | L ]" },
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
	{ "UMASK", UMASK, ARGS, 1,	"[ <sp> umask ]" },
	{ "IDLE", IDLE, ARGS, 1,	"[ <sp> maximum-idle-time ]" },
	{ "CHMOD", CHMOD, NSTR, 1,	"<sp> mode <sp> file-name" },
	{ "HELP", HELP, OSTR, 1,	"[ <sp> <string> ]" },
	{ NULL,   0,    0,    0,	0 }
};

static void	 help(struct tab *, char *);
static struct tab *
		 lookup(struct tab *, const char *);
static void	 sizecmd(const char *);
static int	 yylex(void);

extern int epsvall;

static struct tab *
lookup(struct tab *p, const char *cmd)
{

	for (; p->name != NULL; p++)
		if (strcmp(cmd, p->name) == 0)
			return (p);
	return (NULL);
}

#include <arpa/telnet.h>

/*
 * get_line - a hacked up version of fgets to ignore TELNET escape codes.
 */
int
get_line(char *s, int n)
{
	int c;
	char *cs;

	cs = s;
/* tmpline may contain saved command from urgent mode interruption */
	for (c = 0; tmpline[c] != '\0' && --n > 0; ++c) {
		*cs++ = tmpline[c];
		if (tmpline[c] == '\n') {
			*cs++ = '\0';
			if (debug)
				syslog(LOG_DEBUG, "command: %s", s);
			tmpline[0] = '\0';
			return(0);
		}
		if (c == 0)
			tmpline[0] = '\0';
	}
	while ((c = getc(stdin)) != EOF) {
		c &= 0377;
		if (c == IAC) {
		    if ((c = getc(stdin)) != EOF) {
			c &= 0377;
			switch (c) {
			case WILL:
			case WONT:
				c = getc(stdin);
				printf("%c%c%c", IAC, DONT, 0377&c);
				(void) fflush(stdout);
				continue;
			case DO:
			case DONT:
				c = getc(stdin);
				printf("%c%c%c", IAC, WONT, 0377&c);
				(void) fflush(stdout);
				continue;
			case IAC:
				break;
			default:
				continue;	/* ignore command */
			}
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
			while (c != '\n' && (c = getc(stdin)) != EOF)
				;
			return (-2);
		}
		if (c == '\n')
			break;
	}
	if (c == EOF && cs == s)
		return (-1);
	*cs++ = '\0';
	if (debug) {
		if (!guest && strncasecmp("pass ", s, 5) == 0) {
			/* Don't syslog passwords */
			syslog(LOG_DEBUG, "command: %.5s ???", s);
		} else {
			char *cp;
			int len;

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

void
toolong(int signo)
{
	struct syslog_data sdata = SYSLOG_DATA_INIT;

	reply_r(421,
	    "Timeout (%d seconds): closing control connection.", timeout);
	if (logging)
		syslog_r(LOG_INFO, &sdata, "User %s timed out after %d seconds",
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
			(void) alarm((unsigned) timeout);
			n = get_line(cbuf, sizeof(cbuf)-1);
			if (n == -1) {
				reply(221, "You could at least say goodbye.");
				dologout(0);
			} else if (n == -2) {
				reply(500, "Command too long.");
				alarm(0);
				continue;
			}
			(void) alarm(0);
			if ((cp = strchr(cbuf, '\r'))) {
				*cp++ = '\n';
				*cp = '\0';
			}
			if (strncasecmp(cbuf, "PASS", 4) != 0) {
				if ((cp = strpbrk(cbuf, "\n"))) {
					c = *cp;
					*cp = '\0';
					setproctitle("%s: %s", proctitle, cbuf);
					*cp = c;
				}
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
			if (p != NULL) {
				if (p->implemented == 0) {
					nack(p->name);
					return (LEXERR);
				}
				state = p->state;
				yylval.s = p->name;
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
			if (p != NULL) {
				if (p->implemented == 0) {
					state = CMD;
					nack(p->name);
					return (LEXERR);
				}
				state = p->state;
				yylval.s = p->name;
				return (p->token);
			}
			state = CMD;
			break;

		case OSTR:
			if (cbuf[cpos] == '\n') {
				state = CMD;
				return (CRLF);
			}
			/* FALLTHROUGH */

		case STR1:
		case ZSTR1:
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
				yylval.s = strdup(cp);
				if (yylval.s == NULL)
					fatal("Ran out of memory.");
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
			if (isdigit((unsigned char)cbuf[cpos])) {
				cp = &cbuf[cpos];
				while (isdigit((unsigned char)cbuf[++cpos]))
					;
				c = cbuf[cpos];
				cbuf[cpos] = '\0';
				yylval.i = atoi(cp);
				cbuf[cpos] = c;
				state = STR1;
				return (NUMBER);
			}
			state = STR1;
			goto dostr1;

		case ARGS:
			if (isdigit((unsigned char)cbuf[cpos])) {
				long long llval;

				cp = &cbuf[cpos];
				errno = 0;
				llval = strtoll(cp, &cp2, 10);
				if (llval < 0 ||
				    (errno == ERANGE && llval == LLONG_MAX))
					break;

				cpos = (int)(cp2 - cbuf);
				if (llval > INT_MAX) {
					yylval.o = llval;
					return (BIGNUM);
				} else {
					yylval.i = (int)llval;
					return (NUMBER);
				}
			}
			if (strncasecmp(&cbuf[cpos], "ALL", 3) == 0 &&
			    !isalnum((unsigned char)cbuf[cpos + 3])) {
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
			fatal("Unknown state in scanner.");
		}
		state = CMD;
		return (LEXERR);
	}
}

void
upper(char *s)
{
	char *p;

	for (p = s; *p; p++)
		*p = (char)toupper((unsigned char)*p);
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
	if (s == NULL) {
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
		reply(214, "Direct comments to ftp-bugs@%s.", hostname);
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
sizecmd(const char *filename)
{
	switch (type) {
	case TYPE_L:
	case TYPE_I: {
		struct stat stbuf;
		if (stat(filename, &stbuf) == -1 || !S_ISREG(stbuf.st_mode))
			reply(550, "%s: not a plain file.", filename);
		else
			reply(213, "%lld", (long long)stbuf.st_size);
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
		if (fstat(fileno(fin), &stbuf) == -1 || !S_ISREG(stbuf.st_mode)) {
			reply(550, "%s: not a plain file.", filename);
			(void) fclose(fin);
			return;
		}
		if (stbuf.st_size > 10240) {
			reply(550, "%s: file too large for SIZE.", filename);
			(void) fclose(fin);
			return;
		}

		count = 0;
		while((c = getc(fin)) != EOF) {
			if (c == '\n')	/* will get expanded to \r\n */
				count++;
			count++;
		}
		(void) fclose(fin);

		reply(213, "%lld", (long long)count);
		break; }
	default:
		reply(504, "SIZE not implemented for Type %c.", "?AEIL"[type]);
	}
}
