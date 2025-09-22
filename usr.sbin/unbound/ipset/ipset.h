/**
 * ipset.h
 *
 * Author: Kevin Chou
 * Email: k9982874@gmail.com
 */
#ifndef IPSET_H
#define IPSET_H
/** \file
 *
 * This file implements the ipset module.  It can handle packets by putting
 * the A and AAAA addresses that are configured in unbound.conf as type
 * ipset (local-zone statements) into a firewall rule IPSet.  For firewall
 * blacklist and whitelist usage.
 *
 * To use the IPset module, install the libmnl-dev (or libmnl-devel) package
 * and configure with --enable-ipset.  And compile.  Then enable the ipset
 * module in unbound.conf with module-config: "ipset validator iterator"
 * then create it with ipset -N blacklist iphash and then add
 * local-zone: "example.com." ipset
 * statements for the zones where you want the addresses of the names
 * looked up added to the set.
 *
 * Set the name of the set with
 * ipset:
 *   name-v4: "blacklist"
 *   name-v6: "blacklist6"
 * in unbound.conf.  The set can be used in this way:
 *   iptables -A INPUT -m set --set blacklist src -j DROP
 *   ip6tables -A INPUT -m set --set blacklist6 src -j DROP
 */

#include "util/module.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ipset_env {
	void* dev;

	int v4_enabled;
	int v6_enabled;

	const char *name_v4;
	const char *name_v6;
};

struct ipset_qstate {
	int dummy;
};

/** Startup the ipset module */
int ipset_startup(struct module_env* env, int id);
/** Destartup the ipset module */
void ipset_destartup(struct module_env* env, int id);
/** Init the ipset module */
int ipset_init(struct module_env* env, int id);
/** Deinit the ipset module */
void ipset_deinit(struct module_env* env, int id);
/** Operate on an event on a query (in qstate). */
void ipset_operate(struct module_qstate* qstate, enum module_ev event,
	int id, struct outbound_entry* outbound);
/** Subordinate query done, inform this super request of its conclusion */
void ipset_inform_super(struct module_qstate* qstate, int id,
	struct module_qstate* super);
/** clear the ipset query-specific contents out of qstate */
void ipset_clear(struct module_qstate* qstate, int id);
/** return memory estimate for ipset module */
size_t ipset_get_mem(struct module_env* env, int id);

/**
 * Get the function block with pointers to the ipset functions
 * @return the function block for "ipset".
 */
struct module_func_block* ipset_get_funcblock(void);

#ifdef __cplusplus
}
#endif

#endif /* IPSET_H */

