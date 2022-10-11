// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Multicast support for IPv6
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	Based on linux/ipv4/igmp.c and linux/ipv4/ip_sockglue.c
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
#include <linux/pkt_sched.h>
#include <net/mld.h>
#include <linux/workqueue.h>

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

/* Ensure that we have struct in6_addr aligned on 32bit word. */
static int __mld2_query_bugs[] __attribute__((__unused__)) = {
	BUILD_BUG_ON_ZERO(offsetof(struct mld2_query, mld2q_srcs) % 4),
	BUILD_BUG_ON_ZERO(offsetof(struct mld2_report, mld2r_grec) % 4),
	BUILD_BUG_ON_ZERO(offsetof(struct mld2_grec, grec_mca) % 4)
};

static struct workqueue_struct *mld_wq;
static struct in6_addr mld2_all_mcr = MLD2_ALL_MCR_INIT;

static void igmp6_join_group(struct ifmcaddr6 *ma);
static void igmp6_leave_group(struct ifmcaddr6 *ma);
static void mld_mca_work(struct work_struct *work);

static void mld_ifc_event(struct inet6_dev *idev);
static bool mld_in_v1_mode(const struct inet6_dev *idev);
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
static int __ipv6_dev_mc_inc(struct net_device *dev,
			     const struct in6_addr *addr, unsigned int mode);

#define MLD_QRV_DEFAULT		2
/* RFC3810, 9.2. Query Interval */
#define MLD_QI_DEFAULT		(125 * HZ)
/* RFC3810, 9.3. Query Response Interval */
#define MLD_QRI_DEFAULT		(10 * HZ)

/* RFC3810, 8.1 Query Version Distinctions */
#define MLD_V1_QUERY_LEN	24
#define MLD_V2_QUERY_LEN_MIN	28

#define IPV6_MLD_MAX_MSF	64

int sysctl_mld_max_msf __read_mostly = IPV6_MLD_MAX_MSF;
int sysctl_mld_qrv __read_mostly = MLD_QRV_DEFAULT;

/*
 *	socket join on multicast group
 */
#define mc_dereference(e, idev) \
	rcu_dereference_protected(e, lockdep_is_held(&(idev)->mc_lock))

#define sock_dereference(e, sk) \
	rcu_dereference_protected(e, lockdep_sock_is_held(sk))

#define for_each_pmc_socklock(np, sk, pmc)			\
	for (pmc = sock_dereference((np)->ipv6_mc_list, sk);	\
	     pmc;						\
	     pmc = sock_dereference(pmc->next, sk))

#define for_each_pmc_rcu(np, pmc)				\
	for (pmc = rcu_dereference((np)->ipv6_mc_list);		\
	     pmc;						\
	     pmc = rcu_dereference(pmc->next))

#define for_each_psf_mclock(mc, psf)				\
	for (psf = mc_dereference((mc)->mca_sources, mc->idev);	\
	     psf;						\
	     psf = mc_dereference(psf->sf_next, mc->idev))

#define for_each_psf_rcu(mc, psf)				\
	for (psf = rcu_dereference((mc)->mca_sources);		\
	     psf;						\
	     psf = rcu_dereference(psf->sf_next))

#define for_each_psf_tomb(mc, psf)				\
	for (psf = mc_dereference((mc)->mca_tomb, mc->idev);	\
	     psf;						\
	     psf = mc_dereference(psf->sf_next, mc->idev))

#define for_each_mc_mclock(idev, mc)				\
	for (mc = mc_dereference((idev)->mc_list, idev);	\
	     mc;						\
	     mc = mc_dereference(mc->next, idev))

#define for_each_mc_rcu(idev, mc)				\
	for (mc = rcu_dereference((idev)->mc_list);             \
	     mc;                                                \
	     mc = rcu_dereference(mc->next))

#define for_each_mc_tomb(idev, mc)				\
	for (mc = mc_dereference((idev)->mc_tomb, idev);	\
	     mc;						\
	     mc = mc_dereference(mc->next, idev))

static int unsolicited_report_interval(struct inet6_dev *idev)
{
	int iv;

	if (mld_in_v1_mode(idev))
		iv = idev->cnf.mldv1_unsolicited_report_interval;
	else
		iv = idev->cnf.mldv2_unsolicited_report_interval;

	return iv > 0 ? iv : 1;
}

static int __ipv6_sock_mc_join(struct sock *sk, int ifindex,
			       const struct in6_addr *addr, unsigned int mode)
{
	struct net_device *dev = NULL;
	struct ipv6_mc_socklist *mc_lst;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct net *net = sock_net(sk);
	int err;

	ASSERT_RTNL();

	if (!ipv6_addr_is_multicast(addr))
		return -EINVAL;

	for_each_pmc_socklock(np, sk, mc_lst) {
		if ((ifindex == 0 || mc_lst->ifindex == ifindex) &&
		    ipv6_addr_equal(&mc_lst->addr, addr))
			return -EADDRINUSE;
	}

	mc_lst = sock_kmalloc(sk, sizeof(struct ipv6_mc_socklist), GFP_KERNEL);

	if (!mc_lst)
		return -ENOMEM;

	mc_lst->next = NULL;
	mc_lst->addr = *addr;

	if (ifindex == 0) {
		struct rt6_info *rt;
		rt = rt6_lookup(net, addr, NULL, 0, NULL, 0);
		if (rt) {
			dev = rt->dst.dev;
			ip6_rt_put(rt);
		}
	} else
		dev = __dev_get_by_index(net, ifindex);

	if (!dev) {
		sock_kfree_s(sk, mc_lst, sizeof(*mc_lst));
		return -ENODEV;
	}

	mc_lst->ifindex = dev->ifindex;
	mc_lst->sfmode = mode;
	RCU_INIT_POINTER(mc_lst->sflist, NULL);

	/*
	 *	now add/increase the group membership on the device
	 */

	err = __ipv6_dev_mc_inc(dev, addr, mode);

	if (err) {
		sock_kfree_s(sk, mc_lst, sizeof(*mc_lst));
		return err;
	}

	mc_lst->next = np->ipv6_mc_list;
	rcu_assign_pointer(np->ipv6_mc_list, mc_lst);

	return 0;
}

int ipv6_sock_mc_join(struct sock *sk, int ifindex, const struct in6_addr *addr)
{
	return __ipv6_sock_mc_join(sk, ifindex, addr, MCAST_EXCLUDE);
}
EXPORT_SYMBOL(ipv6_sock_mc_join);

int ipv6_sock_mc_join_ssm(struct sock *sk, int ifindex,
			  const struct in6_addr *addr, unsigned int mode)
{
	return __ipv6_sock_mc_join(sk, ifindex, addr, mode);
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

	ASSERT_RTNL();

	if (!ipv6_addr_is_multicast(addr))
		return -EINVAL;

	for (lnk = &np->ipv6_mc_list;
	     (mc_lst = sock_dereference(*lnk, sk)) != NULL;
	      lnk = &mc_lst->next) {
		if ((ifindex == 0 || mc_lst->ifindex == ifindex) &&
		    ipv6_addr_equal(&mc_lst->addr, addr)) {
			struct net_device *dev;

			*lnk = mc_lst->next;

			dev = __dev_get_by_index(net, mc_lst->ifindex);
			if (dev) {
				struct inet6_dev *idev = __in6_dev_get(dev);

				ip6_mc_leave_src(sk, mc_lst, idev);
				if (idev)
					__ipv6_dev_mc_dec(idev, &mc_lst->addr);
			} else {
				ip6_mc_leave_src(sk, mc_lst, NULL);
			}

			atomic_sub(sizeof(*mc_lst), &sk->sk_omem_alloc);
			kfree_rcu(mc_lst, rcu);
			return 0;
		}
	}

	return -EADDRNOTAVAIL;
}
EXPORT_SYMBOL(ipv6_sock_mc_drop);

static struct inet6_dev *ip6_mc_find_dev_rtnl(struct net *net,
					      const struct in6_addr *group,
					      int ifindex)
{
	struct net_device *dev = NULL;
	struct inet6_dev *idev = NULL;

	if (ifindex == 0) {
		struct rt6_info *rt = rt6_lookup(net, group, NULL, 0, NULL, 0);

		if (rt) {
			dev = rt->dst.dev;
			ip6_rt_put(rt);
		}
	} else {
		dev = __dev_get_by_index(net, ifindex);
	}

	if (!dev)
		return NULL;
	idev = __in6_dev_get(dev);
	if (!idev)
		return NULL;
	if (idev->dead)
		return NULL;
	return idev;
}

void __ipv6_sock_mc_close(struct sock *sk)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct ipv6_mc_socklist *mc_lst;
	struct net *net = sock_net(sk);

	ASSERT_RTNL();

	while ((mc_lst = sock_dereference(np->ipv6_mc_list, sk)) != NULL) {
		struct net_device *dev;

		np->ipv6_mc_list = mc_lst->next;

		dev = __dev_get_by_index(net, mc_lst->ifindex);
		if (dev) {
			struct inet6_dev *idev = __in6_dev_get(dev);

			ip6_mc_leave_src(sk, mc_lst, idev);
			if (idev)
				__ipv6_dev_mc_dec(idev, &mc_lst->addr);
		} else {
			ip6_mc_leave_src(sk, mc_lst, NULL);
		}

		atomic_sub(sizeof(*mc_lst), &sk->sk_omem_alloc);
		kfree_rcu(mc_lst, rcu);
	}
}

void ipv6_sock_mc_close(struct sock *sk)
{
	struct ipv6_pinfo *np = inet6_sk(sk);

	if (!rcu_access_pointer(np->ipv6_mc_list))
		return;

	rtnl_lock();
	lock_sock(sk);
	__ipv6_sock_mc_close(sk);
	release_sock(sk);
	rtnl_unlock();
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
	int err;

	source = &((struct sockaddr_in6 *)&pgsr->gsr_source)->sin6_addr;
	group = &((struct sockaddr_in6 *)&pgsr->gsr_group)->sin6_addr;

	if (!ipv6_addr_is_multicast(group))
		return -EINVAL;

	idev = ip6_mc_find_dev_rtnl(net, group, pgsr->gsr_interface);
	if (!idev)
		return -ENODEV;

	err = -EADDRNOTAVAIL;

	mutex_lock(&idev->mc_lock);
	for_each_pmc_socklock(inet6, sk, pmc) {
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
	if (rcu_access_pointer(pmc->sflist)) {
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

	psl = sock_dereference(pmc->sflist, sk);
	if (!add) {
		if (!psl)
			goto done;	/* err = -EADDRNOTAVAIL */
		rv = !0;
		for (i = 0; i < psl->sl_count; i++) {
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

		for (j = i+1; j < psl->sl_count; j++)
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
		newpsl = sock_kmalloc(sk, struct_size(newpsl, sl_addr, count),
				      GFP_KERNEL);
		if (!newpsl) {
			err = -ENOBUFS;
			goto done;
		}
		newpsl->sl_max = count;
		newpsl->sl_count = count - IP6_SFBLOCK;
		if (psl) {
			for (i = 0; i < psl->sl_count; i++)
				newpsl->sl_addr[i] = psl->sl_addr[i];
			atomic_sub(struct_size(psl, sl_addr, psl->sl_max),
				   &sk->sk_omem_alloc);
		}
		rcu_assign_pointer(pmc->sflist, newpsl);
		kfree_rcu(psl, rcu);
		psl = newpsl;
	}
	rv = 1;	/* > 0 for insert logic below if sl_count is 0 */
	for (i = 0; i < psl->sl_count; i++) {
		rv = !ipv6_addr_equal(&psl->sl_addr[i], source);
		if (rv == 0) /* There is an error in the address. */
			goto done;
	}
	for (j = psl->sl_count-1; j >= i; j--)
		psl->sl_addr[j+1] = psl->sl_addr[j];
	psl->sl_addr[i] = *source;
	psl->sl_count++;
	err = 0;
	/* update the interface list */
	ip6_mc_add_src(idev, group, omode, 1, source, 1);
done:
	mutex_unlock(&idev->mc_lock);
	if (leavegroup)
		err = ipv6_sock_mc_drop(sk, pgsr->gsr_interface, group);
	return err;
}

int ip6_mc_msfilter(struct sock *sk, struct group_filter *gsf,
		    struct sockaddr_storage *list)
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

	idev = ip6_mc_find_dev_rtnl(net, group, gsf->gf_interface);
	if (!idev)
		return -ENODEV;

	err = 0;

	if (gsf->gf_fmode == MCAST_INCLUDE && gsf->gf_numsrc == 0) {
		leavegroup = 1;
		goto done;
	}

	for_each_pmc_socklock(inet6, sk, pmc) {
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
		newpsl = sock_kmalloc(sk, struct_size(newpsl, sl_addr,
						      gsf->gf_numsrc),
				      GFP_KERNEL);
		if (!newpsl) {
			err = -ENOBUFS;
			goto done;
		}
		newpsl->sl_max = newpsl->sl_count = gsf->gf_numsrc;
		for (i = 0; i < newpsl->sl_count; ++i, ++list) {
			struct sockaddr_in6 *psin6;

			psin6 = (struct sockaddr_in6 *)list;
			newpsl->sl_addr[i] = psin6->sin6_addr;
		}
		mutex_lock(&idev->mc_lock);
		err = ip6_mc_add_src(idev, group, gsf->gf_fmode,
				     newpsl->sl_count, newpsl->sl_addr, 0);
		if (err) {
			mutex_unlock(&idev->mc_lock);
			sock_kfree_s(sk, newpsl, struct_size(newpsl, sl_addr,
							     newpsl->sl_max));
			goto done;
		}
		mutex_unlock(&idev->mc_lock);
	} else {
		newpsl = NULL;
		mutex_lock(&idev->mc_lock);
		ip6_mc_add_src(idev, group, gsf->gf_fmode, 0, NULL, 0);
		mutex_unlock(&idev->mc_lock);
	}

	mutex_lock(&idev->mc_lock);
	psl = sock_dereference(pmc->sflist, sk);
	if (psl) {
		ip6_mc_del_src(idev, group, pmc->sfmode,
			       psl->sl_count, psl->sl_addr, 0);
		atomic_sub(struct_size(psl, sl_addr, psl->sl_max),
			   &sk->sk_omem_alloc);
	} else {
		ip6_mc_del_src(idev, group, pmc->sfmode, 0, NULL, 0);
	}
	rcu_assign_pointer(pmc->sflist, newpsl);
	mutex_unlock(&idev->mc_lock);
	kfree_rcu(psl, rcu);
	pmc->sfmode = gsf->gf_fmode;
	err = 0;
done:
	if (leavegroup)
		err = ipv6_sock_mc_drop(sk, gsf->gf_interface, group);
	return err;
}

int ip6_mc_msfget(struct sock *sk, struct group_filter *gsf,
		  sockptr_t optval, size_t ss_offset)
{
	struct ipv6_pinfo *inet6 = inet6_sk(sk);
	const struct in6_addr *group;
	struct ipv6_mc_socklist *pmc;
	struct ip6_sf_socklist *psl;
	int i, count, copycount;

	group = &((struct sockaddr_in6 *)&gsf->gf_group)->sin6_addr;

	if (!ipv6_addr_is_multicast(group))
		return -EINVAL;

	/* changes to the ipv6_mc_list require the socket lock and
	 * rtnl lock. We have the socket lock, so reading the list is safe.
	 */

	for_each_pmc_socklock(inet6, sk, pmc) {
		if (pmc->ifindex != gsf->gf_interface)
			continue;
		if (ipv6_addr_equal(group, &pmc->addr))
			break;
	}
	if (!pmc)		/* must have a prior join */
		return -EADDRNOTAVAIL;

	gsf->gf_fmode = pmc->sfmode;
	psl = sock_dereference(pmc->sflist, sk);
	count = psl ? psl->sl_count : 0;

	copycount = count < gsf->gf_numsrc ? count : gsf->gf_numsrc;
	gsf->gf_numsrc = count;
	for (i = 0; i < copycount; i++) {
		struct sockaddr_in6 *psin6;
		struct sockaddr_storage ss;

		psin6 = (struct sockaddr_in6 *)&ss;
		memset(&ss, 0, sizeof(ss));
		psin6->sin6_family = AF_INET6;
		psin6->sin6_addr = psl->sl_addr[i];
		if (copy_to_sockptr_offset(optval, ss_offset, &ss, sizeof(ss)))
			return -EFAULT;
		ss_offset += sizeof(ss);
	}
	return 0;
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
		return np->mc_all;
	}
	psl = rcu_dereference(mc->sflist);
	if (!psl) {
		rv = mc->sfmode == MCAST_EXCLUDE;
	} else {
		int i;

		for (i = 0; i < psl->sl_count; i++) {
			if (ipv6_addr_equal(&psl->sl_addr[i], src_addr))
				break;
		}
		if (mc->sfmode == MCAST_INCLUDE && i >= psl->sl_count)
			rv = false;
		if (mc->sfmode == MCAST_EXCLUDE && i < psl->sl_count)
			rv = false;
	}
	rcu_read_unlock();

	return rv;
}

/* called with mc_lock */
static void igmp6_group_added(struct ifmcaddr6 *mc)
{
	struct net_device *dev = mc->idev->dev;
	char buf[MAX_ADDR_LEN];

	if (IPV6_ADDR_MC_SCOPE(&mc->mca_addr) <
	    IPV6_ADDR_SCOPE_LINKLOCAL)
		return;

	if (!(mc->mca_flags&MAF_LOADED)) {
		mc->mca_flags |= MAF_LOADED;
		if (ndisc_mc_map(&mc->mca_addr, buf, dev, 0) == 0)
			dev_mc_add(dev, buf);
	}

	if (!(dev->flags & IFF_UP) || (mc->mca_flags & MAF_NOREPORT))
		return;

	if (mld_in_v1_mode(mc->idev)) {
		igmp6_join_group(mc);
		return;
	}
	/* else v2 */

	/* Based on RFC3810 6.1, for newly added INCLUDE SSM, we
	 * should not send filter-mode change record as the mode
	 * should be from IN() to IN(A).
	 */
	if (mc->mca_sfmode == MCAST_EXCLUDE)
		mc->mca_crcount = mc->idev->mc_qrv;

	mld_ifc_event(mc->idev);
}

/* called with mc_lock */
static void igmp6_group_dropped(struct ifmcaddr6 *mc)
{
	struct net_device *dev = mc->idev->dev;
	char buf[MAX_ADDR_LEN];

	if (IPV6_ADDR_MC_SCOPE(&mc->mca_addr) <
	    IPV6_ADDR_SCOPE_LINKLOCAL)
		return;

	if (mc->mca_flags&MAF_LOADED) {
		mc->mca_flags &= ~MAF_LOADED;
		if (ndisc_mc_map(&mc->mca_addr, buf, dev, 0) == 0)
			dev_mc_del(dev, buf);
	}

	if (mc->mca_flags & MAF_NOREPORT)
		return;

	if (!mc->idev->dead)
		igmp6_leave_group(mc);

	if (cancel_delayed_work(&mc->mca_work))
		refcount_dec(&mc->mca_refcnt);
}

/*
 * deleted ifmcaddr6 manipulation
 * called with mc_lock
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
	pmc = kzalloc(sizeof(*pmc), GFP_KERNEL);
	if (!pmc)
		return;

	pmc->idev = im->idev;
	in6_dev_hold(idev);
	pmc->mca_addr = im->mca_addr;
	pmc->mca_crcount = idev->mc_qrv;
	pmc->mca_sfmode = im->mca_sfmode;
	if (pmc->mca_sfmode == MCAST_INCLUDE) {
		struct ip6_sf_list *psf;

		rcu_assign_pointer(pmc->mca_tomb,
				   mc_dereference(im->mca_tomb, idev));
		rcu_assign_pointer(pmc->mca_sources,
				   mc_dereference(im->mca_sources, idev));
		RCU_INIT_POINTER(im->mca_tomb, NULL);
		RCU_INIT_POINTER(im->mca_sources, NULL);

		for_each_psf_mclock(pmc, psf)
			psf->sf_crcount = pmc->mca_crcount;
	}

	rcu_assign_pointer(pmc->next, idev->mc_tomb);
	rcu_assign_pointer(idev->mc_tomb, pmc);
}

/* called with mc_lock */
static void mld_del_delrec(struct inet6_dev *idev, struct ifmcaddr6 *im)
{
	struct ip6_sf_list *psf, *sources, *tomb;
	struct in6_addr *pmca = &im->mca_addr;
	struct ifmcaddr6 *pmc, *pmc_prev;

	pmc_prev = NULL;
	for_each_mc_tomb(idev, pmc) {
		if (ipv6_addr_equal(&pmc->mca_addr, pmca))
			break;
		pmc_prev = pmc;
	}
	if (pmc) {
		if (pmc_prev)
			rcu_assign_pointer(pmc_prev->next, pmc->next);
		else
			rcu_assign_pointer(idev->mc_tomb, pmc->next);
	}

	if (pmc) {
		im->idev = pmc->idev;
		if (im->mca_sfmode == MCAST_INCLUDE) {
			tomb = rcu_replace_pointer(im->mca_tomb,
						   mc_dereference(pmc->mca_tomb, pmc->idev),
						   lockdep_is_held(&im->idev->mc_lock));
			rcu_assign_pointer(pmc->mca_tomb, tomb);

			sources = rcu_replace_pointer(im->mca_sources,
						      mc_dereference(pmc->mca_sources, pmc->idev),
						      lockdep_is_held(&im->idev->mc_lock));
			rcu_assign_pointer(pmc->mca_sources, sources);
			for_each_psf_mclock(im, psf)
				psf->sf_crcount = idev->mc_qrv;
		} else {
			im->mca_crcount = idev->mc_qrv;
		}
		in6_dev_put(pmc->idev);
		ip6_mc_clear_src(pmc);
		kfree_rcu(pmc, rcu);
	}
}

/* called with mc_lock */
static void mld_clear_delrec(struct inet6_dev *idev)
{
	struct ifmcaddr6 *pmc, *nextpmc;

	pmc = mc_dereference(idev->mc_tomb, idev);
	RCU_INIT_POINTER(idev->mc_tomb, NULL);

	for (; pmc; pmc = nextpmc) {
		nextpmc = mc_dereference(pmc->next, idev);
		ip6_mc_clear_src(pmc);
		in6_dev_put(pmc->idev);
		kfree_rcu(pmc, rcu);
	}

	/* clear dead sources, too */
	for_each_mc_mclock(idev, pmc) {
		struct ip6_sf_list *psf, *psf_next;

		psf = mc_dereference(pmc->mca_tomb, idev);
		RCU_INIT_POINTER(pmc->mca_tomb, NULL);
		for (; psf; psf = psf_next) {
			psf_next = mc_dereference(psf->sf_next, idev);
			kfree_rcu(psf, rcu);
		}
	}
}

static void mld_clear_query(struct inet6_dev *idev)
{
	struct sk_buff *skb;

	spin_lock_bh(&idev->mc_query_lock);
	while ((skb = __skb_dequeue(&idev->mc_query_queue)))
		kfree_skb(skb);
	spin_unlock_bh(&idev->mc_query_lock);
}

static void mld_clear_report(struct inet6_dev *idev)
{
	struct sk_buff *skb;

	spin_lock_bh(&idev->mc_report_lock);
	while ((skb = __skb_dequeue(&idev->mc_report_queue)))
		kfree_skb(skb);
	spin_unlock_bh(&idev->mc_report_lock);
}

static void mca_get(struct ifmcaddr6 *mc)
{
	refcount_inc(&mc->mca_refcnt);
}

static void ma_put(struct ifmcaddr6 *mc)
{
	if (refcount_dec_and_test(&mc->mca_refcnt)) {
		in6_dev_put(mc->idev);
		kfree_rcu(mc, rcu);
	}
}

/* called with mc_lock */
static struct ifmcaddr6 *mca_alloc(struct inet6_dev *idev,
				   const struct in6_addr *addr,
				   unsigned int mode)
{
	struct ifmcaddr6 *mc;

	mc = kzalloc(sizeof(*mc), GFP_KERNEL);
	if (!mc)
		return NULL;

	INIT_DELAYED_WORK(&mc->mca_work, mld_mca_work);

	mc->mca_addr = *addr;
	mc->idev = idev; /* reference taken by caller */
	mc->mca_users = 1;
	/* mca_stamp should be updated upon changes */
	mc->mca_cstamp = mc->mca_tstamp = jiffies;
	refcount_set(&mc->mca_refcnt, 1);

	mc->mca_sfmode = mode;
	mc->mca_sfcount[mode] = 1;

	if (ipv6_addr_is_ll_all_nodes(&mc->mca_addr) ||
	    IPV6_ADDR_MC_SCOPE(&mc->mca_addr) < IPV6_ADDR_SCOPE_LINKLOCAL)
		mc->mca_flags |= MAF_NOREPORT;

	return mc;
}

/*
 *	device multicast group inc (add if not found)
 */
static int __ipv6_dev_mc_inc(struct net_device *dev,
			     const struct in6_addr *addr, unsigned int mode)
{
	struct ifmcaddr6 *mc;
	struct inet6_dev *idev;

	ASSERT_RTNL();

	/* we need to take a reference on idev */
	idev = in6_dev_get(dev);

	if (!idev)
		return -EINVAL;

	if (idev->dead) {
		in6_dev_put(idev);
		return -ENODEV;
	}

	mutex_lock(&idev->mc_lock);
	for_each_mc_mclock(idev, mc) {
		if (ipv6_addr_equal(&mc->mca_addr, addr)) {
			mc->mca_users++;
			ip6_mc_add_src(idev, &mc->mca_addr, mode, 0, NULL, 0);
			mutex_unlock(&idev->mc_lock);
			in6_dev_put(idev);
			return 0;
		}
	}

	mc = mca_alloc(idev, addr, mode);
	if (!mc) {
		mutex_unlock(&idev->mc_lock);
		in6_dev_put(idev);
		return -ENOMEM;
	}

	rcu_assign_pointer(mc->next, idev->mc_list);
	rcu_assign_pointer(idev->mc_list, mc);

	mca_get(mc);

	mld_del_delrec(idev, mc);
	igmp6_group_added(mc);
	mutex_unlock(&idev->mc_lock);
	ma_put(mc);
	return 0;
}

int ipv6_dev_mc_inc(struct net_device *dev, const struct in6_addr *addr)
{
	return __ipv6_dev_mc_inc(dev, addr, MCAST_EXCLUDE);
}
EXPORT_SYMBOL(ipv6_dev_mc_inc);

/*
 * device multicast group del
 */
int __ipv6_dev_mc_dec(struct inet6_dev *idev, const struct in6_addr *addr)
{
	struct ifmcaddr6 *ma, __rcu **map;

	ASSERT_RTNL();

	mutex_lock(&idev->mc_lock);
	for (map = &idev->mc_list;
	     (ma = mc_dereference(*map, idev));
	     map = &ma->next) {
		if (ipv6_addr_equal(&ma->mca_addr, addr)) {
			if (--ma->mca_users == 0) {
				*map = ma->next;

				igmp6_group_dropped(ma);
				ip6_mc_clear_src(ma);
				mutex_unlock(&idev->mc_lock);

				ma_put(ma);
				return 0;
			}
			mutex_unlock(&idev->mc_lock);
			return 0;
		}
	}

	mutex_unlock(&idev->mc_lock);
	return -ENOENT;
}

int ipv6_dev_mc_dec(struct net_device *dev, const struct in6_addr *addr)
{
	struct inet6_dev *idev;
	int err;

	ASSERT_RTNL();

	idev = __in6_dev_get(dev);
	if (!idev)
		err = -ENODEV;
	else
		err = __ipv6_dev_mc_dec(idev, addr);

	return err;
}
EXPORT_SYMBOL(ipv6_dev_mc_dec);

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
		for_each_mc_rcu(idev, mc) {
			if (ipv6_addr_equal(&mc->mca_addr, group))
				break;
		}
		if (mc) {
			if (src_addr && !ipv6_addr_any(src_addr)) {
				struct ip6_sf_list *psf;

				for_each_psf_rcu(mc, psf) {
					if (ipv6_addr_equal(&psf->sf_addr, src_addr))
						break;
				}
				if (psf)
					rv = psf->sf_count[MCAST_INCLUDE] ||
						psf->sf_count[MCAST_EXCLUDE] !=
						mc->mca_sfcount[MCAST_EXCLUDE];
				else
					rv = mc->mca_sfcount[MCAST_EXCLUDE] != 0;
			} else
				rv = true; /* don't filter unspecified source */
		}
	}
	rcu_read_unlock();
	return rv;
}

/* called with mc_lock */
static void mld_gq_start_work(struct inet6_dev *idev)
{
	unsigned long tv = prandom_u32() % idev->mc_maxdelay;

	idev->mc_gq_running = 1;
	if (!mod_delayed_work(mld_wq, &idev->mc_gq_work, tv + 2))
		in6_dev_hold(idev);
}

/* called with mc_lock */
static void mld_gq_stop_work(struct inet6_dev *idev)
{
	idev->mc_gq_running = 0;
	if (cancel_delayed_work(&idev->mc_gq_work))
		__in6_dev_put(idev);
}

/* called with mc_lock */
static void mld_ifc_start_work(struct inet6_dev *idev, unsigned long delay)
{
	unsigned long tv = prandom_u32() % delay;

	if (!mod_delayed_work(mld_wq, &idev->mc_ifc_work, tv + 2))
		in6_dev_hold(idev);
}

/* called with mc_lock */
static void mld_ifc_stop_work(struct inet6_dev *idev)
{
	idev->mc_ifc_count = 0;
	if (cancel_delayed_work(&idev->mc_ifc_work))
		__in6_dev_put(idev);
}

/* called with mc_lock */
static void mld_dad_start_work(struct inet6_dev *idev, unsigned long delay)
{
	unsigned long tv = prandom_u32() % delay;

	if (!mod_delayed_work(mld_wq, &idev->mc_dad_work, tv + 2))
		in6_dev_hold(idev);
}

static void mld_dad_stop_work(struct inet6_dev *idev)
{
	if (cancel_delayed_work(&idev->mc_dad_work))
		__in6_dev_put(idev);
}

static void mld_query_stop_work(struct inet6_dev *idev)
{
	spin_lock_bh(&idev->mc_query_lock);
	if (cancel_delayed_work(&idev->mc_query_work))
		__in6_dev_put(idev);
	spin_unlock_bh(&idev->mc_query_lock);
}

static void mld_report_stop_work(struct inet6_dev *idev)
{
	if (cancel_delayed_work_sync(&idev->mc_report_work))
		__in6_dev_put(idev);
}

/*
 * IGMP handling (alias multicast ICMPv6 messages)
 * called with mc_lock
 */
static void igmp6_group_queried(struct ifmcaddr6 *ma, unsigned long resptime)
{
	unsigned long delay = resptime;

	/* Do not start work for these addresses */
	if (ipv6_addr_is_ll_all_nodes(&ma->mca_addr) ||
	    IPV6_ADDR_MC_SCOPE(&ma->mca_addr) < IPV6_ADDR_SCOPE_LINKLOCAL)
		return;

	if (cancel_delayed_work(&ma->mca_work)) {
		refcount_dec(&ma->mca_refcnt);
		delay = ma->mca_work.timer.expires - jiffies;
	}

	if (delay >= resptime)
		delay = prandom_u32() % resptime;

	if (!mod_delayed_work(mld_wq, &ma->mca_work, delay))
		refcount_inc(&ma->mca_refcnt);
	ma->mca_flags |= MAF_TIMER_RUNNING;
}

/* mark EXCLUDE-mode sources
 * called with mc_lock
 */
static bool mld_xmarksources(struct ifmcaddr6 *pmc, int nsrcs,
			     const struct in6_addr *srcs)
{
	struct ip6_sf_list *psf;
	int i, scount;

	scount = 0;
	for_each_psf_mclock(pmc, psf) {
		if (scount == nsrcs)
			break;
		for (i = 0; i < nsrcs; i++) {
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

/* called with mc_lock */
static bool mld_marksources(struct ifmcaddr6 *pmc, int nsrcs,
			    const struct in6_addr *srcs)
{
	struct ip6_sf_list *psf;
	int i, scount;

	if (pmc->mca_sfmode == MCAST_EXCLUDE)
		return mld_xmarksources(pmc, nsrcs, srcs);

	/* mark INCLUDE-mode sources */

	scount = 0;
	for_each_psf_mclock(pmc, psf) {
		if (scount == nsrcs)
			break;
		for (i = 0; i < nsrcs; i++) {
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

static int mld_force_mld_version(const struct inet6_dev *idev)
{
	/* Normally, both are 0 here. If enforcement to a particular is
	 * being used, individual device enforcement will have a lower
	 * precedence over 'all' device (.../conf/all/force_mld_version).
	 */

	if (dev_net(idev->dev)->ipv6.devconf_all->force_mld_version != 0)
		return dev_net(idev->dev)->ipv6.devconf_all->force_mld_version;
	else
		return idev->cnf.force_mld_version;
}

static bool mld_in_v2_mode_only(const struct inet6_dev *idev)
{
	return mld_force_mld_version(idev) == 2;
}

static bool mld_in_v1_mode_only(const struct inet6_dev *idev)
{
	return mld_force_mld_version(idev) == 1;
}

static bool mld_in_v1_mode(const struct inet6_dev *idev)
{
	if (mld_in_v2_mode_only(idev))
		return false;
	if (mld_in_v1_mode_only(idev))
		return true;
	if (idev->mc_v1_seen && time_before(jiffies, idev->mc_v1_seen))
		return true;

	return false;
}

static void mld_set_v1_mode(struct inet6_dev *idev)
{
	/* RFC3810, relevant sections:
	 *  - 9.1. Robustness Variable
	 *  - 9.2. Query Interval
	 *  - 9.3. Query Response Interval
	 *  - 9.12. Older Version Querier Present Timeout
	 */
	unsigned long switchback;

	switchback = (idev->mc_qrv * idev->mc_qi) + idev->mc_qri;

	idev->mc_v1_seen = jiffies + switchback;
}

static void mld_update_qrv(struct inet6_dev *idev,
			   const struct mld2_query *mlh2)
{
	/* RFC3810, relevant sections:
	 *  - 5.1.8. QRV (Querier's Robustness Variable)
	 *  - 9.1. Robustness Variable
	 */

	/* The value of the Robustness Variable MUST NOT be zero,
	 * and SHOULD NOT be one. Catch this here if we ever run
	 * into such a case in future.
	 */
	const int min_qrv = min(MLD_QRV_DEFAULT, sysctl_mld_qrv);
	WARN_ON(idev->mc_qrv == 0);

	if (mlh2->mld2q_qrv > 0)
		idev->mc_qrv = mlh2->mld2q_qrv;

	if (unlikely(idev->mc_qrv < min_qrv)) {
		net_warn_ratelimited("IPv6: MLD: clamping QRV from %u to %u!\n",
				     idev->mc_qrv, min_qrv);
		idev->mc_qrv = min_qrv;
	}
}

static void mld_update_qi(struct inet6_dev *idev,
			  const struct mld2_query *mlh2)
{
	/* RFC3810, relevant sections:
	 *  - 5.1.9. QQIC (Querier's Query Interval Code)
	 *  - 9.2. Query Interval
	 *  - 9.12. Older Version Querier Present Timeout
	 *    (the [Query Interval] in the last Query received)
	 */
	unsigned long mc_qqi;

	if (mlh2->mld2q_qqic < 128) {
		mc_qqi = mlh2->mld2q_qqic;
	} else {
		unsigned long mc_man, mc_exp;

		mc_exp = MLDV2_QQIC_EXP(mlh2->mld2q_qqic);
		mc_man = MLDV2_QQIC_MAN(mlh2->mld2q_qqic);

		mc_qqi = (mc_man | 0x10) << (mc_exp + 3);
	}

	idev->mc_qi = mc_qqi * HZ;
}

static void mld_update_qri(struct inet6_dev *idev,
			   const struct mld2_query *mlh2)
{
	/* RFC3810, relevant sections:
	 *  - 5.1.3. Maximum Response Code
	 *  - 9.3. Query Response Interval
	 */
	idev->mc_qri = msecs_to_jiffies(mldv2_mrc(mlh2));
}

static int mld_process_v1(struct inet6_dev *idev, struct mld_msg *mld,
			  unsigned long *max_delay, bool v1_query)
{
	unsigned long mldv1_md;

	/* Ignore v1 queries */
	if (mld_in_v2_mode_only(idev))
		return -EINVAL;

	mldv1_md = ntohs(mld->mld_maxdelay);

	/* When in MLDv1 fallback and a MLDv2 router start-up being
	 * unaware of current MLDv1 operation, the MRC == MRD mapping
	 * only works when the exponential algorithm is not being
	 * used (as MLDv1 is unaware of such things).
	 *
	 * According to the RFC author, the MLDv2 implementations
	 * he's aware of all use a MRC < 32768 on start up queries.
	 *
	 * Thus, should we *ever* encounter something else larger
	 * than that, just assume the maximum possible within our
	 * reach.
	 */
	if (!v1_query)
		mldv1_md = min(mldv1_md, MLDV1_MRD_MAX_COMPAT);

	*max_delay = max(msecs_to_jiffies(mldv1_md), 1UL);

	/* MLDv1 router present: we need to go into v1 mode *only*
	 * when an MLDv1 query is received as per section 9.12. of
	 * RFC3810! And we know from RFC2710 section 3.7 that MLDv1
	 * queries MUST be of exactly 24 octets.
	 */
	if (v1_query)
		mld_set_v1_mode(idev);

	/* cancel MLDv2 report work */
	mld_gq_stop_work(idev);
	/* cancel the interface change work */
	mld_ifc_stop_work(idev);
	/* clear deleted report items */
	mld_clear_delrec(idev);

	return 0;
}

static void mld_process_v2(struct inet6_dev *idev, struct mld2_query *mld,
			   unsigned long *max_delay)
{
	*max_delay = max(msecs_to_jiffies(mldv2_mrc(mld)), 1UL);

	mld_update_qrv(idev, mld);
	mld_update_qi(idev, mld);
	mld_update_qri(idev, mld);

	idev->mc_maxdelay = *max_delay;

	return;
}

/* called with rcu_read_lock() */
void igmp6_event_query(struct sk_buff *skb)
{
	struct inet6_dev *idev = __in6_dev_get(skb->dev);

	if (!idev || idev->dead)
		goto out;

	spin_lock_bh(&idev->mc_query_lock);
	if (skb_queue_len(&idev->mc_query_queue) < MLD_MAX_SKBS) {
		__skb_queue_tail(&idev->mc_query_queue, skb);
		if (!mod_delayed_work(mld_wq, &idev->mc_query_work, 0))
			in6_dev_hold(idev);
		skb = NULL;
	}
	spin_unlock_bh(&idev->mc_query_lock);
out:
	kfree_skb(skb);
}

static void __mld_query_work(struct sk_buff *skb)
{
	struct mld2_query *mlh2 = NULL;
	const struct in6_addr *group;
	unsigned long max_delay;
	struct inet6_dev *idev;
	struct ifmcaddr6 *ma;
	struct mld_msg *mld;
	int group_type;
	int mark = 0;
	int len, err;

	if (!pskb_may_pull(skb, sizeof(struct in6_addr)))
		goto kfree_skb;

	/* compute payload length excluding extension headers */
	len = ntohs(ipv6_hdr(skb)->payload_len) + sizeof(struct ipv6hdr);
	len -= skb_network_header_len(skb);

	/* RFC3810 6.2
	 * Upon reception of an MLD message that contains a Query, the node
	 * checks if the source address of the message is a valid link-local
	 * address, if the Hop Limit is set to 1, and if the Router Alert
	 * option is present in the Hop-By-Hop Options header of the IPv6
	 * packet.  If any of these checks fails, the packet is dropped.
	 */
	if (!(ipv6_addr_type(&ipv6_hdr(skb)->saddr) & IPV6_ADDR_LINKLOCAL) ||
	    ipv6_hdr(skb)->hop_limit != 1 ||
	    !(IP6CB(skb)->flags & IP6SKB_ROUTERALERT) ||
	    IP6CB(skb)->ra != htons(IPV6_OPT_ROUTERALERT_MLD))
		goto kfree_skb;

	idev = in6_dev_get(skb->dev);
	if (!idev)
		goto kfree_skb;

	mld = (struct mld_msg *)icmp6_hdr(skb);
	group = &mld->mld_mca;
	group_type = ipv6_addr_type(group);

	if (group_type != IPV6_ADDR_ANY &&
	    !(group_type&IPV6_ADDR_MULTICAST))
		goto out;

	if (len < MLD_V1_QUERY_LEN) {
		goto out;
	} else if (len == MLD_V1_QUERY_LEN || mld_in_v1_mode(idev)) {
		err = mld_process_v1(idev, mld, &max_delay,
				     len == MLD_V1_QUERY_LEN);
		if (err < 0)
			goto out;
	} else if (len >= MLD_V2_QUERY_LEN_MIN) {
		int srcs_offset = sizeof(struct mld2_query) -
				  sizeof(struct icmp6hdr);

		if (!pskb_may_pull(skb, srcs_offset))
			goto out;

		mlh2 = (struct mld2_query *)skb_transport_header(skb);

		mld_process_v2(idev, mlh2, &max_delay);

		if (group_type == IPV6_ADDR_ANY) { /* general query */
			if (mlh2->mld2q_nsrcs)
				goto out; /* no sources allowed */

			mld_gq_start_work(idev);
			goto out;
		}
		/* mark sources to include, if group & source-specific */
		if (mlh2->mld2q_nsrcs != 0) {
			if (!pskb_may_pull(skb, srcs_offset +
			    ntohs(mlh2->mld2q_nsrcs) * sizeof(struct in6_addr)))
				goto out;

			mlh2 = (struct mld2_query *)skb_transport_header(skb);
			mark = 1;
		}
	} else {
		goto out;
	}

	if (group_type == IPV6_ADDR_ANY) {
		for_each_mc_mclock(idev, ma) {
			igmp6_group_queried(ma, max_delay);
		}
	} else {
		for_each_mc_mclock(idev, ma) {
			if (!ipv6_addr_equal(group, &ma->mca_addr))
				continue;
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
			break;
		}
	}

out:
	in6_dev_put(idev);
kfree_skb:
	consume_skb(skb);
}

static void mld_query_work(struct work_struct *work)
{
	struct inet6_dev *idev = container_of(to_delayed_work(work),
					      struct inet6_dev,
					      mc_query_work);
	struct sk_buff_head q;
	struct sk_buff *skb;
	bool rework = false;
	int cnt = 0;

	skb_queue_head_init(&q);

	spin_lock_bh(&idev->mc_query_lock);
	while ((skb = __skb_dequeue(&idev->mc_query_queue))) {
		__skb_queue_tail(&q, skb);

		if (++cnt >= MLD_MAX_QUEUE) {
			rework = true;
			break;
		}
	}
	spin_unlock_bh(&idev->mc_query_lock);

	mutex_lock(&idev->mc_lock);
	while ((skb = __skb_dequeue(&q)))
		__mld_query_work(skb);
	mutex_unlock(&idev->mc_lock);

	if (rework && queue_delayed_work(mld_wq, &idev->mc_query_work, 0))
		return;

	in6_dev_put(idev);
}

/* called with rcu_read_lock() */
void igmp6_event_report(struct sk_buff *skb)
{
	struct inet6_dev *idev = __in6_dev_get(skb->dev);

	if (!idev || idev->dead)
		goto out;

	spin_lock_bh(&idev->mc_report_lock);
	if (skb_queue_len(&idev->mc_report_queue) < MLD_MAX_SKBS) {
		__skb_queue_tail(&idev->mc_report_queue, skb);
		if (!mod_delayed_work(mld_wq, &idev->mc_report_work, 0))
			in6_dev_hold(idev);
		skb = NULL;
	}
	spin_unlock_bh(&idev->mc_report_lock);
out:
	kfree_skb(skb);
}

static void __mld_report_work(struct sk_buff *skb)
{
	struct inet6_dev *idev;
	struct ifmcaddr6 *ma;
	struct mld_msg *mld;
	int addr_type;

	/* Our own report looped back. Ignore it. */
	if (skb->pkt_type == PACKET_LOOPBACK)
		goto kfree_skb;

	/* send our report if the MC router may not have heard this report */
	if (skb->pkt_type != PACKET_MULTICAST &&
	    skb->pkt_type != PACKET_BROADCAST)
		goto kfree_skb;

	if (!pskb_may_pull(skb, sizeof(*mld) - sizeof(struct icmp6hdr)))
		goto kfree_skb;

	mld = (struct mld_msg *)icmp6_hdr(skb);

	/* Drop reports with not link local source */
	addr_type = ipv6_addr_type(&ipv6_hdr(skb)->saddr);
	if (addr_type != IPV6_ADDR_ANY &&
	    !(addr_type&IPV6_ADDR_LINKLOCAL))
		goto kfree_skb;

	idev = in6_dev_get(skb->dev);
	if (!idev)
		goto kfree_skb;

	/*
	 *	Cancel the work for this group
	 */

	for_each_mc_mclock(idev, ma) {
		if (ipv6_addr_equal(&ma->mca_addr, &mld->mld_mca)) {
			if (cancel_delayed_work(&ma->mca_work))
				refcount_dec(&ma->mca_refcnt);
			ma->mca_flags &= ~(MAF_LAST_REPORTER |
					   MAF_TIMER_RUNNING);
			break;
		}
	}

	in6_dev_put(idev);
kfree_skb:
	consume_skb(skb);
}

static void mld_report_work(struct work_struct *work)
{
	struct inet6_dev *idev = container_of(to_delayed_work(work),
					      struct inet6_dev,
					      mc_report_work);
	struct sk_buff_head q;
	struct sk_buff *skb;
	bool rework = false;
	int cnt = 0;

	skb_queue_head_init(&q);
	spin_lock_bh(&idev->mc_report_lock);
	while ((skb = __skb_dequeue(&idev->mc_report_queue))) {
		__skb_queue_tail(&q, skb);

		if (++cnt >= MLD_MAX_QUEUE) {
			rework = true;
			break;
		}
	}
	spin_unlock_bh(&idev->mc_report_lock);

	mutex_lock(&idev->mc_lock);
	while ((skb = __skb_dequeue(&q)))
		__mld_report_work(skb);
	mutex_unlock(&idev->mc_lock);

	if (rework && queue_delayed_work(mld_wq, &idev->mc_report_work, 0))
		return;

	in6_dev_put(idev);
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

	for_each_psf_mclock(pmc, psf) {
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

static struct sk_buff *mld_newpack(struct inet6_dev *idev, unsigned int mtu)
{
	u8 ra[8] = { IPPROTO_ICMPV6, 0, IPV6_TLV_ROUTERALERT,
		     2, 0, 0, IPV6_TLV_PADN, 0 };
	struct net_device *dev = idev->dev;
	int hlen = LL_RESERVED_SPACE(dev);
	int tlen = dev->needed_tailroom;
	struct net *net = dev_net(dev);
	const struct in6_addr *saddr;
	struct in6_addr addr_buf;
	struct mld2_report *pmr;
	struct sk_buff *skb;
	unsigned int size;
	struct sock *sk;
	int err;

	sk = net->ipv6.igmp_sk;
	/* we assume size > sizeof(ra) here
	 * Also try to not allocate high-order pages for big MTU
	 */
	size = min_t(int, mtu, PAGE_SIZE / 2) + hlen + tlen;
	skb = sock_alloc_send_skb(sk, size, 1, &err);
	if (!skb)
		return NULL;

	skb->priority = TC_PRIO_CONTROL;
	skb_reserve(skb, hlen);
	skb_tailroom_reserve(skb, mtu, tlen);

	if (ipv6_get_lladdr(dev, &addr_buf, IFA_F_TENTATIVE)) {
		/* <draft-ietf-magma-mld-source-05.txt>:
		 * use unspecified address as the source address
		 * when a valid link-local address is not available.
		 */
		saddr = &in6addr_any;
	} else
		saddr = &addr_buf;

	ip6_mc_hdr(sk, skb, dev, saddr, &mld2_all_mcr, NEXTHDR_HOP, 0);

	skb_put_data(skb, ra, sizeof(ra));

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

	err = NF_HOOK(NFPROTO_IPV6, NF_INET_LOCAL_OUT,
		      net, net->ipv6.igmp_sk, skb, NULL, skb->dev,
		      dst_output);
out:
	if (!err) {
		ICMP6MSGOUT_INC_STATS(net, idev, ICMPV6_MLD2_REPORT);
		ICMP6_INC_STATS(net, idev, ICMP6_MIB_OUTMSGS);
	} else {
		IP6_INC_STATS(net, idev, IPSTATS_MIB_OUTDISCARDS);
	}

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
	int type, struct mld2_grec **ppgr, unsigned int mtu)
{
	struct mld2_report *pmr;
	struct mld2_grec *pgr;

	if (!skb) {
		skb = mld_newpack(pmc->idev, mtu);
		if (!skb)
			return NULL;
	}
	pgr = skb_put(skb, sizeof(struct mld2_grec));
	pgr->grec_type = type;
	pgr->grec_auxwords = 0;
	pgr->grec_nsrcs = 0;
	pgr->grec_mca = pmc->mca_addr;	/* structure copy */
	pmr = (struct mld2_report *)skb_transport_header(skb);
	pmr->mld2r_ngrec = htons(ntohs(pmr->mld2r_ngrec)+1);
	*ppgr = pgr;
	return skb;
}

#define AVAILABLE(skb)	((skb) ? skb_availroom(skb) : 0)

/* called with mc_lock */
static struct sk_buff *add_grec(struct sk_buff *skb, struct ifmcaddr6 *pmc,
				int type, int gdeleted, int sdeleted,
				int crsend)
{
	struct ip6_sf_list *psf, *psf_prev, *psf_next;
	int scount, stotal, first, isquery, truncate;
	struct ip6_sf_list __rcu **psf_list;
	struct inet6_dev *idev = pmc->idev;
	struct net_device *dev = idev->dev;
	struct mld2_grec *pgr = NULL;
	struct mld2_report *pmr;
	unsigned int mtu;

	if (pmc->mca_flags & MAF_NOREPORT)
		return skb;

	mtu = READ_ONCE(dev->mtu);
	if (mtu < IPV6_MIN_MTU)
		return skb;

	isquery = type == MLD2_MODE_IS_INCLUDE ||
		  type == MLD2_MODE_IS_EXCLUDE;
	truncate = type == MLD2_MODE_IS_EXCLUDE ||
		    type == MLD2_CHANGE_TO_EXCLUDE;

	stotal = scount = 0;

	psf_list = sdeleted ? &pmc->mca_tomb : &pmc->mca_sources;

	if (!rcu_access_pointer(*psf_list))
		goto empty_source;

	pmr = skb ? (struct mld2_report *)skb_transport_header(skb) : NULL;

	/* EX and TO_EX get a fresh packet, if needed */
	if (truncate) {
		if (pmr && pmr->mld2r_ngrec &&
		    AVAILABLE(skb) < grec_size(pmc, type, gdeleted, sdeleted)) {
			if (skb)
				mld_sendpack(skb);
			skb = mld_newpack(idev, mtu);
		}
	}
	first = 1;
	psf_prev = NULL;
	for (psf = mc_dereference(*psf_list, idev);
	     psf;
	     psf = psf_next) {
		struct in6_addr *psrc;

		psf_next = mc_dereference(psf->sf_next, idev);

		if (!is_in(pmc, psf, type, gdeleted, sdeleted) && !crsend) {
			psf_prev = psf;
			continue;
		}

		/* Based on RFC3810 6.1. Should not send source-list change
		 * records when there is a filter mode change.
		 */
		if (((gdeleted && pmc->mca_sfmode == MCAST_EXCLUDE) ||
		     (!gdeleted && pmc->mca_crcount)) &&
		    (type == MLD2_ALLOW_NEW_SOURCES ||
		     type == MLD2_BLOCK_OLD_SOURCES) && psf->sf_crcount)
			goto decrease_sf_crcount;

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
			skb = mld_newpack(idev, mtu);
			first = 1;
			scount = 0;
		}
		if (first) {
			skb = add_grhead(skb, pmc, type, &pgr, mtu);
			first = 0;
		}
		if (!skb)
			return NULL;
		psrc = skb_put(skb, sizeof(*psrc));
		*psrc = psf->sf_addr;
		scount++; stotal++;
		if ((type == MLD2_ALLOW_NEW_SOURCES ||
		     type == MLD2_BLOCK_OLD_SOURCES) && psf->sf_crcount) {
decrease_sf_crcount:
			psf->sf_crcount--;
			if ((sdeleted || gdeleted) && psf->sf_crcount == 0) {
				if (psf_prev)
					rcu_assign_pointer(psf_prev->sf_next,
							   mc_dereference(psf->sf_next, idev));
				else
					rcu_assign_pointer(*psf_list,
							   mc_dereference(psf->sf_next, idev));
				kfree_rcu(psf, rcu);
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
		if (pmc->mca_crcount || isquery || crsend) {
			/* make sure we have room for group header */
			if (skb && AVAILABLE(skb) < sizeof(struct mld2_grec)) {
				mld_sendpack(skb);
				skb = NULL; /* add_grhead will get a new one */
			}
			skb = add_grhead(skb, pmc, type, &pgr, mtu);
		}
	}
	if (pgr)
		pgr->grec_nsrcs = htons(scount);

	if (isquery)
		pmc->mca_flags &= ~MAF_GSQUERY;	/* clear query state */
	return skb;
}

/* called with mc_lock */
static void mld_send_report(struct inet6_dev *idev, struct ifmcaddr6 *pmc)
{
	struct sk_buff *skb = NULL;
	int type;

	if (!pmc) {
		for_each_mc_mclock(idev, pmc) {
			if (pmc->mca_flags & MAF_NOREPORT)
				continue;
			if (pmc->mca_sfcount[MCAST_EXCLUDE])
				type = MLD2_MODE_IS_EXCLUDE;
			else
				type = MLD2_MODE_IS_INCLUDE;
			skb = add_grec(skb, pmc, type, 0, 0, 0);
		}
	} else {
		if (pmc->mca_sfcount[MCAST_EXCLUDE])
			type = MLD2_MODE_IS_EXCLUDE;
		else
			type = MLD2_MODE_IS_INCLUDE;
		skb = add_grec(skb, pmc, type, 0, 0, 0);
	}
	if (skb)
		mld_sendpack(skb);
}

/*
 * remove zero-count source records from a source filter list
 * called with mc_lock
 */
static void mld_clear_zeros(struct ip6_sf_list __rcu **ppsf, struct inet6_dev *idev)
{
	struct ip6_sf_list *psf_prev, *psf_next, *psf;

	psf_prev = NULL;
	for (psf = mc_dereference(*ppsf, idev);
	     psf;
	     psf = psf_next) {
		psf_next = mc_dereference(psf->sf_next, idev);
		if (psf->sf_crcount == 0) {
			if (psf_prev)
				rcu_assign_pointer(psf_prev->sf_next,
						   mc_dereference(psf->sf_next, idev));
			else
				rcu_assign_pointer(*ppsf,
						   mc_dereference(psf->sf_next, idev));
			kfree_rcu(psf, rcu);
		} else {
			psf_prev = psf;
		}
	}
}

/* called with mc_lock */
static void mld_send_cr(struct inet6_dev *idev)
{
	struct ifmcaddr6 *pmc, *pmc_prev, *pmc_next;
	struct sk_buff *skb = NULL;
	int type, dtype;

	/* deleted MCA's */
	pmc_prev = NULL;
	for (pmc = mc_dereference(idev->mc_tomb, idev);
	     pmc;
	     pmc = pmc_next) {
		pmc_next = mc_dereference(pmc->next, idev);
		if (pmc->mca_sfmode == MCAST_INCLUDE) {
			type = MLD2_BLOCK_OLD_SOURCES;
			dtype = MLD2_BLOCK_OLD_SOURCES;
			skb = add_grec(skb, pmc, type, 1, 0, 0);
			skb = add_grec(skb, pmc, dtype, 1, 1, 0);
		}
		if (pmc->mca_crcount) {
			if (pmc->mca_sfmode == MCAST_EXCLUDE) {
				type = MLD2_CHANGE_TO_INCLUDE;
				skb = add_grec(skb, pmc, type, 1, 0, 0);
			}
			pmc->mca_crcount--;
			if (pmc->mca_crcount == 0) {
				mld_clear_zeros(&pmc->mca_tomb, idev);
				mld_clear_zeros(&pmc->mca_sources, idev);
			}
		}
		if (pmc->mca_crcount == 0 &&
		    !rcu_access_pointer(pmc->mca_tomb) &&
		    !rcu_access_pointer(pmc->mca_sources)) {
			if (pmc_prev)
				rcu_assign_pointer(pmc_prev->next, pmc_next);
			else
				rcu_assign_pointer(idev->mc_tomb, pmc_next);
			in6_dev_put(pmc->idev);
			kfree_rcu(pmc, rcu);
		} else
			pmc_prev = pmc;
	}

	/* change recs */
	for_each_mc_mclock(idev, pmc) {
		if (pmc->mca_sfcount[MCAST_EXCLUDE]) {
			type = MLD2_BLOCK_OLD_SOURCES;
			dtype = MLD2_ALLOW_NEW_SOURCES;
		} else {
			type = MLD2_ALLOW_NEW_SOURCES;
			dtype = MLD2_BLOCK_OLD_SOURCES;
		}
		skb = add_grec(skb, pmc, type, 0, 0, 0);
		skb = add_grec(skb, pmc, dtype, 0, 1, 0);	/* deleted sources */

		/* filter mode changes */
		if (pmc->mca_crcount) {
			if (pmc->mca_sfmode == MCAST_EXCLUDE)
				type = MLD2_CHANGE_TO_EXCLUDE;
			else
				type = MLD2_CHANGE_TO_INCLUDE;
			skb = add_grec(skb, pmc, type, 0, 0, 0);
			pmc->mca_crcount--;
		}
	}
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

	if (!skb) {
		rcu_read_lock();
		IP6_INC_STATS(net, __in6_dev_get(dev),
			      IPSTATS_MIB_OUTDISCARDS);
		rcu_read_unlock();
		return;
	}
	skb->priority = TC_PRIO_CONTROL;
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

	skb_put_data(skb, ra, sizeof(ra));

	hdr = skb_put_zero(skb, sizeof(struct mld_msg));
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
	err = NF_HOOK(NFPROTO_IPV6, NF_INET_LOCAL_OUT,
		      net, sk, skb, NULL, skb->dev,
		      dst_output);
out:
	if (!err) {
		ICMP6MSGOUT_INC_STATS(net, idev, type);
		ICMP6_INC_STATS(net, idev, ICMP6_MIB_OUTMSGS);
	} else
		IP6_INC_STATS(net, idev, IPSTATS_MIB_OUTDISCARDS);

	rcu_read_unlock();
	return;

err_out:
	kfree_skb(skb);
	goto out;
}

/* called with mc_lock */
static void mld_send_initial_cr(struct inet6_dev *idev)
{
	struct sk_buff *skb;
	struct ifmcaddr6 *pmc;
	int type;

	if (mld_in_v1_mode(idev))
		return;

	skb = NULL;
	for_each_mc_mclock(idev, pmc) {
		if (pmc->mca_sfcount[MCAST_EXCLUDE])
			type = MLD2_CHANGE_TO_EXCLUDE;
		else
			type = MLD2_ALLOW_NEW_SOURCES;
		skb = add_grec(skb, pmc, type, 0, 0, 1);
	}
	if (skb)
		mld_sendpack(skb);
}

void ipv6_mc_dad_complete(struct inet6_dev *idev)
{
	mutex_lock(&idev->mc_lock);
	idev->mc_dad_count = idev->mc_qrv;
	if (idev->mc_dad_count) {
		mld_send_initial_cr(idev);
		idev->mc_dad_count--;
		if (idev->mc_dad_count)
			mld_dad_start_work(idev,
					   unsolicited_report_interval(idev));
	}
	mutex_unlock(&idev->mc_lock);
}

static void mld_dad_work(struct work_struct *work)
{
	struct inet6_dev *idev = container_of(to_delayed_work(work),
					      struct inet6_dev,
					      mc_dad_work);
	mutex_lock(&idev->mc_lock);
	mld_send_initial_cr(idev);
	if (idev->mc_dad_count) {
		idev->mc_dad_count--;
		if (idev->mc_dad_count)
			mld_dad_start_work(idev,
					   unsolicited_report_interval(idev));
	}
	mutex_unlock(&idev->mc_lock);
	in6_dev_put(idev);
}

/* called with mc_lock */
static int ip6_mc_del1_src(struct ifmcaddr6 *pmc, int sfmode,
	const struct in6_addr *psfsrc)
{
	struct ip6_sf_list *psf, *psf_prev;
	int rv = 0;

	psf_prev = NULL;
	for_each_psf_mclock(pmc, psf) {
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
			rcu_assign_pointer(psf_prev->sf_next,
					   mc_dereference(psf->sf_next, idev));
		else
			rcu_assign_pointer(pmc->mca_sources,
					   mc_dereference(psf->sf_next, idev));

		if (psf->sf_oldin && !(pmc->mca_flags & MAF_NOREPORT) &&
		    !mld_in_v1_mode(idev)) {
			psf->sf_crcount = idev->mc_qrv;
			rcu_assign_pointer(psf->sf_next,
					   mc_dereference(pmc->mca_tomb, idev));
			rcu_assign_pointer(pmc->mca_tomb, psf);
			rv = 1;
		} else {
			kfree_rcu(psf, rcu);
		}
	}
	return rv;
}

/* called with mc_lock */
static int ip6_mc_del_src(struct inet6_dev *idev, const struct in6_addr *pmca,
			  int sfmode, int sfcount, const struct in6_addr *psfsrc,
			  int delta)
{
	struct ifmcaddr6 *pmc;
	int	changerec = 0;
	int	i, err;

	if (!idev)
		return -ENODEV;

	for_each_mc_mclock(idev, pmc) {
		if (ipv6_addr_equal(pmca, &pmc->mca_addr))
			break;
	}
	if (!pmc)
		return -ESRCH;

	sf_markstate(pmc);
	if (!delta) {
		if (!pmc->mca_sfcount[sfmode])
			return -EINVAL;

		pmc->mca_sfcount[sfmode]--;
	}
	err = 0;
	for (i = 0; i < sfcount; i++) {
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
		for_each_psf_mclock(pmc, psf)
			psf->sf_crcount = 0;
		mld_ifc_event(pmc->idev);
	} else if (sf_setstate(pmc) || changerec) {
		mld_ifc_event(pmc->idev);
	}

	return err;
}

/*
 * Add multicast single-source filter to the interface list
 * called with mc_lock
 */
static int ip6_mc_add1_src(struct ifmcaddr6 *pmc, int sfmode,
	const struct in6_addr *psfsrc)
{
	struct ip6_sf_list *psf, *psf_prev;

	psf_prev = NULL;
	for_each_psf_mclock(pmc, psf) {
		if (ipv6_addr_equal(&psf->sf_addr, psfsrc))
			break;
		psf_prev = psf;
	}
	if (!psf) {
		psf = kzalloc(sizeof(*psf), GFP_KERNEL);
		if (!psf)
			return -ENOBUFS;

		psf->sf_addr = *psfsrc;
		if (psf_prev) {
			rcu_assign_pointer(psf_prev->sf_next, psf);
		} else {
			rcu_assign_pointer(pmc->mca_sources, psf);
		}
	}
	psf->sf_count[sfmode]++;
	return 0;
}

/* called with mc_lock */
static void sf_markstate(struct ifmcaddr6 *pmc)
{
	struct ip6_sf_list *psf;
	int mca_xcount = pmc->mca_sfcount[MCAST_EXCLUDE];

	for_each_psf_mclock(pmc, psf) {
		if (pmc->mca_sfcount[MCAST_EXCLUDE]) {
			psf->sf_oldin = mca_xcount ==
				psf->sf_count[MCAST_EXCLUDE] &&
				!psf->sf_count[MCAST_INCLUDE];
		} else {
			psf->sf_oldin = psf->sf_count[MCAST_INCLUDE] != 0;
		}
	}
}

/* called with mc_lock */
static int sf_setstate(struct ifmcaddr6 *pmc)
{
	struct ip6_sf_list *psf, *dpsf;
	int mca_xcount = pmc->mca_sfcount[MCAST_EXCLUDE];
	int qrv = pmc->idev->mc_qrv;
	int new_in, rv;

	rv = 0;
	for_each_psf_mclock(pmc, psf) {
		if (pmc->mca_sfcount[MCAST_EXCLUDE]) {
			new_in = mca_xcount == psf->sf_count[MCAST_EXCLUDE] &&
				!psf->sf_count[MCAST_INCLUDE];
		} else
			new_in = psf->sf_count[MCAST_INCLUDE] != 0;
		if (new_in) {
			if (!psf->sf_oldin) {
				struct ip6_sf_list *prev = NULL;

				for_each_psf_tomb(pmc, dpsf) {
					if (ipv6_addr_equal(&dpsf->sf_addr,
					    &psf->sf_addr))
						break;
					prev = dpsf;
				}
				if (dpsf) {
					if (prev)
						rcu_assign_pointer(prev->sf_next,
								   mc_dereference(dpsf->sf_next,
										  pmc->idev));
					else
						rcu_assign_pointer(pmc->mca_tomb,
								   mc_dereference(dpsf->sf_next,
										  pmc->idev));
					kfree_rcu(dpsf, rcu);
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

			for_each_psf_tomb(pmc, dpsf)
				if (ipv6_addr_equal(&dpsf->sf_addr,
				    &psf->sf_addr))
					break;
			if (!dpsf) {
				dpsf = kmalloc(sizeof(*dpsf), GFP_KERNEL);
				if (!dpsf)
					continue;
				*dpsf = *psf;
				rcu_assign_pointer(dpsf->sf_next,
						   mc_dereference(pmc->mca_tomb, pmc->idev));
				rcu_assign_pointer(pmc->mca_tomb, dpsf);
			}
			dpsf->sf_crcount = qrv;
			rv++;
		}
	}
	return rv;
}

/*
 * Add multicast source filter list to the interface list
 * called with mc_lock
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

	for_each_mc_mclock(idev, pmc) {
		if (ipv6_addr_equal(pmca, &pmc->mca_addr))
			break;
	}
	if (!pmc)
		return -ESRCH;

	sf_markstate(pmc);
	isexclude = pmc->mca_sfmode == MCAST_EXCLUDE;
	if (!delta)
		pmc->mca_sfcount[sfmode]++;
	err = 0;
	for (i = 0; i < sfcount; i++) {
		err = ip6_mc_add1_src(pmc, sfmode, &psfsrc[i]);
		if (err)
			break;
	}
	if (err) {
		int j;

		if (!delta)
			pmc->mca_sfcount[sfmode]--;
		for (j = 0; j < i; j++)
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
		for_each_psf_mclock(pmc, psf)
			psf->sf_crcount = 0;
		mld_ifc_event(idev);
	} else if (sf_setstate(pmc)) {
		mld_ifc_event(idev);
	}
	return err;
}

/* called with mc_lock */
static void ip6_mc_clear_src(struct ifmcaddr6 *pmc)
{
	struct ip6_sf_list *psf, *nextpsf;

	for (psf = mc_dereference(pmc->mca_tomb, pmc->idev);
	     psf;
	     psf = nextpsf) {
		nextpsf = mc_dereference(psf->sf_next, pmc->idev);
		kfree_rcu(psf, rcu);
	}
	RCU_INIT_POINTER(pmc->mca_tomb, NULL);
	for (psf = mc_dereference(pmc->mca_sources, pmc->idev);
	     psf;
	     psf = nextpsf) {
		nextpsf = mc_dereference(psf->sf_next, pmc->idev);
		kfree_rcu(psf, rcu);
	}
	RCU_INIT_POINTER(pmc->mca_sources, NULL);
	pmc->mca_sfmode = MCAST_EXCLUDE;
	pmc->mca_sfcount[MCAST_INCLUDE] = 0;
	pmc->mca_sfcount[MCAST_EXCLUDE] = 1;
}

/* called with mc_lock */
static void igmp6_join_group(struct ifmcaddr6 *ma)
{
	unsigned long delay;

	if (ma->mca_flags & MAF_NOREPORT)
		return;

	igmp6_send(&ma->mca_addr, ma->idev->dev, ICMPV6_MGM_REPORT);

	delay = prandom_u32() % unsolicited_report_interval(ma->idev);

	if (cancel_delayed_work(&ma->mca_work)) {
		refcount_dec(&ma->mca_refcnt);
		delay = ma->mca_work.timer.expires - jiffies;
	}

	if (!mod_delayed_work(mld_wq, &ma->mca_work, delay))
		refcount_inc(&ma->mca_refcnt);
	ma->mca_flags |= MAF_TIMER_RUNNING | MAF_LAST_REPORTER;
}

static int ip6_mc_leave_src(struct sock *sk, struct ipv6_mc_socklist *iml,
			    struct inet6_dev *idev)
{
	struct ip6_sf_socklist *psl;
	int err;

	psl = sock_dereference(iml->sflist, sk);

	if (idev)
		mutex_lock(&idev->mc_lock);

	if (!psl) {
		/* any-source empty exclude case */
		err = ip6_mc_del_src(idev, &iml->addr, iml->sfmode, 0, NULL, 0);
	} else {
		err = ip6_mc_del_src(idev, &iml->addr, iml->sfmode,
				     psl->sl_count, psl->sl_addr, 0);
		RCU_INIT_POINTER(iml->sflist, NULL);
		atomic_sub(struct_size(psl, sl_addr, psl->sl_max),
			   &sk->sk_omem_alloc);
		kfree_rcu(psl, rcu);
	}

	if (idev)
		mutex_unlock(&idev->mc_lock);

	return err;
}

/* called with mc_lock */
static void igmp6_leave_group(struct ifmcaddr6 *ma)
{
	if (mld_in_v1_mode(ma->idev)) {
		if (ma->mca_flags & MAF_LAST_REPORTER) {
			igmp6_send(&ma->mca_addr, ma->idev->dev,
				ICMPV6_MGM_REDUCTION);
		}
	} else {
		mld_add_delrec(ma->idev, ma);
		mld_ifc_event(ma->idev);
	}
}

static void mld_gq_work(struct work_struct *work)
{
	struct inet6_dev *idev = container_of(to_delayed_work(work),
					      struct inet6_dev,
					      mc_gq_work);

	mutex_lock(&idev->mc_lock);
	mld_send_report(idev, NULL);
	idev->mc_gq_running = 0;
	mutex_unlock(&idev->mc_lock);

	in6_dev_put(idev);
}

static void mld_ifc_work(struct work_struct *work)
{
	struct inet6_dev *idev = container_of(to_delayed_work(work),
					      struct inet6_dev,
					      mc_ifc_work);

	mutex_lock(&idev->mc_lock);
	mld_send_cr(idev);

	if (idev->mc_ifc_count) {
		idev->mc_ifc_count--;
		if (idev->mc_ifc_count)
			mld_ifc_start_work(idev,
					   unsolicited_report_interval(idev));
	}
	mutex_unlock(&idev->mc_lock);
	in6_dev_put(idev);
}

/* called with mc_lock */
static void mld_ifc_event(struct inet6_dev *idev)
{
	if (mld_in_v1_mode(idev))
		return;

	idev->mc_ifc_count = idev->mc_qrv;
	mld_ifc_start_work(idev, 1);
}

static void mld_mca_work(struct work_struct *work)
{
	struct ifmcaddr6 *ma = container_of(to_delayed_work(work),
					    struct ifmcaddr6, mca_work);

	mutex_lock(&ma->idev->mc_lock);
	if (mld_in_v1_mode(ma->idev))
		igmp6_send(&ma->mca_addr, ma->idev->dev, ICMPV6_MGM_REPORT);
	else
		mld_send_report(ma->idev, ma);
	ma->mca_flags |=  MAF_LAST_REPORTER;
	ma->mca_flags &= ~MAF_TIMER_RUNNING;
	mutex_unlock(&ma->idev->mc_lock);

	ma_put(ma);
}

/* Device changing type */

void ipv6_mc_unmap(struct inet6_dev *idev)
{
	struct ifmcaddr6 *i;

	/* Install multicast list, except for all-nodes (already installed) */

	mutex_lock(&idev->mc_lock);
	for_each_mc_mclock(idev, i)
		igmp6_group_dropped(i);
	mutex_unlock(&idev->mc_lock);
}

void ipv6_mc_remap(struct inet6_dev *idev)
{
	ipv6_mc_up(idev);
}

/* Device going down */
void ipv6_mc_down(struct inet6_dev *idev)
{
	struct ifmcaddr6 *i;

	mutex_lock(&idev->mc_lock);
	/* Withdraw multicast list */
	for_each_mc_mclock(idev, i)
		igmp6_group_dropped(i);
	mutex_unlock(&idev->mc_lock);

	/* Should stop work after group drop. or we will
	 * start work again in mld_ifc_event()
	 */
	synchronize_net();
	mld_query_stop_work(idev);
	mld_report_stop_work(idev);
	mld_ifc_stop_work(idev);
	mld_gq_stop_work(idev);
	mld_dad_stop_work(idev);
}

static void ipv6_mc_reset(struct inet6_dev *idev)
{
	idev->mc_qrv = sysctl_mld_qrv;
	idev->mc_qi = MLD_QI_DEFAULT;
	idev->mc_qri = MLD_QRI_DEFAULT;
	idev->mc_v1_seen = 0;
	idev->mc_maxdelay = unsolicited_report_interval(idev);
}

/* Device going up */

void ipv6_mc_up(struct inet6_dev *idev)
{
	struct ifmcaddr6 *i;

	/* Install multicast list, except for all-nodes (already installed) */

	ipv6_mc_reset(idev);
	mutex_lock(&idev->mc_lock);
	for_each_mc_mclock(idev, i) {
		mld_del_delrec(idev, i);
		igmp6_group_added(i);
	}
	mutex_unlock(&idev->mc_lock);
}

/* IPv6 device initialization. */

void ipv6_mc_init_dev(struct inet6_dev *idev)
{
	idev->mc_gq_running = 0;
	INIT_DELAYED_WORK(&idev->mc_gq_work, mld_gq_work);
	RCU_INIT_POINTER(idev->mc_tomb, NULL);
	idev->mc_ifc_count = 0;
	INIT_DELAYED_WORK(&idev->mc_ifc_work, mld_ifc_work);
	INIT_DELAYED_WORK(&idev->mc_dad_work, mld_dad_work);
	INIT_DELAYED_WORK(&idev->mc_query_work, mld_query_work);
	INIT_DELAYED_WORK(&idev->mc_report_work, mld_report_work);
	skb_queue_head_init(&idev->mc_query_queue);
	skb_queue_head_init(&idev->mc_report_queue);
	spin_lock_init(&idev->mc_query_lock);
	spin_lock_init(&idev->mc_report_lock);
	mutex_init(&idev->mc_lock);
	ipv6_mc_reset(idev);
}

/*
 *	Device is about to be destroyed: clean up.
 */

void ipv6_mc_destroy_dev(struct inet6_dev *idev)
{
	struct ifmcaddr6 *i;

	/* Deactivate works */
	ipv6_mc_down(idev);
	mutex_lock(&idev->mc_lock);
	mld_clear_delrec(idev);
	mutex_unlock(&idev->mc_lock);
	mld_clear_query(idev);
	mld_clear_report(idev);

	/* Delete all-nodes address. */
	/* We cannot call ipv6_dev_mc_dec() directly, our caller in
	 * addrconf.c has NULL'd out dev->ip6_ptr so in6_dev_get() will
	 * fail.
	 */
	__ipv6_dev_mc_dec(idev, &in6addr_linklocal_allnodes);

	if (idev->cnf.forwarding)
		__ipv6_dev_mc_dec(idev, &in6addr_linklocal_allrouters);

	mutex_lock(&idev->mc_lock);
	while ((i = mc_dereference(idev->mc_list, idev))) {
		rcu_assign_pointer(idev->mc_list, mc_dereference(i->next, idev));

		ip6_mc_clear_src(i);
		ma_put(i);
	}
	mutex_unlock(&idev->mc_lock);
}

static void ipv6_mc_rejoin_groups(struct inet6_dev *idev)
{
	struct ifmcaddr6 *pmc;

	ASSERT_RTNL();

	mutex_lock(&idev->mc_lock);
	if (mld_in_v1_mode(idev)) {
		for_each_mc_mclock(idev, pmc)
			igmp6_join_group(pmc);
	} else {
		mld_send_report(idev, NULL);
	}
	mutex_unlock(&idev->mc_lock);
}

static int ipv6_mc_netdev_event(struct notifier_block *this,
				unsigned long event,
				void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct inet6_dev *idev = __in6_dev_get(dev);

	switch (event) {
	case NETDEV_RESEND_IGMP:
		if (idev)
			ipv6_mc_rejoin_groups(idev);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block igmp6_netdev_notifier = {
	.notifier_call = ipv6_mc_netdev_event,
};

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

		im = rcu_dereference(idev->mc_list);
		if (im) {
			state->idev = idev;
			break;
		}
	}
	return im;
}

static struct ifmcaddr6 *igmp6_mc_get_next(struct seq_file *seq, struct ifmcaddr6 *im)
{
	struct igmp6_mc_iter_state *state = igmp6_mc_seq_private(seq);

	im = rcu_dereference(im->next);
	while (!im) {
		state->dev = next_net_device_rcu(state->dev);
		if (!state->dev) {
			state->idev = NULL;
			break;
		}
		state->idev = __in6_dev_get(state->dev);
		if (!state->idev)
			continue;
		im = rcu_dereference(state->idev->mc_list);
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

	if (likely(state->idev))
		state->idev = NULL;
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
		   (im->mca_flags & MAF_TIMER_RUNNING) ?
		   jiffies_to_clock_t(im->mca_work.timer.expires - jiffies) : 0);
	return 0;
}

static const struct seq_operations igmp6_mc_seq_ops = {
	.start	=	igmp6_mc_seq_start,
	.next	=	igmp6_mc_seq_next,
	.stop	=	igmp6_mc_seq_stop,
	.show	=	igmp6_mc_seq_show,
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

		im = rcu_dereference(idev->mc_list);
		if (likely(im)) {
			psf = rcu_dereference(im->mca_sources);
			if (likely(psf)) {
				state->im = im;
				state->idev = idev;
				break;
			}
		}
	}
	return psf;
}

static struct ip6_sf_list *igmp6_mcf_get_next(struct seq_file *seq, struct ip6_sf_list *psf)
{
	struct igmp6_mcf_iter_state *state = igmp6_mcf_seq_private(seq);

	psf = rcu_dereference(psf->sf_next);
	while (!psf) {
		state->im = rcu_dereference(state->im->next);
		while (!state->im) {
			state->dev = next_net_device_rcu(state->dev);
			if (!state->dev) {
				state->idev = NULL;
				goto out;
			}
			state->idev = __in6_dev_get(state->dev);
			if (!state->idev)
				continue;
			state->im = rcu_dereference(state->idev->mc_list);
		}
		if (!state->im)
			break;
		psf = rcu_dereference(state->im->mca_sources);
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

	if (likely(state->im))
		state->im = NULL;
	if (likely(state->idev))
		state->idev = NULL;

	state->dev = NULL;
	rcu_read_unlock();
}

static int igmp6_mcf_seq_show(struct seq_file *seq, void *v)
{
	struct ip6_sf_list *psf = (struct ip6_sf_list *)v;
	struct igmp6_mcf_iter_state *state = igmp6_mcf_seq_private(seq);

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "Idx Device                Multicast Address                   Source Address    INC    EXC\n");
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

static int __net_init igmp6_proc_init(struct net *net)
{
	int err;

	err = -ENOMEM;
	if (!proc_create_net("igmp6", 0444, net->proc_net, &igmp6_mc_seq_ops,
			sizeof(struct igmp6_mc_iter_state)))
		goto out;
	if (!proc_create_net("mcfilter6", 0444, net->proc_net,
			&igmp6_mcf_seq_ops,
			sizeof(struct igmp6_mcf_iter_state)))
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
	net->ipv6.igmp_sk->sk_allocation = GFP_KERNEL;

	err = inet_ctl_sock_create(&net->ipv6.mc_autojoin_sk, PF_INET6,
				   SOCK_RAW, IPPROTO_ICMPV6, net);
	if (err < 0) {
		pr_err("Failed to initialize the IGMP6 autojoin socket (err %d)\n",
		       err);
		goto out_sock_create;
	}

	err = igmp6_proc_init(net);
	if (err)
		goto out_sock_create_autojoin;

	return 0;

out_sock_create_autojoin:
	inet_ctl_sock_destroy(net->ipv6.mc_autojoin_sk);
out_sock_create:
	inet_ctl_sock_destroy(net->ipv6.igmp_sk);
out:
	return err;
}

static void __net_exit igmp6_net_exit(struct net *net)
{
	inet_ctl_sock_destroy(net->ipv6.igmp_sk);
	inet_ctl_sock_destroy(net->ipv6.mc_autojoin_sk);
	igmp6_proc_exit(net);
}

static struct pernet_operations igmp6_net_ops = {
	.init = igmp6_net_init,
	.exit = igmp6_net_exit,
};

int __init igmp6_init(void)
{
	int err;

	err = register_pernet_subsys(&igmp6_net_ops);
	if (err)
		return err;

	mld_wq = create_workqueue("mld");
	if (!mld_wq) {
		unregister_pernet_subsys(&igmp6_net_ops);
		return -ENOMEM;
	}

	return err;
}

int __init igmp6_late_init(void)
{
	return register_netdevice_notifier(&igmp6_netdev_notifier);
}

void igmp6_cleanup(void)
{
	unregister_pernet_subsys(&igmp6_net_ops);
	destroy_workqueue(mld_wq);
}

void igmp6_late_cleanup(void)
{
	unregister_netdevice_notifier(&igmp6_netdev_notifier);
}
