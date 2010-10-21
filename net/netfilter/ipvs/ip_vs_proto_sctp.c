#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/sctp.h>
#include <net/ip.h>
#include <net/ip6_checksum.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <net/sctp/checksum.h>
#include <net/ip_vs.h>

static int
sctp_conn_schedule(int af, struct sk_buff *skb, struct ip_vs_protocol *pp,
		   int *verdict, struct ip_vs_conn **cpp)
{
	struct ip_vs_service *svc;
	sctp_chunkhdr_t _schunkh, *sch;
	sctp_sctphdr_t *sh, _sctph;
	struct ip_vs_iphdr iph;

	ip_vs_fill_iphdr(af, skb_network_header(skb), &iph);

	sh = skb_header_pointer(skb, iph.len, sizeof(_sctph), &_sctph);
	if (sh == NULL)
		return 0;

	sch = skb_header_pointer(skb, iph.len + sizeof(sctp_sctphdr_t),
				 sizeof(_schunkh), &_schunkh);
	if (sch == NULL)
		return 0;

	if ((sch->type == SCTP_CID_INIT) &&
	    (svc = ip_vs_service_get(af, skb->mark, iph.protocol,
				     &iph.daddr, sh->dest))) {
		int ignored;

		if (ip_vs_todrop()) {
			/*
			 * It seems that we are very loaded.
			 * We have to drop this packet :(
			 */
			ip_vs_service_put(svc);
			*verdict = NF_DROP;
			return 0;
		}
		/*
		 * Let the virtual server select a real server for the
		 * incoming connection, and create a connection entry.
		 */
		*cpp = ip_vs_schedule(svc, skb, pp, &ignored);
		if (!*cpp && !ignored) {
			*verdict = ip_vs_leave(svc, skb, pp);
			return 0;
		}
		ip_vs_service_put(svc);
	}

	return 1;
}

static int
sctp_snat_handler(struct sk_buff *skb,
		  struct ip_vs_protocol *pp, struct ip_vs_conn *cp)
{
	sctp_sctphdr_t *sctph;
	unsigned int sctphoff;
	struct sk_buff *iter;
	__be32 crc32;

#ifdef CONFIG_IP_VS_IPV6
	if (cp->af == AF_INET6)
		sctphoff = sizeof(struct ipv6hdr);
	else
#endif
		sctphoff = ip_hdrlen(skb);

	/* csum_check requires unshared skb */
	if (!skb_make_writable(skb, sctphoff + sizeof(*sctph)))
		return 0;

	if (unlikely(cp->app != NULL)) {
		/* Some checks before mangling */
		if (pp->csum_check && !pp->csum_check(cp->af, skb, pp))
			return 0;

		/* Call application helper if needed */
		if (!ip_vs_app_pkt_out(cp, skb))
			return 0;
	}

	sctph = (void *) skb_network_header(skb) + sctphoff;
	sctph->source = cp->vport;

	/* Calculate the checksum */
	crc32 = sctp_start_cksum((u8 *) sctph, skb_headlen(skb) - sctphoff);
	skb_walk_frags(skb, iter)
		crc32 = sctp_update_cksum((u8 *) iter->data, skb_headlen(iter),
				          crc32);
	crc32 = sctp_end_cksum(crc32);
	sctph->checksum = crc32;

	return 1;
}

static int
sctp_dnat_handler(struct sk_buff *skb,
		  struct ip_vs_protocol *pp, struct ip_vs_conn *cp)
{
	sctp_sctphdr_t *sctph;
	unsigned int sctphoff;
	struct sk_buff *iter;
	__be32 crc32;

#ifdef CONFIG_IP_VS_IPV6
	if (cp->af == AF_INET6)
		sctphoff = sizeof(struct ipv6hdr);
	else
#endif
		sctphoff = ip_hdrlen(skb);

	/* csum_check requires unshared skb */
	if (!skb_make_writable(skb, sctphoff + sizeof(*sctph)))
		return 0;

	if (unlikely(cp->app != NULL)) {
		/* Some checks before mangling */
		if (pp->csum_check && !pp->csum_check(cp->af, skb, pp))
			return 0;

		/* Call application helper if needed */
		if (!ip_vs_app_pkt_in(cp, skb))
			return 0;
	}

	sctph = (void *) skb_network_header(skb) + sctphoff;
	sctph->dest = cp->dport;

	/* Calculate the checksum */
	crc32 = sctp_start_cksum((u8 *) sctph, skb_headlen(skb) - sctphoff);
	skb_walk_frags(skb, iter)
		crc32 = sctp_update_cksum((u8 *) iter->data, skb_headlen(iter),
					  crc32);
	crc32 = sctp_end_cksum(crc32);
	sctph->checksum = crc32;

	return 1;
}

static int
sctp_csum_check(int af, struct sk_buff *skb, struct ip_vs_protocol *pp)
{
	unsigned int sctphoff;
	struct sctphdr *sh, _sctph;
	struct sk_buff *iter;
	__le32 cmp;
	__le32 val;
	__u32 tmp;

#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		sctphoff = sizeof(struct ipv6hdr);
	else
#endif
		sctphoff = ip_hdrlen(skb);

	sh = skb_header_pointer(skb, sctphoff, sizeof(_sctph), &_sctph);
	if (sh == NULL)
		return 0;

	cmp = sh->checksum;

	tmp = sctp_start_cksum((__u8 *) sh, skb_headlen(skb));
	skb_walk_frags(skb, iter)
		tmp = sctp_update_cksum((__u8 *) iter->data,
					skb_headlen(iter), tmp);

	val = sctp_end_cksum(tmp);

	if (val != cmp) {
		/* CRC failure, dump it. */
		IP_VS_DBG_RL_PKT(0, af, pp, skb, 0,
				"Failed checksum for");
		return 0;
	}
	return 1;
}

struct ipvs_sctp_nextstate {
	int next_state;
};
enum ipvs_sctp_event_t {
	IP_VS_SCTP_EVE_DATA_CLI,
	IP_VS_SCTP_EVE_DATA_SER,
	IP_VS_SCTP_EVE_INIT_CLI,
	IP_VS_SCTP_EVE_INIT_SER,
	IP_VS_SCTP_EVE_INIT_ACK_CLI,
	IP_VS_SCTP_EVE_INIT_ACK_SER,
	IP_VS_SCTP_EVE_COOKIE_ECHO_CLI,
	IP_VS_SCTP_EVE_COOKIE_ECHO_SER,
	IP_VS_SCTP_EVE_COOKIE_ACK_CLI,
	IP_VS_SCTP_EVE_COOKIE_ACK_SER,
	IP_VS_SCTP_EVE_ABORT_CLI,
	IP_VS_SCTP_EVE__ABORT_SER,
	IP_VS_SCTP_EVE_SHUT_CLI,
	IP_VS_SCTP_EVE_SHUT_SER,
	IP_VS_SCTP_EVE_SHUT_ACK_CLI,
	IP_VS_SCTP_EVE_SHUT_ACK_SER,
	IP_VS_SCTP_EVE_SHUT_COM_CLI,
	IP_VS_SCTP_EVE_SHUT_COM_SER,
	IP_VS_SCTP_EVE_LAST
};

static enum ipvs_sctp_event_t sctp_events[255] = {
	IP_VS_SCTP_EVE_DATA_CLI,
	IP_VS_SCTP_EVE_INIT_CLI,
	IP_VS_SCTP_EVE_INIT_ACK_CLI,
	IP_VS_SCTP_EVE_DATA_CLI,
	IP_VS_SCTP_EVE_DATA_CLI,
	IP_VS_SCTP_EVE_DATA_CLI,
	IP_VS_SCTP_EVE_ABORT_CLI,
	IP_VS_SCTP_EVE_SHUT_CLI,
	IP_VS_SCTP_EVE_SHUT_ACK_CLI,
	IP_VS_SCTP_EVE_DATA_CLI,
	IP_VS_SCTP_EVE_COOKIE_ECHO_CLI,
	IP_VS_SCTP_EVE_COOKIE_ACK_CLI,
	IP_VS_SCTP_EVE_DATA_CLI,
	IP_VS_SCTP_EVE_DATA_CLI,
	IP_VS_SCTP_EVE_SHUT_COM_CLI,
};

static struct ipvs_sctp_nextstate
 sctp_states_table[IP_VS_SCTP_S_LAST][IP_VS_SCTP_EVE_LAST] = {
	/*
	 * STATE : IP_VS_SCTP_S_NONE
	 */
	/*next state *//*event */
	{{IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_SER */ },
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_INIT_CLI */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_INIT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_INIT_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_INIT_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ECHO_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ECHO_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_SER */ },
	 },
	/*
	 * STATE : IP_VS_SCTP_S_INIT_CLI
	 * Cient sent INIT and is waiting for reply from server(In ECHO_WAIT)
	 */
	{{IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_SER */ },
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_INIT_CLI */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_INIT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_INIT_ACK_CLI */ },
	 {IP_VS_SCTP_S_INIT_ACK_SER /* IP_VS_SCTP_EVE_INIT_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ECHO_CLI */ },
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_ECHO_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ACK_CLI */ },
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_COOKIE_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_SER */ }
	 },
	/*
	 * State : IP_VS_SCTP_S_INIT_SER
	 * Server sent INIT and waiting for INIT ACK from the client
	 */
	{{IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_SER */ },
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_INIT_CLI */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_INIT_SER */ },
	 {IP_VS_SCTP_S_INIT_ACK_CLI /* IP_VS_SCTP_EVE_INIT_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_INIT_ACK_SER */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_COOKIE_ECHO_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ECHO_SER */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_COOKIE_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_SER */ }
	 },
	/*
	 * State : IP_VS_SCTP_S_INIT_ACK_CLI
	 * Client sent INIT ACK and waiting for ECHO from the server
	 */
	{{IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_SER */ },
	 /*
	  * We have got an INIT from client. From the spec.“Upon receipt of
	  * an INIT in the COOKIE-WAIT state, an endpoint MUST respond with
	  * an INIT ACK using the same parameters it sent in its  original
	  * INIT chunk (including its Initiate Tag, unchanged”).
	  */
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_INIT_CLI */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_INIT_SER */ },
	 /*
	  * INIT_ACK has been resent by the client, let us stay is in
	  * the same state
	  */
	 {IP_VS_SCTP_S_INIT_ACK_CLI /* IP_VS_SCTP_EVE_INIT_ACK_CLI */ },
	 /*
	  * INIT_ACK sent by the server, close the connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_INIT_ACK_SER */ },
	 /*
	  * ECHO by client, it should not happen, close the connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ECHO_CLI */ },
	 /*
	  * ECHO by server, this is what we are expecting, move to ECHO_SER
	  */
	 {IP_VS_SCTP_S_ECHO_SER /* IP_VS_SCTP_EVE_COOKIE_ECHO_SER */ },
	 /*
	  * COOKIE ACK from client, it should not happen, close the connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ACK_CLI */ },
	 /*
	  * Unexpected COOKIE ACK from server, staty in the same state
	  */
	 {IP_VS_SCTP_S_INIT_ACK_CLI /* IP_VS_SCTP_EVE_COOKIE_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_SER */ }
	 },
	/*
	 * State : IP_VS_SCTP_S_INIT_ACK_SER
	 * Server sent INIT ACK and waiting for ECHO from the client
	 */
	{{IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_SER */ },
	 /*
	  * We have got an INIT from client. From the spec.“Upon receipt of
	  * an INIT in the COOKIE-WAIT state, an endpoint MUST respond with
	  * an INIT ACK using the same parameters it sent in its  original
	  * INIT chunk (including its Initiate Tag, unchanged”).
	  */
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_INIT_CLI */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_INIT_SER */ },
	 /*
	  * Unexpected INIT_ACK by the client, let us close the connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_INIT_ACK_CLI */ },
	 /*
	  * INIT_ACK resent by the server, let us move to same state
	  */
	 {IP_VS_SCTP_S_INIT_ACK_SER /* IP_VS_SCTP_EVE_INIT_ACK_SER */ },
	 /*
	  * Client send the ECHO, this is what we are expecting,
	  * move to ECHO_CLI
	  */
	 {IP_VS_SCTP_S_ECHO_CLI /* IP_VS_SCTP_EVE_COOKIE_ECHO_CLI */ },
	 /*
	  * ECHO received from the server, Not sure what to do,
	  * let us close it
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ECHO_SER */ },
	 /*
	  * COOKIE ACK from client, let us stay in the same state
	  */
	 {IP_VS_SCTP_S_INIT_ACK_SER /* IP_VS_SCTP_EVE_COOKIE_ACK_CLI */ },
	 /*
	  * COOKIE ACK from server, hmm... this should not happen, lets close
	  * the connection.
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_SER */ }
	 },
	/*
	 * State : IP_VS_SCTP_S_ECHO_CLI
	 * Cient  sent ECHO and waiting COOKEI ACK from the Server
	 */
	{{IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_SER */ },
	 /*
	  * We have got an INIT from client. From the spec.“Upon receipt of
	  * an INIT in the COOKIE-WAIT state, an endpoint MUST respond with
	  * an INIT ACK using the same parameters it sent in its  original
	  * INIT chunk (including its Initiate Tag, unchanged”).
	  */
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_INIT_CLI */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_INIT_SER */ },
	 /*
	  * INIT_ACK has been by the client, let us close the connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_INIT_ACK_CLI */ },
	 /*
	  * INIT_ACK sent by the server, Unexpected INIT ACK, spec says,
	  * “If an INIT ACK is received by an endpoint in any state other
	  * than the COOKIE-WAIT state, the endpoint should discard the
	  * INIT ACK chunk”. Stay in the same state
	  */
	 {IP_VS_SCTP_S_ECHO_CLI /* IP_VS_SCTP_EVE_INIT_ACK_SER */ },
	 /*
	  * Client resent the ECHO, let us stay in the same state
	  */
	 {IP_VS_SCTP_S_ECHO_CLI /* IP_VS_SCTP_EVE_COOKIE_ECHO_CLI */ },
	 /*
	  * ECHO received from the server, Not sure what to do,
	  * let us close it
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ECHO_SER */ },
	 /*
	  * COOKIE ACK from client, this shoud not happen, let's close the
	  * connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ACK_CLI */ },
	 /*
	  * COOKIE ACK from server, this is what we are awaiting,lets move to
	  * ESTABLISHED.
	  */
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_COOKIE_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_SER */ }
	 },
	/*
	 * State : IP_VS_SCTP_S_ECHO_SER
	 * Server sent ECHO and waiting COOKEI ACK from the client
	 */
	{{IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_SER */ },
	 /*
	  * We have got an INIT from client. From the spec.“Upon receipt of
	  * an INIT in the COOKIE-WAIT state, an endpoint MUST respond with
	  * an INIT ACK using the same parameters it sent in its  original
	  * INIT chunk (including its Initiate Tag, unchanged”).
	  */
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_INIT_CLI */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_INIT_SER */ },
	 /*
	  * INIT_ACK sent by the server, Unexpected INIT ACK, spec says,
	  * “If an INIT ACK is received by an endpoint in any state other
	  * than the COOKIE-WAIT state, the endpoint should discard the
	  * INIT ACK chunk”. Stay in the same state
	  */
	 {IP_VS_SCTP_S_ECHO_SER /* IP_VS_SCTP_EVE_INIT_ACK_CLI */ },
	 /*
	  * INIT_ACK has been by the server, let us close the connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_INIT_ACK_SER */ },
	 /*
	  * Client sent the ECHO, not sure what to do, let's close the
	  * connection.
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ECHO_CLI */ },
	 /*
	  * ECHO resent by the server, stay in the same state
	  */
	 {IP_VS_SCTP_S_ECHO_SER /* IP_VS_SCTP_EVE_COOKIE_ECHO_SER */ },
	 /*
	  * COOKIE ACK from client, this is what we are expecting, let's move
	  * to ESTABLISHED.
	  */
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_COOKIE_ACK_CLI */ },
	 /*
	  * COOKIE ACK from server, this should not happen, lets close the
	  * connection.
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_SER */ }
	 },
	/*
	 * State : IP_VS_SCTP_S_ESTABLISHED
	 * Association established
	 */
	{{IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_DATA_CLI */ },
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_DATA_SER */ },
	 /*
	  * We have got an INIT from client. From the spec.“Upon receipt of
	  * an INIT in the COOKIE-WAIT state, an endpoint MUST respond with
	  * an INIT ACK using the same parameters it sent in its  original
	  * INIT chunk (including its Initiate Tag, unchanged”).
	  */
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_INIT_CLI */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_INIT_SER */ },
	 /*
	  * INIT_ACK sent by the server, Unexpected INIT ACK, spec says,
	  * “If an INIT ACK is received by an endpoint in any state other
	  * than the COOKIE-WAIT state, the endpoint should discard the
	  * INIT ACK chunk”. Stay in the same state
	  */
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_INIT_ACK_CLI */ },
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_INIT_ACK_SER */ },
	 /*
	  * Client sent ECHO, Spec(sec 5.2.4) says it may be handled by the
	  * peer and peer shall move to the ESTABISHED. if it doesn't handle
	  * it will send ERROR chunk. So, stay in the same state
	  */
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_COOKIE_ECHO_CLI */ },
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_COOKIE_ECHO_SER */ },
	 /*
	  * COOKIE ACK from client, not sure what to do stay in the same state
	  */
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_COOKIE_ACK_CLI */ },
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_COOKIE_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_SER */ },
	 /*
	  * SHUTDOWN from the client, move to SHUDDOWN_CLI
	  */
	 {IP_VS_SCTP_S_SHUT_CLI /* IP_VS_SCTP_EVE_SHUT_CLI */ },
	 /*
	  * SHUTDOWN from the server, move to SHUTDOWN_SER
	  */
	 {IP_VS_SCTP_S_SHUT_SER /* IP_VS_SCTP_EVE_SHUT_SER */ },
	 /*
	  * client sent SHUDTDOWN_ACK, this should not happen, let's close
	  * the connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_SER */ }
	 },
	/*
	 * State : IP_VS_SCTP_S_SHUT_CLI
	 * SHUTDOWN sent from the client, waitinf for SHUT ACK from the server
	 */
	/*
	 * We recieved the data chuck, keep the state unchanged. I assume
	 * that still data chuncks  can be received by both the peers in
	 * SHUDOWN state
	 */

	{{IP_VS_SCTP_S_SHUT_CLI /* IP_VS_SCTP_EVE_DATA_CLI */ },
	 {IP_VS_SCTP_S_SHUT_CLI /* IP_VS_SCTP_EVE_DATA_SER */ },
	 /*
	  * We have got an INIT from client. From the spec.“Upon receipt of
	  * an INIT in the COOKIE-WAIT state, an endpoint MUST respond with
	  * an INIT ACK using the same parameters it sent in its  original
	  * INIT chunk (including its Initiate Tag, unchanged”).
	  */
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_INIT_CLI */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_INIT_SER */ },
	 /*
	  * INIT_ACK sent by the server, Unexpected INIT ACK, spec says,
	  * “If an INIT ACK is received by an endpoint in any state other
	  * than the COOKIE-WAIT state, the endpoint should discard the
	  * INIT ACK chunk”. Stay in the same state
	  */
	 {IP_VS_SCTP_S_SHUT_CLI /* IP_VS_SCTP_EVE_INIT_ACK_CLI */ },
	 {IP_VS_SCTP_S_SHUT_CLI /* IP_VS_SCTP_EVE_INIT_ACK_SER */ },
	 /*
	  * Client sent ECHO, Spec(sec 5.2.4) says it may be handled by the
	  * peer and peer shall move to the ESTABISHED. if it doesn't handle
	  * it will send ERROR chunk. So, stay in the same state
	  */
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_COOKIE_ECHO_CLI */ },
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_COOKIE_ECHO_SER */ },
	 /*
	  * COOKIE ACK from client, not sure what to do stay in the same state
	  */
	 {IP_VS_SCTP_S_SHUT_CLI /* IP_VS_SCTP_EVE_COOKIE_ACK_CLI */ },
	 {IP_VS_SCTP_S_SHUT_CLI /* IP_VS_SCTP_EVE_COOKIE_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_SER */ },
	 /*
	  * SHUTDOWN resent from the client, move to SHUDDOWN_CLI
	  */
	 {IP_VS_SCTP_S_SHUT_CLI /* IP_VS_SCTP_EVE_SHUT_CLI */ },
	 /*
	  * SHUTDOWN from the server, move to SHUTDOWN_SER
	  */
	 {IP_VS_SCTP_S_SHUT_SER /* IP_VS_SCTP_EVE_SHUT_SER */ },
	 /*
	  * client sent SHUDTDOWN_ACK, this should not happen, let's close
	  * the connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_CLI */ },
	 /*
	  * Server sent SHUTDOWN ACK, this is what we are expecting, let's move
	  * to SHUDOWN_ACK_SER
	  */
	 {IP_VS_SCTP_S_SHUT_ACK_SER /* IP_VS_SCTP_EVE_SHUT_ACK_SER */ },
	 /*
	  * SHUTDOWN COM from client, this should not happen, let's close the
	  * connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_SER */ }
	 },
	/*
	 * State : IP_VS_SCTP_S_SHUT_SER
	 * SHUTDOWN sent from the server, waitinf for SHUTDOWN ACK from client
	 */
	/*
	 * We recieved the data chuck, keep the state unchanged. I assume
	 * that still data chuncks  can be received by both the peers in
	 * SHUDOWN state
	 */

	{{IP_VS_SCTP_S_SHUT_SER /* IP_VS_SCTP_EVE_DATA_CLI */ },
	 {IP_VS_SCTP_S_SHUT_SER /* IP_VS_SCTP_EVE_DATA_SER */ },
	 /*
	  * We have got an INIT from client. From the spec.“Upon receipt of
	  * an INIT in the COOKIE-WAIT state, an endpoint MUST respond with
	  * an INIT ACK using the same parameters it sent in its  original
	  * INIT chunk (including its Initiate Tag, unchanged”).
	  */
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_INIT_CLI */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_INIT_SER */ },
	 /*
	  * INIT_ACK sent by the server, Unexpected INIT ACK, spec says,
	  * “If an INIT ACK is received by an endpoint in any state other
	  * than the COOKIE-WAIT state, the endpoint should discard the
	  * INIT ACK chunk”. Stay in the same state
	  */
	 {IP_VS_SCTP_S_SHUT_SER /* IP_VS_SCTP_EVE_INIT_ACK_CLI */ },
	 {IP_VS_SCTP_S_SHUT_SER /* IP_VS_SCTP_EVE_INIT_ACK_SER */ },
	 /*
	  * Client sent ECHO, Spec(sec 5.2.4) says it may be handled by the
	  * peer and peer shall move to the ESTABISHED. if it doesn't handle
	  * it will send ERROR chunk. So, stay in the same state
	  */
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_COOKIE_ECHO_CLI */ },
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_COOKIE_ECHO_SER */ },
	 /*
	  * COOKIE ACK from client, not sure what to do stay in the same state
	  */
	 {IP_VS_SCTP_S_SHUT_SER /* IP_VS_SCTP_EVE_COOKIE_ACK_CLI */ },
	 {IP_VS_SCTP_S_SHUT_SER /* IP_VS_SCTP_EVE_COOKIE_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_SER */ },
	 /*
	  * SHUTDOWN resent from the client, move to SHUDDOWN_CLI
	  */
	 {IP_VS_SCTP_S_SHUT_CLI /* IP_VS_SCTP_EVE_SHUT_CLI */ },
	 /*
	  * SHUTDOWN resent from the server, move to SHUTDOWN_SER
	  */
	 {IP_VS_SCTP_S_SHUT_SER /* IP_VS_SCTP_EVE_SHUT_SER */ },
	 /*
	  * client sent SHUDTDOWN_ACK, this is what we are expecting, let's
	  * move to SHUT_ACK_CLI
	  */
	 {IP_VS_SCTP_S_SHUT_ACK_CLI /* IP_VS_SCTP_EVE_SHUT_ACK_CLI */ },
	 /*
	  * Server sent SHUTDOWN ACK, this should not happen, let's close the
	  * connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_SER */ },
	 /*
	  * SHUTDOWN COM from client, this should not happen, let's close the
	  * connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_SER */ }
	 },

	/*
	 * State : IP_VS_SCTP_S_SHUT_ACK_CLI
	 * SHUTDOWN ACK from the client, awaiting for SHUTDOWN COM from server
	 */
	/*
	 * We recieved the data chuck, keep the state unchanged. I assume
	 * that still data chuncks  can be received by both the peers in
	 * SHUDOWN state
	 */

	{{IP_VS_SCTP_S_SHUT_ACK_CLI /* IP_VS_SCTP_EVE_DATA_CLI */ },
	 {IP_VS_SCTP_S_SHUT_ACK_CLI /* IP_VS_SCTP_EVE_DATA_SER */ },
	 /*
	  * We have got an INIT from client. From the spec.“Upon receipt of
	  * an INIT in the COOKIE-WAIT state, an endpoint MUST respond with
	  * an INIT ACK using the same parameters it sent in its  original
	  * INIT chunk (including its Initiate Tag, unchanged”).
	  */
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_INIT_CLI */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_INIT_SER */ },
	 /*
	  * INIT_ACK sent by the server, Unexpected INIT ACK, spec says,
	  * “If an INIT ACK is received by an endpoint in any state other
	  * than the COOKIE-WAIT state, the endpoint should discard the
	  * INIT ACK chunk”. Stay in the same state
	  */
	 {IP_VS_SCTP_S_SHUT_ACK_CLI /* IP_VS_SCTP_EVE_INIT_ACK_CLI */ },
	 {IP_VS_SCTP_S_SHUT_ACK_CLI /* IP_VS_SCTP_EVE_INIT_ACK_SER */ },
	 /*
	  * Client sent ECHO, Spec(sec 5.2.4) says it may be handled by the
	  * peer and peer shall move to the ESTABISHED. if it doesn't handle
	  * it will send ERROR chunk. So, stay in the same state
	  */
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_COOKIE_ECHO_CLI */ },
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_COOKIE_ECHO_SER */ },
	 /*
	  * COOKIE ACK from client, not sure what to do stay in the same state
	  */
	 {IP_VS_SCTP_S_SHUT_ACK_CLI /* IP_VS_SCTP_EVE_COOKIE_ACK_CLI */ },
	 {IP_VS_SCTP_S_SHUT_ACK_CLI /* IP_VS_SCTP_EVE_COOKIE_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_SER */ },
	 /*
	  * SHUTDOWN sent from the client, move to SHUDDOWN_CLI
	  */
	 {IP_VS_SCTP_S_SHUT_CLI /* IP_VS_SCTP_EVE_SHUT_CLI */ },
	 /*
	  * SHUTDOWN sent from the server, move to SHUTDOWN_SER
	  */
	 {IP_VS_SCTP_S_SHUT_SER /* IP_VS_SCTP_EVE_SHUT_SER */ },
	 /*
	  * client resent SHUDTDOWN_ACK, let's stay in the same state
	  */
	 {IP_VS_SCTP_S_SHUT_ACK_CLI /* IP_VS_SCTP_EVE_SHUT_ACK_CLI */ },
	 /*
	  * Server sent SHUTDOWN ACK, this should not happen, let's close the
	  * connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_SER */ },
	 /*
	  * SHUTDOWN COM from client, this should not happen, let's close the
	  * connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_CLI */ },
	 /*
	  * SHUTDOWN COMPLETE from server this is what we are expecting.
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_SER */ }
	 },

	/*
	 * State : IP_VS_SCTP_S_SHUT_ACK_SER
	 * SHUTDOWN ACK from the server, awaiting for SHUTDOWN COM from client
	 */
	/*
	 * We recieved the data chuck, keep the state unchanged. I assume
	 * that still data chuncks  can be received by both the peers in
	 * SHUDOWN state
	 */

	{{IP_VS_SCTP_S_SHUT_ACK_SER /* IP_VS_SCTP_EVE_DATA_CLI */ },
	 {IP_VS_SCTP_S_SHUT_ACK_SER /* IP_VS_SCTP_EVE_DATA_SER */ },
	 /*
	  * We have got an INIT from client. From the spec.“Upon receipt of
	  * an INIT in the COOKIE-WAIT state, an endpoint MUST respond with
	  * an INIT ACK using the same parameters it sent in its  original
	  * INIT chunk (including its Initiate Tag, unchanged”).
	  */
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_INIT_CLI */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_INIT_SER */ },
	 /*
	  * INIT_ACK sent by the server, Unexpected INIT ACK, spec says,
	  * “If an INIT ACK is received by an endpoint in any state other
	  * than the COOKIE-WAIT state, the endpoint should discard the
	  * INIT ACK chunk”. Stay in the same state
	  */
	 {IP_VS_SCTP_S_SHUT_ACK_SER /* IP_VS_SCTP_EVE_INIT_ACK_CLI */ },
	 {IP_VS_SCTP_S_SHUT_ACK_SER /* IP_VS_SCTP_EVE_INIT_ACK_SER */ },
	 /*
	  * Client sent ECHO, Spec(sec 5.2.4) says it may be handled by the
	  * peer and peer shall move to the ESTABISHED. if it doesn't handle
	  * it will send ERROR chunk. So, stay in the same state
	  */
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_COOKIE_ECHO_CLI */ },
	 {IP_VS_SCTP_S_ESTABLISHED /* IP_VS_SCTP_EVE_COOKIE_ECHO_SER */ },
	 /*
	  * COOKIE ACK from client, not sure what to do stay in the same state
	  */
	 {IP_VS_SCTP_S_SHUT_ACK_SER /* IP_VS_SCTP_EVE_COOKIE_ACK_CLI */ },
	 {IP_VS_SCTP_S_SHUT_ACK_SER /* IP_VS_SCTP_EVE_COOKIE_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_SER */ },
	 /*
	  * SHUTDOWN sent from the client, move to SHUDDOWN_CLI
	  */
	 {IP_VS_SCTP_S_SHUT_CLI /* IP_VS_SCTP_EVE_SHUT_CLI */ },
	 /*
	  * SHUTDOWN sent from the server, move to SHUTDOWN_SER
	  */
	 {IP_VS_SCTP_S_SHUT_SER /* IP_VS_SCTP_EVE_SHUT_SER */ },
	 /*
	  * client sent SHUDTDOWN_ACK, this should not happen let's close
	  * the connection.
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_CLI */ },
	 /*
	  * Server resent SHUTDOWN ACK, stay in the same state
	  */
	 {IP_VS_SCTP_S_SHUT_ACK_SER /* IP_VS_SCTP_EVE_SHUT_ACK_SER */ },
	 /*
	  * SHUTDOWN COM from client, this what we are expecting, let's close
	  * the connection
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_CLI */ },
	 /*
	  * SHUTDOWN COMPLETE from server this should not happen.
	  */
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_SER */ }
	 },
	/*
	 * State : IP_VS_SCTP_S_CLOSED
	 */
	{{IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_DATA_SER */ },
	 {IP_VS_SCTP_S_INIT_CLI /* IP_VS_SCTP_EVE_INIT_CLI */ },
	 {IP_VS_SCTP_S_INIT_SER /* IP_VS_SCTP_EVE_INIT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_INIT_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_INIT_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ECHO_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ECHO_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_COOKIE_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_ABORT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_ACK_SER */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_CLI */ },
	 {IP_VS_SCTP_S_CLOSED /* IP_VS_SCTP_EVE_SHUT_COM_SER */ }
	 }
};

/*
 *      Timeout table[state]
 */
static int sctp_timeouts[IP_VS_SCTP_S_LAST + 1] = {
	[IP_VS_SCTP_S_NONE]         =     2 * HZ,
	[IP_VS_SCTP_S_INIT_CLI]     =     1 * 60 * HZ,
	[IP_VS_SCTP_S_INIT_SER]     =     1 * 60 * HZ,
	[IP_VS_SCTP_S_INIT_ACK_CLI] =     1 * 60 * HZ,
	[IP_VS_SCTP_S_INIT_ACK_SER] =     1 * 60 * HZ,
	[IP_VS_SCTP_S_ECHO_CLI]     =     1 * 60 * HZ,
	[IP_VS_SCTP_S_ECHO_SER]     =     1 * 60 * HZ,
	[IP_VS_SCTP_S_ESTABLISHED]  =    15 * 60 * HZ,
	[IP_VS_SCTP_S_SHUT_CLI]     =     1 * 60 * HZ,
	[IP_VS_SCTP_S_SHUT_SER]     =     1 * 60 * HZ,
	[IP_VS_SCTP_S_SHUT_ACK_CLI] =     1 * 60 * HZ,
	[IP_VS_SCTP_S_SHUT_ACK_SER] =     1 * 60 * HZ,
	[IP_VS_SCTP_S_CLOSED]       =    10 * HZ,
	[IP_VS_SCTP_S_LAST]         =     2 * HZ,
};

static const char *sctp_state_name_table[IP_VS_SCTP_S_LAST + 1] = {
	[IP_VS_SCTP_S_NONE]         =    "NONE",
	[IP_VS_SCTP_S_INIT_CLI]     =    "INIT_CLI",
	[IP_VS_SCTP_S_INIT_SER]     =    "INIT_SER",
	[IP_VS_SCTP_S_INIT_ACK_CLI] =    "INIT_ACK_CLI",
	[IP_VS_SCTP_S_INIT_ACK_SER] =    "INIT_ACK_SER",
	[IP_VS_SCTP_S_ECHO_CLI]     =    "COOKIE_ECHO_CLI",
	[IP_VS_SCTP_S_ECHO_SER]     =    "COOKIE_ECHO_SER",
	[IP_VS_SCTP_S_ESTABLISHED]  =    "ESTABISHED",
	[IP_VS_SCTP_S_SHUT_CLI]     =    "SHUTDOWN_CLI",
	[IP_VS_SCTP_S_SHUT_SER]     =    "SHUTDOWN_SER",
	[IP_VS_SCTP_S_SHUT_ACK_CLI] =    "SHUTDOWN_ACK_CLI",
	[IP_VS_SCTP_S_SHUT_ACK_SER] =    "SHUTDOWN_ACK_SER",
	[IP_VS_SCTP_S_CLOSED]       =    "CLOSED",
	[IP_VS_SCTP_S_LAST]         =    "BUG!"
};


static const char *sctp_state_name(int state)
{
	if (state >= IP_VS_SCTP_S_LAST)
		return "ERR!";
	if (sctp_state_name_table[state])
		return sctp_state_name_table[state];
	return "?";
}

static void sctp_timeout_change(struct ip_vs_protocol *pp, int flags)
{
}

static int
sctp_set_state_timeout(struct ip_vs_protocol *pp, char *sname, int to)
{

return ip_vs_set_state_timeout(pp->timeout_table, IP_VS_SCTP_S_LAST,
				sctp_state_name_table, sname, to);
}

static inline int
set_sctp_state(struct ip_vs_protocol *pp, struct ip_vs_conn *cp,
		int direction, const struct sk_buff *skb)
{
	sctp_chunkhdr_t _sctpch, *sch;
	unsigned char chunk_type;
	int event, next_state;
	int ihl;

#ifdef CONFIG_IP_VS_IPV6
	ihl = cp->af == AF_INET ? ip_hdrlen(skb) : sizeof(struct ipv6hdr);
#else
	ihl = ip_hdrlen(skb);
#endif

	sch = skb_header_pointer(skb, ihl + sizeof(sctp_sctphdr_t),
				sizeof(_sctpch), &_sctpch);
	if (sch == NULL)
		return 0;

	chunk_type = sch->type;
	/*
	 * Section 3: Multiple chunks can be bundled into one SCTP packet
	 * up to the MTU size, except for the INIT, INIT ACK, and
	 * SHUTDOWN COMPLETE chunks. These chunks MUST NOT be bundled with
	 * any other chunk in a packet.
	 *
	 * Section 3.3.7: DATA chunks MUST NOT be bundled with ABORT. Control
	 * chunks (except for INIT, INIT ACK, and SHUTDOWN COMPLETE) MAY be
	 * bundled with an ABORT, but they MUST be placed before the ABORT
	 * in the SCTP packet or they will be ignored by the receiver.
	 */
	if ((sch->type == SCTP_CID_COOKIE_ECHO) ||
	    (sch->type == SCTP_CID_COOKIE_ACK)) {
		sch = skb_header_pointer(skb, (ihl + sizeof(sctp_sctphdr_t) +
				sch->length), sizeof(_sctpch), &_sctpch);
		if (sch) {
			if (sch->type == SCTP_CID_ABORT)
				chunk_type = sch->type;
		}
	}

	event = sctp_events[chunk_type];

	/*
	 *  If the direction is IP_VS_DIR_OUTPUT, this event is from server
	 */
	if (direction == IP_VS_DIR_OUTPUT)
		event++;
	/*
	 * get next state
	 */
	next_state = sctp_states_table[cp->state][event].next_state;

	if (next_state != cp->state) {
		struct ip_vs_dest *dest = cp->dest;

		IP_VS_DBG_BUF(8, "%s %s  %s:%d->"
				"%s:%d state: %s->%s conn->refcnt:%d\n",
				pp->name,
				((direction == IP_VS_DIR_OUTPUT) ?
				 "output " : "input "),
				IP_VS_DBG_ADDR(cp->af, &cp->daddr),
				ntohs(cp->dport),
				IP_VS_DBG_ADDR(cp->af, &cp->caddr),
				ntohs(cp->cport),
				sctp_state_name(cp->state),
				sctp_state_name(next_state),
				atomic_read(&cp->refcnt));
		if (dest) {
			if (!(cp->flags & IP_VS_CONN_F_INACTIVE) &&
				(next_state != IP_VS_SCTP_S_ESTABLISHED)) {
				atomic_dec(&dest->activeconns);
				atomic_inc(&dest->inactconns);
				cp->flags |= IP_VS_CONN_F_INACTIVE;
			} else if ((cp->flags & IP_VS_CONN_F_INACTIVE) &&
				   (next_state == IP_VS_SCTP_S_ESTABLISHED)) {
				atomic_inc(&dest->activeconns);
				atomic_dec(&dest->inactconns);
				cp->flags &= ~IP_VS_CONN_F_INACTIVE;
			}
		}
	}

	 cp->timeout = pp->timeout_table[cp->state = next_state];

	 return 1;
}

static int
sctp_state_transition(struct ip_vs_conn *cp, int direction,
		const struct sk_buff *skb, struct ip_vs_protocol *pp)
{
	int ret = 0;

	spin_lock(&cp->lock);
	ret = set_sctp_state(pp, cp, direction, skb);
	spin_unlock(&cp->lock);

	return ret;
}

/*
 *      Hash table for SCTP application incarnations
 */
#define SCTP_APP_TAB_BITS        4
#define SCTP_APP_TAB_SIZE        (1 << SCTP_APP_TAB_BITS)
#define SCTP_APP_TAB_MASK        (SCTP_APP_TAB_SIZE - 1)

static struct list_head sctp_apps[SCTP_APP_TAB_SIZE];
static DEFINE_SPINLOCK(sctp_app_lock);

static inline __u16 sctp_app_hashkey(__be16 port)
{
	return (((__force u16)port >> SCTP_APP_TAB_BITS) ^ (__force u16)port)
		& SCTP_APP_TAB_MASK;
}

static int sctp_register_app(struct ip_vs_app *inc)
{
	struct ip_vs_app *i;
	__u16 hash;
	__be16 port = inc->port;
	int ret = 0;

	hash = sctp_app_hashkey(port);

	spin_lock_bh(&sctp_app_lock);
	list_for_each_entry(i, &sctp_apps[hash], p_list) {
		if (i->port == port) {
			ret = -EEXIST;
			goto out;
		}
	}
	list_add(&inc->p_list, &sctp_apps[hash]);
	atomic_inc(&ip_vs_protocol_sctp.appcnt);
out:
	spin_unlock_bh(&sctp_app_lock);

	return ret;
}

static void sctp_unregister_app(struct ip_vs_app *inc)
{
	spin_lock_bh(&sctp_app_lock);
	atomic_dec(&ip_vs_protocol_sctp.appcnt);
	list_del(&inc->p_list);
	spin_unlock_bh(&sctp_app_lock);
}

static int sctp_app_conn_bind(struct ip_vs_conn *cp)
{
	int hash;
	struct ip_vs_app *inc;
	int result = 0;

	/* Default binding: bind app only for NAT */
	if (IP_VS_FWD_METHOD(cp) != IP_VS_CONN_F_MASQ)
		return 0;
	/* Lookup application incarnations and bind the right one */
	hash = sctp_app_hashkey(cp->vport);

	spin_lock(&sctp_app_lock);
	list_for_each_entry(inc, &sctp_apps[hash], p_list) {
		if (inc->port == cp->vport) {
			if (unlikely(!ip_vs_app_inc_get(inc)))
				break;
			spin_unlock(&sctp_app_lock);

			IP_VS_DBG_BUF(9, "%s: Binding conn %s:%u->"
					"%s:%u to app %s on port %u\n",
					__func__,
					IP_VS_DBG_ADDR(cp->af, &cp->caddr),
					ntohs(cp->cport),
					IP_VS_DBG_ADDR(cp->af, &cp->vaddr),
					ntohs(cp->vport),
					inc->name, ntohs(inc->port));
			cp->app = inc;
			if (inc->init_conn)
				result = inc->init_conn(inc, cp);
			goto out;
		}
	}
	spin_unlock(&sctp_app_lock);
out:
	return result;
}

static void ip_vs_sctp_init(struct ip_vs_protocol *pp)
{
	IP_VS_INIT_HASH_TABLE(sctp_apps);
	pp->timeout_table = sctp_timeouts;
}


static void ip_vs_sctp_exit(struct ip_vs_protocol *pp)
{

}

struct ip_vs_protocol ip_vs_protocol_sctp = {
	.name = "SCTP",
	.protocol = IPPROTO_SCTP,
	.num_states = IP_VS_SCTP_S_LAST,
	.dont_defrag = 0,
	.appcnt = ATOMIC_INIT(0),
	.init = ip_vs_sctp_init,
	.exit = ip_vs_sctp_exit,
	.register_app = sctp_register_app,
	.unregister_app = sctp_unregister_app,
	.conn_schedule = sctp_conn_schedule,
	.conn_in_get = ip_vs_conn_in_get_proto,
	.conn_out_get = ip_vs_conn_out_get_proto,
	.snat_handler = sctp_snat_handler,
	.dnat_handler = sctp_dnat_handler,
	.csum_check = sctp_csum_check,
	.state_name = sctp_state_name,
	.state_transition = sctp_state_transition,
	.app_conn_bind = sctp_app_conn_bind,
	.debug_packet = ip_vs_tcpudp_debug_packet,
	.timeout_change = sctp_timeout_change,
	.set_state_timeout = sctp_set_state_timeout,
};
