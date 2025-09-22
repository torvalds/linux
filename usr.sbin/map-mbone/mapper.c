/*	$OpenBSD: mapper.c,v 1.27 2024/12/01 09:58:15 kn Exp $	*/
/*	$NetBSD: mapper.c,v 1.3 1995/12/10 11:12:04 mycroft Exp $	*/

/* Mapper for connections between MRouteD multicast routers.
 * Written by Pavel Curtis <Pavel@PARC.Xerox.Com>
 */

/*
 * Copyright (c) 1992, 2001 Xerox Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither name of the Xerox, PARC, nor the names of its contributors may be used
 * to endorse or promote products derived from this software 
 * without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE XEROX CORPORATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include <string.h>
#include <netdb.h>
#include <sys/time.h>
#include "defs.h"
#include <arpa/inet.h>
#include <stdarg.h>
#include <poll.h>
#include <limits.h>
#include <err.h>

#define DEFAULT_TIMEOUT	2	/* How long to wait before retrying requests */
#define DEFAULT_RETRIES 1	/* How many times to ask each router */


/* All IP addresses are stored in the data structure in NET order. */

typedef struct neighbor {
    struct neighbor    *next;
    u_int32_t		addr;		/* IP address in NET order */
    u_char		metric;		/* TTL cost of forwarding */
    u_char		threshold;	/* TTL threshold to forward */
    u_short		flags;		/* flags on connection */
#define NF_PRESENT 0x8000	/* True if flags are meaningful */
} Neighbor;

typedef struct interface {
    struct interface *next;
    u_int32_t	addr;		/* IP address of the interface in NET order */
    Neighbor   *neighbors;	/* List of neighbors' IP addresses */
} Interface;

typedef struct node {
    u_int32_t	addr;		/* IP address of this entry in NET order */
    u_int32_t	version;	/* which mrouted version is running */
    int		tries;		/* How many requests sent?  -1 for aliases */
    union {
	struct node *alias;		/* If alias, to what? */
	struct interface *interfaces;	/* Else, neighbor data */
    } u;
    struct node *left, *right;
} Node;


Node   *routers = 0;
u_int32_t	our_addr, target_addr = 0;		/* in NET order */
int	debug = 0;
int	retries = DEFAULT_RETRIES;
int	timeout = DEFAULT_TIMEOUT;
int	show_names = TRUE;
vifi_t  numvifs;		/* to keep loader happy */
				/* (see COPY_TABLES macro called in kern.c) */

Node *			find_node(u_int32_t addr, Node **ptr);
Interface *		find_interface(u_int32_t addr, Node *node);
Neighbor *		find_neighbor(u_int32_t addr, Node *node);
int			main(int argc, char *argv[]);
void			ask(u_int32_t dst);
void			ask2(u_int32_t dst);
int			retry_requests(Node *node);
char *			inet_name(u_int32_t addr);
void			print_map(Node *node);
char *			graph_name(u_int32_t addr, char *buf, size_t len);
void			graph_edges(Node *node);
void			elide_aliases(Node *node);
void			graph_map(void);
u_int32_t		host_addr(char *name);
void			usage(void);

Node *find_node(u_int32_t addr, Node **ptr)
{
    Node *n = *ptr;

    if (!n) {
	*ptr = n = malloc(sizeof(Node));
	n->addr = addr;
	n->version = 0;
	n->tries = 0;
	n->u.interfaces = 0;
	n->left = n->right = 0;
	return n;
    } else if (addr == n->addr)
	return n;
    else if (addr < n->addr)
	return find_node(addr, &(n->left));
    else
	return find_node(addr, &(n->right));
}


Interface *find_interface(u_int32_t addr, Node *node)
{
    Interface *ifc;

    for (ifc = node->u.interfaces; ifc; ifc = ifc->next)
	if (ifc->addr == addr)
	    return ifc;

    ifc = malloc(sizeof(Interface));
    ifc->addr = addr;
    ifc->next = node->u.interfaces;
    node->u.interfaces = ifc;
    ifc->neighbors = 0;

    return ifc;
}


Neighbor *find_neighbor(u_int32_t addr, Node *node)
{
    Interface *ifc;

    for (ifc = node->u.interfaces; ifc; ifc = ifc->next) {
	Neighbor *nb;

	for (nb = ifc->neighbors; nb; nb = nb->next)
	    if (nb->addr == addr)
		return nb;
    }

    return 0;
}


/*
 * Log errors and other messages to stderr, according to the severity of the
 * message and the current debug level.  For errors of severity LOG_ERR or
 * worse, terminate the program.
 */
void
logit(int severity, int syserr, char *format, ...)
{
    va_list ap;
    char    fmt[100];

    switch (debug) {
	case 0: if (severity > LOG_WARNING) return;
	case 1: if (severity > LOG_NOTICE ) return;
	case 2: if (severity > LOG_INFO   ) return;
	default:
	    va_start(ap, format);
	    fmt[0] = '\0';
	    if (severity == LOG_WARNING)
		strlcat(fmt, "warning - ", sizeof(fmt));
	    strncat(fmt, format, 80);
	    vfprintf(stderr, fmt, ap);
	    va_end(ap);
	    if (syserr == 0)
		fprintf(stderr, "\n");
	    else if (syserr < sys_nerr)
		fprintf(stderr, ": %s\n", sys_errlist[syserr]);
	    else
		fprintf(stderr, ": errno %d\n", syserr);
    }

    if (severity <= LOG_ERR)
	exit(1);
}


/*
 * Send a neighbors-list request.
 */
void ask(u_int32_t dst)
{
    send_igmp(our_addr, dst, IGMP_DVMRP, DVMRP_ASK_NEIGHBORS,
		htonl(MROUTED_LEVEL), 0);
}

void ask2(u_int32_t dst)
{
    send_igmp(our_addr, dst, IGMP_DVMRP, DVMRP_ASK_NEIGHBORS2,
		htonl(MROUTED_LEVEL), 0);
}


/*
 * Process an incoming group membership report.
 */
void accept_group_report(u_int32_t src, u_int32_t dst, u_int32_t group,
    int r_type)
{
    logit(LOG_INFO, 0, "ignoring IGMP group membership report from %s to %s",
	inet_fmt(src, s1), inet_fmt(dst, s2));
}


/*
 * Process an incoming neighbor probe message.
 */
void accept_probe(u_int32_t src, u_int32_t dst, char *p, int datalen,
    u_int32_t level)
{
    logit(LOG_INFO, 0, "ignoring DVMRP probe from %s to %s",
	inet_fmt(src, s1), inet_fmt(dst, s2));
}


/*
 * Process an incoming route report message.
 */
void accept_report(u_int32_t src, u_int32_t dst, char *p, int datalen,
    u_int32_t level)
{
    logit(LOG_INFO, 0, "ignoring DVMRP routing report from %s to %s",
	inet_fmt(src, s1), inet_fmt(dst, s2));
}


/*
 * Process an incoming neighbor-list request message.
 */
void accept_neighbor_request(u_int32_t src, u_int32_t dst)
{
    if (src != our_addr)
	logit(LOG_INFO, 0,
	    "ignoring spurious DVMRP neighbor request from %s to %s",
	    inet_fmt(src, s1), inet_fmt(dst, s2));
}

void accept_neighbor_request2(u_int32_t src, u_int32_t dst)
{
    if (src != our_addr)
	logit(LOG_INFO, 0,
	    "ignoring spurious DVMRP neighbor request2 from %s to %s",
	    inet_fmt(src, s1), inet_fmt(dst, s2));
}


/*
 * Process an incoming neighbor-list message.
 */
void accept_neighbors(u_int32_t src, u_int32_t dst, u_char *p, int datalen,
    u_int32_t level)
{
    Node       *node = find_node(src, &routers);

    if (node->tries == 0)	/* Never heard of 'em; must have hit them at */
	node->tries = 1;	/* least once, though...*/
    else if (node->tries == -1)	/* follow alias link */
	node = node->u.alias;

#define GET_ADDR(a) (a = ((u_int32_t)*p++ << 24), a += ((u_int32_t)*p++ << 16),\
		     a += ((u_int32_t)*p++ << 8), a += *p++)

    /* if node is running a recent mrouted, ask for additional info */
    if (level != 0) {
	node->version = level;
	node->tries = 1;
	ask2(src);
	return;
    }

    if (debug > 3) {
	int i;

	fprintf(stderr, "    datalen = %d\n", datalen);
	for (i = 0; i < datalen; i++) {
	    if ((i & 0xF) == 0)
		fprintf(stderr, "   ");
	    fprintf(stderr, " %02x", p[i]);
	    if ((i & 0xF) == 0xF)
		fprintf(stderr, "\n");
	}
	if ((datalen & 0xF) != 0xF)
	    fprintf(stderr, "\n");
    }

    while (datalen > 0) {	/* loop through interfaces */
	u_int32_t		ifc_addr;
	u_char		metric, threshold, ncount;
	Node   	       *ifc_node;
	Interface      *ifc;
	Neighbor       *old_neighbors;

	if (datalen < 4 + 3) {
	    logit(LOG_WARNING, 0, "received truncated interface record from %s",
		inet_fmt(src, s1));
	    return;
	}

	GET_ADDR(ifc_addr);
	ifc_addr = htonl(ifc_addr);
	metric = *p++;
	threshold = *p++;
	ncount = *p++;
	datalen -= 4 + 3;

	/* Fix up any alias information */
	ifc_node = find_node(ifc_addr, &routers);
	if (ifc_node->tries == 0) { /* new node */
	    ifc_node->tries = -1;
	    ifc_node->u.alias = node;
	} else if (ifc_node != node
		   && (ifc_node->tries > 0  ||  ifc_node->u.alias != node)) {
	    /* must merge two hosts' nodes */
	    Interface  *ifc_i, *next_ifc_i;

	    if (ifc_node->tries == -1) {
		Node *tmp = ifc_node->u.alias;

		ifc_node->u.alias = node;
		ifc_node = tmp;
	    }

	    /* Merge ifc_node (foo_i) into node (foo_n) */

	    if (ifc_node->tries > node->tries)
		node->tries = ifc_node->tries;

	    for (ifc_i = ifc_node->u.interfaces; ifc_i; ifc_i = next_ifc_i) {
		Neighbor *nb_i, *next_nb_i, *nb_n;
		Interface *ifc_n = find_interface(ifc_i->addr, node);

		old_neighbors = ifc_n->neighbors;
		for (nb_i = ifc_i->neighbors; nb_i; nb_i = next_nb_i) {
		    next_nb_i = nb_i->next;
		    for (nb_n = old_neighbors; nb_n; nb_n = nb_n->next)
			if (nb_i->addr == nb_n->addr) {
			    if (nb_i->metric != nb_n->metric
				|| nb_i->threshold != nb_n->threshold)
				logit(LOG_WARNING, 0,
				    "inconsistent %s for neighbor %s of %s",
				    "metric/threshold",
				    inet_fmt(nb_i->addr, s1),
				    inet_fmt(node->addr, s2));
			    free(nb_i);
			    break;
			}
		    if (!nb_n) { /* no match for this neighbor yet */
			nb_i->next = ifc_n->neighbors;
			ifc_n->neighbors = nb_i;
		    }
		}

		next_ifc_i = ifc_i->next;
		free(ifc_i);
	    }

	    ifc_node->tries = -1;
	    ifc_node->u.alias = node;
	}
	
	ifc = find_interface(ifc_addr, node);
	old_neighbors = ifc->neighbors;
	
	/* Add the neighbors for this interface */
	while (ncount--) {
	    u_int32_t 	neighbor;
	    Neighbor   *nb;
	    Node       *n_node;

	    if (datalen < 4) {
		logit(LOG_WARNING, 0, "received truncated neighbor list from %s",
		    inet_fmt(src, s1));
		return;
	    }

	    GET_ADDR(neighbor);
	    neighbor = htonl(neighbor);
	    datalen -= 4;

	    for (nb = old_neighbors; nb; nb = nb->next)
		if (nb->addr == neighbor) {
		    if (metric != nb->metric || threshold != nb->threshold)
			logit(LOG_WARNING, 0,
			    "inconsistent %s for neighbor %s of %s",
			    "metric/threshold",
			    inet_fmt(nb->addr, s1), inet_fmt(node->addr, s2));
		    goto next_neighbor;
		}

	    nb = malloc(sizeof(Neighbor));
	    nb->next = ifc->neighbors;
	    ifc->neighbors = nb;
	    nb->addr = neighbor;
	    nb->metric = metric;
	    nb->threshold = threshold;
	    nb->flags = 0;

	    n_node = find_node(neighbor, &routers);
	    if (n_node->tries == 0  &&  !target_addr) { /* it's a new router */
		ask(neighbor);
		n_node->tries = 1;
	    }

	  next_neighbor: ;
	}
    }
}

void accept_neighbors2(u_int32_t src, u_int32_t dst, u_char *p, int datalen,
    u_int32_t level)
{
    Node       *node = find_node(src, &routers);
    u_int broken_cisco = ((level & 0xffff) == 0x020a); /* 10.2 */
    /* well, only possibly_broken_cisco, but that's too long to type. */

    if (node->tries == 0)	/* Never heard of 'em; must have hit them at */
	node->tries = 1;	/* least once, though...*/
    else if (node->tries == -1)	/* follow alias link */
	node = node->u.alias;

    while (datalen > 0) {	/* loop through interfaces */
	u_int32_t		ifc_addr;
	u_char		metric, threshold, ncount, flags;
	Node   	       *ifc_node;
	Interface      *ifc;
	Neighbor       *old_neighbors;

	if (datalen < 4 + 4) {
	    logit(LOG_WARNING, 0, "received truncated interface record from %s",
		inet_fmt(src, s1));
	    return;
	}

	ifc_addr = *(u_int32_t*)p;
	p += 4;
	metric = *p++;
	threshold = *p++;
	flags = *p++;
	ncount = *p++;
	datalen -= 4 + 4;

	if (broken_cisco && ncount == 0)	/* dumb Ciscos */
		ncount = 1;
	if (broken_cisco && ncount > 15)	/* dumb Ciscos */
		ncount = ncount & 0xf;

	/* Fix up any alias information */
	ifc_node = find_node(ifc_addr, &routers);
	if (ifc_node->tries == 0) { /* new node */
	    ifc_node->tries = -1;
	    ifc_node->u.alias = node;
	} else if (ifc_node != node
		   && (ifc_node->tries > 0  ||  ifc_node->u.alias != node)) {
	    /* must merge two hosts' nodes */
	    Interface  *ifc_i, *next_ifc_i;

	    if (ifc_node->tries == -1) {
		Node *tmp = ifc_node->u.alias;

		ifc_node->u.alias = node;
		ifc_node = tmp;
	    }

	    /* Merge ifc_node (foo_i) into node (foo_n) */

	    if (ifc_node->tries > node->tries)
		node->tries = ifc_node->tries;

	    for (ifc_i = ifc_node->u.interfaces; ifc_i; ifc_i = next_ifc_i) {
		Neighbor *nb_i, *next_nb_i, *nb_n;
		Interface *ifc_n = find_interface(ifc_i->addr, node);

		old_neighbors = ifc_n->neighbors;
		for (nb_i = ifc_i->neighbors; nb_i; nb_i = next_nb_i) {
		    next_nb_i = nb_i->next;
		    for (nb_n = old_neighbors; nb_n; nb_n = nb_n->next)
			if (nb_i->addr == nb_n->addr) {
			    if (nb_i->metric != nb_n->metric
				|| nb_i->threshold != nb_n->threshold)
				logit(LOG_WARNING, 0,
				    "inconsistent %s for neighbor %s of %s",
				    "metric/threshold",
				    inet_fmt(nb_i->addr, s1),
				    inet_fmt(node->addr, s2));
			    free(nb_i);
			    break;
			}
		    if (!nb_n) { /* no match for this neighbor yet */
			nb_i->next = ifc_n->neighbors;
			ifc_n->neighbors = nb_i;
		    }
		}

		next_ifc_i = ifc_i->next;
		free(ifc_i);
	    }

	    ifc_node->tries = -1;
	    ifc_node->u.alias = node;
	}
	
	ifc = find_interface(ifc_addr, node);
	old_neighbors = ifc->neighbors;
	
	/* Add the neighbors for this interface */
	while (ncount-- && datalen > 0) {
	    u_int32_t 	neighbor;
	    Neighbor   *nb;
	    Node       *n_node;

	    if (datalen < 4) {
		logit(LOG_WARNING, 0, "received truncated neighbor list from %s",
		    inet_fmt(src, s1));
		return;
	    }

	    neighbor = *(u_int32_t*)p;
	    p += 4;
	    datalen -= 4;
	    if (neighbor == 0)
		/* make leaf nets point to themselves */
		neighbor = ifc_addr;

	    for (nb = old_neighbors; nb; nb = nb->next)
		if (nb->addr == neighbor) {
		    if (metric != nb->metric || threshold != nb->threshold)
			logit(LOG_WARNING, 0,
			    "inconsistent %s for neighbor %s of %s",
			    "metric/threshold",
			    inet_fmt(nb->addr, s1), inet_fmt(node->addr, s2));
		    goto next_neighbor;
		}

	    nb = malloc(sizeof(Neighbor));
	    nb->next = ifc->neighbors;
	    ifc->neighbors = nb;
	    nb->addr = neighbor;
	    nb->metric = metric;
	    nb->threshold = threshold;
	    nb->flags = flags | NF_PRESENT;

	    n_node = find_node(neighbor, &routers);
	    if (n_node->tries == 0  &&  !target_addr) { /* it's a new router */
		ask(neighbor);
		n_node->tries = 1;
	    }

	  next_neighbor: ;
	}
    }
}


void check_vif_state(void)
{
    logit(LOG_NOTICE, 0, "network marked down...");
}


int retry_requests(Node *node)
{
    int	result;

    if (node) {
	result = retry_requests(node->left);
	if (node->tries > 0  &&  node->tries < retries) {
	    if (node->version)
		ask2(node->addr);
	    else
		ask(node->addr);
	    node->tries++;
	    result = 1;
	}
	return retry_requests(node->right) || result;
    } else
	return 0;
}


char *inet_name(u_int32_t addr)
{
    struct hostent *e;

    e = gethostbyaddr((char *)&addr, sizeof(addr), AF_INET);

    return e ? e->h_name : 0;
}


void print_map(Node *node)
{
    if (node) {
	char *name, *addr;
	
	print_map(node->left);

	addr = inet_fmt(node->addr, s1);
	if (!target_addr
	    || (node->tries >= 0 && node->u.interfaces)
	    || (node->tries == -1
		&& node->u.alias->tries >= 0
		&& node->u.alias->u.interfaces)) {
	    if (show_names && (name = inet_name(node->addr)))
		printf("%s (%s):", addr, name);
	    else
		printf("%s:", addr);
	    if (node->tries < 0)
		printf(" alias for %s\n\n", inet_fmt(node->u.alias->addr, s1));
	    else if (!node->u.interfaces)
		printf(" no response to query\n\n");
	    else {
		Interface *ifc;

		if (node->version)
		    printf(" <v%d.%d>", node->version & 0xff,
					(node->version >> 8) & 0xff);
		printf("\n");
		for (ifc = node->u.interfaces; ifc; ifc = ifc->next) {
		    Neighbor *nb;
		    char *ifc_name = inet_fmt(ifc->addr, s1);
		    int ifc_len = strlen(ifc_name);
		    int count = 0;

		    printf("    %s:", ifc_name);
		    for (nb = ifc->neighbors; nb; nb = nb->next) {
			if (count > 0)
			    printf("%*s", ifc_len + 5, "");
			printf("  %s", inet_fmt(nb->addr, s1));
			if (show_names  &&  (name = inet_name(nb->addr)))
			    printf(" (%s)", name);
			printf(" [%d/%d", nb->metric, nb->threshold);
			if (nb->flags) {
			    u_short flags = nb->flags;
			    if (flags & DVMRP_NF_TUNNEL)
				    printf("/tunnel");
			    if (flags & DVMRP_NF_SRCRT)
				    printf("/srcrt");
			    if (flags & DVMRP_NF_QUERIER)
				    printf("/querier");
			    if (flags & DVMRP_NF_DISABLED)
				    printf("/disabled");
			    if (flags & DVMRP_NF_DOWN)
				    printf("/down");
			}
                        printf("]\n");
			count++;
		    }
		}
		printf("\n");
	    }
	}
	print_map(node->right);
    }
}


char *graph_name(u_int32_t addr, char *buf, size_t len)
{
    char *name;

    if (show_names  &&  (name = inet_name(addr)))
	strlcpy(buf, name, len);
    else
	inet_fmt(addr, buf);

    return buf;
}


void graph_edges(Node *node)
{
    Interface *ifc;
    Neighbor *nb;
    char name[HOST_NAME_MAX+1];

    if (node) {
	graph_edges(node->left);
	if (node->tries >= 0) {
	    printf("  %d {$ NP %d0 %d0 $} \"%s%s\" \n",
		   (int) node->addr,
		   node->addr & 0xFF, (node->addr >> 8) & 0xFF,
		   graph_name(node->addr, name, sizeof(name)),
		   node->u.interfaces ? "" : "*");
	    for (ifc = node->u.interfaces; ifc; ifc = ifc->next)
		for (nb = ifc->neighbors; nb; nb = nb->next) {
		    Node *nb_node = find_node(nb->addr, &routers);
		    Neighbor *nb2;

		    if (nb_node->tries < 0)
			nb_node = nb_node->u.alias;

		    if (node != nb_node &&
			(!(nb2 = find_neighbor(node->addr, nb_node))
			 || node->addr < nb_node->addr)) {
			printf("    %d \"%d/%d",
			       nb_node->addr, nb->metric, nb->threshold);
			if (nb2 && (nb2->metric != nb->metric
				    || nb2->threshold != nb->threshold))
			    printf(",%d/%d", nb2->metric, nb2->threshold);
			if (nb->flags & NF_PRESENT)
			    printf("%s%s",
				   nb->flags & DVMRP_NF_SRCRT ? "" :
				   nb->flags & DVMRP_NF_TUNNEL ? "E" : "P",
				   nb->flags & DVMRP_NF_DOWN ? "D" : "");
			printf("\"\n");
		    }
		}
	    printf("    ;\n");
	}
	graph_edges(node->right);
    }
}

void elide_aliases(Node *node)
{
    if (node) {
	elide_aliases(node->left);
	if (node->tries >= 0) {
	    Interface *ifc;

	    for (ifc = node->u.interfaces; ifc; ifc = ifc->next) {
		Neighbor *nb;

		for (nb = ifc->neighbors; nb; nb = nb->next) {
		    Node *nb_node = find_node(nb->addr, &routers);

		    if (nb_node->tries < 0)
			nb->addr = nb_node->u.alias->addr;
		}
	    }
	}
	elide_aliases(node->right);
    }
}

void graph_map(void)
{
    time_t now = time(0);
    char *nowstr = ctime(&now);

    nowstr[24] = '\0';		/* Kill the newline at the end */
    elide_aliases(routers);
    printf("GRAPH \"Multicast Router Connectivity: %s\" = UNDIRECTED\n",
	   nowstr);
    graph_edges(routers);
    printf("END\n");
}


u_int32_t host_addr(char *name)
{
    struct hostent *e = gethostbyname(name);
    int addr;

    if (e)
	memcpy(&addr, e->h_addr_list[0], e->h_length);
    else {
	addr = inet_addr(name);
	if (addr == -1)
	    addr = 0;
    }

    return addr;
}

void usage(void)
{
    extern char *__progname;

    fprintf(stderr,
	    "usage: %s [-fgn] [-d[level]] [-r count] [-t seconds] "
	    "[starting_router]\n\n", __progname);

    exit(1);
}

int main(int argc, char *argv[])
{
    int flood = FALSE, graph = FALSE;
    int ch;
    const char *errstr;
    
    if (geteuid() != 0) {
      fprintf(stderr, "map-mbone: must be root\n");
      exit(1);
    }

    init_igmp();
    setuid(getuid());

    setvbuf(stderr, NULL, _IOLBF, 0);

    while ((ch = getopt(argc, argv, "d::fgnr:t:")) != -1) {
	    switch (ch) {
	    case 'd':
		    if (!optarg)
			    debug = DEFAULT_DEBUG;
		    else {
			    debug = strtonum(optarg, 0, 3, &errstr);
			    if (errstr) {
				    warnx("debug level %s", errstr);
				    debug = DEFAULT_DEBUG;
			    }
		    }
		    break;
	    case 'f':
		    flood = TRUE;
		    break;
	    case 'g':
		    graph = TRUE;
		    break;
	    case 'n':
		    show_names = FALSE;
		    break;
	    case 'r':
		    retries = strtonum(optarg, 0, INT_MAX, &errstr);
		    if (errstr) {
			    warnx("retries %s", errstr);
			    usage();
		    }
		    break;
	    case 't':
		    timeout = strtonum(optarg, 0, INT_MAX, &errstr);
		    if (errstr) {
			    warnx("timeout %s", errstr);
			    usage();
		    }
		    break;
	    default:
		    usage();
	    }
    }
    argc -= optind;
    argv += optind;
    
    if (argc > 1)
	usage();
    else if (argc == 1 && !(target_addr = host_addr(argv[0]))) {
	fprintf(stderr, "Unknown host: %s\n", argv[0]);
	exit(2);
    }

    if (debug)
	fprintf(stderr, "Debug level %u\n", debug);

    {				/* Find a good local address for us. */
	int udp;
	struct sockaddr_in addr;
	int addrlen = sizeof(addr);

	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_len = sizeof addr;
	addr.sin_addr.s_addr = dvmrp_group;
	addr.sin_port = htons(2000); /* any port over 1024 will do... */
	if ((udp = socket(AF_INET, SOCK_DGRAM, 0)) == -1
	    || connect(udp, (struct sockaddr *) &addr, sizeof(addr)) == -1
	    || getsockname(udp, (struct sockaddr *) &addr, &addrlen) == -1) {
	    perror("Determining local address");
	    exit(1);
	}
	close(udp);
	our_addr = addr.sin_addr.s_addr;
    }

    /* Send initial seed message to all local routers */
    ask(target_addr ? target_addr : allhosts_group);

    if (target_addr) {
	Node *n = find_node(target_addr, &routers);

	n->tries = 1;

	if (flood)
	    target_addr = 0;
    }

    /* Main receive loop */
    for(;;) {
	struct pollfd	pfd[1];
	int 		count, recvlen, dummy = 0;

	pfd[0].fd = igmp_socket;
	pfd[0].events = POLLIN;

	count = poll(pfd, 1, timeout * 1000);

	if (count == -1) {
	    if (errno != EINTR)
		perror("select");
	    continue;
	} else if (count == 0) {
	    logit(LOG_DEBUG, 0, "Timed out receiving neighbor lists");
	    if (retry_requests(routers))
		continue;
	    else
		break;
	}

	recvlen = recvfrom(igmp_socket, recv_buf, RECV_BUF_SIZE,
			   0, NULL, &dummy);
	if (recvlen >= 0)
	    accept_igmp(recvlen);
	else if (errno != EINTR)
	    perror("recvfrom");
    }

    printf("\n");

    if (graph)
	graph_map();
    else {
	if (!target_addr)
	    printf("Multicast Router Connectivity:\n\n");
	print_map(routers);
    }

    exit(0);
}

/* dummies */
void accept_prune(u_int32_t src, u_int32_t dst, char *p, int datalen)
{
}

void accept_graft(u_int32_t src, u_int32_t dst, char *p, int datalen)
{
}

void accept_g_ack(u_int32_t src, u_int32_t dst, char *p, int datalen)
{
}

void add_table_entry(u_int32_t origin, u_int32_t mcastgrp)
{
}

void accept_leave_message(u_int32_t src, u_int32_t dst, u_int32_t group)
{
}

void accept_mtrace(u_int32_t src, u_int32_t dst, u_int32_t group, char *data,
    u_int no, int datalen)
{
}

void accept_membership_query(u_int32_t src, u_int32_t dst, u_int32_t group,
    int tmo)
{
}

void accept_info_request(u_int32_t src, u_int32_t dst, u_char *p, int datalen)
{
}

void accept_info_reply(u_int32_t src, u_int32_t dst, u_char *p, int datalen)
{
}
