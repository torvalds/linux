/*	$NetBSD: route.h,v 1.4 1995/12/10 10:07:13 mycroft Exp $	*/

/*
 * The mrouted program is covered by the license in the accompanying file
 * named "LICENSE".  Use of the mrouted program represents acceptance of
 * the terms and conditions listed in that file.
 *
 * The mrouted program is COPYRIGHT 1989 by The Board of Trustees of
 * Leland Stanford Junior University.
 */

/*
 * Routing Table Entry, one per subnet from which a multicast could originate.
 * (Note: all addresses, subnet numbers and masks are kept in NETWORK order.)
 *
 * The Routing Table is stored as a doubly-linked list of these structures,
 * ordered by decreasing value of rt_originmask and, secondarily, by
 * decreasing value of rt_origin within each rt_originmask value.
 * This data structure is efficient for generating route reports, whether
 * full or partial, for processing received full reports, for clearing the
 * CHANGED flags, and for periodically advancing the timers in all routes.
 * It is not so efficient for updating a small number of routes in response
 * to a partial report.  In a stable topology, the latter are rare; if they
 * turn out to be costing a lot, we can add an auxiliary hash table for
 * faster access to arbitrary route entries.
 */
struct rtentry {
    struct rtentry  *rt_next;		/* link to next entry MUST BE FIRST */
    u_int32_t	     rt_origin;		/* subnet origin of multicasts      */
    u_int32_t	     rt_originmask;	/* subnet mask for origin           */
    short	     rt_originwidth;	/* # bytes of origin subnet number  */
    u_char	     rt_metric;		/* cost of route back to origin     */
    u_char	     rt_flags;		/* RTF_ flags defined below         */
    u_int32_t	     rt_gateway;	/* first-hop gateway back to origin */
    vifi_t	     rt_parent;		/* incoming vif (ie towards origin) */
    vifbitmap_t	     rt_children;	/* outgoing children vifs           */
    vifbitmap_t	     rt_leaves;		/* subset of outgoing children vifs */
    u_int32_t	    *rt_dominants;      /* per vif dominant gateways        */
    u_int32_t	    *rt_subordinates;   /* per vif subordinate gateways     */
    u_int	    *rt_leaf_timers;	/* per vif leaf confirmation timers */
    u_int	     rt_timer;		/* for timing out the route entry   */
    struct rtentry  *rt_prev;		/* link to previous entry           */
    struct gtable   *rt_groups;		/* link to active groups	    */
};

#define	RTF_CHANGED		0x01	/* route changed but not reported   */
#define RTF_LEAF_TIMING		0x02	/* some leaf timers are running     */

#define ALL_ROUTES	0		/* possible arguments to report()   */
#define CHANGED_ROUTES	1		/*  and report_to_all_neighbors()   */
