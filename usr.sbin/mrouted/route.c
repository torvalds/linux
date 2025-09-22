/*	$NetBSD: route.c,v 1.5 1995/12/10 10:07:12 mycroft Exp $	*/

/*
 * The mrouted program is covered by the license in the accompanying file
 * named "LICENSE".  Use of the mrouted program represents acceptance of
 * the terms and conditions listed in that file.
 *
 * The mrouted program is COPYRIGHT 1989 by The Board of Trustees of
 * Leland Stanford Junior University.
 */


#include "defs.h"


/*
 * This define statement saves a lot of space later
 */
#define RT_ADDR	(struct rtentry *)&routing_table

/*
 * Exported variables.
 */
int routes_changed;			/* 1=>some routes have changed */
int delay_change_reports;		/* 1=>postpone change reports  */


/*
 * The routing table is shared with prune.c , so must not be static.
 */
struct rtentry *routing_table;		/* pointer to list of route entries */

/*
 * Private variables.
 */
static struct rtentry *rtp;		/* pointer to a route entry         */
static struct rtentry *rt_end;		/* pointer to last route entry      */
unsigned int nroutes;			/* current number of route entries  */

/*
 * Private functions.
 */
static int init_children_and_leaves(struct rtentry *r, vifi_t parent);
static int find_route(u_int32_t origin, u_int32_t mask);
static void create_route(u_int32_t origin, u_int32_t mask);
static void discard_route(struct rtentry *prev_r);
static int compare_rts(const void *rt1, const void *rt2);
static int report_chunk(struct rtentry *start_rt, vifi_t vifi, u_int32_t dst);

/*
 * Initialize the routing table and associated variables.
 */
void
init_routes()
{
    routing_table        = NULL;
    rt_end		 = RT_ADDR;
    nroutes		 = 0;
    routes_changed       = FALSE;
    delay_change_reports = FALSE;
}


/*
 * Initialize the children and leaf bits for route 'r', along with the
 * associated dominant, subordinate, and leaf timing data structures.
 * Return TRUE if this changes the value of either the children or
 * leaf bitmaps for 'r'.
 */
static int
init_children_and_leaves(struct rtentry *r, vifi_t parent)
{
    vifi_t vifi;
    struct uvif *v;
    vifbitmap_t old_children, old_leaves;

    VIFM_COPY(r->rt_children, old_children);
    VIFM_COPY(r->rt_leaves,   old_leaves  );

    VIFM_CLRALL(r->rt_children);
    VIFM_CLRALL(r->rt_leaves);
    r->rt_flags &= ~RTF_LEAF_TIMING;

    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	r->rt_dominants   [vifi] = 0;
	r->rt_subordinates[vifi] = 0;

	if (vifi != parent && !(v->uv_flags & (VIFF_DOWN|VIFF_DISABLED))) {
	    VIFM_SET(vifi, r->rt_children);
	    if (v->uv_neighbors == NULL) {
		VIFM_SET(vifi, r->rt_leaves);
		r->rt_leaf_timers[vifi] = 0;
	    }
	    else {
		r->rt_leaf_timers[vifi] = LEAF_CONFIRMATION_TIME;
		r->rt_flags |= RTF_LEAF_TIMING;
	    }
	}
	else {
	    r->rt_leaf_timers[vifi] = 0;
	}
    }

    return (!VIFM_SAME(r->rt_children, old_children) ||
	    !VIFM_SAME(r->rt_leaves,   old_leaves));
}


/*
 * A new vif has come up -- update the children and leaf bitmaps in all route
 * entries to take that into account.
 */
void
add_vif_to_routes(vifi_t vifi)
{
    struct rtentry *r;
    struct uvif *v;

    v = &uvifs[vifi];
    for (r = routing_table; r != NULL; r = r->rt_next) {
	if (r->rt_metric != UNREACHABLE &&
	    !VIFM_ISSET(vifi, r->rt_children)) {
	    VIFM_SET(vifi, r->rt_children);
	    r->rt_dominants   [vifi] = 0;
	    r->rt_subordinates[vifi] = 0;
	    if (v->uv_neighbors == NULL) {
		VIFM_SET(vifi, r->rt_leaves);
		r->rt_leaf_timers[vifi] = 0;
	    }
	    else {
		VIFM_CLR(vifi, r->rt_leaves);
		r->rt_leaf_timers[vifi] = LEAF_CONFIRMATION_TIME;
		r->rt_flags |= RTF_LEAF_TIMING;
	    }
	    update_table_entry(r);
	}
    }
}


/*
 * A vif has gone down -- expire all routes that have that vif as parent,
 * and update the children bitmaps in all other route entries to take into
 * account the failed vif.
 */
void
delete_vif_from_routes(vifi_t vifi)
{
    struct rtentry *r;

    for (r = routing_table; r != NULL; r = r->rt_next) {
	if (r->rt_metric != UNREACHABLE) {
	    if (vifi == r->rt_parent) {
		del_table_entry(r, 0, DEL_ALL_ROUTES);
		r->rt_timer    = ROUTE_EXPIRE_TIME;
		r->rt_metric   = UNREACHABLE;
		r->rt_flags   |= RTF_CHANGED;
		routes_changed = TRUE;
	    }
	    else if (VIFM_ISSET(vifi, r->rt_children)) {
		VIFM_CLR(vifi, r->rt_children);
		VIFM_CLR(vifi, r->rt_leaves);
		r->rt_subordinates[vifi] = 0;
		r->rt_leaf_timers [vifi] = 0;
		update_table_entry(r);
	    }
	    else {
		r->rt_dominants[vifi] = 0;
	    }
	}
    }
}


/*
 * A neighbor has failed or become unreachable.  If that neighbor was
 * considered a dominant or subordinate router in any route entries,
 * take appropriate action.
 */
void
delete_neighbor_from_routes(u_int32_t addr, vifi_t vifi)
{
    struct rtentry *r;
    struct uvif *v;

    v = &uvifs[vifi];
    for (r = routing_table; r != NULL; r = r->rt_next) {
	if (r->rt_metric != UNREACHABLE) {
	    if (r->rt_dominants[vifi] == addr) {
		VIFM_SET(vifi, r->rt_children);
		r->rt_dominants   [vifi] = 0;
		r->rt_subordinates[vifi] = 0;
		if (v->uv_neighbors == NULL) {
		    VIFM_SET(vifi, r->rt_leaves);
		    r->rt_leaf_timers[vifi] = 0;
		}
		else {
		    VIFM_CLR(vifi, r->rt_leaves);
		    r->rt_leaf_timers[vifi] = LEAF_CONFIRMATION_TIME;
		    r->rt_flags |= RTF_LEAF_TIMING;
		}
		update_table_entry(r);
	    }
	    else if (r->rt_subordinates[vifi] == addr) {
		r->rt_subordinates[vifi] = 0;
		if (v->uv_neighbors == NULL) {
		    VIFM_SET(vifi, r->rt_leaves);
		    update_table_entry(r);
		}
		else {
		    r->rt_leaf_timers[vifi] = LEAF_CONFIRMATION_TIME;
		    r->rt_flags |= RTF_LEAF_TIMING;
		}
	    }
	    else if (v->uv_neighbors == NULL &&
		     r->rt_leaf_timers[vifi] != 0) {
		VIFM_SET(vifi, r->rt_leaves);
		r->rt_leaf_timers[vifi] = 0;
		update_table_entry(r);
	    }
	}
    }
}


/*
 * Prepare for a sequence of ordered route updates by initializing a pointer
 * to the start of the routing table.  The pointer is used to remember our
 * position in the routing table in order to avoid searching from the
 * beginning for each update; this relies on having the route reports in
 * a single message be in the same order as the route entries in the routing
 * table.
 */
void
start_route_updates(void)
{
    rtp = RT_ADDR;
}


/*
 * Starting at the route entry following the one to which 'rtp' points,
 * look for a route entry matching the specified origin and mask.  If a
 * match is found, return TRUE and leave 'rtp' pointing at the found entry.
 * If no match is found, return FALSE and leave 'rtp' pointing to the route
 * entry preceding the point at which the new origin should be inserted.
 * This code is optimized for the normal case in which the first entry to
 * be examined is the matching entry.
 */
static int
find_route(u_int32_t origin, u_int32_t mask)
{
    struct rtentry *r;

    r = rtp->rt_next;
    while (r != NULL) {
	if (origin == r->rt_origin && mask == r->rt_originmask) {
	    rtp = r;
	    return (TRUE);
	}
	if (ntohl(mask) < ntohl(r->rt_originmask) ||
	    (mask == r->rt_originmask &&
	     ntohl(origin) < ntohl(r->rt_origin))) {
	    rtp = r;
	    r = r->rt_next;
	}
	else break;
    }
    return (FALSE);
}

/*
 * Create a new routing table entry for the specified origin and link it into
 * the routing table.  The shared variable 'rtp' is assumed to point to the
 * routing entry after which the new one should be inserted.  It is left
 * pointing to the new entry.
 *
 * Only the origin, originmask, originwidth and flags fields are initialized
 * in the new route entry; the caller is responsible for filling in the rest.
 */
static void
create_route(u_int32_t origin, u_int32_t mask)
{
    struct rtentry *r;

    if ((r = malloc(sizeof(struct rtentry) +
	(2 * numvifs * sizeof(u_int32_t)) +
	(numvifs * sizeof(u_int)))) == NULL) {
	logit(LOG_ERR, 0, "ran out of memory");	/* fatal */
    }
    r->rt_origin     = origin;
    r->rt_originmask = mask;
    if      (((char *)&mask)[3] != 0) r->rt_originwidth = 4;
    else if (((char *)&mask)[2] != 0) r->rt_originwidth = 3;
    else if (((char *)&mask)[1] != 0) r->rt_originwidth = 2;
    else                              r->rt_originwidth = 1;
    r->rt_flags        = 0;
    r->rt_dominants    = (u_int32_t *)(r + 1);
    r->rt_subordinates = (u_int32_t *)(r->rt_dominants + numvifs);
    r->rt_leaf_timers  = (u_int *)(r->rt_subordinates + numvifs);
    r->rt_groups       = NULL;

    r->rt_next = rtp->rt_next;
    rtp->rt_next = r;
    r->rt_prev = rtp;
    if (r->rt_next != NULL)
      (r->rt_next)->rt_prev = r;
    else
      rt_end = r;
    rtp = r;
    ++nroutes;
}


/*
 * Discard the routing table entry following the one to which 'prev_r' points.
 */
static void
discard_route(struct rtentry *prev_r)
{
    struct rtentry *r;

    r = prev_r->rt_next;
    prev_r->rt_next = r->rt_next;
    if (prev_r->rt_next != NULL)
      (prev_r->rt_next)->rt_prev = prev_r;
    else
      rt_end = prev_r;
    free((char *)r);
    --nroutes;
}


/*
 * Process a route report for a single origin, creating or updating the
 * corresponding routing table entry if necessary.  'src' is either the
 * address of a neighboring router from which the report arrived, or zero
 * to indicate a change of status of one of our own interfaces.
 */
void
update_route(u_int32_t origin, u_int32_t mask, u_int metric, u_int32_t src,
    vifi_t vifi)
{
    struct rtentry *r;
    u_int adj_metric;

    /*
     * Compute an adjusted metric, taking into account the cost of the
     * subnet or tunnel over which the report arrived, and normalizing
     * all unreachable/poisoned metrics into a single value.
     */
    if (src != 0 && (metric < 1 || metric >= 2*UNREACHABLE)) {
	logit(LOG_WARNING, 0,
	    "%s reports out-of-range metric %u for origin %s",
	    inet_fmt(src, s1), metric, inet_fmts(origin, mask, s2));
	return;
    }
    adj_metric = metric + uvifs[vifi].uv_metric;
    if (adj_metric > UNREACHABLE) adj_metric = UNREACHABLE;

    /*
     * Look up the reported origin in the routing table.
     */
    if (!find_route(origin, mask)) {
	/*
	 * Not found.
	 * Don't create a new entry if the report says it's unreachable,
	 * or if the reported origin and mask are invalid.
	 */
	if (adj_metric == UNREACHABLE) {
	    return;
	}
	if (src != 0 && !inet_valid_subnet(origin, mask)) {
	    logit(LOG_WARNING, 0,
		"%s reports an invalid origin (%s) and/or mask (%08x)",
		inet_fmt(src, s1), inet_fmt(origin, s2), ntohl(mask));
	    return;
	}

	/*
	 * OK, create the new routing entry.  'rtp' will be left pointing
	 * to the new entry.
	 */
	create_route(origin, mask);

	/*
	 * Now "steal away" any sources that belong under this route
	 * by deleting any cache entries they might have created
	 * and allowing the kernel to re-request them.
	 */
	steal_sources(rtp);

	rtp->rt_metric = UNREACHABLE;	/* temporary; updated below */
    }

    /*
     * We now have a routing entry for the reported origin.  Update it?
     */
    r = rtp;
    if (r->rt_metric == UNREACHABLE) {
	/*
	 * The routing entry is for a formerly-unreachable or new origin.
	 * If the report claims reachability, update the entry to use
	 * the reported route.
	 */
	if (adj_metric == UNREACHABLE)
	    return;

	r->rt_parent   = vifi;
	init_children_and_leaves(r, vifi);

	r->rt_gateway  = src;
	r->rt_timer    = 0;
	r->rt_metric   = adj_metric;
	r->rt_flags   |= RTF_CHANGED;
	routes_changed = TRUE;
	update_table_entry(r);
    }
    else if (src == r->rt_gateway) {
	/*
	 * The report has come either from the interface directly-connected
	 * to the origin subnet (src and r->rt_gateway both equal zero) or
	 * from the gateway we have chosen as the best first-hop gateway back
	 * towards the origin (src and r->rt_gateway not equal zero).  Reset
	 * the route timer and, if the reported metric has changed, update
	 * our entry accordingly.
	 */
	r->rt_timer = 0;
	if (adj_metric == r->rt_metric)
	    return;

	if (adj_metric == UNREACHABLE) {
	    del_table_entry(r, 0, DEL_ALL_ROUTES);
	    r->rt_timer = ROUTE_EXPIRE_TIME;
	}
	else if (adj_metric < r->rt_metric) {
	    if (init_children_and_leaves(r, vifi)) {
		update_table_entry(r);
	    }
	}
	r->rt_metric   = adj_metric;
	r->rt_flags   |= RTF_CHANGED;
	routes_changed = TRUE;
    }
    else if (src == 0 ||
	     (r->rt_gateway != 0 &&
	      (adj_metric < r->rt_metric ||
	       (adj_metric == r->rt_metric &&
		(ntohl(src) < ntohl(r->rt_gateway) ||
		 r->rt_timer >= ROUTE_SWITCH_TIME))))) {
	/*
	 * The report is for an origin we consider reachable; the report
	 * comes either from one of our own interfaces or from a gateway
	 * other than the one we have chosen as the best first-hop gateway
	 * back towards the origin.  If the source of the update is one of
	 * our own interfaces, or if the origin is not a directly-connected
	 * subnet and the reported metric for that origin is better than
	 * what our routing entry says, update the entry to use the new
	 * gateway and metric.  We also switch gateways if the reported
	 * metric is the same as the one in the route entry and the gateway
	 * associated with the route entry has not been heard from recently,
	 * or if the metric is the same but the reporting gateway has a lower
	 * IP address than the gateway associated with the route entry.
	 * Did you get all that?
	 */
	if (r->rt_parent != vifi || adj_metric < r->rt_metric) {
	    /*
	     * XXX Why do we do this if we are just changing the metric?
	     */
	    r->rt_parent = vifi;
	    if (init_children_and_leaves(r, vifi)) {
		update_table_entry(r);
	    }
	}
	r->rt_gateway  = src;
	r->rt_timer    = 0;
	r->rt_metric   = adj_metric;
	r->rt_flags   |= RTF_CHANGED;
	routes_changed = TRUE;
    }
    else if (vifi != r->rt_parent) {
	/*
	 * The report came from a vif other than the route's parent vif.
	 * Update the children and leaf info, if necessary.
	 */
	if (VIFM_ISSET(vifi, r->rt_children)) {
	    /*
	     * Vif is a child vif for this route.
	     */
	    if (metric  < r->rt_metric ||
		(metric == r->rt_metric &&
		 ntohl(src) < ntohl(uvifs[vifi].uv_lcl_addr))) {
		/*
		 * Neighbor has lower metric to origin (or has same metric
		 * and lower IP address) -- it becomes the dominant router,
		 * and vif is no longer a child for me.
		 */
		VIFM_CLR(vifi, r->rt_children);
		VIFM_CLR(vifi, r->rt_leaves);
		r->rt_dominants   [vifi] = src;
		r->rt_subordinates[vifi] = 0;
		r->rt_leaf_timers [vifi] = 0;
		update_table_entry(r);
	    }
	    else if (metric > UNREACHABLE) {	/* "poisoned reverse" */
		/*
		 * Neighbor considers this vif to be on path to route's
		 * origin; if no subordinate recorded, record this neighbor
		 * as subordinate and clear the leaf flag.
		 */
		if (r->rt_subordinates[vifi] == 0) {
		    VIFM_CLR(vifi, r->rt_leaves);
		    r->rt_subordinates[vifi] = src;
		    r->rt_leaf_timers [vifi] = 0;
		    update_table_entry(r);
		}
	    }
	    else if (src == r->rt_subordinates[vifi]) {
		/*
		 * Current subordinate no longer considers this vif to be on
		 * path to route's origin; it is no longer a subordinate
		 * router, and we set the leaf confirmation timer to give
		 * us time to hear from other subordinates.
		 */
		r->rt_subordinates[vifi] = 0;
		if (uvifs[vifi].uv_neighbors == NULL ||
		    uvifs[vifi].uv_neighbors->al_next == NULL) {
		    VIFM_SET(vifi, r->rt_leaves);
		    update_table_entry(r);
		}
		else {
		    r->rt_leaf_timers [vifi] = LEAF_CONFIRMATION_TIME;
		    r->rt_flags |= RTF_LEAF_TIMING;
		}
	    }

	}
	else if (src == r->rt_dominants[vifi] &&
		 (metric  > r->rt_metric ||
		  (metric == r->rt_metric &&
		   ntohl(src) > ntohl(uvifs[vifi].uv_lcl_addr)))) {
	    /*
	     * Current dominant no longer has a lower metric to origin
	     * (or same metric and lower IP address); we adopt the vif
	     * as our own child.
	     */
	    VIFM_SET(vifi, r->rt_children);
	    r->rt_dominants  [vifi] = 0;
	    if (metric > UNREACHABLE) {
		r->rt_subordinates[vifi] = src;
	    }
	    else if (uvifs[vifi].uv_neighbors == NULL ||
		     uvifs[vifi].uv_neighbors->al_next == NULL) {
		VIFM_SET(vifi, r->rt_leaves);
	    }
	    else {
		r->rt_leaf_timers[vifi] = LEAF_CONFIRMATION_TIME;
		r->rt_flags |= RTF_LEAF_TIMING;
	    }
	    update_table_entry(r);
	}
    }
}


/*
 * On every timer interrupt, advance the timer in each routing entry.
 */
void
age_routes(void)
{
    struct rtentry *r;
    struct rtentry *prev_r;
    vifi_t vifi;

    for (prev_r = RT_ADDR, r = routing_table;
	 r != NULL;
	 prev_r = r, r = r->rt_next) {

	if ((r->rt_timer += TIMER_INTERVAL) < ROUTE_EXPIRE_TIME) {
	    /*
	     * Route is still good; see if any leaf timers need to be
	     * advanced.
	     */
	    if (r->rt_flags & RTF_LEAF_TIMING) {
		r->rt_flags &= ~RTF_LEAF_TIMING;
		for (vifi = 0; vifi < numvifs; ++vifi) {
		    if (r->rt_leaf_timers[vifi] != 0) {
			/*
			 * Unlike other timers, leaf timers decrement.
			 */
			if ((r->rt_leaf_timers[vifi] -= TIMER_INTERVAL) == 0){
#ifdef NOTYET
			    /* If the vif is a physical leaf but has neighbors,
			     * it is not a tree leaf.  If I am a leaf, then no
			     * interface with neighbors is a tree leaf. */
			    if (!(((uvifs[vifi].uv_flags & VIFF_LEAF) ||
				   (vifs_with_neighbors == 1)) &&
				  (uvifs[vifi].uv_neighbors != NULL))) {
#endif
				VIFM_SET(vifi, r->rt_leaves);
				update_table_entry(r);
#ifdef NOTYET
			    }
#endif
			}
			else {
			    r->rt_flags |= RTF_LEAF_TIMING;
			}
		    }
		}
	    }
	}
	else if (r->rt_timer >= ROUTE_DISCARD_TIME) {
	    /*
	     * Time to garbage-collect the route entry.
	     */
	    del_table_entry(r, 0, DEL_ALL_ROUTES);
	    discard_route(prev_r);
	    r = prev_r;
	}
	else if (r->rt_metric != UNREACHABLE) {
	    /*
	     * Time to expire the route entry.  If the gateway is zero,
	     * i.e., it is a route to a directly-connected subnet, just
	     * set the timer back to zero; such routes expire only when
	     * the interface to the subnet goes down.
	     */
	    if (r->rt_gateway == 0) {
		r->rt_timer = 0;
	    }
	    else {
		del_table_entry(r, 0, DEL_ALL_ROUTES);
		r->rt_metric   = UNREACHABLE;
		r->rt_flags   |= RTF_CHANGED;
		routes_changed = TRUE;
	    }
	}
    }
}


/*
 * Mark all routes as unreachable.  This function is called only from
 * hup() in preparation for informing all neighbors that we are going
 * off the air.  For consistency, we ought also to delete all reachable
 * route entries from the kernel, but since we are about to exit we rely
 * on the kernel to do its own cleanup -- no point in making all those
 * expensive kernel calls now.
 */
void
expire_all_routes(void)
{
    struct rtentry *r;

    for (r = routing_table; r != NULL; r = r->rt_next) {
	r->rt_metric   = UNREACHABLE;
	r->rt_flags   |= RTF_CHANGED;
	routes_changed = TRUE;
    }
}


/*
 * Delete all the routes in the routing table.
 */
void
free_all_routes(void)
{
    struct rtentry *r;

    r = RT_ADDR;

    while (r->rt_next)
	discard_route(r);
}


/*
 * Process an incoming neighbor probe message.
 */
void
accept_probe(u_int32_t src, u_int32_t dst, char *p, int datalen,
    u_int32_t level)
{
    vifi_t vifi;

    if ((vifi = find_vif(src, dst)) == NO_VIF) {
	logit(LOG_INFO, 0,
	    "ignoring probe from non-neighbor %s", inet_fmt(src, s1));
	return;
    }

    update_neighbor(vifi, src, DVMRP_PROBE, p, datalen, level);
}

struct newrt {
	u_int32_t mask;
	u_int32_t origin;
	int metric;
	int pad;
};

static int
compare_rts(const void *rt1, const void *rt2)
{
    struct newrt *r1 = (struct newrt *)rt1;
    struct newrt *r2 = (struct newrt *)rt2;
    u_int32_t m1 = ntohl(r1->mask);
    u_int32_t m2 = ntohl(r2->mask);
    u_int32_t o1, o2;

    if (m1 > m2)
	return (-1);
    if (m1 < m2)
	return (1);

    /* masks are equal */
    o1 = ntohl(r1->origin);
    o2 = ntohl(r2->origin);
    if (o1 > o2)
	return (-1);
    if (o1 < o2)
	return (1);
    return (0);
}

/*
 * Process an incoming route report message.
 */
void
accept_report(u_int32_t src, u_int32_t dst, char *p, int datalen,
    u_int32_t level)
{
    vifi_t vifi;
    int width, i, nrt = 0;
    int metric;
    u_int32_t mask;
    u_int32_t origin;
    struct newrt rt[4096];

    if ((vifi = find_vif(src, dst)) == NO_VIF) {
	logit(LOG_INFO, 0,
	    "ignoring route report from non-neighbor %s", inet_fmt(src, s1));
	return;
    }

    if (!update_neighbor(vifi, src, DVMRP_REPORT, NULL, 0, level))
	return;

    if (datalen > 2*4096) {
	logit(LOG_INFO, 0,
	    "ignoring oversize (%d bytes) route report from %s",
	    datalen, inet_fmt(src, s1));
	return;
    }

    while (datalen > 0) {	/* Loop through per-mask lists. */

	if (datalen < 3) {
	    logit(LOG_WARNING, 0,
		"received truncated route report from %s",
		inet_fmt(src, s1));
	    return;
	}
	((u_char *)&mask)[0] = 0xff;            width = 1;
	if ((((u_char *)&mask)[1] = *p++) != 0) width = 2;
	if ((((u_char *)&mask)[2] = *p++) != 0) width = 3;
	if ((((u_char *)&mask)[3] = *p++) != 0) width = 4;
	if (!inet_valid_mask(ntohl(mask))) {
	    logit(LOG_WARNING, 0,
		"%s reports bogus netmask 0x%08x (%s)",
		inet_fmt(src, s1), ntohl(mask), inet_fmt(mask, s2));
	    return;
	}
	datalen -= 3;

	do {			/* Loop through (origin, metric) pairs */
	    if (datalen < width + 1) {
		logit(LOG_WARNING, 0,
		    "received truncated route report from %s",
		    inet_fmt(src, s1));
		return;
	    }
	    origin = 0;
	    for (i = 0; i < width; ++i)
		((char *)&origin)[i] = *p++;
	    metric = *p++;
	    datalen -= width + 1;
	    rt[nrt].mask   = mask;
	    rt[nrt].origin = origin;
	    rt[nrt].metric = (metric & 0x7f);
	    ++nrt;
	} while (!(metric & 0x80));
    }

    qsort((char*)rt, nrt, sizeof(rt[0]), compare_rts);
    start_route_updates();
    /*
     * If the last entry is default, change mask from 0xff000000 to 0
     */
    if (rt[nrt-1].origin == 0)
	rt[nrt-1].mask = 0;

    logit(LOG_DEBUG, 0, "Updating %d routes from %s to %s", nrt,
		inet_fmt(src, s1), inet_fmt(dst, s2));
    for (i = 0; i < nrt; ++i) {
	if (i != 0 && rt[i].origin == rt[i-1].origin &&
		      rt[i].mask == rt[i-1].mask) {
	    logit(LOG_WARNING, 0, "%s reports duplicate route for %s",
		inet_fmt(src, s1), inet_fmts(rt[i].origin, rt[i].mask, s2));
	    continue;
	}
	update_route(rt[i].origin, rt[i].mask, rt[i].metric,
		     src, vifi);
    }

    if (routes_changed && !delay_change_reports)
	report_to_all_neighbors(CHANGED_ROUTES);
}


/*
 * Send a route report message to destination 'dst', via virtual interface
 * 'vifi'.  'which_routes' specifies ALL_ROUTES or CHANGED_ROUTES.
 */
void
report(int which_routes, vifi_t vifi, u_int32_t dst)
{
    struct rtentry *r;
    char *p;
    int i;
    int datalen = 0;
    int width = 0;
    u_int32_t mask = 0;
    u_int32_t src;
    u_int32_t nflags;

    src = uvifs[vifi].uv_lcl_addr;

    p = send_buf + MIN_IP_HEADER_LEN + IGMP_MINLEN;

#ifdef NOTYET
    /* If I'm not a leaf, but the neighbor is a leaf, only advertise default */
    if ((vifs_with_neighbors != 1) && (uvifs[vifi].uv_flags & VIFF_LEAF)) {
      *p++ = 0;       /* 0xff000000 mask */
      *p++ = 0;
      *p++ = 0;
      *p++ = 0;       /* class A net 0.0.0.0 == default */
      *p++ = 0x81;    /*XXX metric 1, is this safe? */
      datalen += 5;
      send_igmp(src, dst, IGMP_DVMRP, DVMRP_REPORT,
                htonl(MROUTED_LEVEL), datalen);
      return;
    }
#endif

    nflags = (uvifs[vifi].uv_flags & VIFF_LEAF) ? 0 : LEAF_FLAGS;

    for (r = rt_end; r != RT_ADDR; r = r->rt_prev) {

	if (which_routes == CHANGED_ROUTES && !(r->rt_flags & RTF_CHANGED))
	    continue;

	/*
	 * If there is no room for this route in the current message,
	 * send the message and start a new one.
	 */
	if (datalen + ((r->rt_originmask == mask) ?
		       (width + 1) :
		       (r->rt_originwidth + 4)) > MAX_DVMRP_DATA_LEN) {
	    *(p-1) |= 0x80;
	    send_igmp(src, dst, IGMP_DVMRP, DVMRP_REPORT,
		      htonl(MROUTED_LEVEL | nflags), datalen);

	    p = send_buf + MIN_IP_HEADER_LEN + IGMP_MINLEN;
	    datalen = 0;
	    mask = 0;
	}

	if (r->rt_originmask != mask || datalen == 0) {
	    mask  = r->rt_originmask;
	    width = r->rt_originwidth;
	    if (datalen != 0) *(p-1) |= 0x80;
	    *p++ = ((char *)&mask)[1];
	    *p++ = ((char *)&mask)[2];
	    *p++ = ((char *)&mask)[3];
	    datalen += 3;
	}

	for (i = 0; i < width; ++i)
	    *p++ = ((char *)&(r->rt_origin))[i];

	*p++ = (r->rt_parent == vifi && r->rt_metric != UNREACHABLE) ?
	    (char)(r->rt_metric + UNREACHABLE) :  /* "poisoned reverse" */
		(char)(r->rt_metric);

	datalen += width + 1;
    }

    if (datalen != 0) {
	*(p-1) |= 0x80;
	send_igmp(src, dst, IGMP_DVMRP, DVMRP_REPORT,
		  htonl(MROUTED_LEVEL | nflags), datalen);
    }
}


/*
 * Send a route report message to all neighboring routers.
 * 'which_routes' specifies ALL_ROUTES or CHANGED_ROUTES.
 */
void
report_to_all_neighbors(int which_routes)
{
    vifi_t vifi;
    struct uvif *v;
    struct rtentry *r;
    int routes_changed_before;

    /*
     * Remember the state of the global routes_changed flag before
     * generating the reports, and clear the flag.
     */
    routes_changed_before = routes_changed;
    routes_changed = FALSE;


    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	if (v->uv_neighbors != NULL) {
	    report(which_routes, vifi,
		   (v->uv_flags & VIFF_TUNNEL) ? v->uv_rmt_addr
		   : dvmrp_group);
	}
    }

    /*
     * If there were changed routes before we sent the reports AND
     * if no new changes occurred while sending the reports, clear
     * the change flags in the individual route entries.  If changes
     * did occur while sending the reports, new reports will be
     * generated at the next timer interrupt.
     */
    if (routes_changed_before && !routes_changed) {
	for (r = routing_table; r != NULL; r = r->rt_next) {
	    r->rt_flags &= ~RTF_CHANGED;
	}
    }

    /*
     * Set a flag to inhibit further reports of changed routes until the
     * next timer interrupt.  This is to alleviate update storms.
     */
    delay_change_reports = TRUE;
}

/*
 * Send a route report message to destination 'dst', via virtual interface
 * 'vifi'.  'which_routes' specifies ALL_ROUTES or CHANGED_ROUTES.
 */
static int
report_chunk(struct rtentry *start_rt, vifi_t vifi, u_int32_t dst)
{
    struct rtentry *r;
    char *p;
    int i;
    int nrt = 0;
    int datalen = 0;
    int width = 0;
    u_int32_t mask = 0;
    u_int32_t src;
    u_int32_t nflags;

    src = uvifs[vifi].uv_lcl_addr;
    p = send_buf + MIN_IP_HEADER_LEN + IGMP_MINLEN;

    nflags = (uvifs[vifi].uv_flags & VIFF_LEAF) ? 0 : LEAF_FLAGS;

    for (r = start_rt; r != RT_ADDR; r = r->rt_prev) {

#ifdef NOTYET
	/* Don't send poisoned routes back to parents if I am a leaf */
	if ((vifs_with_neighbors == 1) && (r->rt_parent == vifi)
		&& (r->rt_metric > 1)) {
	    ++nrt;
	    continue;
	}
#endif

	/*
	 * If there is no room for this route in the current message,
	 * send it & return how many routes we sent.
	 */
	if (datalen + ((r->rt_originmask == mask) ?
		       (width + 1) :
		       (r->rt_originwidth + 4)) > MAX_DVMRP_DATA_LEN) {
	    *(p-1) |= 0x80;
	    send_igmp(src, dst, IGMP_DVMRP, DVMRP_REPORT,
		      htonl(MROUTED_LEVEL | nflags), datalen);
	    return (nrt);
	}
	if (r->rt_originmask != mask || datalen == 0) {
	    mask  = r->rt_originmask;
	    width = r->rt_originwidth;
	    if (datalen != 0) *(p-1) |= 0x80;
	    *p++ = ((char *)&mask)[1];
	    *p++ = ((char *)&mask)[2];
	    *p++ = ((char *)&mask)[3];
	    datalen += 3;
	}
	for (i = 0; i < width; ++i)
	    *p++ = ((char *)&(r->rt_origin))[i];

	*p++ = (r->rt_parent == vifi && r->rt_metric != UNREACHABLE) ?
	    (char)(r->rt_metric + UNREACHABLE) :  /* "poisoned reverse" */
		(char)(r->rt_metric);
	++nrt;
	datalen += width + 1;
    }
    if (datalen != 0) {
	*(p-1) |= 0x80;
	send_igmp(src, dst, IGMP_DVMRP, DVMRP_REPORT,
		  htonl(MROUTED_LEVEL | nflags), datalen);
    }
    return (nrt);
}

/*
 * send the next chunk of our routing table to all neighbors.
 * return the length of the smallest chunk we sent out.
 */
int
report_next_chunk(void)
{
    vifi_t vifi;
    struct uvif *v;
    struct rtentry *sr;
    int i, n = 0, min = 20000;
    static int start_rt;

    if (nroutes <= 0)
	return (0);

    /*
     * find this round's starting route.
     */
    for (sr = rt_end, i = start_rt; --i >= 0; ) {
	sr = sr->rt_prev;
	if (sr == RT_ADDR)
	    sr = rt_end;
    }

    /*
     * send one chunk of routes starting at this round's start to
     * all our neighbors.
     */
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	if ((v->uv_neighbors != NULL)
#ifdef NOTYET
	&& !(v->uv_flags & VIFF_LEAF)
#endif
		) {
	    n = report_chunk(sr, vifi,
			     (v->uv_flags & VIFF_TUNNEL) ? v->uv_rmt_addr
			     : dvmrp_group);
	    if (n < min)
		min = n;
	}
    }
    if (min == 20000)
	min = 0;	/* Neighborless router didn't send any routes */

    n = min;
    logit(LOG_INFO, 0, "update %d starting at %d of %d",
	n, (nroutes - start_rt), nroutes);

    start_rt = (start_rt + n) % nroutes;
    return (n);
}


/*
 * Print the contents of the routing table on file 'fp'.
 */
void
dump_routes(FILE *fp)
{
    struct rtentry *r;
    vifi_t i;


    fprintf(fp,
	    "Multicast Routing Table (%u %s)\n%s\n",
	    nroutes, (nroutes == 1) ? "entry" : "entries",
	    " Origin-Subnet      From-Gateway    Metric Tmr In-Vif  Out-Vifs");

    for (r = routing_table; r != NULL; r = r->rt_next) {

	fprintf(fp, " %-18s %-15s ",
		inet_fmts(r->rt_origin, r->rt_originmask, s1),
		(r->rt_gateway == 0) ? "" : inet_fmt(r->rt_gateway, s2));

	fprintf(fp, (r->rt_metric == UNREACHABLE) ? "  NR " : "%4u ",
		r->rt_metric);

	fprintf(fp, "  %3u %3u   ", r->rt_timer, r->rt_parent);

	for (i = 0; i < numvifs; ++i) {
	    if (VIFM_ISSET(i, r->rt_children)) {
		fprintf(fp, " %u%c",
			i, VIFM_ISSET(i, r->rt_leaves) ? '*' : ' ');
	    }
	}
	fprintf(fp, "\n");
    }
    fprintf(fp, "\n");
}

struct rtentry *
determine_route(u_int32_t src)
{
    struct rtentry *rt;

    for (rt = routing_table; rt != NULL; rt = rt->rt_next) {
	if (rt->rt_origin == (src & rt->rt_originmask))
	    break;
    }
    return rt;
}
