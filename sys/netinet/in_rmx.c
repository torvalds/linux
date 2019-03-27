/*-
 * Copyright 1994, 1995 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/mbuf.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/route_var.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_var.h>

extern int	in_inithead(void **head, int off);
#ifdef VIMAGE
extern int	in_detachhead(void **head, int off);
#endif

/*
 * Do what we need to do when inserting a route.
 */
static struct radix_node *
in_addroute(void *v_arg, void *n_arg, struct radix_head *head,
    struct radix_node *treenodes)
{
	struct rtentry *rt = (struct rtentry *)treenodes;
	struct sockaddr_in *sin = (struct sockaddr_in *)rt_key(rt);

	/*
	 * A little bit of help for both IP output and input:
	 *   For host routes, we make sure that RTF_BROADCAST
	 *   is set for anything that looks like a broadcast address.
	 *   This way, we can avoid an expensive call to in_broadcast()
	 *   in ip_output() most of the time (because the route passed
	 *   to ip_output() is almost always a host route).
	 *
	 *   We also do the same for local addresses, with the thought
	 *   that this might one day be used to speed up ip_input().
	 *
	 * We also mark routes to multicast addresses as such, because
	 * it's easy to do and might be useful (but this is much more
	 * dubious since it's so easy to inspect the address).
	 */
	if (rt->rt_flags & RTF_HOST) {
		if (in_broadcast(sin->sin_addr, rt->rt_ifp)) {
			rt->rt_flags |= RTF_BROADCAST;
		} else if (satosin(rt->rt_ifa->ifa_addr)->sin_addr.s_addr ==
		    sin->sin_addr.s_addr) {
			rt->rt_flags |= RTF_LOCAL;
		}
	}
	if (IN_MULTICAST(ntohl(sin->sin_addr.s_addr)))
		rt->rt_flags |= RTF_MULTICAST;

	if (rt->rt_ifp != NULL) {

		/*
		 * Check route MTU:
		 * inherit interface MTU if not set or
		 * check if MTU is too large.
		 */
		if (rt->rt_mtu == 0) {
			rt->rt_mtu = rt->rt_ifp->if_mtu;
		} else if (rt->rt_mtu > rt->rt_ifp->if_mtu)
			rt->rt_mtu = rt->rt_ifp->if_mtu;
	}

	return (rn_addroute(v_arg, n_arg, head, treenodes));
}

static int _in_rt_was_here;
/*
 * Initialize our routing tree.
 */
int
in_inithead(void **head, int off)
{
	struct rib_head *rh;

	rh = rt_table_init(32);
	if (rh == NULL)
		return (0);

	rh->rnh_addaddr = in_addroute;
	*head = (void *)rh;

	if (_in_rt_was_here == 0 ) {
		_in_rt_was_here = 1;
	}
	return 1;
}

#ifdef VIMAGE
int
in_detachhead(void **head, int off)
{

	rt_table_destroy((struct rib_head *)(*head));
	return (1);
}
#endif

/*
 * This zaps old routes when the interface goes down or interface
 * address is deleted.  In the latter case, it deletes static routes
 * that point to this address.  If we don't do this, we may end up
 * using the old address in the future.  The ones we always want to
 * get rid of are things like ARP entries, since the user might down
 * the interface, walk over to a completely different network, and
 * plug back in.
 */
struct in_ifadown_arg {
	struct ifaddr *ifa;
	int del;
};

static int
in_ifadownkill(const struct rtentry *rt, void *xap)
{
	struct in_ifadown_arg *ap = xap;

	if (rt->rt_ifa != ap->ifa)
		return (0);

	if ((rt->rt_flags & RTF_STATIC) != 0 && ap->del == 0)
		return (0);

	return (1);
}

void
in_ifadown(struct ifaddr *ifa, int delete)
{
	struct in_ifadown_arg arg;

	KASSERT(ifa->ifa_addr->sa_family == AF_INET,
	    ("%s: wrong family", __func__));

	arg.ifa = ifa;
	arg.del = delete;

	rt_foreach_fib_walk_del(AF_INET, in_ifadownkill, &arg);
	ifa->ifa_flags &= ~IFA_ROUTE;		/* XXXlocking? */
}

/*
 * inet versions of rt functions. These have fib extensions and 
 * for now will just reference the _fib variants.
 * eventually this order will be reversed,
 */
void
in_rtalloc_ign(struct route *ro, u_long ignflags, u_int fibnum)
{
	rtalloc_ign_fib(ro, ignflags, fibnum);
}

void
in_rtredirect(struct sockaddr *dst,
	struct sockaddr *gateway,
	struct sockaddr *netmask,
	int flags,
	struct sockaddr *src,
	u_int fibnum)
{
	rtredirect_fib(dst, gateway, netmask, flags, src, fibnum);
}
 
