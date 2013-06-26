/*
 *	Multicast support for IPv6
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	Based on linux/ipv4/igmp.c and linux/ipv4/ip_sockglue.c
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/* Changes:
 *
 *	yoshfuji	: fix format of router-alert option
 *	YOSHIFUJI Hideaki @USAGI:
 *		Fixed source address for MLD message based on
 *		<draft-ietf-magma-mld-source-05.txt>.
 *	YOSHIFUJI Hideaki @USAGI:
 *		- Ignore Queries for invalid addresses.
 *		- MLD for link-local addresses.
 *	David L Stevens <dlstevens@us.ibm.com>:
 *		- MLDv2 support
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/jiffies.h>
#include <linux/times.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/route.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <net/mld.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>

#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/if_inet6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/inet_common.h>

#include <net/ip6_checksum.h>

/* Set to 3 to get tracing... */
#define MCAST_DEBUG 2

#if MCAST_DEBUG >= 3
#define MDBG(x) printk x
#else
#define MDBG(x)
#endif

/* Ensure that we have struct in6_addr aligned on 32bit word. */
static void *__mld2_query_bugs[] __attribute__((__unused__)) = {
	BUILD_BUG_ON_NULL(offsetof(struct mld2_query, mld2q_srcs) % 4),
	BUILD_BUG_ON_NULL(offsetof(struct mld2_report, mld2r_grec) % 4),
	BUILD_BUG_ON_NULL(offsetof(struct mld2_grec, grec_mca) % 4)
};

static struct in6_addr mld2_all_mcr = MLD2_ALL_MCR_INIT;

/* Big mc list lock for all the sockets */
static DEFINE_SPINLOCK(ipv6_sk_mc_lock);

static void igmp6_join_group(struct ifmcaddr6 *ma);
static void igmp6_leave_group(struct ifmcaddr6 *ma);
static void igmp6_timer_handler(unsigned long data);

static void mld_gq_timer_expire(unsigned long data);
static void mld_ifc_timer_expire(unsigned long data);
static void mld_ifc_event(struct inet6_dev *idev);
static void mld_add_delrec(struct inet6_dev *idev, struct ifmcaddr6 *pmc);
static void mld_del_delrec(struct inet6_dev *idev, const struct in6_addr *addr);
static void mld_clear_delrec(struct inet6_dev *idev);
static int sf_setstate(struct ifmcaddr6 *pmc);
static void sf_markstate(struct ifmcaddr6 *pmc);
static void ip6_mc_clear_src(struct ifmcaddr6 *pmc);
static int ip6_mc_del_src(struct inet6_dev *idev, const struct in6_addr *pmca,
			  int sfmode, int sfcount, const struct in6_addr *psfsrc,
			  int delta);
static int ip6_mc_add_src(struct inet6_dev *idev, const struct in6_addr *pmca,
			  int sfmode, int sfcount, const struct in6_addr *psfsrc,
			  int delta);
static int ip6_mc_leave_src(struct sock *sk, struct ipv6_mc_socklist *iml,
			    struct inet6_dev *idev);


#define IGMP6_UNSOLICITED_IVAL	(10*HZ)
#define MLD_QRV_DEFAULT		2

#define MLD_V1_SEEN(idev) (dev_net((idev)->dev)->ipv6.devconf_all->force_mld_version == 1 || \
		(idev)->cnf.force_mld_version == 1 || \
		((idev)->mc_v1_seen && \
		time_before(jiffies, (idev)->mc_v1_seen)))

#define IPV6_MLD_MAX_MSF	64

int sysctl_mld_max_msf __read_mostly = IPV6_MLD_MAX_MSF;

/*
 *	socket join on multicast group
 */

#define for_each_pmc_rcu(np, pmc)				\
	for (pmc = rcu_dereference(np->ipv6_mc_list);		\
	     pmc != NULL;					\
	     pmc = rcu_dereference(pmc->next))

int ipv6_sock_mc_join(struct sock *sk, int ifindex, const struct in6_addr *addr)
{
	struct net_device *dev = NULL;
	struct ipv6_mc_socklist *mc_lst;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct net *net = sock_net(sk);
	int err;

	if (!ipv6_addr_is_multicast(addr))
		return -EINVAL;

	rcu_read_lock();
	for_each_pmc_rcu(np, mc_lst) {
		if ((ifindex == 0 || mc_lst->ifindex == ifindex) &&
		    ipv6_addr_equal(&mc_lst->addr, addr)) {
			rcu_read_unlock();
			return -EADDRINUSE;
		}
	}
	rcu_read_unlock();

	mc_lst = sock_kmalloc(sk, sizeof(struct ipv6_mc_socklist), GFP_KERNEL);

	if (mc_lst == NULL)
		return -ENOMEM;

	mc_lst->next = NULL;
	mc_lst->addr = *addr;

	rcu_read_lock();
	if (ifindex == 0) {
		struct rt6_info *rt;
		rt = rt6_lookup(net, addr, NULL, 0, 0);
		if (rt) {
			dev = rt->dst.dev;
			ip6_rt_put(rt);
		}
	} else
		dev = dev_get_by_index_rcu(net, ifindex);

	if (dev == NULL) {
		rcu_read_unlock();
		sock_kfree_s(sk, mc_lst, sizeof(*mc_lst));
		return -ENODEV;
	}

	mc_lst->ifindex = dev->ifindex;
	mc_lst->sfmode = MCAST_EXCLUDE;
	rwlock_init(&mc_lst->sflock);
	mc_lst->sflist = NULL;

	/*
	 *	now add/increase the group membership on the device
	 */

	err = ipv6_dev_mc_inc(dev, addr);

	if (err) {
		rcu_read_unlock();
		sock_kfree_s(sk, mc_lst, sizeof(*mc_lst));
		return err;
	}

	spin_lock(&ipv6_sk_mc_lock);
	mc_lst->next = np->ipv6_mc_list;
	rcu_assign_pointer(np->ipv6_mc_list, mc_lst);
	spin_unlock(&ipv6_sk_mc_lock);

	rcu_read_unlock();

	return 0;
}

/*
 *	socket leave on multicast group
 */
int ipv6_sock_mc_drop(struct sock *sk, int ifindex, const struct in6_addr *addr)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct ipv6_mc_socklist *mc_lst;
	struct ipv6_mc_socklist __rcu **lnk;
	struct net *net = sock_net(sk);

	if (!ipv6_addr_is_multicast(addr))
		return -EINVAL;

	spin_lock(&ipv6_sk_mc_lock);
	for (lnk = &np->ipv6_mc_list;
	     (mc_lst = rcu_dereference_protected(*lnk,
			lockdep_is_held(&ipv6_sk_mc_lock))) !=NULL ;
	      lnk = &mc_lst->next) {
		if ((ifindex == 0 || mc_lst->ifindex == ifindex) &&
		    ipv6_addr_equal(&mc_lst->addr, addr)) {
			struct net_device *dev;

			*lnk = mc_lst->next;
			spin_unlock(&ipv6_sk_mc_lock);

			rcu_read_lock();
			dev = dev_get_by_index_rcu(net, mc_lst->ifindex);
			if (dev != NULL) {
				struct inet6_dev *idev = __in6_dev_get(dev);

				(void) ip6_mc_leave_src(sk, mc_lst, idev);
				if (idev)
					__ipv6_dev_mc_dec(idev, &mc_lst->addr);
			} else
				(void) ip6_mc_leave_src(sk, mc_lst, NULL);
			rcu_read_unlock();
			atomic_sub(sizeof(*mc_lst), &sk->sk_omem_alloc);
			kfree_rcu(mc_lst, rcu);
			return 0;
		}
	}
	spin_unlock(&ipv6_sk_mc_lock);

	return -EADDRNOTAVAIL;
}

/* called with rcu_read_lock() */
static struct inet6_dev *ip6_mc_find_dev_rcu(struct net *net,
					     const struct in6_addr *group,
					     int ifindex)
{
	struct net_device *dev = NULL;
	struct inet6_dev *idev = NULL;

	if (ifindex == 0) {
		struct rt6_info *rt = rt6_lookup(net, group, NULL, 0, 0);

		if (rt) {
			dev = rt->dst.dev;
			ip6_rt_put(rt);
		}
	} else
		dev = dev_get_by_index_rcu(net, ifindex);

	if (!dev)
		return NULL;
	idev = __in6_dev_get(dev);
	if (!idev)
		return NULL;
	read_lock_bh(&idev->lock);
	if (idev->dead) {
		read_unlock_bh(&idev->lock);
		return NULL;
	}
	return idev;
}

void ipv6_sock_mc_close(struct sock *sk)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct ipv6_mc_socklist *mc_lst;
	struct net *net = sock_net(sk);

	if (!rcu_access_pointer(np->ipv6_mc_list))
		return;

	spin_lock(&ipv6_sk_mc_lock);
	while ((mc_lst = rcu_dereference_protected(np->ipv6_mc_list,
				lockdep_is_held(&ipv6_sk_mc_lock))) != NULL) {
		struct net_device *dev;

		np->ipv6_mc_list = mc_lst->next;
		spin_unlock(&ipv6_sk_mc_lock);

		rcu_read_lock();
		dev = dev_get_by_index_rcu(net, mc_lst->ifindex);
		if (dev) {
			struct inet6_dev *idev = __in6_dev_get(dev);

			(void) ip6_mc_leave_src(sk, mc_lst, idev);
			if (idev)
				__ipv6_dev_mc_dec(idev, &mc_lst->addr);
		} else
			(void) ip6_mc_leave_src(sk, mc_lst, NULL);
		rcu_read_unlock();

		atomic_sub(sizeof(*mc_lst), &sk->sk_omem_alloc);
		kfree_rcu(mc_lst, rcu);

		spin_lock(&ipv6_sk_mc_lock);
	}
	spin_unlock(&ipv6_sk_mc_lock);
}

int ip6_mc_source(int add, int omode, struct sock *sk,
	struct group_source_req *pgsr)
{
	struct in6_addr *source, *group;
	struct ipv6_mc_socklist *pmc;
	struct inet6_dev *idev;
	struct ipv6_pinfo *inet6 = inet6_sk(sk);
	struct ip6_sf_socklist *psl;
	struct net *net = sock_net(sk);
	int i, j, rv;
	int leavegroup = 0;
	int pmclocked = 0;
	int err;

	source = &((struct sockaddr_in6 *)&pgsr->gsr_source)->sin6_addr;
	group = &((struct sockaddr_in6 *)&pgsr->gsr_group)->sin6_addr;

	if (!ipv6_addr_is_multicast(group))
		return -EINVAL;

	rcu_read_lock();
	idev = ip6_mc_find_dev_rcu(net, group, pgsr->gsr_interface);
	if (!idev) {
		rcu_read_unlock();
		return -ENODEV;
	}

	err = -EADDRNOTAVAIL;

	for_each_pmc_rcu(inet6, pmc) {
		if (pgsr->gsr_interface && pmc->ifindex != pgsr->gsr_interface)
			continue;
		if (ipv6_addr_equal(&pmc->addr, group))
			break;
	}
	if (!pmc) {		/* must have a prior join */
		err = -EINVAL;
		goto done;
	}
	/* if a source filter was set, must be the same mode as before */
	if (pmc->sflist) {
		if (pmc->sfmode != omode) {
			err = -EINVAL;
			goto done;
		}
	} else if (pmc->sfmode != omode) {
		/* allow mode switches for empty-set filters */
		ip6_mc_add_src(idev, group, omode, 0, NULL, 0);
		ip6_mc_del_src(idev, group, pmc->sfmode, 0, NULL, 0);
		pmc->sfmode = omode;
	}

	write_lock(&pmc->sflock);
	pmclocked = 1;

	psl = pmc->sflist;
	if (!add) {
		if (!psl)
			goto done;	/* err = -EADDRNOTAVAIL */
		rv = !0;
		for (i=0; i<psl->sl_count; i++) {
			rv = !ipv6_addr_equal(&psl->sl_addr[i], source);
			if (rv == 0)
				break;
		}
		if (rv)		/* source not found */
			goto done;	/* err = -EADDRNOTAVAIL */

		/* special case - (INCLUDE, empty) == LEAVE_GROUP */
		if (psl->sl_count == 1 && omode == MCAST_INCLUDE) {
			leavegroup = 1;
			goto done;
		}

		/* update the interface filter */
		ip6_mc_del_src(idev, group, omode, 1, source, 1);

		for (j=i+1; j<psl->sl_count; j++)
			psl->sl_addr[j-1] = psl->sl_addr[j];
		psl->sl_count--;
		err = 0;
		goto done;
	}
	/* else, add a new source to the filter */

	if (psl && psl->sl_count >= sysctl_mld_max_msf) {
		err = -ENOBUFS;
		goto done;
	}
	if (!psl || psl->sl_count == psl->sl_max) {
		struct ip6_sf_socklist *newpsl;
		int count = IP6_SFBLOCK;

		if (psl)
			count += psl->sl_max;
		newpsl = sock_kmalloc(sk, IP6_SFLSIZE(count), GFP_ATOMIC);
		if (!newpsl) {
			err = -ENOBUFS;
			goto done;
		}
		newpsl->sl_max = count;
		newpsl->sl_count = count - IP6_SFBLOCK;
		if (psl) {
			for (i=0; i<psl->sl_count; i++)
				newpsl->sl_addr[i] = psl->sl_addr[i];
			sock_kfree_s(sk, psl, IP6_SFLSIZE(psl->sl_max));
		}
		pmc->sflist = psl = newpsl;
	}
	rv = 1;	/* > 0 for insert logic below if sl_count is 0 */
	for (i=0; i<psl->sl_count; i++) {
		rv = !ipv6_addr_equal(&psl->sl_addr[i], source);
		if (rv == 0) /* There is an error in the address. */
			goto done;
	}
	for (j=psl->sl_count-1; j>=i; j--)
		psl->sl_addr[j+1] = psl->sl_addr[j];
	psl->sl_addr[i] = *source;
	psl->sl_count++;
	err = 0;
	/* update the interface list */
	ip6_mc_add_src(idev, group, omode, 1, source, 1);
done:
	if (pmclocked)
		write_unlock(&pmc->sflock);
	read_unlock_bh(&idev->lock);
	rcu_read_unlock();
	if (leavegroup)
		return ipv6_sock_mc_drop(sk, pgsr->gsr_interface, group);
	return err;
}

int ip6_mc_msfilter(struct sock *sk, struct group_filter *gsf)
{
	const struct in6_addr *group;
	struct ipv6_mc_socklist *pmc;
	struct inet6_dev *idev;
	struct ipv6_pinfo *inet6 = inet6_sk(sk);
	struct ip6_sf_socklist *newpsl, *psl;
	struct net *net = sock_net(sk);
	int leavegroup = 0;
	int i, err;

	group = &((struct sockaddr_in6 *)&gsf->gf_group)->sin6_addr;

	if (!ipv6_addr_is_multicast(group))
		return -EINVAL;
	if (gsf->gf_fmode != MCAST_INCLUDE &&
	    gsf->gf_fmode != MCAST_EXCLUDE)
		return -EINVAL;

	rcu_read_lock();
	idev = ip6_mc_find_dev_rcu(net, group, gsf->gf_interface);

	if (!idev) {
		rcu_read_unlock();
		return -ENODEV;
	}

	err = 0;

	if (gsf->gf_fmode == MCAST_INCLUDE && gsf->gf_numsrc == 0) {
		leavegroup = 1;
		goto done;
	}

	for_each_pmc_rcu(inet6, pmc) {
		if (pmc->ifindex != gsf->gf_interface)
			continue;
		if (ipv6_addr_equal(&pmc->addr, group))
			break;
	}
	if (!pmc) {		/* must have a prior join */
		err = -EINVAL;
		goto done;
	}
	if (gsf->gf_numsrc) {
		newpsl = sock_kmalloc(sk, IP6_SFLSIZE(gsf->gf_numsrc),
							  GFP_ATOMIC);
		if (!newpsl) {
			err = -ENOBUFS;
			goto done;
		}
		newpsl->sl_max = newpsl->sl_count = gsf->gf_numsrc;
		for (i=0; i<newpsl->sl_count; ++i) {
			struct sockaddr_in6 *psin6;

			psin6 = (struct sockaddr_in6 *)&gsf->gf_slist[i];
			newpsl->sl_addr[i] = psin6->sin6_addr;
		}
		err = ip6_mc_add_src(idev, group, gsf->gf_fmode,
			newpsl->sl_count, newpsl->sl_addr, 0);
		if (err) {
			sock_kfree_s(sk, newpsl, IP6_SFLSIZE(newpsl->sl_max));
			goto done;
		}
	} else {
		newpsl = NULL;
		(void) ip6_mc_add_src(idev, group, gsf->gf_fmode, 0, NULL, 0);
	}

	write_lock(&pmc->sflock);
	psl = pmc->sflist;
	if (psl) {
		(void) ip6_mc_del_src(idev, group, pmc->sfmode,
			psl->sl_count, psl->sl_addr, 0);
		sock_kfree_s(sk, psl, IP6_SFLSIZE(psl->sl_max));
	} else
		(void) ip6_mc_del_src(idev, group, pmc->sfmode, 0, NULL, 0);
	pmc->sflist = newpsl;
	pmc->sfmode = gsf->gf_fmode;
	write_unlock(&pmc->sflock);
	err = 0;
done:
	read_unlock_bh(&idev->lock);
	rcu_read_unlock();
	if (leavegroup)
		err = ipv6_sock_mc_drop(sk, gsf->gf_interface, group);
	return err;
}

int ip6_mc_msfget(struct sock *sk, struct group_filter *gsf,
	struct group_filter __user *optval, int __user *optlen)
{
	int err, i, count, copycount;
	const struct in6_addr *group;
	struct ipv6_mc_socklist *pmc;
	struct inet6_dev *idev;
	struct ipv6_pinfo *inet6 = inet6_sk(sk);
	struct ip6_sf_socklist *psl;
	struct net *net = sock_net(sk);

	group = &((struct sockaddr_in6 *)&gsf->gf_group)->sin6_addr;

	if (!ipv6_addr_is_multicast(group))
		return -EINVAL;

	rcu_read_lock();
	idev = ip6_mc_find_dev_rcu(net, group, gsf->gf_interface);

	if (!idev) {
		rcu_read_unlock();
		return -ENODEV;
	}

	err = -EADDRNOTAVAIL;
	/*
	 * changes to the ipv6_mc_list require the socket lock and
	 * a read lock on ip6_sk_mc_lock. We have the socket lock,
	 * so reading the list is safe.
	 */

	for_each_pmc_rcu(inet6, pmc) {
		if (pmc->ifindex != gsf->gf_interface)
			continue;
		if (ipv6_addr_equal(group, &pmc->addr))
			break;
	}
	if (!pmc)		/* must have a prior join */
		goto done;
	gsf->gf_fmode = pmc->sfmode;
	psl = pmc->sflist;
	count = psl ? psl->sl_count : 0;
	read_unlock_bh(&idev->lock);
	rcu_read_unlock();

	copycount = count < gsf->gf_numsrc ? count : gsf->gf_numsrc;
	gsf->gf_numsrc = count;
	if (put_user(GROUP_FILTER_SIZE(copycount), optlen) ||
	    copy_to_user(optval, gsf, GROUP_FILTER_SIZE(0))) {
		return -EFAULT;
	}
	/* changes to psl require the socket lock, a read lock on
	 * on ipv6_sk_mc_lock and a write lock on pmc->sflock. We
	 * have the socket lock, so reading here is safe.
	 */
	for (i=0; i<copycount; i++) {
		struct sockaddr_in6 *psin6;
		struct sockaddr_storage ss;

		psin6 = (struct sockaddr_in6 *)&ss;
		memset(&ss, 0, sizeof(ss));
		psin6->sin6_family = AF_INET6;
		psin6->sin6_addr = psl->sl_addr[i];
		if (copy_to_user(&optval->gf_slist[i], &ss, sizeof(ss)))
			return -EFAULT;
	}
	return 0;
done:
	read_unlock_bh(&idev->lock);
	rcu_read_unlock();
	return err;
}

bool inet6_mc_check(struct sock *sk, const struct in6_addr *mc_addr,
		    const struct in6_addr *src_addr)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct ipv6_mc_socklist *mc;
	struct ip6_sf_socklist *psl;
	bool rv = true;

	rcu_read_lock();
	for_each_pmc_rcu(np, mc) {
		if (ipv6_addr_equal(&mc->addr, mc_addr))
			break;
	}
	if (!mc) {
		rcu_read_unlock();
		return true;
	}
	read_lock(&mc->sflock);
	psl = mc->sflist;
	if (!psl) {
		rv = mc->sfmode == MCAST_EXCLUDE;
	} else {
		int i;

		for (i=0; i<psl->sl_count; i++) {
			if (ipv6_addr_equal(&psl->sl_addr[i], src_addr))
				break;
		}
		if (mc->sfmode == MCAST_INCLUDE && i >= psl->sl_count)
			rv = false;
		if (mc->sfmode == MCAST_EXCLUDE && i < psl->sl_count)
			rv = false;
	}
	read_unlock(&mc->sflock);
	rcu_read_unlock();

	return rv;
}

static void ma_put(struct ifmcaddr6 *mc)
{
	if (atomic_dec_and_test(&mc->mca_refcnt)) {
		in6_dev_put(mc->idev);
		kfree(mc);
	}
}

static void igmp6_group_added(struct ifmcaddr6 *mc)
{
	struct net_device *dev = mc->idev->dev;
	char buf[MAX_ADDR_LEN];

	if (IPV6_ADDR_MC_SCOPE(&mc->mca_addr) <
	    IPV6_ADDR_SCOPE_LINKLOCAL)
		return;

	spin_lock_bh(&mc->mca_lock);
	if (!(mc->mca_flags&MAF_LOADED)) {
		mc->mca_flags |= MAF_LOADED;
		if (ndisc_mc_map(&mc->mca_addr, buf, dev, 0) == 0)
			dev_mc_add(dev, buf);
	}
	spin_unlock_bh(&mc->mca_lock);

	if (!(dev->flags & IFF_UP) || (mc->mca_flags & MAF_NOREPORT))
		return;

	if (MLD_V1_SEEN(mc->idev)) {
		igmp6_join_group(mc);
		return;
	}
	/* else v2 */

	mc->mca_crcount = mc->idev->mc_qrv;
	mld_ifc_event(mc->idev);
}

static void igmp6_group_dropped(struct ifmcaddr6 *mc)
{
	struct net_device *dev = mc->idev->dev;
	char buf[MAX_ADDR_LEN];

	if (IPV6_ADDR_MC_SCOPE(&mc->mca_addr) <
	    IPV6_ADDR_SCOPE_LINKLOCAL)
		return;

	spin_lock_bh(&mc->mca_lock);
	if (mc->mca_flags&MAF_LOADED) {
		mc->mca_flags &= ~MAF_LOADED;
		if (ndisc_mc_map(&mc->mca_addr, buf, dev, 0) == 0)
			dev_mc_del(dev, buf);
	}

	if (mc->mca_flags & MAF_NOREPORT)
		goto done;
	spin_unlock_bh(&mc->mca_lock);

	if (!mc->idev->dead)
		igmp6_leave_group(mc);

	spin_lock_bh(&mc->mca_lock);
	if (del_timer(&mc->mca_timer))
		atomic_dec(&mc->mca_refcnt);
done:
	ip6_mc_clear_src(mc);
	spin_unlock_bh(&mc->mca_lock);
}

/*
 * deleted ifmcaddr6 manipulation
 */
static void mld_add_delrec(struct inet6_dev *idev, struct ifmcaddr6 *im)
{
	struct ifmcaddr6 *pmc;

	/* this is an "ifmcaddr6" for convenience; only the fields below
	 * are actually used. In particular, the refcnt and users are not
	 * used for management of the delete list. Using the same structure
	 * for deleted items allows change reports to use common code with
	 * non-deleted or query-response MCA's.
	 */
	pmc = kzalloc(sizeof(*pmc), GFP_ATOMIC);
	if (!pmc)
		return;

	spin_lock_bh(&im->mca_lock);
	spin_lock_init(&pmc->mca_lock);
	pmc->idev = im->idev;
	in6_dev_hold(idev);
	pmc->mca_addr = im->mca_addr;
	pmc->mca_crcount = idev->mc_qrv;
	pmc->mca_sfmode = im->mca_sfmode;
	if (pmc->mca_sfmode == MCAST_INCLUDE) {
		struct ip6_sf_list *psf;

		pmc->mca_tomb = im->mca_tomb;
		pmc->mca_sources = im->mca_sources;
		im->mca_tomb = im->mca_sources = NULL;
		for (psf=pmc->mca_sources; psf; psf=psf->sf_next)
			psf->sf_crcount = pmc->mca_crcount;
	}
	spin_unlock_bh(&im->mca_lock);

	spin_lock_bh(&idev->mc_lock);
	pmc->next = idev->mc_tomb;
	idev->mc_tomb = pmc;
	spin_unlock_bh(&idev->mc_lock);
}

static void mld_del_delrec(struct inet6_dev *idev, const struct in6_addr *pmca)
{
	struct ifmcaddr6 *pmc, *pmc_prev;
	struct ip6_sf_list *psf, *psf_next;

	spin_lock_bh(&idev->mc_lock);
	pmc_prev = NULL;
	for (pmc=idev->mc_tomb; pmc; pmc=pmc->next) {
		if (ipv6_addr_equal(&pmc->mca_addr, pmca))
			break;
		pmc_prev = pmc;
	}
	if (pmc) {
		if (pmc_prev)
			pmc_prev->next = pmc->next;
		else
			idev->mc_tomb = pmc->next;
	}
	spin_unlock_bh(&idev->mc_lock);

	if (pmc) {
		for (psf=pmc->mca_tomb; psf; psf=psf_next) {
			psf_next = psf->sf_next;
			kfree(psf);
		}
		in6_dev_put(pmc->idev);
		kfree(pmc);
	}
}

static void mld_clear_delrec(struct inet6_dev *idev)
{
	struct ifmcaddr6 *pmc, *nextpmc;

	spin_lock_bh(&idev->mc_lock);
	pmc = idev->mc_tomb;
	idev->mc_tomb = NULL;
	spin_unlock_bh(&idev->mc_lock);

	for (; pmc; pmc = nextpmc) {
		nextpmc = pmc->next;
		ip6_mc_clear_src(pmc);
		in6_dev_put(pmc->idev);
		kfree(pmc);
	}

	/* clear dead sources, too */
	read_lock_bh(&idev->lock);
	for (pmc=idev->mc_list; pmc; pmc=pmc->next) {
		struct ip6_sf_list *psf, *psf_next;

		spin_lock_bh(&pmc->mca_lock);
		psf = pmc->mca_tomb;
		pmc->mca_tomb = NULL;
		spin_unlock_bh(&pmc->mca_lock);
		for (; psf; psf=psf_next) {
			psf_next = psf->sf_next;
			kfree(psf);
		}
	}
	read_unlock_bh(&idev->lock);
}


/*
 *	device multicast group inc (add if not found)
 */
int ipv6_dev_mc_inc(struct net_device *dev, const struct in6_addr *addr)
{
	struct ifmcaddr6 *mc;
	struct inet6_dev *idev;

	/* we need to take a reference on idev */
	idev = in6_dev_get(dev);

	if (idev == NULL)
		return -EINVAL;

	write_lock_bh(&idev->lock);
	if (idev->dead) {
		write_unlock_bh(&idev->lock);
		in6_dev_put(idev);
		return -ENODEV;
	}

	for (mc = idev->mc_list; mc; mc = mc->next) {
		if (ipv6_addr_equal(&mc->mca_addr, addr)) {
			mc->mca_users++;
			write_unlock_bh(&idev->lock);
			ip6_mc_add_src(idev, &mc->mca_addr, MCAST_EXCLUDE, 0,
				NULL, 0);
			in6_dev_put(idev);
			return 0;
		}
	}

	/*
	 *	not found: create a new one.
	 */

	mc = kzalloc(sizeof(struct ifmcaddr6), GFP_ATOMIC);

	if (mc == NULL) {
		write_unlock_bh(&idev->lock);
		in6_dev_put(idev);
		return -ENOMEM;
	}

	setup_timer(&mc->mca_timer, igmp6_timer_handler, (unsigned long)mc);

	mc->mca_addr = *addr;
	mc->idev = idev; /* (reference taken) */
	mc->mca_users = 1;
	/* mca_stamp should be updated upon changes */
	mc->mca_cstamp = mc->mca_tstamp = jiffies;
	atomic_set(&mc->mca_refcnt, 2);
	spin_lock_init(&mc->mca_lock);

	/* initial mode is (EX, empty) */
	mc->mca_sfmode = MCAST_EXCLUDE;
	mc->mca_sfcount[MCAST_EXCLUDE] = 1;

	if (ipv6_addr_is_ll_all_nodes(&mc->mca_addr) ||
	    IPV6_ADDR_MC_SCOPE(&mc->mca_addr) < IPV6_ADDR_SCOPE_LINKLOCAL)
		mc->mca_flags |= MAF_NOREPORT;

	mc->next = idev->mc_list;
	idev->mc_list = mc;
	write_unlock_bh(&idev->lock);

	mld_del_delrec(idev, &mc->mca_addr);
	igmp6_group_added(mc);
	ma_put(mc);
	return 0;
}

/*
 *	device multicast group del
 */
int __ipv6_dev_mc_dec(struct inet6_dev *idev, const struct in6_addr *addr)
{
	struct ifmcaddr6 *ma, **map;

	write_lock_bh(&idev->lock);
	for (map = &idev->mc_list; (ma=*map) != NULL; map = &ma->next) {
		if (ipv6_addr_equal(&ma->mca_addr, addr)) {
			if (--ma->mca_users == 0) {
				*map = ma->next;
				write_unlock_bh(&idev->lock);

				igmp6_group_dropped(ma);

				ma_put(ma);
				return 0;
			}
			write_unlock_bh(&idev->lock);
			return 0;
		}
	}
	write_unlock_bh(&idev->lock);

	return -ENOENT;
}

int ipv6_dev_mc_dec(struct net_device *dev, const struct in6_addr *addr)
{
	struct inet6_dev *idev;
	int err;

	rcu_read_lock();

	idev = __in6_dev_get(dev);
	if (!idev)
		err = -ENODEV;
	else
		err = __ipv6_dev_mc_dec(idev, addr);

	rcu_read_unlock();
	return err;
}

/*
 *	check if the interface/address pair is valid
 */
bool ipv6_chk_mcast_addr(struct net_device *dev, const struct in6_addr *group,
			 const struct in6_addr *src_addr)
{
	struct inet6_dev *idev;
	struct ifmcaddr6 *mc;
	bool rv = false;

	rcu_read_lock();
	idev = __in6_dev_get(dev);
	if (idev) {
		read_lock_bh(&idev->lock);
		for (mc = idev->mc_list; mc; mc=mc->next) {
			if (ipv6_addr_equal(&mc->mca_addr, group))
				break;
		}
		if (mc) {
			if (src_addr && !ipv6_addr_any(src_addr)) {
				struct ip6_sf_list *psf;

				spin_lock_bh(&mc->mca_lock);
				for (psf=mc->mca_sources;psf;psf=psf->sf_next) {
					if (ipv6_addr_equal(&psf->sf_addr, src_addr))
						break;
				}
				if (psf)
					rv = psf->sf_count[MCAST_INCLUDE] ||
						psf->sf_count[MCAST_EXCLUDE] !=
						mc->mca_sfcount[MCAST_EXCLUDE];
				else
					rv = mc->mca_sfcount[MCAST_EXCLUDE] !=0;
				spin_unlock_bh(&mc->mca_lock);
			} else
				rv = true; /* don't filter unspecified source */
		}
		read_unlock_bh(&idev->lock);
	}
	rcu_read_unlock();
	return rv;
}

static void mld_gq_start_timer(struct inet6_dev *idev)
{
	int tv = net_random() % idev->mc_maxdelay;

	idev->mc_gq_running = 1;
	if (!mod_timer(&idev->mc_gq_timer, jiffies+tv+2))
		in6_dev_hold(idev);
}

static void mld_ifc_start_timer(struct inet6_dev *idev, int delay)
{
	int tv = net_random() % delay;

	if (!mod_timer(&idev->mc_ifc_timer, jiffies+tv+2))
		in6_dev_hold(idev);
}

static void mld_dad_start_timer(struct inet6_dev *idev, int delay)
{
	int tv = net_random() % delay;

	if (!mod_timer(&idev->mc_dad_timer, jiffies+tv+2))
		in6_dev_hold(idev);
}

/*
 *	IGMP handling (alias multicast ICMPv6 messages)
 */

static void igmp6_group_queried(struct ifmcaddr6 *ma, unsigned long resptime)
{
	unsigned long delay = resptime;

	/* Do not start timer for these addresses */
	if (ipv6_addr_is_ll_all_nodes(&ma->mca_addr) ||
	    IPV6_ADDR_MC_SCOPE(&ma->mca_addr) < IPV6_ADDR_SCOPE_LINKLOCAL)
		return;

	if (del_timer(&ma->mca_timer)) {
		atomic_dec(&ma->mca_refcnt);
		delay = ma->mca_timer.expires - jiffies;
	}

	if (delay >= resptime) {
		if (resptime)
			delay = net_random() % resptime;
		else
			delay = 1;
	}
	ma->mca_timer.expires = jiffies + delay;
	if (!mod_timer(&ma->mca_timer, jiffies + delay))
		atomic_inc(&ma->mca_refcnt);
	ma->mca_flags |= MAF_TIMER_RUNNING;
}

/* mark EXCLUDE-mode sources */
static bool mld_xmarksources(struct ifmcaddr6 *pmc, int nsrcs,
			     const struct in6_addr *srcs)
{
	struct ip6_sf_list *psf;
	int i, scount;

	scount = 0;
	for (psf=pmc->mca_sources; psf; psf=psf->sf_next) {
		if (scount == nsrcs)
			break;
		for (i=0; i<nsrcs; i++) {
			/* skip inactive filters */
			if (psf->sf_count[MCAST_INCLUDE] ||
			    pmc->mca_sfcount[MCAST_EXCLUDE] !=
			    psf->sf_count[MCAST_EXCLUDE])
				break;
			if (ipv6_addr_equal(&srcs[i], &psf->sf_addr)) {
				scount++;
				break;
			}
		}
	}
	pmc->mca_flags &= ~MAF_GSQUERY;
	if (scount == nsrcs)	/* all sources excluded */
		return false;
	return true;
}

static bool mld_marksources(struct ifmcaddr6 *pmc, int nsrcs,
			    const struct in6_addr *srcs)
{
	struct ip6_sf_list *psf;
	int i, scount;

	if (pmc->mca_sfmode == MCAST_EXCLUDE)
		return mld_xmarksources(pmc, nsrcs, srcs);

	/* mark INCLUDE-mode sources */

	scount = 0;
	for (psf=pmc->mca_sources; psf; psf=psf->sf_next) {
		if (scount == nsrcs)
			break;
		for (i=0; i<nsrcs; i++) {
			if (ipv6_addr_equal(&srcs[i], &psf->sf_addr)) {
				psf->sf_gsresp = 1;
				scount++;
				break;
			}
		}
	}
	if (!scount) {
		pmc->mca_flags &= ~MAF_GSQUERY;
		return false;
	}
	pmc->mca_flags |= MAF_GSQUERY;
	return true;
}

/* called with rcu_read_lock() */
int igmp6_event_query(struct sk_buff *skb)
{
	struct mld2_query *mlh2 = NULL;
	struct ifmcaddr6 *ma;
	const struct in6_addr *group;
	unsigned long max_delay;
	struct inet6_dev *idev;
	struct mld_msg *mld;
	int group_type;
	int mark = 0;
	int len;

	if (!pskb_may_pull(skb, sizeof(struct in6_addr)))
		return -EINVAL;

	/* compute payload length excluding extension headers */
	len = ntohs(ipv6_hdr(skb)->payload_len) + sizeof(struct ipv6hdr);
	len -= skb_network_header_len(skb);

	/* Drop queries with not link local source */
	if (!(ipv6_addr_type(&ipv6_hdr(skb)->saddr) & IPV6_ADDR_LINKLOCAL))
		return -EINVAL;

	idev = __in6_dev_get(skb->dev);

	if (idev == NULL)
		return 0;

	mld = (struct mld_msg *)icmp6_hdr(skb);
	group = &mld->mld_mca;
	group_type = ipv6_addr_type(group);

	if (group_type != IPV6_ADDR_ANY &&
	    !(group_type&IPV6_ADDR_MULTICAST))
		return -EINVAL;

	if (len == 24) {
		int switchback;
		/* MLDv1 router present */

		/* Translate milliseconds to jiffies */
		max_delay = (ntohs(mld->mld_maxdelay)*HZ)/1000;

		switchback = (idev->mc_qrv + 1) * max_delay;
		idev->mc_v1_seen = jiffies + switchback;

		/* cancel the interface change timer */
		idev->mc_ifc_count = 0;
		if (del_timer(&idev->mc_ifc_timer))
			__in6_dev_put(idev);
		/* clear deleted report items */
		mld_clear_delrec(idev);
	} else if (len >= 28) {
		int srcs_offset = sizeof(struct mld2_query) -
				  sizeof(struct icmp6hdr);
		if (!pskb_may_pull(skb, srcs_offset))
			return -EINVAL;

		mlh2 = (struct mld2_query *)skb_transport_header(skb);
		max_delay = (MLDV2_MRC(ntohs(mlh2->mld2q_mrc))*HZ)/1000;
		if (!max_delay)
			max_delay = 1;
		idev->mc_maxdelay = max_delay;
		if (mlh2->mld2q_qrv)
			idev->mc_qrv = mlh2->mld2q_qrv;
		if (group_type == IPV6_ADDR_ANY) { /* general query */
			if (mlh2->mld2q_nsrcs)
				return -EINVAL; /* no sources allowed */

			mld_gq_start_timer(idev);
			return 0;
		}
		/* mark sources to include, if group & source-specific */
		if (mlh2->mld2q_nsrcs != 0) {
			if (!pskb_may_pull(skb, srcs_offset +
			    ntohs(mlh2->mld2q_nsrcs) * sizeof(struct in6_addr)))
				return -EINVAL;

			mlh2 = (struct mld2_query *)skb_transport_header(skb);
			mark = 1;
		}
	} else
		return -EINVAL;

	read_lock_bh(&idev->lock);
	if (group_type == IPV6_ADDR_ANY) {
		for (ma = idev->mc_list; ma; ma=ma->next) {
			spin_lock_bh(&ma->mca_lock);
			igmp6_group_queried(ma, max_delay);
			spin_unlock_bh(&ma->mca_lock);
		}
	} else {
		for (ma = idev->mc_list; ma; ma=ma->next) {
			if (!ipv6_addr_equal(group, &ma->mca_addr))
				continue;
			spin_lock_bh(&ma->mca_lock);
			if (ma->mca_flags & MAF_TIMER_RUNNING) {
				/* gsquery <- gsquery && mark */
				if (!mark)
					ma->mca_flags &= ~MAF_GSQUERY;
			} else {
				/* gsquery <- mark */
				if (mark)
					ma->mca_flags |= MAF_GSQUERY;
				else
					ma->mca_flags &= ~MAF_GSQUERY;
			}
			if (!(ma->mca_flags & MAF_GSQUERY) ||
			    mld_marksources(ma, ntohs(mlh2->mld2q_nsrcs), mlh2->mld2q_srcs))
				igmp6_group_queried(ma, max_delay);
			spin_unlock_bh(&ma->mca_lock);
			break;
		}
	}
	read_unlock_bh(&idev->lock);

	return 0;
}

/* called with rcu_read_lock() */
int igmp6_event_report(struct sk_buff *skb)
{
	struct ifmcaddr6 *ma;
	struct inet6_dev *idev;
	struct mld_msg *mld;
	int addr_type;

	/* Our own report looped back. Ignore it. */
	if (skb->pkt_type == PACKET_LOOPBACK)
		return 0;

	/* send our report if the MC router may not have heard this report */
	if (skb->pkt_type != PACKET_MULTICAST &&
	    skb->pkt_type != PACKET_BROADCAST)
		return 0;

	if (!pskb_may_pull(skb, sizeof(*mld) - sizeof(struct icmp6hdr)))
		return -EINVAL;

	mld = (struct mld_msg *)icmp6_hdr(skb);

	/* Drop reports with not link local source */
	addr_type = ipv6_addr_type(&ipv6_hdr(skb)->saddr);
	if (addr_type != IPV6_ADDR_ANY &&
	    !(addr_type&IPV6_ADDR_LINKLOCAL))
		return -EINVAL;

	idev = __in6_dev_get(skb->dev);
	if (idev == NULL)
		return -ENODEV;

	/*
	 *	Cancel the timer for this group
	 */

	read_lock_bh(&idev->lock);
	for (ma = idev->mc_list; ma; ma=ma->next) {
		if (ipv6_addr_equal(&ma->mca_addr, &mld->mld_mca)) {
			spin_lock(&ma->mca_lock);
			if (del_timer(&ma->mca_timer))
				atomic_dec(&ma->mca_refcnt);
			ma->mca_flags &= ~(MAF_LAST_REPORTER|MAF_TIMER_RUNNING);
			spin_unlock(&ma->mca_lock);
			break;
		}
	}
	read_unlock_bh(&idev->lock);
	return 0;
}

static bool is_in(struct ifmcaddr6 *pmc, struct ip6_sf_list *psf, int type,
		  int gdeleted, int sdeleted)
{
	switch (type) {
	case MLD2_MODE_IS_INCLUDE:
	case MLD2_MODE_IS_EXCLUDE:
		if (gdeleted || sdeleted)
			return false;
		if (!((pmc->mca_flags & MAF_GSQUERY) && !psf->sf_gsresp)) {
			if (pmc->mca_sfmode == MCAST_INCLUDE)
				return true;
			/* don't include if this source is excluded
			 * in all filters
			 */
			if (psf->sf_count[MCAST_INCLUDE])
				return type == MLD2_MODE_IS_INCLUDE;
			return pmc->mca_sfcount[MCAST_EXCLUDE] ==
				psf->sf_count[MCAST_EXCLUDE];
		}
		return false;
	case MLD2_CHANGE_TO_INCLUDE:
		if (gdeleted || sdeleted)
			return false;
		return psf->sf_count[MCAST_INCLUDE] != 0;
	case MLD2_CHANGE_TO_EXCLUDE:
		if (gdeleted || sdeleted)
			return false;
		if (pmc->mca_sfcount[MCAST_EXCLUDE] == 0 ||
		    psf->sf_count[MCAST_INCLUDE])
			return false;
		return pmc->mca_sfcount[MCAST_EXCLUDE] ==
			psf->sf_count[MCAST_EXCLUDE];
	case MLD2_ALLOW_NEW_SOURCES:
		if (gdeleted || !psf->sf_crcount)
			return false;
		return (pmc->mca_sfmode == MCAST_INCLUDE) ^ sdeleted;
	case MLD2_BLOCK_OLD_SOURCES:
		if (pmc->mca_sfmode == MCAST_INCLUDE)
			return gdeleted || (psf->sf_crcount && sdeleted);
		return psf->sf_crcount && !gdeleted && !sdeleted;
	}
	return false;
}

static int
mld_scount(struct ifmcaddr6 *pmc, int type, int gdeleted, int sdeleted)
{
	struct ip6_sf_list *psf;
	int scount = 0;

	for (psf=pmc->mca_sources; psf; psf=psf->sf_next) {
		if (!is_in(pmc, psf, type, gdeleted, sdeleted))
			continue;
		scount++;
	}
	return scount;
}

static void ip6_mc_hdr(struct sock *sk, struct sk_buff *skb,
		       struct net_device *dev,
		       const struct in6_addr *saddr,
		       const struct in6_addr *daddr,
		       int proto, int len)
{
	struct ipv6hdr *hdr;

	skb->protocol = htons(ETH_P_IPV6);
	skb->dev = dev;

	skb_reset_network_header(skb);
	skb_put(skb, sizeof(struct ipv6hdr));
	hdr = ipv6_hdr(skb);

	ip6_flow_hdr(hdr, 0, 0);

	hdr->payload_len = htons(len);
	hdr->nexthdr = proto;
	hdr->hop_limit = inet6_sk(sk)->hop_limit;

	hdr->saddr = *saddr;
	hdr->daddr = *daddr;
}

static struct sk_buff *mld_newpack(struct net_device *dev, int size)
{
	struct net *net = dev_net(dev);
	struct sock *sk = net->ipv6.igmp_sk;
	struct sk_buff *skb;
	struct mld2_report *pmr;
	struct in6_addr addr_buf;
	const struct in6_addr *saddr;
	int hlen = LL_RESERVED_SPACE(dev);
	int tlen = dev->needed_tailroom;
	int err;
	u8 ra[8] = { IPPROTO_ICMPV6, 0,
		     IPV6_TLV_ROUTERALERT, 2, 0, 0,
		     IPV6_TLV_PADN, 0 };

	/* we assume size > sizeof(ra) here */
	size += hlen + tlen;
	/* limit our allocations to order-0 page */
	size = min_t(int, size, SKB_MAX_ORDER(0, 0));
	skb = sock_alloc_send_skb(sk, size, 1, &err);

	if (!skb)
		return NULL;

	skb_reserve(skb, hlen);

	if (ipv6_get_lladdr(dev, &addr_buf, IFA_F_TENTATIVE)) {
		/* <draft-ietf-magma-mld-source-05.txt>:
		 * use unspecified address as the source address
		 * when a valid link-local address is not available.
		 */
		saddr = &in6addr_any;
	} else
		saddr = &addr_buf;

	ip6_mc_hdr(sk, skb, dev, saddr, &mld2_all_mcr, NEXTHDR_HOP, 0);

	memcpy(skb_put(skb, sizeof(ra)), ra, sizeof(ra));

	skb_set_transport_header(skb, skb_tail_pointer(skb) - skb->data);
	skb_put(skb, sizeof(*pmr));
	pmr = (struct mld2_report *)skb_transport_header(skb);
	pmr->mld2r_type = ICMPV6_MLD2_REPORT;
	pmr->mld2r_resv1 = 0;
	pmr->mld2r_cksum = 0;
	pmr->mld2r_resv2 = 0;
	pmr->mld2r_ngrec = 0;
	return skb;
}

static void mld_sendpack(struct sk_buff *skb)
{
	struct ipv6hdr *pip6 = ipv6_hdr(skb);
	struct mld2_report *pmr =
			      (struct mld2_report *)skb_transport_header(skb);
	int payload_len, mldlen;
	struct inet6_dev *idev;
	struct net *net = dev_net(skb->dev);
	int err;
	struct flowi6 fl6;
	struct dst_entry *dst;

	rcu_read_lock();
	idev = __in6_dev_get(skb->dev);
	IP6_UPD_PO_STATS(net, idev, IPSTATS_MIB_OUT, skb->len);

	payload_len = (skb_tail_pointer(skb) - skb_network_header(skb)) -
		sizeof(*pip6);
	mldlen = skb_tail_pointer(skb) - skb_transport_header(skb);
	pip6->payload_len = htons(payload_len);

	pmr->mld2r_cksum = csum_ipv6_magic(&pip6->saddr, &pip6->daddr, mldlen,
					   IPPROTO_ICMPV6,
					   csum_partial(skb_transport_header(skb),
							mldlen, 0));

	icmpv6_flow_init(net->ipv6.igmp_sk, &fl6, ICMPV6_MLD2_REPORT,
			 &ipv6_hdr(skb)->saddr, &ipv6_hdr(skb)->daddr,
			 skb->dev->ifindex);
	dst = icmp6_dst_alloc(skb->dev, &fl6);

	err = 0;
	if (IS_ERR(dst)) {
		err = PTR_ERR(dst);
		dst = NULL;
	}
	skb_dst_set(skb, dst);
	if (err)
		goto err_out;

	payload_len = skb->len;

	err = NF_HOOK(NFPROTO_IPV6, NF_INET_LOCAL_OUT, skb, NULL, skb->dev,
		      dst_output);
out:
	if (!err) {
		ICMP6MSGOUT_INC_STATS_BH(net, idev, ICMPV6_MLD2_REPORT);
		ICMP6_INC_STATS_BH(net, idev, ICMP6_MIB_OUTMSGS);
		IP6_UPD_PO_STATS_BH(net, idev, IPSTATS_MIB_OUTMCAST, payload_len);
	} else
		IP6_INC_STATS_BH(net, idev, IPSTATS_MIB_OUTDISCARDS);

	rcu_read_unlock();
	return;

err_out:
	kfree_skb(skb);
	goto out;
}

static int grec_size(struct ifmcaddr6 *pmc, int type, int gdel, int sdel)
{
	return sizeof(struct mld2_grec) + 16 * mld_scount(pmc,type,gdel,sdel);
}

static struct sk_buff *add_grhead(struct sk_buff *skb, struct ifmcaddr6 *pmc,
	int type, struct mld2_grec **ppgr)
{
	struct net_device *dev = pmc->idev->dev;
	struct mld2_report *pmr;
	struct mld2_grec *pgr;

	if (!skb)
		skb = mld_newpack(dev, dev->mtu);
	if (!skb)
		return NULL;
	pgr = (struct mld2_grec *)skb_put(skb, sizeof(struct mld2_grec));
	pgr->grec_type = type;
	pgr->grec_auxwords = 0;
	pgr->grec_nsrcs = 0;
	pgr->grec_mca = pmc->mca_addr;	/* structure copy */
	pmr = (struct mld2_report *)skb_transport_header(skb);
	pmr->mld2r_ngrec = htons(ntohs(pmr->mld2r_ngrec)+1);
	*ppgr = pgr;
	return skb;
}

#define AVAILABLE(skb) ((skb) ? ((skb)->dev ? (skb)->dev->mtu - (skb)->len : \
	skb_tailroom(skb)) : 0)

static struct sk_buff *add_grec(struct sk_buff *skb, struct ifmcaddr6 *pmc,
	int type, int gdeleted, int sdeleted)
{
	struct net_device *dev = pmc->idev->dev;
	struct mld2_report *pmr;
	struct mld2_grec *pgr = NULL;
	struct ip6_sf_list *psf, *psf_next, *psf_prev, **psf_list;
	int scount, stotal, first, isquery, truncate;

	if (pmc->mca_flags & MAF_NOREPORT)
		return skb;

	isquery = type == MLD2_MODE_IS_INCLUDE ||
		  type == MLD2_MODE_IS_EXCLUDE;
	truncate = type == MLD2_MODE_IS_EXCLUDE ||
		    type == MLD2_CHANGE_TO_EXCLUDE;

	stotal = scount = 0;

	psf_list = sdeleted ? &pmc->mca_tomb : &pmc->mca_sources;

	if (!*psf_list)
		goto empty_source;

	pmr = skb ? (struct mld2_report *)skb_transport_header(skb) : NULL;

	/* EX and TO_EX get a fresh packet, if needed */
	if (truncate) {
		if (pmr && pmr->mld2r_ngrec &&
		    AVAILABLE(skb) < grec_size(pmc, type, gdeleted, sdeleted)) {
			if (skb)
				mld_sendpack(skb);
			skb = mld_newpack(dev, dev->mtu);
		}
	}
	first = 1;
	psf_prev = NULL;
	for (psf=*psf_list; psf; psf=psf_next) {
		struct in6_addr *psrc;

		psf_next = psf->sf_next;

		if (!is_in(pmc, psf, type, gdeleted, sdeleted)) {
			psf_prev = psf;
			continue;
		}

		/* clear marks on query responses */
		if (isquery)
			psf->sf_gsresp = 0;

		if (AVAILABLE(skb) < sizeof(*psrc) +
		    first*sizeof(struct mld2_grec)) {
			if (truncate && !first)
				break;	 /* truncate these */
			if (pgr)
				pgr->grec_nsrcs = htons(scount);
			if (skb)
				mld_sendpack(skb);
			skb = mld_newpack(dev, dev->mtu);
			first = 1;
			scount = 0;
		}
		if (first) {
			skb = add_grhead(skb, pmc, type, &pgr);
			first = 0;
		}
		if (!skb)
			return NULL;
		psrc = (struct in6_addr *)skb_put(skb, sizeof(*psrc));
		*psrc = psf->sf_addr;
		scount++; stotal++;
		if ((type == MLD2_ALLOW_NEW_SOURCES ||
		     type == MLD2_BLOCK_OLD_SOURCES) && psf->sf_crcount) {
			psf->sf_crcount--;
			if ((sdeleted || gdeleted) && psf->sf_crcount == 0) {
				if (psf_prev)
					psf_prev->sf_next = psf->sf_next;
				else
					*psf_list = psf->sf_next;
				kfree(psf);
				continue;
			}
		}
		psf_prev = psf;
	}

empty_source:
	if (!stotal) {
		if (type == MLD2_ALLOW_NEW_SOURCES ||
		    type == MLD2_BLOCK_OLD_SOURCES)
			return skb;
		if (pmc->mca_crcount || isquery) {
			/* make sure we have room for group header */
			if (skb && AVAILABLE(skb) < sizeof(struct mld2_grec)) {
				mld_sendpack(skb);
				skb = NULL; /* add_grhead will get a new one */
			}
			skb = add_grhead(skb, pmc, type, &pgr);
		}
	}
	if (pgr)
		pgr->grec_nsrcs = htons(scount);

	if (isquery)
		pmc->mca_flags &= ~MAF_GSQUERY;	/* clear query state */
	return skb;
}

static void mld_send_report(struct inet6_dev *idev, struct ifmcaddr6 *pmc)
{
	struct sk_buff *skb = NULL;
	int type;

	if (!pmc) {
		read_lock_bh(&idev->lock);
		for (pmc=idev->mc_list; pmc; pmc=pmc->next) {
			if (pmc->mca_flags & MAF_NOREPORT)
				continue;
			spin_lock_bh(&pmc->mca_lock);
			if (pmc->mca_sfcount[MCAST_EXCLUDE])
				type = MLD2_MODE_IS_EXCLUDE;
			else
				type = MLD2_MODE_IS_INCLUDE;
			skb = add_grec(skb, pmc, type, 0, 0);
			spin_unlock_bh(&pmc->mca_lock);
		}
		read_unlock_bh(&idev->lock);
	} else {
		spin_lock_bh(&pmc->mca_lock);
		if (pmc->mca_sfcount[MCAST_EXCLUDE])
			type = MLD2_MODE_IS_EXCLUDE;
		else
			type = MLD2_MODE_IS_INCLUDE;
		skb = add_grec(skb, pmc, type, 0, 0);
		spin_unlock_bh(&pmc->mca_lock);
	}
	if (skb)
		mld_sendpack(skb);
}

/*
 * remove zero-count source records from a source filter list
 */
static void mld_clear_zeros(struct ip6_sf_list **ppsf)
{
	struct ip6_sf_list *psf_prev, *psf_next, *psf;

	psf_prev = NULL;
	for (psf=*ppsf; psf; psf = psf_next) {
		psf_next = psf->sf_next;
		if (psf->sf_crcount == 0) {
			if (psf_prev)
				psf_prev->sf_next = psf->sf_next;
			else
				*ppsf = psf->sf_next;
			kfree(psf);
		} else
			psf_prev = psf;
	}
}

static void mld_send_cr(struct inet6_dev *idev)
{
	struct ifmcaddr6 *pmc, *pmc_prev, *pmc_next;
	struct sk_buff *skb = NULL;
	int type, dtype;

	read_lock_bh(&idev->lock);
	spin_lock(&idev->mc_lock);

	/* deleted MCA's */
	pmc_prev = NULL;
	for (pmc=idev->mc_tomb; pmc; pmc=pmc_next) {
		pmc_next = pmc->next;
		if (pmc->mca_sfmode == MCAST_INCLUDE) {
			type = MLD2_BLOCK_OLD_SOURCES;
			dtype = MLD2_BLOCK_OLD_SOURCES;
			skb = add_grec(skb, pmc, type, 1, 0);
			skb = add_grec(skb, pmc, dtype, 1, 1);
		}
		if (pmc->mca_crcount) {
			if (pmc->mca_sfmode == MCAST_EXCLUDE) {
				type = MLD2_CHANGE_TO_INCLUDE;
				skb = add_grec(skb, pmc, type, 1, 0);
			}
			pmc->mca_crcount--;
			if (pmc->mca_crcount == 0) {
				mld_clear_zeros(&pmc->mca_tomb);
				mld_clear_zeros(&pmc->mca_sources);
			}
		}
		if (pmc->mca_crcount == 0 && !pmc->mca_tomb &&
		    !pmc->mca_sources) {
			if (pmc_prev)
				pmc_prev->next = pmc_next;
			else
				idev->mc_tomb = pmc_next;
			in6_dev_put(pmc->idev);
			kfree(pmc);
		} else
			pmc_prev = pmc;
	}
	spin_unlock(&idev->mc_lock);

	/* change recs */
	for (pmc=idev->mc_list; pmc; pmc=pmc->next) {
		spin_lock_bh(&pmc->mca_lock);
		if (pmc->mca_sfcount[MCAST_EXCLUDE]) {
			type = MLD2_BLOCK_OLD_SOURCES;
			dtype = MLD2_ALLOW_NEW_SOURCES;
		} else {
			type = MLD2_ALLOW_NEW_SOURCES;
			dtype = MLD2_BLOCK_OLD_SOURCES;
		}
		skb = add_grec(skb, pmc, type, 0, 0);
		skb = add_grec(skb, pmc, dtype, 0, 1);	/* deleted sources */

		/* filter mode changes */
		if (pmc->mca_crcount) {
			if (pmc->mca_sfmode == MCAST_EXCLUDE)
				type = MLD2_CHANGE_TO_EXCLUDE;
			else
				type = MLD2_CHANGE_TO_INCLUDE;
			skb = add_grec(skb, pmc, type, 0, 0);
			pmc->mca_crcount--;
		}
		spin_unlock_bh(&pmc->mca_lock);
	}
	read_unlock_bh(&idev->lock);
	if (!skb)
		return;
	(void) mld_sendpack(skb);
}

static void igmp6_send(struct in6_addr *addr, struct net_device *dev, int type)
{
	struct net *net = dev_net(dev);
	struct sock *sk = net->ipv6.igmp_sk;
	struct inet6_dev *idev;
	struct sk_buff *skb;
	struct mld_msg *hdr;
	const struct in6_addr *snd_addr, *saddr;
	struct in6_addr addr_buf;
	int hlen = LL_RESERVED_SPACE(dev);
	int tlen = dev->needed_tailroom;
	int err, len, payload_len, full_len;
	u8 ra[8] = { IPPROTO_ICMPV6, 0,
		     IPV6_TLV_ROUTERALERT, 2, 0, 0,
		     IPV6_TLV_PADN, 0 };
	struct flowi6 fl6;
	struct dst_entry *dst;

	if (type == ICMPV6_MGM_REDUCTION)
		snd_addr = &in6addr_linklocal_allrouters;
	else
		snd_addr = addr;

	len = sizeof(struct icmp6hdr) + sizeof(struct in6_addr);
	payload_len = len + sizeof(ra);
	full_len = sizeof(struct ipv6hdr) + payload_len;

	rcu_read_lock();
	IP6_UPD_PO_STATS(net, __in6_dev_get(dev),
		      IPSTATS_MIB_OUT, full_len);
	rcu_read_unlock();

	skb = sock_alloc_send_skb(sk, hlen + tlen + full_len, 1, &err);

	if (skb == NULL) {
		rcu_read_lock();
		IP6_INC_STATS(net, __in6_dev_get(dev),
			      IPSTATS_MIB_OUTDISCARDS);
		rcu_read_unlock();
		return;
	}

	skb_reserve(skb, hlen);

	if (ipv6_get_lladdr(dev, &addr_buf, IFA_F_TENTATIVE)) {
		/* <draft-ietf-magma-mld-source-05.txt>:
		 * use unspecified address as the source address
		 * when a valid link-local address is not available.
		 */
		saddr = &in6addr_any;
	} else
		saddr = &addr_buf;

	ip6_mc_hdr(sk, skb, dev, saddr, snd_addr, NEXTHDR_HOP, payload_len);

	memcpy(skb_put(skb, sizeof(ra)), ra, sizeof(ra));

	hdr = (struct mld_msg *) skb_put(skb, sizeof(struct mld_msg));
	memset(hdr, 0, sizeof(struct mld_msg));
	hdr->mld_type = type;
	hdr->mld_mca = *addr;

	hdr->mld_cksum = csum_ipv6_magic(saddr, snd_addr, len,
					 IPPROTO_ICMPV6,
					 csum_partial(hdr, len, 0));

	rcu_read_lock();
	idev = __in6_dev_get(skb->dev);

	icmpv6_flow_init(sk, &fl6, type,
			 &ipv6_hdr(skb)->saddr, &ipv6_hdr(skb)->daddr,
			 skb->dev->ifindex);
	dst = icmp6_dst_alloc(skb->dev, &fl6);
	if (IS_ERR(dst)) {
		err = PTR_ERR(dst);
		goto err_out;
	}

	skb_dst_set(skb, dst);
	err = NF_HOOK(NFPROTO_IPV6, NF_INET_LOCAL_OUT, skb, NULL, skb->dev,
		      dst_output);
out:
	if (!err) {
		ICMP6MSGOUT_INC_STATS(net, idev, type);
		ICMP6_INC_STATS(net, idev, ICMP6_MIB_OUTMSGS);
		IP6_UPD_PO_STATS(net, idev, IPSTATS_MIB_OUTMCAST, full_len);
	} else
		IP6_INC_STATS(net, idev, IPSTATS_MIB_OUTDISCARDS);

	rcu_read_unlock();
	return;

err_out:
	kfree_skb(skb);
	goto out;
}

static void mld_resend_report(struct inet6_dev *idev)
{
	if (MLD_V1_SEEN(idev)) {
		struct ifmcaddr6 *mcaddr;
		read_lock_bh(&idev->lock);
		for (mcaddr = idev->mc_list; mcaddr; mcaddr = mcaddr->next) {
			if (!(mcaddr->mca_flags & MAF_NOREPORT))
				igmp6_send(&mcaddr->mca_addr, idev->dev,
					   ICMPV6_MGM_REPORT);
		}
		read_unlock_bh(&idev->lock);
	} else {
		mld_send_report(idev, NULL);
	}
}

void ipv6_mc_dad_complete(struct inet6_dev *idev)
{
	idev->mc_dad_count = idev->mc_qrv;
	if (idev->mc_dad_count) {
		mld_resend_report(idev);
		idev->mc_dad_count--;
		if (idev->mc_dad_count)
			mld_dad_start_timer(idev, idev->mc_maxdelay);
	}
}

static void mld_dad_timer_expire(unsigned long data)
{
	struct inet6_dev *idev = (struct inet6_dev *)data;

	mld_resend_report(idev);
	if (idev->mc_dad_count) {
		idev->mc_dad_count--;
		if (idev->mc_dad_count)
			mld_dad_start_timer(idev, idev->mc_maxdelay);
	}
	__in6_dev_put(idev);
}

static int ip6_mc_del1_src(struct ifmcaddr6 *pmc, int sfmode,
	const struct in6_addr *psfsrc)
{
	struct ip6_sf_list *psf, *psf_prev;
	int rv = 0;

	psf_prev = NULL;
	for (psf=pmc->mca_sources; psf; psf=psf->sf_next) {
		if (ipv6_addr_equal(&psf->sf_addr, psfsrc))
			break;
		psf_prev = psf;
	}
	if (!psf || psf->sf_count[sfmode] == 0) {
		/* source filter not found, or count wrong =>  bug */
		return -ESRCH;
	}
	psf->sf_count[sfmode]--;
	if (!psf->sf_count[MCAST_INCLUDE] && !psf->sf_count[MCAST_EXCLUDE]) {
		struct inet6_dev *idev = pmc->idev;

		/* no more filters for this source */
		if (psf_prev)
			psf_prev->sf_next = psf->sf_next;
		else
			pmc->mca_sources = psf->sf_next;
		if (psf->sf_oldin && !(pmc->mca_flags & MAF_NOREPORT) &&
		    !MLD_V1_SEEN(idev)) {
			psf->sf_crcount = idev->mc_qrv;
			psf->sf_next = pmc->mca_tomb;
			pmc->mca_tomb = psf;
			rv = 1;
		} else
			kfree(psf);
	}
	return rv;
}

static int ip6_mc_del_src(struct inet6_dev *idev, const struct in6_addr *pmca,
			  int sfmode, int sfcount, const struct in6_addr *psfsrc,
			  int delta)
{
	struct ifmcaddr6 *pmc;
	int	changerec = 0;
	int	i, err;

	if (!idev)
		return -ENODEV;
	read_lock_bh(&idev->lock);
	for (pmc=idev->mc_list; pmc; pmc=pmc->next) {
		if (ipv6_addr_equal(pmca, &pmc->mca_addr))
			break;
	}
	if (!pmc) {
		/* MCA not found?? bug */
		read_unlock_bh(&idev->lock);
		return -ESRCH;
	}
	spin_lock_bh(&pmc->mca_lock);
	sf_markstate(pmc);
	if (!delta) {
		if (!pmc->mca_sfcount[sfmode]) {
			spin_unlock_bh(&pmc->mca_lock);
			read_unlock_bh(&idev->lock);
			return -EINVAL;
		}
		pmc->mca_sfcount[sfmode]--;
	}
	err = 0;
	for (i=0; i<sfcount; i++) {
		int rv = ip6_mc_del1_src(pmc, sfmode, &psfsrc[i]);

		changerec |= rv > 0;
		if (!err && rv < 0)
			err = rv;
	}
	if (pmc->mca_sfmode == MCAST_EXCLUDE &&
	    pmc->mca_sfcount[MCAST_EXCLUDE] == 0 &&
	    pmc->mca_sfcount[MCAST_INCLUDE]) {
		struct ip6_sf_list *psf;

		/* filter mode change */
		pmc->mca_sfmode = MCAST_INCLUDE;
		pmc->mca_crcount = idev->mc_qrv;
		idev->mc_ifc_count = pmc->mca_crcount;
		for (psf=pmc->mca_sources; psf; psf = psf->sf_next)
			psf->sf_crcount = 0;
		mld_ifc_event(pmc->idev);
	} else if (sf_setstate(pmc) || changerec)
		mld_ifc_event(pmc->idev);
	spin_unlock_bh(&pmc->mca_lock);
	read_unlock_bh(&idev->lock);
	return err;
}

/*
 * Add multicast single-source filter to the interface list
 */
static int ip6_mc_add1_src(struct ifmcaddr6 *pmc, int sfmode,
	const struct in6_addr *psfsrc)
{
	struct ip6_sf_list *psf, *psf_prev;

	psf_prev = NULL;
	for (psf=pmc->mca_sources; psf; psf=psf->sf_next) {
		if (ipv6_addr_equal(&psf->sf_addr, psfsrc))
			break;
		psf_prev = psf;
	}
	if (!psf) {
		psf = kzalloc(sizeof(*psf), GFP_ATOMIC);
		if (!psf)
			return -ENOBUFS;

		psf->sf_addr = *psfsrc;
		if (psf_prev) {
			psf_prev->sf_next = psf;
		} else
			pmc->mca_sources = psf;
	}
	psf->sf_count[sfmode]++;
	return 0;
}

static void sf_markstate(struct ifmcaddr6 *pmc)
{
	struct ip6_sf_list *psf;
	int mca_xcount = pmc->mca_sfcount[MCAST_EXCLUDE];

	for (psf=pmc->mca_sources; psf; psf=psf->sf_next)
		if (pmc->mca_sfcount[MCAST_EXCLUDE]) {
			psf->sf_oldin = mca_xcount ==
				psf->sf_count[MCAST_EXCLUDE] &&
				!psf->sf_count[MCAST_INCLUDE];
		} else
			psf->sf_oldin = psf->sf_count[MCAST_INCLUDE] != 0;
}

static int sf_setstate(struct ifmcaddr6 *pmc)
{
	struct ip6_sf_list *psf, *dpsf;
	int mca_xcount = pmc->mca_sfcount[MCAST_EXCLUDE];
	int qrv = pmc->idev->mc_qrv;
	int new_in, rv;

	rv = 0;
	for (psf=pmc->mca_sources; psf; psf=psf->sf_next) {
		if (pmc->mca_sfcount[MCAST_EXCLUDE]) {
			new_in = mca_xcount == psf->sf_count[MCAST_EXCLUDE] &&
				!psf->sf_count[MCAST_INCLUDE];
		} else
			new_in = psf->sf_count[MCAST_INCLUDE] != 0;
		if (new_in) {
			if (!psf->sf_oldin) {
				struct ip6_sf_list *prev = NULL;

				for (dpsf=pmc->mca_tomb; dpsf;
				     dpsf=dpsf->sf_next) {
					if (ipv6_addr_equal(&dpsf->sf_addr,
					    &psf->sf_addr))
						break;
					prev = dpsf;
				}
				if (dpsf) {
					if (prev)
						prev->sf_next = dpsf->sf_next;
					else
						pmc->mca_tomb = dpsf->sf_next;
					kfree(dpsf);
				}
				psf->sf_crcount = qrv;
				rv++;
			}
		} else if (psf->sf_oldin) {
			psf->sf_crcount = 0;
			/*
			 * add or update "delete" records if an active filter
			 * is now inactive
			 */
			for (dpsf=pmc->mca_tomb; dpsf; dpsf=dpsf->sf_next)
				if (ipv6_addr_equal(&dpsf->sf_addr,
				    &psf->sf_addr))
					break;
			if (!dpsf) {
				dpsf = kmalloc(sizeof(*dpsf), GFP_ATOMIC);
				if (!dpsf)
					continue;
				*dpsf = *psf;
				/* pmc->mca_lock held by callers */
				dpsf->sf_next = pmc->mca_tomb;
				pmc->mca_tomb = dpsf;
			}
			dpsf->sf_crcount = qrv;
			rv++;
		}
	}
	return rv;
}

/*
 * Add multicast source filter list to the interface list
 */
static int ip6_mc_add_src(struct inet6_dev *idev, const struct in6_addr *pmca,
			  int sfmode, int sfcount, const struct in6_addr *psfsrc,
			  int delta)
{
	struct ifmcaddr6 *pmc;
	int	isexclude;
	int	i, err;

	if (!idev)
		return -ENODEV;
	read_lock_bh(&idev->lock);
	for (pmc=idev->mc_list; pmc; pmc=pmc->next) {
		if (ipv6_addr_equal(pmca, &pmc->mca_addr))
			break;
	}
	if (!pmc) {
		/* MCA not found?? bug */
		read_unlock_bh(&idev->lock);
		return -ESRCH;
	}
	spin_lock_bh(&pmc->mca_lock);

	sf_markstate(pmc);
	isexclude = pmc->mca_sfmode == MCAST_EXCLUDE;
	if (!delta)
		pmc->mca_sfcount[sfmode]++;
	err = 0;
	for (i=0; i<sfcount; i++) {
		err = ip6_mc_add1_src(pmc, sfmode, &psfsrc[i]);
		if (err)
			break;
	}
	if (err) {
		int j;

		if (!delta)
			pmc->mca_sfcount[sfmode]--;
		for (j=0; j<i; j++)
			ip6_mc_del1_src(pmc, sfmode, &psfsrc[j]);
	} else if (isexclude != (pmc->mca_sfcount[MCAST_EXCLUDE] != 0)) {
		struct ip6_sf_list *psf;

		/* filter mode change */
		if (pmc->mca_sfcount[MCAST_EXCLUDE])
			pmc->mca_sfmode = MCAST_EXCLUDE;
		else if (pmc->mca_sfcount[MCAST_INCLUDE])
			pmc->mca_sfmode = MCAST_INCLUDE;
		/* else no filters; keep old mode for reports */

		pmc->mca_crcount = idev->mc_qrv;
		idev->mc_ifc_count = pmc->mca_crcount;
		for (psf=pmc->mca_sources; psf; psf = psf->sf_next)
			psf->sf_crcount = 0;
		mld_ifc_event(idev);
	} else if (sf_setstate(pmc))
		mld_ifc_event(idev);
	spin_unlock_bh(&pmc->mca_lock);
	read_unlock_bh(&idev->lock);
	return err;
}

static void ip6_mc_clear_src(struct ifmcaddr6 *pmc)
{
	struct ip6_sf_list *psf, *nextpsf;

	for (psf=pmc->mca_tomb; psf; psf=nextpsf) {
		nextpsf = psf->sf_next;
		kfree(psf);
	}
	pmc->mca_tomb = NULL;
	for (psf=pmc->mca_sources; psf; psf=nextpsf) {
		nextpsf = psf->sf_next;
		kfree(psf);
	}
	pmc->mca_sources = NULL;
	pmc->mca_sfmode = MCAST_EXCLUDE;
	pmc->mca_sfcount[MCAST_INCLUDE] = 0;
	pmc->mca_sfcount[MCAST_EXCLUDE] = 1;
}


static void igmp6_join_group(struct ifmcaddr6 *ma)
{
	unsigned long delay;

	if (ma->mca_flags & MAF_NOREPORT)
		return;

	igmp6_send(&ma->mca_addr, ma->idev->dev, ICMPV6_MGM_REPORT);

	delay = net_random() % IGMP6_UNSOLICITED_IVAL;

	spin_lock_bh(&ma->mca_lock);
	if (del_timer(&ma->mca_timer)) {
		atomic_dec(&ma->mca_refcnt);
		delay = ma->mca_timer.expires - jiffies;
	}

	if (!mod_timer(&ma->mca_timer, jiffies + delay))
		atomic_inc(&ma->mca_refcnt);
	ma->mca_flags |= MAF_TIMER_RUNNING | MAF_LAST_REPORTER;
	spin_unlock_bh(&ma->mca_lock);
}

static int ip6_mc_leave_src(struct sock *sk, struct ipv6_mc_socklist *iml,
			    struct inet6_dev *idev)
{
	int err;

	/* callers have the socket lock and a write lock on ipv6_sk_mc_lock,
	 * so no other readers or writers of iml or its sflist
	 */
	if (!iml->sflist) {
		/* any-source empty exclude case */
		return ip6_mc_del_src(idev, &iml->addr, iml->sfmode, 0, NULL, 0);
	}
	err = ip6_mc_del_src(idev, &iml->addr, iml->sfmode,
		iml->sflist->sl_count, iml->sflist->sl_addr, 0);
	sock_kfree_s(sk, iml->sflist, IP6_SFLSIZE(iml->sflist->sl_max));
	iml->sflist = NULL;
	return err;
}

static void igmp6_leave_group(struct ifmcaddr6 *ma)
{
	if (MLD_V1_SEEN(ma->idev)) {
		if (ma->mca_flags & MAF_LAST_REPORTER)
			igmp6_send(&ma->mca_addr, ma->idev->dev,
				ICMPV6_MGM_REDUCTION);
	} else {
		mld_add_delrec(ma->idev, ma);
		mld_ifc_event(ma->idev);
	}
}

static void mld_gq_timer_expire(unsigned long data)
{
	struct inet6_dev *idev = (struct inet6_dev *)data;

	idev->mc_gq_running = 0;
	mld_send_report(idev, NULL);
	__in6_dev_put(idev);
}

static void mld_ifc_timer_expire(unsigned long data)
{
	struct inet6_dev *idev = (struct inet6_dev *)data;

	mld_send_cr(idev);
	if (idev->mc_ifc_count) {
		idev->mc_ifc_count--;
		if (idev->mc_ifc_count)
			mld_ifc_start_timer(idev, idev->mc_maxdelay);
	}
	__in6_dev_put(idev);
}

static void mld_ifc_event(struct inet6_dev *idev)
{
	if (MLD_V1_SEEN(idev))
		return;
	idev->mc_ifc_count = idev->mc_qrv;
	mld_ifc_start_timer(idev, 1);
}


static void igmp6_timer_handler(unsigned long data)
{
	struct ifmcaddr6 *ma = (struct ifmcaddr6 *) data;

	if (MLD_V1_SEEN(ma->idev))
		igmp6_send(&ma->mca_addr, ma->idev->dev, ICMPV6_MGM_REPORT);
	else
		mld_send_report(ma->idev, ma);

	spin_lock(&ma->mca_lock);
	ma->mca_flags |=  MAF_LAST_REPORTER;
	ma->mca_flags &= ~MAF_TIMER_RUNNING;
	spin_unlock(&ma->mca_lock);
	ma_put(ma);
}

/* Device changing type */

void ipv6_mc_unmap(struct inet6_dev *idev)
{
	struct ifmcaddr6 *i;

	/* Install multicast list, except for all-nodes (already installed) */

	read_lock_bh(&idev->lock);
	for (i = idev->mc_list; i; i = i->next)
		igmp6_group_dropped(i);
	read_unlock_bh(&idev->lock);
}

void ipv6_mc_remap(struct inet6_dev *idev)
{
	ipv6_mc_up(idev);
}

/* Device going down */

void ipv6_mc_down(struct inet6_dev *idev)
{
	struct ifmcaddr6 *i;

	/* Withdraw multicast list */

	read_lock_bh(&idev->lock);
	idev->mc_ifc_count = 0;
	if (del_timer(&idev->mc_ifc_timer))
		__in6_dev_put(idev);
	idev->mc_gq_running = 0;
	if (del_timer(&idev->mc_gq_timer))
		__in6_dev_put(idev);
	if (del_timer(&idev->mc_dad_timer))
		__in6_dev_put(idev);

	for (i = idev->mc_list; i; i=i->next)
		igmp6_group_dropped(i);
	read_unlock_bh(&idev->lock);

	mld_clear_delrec(idev);
}


/* Device going up */

void ipv6_mc_up(struct inet6_dev *idev)
{
	struct ifmcaddr6 *i;

	/* Install multicast list, except for all-nodes (already installed) */

	read_lock_bh(&idev->lock);
	for (i = idev->mc_list; i; i=i->next)
		igmp6_group_added(i);
	read_unlock_bh(&idev->lock);
}

/* IPv6 device initialization. */

void ipv6_mc_init_dev(struct inet6_dev *idev)
{
	write_lock_bh(&idev->lock);
	spin_lock_init(&idev->mc_lock);
	idev->mc_gq_running = 0;
	setup_timer(&idev->mc_gq_timer, mld_gq_timer_expire,
			(unsigned long)idev);
	idev->mc_tomb = NULL;
	idev->mc_ifc_count = 0;
	setup_timer(&idev->mc_ifc_timer, mld_ifc_timer_expire,
			(unsigned long)idev);
	setup_timer(&idev->mc_dad_timer, mld_dad_timer_expire,
		    (unsigned long)idev);
	idev->mc_qrv = MLD_QRV_DEFAULT;
	idev->mc_maxdelay = IGMP6_UNSOLICITED_IVAL;
	idev->mc_v1_seen = 0;
	write_unlock_bh(&idev->lock);
}

/*
 *	Device is about to be destroyed: clean up.
 */

void ipv6_mc_destroy_dev(struct inet6_dev *idev)
{
	struct ifmcaddr6 *i;

	/* Deactivate timers */
	ipv6_mc_down(idev);

	/* Delete all-nodes address. */
	/* We cannot call ipv6_dev_mc_dec() directly, our caller in
	 * addrconf.c has NULL'd out dev->ip6_ptr so in6_dev_get() will
	 * fail.
	 */
	__ipv6_dev_mc_dec(idev, &in6addr_linklocal_allnodes);

	if (idev->cnf.forwarding)
		__ipv6_dev_mc_dec(idev, &in6addr_linklocal_allrouters);

	write_lock_bh(&idev->lock);
	while ((i = idev->mc_list) != NULL) {
		idev->mc_list = i->next;
		write_unlock_bh(&idev->lock);

		igmp6_group_dropped(i);
		ma_put(i);

		write_lock_bh(&idev->lock);
	}
	write_unlock_bh(&idev->lock);
}

#ifdef CONFIG_PROC_FS
struct igmp6_mc_iter_state {
	struct seq_net_private p;
	struct net_device *dev;
	struct inet6_dev *idev;
};

#define igmp6_mc_seq_private(seq)	((struct igmp6_mc_iter_state *)(seq)->private)

static inline struct ifmcaddr6 *igmp6_mc_get_first(struct seq_file *seq)
{
	struct ifmcaddr6 *im = NULL;
	struct igmp6_mc_iter_state *state = igmp6_mc_seq_private(seq);
	struct net *net = seq_file_net(seq);

	state->idev = NULL;
	for_each_netdev_rcu(net, state->dev) {
		struct inet6_dev *idev;
		idev = __in6_dev_get(state->dev);
		if (!idev)
			continue;
		read_lock_bh(&idev->lock);
		im = idev->mc_list;
		if (im) {
			state->idev = idev;
			break;
		}
		read_unlock_bh(&idev->lock);
	}
	return im;
}

static struct ifmcaddr6 *igmp6_mc_get_next(struct seq_file *seq, struct ifmcaddr6 *im)
{
	struct igmp6_mc_iter_state *state = igmp6_mc_seq_private(seq);

	im = im->next;
	while (!im) {
		if (likely(state->idev != NULL))
			read_unlock_bh(&state->idev->lock);

		state->dev = next_net_device_rcu(state->dev);
		if (!state->dev) {
			state->idev = NULL;
			break;
		}
		state->idev = __in6_dev_get(state->dev);
		if (!state->idev)
			continue;
		read_lock_bh(&state->idev->lock);
		im = state->idev->mc_list;
	}
	return im;
}

static struct ifmcaddr6 *igmp6_mc_get_idx(struct seq_file *seq, loff_t pos)
{
	struct ifmcaddr6 *im = igmp6_mc_get_first(seq);
	if (im)
		while (pos && (im = igmp6_mc_get_next(seq, im)) != NULL)
			--pos;
	return pos ? NULL : im;
}

static void *igmp6_mc_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	rcu_read_lock();
	return igmp6_mc_get_idx(seq, *pos);
}

static void *igmp6_mc_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct ifmcaddr6 *im = igmp6_mc_get_next(seq, v);

	++*pos;
	return im;
}

static void igmp6_mc_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	struct igmp6_mc_iter_state *state = igmp6_mc_seq_private(seq);

	if (likely(state->idev != NULL)) {
		read_unlock_bh(&state->idev->lock);
		state->idev = NULL;
	}
	state->dev = NULL;
	rcu_read_unlock();
}

static int igmp6_mc_seq_show(struct seq_file *seq, void *v)
{
	struct ifmcaddr6 *im = (struct ifmcaddr6 *)v;
	struct igmp6_mc_iter_state *state = igmp6_mc_seq_private(seq);

	seq_printf(seq,
		   "%-4d %-15s %pi6 %5d %08X %ld\n",
		   state->dev->ifindex, state->dev->name,
		   &im->mca_addr,
		   im->mca_users, im->mca_flags,
		   (im->mca_flags&MAF_TIMER_RUNNING) ?
		   jiffies_to_clock_t(im->mca_timer.expires-jiffies) : 0);
	return 0;
}

static const struct seq_operations igmp6_mc_seq_ops = {
	.start	=	igmp6_mc_seq_start,
	.next	=	igmp6_mc_seq_next,
	.stop	=	igmp6_mc_seq_stop,
	.show	=	igmp6_mc_seq_show,
};

static int igmp6_mc_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &igmp6_mc_seq_ops,
			    sizeof(struct igmp6_mc_iter_state));
}

static const struct file_operations igmp6_mc_seq_fops = {
	.owner		=	THIS_MODULE,
	.open		=	igmp6_mc_seq_open,
	.read		=	seq_read,
	.llseek		=	seq_lseek,
	.release	=	seq_release_net,
};

struct igmp6_mcf_iter_state {
	struct seq_net_private p;
	struct net_device *dev;
	struct inet6_dev *idev;
	struct ifmcaddr6 *im;
};

#define igmp6_mcf_seq_private(seq)	((struct igmp6_mcf_iter_state *)(seq)->private)

static inline struct ip6_sf_list *igmp6_mcf_get_first(struct seq_file *seq)
{
	struct ip6_sf_list *psf = NULL;
	struct ifmcaddr6 *im = NULL;
	struct igmp6_mcf_iter_state *state = igmp6_mcf_seq_private(seq);
	struct net *net = seq_file_net(seq);

	state->idev = NULL;
	state->im = NULL;
	for_each_netdev_rcu(net, state->dev) {
		struct inet6_dev *idev;
		idev = __in6_dev_get(state->dev);
		if (unlikely(idev == NULL))
			continue;
		read_lock_bh(&idev->lock);
		im = idev->mc_list;
		if (likely(im != NULL)) {
			spin_lock_bh(&im->mca_lock);
			psf = im->mca_sources;
			if (likely(psf != NULL)) {
				state->im = im;
				state->idev = idev;
				break;
			}
			spin_unlock_bh(&im->mca_lock);
		}
		read_unlock_bh(&idev->lock);
	}
	return psf;
}

static struct ip6_sf_list *igmp6_mcf_get_next(struct seq_file *seq, struct ip6_sf_list *psf)
{
	struct igmp6_mcf_iter_state *state = igmp6_mcf_seq_private(seq);

	psf = psf->sf_next;
	while (!psf) {
		spin_unlock_bh(&state->im->mca_lock);
		state->im = state->im->next;
		while (!state->im) {
			if (likely(state->idev != NULL))
				read_unlock_bh(&state->idev->lock);

			state->dev = next_net_device_rcu(state->dev);
			if (!state->dev) {
				state->idev = NULL;
				goto out;
			}
			state->idev = __in6_dev_get(state->dev);
			if (!state->idev)
				continue;
			read_lock_bh(&state->idev->lock);
			state->im = state->idev->mc_list;
		}
		if (!state->im)
			break;
		spin_lock_bh(&state->im->mca_lock);
		psf = state->im->mca_sources;
	}
out:
	return psf;
}

static struct ip6_sf_list *igmp6_mcf_get_idx(struct seq_file *seq, loff_t pos)
{
	struct ip6_sf_list *psf = igmp6_mcf_get_first(seq);
	if (psf)
		while (pos && (psf = igmp6_mcf_get_next(seq, psf)) != NULL)
			--pos;
	return pos ? NULL : psf;
}

static void *igmp6_mcf_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	rcu_read_lock();
	return *pos ? igmp6_mcf_get_idx(seq, *pos - 1) : SEQ_START_TOKEN;
}

static void *igmp6_mcf_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct ip6_sf_list *psf;
	if (v == SEQ_START_TOKEN)
		psf = igmp6_mcf_get_first(seq);
	else
		psf = igmp6_mcf_get_next(seq, v);
	++*pos;
	return psf;
}

static void igmp6_mcf_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	struct igmp6_mcf_iter_state *state = igmp6_mcf_seq_private(seq);
	if (likely(state->im != NULL)) {
		spin_unlock_bh(&state->im->mca_lock);
		state->im = NULL;
	}
	if (likely(state->idev != NULL)) {
		read_unlock_bh(&state->idev->lock);
		state->idev = NULL;
	}
	state->dev = NULL;
	rcu_read_unlock();
}

static int igmp6_mcf_seq_show(struct seq_file *seq, void *v)
{
	struct ip6_sf_list *psf = (struct ip6_sf_list *)v;
	struct igmp6_mcf_iter_state *state = igmp6_mcf_seq_private(seq);

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq,
			   "%3s %6s "
			   "%32s %32s %6s %6s\n", "Idx",
			   "Device", "Multicast Address",
			   "Source Address", "INC", "EXC");
	} else {
		seq_printf(seq,
			   "%3d %6.6s %pi6 %pi6 %6lu %6lu\n",
			   state->dev->ifindex, state->dev->name,
			   &state->im->mca_addr,
			   &psf->sf_addr,
			   psf->sf_count[MCAST_INCLUDE],
			   psf->sf_count[MCAST_EXCLUDE]);
	}
	return 0;
}

static const struct seq_operations igmp6_mcf_seq_ops = {
	.start	=	igmp6_mcf_seq_start,
	.next	=	igmp6_mcf_seq_next,
	.stop	=	igmp6_mcf_seq_stop,
	.show	=	igmp6_mcf_seq_show,
};

static int igmp6_mcf_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &igmp6_mcf_seq_ops,
			    sizeof(struct igmp6_mcf_iter_state));
}

static const struct file_operations igmp6_mcf_seq_fops = {
	.owner		=	THIS_MODULE,
	.open		=	igmp6_mcf_seq_open,
	.read		=	seq_read,
	.llseek		=	seq_lseek,
	.release	=	seq_release_net,
};

static int __net_init igmp6_proc_init(struct net *net)
{
	int err;

	err = -ENOMEM;
	if (!proc_create("igmp6", S_IRUGO, net->proc_net, &igmp6_mc_seq_fops))
		goto out;
	if (!proc_create("mcfilter6", S_IRUGO, net->proc_net,
			 &igmp6_mcf_seq_fops))
		goto out_proc_net_igmp6;

	err = 0;
out:
	return err;

out_proc_net_igmp6:
	remove_proc_entry("igmp6", net->proc_net);
	goto out;
}

static void __net_exit igmp6_proc_exit(struct net *net)
{
	remove_proc_entry("mcfilter6", net->proc_net);
	remove_proc_entry("igmp6", net->proc_net);
}
#else
static inline int igmp6_proc_init(struct net *net)
{
	return 0;
}
static inline void igmp6_proc_exit(struct net *net)
{
}
#endif

static int __net_init igmp6_net_init(struct net *net)
{
	int err;

	err = inet_ctl_sock_create(&net->ipv6.igmp_sk, PF_INET6,
				   SOCK_RAW, IPPROTO_ICMPV6, net);
	if (err < 0) {
		pr_err("Failed to initialize the IGMP6 control socket (err %d)\n",
		       err);
		goto out;
	}

	inet6_sk(net->ipv6.igmp_sk)->hop_limit = 1;

	err = igmp6_proc_init(net);
	if (err)
		goto out_sock_create;
out:
	return err;

out_sock_create:
	inet_ctl_sock_destroy(net->ipv6.igmp_sk);
	goto out;
}

static void __net_exit igmp6_net_exit(struct net *net)
{
	inet_ctl_sock_destroy(net->ipv6.igmp_sk);
	igmp6_proc_exit(net);
}

static struct pernet_operations igmp6_net_ops = {
	.init = igmp6_net_init,
	.exit = igmp6_net_exit,
};

int __init igmp6_init(void)
{
	return register_pernet_subsys(&igmp6_net_ops);
}

void igmp6_cleanup(void)
{
	unregister_pernet_subsys(&igmp6_net_ops);
}
