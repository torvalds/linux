/*	$NetBSD: vif.h,v 1.6 1995/12/10 10:07:20 mycroft Exp $	*/

/*
 * The mrouted program is covered by the license in the accompanying file
 * named "LICENSE".  Use of the mrouted program represents acceptance of
 * the terms and conditions listed in that file.
 *
 * The mrouted program is COPYRIGHT 1989 by The Board of Trustees of
 * Leland Stanford Junior University.
 */

/*
 * User level Virtual Interface structure
 *
 * A "virtual interface" is either a physical, multicast-capable interface
 * (called a "phyint") or a virtual point-to-point link (called a "tunnel").
 * (Note: all addresses, subnet numbers and masks are kept in NETWORK order.)
 */
struct uvif {
    u_short	     uv_flags;	    /* VIFF_ flags defined below            */
    u_char	     uv_metric;     /* cost of this vif                     */
    u_int	     uv_rate_limit; /* rate limit on this vif               */
    u_char	     uv_threshold;  /* min ttl required to forward on vif   */
    u_int32_t	     uv_lcl_addr;   /* local address of this vif            */
    u_int32_t	     uv_rmt_addr;   /* remote end-point addr (tunnels only) */
    u_int32_t	     uv_subnet;     /* subnet number         (phyints only) */
    u_int32_t	     uv_subnetmask; /* subnet mask           (phyints only) */
    u_int32_t	     uv_subnetbcast;/* subnet broadcast addr (phyints only) */
    char	     uv_name[IFNAMSIZ]; /* interface name                   */
    struct listaddr *uv_groups;     /* list of local groups  (phyints only) */
    struct listaddr *uv_neighbors;  /* list of neighboring routers          */
    struct vif_acl  *uv_acl;	    /* access control list of groups        */
    int		     uv_leaf_timer; /* time until this vif is considrd leaf */
    struct phaddr   *uv_addrs;	    /* Additional subnets on this vif       */
};

#define VIFF_KERNEL_FLAGS	(VIFF_TUNNEL|VIFF_SRCRT)
#define VIFF_DOWN		0x0100	       /* kernel state of interface */
#define VIFF_DISABLED		0x0200	       /* administratively disabled */
#define VIFF_QUERIER		0x0400	       /* I am the subnet's querier */
#define VIFF_ONEWAY		0x0800         /* Maybe one way interface   */
#define VIFF_LEAF		0x1000         /* all neighbors are leaves  */
#define VIFF_IGMPV1		0x2000         /* Act as an IGMPv1 Router   */

struct phaddr {
    struct phaddr   *pa_next;
    u_int32_t	     pa_subnet;		/* extra subnet			*/
    u_int32_t	     pa_subnetmask;	/* netmask of extra subnet	*/
    u_int32_t	     pa_subnetbcast;	/* broadcast of extra subnet	*/
};

struct vif_acl {
    struct vif_acl  *acl_next;	    /* next acl member         */
    u_int32_t	     acl_addr;	    /* Group address           */
    u_int32_t	     acl_mask;	    /* Group addr. mask        */
};

struct listaddr {
    struct listaddr *al_next;		/* link to next addr, MUST BE FIRST */
    u_int32_t	     al_addr;		/* local group or neighbor address  */
    u_long	     al_timer;		/* for timing out group or neighbor */
    time_t	     al_ctime;		/* neighbor creation time	    */
    u_int32_t	     al_genid;		/* generation id for neighbor       */
    u_char	     al_pv;		/* router protocol version	    */
    u_char	     al_mv;		/* router mrouted version	    */
    u_long           al_timerid;        /* returned by set timer            */
    u_long	     al_query;		/* second query in case of leave    */
    u_short          al_old;            /* time since heard old report      */
    u_char	     al_flags;		/* flags related to this neighbor   */
};

#define NF_LEAF			0x01	/* This neighbor is a leaf */
#define NF_PRUNE		0x02	/* This neighbor understands prunes */
#define NF_GENID		0x04	/* I supply genid & rtrlist in probe*/
#define NF_MTRACE		0x08	/* I can understand mtrace requests */

#define NO_VIF		((vifi_t)MAXVIFS)  /* An invalid vif index */
