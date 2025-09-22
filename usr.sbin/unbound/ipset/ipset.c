/**
 * \file
 * This file implements the ipset module.  It can handle packets by putting
 * the A and AAAA addresses that are configured in unbound.conf as type
 * ipset (local-zone statements) into a firewall rule IPSet.  For firewall
 * blacklist and whitelist usage.
 */
#include "config.h"
#include "ipset/ipset.h"
#include "util/regional.h"
#include "util/net_help.h"
#include "util/config_file.h"

#include "services/cache/dns.h"

#include "sldns/sbuffer.h"
#include "sldns/wire2str.h"
#include "sldns/parseutil.h"

#ifdef HAVE_NET_PFVAR_H
#include <fcntl.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/pfvar.h>
typedef intptr_t filter_dev;
#else
#include <libmnl/libmnl.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/ipset/ip_set.h>
typedef struct mnl_socket * filter_dev;
#endif

#define BUFF_LEN 256

/**
 * Return an error
 * @param qstate: our query state
 * @param id: module id
 * @param rcode: error code (DNS errcode).
 * @return: 0 for use by caller, to make notation easy, like:
 * 	return error_response(..).
 */
static int error_response(struct module_qstate* qstate, int id, int rcode) {
	verbose(VERB_QUERY, "return error response %s",
		sldns_lookup_by_id(sldns_rcodes, rcode)?
		sldns_lookup_by_id(sldns_rcodes, rcode)->name:"??");
	qstate->return_rcode = rcode;
	qstate->return_msg = NULL;
	qstate->ext_state[id] = module_finished;
	return 0;
}

#ifdef HAVE_NET_PFVAR_H
static void * open_filter() {
	filter_dev dev;

	dev = open("/dev/pf", O_RDWR);
	if (dev == -1) {
		log_err("open(\"/dev/pf\") failed: %s", strerror(errno));
		return NULL;
	}
	else
		return (void *)dev;
}
#else
static void * open_filter() {
	filter_dev dev;

	dev = mnl_socket_open(NETLINK_NETFILTER);
	if (!dev) {
		log_err("ipset: could not open netfilter.");
		return NULL;
	}

	if (mnl_socket_bind(dev, 0, MNL_SOCKET_AUTOPID) < 0) {
		mnl_socket_close(dev);
		log_err("ipset: could not bind netfilter.");
		return NULL;
	}
	return (void *)dev;
}
#endif

#ifdef HAVE_NET_PFVAR_H
static int add_to_ipset(filter_dev dev, const char *setname, const void *ipaddr, int af) {
	struct pfioc_table io;
	struct pfr_addr addr;
	const char *p;
	int i;

	bzero(&io, sizeof(io));
	bzero(&addr, sizeof(addr));

	p = strrchr(setname, '/');
	if (p) {
		i = p - setname;
		if (i >= PATH_MAX) {
			errno = ENAMETOOLONG;
			return -1;
		}
		memcpy(io.pfrio_table.pfrt_anchor, setname, i);
		if (i < PATH_MAX)
			io.pfrio_table.pfrt_anchor[i] = '\0';
		p++;
	}
	else
		p = setname;

	if (strlen(p) >= PF_TABLE_NAME_SIZE) {
		errno = ENAMETOOLONG;
		return -1;
	}
	strlcpy(io.pfrio_table.pfrt_name, p, PF_TABLE_NAME_SIZE);

	io.pfrio_buffer = &addr;
	io.pfrio_size = 1;
	io.pfrio_esize = sizeof(addr);

	switch (af) {
		case AF_INET:
			addr.pfra_ip4addr = *(struct in_addr *)ipaddr;
			addr.pfra_net = 32;
			break;
		case AF_INET6:
			addr.pfra_ip6addr = *(struct in6_addr *)ipaddr;
			addr.pfra_net = 128;
			break;
		default:
		errno = EAFNOSUPPORT;
		return -1;
}
	addr.pfra_af = af;

	if (ioctl(dev, DIOCRADDADDRS, &io) == -1) {
		log_err("ioctl failed: %s", strerror(errno));
		return -1;
	}
	return 0;
}
#else
static int add_to_ipset(filter_dev dev, const char *setname, const void *ipaddr, int af) {
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfg;
	struct nlattr *nested[2];
	static char buffer[BUFF_LEN];

	if (strlen(setname) >= IPSET_MAXNAMELEN) {
		errno = ENAMETOOLONG;
		return -1;
	}
	if (af != AF_INET && af != AF_INET6) {
		errno = EAFNOSUPPORT;
		return -1;
	}

	nlh = mnl_nlmsg_put_header(buffer);
	nlh->nlmsg_type = IPSET_CMD_ADD | (NFNL_SUBSYS_IPSET << 8);
	nlh->nlmsg_flags = NLM_F_REQUEST|NLM_F_ACK|NLM_F_EXCL;

	nfg = mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg));
	nfg->nfgen_family = af;
	nfg->version = NFNETLINK_V0;
	nfg->res_id = htons(0);

	mnl_attr_put_u8(nlh, IPSET_ATTR_PROTOCOL, IPSET_PROTOCOL);
	mnl_attr_put(nlh, IPSET_ATTR_SETNAME, strlen(setname) + 1, setname);
	nested[0] = mnl_attr_nest_start(nlh, IPSET_ATTR_DATA);
	nested[1] = mnl_attr_nest_start(nlh, IPSET_ATTR_IP);
	mnl_attr_put(nlh, (af == AF_INET ? IPSET_ATTR_IPADDR_IPV4 : IPSET_ATTR_IPADDR_IPV6)
			| NLA_F_NET_BYTEORDER, (af == AF_INET ? sizeof(struct in_addr) : sizeof(struct in6_addr)), ipaddr);
	mnl_attr_nest_end(nlh, nested[1]);
	mnl_attr_nest_end(nlh, nested[0]);

	if (mnl_socket_sendto(dev, nlh, nlh->nlmsg_len) < 0) {
		return -1;
	}
	return 0;
}
#endif

static void
ipset_add_rrset_data(struct ipset_env *ie,
	struct packed_rrset_data *d, const char* setname, int af,
	const char* dname)
{
	int ret;
	size_t j, rr_len, rd_len;
	uint8_t *rr_data;

	/* to d->count, not d->rrsig_count, because we do not want to add the RRSIGs, only the addresses */
	for (j = 0; j < d->count; j++) {
		rr_len = d->rr_len[j];
		rr_data = d->rr_data[j];

		rd_len = sldns_read_uint16(rr_data);
		if(af == AF_INET && rd_len != INET_SIZE)
			continue;
		if(af == AF_INET6 && rd_len != INET6_SIZE)
			continue;
		if (rr_len - 2 >= rd_len) {
			if(verbosity >= VERB_QUERY) {
				char ip[128];
				if(inet_ntop(af, rr_data+2, ip, (socklen_t)sizeof(ip)) == 0)
					snprintf(ip, sizeof(ip), "(inet_ntop_error)");
				verbose(VERB_QUERY, "ipset: add %s to %s for %s", ip, setname, dname);
			}
			ret = add_to_ipset((filter_dev)ie->dev, setname, rr_data + 2, af);
			if (ret < 0) {
				log_err("ipset: could not add %s into %s", dname, setname);

#if HAVE_NET_PFVAR_H
				/* don't close as we might not be able to open again due to dropped privs */
#else
				mnl_socket_close((filter_dev)ie->dev);
				ie->dev = NULL;
#endif
				break;
			}
		}
	}
}

static int
ipset_check_zones_for_rrset(struct module_env *env, struct ipset_env *ie,
	struct ub_packed_rrset_key *rrset, const char *qname, int qlen,
	const char *setname, int af)
{
	static char dname[BUFF_LEN];
	const char *ds, *qs;
	int dlen, plen;

	struct config_strlist *p;
	struct packed_rrset_data *d;

	dlen = sldns_wire2str_dname_buf(rrset->rk.dname, rrset->rk.dname_len, dname, BUFF_LEN);
	if (dlen == 0) {
		log_err("bad domain name");
		return -1;
	}
	if (dname[dlen - 1] == '.') {
		dlen--;
	}
	if (qname[qlen - 1] == '.') {
		qlen--;
	}

	for (p = env->cfg->local_zones_ipset; p; p = p->next) {
		ds = NULL;
		qs = NULL;
		plen = strlen(p->str);
		if (p->str[plen - 1] == '.') {
			plen--;
		}

		if (dlen == plen || (dlen > plen && dname[dlen - plen - 1] == '.' )) {
			ds = dname + (dlen - plen);
		}
		if (qlen == plen || (qlen > plen && qname[qlen - plen - 1] == '.' )) {
			qs = qname + (qlen - plen);
		}
		if ((ds && strncasecmp(p->str, ds, plen) == 0)
			|| (qs && strncasecmp(p->str, qs, plen) == 0)) {
			d = (struct packed_rrset_data*)rrset->entry.data;
			ipset_add_rrset_data(ie, d, setname, af, dname);
			break;
		}
	}
	return 0;
}

static int ipset_update(struct module_env *env, struct dns_msg *return_msg,
	struct query_info qinfo, struct ipset_env *ie)
{
	size_t i;
	const char *setname;
	struct ub_packed_rrset_key *rrset;
	int af;
	static char qname[BUFF_LEN];
	int qlen;

#ifdef HAVE_NET_PFVAR_H
#else
	if (!ie->dev) {
		/* retry to create mnl socket */
		ie->dev = open_filter();
		if (!ie->dev) {
			log_warn("ipset open_filter failed");
			return -1;
		}
	}
#endif

	qlen = sldns_wire2str_dname_buf(qinfo.qname, qinfo.qname_len,
		qname, BUFF_LEN);
	if(qlen == 0) {
		log_err("bad domain name");
		return -1;
	}

	for(i = 0; i < return_msg->rep->rrset_count; i++) {
		setname = NULL;
		rrset = return_msg->rep->rrsets[i];
		if(ntohs(rrset->rk.type) == LDNS_RR_TYPE_A &&
			ie->v4_enabled == 1) {
			af = AF_INET;
			setname = ie->name_v4;
		} else if(ntohs(rrset->rk.type) == LDNS_RR_TYPE_AAAA &&
			ie->v6_enabled == 1) {
			af = AF_INET6;
			setname = ie->name_v6;
		}

		if (setname) {
			if(ipset_check_zones_for_rrset(env, ie, rrset, qname,
				qlen, setname, af) == -1)
				return -1;
		}
	}

	return 0;
}

int ipset_startup(struct module_env* env, int id) {
	struct ipset_env *ipset_env;

	ipset_env = (struct ipset_env *)calloc(1, sizeof(struct ipset_env));
	if (!ipset_env) {
		log_err("malloc failure");
		return 0;
	}

	env->modinfo[id] = (void *)ipset_env;

#ifdef HAVE_NET_PFVAR_H
	ipset_env->dev = open_filter();
	if (!ipset_env->dev) {
		log_err("ipset open_filter failed");
		return 0;
	}
#else
	ipset_env->dev = NULL;
#endif
	return 1;
}

void ipset_destartup(struct module_env* env, int id) {
	filter_dev dev;
	struct ipset_env *ipset_env;

	if (!env || !env->modinfo[id]) {
		return;
	}
	ipset_env = (struct ipset_env *)env->modinfo[id];

	dev = (filter_dev)ipset_env->dev;
	if (dev) {
#if HAVE_NET_PFVAR_H
		close(dev);
#else
		mnl_socket_close(dev);
#endif
		ipset_env->dev = NULL;
	}

	free(ipset_env);
	env->modinfo[id] = NULL;
}

int ipset_init(struct module_env* env, int id) {
	struct ipset_env *ipset_env = env->modinfo[id];

	ipset_env->name_v4 = env->cfg->ipset_name_v4;
	ipset_env->name_v6 = env->cfg->ipset_name_v6;

	ipset_env->v4_enabled = !ipset_env->name_v4 || (strlen(ipset_env->name_v4) == 0) ? 0 : 1;
	ipset_env->v6_enabled = !ipset_env->name_v6 || (strlen(ipset_env->name_v6) == 0) ? 0 : 1;

	if ((ipset_env->v4_enabled < 1) && (ipset_env->v6_enabled < 1)) {
		log_err("ipset: set name no configuration?");
		return 0;
	}

	return 1;
}

void ipset_deinit(struct module_env *ATTR_UNUSED(env), int ATTR_UNUSED(id)) {
	/* nothing */
}

static int ipset_new(struct module_qstate* qstate, int id) {
	struct ipset_qstate *iq = (struct ipset_qstate *)regional_alloc(
		qstate->region, sizeof(struct ipset_qstate));
	qstate->minfo[id] = iq;
	if (!iq) {
		return 0;
	}

	memset(iq, 0, sizeof(*iq));
	/* initialise it */
	/* TODO */

	return 1;
}

void ipset_operate(struct module_qstate *qstate, enum module_ev event, int id,
	struct outbound_entry *outbound) {
	struct ipset_env *ie = (struct ipset_env *)qstate->env->modinfo[id];
	struct ipset_qstate *iq = (struct ipset_qstate *)qstate->minfo[id];
	verbose(VERB_QUERY, "ipset[module %d] operate: extstate:%s event:%s",
		id, strextstate(qstate->ext_state[id]), strmodulevent(event));
	if (iq) {
		log_query_info(VERB_QUERY, "ipset operate: query", &qstate->qinfo);
	}

	/* perform ipset state machine */
	if ((event == module_event_new || event == module_event_pass) && !iq) {
		if (!ipset_new(qstate, id)) {
			(void)error_response(qstate, id, LDNS_RCODE_SERVFAIL);
			return;
		}
		iq = (struct ipset_qstate*)qstate->minfo[id];
	}

	if (iq && (event == module_event_pass || event == module_event_new)) {
		qstate->ext_state[id] = module_wait_module;
		return;
	}

	if (iq && (event == module_event_moddone)) {
		if (qstate->return_msg && qstate->return_msg->rep) {
			ipset_update(qstate->env, qstate->return_msg, qstate->qinfo, ie);
		}
		qstate->ext_state[id] = module_finished;
		return;
	}

	if (iq && outbound) {
		/* ipset does not need to process responses at this time
		 * ignore it.
		ipset_process_response(qstate, iq, ie, id, outbound, event);
		*/
		return;
	}

	if (event == module_event_error) {
		verbose(VERB_ALGO, "got called with event error, giving up");
		(void)error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		return;
	}

	if (!iq && (event == module_event_moddone)) {
		/* during priming, module done but we never started */
		qstate->ext_state[id] = module_finished;
		return;
	}

	log_err("bad event for ipset");
	(void)error_response(qstate, id, LDNS_RCODE_SERVFAIL);
}

void ipset_inform_super(struct module_qstate *ATTR_UNUSED(qstate),
	int ATTR_UNUSED(id), struct module_qstate *ATTR_UNUSED(super)) {
	/* ipset does not use subordinate requests at this time */
	verbose(VERB_ALGO, "ipset inform_super was called");
}

void ipset_clear(struct module_qstate *qstate, int id) {
	struct cachedb_qstate *iq;
	if (!qstate) {
		return;
	}
	iq = (struct cachedb_qstate *)qstate->minfo[id];
	if (iq) {
		/* free contents of iq */
		/* TODO */
	}
	qstate->minfo[id] = NULL;
}

size_t ipset_get_mem(struct module_env *env, int id) {
	struct ipset_env *ie = (struct ipset_env *)env->modinfo[id];
	if (!ie) {
		return 0;
	}
	return sizeof(*ie);
}

/**
 * The ipset function block 
 */
static struct module_func_block ipset_block = {
	"ipset",
	&ipset_startup, &ipset_destartup, &ipset_init, &ipset_deinit,
	&ipset_operate, &ipset_inform_super, &ipset_clear, &ipset_get_mem
};

struct module_func_block * ipset_get_funcblock(void) {
	return &ipset_block;
}

