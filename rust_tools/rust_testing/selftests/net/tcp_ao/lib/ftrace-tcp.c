// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <pthread.h>
#include "aolib.h"

static const char *trace_event_names[__MAX_TRACE_EVENTS] = {
	/* TCP_HASH_EVENT */
	"tcp_hash_bad_header",
	"tcp_hash_md5_required",
	"tcp_hash_md5_unexpected",
	"tcp_hash_md5_mismatch",
	"tcp_hash_ao_required",
	/* TCP_AO_EVENT */
	"tcp_ao_handshake_failure",
	"tcp_ao_wrong_maclen",
	"tcp_ao_mismatch",
	"tcp_ao_key_not_found",
	"tcp_ao_rnext_request",
	/* TCP_AO_EVENT_SK */
	"tcp_ao_synack_no_key",
	/* TCP_AO_EVENT_SNE */
	"tcp_ao_snd_sne_update",
	"tcp_ao_rcv_sne_update"
};

struct expected_trace_point {
	/* required */
	enum trace_events type;
	int family;
	union tcp_addr src;
	union tcp_addr dst;

	/* optional */
	int src_port;
	int dst_port;
	int L3index;

	int fin;
	int syn;
	int rst;
	int psh;
	int ack;

	int keyid;
	int rnext;
	int maclen;
	int sne;

	size_t matched;
};

static struct expected_trace_point *exp_tps;
static size_t exp_tps_nr;
static size_t exp_tps_size;
static pthread_mutex_t exp_tps_mutex = PTHREAD_MUTEX_INITIALIZER;

int __trace_event_expect(enum trace_events type, int family,
			 union tcp_addr src, union tcp_addr dst,
			 int src_port, int dst_port, int L3index,
			 int fin, int syn, int rst, int psh, int ack,
			 int keyid, int rnext, int maclen, int sne)
{
	struct expected_trace_point new_tp = {
		.type           = type,
		.family         = family,
		.src            = src,
		.dst            = dst,
		.src_port       = src_port,
		.dst_port       = dst_port,
		.L3index        = L3index,
		.fin            = fin,
		.syn            = syn,
		.rst            = rst,
		.psh            = psh,
		.ack            = ack,
		.keyid          = keyid,
		.rnext          = rnext,
		.maclen         = maclen,
		.sne            = sne,
		.matched        = 0,
	};
	int ret = 0;

	if (!kernel_config_has(KCONFIG_FTRACE))
		return 0;

	pthread_mutex_lock(&exp_tps_mutex);
	if (exp_tps_nr == exp_tps_size) {
		struct expected_trace_point *tmp;

		if (exp_tps_size == 0)
			exp_tps_size = 10;
		else
			exp_tps_size = exp_tps_size * 1.6;

		tmp = reallocarray(exp_tps, exp_tps_size, sizeof(exp_tps[0]));
		if (!tmp) {
			ret = -ENOMEM;
			goto out;
		}
		exp_tps = tmp;
	}
	exp_tps[exp_tps_nr] = new_tp;
	exp_tps_nr++;
out:
	pthread_mutex_unlock(&exp_tps_mutex);
	return ret;
}

static void free_expected_events(void)
{
	/* We're from the process destructor - not taking the mutex */
	exp_tps_size = 0;
	exp_tps = NULL;
	free(exp_tps);
}

struct trace_point {
	int family;
	union tcp_addr src;
	union tcp_addr dst;
	unsigned int src_port;
	unsigned int dst_port;
	int L3index;
	unsigned int fin:1,
		     syn:1,
		     rst:1,
		     psh:1,
		     ack:1;

	unsigned int keyid;
	unsigned int rnext;
	unsigned int maclen;

	unsigned int sne;
};

static bool lookup_expected_event(int event_type, struct trace_point *e)
{
	size_t i;

	pthread_mutex_lock(&exp_tps_mutex);
	for (i = 0; i < exp_tps_nr; i++) {
		struct expected_trace_point *p = &exp_tps[i];
		size_t sk_size;

		if (p->type != event_type)
			continue;
		if (p->family != e->family)
			continue;
		if (p->family == AF_INET)
			sk_size = sizeof(p->src.a4);
		else
			sk_size = sizeof(p->src.a6);
		if (memcmp(&p->src, &e->src, sk_size))
			continue;
		if (memcmp(&p->dst, &e->dst, sk_size))
			continue;
		if (p->src_port >= 0 && p->src_port != e->src_port)
			continue;
		if (p->dst_port >= 0 && p->dst_port != e->dst_port)
			continue;
		if (p->L3index >= 0 && p->L3index != e->L3index)
			continue;

		if (p->fin >= 0 && p->fin != e->fin)
			continue;
		if (p->syn >= 0 && p->syn != e->syn)
			continue;
		if (p->rst >= 0 && p->rst != e->rst)
			continue;
		if (p->psh >= 0 && p->psh != e->psh)
			continue;
		if (p->ack >= 0 && p->ack != e->ack)
			continue;

		if (p->keyid >= 0 && p->keyid != e->keyid)
			continue;
		if (p->rnext >= 0 && p->rnext != e->rnext)
			continue;
		if (p->maclen >= 0 && p->maclen != e->maclen)
			continue;
		if (p->sne >= 0 && p->sne != e->sne)
			continue;
		p->matched++;
		pthread_mutex_unlock(&exp_tps_mutex);
		return true;
	}
	pthread_mutex_unlock(&exp_tps_mutex);
	return false;
}

static int check_event_type(const char *line)
{
	size_t i;

	/*
	 * This should have been a set or hashmap, but it's a selftest,
	 * so... KISS.
	 */
	for (i = 0; i < __MAX_TRACE_EVENTS; i++) {
		if (!strncmp(trace_event_names[i], line, strlen(trace_event_names[i])))
			return i;
	}
	return -1;
}

static bool event_has_flags(enum trace_events event)
{
	switch (event) {
	case TCP_HASH_BAD_HEADER:
	case TCP_HASH_MD5_REQUIRED:
	case TCP_HASH_MD5_UNEXPECTED:
	case TCP_HASH_MD5_MISMATCH:
	case TCP_HASH_AO_REQUIRED:
	case TCP_AO_HANDSHAKE_FAILURE:
	case TCP_AO_WRONG_MACLEN:
	case TCP_AO_MISMATCH:
	case TCP_AO_KEY_NOT_FOUND:
	case TCP_AO_RNEXT_REQUEST:
		return true;
	default:
		return false;
	}
}

static int tracer_ip_split(int family, char *src, char **addr, char **port)
{
	char *p;

	if (family == AF_INET) {
		/* fomat is <addr>:port, i.e.: 10.0.254.1:7015 */
		*addr = src;
		p = strchr(src, ':');
		if (!p) {
			test_print("Couldn't parse trace event addr:port %s", src);
			return -EINVAL;
		}
		*p++ = '\0';
		*port = p;
		return 0;
	}
	if (family != AF_INET6)
		return -EAFNOSUPPORT;

	/* format is [<addr>]:port, i.e.: [2001:db8:254::1]:7013 */
	*addr = strchr(src, '[');
	p = strchr(src, ']');

	if (!p || !*addr) {
		test_print("Couldn't parse trace event [addr]:port %s", src);
		return -EINVAL;
	}

	*addr = *addr + 1;      /* '[' */
	*p++ = '\0';            /* ']' */
	if (*p != ':') {
		test_print("Couldn't parse trace event :port %s", p);
		return -EINVAL;
	}
	*p++ = '\0';            /* ':' */
	*port = p;
	return 0;
}

static int tracer_scan_address(int family, char *src,
			       union tcp_addr *dst, unsigned int *port)
{
	char *addr, *port_str;
	int ret;

	ret = tracer_ip_split(family, src, &addr, &port_str);
	if (ret)
		return ret;

	if (inet_pton(family, addr, dst) != 1) {
		test_print("Couldn't parse trace event addr %s", addr);
		return -EINVAL;
	}
	errno = 0;
	*port = (unsigned int)strtoul(port_str, NULL, 10);
	if (errno != 0) {
		test_print("Couldn't parse trace event port %s", port_str);
		return -errno;
	}
	return 0;
}

static int tracer_scan_event(const char *line, enum trace_events event,
			     struct trace_point *out)
{
	char *src = NULL, *dst = NULL, *family = NULL;
	char fin, syn, rst, psh, ack;
	int nr_matched, ret = 0;
	uint64_t netns_cookie;

	switch (event) {
	case TCP_HASH_BAD_HEADER:
	case TCP_HASH_MD5_REQUIRED:
	case TCP_HASH_MD5_UNEXPECTED:
	case TCP_HASH_MD5_MISMATCH:
	case TCP_HASH_AO_REQUIRED: {
		nr_matched = sscanf(line, "%*s net=%" PRIu64 " state%*s family=%ms src=%ms dest=%ms L3index=%d [%c%c%c%c%c]",
				    &netns_cookie, &family,
				    &src, &dst, &out->L3index,
				    &fin, &syn, &rst, &psh, &ack);
		if (nr_matched != 10)
			test_print("Couldn't parse trace event, matched = %d/10",
				   nr_matched);
		break;
	}
	case TCP_AO_HANDSHAKE_FAILURE:
	case TCP_AO_WRONG_MACLEN:
	case TCP_AO_MISMATCH:
	case TCP_AO_KEY_NOT_FOUND:
	case TCP_AO_RNEXT_REQUEST: {
		nr_matched = sscanf(line, "%*s net=%" PRIu64 " state%*s family=%ms src=%ms dest=%ms L3index=%d [%c%c%c%c%c] keyid=%u rnext=%u maclen=%u",
				    &netns_cookie, &family,
				    &src, &dst, &out->L3index,
				    &fin, &syn, &rst, &psh, &ack,
				    &out->keyid, &out->rnext, &out->maclen);
		if (nr_matched != 13)
			test_print("Couldn't parse trace event, matched = %d/13",
				   nr_matched);
		break;
	}
	case TCP_AO_SYNACK_NO_KEY: {
		nr_matched = sscanf(line, "%*s net=%" PRIu64 " state%*s family=%ms src=%ms dest=%ms keyid=%u rnext=%u",
				    &netns_cookie, &family,
				    &src, &dst, &out->keyid, &out->rnext);
		if (nr_matched != 6)
			test_print("Couldn't parse trace event, matched = %d/6",
				   nr_matched);
		break;
	}
	case TCP_AO_SND_SNE_UPDATE:
	case TCP_AO_RCV_SNE_UPDATE: {
		nr_matched = sscanf(line, "%*s net=%" PRIu64 " state%*s family=%ms src=%ms dest=%ms sne=%u",
				    &netns_cookie, &family,
				    &src, &dst, &out->sne);
		if (nr_matched != 5)
			test_print("Couldn't parse trace event, matched = %d/5",
				   nr_matched);
		break;
	}
	default:
		return -1;
	}

	if (family) {
		if (!strcmp(family, "AF_INET")) {
			out->family = AF_INET;
		} else if (!strcmp(family, "AF_INET6")) {
			out->family = AF_INET6;
		} else {
			test_print("Couldn't parse trace event family %s", family);
			ret = -EINVAL;
			goto out_free;
		}
	}

	if (event_has_flags(event)) {
		out->fin = (fin == 'F');
		out->syn = (syn == 'S');
		out->rst = (rst == 'R');
		out->psh = (psh == 'P');
		out->ack = (ack == '.');

		if ((fin != 'F' && fin != ' ') ||
		    (syn != 'S' && syn != ' ') ||
		    (rst != 'R' && rst != ' ') ||
		    (psh != 'P' && psh != ' ') ||
		    (ack != '.' && ack != ' ')) {
			test_print("Couldn't parse trace event flags %c%c%c%c%c",
				   fin, syn, rst, psh, ack);
			ret = -EINVAL;
			goto out_free;
		}
	}

	if (src && tracer_scan_address(out->family, src, &out->src, &out->src_port)) {
		ret = -EINVAL;
		goto out_free;
	}

	if (dst && tracer_scan_address(out->family, dst, &out->dst, &out->dst_port)) {
		ret = -EINVAL;
		goto out_free;
	}

	if (netns_cookie != ns_cookie1 && netns_cookie != ns_cookie2) {
		test_print("Net namespace filter for trace event didn't work: %" PRIu64 " != %" PRIu64 " OR %" PRIu64,
			   netns_cookie, ns_cookie1, ns_cookie2);
		ret = -EINVAL;
	}

out_free:
	free(src);
	free(dst);
	free(family);
	return ret;
}

static enum ftracer_op aolib_tracer_process_event(const char *line)
{
	int event_type = check_event_type(line);
	struct trace_point tmp = {};

	if (event_type < 0)
		return FTRACER_LINE_PRESERVE;

	if (tracer_scan_event(line, event_type, &tmp))
		return FTRACER_LINE_PRESERVE;

	return lookup_expected_event(event_type, &tmp) ?
		FTRACER_LINE_DISCARD : FTRACER_LINE_PRESERVE;
}

static void dump_trace_event(struct expected_trace_point *e)
{
	char src[INET6_ADDRSTRLEN], dst[INET6_ADDRSTRLEN];

	if (!inet_ntop(e->family, &e->src, src, INET6_ADDRSTRLEN))
		test_error("inet_ntop()");
	if (!inet_ntop(e->family, &e->dst, dst, INET6_ADDRSTRLEN))
		test_error("inet_ntop()");
	test_print("trace event filter %s [%s:%d => %s:%d, L3index %d, flags: %s%s%s%s%s, keyid: %d, rnext: %d, maclen: %d, sne: %d] = %zu",
		   trace_event_names[e->type],
		   src, e->src_port, dst, e->dst_port, e->L3index,
		   e->fin ? "F" : "", e->syn ? "S" : "", e->rst ? "R" : "",
		   e->psh ? "P" : "", e->ack ? "." : "",
		   e->keyid, e->rnext, e->maclen, e->sne, e->matched);
}

static void print_match_stats(bool unexpected_events)
{
	size_t matches_per_type[__MAX_TRACE_EVENTS] = {};
	bool expected_but_none = false;
	size_t i, total_matched = 0;
	char *stat_line = NULL;

	for (i = 0; i < exp_tps_nr; i++) {
		struct expected_trace_point *e = &exp_tps[i];

		total_matched += e->matched;
		matches_per_type[e->type] += e->matched;
		if (!e->matched)
			expected_but_none = true;
	}
	for (i = 0; i < __MAX_TRACE_EVENTS; i++) {
		if (!matches_per_type[i])
			continue;
		stat_line = test_sprintf("%s%s[%zu] ", stat_line ?: "",
					 trace_event_names[i],
					 matches_per_type[i]);
		if (!stat_line)
			test_error("test_sprintf()");
	}

	if (unexpected_events || expected_but_none) {
		for (i = 0; i < exp_tps_nr; i++)
			dump_trace_event(&exp_tps[i]);
	}

	if (unexpected_events)
		return;

	if (expected_but_none)
		test_fail("Some trace events were expected, but didn't occur");
	else if (total_matched)
		test_ok("Trace events matched expectations: %zu %s",
			total_matched, stat_line);
	else
		test_ok("No unexpected trace events during the test run");
}

#define dump_events(fmt, ...)                           \
	__test_print(__test_msg, fmt, ##__VA_ARGS__)
static void check_free_events(struct test_ftracer *tracer)
{
	const char **lines;
	size_t nr;

	if (!kernel_config_has(KCONFIG_FTRACE)) {
		test_skip("kernel config doesn't have ftrace - no checks");
		return;
	}

	nr = tracer_get_savedlines_nr(tracer);
	lines = tracer_get_savedlines(tracer);
	print_match_stats(!!nr);
	if (!nr)
		return;

	errno = 0;
	test_xfail("Trace events [%zu] were not expected:", nr);
	while (nr)
		dump_events("\t%s", lines[--nr]);
}

static int setup_tcp_trace_events(struct test_ftracer *tracer)
{
	char *filter;
	size_t i;
	int ret;

	filter = test_sprintf("net_cookie == %zu || net_cookie == %zu",
			      ns_cookie1, ns_cookie2);
	if (!filter)
		return -ENOMEM;

	for (i = 0; i < __MAX_TRACE_EVENTS; i++) {
		char *event_name = test_sprintf("tcp/%s", trace_event_names[i]);

		if (!event_name) {
			ret = -ENOMEM;
			break;
		}
		ret = setup_trace_event(tracer, event_name, filter);
		free(event_name);
		if (ret)
			break;
	}

	free(filter);
	return ret;
}

static void aolib_tracer_destroy(struct test_ftracer *tracer)
{
	check_free_events(tracer);
	free_expected_events();
}

static bool aolib_tracer_expecting_more(void)
{
	size_t i;

	for (i = 0; i < exp_tps_nr; i++)
		if (!exp_tps[i].matched)
			return true;
	return false;
}

int setup_aolib_ftracer(void)
{
	struct test_ftracer *f;

	f = create_ftracer("aolib", aolib_tracer_process_event,
			   aolib_tracer_destroy, aolib_tracer_expecting_more,
			   DEFAULT_FTRACE_BUFFER_KB, DEFAULT_TRACER_LINES_ARR);
	if (!f)
		return -1;

	return setup_tcp_trace_events(f);
}
