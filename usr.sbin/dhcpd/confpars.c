/*	$OpenBSD: confpars.c,v 1.36 2020/04/23 15:00:27 krw Exp $ */

/*
 * Copyright (c) 1995, 1996, 1997 The Internet Software Consortium.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <net/if.h>

#include <limits.h>
#include <netdb.h>
#include <resolv.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dhcp.h"
#include "tree.h"
#include "dhcpd.h"
#include "dhctoken.h"
#include "log.h"

int	parse_cidr(FILE *, unsigned char *, unsigned char *);

/*
 * conf-file :== parameters declarations EOF
 * parameters :== <nil> | parameter | parameters parameter
 * declarations :== <nil> | declaration | declarations declaration
 */
int
readconf(void)
{
	FILE *cfile;
	char *val;
	int token;
	int declaration = 0;

	new_parse(path_dhcpd_conf);

	/* Set up the initial dhcp option universe. */
	initialize_universes();

	/* Set up the global defaults. */
	root_group.default_lease_time = 43200; /* 12 hours. */
	root_group.max_lease_time = 86400; /* 24 hours. */
	root_group.bootp_lease_cutoff = MAX_TIME;
	root_group.boot_unknown_clients = 1;
	root_group.allow_bootp = 1;
	root_group.allow_booting = 1;
	root_group.authoritative = 1;
	root_group.echo_client_id = 1;

	if ((cfile = fopen(path_dhcpd_conf, "r")) == NULL)
		fatal("Can't open %s", path_dhcpd_conf);

	do {
		token = peek_token(&val, cfile);
		if (token == EOF)
			break;
		declaration = parse_statement(cfile, &root_group,
						 ROOT_GROUP,
						 NULL,
						 declaration);
	} while (1);
	token = next_token(&val, cfile); /* Clear the peek buffer */
	fclose(cfile);

	return !warnings_occurred;
}

/*
 * lease-file :== lease-declarations EOF
 * lease-statments :== <nil>
 *		   | lease-declaration
 *		   | lease-declarations lease-declaration
 */
void
read_leases(void)
{
	FILE *cfile;
	char *val;
	int token;

	new_parse(path_dhcpd_db);

	/*
	 * Open the lease file.   If we can't open it, fail.   The reason
	 * for this is that although on initial startup, the absence of
	 * a lease file is perfectly benign, if dhcpd has been running
	 * and this file is absent, it means that dhcpd tried and failed
	 * to rewrite the lease database.   If we proceed and the
	 * problem which caused the rewrite to fail has been fixed, but no
	 * human has corrected the database problem, then we are left
	 * thinking that no leases have been assigned to anybody, which
	 * could create severe network chaos.
	 */
	if ((cfile = fopen(path_dhcpd_db, "r")) == NULL) {
		log_warn("Can't open lease database (%s)", path_dhcpd_db);
		log_warnx("check for failed database rewrite attempt!");
		log_warnx("Please read the dhcpd.leases manual page if you");
		fatalx("don't know what to do about this.");
	}

	do {
		token = next_token(&val, cfile);
		if (token == EOF)
			break;
		if (token != TOK_LEASE) {
			log_warnx("Corrupt lease file - possible data loss!");
			skip_to_semi(cfile);
		} else {
			struct lease *lease;
			lease = parse_lease_declaration(cfile);
			if (lease)
				enter_lease(lease);
			else
				parse_warn("possibly corrupt lease file");
		}

	} while (1);
	fclose(cfile);
}

/*
 * statement :== parameter | declaration
 *
 * parameter :== timestamp
 *	     | DEFAULT_LEASE_TIME lease_time
 *	     | MAX_LEASE_TIME lease_time
 *	     | DYNAMIC_BOOTP_LEASE_CUTOFF date
 *	     | DYNAMIC_BOOTP_LEASE_LENGTH lease_time
 *	     | BOOT_UNKNOWN_CLIENTS boolean
 *	     | GET_LEASE_HOSTNAMES boolean
 *	     | USE_HOST_DECL_NAME boolean
 *	     | NEXT_SERVER ip-addr-or-hostname SEMI
 *	     | option_parameter
 *	     | SERVER-IDENTIFIER ip-addr-or-hostname SEMI
 *	     | FILENAME string-parameter
 *	     | SERVER_NAME string-parameter
 *	     | hardware-parameter
 *	     | fixed-address-parameter
 *	     | ALLOW allow-deny-keyword
 *	     | DENY allow-deny-keyword
 *	     | USE_LEASE_ADDR_FOR_DEFAULT_ROUTE boolean
 *
 * declaration :== host-declaration
 *		 | group-declaration
 *		 | shared-network-declaration
 *		 | subnet-declaration
 *		 | VENDOR_CLASS class-declaration
 *		 | USER_CLASS class-declaration
 *		 | RANGE address-range-declaration
 */
int
parse_statement(FILE *cfile, struct group *group, int type,
	struct host_decl *host_decl, int declaration)
{
	int token;
	char *val;
	struct shared_network *share;
	char *n;
	struct tree *tree;
	struct tree_cache *cache;
	struct hardware hardware;

	switch (next_token(&val, cfile)) {
	case TOK_HOST:
		if (type != HOST_DECL)
			parse_host_declaration(cfile, group);
		else {
			parse_warn("host declarations not allowed here.");
			skip_to_semi(cfile);
		}
		return 1;

	case TOK_GROUP:
		if (type != HOST_DECL)
			parse_group_declaration(cfile, group);
		else {
			parse_warn("host declarations not allowed here.");
			skip_to_semi(cfile);
		}
		return 1;

	case TOK_TIMESTAMP:
		break;

	case TOK_SHARED_NETWORK:
		if (type == SHARED_NET_DECL ||
		    type == HOST_DECL ||
		    type == SUBNET_DECL) {
			parse_warn("shared-network parameters not %s.",
				    "allowed here");
			skip_to_semi(cfile);
			break;
		}

		parse_shared_net_declaration(cfile, group);
		return 1;

	case TOK_SUBNET:
		if (type == HOST_DECL || type == SUBNET_DECL) {
			parse_warn("subnet declarations not allowed here.");
			skip_to_semi(cfile);
			return 1;
		}

		/* If we're in a subnet declaration, just do the parse. */
		if (group->shared_network) {
			parse_subnet_declaration(cfile,
			    group->shared_network);
			break;
		}

		/*
		 * Otherwise, cons up a fake shared network structure
		 * and populate it with the lone subnet.
		 */

		share = calloc(1, sizeof(struct shared_network));
		if (!share)
			fatalx("No memory for shared subnet");
		share->group = clone_group(group, "parse_statement:subnet");
		share->group->shared_network = share;

		parse_subnet_declaration(cfile, share);

		/* share->subnets is the subnet we just parsed. */
		if (share->subnets) {
			share->interface =
				share->subnets->interface;

			/* Make the shared network name from network number. */
			n = piaddr(share->subnets->net);
			share->name = strdup(n);
			if (share->name == NULL)
				fatalx("no memory for subnet name");

			/*
			 * Copy the authoritative parameter from the subnet,
			 * since there is no opportunity to declare it here.
			 */
			share->group->authoritative =
				share->subnets->group->authoritative;
			enter_shared_network(share);
		}
		return 1;

	case TOK_VENDOR_CLASS:
		parse_class_declaration(cfile, group, 0);
		return 1;

	case TOK_USER_CLASS:
		parse_class_declaration(cfile, group, 1);
		return 1;

	case TOK_DEFAULT_LEASE_TIME:
		parse_lease_time(cfile, &group->default_lease_time);
		break;

	case TOK_MAX_LEASE_TIME:
		parse_lease_time(cfile, &group->max_lease_time);
		break;

	case TOK_DYNAMIC_BOOTP_LEASE_CUTOFF:
		group->bootp_lease_cutoff = parse_date(cfile);
		break;

	case TOK_DYNAMIC_BOOTP_LEASE_LENGTH:
		parse_lease_time(cfile, &group->bootp_lease_length);
		break;

	case TOK_BOOT_UNKNOWN_CLIENTS:
		if (type == HOST_DECL)
			parse_warn("boot-unknown-clients not allowed here.");
		group->boot_unknown_clients = parse_boolean(cfile);
		break;

	case TOK_GET_LEASE_HOSTNAMES:
		if (type == HOST_DECL)
			parse_warn("get-lease-hostnames not allowed here.");
		group->get_lease_hostnames = parse_boolean(cfile);
		break;

	case TOK_ALWAYS_REPLY_RFC1048:
		group->always_reply_rfc1048 = parse_boolean(cfile);
		break;

	case TOK_ECHO_CLIENT_ID:
		group->echo_client_id = parse_boolean(cfile);
		break;

	case TOK_USE_HOST_DECL_NAMES:
		if (type == HOST_DECL)
			parse_warn("use-host-decl-names not allowed here.");
		group->use_host_decl_names = parse_boolean(cfile);
		break;

	case TOK_USE_LEASE_ADDR_FOR_DEFAULT_ROUTE:
		group->use_lease_addr_for_default_route =
			parse_boolean(cfile);
		break;

	case TOK_TOKEN_NOT:
		token = next_token(&val, cfile);
		switch (token) {
		case TOK_AUTHORITATIVE:
			if (type == HOST_DECL)
			    parse_warn("authority makes no sense here.");
			group->authoritative = 0;
			parse_semi(cfile);
			break;
		default:
			parse_warn("expecting assertion");
			skip_to_semi(cfile);
			break;
		}
		break;

	case TOK_AUTHORITATIVE:
		if (type == HOST_DECL)
		    parse_warn("authority makes no sense here.");
		group->authoritative = 1;
		parse_semi(cfile);
		break;

	case TOK_NEXT_SERVER:
		tree = parse_ip_addr_or_hostname(cfile, 0);
		if (!tree)
			break;
		cache = tree_cache(tree);
		if (!tree_evaluate (cache))
			fatalx("next-server is not known");
		group->next_server.len = 4;
		memcpy(group->next_server.iabuf,
		    cache->value, group->next_server.len);
		parse_semi(cfile);
		break;

	case TOK_OPTION:
		parse_option_param(cfile, group);
		break;

	case TOK_SERVER_IDENTIFIER:
		tree = parse_ip_addr_or_hostname(cfile, 0);
		if (!tree)
			return declaration;
		group->options[DHO_DHCP_SERVER_IDENTIFIER] = tree_cache(tree);
		token = next_token(&val, cfile);
		break;

	case TOK_FILENAME:
		group->filename = parse_string(cfile);
		break;

	case TOK_SERVER_NAME:
		group->server_name = parse_string(cfile);
		break;

	case TOK_HARDWARE:
		parse_hardware_param(cfile, &hardware);
		if (host_decl)
			host_decl->interface = hardware;
		else
			parse_warn("hardware address parameter %s",
				    "not allowed here.");
		break;

	case TOK_FIXED_ADDR:
		cache = parse_fixed_addr_param(cfile);
		if (host_decl)
			host_decl->fixed_addr = cache;
		else
			parse_warn("fixed-address parameter not %s",
				    "allowed here.");
		break;

	case TOK_RANGE:
		if (type != SUBNET_DECL || !group->subnet) {
			parse_warn("range declaration not allowed here.");
			skip_to_semi(cfile);
			return declaration;
		}
		parse_address_range(cfile, group->subnet);
		return declaration;

	case TOK_ALLOW:
		parse_allow_deny(cfile, group, 1);
		break;

	case TOK_DENY:
		parse_allow_deny(cfile, group, 0);
		break;

	default:
		if (declaration)
			parse_warn("expecting a declaration.");
		else
			parse_warn("expecting a parameter or declaration.");
		skip_to_semi(cfile);
		return declaration;
	}

	if (declaration) {
		parse_warn("parameters not allowed after first declaration.");
		return 1;
	}

	return 0;
}

/*
 * allow-deny-keyword :== BOOTP
 *			| BOOTING
 *			| DYNAMIC_BOOTP
 *			| UNKNOWN_CLIENTS
 */
void
parse_allow_deny(FILE *cfile, struct group *group, int flag)
{
	int token;
	char *val;

	token = next_token(&val, cfile);
	switch (token) {
	case TOK_BOOTP:
		group->allow_bootp = flag;
		break;

	case TOK_BOOTING:
		group->allow_booting = flag;
		break;

	case TOK_DYNAMIC_BOOTP:
		group->dynamic_bootp = flag;
		break;

	case TOK_UNKNOWN_CLIENTS:
		group->boot_unknown_clients = flag;
		break;

	default:
		parse_warn("expecting allow/deny key");
		skip_to_semi(cfile);
		return;
	}
	parse_semi(cfile);
}

/*
 * boolean :== ON SEMI | OFF SEMI | TRUE SEMI | FALSE SEMI
 */
int
parse_boolean(FILE *cfile)
{
	char *val;
	int rv;

	next_token(&val, cfile);
	if (!strcasecmp (val, "true") || !strcasecmp (val, "on"))
		rv = 1;
	else if (!strcasecmp (val, "false") || !strcasecmp (val, "off"))
		rv = 0;
	else {
		parse_warn("boolean value (true/false/on/off) expected");
		skip_to_semi(cfile);
		return 0;
	}
	parse_semi(cfile);
	return rv;
}

/*
 * Expect a left brace; if there isn't one, skip over the rest of the
 * statement and return zero; otherwise, return 1.
 */
int
parse_lbrace(FILE *cfile)
{
	int token;
	char *val;

	token = next_token(&val, cfile);
	if (token != '{') {
		parse_warn("expecting left brace.");
		skip_to_semi(cfile);
		return 0;
	}
	return 1;
}


/*
 * host-declaration :== hostname '{' parameters declarations '}'
 */
void
parse_host_declaration(FILE *cfile, struct group *group)
{
	char *val;
	int token;
	struct host_decl *host;
	char *name = parse_host_name(cfile);
	int declaration = 0;

	if (!name)
		return;

	host = calloc(1, sizeof (struct host_decl));
	if (!host)
		fatalx("can't allocate host decl struct %s.", name);

	host->name = name;
	host->group = clone_group(group, "parse_host_declaration");

	if (!parse_lbrace(cfile)) {
		free(host->name);
		free(host->group);
		free(host);
		return;
	}

	do {
		token = peek_token(&val, cfile);
		if (token == '}') {
			token = next_token(&val, cfile);
			break;
		}
		if (token == EOF) {
			token = next_token(&val, cfile);
			parse_warn("unexpected end of file");
			break;
		}
		declaration = parse_statement(cfile, host->group,
		    HOST_DECL, host, declaration);
	} while (1);

	if (!host->group->options[DHO_HOST_NAME] &&
	    host->group->use_host_decl_names) {
		host->group->options[DHO_HOST_NAME] =
		    new_tree_cache("parse_host_declaration");
		if (!host->group->options[DHO_HOST_NAME])
			fatalx("can't allocate a tree cache for hostname.");
		host->group->options[DHO_HOST_NAME]->len =
			strlen(name);
		host->group->options[DHO_HOST_NAME]->value =
			(unsigned char *)name;
		host->group->options[DHO_HOST_NAME]->buf_size =
			host->group->options[DHO_HOST_NAME]->len;
		host->group->options[DHO_HOST_NAME]->timeout = -1;
		host->group->options[DHO_HOST_NAME]->tree =
			NULL;
	}

	enter_host(host);
}

/*
 * class-declaration :== STRING '{' parameters declarations '}'
 */
void
parse_class_declaration(FILE *cfile, struct group *group, int type)
{
	char *val;
	int token;
	struct class *class;
	int declaration = 0;

	token = next_token(&val, cfile);
	if (token != TOK_STRING) {
		parse_warn("Expecting class name");
		skip_to_semi(cfile);
		return;
	}

	class = add_class (type, val);
	if (!class)
		fatalx("No memory for class %s.", val);
	class->group = clone_group(group, "parse_class_declaration");

	if (!parse_lbrace(cfile)) {
		free(class->name);
		free(class->group);
		free(class);
		return;
	}

	do {
		token = peek_token(&val, cfile);
		if (token == '}') {
			token = next_token(&val, cfile);
			break;
		} else if (token == EOF) {
			token = next_token(&val, cfile);
			parse_warn("unexpected end of file");
			break;
		} else {
			declaration = parse_statement(cfile, class->group,
			    CLASS_DECL, NULL, declaration);
		}
	} while (1);
}

/*
 * shared-network-declaration :==
 *			hostname LBRACE declarations parameters RBRACE
 */
void
parse_shared_net_declaration(FILE *cfile, struct group *group)
{
	char *val;
	int token;
	struct shared_network *share;
	char *name;
	int declaration = 0;

	share = calloc(1, sizeof(struct shared_network));
	if (!share)
		fatalx("No memory for shared subnet");
	share->leases = NULL;
	share->last_lease = NULL;
	share->insertion_point = NULL;
	share->next = NULL;
	share->interface = NULL;
	share->group = clone_group(group, "parse_shared_net_declaration");
	share->group->shared_network = share;

	/* Get the name of the shared network. */
	token = peek_token(&val, cfile);
	if (token == TOK_STRING) {
		token = next_token(&val, cfile);

		if (val[0] == 0) {
			parse_warn("zero-length shared network name");
			val = "<no-name-given>";
		}
		name = strdup(val);
		if (name == NULL)
			fatalx("no memory for shared network name");
	} else {
		name = parse_host_name(cfile);
		if (!name) {
			free(share->group);
			free(share);
			return;
		}
	}
	share->name = name;

	if (!parse_lbrace(cfile)) {
		free(share->group);
		free(share->name);
		free(share);
		return;
	}

	do {
		token = peek_token(&val, cfile);
		if (token == '}') {
			token = next_token(&val, cfile);
			if (!share->subnets) {
				free(share->group);
				free(share->name);
				free(share);
				parse_warn("empty shared-network decl");
				return;
			}
			enter_shared_network(share);
			return;
		} else if (token == EOF) {
			token = next_token(&val, cfile);
			parse_warn("unexpected end of file");
			break;
		}

		declaration = parse_statement(cfile, share->group,
		    SHARED_NET_DECL, NULL, declaration);
	} while (1);
}

/*
 * subnet-declaration :==
 *	net NETMASK netmask RBRACE parameters declarations LBRACE
 */
void
parse_subnet_declaration(FILE *cfile, struct shared_network *share)
{
	char *val;
	int token;
	struct subnet *subnet, *t, *u;
	struct iaddr iaddr;
	unsigned char addr[4];
	int len = sizeof addr;
	int declaration = 0;

	subnet = calloc(1, sizeof(struct subnet));
	if (!subnet)
		fatalx("No memory for new subnet");
	subnet->shared_network = share;
	subnet->group = clone_group(share->group, "parse_subnet_declaration");
	subnet->group->subnet = subnet;

	/* Get the network number. */
	if (!parse_numeric_aggregate(cfile, addr, &len, '.', 10, 8)) {
		free(subnet->group);
		free(subnet);
		return;
	}
	memcpy(iaddr.iabuf, addr, len);
	iaddr.len = len;
	subnet->net = iaddr;

	token = next_token(&val, cfile);
	if (token != TOK_NETMASK) {
		free(subnet->group);
		free(subnet);
		parse_warn("Expecting netmask");
		skip_to_semi(cfile);
		return;
	}

	/* Get the netmask. */
	if (!parse_numeric_aggregate(cfile, addr, &len, '.', 10, 8)) {
		free(subnet->group);
		free(subnet);
		return;
	}
	memcpy(iaddr.iabuf, addr, len);
	iaddr.len = len;
	subnet->netmask = iaddr;

	/* Save only the subnet number. */
	subnet->net = subnet_number(subnet->net, subnet->netmask);

	enter_subnet(subnet);

	if (!parse_lbrace(cfile))
		return;

	do {
		token = peek_token(&val, cfile);
		if (token == '}') {
			token = next_token(&val, cfile);
			break;
		} else if (token == EOF) {
			token = next_token(&val, cfile);
			parse_warn("unexpected end of file");
			break;
		}
		declaration = parse_statement(cfile, subnet->group,
		    SUBNET_DECL, NULL, declaration);
	} while (1);

	/*
	 * If this subnet supports dynamic bootp, flag it so in the
	 * shared_network containing it.
	 */
	if (subnet->group->dynamic_bootp)
		share->group->dynamic_bootp = 1;

	/* Add the subnet to the list of subnets in this shared net. */
	if (!share->subnets)
		share->subnets = subnet;
	else {
		u = NULL;
		for (t = share->subnets; t; t = t->next_sibling) {
			if (subnet_inner_than(subnet, t, 0)) {
				if (u)
					u->next_sibling = subnet;
				else
					share->subnets = subnet;
				subnet->next_sibling = t;
				return;
			}
			u = t;
		}
		u->next_sibling = subnet;
	}
}

/*
 * group-declaration :== RBRACE parameters declarations LBRACE
 */
void
parse_group_declaration(FILE *cfile, struct group *group)
{
	char *val;
	int token;
	struct group *g;
	int declaration = 0;

	g = clone_group(group, "parse_group_declaration");

	if (!parse_lbrace(cfile)) {
		free(g);
		return;
	}

	do {
		token = peek_token(&val, cfile);
		if (token == '}') {
			token = next_token(&val, cfile);
			break;
		} else if (token == EOF) {
			token = next_token(&val, cfile);
			parse_warn("unexpected end of file");
			break;
		}
		declaration = parse_statement(cfile, g, GROUP_DECL, NULL,
		    declaration);
	} while (1);
}

/*
 * cidr :== ip-address "/" bit-count
 * ip-address :== NUMBER [ DOT NUMBER [ DOT NUMBER [ DOT NUMBER ] ] ]
 * bit-count :== 0..32
 */
int
parse_cidr(FILE *cfile, unsigned char *subnet, unsigned char *subnetlen)
{
	uint8_t		 cidr[5];
	const char	*errstr;
	char		*val;
	long long	 numval;
	unsigned int	 i;
	int		 token;

	memset(cidr, 0, sizeof(cidr));
	i = 1;	/* Last four octets hold subnet, first octet the # of bits. */
	do {
		token = next_token(&val, cfile);
		if (i == 0)
			numval = strtonum(val, 0, 32, &errstr);
		else
			numval = strtonum(val, 0, UINT8_MAX, &errstr);
		if (errstr != NULL)
			break;
		cidr[i++] = numval;
		if (i == 1) {
			memcpy(subnet, &cidr[1], 4); /* XXX Need cidr_t */
			*subnetlen = cidr[0];
			return 1;
		}
		token = next_token(NULL, cfile);
		if (token == '/')
			i = 0;
		if (i == sizeof(cidr))
			break;
	} while (token == '.' || token == '/');

	parse_warn("expecting IPv4 CIDR block.");

	if (token != ';')
		skip_to_semi(cfile);
	return 0;
}

/*
 * ip-addr-or-hostname :== ip-address | hostname
 * ip-address :== NUMBER DOT NUMBER DOT NUMBER DOT NUMBER
 *
 * Parse an ip address or a hostname.   If uniform is zero, put in
 * a TREE_LIMIT node to catch hostnames that evaluate to more than
 * one IP address.
 */
struct tree *
parse_ip_addr_or_hostname(FILE *cfile, int uniform)
{
	char *val;
	int token;
	unsigned char addr[4];
	int len = sizeof addr;
	char *name;
	struct tree *rv;
	struct hostent *h;

	name = NULL;
	h = NULL;

	token = peek_token(&val, cfile);
	if (is_identifier(token)) {
		name = parse_host_name(cfile);
		if (name)
			h = gethostbyname(name);
		if (name && h) {
			rv = tree_const(h->h_addr_list[0], h->h_length);
			if (!uniform)
				rv = tree_limit(rv, 4);
			return rv;
		}
	}

	if (token == TOK_NUMBER_OR_NAME) {
		if (!parse_numeric_aggregate(cfile, addr, &len, '.', 10, 8)) {
			parse_warn("%s (%d): expecting IP address or hostname",
				    val, token);
			return NULL;
		}
		rv = tree_const(addr, len);
	} else {
		if (token != '{' && token != '}')
			token = next_token(&val, cfile);
		parse_warn("%s (%d): expecting IP address or hostname",
			    val, token);
		if (token != ';')
			skip_to_semi(cfile);
		return NULL;
	}

	return rv;
}


/*
 * fixed-addr-parameter :== ip-addrs-or-hostnames SEMI
 * ip-addrs-or-hostnames :== ip-addr-or-hostname
 *			   | ip-addrs-or-hostnames ip-addr-or-hostname
 */
struct tree_cache *
parse_fixed_addr_param(FILE *cfile)
{
	char *val;
	int token;
	struct tree *tree = NULL;
	struct tree *tmp;

	do {
		tmp = parse_ip_addr_or_hostname(cfile, 0);
		if (tree)
			tree = tree_concat(tree, tmp);
		else
			tree = tmp;
		token = peek_token(&val, cfile);
		if (token == ',')
			token = next_token(&val, cfile);
	} while (token == ',');

	if (!parse_semi(cfile))
		return NULL;
	return tree_cache(tree);
}

/*
 * option_parameter :== identifier DOT identifier <syntax> SEMI
 *		      | identifier <syntax> SEMI
 *
 * Option syntax is handled specially through format strings, so it
 * would be painful to come up with BNF for it. However, it always
 * starts as above and ends in a SEMI.
 */
void
parse_option_param(FILE *cfile, struct group *group)
{
	char *val;
	int token;
	unsigned char buf[4];
	unsigned char cprefix;
	char *vendor;
	char *fmt;
	struct universe *universe;
	struct option *option;
	struct tree *tree = NULL;
	struct tree *t;

	token = next_token(&val, cfile);
	if (!is_identifier(token)) {
		parse_warn("expecting identifier after option keyword.");
		if (token != ';')
			skip_to_semi(cfile);
		return;
	}
	vendor = strdup(val);
	if (vendor == NULL)
		fatalx("no memory for vendor token.");
	token = peek_token(&val, cfile);
	if (token == '.') {
		/* Go ahead and take the DOT token. */
		token = next_token(&val, cfile);

		/* The next token should be an identifier. */
		token = next_token(&val, cfile);
		if (!is_identifier(token)) {
			parse_warn("expecting identifier after '.'");
			if (token != ';')
				skip_to_semi(cfile);
			free(vendor);
			return;
		}

		/*
		 * Look up the option name hash table for the specified
		 * vendor.
		 */
		universe = ((struct universe *)hash_lookup(&universe_hash,
		    (unsigned char *)vendor, 0));
		/*
		 * If it's not there, we can't parse the rest of the
		 * declaration.
		 */
		if (!universe) {
			parse_warn("no vendor named %s.", vendor);
			skip_to_semi(cfile);
			free(vendor);
			return;
		}
	} else {
		/*
		 * Use the default hash table, which contains all the
		 * standard dhcp option names.
		 */
		val = vendor;
		universe = &dhcp_universe;
	}

	/* Look up the actual option info. */
	option = (struct option *)hash_lookup(universe->hash,
	    (unsigned char *)val, 0);

	/* If we didn't get an option structure, it's an undefined option. */
	if (!option) {
		if (val == vendor)
			parse_warn("no option named %s", val);
		else
			parse_warn("no option named %s for vendor %s",
				    val, vendor);
		skip_to_semi(cfile);
		free(vendor);
		return;
	}

	/* Free the initial identifier token. */
	free(vendor);

	/* Parse the option data. */
	do {
		/*
		 * Set a flag if this is an array of a simple type (i.e.,
		 * not an array of pairs of IP addresses, or something
		 * like that.
		 */
		int uniform = option->format[1] == 'A';

		for (fmt = option->format; *fmt; fmt++) {
			if (*fmt == 'A')
				break;
			switch (*fmt) {
			case 'X':
				token = peek_token(&val, cfile);
				if (token == TOK_NUMBER_OR_NAME) {
					do {
						token = next_token
							(&val, cfile);
						if (token !=
						    TOK_NUMBER_OR_NAME) {
							parse_warn("expecting "
							    "hex number.");
							if (token != ';')
								skip_to_semi(
								    cfile);
							return;
						}
						convert_num(buf, val, 16, 8);
						tree = tree_concat(tree,
						    tree_const(buf, 1));
						token = peek_token(&val,
						    cfile);
						if (token == ':')
							token =
							    next_token(&val,
							    cfile);
					} while (token == ':');
				} else if (token == TOK_STRING) {
					token = next_token(&val, cfile);
					tree = tree_concat(tree,
					    tree_const((unsigned char *)val,
					    strlen(val)));
				} else {
					parse_warn("expecting string %s.",
					    "or hexadecimal data");
					skip_to_semi(cfile);
					return;
				}
				break;

			case 't': /* Text string. */
				token = next_token(&val, cfile);
				if (token != TOK_STRING
				    && !is_identifier(token)) {
					parse_warn("expecting string.");
					if (token != ';')
						skip_to_semi(cfile);
					return;
				}
				tree = tree_concat(tree,
				    tree_const((unsigned char *)val,
				    strlen(val)));
				break;

			case 'I': /* IP address or hostname. */
				t = parse_ip_addr_or_hostname(cfile, uniform);
				if (!t)
					return;
				tree = tree_concat(tree, t);
				break;

			case 'L': /* Unsigned 32-bit integer. */
			case 'l': /* Signed 32-bit integer. */
				token = next_token(&val, cfile);
				if (token != TOK_NUMBER && token !=
				    TOK_NUMBER_OR_NAME) {
					parse_warn("expecting number.");
					if (token != ';')
						skip_to_semi(cfile);
					return;
				}
				convert_num(buf, val, 0, 32);
				tree = tree_concat(tree, tree_const(buf, 4));
				break;
			case 's': /* Signed 16-bit integer. */
			case 'S': /* Unsigned 16-bit integer. */
				token = next_token(&val, cfile);
				if (token != TOK_NUMBER && token !=
				    TOK_NUMBER_OR_NAME) {
					parse_warn("expecting number.");
					if (token != ';')
						skip_to_semi(cfile);
					return;
				}
				convert_num(buf, val, 0, 16);
				tree = tree_concat(tree, tree_const(buf, 2));
				break;
			case 'b': /* Signed 8-bit integer. */
			case 'B': /* Unsigned 8-bit integer. */
				token = next_token(&val, cfile);
				if (token != TOK_NUMBER && token !=
				    TOK_NUMBER_OR_NAME) {
					parse_warn("expecting number.");
					if (token != ';')
						skip_to_semi(cfile);
					return;
				}
				convert_num(buf, val, 0, 8);
				tree = tree_concat(tree, tree_const(buf, 1));
				break;
			case 'f': /* Boolean flag. */
				token = next_token(&val, cfile);
				if (!is_identifier(token)) {
					parse_warn("expecting identifier.");
					if (token != ';')
						skip_to_semi(cfile);
					return;
				}
				if (!strcasecmp(val, "true")
				    || !strcasecmp(val, "on"))
					buf[0] = 1;
				else if (!strcasecmp(val, "false")
					 || !strcasecmp(val, "off"))
					buf[0] = 0;
				else {
					parse_warn("expecting boolean.");
					if (token != ';')
						skip_to_semi(cfile);
					return;
				}
				tree = tree_concat(tree, tree_const(buf, 1));
				break;
			case 'C':
				if (!parse_cidr(cfile, buf, &cprefix))
					return;
				tree = tree_concat(tree, tree_const(&cprefix,
				    sizeof(cprefix)));
				if (cprefix > 0)
					tree = tree_concat(tree, tree_const(
					    buf, (cprefix + 7) / 8));
				break;
			case 'D':
				t = parse_domain_and_comp(cfile);
				if (!t)
					return;
				tree = tree_concat(tree, t);
				break;
			default:
				log_warnx("Bad format %c in "
				    "parse_option_param.", *fmt);
				skip_to_semi(cfile);
				return;
			}
		}
		if (*fmt == 'A') {
			token = peek_token(&val, cfile);
			if (token == ',') {
				token = next_token(&val, cfile);
				continue;
			}
			break;
		}
	} while (*fmt == 'A');

	token = next_token(&val, cfile);
	if (token != ';') {
		parse_warn("semicolon expected.");
		skip_to_semi(cfile);
		return;
	}
	group->options[option->code] = tree_cache(tree);
}

/*
 * lease_declaration :== LEASE ip_address LBRACE lease_parameters RBRACE
 *
 * lease_parameters :== <nil>
 *		      | lease_parameter
 *		      | lease_parameters lease_parameter
 *
 * lease_parameter :== STARTS date
 *		     | ENDS date
 *		     | TIMESTAMP date
 *		     | HARDWARE hardware-parameter
 *		     | UID hex_numbers SEMI
 *		     | HOSTNAME hostname SEMI
 *		     | CLIENT_HOSTNAME hostname SEMI
 *		     | CLASS identifier SEMI
 *		     | DYNAMIC_BOOTP SEMI
 */
struct lease *
parse_lease_declaration(FILE *cfile)
{
	char *val;
	int token;
	unsigned char addr[4];
	int len = sizeof addr;
	int seenmask = 0;
	int seenbit;
	char tbuf[32];
	static struct lease lease;

	/* Zap the lease structure. */
	memset(&lease, 0, sizeof lease);

	/* Get the address for which the lease has been issued. */
	if (!parse_numeric_aggregate(cfile, addr, &len, '.', 10, 8))
		return NULL;
	memcpy(lease.ip_addr.iabuf, addr, len);
	lease.ip_addr.len = len;

	if (!parse_lbrace(cfile))
		return NULL;

	do {
		token = next_token(&val, cfile);
		if (token == '}')
			break;
		else if (token == EOF) {
			parse_warn("unexpected end of file");
			break;
		}
		strlcpy(tbuf, val, sizeof tbuf);

		/* Parse any of the times associated with the lease. */
		if (token == TOK_STARTS || token == TOK_ENDS || token ==
		    TOK_TIMESTAMP) {
			time_t t;
			t = parse_date(cfile);
			switch (token) {
			case TOK_STARTS:
				seenbit = 1;
				lease.starts = t;
				break;

			case TOK_ENDS:
				seenbit = 2;
				lease.ends = t;
				break;

			case TOK_TIMESTAMP:
				seenbit = 4;
				lease.timestamp = t;
				break;

			default:
				/*NOTREACHED*/
				seenbit = 0;
				break;
			}
		} else {
			switch (token) {
				/* Colon-separated hexadecimal octets. */
			case TOK_UID:
				seenbit = 8;
				token = peek_token(&val, cfile);
				if (token == TOK_STRING) {
					token = next_token(&val, cfile);
					lease.uid_len = strlen(val);
					lease.uid = malloc(lease.uid_len);
					if (!lease.uid) {
						log_warnx("no space for uid");
						return NULL;
					}
					memcpy(lease.uid, val, lease.uid_len);
					parse_semi(cfile);
				} else {
					lease.uid_len = 0;
					lease.uid =
					    parse_numeric_aggregate(cfile,
					    NULL, &lease.uid_len, ':', 16, 8);
					if (!lease.uid) {
						log_warnx("no space for uid");
						return NULL;
					}
					if (lease.uid_len == 0) {
						lease.uid = NULL;
						parse_warn("zero-length uid");
						seenbit = 0;
						break;
					}
				}
				if (!lease.uid)
					fatalx("No memory for lease uid");
				break;

			case TOK_CLASS:
				seenbit = 32;
				token = next_token(&val, cfile);
				if (!is_identifier(token)) {
					if (token != ';')
						skip_to_semi(cfile);
					return NULL;
				}
				/* for now, we aren't using this. */
				break;

			case TOK_HARDWARE:
				seenbit = 64;
				parse_hardware_param(cfile,
				    &lease.hardware_addr);
				break;

			case TOK_DYNAMIC_BOOTP:
				seenbit = 128;
				lease.flags |= BOOTP_LEASE;
				break;

			case TOK_ABANDONED:
				seenbit = 256;
				lease.flags |= ABANDONED_LEASE;
				break;

			case TOK_HOSTNAME:
				seenbit = 512;
				token = peek_token(&val, cfile);
				if (token == TOK_STRING)
					lease.hostname = parse_string(cfile);
				else
					lease.hostname =
					    parse_host_name(cfile);
				if (!lease.hostname) {
					seenbit = 0;
					return NULL;
				}
				break;

			case TOK_CLIENT_HOSTNAME:
				seenbit = 1024;
				token = peek_token(&val, cfile);
				if (token == TOK_STRING)
					lease.client_hostname =
					    parse_string(cfile);
				else
					lease.client_hostname =
					    parse_host_name(cfile);
				break;

			default:
				skip_to_semi(cfile);
				seenbit = 0;
				return NULL;
			}

			if (token != TOK_HARDWARE && token != TOK_STRING) {
				token = next_token(&val, cfile);
				if (token != ';') {
					parse_warn("semicolon expected.");
					skip_to_semi(cfile);
					return NULL;
				}
			}
		}
		if (seenmask & seenbit) {
			parse_warn("Too many %s parameters in lease %s\n",
			    tbuf, piaddr(lease.ip_addr));
		} else
			seenmask |= seenbit;

	} while (1);
	return &lease;
}

/*
 * address-range-declaration :== ip-address ip-address SEMI
 *			       | DYNAMIC_BOOTP ip-address ip-address SEMI
 */
void
parse_address_range(FILE *cfile, struct subnet *subnet)
{
	struct iaddr low, high;
	unsigned char addr[4];
	int len = sizeof addr, token, dynamic = 0;
	char *val;

	if ((token = peek_token(&val, cfile)) == TOK_DYNAMIC_BOOTP) {
		token = next_token(&val, cfile);
		subnet->group->dynamic_bootp = dynamic = 1;
	}

	/* Get the bottom address in the range. */
	if (!parse_numeric_aggregate(cfile, addr, &len, '.', 10, 8))
		return;
	memcpy(low.iabuf, addr, len);
	low.len = len;

	/* Only one address? */
	token = peek_token(&val, cfile);
	if (token == ';')
		high = low;
	else {
		/* Get the top address in the range. */
		if (!parse_numeric_aggregate(cfile, addr, &len, '.', 10, 8))
			return;
		memcpy(high.iabuf, addr, len);
		high.len = len;
	}

	token = next_token(&val, cfile);
	if (token != ';') {
		parse_warn("semicolon expected.");
		skip_to_semi(cfile);
		return;
	}

	/* Create the new address range. */
	new_address_range(low, high, subnet, dynamic);
}

static void
push_domain_list(char ***domains, size_t *count, char *domain)
{
	*domains = reallocarray(*domains, *count + 1, sizeof **domains);
	if (!*domains)
		fatalx("Can't allocate domain list");

	(*domains)[*count] = domain;
	++*count;
}

static void
free_domain_list(char **domains, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		free(domains[i]);
	free(domains);
}

struct tree *
parse_domain_and_comp(FILE *cfile)
{
	struct tree	 *rv = NULL;
	char		**domains = NULL;
	char		 *val, *domain;
	unsigned char	 *buf = NULL;
	unsigned char	**bufptrs = NULL;
	size_t		  bufsiz = 0, bufn = 0, count = 0, i;
	int		  token = ';';

	do {
		if (token == ',')
			token = next_token(&val, cfile);

		token = next_token(&val, cfile);
		if (token != TOK_STRING) {
			parse_warn("string expected");
			goto error;
		}
		domain = strdup(val);
		if (domain == NULL)
			fatalx("Can't allocate domain to compress");
		push_domain_list(&domains, &count, domain);

		/*
		 * openbsd.org normally compresses to [7]openbsd[3]org[0].
		 * +2 to string length provides space for leading and
		 * trailing (root) prefix lengths not already accounted for
		 * by dots, and also provides sufficient space for pointer
		 * compression.
		 */
		bufsiz = bufsiz + 2 + strlen(domain);
		token = peek_token(NULL, cfile);
	} while (token == ',');

	buf = malloc(bufsiz);
	if (!buf)
		fatalx("Can't allocate compressed domain buffer");
	bufptrs = calloc(count + 1, sizeof *bufptrs);
	if (!bufptrs)
		fatalx("Can't allocate compressed pointer list");
	bufptrs[0] = buf;

	/* dn_comp takes an int for the output buffer size */
	if (!(bufsiz <= INT_MAX))
		fatalx("Size of compressed domain buffer too large");
	for (i = 0; i < count; i++) {
		int n;

		/* see bufsiz <= INT_MAX assertion, above */
		n = dn_comp(domains[i], &buf[bufn], bufsiz - bufn, bufptrs,
		    &bufptrs[count + 1]);
		if (n == -1)
			fatalx("Can't compress domain");
		bufn += (size_t)n;
	}

	rv = tree_const(buf, bufn);
error:
	free_domain_list(domains, count);
	free(buf);
	free(bufptrs);
	return rv;
}
