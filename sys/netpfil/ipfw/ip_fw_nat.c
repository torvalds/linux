/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Paolo Pisati
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/rwlock.h>
#include <sys/rmlock.h>

#include <netinet/libalias/alias.h>
#include <netinet/libalias/alias_local.h>

#include <net/if.h>
#include <net/if_var.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_fw.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <netpfil/ipfw/ip_fw_private.h>

#include <machine/in_cksum.h>	/* XXX for in_cksum */

struct cfg_spool {
	LIST_ENTRY(cfg_spool)   _next;          /* chain of spool instances */
	struct in_addr          addr;
	uint16_t		port;
};

/* Nat redirect configuration. */
struct cfg_redir {
	LIST_ENTRY(cfg_redir)	_next;	/* chain of redir instances */
	uint16_t		mode;	/* type of redirect mode */
	uint16_t		proto;	/* protocol: tcp/udp */
	struct in_addr		laddr;	/* local ip address */
	struct in_addr		paddr;	/* public ip address */
	struct in_addr		raddr;	/* remote ip address */
	uint16_t		lport;	/* local port */
	uint16_t		pport;	/* public port */
	uint16_t		rport;	/* remote port	*/
	uint16_t		pport_cnt;	/* number of public ports */
	uint16_t		rport_cnt;	/* number of remote ports */
	struct alias_link	**alink;	
	u_int16_t		spool_cnt; /* num of entry in spool chain */
	/* chain of spool instances */
	LIST_HEAD(spool_chain, cfg_spool) spool_chain;
};

/* Nat configuration data struct. */
struct cfg_nat {
	/* chain of nat instances */
	LIST_ENTRY(cfg_nat)	_next;
	int			id;		/* nat id  */
	struct in_addr		ip;		/* nat ip address */
	struct libalias		*lib;		/* libalias instance */
	int			mode;		/* aliasing mode */
	int			redir_cnt; /* number of entry in spool chain */
	/* chain of redir instances */
	LIST_HEAD(redir_chain, cfg_redir) redir_chain;  
	char			if_name[IF_NAMESIZE];	/* interface name */
};

static eventhandler_tag ifaddr_event_tag;

static void
ifaddr_change(void *arg __unused, struct ifnet *ifp)
{
	struct cfg_nat *ptr;
	struct ifaddr *ifa;
	struct ip_fw_chain *chain;

	KASSERT(curvnet == ifp->if_vnet,
	    ("curvnet(%p) differs from iface vnet(%p)", curvnet, ifp->if_vnet));

	if (V_ipfw_vnet_ready == 0 || V_ipfw_nat_ready == 0)
		return;

	chain = &V_layer3_chain;
	IPFW_UH_WLOCK(chain);
	/* Check every nat entry... */
	LIST_FOREACH(ptr, &chain->nat, _next) {
		/* ...using nic 'ifp->if_xname' as dynamic alias address. */
		if (strncmp(ptr->if_name, ifp->if_xname, IF_NAMESIZE) != 0)
			continue;
		if_addr_rlock(ifp);
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr == NULL)
				continue;
			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;
			IPFW_WLOCK(chain);
			ptr->ip = ((struct sockaddr_in *)
			    (ifa->ifa_addr))->sin_addr;
			LibAliasSetAddress(ptr->lib, ptr->ip);
			IPFW_WUNLOCK(chain);
		}
		if_addr_runlock(ifp);
	}
	IPFW_UH_WUNLOCK(chain);
}

/*
 * delete the pointers for nat entry ix, or all of them if ix < 0
 */
static void
flush_nat_ptrs(struct ip_fw_chain *chain, const int ix)
{
	int i;
	ipfw_insn_nat *cmd;

	IPFW_WLOCK_ASSERT(chain);
	for (i = 0; i < chain->n_rules; i++) {
		cmd = (ipfw_insn_nat *)ACTION_PTR(chain->map[i]);
		/* XXX skip log and the like ? */
		if (cmd->o.opcode == O_NAT && cmd->nat != NULL &&
			    (ix < 0 || cmd->nat->id == ix))
			cmd->nat = NULL;
	}
}

static void
del_redir_spool_cfg(struct cfg_nat *n, struct redir_chain *head)
{
	struct cfg_redir *r, *tmp_r;
	struct cfg_spool *s, *tmp_s;
	int i, num;

	LIST_FOREACH_SAFE(r, head, _next, tmp_r) {
		num = 1; /* Number of alias_link to delete. */
		switch (r->mode) {
		case NAT44_REDIR_PORT:
			num = r->pport_cnt;
			/* FALLTHROUGH */
		case NAT44_REDIR_ADDR:
		case NAT44_REDIR_PROTO:
			/* Delete all libalias redirect entry. */
			for (i = 0; i < num; i++)
				LibAliasRedirectDelete(n->lib, r->alink[i]);
			/* Del spool cfg if any. */
			LIST_FOREACH_SAFE(s, &r->spool_chain, _next, tmp_s) {
				LIST_REMOVE(s, _next);
				free(s, M_IPFW);
			}
			free(r->alink, M_IPFW);
			LIST_REMOVE(r, _next);
			free(r, M_IPFW);
			break;
		default:
			printf("unknown redirect mode: %u\n", r->mode);
			/* XXX - panic?!?!? */
			break;
		}
	}
}

static int
add_redir_spool_cfg(char *buf, struct cfg_nat *ptr)
{
	struct cfg_redir *r;
	struct cfg_spool *s;
	struct nat44_cfg_redir *ser_r;
	struct nat44_cfg_spool *ser_s;

	int cnt, off, i;

	for (cnt = 0, off = 0; cnt < ptr->redir_cnt; cnt++) {
		ser_r = (struct nat44_cfg_redir *)&buf[off];
		r = malloc(sizeof(*r), M_IPFW, M_WAITOK | M_ZERO);
		r->mode = ser_r->mode;
		r->laddr = ser_r->laddr;
		r->paddr = ser_r->paddr;
		r->raddr = ser_r->raddr;
		r->lport = ser_r->lport;
		r->pport = ser_r->pport;
		r->rport = ser_r->rport;
		r->pport_cnt = ser_r->pport_cnt;
		r->rport_cnt = ser_r->rport_cnt;
		r->proto = ser_r->proto;
		r->spool_cnt = ser_r->spool_cnt;
		//memcpy(r, ser_r, SOF_REDIR);
		LIST_INIT(&r->spool_chain);
		off += sizeof(struct nat44_cfg_redir);
		r->alink = malloc(sizeof(struct alias_link *) * r->pport_cnt,
		    M_IPFW, M_WAITOK | M_ZERO);
		switch (r->mode) {
		case NAT44_REDIR_ADDR:
			r->alink[0] = LibAliasRedirectAddr(ptr->lib, r->laddr,
			    r->paddr);
			break;
		case NAT44_REDIR_PORT:
			for (i = 0 ; i < r->pport_cnt; i++) {
				/* If remotePort is all ports, set it to 0. */
				u_short remotePortCopy = r->rport + i;
				if (r->rport_cnt == 1 && r->rport == 0)
					remotePortCopy = 0;
				r->alink[i] = LibAliasRedirectPort(ptr->lib,
				    r->laddr, htons(r->lport + i), r->raddr,
				    htons(remotePortCopy), r->paddr,
				    htons(r->pport + i), r->proto);
				if (r->alink[i] == NULL) {
					r->alink[0] = NULL;
					break;
				}
			}
			break;
		case NAT44_REDIR_PROTO:
			r->alink[0] = LibAliasRedirectProto(ptr->lib ,r->laddr,
			    r->raddr, r->paddr, r->proto);
			break;
		default:
			printf("unknown redirect mode: %u\n", r->mode);
			break;
		}
		if (r->alink[0] == NULL) {
			printf("LibAliasRedirect* returned NULL\n");
			free(r->alink, M_IPFW);
			free(r, M_IPFW);
			return (EINVAL);
		}
		/* LSNAT handling. */
		for (i = 0; i < r->spool_cnt; i++) {
			ser_s = (struct nat44_cfg_spool *)&buf[off];
			s = malloc(sizeof(*s), M_IPFW, M_WAITOK | M_ZERO);
			s->addr = ser_s->addr;
			s->port = ser_s->port;
			LibAliasAddServer(ptr->lib, r->alink[0],
			    s->addr, htons(s->port));
			off += sizeof(struct nat44_cfg_spool);
			/* Hook spool entry. */
			LIST_INSERT_HEAD(&r->spool_chain, s, _next);
		}
		/* And finally hook this redir entry. */
		LIST_INSERT_HEAD(&ptr->redir_chain, r, _next);
	}

	return (0);
}

static void
free_nat_instance(struct cfg_nat *ptr)
{

	del_redir_spool_cfg(ptr, &ptr->redir_chain);
	LibAliasUninit(ptr->lib);
	free(ptr, M_IPFW);
}


/*
 * ipfw_nat - perform mbuf header translation.
 *
 * Note V_layer3_chain has to be locked while calling ipfw_nat() in
 * 'global' operation mode (t == NULL).
 *
 */
static int
ipfw_nat(struct ip_fw_args *args, struct cfg_nat *t, struct mbuf *m)
{
	struct mbuf *mcl;
	struct ip *ip;
	/* XXX - libalias duct tape */
	int ldt, retval, found;
	struct ip_fw_chain *chain;
	char *c;

	ldt = 0;
	retval = 0;
	mcl = m_megapullup(m, m->m_pkthdr.len);
	if (mcl == NULL) {
		args->m = NULL;
		return (IP_FW_DENY);
	}
	ip = mtod(mcl, struct ip *);

	/*
	 * XXX - Libalias checksum offload 'duct tape':
	 *
	 * locally generated packets have only pseudo-header checksum
	 * calculated and libalias will break it[1], so mark them for
	 * later fix.  Moreover there are cases when libalias modifies
	 * tcp packet data[2], mark them for later fix too.
	 *
	 * [1] libalias was never meant to run in kernel, so it does
	 * not have any knowledge about checksum offloading, and
	 * expects a packet with a full internet checksum.
	 * Unfortunately, packets generated locally will have just the
	 * pseudo header calculated, and when libalias tries to adjust
	 * the checksum it will actually compute a wrong value.
	 *
	 * [2] when libalias modifies tcp's data content, full TCP
	 * checksum has to be recomputed: the problem is that
	 * libalias does not have any idea about checksum offloading.
	 * To work around this, we do not do checksumming in LibAlias,
	 * but only mark the packets in th_x2 field. If we receive a
	 * marked packet, we calculate correct checksum for it
	 * aware of offloading.  Why such a terrible hack instead of
	 * recalculating checksum for each packet?
	 * Because the previous checksum was not checked!
	 * Recalculating checksums for EVERY packet will hide ALL
	 * transmission errors. Yes, marked packets still suffer from
	 * this problem. But, sigh, natd(8) has this problem, too.
	 *
	 * TODO: -make libalias mbuf aware (so
	 * it can handle delayed checksum and tso)
	 */

	if (mcl->m_pkthdr.rcvif == NULL &&
	    mcl->m_pkthdr.csum_flags & CSUM_DELAY_DATA)
		ldt = 1;

	c = mtod(mcl, char *);

	/* Check if this is 'global' instance */
	if (t == NULL) {
		if (args->flags & IPFW_ARGS_IN) {
			/* Wrong direction, skip processing */
			args->m = mcl;
			return (IP_FW_NAT);
		}

		found = 0;
		chain = &V_layer3_chain;
		IPFW_RLOCK_ASSERT(chain);
		/* Check every nat entry... */
		LIST_FOREACH(t, &chain->nat, _next) {
			if ((t->mode & PKT_ALIAS_SKIP_GLOBAL) != 0)
				continue;
			retval = LibAliasOutTry(t->lib, c,
			    mcl->m_len + M_TRAILINGSPACE(mcl), 0);
			if (retval == PKT_ALIAS_OK) {
				/* Nat instance recognises state */
				found = 1;
				break;
			}
		}
		if (found != 1) {
			/* No instance found, return ignore */
			args->m = mcl;
			return (IP_FW_NAT);
		}
	} else {
		if (args->flags & IPFW_ARGS_IN)
			retval = LibAliasIn(t->lib, c,
				mcl->m_len + M_TRAILINGSPACE(mcl));
		else
			retval = LibAliasOut(t->lib, c,
				mcl->m_len + M_TRAILINGSPACE(mcl));
	}

	/*
	 * We drop packet when:
	 * 1. libalias returns PKT_ALIAS_ERROR;
	 * 2. For incoming packets:
	 *	a) for unresolved fragments;
	 *	b) libalias returns PKT_ALIAS_IGNORED and
	 *		PKT_ALIAS_DENY_INCOMING flag is set.
	 */
	if (retval == PKT_ALIAS_ERROR ||
	    ((args->flags & IPFW_ARGS_IN) &&
	    (retval == PKT_ALIAS_UNRESOLVED_FRAGMENT ||
	    (retval == PKT_ALIAS_IGNORED &&
	    (t->mode & PKT_ALIAS_DENY_INCOMING) != 0)))) {
		/* XXX - should i add some logging? */
		m_free(mcl);
		args->m = NULL;
		return (IP_FW_DENY);
	}

	if (retval == PKT_ALIAS_RESPOND)
		mcl->m_flags |= M_SKIP_FIREWALL;
	mcl->m_pkthdr.len = mcl->m_len = ntohs(ip->ip_len);

	/*
	 * XXX - libalias checksum offload
	 * 'duct tape' (see above)
	 */

	if ((ip->ip_off & htons(IP_OFFMASK)) == 0 &&
	    ip->ip_p == IPPROTO_TCP) {
		struct tcphdr 	*th;

		th = (struct tcphdr *)(ip + 1);
		if (th->th_x2)
			ldt = 1;
	}

	if (ldt) {
		struct tcphdr 	*th;
		struct udphdr 	*uh;
		uint16_t ip_len, cksum;

		ip_len = ntohs(ip->ip_len);
		cksum = in_pseudo(ip->ip_src.s_addr, ip->ip_dst.s_addr,
		    htons(ip->ip_p + ip_len - (ip->ip_hl << 2)));

		switch (ip->ip_p) {
		case IPPROTO_TCP:
			th = (struct tcphdr *)(ip + 1);
			/*
			 * Maybe it was set in
			 * libalias...
			 */
			th->th_x2 = 0;
			th->th_sum = cksum;
			mcl->m_pkthdr.csum_data =
			    offsetof(struct tcphdr, th_sum);
			break;
		case IPPROTO_UDP:
			uh = (struct udphdr *)(ip + 1);
			uh->uh_sum = cksum;
			mcl->m_pkthdr.csum_data =
			    offsetof(struct udphdr, uh_sum);
			break;
		}
		/* No hw checksum offloading: do it ourselves */
		if ((mcl->m_pkthdr.csum_flags & CSUM_DELAY_DATA) == 0) {
			in_delayed_cksum(mcl);
			mcl->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
		}
	}
	args->m = mcl;
	return (IP_FW_NAT);
}

static struct cfg_nat *
lookup_nat(struct nat_list *l, int nat_id)
{
	struct cfg_nat *res;

	LIST_FOREACH(res, l, _next) {
		if (res->id == nat_id)
			break;
	}
	return res;
}

static struct cfg_nat *
lookup_nat_name(struct nat_list *l, char *name)
{
	struct cfg_nat *res;
	int id;
	char *errptr;

	id = strtol(name, &errptr, 10);
	if (id == 0 || *errptr != '\0')
		return (NULL);

	LIST_FOREACH(res, l, _next) {
		if (res->id == id)
			break;
	}
	return (res);
}

/* IP_FW3 configuration routines */

static void
nat44_config(struct ip_fw_chain *chain, struct nat44_cfg_nat *ucfg)
{
	struct cfg_nat *ptr, *tcfg;
	int gencnt;

	/*
	 * Find/create nat rule.
	 */
	IPFW_UH_WLOCK(chain);
	gencnt = chain->gencnt;
	ptr = lookup_nat_name(&chain->nat, ucfg->name);
	if (ptr == NULL) {
		IPFW_UH_WUNLOCK(chain);
		/* New rule: allocate and init new instance. */
		ptr = malloc(sizeof(struct cfg_nat), M_IPFW, M_WAITOK | M_ZERO);
		ptr->lib = LibAliasInit(NULL);
		LIST_INIT(&ptr->redir_chain);
	} else {
		/* Entry already present: temporarily unhook it. */
		IPFW_WLOCK(chain);
		LIST_REMOVE(ptr, _next);
		flush_nat_ptrs(chain, ptr->id);
		IPFW_WUNLOCK(chain);
		IPFW_UH_WUNLOCK(chain);
	}

	/*
	 * Basic nat (re)configuration.
	 */
	ptr->id = strtol(ucfg->name, NULL, 10);
	/*
	 * XXX - what if this rule doesn't nat any ip and just
	 * redirect?
	 * do we set aliasaddress to 0.0.0.0?
	 */
	ptr->ip = ucfg->ip;
	ptr->redir_cnt = ucfg->redir_cnt;
	ptr->mode = ucfg->mode;
	strlcpy(ptr->if_name, ucfg->if_name, sizeof(ptr->if_name));
	LibAliasSetMode(ptr->lib, ptr->mode, ~0);
	LibAliasSetAddress(ptr->lib, ptr->ip);

	/*
	 * Redir and LSNAT configuration.
	 */
	/* Delete old cfgs. */
	del_redir_spool_cfg(ptr, &ptr->redir_chain);
	/* Add new entries. */
	add_redir_spool_cfg((char *)(ucfg + 1), ptr);
	IPFW_UH_WLOCK(chain);

	/* Extra check to avoid race with another ipfw_nat_cfg() */
	tcfg = NULL;
	if (gencnt != chain->gencnt)
	    tcfg = lookup_nat_name(&chain->nat, ucfg->name);
	IPFW_WLOCK(chain);
	if (tcfg != NULL)
		LIST_REMOVE(tcfg, _next);
	LIST_INSERT_HEAD(&chain->nat, ptr, _next);
	IPFW_WUNLOCK(chain);
	chain->gencnt++;

	IPFW_UH_WUNLOCK(chain);

	if (tcfg != NULL)
		free_nat_instance(ptr);
}

/*
 * Creates/configure nat44 instance
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header nat44_cfg_nat .. ]
 *
 * Returns 0 on success
 */
static int
nat44_cfg(struct ip_fw_chain *chain, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_obj_header *oh;
	struct nat44_cfg_nat *ucfg;
	int id;
	size_t read;
	char *errptr;

	/* Check minimum header size */
	if (sd->valsize < (sizeof(*oh) + sizeof(*ucfg)))
		return (EINVAL);

	oh = (ipfw_obj_header *)sd->kbuf;

	/* Basic length checks for TLVs */
	if (oh->ntlv.head.length != sizeof(oh->ntlv))
		return (EINVAL);

	ucfg = (struct nat44_cfg_nat *)(oh + 1);

	/* Check if name is properly terminated and looks like number */
	if (strnlen(ucfg->name, sizeof(ucfg->name)) == sizeof(ucfg->name))
		return (EINVAL);
	id = strtol(ucfg->name, &errptr, 10);
	if (id == 0 || *errptr != '\0')
		return (EINVAL);

	read = sizeof(*oh) + sizeof(*ucfg);
	/* Check number of redirs */
	if (sd->valsize < read + ucfg->redir_cnt*sizeof(struct nat44_cfg_redir))
		return (EINVAL);

	nat44_config(chain, ucfg);
	return (0);
}

/*
 * Destroys given nat instances.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ]
 *
 * Returns 0 on success
 */
static int
nat44_destroy(struct ip_fw_chain *chain, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_obj_header *oh;
	struct cfg_nat *ptr;
	ipfw_obj_ntlv *ntlv;

	/* Check minimum header size */
	if (sd->valsize < sizeof(*oh))
		return (EINVAL);

	oh = (ipfw_obj_header *)sd->kbuf;

	/* Basic length checks for TLVs */
	if (oh->ntlv.head.length != sizeof(oh->ntlv))
		return (EINVAL);

	ntlv = &oh->ntlv;
	/* Check if name is properly terminated */
	if (strnlen(ntlv->name, sizeof(ntlv->name)) == sizeof(ntlv->name))
		return (EINVAL);

	IPFW_UH_WLOCK(chain);
	ptr = lookup_nat_name(&chain->nat, ntlv->name);
	if (ptr == NULL) {
		IPFW_UH_WUNLOCK(chain);
		return (ESRCH);
	}
	IPFW_WLOCK(chain);
	LIST_REMOVE(ptr, _next);
	flush_nat_ptrs(chain, ptr->id);
	IPFW_WUNLOCK(chain);
	IPFW_UH_WUNLOCK(chain);

	free_nat_instance(ptr);

	return (0);
}

static void
export_nat_cfg(struct cfg_nat *ptr, struct nat44_cfg_nat *ucfg)
{

	snprintf(ucfg->name, sizeof(ucfg->name), "%d", ptr->id);
	ucfg->ip = ptr->ip;
	ucfg->redir_cnt = ptr->redir_cnt;
	ucfg->mode = ptr->mode;
	strlcpy(ucfg->if_name, ptr->if_name, sizeof(ucfg->if_name));
}

/*
 * Gets config for given nat instance
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header nat44_cfg_nat .. ]
 *
 * Returns 0 on success
 */
static int
nat44_get_cfg(struct ip_fw_chain *chain, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_obj_header *oh;
	struct nat44_cfg_nat *ucfg;
	struct cfg_nat *ptr;
	struct cfg_redir *r;
	struct cfg_spool *s;
	struct nat44_cfg_redir *ser_r;
	struct nat44_cfg_spool *ser_s;
	size_t sz;

	sz = sizeof(*oh) + sizeof(*ucfg);
	/* Check minimum header size */
	if (sd->valsize < sz)
		return (EINVAL);

	oh = (struct _ipfw_obj_header *)ipfw_get_sopt_header(sd, sz);

	/* Basic length checks for TLVs */
	if (oh->ntlv.head.length != sizeof(oh->ntlv))
		return (EINVAL);

	ucfg = (struct nat44_cfg_nat *)(oh + 1);

	/* Check if name is properly terminated */
	if (strnlen(ucfg->name, sizeof(ucfg->name)) == sizeof(ucfg->name))
		return (EINVAL);

	IPFW_UH_RLOCK(chain);
	ptr = lookup_nat_name(&chain->nat, ucfg->name);
	if (ptr == NULL) {
		IPFW_UH_RUNLOCK(chain);
		return (ESRCH);
	}

	export_nat_cfg(ptr, ucfg);
	
	/* Estimate memory amount */
	sz = sizeof(ipfw_obj_header) + sizeof(struct nat44_cfg_nat);
	LIST_FOREACH(r, &ptr->redir_chain, _next) {
		sz += sizeof(struct nat44_cfg_redir);
		LIST_FOREACH(s, &r->spool_chain, _next)
			sz += sizeof(struct nat44_cfg_spool);
	}

	ucfg->size = sz;
	if (sd->valsize < sz) {

		/*
		 * Submitted buffer size is not enough.
		 * WE've already filled in @ucfg structure with
		 * relevant info including size, so we
		 * can return. Buffer will be flushed automatically.
		 */
		IPFW_UH_RUNLOCK(chain);
		return (ENOMEM);
	}

	/* Size OK, let's copy data */
	LIST_FOREACH(r, &ptr->redir_chain, _next) {
		ser_r = (struct nat44_cfg_redir *)ipfw_get_sopt_space(sd,
		    sizeof(*ser_r));
		ser_r->mode = r->mode;
		ser_r->laddr = r->laddr;
		ser_r->paddr = r->paddr;
		ser_r->raddr = r->raddr;
		ser_r->lport = r->lport;
		ser_r->pport = r->pport;
		ser_r->rport = r->rport;
		ser_r->pport_cnt = r->pport_cnt;
		ser_r->rport_cnt = r->rport_cnt;
		ser_r->proto = r->proto;
		ser_r->spool_cnt = r->spool_cnt;

		LIST_FOREACH(s, &r->spool_chain, _next) {
			ser_s = (struct nat44_cfg_spool *)ipfw_get_sopt_space(
			    sd, sizeof(*ser_s));

			ser_s->addr = s->addr;
			ser_s->port = s->port;
		}
	}

	IPFW_UH_RUNLOCK(chain);

	return (0);
}

/*
 * Lists all nat44 instances currently available in kernel.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_lheader ]
 * Reply: [ ipfw_obj_lheader nat44_cfg_nat x N ]
 *
 * Returns 0 on success
 */
static int
nat44_list_nat(struct ip_fw_chain *chain, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_obj_lheader *olh;
	struct nat44_cfg_nat *ucfg;
	struct cfg_nat *ptr;
	int nat_count;

	/* Check minimum header size */
	if (sd->valsize < sizeof(ipfw_obj_lheader))
		return (EINVAL);

	olh = (ipfw_obj_lheader *)ipfw_get_sopt_header(sd, sizeof(*olh));
	IPFW_UH_RLOCK(chain);
	nat_count = 0;
	LIST_FOREACH(ptr, &chain->nat, _next)
		nat_count++;

	olh->count = nat_count;
	olh->objsize = sizeof(struct nat44_cfg_nat);
	olh->size = sizeof(*olh) + olh->count * olh->objsize;

	if (sd->valsize < olh->size) {
		IPFW_UH_RUNLOCK(chain);
		return (ENOMEM);
	}

	LIST_FOREACH(ptr, &chain->nat, _next) {
		ucfg = (struct nat44_cfg_nat *)ipfw_get_sopt_space(sd,
		    sizeof(*ucfg));
		export_nat_cfg(ptr, ucfg);
	}

	IPFW_UH_RUNLOCK(chain);

	return (0);
}

/*
 * Gets log for given nat instance
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header nat44_cfg_nat ]
 * Reply: [ ipfw_obj_header nat44_cfg_nat LOGBUFFER ]
 *
 * Returns 0 on success
 */
static int
nat44_get_log(struct ip_fw_chain *chain, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_obj_header *oh;
	struct nat44_cfg_nat *ucfg;
	struct cfg_nat *ptr;
	void *pbuf;
	size_t sz;

	sz = sizeof(*oh) + sizeof(*ucfg);
	/* Check minimum header size */
	if (sd->valsize < sz)
		return (EINVAL);

	oh = (struct _ipfw_obj_header *)ipfw_get_sopt_header(sd, sz);

	/* Basic length checks for TLVs */
	if (oh->ntlv.head.length != sizeof(oh->ntlv))
		return (EINVAL);

	ucfg = (struct nat44_cfg_nat *)(oh + 1);

	/* Check if name is properly terminated */
	if (strnlen(ucfg->name, sizeof(ucfg->name)) == sizeof(ucfg->name))
		return (EINVAL);

	IPFW_UH_RLOCK(chain);
	ptr = lookup_nat_name(&chain->nat, ucfg->name);
	if (ptr == NULL) {
		IPFW_UH_RUNLOCK(chain);
		return (ESRCH);
	}

	if (ptr->lib->logDesc == NULL) {
		IPFW_UH_RUNLOCK(chain);
		return (ENOENT);
	}

	export_nat_cfg(ptr, ucfg);
	
	/* Estimate memory amount */
	ucfg->size = sizeof(struct nat44_cfg_nat) + LIBALIAS_BUF_SIZE;
	if (sd->valsize < sz + sizeof(*oh)) {

		/*
		 * Submitted buffer size is not enough.
		 * WE've already filled in @ucfg structure with
		 * relevant info including size, so we
		 * can return. Buffer will be flushed automatically.
		 */
		IPFW_UH_RUNLOCK(chain);
		return (ENOMEM);
	}

	pbuf = (void *)ipfw_get_sopt_space(sd, LIBALIAS_BUF_SIZE);
	memcpy(pbuf, ptr->lib->logDesc, LIBALIAS_BUF_SIZE);
	
	IPFW_UH_RUNLOCK(chain);

	return (0);
}

static struct ipfw_sopt_handler	scodes[] = {
	{ IP_FW_NAT44_XCONFIG,	0,	HDIR_SET,	nat44_cfg },
	{ IP_FW_NAT44_DESTROY,	0,	HDIR_SET,	nat44_destroy },
	{ IP_FW_NAT44_XGETCONFIG,	0,	HDIR_GET,	nat44_get_cfg },
	{ IP_FW_NAT44_LIST_NAT,	0,	HDIR_GET,	nat44_list_nat },
	{ IP_FW_NAT44_XGETLOG,	0,	HDIR_GET,	nat44_get_log },
};


/*
 * Legacy configuration routines
 */

struct cfg_spool_legacy {
	LIST_ENTRY(cfg_spool_legacy)	_next;
	struct in_addr			addr;
	u_short				port;
};

struct cfg_redir_legacy {
	LIST_ENTRY(cfg_redir)   _next;
	u_int16_t               mode;
	struct in_addr	        laddr;
	struct in_addr	        paddr;
	struct in_addr	        raddr;
	u_short                 lport;
	u_short                 pport;
	u_short                 rport;
	u_short                 pport_cnt;
	u_short                 rport_cnt;
	int                     proto;
	struct alias_link       **alink;
	u_int16_t               spool_cnt;
	LIST_HEAD(, cfg_spool_legacy) spool_chain;
};

struct cfg_nat_legacy {
	LIST_ENTRY(cfg_nat_legacy)	_next;
	int				id;
	struct in_addr			ip;
	char				if_name[IF_NAMESIZE];
	int				mode;
	struct libalias			*lib;
	int				redir_cnt;
	LIST_HEAD(, cfg_redir_legacy)	redir_chain;
};

static int
ipfw_nat_cfg(struct sockopt *sopt)
{
	struct cfg_nat_legacy *cfg;
	struct nat44_cfg_nat *ucfg;
	struct cfg_redir_legacy *rdir;
	struct nat44_cfg_redir *urdir;
	char *buf;
	size_t len, len2;
	int error, i;

	len = sopt->sopt_valsize;
	len2 = len + 128;

	/*
	 * Allocate 2x buffer to store converted structures.
	 * new redir_cfg has shrunk, so we're sure that
	 * new buffer size is enough.
	 */
	buf = malloc(roundup2(len, 8) + len2, M_TEMP, M_WAITOK | M_ZERO);
	error = sooptcopyin(sopt, buf, len, sizeof(struct cfg_nat_legacy));
	if (error != 0)
		goto out;

	cfg = (struct cfg_nat_legacy *)buf;
	if (cfg->id < 0) {
		error = EINVAL;
		goto out;
	}

	ucfg = (struct nat44_cfg_nat *)&buf[roundup2(len, 8)];
	snprintf(ucfg->name, sizeof(ucfg->name), "%d", cfg->id);
	strlcpy(ucfg->if_name, cfg->if_name, sizeof(ucfg->if_name));
	ucfg->ip = cfg->ip;
	ucfg->mode = cfg->mode;
	ucfg->redir_cnt = cfg->redir_cnt;

	if (len < sizeof(*cfg) + cfg->redir_cnt * sizeof(*rdir)) {
		error = EINVAL;
		goto out;
	}

	urdir = (struct nat44_cfg_redir *)(ucfg + 1);
	rdir = (struct cfg_redir_legacy *)(cfg + 1);
	for (i = 0; i < cfg->redir_cnt; i++) {
		urdir->mode = rdir->mode;
		urdir->laddr = rdir->laddr;
		urdir->paddr = rdir->paddr;
		urdir->raddr = rdir->raddr;
		urdir->lport = rdir->lport;
		urdir->pport = rdir->pport;
		urdir->rport = rdir->rport;
		urdir->pport_cnt = rdir->pport_cnt;
		urdir->rport_cnt = rdir->rport_cnt;
		urdir->proto = rdir->proto;
		urdir->spool_cnt = rdir->spool_cnt;

		urdir++;
		rdir++;
	}

	nat44_config(&V_layer3_chain, ucfg);

out:
	free(buf, M_TEMP);
	return (error);
}

static int
ipfw_nat_del(struct sockopt *sopt)
{
	struct cfg_nat *ptr;
	struct ip_fw_chain *chain = &V_layer3_chain;
	int i;

	sooptcopyin(sopt, &i, sizeof i, sizeof i);
	/* XXX validate i */
	IPFW_UH_WLOCK(chain);
	ptr = lookup_nat(&chain->nat, i);
	if (ptr == NULL) {
		IPFW_UH_WUNLOCK(chain);
		return (EINVAL);
	}
	IPFW_WLOCK(chain);
	LIST_REMOVE(ptr, _next);
	flush_nat_ptrs(chain, i);
	IPFW_WUNLOCK(chain);
	IPFW_UH_WUNLOCK(chain);
	free_nat_instance(ptr);
	return (0);
}

static int
ipfw_nat_get_cfg(struct sockopt *sopt)
{
	struct ip_fw_chain *chain = &V_layer3_chain;
	struct cfg_nat *n;
	struct cfg_nat_legacy *ucfg;
	struct cfg_redir *r;
	struct cfg_spool *s;
	struct cfg_redir_legacy *ser_r;
	struct cfg_spool_legacy *ser_s;
	char *data;
	int gencnt, nat_cnt, len, error;

	nat_cnt = 0;
	len = sizeof(nat_cnt);

	IPFW_UH_RLOCK(chain);
retry:
	gencnt = chain->gencnt;
	/* Estimate memory amount */
	LIST_FOREACH(n, &chain->nat, _next) {
		nat_cnt++;
		len += sizeof(struct cfg_nat_legacy);
		LIST_FOREACH(r, &n->redir_chain, _next) {
			len += sizeof(struct cfg_redir_legacy);
			LIST_FOREACH(s, &r->spool_chain, _next)
				len += sizeof(struct cfg_spool_legacy);
		}
	}
	IPFW_UH_RUNLOCK(chain);

	data = malloc(len, M_TEMP, M_WAITOK | M_ZERO);
	bcopy(&nat_cnt, data, sizeof(nat_cnt));

	nat_cnt = 0;
	len = sizeof(nat_cnt);

	IPFW_UH_RLOCK(chain);
	if (gencnt != chain->gencnt) {
		free(data, M_TEMP);
		goto retry;
	}
	/* Serialize all the data. */
	LIST_FOREACH(n, &chain->nat, _next) {
		ucfg = (struct cfg_nat_legacy *)&data[len];
		ucfg->id = n->id;
		ucfg->ip = n->ip;
		ucfg->redir_cnt = n->redir_cnt;
		ucfg->mode = n->mode;
		strlcpy(ucfg->if_name, n->if_name, sizeof(ucfg->if_name));
		len += sizeof(struct cfg_nat_legacy);
		LIST_FOREACH(r, &n->redir_chain, _next) {
			ser_r = (struct cfg_redir_legacy *)&data[len];
			ser_r->mode = r->mode;
			ser_r->laddr = r->laddr;
			ser_r->paddr = r->paddr;
			ser_r->raddr = r->raddr;
			ser_r->lport = r->lport;
			ser_r->pport = r->pport;
			ser_r->rport = r->rport;
			ser_r->pport_cnt = r->pport_cnt;
			ser_r->rport_cnt = r->rport_cnt;
			ser_r->proto = r->proto;
			ser_r->spool_cnt = r->spool_cnt;
			len += sizeof(struct cfg_redir_legacy);
			LIST_FOREACH(s, &r->spool_chain, _next) {
				ser_s = (struct cfg_spool_legacy *)&data[len];
				ser_s->addr = s->addr;
				ser_s->port = s->port;
				len += sizeof(struct cfg_spool_legacy);
			}
		}
	}
	IPFW_UH_RUNLOCK(chain);

	error = sooptcopyout(sopt, data, len);
	free(data, M_TEMP);

	return (error);
}

static int
ipfw_nat_get_log(struct sockopt *sopt)
{
	uint8_t *data;
	struct cfg_nat *ptr;
	int i, size;
	struct ip_fw_chain *chain;
	IPFW_RLOCK_TRACKER;

	chain = &V_layer3_chain;

	IPFW_RLOCK(chain);
	/* one pass to count, one to copy the data */
	i = 0;
	LIST_FOREACH(ptr, &chain->nat, _next) {
		if (ptr->lib->logDesc == NULL)
			continue;
		i++;
	}
	size = i * (LIBALIAS_BUF_SIZE + sizeof(int));
	data = malloc(size, M_IPFW, M_NOWAIT | M_ZERO);
	if (data == NULL) {
		IPFW_RUNLOCK(chain);
		return (ENOSPC);
	}
	i = 0;
	LIST_FOREACH(ptr, &chain->nat, _next) {
		if (ptr->lib->logDesc == NULL)
			continue;
		bcopy(&ptr->id, &data[i], sizeof(int));
		i += sizeof(int);
		bcopy(ptr->lib->logDesc, &data[i], LIBALIAS_BUF_SIZE);
		i += LIBALIAS_BUF_SIZE;
	}
	IPFW_RUNLOCK(chain);
	sooptcopyout(sopt, data, size);
	free(data, M_IPFW);
	return(0);
}

static int
vnet_ipfw_nat_init(const void *arg __unused)
{

	V_ipfw_nat_ready = 1;
	return (0);
}

static int
vnet_ipfw_nat_uninit(const void *arg __unused)
{
	struct cfg_nat *ptr, *ptr_temp;
	struct ip_fw_chain *chain;

	chain = &V_layer3_chain;
	IPFW_WLOCK(chain);
	V_ipfw_nat_ready = 0;
	LIST_FOREACH_SAFE(ptr, &chain->nat, _next, ptr_temp) {
		LIST_REMOVE(ptr, _next);
		free_nat_instance(ptr);
	}
	flush_nat_ptrs(chain, -1 /* flush all */);
	IPFW_WUNLOCK(chain);
	return (0);
}

static void
ipfw_nat_init(void)
{

	/* init ipfw hooks */
	ipfw_nat_ptr = ipfw_nat;
	lookup_nat_ptr = lookup_nat;
	ipfw_nat_cfg_ptr = ipfw_nat_cfg;
	ipfw_nat_del_ptr = ipfw_nat_del;
	ipfw_nat_get_cfg_ptr = ipfw_nat_get_cfg;
	ipfw_nat_get_log_ptr = ipfw_nat_get_log;
	IPFW_ADD_SOPT_HANDLER(1, scodes);

	ifaddr_event_tag = EVENTHANDLER_REGISTER(ifaddr_event, ifaddr_change,
	    NULL, EVENTHANDLER_PRI_ANY);
}

static void
ipfw_nat_destroy(void)
{

	EVENTHANDLER_DEREGISTER(ifaddr_event, ifaddr_event_tag);
	/* deregister ipfw_nat */
	IPFW_DEL_SOPT_HANDLER(1, scodes);
	ipfw_nat_ptr = NULL;
	lookup_nat_ptr = NULL;
	ipfw_nat_cfg_ptr = NULL;
	ipfw_nat_del_ptr = NULL;
	ipfw_nat_get_cfg_ptr = NULL;
	ipfw_nat_get_log_ptr = NULL;
}

static int
ipfw_nat_modevent(module_t mod, int type, void *unused)
{
	int err = 0;

	switch (type) {
	case MOD_LOAD:
		break;

	case MOD_UNLOAD:
		break;

	default:
		return EOPNOTSUPP;
		break;
	}
	return err;
}

static moduledata_t ipfw_nat_mod = {
	"ipfw_nat",
	ipfw_nat_modevent,
	0
};

/* Define startup order. */
#define	IPFW_NAT_SI_SUB_FIREWALL	SI_SUB_PROTO_FIREWALL
#define	IPFW_NAT_MODEVENT_ORDER		(SI_ORDER_ANY - 128) /* after ipfw */
#define	IPFW_NAT_MODULE_ORDER		(IPFW_NAT_MODEVENT_ORDER + 1)
#define	IPFW_NAT_VNET_ORDER		(IPFW_NAT_MODEVENT_ORDER + 2)

DECLARE_MODULE(ipfw_nat, ipfw_nat_mod, IPFW_NAT_SI_SUB_FIREWALL, SI_ORDER_ANY);
MODULE_DEPEND(ipfw_nat, libalias, 1, 1, 1);
MODULE_DEPEND(ipfw_nat, ipfw, 3, 3, 3);
MODULE_VERSION(ipfw_nat, 1);

SYSINIT(ipfw_nat_init, IPFW_NAT_SI_SUB_FIREWALL, IPFW_NAT_MODULE_ORDER,
    ipfw_nat_init, NULL);
VNET_SYSINIT(vnet_ipfw_nat_init, IPFW_NAT_SI_SUB_FIREWALL, IPFW_NAT_VNET_ORDER,
    vnet_ipfw_nat_init, NULL);

SYSUNINIT(ipfw_nat_destroy, IPFW_NAT_SI_SUB_FIREWALL, IPFW_NAT_MODULE_ORDER,
    ipfw_nat_destroy, NULL);
VNET_SYSUNINIT(vnet_ipfw_nat_uninit, IPFW_NAT_SI_SUB_FIREWALL,
    IPFW_NAT_VNET_ORDER, vnet_ipfw_nat_uninit, NULL);

/* end of file */
