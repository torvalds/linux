/*	$NetBSD: vif.c,v 1.6 1995/12/10 10:07:19 mycroft Exp $	*/

/*
 * The mrouted program is covered by the license in the accompanying file
 * named "LICENSE".  Use of the mrouted program represents acceptance of
 * the terms and conditions listed in that file.
 *
 * The mrouted program is COPYRIGHT 1989 by The Board of Trustees of
 * Leland Stanford Junior University.
 */


#include "defs.h"
#include <fcntl.h>

/*
 * Exported variables.
 */
struct uvif	uvifs[MAXVIFS];	/* array of virtual interfaces		    */
vifi_t		numvifs;	/* number of vifs in use		    */
int		vifs_down;	/* 1=>some interfaces are down		    */
int		phys_vif;	/* An enabled vif			    */
int		udp_socket;	/* Since the honkin' kernel doesn't support */
				/* ioctls on raw IP sockets, we need a UDP  */
				/* socket as well as our IGMP (raw) socket. */
				/* How dumb.                                */
int		vifs_with_neighbors;	/* == 1 if I am a leaf		    */

typedef struct {
        vifi_t  vifi;
        struct listaddr *g;
	int    q_time;
} cbk_t;

/*
 * Forward declarations.
 */
static void start_vif(vifi_t vifi);
static void start_vif2(vifi_t vifi);
static void stop_vif(vifi_t vifi);
static void age_old_hosts(void);
static void send_probe_on_vif(struct uvif *v);
static int info_version(char *p, int);
static void DelVif(void *arg);
static int SetTimer(int vifi, struct listaddr *g);
static int DeleteTimer(int id);
static void SendQuery(void *arg);
static int SetQueryTimer(struct listaddr *g, vifi_t vifi, int to_expire,
    int q_time);


/*
 * Initialize the virtual interfaces, but do not install
 * them in the kernel.  Start routing on all vifs that are
 * not down or disabled.
 */
void
init_vifs(void)
{
    vifi_t vifi;
    struct uvif *v;
    int enabled_vifs, enabled_phyints;
    extern char *configfilename;

    numvifs = 0;
    vifs_with_neighbors = 0;
    vifs_down = FALSE;

    /*
     * Configure the vifs based on the interface configuration of the
     * the kernel and the contents of the configuration file.
     * (Open a UDP socket for ioctl use in the config procedures.)
     */
    if ((udp_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
	logit(LOG_ERR, errno, "UDP socket");
    logit(LOG_INFO,0,"Getting vifs from kernel interfaces");
    config_vifs_from_kernel();
    logit(LOG_INFO,0,"Getting vifs from %s",configfilename);
    config_vifs_from_file();

    /*
     * Quit if there are fewer than two enabled vifs.
     */
    enabled_vifs    = 0;
    enabled_phyints = 0;
    phys_vif	    = -1;
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	if (!(v->uv_flags & VIFF_DISABLED)) {
	    ++enabled_vifs;
	    if (!(v->uv_flags & VIFF_TUNNEL)) {
		if (phys_vif == -1)
		    phys_vif = vifi;
		++enabled_phyints;
	    }
	}
    }
    if (enabled_vifs < 2)
	logit(LOG_ERR, 0, "can't forward: %s",
	    enabled_vifs == 0 ? "no enabled vifs" : "only one enabled vif");

    if (enabled_phyints == 0)
	logit(LOG_WARNING, 0,
	    "no enabled interfaces, forwarding via tunnels only");

    logit(LOG_INFO, 0, "Installing vifs in mrouted...");
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	if (!(v->uv_flags & VIFF_DISABLED)) {
	    if (!(v->uv_flags & VIFF_DOWN)) {
		if (v->uv_flags & VIFF_TUNNEL)
		    logit(LOG_INFO, 0, "vif #%d, tunnel %s -> %s", vifi,
				inet_fmt(v->uv_lcl_addr, s1),
				inet_fmt(v->uv_rmt_addr, s2));
		else
		    logit(LOG_INFO, 0, "vif #%d, phyint %s", vifi,
				inet_fmt(v->uv_lcl_addr, s1));
		start_vif2(vifi);
	    } else logit(LOG_INFO, 0,
		     "%s is not yet up; vif #%u not in service",
		     v->uv_name, vifi);
	}
    }
}

/*
 * Start routing on all virtual interfaces that are not down or
 * administratively disabled.
 */
void
init_installvifs(void)
{
    vifi_t vifi;
    struct uvif *v;

    logit(LOG_INFO, 0, "Installing vifs in kernel...");
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	if (!(v->uv_flags & VIFF_DISABLED)) {
	    if (!(v->uv_flags & VIFF_DOWN)) {
		if (v->uv_flags & VIFF_TUNNEL)
		    logit(LOG_INFO, 0, "vif #%d, tunnel %s -> %s", vifi,
				inet_fmt(v->uv_lcl_addr, s1),
				inet_fmt(v->uv_rmt_addr, s2));
		else
		    logit(LOG_INFO, 0, "vif #%d, phyint %s", vifi,
				inet_fmt(v->uv_lcl_addr, s1));
		k_add_vif(vifi, &uvifs[vifi]);
	    } else logit(LOG_INFO, 0,
		     "%s is not yet up; vif #%u not in service",
		     v->uv_name, vifi);
	}
    }
}

/*
 * See if any interfaces have changed from up state to down, or vice versa,
 * including any non-multicast-capable interfaces that are in use as local
 * tunnel end-points.  Ignore interfaces that have been administratively
 * disabled.
 */
void
check_vif_state(void)
{
    vifi_t vifi;
    struct uvif *v;
    struct ifreq ifr;

    vifs_down = FALSE;
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {

	if (v->uv_flags & VIFF_DISABLED) continue;

	strncpy(ifr.ifr_name, v->uv_name, IFNAMSIZ);
	if (ioctl(udp_socket, SIOCGIFFLAGS, (char *)&ifr) == -1)
	    logit(LOG_ERR, errno,
		"ioctl SIOCGIFFLAGS for %s", ifr.ifr_name);

	if (v->uv_flags & VIFF_DOWN) {
	    if (ifr.ifr_flags & IFF_UP) {
		v->uv_flags &= ~VIFF_DOWN;
		start_vif(vifi);
		logit(LOG_INFO, 0,
		    "%s has come up; vif #%u now in service",
		    v->uv_name, vifi);
	    }
	    else vifs_down = TRUE;
	}
	else {
	    if (!(ifr.ifr_flags & IFF_UP)) {
		stop_vif(vifi);
		v->uv_flags |= VIFF_DOWN;
		logit(LOG_INFO, 0,
		    "%s has gone down; vif #%u taken out of service",
		    v->uv_name, vifi);
		vifs_down = TRUE;
	    }
	}
    }
}

/*
 * Send a probe message on vif v
 */
static void
send_probe_on_vif(struct uvif *v)
{
    char *p;
    int datalen = 0;
    struct listaddr *nbr;
    int i;

    p = send_buf + MIN_IP_HEADER_LEN + IGMP_MINLEN;

    for (i = 0; i < 4; i++)
	*p++ = ((char *)&(dvmrp_genid))[i];
    datalen += 4;

    /*
     * add the neighbor list on the interface to the message
     */
    nbr = v->uv_neighbors;

    while (nbr) {
	for (i = 0; i < 4; i++)
	    *p++ = ((char *)&nbr->al_addr)[i];
	datalen +=4;
	nbr = nbr->al_next;
    }

    send_igmp(v->uv_lcl_addr,
	      (v->uv_flags & VIFF_TUNNEL) ? v->uv_rmt_addr
	      : dvmrp_group,
	      IGMP_DVMRP, DVMRP_PROBE,
	      htonl(MROUTED_LEVEL |
	      ((v->uv_flags & VIFF_LEAF) ? 0 : LEAF_FLAGS)),
	      datalen);
}

/*
 * Add a vifi to the kernel and start routing on it.
 */
static void
start_vif(vifi_t vifi)
{
    /*
     * Install the interface in the kernel's vif structure.
     */
    k_add_vif(vifi, &uvifs[vifi]);

    start_vif2(vifi);
}

/*
 * Add a vifi to all the user-level data structures but don't add
 * it to the kernel yet.
 */
static void
start_vif2(vifi_t vifi)
{
    struct uvif *v;
    u_int32_t src;
    struct phaddr *p;

    v   = &uvifs[vifi];
    src = v->uv_lcl_addr;

    /*
     * Update the existing route entries to take into account the new vif.
     */
    add_vif_to_routes(vifi);

    if (!(v->uv_flags & VIFF_TUNNEL)) {
	/*
	 * Join the DVMRP multicast group on the interface.
	 * (This is not strictly necessary, since the kernel promiscuously
	 * receives IGMP packets addressed to ANY IP multicast group while
	 * multicast routing is enabled.  However, joining the group allows
	 * this host to receive non-IGMP packets as well, such as 'pings'.)
	 */
	k_join(dvmrp_group, src);

	/*
	 * Join the ALL-ROUTERS multicast group on the interface.
	 * This allows mtrace requests to loop back if they are run
	 * on the multicast router.
	 */
	k_join(allrtrs_group, src);

	/*
	 * Install an entry in the routing table for the subnet to which
	 * the interface is connected.
	 */
	start_route_updates();
	update_route(v->uv_subnet, v->uv_subnetmask, 0, 0, vifi);
	for (p = v->uv_addrs; p; p = p->pa_next) {
	    start_route_updates();
	    update_route(p->pa_subnet, p->pa_subnetmask, 0, 0, vifi);
	}

	/*
	 * Until neighbors are discovered, assume responsibility for sending
	 * periodic group membership queries to the subnet.  Send the first
	 * query.
	 */
	v->uv_flags |= VIFF_QUERIER;
	send_igmp(src, allhosts_group, IGMP_HOST_MEMBERSHIP_QUERY,
	      (v->uv_flags & VIFF_IGMPV1) ? 0 :
	      IGMP_MAX_HOST_REPORT_DELAY * IGMP_TIMER_SCALE, 0, 0);
	age_old_hosts();
    }

    v->uv_leaf_timer = LEAF_CONFIRMATION_TIME;

    /*
     * Send a probe via the new vif to look for neighbors.
     */
    send_probe_on_vif(v);
}

/*
 * Stop routing on the specified virtual interface.
 */
static void
stop_vif(vifi_t vifi)
{
    struct uvif *v;
    struct listaddr *a;
    struct phaddr *p;

    v = &uvifs[vifi];

    if (!(v->uv_flags & VIFF_TUNNEL)) {
	/*
	 * Depart from the DVMRP multicast group on the interface.
	 */
	k_leave(dvmrp_group, v->uv_lcl_addr);

	/*
	 * Depart from the ALL-ROUTERS multicast group on the interface.
	 */
	k_leave(allrtrs_group, v->uv_lcl_addr);

	/*
	 * Update the entry in the routing table for the subnet to which
	 * the interface is connected, to take into account the interface
	 * failure.
	 */
	start_route_updates();
	update_route(v->uv_subnet, v->uv_subnetmask, UNREACHABLE, 0, vifi);
	for (p = v->uv_addrs; p; p = p->pa_next) {
	    start_route_updates();
	    update_route(p->pa_subnet, p->pa_subnetmask, UNREACHABLE, 0, vifi);
	}

	/*
	 * Discard all group addresses.  (No need to tell kernel;
	 * the k_del_vif() call, below, will clean up kernel state.)
	 */
	while (v->uv_groups != NULL) {
	    a = v->uv_groups;
	    v->uv_groups = a->al_next;
	    free((char *)a);
	}

	v->uv_flags &= ~VIFF_QUERIER;
    }

    /*
     * Update the existing route entries to take into account the vif failure.
     */
    delete_vif_from_routes(vifi);

    /*
     * Delete the interface from the kernel's vif structure.
     */
    k_del_vif(vifi);

    /*
     * Discard all neighbor addresses.
     */
    if (v->uv_neighbors)
	vifs_with_neighbors--;

    while (v->uv_neighbors != NULL) {
	a = v->uv_neighbors;
	v->uv_neighbors = a->al_next;
	free((char *)a);
    }
}


/*
 * stop routing on all vifs
 */
void
stop_all_vifs(void)
{
    vifi_t vifi;
    struct uvif *v;
    struct listaddr *a;
    struct vif_acl *acl;

    for (vifi = 0; vifi < numvifs; vifi++) {
	v = &uvifs[vifi];
	while (v->uv_groups != NULL) {
	    a = v->uv_groups;
	    v->uv_groups = a->al_next;
	    free((char *)a);
	}
	while (v->uv_neighbors != NULL) {
	    a = v->uv_neighbors;
	    v->uv_neighbors = a->al_next;
	    free((char *)a);
	}
	while (v->uv_acl != NULL) {
	    acl = v->uv_acl;
	    v->uv_acl = acl->acl_next;
	    free((char *)acl);
	}
    }
}


/*
 * Find the virtual interface from which an incoming packet arrived,
 * based on the packet's source and destination IP addresses.
 */
vifi_t
find_vif(u_int32_t src, u_int32_t dst)
{
    vifi_t vifi;
    struct uvif *v;
    struct phaddr *p;

    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	if (!(v->uv_flags & (VIFF_DOWN|VIFF_DISABLED))) {
	    if (v->uv_flags & VIFF_TUNNEL) {
		if (src == v->uv_rmt_addr && dst == v->uv_lcl_addr)
		    return(vifi);
	    }
	    else {
		if ((src & v->uv_subnetmask) == v->uv_subnet &&
		    ((v->uv_subnetmask == 0xffffffff) ||
		     (src != v->uv_subnetbcast)))
		    return(vifi);
		for (p=v->uv_addrs; p; p=p->pa_next) {
		    if ((src & p->pa_subnetmask) == p->pa_subnet &&
			((p->pa_subnetmask == 0xffffffff) ||
			 (src != p->pa_subnetbcast)))
			return(vifi);
		}
	    }
	}
    }
    return (NO_VIF);
}

static void
age_old_hosts(void)
{
    vifi_t vifi;
    struct uvif *v;
    struct listaddr *g;

    /*
     * Decrement the old-hosts-present timer for each
     * active group on each vif.
     */
    for (vifi = 0, v = uvifs; vifi < numvifs; vifi++, v++)
        for (g = v->uv_groups; g != NULL; g = g->al_next)
	    if (g->al_old)
		g->al_old--;
}


/*
 * Send group membership queries to all subnets for which I am querier.
 */
void
query_groups(void)
{
    vifi_t vifi;
    struct uvif *v;

    for (vifi = 0, v = uvifs; vifi < numvifs; vifi++, v++) {
	if (v->uv_flags & VIFF_QUERIER) {
	    send_igmp(v->uv_lcl_addr, allhosts_group,
		      IGMP_HOST_MEMBERSHIP_QUERY,
		      (v->uv_flags & VIFF_IGMPV1) ? 0 :
		      IGMP_MAX_HOST_REPORT_DELAY * IGMP_TIMER_SCALE, 0, 0);
	}
    }
    age_old_hosts();
}

/*
 * Process an incoming host membership query
 */
void
accept_membership_query(u_int32_t src, u_int32_t dst, u_int32_t group,
    int tmo)
{
    vifi_t vifi;
    struct uvif *v;

    if ((vifi = find_vif(src, dst)) == NO_VIF ||
	(uvifs[vifi].uv_flags & VIFF_TUNNEL)) {
	logit(LOG_INFO, 0,
	    "ignoring group membership query from non-adjacent host %s",
	    inet_fmt(src, s1));
	return;
    }

    v = &uvifs[vifi];

    /*
     * If we consider ourselves the querier for this vif, but hear a
     * query from a router with a lower IP address, yield to them.
     *
     * This is done here as well as in the neighbor discovery in case
     * there is a querier that doesn't speak DVMRP.
     *
     * XXX If this neighbor doesn't speak DVMRP, then we need to create
     * some neighbor state for him so that we can time him out!
     */
    if ((v->uv_flags & VIFF_QUERIER) &&
	(ntohl(src) < ntohl(v->uv_lcl_addr))) {
	    v->uv_flags &= ~VIFF_QUERIER;

    }
}

/*
 * Process an incoming group membership report.
 */
void
accept_group_report(u_int32_t src, u_int32_t dst, u_int32_t group,
    int r_type)
{
    vifi_t vifi;
    struct uvif *v;
    struct listaddr *g;

    if ((vifi = find_vif(src, dst)) == NO_VIF ||
	(uvifs[vifi].uv_flags & VIFF_TUNNEL)) {
	logit(LOG_INFO, 0,
	    "ignoring group membership report from non-adjacent host %s",
	    inet_fmt(src, s1));
	return;
    }

    v = &uvifs[vifi];

    /*
     * Look for the group in our group list; if found, reset its timer.
     */
    for (g = v->uv_groups; g != NULL; g = g->al_next) {
	if (group == g->al_addr) {
	    if (r_type == IGMP_v1_HOST_MEMBERSHIP_REPORT)
		g->al_old = OLD_AGE_THRESHOLD;

	    /** delete old timers, set a timer for expiration **/
	    g->al_timer = GROUP_EXPIRE_TIME;
	    if (g->al_query)
		g->al_query = DeleteTimer(g->al_query);
	    if (g->al_timerid)
		g->al_timerid = DeleteTimer(g->al_timerid);
	    g->al_timerid = SetTimer(vifi, g);
	    break;
	}
    }

    /*
     * If not found, add it to the list and update kernel cache.
     */
    if (g == NULL) {
	g = malloc(sizeof(struct listaddr));
	if (g == NULL)
	    logit(LOG_ERR, 0, "ran out of memory");    /* fatal */

	g->al_addr   = group;
	if (r_type == IGMP_v2_HOST_MEMBERSHIP_REPORT)
	    g->al_old = 0;
	else
	    g->al_old = OLD_AGE_THRESHOLD;

	/** set a timer for expiration **/
        g->al_query = 0;
	g->al_timer  = GROUP_EXPIRE_TIME;
	time(&g->al_ctime);
	g->al_timerid = SetTimer(vifi, g);
	g->al_next   = v->uv_groups;
	v->uv_groups = g;

	update_lclgrp(vifi, group);
    }

    /*
     * Check if a graft is necessary for this group
     */
    chkgrp_graft(vifi, group);
}


void
accept_leave_message(u_int32_t src, u_int32_t dst, u_int32_t group)
{
    vifi_t vifi;
    struct uvif *v;
    struct listaddr *g;

    if ((vifi = find_vif(src, dst)) == NO_VIF ||
	(uvifs[vifi].uv_flags & VIFF_TUNNEL)) {
	logit(LOG_INFO, 0,
	    "ignoring group leave report from non-adjacent host %s",
	    inet_fmt(src, s1));
	return;
    }

    v = &uvifs[vifi];

    if (!(v->uv_flags & VIFF_QUERIER) || (v->uv_flags & VIFF_IGMPV1))
	return;

    /*
     * Look for the group in our group list in order to set up a short-timeout
     * query.
     */
    for (g = v->uv_groups; g != NULL; g = g->al_next) {
	if (group == g->al_addr) {
	    logit(LOG_DEBUG, 0,
		"[vif.c, _accept_leave_message] %d %d \n",
		g->al_old, g->al_query);

	    /* Ignore the leave message if there are old hosts present */
	    if (g->al_old)
		return;

	    /* still waiting for a reply to a query, ignore the leave */
	    if (g->al_query)
		return;

	    /** delete old timer set a timer for expiration **/
	    if (g->al_timerid)
		g->al_timerid = DeleteTimer(g->al_timerid);

	    /** send a group specific querry **/
	    g->al_timer = LEAVE_EXPIRE_TIME;
	    send_igmp(v->uv_lcl_addr, g->al_addr,
		      IGMP_HOST_MEMBERSHIP_QUERY,
		      LEAVE_EXPIRE_TIME / 3 * IGMP_TIMER_SCALE,
		      g->al_addr, 0);
	    g->al_query = SetQueryTimer(g, vifi, g->al_timer / 3,
				LEAVE_EXPIRE_TIME / 3 * IGMP_TIMER_SCALE);
	    g->al_timerid = SetTimer(vifi, g);
	    break;
	}
    }
}


/*
 * Send a periodic probe on all vifs.
 * Useful to determine one-way interfaces.
 * Detect neighbor loss faster.
 */
void
probe_for_neighbors(void)
{
    vifi_t vifi;
    struct uvif *v;

    for (vifi = 0, v = uvifs; vifi < numvifs; vifi++, v++) {
	if (!(v->uv_flags & (VIFF_DOWN|VIFF_DISABLED))) {
	    send_probe_on_vif(v);
	}
    }
}


/*
 * Send a list of all of our neighbors to the requestor, `src'.
 */
void
accept_neighbor_request(u_int32_t src, u_int32_t dst)
{
    vifi_t vifi;
    struct uvif *v;
    u_char *p, *ncount;
    struct listaddr *la;
    int	datalen;
    u_int32_t temp_addr, us, them = src;

    /* Determine which of our addresses to use as the source of our response
     * to this query.
     */
    if (IN_MULTICAST(ntohl(dst))) { /* query sent to a multicast group */
	int udp;		/* find best interface to reply on */
	struct sockaddr_in addr;
	int addrlen = sizeof(addr);

	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_len = sizeof addr;
	addr.sin_addr.s_addr = dst;
	addr.sin_port = htons(2000); /* any port over 1024 will do... */
	if ((udp = socket(AF_INET, SOCK_DGRAM, 0)) == -1
	    || connect(udp, (struct sockaddr *) &addr, sizeof(addr)) == -1
	    || getsockname(udp, (struct sockaddr *) &addr, &addrlen) == -1) {
	    logit(LOG_WARNING, errno, "Determining local address");
	    close(udp);
	    return;
	}
	close(udp);
	us = addr.sin_addr.s_addr;
    } else			/* query sent to us alone */
	us = dst;

#define PUT_ADDR(a)	temp_addr = ntohl(a); \
			*p++ = temp_addr >> 24; \
			*p++ = (temp_addr >> 16) & 0xFF; \
			*p++ = (temp_addr >> 8) & 0xFF; \
			*p++ = temp_addr & 0xFF;

    p = (u_char *) (send_buf + MIN_IP_HEADER_LEN + IGMP_MINLEN);
    datalen = 0;

    for (vifi = 0, v = uvifs; vifi < numvifs; vifi++, v++) {
	if (v->uv_flags & VIFF_DISABLED)
	    continue;

	ncount = 0;

	for (la = v->uv_neighbors; la; la = la->al_next) {

	    /* Make sure that there's room for this neighbor... */
	    if (datalen + (ncount == 0 ? 4 + 3 + 4 : 4) > MAX_DVMRP_DATA_LEN) {
		send_igmp(us, them, IGMP_DVMRP, DVMRP_NEIGHBORS,
			  htonl(MROUTED_LEVEL), datalen);
		p = (u_char *) (send_buf + MIN_IP_HEADER_LEN + IGMP_MINLEN);
		datalen = 0;
		ncount = 0;
	    }

	    /* Put out the header for this neighbor list... */
	    if (ncount == 0) {
		PUT_ADDR(v->uv_lcl_addr);
		*p++ = v->uv_metric;
		*p++ = v->uv_threshold;
		ncount = p;
		*p++ = 0;
		datalen += 4 + 3;
	    }

	    PUT_ADDR(la->al_addr);
	    datalen += 4;
	    (*ncount)++;
	}
    }

    if (datalen != 0)
	send_igmp(us, them, IGMP_DVMRP, DVMRP_NEIGHBORS, htonl(MROUTED_LEVEL),
		  datalen);
}

/*
 * Send a list of all of our neighbors to the requestor, `src'.
 */
void
accept_neighbor_request2(u_int32_t src, u_int32_t dst)
{
    vifi_t vifi;
    struct uvif *v;
    u_char *p, *ncount;
    struct listaddr *la;
    int	datalen;
    u_int32_t us, them = src;

    /* Determine which of our addresses to use as the source of our response
     * to this query.
     */
    if (IN_MULTICAST(ntohl(dst))) { /* query sent to a multicast group */
	int udp;		/* find best interface to reply on */
	struct sockaddr_in addr;
	int addrlen = sizeof(addr);

	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_len = sizeof addr;
	addr.sin_addr.s_addr = dst;
	addr.sin_port = htons(2000); /* any port over 1024 will do... */
	if ((udp = socket(AF_INET, SOCK_DGRAM, 0)) == -1
	    || connect(udp, (struct sockaddr *) &addr, sizeof(addr)) == -1
	    || getsockname(udp, (struct sockaddr *) &addr, &addrlen) == -1) {
	    logit(LOG_WARNING, errno, "Determining local address");
	    close(udp);
	    return;
	}
	close(udp);
	us = addr.sin_addr.s_addr;
    } else			/* query sent to us alone */
	us = dst;

    p = (u_char *) (send_buf + MIN_IP_HEADER_LEN + IGMP_MINLEN);
    datalen = 0;

    for (vifi = 0, v = uvifs; vifi < numvifs; vifi++, v++) {
	u_short vflags = v->uv_flags;
	u_char rflags = 0;
	if (vflags & VIFF_TUNNEL)
	    rflags |= DVMRP_NF_TUNNEL;
	if (vflags & VIFF_SRCRT)
	    rflags |= DVMRP_NF_SRCRT;
	if (vflags & VIFF_DOWN)
	    rflags |= DVMRP_NF_DOWN;
	if (vflags & VIFF_DISABLED)
	    rflags |= DVMRP_NF_DISABLED;
	if (vflags & VIFF_QUERIER)
	    rflags |= DVMRP_NF_QUERIER;
	if (vflags & VIFF_LEAF)
	    rflags |= DVMRP_NF_LEAF;
	ncount = 0;
	la = v->uv_neighbors;
	if (la == NULL) {
	    /*
	     * include down & disabled interfaces and interfaces on
	     * leaf nets.
	     */
	    if (rflags & DVMRP_NF_TUNNEL)
		rflags |= DVMRP_NF_DOWN;
	    if (datalen > MAX_DVMRP_DATA_LEN - 12) {
		send_igmp(us, them, IGMP_DVMRP, DVMRP_NEIGHBORS2,
			  htonl(MROUTED_LEVEL), datalen);
		p = (u_char *) (send_buf + MIN_IP_HEADER_LEN + IGMP_MINLEN);
		datalen = 0;
	    }
	    *(u_int*)p = v->uv_lcl_addr;
	    p += 4;
	    *p++ = v->uv_metric;
	    *p++ = v->uv_threshold;
	    *p++ = rflags;
	    *p++ = 1;
	    *(u_int*)p =  v->uv_rmt_addr;
	    p += 4;
	    datalen += 12;
	} else {
	    for ( ; la; la = la->al_next) {
		/* Make sure that there's room for this neighbor... */
		if (datalen + (ncount == 0 ? 4+4+4 : 4) > MAX_DVMRP_DATA_LEN) {
		    send_igmp(us, them, IGMP_DVMRP, DVMRP_NEIGHBORS2,
			      htonl(MROUTED_LEVEL), datalen);
		    p = (u_char *) (send_buf + MIN_IP_HEADER_LEN + IGMP_MINLEN);
		    datalen = 0;
		    ncount = 0;
		}
		/* Put out the header for this neighbor list... */
		if (ncount == 0) {
		    *(u_int*)p = v->uv_lcl_addr;
		    p += 4;
		    *p++ = v->uv_metric;
		    *p++ = v->uv_threshold;
		    *p++ = rflags;
		    ncount = p;
		    *p++ = 0;
		    datalen += 4 + 4;
		}
		*(u_int*)p = la->al_addr;
		p += 4;
		datalen += 4;
		(*ncount)++;
	    }
	}
    }
    if (datalen != 0)
	send_igmp(us, them, IGMP_DVMRP, DVMRP_NEIGHBORS2, htonl(MROUTED_LEVEL),
		  datalen);
}

void
accept_info_request(u_int32_t src, u_int32_t dst, u_char *p, int datalen)
{
    u_char *q;
    int len;
    int outlen = 0;

    q = (u_char *) (send_buf + MIN_IP_HEADER_LEN + IGMP_MINLEN);

    /* To be general, this must deal properly with breaking up over-sized
     * packets.  That implies passing a length to each function, and
     * allowing each function to request to be called again.  Right now,
     * we're only implementing the one thing we are positive will fit into
     * a single packet, so we wimp out.
     */
    while (datalen > 0) {
	len = 0;
	switch (*p) {
	    case DVMRP_INFO_VERSION:
		len = info_version(q, (u_char *)send_buf + RECV_BUF_SIZE - q);
		break;

	    case DVMRP_INFO_NEIGHBORS:
	    default:
		logit(LOG_INFO, 0, "ignoring unknown info type %d", *p);
		break;
	}
	*(q+1) = len++;
	outlen += len * 4;
	q += len * 4;
	len = (*(p+1) + 1) * 4;
	p += len;
	datalen -= len;
    }

    if (outlen != 0)
	send_igmp(INADDR_ANY, src, IGMP_DVMRP, DVMRP_INFO_REPLY,
			htonl(MROUTED_LEVEL), outlen);
}

/*
 * Information response -- return version string
 */
static int
info_version(char *p, int len)
{
    extern char versionstring[];

    if (len < 5)
	return (0);
    *p++ = DVMRP_INFO_VERSION;
    p++;	/* skip over length */
    *p++ = 0;	/* zero out */
    *p++ = 0;	/* reserved fields */
    strlcpy(p, versionstring, len - 4);

    len = strlen(p);
    return ((len + 3) / 4);
}

/*
 * Process an incoming neighbor-list message.
 */
void
accept_neighbors(u_int32_t src, u_int32_t dst, u_char *p, int datalen,
    u_int32_t level)
{
    logit(LOG_INFO, 0, "ignoring spurious DVMRP neighbor list from %s to %s",
	inet_fmt(src, s1), inet_fmt(dst, s2));
}


/*
 * Process an incoming neighbor-list message.
 */
void
accept_neighbors2(u_int32_t src, u_int32_t dst, u_char *p, int datalen,
    u_int32_t level)
{
    logit(LOG_INFO, 0, "ignoring spurious DVMRP neighbor list2 from %s to %s",
	inet_fmt(src, s1), inet_fmt(dst, s2));
}

/*
 * Process an incoming info reply message.
 */
void
accept_info_reply(u_int32_t src, u_int32_t dst, u_char *p, int datalen)
{
    logit(LOG_INFO, 0, "ignoring spurious DVMRP info reply from %s to %s",
	inet_fmt(src, s1), inet_fmt(dst, s2));
}


/*
 * Update the neighbor entry for neighbor 'addr' on vif 'vifi'.
 * 'msgtype' is the type of DVMRP message received from the neighbor.
 * Return TRUE if 'addr' is a valid neighbor, FALSE otherwise.
 */
int
update_neighbor(vifi_t vifi, u_int32_t addr, int msgtype, char *p,
    int datalen, u_int32_t level)
{
    struct uvif *v;
    struct listaddr *n;
    u_int32_t genid = 0;
    u_int32_t router;
    u_int32_t send_tables = 0;
    int do_reset = FALSE;
    int nflags;

    v = &uvifs[vifi];
    nflags = (level >> 16) & 0xff;

    /*
     * Confirm that 'addr' is a valid neighbor address on vif 'vifi'.
     * IT IS ASSUMED that this was preceded by a call to find_vif(), which
     * checks that 'addr' is either a valid remote tunnel endpoint or a
     * non-broadcast address belonging to a directly-connected subnet.
     * Therefore, here we check only that 'addr' is not our own address
     * (due to an impostor or erroneous loopback) or an address of the form
     * {subnet,0} ("the unknown host").  These checks are not performed in
     * find_vif() because those types of address are acceptable for some
     * types of IGMP message (such as group membership reports).
     */
    if (!(v->uv_flags & VIFF_TUNNEL) &&
	(addr == v->uv_lcl_addr ||
	 addr == v->uv_subnet )) {
	logit(LOG_WARNING, 0,
	    "received DVMRP message from 'the unknown host' or self: %s",
	    inet_fmt(addr, s1));
	return (FALSE);
    }

    /*
     * Look for addr in list of neighbors.
     */
    for (n = v->uv_neighbors; n != NULL; n = n->al_next) {
	if (addr == n->al_addr) {
	    break;
	}
    }

    /*
     * Found it.  Reset its timer, and check for a version change
     */
    if (n) {
	n->al_timer = 0;

	/*
	 * update the neighbors version and protocol number
	 * if changed => router went down and came up,
	 * so take action immediately.
	 */
	if ((n->al_pv != (level & 0xff)) ||
	    (n->al_mv != ((level >> 8) & 0xff))) {

	    do_reset = TRUE;
	    logit(LOG_DEBUG, 0,
		"version change neighbor %s [old:%d.%d, new:%d.%d]",
		inet_fmt(addr, s1),
		n->al_pv, n->al_mv, level&0xff, (level >> 8) & 0xff);

	    n->al_pv = level & 0xff;
	    n->al_mv = (level >> 8) & 0xff;
	}
    } else {
	/*
	 * If not found, add it to the list.  If the neighbor has a lower
	 * IP address than me, yield querier duties to it.
	 */
	logit(LOG_DEBUG, 0, "New neighbor %s on vif %d v%d.%d nf 0x%02x",
	    inet_fmt(addr, s1), vifi, level & 0xff, (level >> 8) & 0xff,
	    (level >> 16) & 0xff);

	n = malloc(sizeof(struct listaddr));
	if (n == NULL)
	    logit(LOG_ERR, 0, "ran out of memory");    /* fatal */

	n->al_addr      = addr;
	n->al_pv	= level & 0xff;
	n->al_mv	= (level >> 8) & 0xff;
	n->al_genid	= 0;

	time(&n->al_ctime);
	n->al_timer     = 0;
	n->al_next      = v->uv_neighbors;

	/*
	 * If we thought that we had no neighbors on this vif, send a route
	 * report to the vif.  If this is just a new neighbor on the same
	 * vif, send the route report just to the new neighbor.
	 */
	if (v->uv_neighbors == NULL) {
	    send_tables = (v->uv_flags & VIFF_TUNNEL) ? addr : dvmrp_group;
	    vifs_with_neighbors++;
	} else {
	    send_tables = addr;
	}

	v->uv_neighbors = n;

	if (!(v->uv_flags & VIFF_TUNNEL) &&
	    ntohl(addr) < ntohl(v->uv_lcl_addr))
	    v->uv_flags &= ~VIFF_QUERIER;
    }

    /*
     * Check if the router gen-ids are the same.
     * Need to reset the prune state of the router if not.
     * Also check for one-way interfaces by seeing if we are in our
     * neighbor's list of known routers.
     */
    if (msgtype == DVMRP_PROBE) {

	/* Check genid neighbor flag.  Also check version number; 3.3 and
	 * 3.4 didn't set this flag. */
	if ((((level >> 16) & 0xff) & NF_GENID) ||
	    (((level & 0xff) == 3) && (((level >> 8) & 0xff) > 2))) {

	    int i;

	    if (datalen < 4) {
		logit(LOG_WARNING, 0,
		    "received truncated probe message from %s (len %d)",
		    inet_fmt(addr, s1), datalen);
		return (FALSE);
	    }

	    for (i = 0; i < 4; i++)
	      ((char *)&genid)[i] = *p++;
	    datalen -= 4;

	    if (n->al_genid == 0)
		n->al_genid = genid;
	    else if (n->al_genid != genid) {
		logit(LOG_DEBUG, 0,
		    "new genid neighbor %s on vif %d [old:%x, new:%x]",
		    inet_fmt(addr, s1), vifi, n->al_genid, genid);

		n->al_genid = genid;
		do_reset = TRUE;
	    }

	    /*
	     * loop through router list and check for one-way ifs.
	     */

	    v->uv_flags |= VIFF_ONEWAY;

	    while (datalen > 0) {
		if (datalen < 4) {
		    logit(LOG_WARNING, 0,
			"received truncated probe message from %s (len %d)",
			inet_fmt(addr, s1), datalen);
		    return (FALSE);
		}
		for (i = 0; i < 4; i++)
		  ((char *)&router)[i] = *p++;
		datalen -= 4;
		if (router == v->uv_lcl_addr) {
		    v->uv_flags &= ~VIFF_ONEWAY;
		    break;
		}
	    }
	}
    }
    if (n->al_flags != nflags) {
	n->al_flags = nflags;

	if (n->al_flags & NF_LEAF) {
	    /*XXX If we have non-leaf neighbors then we know we shouldn't
	     * mark this vif as a leaf.  For now we just count on other
	     * probes and/or reports resetting the timer. */
	    if (!v->uv_leaf_timer)
		v->uv_leaf_timer = LEAF_CONFIRMATION_TIME;
	} else {
	    /* If we get a leaf to non-leaf transition, we *must* update
	     * the routing table. */
	    if (v->uv_flags & VIFF_LEAF && send_tables == 0)
		send_tables = addr;
	    v->uv_flags &= ~VIFF_LEAF;
	    v->uv_leaf_timer = 0;
	}
    }
    if (do_reset) {
	reset_neighbor_state(vifi, addr);
	if (!send_tables)
	    send_tables = addr;
    }
    if (send_tables)
	report(ALL_ROUTES, vifi, send_tables);

    return (TRUE);
}


/*
 * On every timer interrupt, advance the timer in each neighbor and
 * group entry on every vif.
 */
void
age_vifs(void)
{
    vifi_t vifi;
    struct uvif *v;
    struct listaddr *a, *prev_a, *n;
    u_int32_t addr;

    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v ) {
	if (v->uv_leaf_timer && (v->uv_leaf_timer -= TIMER_INTERVAL == 0)) {
		v->uv_flags |= VIFF_LEAF;
	}

	for (prev_a = (struct listaddr *)&(v->uv_neighbors),
	     a = v->uv_neighbors;
	     a != NULL;
	     prev_a = a, a = a->al_next) {

	    if ((a->al_timer += TIMER_INTERVAL) < NEIGHBOR_EXPIRE_TIME)
		continue;

	    /*
	     * Neighbor has expired; delete it from the neighbor list,
	     * delete it from the 'dominants' and 'subordinates arrays of
	     * any route entries and assume querier duties unless there is
	     * another neighbor with a lower IP address than mine.
	     */
	    addr = a->al_addr;
	    prev_a->al_next = a->al_next;
	    free((char *)a);
	    a = prev_a;

	    delete_neighbor_from_routes(addr, vifi);

	    if (v->uv_neighbors == NULL)
		vifs_with_neighbors--;

	    v->uv_leaf_timer = LEAF_CONFIRMATION_TIME;

	    if (!(v->uv_flags & VIFF_TUNNEL)) {
		v->uv_flags |= VIFF_QUERIER;
		for (n = v->uv_neighbors; n != NULL; n = n->al_next) {
		    if (ntohl(n->al_addr) < ntohl(v->uv_lcl_addr)) {
			v->uv_flags &= ~VIFF_QUERIER;
		    }
		    if (!(n->al_flags & NF_LEAF)) {
			v->uv_leaf_timer = 0;
		    }
		}
	    }
	}
    }
}

/*
 * Returns the neighbor info struct for a given neighbor
 */
struct listaddr *
neighbor_info(vifi_t vifi, u_int32_t addr)
{
    struct listaddr *u;

    for (u = uvifs[vifi].uv_neighbors; u; u = u->al_next)
	if (u->al_addr == addr)
	    return u;

    return NULL;
}

/*
 * Print the contents of the uvifs array on file 'fp'.
 */
void
dump_vifs(FILE *fp)
{
    vifi_t vifi;
    struct uvif *v;
    struct listaddr *a;
    struct phaddr *p;
    struct sioc_vif_req v_req;

    fprintf(fp, "vifs_with_neighbors = %d\n", vifs_with_neighbors);

    if (vifs_with_neighbors == 1)
	fprintf(fp,"[This host is a leaf]\n\n");

    fprintf(fp,
    "\nVirtual Interface Table\n%s",
    "Vif  Name  Local-Address                               ");
    fprintf(fp,
    "M  Thr  Rate   Flags\n");

    for (vifi = 0, v = uvifs; vifi < numvifs; vifi++, v++) {

	fprintf(fp, "%2u %6s  %-15s %6s: %-18s %2u %3u  %5u  ",
		vifi,
		v->uv_name,
		inet_fmt(v->uv_lcl_addr, s1),
		(v->uv_flags & VIFF_TUNNEL) ?
			"tunnel":
			"subnet",
		(v->uv_flags & VIFF_TUNNEL) ?
			inet_fmt(v->uv_rmt_addr, s2) :
			inet_fmts(v->uv_subnet, v->uv_subnetmask, s3),
		v->uv_metric,
		v->uv_threshold,
		v->uv_rate_limit);

	if (v->uv_flags & VIFF_ONEWAY)   fprintf(fp, " one-way");
	if (v->uv_flags & VIFF_DOWN)     fprintf(fp, " down");
	if (v->uv_flags & VIFF_DISABLED) fprintf(fp, " disabled");
	if (v->uv_flags & VIFF_QUERIER)  fprintf(fp, " querier");
	if (v->uv_flags & VIFF_SRCRT)    fprintf(fp, " src-rt");
	if (v->uv_flags & VIFF_LEAF)	 fprintf(fp, " leaf");
	if (v->uv_flags & VIFF_IGMPV1)	 fprintf(fp, " IGMPv1");
	fprintf(fp, "\n");

	if (v->uv_addrs != NULL) {
	    fprintf(fp, "                alternate subnets: %s\n",
		    inet_fmts(v->uv_addrs->pa_subnet, v->uv_addrs->pa_subnetmask, s1));
	    for (p = v->uv_addrs->pa_next; p; p = p->pa_next) {
		fprintf(fp, "                                   %s\n",
			inet_fmts(p->pa_subnet, p->pa_subnetmask, s1));
	    }
	}

	if (v->uv_neighbors != NULL) {
	    fprintf(fp, "                            peers: %s (%d.%d) (0x%x)\n",
		    inet_fmt(v->uv_neighbors->al_addr, s1),
		    v->uv_neighbors->al_pv, v->uv_neighbors->al_mv,
		    v->uv_neighbors->al_flags);
	    for (a = v->uv_neighbors->al_next; a != NULL; a = a->al_next) {
		fprintf(fp, "                                   %s (%d.%d) (0x%x)\n",
			inet_fmt(a->al_addr, s1), a->al_pv, a->al_mv,
			a->al_flags);
	    }
	}

	if (v->uv_groups != NULL) {
	    fprintf(fp, "                           groups: %-15s\n",
		    inet_fmt(v->uv_groups->al_addr, s1));
	    for (a = v->uv_groups->al_next; a != NULL; a = a->al_next) {
		fprintf(fp, "                                   %-15s\n",
			inet_fmt(a->al_addr, s1));
	    }
	}
	if (v->uv_acl != NULL) {
	    struct vif_acl *acl;

	    fprintf(fp, "                       boundaries: %-18s\n",
		    inet_fmts(v->uv_acl->acl_addr, v->uv_acl->acl_mask, s1));
	    for (acl = v->uv_acl->acl_next; acl != NULL; acl = acl->acl_next) {
		fprintf(fp, "                                 : %-18s\n",
			inet_fmts(acl->acl_addr, acl->acl_mask, s1));
	    }
	}
	v_req.vifi = vifi;
	if (ioctl(udp_socket, SIOCGETVIFCNT, (char *)&v_req) == -1) {
	    logit(LOG_WARNING, 0,
		"SIOCGETVIFCNT fails");
	}
	else {
	    fprintf(fp, "                         pkts in : %ld\n",
		    v_req.icount);
	    fprintf(fp, "                         pkts out: %ld\n",
		    v_req.ocount);
	}
	fprintf(fp, "\n");
    }
    fprintf(fp, "\n");
}

/*
 * Time out record of a group membership on a vif
 */
static void
DelVif(void *arg)
{
    cbk_t *cbk = (cbk_t *)arg;
    vifi_t vifi = cbk->vifi;
    struct uvif *v = &uvifs[vifi];
    struct listaddr *a, **anp, *g = cbk->g;

    /*
     * Group has expired
     * delete all kernel cache entries with this group
     */
    if (g->al_query)
	DeleteTimer(g->al_query);

    delete_lclgrp(vifi, g->al_addr);

    anp = &(v->uv_groups);
    while ((a = *anp) != NULL) {
	if (a == g) {
	    *anp = a->al_next;
	    free((char *)a);
	} else {
	    anp = &a->al_next;
	}
    }

    free(cbk);
}

/*
 * Set a timer to delete the record of a group membership on a vif.
 */
static int
SetTimer(int vifi, struct listaddr *g)
{
    cbk_t *cbk;

    cbk = malloc(sizeof(cbk_t));
    cbk->g = g;
    cbk->vifi = vifi;
    return timer_setTimer(g->al_timer, (cfunc_t)DelVif, (void *)cbk);
}

/*
 * Delete a timer that was set above.
 */
static int
DeleteTimer(int id)
{
    timer_clearTimer(id);
    return 0;
}

/*
 * Send a group-specific query.
 */
static void
SendQuery(void *arg)
{
    cbk_t *cbk = (cbk_t *)arg;
    struct uvif *v = &uvifs[cbk->vifi];

    send_igmp(v->uv_lcl_addr, cbk->g->al_addr,
	      IGMP_HOST_MEMBERSHIP_QUERY,
	      cbk->q_time, cbk->g->al_addr, 0);
    cbk->g->al_query = 0;
    free(cbk);
}

/*
 * Set a timer to send a group-specific query.
 */
static int
SetQueryTimer(struct listaddr *g, vifi_t vifi, int to_expire, int q_time)
{
    cbk_t *cbk;

    cbk = malloc(sizeof(cbk_t));
    cbk->g = g;
    cbk->q_time = q_time;
    cbk->vifi = vifi;
    return timer_setTimer(to_expire, (cfunc_t)SendQuery, (void *)cbk);
}
