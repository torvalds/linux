/*	$OpenBSD: printconf.c,v 1.28 2019/01/23 02:02:04 dlg Exp $ */

/*
 * Copyright (c) 2013, 2016 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
 * Copyright (c) 2004, 2005, 2008 Esben Norby <norby@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <netdb.h>
#include <err.h>

#include "ldpd.h"
#include "ldpe.h"
#include "log.h"

static void	print_mainconf(struct ldpd_conf *);
static void	print_af(int, struct ldpd_conf *, struct ldpd_af_conf *);
static void	print_iface(struct iface *, struct iface_af *);
static void	print_tnbr(struct tnbr *);
static void	print_nbrp(struct nbr_params *);
static void	print_l2vpn(struct l2vpn *);
static void	print_pw(struct l2vpn_pw *);

static void
print_mainconf(struct ldpd_conf *conf)
{
	printf("router-id %s\n", inet_ntoa(conf->rtr_id));

	if (conf->flags & F_LDPD_NO_FIB_UPDATE)
		printf("fib-update no\n");
	else
		printf("fib-update yes\n");

	printf("rdomain %u\n", conf->rdomain);
	if (conf->trans_pref == DUAL_STACK_LDPOV4)
		printf("transport-preference ipv4\n");
	else if (conf->trans_pref == DUAL_STACK_LDPOV6)
		printf("transport-preference ipv6\n");

	if (conf->flags & F_LDPD_DS_CISCO_INTEROP)
		printf("ds-cisco-interop yes\n");
	else
		printf("ds-cisco-interop no\n");
}

static void
print_af(int af, struct ldpd_conf *conf, struct ldpd_af_conf *af_conf)
{
	struct iface		*iface;
	struct iface_af		*ia;
	struct tnbr		*tnbr;

	printf("\naddress-family %s {\n", af_name(af));

	if (af_conf->flags & F_LDPD_AF_THELLO_ACCEPT)
		printf("\ttargeted-hello-accept yes\n");
	else
		printf("\ttargeted-hello-accept no\n");

	if (af_conf->flags & F_LDPD_AF_EXPNULL)
		printf("\texplicit-null yes\n");
	else
		printf("\texplicit-null no\n");

	if (af_conf->flags & F_LDPD_AF_NO_GTSM)
		printf("\tgtsm-enable no\n");
	else
		printf("\tgtsm-enable yes\n");

	printf("\tkeepalive %u\n", af_conf->keepalive);
	printf("\ttransport-address %s\n", log_addr(af, &af_conf->trans_addr));

	LIST_FOREACH(iface, &conf->iface_list, entry) {
		ia = iface_af_get(iface, af);
		if (ia->enabled)
			print_iface(iface, ia);
	}

	LIST_FOREACH(tnbr, &conf->tnbr_list, entry)
		if (tnbr->af == af && tnbr->flags & F_TNBR_CONFIGURED)
			print_tnbr(tnbr);

	printf("}\n");
}

static void
print_iface(struct iface *iface, struct iface_af *ia)
{
	printf("\tinterface %s {\n", iface->name);
	printf("\t\tlink-hello-holdtime %u\n", ia->hello_holdtime);
	printf("\t\tlink-hello-interval %u\n", ia->hello_interval);
	printf("\t}\n");
}

static void
print_tnbr(struct tnbr *tnbr)
{
	printf("\n\ttargeted-neighbor %s {\n", log_addr(tnbr->af, &tnbr->addr));
	printf("\t\ttargeted-hello-holdtime %u\n", tnbr->hello_holdtime);
	printf("\t\ttargeted-hello-interval %u\n", tnbr->hello_interval);
	printf("\t}\n");
}

static void
print_nbrp(struct nbr_params *nbrp)
{
	printf("\nneighbor %s {\n", inet_ntoa(nbrp->lsr_id));

	if (nbrp->flags & F_NBRP_KEEPALIVE)
		printf("\tkeepalive %u\n", nbrp->keepalive);

	if (nbrp->flags & F_NBRP_GTSM) {
		if (nbrp->gtsm_enabled)
			printf("\tgtsm-enable yes\n");
		else
			printf("\tgtsm-enable no\n");
	}

	if (nbrp->flags & F_NBRP_GTSM_HOPS)
		printf("\tgtsm-hops %u\n", nbrp->gtsm_hops);

	printf("}\n");
}

static void
print_l2vpn(struct l2vpn *l2vpn)
{
	struct l2vpn_if	*lif;
	struct l2vpn_pw	*pw;

	printf("\nl2vpn %s type vpls {\n", l2vpn->name);

	if (l2vpn->pw_type == PW_TYPE_ETHERNET)
		printf("\tpw-type ethernet\n");
	else
		printf("\tpw-type ethernet-tagged\n");

	printf("\tmtu %u\n", l2vpn->mtu);
	if (l2vpn->br_ifindex != 0)
		printf("\tbridge %s\n", l2vpn->br_ifname);
	LIST_FOREACH(lif, &l2vpn->if_list, entry)
		printf("\tinterface %s\n", lif->ifname);
	LIST_FOREACH(pw, &l2vpn->pw_list, entry)
		print_pw(pw);

	printf("}\n");
}

static void
print_pw(struct l2vpn_pw *pw)
{
	printf("\tpseudowire %s {\n", pw->ifname);

	printf("\t\tneighbor-id %s\n", inet_ntoa(pw->lsr_id));
	printf("\t\tneighbor-addr %s\n", log_addr(pw->af, &pw->addr));
	printf("\t\tpw-id %u\n", pw->pwid);

	if (pw->flags & F_PW_STATUSTLV_CONF)
		printf("\t\tstatus-tlv yes\n");
	else
		printf("\t\tstatus-tlv no\n");

	if (pw->flags & F_PW_CWORD_CONF)
		printf("\t\tcontrol-word yes\n");
	else
		printf("\t\tcontrol-word no\n");

	printf("\t}\n");
}

static void
print_auth(struct ldpd_conf *conf)
{
	struct ldp_auth *auth;

	printf("\n");

	LIST_FOREACH(auth, &conf->auth_list, entry) {
		if (auth->md5key_len)
			printf("tcp md5sig key XXX");
		else
			printf("no tcp md5sig");
		if (auth->idlen) {
			char hbuf[NI_MAXHOST];

			if (inet_net_ntop(AF_INET, &auth->id, auth->idlen,
			    hbuf, sizeof(hbuf)) == NULL)
				err(1, "inet_net_ntop");

			printf(" %s", hbuf);
		}
		printf("\n");
	}
}

void
print_config(struct ldpd_conf *conf)
{
	struct nbr_params	*nbrp;
	struct l2vpn		*l2vpn;

	print_mainconf(conf);

	if (!LIST_EMPTY(&conf->auth_list))
		print_auth(conf);

	if (conf->ipv4.flags & F_LDPD_AF_ENABLED)
		print_af(AF_INET, conf, &conf->ipv4);
	if (conf->ipv6.flags & F_LDPD_AF_ENABLED)
		print_af(AF_INET6, conf, &conf->ipv6);

	LIST_FOREACH(nbrp, &conf->nbrp_list, entry)
		print_nbrp(nbrp);

	LIST_FOREACH(l2vpn, &conf->l2vpn_list, entry)
		print_l2vpn(l2vpn);
}
