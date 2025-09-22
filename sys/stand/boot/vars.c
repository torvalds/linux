/*	$OpenBSD: vars.c,v 1.17 2023/03/13 20:19:22 miod Exp $	*/

/*
 * Copyright (c) 1998-2000 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <libsa.h>
#include <sys/reboot.h>
#include <lib/libkern/funcs.h>
#include "cmd.h"

extern char prog_ident[];
extern int debug;
int db_console = -1;

static int Xdevice(void);
#ifdef DEBUG
static int Xdebug(void);
#endif
static int Xdb_console(void);
static int Ximage(void);
static int Xhowto(void);
#ifdef BOOT_STTY
static int Xtty(void);
#endif
static int Xtimeout(void);
int Xset(void);
int Xenv(void);

const struct cmd_table cmd_set[] = {
	{"howto",  CMDT_VAR, Xhowto},
#ifdef DEBUG
	{"debug",  CMDT_VAR, Xdebug},
#endif
	{"device", CMDT_VAR, Xdevice},
#ifdef BOOT_STTY
	{"tty",    CMDT_VAR, Xtty},
#endif
	{"image",  CMDT_VAR, Ximage},
	{"timeout",CMDT_VAR, Xtimeout},
	{"db_console", CMDT_VAR, Xdb_console},
	{NULL,0}
};

#ifdef DEBUG
static int
Xdebug(void)
{
	if (cmd.argc != 2)
		printf( "o%s\n", debug? "n": "ff" );
	else
		debug = (cmd.argv[1][0] == '0' ||
			 (cmd.argv[1][0] == 'o' && cmd.argv[1][1] == 'f'))?
			 0: 1;
	return 0;
}
#endif

int
Xdb_console(void)
{
	if (cmd.argc != 2) {
		switch (db_console) {
		case 0:
			printf("off\n");
			break;
		case 1:
			printf("on\n");
			break;
		default:
			printf("unset\n");
			break;
		}
	} else {
		if (strcmp(cmd.argv[1], "0") == 0 ||
		    strcmp(cmd.argv[1], "off") == 0)
			db_console = 0;
		else if (strcmp(cmd.argv[1], "1") == 0 ||
		    strcmp(cmd.argv[1], "on") == 0)
			db_console = 1;
	}

	return (0);
}

static int
Xtimeout(void)
{
	if (cmd.argc != 2)
		printf( "%d\n", cmd.timeout );
	else
		cmd.timeout = (int)strtol( cmd.argv[1], (char **)NULL, 0 );
	return 0;
}

/* called only w/ no arguments */
int
Xset(void)
{
	const struct cmd_table *ct;

	printf("%s\n", prog_ident);
	for (ct = cmd_set; ct->cmd_name != NULL; ct++) {
		printf("%s\t ", ct->cmd_name);
		(*ct->cmd_exec)();
	}
	return 0;
}

static int
Xdevice(void)
{
	if (cmd.argc != 2)
		printf("%s\n", cmd.bootdev);
	else
		strlcpy(cmd.bootdev, cmd.argv[1], sizeof(cmd.bootdev));
	return 0;
}

static int
Ximage(void)
{
	if (cmd.argc != 2)
		printf("%s\n", cmd.image);
	else
		strlcpy(cmd.image, cmd.argv[1], sizeof(cmd.image));
	return 0;
}

#ifdef BOOT_STTY
static int
Xtty(void)
{
	dev_t dev;

	if (cmd.argc != 2)
		printf("%s\n", ttyname(0));
	else {
		dev = ttydev(cmd.argv[1]);
		if (dev == NODEV)
			printf("%s not a console device\n", cmd.argv[1]);
		else {
			printf("switching console to %s\n", cmd.argv[1]);
			if (cnset(dev))
				printf("%s console not present\n",
				    cmd.argv[1]);
			else
				printf("%s\n", prog_ident);
		}
	}
	return 0;
}
#endif

#ifdef __alpha__
#define	ASKNAME_LETTER 'n'
#else
#define	ASKNAME_LETTER 'a'
#endif

static int
Xhowto(void)
{
	if (cmd.argc == 1) {
		if (cmd.boothowto) {
			putchar('-');
			if (cmd.boothowto & RB_ASKNAME)
				putchar(ASKNAME_LETTER);
			if (cmd.boothowto & RB_CONFIG)
				putchar('c');
			if (cmd.boothowto & RB_SINGLE)
				putchar('s');
			if (cmd.boothowto & RB_KDB)
				putchar('d');
		}
		putchar('\n');
	} else
		bootparse(1);
	return 0;
}

int
bootparse(int i)
{
	char *cp;
	int howto = cmd.boothowto;

	for (; i < cmd.argc; i++) {
		cp = cmd.argv[i];
		if (*cp == '-') {
			while (*++cp) {
				switch (*cp) {
				case ASKNAME_LETTER:
					howto |= RB_ASKNAME;
					break;
				case 'c':
					howto |= RB_CONFIG;
					break;
				case 's':
					howto |= RB_SINGLE;
					break;
				case 'd':
					howto |= RB_KDB;
					break;
				default:
					printf("howto: bad option: %c\n", *cp);
					return 1;
				}
			}
		} else {
			printf("boot: illegal argument %s\n", cmd.argv[i]);
			return 1;
		}
	}
	cmd.boothowto = howto;
	return 0;
}

/*
 * maintain environment as a sequence of '\n' separated
 * variable definitions in the form <name>=[<value>]
 * terminated by the usual '\0'
 */
char *environ;

int
Xenv(void)
{
	if (cmd.argc == 1) {
		if (environ)
			printf("%s", environ);
		else
			printf("empty\n");
	} else {
		char *p, *q;
		int l;

		for (p = environ; p && *p; p = q) {
			l = strlen(cmd.argv[1]);
			for (q = p; *q != '='; q++)
				;
			l = max(l, q - p) + 1;
			for (q = p; *q != '\n'; q++)
				;
			if (*q)
				q++;
			if (!strncmp(p, cmd.argv[1], l)) {
				while((*p++ = *q++))
					;
				p--;
			}
		}
		if (!p)
			p = environ = alloc(4096);
		snprintf(p, environ + 4096 - p, "%s=%s\n",
		    cmd.argv[1], (cmd.argc==3?cmd.argv[2]:""));
	}

	return 0;
}
