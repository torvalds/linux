/*	$OpenBSD: ukcutil.c,v 1.28 2025/05/19 19:58:58 tedu Exp $ */

/*
 * Copyright (c) 1999-2001 Mats O Jansson.  All rights reserved.
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

#include <sys/types.h>
#include <sys/device.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <nlist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd.h"
#include "exec.h"
#include "ukc.h"
#include "misc.h"

extern	int ukc_mod_kernel;

struct	cfdata *
get_cfdata(int idx)
{
	return((struct cfdata *)(adjust((caddr_t)nl[P_CFDATA].n_value) +
	    idx * sizeof(struct cfdata)));
}

short *
get_locnamp(int idx)
{
	return((short *)(adjust((caddr_t)nl[S_LOCNAMP].n_value) +
	    idx * sizeof(short)));
}

static caddr_t *
get_locnames(int idx)
{
	return((caddr_t *)(adjust((caddr_t)nl[P_LOCNAMES].n_value) +
	    idx * sizeof(caddr_t)));
}

static long *
get_extraloc(int nslots)
{
	long *extralocp, *locp;
	int *rextralocp, *textralocp;

	extralocp = (long *)adjust((caddr_t)nl[IA_EXTRALOC].n_value);
	rextralocp = (int *)adjust((caddr_t)nl[I_REXTRALOC].n_value);
	textralocp = (int *)adjust((caddr_t)nl[I_TEXTRALOC].n_value);
	if (*rextralocp < nslots)
		return (NULL);

	locp = &extralocp[*textralocp - *rextralocp];
	*rextralocp -= nslots;
	return locp;
}

static char *
get_pdevnames(int idx)
{
	caddr_t *p;

	p = (caddr_t *)adjust((caddr_t)nl[P_PDEVNAMES].n_value +
	    idx * sizeof(caddr_t));
	return(char *)adjust((caddr_t)*p);

}

static struct pdevinit *
get_pdevinit(int idx)
{
	return((struct pdevinit *)(adjust((caddr_t)nl[S_PDEVINIT].n_value) +
	    idx * sizeof(struct pdevinit)));
}

int
more(void)
{
	int quit = 0;
	cmd_t cmd;

	if (cnt != -1) {
		if (cnt > 0 && cnt == lines) {
			printf("--- more ---");
			fflush(stdout);
			ask_cmd(&cmd);
			cnt = 0;
			if (cmd.cmd[0] == 'q' || cmd.cmd[0] == 'Q')
				quit = 1;
		}
		cnt++;
	}
	return (quit);
}

static void
pnum(long val)
{
	if (val > -2 && val < 16) {
		printf("%ld", val);
		return;
	}

	switch (base) {
	case 8:
		printf("0%lo", val);
		break;
	case 10:
		printf("%ld", val);
		break;
	case 16:
	default:
		printf("0x%lx", val);
		break;
	}
}

static void
pdevnam(short devno)
{
	struct cfdata *cd;
	struct cfdriver *cdrv;

	cd = get_cfdata(devno);

	cdrv = (struct cfdriver *)adjust((caddr_t)cd->cf_driver);
	printf("%s", adjust((caddr_t)cdrv->cd_name));

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
pdev(short devno)
{
	struct pdevinit *pi;
	struct cfdata *cd;
	short	*s, *ln;
	long	*i;
	caddr_t	*p;
	char	c;

	if (nopdev == 0) {
		if (devno > maxdev && devno <= totdev) {
			printf("%3d free slot (for add)\n", devno);
			return;
		}
		if (devno > totdev && devno <= totdev + maxpseudo) {
			pi = get_pdevinit(devno - totdev -1);
			printf("%3d %s count %d", devno,
			    get_pdevnames(devno - totdev - 1),
			    abs(pi->pdev_count));
			if (pi->pdev_count < 0)
				printf(" disable");
			printf(" (pseudo device)\n");
			return;
		}
	}

	if (devno > maxdev) {
		printf("Unknown devno (max is %d)\n", maxdev);
		return;
	}

	cd = get_cfdata(devno);

	printf("%3d ", devno);
	pdevnam(devno);
	printf(" at");

	c = ' ';
	s = (short *)adjust((caddr_t)cd->cf_parents);
	if (*s == -1)
		printf(" root");
	while (*s != -1) {
		printf("%c", c);
		pdevnam(*s);
		c = '|';
		s++;
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

	i = (long *)adjust((caddr_t)cd->cf_loc);
	ln = get_locnamp(cd->cf_locnames);
	while (*ln != -1) {
		p = get_locnames(*ln);
		printf(" %s ", adjust((caddr_t)*p));
		ln++;
		pnum(*i);
		i++;
	}
	printf(" flags 0x%x\n", cd->cf_flags);
}

static int
numberl(const char *c, long *val)
{
	char *ep;

	errno = 0;
	*val = strtol(c, &ep, 0);
	if (*c == '\0' || (!isspace((unsigned char)*ep) && *ep != '\0') ||
	    (errno == ERANGE && (*val == LONG_MAX || *val == LONG_MIN)))
		return (-1);
	return (0);
}

int
number(const char *c, int *val)
{
	long v;
	int ret = numberl(c, &v);

	if (ret == 0) {
		if (v <= INT_MAX && v >= INT_MIN)
			*val = (int)v;
		else
			ret = -1;
	}
	return (ret);
}

int
device(char *cmd, int *len, short *unit, short *state)
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
attr(char *cmd, int *val)
{
	short attr = -1, i = 0, l = 0;
	caddr_t *p;
	char *c;

	c = cmd;
	while (*c != ' ' && *c != '\t' && *c != '\n' && *c != '\0') {
		c++;
		l++;
	}

	p = get_locnames(0);

	while (i <= maxlocnames) {
		if (strlen((char *)adjust((caddr_t)*p)) == l) {
			if (strncasecmp(cmd, adjust((caddr_t)*p), l) == 0)
				attr = i;
		}
		p++;
		i++;
	}
	if (attr == -1)
		return (-1);

	*val = attr;
	return(0);
}

static int
modifyl(char *item, long *val)
{
	cmd_t cmd;
	long a;

	ukc_mod_kernel = 1;
	while (1) {
		printf("%s [", item);
		pnum(*val);
		printf("] ? ");
		fflush(stdout);

		ask_cmd(&cmd);

		if (strlen(cmd.cmd) != 0) {
			if (strlen(cmd.args) == 0) {
				if (numberl(cmd.cmd, &a) == 0) {
					*val = a;
					return (1);
				} else
					printf("Unknown argument\n");
			} else
				printf("Too many arguments\n");
		} else
			return (0);
	}
}

void
modify(char *item, int *val)
{
	long a = *val;

	while (modifyl(item, &a)) {
		if (a <= INT_MAX && a >= INT_MIN) {
			*val = (int)a;
			break;
		}
		printf("Out of range argument\n");
	}
}

void
change(int devno)
{
	int	i, share = 0;
	long	*j = NULL, *l = NULL;
	struct cfdata *cd, *c;
	struct pdevinit *pi;
	short	*ln, *lk;
	caddr_t	*p;

	ukc_mod_kernel = 1;
	if (devno <=  maxdev) {
		pdev(devno);
		if (!ask_yn("change"))
			return;

		cd = get_cfdata(devno);

		/*
		 * Search for some other driver sharing this
		 * locator table. if one does, we may need to
		 * replace the locators with a new copy.
		 */
		c = get_cfdata(0);
		for (i = 0; c->cf_driver; i++) {
			if (i != devno && c->cf_loc == cd->cf_loc)
				share = 1;
			c++;
		}

		ln = get_locnamp(cd->cf_locnames);
		l = (long *)adjust((caddr_t)cd->cf_loc);

		if (share) {
			if (oldkernel) {
				printf("Can't do that on this kernel\n");
				return;
			}

			lk = ln;
			i = 0;
			while (*lk != -1) {
				lk++;
				i++;
			}
			lk = ln;

			j = l = get_extraloc(i);
			if (l == NULL) {
				printf("Not enough space to change device.\n");
				return;
			}
			if (i)
				bcopy(adjust((caddr_t)cd->cf_loc), l,
				    sizeof(long) * i);
		}

		while (*ln != -1) {
			p = get_locnames(*ln);
			modifyl((char *)adjust(*p), l);
			ln++;
			l++;
		}
		modify("flags", &cd->cf_flags);

		if (share) {
			if (bcmp(adjust((caddr_t)cd->cf_loc),
			    j, sizeof(long) * i)) {
				cd->cf_loc = (long *)readjust((caddr_t)j);
			}
		}

		printf("%3d ", devno);
		pdevnam(devno);
		printf(" changed\n");
		pdev(devno);
		return;
	}

	if (nopdev == 0) {
		if (devno > maxdev && devno <= totdev) {
			printf("%3d can't change free slot\n", devno);
			return;
		}

		if (devno > totdev && devno <= totdev + maxpseudo) {
			pdev(devno);
			if (ask_yn("change")) {
				pi = get_pdevinit(devno-totdev-1);
				modify("count", &pi->pdev_count);
				printf("%3d %s changed\n", devno,
				    get_pdevnames(devno - totdev - 1));
				pdev(devno);
			}
			return;
		}
	}

	printf("Unknown devno (max is %d)\n", totdev+maxpseudo);
}

void
change_history(int devno, char *str)
{
	int	i, share = 0;
	long	*j = NULL, *l = NULL;
	struct cfdata *cd, *c;
	struct pdevinit *pi;
	short	*ln, *lk;

	ukc_mod_kernel = 1;

	if (devno <= maxdev) {

		pdev(devno);
		cd = get_cfdata(devno);

		/*
		 * Search for some other driver sharing this
		 * locator table. if one does, we may need to
		 * replace the locators with a new copy.
		 */
		c = get_cfdata(0);
		for (i = 0; c->cf_driver; i++) {
			if (i != devno && c->cf_loc == cd->cf_loc)
				share = 1;
			c++;
		}

		ln = get_locnamp(cd->cf_locnames);
		l = (long *)adjust((caddr_t)cd->cf_loc);

		if (share) {
			if (oldkernel) {
				printf("Can't do that on this kernel\n");
				return;
			}

			lk = ln;
			i = 0;
			while (*lk != -1) {
				lk++;
				i++;
			}
			lk = ln;

			j = l = get_extraloc(i);
			if (l == NULL) {
				printf("Not enough space to change device.\n");
				return;
			}
			if (i)
				bcopy(adjust((caddr_t)cd->cf_loc), l,
				    sizeof(long) * i);
		}

		while (*ln != -1) {
			*l = atoi(str);
			if (*str == '-')
				str++;
			while (*str >= '0' && *str <= '9')
				str++;
			if (*str == ' ')
				str++;
			ln++;
			l++;
		}

		if (*str) {
			cd->cf_flags = atoi(str);
			if (*str == '-')
				str++;
			while (*str >= '0' && *str <= '9')
				str++;
			if (*str == ' ')
				str++;
		}

		if (share) {
			if (bcmp(adjust((caddr_t)cd->cf_loc),
			    j, sizeof(long) * i)) {
				cd->cf_loc = (long *)readjust((caddr_t)j);
			}
		}

		printf("%3d ", devno);
		pdevnam(devno);
		printf(" changed\n");
		pdev(devno);
		return;
	}

	if (nopdev == 0) {
		if (devno > maxdev && devno <= totdev) {
			printf("%3d can't change free slot\n", devno);
			return;
		}
		if (devno > totdev && devno <= totdev + maxpseudo) {
			pdev(devno);
			pi = get_pdevinit(devno-totdev-1);

			if (*str) {
				pi->pdev_count = atoi(str);
				if (*str == '-')
					str++;
				while (*str >= '0' && *str <= '9')
					str++;
				if (*str == ' ')
					str++;
			}

			printf("%3d %s changed\n", devno,
			    get_pdevnames(devno - totdev - 1));
			pdev(devno);
			return;
		}
	}

	printf("Unknown devno (max is %d)\n", totdev + maxpseudo);
}

void
disable(int devno)
{
	struct cfdata *cd;
	struct pdevinit *pi;
	int done = 0;

	if (devno <= maxdev) {

		cd = get_cfdata(devno);

		switch (cd->cf_fstate) {
		case FSTATE_NOTFOUND:
			cd->cf_fstate = FSTATE_DNOTFOUND;
			break;
		case FSTATE_STAR:
			cd->cf_fstate = FSTATE_DSTAR;
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
		pdevnam(devno);
		if (done) {
			printf(" already");
		} else {
			ukc_mod_kernel = 1;
		}
		printf(" disabled\n");

		return;
	}

	if (nopdev == 0) {
		if (devno > maxdev && devno <= totdev) {
			printf("%3d can't disable free slot\n", devno);
			return;
		}
		if (devno > totdev && devno <= totdev + maxpseudo) {
			pi = get_pdevinit(devno-totdev-1);

			printf("%3d %s", devno,
				get_pdevnames(devno - totdev - 1));
			if (pi->pdev_count < 1) {
				printf(" already");
			} else {
				ukc_mod_kernel = 1;
				pi->pdev_count*=-1;
			}
			printf(" disabled\n");
			return;
		}
	}

	printf("Unknown devno (max is %d)\n", totdev+maxpseudo);

}

void
enable(int devno)
{
	struct cfdata *cd;
	struct pdevinit *pi;
	int done = 0;

	if (devno <= maxdev) {
		cd = get_cfdata(devno);

		switch (cd->cf_fstate) {
		case FSTATE_DNOTFOUND:
			cd->cf_fstate = FSTATE_NOTFOUND;
			break;
		case FSTATE_DSTAR:
			cd->cf_fstate = FSTATE_STAR;
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
		pdevnam(devno);
		if (done) {
			printf(" already");
		} else {
			ukc_mod_kernel = 1;
		}
		printf(" enabled\n");

		return;
	}

	if (nopdev == 0) {
		if (devno > maxdev && devno <= totdev) {
			printf("%3d can't enable free slot\n", devno);
			return;
		}
		if (devno > totdev && devno <= totdev + maxpseudo) {
			pi = get_pdevinit(devno-totdev-1);

			printf("%3d %s", devno,
				get_pdevnames(devno - totdev - 1));
			if (pi->pdev_count > 0) {
				printf(" already");
			} else {
				ukc_mod_kernel = 1;
				pi->pdev_count*=-1;
			}
			printf(" enabled\n");
			return;
		}
	}

	printf("Unknown devno (max is %d)\n", totdev+maxpseudo);
}

void
show(void)
{
	caddr_t *p;
	int	i = 0;

	cnt = 0;

	p = get_locnames(0);

	while (i <= maxlocnames) {
		if (more())
			break;
		printf("%s\n", (char *)adjust(*p));
		p++;
		i++;
	}

	cnt = -1;
}

void
common_attr_val(short attr, int *val, char routine)
{
	int	i = 0;
	struct cfdata *cd;
	long	*l;
	short *ln;
	int quit = 0;

	cnt = 0;

	cd = get_cfdata(0);

	while (cd->cf_attach != 0) {
		l = (long *)adjust((caddr_t)cd->cf_loc);
		ln = get_locnamp(cd->cf_locnames);
		while (*ln != -1) {
			if (*ln == attr) {
				if (val == NULL) {
					quit = more();
					pdev(i);
				} else {
					if (*val == *l) {
						quit = more();
						switch (routine) {
						case UC_ENABLE:
							enable(i);
							break;
						case UC_DISABLE:
							disable(i);
							break;
						case UC_SHOW:
							pdev(i);
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
		cd++;
	}

	cnt = -1;
}

void
show_attr(char *cmd)
{
	char *c;
	caddr_t *p;
	short attr = -1, i = 0, l = 0;
	int a;

	c = cmd;
	while (*c != ' ' && *c != '\t' && *c != '\n' && *c != '\0') {
		c++;
		l++;
	}
	while (*c == ' ' || *c == '\t' || *c == '\n') {
		c++;
	}

	p = get_locnames(0);

	while (i <= maxlocnames) {
		if (strlen((char *)adjust(*p)) == l) {
			if (strncasecmp(cmd, adjust(*p), l) == 0)
				attr = i;
		}
		p++;
		i++;
	}

	if (attr == -1) {
		printf("Unknown attribute\n");
		return;
	}

	if (*c == '\0') {
		common_attr_val(attr, NULL, UC_SHOW);
	} else {
		if (number(c, &a) == 0) {
			common_attr_val(attr, &a, UC_SHOW);
		} else {
			printf("Unknown argument\n");
		}
	}
}

void
common_dev(char *dev, int len, short unit, short state, char routine)
{
	struct cfdata *cd;
	struct cfdriver *cdrv;
	int i = 0;

	switch (routine) {
	case UC_CHANGE:
		break;
	default:
		cnt = 0;
		break;
	}

	cnt = 0;

	cd = get_cfdata(0);

	while (cd->cf_attach != 0) {
		cdrv = (struct cfdriver *)adjust((caddr_t)cd->cf_driver);

		if (strlen((char *)adjust(cdrv->cd_name)) == len) {
			/*
			 * Ok, if device name is correct
			 *  If state == FSTATE_FOUND, look for "dev"
			 *  If state == FSTATE_STAR, look for "dev*"
			 *  If state == FSTATE_NOTFOUND, look for "dev0"
			 */
			if (!strncasecmp(dev,(char *)adjust(cdrv->cd_name), len) &&
			    (state == FSTATE_FOUND ||
			    (state == FSTATE_STAR &&
			    (cd->cf_fstate == FSTATE_STAR ||
			    cd->cf_fstate == FSTATE_DSTAR)) ||
			    (state == FSTATE_NOTFOUND &&
			    cd->cf_unit == unit &&
			    (cd->cf_fstate == FSTATE_NOTFOUND ||
			    cd->cf_fstate == FSTATE_DNOTFOUND)))) {
				if (more())
					break;
				switch (routine) {
				case UC_CHANGE:
					change(i);
					break;
				case UC_ENABLE:
					enable(i);
					break;
				case UC_DISABLE:
					disable(i);
					break;
				case UC_FIND:
					pdev(i);
					break;
				default:
					printf("Unknown routine /%c/\n",
					    routine);
					break;
				}
			}
		}
		i++;
		cd++;
	}

	if (nopdev == 0) {
		for (i = 0; i < maxpseudo; i++) {
			if (!strncasecmp(dev, (char *)get_pdevnames(i), len) &&
			    state == FSTATE_FOUND) {
				switch (routine) {
				case UC_CHANGE:
					change(totdev+1+i);
					break;
				case UC_ENABLE:
					enable(totdev+1+i);
					break;
				case UC_DISABLE:
					disable(totdev+1+i);
					break;
				case UC_FIND:
					pdev(totdev+1+i);
					break;
				default:
					printf("Unknown pseudo routine /%c/\n",
					    routine);
					break;
				}
			}
		}
	}

	switch (routine) {
	case UC_CHANGE:
		break;
	default:
		cnt = -1;
		break;
	}
}

void
common_attr(char *cmd, int attr, char routine)
{
	char *c;
	short l = 0;
	int a;

	c = cmd;
	while (*c != ' ' && *c != '\t' && *c != '\n' && *c != '\0') {
		c++;
		l++;
	}
	while (*c == ' ' || *c == '\t' || *c == '\n') {
		c++;
	}
	if (*c == '\0') {
		printf("Value missing for attribute\n");
		return;
	}

	if (number(c, &a) == 0) {
		common_attr_val(attr, &a, routine);
	} else {
		printf("Unknown argument\n");
	}
}

void
add_read(char *prompt, char field, char *dev, int len, int *val)
{
	int ok = 0;
	int a;
	cmd_t cmd;
	struct cfdata *cd;
	struct cfdriver *cdrv;

	*val = -1;

	while (!ok) {
		printf("%s ? ", prompt);
		fflush(stdout);

		ask_cmd(&cmd);

		if (strlen(cmd.cmd) != 0) {
			if (number(cmd.cmd, &a) == 0) {
				if (a > maxdev) {
					printf("Unknown devno (max is %d)\n",
					    maxdev);
				} else {
					cd = get_cfdata(a);
					cdrv = (struct cfdriver *)
					  adjust((caddr_t)cd->cf_driver);
					if (strncasecmp(dev,
							(char *)adjust(cdrv->cd_name),
							len) != 0 &&
					    field == 'a') {
						printf("Not same device type\n");
					} else {
						*val = a;
						ok = 1;
					}
				}
			} else if (cmd.cmd[0] == '?') {
				common_dev(dev, len, 0,
				    FSTATE_FOUND, UC_FIND);
			} else if (cmd.cmd[0] == 'q' ||
				   cmd.cmd[0] == 'Q') {
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
add(char *dev, int len, short unit, short state)
{
	int i = 0, found = 0, *p;
	short *pv;
	struct cfdata new, *cd, *cdp;
	struct cfdriver *cdrv;
	int  val, max_unit, star_unit;

	ukc_mod_kernel = 1;

	bzero(&new, sizeof(struct cfdata));

	if (maxdev == totdev) {
		printf("No more space for new devices.\n");
		return;
	}

	if (state == FSTATE_FOUND) {
		printf("Device not complete number or * is missing\n");
		return;
	}

	cd = get_cfdata(0);

	while (cd->cf_attach != 0) {
		cdrv = (struct cfdriver *)adjust((caddr_t)cd->cf_driver);

		if (strlen((char *)adjust(cdrv->cd_name)) == len &&
		    strncasecmp(dev, (char *)adjust(cdrv->cd_name), len) == 0)
			found = 1;
		cd++;
	}

	if (!found) {
		printf("No device of this type exists.\n");
		return;
	}

	add_read("Clone Device (DevNo, 'q' or '?')", 'a', dev, len, &val);

	if (val != -1) {
		cd = get_cfdata(val);
		new = *cd;
		new.cf_unit = unit;
		new.cf_fstate = state;
		add_read("Insert before Device (DevNo, 'q' or '?')",
		    'i', dev, len, &val);
	}

	if (val != -1) {

		/* Insert the new record */
		cdp = cd = get_cfdata(maxdev+1);
		cdp--;
		for (i = maxdev; val <= i; i--) {
			*cd-- = *cdp--;
		}
		cd = get_cfdata(val);
		*cd = new;

		/* Fix indexs in pv */
		p = (int *)adjust((caddr_t)nl[I_PV_SIZE].n_value);
		pv = (short *)adjust((caddr_t)nl[SA_PV].n_value);
		for (i = 0; i < *p; i++) {
			if (*pv != 1 && *pv >= val)
				*pv = *pv + 1;
			pv++;
		}

		/* Fix indexs in cfroots */
		p = (int *)adjust((caddr_t)nl[I_CFROOTS_SIZE].n_value);
		pv = (short *)adjust((caddr_t)nl[SA_CFROOTS].n_value);
		for (i = 0; i < *p; i++) {
			if (*pv != 1 && *pv >= val)
				*pv = *pv + 1;
			pv++;
		}

		maxdev++;

		max_unit = -1;

		/* Find max unit number of the device type */

		cd = get_cfdata(0);

		while (cd->cf_attach != 0) {
			cdrv = (struct cfdriver *)
			  adjust((caddr_t)cd->cf_driver);

			if (strlen((char *)adjust(cdrv->cd_name)) == len &&
			    strncasecmp(dev, (char *)adjust(cdrv->cd_name),
			    len) == 0) {
				switch (cd->cf_fstate) {
				case FSTATE_NOTFOUND:
				case FSTATE_DNOTFOUND:
					if (cd->cf_unit > max_unit)
						max_unit = cd->cf_unit;
					break;
				default:
					break;
				}
			}
			cd++;
		}

		/*
		 * For all * entries set unit number to max+1, and update
		 * cf_starunit1 if necessary.
		 */
		max_unit++;
		star_unit = -1;
		cd = get_cfdata(0);
		while (cd->cf_attach != 0) {
			cdrv = (struct cfdriver *)
			    adjust((caddr_t)cd->cf_driver);

			if (strlen((char *)adjust(cdrv->cd_name)) == len &&
			    strncasecmp(dev, (char *)adjust(cdrv->cd_name),
			    len) == 0) {
				switch (cd->cf_fstate) {
				case FSTATE_NOTFOUND:
				case FSTATE_DNOTFOUND:
					if (cd->cf_unit > star_unit)
						star_unit = cd->cf_unit;
					break;
				default:
					break;
				}
			}
			cd++;
		}
		star_unit++;

		cd = get_cfdata(0);
		while (cd->cf_attach != 0) {
			cdrv = (struct cfdriver *)
			    adjust((caddr_t)cd->cf_driver);

			if (strlen((char *)adjust(cdrv->cd_name)) == len &&
			    strncasecmp(dev, (char *)adjust(cdrv->cd_name),
			    len) == 0) {
				switch (cd->cf_fstate) {
				case FSTATE_STAR:
				case FSTATE_DSTAR:
					cd->cf_unit = max_unit;
					if (cd->cf_starunit1 < star_unit)
						cd->cf_starunit1 = star_unit;
					break;
				default:
					break;
				}
			}
			cd++;
		}

		pdev(val);
	}

	/* cf_attach, cf_driver, cf_unit, cf_fstate, cf_loc, cf_flags,
	   cf_parents, cf_locnames, cf_locnames and cf_ivstubs */
}

void
add_history(int devno, short unit, short state, int newno)
{
	int i = 0, *p;
	short *pv;
	struct cfdata new, *cd, *cdp;
	struct cfdriver *cdrv;
	int  val, max_unit;
	int  len;
	char *dev;

	ukc_mod_kernel = 1;

	bzero(&new, sizeof(struct cfdata));
	cd = get_cfdata(devno);
	new = *cd;
	new.cf_unit = unit;
	new.cf_fstate = state;

	val = newno;

	cdrv = (struct cfdriver *) adjust((caddr_t)cd->cf_driver);
	dev = adjust((caddr_t)cdrv->cd_name);
	len = strlen(dev);

	/* Insert the new record */
	cdp = cd = get_cfdata(maxdev+1);
	cdp--;
	for (i = maxdev; val <= i; i--)
		*cd-- = *cdp--;
	cd = get_cfdata(val);
	*cd = new;

	/* Fix indexs in pv */
	p = (int *)adjust((caddr_t)nl[I_PV_SIZE].n_value);
	pv = (short *)adjust((caddr_t)nl[SA_PV].n_value);
	for (i = 0; i < *p; i++) {
		if (*pv != 1 && *pv >= val)
			*pv = *pv + 1;
		pv++;
	}

	/* Fix indexs in cfroots */
	p = (int *)adjust((caddr_t)nl[I_CFROOTS_SIZE].n_value);
	pv = (short *)adjust((caddr_t)nl[SA_CFROOTS].n_value);
	for (i = 0; i < *p; i++) {
		if (*pv != 1 && *pv >= val)
			*pv = *pv + 1;
		pv++;
	}

	maxdev++;
	max_unit = -1;

	/* Find max unit number of the device type */
	cd = get_cfdata(0);
	while (cd->cf_attach != 0) {
		cdrv = (struct cfdriver *)
		    adjust((caddr_t)cd->cf_driver);

		if (strlen((char *)adjust(cdrv->cd_name)) == len &&
		    strncasecmp(dev, (char *)adjust(cdrv->cd_name),
		    len) == 0) {
			switch (cd->cf_fstate) {
			case FSTATE_NOTFOUND:
			case FSTATE_DNOTFOUND:
				if (cd->cf_unit > max_unit)
					max_unit = cd->cf_unit;
				break;
			default:
				break;
			}
		}
		cd++;
	}

	/* For all * entries set unit number to max+1 */
	max_unit++;
	cd = get_cfdata(0);
	while (cd->cf_attach != 0) {
		cdrv = (struct cfdriver *)
		    adjust((caddr_t)cd->cf_driver);

		if (strlen((char *)adjust(cdrv->cd_name)) == len &&
		    strncasecmp(dev, (char *)adjust(cdrv->cd_name),
		    len) == 0) {
			switch (cd->cf_fstate) {
			case FSTATE_STAR:
			case FSTATE_DSTAR:
				cd->cf_unit = max_unit;
				break;
			default:
				break;
			}
		}
		cd++;
	}

	printf("%3d ", newno);
	pdevnam(newno);
	printf(" added\n");
	pdev(val);
}

int
config(void)
{
	extern char *cmdfile;
	cmd_t cmd;
	int i, st;

	/* Set up command table pointer */
	cmd.table = cmd_table;

	if (cmdfile == NULL)
		printf("Enter 'help' for information\n");

	/* Edit cycle */
	do {
again:
		/* Get input */
		if (cmdfile == NULL) {
			printf("ukc> ");
			fflush(stdout);
		}
		ask_cmd(&cmd);

		if (cmd.cmd[0] == '\0') {
			if (cmdfile != NULL)
				return 1;
			goto again;
		}
		if (cmd.cmd[0] == '#')
			goto again;
		for (i = 0; cmd_table[i].cmd != NULL; i++)
			if (strstr(cmd_table[i].cmd, cmd.cmd) ==
			    cmd_table[i].cmd)
				break;

		/* Quick hack to put in '?' == 'help' */
		if (!strcmp(cmd.cmd, "?"))
			i = 0;

		/* Check for valid command */
		if (cmd_table[i].cmd == NULL) {
			if (cmdfile == NULL) {
				printf("Invalid command '%s'\n", cmd.cmd);
				exit(1);
			}
			printf("Invalid command '%s'.  Try 'help'.\n", cmd.cmd);
			continue;
		} else
			strlcpy(cmd.cmd, cmd_table[i].cmd, sizeof cmd.cmd);

		/* Call function */
		st = cmd_table[i].fcn(&cmd);

		/* Update status */
		if (st == CMD_EXIT)
			break;
		if (st == CMD_SAVE)
			break;
	} while (1);

	return (st == CMD_SAVE);
}

void
process_history(int len, char *buf)
{
	char *c;
	int devno, newno;
	short unit, state;

	if (len == 0) {
		printf("History is empty\n");
		return;
	}

	printf("Processing history...\n");

	buf[len] = 0;

	c = buf;

	while (*c != '\0') {
		switch (*c) {
		case 'a':
			c++;
			c++;
			devno = atoi(c);
			while (*c >= '0' && *c <= '9')
				c++;
			c++;
			unit = atoi(c);
			if (*c == '-') c++;
			while (*c >= '0' && *c <= '9')
				c++;
			c++;
			state = atoi(c);
			if (*c == '-')
				c++;
			while (*c >= '0' && *c <= '9')
				c++;
			c++;
			newno = atoi(c);
			while (*c >= '0' && *c <= '9')
				c++;
			add_history(devno, unit, state, newno);
			while (*c != '\n')
				c++;
			c++;
			break;
		case 'c':
			c++;
			c++;
			devno = atoi(c);
			while (*c >= '0' && *c <= '9')
				c++;
			if (*c == ' ')
				c++;
			if (*c != '\n')
				change_history(devno, c);
			while (*c != '\n')
				c++;
			c++;
			break;
		case 'd':
			c++;
			devno = atoi(c);
			disable(devno);
			while (*c != '\n')
				c++;
			c++;
			break;
		case 'e':
			c++;
			devno = atoi(c);
			enable(devno);
			while (*c != '\n')
				c++;
			c++;
			break;
		case 'q':
			while (*c != '\0')
				c++;
			break;
		default:
			printf("unknown command %c\n", *c);
			while (*c != '\0' && *c != '\n')
				c++;
			break;
		}
	}
}
