/*	$OpenBSD: conf.y,v 1.22 2021/10/24 21:24:19 deraadt Exp $	*/

/*
 * Copyright (c) 2005 Håkan Olsson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Definitions */
%{
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

#include "sasyncd.h"
#include "net.h"

/* Global configuration context.  */
struct cfgstate	cfgstate;

/* Local variables */
int	conflen = 0;
char	*confbuf, *confptr;

int	yyparse(void);
int	yylex(void);
void	yyerror(const char *);
unsigned char x2i(unsigned char *);
%}

%union {
	char	*string;
	int	 val;
	struct {
		unsigned char	*data;
		int	 len;
	} hex;
}

%token MODE INTERFACE INTERVAL LISTEN ON PORT PEER SHAREDKEY
%token Y_SLAVE Y_MASTER INET INET6 FLUSHMODE STARTUP NEVER SYNC
%token GROUP SKIPSLAVE CONTROL
%token <string> STRING
%token <hex>	HEX
%token <val>	VALUE
%type  <val>	af port mode flushmode ctlmode

%%
/* Rules */

settings	: /* empty */
		| settings setting
		;

af		: /* empty */		{ $$ = AF_UNSPEC; }
		| INET			{ $$ = AF_INET; }
		| INET6			{ $$ = AF_INET6; }
		;

port		: /* empty */		{ $$ = SASYNCD_DEFAULT_PORT; }
		| PORT VALUE		{ $$ = $2; }
		;

mode		: Y_MASTER		{ $$ = MASTER; }
		| Y_SLAVE		{ $$ = SLAVE; }
		;

modes		: SKIPSLAVE
		{
			cfgstate.flags |= SKIP_LOCAL_SAS;
			log_msg(2, "config: not syncing SA to peers");
		}
		| mode
		{
			const char *m[] = CARPSTATES;
			cfgstate.lockedstate = $1;
			log_msg(2, "config: mode set to %s", m[$1]);
		}
		;

flushmode	: STARTUP		{ $$ = FM_STARTUP; }
		| NEVER			{ $$ = FM_NEVER; }
		| SYNC			{ $$ = FM_SYNC; }
		;

key		: STRING
		{
			if (cfgstate.sharedkey)
				free(cfgstate.sharedkey);
			cfgstate.sharedkey = $1;
			cfgstate.sharedkey_len = strlen($1) * 8;
			log_msg(2, "config: shared ascii key");
		}
		| HEX
		{
			if (cfgstate.sharedkey)
				free(cfgstate.sharedkey);
			cfgstate.sharedkey = $1.data;
			cfgstate.sharedkey_len = $1.len * 8;
			log_msg(2, "config: %d byte shared hex key", $1.len);
		}

ctlmode		: STRING
		{
			/* Compare strings to avoid keywords for daemons */
			if (strcmp("isakmpd", $1) == 0)
				$$ = CTL_ISAKMPD;
			else if (strcmp("iked", $1) == 0)
				$$ = CTL_IKED;
			else if (strcmp("all", $1) == 0)
				$$ = CTL_MASK;
			else if (strcmp("none", $1) == 0)
				$$ = CTL_NONE;
			else {
				log_err("config: invalid control mode");
				free($1);
				YYERROR;
			}
			log_msg(2, "config: control mode set to %s", $1);
			free($1);
		}
		;

setting		: INTERFACE STRING
		{
			if (cfgstate.carp_ifname)
				free(cfgstate.carp_ifname);
			cfgstate.carp_ifname = $2;
			log_msg(2, "config: interface %s",
			    cfgstate.carp_ifname);
		}
		| GROUP STRING
		{
			if (cfgstate.carp_ifgroup)
				free(cfgstate.carp_ifgroup);
			cfgstate.carp_ifgroup = $2;
			log_msg(2, "config: group %s",
			    cfgstate.carp_ifgroup);
		}
		| FLUSHMODE flushmode
		{
			const char *fm[] = { "STARTUP", "NEVER", "SYNC" };
			cfgstate.flags |= $2;
			log_msg(2, "config: flush mode set to %s", fm[$2]);
		}
		| PEER STRING
		{
			struct syncpeer	*peer;
			int		 duplicate = 0;

			for (peer = LIST_FIRST(&cfgstate.peerlist); peer;
			     peer = LIST_NEXT(peer, link))
				if (strcmp($2, peer->name) == 0) {
					duplicate++;
					break;
				}
			if (duplicate)
				free($2);
			else {
				peer = calloc(1, sizeof *peer);
				if (!peer) {
					log_err("config: calloc(1, %lu) "
					    "failed", sizeof *peer);
					free($2);
					YYERROR;
				}
				peer->name = $2;
			}
			LIST_INSERT_HEAD(&cfgstate.peerlist, peer, link);
			cfgstate.peercnt++;
			log_msg(2, "config: add peer %s", peer->name);
		}
		| LISTEN ON STRING af port
		{
			char pstr[20];

			if (cfgstate.listen_on)
				free(cfgstate.listen_on);
			cfgstate.listen_on = $3;
			cfgstate.listen_family = $4;
			cfgstate.listen_port = $5;
			if ($5 < 1 || $5 > IPPORT_HILASTAUTO) {
				cfgstate.listen_port = SASYNCD_DEFAULT_PORT;
				log_msg(0, "config: bad port, listen-port "
				    "reset to %u", SASYNCD_DEFAULT_PORT);
			}
			if ($5 != SASYNCD_DEFAULT_PORT)
				snprintf(pstr, sizeof pstr, "port %d",$5);
			log_msg(2, "config: listen on %s %s%s",
			    cfgstate.listen_on, $4 == AF_INET6 ? "(IPv6) " :
			    ($4 == AF_INET ? "(IPv4) " : ""),
			    $5 != SASYNCD_DEFAULT_PORT ? pstr : "");
		}
		| MODE modes
		| SHAREDKEY key
		{
			int bits;

			bits = cfgstate.sharedkey_len;
			if (bits != 128 && bits != 192 && bits != 256) {
				log_err("config: bad shared key length %d, "
				    "should be 128, 192 or 256 bits\n", bits);
				YYERROR;
			}
			log_msg(2, "config: shared key set");
		}
		| CONTROL ctlmode
		{
			cfgstate.flags &= ~CTL_MASK;
			cfgstate.flags |= $2;
		}
		;

%%
/* Program */

struct keyword {
	char *name;
	int   value;
};

static int
match_cmp(const void *a, const void *b)
{
	return strcmp(a, ((const struct keyword *)b)->name);
}

static int
match(char *token)
{
	/* Sorted */
	static const struct keyword keywords[] = {
		{ "control", CONTROL },
		{ "flushmode", FLUSHMODE },
		{ "group", GROUP },
		{ "inet", INET },
		{ "inet6", INET6 },
		{ "interface", INTERFACE },
		{ "listen", LISTEN },
		{ "master", Y_MASTER },
		{ "mode", MODE },
		{ "never", NEVER },
		{ "on", ON },
		{ "peer", PEER },
		{ "port", PORT },
		{ "sharedkey", SHAREDKEY },
		{ "skipslave", SKIPSLAVE },
		{ "slave", Y_SLAVE },
		{ "startup", STARTUP },
		{ "sync", SYNC },
	};
	const struct keyword *k;

	k = bsearch(token, keywords, sizeof keywords / sizeof keywords[0],
	    sizeof keywords[0], match_cmp);

	return k ? k->value : STRING;
}

int
yylex(void)
{
	char *p;
	int v, i, len;

	/* Locate next token */
	if (!confptr)
		confptr = confbuf;
	else {
		for (p = confptr; p < confbuf + conflen && *p; p++)
			;
		if (p == confbuf + conflen)
			return 0;
		p++;
		if (!*p)
			return 0;
		confptr = p;
	}

	/* Hex token? */
	p = confptr;
	if (!strncmp(p, "0x", 2)) {
		for (p = confptr + 2; *p; p++)
			if (!isxdigit(*p))
				goto is_string;
		p = confptr + 2;
		len = strlen(p) / 2;
		if ((yylval.hex.data = calloc(len, sizeof(unsigned char)))
		    == NULL) {
			log_err("yylex: calloc()");
			exit(1);
		}
		for (i = 0; i < len; i++)
			yylval.hex.data[i] = x2i(p + 2 * i);
		yylval.hex.len = len;
		return HEX;
	}

	/* Numerical token? */
	if (isdigit(*confptr)) {
		for (p = confptr; *p; p++)
			if (*p == '.' || *p == ':') /* IP address, or bad input */
				goto is_string;
		v = (int)strtol(confptr, (char **)NULL, 10);
		yylval.val = v;
		return VALUE;
	}

  is_string:
	v = match(confptr);
	if (v == STRING) {
		yylval.string = strdup(confptr);
		if (!yylval.string) {
			log_err("yylex: strdup()");
			exit(1);
		}
	}
	return v;
}

int
conf_parse_file(char *cfgfile)
{
	struct stat	st;
	int		fd, r;
	char		*buf, *s, *d;
	struct passwd	*pw;

	if (stat(cfgfile, &st) != 0)
		goto bad;

	pw = getpwnam(SASYNCD_USER);
	if (pw == NULL) {
		log_err("getpwnam(%s) failed", SASYNCD_USER);
		return 1;
	}

	/* Valid file? */
	if ((st.st_uid && st.st_uid != pw->pw_uid) ||
	    ((st.st_mode & S_IFMT) != S_IFREG) ||
	    ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0)) {
		log_msg(0, "configuration file has bad owner, type or mode");
		goto bad;
	}

	fd = open(cfgfile, O_RDONLY);
	if (fd == -1)
		goto bad;

	conflen = st.st_size;
	buf = malloc(conflen + 1);
	if (!buf) {
		log_err("malloc(%d) failed", conflen + 1);
		close(fd);
		return 1;
	}

	if (read(fd, buf, conflen) != conflen) {
		log_err("read() failed");
		free(buf);
		close(fd);
		return 1;
	}
	close(fd);

	/* Prepare the buffer somewhat in the way of strsep() */
	buf[conflen] = (char)0;
	for (s = buf, d = s; s < buf + conflen && *s; s++) {
		if (isspace(*s) && isspace(*(s+1)))
			continue;
		if (*s == '#') {
			while (*s != '\n' && s < buf + conflen)
				s++;
			while (*s == '\n' && s < buf + conflen)
				s++;
			s--;
			continue;
		}
		if (d == buf && isspace(*s))
			continue;
		*d++ = *s;
	}
	*d = (char)0;
	for (s = buf; s <= d; s++)
		if (isspace(*s))
			*s = (char)0;

	confbuf = buf;
	confptr = NULL;
	r = yyparse();
	free(buf);

	if (!cfgstate.carp_ifgroup)
		cfgstate.carp_ifgroup = strdup("carp");

	return r;

  bad:
	log_msg(0, "failed to open \"%s\"", cfgfile);
	return 1;
}

unsigned char
x2i(unsigned char *s)
{
        char    ss[3];

        ss[0] = s[0];
        ss[1] = s[1];
        ss[2] = 0;

        return ((unsigned char)strtoul(ss, NULL, 16));
}

void
yyerror(const char *s)
{
	fprintf(stderr, "config: %s\n", s);
}
