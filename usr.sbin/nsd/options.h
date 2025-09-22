/*
 * options.h -- nsd.conf options definitions and prototypes
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef OPTIONS_H
#define OPTIONS_H

#include <stdarg.h>
#ifdef HAVE_SSL
#include <openssl/ssl.h>
#endif
#include "region-allocator.h"
#include "rbtree.h"
struct query;
struct dname;
struct tsig_key;
struct buffer;
struct nsd;
struct proxy_protocol_port_list;


typedef struct nsd_options nsd_options_type;
typedef struct pattern_options pattern_options_type;
typedef struct zone_options zone_options_type;
typedef struct range_option range_option_type;
typedef struct ip_address_option ip_address_option_type;
typedef struct cpu_option cpu_option_type;
typedef struct cpu_map_option cpu_map_option_type;
typedef struct acl_options acl_options_type;
typedef struct key_options key_options_type;
typedef struct tls_auth_options tls_auth_options_type;
typedef struct config_parser_state config_parser_state_type;

#define VERIFY_ZONE_INHERIT (2)
#define VERIFIER_FEED_ZONE_INHERIT (2)
#define VERIFIER_TIMEOUT_INHERIT (-1)
#define CATALOG_ROLE_INHERIT  (0)
#define CATALOG_ROLE_CONSUMER (1)
#define CATALOG_ROLE_PRODUCER (2)

/*
 * Options global for nsd.
 */
struct nsd_options {
	/* config file name */
	char* configfile;
	/* options for zones, by apex, contains zone_options */
	rbtree_type* zone_options;
	/* patterns, by name, contains pattern_options */
	rbtree_type* patterns;

	/* free space in zonelist file, contains zonelist_bucket */
	rbtree_type* zonefree;
	/* number of free space lines in zonelist file */
	size_t zonefree_number;
	/* zonelist file if open */
	FILE* zonelist;
	/* last offset in file (or 0 if none) */
	off_t zonelist_off;

	/* tree of zonestat names and their id values, entries are struct
	 * zonestatname with malloced key=stringname. The number of items
	 * is the max statnameid, no items are freed from this. 
	 * kept correct in the xfrd process, and on startup. */
	rbtree_type* zonestatnames;

	/* rbtree of keys defined, by name */
	rbtree_type* keys;

	/* rbtree of tls_auth defined, by name */
	rbtree_type* tls_auths;

	/* list of ip addresses to bind to (or NULL for all) */
	struct ip_address_option* ip_addresses;

	int ip_transparent;
	int ip_freebind;
	int send_buffer_size;
	int receive_buffer_size;
	int debug_mode;
	int verbosity;
	int hide_version;
	int hide_identity;
	int drop_updates;
	int do_ip4;
	int do_ip6;
	const char* identity;
	const char* version;
	const char* logfile;
	int log_only_syslog;
	int server_count;
	struct cpu_option* cpu_affinity;
	struct cpu_map_option* service_cpu_affinity;
	int tcp_count;
	int tcp_reject_overflow;
	int confine_to_zone;
	int tcp_query_count;
	int tcp_timeout;
	int tcp_mss;
	int outgoing_tcp_mss;
	size_t ipv4_edns_size;
	size_t ipv6_edns_size;
	const char* pidfile;
	const char* port;
	int statistics;
	const char* chroot;
	const char* username;
	const char* zonesdir;
	const char* xfrdfile;
	const char* xfrdir;
	const char* zonelistfile;
	const char* nsid;
	int xfrd_reload_timeout;
	int reload_config;
	int zonefiles_check;
	int zonefiles_write;
	int log_time_ascii;
	int log_time_iso;
	int round_robin;
	int minimal_responses;
	int refuse_any;
	int reuseport;
	/* max number of xfrd tcp sockets */
	int xfrd_tcp_max;
	/* max number of simultaneous requests on xfrd tcp socket */
	int xfrd_tcp_pipeline;

	/* private key file for TLS */
	char* tls_service_key;
	/* ocsp stapling file for TLS */
	char* tls_service_ocsp;
	/* certificate file for TLS */
	char* tls_service_pem;
	/* TLS dedicated port */
	const char* tls_port;
	/* TLS-AUTH dedicated port */
	const char* tls_auth_port;
	/* TLS certificate bundle */
	const char* tls_cert_bundle;
	/* Answer XFR only from tls_auth_port and after authentication */
	int tls_auth_xfr_only;

	/* proxy protocol port list */
	struct proxy_protocol_port_list* proxy_protocol_port;

	/** remote control section. enable toggle. */
	int control_enable;
	/** the interfaces the remote control should listen on */
	struct ip_address_option* control_interface;
	/** port number for the control port */
	int control_port;
	/** private key file for server */
	char* server_key_file;
	/** certificate file for server */
	char* server_cert_file;
	/** private key file for nsd-control */
	char* control_key_file;
	/** certificate file for nsd-control */
	char* control_cert_file;

#ifdef USE_XDP
	/** XDP interface name */
	const char* xdp_interface;
	/** XDP/eBPF program file path */
	const char* xdp_program_path;
	/** if NSD should load the XDP/eBPF program */
	int xdp_program_load;
	/** path to bpffs for pinned BPF objects */
	const char* xdp_bpffs_path;
	/** force copy mode instead of zero copy mode */
	int xdp_force_copy;
#endif

#ifdef USE_METRICS
	/** metrics section. enable toggle. */
	int metrics_enable;
	/** the interfaces the metrics endpoint should listen on */
	struct ip_address_option* metrics_interface;
	/** port number for the metrics endpoint */
	int metrics_port;
	/** HTTP path for the metrics endpoint */
	char* metrics_path;
#endif /* USE_METRICS */

#ifdef RATELIMIT
	/** number of buckets in rrl hashtable */
	size_t rrl_size;
	/** max qps for queries, 0 is nolimit */
	size_t rrl_ratelimit;
	/** ratio of slipped responses, 0 is noslip */
	size_t rrl_slip;
	/** ip prefix length */
	size_t rrl_ipv4_prefix_length;
	size_t rrl_ipv6_prefix_length;
	/** max qps for whitelisted queries, 0 is nolimit */
	size_t rrl_whitelist_ratelimit;
#endif
	/** if dnstap is enabled */
	int dnstap_enable;
	/** dnstap socket path */
	char* dnstap_socket_path;
	/** dnstap IP, if "", it uses socket path. */
	char* dnstap_ip;
	/** dnstap TLS enable */
	int dnstap_tls;
	/** dnstap tls server authentication name */
	char* dnstap_tls_server_name;
	/** dnstap server cert bundle */
	char* dnstap_tls_cert_bundle;
	/** dnstap client key for client authentication */
	char* dnstap_tls_client_key_file;
	/** dnstap client cert for client authentication */
	char* dnstap_tls_client_cert_file;
	/** true to send "identity" via dnstap */
	int dnstap_send_identity;
	/** true to send "version" via dnstap */
	int dnstap_send_version;
	/** dnstap "identity", hostname is used if "". */
	char* dnstap_identity;
	/** dnstap "version", package version is used if "". */
	char* dnstap_version;
	/** true to log dnstap AUTH_QUERY message events */
	int dnstap_log_auth_query_messages;
	/** true to log dnstap AUTH_RESPONSE message events */
	int dnstap_log_auth_response_messages;

	/** do answer with server cookie when request contained cookie option */
	int answer_cookie;
	/** cookie secret */
	char *cookie_secret;
	/** cookie staging secret */
	char *cookie_staging_secret;
	/** path to cookie secret store */
	char *cookie_secret_file;
	/** set when the cookie_secret_file whas not explicitely configured */
	uint8_t cookie_secret_file_is_default;
	/** enable verify */
	int verify_enable;
	/** list of ip addresses used to serve zones for verification */
	struct ip_address_option* verify_ip_addresses;
	/** default port 5347 */
	char *verify_port;
	/** verify zones by default */
	int verify_zones;
	/** default command to verify zones with */
	char **verifier;
	/** maximum number of verifiers that may run simultaneously */
	int verifier_count;
	/** whether or not to feed the zone to the verifier over stdin */
	uint8_t verifier_feed_zone;
	/** maximum number of seconds that a verifier may take */
	uint32_t verifier_timeout;

	region_type* region;
};

struct range_option {
	struct range_option* next;
	int first;
	int last;
};

struct ip_address_option {
	struct ip_address_option* next;
	char* address;
	struct range_option* servers;
	int dev;
	int fib;
};

struct cpu_option {
	struct cpu_option* next;
	int cpu;
};

struct cpu_map_option {
	struct cpu_map_option* next;
	int service;
	int cpu;
};

/*
 * Defines for min_expire_time_expr value
 */
#define EXPIRE_TIME_HAS_VALUE     0
#define EXPIRE_TIME_IS_DEFAULT    1
#define REFRESHPLUSRETRYPLUS1     2
#define REFRESHPLUSRETRYPLUS1_STR "refresh+retry+1"
#define expire_time_is_default(x) (!(  (x) == REFRESHPLUSRETRYPLUS1 \
                                    || (x) == EXPIRE_TIME_HAS_VALUE ))


/*
 * Pattern of zone options, used to contain options for zone(s).
 */
struct pattern_options {
	rbnode_type node;
	const char* pname; /* name of the pattern, key of rbtree */
	const char* zonefile;
	struct acl_options* allow_notify;
	struct acl_options* request_xfr;
	struct acl_options* notify;
	struct acl_options* provide_xfr;
	struct acl_options* allow_query;
	struct acl_options* outgoing_interface;
	const char* zonestats;
#ifdef RATELIMIT
	uint16_t rrl_whitelist; /* bitmap with rrl types */
#endif
	uint8_t allow_axfr_fallback;
	uint8_t allow_axfr_fallback_is_default;
	uint8_t notify_retry;
	uint8_t notify_retry_is_default;
	uint8_t implicit; /* pattern is implicit, part_of_config zone used */
	uint8_t xfrd_flags;
	uint32_t max_refresh_time;
	uint8_t max_refresh_time_is_default;
	uint32_t min_refresh_time;
	uint8_t min_refresh_time_is_default;
	uint32_t max_retry_time;
	uint8_t max_retry_time_is_default;
	uint32_t min_retry_time;
	uint8_t min_retry_time_is_default;
	uint32_t min_expire_time;
	/* min_expir_time_expr is either a known value (REFRESHPLUSRETRYPLUS1
	 * or EXPIRE_EXPR_HAS_VALUE) or else min_expire_time is the default.
	 * This can be tested with expire_time_is_default(x) define.
	 */
	uint8_t min_expire_time_expr;
	uint64_t size_limit_xfr;
	uint8_t multi_primary_check;
	uint8_t store_ixfr;
	uint8_t store_ixfr_is_default;
	uint64_t ixfr_size;
	uint8_t ixfr_size_is_default;
	uint32_t ixfr_number;
	uint8_t ixfr_number_is_default;
	uint8_t create_ixfr;
	uint8_t create_ixfr_is_default;
	uint8_t verify_zone;
	uint8_t verify_zone_is_default;
	char **verifier;
	uint8_t verifier_feed_zone;
	uint8_t verifier_feed_zone_is_default;
	int32_t verifier_timeout;
	uint8_t verifier_timeout_is_default;
	uint8_t catalog_role;
	uint8_t catalog_role_is_default;
	const char* catalog_member_pattern;
	const char* catalog_producer_zone;
} ATTR_PACKED;

#define PATTERN_IMPLICIT_MARKER "_implicit_"

/*
 * Options for a zone
 */
struct zone_options {
	/* key is dname of apex */
	rbnode_type node;

	/* is apex of the zone */
	const char* name;
	/* if not part of config, the offset and linesize of zonelist entry */
	off_t off;
	int linesize;
	/* pattern for the zone options, if zone is part_of_config, this is
	 * a anonymous pattern created in-place */
	struct pattern_options* pattern;
	/* zone is fixed into the main config, not in zonelist, cannot delete */
	unsigned part_of_config        : 1;
	unsigned is_catalog_member_zone: 1;
} ATTR_PACKED;

/*
 * Options for catalog member zones
 * assert(options->is_catalog_member_zone == 1)
 * when options->pattern->catalog_producer_zone is set, this is a
 * producer member zone, otherwise a consumer member zone.
 * A catalog member zone is either a member zone of a catalog producer zone
 * or a catalog consumer zone. They are mutually exclusive.
 */
struct catalog_member_zone {
	struct zone_options          options;
	const struct dname*          member_id;
	/* node in the associated catalog consumer or producer zone */
	rbnode_type                  node;
} ATTR_PACKED;

typedef void (*new_member_id_type)(struct catalog_member_zone* zone);

union acl_addr_storage {
#ifdef INET6
	struct in_addr addr;
	struct in6_addr addr6;
#else
	struct in_addr addr;
#endif
};

/*
 * Access control list element
 */
struct acl_options {
	struct acl_options* next;

	/* options */
	time_t ixfr_disabled;
	int bad_xfr_count;
	uint8_t use_axfr_only;
	uint8_t allow_udp;

	/* ip address range */
	const char* ip_address_spec;
	uint8_t is_ipv6;
	unsigned int port;	/* is 0(no port) or suffix @port value */
	union acl_addr_storage addr;
	union acl_addr_storage range_mask;
	enum {
		acl_range_single = 0,	/* single address */
		acl_range_mask = 1,	/* 10.20.30.40&255.255.255.0 */
		acl_range_subnet = 2,	/* 10.20.30.40/28 */
		acl_range_minmax = 3	/* 10.20.30.40-10.20.30.60 (mask=max) */
	} rangetype;

	/* key */
	uint8_t nokey;
	uint8_t blocked;
	const char* key_name;
	struct key_options* key_options;

	/* tls_auth for XoT */
	const char* tls_auth_name;
	struct tls_auth_options* tls_auth_options;
} ATTR_PACKED;

/*
 * Key definition
 */
struct key_options {
	rbnode_type node; /* key of tree is name */
	char* name;
	char* algorithm;
	char* secret;
	struct tsig_key* tsig_key;
} ATTR_PACKED;

/*
 * TLS Auth definition for XoT
 */
struct tls_auth_options {
	rbnode_type node; /* key of tree is name */
	char* name;
	char* auth_domain_name;
	char* client_cert;
	char* client_key;
	char* client_key_pw;
};

/* proxy protocol port option list */
struct proxy_protocol_port_list {
	struct proxy_protocol_port_list* next;
	int port;
};

/** zone list free space */
struct zonelist_free {
	struct zonelist_free* next;
	off_t off;
};
/** zonelist free bucket for a particular line length */
struct zonelist_bucket {
	rbnode_type node; /* key is ptr to linesize */
	int linesize;
	struct zonelist_free* list;
};

/* default zonefile write interval if database is "", in seconds */
#define ZONEFILES_WRITE_INTERVAL 3600

struct zonestatname {
	rbnode_type node; /* key is malloced string with cooked zonestat name */
	unsigned id; /* index in nsd.zonestat array */
};

/*
 * Used during options parsing
 */
struct config_parser_state {
	char* filename;
	const char* chroot;
	int line;
	int errors;
	struct nsd_options* opt;
	struct pattern_options *pattern;
	struct zone_options *zone;
	struct key_options *key;
	struct tls_auth_options *tls_auth;
	struct ip_address_option *ip;
	void (*err)(void*,const char*);
	void* err_arg;
};

extern config_parser_state_type* cfg_parser;

/* region will be put in nsd_options struct. Returns empty options struct. */
struct nsd_options* nsd_options_create(region_type* region);
/* the number of zones that are configured */
static inline size_t nsd_options_num_zones(struct nsd_options* opt)
{ return opt->zone_options->count; }
/* insert a zone into the main options tree, returns 0 on error */
int nsd_options_insert_zone(struct nsd_options* opt, struct zone_options* zone);
/* insert a pattern into the main options tree, returns 0 on error */
int nsd_options_insert_pattern(struct nsd_options* opt,
	struct pattern_options* pat);

/* parses options file. Returns false on failure. callback, if nonNULL,
 * gets called with error strings, default prints. */
int parse_options_file(struct nsd_options* opt, const char* file,
	void (*err)(void*,const char*), void* err_arg,
	struct nsd_options* old_opts);
struct zone_options* zone_options_create(region_type* region);
void zone_options_delete(struct nsd_options* opt, struct zone_options* zone);
struct catalog_member_zone* catalog_member_zone_create(region_type* region);
static inline struct catalog_member_zone* as_catalog_member_zone(struct zone_options* zopt)
{ return zopt && zopt->is_catalog_member_zone ? (struct catalog_member_zone*)zopt : NULL; }
/* find a zone by apex domain name, or NULL if not found. */
struct zone_options* zone_options_find(struct nsd_options* opt,
	const struct dname* apex);
struct pattern_options* pattern_options_create(region_type* region);
struct pattern_options* pattern_options_find(struct nsd_options* opt, const char* name);
int pattern_options_equal(struct pattern_options* p, struct pattern_options* q);
void pattern_options_remove(struct nsd_options* opt, const char* name);
void pattern_options_add_modify(struct nsd_options* opt,
	struct pattern_options* p);
void pattern_options_marshal(struct buffer* buffer, struct pattern_options* p);
struct pattern_options* pattern_options_unmarshal(region_type* r,
	struct buffer* b);
struct key_options* key_options_create(region_type* region);
void key_options_insert(struct nsd_options* opt, struct key_options* key);
struct key_options* key_options_find(struct nsd_options* opt, const char* name);
void key_options_remove(struct nsd_options* opt, const char* name);
int key_options_equal(struct key_options* p, struct key_options* q);
void key_options_add_modify(struct nsd_options* opt, struct key_options* key);
void key_options_setup(region_type* region, struct key_options* key);
void key_options_desetup(region_type* region, struct key_options* key);
/* TLS auth */
struct tls_auth_options* tls_auth_options_create(region_type* region);
void tls_auth_options_insert(struct nsd_options* opt, struct tls_auth_options* auth);
struct tls_auth_options* tls_auth_options_find(struct nsd_options* opt, const char* name);
/* read in zone list file. Returns false on failure */
int parse_zone_list_file(struct nsd_options* opt);
/* create (potential) catalog producer member entry and add to the zonelist */
struct zone_options* zone_list_add_or_cat(struct nsd_options* opt,
	const char* zname, const char* pname, new_member_id_type new_member_id);
/* create zone entry and add to the zonelist file */
static inline struct zone_options* zone_list_add(struct nsd_options* opt,
	const char* zname, const char* pname)
{ return zone_list_add_or_cat(opt, zname, pname, NULL); }
/* create zonelist entry, do not insert in file (called by _add) */
struct zone_options* zone_list_zone_insert(struct nsd_options* opt,
	const char* nm, const char* patnm);
void zone_list_del(struct nsd_options* opt, struct zone_options* zone);
void zone_list_compact(struct nsd_options* opt);
void zone_list_close(struct nsd_options* opt);

/* create zonestat name tree , for initially created zones */
void options_zonestatnames_create(struct nsd_options* opt);
/* Get zonestat id for zone options, add new entry if necessary.
 * instantiates the pattern's zonestat string */
unsigned getzonestatid(struct nsd_options* opt, struct zone_options* zopt);
/* create string, same options as zonefile but no chroot changes */
const char* config_cook_string(struct zone_options* zone, const char* input);

/** check if config for remote control turns on IP-address interface
 * with certificates or a named pipe without certificates. */
int options_remote_is_address(struct nsd_options* cfg);

#if defined(HAVE_SSL)
/* tsig must be inited, adds all keys in options to tsig. */
void key_options_tsig_add(struct nsd_options* opt);
#endif

/* check acl list, acl number that matches if passed(0..),
 * or failure (-1) if dropped */
/* the reason why (the acl) is returned too (or NULL) */
int acl_check_incoming(struct acl_options* acl, struct query* q,
	struct acl_options** reason);
int acl_addr_matches_host(struct acl_options* acl, struct acl_options* host);
int acl_addr_matches(struct acl_options* acl, struct query* q);
int acl_addr_matches_proxy(struct acl_options* acl, struct query* q);
#ifdef HAVE_SSL
int acl_tls_hostname_matches(SSL* ssl, const char* acl_cert_cn);
#endif
int acl_key_matches(struct acl_options* acl, struct query* q);
int acl_addr_match_mask(uint32_t* a, uint32_t* b, uint32_t* mask, size_t sz);
int acl_addr_match_range_v6(uint32_t* minval, uint32_t* x, uint32_t* maxval, size_t sz);
int acl_addr_match_range_v4(uint32_t* minval, uint32_t* x, uint32_t* maxval, size_t sz);

/* check acl list for blocks on address, return 0 if none, -1 if blocked. */
int acl_check_incoming_block_proxy(struct acl_options* acl, struct query* q,
	struct acl_options** reason);

/* returns true if acls are both from the same host */
int acl_same_host(struct acl_options* a, struct acl_options* b);
/* find acl by number in the list */
struct acl_options* acl_find_num(struct acl_options* acl, int num);

/* see if two acl lists are the same (same elements in same order, or empty) */
int acl_list_equal(struct acl_options* p, struct acl_options* q);
/* see if two acl are the same */
int acl_equal(struct acl_options* p, struct acl_options* q);

/* see if a zone is a slave or a master zone */
int zone_is_slave(struct zone_options* opt);
/* see if a zone is a catalog consumer */
static inline int zone_is_catalog_consumer(struct zone_options* opt)
{ return opt && opt->pattern
             && opt->pattern->catalog_role == CATALOG_ROLE_CONSUMER; }
static inline int zone_is_catalog_producer(struct zone_options* opt)
{ return opt && opt->pattern
             && opt->pattern->catalog_role == CATALOG_ROLE_PRODUCER; }
static inline int zone_is_catalog_member(struct zone_options* opt)
{ return opt && opt->is_catalog_member_zone; }
static inline const char* zone_is_catalog_producer_member(struct zone_options* opt)
{ return opt && opt->pattern && opt->pattern->catalog_producer_zone
                              ? opt->pattern->catalog_producer_zone : NULL; }
static inline int zone_is_catalog_consumer_member(struct zone_options* opt)
{ return zone_is_catalog_member(opt) && !zone_is_catalog_producer_member(opt); }
/* create zonefile name, returns static pointer (perhaps to options data) */
const char* config_make_zonefile(struct zone_options* zone, struct nsd* nsd);

#define ZONEC_PCT_TIME 5 /* seconds, then it starts to print pcts */
#define ZONEC_PCT_COUNT 100000 /* elements before pct check is done */

/* parsing helpers */
void c_error(const char* msg, ...) ATTR_FORMAT(printf, 1,2);
int c_wrap(void);
struct acl_options* parse_acl_info(region_type* region, char* ip,
	const char* key);
/* true if ipv6 address, false if ipv4 */
int parse_acl_is_ipv6(const char* p);
/* returns range type. mask is the 2nd part of the range */
int parse_acl_range_type(char* ip, char** mask);
/* parses subnet mask, fills 0 mask as well */
void parse_acl_range_subnet(char* p, void* addr, int maxbits);
/* clean up options */
void nsd_options_destroy(struct nsd_options* opt);
/* replace occurrences of one with two in buf, pass length of buffer */
void replace_str(char* buf, size_t len, const char* one, const char* two);
/* apply pattern to the existing pattern in the parser */
void config_apply_pattern(struct pattern_options *dest, const char* name);
/* if the file is a directory, print a warning, because flex just exit()s
 * when a fileread fails because it is a directory, helps the user figure
 * out what just happened */
void warn_if_directory(const char* filetype, FILE* f, const char* fname);
/* resolve interface names in the options "ip-address:" (or "interface:")
 * and "control-interface:" into the ip-addresses associated with those
 * names. */
void resolve_interface_names(struct nsd_options* options);

/* See if the sockaddr port number is listed in the proxy protocol ports. */
int sockaddr_uses_proxy_protocol_port(struct nsd_options* options,
	struct sockaddr* addr);

#endif /* OPTIONS_H */
