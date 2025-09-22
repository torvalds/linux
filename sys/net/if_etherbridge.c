/*	$OpenBSD: if_etherbridge.c,v 1.8 2025/07/07 02:28:50 jsg Exp $ */

/*
 * Copyright (c) 2018, 2021 David Gwynne <dlg@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/timeout.h>
#include <sys/pool.h>
#include <sys/tree.h>

#include <net/if.h>
#include <net/toeplitz.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

/* for bridge stuff */
#include <net/if_bridge.h>

#include <net/if_etherbridge.h>

static inline void	ebe_rele(struct eb_entry *);
static void		ebe_free(void *);

static void		etherbridge_age(void *);

RBT_PROTOTYPE(eb_tree, eb_entry, ebe_tentry, ebt_cmp);

static struct pool	eb_entry_pool;

static inline int
eb_port_eq(struct etherbridge *eb, void *a, void *b)
{
	return ((*eb->eb_ops->eb_op_port_eq)(eb->eb_cookie, a, b));
}

static inline void *
eb_port_take(struct etherbridge *eb, void *port)
{
	return ((*eb->eb_ops->eb_op_port_take)(eb->eb_cookie, port));
}

static inline void
eb_port_rele(struct etherbridge *eb, void *port)
{
	return ((*eb->eb_ops->eb_op_port_rele)(eb->eb_cookie, port));
}

static inline size_t
eb_port_ifname(struct etherbridge *eb, char *dst, size_t len, void *port)
{
	return ((*eb->eb_ops->eb_op_port_ifname)(eb->eb_cookie, dst, len,
	    port));
}

static inline void
eb_port_sa(struct etherbridge *eb, struct sockaddr_storage *ss, void *port)
{
	(*eb->eb_ops->eb_op_port_sa)(eb->eb_cookie, ss, port);
}

int
etherbridge_init(struct etherbridge *eb, const char *name,
    const struct etherbridge_ops *ops, void *cookie)
{
	size_t i;

	if (eb_entry_pool.pr_size == 0) {
		pool_init(&eb_entry_pool, sizeof(struct eb_entry),
		    0, IPL_SOFTNET, 0, "ebepl", NULL);
	}

	eb->eb_table = mallocarray(ETHERBRIDGE_TABLE_SIZE,
	    sizeof(*eb->eb_table), M_DEVBUF, M_WAITOK|M_CANFAIL);
	if (eb->eb_table == NULL)
		return (ENOMEM);

	eb->eb_name = name;
	eb->eb_ops = ops;
	eb->eb_cookie = cookie;

	mtx_init(&eb->eb_lock, IPL_SOFTNET);
	RBT_INIT(eb_tree, &eb->eb_tree);

	eb->eb_num = 0;
	eb->eb_max = 100;
	eb->eb_max_age = 240;
	timeout_set(&eb->eb_tmo_age, etherbridge_age, eb);

	for (i = 0; i < ETHERBRIDGE_TABLE_SIZE; i++) {
		struct eb_list *ebl = &eb->eb_table[i];
		SMR_TAILQ_INIT(ebl);
	}

	return (0);
}

int
etherbridge_up(struct etherbridge *eb)
{
	etherbridge_age(eb);

	return (0);
}

int
etherbridge_down(struct etherbridge *eb)
{
	smr_barrier();

	return (0);
}

void
etherbridge_destroy(struct etherbridge *eb)
{
	struct eb_entry *ebe, *nebe;

	/* XXX assume that nothing will calling etherbridge_map now */

	timeout_del_barrier(&eb->eb_tmo_age);

	free(eb->eb_table, M_DEVBUF,
	    ETHERBRIDGE_TABLE_SIZE * sizeof(*eb->eb_table));

	RBT_FOREACH_SAFE(ebe, eb_tree, &eb->eb_tree, nebe) {
		RBT_REMOVE(eb_tree, &eb->eb_tree, ebe);
		ebe_free(ebe);
	}
}

static struct eb_list *
etherbridge_list(struct etherbridge *eb, uint64_t eba)
{
	uint16_t hash;

	hash = stoeplitz_h64(eba) & ETHERBRIDGE_TABLE_MASK;

	return (&eb->eb_table[hash]);
}

static struct eb_entry *
ebl_find(struct eb_list *ebl, uint64_t eba)
{
	struct eb_entry *ebe;

	SMR_TAILQ_FOREACH(ebe, ebl, ebe_lentry) {
		if (ebe->ebe_addr == eba)
			return (ebe);
	}

	return (NULL);
}

static inline void
ebl_insert(struct eb_list *ebl, struct eb_entry *ebe)
{
	SMR_TAILQ_INSERT_TAIL_LOCKED(ebl, ebe, ebe_lentry);
}

static inline void
ebl_remove(struct eb_list *ebl, struct eb_entry *ebe)
{
	SMR_TAILQ_REMOVE_LOCKED(ebl, ebe, ebe_lentry);
}

static inline int
ebt_cmp(const struct eb_entry *aebe, const struct eb_entry *bebe)
{
	if (aebe->ebe_addr > bebe->ebe_addr)
		return (1);
	if (aebe->ebe_addr < bebe->ebe_addr)
		return (-1);
	return (0);
}

RBT_GENERATE(eb_tree, eb_entry, ebe_tentry, ebt_cmp);

static inline struct eb_entry *
ebt_insert(struct etherbridge *eb, struct eb_entry *ebe)
{
	return (RBT_INSERT(eb_tree, &eb->eb_tree, ebe));
}

static inline struct eb_entry *
ebt_find(struct etherbridge *eb, const struct eb_entry *ebe)
{
	return (RBT_FIND(eb_tree, &eb->eb_tree, ebe));
}

static inline void
ebt_replace(struct etherbridge *eb, struct eb_entry *oebe,
    struct eb_entry *nebe)
{
	struct eb_entry *rvebe;

	RBT_REMOVE(eb_tree, &eb->eb_tree, oebe);
	rvebe = RBT_INSERT(eb_tree, &eb->eb_tree, nebe);
	KASSERTMSG(rvebe == NULL, "ebt_replace eb %p nebe %p rvebe %p",
	    eb, nebe, rvebe);
}

static inline void
ebt_remove(struct etherbridge *eb, struct eb_entry *ebe)
{
	RBT_REMOVE(eb_tree, &eb->eb_tree, ebe);
}

static inline void
ebe_rele(struct eb_entry *ebe)
{
	smr_call(&ebe->ebe_smr_entry, ebe_free, ebe);
}

static void
ebe_free(void *arg)
{
	struct eb_entry *ebe = arg;
	struct etherbridge *eb = ebe->ebe_etherbridge;

	eb_port_rele(eb, ebe->ebe_port);
	pool_put(&eb_entry_pool, ebe);
}

void *
etherbridge_resolve_ea(struct etherbridge *eb,
    const struct ether_addr *ea)
{
	return (etherbridge_resolve(eb, ether_addr_to_e64(ea)));
}

void *
etherbridge_resolve(struct etherbridge *eb, uint64_t eba)
{
	struct eb_list *ebl = etherbridge_list(eb, eba);
	struct eb_entry *ebe;

	SMR_ASSERT_CRITICAL();

	ebe = ebl_find(ebl, eba);
	if (ebe != NULL) {
		if (ebe->ebe_type == EBE_DYNAMIC) {
			int diff = getuptime() - ebe->ebe_age;
			if (diff > eb->eb_max_age)
				return (NULL);
		}

		return (ebe->ebe_port);
	}

	return (NULL);
}

void
etherbridge_map_ea(struct etherbridge *eb, void *port,
    const struct ether_addr *ea)
{
	etherbridge_map(eb, port, ether_addr_to_e64(ea));
}

void
etherbridge_map(struct etherbridge *eb, void *port, uint64_t eba)
{
	struct eb_list *ebl;
	struct eb_entry *oebe, *nebe;
	unsigned int num;
	void *nport;
	int new = 0;
	time_t now;

	if (ETH64_IS_MULTICAST(eba) || ETH64_IS_ANYADDR(eba))
		return;

	now = getuptime();
	ebl = etherbridge_list(eb, eba);

	smr_read_enter();
	oebe = ebl_find(ebl, eba);
	if (oebe == NULL) {
		/*
		 * peek at the space to see if it's worth trying
		 * to make a new entry.
		 */
		if (eb->eb_num < eb->eb_max)
			new = 1;
	} else {
		if (oebe->ebe_age != now)
			oebe->ebe_age = now;

		/* does this entry need to be replaced? */
		if (oebe->ebe_type == EBE_DYNAMIC &&
		    !eb_port_eq(eb, oebe->ebe_port, port))
			new = 1;
	}
	smr_read_leave();

	if (!new)
		return;

	nport = eb_port_take(eb, port);
	if (nport == NULL) {
		/* XXX should we remove the old one and flood? */
		return;
	}

	nebe = pool_get(&eb_entry_pool, PR_NOWAIT);
	if (nebe == NULL) {
		/* XXX should we remove the old one and flood? */
		eb_port_rele(eb, nport);
		return;
	}

	smr_init(&nebe->ebe_smr_entry);
	nebe->ebe_etherbridge = eb;

	nebe->ebe_addr = eba;
	nebe->ebe_port = nport;
	nebe->ebe_type = EBE_DYNAMIC;
	nebe->ebe_age = now;

	mtx_enter(&eb->eb_lock);
	oebe = ebt_find(eb, nebe);
	if (oebe == NULL) {
		num = eb->eb_num + 1;
		if (num <= eb->eb_max) {
			ebl_insert(ebl, nebe);

			oebe = ebt_insert(eb, nebe);
			if (oebe != NULL) {
				panic("etherbridge %p changed while locked",
				    eb);
			}

			/* great success */
			eb->eb_num = num;
			nebe = NULL; /* give ref to table */
		}
	} else if (oebe->ebe_type == EBE_DYNAMIC) {
		/* do the update */
		ebl_insert(ebl, nebe);

		ebl_remove(ebl, oebe);
		ebt_replace(eb, oebe, nebe);

		nebe = NULL; /* give ref to table */
	} else {
		/*
		 * oebe is not a dynamic entry, so don't replace it.
		 */
		oebe = NULL;
	}
	mtx_leave(&eb->eb_lock);

	if (nebe != NULL) {
		/*
		 * the new entry didn't make it into the
		 * table so it can be freed directly.
		 */
		ebe_free(nebe);
	}

	if (oebe != NULL) {
		/*
		 * we replaced this entry, it needs to be released.
		 */
		ebe_rele(oebe);
	}
}

int
etherbridge_add_addr(struct etherbridge *eb, void *port,
    const struct ether_addr *ea, unsigned int type)
{
	uint64_t eba = ether_addr_to_e64(ea);
	struct eb_list *ebl;
	struct eb_entry *nebe;
	unsigned int num;
	void *nport;
	int error = 0;

	if (ETH64_IS_MULTICAST(eba) || ETH64_IS_ANYADDR(eba))
		return (EADDRNOTAVAIL);

	nport = eb_port_take(eb, port);
	if (nport == NULL)
		return (ENOMEM);

	nebe = pool_get(&eb_entry_pool, PR_NOWAIT);
	if (nebe == NULL) {
		eb_port_rele(eb, nport);
		return (ENOMEM);
	}

	smr_init(&nebe->ebe_smr_entry);
	nebe->ebe_etherbridge = eb;

	nebe->ebe_addr = eba;
	nebe->ebe_port = nport;
	nebe->ebe_type = type;
	nebe->ebe_age = getuptime();

	ebl = etherbridge_list(eb, eba);

	mtx_enter(&eb->eb_lock);
	num = eb->eb_num + 1;
	if (num >= eb->eb_max)
		error = ENOSPC;
	else if (ebt_insert(eb, nebe) != NULL)
		error = EADDRINUSE;
	else {
		/* we win, do the insert */
		ebl_insert(ebl, nebe); /* give the ref to etherbridge */
		eb->eb_num = num;
	}
	mtx_leave(&eb->eb_lock);

	if (error != 0) {
		/*
		 * the new entry didn't make it into the
		 * table, so it can be freed directly.
		 */
		ebe_free(nebe);
	}

	return (error);
}
int
etherbridge_del_addr(struct etherbridge *eb, const struct ether_addr *ea)
{
	uint64_t eba = ether_addr_to_e64(ea);
	struct eb_list *ebl;
	struct eb_entry *oebe;
	const struct eb_entry key = {
		.ebe_addr = eba,
	};
	int error = 0;

	ebl = etherbridge_list(eb, eba);

	mtx_enter(&eb->eb_lock);
	oebe = ebt_find(eb, &key);
	if (oebe == NULL)
		error = ESRCH;
	else {
		KASSERT(eb->eb_num > 0);
		eb->eb_num--;

		ebl_remove(ebl, oebe); /* it's our ref now */
		ebt_remove(eb, oebe);
	}
	mtx_leave(&eb->eb_lock);

	if (oebe != NULL)
		ebe_rele(oebe);

	return (error);
}

static void
etherbridge_age(void *arg)
{
	struct etherbridge *eb = arg;
	struct eb_entry *ebe, *nebe;
	struct eb_queue ebq = TAILQ_HEAD_INITIALIZER(ebq);
	int diff;
	unsigned int now = getuptime();
	size_t i;

	timeout_add_sec(&eb->eb_tmo_age, 100);

	for (i = 0; i < ETHERBRIDGE_TABLE_SIZE; i++) {
		struct eb_list *ebl = &eb->eb_table[i];
#if 0
		if (SMR_TAILQ_EMPTY(ebl));
			continue;
#endif

		mtx_enter(&eb->eb_lock); /* don't block map too much */
		SMR_TAILQ_FOREACH_SAFE_LOCKED(ebe, ebl, ebe_lentry, nebe) {
			if (ebe->ebe_type != EBE_DYNAMIC)
				continue;

			diff = now - ebe->ebe_age;
			if (diff < eb->eb_max_age)
				continue;

			ebl_remove(ebl, ebe);
			ebt_remove(eb, ebe);
			eb->eb_num--;

			/* we own the tables ref now */

			TAILQ_INSERT_TAIL(&ebq, ebe, ebe_qentry);
		}
		mtx_leave(&eb->eb_lock);
	}

	TAILQ_FOREACH_SAFE(ebe, &ebq, ebe_qentry, nebe) {
		TAILQ_REMOVE(&ebq, ebe, ebe_qentry);
		ebe_rele(ebe);
	}
}

void
etherbridge_detach_port(struct etherbridge *eb, void *port)
{
	struct eb_entry *ebe, *nebe;
	struct eb_queue ebq = TAILQ_HEAD_INITIALIZER(ebq);
	size_t i;

	for (i = 0; i < ETHERBRIDGE_TABLE_SIZE; i++) {
		struct eb_list *ebl = &eb->eb_table[i];

		mtx_enter(&eb->eb_lock); /* don't block map too much */
		SMR_TAILQ_FOREACH_SAFE_LOCKED(ebe, ebl, ebe_lentry, nebe) {
			if (!eb_port_eq(eb, ebe->ebe_port, port))
				continue;

			ebl_remove(ebl, ebe);
			ebt_remove(eb, ebe);
			eb->eb_num--;

			/* we own the tables ref now */

			TAILQ_INSERT_TAIL(&ebq, ebe, ebe_qentry);
		}
		mtx_leave(&eb->eb_lock);
	}

	if (TAILQ_EMPTY(&ebq))
		return;

	/*
	 * do one smr barrier for all the entries rather than an
	 * smr_call each.
	 */
	smr_barrier();

	TAILQ_FOREACH_SAFE(ebe, &ebq, ebe_qentry, nebe) {
		TAILQ_REMOVE(&ebq, ebe, ebe_qentry);
		ebe_free(ebe);
	}
}

void
etherbridge_flush(struct etherbridge *eb, uint32_t flags)
{
	struct eb_entry *ebe, *nebe;
	struct eb_queue ebq = TAILQ_HEAD_INITIALIZER(ebq);
	size_t i;

	for (i = 0; i < ETHERBRIDGE_TABLE_SIZE; i++) {
		struct eb_list *ebl = &eb->eb_table[i];

		mtx_enter(&eb->eb_lock); /* don't block map too much */
		SMR_TAILQ_FOREACH_SAFE_LOCKED(ebe, ebl, ebe_lentry, nebe) {
			if (flags == IFBF_FLUSHDYN &&
			    ebe->ebe_type != EBE_DYNAMIC)
				continue;

			ebl_remove(ebl, ebe);
			ebt_remove(eb, ebe);
			eb->eb_num--;

			/* we own the tables ref now */

			TAILQ_INSERT_TAIL(&ebq, ebe, ebe_qentry);
		}
		mtx_leave(&eb->eb_lock);
	}

	if (TAILQ_EMPTY(&ebq))
		return;

	/*
	 * do one smr barrier for all the entries rather than an
	 * smr_call each.
	 */
	smr_barrier();

	TAILQ_FOREACH_SAFE(ebe, &ebq, ebe_qentry, nebe) {
		TAILQ_REMOVE(&ebq, ebe, ebe_qentry);
		ebe_free(ebe);
	}
}

int
etherbridge_rtfind(struct etherbridge *eb, struct ifbaconf *baconf)
{
	struct eb_entry *ebe;
	struct ifbareq bareq;
	caddr_t buf;
	size_t len, nlen;
	time_t age, now = getuptime();
	int error;

	if (baconf->ifbac_len == 0) {
		/* single read is atomic */
		baconf->ifbac_len = eb->eb_num * sizeof(bareq);
		return (0);
	}

	buf = malloc(baconf->ifbac_len, M_TEMP, M_WAITOK|M_CANFAIL);
	if (buf == NULL)
		return (ENOMEM);
	len = 0;

	mtx_enter(&eb->eb_lock);
	RBT_FOREACH(ebe, eb_tree, &eb->eb_tree) {
		nlen = len + sizeof(bareq);
		if (nlen > baconf->ifbac_len)
			break;

		strlcpy(bareq.ifba_name, eb->eb_name,
		    sizeof(bareq.ifba_name));
		eb_port_ifname(eb,
		    bareq.ifba_ifsname, sizeof(bareq.ifba_ifsname),
		    ebe->ebe_port);
		ether_e64_to_addr(&bareq.ifba_dst, ebe->ebe_addr);

		memset(&bareq.ifba_dstsa, 0, sizeof(bareq.ifba_dstsa));
		eb_port_sa(eb, &bareq.ifba_dstsa, ebe->ebe_port);

		switch (ebe->ebe_type) {
		case EBE_DYNAMIC:
			age = now - ebe->ebe_age;
			bareq.ifba_age = MIN(age, 0xff);
			bareq.ifba_flags = IFBAF_DYNAMIC;
			break;
		case EBE_STATIC:
			bareq.ifba_age = 0;
			bareq.ifba_flags = IFBAF_STATIC;
			break;
		}

		memcpy(buf + len, &bareq, sizeof(bareq));
		len = nlen;
	}
	nlen = baconf->ifbac_len;
	baconf->ifbac_len = eb->eb_num * sizeof(bareq);
	mtx_leave(&eb->eb_lock);

	error = copyout(buf, baconf->ifbac_buf, len);
	free(buf, M_TEMP, nlen);

	return (error);
}

int
etherbridge_set_max(struct etherbridge *eb, struct ifbrparam *bparam)
{
	if (bparam->ifbrp_csize < 1 ||
	    bparam->ifbrp_csize > 4096) /* XXX */
		return (EINVAL);

	/* commit */
	eb->eb_max = bparam->ifbrp_csize;

	return (0);
}

int
etherbridge_get_max(struct etherbridge *eb, struct ifbrparam *bparam)
{
	bparam->ifbrp_csize = eb->eb_max;

	return (0);
}

int
etherbridge_set_tmo(struct etherbridge *eb, struct ifbrparam *bparam)
{
	if (bparam->ifbrp_ctime < 8 ||
	    bparam->ifbrp_ctime > 3600)
		return (EINVAL);

	/* commit */
	eb->eb_max_age = bparam->ifbrp_ctime;

	return (0);
}

int
etherbridge_get_tmo(struct etherbridge *eb, struct ifbrparam *bparam)
{
	bparam->ifbrp_ctime = eb->eb_max_age;

	return (0);
}
