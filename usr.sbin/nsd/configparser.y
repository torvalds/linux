/*
 * configparser.y -- yacc grammar for NSD configuration files
 *
 * Copyright (c) 2001-2019, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

%{
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "options.h"
#include "util.h"
#include "dname.h"
#include "tsig.h"
#include "rrl.h"

int yylex(void);

#ifdef __cplusplus
extern "C"
#endif

/* these need to be global, otherwise they cannot be used inside yacc */
extern config_parser_state_type *cfg_parser;

static void append_acl(struct acl_options **list, struct acl_options *acl);
static void add_to_last_acl(struct acl_options **list, char *ac);
static int parse_boolean(const char *str, int *bln);
static int parse_catalog_role(const char *str, int *role);
static int parse_expire_expr(const char *str, long long *num, uint8_t *expr);
static int parse_number(const char *str, long long *num);
static int parse_range(const char *str, long long *low, long long *high);

struct component {
	struct component *next;
	char *str;
};

%}

%union {
  char *str;
  long long llng;
  int bln;
  struct ip_address_option *ip;
  struct range_option *range;
  struct cpu_option *cpu;
  char **strv;
  struct component *comp;
  int role;
}

%token <str> STRING
%type <llng> number
%type <bln> boolean
%type <ip> ip_address
%type <llng> service_cpu_affinity
%type <cpu> cpus
%type <strv> command
%type <comp> arguments
%type <role> catalog_role

/* server */
%token VAR_SERVER
%token VAR_SERVER_COUNT
%token VAR_IP_ADDRESS
%token VAR_IP_TRANSPARENT
%token VAR_IP_FREEBIND
%token VAR_REUSEPORT
%token VAR_SEND_BUFFER_SIZE
%token VAR_RECEIVE_BUFFER_SIZE
%token VAR_DEBUG_MODE
%token VAR_IP4_ONLY
%token VAR_IP6_ONLY
%token VAR_DO_IP4
%token VAR_DO_IP6
%token VAR_PORT
%token VAR_USE_SYSTEMD
%token VAR_VERBOSITY
%token VAR_USERNAME
%token VAR_CHROOT
%token VAR_ZONESDIR
%token VAR_ZONELISTFILE
%token VAR_DATABASE
%token VAR_LOGFILE
%token VAR_LOG_ONLY_SYSLOG
%token VAR_PIDFILE
%token VAR_DIFFFILE
%token VAR_XFRDFILE
%token VAR_XFRDIR
%token VAR_HIDE_VERSION
%token VAR_HIDE_IDENTITY
%token VAR_VERSION
%token VAR_IDENTITY
%token VAR_NSID
%token VAR_TCP_COUNT
%token VAR_TCP_REJECT_OVERFLOW
%token VAR_TCP_QUERY_COUNT
%token VAR_TCP_TIMEOUT
%token VAR_TCP_MSS
%token VAR_OUTGOING_TCP_MSS
%token VAR_IPV4_EDNS_SIZE
%token VAR_IPV6_EDNS_SIZE
%token VAR_STATISTICS
%token VAR_XFRD_RELOAD_TIMEOUT
%token VAR_LOG_TIME_ASCII
%token VAR_LOG_TIME_ISO
%token VAR_ROUND_ROBIN
%token VAR_MINIMAL_RESPONSES
%token VAR_CONFINE_TO_ZONE
%token VAR_REFUSE_ANY
%token VAR_RELOAD_CONFIG
%token VAR_ZONEFILES_CHECK
%token VAR_ZONEFILES_WRITE
%token VAR_RRL_SIZE
%token VAR_RRL_RATELIMIT
%token VAR_RRL_SLIP
%token VAR_RRL_IPV4_PREFIX_LENGTH
%token VAR_RRL_IPV6_PREFIX_LENGTH
%token VAR_RRL_WHITELIST_RATELIMIT
%token VAR_TLS_SERVICE_KEY
%token VAR_TLS_SERVICE_PEM
%token VAR_TLS_SERVICE_OCSP
%token VAR_TLS_PORT
%token VAR_TLS_AUTH_PORT
%token VAR_TLS_AUTH_XFR_ONLY
%token VAR_TLS_CERT_BUNDLE
%token VAR_PROXY_PROTOCOL_PORT
%token VAR_CPU_AFFINITY
%token VAR_XFRD_CPU_AFFINITY
%token <llng> VAR_SERVER_CPU_AFFINITY
%token VAR_DROP_UPDATES
%token VAR_XFRD_TCP_MAX
%token VAR_XFRD_TCP_PIPELINE
%token VAR_METRICS_ENABLE
%token VAR_METRICS_INTERFACE
%token VAR_METRICS_PORT
%token VAR_METRICS_PATH

/* dnstap */
%token VAR_DNSTAP
%token VAR_DNSTAP_ENABLE
%token VAR_DNSTAP_SOCKET_PATH
%token VAR_DNSTAP_IP
%token VAR_DNSTAP_TLS
%token VAR_DNSTAP_TLS_SERVER_NAME
%token VAR_DNSTAP_TLS_CERT_BUNDLE
%token VAR_DNSTAP_TLS_CLIENT_KEY_FILE
%token VAR_DNSTAP_TLS_CLIENT_CERT_FILE
%token VAR_DNSTAP_SEND_IDENTITY
%token VAR_DNSTAP_SEND_VERSION
%token VAR_DNSTAP_IDENTITY
%token VAR_DNSTAP_VERSION
%token VAR_DNSTAP_LOG_AUTH_QUERY_MESSAGES
%token VAR_DNSTAP_LOG_AUTH_RESPONSE_MESSAGES

/* remote-control */
%token VAR_REMOTE_CONTROL
%token VAR_CONTROL_ENABLE
%token VAR_CONTROL_INTERFACE
%token VAR_CONTROL_PORT
%token VAR_SERVER_KEY_FILE
%token VAR_SERVER_CERT_FILE
%token VAR_CONTROL_KEY_FILE
%token VAR_CONTROL_CERT_FILE

/* key */
%token VAR_KEY
%token VAR_ALGORITHM
%token VAR_SECRET

/* xot auth */
%token VAR_TLS_AUTH
%token VAR_TLS_AUTH_DOMAIN_NAME
%token VAR_TLS_AUTH_CLIENT_CERT
%token VAR_TLS_AUTH_CLIENT_KEY
%token VAR_TLS_AUTH_CLIENT_KEY_PW

/* pattern */
%token VAR_PATTERN
%token VAR_NAME
%token VAR_ZONEFILE
%token VAR_NOTIFY
%token VAR_PROVIDE_XFR
%token VAR_ALLOW_QUERY
%token VAR_AXFR
%token VAR_UDP
%token VAR_NOTIFY_RETRY
%token VAR_ALLOW_NOTIFY
%token VAR_REQUEST_XFR
%token VAR_ALLOW_AXFR_FALLBACK
%token VAR_OUTGOING_INTERFACE
%token VAR_ANSWER_COOKIE
%token VAR_COOKIE_SECRET
%token VAR_COOKIE_SECRET_FILE
%token VAR_COOKIE_STAGING_SECRET
%token VAR_MAX_REFRESH_TIME
%token VAR_MIN_REFRESH_TIME
%token VAR_MAX_RETRY_TIME
%token VAR_MIN_RETRY_TIME
%token VAR_MIN_EXPIRE_TIME
%token VAR_MULTI_PRIMARY_CHECK
%token VAR_SIZE_LIMIT_XFR
%token VAR_ZONESTATS
%token VAR_INCLUDE_PATTERN
%token VAR_STORE_IXFR
%token VAR_IXFR_SIZE
%token VAR_IXFR_NUMBER
%token VAR_CREATE_IXFR
%token VAR_CATALOG
%token VAR_CATALOG_MEMBER_PATTERN
%token VAR_CATALOG_PRODUCER_ZONE
%token VAR_XDP_INTERFACE
%token VAR_XDP_PROGRAM_PATH
%token VAR_XDP_PROGRAM_LOAD
%token VAR_XDP_BPFFS_PATH
%token VAR_XDP_FORCE_COPY

/* zone */
%token VAR_ZONE
%token VAR_RRL_WHITELIST

/* socket options */
%token VAR_SERVERS
%token VAR_BINDTODEVICE
%token VAR_SETFIB

/* verify */
%token VAR_VERIFY
%token VAR_ENABLE
%token VAR_VERIFY_ZONE
%token VAR_VERIFY_ZONES
%token VAR_VERIFIER
%token VAR_VERIFIER_COUNT
%token VAR_VERIFIER_FEED_ZONE
%token VAR_VERIFIER_TIMEOUT

%%

blocks:
    /* may be empty */
  | blocks block ;

block:
    server
  | dnstap
  | remote_control
  | key
  | tls_auth
  | pattern
  | zone
  | verify ;

server:
    VAR_SERVER server_block ;

server_block:
    server_block server_option | ;

server_option:
    VAR_IP_ADDRESS ip_address
      {
        struct ip_address_option *ip = cfg_parser->opt->ip_addresses;

        if(ip == NULL) {
          cfg_parser->opt->ip_addresses = $2;
        } else {
          while(ip->next) { ip = ip->next; }
          ip->next = $2;
        }

        cfg_parser->ip = $2;
      }
    socket_options
    {
      cfg_parser->ip = NULL;
    }
  | VAR_SERVER_COUNT number
    {
      if ($2 > 0) {
        cfg_parser->opt->server_count = (int)$2;
      } else {
        yyerror("expected a number greater than zero");
      }
    }
  | VAR_IP_TRANSPARENT boolean
    { cfg_parser->opt->ip_transparent = $2; }
  | VAR_IP_FREEBIND boolean
    { cfg_parser->opt->ip_freebind = $2; }
  | VAR_SEND_BUFFER_SIZE number
    { cfg_parser->opt->send_buffer_size = (int)$2; }
  | VAR_RECEIVE_BUFFER_SIZE number
    { cfg_parser->opt->receive_buffer_size = (int)$2; }
  | VAR_DEBUG_MODE boolean
    { cfg_parser->opt->debug_mode = $2; }
  | VAR_USE_SYSTEMD boolean
    { /* ignored, obsolete */ }
  | VAR_HIDE_VERSION boolean
    { cfg_parser->opt->hide_version = $2; }
  | VAR_HIDE_IDENTITY boolean
    { cfg_parser->opt->hide_identity = $2; }
  | VAR_DROP_UPDATES boolean
    { cfg_parser->opt->drop_updates = $2; }
  | VAR_IP4_ONLY boolean
    { if($2) { cfg_parser->opt->do_ip4 = 1; cfg_parser->opt->do_ip6 = 0; } }
  | VAR_IP6_ONLY boolean
    { if($2) { cfg_parser->opt->do_ip4 = 0; cfg_parser->opt->do_ip6 = 1; } }
  | VAR_DO_IP4 boolean
    { cfg_parser->opt->do_ip4 = $2; }
  | VAR_DO_IP6 boolean
    { cfg_parser->opt->do_ip6 = $2; }
  | VAR_DATABASE STRING
    { /* ignored, obsolete */ }
  | VAR_IDENTITY STRING
    { cfg_parser->opt->identity = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_VERSION STRING
    { cfg_parser->opt->version = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_NSID STRING
    {
      unsigned char* nsid = 0;
      size_t nsid_len = strlen($2);

      if (strncasecmp($2, "ascii_", 6) == 0) {
        nsid_len -= 6; /* discard "ascii_" */
        if(nsid_len < 65535) {
          cfg_parser->opt->nsid = region_alloc(cfg_parser->opt->region, nsid_len*2+1);
          hex_ntop((uint8_t*)$2+6, nsid_len, (char*)cfg_parser->opt->nsid, nsid_len*2+1);
        } else {
          yyerror("NSID too long");
        }
      } else if (nsid_len % 2 != 0) {
        yyerror("the NSID must be a hex string of an even length.");
      } else {
        nsid_len = nsid_len / 2;
        if(nsid_len < 65535) {
          nsid = xalloc(nsid_len);
          if (hex_pton($2, nsid, nsid_len) == -1) {
            yyerror("hex string cannot be parsed in NSID.");
          } else {
            cfg_parser->opt->nsid = region_strdup(cfg_parser->opt->region, $2);
          }
          free(nsid);
        } else {
          yyerror("NSID too long");
        }
      }
    }
  | VAR_LOGFILE STRING
    { cfg_parser->opt->logfile = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_LOG_ONLY_SYSLOG boolean
    { cfg_parser->opt->log_only_syslog = $2; }
  | VAR_TCP_COUNT number
    {
      if ($2 > 0) {
        cfg_parser->opt->tcp_count = (int)$2;
      } else {
        yyerror("expected a number greater than zero");
      }
    }
  | VAR_TCP_REJECT_OVERFLOW boolean
    { cfg_parser->opt->tcp_reject_overflow = $2; }
  | VAR_TCP_QUERY_COUNT number
    { cfg_parser->opt->tcp_query_count = (int)$2; }
  | VAR_TCP_TIMEOUT number
    { cfg_parser->opt->tcp_timeout = (int)$2; }
  | VAR_TCP_MSS number
    { cfg_parser->opt->tcp_mss = (int)$2; }
  | VAR_OUTGOING_TCP_MSS number
    { cfg_parser->opt->outgoing_tcp_mss = (int)$2; }
  | VAR_IPV4_EDNS_SIZE number
    { cfg_parser->opt->ipv4_edns_size = (size_t)$2; }
  | VAR_IPV6_EDNS_SIZE number
    { cfg_parser->opt->ipv6_edns_size = (size_t)$2; }
  | VAR_PIDFILE STRING
    { cfg_parser->opt->pidfile = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_PORT number
    {
      /* port number, stored as a string */
      char buf[16];
      (void)snprintf(buf, sizeof(buf), "%lld", $2);
      cfg_parser->opt->port = region_strdup(cfg_parser->opt->region, buf);
    }
  | VAR_REUSEPORT boolean
    { cfg_parser->opt->reuseport = $2; }
  | VAR_STATISTICS number
    { cfg_parser->opt->statistics = (int)$2; }
  | VAR_CHROOT STRING
    { cfg_parser->opt->chroot = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_USERNAME STRING
    { cfg_parser->opt->username = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_ZONESDIR STRING
    { cfg_parser->opt->zonesdir = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_ZONELISTFILE STRING
    { cfg_parser->opt->zonelistfile = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_DIFFFILE STRING
    { /* ignored, obsolete */ }
  | VAR_XFRDFILE STRING
    { cfg_parser->opt->xfrdfile = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_XFRDIR STRING
    { cfg_parser->opt->xfrdir = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_XFRD_RELOAD_TIMEOUT number
    { cfg_parser->opt->xfrd_reload_timeout = (int)$2; }
  | VAR_VERBOSITY number
    { cfg_parser->opt->verbosity = (int)$2; }
  | VAR_RRL_SIZE number
    {
#ifdef RATELIMIT
      if ($2 > 0) {
        cfg_parser->opt->rrl_size = (size_t)$2;
      } else {
        yyerror("expected a number greater than zero");
      }
#endif
    }
  | VAR_RRL_RATELIMIT number
    {
#ifdef RATELIMIT
      cfg_parser->opt->rrl_ratelimit = (size_t)$2;
#endif
    }
  | VAR_RRL_SLIP number
    {
#ifdef RATELIMIT
      cfg_parser->opt->rrl_slip = (size_t)$2;
#endif
    }
  | VAR_RRL_IPV4_PREFIX_LENGTH number
    {
#ifdef RATELIMIT
      if ($2 > 32) {
        yyerror("invalid IPv4 prefix length");
      } else {
        cfg_parser->opt->rrl_ipv4_prefix_length = (size_t)$2;
      }
#endif
    }
  | VAR_RRL_IPV6_PREFIX_LENGTH number
    {
#ifdef RATELIMIT
      if ($2 > 64) {
        yyerror("invalid IPv6 prefix length");
      } else {
        cfg_parser->opt->rrl_ipv6_prefix_length = (size_t)$2;
      }
#endif
    }
  | VAR_RRL_WHITELIST_RATELIMIT number
    {
#ifdef RATELIMIT
      cfg_parser->opt->rrl_whitelist_ratelimit = (size_t)$2;
#endif
    }
  | VAR_RELOAD_CONFIG boolean
    { cfg_parser->opt->reload_config = $2; }
  | VAR_ZONEFILES_CHECK boolean
    { cfg_parser->opt->zonefiles_check = $2; }
  | VAR_ZONEFILES_WRITE number
    { cfg_parser->opt->zonefiles_write = (int)$2; }
  | VAR_LOG_TIME_ASCII boolean
    {
      cfg_parser->opt->log_time_ascii = $2;
      log_time_asc = cfg_parser->opt->log_time_ascii;
    }
  | VAR_LOG_TIME_ISO boolean
    {
      cfg_parser->opt->log_time_iso = $2;
      log_time_iso = cfg_parser->opt->log_time_iso;
    }
  | VAR_ROUND_ROBIN boolean
    {
      cfg_parser->opt->round_robin = $2;
      round_robin = cfg_parser->opt->round_robin;
    }
  | VAR_MINIMAL_RESPONSES boolean
    {
      cfg_parser->opt->minimal_responses = $2;
      minimal_responses = cfg_parser->opt->minimal_responses;
    }
  | VAR_CONFINE_TO_ZONE boolean
    { cfg_parser->opt->confine_to_zone = $2; }
  | VAR_REFUSE_ANY boolean
    { cfg_parser->opt->refuse_any = $2; }
  | VAR_TLS_SERVICE_KEY STRING
    { cfg_parser->opt->tls_service_key = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_TLS_SERVICE_OCSP STRING
    { cfg_parser->opt->tls_service_ocsp = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_TLS_SERVICE_PEM STRING
    { cfg_parser->opt->tls_service_pem = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_TLS_PORT number
    {
      /* port number, stored as string */
      char buf[16];
      (void)snprintf(buf, sizeof(buf), "%lld", $2);
      cfg_parser->opt->tls_port = region_strdup(cfg_parser->opt->region, buf);
    }
  | VAR_TLS_AUTH_PORT number
    {
      /* port number, stored as string */
      char buf[16];
      (void)snprintf(buf, sizeof(buf), "%lld", $2);
      cfg_parser->opt->tls_auth_port = region_strdup(cfg_parser->opt->region, buf);
    }
  | VAR_TLS_AUTH_XFR_ONLY boolean
    {
      if (!cfg_parser->opt->tls_auth_port) {
        yyerror("tls-auth-xfr-only set without or before tls-auth-port");
        YYABORT;
      }
      cfg_parser->opt->tls_auth_xfr_only = $2;
    }
  | VAR_TLS_CERT_BUNDLE STRING
    { cfg_parser->opt->tls_cert_bundle = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_PROXY_PROTOCOL_PORT number
    {
      struct proxy_protocol_port_list* elem = region_alloc_zero(
	cfg_parser->opt->region, sizeof(*elem));
      elem->port = $2;
      elem->next = cfg_parser->opt->proxy_protocol_port;
      cfg_parser->opt->proxy_protocol_port = elem;
    }
  | VAR_ANSWER_COOKIE boolean
    { cfg_parser->opt->answer_cookie = $2; }
  | VAR_COOKIE_SECRET STRING
    {
      uint8_t secret[32];
      ssize_t len = hex_pton($2, secret, NSD_COOKIE_SECRET_SIZE);

      if(len != NSD_COOKIE_SECRET_SIZE) {
        yyerror("expected a 128 bit hex string");
      } else {
        cfg_parser->opt->cookie_secret = region_strdup(cfg_parser->opt->region, $2);
      }
    }
  | VAR_COOKIE_STAGING_SECRET STRING
    {
      uint8_t secret[32];
      ssize_t len = hex_pton($2, secret, NSD_COOKIE_SECRET_SIZE);

      if(len != NSD_COOKIE_SECRET_SIZE) {
        yyerror("expected a 128 bit hex string");
      } else {
        cfg_parser->opt->cookie_staging_secret = region_strdup(cfg_parser->opt->region, $2);
      }
    }
  | VAR_COOKIE_SECRET_FILE STRING
    {
      /* Empty filename means explicitly disabled cookies from file, internally
       * represented as NULL.
       * Note that after parsing, if no value was configured, then
       * cookie_secret_file_is_default is still 1, then the default cookie
       * secret file value will be assigned to cookie_secret_file.
       */
      if(*$2) cfg_parser->opt->cookie_secret_file = region_strdup(cfg_parser->opt->region, $2);
      cfg_parser->opt->cookie_secret_file_is_default = 0;
    }
    
  | VAR_XFRD_TCP_MAX number
    { cfg_parser->opt->xfrd_tcp_max = (int)$2; }
  | VAR_XFRD_TCP_PIPELINE number
    { cfg_parser->opt->xfrd_tcp_pipeline = (int)$2; }
  | VAR_CPU_AFFINITY cpus
    {
      cfg_parser->opt->cpu_affinity = $2;
    }
  | service_cpu_affinity number
    {
      if($2 < 0) {
        yyerror("expected a non-negative number");
        YYABORT;
      } else {
        struct cpu_map_option *opt, *tail;

        opt = cfg_parser->opt->service_cpu_affinity;
        while(opt && opt->service != $1) { opt = opt->next; }

        if(opt) {
          opt->cpu = $2;
        } else {
          opt = region_alloc_zero(cfg_parser->opt->region, sizeof(*opt));
          opt->service = (int)$1;
          opt->cpu = (int)$2;

          tail = cfg_parser->opt->service_cpu_affinity;
          if(tail) {
            while(tail->next) { tail = tail->next; }
            tail->next = opt;
          } else {
            cfg_parser->opt->service_cpu_affinity = opt;
          }
        }
      }
    }
  | VAR_XDP_INTERFACE STRING
    {
#ifdef USE_XDP
      cfg_parser->opt->xdp_interface = region_strdup(cfg_parser->opt->region, $2);
#endif
    }
  | VAR_XDP_PROGRAM_PATH STRING
    {
#ifdef USE_XDP
      cfg_parser->opt->xdp_program_path = region_strdup(cfg_parser->opt->region, $2);
#endif
    }
  | VAR_XDP_PROGRAM_LOAD boolean
    {
#ifdef USE_XDP
      cfg_parser->opt->xdp_program_load = $2;
#endif
    }
  | VAR_XDP_BPFFS_PATH STRING
    {
#ifdef USE_XDP
      cfg_parser->opt->xdp_bpffs_path = region_strdup(cfg_parser->opt->region, $2);
#endif
	}
  | VAR_XDP_FORCE_COPY boolean
    {
#ifdef USE_XDP
      cfg_parser->opt->xdp_force_copy = $2;
#endif
    }
  | VAR_METRICS_ENABLE boolean
    {
#ifdef USE_METRICS
      cfg_parser->opt->metrics_enable = $2;
#endif /* USE_METRICS */
    }
  | VAR_METRICS_INTERFACE ip_address
    {
#ifdef USE_METRICS
      struct ip_address_option *ip = cfg_parser->opt->metrics_interface;
      if(ip == NULL) {
        cfg_parser->opt->metrics_interface = $2;
      } else {
        while(ip->next != NULL) { ip = ip->next; }
        ip->next = $2;
      }
#endif /* USE_METRICS */
    }
  | VAR_METRICS_PORT number
    {
#ifdef USE_METRICS
      if($2 == 0) {
        yyerror("metrics port number expected");
      } else {
        cfg_parser->opt->metrics_port = (int)$2;
      }
#endif /* USE_METRICS */
    }
  | VAR_METRICS_PATH STRING
    {
#ifdef USE_METRICS
      cfg_parser->opt->metrics_path = region_strdup(cfg_parser->opt->region, $2);
#endif /* USE_METRICS */
    }
  ;

socket_options:
  | socket_options socket_option ;

socket_option:
    VAR_SERVERS STRING
    {
      char *tok, *ptr, *str;
      struct range_option *servers = NULL;
      long long first, last;

      /* user may specify "0 1", "0" "1", 0 1 or a combination thereof */
      for(str = $2; (tok = strtok_r(str, " \t", &ptr)); str = NULL) {
        struct range_option *opt =
          region_alloc(cfg_parser->opt->region, sizeof(*opt));
        first = last = 0;
        if(!parse_range(tok, &first, &last)) {
          yyerror("invalid server range '%s'", tok);
          YYABORT;
        }
        assert(first >= 0);
        assert(last >= 0);
        opt->next = NULL;
        opt->first = (int)first;
        opt->last = (int)last;
        if(servers) {
          servers = servers->next = opt;
        } else {
          servers = cfg_parser->ip->servers = opt;
        }
      }
    }
  | VAR_BINDTODEVICE boolean
    { cfg_parser->ip->dev = $2; }
  | VAR_SETFIB number
    { cfg_parser->ip->fib = $2; }
  ;

cpus:
    { $$ = NULL; }
  | cpus STRING
    {
      char *tok, *ptr, *str;
      struct cpu_option *tail;
      long long cpu;

      str = $2;
      $$ = tail = $1;
      if(tail) {
        while(tail->next) { tail = tail->next; }
      }

      /* Users may specify "0 1", "0" "1", 0 1 or a combination thereof. */
      for(str = $2; (tok = strtok_r(str, " \t", &ptr)); str = NULL) {
        struct cpu_option *opt =
          region_alloc_zero(cfg_parser->opt->region, sizeof(*opt));
        cpu = 0;
        if(!parse_number(tok, &cpu) || cpu < 0) {
          yyerror("expected a positive number");
          YYABORT;
        }
        assert(cpu >=0);
        opt->cpu = (int)cpu;
        if(tail) {
          tail->next = opt;
          tail = opt;
        } else {
          $$ = tail = opt;
        }
      }
    }
  ;

service_cpu_affinity:
    VAR_XFRD_CPU_AFFINITY
    { $$ = -1; }
  | VAR_SERVER_CPU_AFFINITY
    {
      if($1 <= 0) {
        yyerror("invalid server identifier");
        YYABORT;
      }
      $$ = $1;
    }
  ;

dnstap:
    VAR_DNSTAP dnstap_block ;

dnstap_block:
    dnstap_block dnstap_option | ;

dnstap_option:
    VAR_DNSTAP_ENABLE boolean
    { cfg_parser->opt->dnstap_enable = $2; }
  | VAR_DNSTAP_SOCKET_PATH STRING
    { cfg_parser->opt->dnstap_socket_path = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_DNSTAP_IP STRING
    { cfg_parser->opt->dnstap_ip = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_DNSTAP_TLS boolean
    { cfg_parser->opt->dnstap_tls = $2; }
  | VAR_DNSTAP_TLS_SERVER_NAME STRING
    { cfg_parser->opt->dnstap_tls_server_name = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_DNSTAP_TLS_CERT_BUNDLE STRING
    { cfg_parser->opt->dnstap_tls_cert_bundle = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_DNSTAP_TLS_CLIENT_KEY_FILE STRING
    { cfg_parser->opt->dnstap_tls_client_key_file = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_DNSTAP_TLS_CLIENT_CERT_FILE STRING
    { cfg_parser->opt->dnstap_tls_client_cert_file = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_DNSTAP_SEND_IDENTITY boolean
    { cfg_parser->opt->dnstap_send_identity = $2; }
  | VAR_DNSTAP_SEND_VERSION boolean
    { cfg_parser->opt->dnstap_send_version = $2; }
  | VAR_DNSTAP_IDENTITY STRING
    { cfg_parser->opt->dnstap_identity = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_DNSTAP_VERSION STRING
    { cfg_parser->opt->dnstap_version = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_DNSTAP_LOG_AUTH_QUERY_MESSAGES boolean
    { cfg_parser->opt->dnstap_log_auth_query_messages = $2; }
  | VAR_DNSTAP_LOG_AUTH_RESPONSE_MESSAGES boolean
    { cfg_parser->opt->dnstap_log_auth_response_messages = $2; }
  ;

remote_control:
    VAR_REMOTE_CONTROL remote_control_block ;

remote_control_block:
    remote_control_block remote_control_option | ;

remote_control_option:
    VAR_CONTROL_ENABLE boolean
    { cfg_parser->opt->control_enable = $2; }
  | VAR_CONTROL_INTERFACE ip_address
    {
      struct ip_address_option *ip = cfg_parser->opt->control_interface;
      if(ip == NULL) {
        cfg_parser->opt->control_interface = $2;
      } else {
        while(ip->next != NULL) { ip = ip->next; }
        ip->next = $2;
      }
    }
  | VAR_CONTROL_PORT number
    {
      if($2 == 0) {
        yyerror("control port number expected");
      } else {
        cfg_parser->opt->control_port = (int)$2;
      }
    }
  | VAR_SERVER_KEY_FILE STRING
    { cfg_parser->opt->server_key_file = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_SERVER_CERT_FILE STRING
    { cfg_parser->opt->server_cert_file = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_CONTROL_KEY_FILE STRING
    { cfg_parser->opt->control_key_file = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_CONTROL_CERT_FILE STRING
    { cfg_parser->opt->control_cert_file = region_strdup(cfg_parser->opt->region, $2); }
  ;

tls_auth:
    VAR_TLS_AUTH
      {
        tls_auth_options_type *tls_auth = tls_auth_options_create(cfg_parser->opt->region);
        assert(cfg_parser->tls_auth == NULL);
        cfg_parser->tls_auth = tls_auth;
      }
      tls_auth_block
    {
      struct tls_auth_options *tls_auth = cfg_parser->tls_auth;
      if(tls_auth->name == NULL) {
        yyerror("tls-auth has no name");
      } else if(tls_auth->auth_domain_name == NULL) {
        yyerror("tls-auth %s has no auth-domain-name", tls_auth->name);
      } else if(tls_auth_options_find(cfg_parser->opt, tls_auth->name)) {
        yyerror("duplicate tls-auth %s", tls_auth->name);
      } else {
      	tls_auth_options_insert(cfg_parser->opt, tls_auth);
        cfg_parser->tls_auth = NULL;
      }
    } ;

tls_auth_block:
    tls_auth_block tls_auth_option | ;

tls_auth_option:
    VAR_NAME STRING
    {
      dname_type *dname;
      dname = (dname_type *)dname_parse(cfg_parser->opt->region, $2);
      cfg_parser->tls_auth->name = region_strdup(cfg_parser->opt->region, $2);
      if(dname == NULL) {
        yyerror("bad tls-auth name %s", $2);
      } else {
        region_recycle(cfg_parser->opt->region, dname, dname_total_size(dname));
      }
    }
  | VAR_TLS_AUTH_DOMAIN_NAME STRING
    {
      cfg_parser->tls_auth->auth_domain_name = region_strdup(cfg_parser->opt->region, $2);
    }
  | VAR_TLS_AUTH_CLIENT_CERT STRING
    {
	    cfg_parser->tls_auth->client_cert = region_strdup(cfg_parser->opt->region, $2);
    }
  | VAR_TLS_AUTH_CLIENT_KEY STRING
    {
	    cfg_parser->tls_auth->client_key = region_strdup(cfg_parser->opt->region, $2);
    }
  | VAR_TLS_AUTH_CLIENT_KEY_PW STRING
    {
	    cfg_parser->tls_auth->client_key_pw = region_strdup(cfg_parser->opt->region, $2);
    }
  ;

key:
    VAR_KEY
      {
        key_options_type *key = key_options_create(cfg_parser->opt->region);
        key->algorithm = region_strdup(cfg_parser->opt->region, "sha256");
        assert(cfg_parser->key == NULL);
        cfg_parser->key = key;
      }
      key_block
    {
      struct key_options *key = cfg_parser->key;
      if(key->name == NULL) {
        yyerror("tsig key has no name");
      } else if(key->algorithm == NULL) {
        yyerror("tsig key %s has no algorithm", key->name);
      } else if(key->secret == NULL) {
        yyerror("tsig key %s has no secret blob", key->name);
      } else if(key_options_find(cfg_parser->opt, key->name)) {
        yyerror("duplicate tsig key %s", key->name);
      } else {
        key_options_insert(cfg_parser->opt, key);
        cfg_parser->key = NULL;
      }
    } ;

key_block:
    key_block key_option | ;

key_option:
    VAR_NAME STRING
    {
      dname_type *dname;

      dname = (dname_type *)dname_parse(cfg_parser->opt->region, $2);
      cfg_parser->key->name = region_strdup(cfg_parser->opt->region, $2);
      if(dname == NULL) {
        yyerror("bad tsig key name %s", $2);
      } else {
        region_recycle(cfg_parser->opt->region, dname, dname_total_size(dname));
      }
    }
  | VAR_ALGORITHM STRING
    {
      if(tsig_get_algorithm_by_name($2) == NULL) {
        yyerror("bad tsig key algorithm %s", $2);
      } else {
        cfg_parser->key->algorithm = region_strdup(cfg_parser->opt->region, $2);
      }
    }
  | VAR_SECRET STRING
    {
      uint8_t data[16384];
      int size;

      cfg_parser->key->secret = region_strdup(cfg_parser->opt->region, $2);
      size = __b64_pton($2, data, sizeof(data));
      if(size == -1) {
        yyerror("cannot base64 decode tsig secret %s",
          cfg_parser->key->name?
          cfg_parser->key->name:"");
      } else if(size != 0) {
        memset(data, 0xdd, size); /* wipe secret */
      }
    } ;


zone:
    VAR_ZONE
      {
        assert(cfg_parser->pattern == NULL);
        assert(cfg_parser->zone == NULL);
        cfg_parser->zone = zone_options_create(cfg_parser->opt->region);
        cfg_parser->zone->part_of_config = 1;
        cfg_parser->zone->pattern = cfg_parser->pattern =
          pattern_options_create(cfg_parser->opt->region);
        cfg_parser->zone->pattern->implicit = 1;
      }
    zone_block
    {
      assert(cfg_parser->zone != NULL);
      if(cfg_parser->zone->name == NULL) {
        yyerror("zone has no name");
      } else if(!nsd_options_insert_zone(cfg_parser->opt, cfg_parser->zone)) {
        yyerror("duplicate zone %s", cfg_parser->zone->name);
      } else if(!nsd_options_insert_pattern(cfg_parser->opt, cfg_parser->zone->pattern)) {
        yyerror("duplicate pattern %s", cfg_parser->zone->pattern->pname);
      }
      cfg_parser->pattern = NULL;
      cfg_parser->zone = NULL;
    } ;

zone_block:
    zone_block zone_option | ;

zone_option:
    VAR_NAME STRING
    {
      const char *marker = PATTERN_IMPLICIT_MARKER;
      char *pname = region_alloc(cfg_parser->opt->region, strlen($2) + strlen(marker) + 1);
      memmove(pname, marker, strlen(marker));
      memmove(pname + strlen(marker), $2, strlen($2) + 1);
      cfg_parser->zone->pattern->pname = pname;
      cfg_parser->zone->name = region_strdup(cfg_parser->opt->region, $2);
      if(pattern_options_find(cfg_parser->opt, pname)) {
        yyerror("zone %s cannot be created because implicit pattern %s "
                    "already exists", $2, pname);
      }
    }
  | pattern_or_zone_option ;

pattern:
    VAR_PATTERN
      {
        assert(cfg_parser->pattern == NULL);
        cfg_parser->pattern = pattern_options_create(cfg_parser->opt->region);
      }
      pattern_block
    {
      pattern_options_type *pattern = cfg_parser->pattern;
      if(pattern->pname == NULL) {
        yyerror("pattern has no name");
      } else if(!nsd_options_insert_pattern(cfg_parser->opt, pattern)) {
        yyerror("duplicate pattern %s", pattern->pname);
      }
      cfg_parser->pattern = NULL;
    } ;

pattern_block:
    pattern_block pattern_option | ;

pattern_option:
    VAR_NAME STRING
    {
      if(strchr($2, ' ')) {
        yyerror("space is not allowed in pattern name: '%s'", $2);
      }
      cfg_parser->pattern->pname = region_strdup(cfg_parser->opt->region, $2);
    }
  | pattern_or_zone_option ;

pattern_or_zone_option:
    VAR_RRL_WHITELIST STRING
    {
#ifdef RATELIMIT
      cfg_parser->pattern->rrl_whitelist |= rrlstr2type($2);
#endif
    }
  | VAR_ZONEFILE STRING
    { cfg_parser->pattern->zonefile = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_ZONESTATS STRING
    { cfg_parser->pattern->zonestats = region_strdup(cfg_parser->opt->region, $2); }
  | VAR_SIZE_LIMIT_XFR number
    {
      if($2 > 0) {
        cfg_parser->pattern->size_limit_xfr = (int)$2;
      } else {
        yyerror("expected a number greater than zero");
      }
    }
  | VAR_MULTI_PRIMARY_CHECK boolean
    { cfg_parser->pattern->multi_primary_check = (int)$2; }
  | VAR_INCLUDE_PATTERN STRING
    { config_apply_pattern(cfg_parser->pattern, $2); }
  | VAR_REQUEST_XFR STRING STRING
    {
      acl_options_type *acl = parse_acl_info(cfg_parser->opt->region, $2, $3);
      if(cfg_parser->pattern->catalog_role == CATALOG_ROLE_PRODUCER)
        yyerror("catalog producer zones cannot be secondary zones");
      if(acl->blocked)
        yyerror("blocked address used for request-xfr");
      if(acl->rangetype != acl_range_single)
        yyerror("address range used for request-xfr");
      append_acl(&cfg_parser->pattern->request_xfr, acl);
    }
	request_xfr_tlsauth_option
	{ }
  | VAR_REQUEST_XFR VAR_AXFR STRING STRING
    {
      acl_options_type *acl = parse_acl_info(cfg_parser->opt->region, $3, $4);
      acl->use_axfr_only = 1;
      if(acl->blocked)
        yyerror("blocked address used for request-xfr");
      if(acl->rangetype != acl_range_single)
        yyerror("address range used for request-xfr");
      append_acl(&cfg_parser->pattern->request_xfr, acl);
    }
	request_xfr_tlsauth_option
	{ }
  | VAR_REQUEST_XFR VAR_UDP STRING STRING
    {
      acl_options_type *acl = parse_acl_info(cfg_parser->opt->region, $3, $4);
      acl->allow_udp = 1;
      if(acl->blocked)
        yyerror("blocked address used for request-xfr");
      if(acl->rangetype != acl_range_single)
        yyerror("address range used for request-xfr");
      append_acl(&cfg_parser->pattern->request_xfr, acl);
    }
  | VAR_ALLOW_NOTIFY STRING STRING
    {
      acl_options_type *acl = parse_acl_info(cfg_parser->opt->region, $2, $3);
      append_acl(&cfg_parser->pattern->allow_notify, acl);
    }
  | VAR_NOTIFY STRING STRING
    {
      acl_options_type *acl = parse_acl_info(cfg_parser->opt->region, $2, $3);
      if(acl->blocked)
        yyerror("blocked address used for notify");
      if(acl->rangetype != acl_range_single)
        yyerror("address range used for notify");
      append_acl(&cfg_parser->pattern->notify, acl);
    }
  | VAR_PROVIDE_XFR STRING STRING
    {
      acl_options_type *acl = parse_acl_info(cfg_parser->opt->region, $2, $3);
      append_acl(&cfg_parser->pattern->provide_xfr, acl);
    }
	provide_xfr_tlsauth_option
	{ }
  | VAR_ALLOW_QUERY STRING STRING
    {
      acl_options_type *acl = parse_acl_info(cfg_parser->opt->region, $2, $3);
      append_acl(&cfg_parser->pattern->allow_query, acl);
    }
  | VAR_OUTGOING_INTERFACE STRING
    {
      acl_options_type *acl = parse_acl_info(cfg_parser->opt->region, $2, "NOKEY");
      append_acl(&cfg_parser->pattern->outgoing_interface, acl);
    }
  | VAR_ALLOW_AXFR_FALLBACK boolean
    {
      cfg_parser->pattern->allow_axfr_fallback = $2;
      cfg_parser->pattern->allow_axfr_fallback_is_default = 0;
    }
  | VAR_NOTIFY_RETRY number
    {
      cfg_parser->pattern->notify_retry = $2;
      cfg_parser->pattern->notify_retry_is_default = 0;
    }
  | VAR_MAX_REFRESH_TIME number
    {
      cfg_parser->pattern->max_refresh_time = $2;
      cfg_parser->pattern->max_refresh_time_is_default = 0;
    }
  | VAR_MIN_REFRESH_TIME number
    {
      cfg_parser->pattern->min_refresh_time = $2;
      cfg_parser->pattern->min_refresh_time_is_default = 0;
    }
  | VAR_MAX_RETRY_TIME number
    {
      cfg_parser->pattern->max_retry_time = $2;
      cfg_parser->pattern->max_retry_time_is_default = 0;
    }
  | VAR_MIN_RETRY_TIME number
    {
      cfg_parser->pattern->min_retry_time = $2;
      cfg_parser->pattern->min_retry_time_is_default = 0;
    } 
  | VAR_MIN_EXPIRE_TIME STRING
    {
      long long num;
      uint8_t expr;

      if (!parse_expire_expr($2, &num, &expr)) {
        yyerror("expected an expire time in seconds or \"refresh+retry+1\"");
        YYABORT; /* trigger a parser error */
      }
      cfg_parser->pattern->min_expire_time = num;
      cfg_parser->pattern->min_expire_time_expr = expr;
    }
  | VAR_STORE_IXFR boolean
    {
      cfg_parser->pattern->store_ixfr = $2;
      cfg_parser->pattern->store_ixfr_is_default = 0;
    }
  | VAR_IXFR_SIZE number
    {
      cfg_parser->pattern->ixfr_size = $2;
      cfg_parser->pattern->ixfr_size_is_default = 0;
    }
  | VAR_IXFR_NUMBER number
    {
      cfg_parser->pattern->ixfr_number = $2;
      cfg_parser->pattern->ixfr_number_is_default = 0;
    }
  | VAR_CREATE_IXFR boolean
    {
      cfg_parser->pattern->create_ixfr = $2;
      cfg_parser->pattern->create_ixfr_is_default = 0;
    }
  | VAR_VERIFY_ZONE boolean
    { cfg_parser->pattern->verify_zone = $2; }
  | VAR_VERIFIER command
    { cfg_parser->pattern->verifier = $2; }
  | VAR_VERIFIER_FEED_ZONE boolean
    { cfg_parser->pattern->verifier_feed_zone = $2; }
  | VAR_VERIFIER_TIMEOUT number
    { cfg_parser->pattern->verifier_timeout = $2; } 
  | VAR_CATALOG catalog_role
    {
      if($2 == CATALOG_ROLE_PRODUCER && cfg_parser->pattern->request_xfr)
        yyerror("catalog producer zones cannot be secondary zones");
      cfg_parser->pattern->catalog_role = $2;
      cfg_parser->pattern->catalog_role_is_default = 0;
    }
  | VAR_CATALOG_MEMBER_PATTERN STRING 
    { 
      cfg_parser->pattern->catalog_member_pattern = region_strdup(cfg_parser->opt->region, $2); 
    }
  | VAR_CATALOG_PRODUCER_ZONE STRING 
    {
      dname_type *dname;

      if(cfg_parser->zone) {
        yyerror("catalog-producer-zone option is for patterns only and cannot "
                "be used in a zone clause");
      } else if(!(dname = (dname_type *)dname_parse(cfg_parser->opt->region, $2))) {
        yyerror("bad catalog producer name %s", $2);
      } else {
        region_recycle(cfg_parser->opt->region, dname, dname_total_size(dname));
        cfg_parser->pattern->catalog_producer_zone = region_strdup(cfg_parser->opt->region, $2); 
      }
    };

verify:
    VAR_VERIFY verify_block ;

verify_block:
    verify_block verify_option | ;

verify_option:
    VAR_ENABLE boolean
    { cfg_parser->opt->verify_enable = $2; }
  | VAR_IP_ADDRESS ip_address
    {
      struct ip_address_option *ip = cfg_parser->opt->verify_ip_addresses;
      if(!ip) {
        cfg_parser->opt->verify_ip_addresses = $2;
      } else {
        while(ip->next) { ip = ip->next; }
        ip->next = $2;
      }
    }
  | VAR_PORT number
    {
      /* port number, stored as a string */
      char buf[16];
      (void)snprintf(buf, sizeof(buf), "%lld", $2);
      cfg_parser->opt->verify_port = region_strdup(cfg_parser->opt->region, buf);
    }
  | VAR_VERIFY_ZONES boolean
    { cfg_parser->opt->verify_zones = $2; }
  | VAR_VERIFIER command
    { cfg_parser->opt->verifier = $2; }
  | VAR_VERIFIER_COUNT number
    { cfg_parser->opt->verifier_count = (int)$2; }
  | VAR_VERIFIER_TIMEOUT number
    { cfg_parser->opt->verifier_timeout = (int)$2; }
  | VAR_VERIFIER_FEED_ZONE boolean
    { cfg_parser->opt->verifier_feed_zone = $2; } ;

command:
    STRING arguments
    {
      char **argv;
      size_t argc = 1;
      for(struct component *i = $2; i; i = i->next) {
        argc++;
      }
      argv = region_alloc_zero(
        cfg_parser->opt->region, (argc + 1) * sizeof(char *));
      argc = 0;
      argv[argc++] = $1;
      for(struct component *j, *i = $2; i; i = j) {
        j = i->next;
        argv[argc++] = i->str;
        region_recycle(cfg_parser->opt->region, i, sizeof(*i));
      }
      $$ = argv;
    } ;

arguments:
    { $$ = NULL; }
  | arguments STRING
    {
      struct component *comp = region_alloc_zero(
        cfg_parser->opt->region, sizeof(*comp));
      comp->str = region_strdup(cfg_parser->opt->region, $2);
      if($1) {
        struct component *tail = $1;
        while(tail->next) {
         tail = tail->next;
        }
        tail->next = comp;
        $$ = $1;
      } else {
        $$ = comp;
      }
    } ;

ip_address:
    STRING
    {
      struct ip_address_option *ip = region_alloc_zero(
        cfg_parser->opt->region, sizeof(*ip));
      ip->address = region_strdup(cfg_parser->opt->region, $1);
      ip->fib = -1;
      $$ = ip;
    } ;

number:
    STRING
    {
      if(!parse_number($1, &$$)) {
        yyerror("expected a number");
        YYABORT; /* trigger a parser error */
      }
    } ;

boolean:
    STRING
    {
      if(!parse_boolean($1, &$$)) {
        yyerror("expected yes or no");
        YYABORT; /* trigger a parser error */
      }
    } ;

request_xfr_tlsauth_option:
	| STRING
	{ char *tls_auth_name = region_strdup(cfg_parser->opt->region, $1);
	  add_to_last_acl(&cfg_parser->pattern->request_xfr, tls_auth_name);} ;

provide_xfr_tlsauth_option:
	| STRING
	{ char *tls_auth_name = region_strdup(cfg_parser->opt->region, $1);
	  add_to_last_acl(&cfg_parser->pattern->provide_xfr, tls_auth_name);} ;

catalog_role:
    STRING
    {
      if(!parse_catalog_role($1, &$$)) {
        yyerror("expected consumer or producer");
        YYABORT; /* trigger a parser error */
      }
    } ;

%%

static void
append_acl(struct acl_options **list, struct acl_options *acl)
{
	assert(list != NULL);

	if(*list == NULL) {
		*list = acl;
	} else {
		struct acl_options *tail = *list;
		while(tail->next != NULL)
			tail = tail->next;
		tail->next = acl;
	}
}

static void
add_to_last_acl(struct acl_options **list, char *tls_auth_name)
{
	struct acl_options *tail = *list;
	assert(list != NULL);
	assert(*list != NULL);
	while(tail->next != NULL)
		tail = tail->next;
	tail->tls_auth_name = tls_auth_name;
}

static int
parse_boolean(const char *str, int *bln)
{
	if(strcmp(str, "yes") == 0) {
		*bln = 1;
	} else if(strcmp(str, "no") == 0) {
		*bln = 0;
	} else {
		return 0;
	}

	return 1;
}

static int
parse_expire_expr(const char *str, long long *num, uint8_t *expr)
{
	if(parse_number(str, num)) {
		*expr = EXPIRE_TIME_HAS_VALUE;
		return 1;
	}
	if(strcmp(str, REFRESHPLUSRETRYPLUS1_STR) == 0) {
		*num = 0;
		*expr = REFRESHPLUSRETRYPLUS1;
		return 1;
	}
	return 0;
}

static int
parse_number(const char *str, long long *num)
{
	/* ensure string consists entirely of digits */
	size_t pos = 0;
	while(str[pos] >= '0' && str[pos] <= '9') {
		pos++;
	}

	if(pos != 0 && str[pos] == '\0') {
		*num = strtoll(str, NULL, 10);
		return 1;
	}

	return 0;
}

static int
parse_range(const char *str, long long *low, long long *high)
{
	const char *ptr = str;
	long long num[2];

	/* require range to begin with a number */
	if(*ptr < '0' || *ptr > '9') {
		return 0;
	}

	num[0] = strtoll(ptr, (char **)&ptr, 10);

	/* require number to be followed by nothing at all or a dash */
	if(*ptr == '\0') {
		*low = num[0];
		*high = num[0];
		return 1;
	} else if(*ptr != '-') {
		return 0;
	}

	++ptr;
	/* require dash to be followed by a number */
	if(*ptr < '0' || *ptr > '9') {
		return 0;
	}

	num[1] = strtoll(ptr, (char **)&ptr, 10);

	/* require number to be followed by nothing at all */
	if(*ptr == '\0') {
		if(num[0] < num[1]) {
			*low = num[0];
			*high = num[1];
		} else {
			*low = num[1];
			*high = num[0];
		}
		return 1;
	}

	return 0;
}

static int
parse_catalog_role(const char *str, int *role)
{
	if(strcasecmp(str, "consumer") == 0) {
		*role = CATALOG_ROLE_CONSUMER;
	} else if(strcmp(str, "producer") == 0) {
		*role = CATALOG_ROLE_PRODUCER;
	} else {
		return 0;
	}
	return 1;
}


