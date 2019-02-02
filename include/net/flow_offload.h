#ifndef _NET_FLOW_OFFLOAD_H
#define _NET_FLOW_OFFLOAD_H

#include <net/flow_dissector.h>

struct flow_match {
	struct flow_dissector	*dissector;
	void			*mask;
	void			*key;
};

struct flow_match_basic {
	struct flow_dissector_key_basic *key, *mask;
};

struct flow_match_control {
	struct flow_dissector_key_control *key, *mask;
};

struct flow_match_eth_addrs {
	struct flow_dissector_key_eth_addrs *key, *mask;
};

struct flow_match_vlan {
	struct flow_dissector_key_vlan *key, *mask;
};

struct flow_match_ipv4_addrs {
	struct flow_dissector_key_ipv4_addrs *key, *mask;
};

struct flow_match_ipv6_addrs {
	struct flow_dissector_key_ipv6_addrs *key, *mask;
};

struct flow_match_ip {
	struct flow_dissector_key_ip *key, *mask;
};

struct flow_match_ports {
	struct flow_dissector_key_ports *key, *mask;
};

struct flow_match_icmp {
	struct flow_dissector_key_icmp *key, *mask;
};

struct flow_match_tcp {
	struct flow_dissector_key_tcp *key, *mask;
};

struct flow_match_mpls {
	struct flow_dissector_key_mpls *key, *mask;
};

struct flow_match_enc_keyid {
	struct flow_dissector_key_keyid *key, *mask;
};

struct flow_match_enc_opts {
	struct flow_dissector_key_enc_opts *key, *mask;
};

struct flow_rule;

void flow_rule_match_basic(const struct flow_rule *rule,
			   struct flow_match_basic *out);
void flow_rule_match_control(const struct flow_rule *rule,
			     struct flow_match_control *out);
void flow_rule_match_eth_addrs(const struct flow_rule *rule,
			       struct flow_match_eth_addrs *out);
void flow_rule_match_vlan(const struct flow_rule *rule,
			  struct flow_match_vlan *out);
void flow_rule_match_ipv4_addrs(const struct flow_rule *rule,
				struct flow_match_ipv4_addrs *out);
void flow_rule_match_ipv6_addrs(const struct flow_rule *rule,
				struct flow_match_ipv6_addrs *out);
void flow_rule_match_ip(const struct flow_rule *rule,
			struct flow_match_ip *out);
void flow_rule_match_ports(const struct flow_rule *rule,
			   struct flow_match_ports *out);
void flow_rule_match_tcp(const struct flow_rule *rule,
			 struct flow_match_tcp *out);
void flow_rule_match_icmp(const struct flow_rule *rule,
			  struct flow_match_icmp *out);
void flow_rule_match_mpls(const struct flow_rule *rule,
			  struct flow_match_mpls *out);
void flow_rule_match_enc_control(const struct flow_rule *rule,
				 struct flow_match_control *out);
void flow_rule_match_enc_ipv4_addrs(const struct flow_rule *rule,
				    struct flow_match_ipv4_addrs *out);
void flow_rule_match_enc_ipv6_addrs(const struct flow_rule *rule,
				    struct flow_match_ipv6_addrs *out);
void flow_rule_match_enc_ip(const struct flow_rule *rule,
			    struct flow_match_ip *out);
void flow_rule_match_enc_ports(const struct flow_rule *rule,
			       struct flow_match_ports *out);
void flow_rule_match_enc_keyid(const struct flow_rule *rule,
			       struct flow_match_enc_keyid *out);
void flow_rule_match_enc_opts(const struct flow_rule *rule,
			      struct flow_match_enc_opts *out);

struct flow_rule {
	struct flow_match	match;
};

struct flow_rule *flow_rule_alloc(void);

static inline bool flow_rule_match_key(const struct flow_rule *rule,
				       enum flow_dissector_key_id key)
{
	return dissector_uses_key(rule->match.dissector, key);
}

#endif /* _NET_FLOW_OFFLOAD_H */
