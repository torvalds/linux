/*	$OpenBSD: print.c,v 1.6 2024/12/26 18:25:51 sthen Exp $	*/

/*
 * Copyright (c) 2019-2021 Tobias Heider <tobias.heider@stusta.de>
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <net/if.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <event.h>

#include "iked.h"
#include "ikev2.h"
#include "eap.h"

const char *
print_xf(unsigned int id, unsigned int length, const struct ipsec_xf xfs[])
{
	int		i;

	for (i = 0; xfs[i].name != NULL; i++) {
		if (xfs[i].id == id) {
			if (length == 0 || length == xfs[i].length)
				return (xfs[i].name);
		}
	}
	return ("unknown");
}

void
print_user(struct iked_user *usr)
{
	print_verbose("user \"%s\" \"%s\"\n", usr->usr_name, usr->usr_pass);
}

void
print_policy(struct iked_policy *pol)
{
	struct iked_proposal	*pp;
	struct iked_transform	*xform;
	struct iked_flow	*flow;
	struct iked_cfg		*cfg;
	unsigned int		 i, j;
	const struct ipsec_xf	*xfs = NULL;
	char			 iface[IF_NAMESIZE];

	print_verbose("ikev2");

	if (pol->pol_name[0] != '\0')
		print_verbose(" \"%s\"", pol->pol_name);

	if (pol->pol_flags & IKED_POLICY_DEFAULT)
		print_verbose(" default");
	else if (pol->pol_flags & IKED_POLICY_QUICK)
		print_verbose(" quick");
	else if (pol->pol_flags & IKED_POLICY_SKIP)
		print_verbose(" skip");

	if (pol->pol_flags & IKED_POLICY_ACTIVE)
		print_verbose(" active");
	else
		print_verbose(" passive");

	if (pol->pol_flags & IKED_POLICY_IPCOMP)
		print_verbose(" ipcomp");

	if (pol->pol_flags & IKED_POLICY_TRANSPORT)
		print_verbose(" transport");
	else
		print_verbose(" tunnel");

	if (pol->pol_flags & IKED_POLICY_NATT_FORCE)
		print_verbose(" natt");

	print_verbose(" %s", print_xf(pol->pol_saproto, 0, saxfs));

	if (pol->pol_nipproto > 0) {
		print_verbose(" proto {");
		for (i = 0; i < pol->pol_nipproto; i++) {
			if (i == 0)
				print_verbose(" %s", print_proto(pol->pol_ipproto[i]));
			else
				print_verbose(", %s", print_proto(pol->pol_ipproto[i]));
		}
		print_verbose(" }");
	}

	if (pol->pol_af) {
		if (pol->pol_af == AF_INET)
			print_verbose(" inet");
		else
			print_verbose(" inet6");
	}

	if (pol->pol_rdomain >= 0)
		print_verbose(" rdomain %d", pol->pol_rdomain);

	RB_FOREACH(flow, iked_flows, &pol->pol_flows) {
		print_verbose(" from %s", print_addr(&flow->flow_src.addr));
		if (flow->flow_src.addr_af != AF_UNSPEC &&
		    flow->flow_src.addr_net)
			print_verbose("/%d", flow->flow_src.addr_mask);
		if (flow->flow_src.addr_port)
			print_verbose(" port %d",
			    ntohs(flow->flow_src.addr_port));

		print_verbose(" to %s", print_addr(&flow->flow_dst.addr));
		if (flow->flow_dst.addr_af != AF_UNSPEC &&
		    flow->flow_dst.addr_net)
			print_verbose("/%d", flow->flow_dst.addr_mask);
		if (flow->flow_dst.addr_port)
			print_verbose(" port %d",
			    ntohs(flow->flow_dst.addr_port));
	}

	if ((pol->pol_flags & IKED_POLICY_DEFAULT) == 0) {
		print_verbose(" local %s", print_addr(&pol->pol_local.addr));
		if (pol->pol_local.addr.ss_family != AF_UNSPEC &&
		    pol->pol_local.addr_net)
			print_verbose("/%d", pol->pol_local.addr_mask);

		print_verbose(" peer %s", print_addr(&pol->pol_peer.addr));
		if (pol->pol_peer.addr.ss_family != AF_UNSPEC &&
		    pol->pol_peer.addr_net)
			print_verbose("/%d", pol->pol_peer.addr_mask);
	}

	TAILQ_FOREACH(pp, &pol->pol_proposals, prop_entry) {
		if (!pp->prop_nxforms)
			continue;
		if (pp->prop_protoid == IKEV2_SAPROTO_IKE)
			print_verbose(" ikesa");
		else
			print_verbose(" childsa");

		for (j = 0; ikev2_xformtype_map[j].cm_type != 0; j++) {
			xfs = NULL;

			for (i = 0; i < pp->prop_nxforms; i++) {
				xform = pp->prop_xforms + i;

				if (xform->xform_type !=
				    ikev2_xformtype_map[j].cm_type)
					continue;

				switch (xform->xform_type) {
				case IKEV2_XFORMTYPE_INTEGR:
					print_verbose(" auth ");
					xfs = authxfs;
					break;
				case IKEV2_XFORMTYPE_ENCR:
					print_verbose(" enc ");
					if (pp->prop_protoid ==
					    IKEV2_SAPROTO_IKE)
						xfs = ikeencxfs;
					else
						xfs = ipsecencxfs;
					break;
				case IKEV2_XFORMTYPE_PRF:
					print_verbose(" prf ");
					xfs = prfxfs;
					break;
				case IKEV2_XFORMTYPE_DH:
					print_verbose(" group ");
					xfs = groupxfs;
					break;
				case IKEV2_XFORMTYPE_ESN:
					print_verbose(" ");
					xfs = esnxfs;
					break;
				default:
					continue;
				}

				print_verbose("%s", print_xf(xform->xform_id,
				    xform->xform_length / 8, xfs));
			}
		}
	}

	if (pol->pol_localid.id_length != 0)
		print_verbose(" srcid %s", pol->pol_localid.id_data);
	if (pol->pol_peerid.id_length != 0)
		print_verbose(" dstid %s", pol->pol_peerid.id_data);

	if (pol->pol_rekey)
		print_verbose(" ikelifetime %u", pol->pol_rekey);

	print_verbose(" lifetime %llu bytes %llu",
	    pol->pol_lifetime.lt_seconds, pol->pol_lifetime.lt_bytes);

	switch (pol->pol_auth.auth_method) {
	case IKEV2_AUTH_NONE:
		print_verbose (" none");
		break;
	case IKEV2_AUTH_SHARED_KEY_MIC:
		print_verbose(" psk 0x");
		for (i = 0; i < pol->pol_auth.auth_length; i++)
			print_verbose("%02x", pol->pol_auth.auth_data[i]);
		break;
	default:
		if (pol->pol_auth.auth_eap)
			print_verbose(" eap \"%s\"",
			    print_map(pol->pol_auth.auth_eap, eap_type_map));
		else
			print_verbose(" %s",
			    print_xf(pol->pol_auth.auth_method, 0, methodxfs));
	}

	for (i = 0; i < pol->pol_ncfg; i++) {
		cfg = &pol->pol_cfg[i];
		print_verbose(" %s %s %s",
		    cfg->cfg_action == IKEV2_CP_REPLY ? "config" : "request",
		    print_xf(cfg->cfg_type,
		    cfg->cfg.address.addr_af, cpxfs),
		    print_addr(&cfg->cfg.address.addr));
	}

	if (pol->pol_iface != 0 && if_indextoname(pol->pol_iface, iface) != NULL)
		print_verbose(" iface %s", iface);

	if (pol->pol_tag[0] != '\0')
		print_verbose(" tag \"%s\"", pol->pol_tag);

	if (pol->pol_tap != 0)
		print_verbose(" tap \"enc%u\"", pol->pol_tap);

	print_verbose("\n");
}
