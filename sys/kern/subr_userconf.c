/*	$OpenBSD: subr_userconf.c,v 1.48 2022/08/14 01:58:28 jsg Exp $	*/

/*
 * Copyright (c) 1996-2001 Mats O Jansson <moj@stacken.kth.se>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <dev/cons.h>

extern char *locnames[];
extern short locnamp[];
extern short cfroots[];
extern int cfroots_size;
extern int pv_size;
extern short pv[];
extern char *pdevnames[];
extern int pdevnames_size;
extern struct pdevinit pdevinit[];

int userconf_base = 16;				/* Base for "large" numbers */
int userconf_maxdev = -1;			/* # of used device slots   */
int userconf_totdev = -1;			/* # of device slots        */
int userconf_maxlocnames = -1;			/* # of locnames            */
int userconf_cnt = -1;				/* Line counter for ...     */
int userconf_lines = 12;			/* ... # of lines per page  */
int userconf_histlen = 0;
int userconf_histcur = 0;
char userconf_history[1024];
int userconf_histsz = sizeof(userconf_history);
char userconf_argbuf[40];			/* Additional input         */
char userconf_cmdbuf[40];			/* Command line             */
char userconf_histbuf[40];

void userconf_init(void);
int userconf_more(void);
void userconf_modify(char *, long *, long);
void userconf_hist_cmd(char);
void userconf_hist_int(long);
void userconf_hist_eoc(void);
void userconf_pnum(long);
void userconf_pdevnam(short);
void userconf_pdev(short);
int userconf_number(char *, long *, long);
int userconf_device(char *, long *, short *, short *);
int userconf_attr(char *, long *);
void userconf_change(int);
void userconf_disable(int);
void userconf_enable(int);
void userconf_help(void);
void userconf_list(void);
void userconf_show(void);
void userconf_common_attr_val(short, long *, char);
void userconf_show_attr(char *);
void userconf_common_dev(char *, int, short, short, char);
void userconf_common_attr(char *, int, char);
void userconf_add_read(char *, char, char *, int, long *);
void userconf_add(char *, int, short, short);
int userconf_parse(char *);

#define UC_CHANGE 'c'
#define UC_DISABLE 'd'
#define UC_ENABLE 'e'
#define UC_FIND 'f'
#define UC_SHOW 's'

char *userconf_cmds[] = {
	"add",		"a",
	"base",		"b",
	"change",	"c",
#if defined(DDB)
	"ddb",		"D",
#endif
	"disable",	"d",
	"enable",	"e",
	"exit",		"q",
	"find",		"f",
	"help",		"h",
	"list",		"l",
	"lines",	"L",
	"quit",		"q",
	"show",		"s",
	"verbose",	"v",
	"?",		"h",
	"",		 "",
};

void
userconf_init(void)
{
	int i = 0;
	struct cfdata *cd;
	int   ln;

	while (cfdata[i].cf_attach != NULL) {
		userconf_maxdev = i;
		userconf_totdev = i;

		cd = &cfdata[i];
		ln = cd->cf_locnames;
		while (locnamp[ln] != -1) {
			if (locnamp[ln] > userconf_maxlocnames)
				userconf_maxlocnames = locnamp[ln];
			ln++;
		}
		i++;
	}

	while (cfdata[i].cf_attach == NULL) {
		userconf_totdev = i;
		i++;
	}
	userconf_totdev = userconf_totdev - 1;
}

int
userconf_more(void)
{
	int quit = 0;
	char c = '\0';

	if (userconf_cnt != -1) {
		if (userconf_cnt == userconf_lines) {
			printf("--- more ---");
			c = cngetc();
			userconf_cnt = 0;
			printf("\r            \r");
		}
		userconf_cnt++;
		if (c == 'q' || c == 'Q')
			quit = 1;
	}
	return (quit);
}

void
userconf_hist_cmd(char cmd)
{
	userconf_histcur = userconf_histlen;
	if (userconf_histcur < userconf_histsz) {
		userconf_history[userconf_histcur] = cmd;
		userconf_histcur++;
	}
}

void
userconf_hist_int(long val)
{
	snprintf(userconf_histbuf, sizeof userconf_histbuf, " %ld", val);
	if (userconf_histcur + strlen(userconf_histbuf) < userconf_histsz) {
		bcopy(userconf_histbuf,
		    &userconf_history[userconf_histcur],
		    strlen(userconf_histbuf));
		userconf_histcur = userconf_histcur + strlen(userconf_histbuf);
	}
}

void
userconf_hist_eoc(void)
{
	if (userconf_histcur < userconf_histsz) {
		userconf_history[userconf_histcur] = '\n';
		userconf_histcur++;
		userconf_histlen = userconf_histcur;
	}
}

void
userconf_pnum(long val)
{
	if (val > -2 && val < 16) {
		printf("%ld",val);
		return;
	}

	switch (userconf_base) {
	case 8:
		printf("0%lo",val);
		break;
	case 10:
		printf("%ld",val);
		break;
	case 16:
	default:
		printf("0x%lx",val);
		break;
	}
}

void
userconf_pdevnam(short dev)
{
	struct cfdata *cd;

	cd = &cfdata[dev];
	printf("%s", cd->cf_driver->cd_name);
	switch (cd->cf_fstate) {
	case FSTATE_NOTFOUND:
	case FSTATE_DNOTFOUND:
		printf("%d", cd->cf_unit);
		break;
	case FSTATE_FOUND:
		printf("*FOUND*");
		break;
	case FSTATE_STAR:
	case FSTATE_DSTAR:
		printf("*");
		break;
	default:
		printf("*UNKNOWN*");
		break;
	}
}

void
userconf_pdev(short devno)
{
	struct cfdata *cd;
	short *p;
	long  *l;
	int   ln;
	char c;

	if (devno > userconf_maxdev && devno <= userconf_totdev) {
		printf("%3d free slot (for add)\n", devno);
		return;
	}

	if (devno > userconf_totdev &&
	    devno <= userconf_totdev+pdevnames_size) {
		printf("%3d %s count %d", devno,
		    pdevnames[devno-userconf_totdev-1],
		    abs(pdevinit[devno-userconf_totdev-1].pdev_count));
		if (pdevinit[devno-userconf_totdev-1].pdev_count < 1)
			printf(" disable");
		printf(" (pseudo device)\n");
		return;
	}

	if (devno >  userconf_maxdev) {
		printf("Unknown devno (max is %d)\n", userconf_maxdev);
		return;
	}

	cd = &cfdata[devno];

	printf("%3d ", devno);
	userconf_pdevnam(devno);
	printf(" at");
	c = ' ';
	p = cd->cf_parents;
	if (*p == -1)
		printf(" root");
	while (*p != -1) {
		printf("%c", c);
		userconf_pdevnam(*p++);
		c = '|';
	}
	switch (cd->cf_fstate) {
	case FSTATE_NOTFOUND:
	case FSTATE_FOUND:
	case FSTATE_STAR:
		break;
	case FSTATE_DNOTFOUND:
	case FSTATE_DSTAR:
		printf(" disable");
		break;
	default:
		printf(" ???");
		break;
	}
	l = cd->cf_loc;
	ln = cd->cf_locnames;
	while (locnamp[ln] != -1) {
		printf(" %s ", locnames[locnamp[ln]]);
		ln++;
		userconf_pnum(*l++);
	}
	printf(" flags 0x%x\n", cd->cf_flags);
}

int
userconf_number(char *c, long *val, long limit)
{
	u_long num = 0;
	int neg = 0;
	int base = 10;

	if (*c == '-') {
		neg = 1;
		c++;
	}
	if (*c == '0') {
		base = 8;
		c++;
		if (*c == 'x' || *c == 'X') {
			base = 16;
			c++;
		}
	}
	while (*c != '\n' && *c != '\t' && *c != ' ' && *c != '\0') {
		u_char cc = *c;

		if (cc >= '0' && cc <= '9')
			cc = cc - '0';
		else if (cc >= 'a' && cc <= 'f')
			cc = cc - 'a' + 10;
		else if (cc >= 'A' && cc <= 'F')
			cc = cc - 'A' + 10;
		else
			return (-1);

		if (cc > base)
			return (-1);
		num = num * base + cc;
		c++;
	}

	if (neg && num > limit)	/* overflow */
		return (1);
	*val = neg ? - num : num;
	return (0);
}

int
userconf_device(char *cmd, long *len, short *unit, short *state)
{
	short u = 0, s = FSTATE_FOUND;
	int l = 0;
	char *c;

	c = cmd;
	while (*c >= 'a' && *c <= 'z') {
		l++;
		c++;
	}
	if (*c == '*') {
		s = FSTATE_STAR;
		c++;
	} else {
		while (*c >= '0' && *c <= '9') {
			s = FSTATE_NOTFOUND;
			u = u*10 + *c - '0';
			c++;
		}
	}
	while (*c == ' ' || *c == '\t' || *c == '\n')
		c++;

	if (*c == '\0') {
		*len = l;
		*unit = u;
		*state = s;
		return(0);
	}

	return(-1);
}

int
userconf_attr(char *cmd, long *val)
{
	char *c;
	short attr = -1, i = 0, l = 0;

	c = cmd;
	while (*c != ' ' && *c != '\t' && *c != '\n' && *c != '\0') {
		c++;
		l++;
	}

	while (i <= userconf_maxlocnames) {
		if (strlen(locnames[i]) == l) {
			if (strncasecmp(cmd, locnames[i], l) == 0)
				attr = i;
		}
		i++;
	}

	if (attr == -1) {
		return (-1);
	}

	*val = attr;

	return(0);
}

void
userconf_modify(char *item, long *val, long limit)
{
	int ok = 0;
	long a;
	char *c;
	int i;

	while (!ok) {
		printf("%s [", item);
		userconf_pnum(*val);
		printf("] ? ");

		i = getsn(userconf_argbuf, sizeof(userconf_argbuf));

		c = userconf_argbuf;
		while (*c == ' ' || *c == '\t' || *c == '\n') c++;

		if (*c != '\0') {
			if (userconf_number(c, &a, limit) == 0) {
				*val = a;
				ok = 1;
			} else {
				printf("Unknown argument\n");
			}
		} else {
			ok = 1;
		}
	}
}

void
userconf_change(int devno)
{
	struct cfdata *cd;
	char c = '\0';
	long  *l, tmp;
	int   ln;

	if (devno <=  userconf_maxdev) {
		userconf_pdev(devno);

		while (c != 'y' && c != 'Y' && c != 'n' && c != 'N') {
			printf("change (y/n) ?");
			c = cngetc();
			printf("\n");
		}

		if (c == 'y' || c == 'Y') {
			int share = 0, i, lklen;
			long *lk;

			/* XXX add cmd 'c' <devno> */
			userconf_hist_cmd('c');
			userconf_hist_int(devno);

			cd = &cfdata[devno];
			l = cd->cf_loc;
			ln = cd->cf_locnames;

			/*
			 * Search for some other driver sharing this
			 * locator table. if one does, we may need to
			 * replace the locators with a malloc'd copy.
			 */
			for (i = 0; cfdata[i].cf_driver; i++)
				if (i != devno && cfdata[i].cf_loc == l)
					share = 1;
			if (share) {
				for (i = 0; locnamp[ln+i] != -1 ; i++)
					;
				lk = l = mallocarray(i, sizeof(long),
				    M_TEMP, M_NOWAIT);
				if (lk == NULL) {
					printf("out of memory.\n");
					return;
				}
				lklen = i * sizeof(long);
				bcopy(cd->cf_loc, l, lklen);
			}

			while (locnamp[ln] != -1) {
				userconf_modify(locnames[locnamp[ln]], l,
				    LONG_MAX);

				/* XXX add *l */
				userconf_hist_int(*l);

				ln++;
				l++;
			}
			tmp = cd->cf_flags;
			userconf_modify("flags", &tmp, INT_MAX);
			userconf_hist_int(tmp);
			cd->cf_flags = tmp;

			if (share) {
				if (memcmp(cd->cf_loc, lk, lklen))
					cd->cf_loc = lk;
				else
					free(lk, M_TEMP, lklen);
			}

			printf("%3d ", devno);
			userconf_pdevnam(devno);
			printf(" changed\n");
			userconf_pdev(devno);
		}
		return;
	}

	if (devno > userconf_maxdev && devno <= userconf_totdev) {
		printf("%3d can't change free slot\n", devno);
		return;
	}

	if (devno > userconf_totdev &&
	    devno <= userconf_totdev+pdevnames_size) {
		userconf_pdev(devno);
		while (c != 'y' && c != 'Y' && c != 'n' && c != 'N') {
			printf("change (y/n) ?");
			c = cngetc();
			printf("\n");
		}

		if (c == 'y' || c == 'Y') {
			/* XXX add cmd 'c' <devno> */
			userconf_hist_cmd('c');
			userconf_hist_int(devno);

			tmp = pdevinit[devno-userconf_totdev-1].pdev_count;
			userconf_modify("count", &tmp, INT_MAX);
			userconf_hist_int(tmp);
			pdevinit[devno-userconf_totdev-1].pdev_count = tmp;

			printf("%3d %s changed\n", devno,
			    pdevnames[devno-userconf_totdev-1]);
			userconf_pdev(devno);

			/* XXX add eoc */
			userconf_hist_eoc();
		}
		return;
	}

	printf("Unknown devno (max is %d)\n", userconf_totdev+pdevnames_size);
}

void
userconf_disable(int devno)
{
	int done = 0;

	if (devno <= userconf_maxdev) {
		switch (cfdata[devno].cf_fstate) {
		case FSTATE_NOTFOUND:
			cfdata[devno].cf_fstate = FSTATE_DNOTFOUND;
			break;
		case FSTATE_STAR:
			cfdata[devno].cf_fstate = FSTATE_DSTAR;
			break;
		case FSTATE_DNOTFOUND:
		case FSTATE_DSTAR:
			done = 1;
			break;
		default:
			printf("Error unknown state\n");
			break;
		}

		printf("%3d ", devno);
		userconf_pdevnam(devno);
		if (done) {
			printf(" already");
		} else {
			/* XXX add cmd 'd' <devno> eoc */
			userconf_hist_cmd('d');
			userconf_hist_int(devno);
			userconf_hist_eoc();
		}
		printf(" disabled\n");

		return;
	}

	if (devno > userconf_maxdev && devno <= userconf_totdev) {
		printf("%3d can't disable free slot\n", devno);
		return;
	}

	if (devno > userconf_totdev &&
	    devno <= userconf_totdev+pdevnames_size) {
		printf("%3d %s", devno, pdevnames[devno-userconf_totdev-1]);
		if (pdevinit[devno-userconf_totdev-1].pdev_count < 1) {
			printf(" already ");
		} else {
			pdevinit[devno-userconf_totdev-1].pdev_count *= -1;
			/* XXX add cmd 'd' <devno> eoc */
			userconf_hist_cmd('d');
			userconf_hist_int(devno);
			userconf_hist_eoc();
		}
		printf(" disabled\n");
		return;
	}

	printf("Unknown devno (max is %d)\n", userconf_totdev+pdevnames_size);
}

void
userconf_enable(int devno)
{
	int done = 0;

	if (devno <= userconf_maxdev) {
		switch (cfdata[devno].cf_fstate) {
		case FSTATE_DNOTFOUND:
			cfdata[devno].cf_fstate = FSTATE_NOTFOUND;
			break;
		case FSTATE_DSTAR:
			cfdata[devno].cf_fstate = FSTATE_STAR;
			break;
		case FSTATE_NOTFOUND:
		case FSTATE_STAR:
			done = 1;
			break;
		default:
			printf("Error unknown state\n");
			break;
		}

		printf("%3d ", devno);
		userconf_pdevnam(devno);
		if (done) {
			printf(" already");
		} else {
			/* XXX add cmd 'e' <devno> eoc */
			userconf_hist_cmd('e');
			userconf_hist_int(devno);
			userconf_hist_eoc();
		}
		printf(" enabled\n");
		return;
	}

	if (devno > userconf_maxdev && devno <= userconf_totdev) {
		printf("%3d can't enable free slot\n", devno);
		return;
	}

	if (devno > userconf_totdev &&
	    devno <= userconf_totdev+pdevnames_size) {
		printf("%3d %s", devno, pdevnames[devno-userconf_totdev-1]);
		if (pdevinit[devno-userconf_totdev-1].pdev_count > 0) {
			printf(" already");
		} else {
			pdevinit[devno-userconf_totdev-1].pdev_count *= -1;
			/* XXX add cmd 'e' <devno> eoc */
			userconf_hist_cmd('e');
			userconf_hist_int(devno);
			userconf_hist_eoc();
		}
		printf(" enabled\n");
		return;
	}

	printf("Unknown devno (max is %d)\n", userconf_totdev+pdevnames_size);
}

void
userconf_help(void)
{
	int j = 0, k;

	printf("command   args                description\n");
	while (*userconf_cmds[j] != '\0') {
		printf("%s", userconf_cmds[j]);
		k = strlen(userconf_cmds[j]);
		while (k < 10) {
			printf(" ");
			k++;
		}
		switch (*userconf_cmds[j+1]) {
		case 'L':
			printf("[count]             number of lines before more");
			break;
		case 'a':
			printf("dev                 add a device");
			break;
		case 'b':
			printf("8|10|16             base on large numbers");
			break;
		case 'c':
			printf("devno|dev           change devices");
			break;
#if defined(DDB)
		case 'D':
			printf("                    enter ddb");
			break;
#endif
		case 'd':
			printf("attr val|devno|dev  disable devices");
			break;
		case 'e':
			printf("attr val|devno|dev  enable devices");
			break;
		case 'f':
			printf("devno|dev           find devices");
			break;
		case 'h':
			printf("                    this message");
			break;
		case 'l':
			printf("                    list configuration");
			break;
		case 'q':
			printf("                    leave UKC");
			break;
		case 's':
			printf("[attr [val]]        "
			   "show attributes (or devices with an attribute)");
			break;
		case 'v':
			printf("                    toggle verbose booting");
			break;
		default:
			printf("                    don't know");
			break;
		}
		printf("\n");
		j += 2;
	}
}

void
userconf_list(void)
{
	int i = 0;

	userconf_cnt = 0;

	while (i <= (userconf_totdev+pdevnames_size)) {
		if (userconf_more())
			break;
		userconf_pdev(i++);
	}

	userconf_cnt = -1;
}

void
userconf_show(void)
{
	int i = 0;

	userconf_cnt = 0;

	while (i <= userconf_maxlocnames) {
		if (userconf_more())
			break;
		printf("%s\n", locnames[i++]);
	}

	userconf_cnt = -1;
}

void
userconf_common_attr_val(short attr, long *val, char routine)
{
	struct cfdata *cd;
	long  *l;
	int   ln;
	int i = 0, quit = 0;

	userconf_cnt = 0;

	while (i <= userconf_maxdev) {
		cd = &cfdata[i];
		l = cd->cf_loc;
		ln = cd->cf_locnames;
		while (locnamp[ln] != -1) {
			if (locnamp[ln] == attr) {
				if (val == NULL) {
					quit = userconf_more();
					userconf_pdev(i);
				} else {
					if (*val == *l) {
						quit = userconf_more();
						switch (routine) {
						case UC_ENABLE:
							userconf_enable(i);
							break;
						case UC_DISABLE:
							userconf_disable(i);
							break;
						case UC_SHOW:
							userconf_pdev(i);
							break;
						default:
							printf("Unknown routine /%c/\n",
							    routine);
							break;
						}
					}
				}
			}
			if (quit)
				break;
			ln++;
			l++;
		}
		if (quit)
			break;
		i++;
	}

	userconf_cnt = -1;
}

void
userconf_show_attr(char *cmd)
{
	char *c;
	short attr = -1, i = 0, l = 0;
	long a;

	c = cmd;
	while (*c != ' ' && *c != '\t' && *c != '\n' && *c != '\0') {
		c++;
		l++;
	}
	while (*c == ' ' || *c == '\t' || *c == '\n') {
		c++;
	}
	while (i <= userconf_maxlocnames) {
		if (strlen(locnames[i]) == l) {
			if (strncasecmp(cmd, locnames[i], l) == 0) {
				attr = i;
			}
		}
		i++;
	}

	if (attr == -1) {
		printf("Unknown attribute\n");
		return;
	}

	if (*c == '\0') {
		userconf_common_attr_val(attr, NULL, UC_SHOW);
	} else {
		if (userconf_number(c, &a, INT_MAX) == 0) {
			userconf_common_attr_val(attr, &a, UC_SHOW);
		} else {
			printf("Unknown argument\n");
		}
	}
}

void
userconf_common_dev(char *dev, int len, short unit, short state, char routine)
{
	int i = 0;

	switch (routine) {
	case UC_CHANGE:
		break;
	default:
		userconf_cnt = 0;
		break;
	}

	while (cfdata[i].cf_attach != NULL) {
		if (strlen(cfdata[i].cf_driver->cd_name) == len) {

			/*
			 * Ok, if device name is correct
			 *  If state == FSTATE_FOUND, look for "dev"
			 *  If state == FSTATE_STAR, look for "dev*"
			 *  If state == FSTATE_NOTFOUND, look for "dev0"
			 */
			if (strncasecmp(dev, cfdata[i].cf_driver->cd_name,
					len) == 0 &&
			    (state == FSTATE_FOUND ||
			     (state == FSTATE_STAR &&
			      (cfdata[i].cf_fstate == FSTATE_STAR ||
			       cfdata[i].cf_fstate == FSTATE_DSTAR)) ||
			     (state == FSTATE_NOTFOUND &&
			      cfdata[i].cf_unit == unit &&
			      (cfdata[i].cf_fstate == FSTATE_NOTFOUND ||
			       cfdata[i].cf_fstate == FSTATE_DNOTFOUND)))) {
				if (userconf_more())
					break;
				switch (routine) {
				case UC_CHANGE:
					userconf_change(i);
					break;
				case UC_ENABLE:
					userconf_enable(i);
					break;
				case UC_DISABLE:
					userconf_disable(i);
					break;
				case UC_FIND:
					userconf_pdev(i);
					break;
				default:
					printf("Unknown routine /%c/\n",
					    routine);
					break;
				}
			}
		}
		i++;
	}

	for (i = 0; i < pdevnames_size; i++) {
		if (strncasecmp(dev, pdevnames[i], len) == 0 &&
		    state == FSTATE_FOUND) {
			switch(routine) {
			case UC_CHANGE:
				userconf_change(userconf_totdev+1+i);
				break;
			case UC_ENABLE:
				userconf_enable(userconf_totdev+1+i);
				break;
			case UC_DISABLE:
				userconf_disable(userconf_totdev+1+i);
				break;
			case UC_FIND:
				userconf_pdev(userconf_totdev+1+i);
				break;
			default:
				printf("Unknown pseudo routine /%c/\n",routine);
				break;
			}
		}
	}

	switch (routine) {
	case UC_CHANGE:
		break;
	default:
		userconf_cnt = -1;
		break;
	}
}

void
userconf_common_attr(char *cmd, int attr, char routine)
{
	char *c;
	short l = 0;
	long a;

	c = cmd;
	while (*c != ' ' && *c != '\t' && *c != '\n' && *c != '\0') {
		c++;
		l++;
	}
	while (*c == ' ' || *c == '\t' || *c == '\n')
		c++;

	if (*c == '\0') {
		printf("Value missing for attribute\n");
		return;
	}

	if (userconf_number(c, &a, INT_MAX) == 0) {
		userconf_common_attr_val(attr, &a, routine);
	} else {
		printf("Unknown argument\n");
	}
}

void
userconf_add_read(char *prompt, char field, char *dev, int len, long *val)
{
	int ok = 0;
	long a;
	char *c;
	int i;

	*val = -1;

	while (!ok) {
		printf("%s ? ", prompt);

		i = getsn(userconf_argbuf, sizeof(userconf_argbuf));

		c = userconf_argbuf;
		while (*c == ' ' || *c == '\t' || *c == '\n')
			c++;

		if (*c != '\0') {
			if (userconf_number(c, &a, INT_MAX) == 0) {
				if (a > userconf_maxdev) {
					printf("Unknown devno (max is %d)\n",
					    userconf_maxdev);
				} else if (strncasecmp(dev,
				    cfdata[a].cf_driver->cd_name, len) != 0 &&
				    field == 'a') {
					printf("Not same device type\n");
				} else {
					*val = a;
					ok = 1;
				}
			} else if (*c == '?') {
				userconf_common_dev(dev, len, 0,
				    FSTATE_FOUND, UC_FIND);
			} else if (*c == 'q' || *c == 'Q') {
				ok = 1;
			} else {
				printf("Unknown argument\n");
			}
		} else {
			ok = 1;
		}
	}
}

void
userconf_add(char *dev, int len, short unit, short state)
{
	int found = 0;
	struct cfdata new;
	int max_unit, star_unit;
	long i = 0, val, orig;

	memset(&new, 0, sizeof(struct cfdata));

	if (userconf_maxdev == userconf_totdev) {
		printf("No more space for new devices.\n");
		return;
	}

	if (state == FSTATE_FOUND) {
		printf("Device not complete number or * is missing\n");
		return;
	}

	for (i = 0; cfdata[i].cf_driver; i++)
		if (strlen(cfdata[i].cf_driver->cd_name) == len &&
		    strncasecmp(dev, cfdata[i].cf_driver->cd_name, len) == 0)
			found = 1;

	if (!found) {
		printf("No device of this type exists.\n");
		return;
	}

	userconf_add_read("Clone Device (DevNo, 'q' or '?')",
	    'a', dev, len, &val);

	if (val != -1) {
		orig = val;
		new = cfdata[val];
		new.cf_unit = unit;
		new.cf_fstate = state;
		userconf_add_read("Insert before Device (DevNo, 'q' or '?')",
		    'i', dev, len, &val);
	}

	if (val != -1) {
		/* XXX add cmd 'a' <orig> <val> eoc */
		userconf_hist_cmd('a');
		userconf_hist_int(orig);
		userconf_hist_int(unit);
		userconf_hist_int(state);
		userconf_hist_int(val);
		userconf_hist_eoc();

		/* Insert the new record */
		for (i = userconf_maxdev; val <= i; i--)
			cfdata[i+1] = cfdata[i];
		cfdata[val] = new;

		/* Fix indexs in pv */
		for (i = 0; i < pv_size; i++) {
			if (pv[i] != -1 && pv[i] >= val)
				pv[i]++;
		}

		/* Fix indexs in cfroots */
		for (i = 0; i < cfroots_size; i++) {
			if (cfroots[i] != -1 && cfroots[i] >= val)
				cfroots[i]++;
		}

		userconf_maxdev++;

		max_unit = -1;

		/* Find max unit number of the device type */

		i = 0;
		while (cfdata[i].cf_attach != NULL) {
			if (strlen(cfdata[i].cf_driver->cd_name) == len &&
			    strncasecmp(dev, cfdata[i].cf_driver->cd_name,
			    len) == 0) {
				switch (cfdata[i].cf_fstate) {
				case FSTATE_NOTFOUND:
				case FSTATE_DNOTFOUND:
					if (cfdata[i].cf_unit > max_unit)
						max_unit = cfdata[i].cf_unit;
					break;
				default:
					break;
				}
			}
			i++;
		}

		/*
		 * For all * entries set unit number to max+1, and update
		 * cf_starunit1 if necessary.
		 */
		max_unit++;
		star_unit = -1;

		i = 0;
		while (cfdata[i].cf_attach != NULL) {
			if (strlen(cfdata[i].cf_driver->cd_name) == len &&
			    strncasecmp(dev, cfdata[i].cf_driver->cd_name,
			    len) == 0) {
				switch (cfdata[i].cf_fstate) {
				case FSTATE_NOTFOUND:
				case FSTATE_DNOTFOUND:
					if (cfdata[i].cf_unit > star_unit)
						star_unit = cfdata[i].cf_unit;
					break;
				default:
					break;
				}
			}
			i++;
		}
		star_unit++;

		i = 0;
		while (cfdata[i].cf_attach != NULL) {
			if (strlen(cfdata[i].cf_driver->cd_name) == len &&
			    strncasecmp(dev, cfdata[i].cf_driver->cd_name,
			    len) == 0) {
				switch (cfdata[i].cf_fstate) {
				case FSTATE_STAR:
				case FSTATE_DSTAR:
					cfdata[i].cf_unit = max_unit;
					if (cfdata[i].cf_starunit1 < star_unit)
						cfdata[i].cf_starunit1 =
						    star_unit;
					break;
				default:
					break;
				}
			}
			i++;
		}
		userconf_pdev(val);
	}

	/* cf_attach, cf_driver, cf_unit, cf_fstate, cf_loc, cf_flags,
	   cf_parents, cf_locnames, and cf_locnames */
}

int
userconf_parse(char *cmd)
{
	char *c, *v;
	int i = 0, j = 0, k;
	long a;
	short unit, state;

	c = cmd;
	while (*c == ' ' || *c == '\t')
		c++;
	v = c;
	while (*c != ' ' && *c != '\t' && *c != '\n' && *c != '\0') {
		c++;
		i++;
	}

	k = -1;
	while (*userconf_cmds[j] != '\0') {
		if (strlen(userconf_cmds[j]) == i) {
			if (strncasecmp(v, userconf_cmds[j], i) == 0)
				k = j;
		}
		j += 2;
	}

	while (*c == ' ' || *c == '\t' || *c == '\n')
		c++;

	if (k == -1) {
		if (*v != '\n')
			printf("Unknown command, try help\n");
	} else {
		switch (*userconf_cmds[k+1]) {
		case 'L':
			if (*c == '\0')
				printf("Argument expected\n");
			else if (userconf_number(c, &a, INT_MAX) == 0)
				userconf_lines = a;
			else
				printf("Unknown argument\n");
			break;
		case 'a':
			if (*c == '\0')
				printf("Dev expected\n");
			else if (userconf_device(c, &a, &unit, &state) == 0)
				userconf_add(c, a, unit, state);
			else
				printf("Unknown argument\n");
			break;
		case 'b':
			if (*c == '\0')
				printf("8|10|16 expected\n");
			else if (userconf_number(c, &a, INT_MAX) == 0) {
				if (a == 8 || a == 10 || a == 16) {
					userconf_base = a;
				} else {
					printf("8|10|16 expected\n");
				}
			} else
				printf("Unknown argument\n");
			break;
		case 'c':
			if (*c == '\0')
				printf("DevNo or Dev expected\n");
			else if (userconf_number(c, &a, INT_MAX) == 0)
				userconf_change(a);
			else if (userconf_device(c, &a, &unit, &state) == 0)
				userconf_common_dev(c, a, unit, state, UC_CHANGE);
			else
				printf("Unknown argument\n");
			break;
#if defined(DDB)
		case 'D':
			db_enter();
			break;
#endif
		case 'd':
			if (*c == '\0')
				printf("Attr, DevNo or Dev expected\n");
			else if (userconf_attr(c, &a) == 0)
				userconf_common_attr(c, a, UC_DISABLE);
			else if (userconf_number(c, &a, INT_MAX) == 0)
				userconf_disable(a);
			else if (userconf_device(c, &a, &unit, &state) == 0)
				userconf_common_dev(c, a, unit, state, UC_DISABLE);
			else
				printf("Unknown argument\n");
			break;
		case 'e':
			if (*c == '\0')
				printf("Attr, DevNo or Dev expected\n");
			else if (userconf_attr(c, &a) == 0)
				userconf_common_attr(c, a, UC_ENABLE);
			else if (userconf_number(c, &a, INT_MAX) == 0)
				userconf_enable(a);
			else if (userconf_device(c, &a, &unit, &state) == 0)
				userconf_common_dev(c, a, unit, state, UC_ENABLE);
			else
				printf("Unknown argument\n");
			break;
		case 'f':
			if (*c == '\0')
				printf("DevNo or Dev expected\n");
			else if (userconf_number(c, &a, INT_MAX) == 0)
				userconf_pdev(a);
			else if (userconf_device(c, &a, &unit, &state) == 0)
				userconf_common_dev(c, a, unit, state, UC_FIND);
			else
				printf("Unknown argument\n");
			break;
		case 'h':
			userconf_help();
			break;
		case 'l':
			if (*c == '\0')
				userconf_list();
			else
				printf("Unknown argument\n");
			break;
		case 'q':
			/* XXX add cmd 'q' eoc */
			userconf_hist_cmd('q');
			userconf_hist_eoc();
			return(-1);
			break;
		case 's':
			if (*c == '\0')
				userconf_show();
			else
				userconf_show_attr(c);
			break;
		case 'v':
			autoconf_verbose = !autoconf_verbose;
			printf("autoconf verbose %sabled\n",
			    autoconf_verbose ? "en" : "dis");
			break;
		default:
			printf("Unknown command\n");
			break;
		}
	}
	return(0);
}

void
user_config(void)
{
	userconf_init();
	printf("User Kernel Config\n");

	cnpollc(1);
	while (1) {
		printf("UKC> ");
		if (getsn(userconf_cmdbuf, sizeof(userconf_cmdbuf)) > 0 &&
		    userconf_parse(userconf_cmdbuf))
			break;
	}
	cnpollc(0);

	printf("Continuing...\n");
}
