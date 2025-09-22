/*
 * checkconf - Read and repeat configuration file to output.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include "tsig.h"
#include "options.h"
#include "util.h"
#include "dname.h"
#include "rrl.h"

extern char *optarg;
extern int optind;
static void usage(void) ATTR_NORETURN;
int zonec_parse_string(region_type* ATTR_UNUSED(region),
	domain_table_type* ATTR_UNUSED(domains), zone_type* ATTR_UNUSED(zone),
	char* ATTR_UNUSED(str), domain_type** ATTR_UNUSED(parsed),
	int* ATTR_UNUSED(num_rrs))
{
	return 0;
}

#define ZONE_GET_ACL(NAME, VAR, PATTERN) 		\
	if (strcasecmp(#NAME, (VAR)) == 0) { 	\
		quote_acl(PATTERN->NAME); 	\
		return; 			\
	}

#define ZONE_GET_OUTGOING(NAME, VAR, PATTERN)			\
	if (strcasecmp(#NAME, (VAR)) == 0) {		\
		acl_options_type* acl; 			\
		for(acl=PATTERN->NAME; acl; acl=acl->next)	\
			quote(acl->ip_address_spec);	\
		return; 				\
	}

#define ZONE_GET_STR(NAME, VAR, PATTERN) 		\
	if (strcasecmp(#NAME, (VAR)) == 0) { 	\
		quote(PATTERN->NAME); 		\
		return; 			\
	}

#define ZONE_GET_PATH(FINAL, NAME, VAR, PATTERN) 	\
	if (strcasecmp(#NAME, (VAR)) == 0) { 		\
		quotepath(opt, FINAL, PATTERN->NAME); 	\
		return; 				\
	}

#define ZONE_GET_BIN(NAME, VAR, PATTERN) 			\
	if (strcasecmp(#NAME, (VAR)) == 0) { 		\
		printf("%s\n", (PATTERN->NAME)?"yes":"no"); 	\
		return;					\
	}

#define ZONE_GET_RRL(NAME, VAR, PATTERN) 			\
	if (strcasecmp(#NAME, (VAR)) == 0) { 		\
		zone_print_rrl_whitelist("", PATTERN->NAME);	\
		return;					\
	}

#define ZONE_GET_INT(NAME, VAR, PATTERN) 		\
	if (strcasecmp(#NAME, (VAR)) == 0) { 	\
		printf("%d\n", (int) PATTERN->NAME); 	\
		return; 			\
	}

#define SERV_GET_BIN(NAME, VAR) 			\
	if (strcasecmp(#NAME, (VAR)) == 0) { 		\
		printf("%s\n", opt->NAME?"yes":"no"); 	\
		return;					\
	}

#define SERV_GET_STR(NAME, VAR) 		\
	if (strcasecmp(#NAME, (VAR)) == 0) { 	\
		quote(opt->NAME); 		\
		return; 			\
	}

#define SERV_GET_PATH(FINAL, NAME, VAR) 	\
	if (strcasecmp(#NAME, (VAR)) == 0) { 	\
		quotepath(opt, FINAL, opt->NAME); 	\
		return; 			\
	}

#define SERV_GET_INT(NAME, VAR) 		\
	if (strcasecmp(#NAME, (VAR)) == 0) { 	\
		printf("%d\n", (int) opt->NAME); 	\
		return; 			\
	}

#define SERV_GET_IP(NAME, MEMBER, VAR) 				\
	if (strcasecmp(#NAME, (VAR)) == 0) { 		\
		for(ip = opt->MEMBER; ip; ip=ip->next)	\
		{						\
			quote(ip->address);			\
		}						\
		return;						\
	}

#ifdef RATELIMIT
static void zone_print_rrl_whitelist(const char* s, uint16_t w)
{
	int i;
	if(w==rrl_type_all) {
		printf("%sall\n", s);
		return;
	}
	for(i=0x01; i <= 0x80; i<<=1) {
		if( (w&i) )
			printf("%s%s\n", s, rrltype2str(i));
	}
}
#endif /* RATELIMIT */

static char buf[BUFSIZ];

static char *
underscore(const char *s) {
	const char *j = s;
	size_t i = 0;

	while(j && *j) {
		if (*j == '-') {
			buf[i++] = '_';
		} else {
			buf[i++] = *j;
		}
		j++;
		if (i >= BUFSIZ) {
			return NULL;
		}
	}
	buf[i] = '\0';
	return buf;
}

static void
usage(void)
{
	fprintf(stderr, "usage: nsd-checkconf [-v|-h] [-o option] [-z zonename]\n");
	fprintf(stderr, "                     [-s keyname] [-t tlsauthname] <configfilename>\n");
	fprintf(stderr, "       Checks NSD configuration file for errors.\n");
	fprintf(stderr, "       Version %s. Report bugs to <%s>.\n\n",
		PACKAGE_VERSION, PACKAGE_BUGREPORT);
	fprintf(stderr, "Use with a configfile as argument to check syntax.\n");
	fprintf(stderr, "Use with -o, -z, -t or -s options to query the configuration.\n\n");
	fprintf(stderr, "-v			Verbose, echo settings that take effect to std output.\n");
	fprintf(stderr, "-h			Print this help information.\n");
	fprintf(stderr, "-f			Use with -o to print final pathnames, ie. with chroot.\n");
	fprintf(stderr, "-o option		Print value of the option specified to stdout.\n");
	fprintf(stderr, "-p pattern		Print option value for the pattern given.\n");
	fprintf(stderr, "-z zonename		Print option value for the zone given.\n");
	fprintf(stderr, "-a keyname		Print algorithm name for the TSIG key.\n");
	fprintf(stderr, "-s keyname		Print base64 secret blob for the TSIG key.\n");
	fprintf(stderr, "-t tls-auth-name	Print auth domain name for the tls-auth clause.\n");
	exit(1);
}

static void
print_string_var(const char* varname, const char* value)
{
	if (!value) {
		printf("\t#%s\n", varname);
	} else {
		printf("\t%s \"%s\"\n", varname, value);
	}
}

static void
quote(const char *v)
{
	if(v==NULL)
		printf("\n");
	else
		printf("%s\n", v);
}

static void
quotepath(nsd_options_type* opt, int final, const char *f)
{
	const char* chr = opt->chroot;
#ifdef CHROOTDIR
	if(chr == 0) chr = CHROOTDIR;
#endif
	if(f == 0 || f[0] == '/' || !final || !chr || chr[0]==0) {
		quote(f);
		return;
	}
	/* chroot has had trailing slash applied in check part of checkconf */
	printf("%s%s\n", chr, f);
}

static void
quote_acl(acl_options_type* acl)
{
	while(acl)
	{
		if (acl->tls_auth_name)
			printf("%s %s %s\n", acl->ip_address_spec,
				acl->nokey?"NOKEY":(acl->blocked?"BLOCKED":
				(acl->key_name?acl->key_name:"(null)")),
				acl->tls_auth_name?acl->tls_auth_name:"");
		else
			printf("%s %s\n", acl->ip_address_spec,
				acl->nokey?"NOKEY":(acl->blocked?"BLOCKED":
				(acl->key_name?acl->key_name:"(null)")));
		acl=acl->next;
	}
}

static void
print_acl(const char* varname, acl_options_type* acl)
{
	while(acl)
	{
		printf("\t%s ", varname);
		if(acl->use_axfr_only)
			printf("AXFR ");
		if(acl->allow_udp)
			printf("UDP ");
		if (acl->tls_auth_name)
			printf("%s %s %s\n", acl->ip_address_spec,
				acl->nokey?"NOKEY":(acl->blocked?"BLOCKED":
				(acl->key_name?acl->key_name:"(null)")),
				acl->tls_auth_name?acl->tls_auth_name:"");
		else
			printf("%s %s\n", acl->ip_address_spec,
				acl->nokey?"NOKEY":(acl->blocked?"BLOCKED":
				(acl->key_name?acl->key_name:"(null)")));
		if(verbosity>1) {
			printf("\t# %s", acl->is_ipv6?"ip6":"ip4");
			if(acl->port == 0) printf(" noport");
			else printf(" port=%d", acl->port);
			if(acl->rangetype == acl_range_single) printf(" single");
			if(acl->rangetype == acl_range_mask)   printf(" masked");
			if(acl->rangetype == acl_range_subnet) printf(" subnet");
			if(acl->rangetype == acl_range_minmax) printf(" minmax");
			if(acl->is_ipv6) {
#ifdef INET6
				char dest[128];
				inet_ntop(AF_INET6, &acl->addr.addr6, dest, sizeof(dest));
				printf(" addr=%s", dest);
				if(acl->rangetype != acl_range_single) {
					inet_ntop(AF_INET6, &acl->range_mask.addr6, dest, sizeof(dest));
					printf(" rangemask=%s", dest);
				}
#else
				printf(" ip6addr-noip6defined");
#endif
			} else {
				char dest[128];
				inet_ntop(AF_INET, &acl->addr.addr, dest, sizeof(dest));
				printf(" addr=%s", dest);
				if(acl->rangetype != acl_range_single) {
					inet_ntop(AF_INET, &acl->range_mask.addr, dest, sizeof(dest));
					printf(" rangemask=%s", dest);
				}
			}
			printf("\n");
		}
		acl=acl->next;
	}
}

static void
print_acl_ips(const char* varname, acl_options_type* acl)
{
	while(acl)
	{
		printf("\t%s %s\n", varname, acl->ip_address_spec);
		acl=acl->next;
	}
}

void
config_print_zone(nsd_options_type* opt, const char* k, int s, const char *o,
	const char *z, const char* pat, const char* tls, int final)
{
	ip_address_option_type* ip;

	if (k) {
		/* find key */
		key_options_type* key = key_options_find(opt, k);
		if(key) {
			if (s) {
				quote(key->secret);
			} else {
				quote(key->algorithm);
			}
			return;
		}
		printf("Could not find key %s\n", k);
		return;
	}

	if (tls) {
		/* find tlsauth */
		tls_auth_options_type* tlsauth = tls_auth_options_find(opt, tls);
		if(tlsauth) {
			quote(tlsauth->auth_domain_name);
			return;
		}
		printf("Could not find tls-auth %s\n", tls);
		return;
	}

	if (!o) {
		return;
	}

	if (z) {
		zone_options_type* zone;
		const dname_type *dname = dname_parse(opt->region, z);
		if(!dname) {
			printf("Could not parse zone name %s\n", z);
			exit(1);
		}
		zone = zone_options_find(opt, dname);
		if(!zone) {
			printf("Zone does not exist: %s\n", z);
			exit(1);
		}
		ZONE_GET_STR(name, o, zone);
		if(strcasecmp("pattern", o)==0) {
			quote(zone->pattern->pname);
			return;
		}
		ZONE_GET_BIN(part_of_config, o, zone);
		ZONE_GET_PATH(final, zonefile, o, zone->pattern);
		ZONE_GET_ACL(allow_query, o, zone->pattern);
		ZONE_GET_ACL(request_xfr, o, zone->pattern);
		ZONE_GET_ACL(provide_xfr, o, zone->pattern);
		ZONE_GET_ACL(allow_notify, o, zone->pattern);
		ZONE_GET_ACL(notify, o, zone->pattern);
		ZONE_GET_BIN(notify_retry, o, zone->pattern);
		ZONE_GET_STR(zonestats, o, zone->pattern);
		ZONE_GET_OUTGOING(outgoing_interface, o, zone->pattern);
		ZONE_GET_BIN(allow_axfr_fallback, o, zone->pattern);
		ZONE_GET_INT(max_refresh_time, o, zone->pattern);
		ZONE_GET_INT(min_refresh_time, o, zone->pattern);
		ZONE_GET_INT(max_retry_time, o, zone->pattern);
		ZONE_GET_INT(min_retry_time, o, zone->pattern);
		ZONE_GET_INT(min_expire_time, o, zone->pattern);
		ZONE_GET_INT(size_limit_xfr, o, zone->pattern);
#ifdef RATELIMIT
		ZONE_GET_RRL(rrl_whitelist, o, zone->pattern);
#endif
		ZONE_GET_BIN(multi_primary_check, o, zone->pattern);
		ZONE_GET_BIN(store_ixfr, o, zone->pattern);
		ZONE_GET_INT(ixfr_size, o, zone->pattern);
		ZONE_GET_INT(ixfr_number, o, zone->pattern);
		ZONE_GET_BIN(create_ixfr, o, zone->pattern);
		printf("Zone option not handled: %s %s\n", z, o);
		exit(1);
	} else if(pat) {
		pattern_options_type* p = pattern_options_find(opt, pat);
		if(!p) {
			printf("Pattern does not exist: %s\n", pat);
			exit(1);
		}
		if(strcasecmp("name", o)==0) {
			quote(p->pname);
			return;
		}
		ZONE_GET_STR(zonefile, o, p);
		ZONE_GET_PATH(final, zonefile, o, p);
		ZONE_GET_ACL(allow_query, o, p);
		ZONE_GET_ACL(request_xfr, o, p);
		ZONE_GET_ACL(provide_xfr, o, p);
		ZONE_GET_ACL(allow_notify, o, p);
		ZONE_GET_ACL(notify, o, p);
		ZONE_GET_BIN(notify_retry, o, p);
		ZONE_GET_STR(zonestats, o, p);
		ZONE_GET_OUTGOING(outgoing_interface, o, p);
		ZONE_GET_BIN(allow_axfr_fallback, o, p);
		ZONE_GET_INT(max_refresh_time, o, p);
		ZONE_GET_INT(min_refresh_time, o, p);
		ZONE_GET_INT(max_retry_time, o, p);
		ZONE_GET_INT(min_retry_time, o, p);
		ZONE_GET_INT(min_expire_time, o, p);
		ZONE_GET_INT(size_limit_xfr, o, p);
#ifdef RATELIMIT
		ZONE_GET_RRL(rrl_whitelist, o, p);
#endif
		ZONE_GET_BIN(multi_primary_check, o, p);
		ZONE_GET_BIN(store_ixfr, o, p);
		ZONE_GET_INT(ixfr_size, o, p);
		ZONE_GET_INT(ixfr_number, o, p);
		ZONE_GET_BIN(create_ixfr, o, p);
		printf("Pattern option not handled: %s %s\n", pat, o);
		exit(1);
	} else {
		/* look in the server section */
		SERV_GET_IP(ip_address, ip_addresses, o);
		/* bin */
		SERV_GET_BIN(ip_transparent, o);
		SERV_GET_BIN(ip_freebind, o);
		SERV_GET_BIN(debug_mode, o);
		SERV_GET_BIN(do_ip4, o);
		SERV_GET_BIN(do_ip6, o);
		SERV_GET_BIN(reuseport, o);
		SERV_GET_BIN(hide_version, o);
		SERV_GET_BIN(hide_identity, o);
		SERV_GET_BIN(drop_updates, o);
		SERV_GET_BIN(reload_config, o);
		SERV_GET_BIN(zonefiles_check, o);
		SERV_GET_BIN(log_time_ascii, o);
		SERV_GET_BIN(log_time_iso, o);
		SERV_GET_BIN(round_robin, o);
		SERV_GET_BIN(minimal_responses, o);
		SERV_GET_BIN(confine_to_zone, o);
		SERV_GET_BIN(refuse_any, o);
		SERV_GET_BIN(tcp_reject_overflow, o);
		SERV_GET_BIN(log_only_syslog, o);
		/* str */
		SERV_GET_STR(identity, o);
		SERV_GET_STR(version, o);
		SERV_GET_STR(nsid, o);
		SERV_GET_PATH(final, logfile, o);
		SERV_GET_PATH(final, pidfile, o);
		SERV_GET_STR(chroot, o);
		SERV_GET_STR(username, o);
		SERV_GET_PATH(final, zonesdir, o);
		SERV_GET_PATH(final, xfrdfile, o);
		SERV_GET_PATH(final, xfrdir, o);
		SERV_GET_PATH(final, zonelistfile, o);
		SERV_GET_STR(port, o);
		SERV_GET_STR(tls_service_key, o);
		SERV_GET_STR(tls_service_ocsp, o);
		SERV_GET_STR(tls_service_pem, o);
		SERV_GET_STR(tls_port, o);
		SERV_GET_STR(tls_cert_bundle, o);
		SERV_GET_STR(cookie_secret, o);
		SERV_GET_STR(cookie_staging_secret, o);
		SERV_GET_STR(cookie_secret_file, o);
		SERV_GET_BIN(answer_cookie, o);
		/* int */
		SERV_GET_INT(server_count, o);
		SERV_GET_INT(tcp_count, o);
		SERV_GET_INT(tcp_query_count, o);
		SERV_GET_INT(tcp_timeout, o);
		SERV_GET_INT(tcp_mss, o);
		SERV_GET_INT(outgoing_tcp_mss, o);
		SERV_GET_INT(xfrd_tcp_max, o);
		SERV_GET_INT(xfrd_tcp_pipeline, o);
		SERV_GET_INT(ipv4_edns_size, o);
		SERV_GET_INT(ipv6_edns_size, o);
		SERV_GET_INT(statistics, o);
		SERV_GET_INT(xfrd_reload_timeout, o);
		SERV_GET_INT(verbosity, o);
		SERV_GET_INT(send_buffer_size, o);
		SERV_GET_INT(receive_buffer_size, o);
#ifdef RATELIMIT
		SERV_GET_INT(rrl_size, o);
		SERV_GET_INT(rrl_ratelimit, o);
		SERV_GET_INT(rrl_slip, o);
		SERV_GET_INT(rrl_ipv4_prefix_length, o);
		SERV_GET_INT(rrl_ipv6_prefix_length, o);
		SERV_GET_INT(rrl_whitelist_ratelimit, o);
#endif
#ifdef USE_METRICS
		SERV_GET_BIN(metrics_enable, o);
		SERV_GET_IP(metrics_interface, metrics_interface, o);
		SERV_GET_INT(metrics_port, o);
		SERV_GET_STR(metrics_path, o);
#endif /* USE_METRICS */
#ifdef USE_DNSTAP
		SERV_GET_BIN(dnstap_enable, o);
		SERV_GET_STR(dnstap_socket_path, o);
		SERV_GET_STR(dnstap_ip, o);
		SERV_GET_BIN(dnstap_tls, o);
		SERV_GET_STR(dnstap_tls_server_name, o);
		SERV_GET_STR(dnstap_tls_cert_bundle, o);
		SERV_GET_STR(dnstap_tls_client_key_file, o);
		SERV_GET_STR(dnstap_tls_client_cert_file, o);
		SERV_GET_BIN(dnstap_send_identity, o);
		SERV_GET_BIN(dnstap_send_version, o);
		SERV_GET_STR(dnstap_identity, o);
		SERV_GET_STR(dnstap_version, o);
		SERV_GET_BIN(dnstap_log_auth_query_messages, o);
		SERV_GET_BIN(dnstap_log_auth_response_messages, o);
#endif
		SERV_GET_INT(zonefiles_write, o);
		/* remote control */
		SERV_GET_BIN(control_enable, o);
		SERV_GET_IP(control_interface, control_interface, o);
		SERV_GET_INT(control_port, o);
		SERV_GET_STR(server_key_file, o);
		SERV_GET_STR(server_cert_file, o);
		SERV_GET_STR(control_key_file, o);
		SERV_GET_STR(control_cert_file, o);

		if(strcasecmp(o, "zones") == 0) {
			zone_options_type* zone;
			RBTREE_FOR(zone, zone_options_type*, opt->zone_options)
				quote(zone->name);
			return;
		}
		if(strcasecmp(o, "patterns") == 0) {
			pattern_options_type* p;
			RBTREE_FOR(p, pattern_options_type*, opt->patterns)
				quote(p->pname);
			return;
		}
		if(strcasecmp(o, "proxy_protocol_port") == 0) {
			struct proxy_protocol_port_list* p;
			for(p = opt->proxy_protocol_port; p; p = p->next)
				printf("%d\n", p->port);
			return;
		}
		printf("Server option not handled: %s\n", o);
		exit(1);
	}
}

/* print zone content items */
static void print_zone_content_elems(pattern_options_type* pat)
{
	if(pat->zonefile)
		print_string_var("zonefile:", pat->zonefile);
#ifdef RATELIMIT
	zone_print_rrl_whitelist("\trrl-whitelist: ", pat->rrl_whitelist);
#endif
	print_acl("allow_query:", pat->allow_query);
	print_acl("allow-notify:", pat->allow_notify);
	print_acl("request-xfr:", pat->request_xfr);
	if(pat->multi_primary_check)
		printf("\tmulti-primary-check: %s\n", pat->multi_primary_check?"yes":"no");
	if(!pat->notify_retry_is_default)
		printf("\tnotify-retry: %d\n", pat->notify_retry);
	print_acl("notify:", pat->notify);
	print_acl("provide-xfr:", pat->provide_xfr);
	if(pat->zonestats)
		print_string_var("zonestats:", pat->zonestats);
	print_acl_ips("outgoing-interface:", pat->outgoing_interface);
	if(!pat->allow_axfr_fallback_is_default)
		printf("\tallow-axfr-fallback: %s\n",
			pat->allow_axfr_fallback?"yes":"no");
	if(!pat->max_refresh_time_is_default)
		printf("\tmax-refresh-time: %d\n", pat->max_refresh_time);
	if(!pat->min_refresh_time_is_default)
		printf("\tmin-refresh-time: %d\n", pat->min_refresh_time);
	if(!pat->max_retry_time_is_default)
		printf("\tmax-retry-time: %d\n", pat->max_retry_time);
	if(!pat->min_retry_time_is_default)
		printf("\tmin-retry-time: %d\n", pat->min_retry_time);
	if(pat->min_expire_time_expr == REFRESHPLUSRETRYPLUS1)
		printf("\tmin-expire-time: " REFRESHPLUSRETRYPLUS1_STR "\n");
	else if(pat->min_expire_time_expr == EXPIRE_TIME_HAS_VALUE)
		printf("\tmin-expire-time: %d\n", pat->min_expire_time);
	if(pat->size_limit_xfr != 0)
		printf("\tsize-limit-xfr: %llu\n",
			(long long unsigned)pat->size_limit_xfr);
	if(!pat->store_ixfr_is_default)
		printf("\tstore-ixfr: %s\n", pat->store_ixfr?"yes":"no");
	if(!pat->ixfr_number_is_default)
		printf("\tixfr-number: %u\n", (unsigned)pat->ixfr_number);
	if(!pat->ixfr_size_is_default)
		printf("\tixfr-size: %u\n", (unsigned)pat->ixfr_size);
	if(!pat->create_ixfr_is_default)
		printf("\tcreate-ixfr: %s\n", pat->create_ixfr?"yes":"no");
	if(pat->verify_zone != VERIFY_ZONE_INHERIT) {
		printf("\tverify-zone: ");
		if(pat->verify_zone) {
			printf("yes\n");
		} else {
			printf("no\n");
		}
	}
	if(pat->verifier) {
		printf("\tverifier:");
		for(char *const *s = pat->verifier; *s; s++) {
			printf(" \"%s\"", *s);
		}
		printf("\n");
	}
	if(pat->verifier_feed_zone != VERIFIER_FEED_ZONE_INHERIT) {
		printf("\tverifier-feed-zone: ");
		if(pat->verifier_feed_zone) {
			printf("yes\n");
		} else {
			printf("no\n");
		}
	}
	if(pat->verifier_timeout != VERIFIER_TIMEOUT_INHERIT) {
		printf("\tverifier-timeout: %d\n", pat->verifier_timeout);
	}

	if(!pat->catalog_role_is_default)
	    switch(pat->catalog_role) {
	case CATALOG_ROLE_CONSUMER: printf("\tcatalog: consumer\n");
	                            break;
	case CATALOG_ROLE_PRODUCER: printf("\tcatalog: producer\n");
	                            break;
	default                   : break;
	}

	if(pat->catalog_member_pattern)
		print_string_var("catalog-member-pattern:", pat->catalog_member_pattern);
	if(pat->catalog_producer_zone)
		print_string_var("catalog-producer-zone:", pat->catalog_producer_zone);
}

void
config_test_print_server(nsd_options_type* opt)
{
	ip_address_option_type* ip;
	key_options_type* key;
	tls_auth_options_type* tlsauth;
	zone_options_type* zone;
	pattern_options_type* pat;

	printf("# Config settings.\n");
	printf("server:\n");
	printf("\tdebug-mode: %s\n", opt->debug_mode?"yes":"no");
	printf("\tip-transparent: %s\n", opt->ip_transparent?"yes":"no");
	printf("\tip-freebind: %s\n", opt->ip_freebind?"yes":"no");
	printf("\treuseport: %s\n", opt->reuseport?"yes":"no");
	printf("\tdo-ip4: %s\n", opt->do_ip4?"yes":"no");
	printf("\tdo-ip6: %s\n", opt->do_ip6?"yes":"no");
	printf("\tsend-buffer-size: %d\n", opt->send_buffer_size);
	printf("\treceive-buffer-size: %d\n", opt->receive_buffer_size);
	printf("\thide-version: %s\n", opt->hide_version?"yes":"no");
	printf("\thide-identity: %s\n", opt->hide_identity?"yes":"no");
	printf("\tdrop-updates: %s\n", opt->drop_updates?"yes":"no");
	printf("\ttcp-reject-overflow: %s\n",
		opt->tcp_reject_overflow ? "yes" : "no");
	print_string_var("identity:", opt->identity);
	print_string_var("version:", opt->version);
	print_string_var("nsid:", opt->nsid);
	print_string_var("logfile:", opt->logfile);
	printf("\tlog-only-syslog: %s\n", opt->log_only_syslog?"yes":"no");
	printf("\tserver-count: %d\n", opt->server_count);
	if(opt->cpu_affinity) {
		cpu_option_type *n;
		printf("\tcpu-affinity:");
		for(n = opt->cpu_affinity; n; n = n->next) {
			printf(" %d", n->cpu);
		}
		printf("\n");
	}
	if(opt->cpu_affinity && opt->service_cpu_affinity) {
		cpu_map_option_type *n;
		for(n = opt->service_cpu_affinity; n; n = n->next) {
			if(n->service > 0) {
				printf("\tserver-%d-cpu-affinity: %d\n",
				       n->service, n->cpu);
			} else if(n->service == -1) {
				printf("\txfrd-cpu-affinity: %d\n",
				       n->cpu);
			}
		}
	}
	printf("\ttcp-count: %d\n", opt->tcp_count);
	printf("\ttcp-query-count: %d\n", opt->tcp_query_count);
	printf("\ttcp-timeout: %d\n", opt->tcp_timeout);
	printf("\ttcp-mss: %d\n", opt->tcp_mss);
	printf("\toutgoing-tcp-mss: %d\n", opt->outgoing_tcp_mss);
	printf("\txfrd-tcp-max: %d\n", opt->xfrd_tcp_max);
	printf("\txfrd-tcp-pipeline: %d\n", opt->xfrd_tcp_pipeline);
	printf("\tipv4-edns-size: %d\n", (int) opt->ipv4_edns_size);
	printf("\tipv6-edns-size: %d\n", (int) opt->ipv6_edns_size);
	print_string_var("pidfile:", opt->pidfile);
	print_string_var("port:", opt->port);
	printf("\tstatistics: %d\n", opt->statistics);
	print_string_var("chroot:", opt->chroot);
	print_string_var("username:", opt->username);
	print_string_var("zonesdir:", opt->zonesdir);
	print_string_var("xfrdfile:", opt->xfrdfile);
	print_string_var("zonelistfile:", opt->zonelistfile);
	print_string_var("xfrdir:", opt->xfrdir);
	printf("\txfrd-reload-timeout: %d\n", opt->xfrd_reload_timeout);
	printf("\tlog-time-ascii: %s\n", opt->log_time_ascii?"yes":"no");
	printf("\tlog-time-iso: %s\n", opt->log_time_iso?"yes":"no");
	printf("\tround-robin: %s\n", opt->round_robin?"yes":"no");
	printf("\tminimal-responses: %s\n", opt->minimal_responses?"yes":"no");
	printf("\tconfine-to-zone: %s\n",
		opt->confine_to_zone ? "yes" : "no");
	printf("\trefuse-any: %s\n", opt->refuse_any?"yes":"no");
	printf("\tverbosity: %d\n", opt->verbosity);
	for(ip = opt->ip_addresses; ip; ip=ip->next)
	{
		printf("\tip-address: %s", ip->address);
		if(ip->servers) {
			const char *sep;
			struct range_option *n;
			printf(" servers=\"");
			for(n=ip->servers, sep=""; n; n = n->next, sep=" ") {
				if(n->first == n->last) {
					printf("%s%d", sep, n->first);
				} else {
					printf("%s%d-%d", sep, n->first, n->last);
				}
			}
			printf("\"");
		}
		if(ip->fib != -1) {
			printf(" setfib=%d", ip->fib);
		}
		printf("\n");
	}
#ifdef RATELIMIT
	printf("\trrl-size: %d\n", (int)opt->rrl_size);
	printf("\trrl-ratelimit: %d\n", (int)opt->rrl_ratelimit);
	printf("\trrl-slip: %d\n", (int)opt->rrl_slip);
	printf("\trrl-ipv4-prefix-length: %d\n", (int)opt->rrl_ipv4_prefix_length);
	printf("\trrl-ipv6-prefix-length: %d\n", (int)opt->rrl_ipv6_prefix_length);
	printf("\trrl-whitelist-ratelimit: %d\n", (int)opt->rrl_whitelist_ratelimit);
#endif
	printf("\treload-config: %s\n", opt->reload_config?"yes":"no");
	printf("\tzonefiles-check: %s\n", opt->zonefiles_check?"yes":"no");
	printf("\tzonefiles-write: %d\n", opt->zonefiles_write);
	print_string_var("tls-service-key:", opt->tls_service_key);
	print_string_var("tls-service-pem:", opt->tls_service_pem);
	print_string_var("tls-service-ocsp:", opt->tls_service_ocsp);
	print_string_var("tls-port:", opt->tls_port);
	print_string_var("tls-cert-bundle:", opt->tls_cert_bundle);
	printf("\tanswer-cookie: %s\n", opt->answer_cookie?"yes":"no");
	print_string_var("cookie-secret:", opt->cookie_secret);
	print_string_var("cookie-staging-secret:", opt->cookie_staging_secret);
	if(opt->cookie_secret_file_is_default) {
		print_string_var("#cookie-secret-file:", opt->cookie_secret_file);
	} else if(opt->cookie_secret_file) {
		print_string_var("cookie-secret-file:", opt->cookie_secret_file);
	} else {
		print_string_var("cookie-secret-file:", "");
	}
	if(opt->proxy_protocol_port) {
		struct proxy_protocol_port_list* p;
		for(p = opt->proxy_protocol_port; p; p = p->next)
			printf("\tproxy-protocol-port: %d\n", p->port);
	}

#ifdef USE_METRICS
	printf("\tmetrics-enable: %s\n", opt->metrics_enable?"yes":"no");
	for(ip = opt->metrics_interface; ip; ip=ip->next)
		print_string_var("metrics-interface:", ip->address);
	printf("\tmetrics-port: %d\n", opt->metrics_port);
	print_string_var("metrics-path:", opt->metrics_path);
#endif /* USE_METRICS */

#ifdef USE_DNSTAP
	printf("\ndnstap:\n");
	printf("\tdnstap-enable: %s\n", opt->dnstap_enable?"yes":"no");
	print_string_var("dnstap-socket-path:", opt->dnstap_socket_path);
	print_string_var("dnstap-ip:", opt->dnstap_ip);
	printf("\tdnstap-tls: %s\n", opt->dnstap_tls?"yes":"no");
	print_string_var("dnstap-tls-server-name:", opt->dnstap_tls_server_name);
	print_string_var("dnstap-tls-cert-bundle:", opt->dnstap_tls_cert_bundle);
	print_string_var("dnstap-tls-client-key-file:", opt->dnstap_tls_client_key_file);
	print_string_var("dnstap-tls-client-cert-file:", opt->dnstap_tls_client_cert_file);
	printf("\tdnstap-send-identity: %s\n", opt->dnstap_send_identity?"yes":"no");
	printf("\tdnstap-send-version: %s\n", opt->dnstap_send_version?"yes":"no");
	print_string_var("dnstap-identity:", opt->dnstap_identity);
	print_string_var("dnstap-version:", opt->dnstap_version);
	printf("\tdnstap-log-auth-query-messages: %s\n", opt->dnstap_log_auth_query_messages?"yes":"no");
	printf("\tdnstap-log-auth-response-messages: %s\n", opt->dnstap_log_auth_response_messages?"yes":"no");
#endif

	printf("\nremote-control:\n");
	printf("\tcontrol-enable: %s\n", opt->control_enable?"yes":"no");
	for(ip = opt->control_interface; ip; ip=ip->next)
		print_string_var("control-interface:", ip->address);
	printf("\tcontrol-port: %d\n", opt->control_port);
	print_string_var("server-key-file:", opt->server_key_file);
	print_string_var("server-cert-file:", opt->server_cert_file);
	print_string_var("control-key-file:", opt->control_key_file);
	print_string_var("control-cert-file:", opt->control_cert_file);

	printf("\nverify:\n");
	printf("\tenable: %s\n", opt->verify_enable?"yes":"no");
	for(ip = opt->verify_ip_addresses; ip; ip=ip->next) {
		print_string_var("ip-address:", ip->address);
	}
	printf("\tport: %s\n", opt->verify_port);
	printf("\tverify-zones: %s\n", opt->verify_zones?"yes":"no");
	if(opt->verifier) {
		printf("\tverifier:");
		for(char **s = opt->verifier; *s; s++) {
			printf(" \"%s\"", *s);
		}
		printf("\n");
	}
	printf("\tverifier-count: %d\n", opt->verifier_count);
	printf("\tverifier-feed-zone: %s\n", opt->verifier_feed_zone?"yes":"no");
	printf("\tverifier-timeout: %d\n", opt->verifier_timeout);

	RBTREE_FOR(key, key_options_type*, opt->keys)
	{
		printf("\nkey:\n");
		print_string_var("name:", key->name);
		print_string_var("algorithm:", key->algorithm);
		print_string_var("secret:", key->secret);
	}
	RBTREE_FOR(tlsauth, tls_auth_options_type*, opt->tls_auths)
	{
		printf("\ntls-auth:\n");
		print_string_var("name:", tlsauth->name);
		print_string_var("auth-domain-name:", tlsauth->auth_domain_name);
	}
	RBTREE_FOR(pat, pattern_options_type*, opt->patterns)
	{
		if(pat->implicit) continue;
		printf("\npattern:\n");
		print_string_var("name:", pat->pname);
		print_zone_content_elems(pat);
	}
	RBTREE_FOR(zone, zone_options_type*, opt->zone_options)
	{
		if(!zone->part_of_config)
			continue;
		printf("\nzone:\n");
		print_string_var("name:", zone->name);
		print_zone_content_elems(zone->pattern);
	}
}

static int
additional_checks(nsd_options_type* opt, const char* filename)
{
	zone_options_type* zone;
	int errors = 0;

	RBTREE_FOR(zone, zone_options_type*, opt->zone_options)
	{
		const dname_type* dname = dname_parse(opt->region, zone->name); /* memory leak. */
		if(!dname) {
			fprintf(stderr, "%s: cannot parse zone name syntax for zone %s.\n", filename, zone->name);
			errors ++;
			continue;
		}
		if(zone->pattern->allow_notify && !zone->pattern->request_xfr) {
			fprintf(stderr, "%s: zone %s has allow-notify but no request-xfr"
				" items. Where can it get a zone transfer when a notify "
				"is received?\n", filename, zone->name);
			errors ++;
		}
		if(!zone_is_slave(zone) && !zone_is_catalog_producer(zone)
		&& (!zone->pattern->zonefile || zone->pattern->zonefile[0] == 0)) {
			fprintf(stderr, "%s: zone %s is a primary zone but has "
				"no zonefile. Where can the data come from?\n",
				filename, zone->name);
			errors ++;
		}
	}

#ifndef BIND8_STATS
	if(opt->statistics > 0)
	{
		fprintf(stderr, "%s: 'statistics: %d' but BIND 8 statistics feature not enabled.\n",
			filename, opt->statistics);
		errors ++;
	}
#endif
#ifndef HAVE_CHROOT
	if(opt->chroot != 0)
	{
		fprintf(stderr, "%s: chroot %s given. chroot not supported on this platform.\n",
			filename, opt->chroot);
		errors ++;
	}
#endif
	if (opt->identity && strlen(opt->identity) > UCHAR_MAX) {
                fprintf(stderr, "%s: server identity too long (%u characters)\n",
                      filename, (unsigned) strlen(opt->identity));
		errors ++;
        }
	if (opt->version && strlen(opt->version) > UCHAR_MAX) {
                fprintf(stderr, "%s: server version too long (%u characters)\n",
                      filename, (unsigned) strlen(opt->version));
		errors ++;
        }

	/* not done here: parsing of ip-address. parsing of username. */

        if (opt->chroot && opt->chroot[0]) {
		/* append trailing slash for strncmp checking */
		append_trailing_slash(&opt->chroot, opt->region);
		append_trailing_slash(&opt->xfrdir, opt->region);
		append_trailing_slash(&opt->zonesdir, opt->region);

		/* zonesdir must be absolute and within chroot,
		 * all other pathnames may be relative to zonesdir */
		if (strncmp(opt->zonesdir, opt->chroot, strlen(opt->chroot)) != 0) {
			fprintf(stderr, "%s: zonesdir %s has to be an absolute path that starts with the chroot path %s\n",
				filename, opt->zonesdir, opt->chroot);
			errors ++;
                }
		if (!file_inside_chroot(opt->pidfile, opt->chroot)) {
			fprintf(stderr, "%s: pidfile %s is not relative to chroot %s.\n",
				filename, opt->pidfile, opt->chroot);
			errors ++;
                }
		if (!file_inside_chroot(opt->xfrdfile, opt->chroot)) {
			fprintf(stderr, "%s: xfrdfile %s is not relative to chroot %s.\n",
				filename, opt->xfrdfile, opt->chroot);
			errors ++;
                }
		if (!file_inside_chroot(opt->zonelistfile, opt->chroot)) {
			fprintf(stderr, "%s: zonelistfile %s is not relative to chroot %s.\n",
				filename, opt->zonelistfile, opt->chroot);
			errors ++;
                }
		if (!file_inside_chroot(opt->xfrdir, opt->chroot)) {
			fprintf(stderr, "%s: xfrdir %s is not relative to chroot %s.\n",
				filename, opt->xfrdir, opt->chroot);
			errors ++;
                }
	}

	if (atoi(opt->port) <= 0) {
		fprintf(stderr, "%s: port number '%s' is not a positive number.\n",
			filename, opt->port);
		errors ++;
	}
	if(errors != 0) {
		fprintf(stderr, "%s: %d semantic errors in %d zones, %d keys, %d tls-auth.\n",
			filename, errors, (int)nsd_options_num_zones(opt),
			(int)opt->keys->count,
			(int)opt->tls_auths->count);
	}

	return (errors == 0);
}

int
main(int argc, char* argv[])
{
	int c;
	int verbose = 0;
	int key_sec = 0;
	int final = 0;
	const char * conf_opt = NULL; /* what option do you want? Can be NULL -> print all */
	const char * conf_zone = NULL; /* what zone are we talking about */
	const char * conf_key = NULL; /* what key is needed */
	const char * conf_tlsauth = NULL; /* what tls-auth is needed */
	const char * conf_pat = NULL; /* what pattern is talked about */
	const char* configfile;
	nsd_options_type *options;

	log_init("nsd-checkconf");

	/* Parse the command line... */
	while ((c = getopt(argc, argv, "vfho:a:p:s:z:t:")) != -1) {
		switch (c) {
		case 'v':
			verbose = 1;
			verbosity++;
			break;
		case 'o':
			conf_opt = optarg;
			break;
		case 'f':
			final = 1;
			break;
		case 'p':
			conf_pat = optarg;
			break;
		case 'a':
			if (conf_key) {
				fprintf(stderr, "Error: cannot combine -a with -s or other -a.\n");
				exit(1);
			}
			conf_key = optarg;
			break;
		case 's':
			if (conf_key) {
				fprintf(stderr, "Error: cannot combine -s with -a or other -s.\n");
				exit(1);
			}
			conf_key = optarg;
			key_sec = 1;
			break;
		case 't':
			conf_tlsauth = optarg;
			break;
		case 'z':
			conf_zone = optarg;
			break;
		case 'h':
		default:
			usage();
		};
	}
	argc -= optind;
	argv += optind;
	if (argc == 0 || argc>=2) {
		usage();
	}
	configfile = argv[0];

	/* read config file */
	options = nsd_options_create(region_create(xalloc, free));
	tsig_init(options->region);
	if (!parse_options_file(options, configfile, NULL, NULL, NULL) ||
	   !additional_checks(options, configfile)) {
		exit(2);
	}
	if (conf_opt || conf_key || conf_tlsauth) {
		config_print_zone(options, conf_key, key_sec,
			underscore(conf_opt), conf_zone, conf_pat,  conf_tlsauth, final);
	} else {
		if (verbose) {
			printf("# Read file %s: %d patterns, %d fixed-zones, "
				"%d keys, %d tls-auth.\n",
				configfile,
				(int)options->patterns->count,
				(int)nsd_options_num_zones(options),
				(int)options->keys->count,
				(int)options->tls_auths->count);
			config_test_print_server(options);
		}
	}
	return 0;
}
