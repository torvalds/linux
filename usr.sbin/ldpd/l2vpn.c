/*	$OpenBSD: l2vpn.c,v 1.24 2017/03/04 00:21:48 renato Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
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
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "ldpd.h"
#include "ldpe.h"
#include "lde.h"
#include "log.h"

static void	 l2vpn_pw_fec(struct l2vpn_pw *, struct fec *);

struct l2vpn *
l2vpn_new(const char *name)
{
	struct l2vpn	*l2vpn;

	if ((l2vpn = calloc(1, sizeof(*l2vpn))) == NULL)
		fatal("l2vpn_new: calloc");

	strlcpy(l2vpn->name, name, sizeof(l2vpn->name));

	/* set default values */
	l2vpn->mtu = DEFAULT_L2VPN_MTU;
	l2vpn->pw_type = DEFAULT_PW_TYPE;

	LIST_INIT(&l2vpn->if_list);
	LIST_INIT(&l2vpn->pw_list);

	return (l2vpn);
}

struct l2vpn *
l2vpn_find(struct ldpd_conf *xconf, const char *name)
{
	struct l2vpn	*l2vpn;

	LIST_FOREACH(l2vpn, &xconf->l2vpn_list, entry)
		if (strcmp(l2vpn->name, name) == 0)
			return (l2vpn);

	return (NULL);
}

void
l2vpn_del(struct l2vpn *l2vpn)
{
	struct l2vpn_if		*lif;
	struct l2vpn_pw		*pw;

	while ((lif = LIST_FIRST(&l2vpn->if_list)) != NULL) {
		LIST_REMOVE(lif, entry);
		free(lif);
	}
	while ((pw = LIST_FIRST(&l2vpn->pw_list)) != NULL) {
		LIST_REMOVE(pw, entry);
		free(pw);
	}

	free(l2vpn);
}

void
l2vpn_init(struct l2vpn *l2vpn)
{
	struct l2vpn_pw	*pw;

	LIST_FOREACH(pw, &l2vpn->pw_list, entry)
		l2vpn_pw_init(pw);
}

void
l2vpn_exit(struct l2vpn *l2vpn)
{
	struct l2vpn_pw		*pw;

	LIST_FOREACH(pw, &l2vpn->pw_list, entry)
		l2vpn_pw_exit(pw);
}

struct l2vpn_if *
l2vpn_if_new(struct l2vpn *l2vpn, struct kif *kif)
{
	struct l2vpn_if	*lif;

	if ((lif = calloc(1, sizeof(*lif))) == NULL)
		fatal("l2vpn_if_new: calloc");

	lif->l2vpn = l2vpn;
	strlcpy(lif->ifname, kif->ifname, sizeof(lif->ifname));
	lif->ifindex = kif->ifindex;
	lif->flags = kif->flags;
	lif->linkstate = kif->link_state;

	return (lif);
}

struct l2vpn_if *
l2vpn_if_find(struct l2vpn *l2vpn, unsigned int ifindex)
{
	struct l2vpn_if	*lif;

	LIST_FOREACH(lif, &l2vpn->if_list, entry)
		if (lif->ifindex == ifindex)
			return (lif);

	return (NULL);
}

void
l2vpn_if_update(struct l2vpn_if *lif)
{
	struct l2vpn	*l2vpn = lif->l2vpn;
	struct l2vpn_pw	*pw;
	struct map	 fec;
	struct nbr	*nbr;

	if ((lif->flags & IFF_UP) && LINK_STATE_IS_UP(lif->linkstate))
		return;

	LIST_FOREACH(pw, &l2vpn->pw_list, entry) {
		nbr = nbr_find_ldpid(pw->lsr_id.s_addr);
		if (nbr == NULL)
			continue;

		memset(&fec, 0, sizeof(fec));
		fec.type = MAP_TYPE_PWID;
		fec.fec.pwid.type = l2vpn->pw_type;
		fec.fec.pwid.group_id = 0;
		fec.flags |= F_MAP_PW_ID;
		fec.fec.pwid.pwid = pw->pwid;

		send_mac_withdrawal(nbr, &fec, lif->mac);
	}
}

struct l2vpn_pw *
l2vpn_pw_new(struct l2vpn *l2vpn, struct kif *kif)
{
	struct l2vpn_pw	*pw;

	if ((pw = calloc(1, sizeof(*pw))) == NULL)
		fatal("l2vpn_pw_new: calloc");

	pw->l2vpn = l2vpn;
	strlcpy(pw->ifname, kif->ifname, sizeof(pw->ifname));
	pw->ifindex = kif->ifindex;

	return (pw);
}

struct l2vpn_pw *
l2vpn_pw_find(struct l2vpn *l2vpn, unsigned int ifindex)
{
	struct l2vpn_pw	*pw;

	LIST_FOREACH(pw, &l2vpn->pw_list, entry)
		if (pw->ifindex == ifindex)
			return (pw);

	return (NULL);
}

void
l2vpn_pw_init(struct l2vpn_pw *pw)
{
	struct fec	 fec;

	l2vpn_pw_reset(pw);

	l2vpn_pw_fec(pw, &fec);
	lde_kernel_insert(&fec, AF_INET, (union ldpd_addr*)&pw->lsr_id, 0,
	    0, (void *)pw);
}

void
l2vpn_pw_exit(struct l2vpn_pw *pw)
{
	struct fec	 fec;

	l2vpn_pw_fec(pw, &fec);
	lde_kernel_remove(&fec, AF_INET, (union ldpd_addr*)&pw->lsr_id, 0);
}

static void
l2vpn_pw_fec(struct l2vpn_pw *pw, struct fec *fec)
{
	memset(fec, 0, sizeof(*fec));
	fec->type = FEC_TYPE_PWID;
	fec->u.pwid.type = pw->l2vpn->pw_type;
	fec->u.pwid.pwid = pw->pwid;
	fec->u.pwid.lsr_id = pw->lsr_id;
}

void
l2vpn_pw_reset(struct l2vpn_pw *pw)
{
	pw->remote_group = 0;
	pw->remote_mtu = 0;
	pw->remote_status = 0;

	if (pw->flags & F_PW_CWORD_CONF)
		pw->flags |= F_PW_CWORD;
	else
		pw->flags &= ~F_PW_CWORD;

	if (pw->flags & F_PW_STATUSTLV_CONF)
		pw->flags |= F_PW_STATUSTLV;
	else
		pw->flags &= ~F_PW_STATUSTLV;
}

int
l2vpn_pw_ok(struct l2vpn_pw *pw, struct fec_nh *fnh)
{
	struct fec		 fec;
	struct fec_node		*fn;

	/* check for a remote label */
	if (fnh->remote_label == NO_LABEL)
		return (0);

	/* MTUs must match */
	if (pw->l2vpn->mtu != pw->remote_mtu)
		return (0);

	/* check pw status if applicable */
	if ((pw->flags & F_PW_STATUSTLV) &&
	    pw->remote_status != PW_FORWARDING)
		return (0);

	/* check for a working lsp to the nexthop */
	memset(&fec, 0, sizeof(fec));
	switch (pw->af) {
	case AF_INET:
		fec.type = FEC_TYPE_IPV4;
		fec.u.ipv4.prefix = pw->addr.v4;
		fec.u.ipv4.prefixlen = 32;
		break;
	case AF_INET6:
		fec.type = FEC_TYPE_IPV6;
		fec.u.ipv6.prefix = pw->addr.v6;
		fec.u.ipv6.prefixlen = 128;
		break;
	default:
		fatalx("l2vpn_pw_ok: unknown af");
	}

	fn = (struct fec_node *)fec_find(&ft, &fec);
	if (fn == NULL || fn->local_label == NO_LABEL)
		return (0);
	/*
	 * Need to ensure that there's a label binding for all nexthops.
	 * Otherwise, ECMP for this route could render the pseudowire unusable.
	 */
	LIST_FOREACH(fnh, &fn->nexthops, entry)
		if (fnh->remote_label == NO_LABEL)
			return (0);

	return (1);
}

int
l2vpn_pw_negotiate(struct lde_nbr *ln, struct fec_node *fn, struct map *map)
{
	struct l2vpn_pw		*pw;
	struct status_tlv	 st;

	/* NOTE: thanks martini & friends for all this mess */

	pw = (struct l2vpn_pw *) fn->data;
	if (pw == NULL)
		/*
		 * pseudowire not configured, return and record
		 * the mapping later
		 */
		return (0);

	/* RFC4447 - Section 6.2: control word negotiation */
	if (fec_find(&ln->sent_map, &fn->fec)) {
		if ((map->flags & F_MAP_PW_CWORD) &&
		    !(pw->flags & F_PW_CWORD_CONF)) {
			/* ignore the received label mapping */
			return (1);
		} else if (!(map->flags & F_MAP_PW_CWORD) &&
		    (pw->flags & F_PW_CWORD_CONF)) {
			/* append a "Wrong C-bit" status code */
			st.status_code = S_WRONG_CBIT;
			st.msg_id = map->msg_id;
			st.msg_type = htons(MSG_TYPE_LABELMAPPING);
			lde_send_labelwithdraw(ln, fn, NULL, &st);

			pw->flags &= ~F_PW_CWORD;
			lde_send_labelmapping(ln, fn, 1);
		}
	} else if (map->flags & F_MAP_PW_CWORD) {
		if (pw->flags & F_PW_CWORD_CONF)
			pw->flags |= F_PW_CWORD;
		else
			/* act as if no label mapping had been received */
			return (1);
	} else
		pw->flags &= ~F_PW_CWORD;

	/* RFC4447 - Section 5.4.3: pseudowire status negotiation */
	if (fec_find(&ln->recv_map, &fn->fec) == NULL &&
	    !(map->flags & F_MAP_PW_STATUS))
		pw->flags &= ~F_PW_STATUSTLV;

	return (0);
}

void
l2vpn_send_pw_status(struct lde_nbr *ln, uint32_t status, struct fec *fec)
{
	struct notify_msg	 nm;

	memset(&nm, 0, sizeof(nm));
	nm.status_code = S_PW_STATUS;
	nm.pw_status = status;
	nm.flags |= F_NOTIF_PW_STATUS;
	lde_fec2map(fec, &nm.fec);
	nm.flags |= F_NOTIF_FEC;

	lde_imsg_compose_ldpe(IMSG_NOTIFICATION_SEND, ln->peerid, 0, &nm,
	    sizeof(nm));
}

void
l2vpn_send_pw_status_wcard(struct lde_nbr *ln, uint32_t status,
    uint16_t pw_type, uint32_t group_id)
{
	struct notify_msg	 nm;

	memset(&nm, 0, sizeof(nm));
	nm.status_code = S_PW_STATUS;
	nm.pw_status = status;
	nm.flags |= F_NOTIF_PW_STATUS;
	nm.fec.type = MAP_TYPE_PWID;
	nm.fec.fec.pwid.type = pw_type;
	nm.fec.fec.pwid.group_id = group_id;
	nm.flags |= F_NOTIF_FEC;

	lde_imsg_compose_ldpe(IMSG_NOTIFICATION_SEND, ln->peerid, 0, &nm,
	    sizeof(nm));
}

void
l2vpn_recv_pw_status(struct lde_nbr *ln, struct notify_msg *nm)
{
	struct fec		 fec;
	struct fec_node		*fn;
	struct fec_nh		*fnh;
	struct l2vpn_pw		*pw;

	if (nm->fec.type == MAP_TYPE_TYPED_WCARD ||
	    !(nm->fec.flags & F_MAP_PW_ID)) {
		l2vpn_recv_pw_status_wcard(ln, nm);
		return;
	}

	lde_map2fec(&nm->fec, ln->id, &fec);
	fn = (struct fec_node *)fec_find(&ft, &fec);
	if (fn == NULL)
		/* unknown fec */
		return;

	pw = (struct l2vpn_pw *) fn->data;
	if (pw == NULL)
		return;

	fnh = fec_nh_find(fn, AF_INET, (union ldpd_addr *)&ln->id, 0);
	if (fnh == NULL)
		return;

	/* remote status didn't change */
	if (pw->remote_status == nm->pw_status)
		return;
	pw->remote_status = nm->pw_status;

	if (l2vpn_pw_ok(pw, fnh))
		lde_send_change_klabel(fn, fnh);
	else
		lde_send_delete_klabel(fn, fnh);
}

/* RFC4447 PWid group wildcard */
void
l2vpn_recv_pw_status_wcard(struct lde_nbr *ln, struct notify_msg *nm)
{
	struct fec		*f;
	struct fec_node		*fn;
	struct fec_nh		*fnh;
	struct l2vpn_pw		*pw;
	struct map		*wcard = &nm->fec;

	RB_FOREACH(f, fec_tree, &ft) {
		fn = (struct fec_node *)f;
		if (fn->fec.type != FEC_TYPE_PWID)
			continue;

		pw = (struct l2vpn_pw *) fn->data;
		if (pw == NULL)
			continue;

		switch (wcard->type) {
		case MAP_TYPE_TYPED_WCARD:
			if (wcard->fec.twcard.u.pw_type != PW_TYPE_WILDCARD &&
			    wcard->fec.twcard.u.pw_type != fn->fec.u.pwid.type)
				continue;
			break;
		case MAP_TYPE_PWID:
			if (wcard->fec.pwid.type != fn->fec.u.pwid.type)
				continue;
			if (wcard->fec.pwid.group_id != pw->remote_group)
				continue;
			break;
		}

		fnh = fec_nh_find(fn, AF_INET, (union ldpd_addr *)&ln->id, 0);
		if (fnh == NULL)
			continue;

		/* remote status didn't change */
		if (pw->remote_status == nm->pw_status)
			continue;
		pw->remote_status = nm->pw_status;

		if (l2vpn_pw_ok(pw, fnh))
			lde_send_change_klabel(fn, fnh);
		else
			lde_send_delete_klabel(fn, fnh);
	}
}

void
l2vpn_sync_pws(int af, union ldpd_addr *addr)
{
	struct l2vpn		*l2vpn;
	struct l2vpn_pw		*pw;
	struct fec		 fec;
	struct fec_node		*fn;
	struct fec_nh		*fnh;

	LIST_FOREACH(l2vpn, &ldeconf->l2vpn_list, entry) {
		LIST_FOREACH(pw, &l2vpn->pw_list, entry) {
			if (af != pw->af || ldp_addrcmp(af, &pw->addr, addr))
				continue;

			l2vpn_pw_fec(pw, &fec);
			fn = (struct fec_node *)fec_find(&ft, &fec);
			if (fn == NULL)
				continue;
			fnh = fec_nh_find(fn, AF_INET, (union ldpd_addr *)
			    &pw->lsr_id, 0);
			if (fnh == NULL)
				continue;

			if (l2vpn_pw_ok(pw, fnh))
				lde_send_change_klabel(fn, fnh);
			else
				lde_send_delete_klabel(fn, fnh);
		}
	}
}

void
l2vpn_pw_ctl(pid_t pid)
{
	struct l2vpn		*l2vpn;
	struct l2vpn_pw		*pw;
	static struct ctl_pw	 pwctl;

	LIST_FOREACH(l2vpn, &ldeconf->l2vpn_list, entry)
		LIST_FOREACH(pw, &l2vpn->pw_list, entry) {
			memset(&pwctl, 0, sizeof(pwctl));
			strlcpy(pwctl.ifname, pw->ifname,
			    sizeof(pwctl.ifname));
			pwctl.pwid = pw->pwid;
			pwctl.lsr_id = pw->lsr_id;
			pwctl.status = pw->flags & F_PW_STATUS_UP;

			lde_imsg_compose_ldpe(IMSG_CTL_SHOW_L2VPN_PW, 0,
			    pid, &pwctl, sizeof(pwctl));
		}
}

void
l2vpn_binding_ctl(pid_t pid)
{
	struct fec		*f;
	struct fec_node		*fn;
	struct lde_map		*me;
	struct l2vpn_pw		*pw;
	static struct ctl_pw	 pwctl;

	RB_FOREACH(f, fec_tree, &ft) {
		if (f->type != FEC_TYPE_PWID)
			continue;

		fn = (struct fec_node *)f;
		if (fn->local_label == NO_LABEL &&
		    LIST_EMPTY(&fn->downstream))
			continue;

		memset(&pwctl, 0, sizeof(pwctl));
		pwctl.type = f->u.pwid.type;
		pwctl.pwid = f->u.pwid.pwid;
		pwctl.lsr_id = f->u.pwid.lsr_id;

		pw = (struct l2vpn_pw *) fn->data;
		if (pw) {
			pwctl.local_label = fn->local_label;
			pwctl.local_gid = 0;
			pwctl.local_ifmtu = pw->l2vpn->mtu;
		} else
			pwctl.local_label = NO_LABEL;

		LIST_FOREACH(me, &fn->downstream, entry)
			if (f->u.pwid.lsr_id.s_addr == me->nexthop->id.s_addr)
				break;

		if (me) {
			pwctl.remote_label = me->map.label;
			pwctl.remote_gid = me->map.fec.pwid.group_id;
			if (me->map.flags & F_MAP_PW_IFMTU)
				pwctl.remote_ifmtu = me->map.fec.pwid.ifmtu;

			lde_imsg_compose_ldpe(IMSG_CTL_SHOW_L2VPN_BINDING,
			    0, pid, &pwctl, sizeof(pwctl));
		} else if (pw) {
			pwctl.remote_label = NO_LABEL;

			lde_imsg_compose_ldpe(IMSG_CTL_SHOW_L2VPN_BINDING,
			    0, pid, &pwctl, sizeof(pwctl));
		}
	}
}

/* ldpe */

void
ldpe_l2vpn_init(struct l2vpn *l2vpn)
{
	struct l2vpn_pw		*pw;

	LIST_FOREACH(pw, &l2vpn->pw_list, entry)
		ldpe_l2vpn_pw_init(pw);
}

void
ldpe_l2vpn_exit(struct l2vpn *l2vpn)
{
	struct l2vpn_pw		*pw;

	LIST_FOREACH(pw, &l2vpn->pw_list, entry)
		ldpe_l2vpn_pw_exit(pw);
}

void
ldpe_l2vpn_pw_init(struct l2vpn_pw *pw)
{
	struct tnbr		*tnbr;

	tnbr = tnbr_find(leconf, pw->af, &pw->addr);
	if (tnbr == NULL) {
		tnbr = tnbr_new(leconf, pw->af, &pw->addr);
		tnbr_update(tnbr);
		LIST_INSERT_HEAD(&leconf->tnbr_list, tnbr, entry);
	}

	tnbr->pw_count++;
}

void
ldpe_l2vpn_pw_exit(struct l2vpn_pw *pw)
{
	struct tnbr		*tnbr;

	tnbr = tnbr_find(leconf, pw->af, &pw->addr);
	if (tnbr) {
		tnbr->pw_count--;
		tnbr_check(tnbr);
	}
}
