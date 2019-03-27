/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
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
 * $FreeBSD$
 */

#include "defs.h"
#include "pathnames.h"
#include <sys/stat.h>

#ifdef __NetBSD__
__RCSID("$NetBSD$");
#elif defined(__FreeBSD__)
__RCSID("$FreeBSD$");
#else
__RCSID("$Revision: 2.26 $");
#ident "$Revision: 2.26 $"
#endif


static struct parm *parms;
struct intnet *intnets;
struct r1net *r1nets;
struct tgate *tgates;


/* use configured parameters
 */
void
get_parms(struct interface *ifp)
{
	static int warned_auth_in, warned_auth_out;
	struct parm *parmp;
	int i, num_passwds = 0;

	/* get all relevant parameters
	 */
	for (parmp = parms; parmp != NULL; parmp = parmp->parm_next) {
		if (parmp->parm_name[0] == '\0'
		    || !strcmp(ifp->int_name, parmp->parm_name)
		    || (parmp->parm_name[0] == '\n'
			&& on_net(ifp->int_addr,
				  parmp->parm_net, parmp->parm_mask))) {

			/* This group of parameters is relevant,
			 * so get its settings
			 */
			ifp->int_state |= parmp->parm_int_state;
			for (i = 0; i < MAX_AUTH_KEYS; i++) {
				if (parmp->parm_auth[0].type == RIP_AUTH_NONE
				    || num_passwds >= MAX_AUTH_KEYS)
					break;
				memcpy(&ifp->int_auth[num_passwds++],
				       &parmp->parm_auth[i],
				       sizeof(ifp->int_auth[0]));
			}
			if (parmp->parm_rdisc_pref != 0)
				ifp->int_rdisc_pref = parmp->parm_rdisc_pref;
			if (parmp->parm_rdisc_int != 0)
				ifp->int_rdisc_int = parmp->parm_rdisc_int;
			if (parmp->parm_adj_inmetric != 0)
			    ifp->int_adj_inmetric = parmp->parm_adj_inmetric;
			if (parmp->parm_adj_outmetric != 0)
			    ifp->int_adj_outmetric = parmp->parm_adj_outmetric;
		}
	}

	/* Set general defaults.
	 *
	 * Default poor-man's router discovery to a metric that will
	 * be heard by old versions of `routed`.  They ignored received
	 * routes with metric 15.
	 */
	if ((ifp->int_state & IS_PM_RDISC)
	    && ifp->int_d_metric == 0)
		ifp->int_d_metric = FAKE_METRIC;

	if (ifp->int_rdisc_int == 0)
		ifp->int_rdisc_int = DefMaxAdvertiseInterval;

	if (!(ifp->int_if_flags & IFF_MULTICAST)
	    && !(ifp->int_state & IS_REMOTE))
		ifp->int_state |= IS_BCAST_RDISC;

	if (ifp->int_if_flags & IFF_POINTOPOINT) {
		ifp->int_state |= IS_BCAST_RDISC;
		/* By default, point-to-point links should be passive
		 * about router-discovery for the sake of demand-dialing.
		 */
		if (0 == (ifp->int_state & GROUP_IS_SOL_OUT))
			ifp->int_state |= IS_NO_SOL_OUT;
		if (0 == (ifp->int_state & GROUP_IS_ADV_OUT))
			ifp->int_state |= IS_NO_ADV_OUT;
	}

	if (0 != (ifp->int_state & (IS_PASSIVE | IS_REMOTE)))
		ifp->int_state |= IS_NO_RDISC;
	if (ifp->int_state & IS_PASSIVE)
		ifp->int_state |= IS_NO_RIP;

	if (!IS_RIP_IN_OFF(ifp->int_state)
	    && ifp->int_auth[0].type != RIP_AUTH_NONE
	    && !(ifp->int_state & IS_NO_RIPV1_IN)
	    && !warned_auth_in) {
		msglog("Warning: RIPv1 input via %s"
		       " will be accepted without authentication",
		       ifp->int_name);
		warned_auth_in = 1;
	}
	if (!IS_RIP_OUT_OFF(ifp->int_state)
	    && ifp->int_auth[0].type != RIP_AUTH_NONE
	    && !(ifp->int_state & IS_NO_RIPV1_OUT)) {
		if (!warned_auth_out) {
			msglog("Warning: RIPv1 output via %s"
			       " will be sent without authentication",
			       ifp->int_name);
			warned_auth_out = 1;
		}
	}
}


/* Read a list of gateways from /etc/gateways and add them to our tables.
 *
 * This file contains a list of "remote" gateways.  That is usually
 * a gateway which we cannot immediately determine if it is present or
 * not as we can do for those provided by directly connected hardware.
 *
 * If a gateway is marked "passive" in the file, then we assume it
 * does not understand RIP and assume it is always present.  Those
 * not marked passive are treated as if they were directly connected
 * and assumed to be broken if they do not send us advertisements.
 * All remote interfaces are added to our list, and those not marked
 * passive are sent routing updates.
 *
 * A passive interface can also be local, hardware interface exempt
 * from RIP.
 */
void
gwkludge(void)
{
	FILE *fp;
	char *p, *lptr;
	const char *cp;
	char lbuf[200], net_host[5], dname[64+1+64+1];
	char gname[GNAME_LEN+1], qual[9];
	struct interface *ifp;
	naddr dst, netmask, gate;
	int metric, n, lnum;
	struct stat sb;
	u_int state;
	const char *type;


	fp = fopen(_PATH_GATEWAYS, "r");
	if (fp == NULL)
		return;

	if (0 > fstat(fileno(fp), &sb)) {
		msglog("could not stat() "_PATH_GATEWAYS);
		(void)fclose(fp);
		return;
	}

	for (lnum = 1; ; lnum++) {
		if (fgets(lbuf, sizeof(lbuf), fp) == NULL)
			break;
		lptr = lbuf;
		while (*lptr == ' ')
			lptr++;
		p = lptr+strlen(lptr)-1;
		while (*p == '\n'
		       || (*p == ' ' && (p == lptr+1 || *(p-1) != '\\')))
			*p-- = '\0';
		if (*lptr == '\0'	/* ignore null and comment lines */
		    || *lptr == '#')
			continue;

		/* notice newfangled parameter lines
		 */
		if (strncasecmp("net", lptr, 3)
		    && strncasecmp("host", lptr, 4)) {
			cp = parse_parms(lptr,
					 (sb.st_uid == 0
					  && !(sb.st_mode&(S_IRWXG|S_IRWXO))));
			if (cp != NULL)
				msglog("%s in line %d of "_PATH_GATEWAYS,
				       cp, lnum);
			continue;
		}

/*  {net | host} XX[/M] XX gateway XX metric DD [passive | external]\n */
		qual[0] = '\0';
		/* the '64' here must be GNAME_LEN */
		n = sscanf(lptr, "%4s %129[^ \t] gateway"
			   " %64[^ / \t] metric %u %8s\n",
			   net_host, dname, gname, &metric, qual);
		if (n != 4 && n != 5) {
			msglog("bad "_PATH_GATEWAYS" entry \"%s\"; %d values",
			       lptr, n);
			continue;
		}
		if (metric >= HOPCNT_INFINITY) {
			msglog("bad metric in "_PATH_GATEWAYS" entry \"%s\"",
			       lptr);
			continue;
		}
		if (!strcasecmp(net_host, "host")) {
			if (!gethost(dname, &dst)) {
				msglog("bad host \"%s\" in "_PATH_GATEWAYS
				       " entry \"%s\"", dname, lptr);
				continue;
			}
			netmask = HOST_MASK;
		} else if (!strcasecmp(net_host, "net")) {
			if (!getnet(dname, &dst, &netmask)) {
				msglog("bad net \"%s\" in "_PATH_GATEWAYS
				       " entry \"%s\"", dname, lptr);
				continue;
			}
			if (dst == RIP_DEFAULT) {
				msglog("bad net \"%s\" in "_PATH_GATEWAYS
				       " entry \"%s\"--cannot be default",
				       dname, lptr);
				continue;
			}
			/* Turn network # into IP address. */
			dst = htonl(dst);
		} else {
			msglog("bad \"%s\" in "_PATH_GATEWAYS
			       " entry \"%s\"", net_host, lptr);
			continue;
		}

		if (!gethost(gname, &gate)) {
			msglog("bad gateway \"%s\" in "_PATH_GATEWAYS
			       " entry \"%s\"", gname, lptr);
			continue;
		}

		if (!strcasecmp(qual, type = "passive")) {
			/* Passive entries are not placed in our tables,
			 * only the kernel's, so we don't copy all of the
			 * external routing information within a net.
			 * Internal machines should use the default
			 * route to a suitable gateway (like us).
			 */
			state = IS_REMOTE | IS_PASSIVE;
			if (metric == 0)
				metric = 1;

		} else if (!strcasecmp(qual, type = "external")) {
			/* External entries are handled by other means
			 * such as EGP, and are placed only in the daemon
			 * tables to prevent overriding them with something
			 * else.
			 */
			strcpy(qual,"external");
			state = IS_REMOTE | IS_PASSIVE | IS_EXTERNAL;
			if (metric == 0)
				metric = 1;

		} else if (!strcasecmp(qual, "active")
			   || qual[0] == '\0') {
			if (metric != 0) {
				/* Entries that are neither "passive" nor
				 * "external" are "remote" and must behave
				 * like physical interfaces.  If they are not
				 * heard from regularly, they are deleted.
				 */
				state = IS_REMOTE;
				type = "remote";
			} else {
				/* "remote" entries with a metric of 0
				 * are aliases for our own interfaces
				 */
				state = IS_REMOTE | IS_PASSIVE | IS_ALIAS;
				type = "alias";
			}

		} else {
			msglog("bad "_PATH_GATEWAYS" entry \"%s\";"
			       " unknown type %s", lptr, qual);
			continue;
		}

		if (0 != (state & (IS_PASSIVE | IS_REMOTE)))
			state |= IS_NO_RDISC;
		if (state & IS_PASSIVE)
			state |= IS_NO_RIP;

		ifp = check_dup(gate,dst,netmask,state);
		if (ifp != NULL) {
			msglog("duplicate "_PATH_GATEWAYS" entry \"%s\"",lptr);
			continue;
		}

		ifp = (struct interface *)rtmalloc(sizeof(*ifp), "gwkludge()");
		memset(ifp, 0, sizeof(*ifp));

		ifp->int_state = state;
		if (netmask == HOST_MASK)
			ifp->int_if_flags = IFF_POINTOPOINT | IFF_UP;
		else
			ifp->int_if_flags = IFF_UP;
		ifp->int_act_time = NEVER;
		ifp->int_addr = gate;
		ifp->int_dstaddr = dst;
		ifp->int_mask = netmask;
		ifp->int_ripv1_mask = netmask;
		ifp->int_std_mask = std_mask(gate);
		ifp->int_net = ntohl(dst);
		ifp->int_std_net = ifp->int_net & ifp->int_std_mask;
		ifp->int_std_addr = htonl(ifp->int_std_net);
		ifp->int_metric = metric;
		if (!(state & IS_EXTERNAL)
		    && ifp->int_mask != ifp->int_std_mask)
			ifp->int_state |= IS_SUBNET;
		(void)sprintf(ifp->int_name, "%s(%s)", type, gname);
		ifp->int_index = -1;

		if_link(ifp);
	}

	/* After all of the parameter lines have been read,
	 * apply them to any remote interfaces.
	 */
	LIST_FOREACH(ifp, &ifnet, int_list) {
		get_parms(ifp);

		tot_interfaces++;
		if (!IS_RIP_OFF(ifp->int_state))
			rip_interfaces++;

		trace_if("Add", ifp);
	}

	(void)fclose(fp);
}


/* like strtok(), but honoring backslash and not changing the source string
 */
static int				/* 0=ok, -1=bad */
parse_quote(char **linep,		/* look here */
	    const char *delims,		/* for these delimiters */
	    char *delimp,		/* 0 or put found delimiter here */
	    char *buf,			/* copy token to here */
	    int	lim)			/* at most this many bytes */
{
	char c = '\0', *pc;
	const char *p;


	pc = *linep;
	if (*pc == '\0')
		return -1;

	while (lim != 0) {
		c = *pc++;
		if (c == '\0')
			break;

		if (c == '\\' && *pc != '\0') {
			if ((c = *pc++) == 'n') {
				c = '\n';
			} else if (c == 'r') {
				c = '\r';
			} else if (c == 't') {
				c = '\t';
			} else if (c == 'b') {
				c = '\b';
			} else if (c >= '0' && c <= '7') {
				c -= '0';
				if (*pc >= '0' && *pc <= '7') {
					c = (c<<3)+(*pc++ - '0');
					if (*pc >= '0' && *pc <= '7')
					    c = (c<<3)+(*pc++ - '0');
				}
			}

		} else {
			for (p = delims; *p != '\0'; ++p) {
				if (*p == c)
					goto exit;
			}
		}

		*buf++ = c;
		--lim;
	}
exit:
	if (lim == 0)
		return -1;

	*buf = '\0';			/* terminate copy of token */
	if (delimp != NULL)
		*delimp = c;		/* return delimiter */
	*linep = pc-1;			/* say where we ended */
	return 0;
}


/* Parse password timestamp
 */
static char *
parse_ts(time_t *tp,
	 char **valp,
	 char *val0,
	 char *delimp,
	 char *buf,
	 u_int bufsize)
{
	struct tm tm;
	char *ptr;

	if (0 > parse_quote(valp, "| ,\n\r", delimp,
			    buf,bufsize)
	    || buf[bufsize-1] != '\0'
	    || buf[bufsize-2] != '\0') {
		sprintf(buf,"bad timestamp %.25s", val0);
		return buf;
	}
	strcat(buf,"\n");
	memset(&tm, 0, sizeof(tm));
	ptr = strptime(buf, "%y/%m/%d@%H:%M\n", &tm);
	if (ptr == NULL || *ptr != '\0') {
		sprintf(buf,"bad timestamp %.25s", val0);
		return buf;
	}

	if ((*tp = mktime(&tm)) == -1) {
		sprintf(buf,"bad timestamp %.25s", val0);
		return buf;
	}

	return 0;
}


/* Get a password, key ID, and expiration date in the format
 *	passwd|keyID|year/mon/day@hour:min|year/mon/day@hour:min
 */
static const char *			/* 0 or error message */
get_passwd(char *tgt,
	   char *val,
	   struct parm *parmp,
	   u_int16_t type,
	   int safe)			/* 1=from secure file */
{
	static char buf[80];
	char *val0, *p, delim;
	struct auth k, *ap, *ap2;
	int i;
	u_long l;

	assert(val != NULL);
	if (!safe)
		return "ignore unsafe password";

	for (ap = parmp->parm_auth, i = 0;
	     ap->type != RIP_AUTH_NONE; i++, ap++) {
		if (i >= MAX_AUTH_KEYS)
			return "too many passwords";
	}

	memset(&k, 0, sizeof(k));
	k.type = type;
	k.end = -1-DAY;

	val0 = val;
	if (0 > parse_quote(&val, "| ,\n\r", &delim,
			    (char *)k.key, sizeof(k.key)))
		return tgt;

	if (delim != '|') {
		if (type == RIP_AUTH_MD5)
			return "missing Keyid";
	} else {
		val0 = ++val;
		buf[sizeof(buf)-1] = '\0';
		if (0 > parse_quote(&val, "| ,\n\r", &delim, buf,sizeof(buf))
		    || buf[sizeof(buf)-1] != '\0'
		    || (l = strtoul(buf,&p,0)) > 255
		    || *p != '\0') {
			sprintf(buf,"bad KeyID \"%.20s\"", val0);
			return buf;
		}
		for (ap2 = parmp->parm_auth; ap2 < ap; ap2++) {
			if (ap2->keyid == l) {
				sprintf(buf,"duplicate KeyID \"%.20s\"", val0);
				return buf;
			}
		}
		k.keyid = (int)l;

		if (delim == '|') {
			val0 = ++val;
			if (NULL != (p = parse_ts(&k.start,&val,val0,&delim,
						  buf,sizeof(buf))))
				return p;
			if (delim != '|')
				return "missing second timestamp";
			val0 = ++val;
			if (NULL != (p = parse_ts(&k.end,&val,val0,&delim,
						  buf,sizeof(buf))))
				return p;
			if ((u_long)k.start > (u_long)k.end) {
				sprintf(buf,"out of order timestamp %.30s",
					val0);
				return buf;
			}
		}
	}
	if (delim != '\0')
		return tgt;

	memmove(ap, &k, sizeof(*ap));
	return 0;
}


static const char *
bad_str(const char *estr)
{
	static char buf[100+8];

	sprintf(buf, "bad \"%.100s\"", estr);
	return buf;
}


/* Parse a set of parameters for an interface.
 */
const char *					/* 0 or error message */
parse_parms(char *line,
	    int safe)			/* 1=from secure file */
{
#define PARS(str) (!strcasecmp(tgt, str))
#define PARSEQ(str) (!strncasecmp(tgt, str"=", sizeof(str)))
#define CKF(g,b) {if (0 != (parm.parm_int_state & ((g) & ~(b)))) break;	\
	parm.parm_int_state |= (b);}
	struct parm parm;
	struct intnet *intnetp;
	struct r1net *r1netp;
	struct tgate *tg;
	naddr addr, mask;
	char delim, *val0 = NULL, *tgt, *val, *p;
	const char *msg;
	char buf[BUFSIZ], buf2[BUFSIZ];
	int i;


	/* "subnet=x.y.z.u/mask[,metric]" must be alone on the line */
	if (!strncasecmp(line, "subnet=", sizeof("subnet=")-1)
	    && *(val = &line[sizeof("subnet=")-1]) != '\0') {
		if (0 > parse_quote(&val, ",", &delim, buf, sizeof(buf)))
			return bad_str(line);
		intnetp = (struct intnet*)rtmalloc(sizeof(*intnetp),
						   "parse_parms subnet");
		intnetp->intnet_metric = 1;
		if (delim == ',') {
			intnetp->intnet_metric = (int)strtol(val+1,&p,0);
			if (*p != '\0'
			    || intnetp->intnet_metric <= 0
			    || intnetp->intnet_metric >= HOPCNT_INFINITY) {
				free(intnetp);
				return bad_str(line);
			}
		}
		if (!getnet(buf, &intnetp->intnet_addr, &intnetp->intnet_mask)
		    || intnetp->intnet_mask == HOST_MASK
		    || intnetp->intnet_addr == RIP_DEFAULT) {
			free(intnetp);
			return bad_str(line);
		}
		intnetp->intnet_addr = htonl(intnetp->intnet_addr);
		intnetp->intnet_next = intnets;
		intnets = intnetp;
		return 0;
	}

	/* "ripv1_mask=x.y.z.u/mask1,mask2" must be alone on the line.
	 * This requires that x.y.z.u/mask1 be considered a subnet of
	 * x.y.z.u/mask2, as if x.y.z.u/mask2 were a class-full network.
	 */
	if (!strncasecmp(line, "ripv1_mask=", sizeof("ripv1_mask=")-1)
	    && *(val = &line[sizeof("ripv1_mask=")-1]) != '\0') {
		if (0 > parse_quote(&val, ",", &delim, buf, sizeof(buf))
		    || delim == '\0')
			return bad_str(line);
		if ((i = (int)strtol(val+1, &p, 0)) <= 0
		    || i > 32 || *p != '\0')
			return bad_str(line);
		r1netp = (struct r1net *)rtmalloc(sizeof(*r1netp),
						  "parse_parms ripv1_mask");
		r1netp->r1net_mask = HOST_MASK << (32-i);
		if (!getnet(buf, &r1netp->r1net_net, &r1netp->r1net_match)
		    || r1netp->r1net_net == RIP_DEFAULT
		    || r1netp->r1net_mask > r1netp->r1net_match) {
			free(r1netp);
			return bad_str(line);
		}
		r1netp->r1net_next = r1nets;
		r1nets = r1netp;
		return 0;
	}

	memset(&parm, 0, sizeof(parm));

	for (;;) {
		tgt = line + strspn(line, " ,\n\r");
		if (*tgt == '\0' || *tgt == '#')
			break;
		line = tgt+strcspn(tgt, "= #,\n\r");
		delim = *line;
		if (delim == '=') {
			val0 = ++line;
			if (0 > parse_quote(&line, " #,\n\r",&delim,
					    buf,sizeof(buf)))
				return bad_str(tgt);
		} else {
			val0 = NULL;
		}
		if (delim != '\0') {
			for (;;) {
				*line = '\0';
				if (delim == '#')
					break;
				++line;
				if (delim != ' '
				    || (delim = *line) != ' ')
					break;
			}
		}

		if (PARSEQ("if")) {
			if (parm.parm_name[0] != '\0'
			    || strlen(buf) > IF_NAME_LEN)
				return bad_str(tgt);
			strcpy(parm.parm_name, buf);

		} else if (PARSEQ("addr")) {
			/* This is a bad idea, because the address based
			 * sets of parameters cannot be checked for
			 * consistency with the interface name parameters.
			 * The parm_net stuff is needed to allow several
			 * -F settings.
			 */
			if (val0 == NULL || !getnet(val0, &addr, &mask)
			    || parm.parm_name[0] != '\0')
				return bad_str(tgt);
			parm.parm_net = addr;
			parm.parm_mask = mask;
			parm.parm_name[0] = '\n';

		} else if (PARSEQ("passwd")) {
			/* since cleartext passwords are so weak allow
			 * them anywhere
			 */
			if (val0 == NULL)
				return bad_str("no passwd");
			msg = get_passwd(tgt,val0,&parm,RIP_AUTH_PW,1);
			if (msg) {
				*val0 = '\0';
				return bad_str(msg);
			}

		} else if (PARSEQ("md5_passwd")) {
			msg = get_passwd(tgt,val0,&parm,RIP_AUTH_MD5,safe);
			if (msg) {
				*val0 = '\0';
				return bad_str(msg);
			}

		} else if (PARS("no_ag")) {
			parm.parm_int_state |= (IS_NO_AG | IS_NO_SUPER_AG);

		} else if (PARS("no_super_ag")) {
			parm.parm_int_state |= IS_NO_SUPER_AG;

		} else if (PARS("no_rip_out")) {
			parm.parm_int_state |= IS_NO_RIP_OUT;

		} else if (PARS("no_ripv1_in")) {
			parm.parm_int_state |= IS_NO_RIPV1_IN;

		} else if (PARS("no_ripv2_in")) {
			parm.parm_int_state |= IS_NO_RIPV2_IN;

		} else if (PARS("ripv2_out")) {
			if (parm.parm_int_state & IS_NO_RIPV2_OUT)
				return bad_str(tgt);
			parm.parm_int_state |= IS_NO_RIPV1_OUT;

		} else if (PARS("ripv2")) {
			if ((parm.parm_int_state & IS_NO_RIPV2_OUT)
			    || (parm.parm_int_state & IS_NO_RIPV2_IN))
				return bad_str(tgt);
			parm.parm_int_state |= (IS_NO_RIPV1_IN
						| IS_NO_RIPV1_OUT);

		} else if (PARS("no_rip")) {
			CKF(IS_PM_RDISC, IS_NO_RIP);

		} else if (PARS("no_rip_mcast")) {
			parm.parm_int_state |= IS_NO_RIP_MCAST;

		} else if (PARS("no_rdisc")) {
			CKF((GROUP_IS_SOL_OUT|GROUP_IS_ADV_OUT), IS_NO_RDISC);

		} else if (PARS("no_solicit")) {
			CKF(GROUP_IS_SOL_OUT, IS_NO_SOL_OUT);

		} else if (PARS("send_solicit")) {
			CKF(GROUP_IS_SOL_OUT, IS_SOL_OUT);

		} else if (PARS("no_rdisc_adv")) {
			CKF(GROUP_IS_ADV_OUT, IS_NO_ADV_OUT);

		} else if (PARS("rdisc_adv")) {
			CKF(GROUP_IS_ADV_OUT, IS_ADV_OUT);

		} else if (PARS("bcast_rdisc")) {
			parm.parm_int_state |= IS_BCAST_RDISC;

		} else if (PARS("passive")) {
			CKF((GROUP_IS_SOL_OUT|GROUP_IS_ADV_OUT), IS_NO_RDISC);
			parm.parm_int_state |= IS_NO_RIP | IS_PASSIVE;

		} else if (PARSEQ("rdisc_pref")) {
			if (parm.parm_rdisc_pref != 0
			    || (parm.parm_rdisc_pref = (int)strtol(buf,&p,0),
				*p != '\0'))
				return bad_str(tgt);

		} else if (PARS("pm_rdisc")) {
			if (IS_RIP_OUT_OFF(parm.parm_int_state))
				return bad_str(tgt);
			parm.parm_int_state |= IS_PM_RDISC;

		} else if (PARSEQ("rdisc_interval")) {
			if (parm.parm_rdisc_int != 0
			    || (parm.parm_rdisc_int = (int)strtoul(buf,&p,0),
				*p != '\0')
			    || parm.parm_rdisc_int < MinMaxAdvertiseInterval
			    || parm.parm_rdisc_int > MaxMaxAdvertiseInterval)
				return bad_str(tgt);

		} else if (PARSEQ("fake_default")) {
			if (parm.parm_d_metric != 0
			    || IS_RIP_OUT_OFF(parm.parm_int_state)
			    || (i = strtoul(buf,&p,0), *p != '\0')
			    || i > HOPCNT_INFINITY-1)
				return bad_str(tgt);
			parm.parm_d_metric = i;

		} else if (PARSEQ("adj_inmetric")) {
			if (parm.parm_adj_inmetric != 0
			    || (i = strtoul(buf,&p,0), *p != '\0')
			    || i > HOPCNT_INFINITY-1)
				return bad_str(tgt);
			parm.parm_adj_inmetric = i;

		} else if (PARSEQ("adj_outmetric")) {
			if (parm.parm_adj_outmetric != 0
			    || (i = strtoul(buf,&p,0), *p != '\0')
			    || i > HOPCNT_INFINITY-1)
				return bad_str(tgt);
			parm.parm_adj_outmetric = i;

		} else if (PARSEQ("trust_gateway")) {
			/* look for trust_gateway=x.y.z|net/mask|...) */
			p = buf;
			if (0 > parse_quote(&p, "|", &delim,
					    buf2, sizeof(buf2))
			    || !gethost(buf2,&addr))
				return bad_str(tgt);
			tg = (struct tgate *)rtmalloc(sizeof(*tg),
						      "parse_parms"
						      "trust_gateway");
			memset(tg, 0, sizeof(*tg));
			tg->tgate_addr = addr;
			i = 0;
			/* The default is to trust all routes. */
			while (delim == '|') {
				p++;
				if (i >= MAX_TGATE_NETS
				    || 0 > parse_quote(&p, "|", &delim,
						       buf2, sizeof(buf2))
				    || !getnet(buf2, &tg->tgate_nets[i].net,
					       &tg->tgate_nets[i].mask)
				    || tg->tgate_nets[i].net == RIP_DEFAULT
				    || tg->tgate_nets[i].mask == 0) {
					free(tg);
					return bad_str(tgt);
				}
				i++;
			}
			tg->tgate_next = tgates;
			tgates = tg;
			parm.parm_int_state |= IS_DISTRUST;

		} else if (PARS("redirect_ok")) {
			parm.parm_int_state |= IS_REDIRECT_OK;

		} else {
			return bad_str(tgt);	/* error */
		}
	}

	return check_parms(&parm);
#undef PARS
#undef PARSEQ
}


/* check for duplicate parameter specifications */
const char *				/* 0 or error message */
check_parms(struct parm *new)
{
	struct parm *parmp, **parmpp;
	int i, num_passwds;

	/* set implicit values
	 */
	if (new->parm_int_state & IS_NO_ADV_IN)
		new->parm_int_state |= IS_NO_SOL_OUT;
	if (new->parm_int_state & IS_NO_SOL_OUT)
		new->parm_int_state |= IS_NO_ADV_IN;

	for (i = num_passwds = 0; i < MAX_AUTH_KEYS; i++) {
		if (new->parm_auth[i].type != RIP_AUTH_NONE)
			num_passwds++;
	}

	/* compare with existing sets of parameters
	 */
	for (parmpp = &parms;
	     (parmp = *parmpp) != NULL;
	     parmpp = &parmp->parm_next) {
		if (strcmp(new->parm_name, parmp->parm_name))
			continue;
		if (!on_net(htonl(parmp->parm_net),
			    new->parm_net, new->parm_mask)
		    && !on_net(htonl(new->parm_net),
			       parmp->parm_net, parmp->parm_mask))
			continue;

		for (i = 0; i < MAX_AUTH_KEYS; i++) {
			if (parmp->parm_auth[i].type != RIP_AUTH_NONE)
				num_passwds++;
		}
		if (num_passwds > MAX_AUTH_KEYS)
			return "too many conflicting passwords";

		if ((0 != (new->parm_int_state & GROUP_IS_SOL_OUT)
		     && 0 != (parmp->parm_int_state & GROUP_IS_SOL_OUT)
		     && 0 != ((new->parm_int_state ^ parmp->parm_int_state)
			      & GROUP_IS_SOL_OUT))
		    || (0 != (new->parm_int_state & GROUP_IS_ADV_OUT)
			&& 0 != (parmp->parm_int_state & GROUP_IS_ADV_OUT)
			&& 0 != ((new->parm_int_state ^ parmp->parm_int_state)
				 & GROUP_IS_ADV_OUT))
		    || (new->parm_rdisc_pref != 0
			&& parmp->parm_rdisc_pref != 0
			&& new->parm_rdisc_pref != parmp->parm_rdisc_pref)
		    || (new->parm_rdisc_int != 0
			&& parmp->parm_rdisc_int != 0
			&& new->parm_rdisc_int != parmp->parm_rdisc_int)) {
			return ("conflicting, duplicate router discovery"
				" parameters");

		}

		if (new->parm_d_metric != 0
		     && parmp->parm_d_metric != 0
		     && new->parm_d_metric != parmp->parm_d_metric) {
			return ("conflicting, duplicate poor man's router"
				" discovery or fake default metric");
		}

		if (new->parm_adj_inmetric != 0
		    && parmp->parm_adj_inmetric != 0
		    && new->parm_adj_inmetric != parmp->parm_adj_inmetric) {
			return ("conflicting interface input "
				"metric adjustments");
		}

		if (new->parm_adj_outmetric != 0
		    && parmp->parm_adj_outmetric != 0
		    && new->parm_adj_outmetric != parmp->parm_adj_outmetric) {
			return ("conflicting interface output "
				"metric adjustments");
		}
	}

	/* link new entry on the list so that when the entries are scanned,
	 * they affect the result in the order the operator specified.
	 */
	parmp = (struct parm*)rtmalloc(sizeof(*parmp), "check_parms");
	memcpy(parmp, new, sizeof(*parmp));
	*parmpp = parmp;

	return 0;
}


/* get a network number as a name or a number, with an optional "/xx"
 * netmask.
 */
int					/* 0=bad */
getnet(char *name,
       naddr *netp,			/* network in host byte order */
       naddr *maskp)			/* masks are always in host order */
{
	int i;
	struct netent *np;
	naddr mask;			/* in host byte order */
	struct in_addr in;		/* a network and so host byte order */
	char hname[MAXHOSTNAMELEN+1];
	char *mname, *p;


	/* Detect and separate "1.2.3.4/24"
	 */
	if (NULL != (mname = strrchr(name,'/'))) {
		i = (int)(mname - name);
		if (i > (int)sizeof(hname)-1)	/* name too long */
			return 0;
		memmove(hname, name, i);
		hname[i] = '\0';
		mname++;
		name = hname;
	}

	np = getnetbyname(name);
	if (np != NULL) {
		in.s_addr = (naddr)np->n_net;
		if (0 == (in.s_addr & 0xff000000))
			in.s_addr <<= 8;
		if (0 == (in.s_addr & 0xff000000))
			in.s_addr <<= 8;
		if (0 == (in.s_addr & 0xff000000))
			in.s_addr <<= 8;
	} else if (inet_aton(name, &in) == 1) {
		in.s_addr = ntohl(in.s_addr);
	} else if (!mname && !strcasecmp(name,"default")) {
		in.s_addr = RIP_DEFAULT;
	} else {
		return 0;
	}

	if (!mname) {
		/* we cannot use the interfaces here because we have not
		 * looked at them yet.
		 */
		mask = std_mask(htonl(in.s_addr));
		if ((~mask & in.s_addr) != 0)
			mask = HOST_MASK;
	} else {
		mask = (naddr)strtoul(mname, &p, 0);
		if (*p != '\0' || mask > 32)
			return 0;
		if (mask != 0)
			mask = HOST_MASK << (32-mask);
	}

	/* must have mask of 0 with default */
	if (mask != 0 && in.s_addr == RIP_DEFAULT)
		return 0;
	/* no host bits allowed in a network number */
	if ((~mask & in.s_addr) != 0)
		return 0;
	/* require non-zero network number */
	if ((mask & in.s_addr) == 0 && in.s_addr != RIP_DEFAULT)
		return 0;
	if (in.s_addr>>24 == 0 && in.s_addr != RIP_DEFAULT)
		return 0;
	if (in.s_addr>>24 == 0xff)
		return 0;

	*netp = in.s_addr;
	*maskp = mask;
	return 1;
}


int					/* 0=bad */
gethost(char *name,
	naddr *addrp)
{
	struct hostent *hp;
	struct in_addr in;


	/* Try for a number first, even in IRIX where gethostbyname()
	 * is smart.  This avoids hitting the name server which
	 * might be sick because routing is.
	 */
	if (inet_aton(name, &in) == 1) {
		/* get a good number, but check that it it makes some
		 * sense.
		 */
		if (ntohl(in.s_addr)>>24 == 0
		    || ntohl(in.s_addr)>>24 == 0xff)
			return 0;
		*addrp = in.s_addr;
		return 1;
	}

	hp = gethostbyname(name);
	if (hp) {
		memcpy(addrp, hp->h_addr, sizeof(*addrp));
		return 1;
	}

	return 0;
}
