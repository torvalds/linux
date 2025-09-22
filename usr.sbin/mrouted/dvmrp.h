/*	$NetBSD: dvmrp.h,v 1.5 1995/12/10 10:07:00 mycroft Exp $	*/

/*
 * The mrouted program is covered by the license in the accompanying file
 * named "LICENSE".  Use of the mrouted program represents acceptance of
 * the terms and conditions listed in that file.
 *
 * The mrouted program is COPYRIGHT 1989 by The Board of Trustees of
 * Leland Stanford Junior University.
 */

/*
 * A DVMRP message consists of an IP header + an IGMP header + (for some types)
 * zero or more bytes of data.
 *
 * For REPORT messages, the data is route information; the route information
 * consists of one or more lists of the following form:
 *
 *		(mask, (origin, metric), (origin, metric), ...)
 *
 * where:
 *
 *	"mask" is the subnet mask for all the origins in the list.
 *		It is always THREE bytes long, containing the low-order
 *		three bytes of the mask (the high-order byte is always
 *		0xff and therefore need not be transmitted).
 *
 *	"origin" is the number of a subnet from which multicast datagrams
 *		may originate.  It is from one to four bytes long,
 *		depending on the value of "mask":
 *			if all bytes of the mask are zero
 *			    the subnet number is one byte long
 *			else if the low-order two bytes of the mask are zero
 *			    the subnet number is two bytes long
 *			else if the lowest-order byte of the mask is zero
 *			    the subnet number is three bytes long,
 *			else
 *			    the subnet number is four bytes long.
 *
 *	"metric" is a one-byte value consisting of two subfields:
 *		- the high-order bit is a flag which, when set, indicates
 *		  the last (origin, metric) pair of a list.
 *		- the low-order seven bits contain the routing metric for
 *		  the corresponding origin, relative to the sender of the
 *		  DVMRP report.  The metric may have the value of UNREACHABLE
 *		  added to it as a "split horizon" indication (so called
 *		  "poisoned reverse").
 *
 * Within a list, the origin subnet numbers must be in ascending order, and
 * the lists themselves are in order of increasing mask value.  A message may
 * not exceed 576 bytes, the default maximum IP reassembly size, including
 * the IP and IGMP headers; the route information may be split across more
 * than one message if necessary, by terminating a list in one message and
 * starting a new list in the next message (repeating the same mask value,
 * if necessary).
 *
 * For NEIGHBORS messages, the data is neighboring-router information
 * consisting of one or more lists of the following form:
 *
 *	(local-addr, metric, threshold, ncount, neighbor, neighbor, ...)
 *
 * where:
 *
 *	"local-addr" is the sending router's address as seen by the neighbors
 *		     in this list; it is always four bytes long.
 *	"metric" is a one-byte unsigned value, the TTL `cost' of forwarding
 *		 packets to any of the neighbors on this list.
 *	"threshold" is a one-byte unsigned value, a lower bound on the TTL a
 *		    packet must have to be forwarded to any of the neighbors on
 *		    this list.
 *	"ncount" is the number of neighbors in this list.
 *	"neighbor" is the address of a neighboring router, four bytes long.
 *
 * As with REPORT messages, NEIGHBORS messages should not exceed 576 bytes,
 * including the IP and IGMP headers; split longer messages by terminating the
 * list in one and continuing in another, repeating the local-addr, etc., if
 * necessary.
 *
 * For NEIGHBORS2 messages, the data is identical to NEIGHBORS except
 * there is a flags byte before the neighbor count:
 *
 *	(local-addr, metric, threshold, flags, ncount, neighbor, neighbor, ...)
 */

/*
 * DVMRP message types (carried in the "code" field of an IGMP header)
 */
#define DVMRP_PROBE		1	/* for finding neighbors             */
#define DVMRP_REPORT		2	/* for reporting some or all routes  */
#define DVMRP_ASK_NEIGHBORS	3	/* sent by mapper, asking for a list */
					/* of this router's neighbors. */
#define DVMRP_NEIGHBORS		4	/* response to such a request */
#define DVMRP_ASK_NEIGHBORS2	5	/* as above, want new format reply */
#define DVMRP_NEIGHBORS2	6
#define DVMRP_PRUNE		7	/* prune message */
#define DVMRP_GRAFT		8	/* graft message */
#define DVMRP_GRAFT_ACK		9	/* graft acknowledgement */
#define DVMRP_INFO_REQUEST	10	/* information request */
#define DVMRP_INFO_REPLY	11	/* information reply */

/*
 * 'flags' byte values in DVMRP_NEIGHBORS2 reply.
 */
#define DVMRP_NF_TUNNEL		0x01	/* neighbors reached via tunnel */
#define DVMRP_NF_SRCRT		0x02	/* tunnel uses IP source routing */
#define DVMRP_NF_PIM		0x04	/* neighbor is a PIM neighbor */
#define DVMRP_NF_DOWN		0x10	/* kernel state of interface */
#define DVMRP_NF_DISABLED	0x20	/* administratively disabled */
#define DVMRP_NF_QUERIER	0x40	/* I am the subnet's querier */
#define DVMRP_NF_LEAF		0x80	/* Neighbor reports that it is a leaf */

/*
 * Request/reply types for info queries/replies
 */
#define DVMRP_INFO_VERSION	1	/* version string */
#define DVMRP_INFO_NEIGHBORS	2	/* neighbors2 data */

/*
 * Limit on length of route data
 */
#define MAX_IP_PACKET_LEN	576
#define MIN_IP_HEADER_LEN	20
#define MAX_IP_HEADER_LEN	60
#define MAX_DVMRP_DATA_LEN \
		( MAX_IP_PACKET_LEN - MAX_IP_HEADER_LEN - IGMP_MINLEN )

/*
 * Various protocol constants (all times in seconds)
 */
				        /* address for multicast DVMRP msgs */
#define INADDR_DVMRP_GROUP	(u_int32_t)0xe0000004     /* 224.0.0.4 */
/*
 * The IGMPv2 <netinet/in.h> defines INADDR_ALLRTRS_GROUP, but earlier
 * ones don't, so we define it conditionally here.
 */
#ifndef INADDR_ALLRTRS_GROUP
					/* address for multicast mtrace msg */
#define INADDR_ALLRTRS_GROUP	(u_int32_t)0xe0000002	/* 224.0.0.2 */
#endif

#define ROUTE_MAX_REPORT_DELAY	5	/* max delay for reporting changes  */
					/*  (This is the timer interrupt    */
					/*  interval; all times must be     */
					/*  multiples of this value.)       */

#define	ROUTE_REPORT_INTERVAL	60	/* periodic route report interval   */
#define ROUTE_SWITCH_TIME	140	/* time to switch to equivalent gw  */
#define	ROUTE_EXPIRE_TIME	200	/* time to mark route invalid       */
#define	ROUTE_DISCARD_TIME	340	/* time to garbage collect route    */

#define LEAF_CONFIRMATION_TIME	200	/* time to consider subnet a leaf   */

#define NEIGHBOR_PROBE_INTERVAL	10	/* periodic neighbor probe interval */
#define NEIGHBOR_EXPIRE_TIME	140	/* time to consider neighbor gone   */

#define GROUP_QUERY_INTERVAL	125	/* periodic group query interval    */
#define GROUP_EXPIRE_TIME	270	/* time to consider group gone      */
#define LEAVE_EXPIRE_TIME	3	/* " " after receiving a leave	    */
/* Note: LEAVE_EXPIRE_TIME should ideally be shorter, but the resolution of
 * the timer in mrouted doesn't allow us to make it any shorter. */

#define UNREACHABLE		32	/* "infinity" metric, must be <= 64 */
#define DEFAULT_METRIC		1	/* default subnet/tunnel metric     */
#define DEFAULT_THRESHOLD	1	/* default subnet/tunnel threshold  */

#define MAX_RATE_LIMIT		100000	/* max rate limit		    */
#define DEFAULT_PHY_RATE_LIMIT  0	/* default phyint rate limit	    */
#define DEFAULT_TUN_RATE_LIMIT	500	/* default tunnel rate limit	    */

#define DEFAULT_CACHE_LIFETIME	300	/* kernel route entry discard time  */
#define GRAFT_TIMEOUT_VAL	5	/* retransmission time for grafts   */

#define OLD_AGE_THRESHOLD	2
