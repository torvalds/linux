/*	$OpenBSD: clparse.c,v 1.18 2004/09/15 18:15:18 henning Exp $	*/

/* Parser for dhclient config and lease files... */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997 The Internet Software Consortium.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "dhcpd.h"
#include "dhctoken.h"

struct client_config top_level_config;
static struct interface_info *dummy_interfaces;

static char client_script_name[] = "/sbin/dhclient-script";

/*
 * client-conf-file :== client-declarations EOF
 * client-declarations :== <nil>
 *			 | client-declaration
 *			 | client-declarations client-declaration
 */
int
read_client_conf(void)
{
	FILE			*cfile;
	char			*val;
	int			 token;
	struct client_config	*config;

	new_parse(path_dhclient_conf);

	/* Set up the initial dhcp option universe. */
	initialize_universes();

	/* Initialize the top level client configuration. */
	memset(&top_level_config, 0, sizeof(top_level_config));

	/* Set some defaults... */
	top_level_config.timeout = 60;
	top_level_config.select_interval = 0;
	top_level_config.reboot_timeout = 10;
	top_level_config.retry_interval = 300;
	top_level_config.backoff_cutoff = 15;
	top_level_config.initial_interval = 3;
	top_level_config.bootp_policy = ACCEPT;
	top_level_config.script_name = client_script_name;
	top_level_config.requested_options
	    [top_level_config.requested_option_count++] = DHO_SUBNET_MASK;
	top_level_config.requested_options
	    [top_level_config.requested_option_count++] = DHO_BROADCAST_ADDRESS;
	top_level_config.requested_options
	    [top_level_config.requested_option_count++] = DHO_TIME_OFFSET;
	top_level_config.requested_options
	    [top_level_config.requested_option_count++] = DHO_CLASSLESS_ROUTES;
	top_level_config.requested_options
	    [top_level_config.requested_option_count++] = DHO_ROUTERS;
	top_level_config.requested_options
	    [top_level_config.requested_option_count++] = DHO_DOMAIN_NAME;
	top_level_config.requested_options
	    [top_level_config.requested_option_count++] =
	    DHO_DOMAIN_NAME_SERVERS;
	top_level_config.requested_options
	    [top_level_config.requested_option_count++] = DHO_HOST_NAME;
	top_level_config.requested_options
	    [top_level_config.requested_option_count++] = DHO_DOMAIN_SEARCH;
	top_level_config.requested_options
	    [top_level_config.requested_option_count++] = DHO_INTERFACE_MTU;

	if ((cfile = fopen(path_dhclient_conf, "r")) != NULL) {
		do {
			token = peek_token(&val, cfile);
			if (token == EOF)
				break;
			parse_client_statement(cfile, NULL, &top_level_config);
		} while (1);
		token = next_token(&val, cfile); /* Clear the peek buffer */
		fclose(cfile);
	}

	/*
	 * Set up state and config structures for clients that don't
	 * have per-interface configuration declarations.
	 */
	config = NULL;
	if (!ifi->client) {
		ifi->client = malloc(sizeof(struct client_state));
		if (!ifi->client)
			error("no memory for client state.");
		memset(ifi->client, 0, sizeof(*(ifi->client)));
	}
	if (!ifi->client->config) {
		if (!config) {
			config = malloc(sizeof(struct client_config));
			if (!config)
				error("no memory for client config.");
			memcpy(config, &top_level_config,
				sizeof(top_level_config));
		}
		ifi->client->config = config;
	}

	return (!warnings_occurred);
}

/*
 * lease-file :== client-lease-statements EOF
 * client-lease-statements :== <nil>
 *		     | client-lease-statements LEASE client-lease-statement
 */
void
read_client_leases(void)
{
	FILE	*cfile;
	char	*val;
	int	 token;

	new_parse(path_dhclient_db);

	/* Open the lease file.   If we can't open it, just return -
	   we can safely trust the server to remember our state. */
	if ((cfile = fopen(path_dhclient_db, "r")) == NULL)
		return;
	do {
		token = next_token(&val, cfile);
		if (token == EOF)
			break;
		if (token != LEASE) {
			warning("Corrupt lease file - possible data loss!");
			skip_to_semi(cfile);
			break;
		} else
			parse_client_lease_statement(cfile, 0);

	} while (1);
	fclose(cfile);
}

/*
 * client-declaration :==
 *	SEND option-decl |
 *	DEFAULT option-decl |
 *	SUPERSEDE option-decl |
 *	PREPEND option-decl |
 *	APPEND option-decl |
 *	hardware-declaration |
 *	REQUEST option-list |
 *	REQUIRE option-list |
 *	TIMEOUT number |
 *	RETRY number |
 *	REBOOT number |
 *	SELECT_TIMEOUT number |
 *	SCRIPT string |
 *	interface-declaration |
 *	LEASE client-lease-statement |
 *	ALIAS client-lease-statement
 */
void
parse_client_statement(FILE *cfile, struct interface_info *ip,
    struct client_config *config)
{
	int		 token;
	char		*val;
	struct option	*option;

	switch (next_token(&val, cfile)) {
	case SEND:
		parse_option_decl(cfile, &config->send_options[0]);
		return;
	case DEFAULT:
		option = parse_option_decl(cfile, &config->defaults[0]);
		if (option)
			config->default_actions[option->code] = ACTION_DEFAULT;
		return;
	case SUPERSEDE:
		option = parse_option_decl(cfile, &config->defaults[0]);
		if (option)
			config->default_actions[option->code] =
			    ACTION_SUPERSEDE;
		return;
	case APPEND:
		option = parse_option_decl(cfile, &config->defaults[0]);
		if (option)
			config->default_actions[option->code] = ACTION_APPEND;
		return;
	case PREPEND:
		option = parse_option_decl(cfile, &config->defaults[0]);
		if (option)
			config->default_actions[option->code] = ACTION_PREPEND;
		return;
	case MEDIA:
		parse_string_list(cfile, &config->media, 1);
		return;
	case HARDWARE:
		if (ip)
			parse_hardware_param(cfile, &ip->hw_address);
		else {
			parse_warn("hardware address parameter %s",
				    "not allowed here.");
			skip_to_semi(cfile);
		}
		return;
	case REQUEST:
		config->requested_option_count =
			parse_option_list(cfile, config->requested_options);
		return;
	case REQUIRE:
		memset(config->required_options, 0,
		    sizeof(config->required_options));
		parse_option_list(cfile, config->required_options);
		return;
	case TIMEOUT:
		parse_lease_time(cfile, &config->timeout);
		return;
	case RETRY:
		parse_lease_time(cfile, &config->retry_interval);
		return;
	case SELECT_TIMEOUT:
		parse_lease_time(cfile, &config->select_interval);
		return;
	case REBOOT:
		parse_lease_time(cfile, &config->reboot_timeout);
		return;
	case BACKOFF_CUTOFF:
		parse_lease_time(cfile, &config->backoff_cutoff);
		return;
	case INITIAL_INTERVAL:
		parse_lease_time(cfile, &config->initial_interval);
		return;
	case SCRIPT:
		config->script_name = parse_string(cfile);
		return;
	case INTERFACE:
		if (ip)
			parse_warn("nested interface declaration.");
		parse_interface_declaration(cfile, config);
		return;
	case LEASE:
		parse_client_lease_statement(cfile, 1);
		return;
	case ALIAS:
		parse_client_lease_statement(cfile, 2);
		return;
	case REJECT:
		parse_reject_statement(cfile, config);
		return;
	default:
		parse_warn("expecting a statement.");
		skip_to_semi(cfile);
		break;
	}
	token = next_token(&val, cfile);
	if (token != SEMI) {
		parse_warn("semicolon expected.");
		skip_to_semi(cfile);
	}
}

unsigned
parse_X(FILE *cfile, u_int8_t *buf, unsigned max)
{
	int	 token;
	char	*val;
	unsigned len;

	token = peek_token(&val, cfile);
	if (token == NUMBER_OR_NAME || token == NUMBER) {
		len = 0;
		do {
			token = next_token(&val, cfile);
			if (token != NUMBER && token != NUMBER_OR_NAME) {
				parse_warn("expecting hexadecimal constant.");
				skip_to_semi(cfile);
				return (0);
			}
			convert_num(&buf[len], val, 16, 8);
			if (len++ > max) {
				parse_warn("hexadecimal constant too long.");
				skip_to_semi(cfile);
				return (0);
			}
			token = peek_token(&val, cfile);
			if (token == COLON)
				token = next_token(&val, cfile);
		} while (token == COLON);
		val = (char *)buf;
	} else if (token == STRING) {
		token = next_token(&val, cfile);
		len = strlen(val);
		if (len + 1 > max) {
			parse_warn("string constant too long.");
			skip_to_semi(cfile);
			return (0);
		}
		memcpy(buf, val, len + 1);
	} else {
		parse_warn("expecting string or hexadecimal data");
		skip_to_semi(cfile);
		return (0);
	}
	return (len);
}

/*
 * option-list :== option_name |
 *		   option_list COMMA option_name
 */
int
parse_option_list(FILE *cfile, u_int8_t *list)
{
	int	 ix, i;
	int	 token;
	char	*val;

	ix = 0;
	do {
		token = next_token(&val, cfile);
		if (!is_identifier(token)) {
			parse_warn("expected option name.");
			skip_to_semi(cfile);
			return (0);
		}
		for (i = 0; i < 256; i++)
			if (!strcasecmp(dhcp_options[i].name, val))
				break;

		if (i == 256) {
			parse_warn("%s: unexpected option name.", val);
			skip_to_semi(cfile);
			return (0);
		}
		list[ix++] = i;
		if (ix == 256) {
			parse_warn("%s: too many options.", val);
			skip_to_semi(cfile);
			return (0);
		}
		token = next_token(&val, cfile);
	} while (token == COMMA);
	if (token != SEMI) {
		parse_warn("expecting semicolon.");
		skip_to_semi(cfile);
		return (0);
	}
	return (ix);
}

/*
 * interface-declaration :==
 *	INTERFACE string LBRACE client-declarations RBRACE
 */
void
parse_interface_declaration(FILE *cfile, struct client_config *outer_config)
{
	int			 token;
	char			*val;
	struct interface_info	*ip;

	token = next_token(&val, cfile);
	if (token != STRING) {
		parse_warn("expecting interface name (in quotes).");
		skip_to_semi(cfile);
		return;
	}

	ip = interface_or_dummy(val);

	if (!ip->client)
		make_client_state(ip);

	if (!ip->client->config)
		make_client_config(ip, outer_config);

	token = next_token(&val, cfile);
	if (token != LBRACE) {
		parse_warn("expecting left brace.");
		skip_to_semi(cfile);
		return;
	}

	do {
		token = peek_token(&val, cfile);
		if (token == EOF) {
			parse_warn("unterminated interface declaration.");
			return;
		}
		if (token == RBRACE)
			break;
		parse_client_statement(cfile, ip, ip->client->config);
	} while (1);
	token = next_token(&val, cfile);
}

struct interface_info *
interface_or_dummy(char *name)
{
	struct interface_info	*ip;

	/* Find the interface (if any) that matches the name. */
	if (!strcmp(ifi->name, name))
		return (ifi);

	/* If it's not a real interface, see if it's on the dummy list. */
	for (ip = dummy_interfaces; ip; ip = ip->next)
		if (!strcmp(ip->name, name))
			return (ip);

	/*
	 * If we didn't find an interface, make a dummy interface as a
	 * placeholder.
	 */
	ip = malloc(sizeof(*ip));
	if (!ip)
		error("Insufficient memory to record interface %s", name);
	memset(ip, 0, sizeof(*ip));
	strlcpy(ip->name, name, IFNAMSIZ);
	ip->next = dummy_interfaces;
	dummy_interfaces = ip;
	return (ip);
}

void
make_client_state(struct interface_info *ip)
{
	ip->client = malloc(sizeof(*(ip->client)));
	if (!ip->client)
		error("no memory for state on %s", ip->name);
	memset(ip->client, 0, sizeof(*(ip->client)));
}

void
make_client_config(struct interface_info *ip, struct client_config *config)
{
	ip->client->config = malloc(sizeof(struct client_config));
	if (!ip->client->config)
		error("no memory for config for %s", ip->name);
	memset(ip->client->config, 0, sizeof(*(ip->client->config)));
	memcpy(ip->client->config, config, sizeof(*config));
}

/*
 * client-lease-statement :==
 *	RBRACE client-lease-declarations LBRACE
 *
 *	client-lease-declarations :==
 *		<nil> |
 *		client-lease-declaration |
 *		client-lease-declarations client-lease-declaration
 */
void
parse_client_lease_statement(FILE *cfile, int is_static)
{
	struct client_lease	*lease, *lp, *pl;
	struct interface_info	*ip;
	int			 token;
	char			*val;

	token = next_token(&val, cfile);
	if (token != LBRACE) {
		parse_warn("expecting left brace.");
		skip_to_semi(cfile);
		return;
	}

	lease = malloc(sizeof(struct client_lease));
	if (!lease)
		error("no memory for lease.");
	memset(lease, 0, sizeof(*lease));
	lease->is_static = is_static;

	ip = NULL;

	do {
		token = peek_token(&val, cfile);
		if (token == EOF) {
			parse_warn("unterminated lease declaration.");
			free_client_lease(lease);
			return;
		}
		if (token == RBRACE)
			break;
		parse_client_lease_declaration(cfile, lease, &ip);
	} while (1);
	token = next_token(&val, cfile);

	/* If the lease declaration didn't include an interface
	 * declaration that we recognized, it's of no use to us.
	 */
	if (!ip) {
		free_client_lease(lease);
		return;
	}

	/* Make sure there's a client state structure... */
	if (!ip->client)
		make_client_state(ip);

	/* If this is an alias lease, it doesn't need to be sorted in. */
	if (is_static == 2) {
		ip->client->alias = lease;
		return;
	}

	/*
	 * The new lease may supersede a lease that's not the active
	 * lease but is still on the lease list, so scan the lease list
	 * looking for a lease with the same address, and if we find it,
	 * toss it.
	 */
	pl = NULL;
	for (lp = ip->client->leases; lp; lp = lp->next) {
		if (lp->address.len == lease->address.len &&
		    !memcmp(lp->address.iabuf, lease->address.iabuf,
		    lease->address.len)) {
			if (pl)
				pl->next = lp->next;
			else
				ip->client->leases = lp->next;
			free_client_lease(lp);
			break;
		}
	}

	/*
	 * If this is a preloaded lease, just put it on the list of
	 * recorded leases - don't make it the active lease.
	 */
	if (is_static) {
		lease->next = ip->client->leases;
		ip->client->leases = lease;
		return;
	}

	/*
	 * The last lease in the lease file on a particular interface is
	 * the active lease for that interface.    Of course, we don't
	 * know what the last lease in the file is until we've parsed
	 * the whole file, so at this point, we assume that the lease we
	 * just parsed is the active lease for its interface.   If
	 * there's already an active lease for the interface, and this
	 * lease is for the same ip address, then we just toss the old
	 * active lease and replace it with this one.   If this lease is
	 * for a different address, then if the old active lease has
	 * expired, we dump it; if not, we put it on the list of leases
	 * for this interface which are still valid but no longer
	 * active.
	 */
	if (ip->client->active) {
		if (ip->client->active->expiry < cur_time)
			free_client_lease(ip->client->active);
		else if (ip->client->active->address.len ==
		    lease->address.len &&
		    !memcmp(ip->client->active->address.iabuf,
		    lease->address.iabuf, lease->address.len))
			free_client_lease(ip->client->active);
		else {
			ip->client->active->next = ip->client->leases;
			ip->client->leases = ip->client->active;
		}
	}
	ip->client->active = lease;

	/* Phew. */
}

/*
 * client-lease-declaration :==
 *	BOOTP |
 *	INTERFACE string |
 *	FIXED_ADDR ip_address |
 *	FILENAME string |
 *	SERVER_NAME string |
 *	OPTION option-decl |
 *	RENEW time-decl |
 *	REBIND time-decl |
 *	EXPIRE time-decl
 */
void
parse_client_lease_declaration(FILE *cfile, struct client_lease *lease,
    struct interface_info **ipp)
{
	int			 token;
	char			*val;
	struct interface_info	*ip;

	switch (next_token(&val, cfile)) {
	case BOOTP:
		lease->is_bootp = 1;
		break;
	case INTERFACE:
		token = next_token(&val, cfile);
		if (token != STRING) {
			parse_warn("expecting interface name (in quotes).");
			skip_to_semi(cfile);
			break;
		}
		ip = interface_or_dummy(val);
		*ipp = ip;
		break;
	case FIXED_ADDR:
		if (!parse_ip_addr(cfile, &lease->address))
			return;
		break;
	case MEDIUM:
		parse_string_list(cfile, &lease->medium, 0);
		return;
	case FILENAME:
		lease->filename = parse_string(cfile);
		return;
	case NEXT_SERVER:
		if (!parse_ip_addr(cfile, &lease->nextserver))
			return;
		break;
	case SERVER_NAME:
		lease->server_name = parse_string(cfile);
		return;
	case RENEW:
		lease->renewal = parse_date(cfile);
		return;
	case REBIND:
		lease->rebind = parse_date(cfile);
		return;
	case EXPIRE:
		lease->expiry = parse_date(cfile);
		return;
	case OPTION:
		parse_option_decl(cfile, lease->options);
		return;
	default:
		parse_warn("expecting lease declaration.");
		skip_to_semi(cfile);
		break;
	}
	token = next_token(&val, cfile);
	if (token != SEMI) {
		parse_warn("expecting semicolon.");
		skip_to_semi(cfile);
	}
}

struct option *
parse_option_decl(FILE *cfile, struct option_data *options)
{
	char		*val;
	int		 token;
	u_int8_t	 buf[4];
	u_int8_t	 hunkbuf[1024];
	unsigned	 hunkix = 0;
	char		*vendor;
	const char	*fmt;
	struct universe	*universe;
	struct option	*option;
	struct iaddr	 ip_addr;
	u_int8_t	*dp;
	unsigned	 len;
	int		 nul_term = 0;

	token = next_token(&val, cfile);
	if (!is_identifier(token)) {
		parse_warn("expecting identifier after option keyword.");
		if (token != SEMI)
			skip_to_semi(cfile);
		return (NULL);
	}
	if ((vendor = strdup(val)) == NULL)
		error("no memory for vendor information.");

	token = peek_token(&val, cfile);
	if (token == DOT) {
		/* Go ahead and take the DOT token... */
		token = next_token(&val, cfile);

		/* The next token should be an identifier... */
		token = next_token(&val, cfile);
		if (!is_identifier(token)) {
			parse_warn("expecting identifier after '.'");
			if (token != SEMI)
				skip_to_semi(cfile);
			free(vendor);
			return (NULL);
		}

		/* Look up the option name hash table for the specified
		   vendor. */
		universe = ((struct universe *)hash_lookup(&universe_hash,
		    (unsigned char *)vendor, 0));
		/* If it's not there, we can't parse the rest of the
		   declaration. */
		if (!universe) {
			parse_warn("no vendor named %s.", vendor);
			skip_to_semi(cfile);
			free(vendor);
			return (NULL);
		}
	} else {
		/* Use the default hash table, which contains all the
		   standard dhcp option names. */
		val = vendor;
		universe = &dhcp_universe;
	}

	/* Look up the actual option info... */
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
		return (NULL);
	}

	/* Free the initial identifier token. */
	free(vendor);

	/* Parse the option data... */
	do {
		for (fmt = option->format; *fmt; fmt++) {
			if (*fmt == 'A')
				break;
			switch (*fmt) {
			case 'X':
				len = parse_X(cfile, &hunkbuf[hunkix],
				    sizeof(hunkbuf) - hunkix);
				hunkix += len;
				break;
			case 't': /* Text string... */
				token = next_token(&val, cfile);
				if (token != STRING) {
					parse_warn("expecting string.");
					skip_to_semi(cfile);
					return (NULL);
				}
				len = strlen(val);
				if (hunkix + len + 1 > sizeof(hunkbuf)) {
					parse_warn("option data buffer %s",
					    "overflow");
					skip_to_semi(cfile);
					return (NULL);
				}
				memcpy(&hunkbuf[hunkix], val, len + 1);
				nul_term = 1;
				hunkix += len;
				break;
			case 'I': /* IP address. */
				if (!parse_ip_addr(cfile, &ip_addr))
					return (NULL);
				len = ip_addr.len;
				dp = ip_addr.iabuf;
alloc:
				if (hunkix + len > sizeof(hunkbuf)) {
					parse_warn("option data buffer "
					    "overflow");
					skip_to_semi(cfile);
					return (NULL);
				}
				memcpy(&hunkbuf[hunkix], dp, len);
				hunkix += len;
				break;
			case 'L':	/* Unsigned 32-bit integer... */
			case 'l':	/* Signed 32-bit integer... */
				token = next_token(&val, cfile);
				if (token != NUMBER) {
need_number:
					parse_warn("expecting number.");
					if (token != SEMI)
						skip_to_semi(cfile);
					return (NULL);
				}
				convert_num(buf, val, 0, 32);
				len = 4;
				dp = buf;
				goto alloc;
			case 's':	/* Signed 16-bit integer. */
			case 'S':	/* Unsigned 16-bit integer. */
				token = next_token(&val, cfile);
				if (token != NUMBER)
					goto need_number;
				convert_num(buf, val, 0, 16);
				len = 2;
				dp = buf;
				goto alloc;
			case 'b':	/* Signed 8-bit integer. */
			case 'B':	/* Unsigned 8-bit integer. */
				token = next_token(&val, cfile);
				if (token != NUMBER)
					goto need_number;
				convert_num(buf, val, 0, 8);
				len = 1;
				dp = buf;
				goto alloc;
			case 'f': /* Boolean flag. */
				token = next_token(&val, cfile);
				if (!is_identifier(token)) {
					parse_warn("expecting identifier.");
bad_flag:
					if (token != SEMI)
						skip_to_semi(cfile);
					return (NULL);
				}
				if (!strcasecmp(val, "true") ||
				    !strcasecmp(val, "on"))
					buf[0] = 1;
				else if (!strcasecmp(val, "false") ||
				    !strcasecmp(val, "off"))
					buf[0] = 0;
				else {
					parse_warn("expecting boolean.");
					goto bad_flag;
				}
				len = 1;
				dp = buf;
				goto alloc;
			default:
				warning("Bad format %c in parse_option_param.",
				    *fmt);
				skip_to_semi(cfile);
				return (NULL);
			}
		}
		token = next_token(&val, cfile);
	} while (*fmt == 'A' && token == COMMA);

	if (token != SEMI) {
		parse_warn("semicolon expected.");
		skip_to_semi(cfile);
		return (NULL);
	}

	options[option->code].data = malloc(hunkix + nul_term);
	if (!options[option->code].data)
		error("out of memory allocating option data.");
	memcpy(options[option->code].data, hunkbuf, hunkix + nul_term);
	options[option->code].len = hunkix;
	return (option);
}

void
parse_string_list(FILE *cfile, struct string_list **lp, int multiple)
{
	int			 token;
	char			*val;
	size_t			 valsize;
	struct string_list	*cur, *tmp;

	/* Find the last medium in the media list. */
	if (*lp)
		for (cur = *lp; cur->next; cur = cur->next)
			;	/* nothing */
	else
		cur = NULL;

	do {
		token = next_token(&val, cfile);
		if (token != STRING) {
			parse_warn("Expecting media options.");
			skip_to_semi(cfile);
			return;
		}

		valsize = strlen(val) + 1;
		tmp = new_string_list(valsize);
		if (tmp == NULL)
			error("no memory for string list entry.");
		memcpy(tmp->string, val, valsize);
		tmp->next = NULL;

		/* Store this medium at the end of the media list. */
		if (cur)
			cur->next = tmp;
		else
			*lp = tmp;
		cur = tmp;

		token = next_token(&val, cfile);
	} while (multiple && token == COMMA);

	if (token != SEMI) {
		parse_warn("expecting semicolon.");
		skip_to_semi(cfile);
	}
}

void
parse_reject_statement(FILE *cfile, struct client_config *config)
{
	int			 token;
	char			*val;
	struct iaddr		 addr;
	struct iaddrlist	*list;

	do {
		if (!parse_ip_addr(cfile, &addr)) {
			parse_warn("expecting IP address.");
			skip_to_semi(cfile);
			return;
		}

		list = malloc(sizeof(struct iaddrlist));
		if (!list)
			error("no memory for reject list!");

		list->addr = addr;
		list->next = config->reject_list;
		config->reject_list = list;

		token = next_token(&val, cfile);
	} while (token == COMMA);

	if (token != SEMI) {
		parse_warn("expecting semicolon.");
		skip_to_semi(cfile);
	}
}
