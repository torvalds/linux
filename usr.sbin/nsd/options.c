/*
 * options.c -- options functions.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif
#ifdef HAVE_SSL
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include "options.h"
#include "query.h"
#include "tsig.h"
#include "ixfr.h"
#include "difffile.h"
#include "rrl.h"
#include "bitset.h"
#include "xfrd.h"

#include "configparser.h"
config_parser_state_type* cfg_parser = 0;
extern FILE* c_in, *c_out;
int c_parse(void);
int c_lex(void);
int c_wrap(void);
int c_lex_destroy(void);
extern char* c_text;

static int
rbtree_strcmp(const void* p1, const void* p2)
{
	if(p1 == NULL && p2 == NULL) return 0;
	if(p1 == NULL) return -1;
	if(p2 == NULL) return 1;
	return strcmp((const char*)p1, (const char*)p2);
}

struct nsd_options*
nsd_options_create(region_type* region)
{
	struct nsd_options* opt;
	opt = (struct nsd_options*)region_alloc(region, sizeof(
		struct nsd_options));
	opt->region = region;
	opt->zone_options = rbtree_create(region,
		(int (*)(const void *, const void *)) dname_compare);
	opt->configfile = NULL;
	opt->zonestatnames = rbtree_create(opt->region, rbtree_strcmp);
	opt->patterns = rbtree_create(region, rbtree_strcmp);
	opt->keys = rbtree_create(region, rbtree_strcmp);
	opt->tls_auths = rbtree_create(region, rbtree_strcmp);
	opt->ip_addresses = NULL;
	opt->ip_transparent = 0;
	opt->ip_freebind = 0;
	opt->send_buffer_size = 0;
	opt->receive_buffer_size = 0;
	opt->debug_mode = 0;
	opt->verbosity = 0;
	opt->hide_version = 0;
	opt->hide_identity = 0;
	opt->drop_updates = 0;
	opt->do_ip4 = 1;
	opt->do_ip6 = 1;
	opt->identity = 0;
	opt->version = 0;
	opt->nsid = 0;
	opt->logfile = 0;
	opt->log_only_syslog = 0;
	opt->log_time_ascii = 1;
	opt->log_time_iso = 0;
	opt->round_robin = 0; /* also packet.h::round_robin */
	opt->minimal_responses = 1; /* also packet.h::minimal_responses */
	opt->confine_to_zone = 0;
	opt->refuse_any = 1;
	opt->server_count = 1;
	opt->cpu_affinity = NULL;
	opt->service_cpu_affinity = NULL;
	opt->tcp_count = 100;
	opt->tcp_reject_overflow = 0;
	opt->tcp_query_count = 0;
	opt->tcp_timeout = TCP_TIMEOUT;
	opt->tcp_mss = 0;
	opt->outgoing_tcp_mss = 0;
	opt->ipv4_edns_size = EDNS_MAX_MESSAGE_LEN;
	opt->ipv6_edns_size = EDNS_MAX_MESSAGE_LEN;
	opt->pidfile = PIDFILE;
	opt->port = UDP_PORT;
/* deprecated?	opt->port = TCP_PORT; */
	opt->reuseport = 0;
	opt->xfrd_tcp_max = 128;
	opt->xfrd_tcp_pipeline = 128;
	opt->statistics = 0;
	opt->chroot = 0;
	opt->username = USER;
	opt->zonesdir = ZONESDIR;
	opt->xfrdfile = XFRDFILE;
	opt->xfrdir = XFRDIR;
	opt->zonelistfile = ZONELISTFILE;
#ifdef RATELIMIT
	opt->rrl_size = RRL_BUCKETS;
	opt->rrl_slip = RRL_SLIP;
	opt->rrl_ipv4_prefix_length = RRL_IPV4_PREFIX_LENGTH;
	opt->rrl_ipv6_prefix_length = RRL_IPV6_PREFIX_LENGTH;
#  ifdef RATELIMIT_DEFAULT_OFF
	opt->rrl_ratelimit = 0;
	opt->rrl_whitelist_ratelimit = 0;
#  else
	opt->rrl_ratelimit = RRL_LIMIT/2;
	opt->rrl_whitelist_ratelimit = RRL_WLIST_LIMIT/2;
#  endif
#endif
#ifdef USE_DNSTAP
	opt->dnstap_enable = 0;
	opt->dnstap_socket_path = DNSTAP_SOCKET_PATH;
	opt->dnstap_ip = "";
	opt->dnstap_tls = 1;
	opt->dnstap_tls_server_name = NULL;
	opt->dnstap_tls_cert_bundle = NULL;
	opt->dnstap_tls_client_key_file = NULL;
	opt->dnstap_tls_client_cert_file = NULL;
	opt->dnstap_send_identity = 0;
	opt->dnstap_send_version = 0;
	opt->dnstap_identity = NULL;
	opt->dnstap_version = NULL;
	opt->dnstap_log_auth_query_messages = 0;
	opt->dnstap_log_auth_response_messages = 0;
#endif
	opt->reload_config = 0;
	opt->zonefiles_check = 1;
	opt->zonefiles_write = ZONEFILES_WRITE_INTERVAL;
	opt->xfrd_reload_timeout = 1;
	opt->tls_service_key = NULL;
	opt->tls_service_ocsp = NULL;
	opt->tls_service_pem = NULL;
	opt->tls_port = TLS_PORT;
	opt->tls_auth_port = NULL;
	opt->tls_cert_bundle = NULL;
	opt->tls_auth_xfr_only = 0;
	opt->proxy_protocol_port = NULL;
	opt->answer_cookie = 1;
	opt->cookie_secret = NULL;
	opt->cookie_staging_secret = NULL;
	opt->cookie_secret_file = NULL;
	opt->cookie_secret_file_is_default = 1;
	opt->control_enable = 0;
	opt->control_interface = NULL;
	opt->control_port = NSD_CONTROL_PORT;
	opt->server_key_file = CONFIGDIR"/nsd_server.key";
	opt->server_cert_file = CONFIGDIR"/nsd_server.pem";
	opt->control_key_file = CONFIGDIR"/nsd_control.key";
	opt->control_cert_file = CONFIGDIR"/nsd_control.pem";
#ifdef USE_XDP
	opt->xdp_interface = NULL;
	opt->xdp_program_path = SHAREDFILESDIR"/xdp-dns-redirect_kern.o";
	opt->xdp_program_load = 1;
	opt->xdp_bpffs_path = "/sys/fs/bpf";
	opt->xdp_force_copy = 0;
#endif
#ifdef USE_METRICS
	opt->metrics_enable = 0;
	opt->metrics_interface = NULL;
	opt->metrics_port = NSD_METRICS_PORT;
	opt->metrics_path = "/metrics";
#endif /* USE_METRICS */

	opt->verify_enable = 0;
	opt->verify_ip_addresses = NULL;
	opt->verify_port = VERIFY_PORT;
	opt->verify_zones = 1;
	opt->verifier = NULL;
	opt->verifier_count = 1;
	opt->verifier_feed_zone = 1;
	opt->verifier_timeout = 0;

	return opt;
}

int
nsd_options_insert_zone(struct nsd_options* opt, struct zone_options* zone)
{
	/* create dname for lookup */
	const dname_type* dname = dname_parse(opt->region, zone->name);
	if(!dname)
		return 0;
	zone->node.key = dname;
	if(!rbtree_insert(opt->zone_options, (rbnode_type*)zone))
		return 0;
	return 1;
}

int
nsd_options_insert_pattern(struct nsd_options* opt,
	struct pattern_options* pat)
{
	if(!pat->pname)
		return 0;
	pat->node.key = pat->pname;
	if(!rbtree_insert(opt->patterns, (rbnode_type*)pat))
		return 0;
	return 1;
}

void
warn_if_directory(const char* filetype, FILE* f, const char* fname)
{
	if(fileno(f) != -1) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		if(fstat(fileno(f), &st) != -1) {
			if(S_ISDIR(st.st_mode)) {
				log_msg(LOG_WARNING, "trying to read %s but it is a directory: %s", filetype, fname);
			}
		}
	}
}

int
parse_options_file(struct nsd_options* opt, const char* file,
	void (*err)(void*,const char*), void* err_arg,
	struct nsd_options* old_opts)
{
	FILE *in = 0;
	struct pattern_options* pat;
	struct acl_options* acl;

	if(!cfg_parser) {
		cfg_parser = (config_parser_state_type*)region_alloc(
			opt->region, sizeof(config_parser_state_type));
		cfg_parser->chroot = 0;
	}
	cfg_parser->err = err;
	cfg_parser->err_arg = err_arg;
	cfg_parser->filename = (char*)file;
	cfg_parser->line = 1;
	cfg_parser->errors = 0;
	cfg_parser->opt = opt;
	cfg_parser->pattern = NULL;
	cfg_parser->zone = NULL;
	cfg_parser->key = NULL;
	cfg_parser->tls_auth = NULL;

	in = fopen(cfg_parser->filename, "r");
	if(!in) {
		if(err) {
			char m[MAXSYSLOGMSGLEN];
			snprintf(m, sizeof(m), "Could not open %s: %s\n",
				file, strerror(errno));
			err(err_arg, m);
		} else {
			fprintf(stderr, "Could not open %s: %s\n",
				file, strerror(errno));
		}
		return 0;
	}
	warn_if_directory("configfile", in, file);
	c_in = in;
	c_parse();
	fclose(in);

	opt->configfile = region_strdup(opt->region, file);

	/* Set default cookie_secret_file value */
	if(opt->cookie_secret_file_is_default && !opt->cookie_secret_file) {
		opt->cookie_secret_file =
			region_strdup(opt->region, COOKIESECRETSFILE);
	}
	/* Semantic errors */
	if(opt->cookie_staging_secret && !opt->cookie_secret) {
		c_error("a cookie-staging-secret cannot be configured without "
		        "also providing a cookie-secret");
	}
	RBTREE_FOR(pat, struct pattern_options*, opt->patterns)
	{
		struct pattern_options* old_pat =
			old_opts ? pattern_options_find(old_opts, pat->pname)
			         : NULL;

		/* lookup keys for acls */
		for(acl=pat->allow_notify; acl; acl=acl->next)
		{
			if(acl->nokey || acl->blocked)
				continue;
			acl->key_options = key_options_find(opt, acl->key_name);
			if(!acl->key_options)
				c_error("key %s in pattern %s could not be found",
					acl->key_name, pat->pname);
		}
		for(acl=pat->notify; acl; acl=acl->next)
		{
			if(acl->nokey || acl->blocked)
				continue;
			acl->key_options = key_options_find(opt, acl->key_name);
			if(!acl->key_options)
				c_error("key %s in pattern %s could not be found",
					acl->key_name, pat->pname);
		}
		for(acl=pat->request_xfr; acl; acl=acl->next)
		{
			/* Find tls_auth */
			if (!acl->tls_auth_name)
				; /* pass */
			else if (!(acl->tls_auth_options =
			                tls_auth_options_find(opt, acl->tls_auth_name)))
				c_error("tls_auth %s in pattern %s could not be found",
						acl->tls_auth_name, pat->pname);
			/* Find key */
			if(acl->nokey || acl->blocked)
				continue;
			acl->key_options = key_options_find(opt, acl->key_name);
			if(!acl->key_options)
				c_error("key %s in pattern %s could not be found",
					acl->key_name, pat->pname);
		}
		for(acl=pat->provide_xfr; acl; acl=acl->next)
		{
			/* Find tls_auth */
			if (acl->tls_auth_name) {
				if (!(acl->tls_auth_options =
			                tls_auth_options_find(opt, acl->tls_auth_name)))
				    c_error("tls_auth %s in pattern %s could not be found",
						acl->tls_auth_name, pat->pname);
			}
			if(acl->nokey || acl->blocked)
				continue;
			acl->key_options = key_options_find(opt, acl->key_name);
			if(!acl->key_options)
				c_error("key %s in pattern %s could not be found",
					acl->key_name, pat->pname);
		}
		for(acl=pat->allow_query; acl; acl=acl->next)
		{
			if(acl->nokey || acl->blocked)
				continue;
			acl->key_options = key_options_find(opt, acl->key_name);
			if(!acl->key_options)
				c_error("key %s in pattern %s could not be found",
					acl->key_name, pat->pname);
		}
		/* lookup zones for catalog-producer-zone options */
		if(pat->catalog_producer_zone) {
			struct zone_options* zopt;
			const dname_type *dname = dname_parse(opt->region,
					pat->catalog_producer_zone);
			if(dname == NULL) {
				; /* pass; already erred during parsing */

			} else if (!(zopt = zone_options_find(opt, dname))) {
				c_error("catalog producer zone %s in pattern "
					"%s could not be found",
					pat->catalog_producer_zone,
					pat->pname);

			} else if (!zone_is_catalog_producer(zopt)) {
				c_error("catalog-producer-zone %s in pattern "
					"%s is not configered as a "
					"catalog: producer",
					pat->catalog_producer_zone,
					pat->pname);
			}
		}
		if( !old_opts /* Okay to add a cat producer member zone pat */
		|| (!old_pat) /* But not to add, change or del an existing */
		|| ( old_pat && !old_pat->catalog_producer_zone
		             &&     !pat->catalog_producer_zone)
		|| ( old_pat &&  old_pat->catalog_producer_zone
		             &&      pat->catalog_producer_zone
		             && strcmp( old_pat->catalog_producer_zone
		                      ,     pat->catalog_producer_zone) == 0)){
			; /* No existing catalog producer member zone added
			   * or changed. Everyting is fine: pass */
		} else {
			c_error("catalog-producer-zone in pattern %s cannot "
				"be removed or changed on a running NSD",
				pat->pname);
		}
	}

	if(cfg_parser->errors > 0)
	{
		if(err) {
			char m[MAXSYSLOGMSGLEN];
			snprintf(m, sizeof(m), "read %s failed: %d errors in "
				"configuration file\n", file,
				cfg_parser->errors);
			err(err_arg, m);
		} else {
			fprintf(stderr, "read %s failed: %d errors in "
				"configuration file\n", file,
				cfg_parser->errors);
		}
		return 0;
	}
	return 1;
}

void options_zonestatnames_create(struct nsd_options* opt)
{
	struct zone_options* zopt;
	/* allocate "" as zonestat 0, for zones without a zonestat */
	if(!rbtree_search(opt->zonestatnames, "")) {
		struct zonestatname* n;
		n = (struct zonestatname*)region_alloc_zero(opt->region,
			sizeof(*n));
		n->node.key = region_strdup(opt->region, "");
		if(!n->node.key) {
			log_msg(LOG_ERR, "malloc failed: %s", strerror(errno));
			exit(1);
		}
		n->id = (unsigned)(opt->zonestatnames->count);
		rbtree_insert(opt->zonestatnames, (rbnode_type*)n);
	}
	RBTREE_FOR(zopt, struct zone_options*, opt->zone_options) {
		/* insert into tree, so that when read in later id exists */
		(void)getzonestatid(opt, zopt);
	}
}

#define ZONELIST_HEADER "# NSD zone list\n# name pattern\n"
static int
comp_zonebucket(const void* a, const void* b)
{
	/* the line size is much smaller than max-int, and positive,
	 * so the subtraction works */
	return *(const int*)b - *(const int*)a;
}

/* insert free entry into zonelist free buckets */
static void
zone_list_free_insert(struct nsd_options* opt, int linesize, off_t off)
{
	struct zonelist_free* e;
	struct zonelist_bucket* b = (struct zonelist_bucket*)rbtree_search(
		opt->zonefree, &linesize);
	if(!b) {
		b = region_alloc_zero(opt->region, sizeof(*b));
		b->linesize = linesize;
		b->node = *RBTREE_NULL;
		b->node.key = &b->linesize;
		rbtree_insert(opt->zonefree, &b->node);
	}
	e = (struct zonelist_free*)region_alloc_zero(opt->region, sizeof(*e));
	e->next = b->list;
	b->list = e;
	e->off = off;
	opt->zonefree_number++;
}

static struct zone_options*
zone_list_member_zone_insert(struct nsd_options* opt, const char* nm,
	const char* patnm, int linesize, off_t off, const char* mem_idnm,
	new_member_id_type new_member_id)
{
	struct pattern_options* pat = pattern_options_find(opt, patnm);
	struct catalog_member_zone* cmz = NULL;
	struct zone_options* zone;
	char member_id_str[MAXDOMAINLEN * 5 + 3] = "ERROR!";
	DEBUG(DEBUG_XFRD, 2, (LOG_INFO, "zone_list_zone_insert(\"%s\", \"%s\""
	        ", %d, \"%s\")", nm, patnm, linesize,
		(mem_idnm ? mem_idnm : "<NULL>")));
	if(!pat) {
		log_msg(LOG_ERR, "pattern does not exist for zone %s "
			"pattern %s", nm, patnm);
		return NULL;
	}
	zone = pat->catalog_producer_zone
	     ? &(cmz = catalog_member_zone_create(opt->region))->options
	     : zone_options_create(opt->region);
	zone->part_of_config = 0;
	zone->name = region_strdup(opt->region, nm);
	zone->linesize = linesize;
	zone->off = off;
	zone->pattern = pat;
	if(!nsd_options_insert_zone(opt, zone)) {
		log_msg(LOG_ERR, "bad domain name or duplicate zone '%s' "
			"pattern %s", nm, patnm);
		region_recycle(opt->region, (void*)zone->name, strlen(nm)+1);
		region_recycle(opt->region, zone, sizeof(*zone));
		return NULL;
	}
	if(!mem_idnm) {
		if(cmz && new_member_id)
			new_member_id(cmz);
		if(cmz && cmz->member_id) {
			/* Assume all bytes of member_id are printable.
			 * plus 1 for space
			 */
			zone->linesize += label_length(dname_name(cmz->member_id)) + 1;
			DEBUG(DEBUG_XFRD, 2, (LOG_INFO, "new linesize: %d",
				(int)zone->linesize));
		}
	} else if(!cmz)
		log_msg(LOG_ERR, "member ID '%s' given, but no catalog-producer-"
			"zone value provided in zone '%s' or pattern '%s'",
			mem_idnm, nm, patnm);

	else if(snprintf(member_id_str, sizeof(member_id_str),
	    "%s.zones.%s", mem_idnm, pat->catalog_producer_zone) >=
	    (int)sizeof(member_id_str))
		log_msg(LOG_ERR, "syntax error in member ID '%s.zones.%s' for "
			"zone '%s'", mem_idnm, pat->catalog_producer_zone, nm);

	else if(!(cmz->member_id = dname_parse(opt->region, member_id_str)))
		log_msg(LOG_ERR, "parse error in member ID '%s' for "
			"zone '%s'", member_id_str, nm);
	return zone;
}

struct zone_options*
zone_list_zone_insert(struct nsd_options* opt,const char* nm,const char* patnm)
{
	return zone_list_member_zone_insert(opt, nm, patnm, 0, 0, NULL, NULL);
}

int
parse_zone_list_file(struct nsd_options* opt)
{
	/* zonelist looks like this:
	# name pattern
	add example.com master
	del example.net slave
	add foo.bar.nl slave
	add rutabaga.uk config
	*/
	char hdr[64];
	char buf[1024];
	
	/* create empty data structures */
	opt->zonefree = rbtree_create(opt->region, comp_zonebucket);
	opt->zonelist = NULL;
	opt->zonefree_number = 0;
	opt->zonelist_off = 0;

	/* try to open the zonelist file, an empty or nonexist file is OK */
	opt->zonelist = fopen(opt->zonelistfile, "r+");
	if(!opt->zonelist) {
		if(errno == ENOENT)
			return 1; /* file does not exist, it is created later */
		log_msg(LOG_ERR, "could not open zone list %s: %s", opt->zonelistfile,
			strerror(errno));
		return 0;
	}
	/* read header */
	hdr[strlen(ZONELIST_HEADER)] = 0;
	if(fread(hdr, 1, strlen(ZONELIST_HEADER), opt->zonelist) !=
		strlen(ZONELIST_HEADER) || strncmp(hdr, ZONELIST_HEADER,
		strlen(ZONELIST_HEADER)) != 0) {
		log_msg(LOG_ERR, "zone list %s contains bad header\n", opt->zonelistfile);
		fclose(opt->zonelist);
		opt->zonelist = NULL;
		return 0;
	}
	buf[sizeof(buf)-1]=0;

	/* read entries in file */
	while(fgets(buf, sizeof(buf), opt->zonelist)) {
		/* skip comments and empty lines */
		if(buf[0] == 0 || buf[0] == '\n' || buf[0] == '#')
			continue;
		if(strncmp(buf, "add ", 4) == 0) {
			int linesize = strlen(buf);
			/* parse the 'add' line */
			/* pick last space on the line, so that the domain
			 * name can have a space in it (but not the pattern)*/
			char* space = strrchr(buf+4, ' ');
			char* nm, *patnm;
			if(!space) {
				/* parse error */
				log_msg(LOG_ERR, "parse error in %s: '%s'",
					opt->zonelistfile, buf);
				continue;
			}
			nm = buf+4;
			*space = 0;
			patnm = space+1;
			if(linesize && buf[linesize-1] == '\n')
				buf[linesize-1] = 0;

			/* store offset and line size for zone entry */
			/* and create zone entry in zonetree */
			(void)zone_list_member_zone_insert(opt, nm, patnm,
				linesize, ftello(opt->zonelist)-linesize,
				NULL, NULL);

		} else if(strncmp(buf, "cat ", 4) == 0) {
			int linesize = strlen(buf);
			/* parse the 'add' line */
			/* pick last space on the line, so that the domain
			 * name can have a space in it (but not the pattern)*/
			char* nm = buf + 4;
			char* mem_idnm = strrchr(nm, ' '), *patnm;
			if(!mem_idnm) {
				/* parse error */
				log_msg(LOG_ERR, "parse error in %s: '%s'",
					opt->zonelistfile, buf);
				continue;
			}
			*mem_idnm++ = 0;
			patnm = strrchr(nm, ' ');
			if(!patnm) {
				*--mem_idnm = ' ';
				/* parse error */
				log_msg(LOG_ERR, "parse error in %s: '%s'",
					opt->zonelistfile, buf);
				continue;
			}
			*patnm++ = 0;
			if(linesize && buf[linesize-1] == '\n')
				buf[linesize-1] = 0;

			/* store offset and line size for zone entry */
			/* and create zone entry in zonetree */
			(void)zone_list_member_zone_insert(opt, nm, patnm,
				linesize, ftello(opt->zonelist)-linesize,
				mem_idnm, NULL);

		} else if(strncmp(buf, "del ", 4) == 0) {
			/* store offset and line size for deleted entry */
			int linesize = strlen(buf);
			zone_list_free_insert(opt, linesize,
				ftello(opt->zonelist)-linesize);
		} else {
			log_msg(LOG_WARNING, "bad data in %s, '%s'", opt->zonelistfile,
				buf);
		}
	}
	/* store EOF offset */
	opt->zonelist_off = ftello(opt->zonelist);
	return 1;
}

void
zone_options_delete(struct nsd_options* opt, struct zone_options* zone)
{
	struct catalog_member_zone* member_zone = as_catalog_member_zone(zone);

	rbtree_delete(opt->zone_options, zone->node.key);
	region_recycle(opt->region, (void*)zone->node.key, dname_total_size(
		(dname_type*)zone->node.key));
	if(!member_zone) {
		region_recycle(opt->region, zone, sizeof(*zone));
		return;
	}
	/* Because catalog member zones are in xfrd only deleted through
	 * catalog_del_consumer_member_zone() or through
	 * xfrd_del_catalog_producer_member(), which both clear the node,
	 * and because member zones in the main and serve processes are not
	 * indexed, *member_zone->node == *RBTREE_NULL.
	 * member_id is cleared too by those delete function, but there may be
	 * leftover member_id's from the initial zone.list processing, which
	 * made it to the main and serve processes.
	 */
	assert(!memcmp(&member_zone->node, RBTREE_NULL, sizeof(*RBTREE_NULL)));
	if(member_zone->member_id) {
		region_recycle(opt->region, (void*)member_zone->member_id,
				dname_total_size(member_zone->member_id));
	}
	region_recycle(opt->region, member_zone, sizeof(*member_zone));
}


/* add a new zone to the zonelist */
struct zone_options*
zone_list_add_or_cat(struct nsd_options* opt, const char* zname,
		const char* pname, new_member_id_type new_member_id)
{
	int r;
	struct zonelist_free* e;
	struct zonelist_bucket* b;
	char zone_list_line[6 + 5 * MAXDOMAINLEN + 2024 + 65];
	struct catalog_member_zone* cmz;

	/* create zone entry */
	struct zone_options* zone = zone_list_member_zone_insert(
		opt, zname, pname, 6 + strlen(zname) + strlen(pname),
		0, NULL, new_member_id);
	if(!zone)
		return NULL;

	if(zone_is_catalog_producer_member(zone)
	&& (cmz = as_catalog_member_zone(zone))
	&& cmz->member_id) {
		snprintf(zone_list_line, sizeof(zone_list_line),
			"cat %s %s %.*s\n", zname, pname,
			(int)label_length(dname_name(cmz->member_id)),
			(const char*)dname_name(cmz->member_id) + 1);
	} else {
		snprintf(zone_list_line, sizeof(zone_list_line),
			"add %s %s\n", zname, pname);
	}
	/* use free entry or append to file or create new file */
	if(!opt->zonelist || opt->zonelist_off == 0) {
		/* create new file */
		if(opt->zonelist) fclose(opt->zonelist);
		opt->zonelist = fopen(opt->zonelistfile, "w+");
		if(!opt->zonelist) {
			log_msg(LOG_ERR, "could not create zone list %s: %s",
				opt->zonelistfile, strerror(errno));
			log_msg(LOG_ERR, "zone %s could not be added", zname);
			zone_options_delete(opt, zone);
			return NULL;
		}
		r = fprintf(opt->zonelist, ZONELIST_HEADER);
		if(r != strlen(ZONELIST_HEADER)) {
			if(r == -1)
				log_msg(LOG_ERR, "could not write to %s: %s",
					opt->zonelistfile, strerror(errno));
			else log_msg(LOG_ERR, "partial write to %s: disk full",
				opt->zonelistfile);
			log_msg(LOG_ERR, "zone %s could not be added", zname);
			zone_options_delete(opt, zone);
			return NULL;
		}
		zone->off = ftello(opt->zonelist);
		if(zone->off == -1)
			log_msg(LOG_ERR, "ftello(%s): %s", opt->zonelistfile, strerror(errno));
		r = fprintf(opt->zonelist, "%s", zone_list_line);
		if(r != zone->linesize) {
			if(r == -1)
				log_msg(LOG_ERR, "could not write to %s: %s",
					opt->zonelistfile, strerror(errno));
			else log_msg(LOG_ERR, "partial write to %s: disk full",
				opt->zonelistfile);
			log_msg(LOG_ERR, "zone %s could not be added", zname);
			zone_options_delete(opt, zone);
			return NULL;
		}
		opt->zonelist_off = ftello(opt->zonelist);
		if(opt->zonelist_off == -1)
			log_msg(LOG_ERR, "ftello(%s): %s", opt->zonelistfile, strerror(errno));
		if(fflush(opt->zonelist) != 0) {
			log_msg(LOG_ERR, "fflush %s: %s", opt->zonelistfile, strerror(errno));
		}
		return zone;
	}
	b = (struct zonelist_bucket*)rbtree_search(opt->zonefree,
		&zone->linesize);
	if(!b || b->list == NULL) {
		/* no empty place, append to file */
		zone->off = opt->zonelist_off;
		if(fseeko(opt->zonelist, zone->off, SEEK_SET) == -1) {
			log_msg(LOG_ERR, "fseeko(%s): %s", opt->zonelistfile, strerror(errno));
			log_msg(LOG_ERR, "zone %s could not be added", zname);
			zone_options_delete(opt, zone);
			return NULL;
		}
		r = fprintf(opt->zonelist, "%s", zone_list_line);
		if(r != zone->linesize) {
			if(r == -1)
				log_msg(LOG_ERR, "could not write to %s: %s",
					opt->zonelistfile, strerror(errno));
			else log_msg(LOG_ERR, "partial write to %s: disk full",
				opt->zonelistfile);
			log_msg(LOG_ERR, "zone %s could not be added", zname);
			zone_options_delete(opt, zone);
			return NULL;
		}
		opt->zonelist_off += zone->linesize;
		if(fflush(opt->zonelist) != 0) {
			log_msg(LOG_ERR, "fflush %s: %s", opt->zonelistfile, strerror(errno));
		}
		return zone;
	}
	/* reuse empty spot */
	e = b->list;
	zone->off = e->off;
	if(fseeko(opt->zonelist, zone->off, SEEK_SET) == -1) {
		log_msg(LOG_ERR, "fseeko(%s): %s", opt->zonelistfile, strerror(errno));
		log_msg(LOG_ERR, "zone %s could not be added", zname);
		zone_options_delete(opt, zone);
		return NULL;
	}
	r = fprintf(opt->zonelist, "%s", zone_list_line);
	if(r != zone->linesize) {
		if(r == -1)
			log_msg(LOG_ERR, "could not write to %s: %s",
				opt->zonelistfile, strerror(errno));
		else log_msg(LOG_ERR, "partial write to %s: disk full",
			opt->zonelistfile);
		log_msg(LOG_ERR, "zone %s could not be added", zname);
		zone_options_delete(opt, zone);
		return NULL;
	}
	if(fflush(opt->zonelist) != 0) {
		log_msg(LOG_ERR, "fflush %s: %s", opt->zonelistfile, strerror(errno));
	}

	/* snip off and recycle element */
	b->list = e->next;
	region_recycle(opt->region, e, sizeof(*e));
	if(b->list == NULL) {
		rbtree_delete(opt->zonefree, &b->linesize);
		region_recycle(opt->region, b, sizeof(*b));
	}
	opt->zonefree_number--;
	return zone;
}

/* remove a zone on the zonelist */
void
zone_list_del(struct nsd_options* opt, struct zone_options* zone)
{
	if (zone_is_catalog_consumer_member(zone)) {
		/* catalog consumer member zones are not in the zones.list file */
		zone_options_delete(opt, zone);
		return;
	}
	/* put its space onto the free entry */
	if(fseeko(opt->zonelist, zone->off, SEEK_SET) == -1) {
		log_msg(LOG_ERR, "fseeko(%s): %s", opt->zonelistfile, strerror(errno));
		return;
	}
	fprintf(opt->zonelist, "del");
	zone_list_free_insert(opt, zone->linesize, zone->off);

	/* remove zone_options */
	zone_options_delete(opt, zone);

	/* see if we need to compact: it is going to halve the zonelist */
	if(opt->zonefree_number > opt->zone_options->count) {
		zone_list_compact(opt);
	} else {
		if(fflush(opt->zonelist) != 0) {
			log_msg(LOG_ERR, "fflush %s: %s", opt->zonelistfile, strerror(errno));
		}
	}
}
/* postorder delete of zonelist free space tree */
static void
delbucket(region_type* region, struct zonelist_bucket* b)
{
	struct zonelist_free* e, *f;
	if(!b || (rbnode_type*)b==RBTREE_NULL)
		return;
	delbucket(region, (struct zonelist_bucket*)b->node.left);
	delbucket(region, (struct zonelist_bucket*)b->node.right);
	e = b->list;
	while(e) {
		f = e->next;
		region_recycle(region, e, sizeof(*e));
		e = f;
	}
	region_recycle(region, b, sizeof(*b));
}

/* compact zonelist file */
void
zone_list_compact(struct nsd_options* opt)
{
	char outname[1024];
	FILE* out;
	struct zone_options* zone;
	off_t off;
	int r;
	snprintf(outname, sizeof(outname), "%s~", opt->zonelistfile);
	/* useful, when : count-of-free > count-of-used */
	/* write zonelist to zonelist~ */
	out = fopen(outname, "w+");
	if(!out) {
		log_msg(LOG_ERR, "could not open %s: %s", outname, strerror(errno));
		return;
	}
	r = fprintf(out, ZONELIST_HEADER);
	if(r == -1) {
		log_msg(LOG_ERR, "write %s failed: %s", outname,
			strerror(errno));
		fclose(out);
		return;
	} else if(r != strlen(ZONELIST_HEADER)) {
		log_msg(LOG_ERR, "write %s was partial: disk full",
			outname);
		fclose(out);
		return;
	}
	off = ftello(out);
	if(off == -1) {
		log_msg(LOG_ERR, "ftello(%s): %s", outname, strerror(errno));
		fclose(out);
		return;
	}
	RBTREE_FOR(zone, struct zone_options*, opt->zone_options) {
		struct catalog_member_zone* cmz;

		if(zone->part_of_config)
			continue;
		if(zone_is_catalog_producer_member(zone)
		&& (cmz = as_catalog_member_zone(zone))
		&& cmz->member_id) {
			r = fprintf(out, "cat %s %s %.*s\n", zone->name,
				zone->pattern->pname,
				(int)label_length(dname_name(cmz->member_id)),
				(const char*)dname_name(cmz->member_id) + 1);
		} else {
			r = fprintf(out, "add %s %s\n", zone->name,
					zone->pattern->pname);
		}
		if(r < 0) {
			log_msg(LOG_ERR, "write %s failed: %s", outname,
				strerror(errno));
			fclose(out);
			return;
		} else if(r != zone->linesize) {
			log_msg(LOG_ERR, "write %s was partial: disk full",
				outname);
			fclose(out);
			return;
		}
	}
	if(fflush(out) != 0) {
		log_msg(LOG_ERR, "fflush %s: %s", outname, strerror(errno));
	}

	/* rename zonelist~ onto zonelist */
	if(rename(outname, opt->zonelistfile) == -1) {
		log_msg(LOG_ERR, "rename(%s to %s) failed: %s",
			outname, opt->zonelistfile, strerror(errno));
		fclose(out);
		return;
	}
	fclose(opt->zonelist);
	/* set offsets */
	RBTREE_FOR(zone, struct zone_options*, opt->zone_options) {
		if(zone->part_of_config)
			continue;
		zone->off = off;
		off += zone->linesize;
	}
	/* empty the free tree */
	delbucket(opt->region, (struct zonelist_bucket*)opt->zonefree->root);
	opt->zonefree->root = RBTREE_NULL;
	opt->zonefree->count = 0;
	opt->zonefree_number = 0;
	/* finish */
	opt->zonelist = out;
	opt->zonelist_off = off;
}

/* close zonelist file */
void
zone_list_close(struct nsd_options* opt)
{
	if(opt->zonelist) {
		fclose(opt->zonelist);
		opt->zonelist = NULL;
	}
}

static void
c_error_va_list_pos(int showpos, const char* fmt, va_list args)
{
	char* at = NULL;
	cfg_parser->errors++;
	if(showpos && c_text && c_text[0]!=0) {
		at = c_text;
	}
	if(cfg_parser->err) {
		char m[MAXSYSLOGMSGLEN];
		snprintf(m, sizeof(m), "%s:%d: ", cfg_parser->filename,
			cfg_parser->line);
		(*cfg_parser->err)(cfg_parser->err_arg, m);
		if(at) {
			snprintf(m, sizeof(m), "at '%s': ", at);
			(*cfg_parser->err)(cfg_parser->err_arg, m);
		}
		(*cfg_parser->err)(cfg_parser->err_arg, "error: ");
		vsnprintf(m, sizeof(m), fmt, args);
		(*cfg_parser->err)(cfg_parser->err_arg, m);
		(*cfg_parser->err)(cfg_parser->err_arg, "\n");
		return;
	}
        fprintf(stderr, "%s:%d: ", cfg_parser->filename, cfg_parser->line);
	if(at) fprintf(stderr, "at '%s': ", at);
	fprintf(stderr, "error: ");
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
}

void
c_error(const char *fmt, ...)
{
	va_list ap;
	int showpos = 0;

	if (strcmp(fmt, "syntax error") == 0 || strcmp(fmt, "parse error") == 0) {
		showpos = 1;
	}

	va_start(ap, fmt);
	c_error_va_list_pos(showpos, fmt, ap);
	va_end(ap);
}

int
c_wrap(void)
{
	return 1;
}

struct zone_options*
zone_options_create(region_type* region)
{
	struct zone_options* zone;
	zone = (struct zone_options*)region_alloc(region, sizeof(
		struct zone_options));
	zone->node = *RBTREE_NULL;
	zone->name = 0;
	zone->pattern = 0;
	zone->part_of_config = 0;
	zone->is_catalog_member_zone = 0;
	return zone;
}

struct catalog_member_zone*
catalog_member_zone_create(region_type* region)
{
	struct catalog_member_zone* member_zone;
	member_zone = (struct catalog_member_zone*)region_alloc(region,
			sizeof(struct catalog_member_zone));
	member_zone->options.node = *RBTREE_NULL;
	member_zone->options.name = 0;
	member_zone->options.pattern = 0;
	member_zone->options.part_of_config = 0;
	member_zone->options.is_catalog_member_zone = 1;
	member_zone->member_id = NULL;
	member_zone->node = *RBTREE_NULL;
	return member_zone;
}

/* true is booleans are the same truth value */
#define booleq(x,y) ( ((x) && (y)) || (!(x) && !(y)) )

/* true is min_expire_time_expr has either an equal known value
 * or none of these known values but booleanally equal
 */
#define expire_expr_eq(x,y) (  (  (x) == REFRESHPLUSRETRYPLUS1 \
                               && (y) == REFRESHPLUSRETRYPLUS1 ) \
                            || (  (x) != REFRESHPLUSRETRYPLUS1 \
                               && (y) != REFRESHPLUSRETRYPLUS1 \
                               && booleq((x), (y))))


int
acl_equal(struct acl_options* p, struct acl_options* q)
{
	if(!booleq(p->use_axfr_only, q->use_axfr_only)) return 0;
	if(!booleq(p->allow_udp, q->allow_udp)) return 0;
	if(strcmp(p->ip_address_spec, q->ip_address_spec)!=0) return 0;
	/* the ip6, port, addr, mask, type: are derived from the ip_address_spec */
	if(!booleq(p->nokey, q->nokey)) return 0;
	if(!booleq(p->blocked, q->blocked)) return 0;
	if(p->key_name && q->key_name) {
		if(strcmp(p->key_name, q->key_name)!=0) return 0;
	} else if(p->key_name && !q->key_name) return 0;
	else if(!p->key_name && q->key_name) return 0;
	/* key_options is derived from key_name */
	if(p->tls_auth_name && q->tls_auth_name) {
		if(strcmp(p->tls_auth_name, q->tls_auth_name)!=0) return 0;
	} else if(p->tls_auth_name && !q->tls_auth_name) return 0;
	else if(!p->tls_auth_name && q->tls_auth_name) return 0;
	/* tls_auth_options is derived from tls_auth_name */
	return 1;
}

int
acl_list_equal(struct acl_options* p, struct acl_options* q)
{
	/* must be same and in same order */
	while(p && q) {
		if(!acl_equal(p, q))
			return 0;
		p = p->next;
		q = q->next;
	}
	if(!p && !q) return 1;
	/* different lengths */
	return 0;
}

struct pattern_options*
pattern_options_create(region_type* region)
{
	struct pattern_options* p;
	p = (struct pattern_options*)region_alloc(region, sizeof(
		struct pattern_options));
	p->node = *RBTREE_NULL;
	p->pname = 0;
	p->zonefile = 0;
	p->zonestats = 0;
	p->allow_notify = 0;
	p->request_xfr = 0;
	p->size_limit_xfr = 0;
	p->notify = 0;
	p->provide_xfr = 0;
	p->allow_query = 0;
	p->outgoing_interface = 0;
	p->notify_retry = 5;
	p->notify_retry_is_default = 1;
	p->allow_axfr_fallback = 1;
	p->allow_axfr_fallback_is_default = 1;
	p->implicit = 0;
	p->xfrd_flags = 0;
	p->max_refresh_time = 2419200;	/* 4 weeks */
	p->max_refresh_time_is_default = 1;
	p->min_refresh_time = 0;
	p->min_refresh_time_is_default = 1;
	p->max_retry_time = 1209600;	/* 2 weeks */
	p->max_retry_time_is_default = 1;
	p->min_retry_time = 0;
	p->min_retry_time_is_default = 1;
	p->min_expire_time = 0;
	p->min_expire_time_expr = EXPIRE_TIME_IS_DEFAULT;
#ifdef RATELIMIT
	p->rrl_whitelist = 0;
#endif
	p->multi_primary_check = 0;
	p->store_ixfr = 0;
	p->store_ixfr_is_default = 1;
	p->ixfr_size = IXFR_SIZE_DEFAULT;
	p->ixfr_size_is_default = 1;
	p->ixfr_number = IXFR_NUMBER_DEFAULT;
	p->ixfr_number_is_default = 1;
	p->create_ixfr = 0;
	p->create_ixfr_is_default = 1;
	p->verify_zone = VERIFY_ZONE_INHERIT;
	p->verify_zone_is_default = 1;
	p->verifier = NULL;
	p->verifier_feed_zone = VERIFIER_FEED_ZONE_INHERIT;
	p->verifier_feed_zone_is_default = 1;
	p->verifier_timeout = VERIFIER_TIMEOUT_INHERIT;
	p->verifier_timeout_is_default = 1;
	p->catalog_role = CATALOG_ROLE_INHERIT;
	p->catalog_role_is_default = 1;
	p->catalog_member_pattern = NULL;
	p->catalog_producer_zone = NULL;
	return p;
}

static void
acl_delete(region_type* region, struct acl_options* acl)
{
	if(acl->ip_address_spec)
		region_recycle(region, (void*)acl->ip_address_spec,
			strlen(acl->ip_address_spec)+1);
	if(acl->key_name)
		region_recycle(region, (void*)acl->key_name,
			strlen(acl->key_name)+1);
	if(acl->tls_auth_name)
		region_recycle(region, (void*)acl->tls_auth_name,
			strlen(acl->tls_auth_name)+1);
	/* key_options is a convenience pointer, not owned by the acl */
	region_recycle(region, acl, sizeof(*acl));
}

static void
acl_list_delete(region_type* region, struct acl_options* list)
{
	struct acl_options* n;
	while(list) {
		n = list->next;
		acl_delete(region, list);
		list = n;
	}
}

static void
verifier_delete(region_type* region, char **v)
{
	if(v != NULL) {
		size_t vc = 0;
		for(vc = 0; v[vc] != NULL; vc++)
			region_recycle(region, v[vc], strlen(v[vc]) + 1);
		region_recycle(region, v, (vc + 1) * sizeof(char *));
	}
}

void
pattern_options_remove(struct nsd_options* opt, const char* name)
{
	struct pattern_options* p = (struct pattern_options*)rbtree_delete(
		opt->patterns, name);
	/* delete p and its contents */
	if (!p)
		return;
	if(p->pname)
		region_recycle(opt->region, (void*)p->pname,
			strlen(p->pname)+1);
	if(p->zonefile)
		region_recycle(opt->region, (void*)p->zonefile,
			strlen(p->zonefile)+1);
	if(p->zonestats)
		region_recycle(opt->region, (void*)p->zonestats,
			strlen(p->zonestats)+1);
	acl_list_delete(opt->region, p->allow_notify);
	acl_list_delete(opt->region, p->request_xfr);
	acl_list_delete(opt->region, p->notify);
	acl_list_delete(opt->region, p->provide_xfr);
	acl_list_delete(opt->region, p->allow_query);
	acl_list_delete(opt->region, p->outgoing_interface);
	verifier_delete(opt->region, p->verifier);

	region_recycle(opt->region, p, sizeof(struct pattern_options));
}

static struct acl_options*
copy_acl(region_type* region, struct acl_options* a)
{
	struct acl_options* b;
	if(!a) return NULL;
	b = (struct acl_options*)region_alloc(region, sizeof(*b));
	/* copy the whole lot */
	*b = *a;
	/* fix the pointers */
	if(a->ip_address_spec)
		b->ip_address_spec = region_strdup(region, a->ip_address_spec);
	if(a->key_name)
		b->key_name = region_strdup(region, a->key_name);
	if(a->tls_auth_name)
		b->tls_auth_name = region_strdup(region, a->tls_auth_name);
	b->next = NULL;
	b->key_options = NULL;
	b->tls_auth_options = NULL;
	return b;
}

static struct acl_options*
copy_acl_list(struct nsd_options* opt, struct acl_options* a)
{
	struct acl_options* b, *blast = NULL, *blist = NULL;
	while(a) {
		b = copy_acl(opt->region, a);
		/* fixup key_options */
		if(b->key_name)
			b->key_options = key_options_find(opt, b->key_name);
		else	b->key_options = NULL;
		/* fixup tls_auth_options */
		if(b->tls_auth_name)
			b->tls_auth_options = tls_auth_options_find(opt, b->tls_auth_name);
		else	b->tls_auth_options = NULL;

		/* link as last into list */
		b->next = NULL;
		if(!blist) blist = b;
		else blast->next = b;
		blast = b;
		
		a = a->next;
	}
	return blist;
}

static void
copy_changed_acl(struct nsd_options* opt, struct acl_options** orig,
	struct acl_options* anew)
{
	if(!acl_list_equal(*orig, anew)) {
		acl_list_delete(opt->region, *orig);
		*orig = copy_acl_list(opt, anew);
	}
}

static void
copy_changed_verifier(struct nsd_options* opt, char ***ov, char **nv)
{
	size_t ovc, nvc;
	assert(ov != NULL);
	ovc = nvc = 0;
	if(nv != NULL) {
		for(; nv[nvc] != NULL; nvc++) ;
	} else {
		verifier_delete(opt->region, *ov);
		*ov = NULL;
		return;
	}
	if(*ov != NULL) {
		for(; (*ov)[ovc] != NULL; ovc++) {
			if(ovc < nvc && strcmp((*ov)[ovc], nv[ovc]) != 0)
				break;
		}
		if(ovc == nvc)
			return;
		verifier_delete(opt->region, *ov);
		*ov = NULL;
	}
	*ov = region_alloc(opt->region, (nvc + 1) * sizeof(*nv));
	for(ovc = 0; nv[ovc] != NULL; ovc++) {
		(*ov)[ovc] = region_strdup(opt->region, nv[ovc]);
	}
	(*ov)[ovc] = NULL;
	assert(ovc == nvc);
}

static void
copy_pat_fixed(region_type* region, struct pattern_options* orig,
	struct pattern_options* p)
{
	orig->allow_axfr_fallback = p->allow_axfr_fallback;
	orig->allow_axfr_fallback_is_default =
		p->allow_axfr_fallback_is_default;
	orig->notify_retry = p->notify_retry;
	orig->notify_retry_is_default = p->notify_retry_is_default;
	orig->implicit = p->implicit;
	if(p->zonefile)
		orig->zonefile = region_strdup(region, p->zonefile);
	else orig->zonefile = NULL;
	if(p->zonestats)
		orig->zonestats = region_strdup(region, p->zonestats);
	else orig->zonestats = NULL;
	orig->max_refresh_time = p->max_refresh_time;
	orig->max_refresh_time_is_default = p->max_refresh_time_is_default;
	orig->min_refresh_time = p->min_refresh_time;
	orig->min_refresh_time_is_default = p->min_refresh_time_is_default;
	orig->max_retry_time = p->max_retry_time;
	orig->max_retry_time_is_default = p->max_retry_time_is_default;
	orig->min_retry_time = p->min_retry_time;
	orig->min_retry_time_is_default = p->min_retry_time_is_default;
	orig->min_expire_time = p->min_expire_time;
	orig->min_expire_time_expr = p->min_expire_time_expr;
#ifdef RATELIMIT
	orig->rrl_whitelist = p->rrl_whitelist;
#endif
	orig->multi_primary_check = p->multi_primary_check;
	orig->store_ixfr = p->store_ixfr;
	orig->store_ixfr_is_default = p->store_ixfr_is_default;
	orig->ixfr_size = p->ixfr_size;
	orig->ixfr_size_is_default = p->ixfr_size_is_default;
	orig->ixfr_number = p->ixfr_number;
	orig->ixfr_number_is_default = p->ixfr_number_is_default;
	orig->create_ixfr = p->create_ixfr;
	orig->create_ixfr_is_default = p->create_ixfr_is_default;
	orig->verify_zone = p->verify_zone;
	orig->verify_zone_is_default = p->verify_zone_is_default;
	orig->verifier_timeout = p->verifier_timeout;
	orig->verifier_timeout_is_default = p->verifier_timeout_is_default;
	orig->verifier_feed_zone = p->verifier_feed_zone;
	orig->verifier_feed_zone_is_default = p->verifier_feed_zone_is_default;
	orig->catalog_role = p->catalog_role;
	orig->catalog_role_is_default = p->catalog_role_is_default;
	if(p->catalog_member_pattern)
		orig->catalog_member_pattern =
			region_strdup(region, p->catalog_member_pattern);
	else orig->catalog_member_pattern = NULL;
	if(p->catalog_producer_zone)
		orig->catalog_producer_zone =
			region_strdup(region, p->catalog_producer_zone);
	else orig->catalog_producer_zone = NULL;
}

void
pattern_options_add_modify(struct nsd_options* opt, struct pattern_options* p)
{
	struct pattern_options* orig = pattern_options_find(opt, p->pname);
	if(!orig) {
		/* needs to be copied to opt region */
		orig = pattern_options_create(opt->region);
		orig->pname = region_strdup(opt->region, p->pname);
		copy_pat_fixed(opt->region, orig, p);
		orig->allow_notify = copy_acl_list(opt, p->allow_notify);
		orig->request_xfr = copy_acl_list(opt, p->request_xfr);
		orig->notify = copy_acl_list(opt, p->notify);
		orig->provide_xfr = copy_acl_list(opt, p->provide_xfr);
		orig->allow_query = copy_acl_list(opt, p->allow_query);
		orig->outgoing_interface = copy_acl_list(opt,
			p->outgoing_interface);
		copy_changed_verifier(opt, &orig->verifier, p->verifier);
		nsd_options_insert_pattern(opt, orig);
	} else {
		/* modify in place so pointers stay valid (and copy
		   into region). Do not touch unchanged acls. */
		if(orig->zonefile)
			region_recycle(opt->region, (char*)orig->zonefile,
				strlen(orig->zonefile)+1);
		if(orig->zonestats)
			region_recycle(opt->region, (char*)orig->zonestats,
				strlen(orig->zonestats)+1);
		copy_pat_fixed(opt->region, orig, p);
		copy_changed_acl(opt, &orig->allow_notify, p->allow_notify);
		copy_changed_acl(opt, &orig->request_xfr, p->request_xfr);
		copy_changed_acl(opt, &orig->notify, p->notify);
		copy_changed_acl(opt, &orig->provide_xfr, p->provide_xfr);
		copy_changed_acl(opt, &orig->allow_query, p->allow_query);
		copy_changed_acl(opt, &orig->outgoing_interface,
			p->outgoing_interface);
		copy_changed_verifier(opt, &orig->verifier, p->verifier);
	}
}

struct pattern_options*
pattern_options_find(struct nsd_options* opt, const char* name)
{
	return (struct pattern_options*)rbtree_search(opt->patterns, name);
}

static int
pattern_verifiers_equal(const char **vp, const char **vq)
{
	size_t vpc, vqc;
	if(vp == NULL)
		return vq == NULL;
	if(vq == NULL)
		return 0;
	for(vpc = 0; vp[vpc] != NULL; vpc++) ;
	for(vqc = 0; vq[vqc] != NULL; vqc++) ;
	if(vpc != vqc)
		return 0;
	for(vpc = 0; vp[vpc] != NULL; vpc++) {
		assert(vq[vpc] != NULL);
		if (strcmp(vp[vpc], vq[vpc]) != 0)
			return 0;
	}
	return 1;
}

int
pattern_options_equal(struct pattern_options* p, struct pattern_options* q)
{
	if(strcmp(p->pname, q->pname) != 0) return 0;
	if(!p->zonefile && q->zonefile) return 0;
	else if(p->zonefile && !q->zonefile) return 0;
	else if(p->zonefile && q->zonefile) {
		if(strcmp(p->zonefile, q->zonefile) != 0) return 0;
	}
	if(!p->zonestats && q->zonestats) return 0;
	else if(p->zonestats && !q->zonestats) return 0;
	else if(p->zonestats && q->zonestats) {
		if(strcmp(p->zonestats, q->zonestats) != 0) return 0;
	}
	if(!booleq(p->allow_axfr_fallback, q->allow_axfr_fallback)) return 0;
	if(!booleq(p->allow_axfr_fallback_is_default,
		q->allow_axfr_fallback_is_default)) return 0;
	if(p->notify_retry != q->notify_retry) return 0;
	if(!booleq(p->notify_retry_is_default,
		q->notify_retry_is_default)) return 0;
	if(!booleq(p->implicit, q->implicit)) return 0;
	if(!acl_list_equal(p->allow_notify, q->allow_notify)) return 0;
	if(!acl_list_equal(p->request_xfr, q->request_xfr)) return 0;
	if(!acl_list_equal(p->notify, q->notify)) return 0;
	if(!acl_list_equal(p->provide_xfr, q->provide_xfr)) return 0;
	if(!acl_list_equal(p->allow_query, q->allow_query)) return 0;
	if(!acl_list_equal(p->outgoing_interface, q->outgoing_interface))
		return 0;
	if(p->max_refresh_time != q->max_refresh_time) return 0;
	if(!booleq(p->max_refresh_time_is_default,
		q->max_refresh_time_is_default)) return 0;
	if(p->min_refresh_time != q->min_refresh_time) return 0;
	if(!booleq(p->min_refresh_time_is_default,
		q->min_refresh_time_is_default)) return 0;
	if(p->max_retry_time != q->max_retry_time) return 0;
	if(!booleq(p->max_retry_time_is_default,
		q->max_retry_time_is_default)) return 0;
	if(p->min_retry_time != q->min_retry_time) return 0;
	if(!booleq(p->min_retry_time_is_default,
		q->min_retry_time_is_default)) return 0;
	if(p->min_expire_time != q->min_expire_time) return 0;
	if(!expire_expr_eq(p->min_expire_time_expr,
		q->min_expire_time_expr)) return 0;
#ifdef RATELIMIT
	if(p->rrl_whitelist != q->rrl_whitelist) return 0;
#endif
	if(!booleq(p->multi_primary_check,q->multi_primary_check)) return 0;
	if(p->size_limit_xfr != q->size_limit_xfr) return 0;
	if(!booleq(p->store_ixfr,q->store_ixfr)) return 0;
	if(!booleq(p->store_ixfr_is_default,q->store_ixfr_is_default)) return 0;
	if(p->ixfr_size != q->ixfr_size) return 0;
	if(!booleq(p->ixfr_size_is_default,q->ixfr_size_is_default)) return 0;
	if(p->ixfr_number != q->ixfr_number) return 0;
	if(!booleq(p->ixfr_number_is_default,q->ixfr_number_is_default)) return 0;
	if(!booleq(p->create_ixfr,q->create_ixfr)) return 0;
	if(!booleq(p->create_ixfr_is_default,q->create_ixfr_is_default)) return 0;
	if(p->verify_zone != q->verify_zone) return 0;
	if(!booleq(p->verify_zone_is_default,
		q->verify_zone_is_default)) return 0;
	if(!pattern_verifiers_equal((const char **)p->verifier,
		(const char **)q->verifier)) return 0;
	if(p->verifier_feed_zone != q->verifier_feed_zone) return 0;
	if(!booleq(p->verifier_feed_zone_is_default,
		q->verifier_feed_zone_is_default)) return 0;
	if(p->verifier_timeout != q->verifier_timeout) return 0;
	if(!booleq(p->verifier_timeout_is_default,
		q->verifier_timeout_is_default)) return 0;
	if(p->catalog_role != q->catalog_role) return 0;
	if(!booleq(p->catalog_role_is_default,
		q->catalog_role_is_default)) return 0;
	if(!p->catalog_member_pattern && q->catalog_member_pattern) return 0;
	else if(p->catalog_member_pattern && !q->catalog_member_pattern) return 0;
	else if(p->catalog_member_pattern && q->catalog_member_pattern) {
		if(strcmp(p->catalog_member_pattern, q->catalog_member_pattern) != 0) return 0;
	}
	if(!p->catalog_producer_zone && q->catalog_producer_zone) return 0;
	else if(p->catalog_producer_zone && !q->catalog_producer_zone) return 0;
	else if(p->catalog_producer_zone && q->catalog_producer_zone) {
		if(strcmp(p->catalog_producer_zone, q->catalog_producer_zone) != 0) return 0;
	}
	return 1;
}

static void
marshal_u8(struct buffer* b, uint8_t v)
{
	buffer_reserve(b, 1);
	buffer_write_u8(b, v);
}

static uint8_t
unmarshal_u8(struct buffer* b)
{
	return buffer_read_u8(b);
}

static void
marshal_u64(struct buffer* b, uint64_t v)
{
	buffer_reserve(b, 8);
	buffer_write_u64(b, v);
}

static uint64_t
unmarshal_u64(struct buffer* b)
{
	return buffer_read_u64(b);
}

#ifdef RATELIMIT
static void
marshal_u16(struct buffer* b, uint16_t v)
{
	buffer_reserve(b, 2);
	buffer_write_u16(b, v);
}
#endif

#ifdef RATELIMIT
static uint16_t
unmarshal_u16(struct buffer* b)
{
	return buffer_read_u16(b);
}
#endif

static void
marshal_u32(struct buffer* b, uint32_t v)
{
	buffer_reserve(b, 4);
	buffer_write_u32(b, v);
}

static uint32_t
unmarshal_u32(struct buffer* b)
{
	return buffer_read_u32(b);
}

static void
marshal_str(struct buffer* b, const char* s)
{
	if(!s) marshal_u8(b, 0);
	else {
		size_t len = strlen(s);
		marshal_u8(b, 1);
		buffer_reserve(b, len+1);
		buffer_write(b, s, len+1);
	}
}

static char*
unmarshal_str(region_type* r, struct buffer* b)
{
	uint8_t nonnull = unmarshal_u8(b);
	if(nonnull) {
		char* result = region_strdup(r, (char*)buffer_current(b));
		size_t len = strlen((char*)buffer_current(b));
		buffer_skip(b, len+1);
		return result;
	} else return NULL;
}

static void
marshal_acl(struct buffer* b, struct acl_options* acl)
{
	buffer_reserve(b, sizeof(*acl));
	buffer_write(b, acl, sizeof(*acl));
	marshal_str(b, acl->ip_address_spec);
	marshal_str(b, acl->key_name);
	marshal_str(b, acl->tls_auth_name);
}

static struct acl_options*
unmarshal_acl(region_type* r, struct buffer* b)
{
	struct acl_options* acl = (struct acl_options*)region_alloc(r,
		sizeof(*acl));
	buffer_read(b, acl, sizeof(*acl));
	acl->next = NULL;
	acl->key_options = NULL;
	acl->tls_auth_options = NULL;
	acl->ip_address_spec = unmarshal_str(r, b);
	acl->key_name = unmarshal_str(r, b);
	acl->tls_auth_name = unmarshal_str(r, b);
	return acl;
}

static void
marshal_acl_list(struct buffer* b, struct acl_options* list)
{
	while(list) {
		marshal_u8(b, 1); /* is there a next one marker */
		marshal_acl(b, list);
		list = list->next;
	}
	marshal_u8(b, 0); /* end of list marker */
}

static struct acl_options*
unmarshal_acl_list(region_type* r, struct buffer* b)
{
	struct acl_options* a, *last=NULL, *list=NULL;
	while(unmarshal_u8(b)) {
		a = unmarshal_acl(r, b);
		/* link in */
		a->next = NULL;
		if(!list) list = a;
		else last->next = a;
		last = a;
	}
	return list;
}

static void
marshal_strv(struct buffer* b, char **strv)
{
	uint32_t i, n;

	assert(b != NULL);

	if (strv == NULL) {
		marshal_u32(b, 0);
		return;
	}
	for(n = 0; strv[n]; n++) {
		/* do nothing */
	}
	marshal_u32(b, n);
	for(i = 0; strv[i] != NULL; i++) {
		marshal_str(b, strv[i]);
	}
	marshal_u8(b, 0);
}

static char **
unmarshal_strv(region_type* r, struct buffer* b)
{
	uint32_t i, n;
	char **strv;

	assert(r != NULL);
	assert(b != NULL);

	if ((n = unmarshal_u32(b)) == 0) {
		return NULL;
	}
	strv = region_alloc_zero(r, (n + 1) * sizeof(char *));
	for(i = 0; i <= n; i++) {
		strv[i] = unmarshal_str(r, b);
	}
	assert(i == (n + 1));
	assert(strv[i - 1] == NULL);

	return strv;
}

void
pattern_options_marshal(struct buffer* b, struct pattern_options* p)
{
	marshal_str(b, p->pname);
	marshal_str(b, p->zonefile);
	marshal_str(b, p->zonestats);
#ifdef RATELIMIT
	marshal_u16(b, p->rrl_whitelist);
#endif
	marshal_u8(b, p->allow_axfr_fallback);
	marshal_u8(b, p->allow_axfr_fallback_is_default);
	marshal_u8(b, p->notify_retry);
	marshal_u8(b, p->notify_retry_is_default);
	marshal_u8(b, p->implicit);
	marshal_u64(b, p->size_limit_xfr);
	marshal_acl_list(b, p->allow_notify);
	marshal_acl_list(b, p->request_xfr);
	marshal_acl_list(b, p->notify);
	marshal_acl_list(b, p->provide_xfr);
	marshal_acl_list(b, p->allow_query);
	marshal_acl_list(b, p->outgoing_interface);
	marshal_u32(b, p->max_refresh_time);
	marshal_u8(b, p->max_refresh_time_is_default);
	marshal_u32(b, p->min_refresh_time);
	marshal_u8(b, p->min_refresh_time_is_default);
	marshal_u32(b, p->max_retry_time);
	marshal_u8(b, p->max_retry_time_is_default);
	marshal_u32(b, p->min_retry_time);
	marshal_u8(b, p->min_retry_time_is_default);
	marshal_u32(b, p->min_expire_time);
	marshal_u8(b, p->min_expire_time_expr);
	marshal_u8(b, p->multi_primary_check);
	marshal_u8(b, p->store_ixfr);
	marshal_u8(b, p->store_ixfr_is_default);
	marshal_u64(b, p->ixfr_size);
	marshal_u8(b, p->ixfr_size_is_default);
	marshal_u32(b, p->ixfr_number);
	marshal_u8(b, p->ixfr_number_is_default);
	marshal_u8(b, p->create_ixfr);
	marshal_u8(b, p->create_ixfr_is_default);
	marshal_u8(b, p->verify_zone);
	marshal_u8(b, p->verify_zone_is_default);
	marshal_strv(b, p->verifier);
	marshal_u8(b, p->verifier_feed_zone);
	marshal_u8(b, p->verifier_feed_zone_is_default);
	marshal_u32(b, p->verifier_timeout);
	marshal_u8(b, p->verifier_timeout_is_default);
	marshal_u8(b, p->catalog_role);
	marshal_u8(b, p->catalog_role_is_default);
	marshal_str(b, p->catalog_member_pattern);
	marshal_str(b, p->catalog_producer_zone);
}

struct pattern_options*
pattern_options_unmarshal(region_type* r, struct buffer* b)
{
	struct pattern_options* p = pattern_options_create(r);
	p->pname = unmarshal_str(r, b);
	p->zonefile = unmarshal_str(r, b);
	p->zonestats = unmarshal_str(r, b);
#ifdef RATELIMIT
	p->rrl_whitelist = unmarshal_u16(b);
#endif
	p->allow_axfr_fallback = unmarshal_u8(b);
	p->allow_axfr_fallback_is_default = unmarshal_u8(b);
	p->notify_retry = unmarshal_u8(b);
	p->notify_retry_is_default = unmarshal_u8(b);
	p->implicit = unmarshal_u8(b);
	p->size_limit_xfr = unmarshal_u64(b);
	p->allow_notify = unmarshal_acl_list(r, b);
	p->request_xfr = unmarshal_acl_list(r, b);
	p->notify = unmarshal_acl_list(r, b);
	p->provide_xfr = unmarshal_acl_list(r, b);
	p->allow_query = unmarshal_acl_list(r, b);
	p->outgoing_interface = unmarshal_acl_list(r, b);
	p->max_refresh_time = unmarshal_u32(b);
	p->max_refresh_time_is_default = unmarshal_u8(b);
	p->min_refresh_time = unmarshal_u32(b);
	p->min_refresh_time_is_default = unmarshal_u8(b);
	p->max_retry_time = unmarshal_u32(b);
	p->max_retry_time_is_default = unmarshal_u8(b);
	p->min_retry_time = unmarshal_u32(b);
	p->min_retry_time_is_default = unmarshal_u8(b);
	p->min_expire_time = unmarshal_u32(b);
	p->min_expire_time_expr = unmarshal_u8(b);
	p->multi_primary_check = unmarshal_u8(b);
	p->store_ixfr = unmarshal_u8(b);
	p->store_ixfr_is_default = unmarshal_u8(b);
	p->ixfr_size = unmarshal_u64(b);
	p->ixfr_size_is_default = unmarshal_u8(b);
	p->ixfr_number = unmarshal_u32(b);
	p->ixfr_number_is_default = unmarshal_u8(b);
	p->create_ixfr = unmarshal_u8(b);
	p->create_ixfr_is_default = unmarshal_u8(b);
	p->verify_zone = unmarshal_u8(b);
	p->verify_zone_is_default = unmarshal_u8(b);
	p->verifier = unmarshal_strv(r, b);
	p->verifier_feed_zone = unmarshal_u8(b);
	p->verifier_feed_zone_is_default = unmarshal_u8(b);
	p->verifier_timeout = unmarshal_u32(b);
	p->verifier_timeout_is_default = unmarshal_u8(b);
	p->catalog_role = unmarshal_u8(b);
	p->catalog_role_is_default = unmarshal_u8(b);
	p->catalog_member_pattern = unmarshal_str(r, b);
	p->catalog_producer_zone = unmarshal_str(r, b);
	return p;
}

struct key_options*
key_options_create(region_type* region)
{
	struct key_options* key;
	key = (struct key_options*)region_alloc_zero(region,
		sizeof(struct key_options));
	return key;
}

struct tls_auth_options*
tls_auth_options_create(region_type* region)
{
	struct tls_auth_options* tls_auth_options;
	tls_auth_options = (struct tls_auth_options*)region_alloc_zero(region, sizeof(struct tls_auth_options));
	return tls_auth_options;
}

void
key_options_insert(struct nsd_options* opt, struct key_options* key)
{
	if(!key->name) return;
	key->node.key = key->name;
	(void)rbtree_insert(opt->keys, &key->node);
}

struct key_options*
key_options_find(struct nsd_options* opt, const char* name)
{
	return (struct key_options*)rbtree_search(opt->keys, name);
}

void
tls_auth_options_insert(struct nsd_options* opt, struct tls_auth_options* auth)
{
	if(!auth->name) return;
	auth->node.key = auth->name;
	(void)rbtree_insert(opt->tls_auths, &auth->node);
}

struct tls_auth_options*
tls_auth_options_find(struct nsd_options* opt, const char* name)
{
	return (struct tls_auth_options*)rbtree_search(opt->tls_auths, name);
}

/** remove tsig_key contents */
void
key_options_desetup(region_type* region, struct key_options* key)
{
	/* keep tsig_key pointer so that existing references keep valid */
	if(!key->tsig_key)
		return;
	/* name stays the same */
	if(key->tsig_key->data) {
		/* wipe secret! */
		memset(key->tsig_key->data, 0xdd, key->tsig_key->size);
		region_recycle(region, key->tsig_key->data,
			key->tsig_key->size);
		key->tsig_key->data = NULL;
		key->tsig_key->size = 0;
	}
}

/** add tsig_key contents */
void
key_options_setup(region_type* region, struct key_options* key)
{
	uint8_t data[16384]; /* 16KB */
	int size;
	if(!key->tsig_key) {
		/* create it */
		key->tsig_key = (tsig_key_type *) region_alloc(region,
			sizeof(tsig_key_type));
		/* create name */
		key->tsig_key->name = dname_parse(region, key->name);
		if(!key->tsig_key->name) {
			log_msg(LOG_ERR, "Failed to parse tsig key name %s",
				key->name);
			/* key and base64 were checked during syntax parse */
			exit(1);
		}
		key->tsig_key->size = 0;
		key->tsig_key->data = NULL;
	}
	size = __b64_pton(key->secret, data, sizeof(data));
	if(size == -1) {
		log_msg(LOG_ERR, "Failed to parse tsig key data %s",
			key->name);
		/* key and base64 were checked during syntax parse */
		exit(1);
	}
	key->tsig_key->size = size;
	key->tsig_key->data = (uint8_t *)region_alloc_init(region, data, size);
}

void
key_options_remove(struct nsd_options* opt, const char* name)
{
	struct key_options* k = key_options_find(opt, name);
	if(!k) return;
	(void)rbtree_delete(opt->keys, name);
	if(k->name)
		region_recycle(opt->region, k->name, strlen(k->name)+1);
	if(k->algorithm)
		region_recycle(opt->region, k->algorithm, strlen(k->algorithm)+1);
	if(k->secret) {
		memset(k->secret, 0xdd, strlen(k->secret)); /* wipe secret! */
		region_recycle(opt->region, k->secret, strlen(k->secret)+1);
	}
	if(k->tsig_key) {
		tsig_del_key(k->tsig_key);
		if(k->tsig_key->name)
			region_recycle(opt->region, (void*)k->tsig_key->name,
				dname_total_size(k->tsig_key->name));
		key_options_desetup(opt->region, k);
		region_recycle(opt->region, k->tsig_key, sizeof(tsig_key_type));
	}
	region_recycle(opt->region, k, sizeof(struct key_options));
}

int
key_options_equal(struct key_options* p, struct key_options* q)
{
	return strcmp(p->name, q->name)==0 && strcmp(p->algorithm,
		q->algorithm)==0 && strcmp(p->secret, q->secret)==0;
}

void
key_options_add_modify(struct nsd_options* opt, struct key_options* key)
{
	struct key_options* orig = key_options_find(opt, key->name);
	if(!orig) {
		/* needs to be copied to opt region */
		orig = key_options_create(opt->region);
		orig->name = region_strdup(opt->region, key->name);
		orig->algorithm = region_strdup(opt->region, key->algorithm);
		orig->secret = region_strdup(opt->region, key->secret);
		key_options_setup(opt->region, orig);
		tsig_add_key(orig->tsig_key);
		key_options_insert(opt, orig);
	} else {
		/* modify entries in existing key, and copy to opt region */
		key_options_desetup(opt->region, orig);
		region_recycle(opt->region, orig->algorithm,
			strlen(orig->algorithm)+1);
		orig->algorithm = region_strdup(opt->region, key->algorithm);
		region_recycle(opt->region, orig->secret,
			strlen(orig->secret)+1);
		orig->secret = region_strdup(opt->region, key->secret);
		key_options_setup(opt->region, orig);
	}
}

int
acl_check_incoming_block_proxy(struct acl_options* acl, struct query* q,
	struct acl_options** reason)
{
	/* check each acl element.
	 * if it is blocked, return -1.
	 * return false if no matches for blocked elements. */
	if(reason)
		*reason = NULL;

	while(acl)
	{
		DEBUG(DEBUG_XFRD,2, (LOG_INFO, "proxy testing acl %s %s",
			acl->ip_address_spec, acl->nokey?"NOKEY":
			(acl->blocked?"BLOCKED":acl->key_name)));
		if(acl_addr_matches_proxy(acl, q) && acl->blocked) {
			if(reason)
				*reason = acl;
			return -1;
		}
		acl = acl->next;
	}

	return 0;
}

int
acl_check_incoming(struct acl_options* acl, struct query* q,
	struct acl_options** reason)
{
	/* check each acl element.
	   if 1 blocked element matches - return -1.
	   if any element matches - return number.
	   else return -1. */
	int found_match = -1;
	int number = 0;
	struct acl_options* match = 0;

	if(reason)
		*reason = NULL;

	while(acl)
	{
#ifdef HAVE_SSL
		DEBUG(DEBUG_XFRD,2, (LOG_INFO, "testing acl %s %s %s",
			acl->ip_address_spec, acl->nokey?"NOKEY":
			(acl->blocked?"BLOCKED":acl->key_name),
			(acl->tls_auth_name && q->tls_auth)?acl->tls_auth_name:""));
#else
		DEBUG(DEBUG_XFRD,2, (LOG_INFO, "testing acl %s %s",
			acl->ip_address_spec, acl->nokey?"NOKEY":
			(acl->blocked?"BLOCKED":acl->key_name)));
#endif
		if(acl_addr_matches(acl, q) && acl_key_matches(acl, q)) {
			if(!match)
			{
				match = acl; /* remember first match */
				found_match=number;
			}
			if(acl->blocked) {
				if(reason)
					*reason = acl;
				return -1;
			}
		}
#ifdef HAVE_SSL
		/* we are in a acl with tls_auth */
		if (acl->tls_auth_name && q->tls_auth) {
			/* we have auth_domain_name in tls_auth */
			if (acl->tls_auth_options && acl->tls_auth_options->auth_domain_name) {
				if (!acl_tls_hostname_matches(q->tls_auth, acl->tls_auth_options->auth_domain_name)) {
					VERBOSITY(3, (LOG_WARNING,
							"client cert does not match %s %s",
							acl->tls_auth_name, acl->tls_auth_options->auth_domain_name));
					q->cert_cn = NULL;
					return -1;
				}
				VERBOSITY(5, (LOG_INFO, "%s %s verified",
					acl->tls_auth_name, acl->tls_auth_options->auth_domain_name));
				q->cert_cn = acl->tls_auth_options->auth_domain_name;
			} else {
				/* nsd gives error on start for this, but check just in case */
				log_msg(LOG_ERR, "auth-domain-name not defined in %s", acl->tls_auth_name);
			}
		}
#endif
		number++;
		acl = acl->next;
	}

	if(reason)
		*reason = match;
	return found_match;
}

#ifdef INET6
int
acl_addr_matches_ipv6host(struct acl_options* acl, struct sockaddr_storage* addr_storage, unsigned int port)
{
	struct sockaddr_in6* addr = (struct sockaddr_in6*)addr_storage;
	if(acl->port != 0 && acl->port != port)
		return 0;
	switch(acl->rangetype) {
	case acl_range_mask:
	case acl_range_subnet:
		if(!acl_addr_match_mask((uint32_t*)&acl->addr.addr6, (uint32_t*)&addr->sin6_addr,
			(uint32_t*)&acl->range_mask.addr6, sizeof(struct in6_addr)))
			return 0;
		break;
	case acl_range_minmax:
		if(!acl_addr_match_range_v6((uint32_t*)&acl->addr.addr6, (uint32_t*)&addr->sin6_addr,
			(uint32_t*)&acl->range_mask.addr6, sizeof(struct in6_addr)))
			return 0;
		break;
	case acl_range_single:
	default:
		if(memcmp(&addr->sin6_addr, &acl->addr.addr6,
			sizeof(struct in6_addr)) != 0)
			return 0;
		break;
	}
	return 1;
}
#endif

int
acl_addr_matches_ipv4host(struct acl_options* acl, struct sockaddr_in* addr, unsigned int port)
{
	if(acl->port != 0 && acl->port != port)
		return 0;
	switch(acl->rangetype) {
	case acl_range_mask:
	case acl_range_subnet:
		if(!acl_addr_match_mask((uint32_t*)&acl->addr.addr, (uint32_t*)&addr->sin_addr,
			(uint32_t*)&acl->range_mask.addr, sizeof(struct in_addr)))
			return 0;
		break;
	case acl_range_minmax:
		if(!acl_addr_match_range_v4((uint32_t*)&acl->addr.addr, (uint32_t*)&addr->sin_addr,
			(uint32_t*)&acl->range_mask.addr, sizeof(struct in_addr)))
			return 0;
		break;
	case acl_range_single:
	default:
		if(memcmp(&addr->sin_addr, &acl->addr.addr,
			sizeof(struct in_addr)) != 0)
			return 0;
		break;
	}
	return 1;
}

int
acl_addr_matches_host(struct acl_options* acl, struct acl_options* host)
{
	if(acl->is_ipv6)
	{
#ifdef INET6
		struct sockaddr_storage* addr = (struct sockaddr_storage*)&host->addr;
		if(!host->is_ipv6) return 0;
		return acl_addr_matches_ipv6host(acl, addr, host->port);
#else
		return 0; /* no inet6, no match */
#endif
	}
	else
	{
		struct sockaddr_in* addr = (struct sockaddr_in*)&host->addr;
		if(host->is_ipv6) return 0;
		return acl_addr_matches_ipv4host(acl, addr, host->port);
	}
	/* ENOTREACH */
	return 0;
}

int
acl_addr_matches(struct acl_options* acl, struct query* q)
{
	if(acl->is_ipv6)
	{
#ifdef INET6
		struct sockaddr_storage* addr = (struct sockaddr_storage*)&q->client_addr;
		if(addr->ss_family != AF_INET6)
			return 0;
		return acl_addr_matches_ipv6host(acl, addr, ntohs(((struct sockaddr_in6*)addr)->sin6_port));
#else
		return 0; /* no inet6, no match */
#endif
	}
	else
	{
		struct sockaddr_in* addr = (struct sockaddr_in*)&q->client_addr;
		if(addr->sin_family != AF_INET)
			return 0;
		return acl_addr_matches_ipv4host(acl, addr, ntohs(addr->sin_port));
	}
	/* ENOTREACH */
	return 0;
}

int
acl_addr_matches_proxy(struct acl_options* acl, struct query* q)
{
	if(acl->is_ipv6)
	{
#ifdef INET6
		struct sockaddr_storage* addr = (struct sockaddr_storage*)&q->remote_addr;
		if(addr->ss_family != AF_INET6)
			return 0;
		return acl_addr_matches_ipv6host(acl, addr, ntohs(((struct sockaddr_in6*)addr)->sin6_port));
#else
		return 0; /* no inet6, no match */
#endif
	}
	else
	{
		struct sockaddr_in* addr = (struct sockaddr_in*)&q->remote_addr;
		if(addr->sin_family != AF_INET)
			return 0;
		return acl_addr_matches_ipv4host(acl, addr, ntohs(addr->sin_port));
	}
	/* ENOTREACH */
	return 0;
}

int
acl_addr_match_mask(uint32_t* a, uint32_t* b, uint32_t* mask, size_t sz)
{
	size_t i;
#ifndef NDEBUG
	assert(sz % 4 == 0);
#endif
	sz /= 4;
	for(i=0; i<sz; ++i)
	{
		if(((*a++)&*mask) != ((*b++)&*mask))
			return 0;
		++mask;
	}
	return 1;
}

int
acl_addr_match_range_v4(uint32_t* minval, uint32_t* x, uint32_t* maxval, size_t sz)
{
	assert(sz == 4); (void)sz;
	/* check treats x as one huge number */

	/* if outside bounds, we are done */
	if(*minval > *x)
		return 0;
	if(*maxval < *x)
		return 0;

	return 1;
}

#ifdef INET6
int
acl_addr_match_range_v6(uint32_t* minval, uint32_t* x, uint32_t* maxval, size_t sz)
{
	size_t i;
	uint8_t checkmin = 1, checkmax = 1;
#ifndef NDEBUG
	assert(sz % 4 == 0);
#endif
	/* check treats x as one huge number */
	sz /= 4;
	for(i=0; i<sz; ++i)
	{
		/* if outside bounds, we are done */
		if(checkmin)
			if(minval[i] > x[i])
				return 0;
		if(checkmax)
			if(maxval[i] < x[i])
				return 0;
		/* if x is equal to a bound, that bound needs further checks */
		if(checkmin && minval[i]!=x[i])
			checkmin = 0;
		if(checkmax && maxval[i]!=x[i])
			checkmax = 0;
		if(!checkmin && !checkmax)
			return 1; /* will always match */
	}
	return 1;
}
#endif /* INET6 */

#ifdef HAVE_SSL
/* Code in for matches_subject_alternative_name and matches_common_name
 * functions is from https://wiki.openssl.org/index.php/Hostname_validation
 * with modifications.
 *
 * Obtained from: https://github.com/iSECPartners/ssl-conservatory
 * Copyright (C) 2012, iSEC Partners.
 * License: MIT License
 * Author:  Alban Diquet
 */
static int matches_subject_alternative_name(
	const char *acl_cert_cn, size_t acl_cert_cn_len, const X509 *cert)
{
	int result = 0;
	int san_names_nb = -1;
	STACK_OF(GENERAL_NAME) *san_names = NULL;

	/* Try to extract the names within the SAN extension from the certificate */
	san_names = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
	if (san_names == NULL)
		return 0;

	san_names_nb = sk_GENERAL_NAME_num(san_names);

	/* Check each name within the extension */
	for (int i = 0; i < san_names_nb && !result; i++) {
		int len;
		const char *str;
		const GENERAL_NAME *current_name = sk_GENERAL_NAME_value(san_names, i);
		/* Skip non-DNS SAN entries. */
		if (current_name->type != GEN_DNS)
			continue;
#if HAVE_ASN1_STRING_GET0_DATA
		str = (const char *)ASN1_STRING_get0_data(current_name->d.dNSName);
#else
		str = (const char *)ASN1_STRING_data(current_name->d.dNSName);
#endif
		if (str == NULL)
			continue;
		len = ASN1_STRING_length(current_name->d.dNSName);
		if (acl_cert_cn_len == (size_t)len &&
		    strncasecmp(str, acl_cert_cn, len) == 0)
		{
			result = 1;
		} else {
			/* Make sure there isn't an embedded NUL character in the DNS name */
			/* From the man page: In general it cannot be assumed that the data
			 * returned by ASN1_STRING_data() is null terminated or does not
			 * contain embedded nulls. */
			int pos = 0;
			while (pos < len && str[pos] != 0)
				pos++;
			if (pos == len) {
				DEBUG(DEBUG_XFRD, 2, (LOG_INFO,
					"SAN %*s does not match acl for %s", len, str, acl_cert_cn));
			} else {
				DEBUG(DEBUG_XFRD, 2, (LOG_INFO, "Malformed SAN in certificate"));
				break;
			}
		}
	}
	sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);

	return result;
}

static int matches_common_name(
	const char *acl_cert_cn, size_t acl_cert_cn_len, const X509 *cert)
{
	int len;
	int common_name_loc = -1;
	const char *common_name_str = NULL;
	X509_NAME *subject_name = NULL;
	X509_NAME_ENTRY *common_name_entry = NULL;
	ASN1_STRING *common_name_asn1 = NULL;

	if ((subject_name = X509_get_subject_name(cert)) == NULL)
		return 0;

	/* Find the position of the CN field in the Subject field of the certificate */
	common_name_loc = X509_NAME_get_index_by_NID(subject_name, NID_commonName, -1);
	if (common_name_loc < 0)
		return 0;

	/* Extract the CN field */
	common_name_entry = X509_NAME_get_entry(subject_name, common_name_loc);
	if (common_name_entry == NULL)
		return 0;

	/* Convert the CN field to a C string */
	common_name_asn1 = X509_NAME_ENTRY_get_data(common_name_entry);
	if (common_name_asn1 == NULL)
		return 0;

#if HAVE_ASN1_STRING_GET0_DATA
	common_name_str = (const char *)ASN1_STRING_get0_data(common_name_asn1);
#else
	common_name_str = (const char *)ASN1_STRING_data(common_name_asn1);
#endif

	len = ASN1_STRING_length(common_name_asn1);
	if (acl_cert_cn_len == (size_t)len &&
	    strncasecmp(acl_cert_cn, common_name_str, len) == 0)
	{
		return 1;
	} else {
		/* Make sure there isn't an embedded NUL character in the CN */
		int pos = 0;
		while (pos < len && common_name_str[pos] != 0)
			pos++;
		if (pos == len) {
			DEBUG(DEBUG_XFRD, 2, (LOG_INFO,
				"CN %*s does not match acl for %s", len, common_name_str, acl_cert_cn));
		} else {
			DEBUG(DEBUG_XFRD, 2, (LOG_INFO, "Malformed CN in certificate"));
		}
	}

	return 0;
}

int
acl_tls_hostname_matches(SSL* tls_auth, const char *acl_cert_cn)
{
	int result = 0;
	size_t acl_cert_cn_len;
	X509 *client_cert;

	assert(acl_cert_cn != NULL);

#ifdef HAVE_SSL_GET1_PEER_CERTIFICATE
	client_cert = SSL_get1_peer_certificate(tls_auth);
#else
	client_cert = SSL_get_peer_certificate(tls_auth);
#endif

	if (client_cert == NULL)
		return 0;

	/* OpenSSL provides functions for hostname checking from certificate
	 * Following code should work but it doesn't.
	 * Keep it for future test in order to not use custom code
	 *
	 * X509_VERIFY_PARAM *vpm = SSL_get0_param(tls_auth);
	 * Hostname check is done here:
	 * X509_VERIFY_PARAM_set1_host(vpm, acl_cert_cn, 0); // recommended
	 * X509_check_host() // can also be used instead. Not recommended DANE-EE
	 * SSL_get_verify_result(tls_auth) != X509_V_OK) // NOT ok
	 * const char *peername = X509_VERIFY_PARAM_get0_peername(vpm); // NOT ok
	 */

	acl_cert_cn_len = strlen(acl_cert_cn);
	/* semi follow RFC6125#section-6.4.4 check SAN DNS first */
	if (!(result = matches_subject_alternative_name(acl_cert_cn, acl_cert_cn_len, client_cert)))
		result = matches_common_name(acl_cert_cn, acl_cert_cn_len, client_cert);

	X509_free(client_cert);

	return result;
}
#endif

int
acl_key_matches(struct acl_options* acl, struct query* q)
{
	if(acl->blocked)
		return 1;
	if(acl->nokey) {
		if(q->tsig.status == TSIG_NOT_PRESENT)
			return 1;
		return 0;
	}
	/* check name of tsig key */
	if(q->tsig.status != TSIG_OK) {
		DEBUG(DEBUG_XFRD,2, (LOG_INFO, "keymatch fail query has no TSIG"));
		return 0; /* query has no TSIG */
	}
	if(q->tsig.error_code != TSIG_ERROR_NOERROR) {
		DEBUG(DEBUG_XFRD,2, (LOG_INFO, "keymatch fail, tsig has error"));
		return 0; /* some tsig error */
	}
	if(!acl->key_options->tsig_key) {
		DEBUG(DEBUG_XFRD,2, (LOG_INFO, "keymatch fail no config"));
		return 0; /* key not properly configured */
	}
	if(dname_compare(q->tsig.key_name,
		acl->key_options->tsig_key->name) != 0) {
		DEBUG(DEBUG_XFRD,2, (LOG_INFO, "keymatch fail wrong key name"));
		return 0; /* wrong key name */
	}
	if(tsig_strlowercmp(q->tsig.algorithm->short_name,
		acl->key_options->algorithm) != 0 && (
		strncmp("hmac-", q->tsig.algorithm->short_name, 5) != 0 ||
		tsig_strlowercmp(q->tsig.algorithm->short_name+5,
		acl->key_options->algorithm) != 0) ) {
		DEBUG(DEBUG_XFRD,2, (LOG_ERR, "query tsig wrong algorithm"));
		return 0; /* no such algo */
	}
	return 1;
}

int
acl_same_host(struct acl_options* a, struct acl_options* b)
{
	if(a->is_ipv6 && !b->is_ipv6)
		return 0;
	if(!a->is_ipv6 && b->is_ipv6)
		return 0;
	if(a->port != b->port)
		return 0;
	if(a->rangetype != b->rangetype)
		return 0;
	if(!a->is_ipv6) {
		if(memcmp(&a->addr.addr, &b->addr.addr,
		   sizeof(struct in_addr)) != 0)
			return 0;
		if(a->rangetype != acl_range_single &&
		   memcmp(&a->range_mask.addr, &b->range_mask.addr,
		   sizeof(struct in_addr)) != 0)
			return 0;
	} else {
#ifdef INET6
		if(memcmp(&a->addr.addr6, &b->addr.addr6,
		   sizeof(struct in6_addr)) != 0)
			return 0;
		if(a->rangetype != acl_range_single &&
		   memcmp(&a->range_mask.addr6, &b->range_mask.addr6,
		   sizeof(struct in6_addr)) != 0)
			return 0;
#else
		return 0;
#endif
	}
	return 1;
}

#if defined(HAVE_SSL)
void
key_options_tsig_add(struct nsd_options* opt)
{
	struct key_options* optkey;
	RBTREE_FOR(optkey, struct key_options*, opt->keys) {
		key_options_setup(opt->region, optkey);
		tsig_add_key(optkey->tsig_key);
	}
}
#endif

int
zone_is_slave(struct zone_options* opt)
{
	return opt && opt->pattern && opt->pattern->request_xfr != 0;
}

/* get a character in string (or replacement char if not long enough) */
static const char*
get_char(const char* str, size_t i)
{
	static char res[2];
	if(i >= strlen(str))
		return ".";
	res[0] = str[i];
	res[1] = 0;
	return res;
}
/* get end label of the zone name (or .) */
static const char*
get_end_label(struct zone_options* zone, int i)
{
	const dname_type* d = (const dname_type*)zone->node.key;
	if(i >= d->label_count) {
		return ".";
	}
	return wirelabel2str(dname_label(d, i));
}
/* replace occurrences of one with two */
void
replace_str(char* str, size_t len, const char* one, const char* two)
{
	char* pos;
	char* at = str;
	while( (pos=strstr(at, one)) ) {
		if(strlen(str)+strlen(two)-strlen(one) >= len)
			return; /* no more space to replace */
		/* stuff before pos is fine */
		/* move the stuff after pos to make space for two, add
		 * one to length of remainder to also copy the 0 byte end */
		memmove(pos+strlen(two), pos+strlen(one),
			strlen(pos+strlen(one))+1);
		/* copy in two */
		memmove(pos, two, strlen(two));
		/* at is end of the newly inserted two (avoids recursion if
		 * two contains one) */
		at = pos+strlen(two);
	}
}

const char*
config_cook_string(struct zone_options* zone, const char* input)
{
	static char f[1024];
	/* if not a template, return as-is */
	if(!strchr(input, '%')) {
		return input;
	}
	strlcpy(f, input, sizeof(f));
	if(strstr(f, "%1"))
		replace_str(f, sizeof(f), "%1", get_char(zone->name, 0));
	if(strstr(f, "%2"))
		replace_str(f, sizeof(f), "%2", get_char(zone->name, 1));
	if(strstr(f, "%3"))
		replace_str(f, sizeof(f), "%3", get_char(zone->name, 2));
	if(strstr(f, "%z"))
		replace_str(f, sizeof(f), "%z", get_end_label(zone, 1));
	if(strstr(f, "%y"))
		replace_str(f, sizeof(f), "%y", get_end_label(zone, 2));
	if(strstr(f, "%x"))
		replace_str(f, sizeof(f), "%x", get_end_label(zone, 3));
	if(strstr(f, "%s"))
		replace_str(f, sizeof(f), "%s", zone->name);
	return f;
}

const char*
config_make_zonefile(struct zone_options* zone, struct nsd* nsd)
{
	static char f[1024];
	/* if not a template, return as-is */
	if(!strchr(zone->pattern->zonefile, '%')) {
		if (nsd->chrootdir && nsd->chrootdir[0] &&
			zone->pattern->zonefile &&
			zone->pattern->zonefile[0] == '/' &&
			strncmp(zone->pattern->zonefile, nsd->chrootdir,
			strlen(nsd->chrootdir)) == 0)
			/* -1 because chrootdir ends in trailing slash */
			return zone->pattern->zonefile + strlen(nsd->chrootdir) - 1;
		return zone->pattern->zonefile;
	}
	strlcpy(f, zone->pattern->zonefile, sizeof(f));
	if(strstr(f, "%1"))
		replace_str(f, sizeof(f), "%1", get_char(zone->name, 0));
	if(strstr(f, "%2"))
		replace_str(f, sizeof(f), "%2", get_char(zone->name, 1));
	if(strstr(f, "%3"))
		replace_str(f, sizeof(f), "%3", get_char(zone->name, 2));
	if(strstr(f, "%z"))
		replace_str(f, sizeof(f), "%z", get_end_label(zone, 1));
	if(strstr(f, "%y"))
		replace_str(f, sizeof(f), "%y", get_end_label(zone, 2));
	if(strstr(f, "%x"))
		replace_str(f, sizeof(f), "%x", get_end_label(zone, 3));
	if(strstr(f, "%s"))
		replace_str(f, sizeof(f), "%s", zone->name);
	if (nsd->chrootdir && nsd->chrootdir[0] && f[0] == '/' &&
		strncmp(f, nsd->chrootdir, strlen(nsd->chrootdir)) == 0)
		/* -1 because chrootdir ends in trailing slash */
		return f + strlen(nsd->chrootdir) - 1;
	return f;
}

struct zone_options*
zone_options_find(struct nsd_options* opt, const struct dname* apex)
{
	return (struct zone_options*) rbtree_search(opt->zone_options, apex);
}

struct acl_options*
acl_find_num(struct acl_options* acl, int num)
{
	int count = num;
	if(num < 0)
		return 0;
	while(acl && count > 0) {
		acl = acl->next;
		count--;
	}
	if(count == 0)
		return acl;
	return 0;
}

/* true if ipv6 address, false if ipv4 */
int
parse_acl_is_ipv6(const char* p)
{
	/* see if addr is ipv6 or ipv4 -- by : and . */
	while(*p) {
		if(*p == '.') return 0;
		if(*p == ':') return 1;
		++p;
	}
	return 0;
}

/* returns range type. mask is the 2nd part of the range */
int
parse_acl_range_type(char* ip, char** mask)
{
	char *p;
	if((p=strchr(ip, '&'))!=0) {
		*p = 0;
		*mask = p+1;
		return acl_range_mask;
	}
	if((p=strchr(ip, '/'))!=0) {
		*p = 0;
		*mask = p+1;
		return acl_range_subnet;
	}
	if((p=strchr(ip, '-'))!=0) {
		*p = 0;
		*mask = p+1;
		return acl_range_minmax;
	}
	*mask = 0;
	return acl_range_single;
}

/* parses subnet mask, fills 0 mask as well */
void
parse_acl_range_subnet(char* p, void* addr, int maxbits)
{
	int subnet_bits = atoi(p);
	uint8_t* addr_bytes = (uint8_t*)addr;
	if(subnet_bits == 0 && strcmp(p, "0")!=0) {
		c_error("bad subnet range '%s'", p);
		return;
	}
	if(subnet_bits < 0 || subnet_bits > maxbits) {
		c_error("subnet of %d bits out of range [0..%d]", subnet_bits, maxbits);
		return;
	}
	/* fill addr with n bits of 1s (struct has been zeroed) */
	while(subnet_bits >= 8) {
		*addr_bytes++ = 0xff;
		subnet_bits -= 8;
	}
	if(subnet_bits > 0) {
		uint8_t shifts[] = {0x0, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff};
		*addr_bytes = shifts[subnet_bits];
	}
}

struct acl_options*
parse_acl_info(region_type* region, char* ip, const char* key)
{
	char* p;
	struct acl_options* acl = (struct acl_options*)region_alloc(region,
		sizeof(struct acl_options));
	acl->next = 0;
	/* ip */
	acl->ip_address_spec = region_strdup(region, ip);
	acl->use_axfr_only = 0;
	acl->allow_udp = 0;
	acl->ixfr_disabled = 0;
	acl->bad_xfr_count = 0;
	acl->key_options = 0;
	acl->tls_auth_options = 0;
	acl->tls_auth_name = 0;
	acl->is_ipv6 = 0;
	acl->port = 0;
	memset(&acl->addr, 0, sizeof(union acl_addr_storage));
	memset(&acl->range_mask, 0, sizeof(union acl_addr_storage));
	if((p=strrchr(ip, '@'))!=0) {
		if(atoi(p+1) == 0) c_error("expected port number after '@'");
		else acl->port = atoi(p+1);
		*p=0;
	}
	acl->rangetype = parse_acl_range_type(ip, &p);
	if(parse_acl_is_ipv6(ip)) {
		acl->is_ipv6 = 1;
#ifdef INET6
		if(inet_pton(AF_INET6, ip, &acl->addr.addr6) != 1)
			c_error("Bad ip6 address '%s'", ip);
		if(acl->rangetype==acl_range_mask || acl->rangetype==acl_range_minmax) {
			assert(p);
			if(inet_pton(AF_INET6, p, &acl->range_mask.addr6) != 1)
				c_error("Bad ip6 address mask '%s'", p);
		}
		if(acl->rangetype==acl_range_subnet) {
			assert(p);
			parse_acl_range_subnet(p, &acl->range_mask.addr6, 128);
		}
#else
		c_error("encountered IPv6 address '%s'.", ip);
#endif /* INET6 */
	} else {
		acl->is_ipv6 = 0;
		if(inet_pton(AF_INET, ip, &acl->addr.addr) != 1)
			c_error("Bad ip4 address '%s'", ip);
		if(acl->rangetype==acl_range_mask || acl->rangetype==acl_range_minmax) {
			assert(p);
			if(inet_pton(AF_INET, p, &acl->range_mask.addr) != 1)
				c_error("Bad ip4 address mask '%s'", p);
		}
		if(acl->rangetype==acl_range_subnet) {
			assert(p);
			parse_acl_range_subnet(p, &acl->range_mask.addr, 32);
		}
	}

	/* key */
	if(strcmp(key, "NOKEY")==0) {
		acl->nokey = 1;
		acl->blocked = 0;
		acl->key_name = 0;
	} else if(strcmp(key, "BLOCKED")==0) {
		acl->nokey = 0;
		acl->blocked = 1;
		acl->key_name = 0;
	} else {
		acl->nokey = 0;
		acl->blocked = 0;
		acl->key_name = region_strdup(region, key);
	}
	return acl;
}

/* copy acl list at end of parser start, update current */
static
void copy_and_append_acls(struct acl_options** start, struct acl_options* list)
{
	struct acl_options *tail = NULL;

	assert(start != NULL);

	tail = *start;
	if(tail) {
		while(tail->next) {
			tail = tail->next;
		}
	}

	while(list) {
		struct acl_options* acl = copy_acl(cfg_parser->opt->region,
			list);
		acl->next = NULL;
		if(tail) {
			tail->next = acl;
		} else {
			*start = acl;
		}
		tail = acl;
		list = list->next;
	}
}

void
config_apply_pattern(struct pattern_options *dest, const char* name)
{
	/* find the pattern */
	struct pattern_options* pat = pattern_options_find(cfg_parser->opt,
		name);
	if(!pat) {
		c_error("could not find pattern %s", name);
		return;
	}
	if( (!dest->pname || strncmp(dest->pname, PATTERN_IMPLICIT_MARKER,
				strlen(PATTERN_IMPLICIT_MARKER)) == 0)
	&& pat->catalog_producer_zone) {
		c_error("patterns with an catalog-producer-zone option are to "
		        "be used with \"nsd-control addzone\" only and cannot "
			"be included from zone clauses in the config file");
		return;
	}
	if((dest->catalog_role == CATALOG_ROLE_PRODUCER &&  pat->request_xfr)
	|| ( pat->catalog_role == CATALOG_ROLE_PRODUCER && dest->request_xfr)){
		c_error("catalog producer zones cannot be secondary zones");
	}

	/* apply settings */
	if(pat->zonefile)
		dest->zonefile = region_strdup(cfg_parser->opt->region,
			pat->zonefile);
	if(pat->zonestats)
		dest->zonestats = region_strdup(cfg_parser->opt->region,
			pat->zonestats);
	if(!pat->allow_axfr_fallback_is_default) {
		dest->allow_axfr_fallback = pat->allow_axfr_fallback;
		dest->allow_axfr_fallback_is_default = 0;
	}
	if(!pat->notify_retry_is_default) {
		dest->notify_retry = pat->notify_retry;
		dest->notify_retry_is_default = 0;
	}
	if(!pat->max_refresh_time_is_default) {
		dest->max_refresh_time = pat->max_refresh_time;
		dest->max_refresh_time_is_default = 0;
	}
	if(!pat->min_refresh_time_is_default) {
		dest->min_refresh_time = pat->min_refresh_time;
		dest->min_refresh_time_is_default = 0;
	}
	if(!pat->max_retry_time_is_default) {
		dest->max_retry_time = pat->max_retry_time;
		dest->max_retry_time_is_default = 0;
	}
	if(!pat->min_retry_time_is_default) {
		dest->min_retry_time = pat->min_retry_time;
		dest->min_retry_time_is_default = 0;
	}
	if(!expire_time_is_default(pat->min_expire_time_expr)) {
		dest->min_expire_time = pat->min_expire_time;
		dest->min_expire_time_expr = pat->min_expire_time_expr;
	}
	if(!pat->store_ixfr_is_default) {
		dest->store_ixfr = pat->store_ixfr;
		dest->store_ixfr_is_default = 0;
	}
	if(!pat->ixfr_size_is_default) {
		dest->ixfr_size = pat->ixfr_size;
		dest->ixfr_size_is_default = 0;
	}
	if(!pat->ixfr_number_is_default) {
		dest->ixfr_number = pat->ixfr_number;
		dest->ixfr_number_is_default = 0;
	}
	if(!pat->create_ixfr_is_default) {
		dest->create_ixfr = pat->create_ixfr;
		dest->create_ixfr_is_default = 0;
	}
	dest->size_limit_xfr = pat->size_limit_xfr;
#ifdef RATELIMIT
	dest->rrl_whitelist |= pat->rrl_whitelist;
#endif
	/* append acl items */
	copy_and_append_acls(&dest->allow_notify, pat->allow_notify);
	copy_and_append_acls(&dest->request_xfr, pat->request_xfr);
	copy_and_append_acls(&dest->notify, pat->notify);
	copy_and_append_acls(&dest->provide_xfr, pat->provide_xfr);
	copy_and_append_acls(&dest->allow_query, pat->allow_query);
	copy_and_append_acls(&dest->outgoing_interface, pat->outgoing_interface);
	if(pat->multi_primary_check)
		dest->multi_primary_check = pat->multi_primary_check;

	if(!pat->verify_zone_is_default) {
		dest->verify_zone = pat->verify_zone;
		dest->verify_zone_is_default = 0;
	}
	if(!pat->verifier_timeout_is_default) {
		dest->verifier_timeout = pat->verifier_timeout;
		dest->verifier_timeout_is_default = 0;
	}
	if(!pat->verifier_feed_zone_is_default) {
		dest->verifier_feed_zone = pat->verifier_feed_zone;
		dest->verifier_feed_zone_is_default = 0;
	}
	if(pat->verifier != NULL) {
		size_t cnt;
		char **vec;
		region_type *region = cfg_parser->opt->region;

		for(cnt = 0; pat->verifier[cnt] != NULL; cnt++) ;
		vec = region_alloc(region, (cnt + 1) * sizeof(char *));
		for(cnt = 0; pat->verifier[cnt] != NULL; cnt++) {
			vec[cnt] = region_strdup(region, pat->verifier[cnt]);
		}
		vec[cnt] = NULL;
		if(dest->verifier != NULL) {
			size_t size;
			for(cnt = 0; dest->verifier[cnt] != NULL; cnt++) {
				size = strlen(dest->verifier[cnt]) + 1;
				region_recycle(
					region, dest->verifier[cnt], size);
			}
			size = (cnt + 1) * sizeof(char *);
			region_recycle(region, dest->verifier, size);
		}
		dest->verifier = vec;
	}
	if(!pat->catalog_role_is_default) {
		dest->catalog_role = pat->catalog_role;
		dest->catalog_role_is_default = 0;
	}
	if(pat->catalog_member_pattern)
		dest->catalog_member_pattern = region_strdup(
			cfg_parser->opt->region, pat->catalog_member_pattern);
	if(pat->catalog_producer_zone)
		dest->catalog_producer_zone = region_strdup(
			cfg_parser->opt->region, pat->catalog_producer_zone);
}

void
nsd_options_destroy(struct nsd_options* opt)
{
	region_destroy(opt->region);
#ifdef MEMCLEAN /* OS collects memory pages */
	c_lex_destroy();
#endif
}

unsigned getzonestatid(struct nsd_options* opt, struct zone_options* zopt)
{
#ifdef USE_ZONE_STATS
	const char* statname;
	struct zonestatname* n;
	rbnode_type* res;
	/* try to find the instantiated zonestat name */
	if(!zopt->pattern->zonestats || zopt->pattern->zonestats[0]==0)
		return 0; /* no zone stats */
	statname = config_cook_string(zopt, zopt->pattern->zonestats);
	res = rbtree_search(opt->zonestatnames, statname);
	if(res)
		return ((struct zonestatname*)res)->id;
	/* create it */
	n = (struct zonestatname*)region_alloc_zero(opt->region, sizeof(*n));
	n->node.key = region_strdup(opt->region, statname);
	if(!n->node.key) {
		log_msg(LOG_ERR, "malloc failed: %s", strerror(errno));
		exit(1);
	}
	n->id = (unsigned)(opt->zonestatnames->count);
	rbtree_insert(opt->zonestatnames, (rbnode_type*)n);
	return n->id;
#else /* USE_ZONE_STATS */
	(void)opt; (void)zopt;
	return 0;
#endif /* USE_ZONE_STATS */
}

/** check if config turns on IP-address interface with certificates or a
 * named pipe without certificates. */
int
options_remote_is_address(struct nsd_options* cfg)
{
	if(!cfg->control_enable) return 0;
	if(!cfg->control_interface) return 1;
	if(!cfg->control_interface->address) return 1;
	if(cfg->control_interface->address[0] == 0) return 1;
	return (cfg->control_interface->address[0] != '/');
}

#ifdef HAVE_GETIFADDRS
static void
resolve_ifa_name(struct ifaddrs *ifas, const char *search_ifa, char ***ip_addresses, size_t *ip_addresses_size)
{
	struct ifaddrs *ifa;
	size_t last_ip_addresses_size = *ip_addresses_size;

	for(ifa = ifas; ifa != NULL; ifa = ifa->ifa_next) {
		sa_family_t family;
		const char* atsign;
#ifdef INET6      /* |   address ip    | % |  ifa name  | @ |  port  | nul */
		char addr_buf[INET6_ADDRSTRLEN + 1 + IF_NAMESIZE + 1 + 16 + 1];
#else
		char addr_buf[INET_ADDRSTRLEN + 1 + 16 + 1];
#endif

		if((atsign=strrchr(search_ifa, '@')) != NULL) {
			if(strlen(ifa->ifa_name) != (size_t)(atsign-search_ifa)
			   || strncmp(ifa->ifa_name, search_ifa,
			   atsign-search_ifa) != 0)
				continue;
		} else {
			if(strcmp(ifa->ifa_name, search_ifa) != 0)
				continue;
			atsign = "";
		}

		if(ifa->ifa_addr == NULL)
			continue;

		family = ifa->ifa_addr->sa_family;
		if(family == AF_INET) {
			char a4[INET_ADDRSTRLEN + 1];
			struct sockaddr_in *in4 = (struct sockaddr_in *)
				ifa->ifa_addr;
			if(!inet_ntop(family, &in4->sin_addr, a4, sizeof(a4)))
				error("inet_ntop");
			snprintf(addr_buf, sizeof(addr_buf), "%s%s",
				a4, atsign);
		}
#ifdef INET6
		else if(family == AF_INET6) {
			struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)
				ifa->ifa_addr;
			char a6[INET6_ADDRSTRLEN + 1];
			char if_index_name[IF_NAMESIZE + 1];
			if_index_name[0] = 0;
			if(!inet_ntop(family, &in6->sin6_addr, a6, sizeof(a6)))
				error("inet_ntop");
			if_indextoname(in6->sin6_scope_id,
				(char *)if_index_name);
			if (strlen(if_index_name) != 0) {
				snprintf(addr_buf, sizeof(addr_buf),
					"%s%%%s%s", a6, if_index_name, atsign);
			} else {
				snprintf(addr_buf, sizeof(addr_buf), "%s%s",
					a6, atsign);
			}
		}
#endif
		else {
			continue;
		}
		VERBOSITY(4, (LOG_INFO, "interface %s has address %s",
			search_ifa, addr_buf));

		*ip_addresses = xrealloc(*ip_addresses, sizeof(char *) * (*ip_addresses_size + 1));
		(*ip_addresses)[*ip_addresses_size] = xstrdup(addr_buf);
		(*ip_addresses_size)++;
	}

	if (*ip_addresses_size == last_ip_addresses_size) {
		*ip_addresses = xrealloc(*ip_addresses, sizeof(char *) * (*ip_addresses_size + 1));
		(*ip_addresses)[*ip_addresses_size] = xstrdup(search_ifa);
		(*ip_addresses_size)++;
	}
}

static void
resolve_interface_names_for_ref(struct ip_address_option** ip_addresses_ref,
		struct ifaddrs *addrs, region_type* region)
{
	struct ip_address_option *ip_addr;
	struct ip_address_option *last = NULL;
	struct ip_address_option *first = NULL;

	/* replace the list of ip_adresses with a new list where the
	 * interface names are replaced with their ip-address strings
	 * from getifaddrs.  An interface can have several addresses. */
	for(ip_addr = *ip_addresses_ref; ip_addr; ip_addr = ip_addr->next) {
		char **ip_addresses = NULL;
		size_t ip_addresses_size = 0, i;
		resolve_ifa_name(addrs, ip_addr->address, &ip_addresses,
			&ip_addresses_size);

		for (i = 0; i < ip_addresses_size; i++) {
			struct ip_address_option *current;
			/* this copies the range_option, dev, and fib from
			 * the original ip_address option to the new ones
			 * with the addresses spelled out by resolve_ifa_name*/
			current = region_alloc_init(region, ip_addr,
				sizeof(*ip_addr));
			current->address = region_strdup(region,
				ip_addresses[i]);
			current->next = NULL;
			free(ip_addresses[i]);

			if(first == NULL) {
				first = current;
			} else {
				last->next = current;
			}
			last = current;
		}
		free(ip_addresses);
	}
	*ip_addresses_ref = first;

}
#endif /* HAVE_GETIFADDRS */

void
resolve_interface_names(struct nsd_options* options)
{
#ifdef HAVE_GETIFADDRS
	struct ifaddrs *addrs;

	if(getifaddrs(&addrs) == -1)
		  error("failed to list interfaces");

	resolve_interface_names_for_ref(&options->ip_addresses, 
			addrs, options->region);
	resolve_interface_names_for_ref(&options->control_interface, 
			addrs, options->region);
#ifdef USE_METRICS
	resolve_interface_names_for_ref(&options->metrics_interface,
			addrs, options->region);
#endif /* USE_METRICS */

	freeifaddrs(addrs);
#else
	(void)options;
#endif /* HAVE_GETIFADDRS */
}

int
sockaddr_uses_proxy_protocol_port(struct nsd_options* options,
	struct sockaddr* addr)
{
	struct proxy_protocol_port_list* p;
	int port;
#ifdef INET6
	struct sockaddr_storage* ss = (struct sockaddr_storage*)addr;
	if(ss->ss_family == AF_INET6) {
		struct sockaddr_in6* a6 = (struct sockaddr_in6*)addr;
		port = ntohs(a6->sin6_port);
	} else if(ss->ss_family == AF_INET) {
#endif
		struct sockaddr_in* a = (struct sockaddr_in*)addr;
#ifndef INET6
		if(a->sin_family != AF_INET)
			return 0; /* unknown family */
#endif
		port = ntohs(a->sin_port);
#ifdef INET6
	} else {
		return 0; /* unknown family */
	}
#endif
	p = options->proxy_protocol_port;
	while(p) {
		if(p->port == port)
			return 1;
		p = p->next;
	}
	return 0;
}
