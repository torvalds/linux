/*-
 * Copyright 1998 Massachusetts Institute of Technology
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
 *
 * $FreeBSD$
 */

#ifndef _NET_IF_VLAN_VAR_H_
#define	_NET_IF_VLAN_VAR_H_	1

/* Set the VLAN ID in an mbuf packet header non-destructively. */
#define EVL_APPLY_VLID(m, vlid)						\
	do {								\
		if ((m)->m_flags & M_VLANTAG) {				\
			(m)->m_pkthdr.ether_vtag &= EVL_VLID_MASK;	\
			(m)->m_pkthdr.ether_vtag |= (vlid);		\
		} else {						\
			(m)->m_pkthdr.ether_vtag = (vlid);		\
			(m)->m_flags |= M_VLANTAG;			\
		}							\
	} while (0)

/* Set the priority ID in an mbuf packet header non-destructively. */
#define EVL_APPLY_PRI(m, pri)						\
	do {								\
		if ((m)->m_flags & M_VLANTAG) {				\
			uint16_t __vlantag = (m)->m_pkthdr.ether_vtag;	\
			(m)->m_pkthdr.ether_vtag |= EVL_MAKETAG(	\
			    EVL_VLANOFTAG(__vlantag), (pri),		\
			    EVL_CFIOFTAG(__vlantag));			\
		} else {						\
			(m)->m_pkthdr.ether_vtag =			\
			    EVL_MAKETAG(0, (pri), 0);			\
			(m)->m_flags |= M_VLANTAG;			\
		}							\
	} while (0)

/* sysctl(3) tags, for compatibility purposes */
#define	VLANCTL_PROTO	1
#define	VLANCTL_MAX	2

/*
 * Configuration structure for SIOCSETVLAN and SIOCGETVLAN ioctls.
 */
struct	vlanreq {
	char	vlr_parent[IFNAMSIZ];
	u_short	vlr_tag;
};
#define	SIOCSETVLAN	SIOCSIFGENERIC
#define	SIOCGETVLAN	SIOCGIFGENERIC

#define	SIOCGVLANPCP	SIOCGLANPCP	/* Get VLAN PCP */
#define	SIOCSVLANPCP	SIOCSLANPCP	/* Set VLAN PCP */

#ifdef _KERNEL
/*
 * Drivers that are capable of adding and removing the VLAN header
 * in hardware indicate they support this by marking IFCAP_VLAN_HWTAGGING
 * in if_capabilities.  Drivers for hardware that is capable
 * of handling larger MTU's that may include a software-appended
 * VLAN header w/o lowering the normal MTU should mark IFCAP_VLAN_MTU
 * in if_capabilities; this notifies the VLAN code it can leave the
 * MTU on the vlan interface at the normal setting.
 */

/*
 * VLAN tags are stored in host byte order.  Byte swapping may be
 * necessary.
 *
 * Drivers that support hardware VLAN tag stripping fill in the
 * received VLAN tag (containing both vlan and priority information)
 * into the ether_vtag mbuf packet header field:
 * 
 *	m->m_pkthdr.ether_vtag = vtag;		// ntohs()?
 *	m->m_flags |= M_VLANTAG;
 *
 * to mark the packet m with the specified VLAN tag.
 *
 * On output the driver should check the mbuf for the M_VLANTAG
 * flag to see if a VLAN tag is present and valid:
 *
 *	if (m->m_flags & M_VLANTAG) {
 *		... = m->m_pkthdr.ether_vtag;	// htons()?
 *		... pass tag to hardware ...
 *	}
 *
 * Note that a driver must indicate it supports hardware VLAN
 * stripping/insertion by marking IFCAP_VLAN_HWTAGGING in
 * if_capabilities.
 */

/*
 * The 802.1q code may also tag mbufs with the PCP (priority) field for use in
 * other layers of the stack, in which case an m_tag will be used.  This is
 * semantically quite different from use of the ether_vtag field, which is
 * defined only between the device driver and VLAN layer.
 */
#define	MTAG_8021Q		1326104895
#define	MTAG_8021Q_PCP_IN	0		/* Input priority. */
#define	MTAG_8021Q_PCP_OUT	1		/* Output priority. */

#define	VLAN_CAPABILITIES(_ifp) do {				\
	if ((_ifp)->if_vlantrunk != NULL) 			\
		(*vlan_trunk_cap_p)(_ifp);			\
} while (0)

#define	VLAN_TRUNKDEV(_ifp)					\
	((_ifp)->if_type == IFT_L2VLAN ? (*vlan_trunkdev_p)((_ifp)) : NULL)
#define	VLAN_TAG(_ifp, _vid)					\
	((_ifp)->if_type == IFT_L2VLAN ? (*vlan_tag_p)((_ifp), (_vid)) : EINVAL)
#define	VLAN_PCP(_ifp, _pcp)					\
	((_ifp)->if_type == IFT_L2VLAN ? (*vlan_pcp_p)((_ifp), (_pcp)) : EINVAL)
#define	VLAN_COOKIE(_ifp)					\
	((_ifp)->if_type == IFT_L2VLAN ? (*vlan_cookie_p)((_ifp)) : NULL)
#define	VLAN_SETCOOKIE(_ifp, _cookie)				\
	((_ifp)->if_type == IFT_L2VLAN ?			\
	    (*vlan_setcookie_p)((_ifp), (_cookie)) : EINVAL)
#define	VLAN_DEVAT(_ifp, _vid)					\
	((_ifp)->if_vlantrunk != NULL ? (*vlan_devat_p)((_ifp), (_vid)) : NULL)

extern	void (*vlan_trunk_cap_p)(struct ifnet *);
extern	struct ifnet *(*vlan_trunkdev_p)(struct ifnet *);
extern	struct ifnet *(*vlan_devat_p)(struct ifnet *, uint16_t);
extern	int (*vlan_tag_p)(struct ifnet *, uint16_t *);
extern	int (*vlan_pcp_p)(struct ifnet *, uint16_t *);
extern	int (*vlan_setcookie_p)(struct ifnet *, void *);
extern	void *(*vlan_cookie_p)(struct ifnet *);

#ifdef _SYS_EVENTHANDLER_H_
/* VLAN state change events */
typedef void (*vlan_config_fn)(void *, struct ifnet *, uint16_t);
typedef void (*vlan_unconfig_fn)(void *, struct ifnet *, uint16_t);
EVENTHANDLER_DECLARE(vlan_config, vlan_config_fn);
EVENTHANDLER_DECLARE(vlan_unconfig, vlan_unconfig_fn);
#endif /* _SYS_EVENTHANDLER_H_ */

#endif /* _KERNEL */

#endif /* _NET_IF_VLAN_VAR_H_ */
