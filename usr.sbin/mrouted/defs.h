/*	$NetBSD: defs.h,v 1.6 1995/12/10 10:06:59 mycroft Exp $	*/

/*
 * The mrouted program is covered by the license in the accompanying file
 * named "LICENSE".  Use of the mrouted program represents acceptance of
 * the terms and conditions listed in that file.
 *
 * The mrouted program is COPYRIGHT 1989 by The Board of Trustees of
 * Leland Stanford Junior University.
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/igmp.h>
#include <netinet/ip_mroute.h>
#ifdef RSRR
#include <sys/un.h>
#endif /* RSRR */

typedef void (*cfunc_t)(void *);
typedef void (*ihfunc_t)(int);

#include "dvmrp.h"
#include "vif.h"
#include "route.h"
#include "prune.h"
#include "pathnames.h"
#ifdef RSRR
#include "rsrr.h"
#include "rsrr_var.h"
#endif /* RSRR */

/*
 * Miscellaneous constants and macros.
 */
#define FALSE		0
#define TRUE		1

#define EQUAL(s1, s2)	(strcmp((s1), (s2)) == 0)

#define TIMER_INTERVAL	ROUTE_MAX_REPORT_DELAY

#define VENDOR_CODE	1   /* Get a new vendor code if you make significant
			     * changes to mrouted. */

#define PROTOCOL_VERSION 3  /* increment when packet format/content changes */

#define MROUTED_VERSION  8  /* increment on local changes or bug fixes, */
			    /* reset to 0 whenever PROTOCOL_VERSION increments */

#define MROUTED_LEVEL  ((MROUTED_VERSION << 8) | PROTOCOL_VERSION | \
			((NF_PRUNE | NF_GENID | NF_MTRACE) << 16) | \
			(VENDOR_CODE << 24))
			    /* for IGMP 'group' field of DVMRP messages */

#define LEAF_FLAGS	(( vifs_with_neighbors == 1 ) ? 0x010000 : 0)
			    /* more for IGMP 'group' field of DVMRP messages */
#define	DEL_RTE_GROUP		0
#define	DEL_ALL_ROUTES		1
			    /* for Deleting kernel table entries */

#define JAN_1970	2208988800UL	/* 1970 - 1900 in seconds */

#ifdef RSRR
#define BIT_ZERO(X)      ((X) = 0)
#define BIT_SET(X,n)     ((X) |= 1 << (n))
#define BIT_CLR(X,n)     ((X) &= ~(1 << (n)))
#define BIT_TST(X,n)     ((X) & 1 << (n))
#endif /* RSRR */

/*
 * External declarations for global variables and functions.
 */
#define RECV_BUF_SIZE 8192
extern char		*recv_buf;
extern char		*send_buf;
extern int		igmp_socket;
#ifdef RSRR
extern int              rsrr_socket;
#endif /* RSRR */
extern u_int32_t	allhosts_group;
extern u_int32_t	allrtrs_group;
extern u_int32_t	dvmrp_group;
extern u_int32_t	dvmrp_genid;

#define DEFAULT_DEBUG  2	/* default if "-d" given without value */

extern int		debug;
extern u_char		pruning;

extern int		routes_changed;
extern int		delay_change_reports;
extern unsigned		nroutes;

extern struct uvif	uvifs[MAXVIFS];
extern vifi_t		numvifs;
extern int		vifs_down;
extern int		udp_socket;
extern int		vifs_with_neighbors;

extern char		s1[];
extern char		s2[];
extern char		s3[];
extern char		s4[];

#ifdef OLD_KERNEL
#define	MRT_INIT	DVMRP_INIT
#define	MRT_DONE	DVMRP_DONE
#define	MRT_ADD_VIF	DVMRP_ADD_VIF
#define	MRT_DEL_VIF	DVMRP_DEL_VIF
#define	MRT_ADD_MFC	DVMRP_ADD_MFC
#define	MRT_DEL_MFC	DVMRP_DEL_MFC

#define	IGMP_PIM	0x14
#endif

/* main.c */
extern void		logit(int, int, char *, ...);
extern int		register_input_handler(int fd, ihfunc_t func);

/* igmp.c */
extern void		init_igmp(void);
extern void		accept_igmp(int recvlen);
extern void		send_igmp(u_int32_t src, u_int32_t dst, int type,
			    int code, u_int32_t group, int datalen);

/* callout.c */
extern void		callout_init(void);
extern void		age_callout_queue(void);
extern int		timer_setTimer(int delay, cfunc_t action, char *data);
extern void		timer_clearTimer(int timer_id);

/* route.c */
extern void		init_routes(void);
extern void		start_route_updates(void);
extern void		update_route(u_int32_t origin, u_int32_t mask,
			    u_int metric, u_int32_t src, vifi_t vifi);
extern void		age_routes(void);
extern void		expire_all_routes(void);
extern void		free_all_routes(void);
extern void		accept_probe(u_int32_t src, u_int32_t dst, char *p,
			    int datalen, u_int32_t level);
extern void		accept_report(u_int32_t src, u_int32_t dst, char *p,
			    int datalen, u_int32_t level);
extern struct rtentry *	determine_route(u_int32_t src);
extern void		report(int which_routes, vifi_t vifi, u_int32_t dst);
extern void		report_to_all_neighbors(int which_routes);
extern int		report_next_chunk(void);
extern void		add_vif_to_routes(vifi_t vifi);
extern void		delete_vif_from_routes(vifi_t vifi);
extern void		delete_neighbor_from_routes(u_int32_t addr,
			    vifi_t vifi);
extern void		dump_routes(FILE *fp);
extern void		start_route_updates(void);

/* vif.c */
extern void		init_vifs(void);
extern void		check_vif_state(void);
extern vifi_t		find_vif(u_int32_t src, u_int32_t dst);
extern void		age_vifs(void);
extern void		dump_vifs(FILE *fp);
extern void		stop_all_vifs(void);
extern struct listaddr *neighbor_info(vifi_t vifi, u_int32_t addr);
extern void		accept_group_report(u_int32_t src, u_int32_t dst,
			    u_int32_t group, int r_type);
extern void		query_groups(void);
extern void		probe_for_neighbors(void);
extern int		update_neighbor(vifi_t vifi, u_int32_t addr,
			    int msgtype, char *p, int datalen, u_int32_t level);
extern void		accept_neighbor_request(u_int32_t src, u_int32_t dst);
extern void		accept_neighbor_request2(u_int32_t src, u_int32_t dst);
extern void		accept_neighbors(u_int32_t src, u_int32_t dst,
			    u_char *p, int datalen, u_int32_t level);
extern void		accept_neighbors2(u_int32_t src, u_int32_t dst,
			    u_char *p, int datalen, u_int32_t level);
extern void		accept_leave_message(u_int32_t src, u_int32_t dst,
			    u_int32_t group);
extern void		accept_membership_query(u_int32_t src, u_int32_t dst,
			    u_int32_t group, int tmo);
extern void		init_installvifs(void);

/* config.c */
extern void		config_vifs_from_kernel(void);

/* cfparse.y */
extern void		config_vifs_from_file(void);

/* inet.c */
extern int		inet_valid_host(u_int32_t naddr);
extern int		inet_valid_subnet(u_int32_t nsubnet, u_int32_t nmask);
extern char *		inet_fmt(u_int32_t addr, char *s);
extern char *		inet_fmts(u_int32_t addr, u_int32_t mask, char *s);
extern u_int32_t	inet_parse(char *s);
extern int		inet_cksum(u_int16_t *addr, u_int len);
extern int		inet_valid_host(u_int32_t naddr);
extern int		inet_valid_mask(u_int32_t mask);

/* prune.c */
extern unsigned		kroutes;
extern void		add_table_entry(u_int32_t origin, u_int32_t mcastgrp);
extern void		del_table_entry(struct rtentry *r,
			    u_int32_t mcastgrp, u_int del_flag);
extern void		update_table_entry(struct rtentry *r);
extern void		init_ktable(void);
extern void		accept_prune(u_int32_t src, u_int32_t dst, char *p,
			    int datalen);
extern void		steal_sources(struct rtentry *rt);
extern void		reset_neighbor_state(vifi_t vifi, u_int32_t addr);
extern int		grplst_mem(vifi_t vifi, u_int32_t mcastgrp);
extern int		scoped_addr(vifi_t vifi, u_int32_t addr);
extern void		free_all_prunes(void);
extern void		age_table_entry(void);
extern void		dump_cache(FILE *fp2);
extern void		update_lclgrp(vifi_t vifi, u_int32_t mcastgrp);
extern void		delete_lclgrp(vifi_t vifi, u_int32_t mcastgrp);
extern void		chkgrp_graft(vifi_t vifi, u_int32_t mcastgrp);
extern void		accept_graft(u_int32_t src, u_int32_t dst, char *p,
			    int datalen);
extern void		accept_g_ack(u_int32_t src, u_int32_t dst, char *p,
			    int datalen);
/* u_int is promoted u_char */
extern void		accept_mtrace(u_int32_t src, u_int32_t dst,
			    u_int32_t group, char *data, u_int no, int datalen);
extern void		accept_info_request(u_int32_t src, u_int32_t dst,
			    u_char *p, int datalen);
extern void		accept_info_reply(u_int32_t src, u_int32_t dst,
			    u_char *p, int datalen);

/* kern.c */
extern void		k_set_rcvbuf(int bufsize);
extern void		k_hdr_include(int bool);
extern void		k_set_ttl(int t);
extern void		k_set_loop(int l);
extern void		k_set_if(u_int32_t ifa);
extern void		k_join(u_int32_t grp, u_int32_t ifa);
extern void		k_leave(u_int32_t grp, u_int32_t ifa);
extern void		k_init_dvmrp(void);
extern void		k_stop_dvmrp(void);
extern void		k_add_vif(vifi_t vifi, struct uvif *v);
extern void		k_del_vif(vifi_t vifi);
extern void		k_add_rg(u_int32_t origin, struct gtable *g);
extern int		k_del_rg(u_int32_t origin, struct gtable *g);
extern int		k_get_version(void);

#ifdef RSRR
/* prune.c */
extern struct gtable	*kernel_table;
extern struct gtable	*gtp;
extern int		find_src_grp(u_int32_t src, u_int32_t mask,
			    u_int32_t grp);

/* rsrr.c */
extern void		rsrr_init(void);
extern void		rsrr_read(int f);
extern void		rsrr_clean(void);
extern void		rsrr_cache_send(struct gtable *gt, int notify);
extern void		rsrr_cache_clean(struct gtable *gt);
#endif /* RSRR */
