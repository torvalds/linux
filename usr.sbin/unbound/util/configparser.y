/*
 * configparser.y -- yacc grammar for unbound configuration files
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

%{
#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "util/configyyrename.h"
#include "util/config_file.h"
#include "util/net_help.h"
#include "sldns/str2wire.h"

int ub_c_lex(void);
void ub_c_error(const char *message);

static void validate_respip_action(const char* action);
static void validate_acl_action(const char* action);

/* these need to be global, otherwise they cannot be used inside yacc */
extern struct config_parser_state* cfg_parser;

#if 0
#define OUTYY(s)  printf s /* used ONLY when debugging */
#else
#define OUTYY(s)
#endif

%}
%union {
	char*	str;
};

%token SPACE LETTER NEWLINE COMMENT COLON ANY ZONESTR
%token <str> STRING_ARG
%token VAR_FORCE_TOPLEVEL
%token VAR_SERVER VAR_VERBOSITY VAR_NUM_THREADS VAR_PORT
%token VAR_OUTGOING_RANGE VAR_INTERFACE VAR_PREFER_IP4
%token VAR_DO_IP4 VAR_DO_IP6 VAR_DO_NAT64 VAR_PREFER_IP6 VAR_DO_UDP VAR_DO_TCP
%token VAR_TCP_MSS VAR_OUTGOING_TCP_MSS VAR_TCP_IDLE_TIMEOUT
%token VAR_EDNS_TCP_KEEPALIVE VAR_EDNS_TCP_KEEPALIVE_TIMEOUT
%token VAR_SOCK_QUEUE_TIMEOUT
%token VAR_CHROOT VAR_USERNAME VAR_DIRECTORY VAR_LOGFILE VAR_PIDFILE
%token VAR_MSG_CACHE_SIZE VAR_MSG_CACHE_SLABS VAR_NUM_QUERIES_PER_THREAD
%token VAR_RRSET_CACHE_SIZE VAR_RRSET_CACHE_SLABS VAR_OUTGOING_NUM_TCP
%token VAR_INFRA_HOST_TTL VAR_INFRA_LAME_TTL VAR_INFRA_CACHE_SLABS
%token VAR_INFRA_CACHE_NUMHOSTS VAR_INFRA_CACHE_LAME_SIZE VAR_NAME
%token VAR_STUB_ZONE VAR_STUB_HOST VAR_STUB_ADDR VAR_TARGET_FETCH_POLICY
%token VAR_HARDEN_SHORT_BUFSIZE VAR_HARDEN_LARGE_QUERIES
%token VAR_FORWARD_ZONE VAR_FORWARD_HOST VAR_FORWARD_ADDR
%token VAR_DO_NOT_QUERY_ADDRESS VAR_HIDE_IDENTITY VAR_HIDE_VERSION
%token VAR_IDENTITY VAR_VERSION VAR_HARDEN_GLUE VAR_MODULE_CONF
%token VAR_TRUST_ANCHOR_FILE VAR_TRUST_ANCHOR VAR_VAL_OVERRIDE_DATE
%token VAR_BOGUS_TTL VAR_VAL_CLEAN_ADDITIONAL VAR_VAL_PERMISSIVE_MODE
%token VAR_INCOMING_NUM_TCP VAR_MSG_BUFFER_SIZE VAR_KEY_CACHE_SIZE
%token VAR_KEY_CACHE_SLABS VAR_TRUSTED_KEYS_FILE
%token VAR_VAL_NSEC3_KEYSIZE_ITERATIONS VAR_USE_SYSLOG
%token VAR_OUTGOING_INTERFACE VAR_ROOT_HINTS VAR_DO_NOT_QUERY_LOCALHOST
%token VAR_CACHE_MAX_TTL VAR_HARDEN_DNSSEC_STRIPPED VAR_ACCESS_CONTROL
%token VAR_LOCAL_ZONE VAR_LOCAL_DATA VAR_INTERFACE_AUTOMATIC
%token VAR_STATISTICS_INTERVAL VAR_DO_DAEMONIZE VAR_USE_CAPS_FOR_ID
%token VAR_STATISTICS_CUMULATIVE VAR_OUTGOING_PORT_PERMIT
%token VAR_OUTGOING_PORT_AVOID VAR_DLV_ANCHOR_FILE VAR_DLV_ANCHOR
%token VAR_NEG_CACHE_SIZE VAR_HARDEN_REFERRAL_PATH VAR_PRIVATE_ADDRESS
%token VAR_PRIVATE_DOMAIN VAR_REMOTE_CONTROL VAR_CONTROL_ENABLE
%token VAR_CONTROL_INTERFACE VAR_CONTROL_PORT VAR_SERVER_KEY_FILE
%token VAR_SERVER_CERT_FILE VAR_CONTROL_KEY_FILE VAR_CONTROL_CERT_FILE
%token VAR_CONTROL_USE_CERT VAR_TCP_REUSE_TIMEOUT VAR_MAX_REUSE_TCP_QUERIES
%token VAR_EXTENDED_STATISTICS VAR_LOCAL_DATA_PTR VAR_JOSTLE_TIMEOUT
%token VAR_STUB_PRIME VAR_UNWANTED_REPLY_THRESHOLD VAR_LOG_TIME_ASCII
%token VAR_DOMAIN_INSECURE VAR_PYTHON VAR_PYTHON_SCRIPT VAR_VAL_SIG_SKEW_MIN
%token VAR_VAL_SIG_SKEW_MAX VAR_VAL_MAX_RESTART VAR_CACHE_MIN_TTL
%token VAR_VAL_LOG_LEVEL VAR_AUTO_TRUST_ANCHOR_FILE VAR_KEEP_MISSING
%token VAR_ADD_HOLDDOWN VAR_DEL_HOLDDOWN VAR_SO_RCVBUF VAR_EDNS_BUFFER_SIZE
%token VAR_PREFETCH VAR_PREFETCH_KEY VAR_SO_SNDBUF VAR_SO_REUSEPORT
%token VAR_HARDEN_BELOW_NXDOMAIN VAR_IGNORE_CD_FLAG VAR_LOG_QUERIES
%token VAR_LOG_REPLIES VAR_LOG_LOCAL_ACTIONS VAR_TCP_UPSTREAM
%token VAR_SSL_UPSTREAM VAR_TCP_AUTH_QUERY_TIMEOUT VAR_SSL_SERVICE_KEY
%token VAR_SSL_SERVICE_PEM VAR_SSL_PORT VAR_FORWARD_FIRST
%token VAR_STUB_SSL_UPSTREAM VAR_FORWARD_SSL_UPSTREAM VAR_TLS_CERT_BUNDLE
%token VAR_STUB_TCP_UPSTREAM VAR_FORWARD_TCP_UPSTREAM
%token VAR_HTTPS_PORT VAR_HTTP_ENDPOINT VAR_HTTP_MAX_STREAMS
%token VAR_HTTP_QUERY_BUFFER_SIZE VAR_HTTP_RESPONSE_BUFFER_SIZE
%token VAR_HTTP_NODELAY VAR_HTTP_NOTLS_DOWNSTREAM
%token VAR_STUB_FIRST VAR_MINIMAL_RESPONSES VAR_RRSET_ROUNDROBIN
%token VAR_MAX_UDP_SIZE VAR_DELAY_CLOSE VAR_UDP_CONNECT
%token VAR_UNBLOCK_LAN_ZONES VAR_INSECURE_LAN_ZONES
%token VAR_INFRA_CACHE_MIN_RTT VAR_INFRA_CACHE_MAX_RTT VAR_INFRA_KEEP_PROBING
%token VAR_DNS64_PREFIX VAR_DNS64_SYNTHALL VAR_DNS64_IGNORE_AAAA
%token VAR_NAT64_PREFIX
%token VAR_DNSTAP VAR_DNSTAP_ENABLE VAR_DNSTAP_SOCKET_PATH VAR_DNSTAP_IP
%token VAR_DNSTAP_TLS VAR_DNSTAP_TLS_SERVER_NAME VAR_DNSTAP_TLS_CERT_BUNDLE
%token VAR_DNSTAP_TLS_CLIENT_KEY_FILE VAR_DNSTAP_TLS_CLIENT_CERT_FILE
%token VAR_DNSTAP_SEND_IDENTITY VAR_DNSTAP_SEND_VERSION VAR_DNSTAP_BIDIRECTIONAL
%token VAR_DNSTAP_IDENTITY VAR_DNSTAP_VERSION
%token VAR_DNSTAP_LOG_RESOLVER_QUERY_MESSAGES
%token VAR_DNSTAP_LOG_RESOLVER_RESPONSE_MESSAGES
%token VAR_DNSTAP_LOG_CLIENT_QUERY_MESSAGES
%token VAR_DNSTAP_LOG_CLIENT_RESPONSE_MESSAGES
%token VAR_DNSTAP_LOG_FORWARDER_QUERY_MESSAGES
%token VAR_DNSTAP_LOG_FORWARDER_RESPONSE_MESSAGES
%token VAR_DNSTAP_SAMPLE_RATE
%token VAR_RESPONSE_IP_TAG VAR_RESPONSE_IP VAR_RESPONSE_IP_DATA
%token VAR_HARDEN_ALGO_DOWNGRADE VAR_IP_TRANSPARENT
%token VAR_IP_DSCP
%token VAR_DISABLE_DNSSEC_LAME_CHECK
%token VAR_IP_RATELIMIT VAR_IP_RATELIMIT_SLABS VAR_IP_RATELIMIT_SIZE
%token VAR_RATELIMIT VAR_RATELIMIT_SLABS VAR_RATELIMIT_SIZE
%token VAR_OUTBOUND_MSG_RETRY VAR_MAX_SENT_COUNT VAR_MAX_QUERY_RESTARTS
%token VAR_RATELIMIT_FOR_DOMAIN VAR_RATELIMIT_BELOW_DOMAIN
%token VAR_IP_RATELIMIT_FACTOR VAR_RATELIMIT_FACTOR
%token VAR_IP_RATELIMIT_BACKOFF VAR_RATELIMIT_BACKOFF
%token VAR_SEND_CLIENT_SUBNET VAR_CLIENT_SUBNET_ZONE
%token VAR_CLIENT_SUBNET_ALWAYS_FORWARD VAR_CLIENT_SUBNET_OPCODE
%token VAR_MAX_CLIENT_SUBNET_IPV4 VAR_MAX_CLIENT_SUBNET_IPV6
%token VAR_MIN_CLIENT_SUBNET_IPV4 VAR_MIN_CLIENT_SUBNET_IPV6
%token VAR_MAX_ECS_TREE_SIZE_IPV4 VAR_MAX_ECS_TREE_SIZE_IPV6
%token VAR_CAPS_WHITELIST VAR_CACHE_MAX_NEGATIVE_TTL VAR_PERMIT_SMALL_HOLDDOWN
%token VAR_CACHE_MIN_NEGATIVE_TTL
%token VAR_QNAME_MINIMISATION VAR_QNAME_MINIMISATION_STRICT VAR_IP_FREEBIND
%token VAR_DEFINE_TAG VAR_LOCAL_ZONE_TAG VAR_ACCESS_CONTROL_TAG
%token VAR_LOCAL_ZONE_OVERRIDE VAR_ACCESS_CONTROL_TAG_ACTION
%token VAR_ACCESS_CONTROL_TAG_DATA VAR_VIEW VAR_ACCESS_CONTROL_VIEW
%token VAR_VIEW_FIRST VAR_SERVE_EXPIRED VAR_SERVE_EXPIRED_TTL
%token VAR_SERVE_EXPIRED_TTL_RESET VAR_SERVE_EXPIRED_REPLY_TTL
%token VAR_SERVE_EXPIRED_CLIENT_TIMEOUT VAR_EDE_SERVE_EXPIRED
%token VAR_SERVE_ORIGINAL_TTL VAR_FAKE_DSA
%token VAR_FAKE_SHA1 VAR_LOG_IDENTITY VAR_HIDE_TRUSTANCHOR
%token VAR_HIDE_HTTP_USER_AGENT VAR_HTTP_USER_AGENT
%token VAR_TRUST_ANCHOR_SIGNALING VAR_AGGRESSIVE_NSEC VAR_USE_SYSTEMD
%token VAR_SHM_ENABLE VAR_SHM_KEY VAR_ROOT_KEY_SENTINEL
%token VAR_DNSCRYPT VAR_DNSCRYPT_ENABLE VAR_DNSCRYPT_PORT VAR_DNSCRYPT_PROVIDER
%token VAR_DNSCRYPT_SECRET_KEY VAR_DNSCRYPT_PROVIDER_CERT
%token VAR_DNSCRYPT_PROVIDER_CERT_ROTATED
%token VAR_DNSCRYPT_SHARED_SECRET_CACHE_SIZE
%token VAR_DNSCRYPT_SHARED_SECRET_CACHE_SLABS
%token VAR_DNSCRYPT_NONCE_CACHE_SIZE
%token VAR_DNSCRYPT_NONCE_CACHE_SLABS
%token VAR_PAD_RESPONSES VAR_PAD_RESPONSES_BLOCK_SIZE
%token VAR_PAD_QUERIES VAR_PAD_QUERIES_BLOCK_SIZE
%token VAR_IPSECMOD_ENABLED VAR_IPSECMOD_HOOK VAR_IPSECMOD_IGNORE_BOGUS
%token VAR_IPSECMOD_MAX_TTL VAR_IPSECMOD_WHITELIST VAR_IPSECMOD_STRICT
%token VAR_CACHEDB VAR_CACHEDB_BACKEND VAR_CACHEDB_SECRETSEED
%token VAR_CACHEDB_REDISHOST VAR_CACHEDB_REDISREPLICAHOST
%token VAR_CACHEDB_REDISPORT VAR_CACHEDB_REDISREPLICAPORT
%token VAR_CACHEDB_REDISTIMEOUT VAR_CACHEDB_REDISREPLICATIMEOUT
%token VAR_CACHEDB_REDISEXPIRERECORDS
%token VAR_CACHEDB_REDISPATH VAR_CACHEDB_REDISREPLICAPATH
%token VAR_CACHEDB_REDISPASSWORD VAR_CACHEDB_REDISREPLICAPASSWORD
%token VAR_CACHEDB_REDISLOGICALDB VAR_CACHEDB_REDISREPLICALOGICALDB
%token VAR_CACHEDB_REDISCOMMANDTIMEOUT VAR_CACHEDB_REDISREPLICACOMMANDTIMEOUT
%token VAR_CACHEDB_REDISCONNECTTIMEOUT VAR_CACHEDB_REDISREPLICACONNECTTIMEOUT
%token VAR_UDP_UPSTREAM_WITHOUT_DOWNSTREAM VAR_FOR_UPSTREAM
%token VAR_AUTH_ZONE VAR_ZONEFILE VAR_MASTER VAR_URL VAR_FOR_DOWNSTREAM
%token VAR_FALLBACK_ENABLED VAR_TLS_ADDITIONAL_PORT VAR_LOW_RTT VAR_LOW_RTT_PERMIL
%token VAR_FAST_SERVER_PERMIL VAR_FAST_SERVER_NUM
%token VAR_ALLOW_NOTIFY VAR_TLS_WIN_CERT VAR_TCP_CONNECTION_LIMIT
%token VAR_ANSWER_COOKIE VAR_COOKIE_SECRET VAR_IP_RATELIMIT_COOKIE
%token VAR_FORWARD_NO_CACHE VAR_STUB_NO_CACHE VAR_LOG_SERVFAIL VAR_DENY_ANY
%token VAR_UNKNOWN_SERVER_TIME_LIMIT VAR_LOG_TAG_QUERYREPLY
%token VAR_DISCARD_TIMEOUT VAR_WAIT_LIMIT VAR_WAIT_LIMIT_COOKIE
%token VAR_WAIT_LIMIT_NETBLOCK VAR_WAIT_LIMIT_COOKIE_NETBLOCK
%token VAR_STREAM_WAIT_SIZE VAR_TLS_CIPHERS VAR_TLS_CIPHERSUITES VAR_TLS_USE_SNI
%token VAR_IPSET VAR_IPSET_NAME_V4 VAR_IPSET_NAME_V6
%token VAR_TLS_SESSION_TICKET_KEYS VAR_RPZ VAR_TAGS VAR_RPZ_ACTION_OVERRIDE
%token VAR_RPZ_CNAME_OVERRIDE VAR_RPZ_LOG VAR_RPZ_LOG_NAME
%token VAR_DYNLIB VAR_DYNLIB_FILE VAR_EDNS_CLIENT_STRING
%token VAR_EDNS_CLIENT_STRING_OPCODE VAR_NSID
%token VAR_ZONEMD_PERMISSIVE_MODE VAR_ZONEMD_CHECK VAR_ZONEMD_REJECT_ABSENCE
%token VAR_RPZ_SIGNAL_NXDOMAIN_RA VAR_INTERFACE_AUTOMATIC_PORTS VAR_EDE
%token VAR_DNS_ERROR_REPORTING
%token VAR_INTERFACE_ACTION VAR_INTERFACE_VIEW VAR_INTERFACE_TAG
%token VAR_INTERFACE_TAG_ACTION VAR_INTERFACE_TAG_DATA
%token VAR_QUIC_PORT VAR_QUIC_SIZE
%token VAR_PROXY_PROTOCOL_PORT VAR_STATISTICS_INHIBIT_ZERO
%token VAR_HARDEN_UNKNOWN_ADDITIONAL VAR_DISABLE_EDNS_DO VAR_CACHEDB_NO_STORE
%token VAR_LOG_DESTADDR VAR_CACHEDB_CHECK_WHEN_SERVE_EXPIRED
%token VAR_COOKIE_SECRET_FILE VAR_ITER_SCRUB_NS VAR_ITER_SCRUB_CNAME
%token VAR_MAX_GLOBAL_QUOTA VAR_HARDEN_UNVERIFIED_GLUE VAR_LOG_TIME_ISO

%%
toplevelvars: /* empty */ | toplevelvars toplevelvar ;
toplevelvar: serverstart contents_server | stub_clause |
	forward_clause | pythonstart contents_py |
	rcstart contents_rc | dtstart contents_dt | view_clause |
	dnscstart contents_dnsc | cachedbstart contents_cachedb |
	ipsetstart contents_ipset | authstart contents_auth |
	rpzstart contents_rpz | dynlibstart contents_dl |
	force_toplevel
	;
force_toplevel: VAR_FORCE_TOPLEVEL
	{
		OUTYY(("\nP(force-toplevel)\n"));
		cfg_parser->started_toplevel = 0;
	}
	;
/* server: declaration */
serverstart: VAR_SERVER
	{
		OUTYY(("\nP(server:)\n"));
		cfg_parser->started_toplevel = 1;
	}
	;
contents_server: contents_server content_server
	| ;
content_server: server_num_threads | server_verbosity | server_port |
	server_outgoing_range | server_do_ip4 |
	server_do_ip6 | server_do_nat64 | server_prefer_ip4 |
	server_prefer_ip6 | server_do_udp | server_do_tcp |
	server_tcp_mss | server_outgoing_tcp_mss | server_tcp_idle_timeout |
	server_tcp_keepalive | server_tcp_keepalive_timeout |
	server_sock_queue_timeout |
	server_interface | server_chroot | server_username |
	server_directory | server_logfile | server_pidfile |
	server_msg_cache_size | server_msg_cache_slabs |
	server_num_queries_per_thread | server_rrset_cache_size |
	server_rrset_cache_slabs | server_outgoing_num_tcp |
	server_infra_host_ttl | server_infra_lame_ttl |
	server_infra_cache_slabs | server_infra_cache_numhosts |
	server_infra_cache_lame_size | server_target_fetch_policy |
	server_harden_short_bufsize | server_harden_large_queries |
	server_do_not_query_address | server_hide_identity |
	server_hide_version | server_identity | server_version |
	server_hide_http_user_agent | server_http_user_agent |
	server_harden_glue | server_module_conf | server_trust_anchor_file |
	server_trust_anchor | server_val_override_date | server_bogus_ttl |
	server_val_clean_additional | server_val_permissive_mode |
	server_incoming_num_tcp | server_msg_buffer_size |
	server_key_cache_size | server_key_cache_slabs |
	server_trusted_keys_file | server_val_nsec3_keysize_iterations |
	server_use_syslog | server_outgoing_interface | server_root_hints |
	server_do_not_query_localhost | server_cache_max_ttl |
	server_harden_dnssec_stripped | server_access_control |
	server_local_zone | server_local_data | server_interface_automatic |
	server_statistics_interval | server_do_daemonize |
	server_use_caps_for_id | server_statistics_cumulative |
	server_outgoing_port_permit | server_outgoing_port_avoid |
	server_dlv_anchor_file | server_dlv_anchor | server_neg_cache_size |
	server_harden_referral_path | server_private_address |
	server_private_domain | server_extended_statistics |
	server_local_data_ptr | server_jostle_timeout |
	server_unwanted_reply_threshold | server_log_time_ascii |
	server_domain_insecure | server_val_sig_skew_min |
	server_val_sig_skew_max | server_val_max_restart |
	server_cache_min_ttl | server_val_log_level |
	server_auto_trust_anchor_file |	server_add_holddown |
	server_del_holddown | server_keep_missing | server_so_rcvbuf |
	server_edns_buffer_size | server_prefetch | server_prefetch_key |
	server_so_sndbuf | server_harden_below_nxdomain | server_ignore_cd_flag |
	server_log_queries | server_log_replies | server_tcp_upstream | server_ssl_upstream |
	server_log_local_actions |
	server_ssl_service_key | server_ssl_service_pem | server_ssl_port |
	server_https_port | server_http_endpoint | server_http_max_streams |
	server_http_query_buffer_size | server_http_response_buffer_size |
	server_http_nodelay | server_http_notls_downstream |
	server_minimal_responses | server_rrset_roundrobin | server_max_udp_size |
	server_so_reuseport | server_delay_close | server_udp_connect |
	server_unblock_lan_zones | server_insecure_lan_zones |
	server_dns64_prefix | server_dns64_synthall | server_dns64_ignore_aaaa |
	server_nat64_prefix |
	server_infra_cache_min_rtt | server_infra_cache_max_rtt | server_harden_algo_downgrade |
	server_ip_transparent | server_ip_ratelimit | server_ratelimit |
	server_ip_dscp | server_infra_keep_probing |
	server_ip_ratelimit_slabs | server_ratelimit_slabs |
	server_ip_ratelimit_size | server_ratelimit_size |
	server_ratelimit_for_domain |
	server_ratelimit_below_domain | server_ratelimit_factor |
	server_ip_ratelimit_factor | server_ratelimit_backoff |
	server_ip_ratelimit_backoff | server_outbound_msg_retry |
	server_max_sent_count | server_max_query_restarts |
	server_send_client_subnet | server_client_subnet_zone |
	server_client_subnet_always_forward | server_client_subnet_opcode |
	server_max_client_subnet_ipv4 | server_max_client_subnet_ipv6 |
	server_min_client_subnet_ipv4 | server_min_client_subnet_ipv6 |
	server_max_ecs_tree_size_ipv4 | server_max_ecs_tree_size_ipv6 |
	server_caps_whitelist | server_cache_max_negative_ttl |
	server_cache_min_negative_ttl |
	server_permit_small_holddown | server_qname_minimisation |
	server_ip_freebind | server_define_tag | server_local_zone_tag |
	server_disable_dnssec_lame_check | server_access_control_tag |
	server_local_zone_override | server_access_control_tag_action |
	server_access_control_tag_data | server_access_control_view |
	server_interface_action | server_interface_view | server_interface_tag |
	server_interface_tag_action | server_interface_tag_data |
	server_qname_minimisation_strict |
	server_pad_responses | server_pad_responses_block_size |
	server_pad_queries | server_pad_queries_block_size |
	server_serve_expired |
	server_serve_expired_ttl | server_serve_expired_ttl_reset |
	server_serve_expired_reply_ttl | server_serve_expired_client_timeout |
	server_ede_serve_expired | server_serve_original_ttl | server_fake_dsa |
	server_log_identity | server_use_systemd |
	server_response_ip_tag | server_response_ip | server_response_ip_data |
	server_shm_enable | server_shm_key | server_fake_sha1 |
	server_hide_trustanchor | server_trust_anchor_signaling |
	server_root_key_sentinel |
	server_ipsecmod_enabled | server_ipsecmod_hook |
	server_ipsecmod_ignore_bogus | server_ipsecmod_max_ttl |
	server_ipsecmod_whitelist | server_ipsecmod_strict |
	server_udp_upstream_without_downstream | server_aggressive_nsec |
	server_tls_cert_bundle | server_tls_additional_port | server_low_rtt |
	server_fast_server_permil | server_fast_server_num  | server_tls_win_cert |
	server_tcp_connection_limit | server_log_servfail | server_deny_any |
	server_unknown_server_time_limit | server_log_tag_queryreply |
	server_discard_timeout | server_wait_limit | server_wait_limit_cookie |
	server_wait_limit_netblock | server_wait_limit_cookie_netblock |
	server_stream_wait_size | server_tls_ciphers |
	server_tls_ciphersuites | server_tls_session_ticket_keys |
	server_answer_cookie | server_cookie_secret | server_ip_ratelimit_cookie |
	server_tls_use_sni | server_edns_client_string |
	server_edns_client_string_opcode | server_nsid |
	server_zonemd_permissive_mode | server_max_reuse_tcp_queries |
	server_tcp_reuse_timeout | server_tcp_auth_query_timeout |
	server_quic_port | server_quic_size |
	server_interface_automatic_ports | server_ede |
	server_dns_error_reporting |
	server_proxy_protocol_port | server_statistics_inhibit_zero |
	server_harden_unknown_additional | server_disable_edns_do |
	server_log_destaddr | server_cookie_secret_file |
	server_iter_scrub_ns | server_iter_scrub_cname | server_max_global_quota |
	server_harden_unverified_glue | server_log_time_iso
	;
stub_clause: stubstart contents_stub
	{
		/* stub end */
		if(cfg_parser->cfg->stubs &&
			!cfg_parser->cfg->stubs->name)
			yyerror("stub-zone without name");
	}
	;
stubstart: VAR_STUB_ZONE
	{
		struct config_stub* s;
		OUTYY(("\nP(stub_zone:)\n"));
		cfg_parser->started_toplevel = 1;
		s = (struct config_stub*)calloc(1, sizeof(struct config_stub));
		if(s) {
			s->next = cfg_parser->cfg->stubs;
			cfg_parser->cfg->stubs = s;
		} else {
			yyerror("out of memory");
		}
	}
	;
contents_stub: contents_stub content_stub
	| ;
content_stub: stub_name | stub_host | stub_addr | stub_prime | stub_first |
	stub_no_cache | stub_ssl_upstream | stub_tcp_upstream
	;
forward_clause: forwardstart contents_forward
	{
		/* forward end */
		if(cfg_parser->cfg->forwards &&
			!cfg_parser->cfg->forwards->name)
			yyerror("forward-zone without name");
	}
	;
forwardstart: VAR_FORWARD_ZONE
	{
		struct config_stub* s;
		OUTYY(("\nP(forward_zone:)\n"));
		cfg_parser->started_toplevel = 1;
		s = (struct config_stub*)calloc(1, sizeof(struct config_stub));
		if(s) {
			s->next = cfg_parser->cfg->forwards;
			cfg_parser->cfg->forwards = s;
		} else {
			yyerror("out of memory");
		}
	}
	;
contents_forward: contents_forward content_forward
	| ;
content_forward: forward_name | forward_host | forward_addr | forward_first |
	forward_no_cache | forward_ssl_upstream | forward_tcp_upstream
	;
view_clause: viewstart contents_view
	{
		/* view end */
		if(cfg_parser->cfg->views &&
			!cfg_parser->cfg->views->name)
			yyerror("view without name");
	}
	;
viewstart: VAR_VIEW
	{
		struct config_view* s;
		OUTYY(("\nP(view:)\n"));
		cfg_parser->started_toplevel = 1;
		s = (struct config_view*)calloc(1, sizeof(struct config_view));
		if(s) {
			s->next = cfg_parser->cfg->views;
			cfg_parser->cfg->views = s;
		} else {
			yyerror("out of memory");
		}
	}
	;
contents_view: contents_view content_view
	| ;
content_view: view_name | view_local_zone | view_local_data | view_first |
		view_response_ip | view_response_ip_data | view_local_data_ptr
	;
authstart: VAR_AUTH_ZONE
	{
		struct config_auth* s;
		OUTYY(("\nP(auth_zone:)\n"));
		cfg_parser->started_toplevel = 1;
		s = (struct config_auth*)calloc(1, sizeof(struct config_auth));
		if(s) {
			s->next = cfg_parser->cfg->auths;
			cfg_parser->cfg->auths = s;
			/* defaults for auth zone */
			s->for_downstream = 1;
			s->for_upstream = 1;
			s->fallback_enabled = 0;
			s->zonemd_check = 0;
			s->zonemd_reject_absence = 0;
			s->isrpz = 0;
		} else {
			yyerror("out of memory");
		}
	}
	;
contents_auth: contents_auth content_auth
	| ;
content_auth: auth_name | auth_zonefile | auth_master | auth_url |
	auth_for_downstream | auth_for_upstream | auth_fallback_enabled |
	auth_allow_notify | auth_zonemd_check | auth_zonemd_reject_absence
	;

rpz_tag: VAR_TAGS STRING_ARG
	{
		uint8_t* bitlist;
		size_t len = 0;
		OUTYY(("P(server_local_zone_tag:%s)\n", $2));
		bitlist = config_parse_taglist(cfg_parser->cfg, $2,
			&len);
		free($2);
		if(!bitlist) {
			yyerror("could not parse tags, (define-tag them first)");
		}
		if(bitlist) {
			cfg_parser->cfg->auths->rpz_taglist = bitlist;
			cfg_parser->cfg->auths->rpz_taglistlen = len;

		}
	}
	;

rpz_action_override: VAR_RPZ_ACTION_OVERRIDE STRING_ARG
	{
		OUTYY(("P(rpz_action_override:%s)\n", $2));
		if(strcmp($2, "nxdomain")!=0 && strcmp($2, "nodata")!=0 &&
		   strcmp($2, "passthru")!=0 && strcmp($2, "drop")!=0 &&
		   strcmp($2, "cname")!=0 && strcmp($2, "disabled")!=0) {
			yyerror("rpz-action-override action: expected nxdomain, "
				"nodata, passthru, drop, cname or disabled");
			free($2);
			cfg_parser->cfg->auths->rpz_action_override = NULL;
		}
		else {
			cfg_parser->cfg->auths->rpz_action_override = $2;
		}
	}
	;

rpz_cname_override: VAR_RPZ_CNAME_OVERRIDE STRING_ARG
	{
		OUTYY(("P(rpz_cname_override:%s)\n", $2));
		free(cfg_parser->cfg->auths->rpz_cname);
		cfg_parser->cfg->auths->rpz_cname = $2;
	}
	;

rpz_log: VAR_RPZ_LOG STRING_ARG
	{
		OUTYY(("P(rpz_log:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->auths->rpz_log = (strcmp($2, "yes")==0);
		free($2);
	}
	;

rpz_log_name: VAR_RPZ_LOG_NAME STRING_ARG
	{
		OUTYY(("P(rpz_log_name:%s)\n", $2));
		free(cfg_parser->cfg->auths->rpz_log_name);
		cfg_parser->cfg->auths->rpz_log_name = $2;
	}
	;
rpz_signal_nxdomain_ra: VAR_RPZ_SIGNAL_NXDOMAIN_RA STRING_ARG
	{
		OUTYY(("P(rpz_signal_nxdomain_ra:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->auths->rpz_signal_nxdomain_ra = (strcmp($2, "yes")==0);
		free($2);
	}
	;

rpzstart: VAR_RPZ
	{
		struct config_auth* s;
		OUTYY(("\nP(rpz:)\n"));
		cfg_parser->started_toplevel = 1;
		s = (struct config_auth*)calloc(1, sizeof(struct config_auth));
		if(s) {
			s->next = cfg_parser->cfg->auths;
			cfg_parser->cfg->auths = s;
			/* defaults for RPZ auth zone */
			s->for_downstream = 0;
			s->for_upstream = 0;
			s->fallback_enabled = 0;
			s->isrpz = 1;
		} else {
			yyerror("out of memory");
		}
	}
	;
contents_rpz: contents_rpz content_rpz
	| ;
content_rpz: auth_name | auth_zonefile | rpz_tag | auth_master | auth_url |
	   auth_allow_notify | rpz_action_override | rpz_cname_override |
	   rpz_log | rpz_log_name | rpz_signal_nxdomain_ra | auth_for_downstream
	;
server_num_threads: VAR_NUM_THREADS STRING_ARG
	{
		OUTYY(("P(server_num_threads:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->num_threads = atoi($2);
		free($2);
	}
	;
server_verbosity: VAR_VERBOSITY STRING_ARG
	{
		OUTYY(("P(server_verbosity:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->verbosity = atoi($2);
		free($2);
	}
	;
server_statistics_interval: VAR_STATISTICS_INTERVAL STRING_ARG
	{
		OUTYY(("P(server_statistics_interval:%s)\n", $2));
		if(strcmp($2, "") == 0 || strcmp($2, "0") == 0)
			cfg_parser->cfg->stat_interval = 0;
		else if(atoi($2) == 0)
			yyerror("number expected");
		else cfg_parser->cfg->stat_interval = atoi($2);
		free($2);
	}
	;
server_statistics_cumulative: VAR_STATISTICS_CUMULATIVE STRING_ARG
	{
		OUTYY(("P(server_statistics_cumulative:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->stat_cumulative = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_extended_statistics: VAR_EXTENDED_STATISTICS STRING_ARG
	{
		OUTYY(("P(server_extended_statistics:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->stat_extended = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_statistics_inhibit_zero: VAR_STATISTICS_INHIBIT_ZERO STRING_ARG
	{
		OUTYY(("P(server_statistics_inhibit_zero:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->stat_inhibit_zero = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_shm_enable: VAR_SHM_ENABLE STRING_ARG
	{
		OUTYY(("P(server_shm_enable:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->shm_enable = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_shm_key: VAR_SHM_KEY STRING_ARG
	{
		OUTYY(("P(server_shm_key:%s)\n", $2));
		if(strcmp($2, "") == 0 || strcmp($2, "0") == 0)
			cfg_parser->cfg->shm_key = 0;
		else if(atoi($2) == 0)
			yyerror("number expected");
		else cfg_parser->cfg->shm_key = atoi($2);
		free($2);
	}
	;
server_port: VAR_PORT STRING_ARG
	{
		OUTYY(("P(server_port:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("port number expected");
		else cfg_parser->cfg->port = atoi($2);
		free($2);
	}
	;
server_send_client_subnet: VAR_SEND_CLIENT_SUBNET STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(server_send_client_subnet:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->client_subnet, $2))
			fatal_exit("out of memory adding client-subnet");
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
		free($2);
	#endif
	}
	;
server_client_subnet_zone: VAR_CLIENT_SUBNET_ZONE STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(server_client_subnet_zone:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->client_subnet_zone,
			$2))
			fatal_exit("out of memory adding client-subnet-zone");
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
		free($2);
	#endif
	}
	;
server_client_subnet_always_forward:
	VAR_CLIENT_SUBNET_ALWAYS_FORWARD STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(server_client_subnet_always_forward:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else
			cfg_parser->cfg->client_subnet_always_forward =
				(strcmp($2, "yes")==0);
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_client_subnet_opcode: VAR_CLIENT_SUBNET_OPCODE STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(client_subnet_opcode:%s)\n", $2));
		OUTYY(("P(Deprecated option, ignoring)\n"));
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_max_client_subnet_ipv4: VAR_MAX_CLIENT_SUBNET_IPV4 STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(max_client_subnet_ipv4:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("IPv4 subnet length expected");
		else if (atoi($2) > 32)
			cfg_parser->cfg->max_client_subnet_ipv4 = 32;
		else if (atoi($2) < 0)
			cfg_parser->cfg->max_client_subnet_ipv4 = 0;
		else cfg_parser->cfg->max_client_subnet_ipv4 = (uint8_t)atoi($2);
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_max_client_subnet_ipv6: VAR_MAX_CLIENT_SUBNET_IPV6 STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(max_client_subnet_ipv6:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("Ipv6 subnet length expected");
		else if (atoi($2) > 128)
			cfg_parser->cfg->max_client_subnet_ipv6 = 128;
		else if (atoi($2) < 0)
			cfg_parser->cfg->max_client_subnet_ipv6 = 0;
		else cfg_parser->cfg->max_client_subnet_ipv6 = (uint8_t)atoi($2);
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_min_client_subnet_ipv4: VAR_MIN_CLIENT_SUBNET_IPV4 STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(min_client_subnet_ipv4:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("IPv4 subnet length expected");
		else if (atoi($2) > 32)
			cfg_parser->cfg->min_client_subnet_ipv4 = 32;
		else if (atoi($2) < 0)
			cfg_parser->cfg->min_client_subnet_ipv4 = 0;
		else cfg_parser->cfg->min_client_subnet_ipv4 = (uint8_t)atoi($2);
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_min_client_subnet_ipv6: VAR_MIN_CLIENT_SUBNET_IPV6 STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(min_client_subnet_ipv6:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("Ipv6 subnet length expected");
		else if (atoi($2) > 128)
			cfg_parser->cfg->min_client_subnet_ipv6 = 128;
		else if (atoi($2) < 0)
			cfg_parser->cfg->min_client_subnet_ipv6 = 0;
		else cfg_parser->cfg->min_client_subnet_ipv6 = (uint8_t)atoi($2);
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_max_ecs_tree_size_ipv4: VAR_MAX_ECS_TREE_SIZE_IPV4 STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(max_ecs_tree_size_ipv4:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("IPv4 ECS tree size expected");
		else if (atoi($2) < 0)
			cfg_parser->cfg->max_ecs_tree_size_ipv4 = 0;
		else cfg_parser->cfg->max_ecs_tree_size_ipv4 = (uint32_t)atoi($2);
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_max_ecs_tree_size_ipv6: VAR_MAX_ECS_TREE_SIZE_IPV6 STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(max_ecs_tree_size_ipv6:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("IPv6 ECS tree size expected");
		else if (atoi($2) < 0)
			cfg_parser->cfg->max_ecs_tree_size_ipv6 = 0;
		else cfg_parser->cfg->max_ecs_tree_size_ipv6 = (uint32_t)atoi($2);
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_interface: VAR_INTERFACE STRING_ARG
	{
		OUTYY(("P(server_interface:%s)\n", $2));
		if(cfg_parser->cfg->num_ifs == 0)
			cfg_parser->cfg->ifs = calloc(1, sizeof(char*));
		else cfg_parser->cfg->ifs = realloc(cfg_parser->cfg->ifs,
				(cfg_parser->cfg->num_ifs+1)*sizeof(char*));
		if(!cfg_parser->cfg->ifs)
			yyerror("out of memory");
		else
			cfg_parser->cfg->ifs[cfg_parser->cfg->num_ifs++] = $2;
	}
	;
server_outgoing_interface: VAR_OUTGOING_INTERFACE STRING_ARG
	{
		OUTYY(("P(server_outgoing_interface:%s)\n", $2));
		if(cfg_parser->cfg->num_out_ifs == 0)
			cfg_parser->cfg->out_ifs = calloc(1, sizeof(char*));
		else cfg_parser->cfg->out_ifs = realloc(
			cfg_parser->cfg->out_ifs,
			(cfg_parser->cfg->num_out_ifs+1)*sizeof(char*));
		if(!cfg_parser->cfg->out_ifs)
			yyerror("out of memory");
		else
			cfg_parser->cfg->out_ifs[
				cfg_parser->cfg->num_out_ifs++] = $2;
	}
	;
server_outgoing_range: VAR_OUTGOING_RANGE STRING_ARG
	{
		OUTYY(("P(server_outgoing_range:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else cfg_parser->cfg->outgoing_num_ports = atoi($2);
		free($2);
	}
	;
server_outgoing_port_permit: VAR_OUTGOING_PORT_PERMIT STRING_ARG
	{
		OUTYY(("P(server_outgoing_port_permit:%s)\n", $2));
		if(!cfg_mark_ports($2, 1,
			cfg_parser->cfg->outgoing_avail_ports, 65536))
			yyerror("port number or range (\"low-high\") expected");
		free($2);
	}
	;
server_outgoing_port_avoid: VAR_OUTGOING_PORT_AVOID STRING_ARG
	{
		OUTYY(("P(server_outgoing_port_avoid:%s)\n", $2));
		if(!cfg_mark_ports($2, 0,
			cfg_parser->cfg->outgoing_avail_ports, 65536))
			yyerror("port number or range (\"low-high\") expected");
		free($2);
	}
	;
server_outgoing_num_tcp: VAR_OUTGOING_NUM_TCP STRING_ARG
	{
		OUTYY(("P(server_outgoing_num_tcp:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->outgoing_num_tcp = atoi($2);
		free($2);
	}
	;
server_incoming_num_tcp: VAR_INCOMING_NUM_TCP STRING_ARG
	{
		OUTYY(("P(server_incoming_num_tcp:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->incoming_num_tcp = atoi($2);
		free($2);
	}
	;
server_interface_automatic: VAR_INTERFACE_AUTOMATIC STRING_ARG
	{
		OUTYY(("P(server_interface_automatic:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->if_automatic = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_interface_automatic_ports: VAR_INTERFACE_AUTOMATIC_PORTS STRING_ARG
	{
		OUTYY(("P(server_interface_automatic_ports:%s)\n", $2));
		free(cfg_parser->cfg->if_automatic_ports);
		cfg_parser->cfg->if_automatic_ports = $2;
	}
	;
server_do_ip4: VAR_DO_IP4 STRING_ARG
	{
		OUTYY(("P(server_do_ip4:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->do_ip4 = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_do_ip6: VAR_DO_IP6 STRING_ARG
	{
		OUTYY(("P(server_do_ip6:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->do_ip6 = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_do_nat64: VAR_DO_NAT64 STRING_ARG
	{
		OUTYY(("P(server_do_nat64:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->do_nat64 = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_do_udp: VAR_DO_UDP STRING_ARG
	{
		OUTYY(("P(server_do_udp:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->do_udp = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_do_tcp: VAR_DO_TCP STRING_ARG
	{
		OUTYY(("P(server_do_tcp:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->do_tcp = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_prefer_ip4: VAR_PREFER_IP4 STRING_ARG
	{
		OUTYY(("P(server_prefer_ip4:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->prefer_ip4 = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_prefer_ip6: VAR_PREFER_IP6 STRING_ARG
	{
		OUTYY(("P(server_prefer_ip6:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->prefer_ip6 = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_tcp_mss: VAR_TCP_MSS STRING_ARG
	{
		OUTYY(("P(server_tcp_mss:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
				yyerror("number expected");
		else cfg_parser->cfg->tcp_mss = atoi($2);
		free($2);
	}
	;
server_outgoing_tcp_mss: VAR_OUTGOING_TCP_MSS STRING_ARG
	{
		OUTYY(("P(server_outgoing_tcp_mss:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->outgoing_tcp_mss = atoi($2);
		free($2);
	}
	;
server_tcp_idle_timeout: VAR_TCP_IDLE_TIMEOUT STRING_ARG
	{
		OUTYY(("P(server_tcp_idle_timeout:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else if (atoi($2) > 120000)
			cfg_parser->cfg->tcp_idle_timeout = 120000;
		else if (atoi($2) < 1)
			cfg_parser->cfg->tcp_idle_timeout = 1;
		else cfg_parser->cfg->tcp_idle_timeout = atoi($2);
		free($2);
	}
	;
server_max_reuse_tcp_queries: VAR_MAX_REUSE_TCP_QUERIES STRING_ARG
	{
		OUTYY(("P(server_max_reuse_tcp_queries:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else if (atoi($2) < 1)
			cfg_parser->cfg->max_reuse_tcp_queries = 0;
		else cfg_parser->cfg->max_reuse_tcp_queries = atoi($2);
		free($2);
	}
	;
server_tcp_reuse_timeout: VAR_TCP_REUSE_TIMEOUT STRING_ARG
	{
		OUTYY(("P(server_tcp_reuse_timeout:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else if (atoi($2) < 1)
			cfg_parser->cfg->tcp_reuse_timeout = 0;
		else cfg_parser->cfg->tcp_reuse_timeout = atoi($2);
		free($2);
	}
	;
server_tcp_auth_query_timeout: VAR_TCP_AUTH_QUERY_TIMEOUT STRING_ARG
	{
		OUTYY(("P(server_tcp_auth_query_timeout:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else if (atoi($2) < 1)
			cfg_parser->cfg->tcp_auth_query_timeout = 0;
		else cfg_parser->cfg->tcp_auth_query_timeout = atoi($2);
		free($2);
	}
	;
server_tcp_keepalive: VAR_EDNS_TCP_KEEPALIVE STRING_ARG
	{
		OUTYY(("P(server_tcp_keepalive:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->do_tcp_keepalive = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_tcp_keepalive_timeout: VAR_EDNS_TCP_KEEPALIVE_TIMEOUT STRING_ARG
	{
		OUTYY(("P(server_tcp_keepalive_timeout:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else if (atoi($2) > 6553500)
			cfg_parser->cfg->tcp_keepalive_timeout = 6553500;
		else if (atoi($2) < 1)
			cfg_parser->cfg->tcp_keepalive_timeout = 0;
		else cfg_parser->cfg->tcp_keepalive_timeout = atoi($2);
		free($2);
	}
	;
server_sock_queue_timeout: VAR_SOCK_QUEUE_TIMEOUT STRING_ARG
	{
		OUTYY(("P(server_sock_queue_timeout:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else if (atoi($2) > 6553500)
			cfg_parser->cfg->sock_queue_timeout = 6553500;
		else if (atoi($2) < 1)
			cfg_parser->cfg->sock_queue_timeout = 0;
		else cfg_parser->cfg->sock_queue_timeout = atoi($2);
		free($2);
	}
	;
server_tcp_upstream: VAR_TCP_UPSTREAM STRING_ARG
	{
		OUTYY(("P(server_tcp_upstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->tcp_upstream = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_udp_upstream_without_downstream: VAR_UDP_UPSTREAM_WITHOUT_DOWNSTREAM STRING_ARG
	{
		OUTYY(("P(server_udp_upstream_without_downstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->udp_upstream_without_downstream = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_ssl_upstream: VAR_SSL_UPSTREAM STRING_ARG
	{
		OUTYY(("P(server_ssl_upstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ssl_upstream = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_ssl_service_key: VAR_SSL_SERVICE_KEY STRING_ARG
	{
		OUTYY(("P(server_ssl_service_key:%s)\n", $2));
		free(cfg_parser->cfg->ssl_service_key);
		cfg_parser->cfg->ssl_service_key = $2;
	}
	;
server_ssl_service_pem: VAR_SSL_SERVICE_PEM STRING_ARG
	{
		OUTYY(("P(server_ssl_service_pem:%s)\n", $2));
		free(cfg_parser->cfg->ssl_service_pem);
		cfg_parser->cfg->ssl_service_pem = $2;
	}
	;
server_ssl_port: VAR_SSL_PORT STRING_ARG
	{
		OUTYY(("P(server_ssl_port:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("port number expected");
		else cfg_parser->cfg->ssl_port = atoi($2);
		free($2);
	}
	;
server_tls_cert_bundle: VAR_TLS_CERT_BUNDLE STRING_ARG
	{
		OUTYY(("P(server_tls_cert_bundle:%s)\n", $2));
		free(cfg_parser->cfg->tls_cert_bundle);
		cfg_parser->cfg->tls_cert_bundle = $2;
	}
	;
server_tls_win_cert: VAR_TLS_WIN_CERT STRING_ARG
	{
		OUTYY(("P(server_tls_win_cert:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->tls_win_cert = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_tls_additional_port: VAR_TLS_ADDITIONAL_PORT STRING_ARG
	{
		OUTYY(("P(server_tls_additional_port:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->tls_additional_port,
			$2))
			yyerror("out of memory");
	}
	;
server_tls_ciphers: VAR_TLS_CIPHERS STRING_ARG
	{
		OUTYY(("P(server_tls_ciphers:%s)\n", $2));
		free(cfg_parser->cfg->tls_ciphers);
		cfg_parser->cfg->tls_ciphers = $2;
	}
	;
server_tls_ciphersuites: VAR_TLS_CIPHERSUITES STRING_ARG
	{
		OUTYY(("P(server_tls_ciphersuites:%s)\n", $2));
		free(cfg_parser->cfg->tls_ciphersuites);
		cfg_parser->cfg->tls_ciphersuites = $2;
	}
	;
server_tls_session_ticket_keys: VAR_TLS_SESSION_TICKET_KEYS STRING_ARG
	{
		OUTYY(("P(server_tls_session_ticket_keys:%s)\n", $2));
		if(!cfg_strlist_append(&cfg_parser->cfg->tls_session_ticket_keys,
			$2))
			yyerror("out of memory");
	}
	;
server_tls_use_sni: VAR_TLS_USE_SNI STRING_ARG
	{
		OUTYY(("P(server_tls_use_sni:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->tls_use_sni = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_https_port: VAR_HTTPS_PORT STRING_ARG
	{
		OUTYY(("P(server_https_port:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("port number expected");
		else cfg_parser->cfg->https_port = atoi($2);
		free($2);
	};
server_http_endpoint: VAR_HTTP_ENDPOINT STRING_ARG
	{
		OUTYY(("P(server_http_endpoint:%s)\n", $2));
		free(cfg_parser->cfg->http_endpoint);
		if($2 && $2[0] != '/') {
			cfg_parser->cfg->http_endpoint = malloc(strlen($2)+2);
			if(!cfg_parser->cfg->http_endpoint)
				yyerror("out of memory");
			cfg_parser->cfg->http_endpoint[0] = '/';
			memmove(cfg_parser->cfg->http_endpoint+1, $2,
				strlen($2)+1);
			free($2);
		} else {
			cfg_parser->cfg->http_endpoint = $2;
		}
	};
server_http_max_streams: VAR_HTTP_MAX_STREAMS STRING_ARG
	{
		OUTYY(("P(server_http_max_streams:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->http_max_streams = atoi($2);
		free($2);
	};
server_http_query_buffer_size: VAR_HTTP_QUERY_BUFFER_SIZE STRING_ARG
	{
		OUTYY(("P(server_http_query_buffer_size:%s)\n", $2));
		if(!cfg_parse_memsize($2,
			&cfg_parser->cfg->http_query_buffer_size))
			yyerror("memory size expected");
		free($2);
	};
server_http_response_buffer_size: VAR_HTTP_RESPONSE_BUFFER_SIZE STRING_ARG
	{
		OUTYY(("P(server_http_response_buffer_size:%s)\n", $2));
		if(!cfg_parse_memsize($2,
			&cfg_parser->cfg->http_response_buffer_size))
			yyerror("memory size expected");
		free($2);
	};
server_http_nodelay: VAR_HTTP_NODELAY STRING_ARG
	{
		OUTYY(("P(server_http_nodelay:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->http_nodelay = (strcmp($2, "yes")==0);
		free($2);
	};
server_http_notls_downstream: VAR_HTTP_NOTLS_DOWNSTREAM STRING_ARG
	{
		OUTYY(("P(server_http_notls_downstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->http_notls_downstream = (strcmp($2, "yes")==0);
		free($2);
	};
server_quic_port: VAR_QUIC_PORT STRING_ARG
	{
		OUTYY(("P(server_quic_port:%s)\n", $2));
#ifndef HAVE_NGTCP2
		log_warn("%s:%d: Unbound is not compiled with "
			"ngtcp2. This is required to use DNS "
			"over QUIC.", cfg_parser->filename, cfg_parser->line);
#endif
		if(atoi($2) == 0)
			yyerror("port number expected");
		else cfg_parser->cfg->quic_port = atoi($2);
		free($2);
	};
server_quic_size: VAR_QUIC_SIZE STRING_ARG
	{
		OUTYY(("P(server_quic_size:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->quic_size))
			yyerror("memory size expected");
		free($2);
	};
server_use_systemd: VAR_USE_SYSTEMD STRING_ARG
	{
		OUTYY(("P(server_use_systemd:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->use_systemd = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_do_daemonize: VAR_DO_DAEMONIZE STRING_ARG
	{
		OUTYY(("P(server_do_daemonize:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->do_daemonize = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_use_syslog: VAR_USE_SYSLOG STRING_ARG
	{
		OUTYY(("P(server_use_syslog:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->use_syslog = (strcmp($2, "yes")==0);
#if !defined(HAVE_SYSLOG_H) && !defined(UB_ON_WINDOWS)
		if(strcmp($2, "yes") == 0)
			yyerror("no syslog services are available. "
				"(reconfigure and compile to add)");
#endif
		free($2);
	}
	;
server_log_time_ascii: VAR_LOG_TIME_ASCII STRING_ARG
	{
		OUTYY(("P(server_log_time_ascii:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->log_time_ascii = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_log_time_iso: VAR_LOG_TIME_ISO STRING_ARG
	{
		OUTYY(("P(server_log_time_iso:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->log_time_iso = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_log_queries: VAR_LOG_QUERIES STRING_ARG
	{
		OUTYY(("P(server_log_queries:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->log_queries = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_log_replies: VAR_LOG_REPLIES STRING_ARG
	{
		OUTYY(("P(server_log_replies:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->log_replies = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_log_tag_queryreply: VAR_LOG_TAG_QUERYREPLY STRING_ARG
	{
		OUTYY(("P(server_log_tag_queryreply:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->log_tag_queryreply = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_log_servfail: VAR_LOG_SERVFAIL STRING_ARG
	{
		OUTYY(("P(server_log_servfail:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->log_servfail = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_log_destaddr: VAR_LOG_DESTADDR STRING_ARG
	{
		OUTYY(("P(server_log_destaddr:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->log_destaddr = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_log_local_actions: VAR_LOG_LOCAL_ACTIONS STRING_ARG
	{
		OUTYY(("P(server_log_local_actions:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->log_local_actions = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_chroot: VAR_CHROOT STRING_ARG
	{
		OUTYY(("P(server_chroot:%s)\n", $2));
		free(cfg_parser->cfg->chrootdir);
		cfg_parser->cfg->chrootdir = $2;
	}
	;
server_username: VAR_USERNAME STRING_ARG
	{
		OUTYY(("P(server_username:%s)\n", $2));
		free(cfg_parser->cfg->username);
		cfg_parser->cfg->username = $2;
	}
	;
server_directory: VAR_DIRECTORY STRING_ARG
	{
		OUTYY(("P(server_directory:%s)\n", $2));
		free(cfg_parser->cfg->directory);
		cfg_parser->cfg->directory = $2;
		/* change there right away for includes relative to this */
		if($2[0]) {
			char* d;
#ifdef UB_ON_WINDOWS
			w_config_adjust_directory(cfg_parser->cfg);
#endif
			d = cfg_parser->cfg->directory;
			/* adjust directory if we have already chroot,
			 * like, we reread after sighup */
			if(cfg_parser->chroot && cfg_parser->chroot[0] &&
				strncmp(d, cfg_parser->chroot, strlen(
				cfg_parser->chroot)) == 0)
				d += strlen(cfg_parser->chroot);
			if(d[0]) {
				if(chdir(d))
				log_err("cannot chdir to directory: %s (%s)",
					d, strerror(errno));
			}
		}
	}
	;
server_logfile: VAR_LOGFILE STRING_ARG
	{
		OUTYY(("P(server_logfile:%s)\n", $2));
		free(cfg_parser->cfg->logfile);
		cfg_parser->cfg->logfile = $2;
		cfg_parser->cfg->use_syslog = 0;
	}
	;
server_pidfile: VAR_PIDFILE STRING_ARG
	{
		OUTYY(("P(server_pidfile:%s)\n", $2));
		free(cfg_parser->cfg->pidfile);
		cfg_parser->cfg->pidfile = $2;
	}
	;
server_root_hints: VAR_ROOT_HINTS STRING_ARG
	{
		OUTYY(("P(server_root_hints:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->root_hints, $2))
			yyerror("out of memory");
	}
	;
server_dlv_anchor_file: VAR_DLV_ANCHOR_FILE STRING_ARG
	{
		OUTYY(("P(server_dlv_anchor_file:%s)\n", $2));
		log_warn("option dlv-anchor-file ignored: DLV is decommissioned");
		free($2);
	}
	;
server_dlv_anchor: VAR_DLV_ANCHOR STRING_ARG
	{
		OUTYY(("P(server_dlv_anchor:%s)\n", $2));
		log_warn("option dlv-anchor ignored: DLV is decommissioned");
		free($2);
	}
	;
server_auto_trust_anchor_file: VAR_AUTO_TRUST_ANCHOR_FILE STRING_ARG
	{
		OUTYY(("P(server_auto_trust_anchor_file:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->
			auto_trust_anchor_file_list, $2))
			yyerror("out of memory");
	}
	;
server_trust_anchor_file: VAR_TRUST_ANCHOR_FILE STRING_ARG
	{
		OUTYY(("P(server_trust_anchor_file:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->
			trust_anchor_file_list, $2))
			yyerror("out of memory");
	}
	;
server_trusted_keys_file: VAR_TRUSTED_KEYS_FILE STRING_ARG
	{
		OUTYY(("P(server_trusted_keys_file:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->
			trusted_keys_file_list, $2))
			yyerror("out of memory");
	}
	;
server_trust_anchor: VAR_TRUST_ANCHOR STRING_ARG
	{
		OUTYY(("P(server_trust_anchor:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->trust_anchor_list, $2))
			yyerror("out of memory");
	}
	;
server_trust_anchor_signaling: VAR_TRUST_ANCHOR_SIGNALING STRING_ARG
	{
		OUTYY(("P(server_trust_anchor_signaling:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else
			cfg_parser->cfg->trust_anchor_signaling =
				(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_root_key_sentinel: VAR_ROOT_KEY_SENTINEL STRING_ARG
	{
		OUTYY(("P(server_root_key_sentinel:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else
			cfg_parser->cfg->root_key_sentinel =
				(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_domain_insecure: VAR_DOMAIN_INSECURE STRING_ARG
	{
		OUTYY(("P(server_domain_insecure:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->domain_insecure, $2))
			yyerror("out of memory");
	}
	;
server_hide_identity: VAR_HIDE_IDENTITY STRING_ARG
	{
		OUTYY(("P(server_hide_identity:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->hide_identity = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_hide_version: VAR_HIDE_VERSION STRING_ARG
	{
		OUTYY(("P(server_hide_version:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->hide_version = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_hide_trustanchor: VAR_HIDE_TRUSTANCHOR STRING_ARG
	{
		OUTYY(("P(server_hide_trustanchor:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->hide_trustanchor = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_hide_http_user_agent: VAR_HIDE_HTTP_USER_AGENT STRING_ARG
	{
		OUTYY(("P(server_hide_user_agent:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->hide_http_user_agent = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_identity: VAR_IDENTITY STRING_ARG
	{
		OUTYY(("P(server_identity:%s)\n", $2));
		free(cfg_parser->cfg->identity);
		cfg_parser->cfg->identity = $2;
	}
	;
server_version: VAR_VERSION STRING_ARG
	{
		OUTYY(("P(server_version:%s)\n", $2));
		free(cfg_parser->cfg->version);
		cfg_parser->cfg->version = $2;
	}
	;
server_http_user_agent: VAR_HTTP_USER_AGENT STRING_ARG
	{
		OUTYY(("P(server_http_user_agent:%s)\n", $2));
		free(cfg_parser->cfg->http_user_agent);
		cfg_parser->cfg->http_user_agent = $2;
	}
	;
server_nsid: VAR_NSID STRING_ARG
	{
		OUTYY(("P(server_nsid:%s)\n", $2));
		free(cfg_parser->cfg->nsid_cfg_str);
		cfg_parser->cfg->nsid_cfg_str = $2;
		free(cfg_parser->cfg->nsid);
		cfg_parser->cfg->nsid = NULL;
		cfg_parser->cfg->nsid_len = 0;
		if (*$2 == 0)
			; /* pass; empty string is not setting nsid */
		else if (!(cfg_parser->cfg->nsid = cfg_parse_nsid(
					$2, &cfg_parser->cfg->nsid_len)))
			yyerror("the NSID must be either a hex string or an "
			    "ascii character string prepended with ascii_.");
	}
	;
server_so_rcvbuf: VAR_SO_RCVBUF STRING_ARG
	{
		OUTYY(("P(server_so_rcvbuf:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->so_rcvbuf))
			yyerror("buffer size expected");
		free($2);
	}
	;
server_so_sndbuf: VAR_SO_SNDBUF STRING_ARG
	{
		OUTYY(("P(server_so_sndbuf:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->so_sndbuf))
			yyerror("buffer size expected");
		free($2);
	}
	;
server_so_reuseport: VAR_SO_REUSEPORT STRING_ARG
	{
		OUTYY(("P(server_so_reuseport:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->so_reuseport =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_ip_transparent: VAR_IP_TRANSPARENT STRING_ARG
	{
		OUTYY(("P(server_ip_transparent:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ip_transparent =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_ip_freebind: VAR_IP_FREEBIND STRING_ARG
	{
		OUTYY(("P(server_ip_freebind:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ip_freebind =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_ip_dscp: VAR_IP_DSCP STRING_ARG
	{
		OUTYY(("P(server_ip_dscp:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else if (atoi($2) > 63)
			yyerror("value too large (max 63)");
		else if (atoi($2) < 0)
			yyerror("value too small (min 0)");
		else
			cfg_parser->cfg->ip_dscp = atoi($2);
		free($2);
	}
	;
server_stream_wait_size: VAR_STREAM_WAIT_SIZE STRING_ARG
	{
		OUTYY(("P(server_stream_wait_size:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->stream_wait_size))
			yyerror("memory size expected");
		free($2);
	}
	;
server_edns_buffer_size: VAR_EDNS_BUFFER_SIZE STRING_ARG
	{
		OUTYY(("P(server_edns_buffer_size:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else if (atoi($2) < 12)
			yyerror("edns buffer size too small");
		else if (atoi($2) > 65535)
			cfg_parser->cfg->edns_buffer_size = 65535;
		else cfg_parser->cfg->edns_buffer_size = atoi($2);
		free($2);
	}
	;
server_msg_buffer_size: VAR_MSG_BUFFER_SIZE STRING_ARG
	{
		OUTYY(("P(server_msg_buffer_size:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else if (atoi($2) < 4096)
			yyerror("message buffer size too small (use 4096)");
		else cfg_parser->cfg->msg_buffer_size = atoi($2);
		free($2);
	}
	;
server_msg_cache_size: VAR_MSG_CACHE_SIZE STRING_ARG
	{
		OUTYY(("P(server_msg_cache_size:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->msg_cache_size))
			yyerror("memory size expected");
		free($2);
	}
	;
server_msg_cache_slabs: VAR_MSG_CACHE_SLABS STRING_ARG
	{
		OUTYY(("P(server_msg_cache_slabs:%s)\n", $2));
		if(atoi($2) == 0) {
			yyerror("number expected");
		} else {
			cfg_parser->cfg->msg_cache_slabs = atoi($2);
			if(!is_pow2(cfg_parser->cfg->msg_cache_slabs))
				yyerror("must be a power of 2");
		}
		free($2);
	}
	;
server_num_queries_per_thread: VAR_NUM_QUERIES_PER_THREAD STRING_ARG
	{
		OUTYY(("P(server_num_queries_per_thread:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else cfg_parser->cfg->num_queries_per_thread = atoi($2);
		free($2);
	}
	;
server_jostle_timeout: VAR_JOSTLE_TIMEOUT STRING_ARG
	{
		OUTYY(("P(server_jostle_timeout:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->jostle_time = atoi($2);
		free($2);
	}
	;
server_delay_close: VAR_DELAY_CLOSE STRING_ARG
	{
		OUTYY(("P(server_delay_close:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->delay_close = atoi($2);
		free($2);
	}
	;
server_udp_connect: VAR_UDP_CONNECT STRING_ARG
	{
		OUTYY(("P(server_udp_connect:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->udp_connect = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_unblock_lan_zones: VAR_UNBLOCK_LAN_ZONES STRING_ARG
	{
		OUTYY(("P(server_unblock_lan_zones:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->unblock_lan_zones =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_insecure_lan_zones: VAR_INSECURE_LAN_ZONES STRING_ARG
	{
		OUTYY(("P(server_insecure_lan_zones:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->insecure_lan_zones =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_rrset_cache_size: VAR_RRSET_CACHE_SIZE STRING_ARG
	{
		OUTYY(("P(server_rrset_cache_size:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->rrset_cache_size))
			yyerror("memory size expected");
		free($2);
	}
	;
server_rrset_cache_slabs: VAR_RRSET_CACHE_SLABS STRING_ARG
	{
		OUTYY(("P(server_rrset_cache_slabs:%s)\n", $2));
		if(atoi($2) == 0) {
			yyerror("number expected");
		} else {
			cfg_parser->cfg->rrset_cache_slabs = atoi($2);
			if(!is_pow2(cfg_parser->cfg->rrset_cache_slabs))
				yyerror("must be a power of 2");
		}
		free($2);
	}
	;
server_infra_host_ttl: VAR_INFRA_HOST_TTL STRING_ARG
	{
		OUTYY(("P(server_infra_host_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->host_ttl = atoi($2);
		free($2);
	}
	;
server_infra_lame_ttl: VAR_INFRA_LAME_TTL STRING_ARG
	{
		OUTYY(("P(server_infra_lame_ttl:%s)\n", $2));
		verbose(VERB_DETAIL, "ignored infra-lame-ttl: %s (option "
			"removed, use infra-host-ttl)", $2);
		free($2);
	}
	;
server_infra_cache_numhosts: VAR_INFRA_CACHE_NUMHOSTS STRING_ARG
	{
		OUTYY(("P(server_infra_cache_numhosts:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else cfg_parser->cfg->infra_cache_numhosts = atoi($2);
		free($2);
	}
	;
server_infra_cache_lame_size: VAR_INFRA_CACHE_LAME_SIZE STRING_ARG
	{
		OUTYY(("P(server_infra_cache_lame_size:%s)\n", $2));
		verbose(VERB_DETAIL, "ignored infra-cache-lame-size: %s "
			"(option removed, use infra-cache-numhosts)", $2);
		free($2);
	}
	;
server_infra_cache_slabs: VAR_INFRA_CACHE_SLABS STRING_ARG
	{
		OUTYY(("P(server_infra_cache_slabs:%s)\n", $2));
		if(atoi($2) == 0) {
			yyerror("number expected");
		} else {
			cfg_parser->cfg->infra_cache_slabs = atoi($2);
			if(!is_pow2(cfg_parser->cfg->infra_cache_slabs))
				yyerror("must be a power of 2");
		}
		free($2);
	}
	;
server_infra_cache_min_rtt: VAR_INFRA_CACHE_MIN_RTT STRING_ARG
	{
		OUTYY(("P(server_infra_cache_min_rtt:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->infra_cache_min_rtt = atoi($2);
		free($2);
	}
	;
server_infra_cache_max_rtt: VAR_INFRA_CACHE_MAX_RTT STRING_ARG
	{
		OUTYY(("P(server_infra_cache_max_rtt:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->infra_cache_max_rtt = atoi($2);
		free($2);
	}
	;
server_infra_keep_probing: VAR_INFRA_KEEP_PROBING STRING_ARG
	{
		OUTYY(("P(server_infra_keep_probing:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->infra_keep_probing =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_target_fetch_policy: VAR_TARGET_FETCH_POLICY STRING_ARG
	{
		OUTYY(("P(server_target_fetch_policy:%s)\n", $2));
		free(cfg_parser->cfg->target_fetch_policy);
		cfg_parser->cfg->target_fetch_policy = $2;
	}
	;
server_harden_short_bufsize: VAR_HARDEN_SHORT_BUFSIZE STRING_ARG
	{
		OUTYY(("P(server_harden_short_bufsize:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_short_bufsize =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_harden_large_queries: VAR_HARDEN_LARGE_QUERIES STRING_ARG
	{
		OUTYY(("P(server_harden_large_queries:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_large_queries =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_harden_glue: VAR_HARDEN_GLUE STRING_ARG
	{
		OUTYY(("P(server_harden_glue:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_glue =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_harden_unverified_glue: VAR_HARDEN_UNVERIFIED_GLUE STRING_ARG
       {
               OUTYY(("P(server_harden_unverified_glue:%s)\n", $2));
               if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
                       yyerror("expected yes or no.");
               else cfg_parser->cfg->harden_unverified_glue =
                       (strcmp($2, "yes")==0);
               free($2);
       }
       ;
server_harden_dnssec_stripped: VAR_HARDEN_DNSSEC_STRIPPED STRING_ARG
	{
		OUTYY(("P(server_harden_dnssec_stripped:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_dnssec_stripped =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_harden_below_nxdomain: VAR_HARDEN_BELOW_NXDOMAIN STRING_ARG
	{
		OUTYY(("P(server_harden_below_nxdomain:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_below_nxdomain =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_harden_referral_path: VAR_HARDEN_REFERRAL_PATH STRING_ARG
	{
		OUTYY(("P(server_harden_referral_path:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_referral_path =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_harden_algo_downgrade: VAR_HARDEN_ALGO_DOWNGRADE STRING_ARG
	{
		OUTYY(("P(server_harden_algo_downgrade:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_algo_downgrade =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_harden_unknown_additional: VAR_HARDEN_UNKNOWN_ADDITIONAL STRING_ARG
	{
		OUTYY(("P(server_harden_unknown_additional:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_unknown_additional =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_use_caps_for_id: VAR_USE_CAPS_FOR_ID STRING_ARG
	{
		OUTYY(("P(server_use_caps_for_id:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->use_caps_bits_for_id =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_caps_whitelist: VAR_CAPS_WHITELIST STRING_ARG
	{
		OUTYY(("P(server_caps_whitelist:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->caps_whitelist, $2))
			yyerror("out of memory");
	}
	;
server_private_address: VAR_PRIVATE_ADDRESS STRING_ARG
	{
		OUTYY(("P(server_private_address:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->private_address, $2))
			yyerror("out of memory");
	}
	;
server_private_domain: VAR_PRIVATE_DOMAIN STRING_ARG
	{
		OUTYY(("P(server_private_domain:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->private_domain, $2))
			yyerror("out of memory");
	}
	;
server_prefetch: VAR_PREFETCH STRING_ARG
	{
		OUTYY(("P(server_prefetch:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->prefetch = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_prefetch_key: VAR_PREFETCH_KEY STRING_ARG
	{
		OUTYY(("P(server_prefetch_key:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->prefetch_key = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_deny_any: VAR_DENY_ANY STRING_ARG
	{
		OUTYY(("P(server_deny_any:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->deny_any = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_unwanted_reply_threshold: VAR_UNWANTED_REPLY_THRESHOLD STRING_ARG
	{
		OUTYY(("P(server_unwanted_reply_threshold:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->unwanted_threshold = atoi($2);
		free($2);
	}
	;
server_do_not_query_address: VAR_DO_NOT_QUERY_ADDRESS STRING_ARG
	{
		OUTYY(("P(server_do_not_query_address:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->donotqueryaddrs, $2))
			yyerror("out of memory");
	}
	;
server_do_not_query_localhost: VAR_DO_NOT_QUERY_LOCALHOST STRING_ARG
	{
		OUTYY(("P(server_do_not_query_localhost:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->donotquery_localhost =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_access_control: VAR_ACCESS_CONTROL STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_access_control:%s %s)\n", $2, $3));
		validate_acl_action($3);
		if(!cfg_str2list_insert(&cfg_parser->cfg->acls, $2, $3))
			fatal_exit("out of memory adding acl");
	}
	;
server_interface_action: VAR_INTERFACE_ACTION STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_interface_action:%s %s)\n", $2, $3));
		validate_acl_action($3);
		if(!cfg_str2list_insert(
			&cfg_parser->cfg->interface_actions, $2, $3))
			fatal_exit("out of memory adding acl");
	}
	;
server_module_conf: VAR_MODULE_CONF STRING_ARG
	{
		OUTYY(("P(server_module_conf:%s)\n", $2));
		free(cfg_parser->cfg->module_conf);
		cfg_parser->cfg->module_conf = $2;
	}
	;
server_val_override_date: VAR_VAL_OVERRIDE_DATE STRING_ARG
	{
		OUTYY(("P(server_val_override_date:%s)\n", $2));
		if(*$2 == '\0' || strcmp($2, "0") == 0) {
			cfg_parser->cfg->val_date_override = 0;
		} else if(strlen($2) == 14) {
			cfg_parser->cfg->val_date_override =
				cfg_convert_timeval($2);
			if(!cfg_parser->cfg->val_date_override)
				yyerror("bad date/time specification");
		} else {
			if(atoi($2) == 0)
				yyerror("number expected");
			cfg_parser->cfg->val_date_override = atoi($2);
		}
		free($2);
	}
	;
server_val_sig_skew_min: VAR_VAL_SIG_SKEW_MIN STRING_ARG
	{
		OUTYY(("P(server_val_sig_skew_min:%s)\n", $2));
		if(*$2 == '\0' || strcmp($2, "0") == 0) {
			cfg_parser->cfg->val_sig_skew_min = 0;
		} else {
			cfg_parser->cfg->val_sig_skew_min = atoi($2);
			if(!cfg_parser->cfg->val_sig_skew_min)
				yyerror("number expected");
		}
		free($2);
	}
	;
server_val_sig_skew_max: VAR_VAL_SIG_SKEW_MAX STRING_ARG
	{
		OUTYY(("P(server_val_sig_skew_max:%s)\n", $2));
		if(*$2 == '\0' || strcmp($2, "0") == 0) {
			cfg_parser->cfg->val_sig_skew_max = 0;
		} else {
			cfg_parser->cfg->val_sig_skew_max = atoi($2);
			if(!cfg_parser->cfg->val_sig_skew_max)
				yyerror("number expected");
		}
		free($2);
	}
	;
server_val_max_restart: VAR_VAL_MAX_RESTART STRING_ARG
	{
		OUTYY(("P(server_val_max_restart:%s)\n", $2));
		if(*$2 == '\0' || strcmp($2, "0") == 0) {
			cfg_parser->cfg->val_max_restart = 0;
		} else {
			cfg_parser->cfg->val_max_restart = atoi($2);
			if(!cfg_parser->cfg->val_max_restart)
				yyerror("number expected");
		}
		free($2);
	}
	;
server_cache_max_ttl: VAR_CACHE_MAX_TTL STRING_ARG
	{
		OUTYY(("P(server_cache_max_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->max_ttl = atoi($2);
		free($2);
	}
	;
server_cache_max_negative_ttl: VAR_CACHE_MAX_NEGATIVE_TTL STRING_ARG
	{
		OUTYY(("P(server_cache_max_negative_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->max_negative_ttl = atoi($2);
		free($2);
	}
	;
server_cache_min_negative_ttl: VAR_CACHE_MIN_NEGATIVE_TTL STRING_ARG
	{
		OUTYY(("P(server_cache_min_negative_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->min_negative_ttl = atoi($2);
		free($2);
	}
	;
server_cache_min_ttl: VAR_CACHE_MIN_TTL STRING_ARG
	{
		OUTYY(("P(server_cache_min_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->min_ttl = atoi($2);
		free($2);
	}
	;
server_bogus_ttl: VAR_BOGUS_TTL STRING_ARG
	{
		OUTYY(("P(server_bogus_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->bogus_ttl = atoi($2);
		free($2);
	}
	;
server_val_clean_additional: VAR_VAL_CLEAN_ADDITIONAL STRING_ARG
	{
		OUTYY(("P(server_val_clean_additional:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->val_clean_additional =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_val_permissive_mode: VAR_VAL_PERMISSIVE_MODE STRING_ARG
	{
		OUTYY(("P(server_val_permissive_mode:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->val_permissive_mode =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_aggressive_nsec: VAR_AGGRESSIVE_NSEC STRING_ARG
	{
		OUTYY(("P(server_aggressive_nsec:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else
			cfg_parser->cfg->aggressive_nsec =
				(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_ignore_cd_flag: VAR_IGNORE_CD_FLAG STRING_ARG
	{
		OUTYY(("P(server_ignore_cd_flag:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ignore_cd = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_disable_edns_do: VAR_DISABLE_EDNS_DO STRING_ARG
	{
		OUTYY(("P(server_disable_edns_do:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->disable_edns_do = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_serve_expired: VAR_SERVE_EXPIRED STRING_ARG
	{
		OUTYY(("P(server_serve_expired:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->serve_expired = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_serve_expired_ttl: VAR_SERVE_EXPIRED_TTL STRING_ARG
	{
		OUTYY(("P(server_serve_expired_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->serve_expired_ttl = atoi($2);
		free($2);
	}
	;
server_serve_expired_ttl_reset: VAR_SERVE_EXPIRED_TTL_RESET STRING_ARG
	{
		OUTYY(("P(server_serve_expired_ttl_reset:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->serve_expired_ttl_reset = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_serve_expired_reply_ttl: VAR_SERVE_EXPIRED_REPLY_TTL STRING_ARG
	{
		OUTYY(("P(server_serve_expired_reply_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->serve_expired_reply_ttl = atoi($2);
		free($2);
	}
	;
server_serve_expired_client_timeout: VAR_SERVE_EXPIRED_CLIENT_TIMEOUT STRING_ARG
	{
		OUTYY(("P(server_serve_expired_client_timeout:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->serve_expired_client_timeout = atoi($2);
		free($2);
	}
	;
server_ede_serve_expired: VAR_EDE_SERVE_EXPIRED STRING_ARG
	{
		OUTYY(("P(server_ede_serve_expired:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ede_serve_expired = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_serve_original_ttl: VAR_SERVE_ORIGINAL_TTL STRING_ARG
	{
		OUTYY(("P(server_serve_original_ttl:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->serve_original_ttl = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_fake_dsa: VAR_FAKE_DSA STRING_ARG
	{
		OUTYY(("P(server_fake_dsa:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
#if defined(HAVE_SSL) || defined(HAVE_NETTLE)
		else fake_dsa = (strcmp($2, "yes")==0);
		if(fake_dsa)
			log_warn("test option fake_dsa is enabled");
#endif
		free($2);
	}
	;
server_fake_sha1: VAR_FAKE_SHA1 STRING_ARG
	{
		OUTYY(("P(server_fake_sha1:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
#if defined(HAVE_SSL) || defined(HAVE_NETTLE)
		else fake_sha1 = (strcmp($2, "yes")==0);
		if(fake_sha1)
			log_warn("test option fake_sha1 is enabled");
#endif
		free($2);
	}
	;
server_val_log_level: VAR_VAL_LOG_LEVEL STRING_ARG
	{
		OUTYY(("P(server_val_log_level:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->val_log_level = atoi($2);
		free($2);
	}
	;
server_val_nsec3_keysize_iterations: VAR_VAL_NSEC3_KEYSIZE_ITERATIONS STRING_ARG
	{
		OUTYY(("P(server_val_nsec3_keysize_iterations:%s)\n", $2));
		free(cfg_parser->cfg->val_nsec3_key_iterations);
		cfg_parser->cfg->val_nsec3_key_iterations = $2;
	}
	;
server_zonemd_permissive_mode: VAR_ZONEMD_PERMISSIVE_MODE STRING_ARG
	{
		OUTYY(("P(server_zonemd_permissive_mode:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else	cfg_parser->cfg->zonemd_permissive_mode = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_add_holddown: VAR_ADD_HOLDDOWN STRING_ARG
	{
		OUTYY(("P(server_add_holddown:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->add_holddown = atoi($2);
		free($2);
	}
	;
server_del_holddown: VAR_DEL_HOLDDOWN STRING_ARG
	{
		OUTYY(("P(server_del_holddown:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->del_holddown = atoi($2);
		free($2);
	}
	;
server_keep_missing: VAR_KEEP_MISSING STRING_ARG
	{
		OUTYY(("P(server_keep_missing:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->keep_missing = atoi($2);
		free($2);
	}
	;
server_permit_small_holddown: VAR_PERMIT_SMALL_HOLDDOWN STRING_ARG
	{
		OUTYY(("P(server_permit_small_holddown:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->permit_small_holddown =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_key_cache_size: VAR_KEY_CACHE_SIZE STRING_ARG
	{
		OUTYY(("P(server_key_cache_size:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->key_cache_size))
			yyerror("memory size expected");
		free($2);
	}
	;
server_key_cache_slabs: VAR_KEY_CACHE_SLABS STRING_ARG
	{
		OUTYY(("P(server_key_cache_slabs:%s)\n", $2));
		if(atoi($2) == 0) {
			yyerror("number expected");
		} else {
			cfg_parser->cfg->key_cache_slabs = atoi($2);
			if(!is_pow2(cfg_parser->cfg->key_cache_slabs))
				yyerror("must be a power of 2");
		}
		free($2);
	}
	;
server_neg_cache_size: VAR_NEG_CACHE_SIZE STRING_ARG
	{
		OUTYY(("P(server_neg_cache_size:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->neg_cache_size))
			yyerror("memory size expected");
		free($2);
	}
	;
server_local_zone: VAR_LOCAL_ZONE STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_local_zone:%s %s)\n", $2, $3));
		if(strcmp($3, "static")!=0 && strcmp($3, "deny")!=0 &&
		   strcmp($3, "refuse")!=0 && strcmp($3, "redirect")!=0 &&
		   strcmp($3, "transparent")!=0 && strcmp($3, "nodefault")!=0
		   && strcmp($3, "typetransparent")!=0
		   && strcmp($3, "always_transparent")!=0
		   && strcmp($3, "block_a")!=0
		   && strcmp($3, "always_refuse")!=0
		   && strcmp($3, "always_nxdomain")!=0
		   && strcmp($3, "always_nodata")!=0
		   && strcmp($3, "always_deny")!=0
		   && strcmp($3, "always_null")!=0
		   && strcmp($3, "noview")!=0
		   && strcmp($3, "inform")!=0 && strcmp($3, "inform_deny")!=0
		   && strcmp($3, "inform_redirect") != 0
		   && strcmp($3, "ipset") != 0) {
			yyerror("local-zone type: expected static, deny, "
				"refuse, redirect, transparent, "
				"typetransparent, inform, inform_deny, "
				"inform_redirect, always_transparent, block_a,"
				"always_refuse, always_nxdomain, "
				"always_nodata, always_deny, always_null, "
				"noview, nodefault or ipset");
			free($2);
			free($3);
		} else if(strcmp($3, "nodefault")==0) {
			if(!cfg_strlist_insert(&cfg_parser->cfg->
				local_zones_nodefault, $2))
				fatal_exit("out of memory adding local-zone");
			free($3);
#ifdef USE_IPSET
		} else if(strcmp($3, "ipset")==0) {
			size_t len = strlen($2);
			/* Make sure to add the trailing dot.
			 * These are str compared to domain names. */
			if($2[len-1] != '.') {
				if(!($2 = realloc($2, len+2))) {
					fatal_exit("out of memory adding local-zone");
				}
				$2[len] = '.';
				$2[len+1] = 0;
			}
			if(!cfg_strlist_insert(&cfg_parser->cfg->
				local_zones_ipset, $2))
				fatal_exit("out of memory adding local-zone");
			free($3);
#endif
		} else {
			if(!cfg_str2list_insert(&cfg_parser->cfg->local_zones,
				$2, $3))
				fatal_exit("out of memory adding local-zone");
		}
	}
	;
server_local_data: VAR_LOCAL_DATA STRING_ARG
	{
		OUTYY(("P(server_local_data:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->local_data, $2))
			fatal_exit("out of memory adding local-data");
	}
	;
server_local_data_ptr: VAR_LOCAL_DATA_PTR STRING_ARG
	{
		char* ptr;
		OUTYY(("P(server_local_data_ptr:%s)\n", $2));
		ptr = cfg_ptr_reverse($2);
		free($2);
		if(ptr) {
			if(!cfg_strlist_insert(&cfg_parser->cfg->
				local_data, ptr))
				fatal_exit("out of memory adding local-data");
		} else {
			yyerror("local-data-ptr could not be reversed");
		}
	}
	;
server_minimal_responses: VAR_MINIMAL_RESPONSES STRING_ARG
	{
		OUTYY(("P(server_minimal_responses:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->minimal_responses =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_rrset_roundrobin: VAR_RRSET_ROUNDROBIN STRING_ARG
	{
		OUTYY(("P(server_rrset_roundrobin:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->rrset_roundrobin =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_unknown_server_time_limit: VAR_UNKNOWN_SERVER_TIME_LIMIT STRING_ARG
	{
		OUTYY(("P(server_unknown_server_time_limit:%s)\n", $2));
		cfg_parser->cfg->unknown_server_time_limit = atoi($2);
		free($2);
	}
	;
server_discard_timeout: VAR_DISCARD_TIMEOUT STRING_ARG
	{
		OUTYY(("P(server_discard_timeout:%s)\n", $2));
		cfg_parser->cfg->discard_timeout = atoi($2);
		free($2);
	}
	;
server_wait_limit: VAR_WAIT_LIMIT STRING_ARG
	{
		OUTYY(("P(server_wait_limit:%s)\n", $2));
		cfg_parser->cfg->wait_limit = atoi($2);
		free($2);
	}
	;
server_wait_limit_cookie: VAR_WAIT_LIMIT_COOKIE STRING_ARG
	{
		OUTYY(("P(server_wait_limit_cookie:%s)\n", $2));
		cfg_parser->cfg->wait_limit_cookie = atoi($2);
		free($2);
	}
	;
server_wait_limit_netblock: VAR_WAIT_LIMIT_NETBLOCK STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_wait_limit_netblock:%s %s)\n", $2, $3));
		if(atoi($3) == 0 && strcmp($3, "0") != 0) {
			yyerror("number expected");
			free($2);
			free($3);
		} else {
			if(!cfg_str2list_insert(&cfg_parser->cfg->
				wait_limit_netblock, $2, $3))
				fatal_exit("out of memory adding "
					"wait-limit-netblock");
		}
	}
	;
server_wait_limit_cookie_netblock: VAR_WAIT_LIMIT_COOKIE_NETBLOCK STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_wait_limit_cookie_netblock:%s %s)\n", $2, $3));
		if(atoi($3) == 0 && strcmp($3, "0") != 0) {
			yyerror("number expected");
			free($2);
			free($3);
		} else {
			if(!cfg_str2list_insert(&cfg_parser->cfg->
				wait_limit_cookie_netblock, $2, $3))
				fatal_exit("out of memory adding "
					"wait-limit-cookie-netblock");
		}
	}
	;
server_max_udp_size: VAR_MAX_UDP_SIZE STRING_ARG
	{
		OUTYY(("P(server_max_udp_size:%s)\n", $2));
		cfg_parser->cfg->max_udp_size = atoi($2);
		free($2);
	}
	;
server_dns64_prefix: VAR_DNS64_PREFIX STRING_ARG
	{
		OUTYY(("P(dns64_prefix:%s)\n", $2));
		free(cfg_parser->cfg->dns64_prefix);
		cfg_parser->cfg->dns64_prefix = $2;
	}
	;
server_dns64_synthall: VAR_DNS64_SYNTHALL STRING_ARG
	{
		OUTYY(("P(server_dns64_synthall:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dns64_synthall = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_dns64_ignore_aaaa: VAR_DNS64_IGNORE_AAAA STRING_ARG
	{
		OUTYY(("P(dns64_ignore_aaaa:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->dns64_ignore_aaaa,
			$2))
			fatal_exit("out of memory adding dns64-ignore-aaaa");
	}
	;
server_nat64_prefix: VAR_NAT64_PREFIX STRING_ARG
	{
		OUTYY(("P(nat64_prefix:%s)\n", $2));
		free(cfg_parser->cfg->nat64_prefix);
		cfg_parser->cfg->nat64_prefix = $2;
	}
	;
server_define_tag: VAR_DEFINE_TAG STRING_ARG
	{
		char* p, *s = $2;
		OUTYY(("P(server_define_tag:%s)\n", $2));
		while((p=strsep(&s, " \t\n")) != NULL) {
			if(*p) {
				if(!config_add_tag(cfg_parser->cfg, p))
					yyerror("could not define-tag, "
						"out of memory");
			}
		}
		free($2);
	}
	;
server_local_zone_tag: VAR_LOCAL_ZONE_TAG STRING_ARG STRING_ARG
	{
		size_t len = 0;
		uint8_t* bitlist = config_parse_taglist(cfg_parser->cfg, $3,
			&len);
		free($3);
		OUTYY(("P(server_local_zone_tag:%s)\n", $2));
		if(!bitlist) {
			yyerror("could not parse tags, (define-tag them first)");
			free($2);
		}
		if(bitlist) {
			if(!cfg_strbytelist_insert(
				&cfg_parser->cfg->local_zone_tags,
				$2, bitlist, len)) {
				yyerror("out of memory");
				free($2);
			}
		}
	}
	;
server_access_control_tag: VAR_ACCESS_CONTROL_TAG STRING_ARG STRING_ARG
	{
		size_t len = 0;
		uint8_t* bitlist = config_parse_taglist(cfg_parser->cfg, $3,
			&len);
		free($3);
		OUTYY(("P(server_access_control_tag:%s)\n", $2));
		if(!bitlist) {
			yyerror("could not parse tags, (define-tag them first)");
			free($2);
		}
		if(bitlist) {
			if(!cfg_strbytelist_insert(
				&cfg_parser->cfg->acl_tags,
				$2, bitlist, len)) {
				yyerror("out of memory");
				free($2);
			}
		}
	}
	;
server_access_control_tag_action: VAR_ACCESS_CONTROL_TAG_ACTION STRING_ARG STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_access_control_tag_action:%s %s %s)\n", $2, $3, $4));
		if(!cfg_str3list_insert(&cfg_parser->cfg->acl_tag_actions,
			$2, $3, $4)) {
			yyerror("out of memory");
			free($2);
			free($3);
			free($4);
		}
	}
	;
server_access_control_tag_data: VAR_ACCESS_CONTROL_TAG_DATA STRING_ARG STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_access_control_tag_data:%s %s %s)\n", $2, $3, $4));
		if(!cfg_str3list_insert(&cfg_parser->cfg->acl_tag_datas,
			$2, $3, $4)) {
			yyerror("out of memory");
			free($2);
			free($3);
			free($4);
		}
	}
	;
server_local_zone_override: VAR_LOCAL_ZONE_OVERRIDE STRING_ARG STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_local_zone_override:%s %s %s)\n", $2, $3, $4));
		if(!cfg_str3list_insert(&cfg_parser->cfg->local_zone_overrides,
			$2, $3, $4)) {
			yyerror("out of memory");
			free($2);
			free($3);
			free($4);
		}
	}
	;
server_access_control_view: VAR_ACCESS_CONTROL_VIEW STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_access_control_view:%s %s)\n", $2, $3));
		if(!cfg_str2list_insert(&cfg_parser->cfg->acl_view,
			$2, $3)) {
			yyerror("out of memory");
		}
	}
	;
server_interface_tag: VAR_INTERFACE_TAG STRING_ARG STRING_ARG
	{
		size_t len = 0;
		uint8_t* bitlist = config_parse_taglist(cfg_parser->cfg, $3,
			&len);
		free($3);
		OUTYY(("P(server_interface_tag:%s)\n", $2));
		if(!bitlist) {
			yyerror("could not parse tags, (define-tag them first)");
			free($2);
		}
		if(bitlist) {
			if(!cfg_strbytelist_insert(
				&cfg_parser->cfg->interface_tags,
				$2, bitlist, len)) {
				yyerror("out of memory");
				free($2);
			}
		}
	}
	;
server_interface_tag_action: VAR_INTERFACE_TAG_ACTION STRING_ARG STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_interface_tag_action:%s %s %s)\n", $2, $3, $4));
		if(!cfg_str3list_insert(&cfg_parser->cfg->interface_tag_actions,
			$2, $3, $4)) {
			yyerror("out of memory");
			free($2);
			free($3);
			free($4);
		}
	}
	;
server_interface_tag_data: VAR_INTERFACE_TAG_DATA STRING_ARG STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_interface_tag_data:%s %s %s)\n", $2, $3, $4));
		if(!cfg_str3list_insert(&cfg_parser->cfg->interface_tag_datas,
			$2, $3, $4)) {
			yyerror("out of memory");
			free($2);
			free($3);
			free($4);
		}
	}
	;
server_interface_view: VAR_INTERFACE_VIEW STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_interface_view:%s %s)\n", $2, $3));
		if(!cfg_str2list_insert(&cfg_parser->cfg->interface_view,
			$2, $3)) {
			yyerror("out of memory");
		}
	}
	;
server_response_ip_tag: VAR_RESPONSE_IP_TAG STRING_ARG STRING_ARG
	{
		size_t len = 0;
		uint8_t* bitlist = config_parse_taglist(cfg_parser->cfg, $3,
			&len);
		free($3);
		OUTYY(("P(response_ip_tag:%s)\n", $2));
		if(!bitlist) {
			yyerror("could not parse tags, (define-tag them first)");
			free($2);
		}
		if(bitlist) {
			if(!cfg_strbytelist_insert(
				&cfg_parser->cfg->respip_tags,
				$2, bitlist, len)) {
				yyerror("out of memory");
				free($2);
			}
		}
	}
	;
server_ip_ratelimit: VAR_IP_RATELIMIT STRING_ARG
	{
		OUTYY(("P(server_ip_ratelimit:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->ip_ratelimit = atoi($2);
		free($2);
	}
	;
server_ip_ratelimit_cookie: VAR_IP_RATELIMIT_COOKIE STRING_ARG
	{
		OUTYY(("P(server_ip_ratelimit_cookie:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->ip_ratelimit_cookie = atoi($2);
		free($2);
	}
	;
server_ratelimit: VAR_RATELIMIT STRING_ARG
	{
		OUTYY(("P(server_ratelimit:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->ratelimit = atoi($2);
		free($2);
	}
	;
server_ip_ratelimit_size: VAR_IP_RATELIMIT_SIZE STRING_ARG
	{
		OUTYY(("P(server_ip_ratelimit_size:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->ip_ratelimit_size))
			yyerror("memory size expected");
		free($2);
	}
	;
server_ratelimit_size: VAR_RATELIMIT_SIZE STRING_ARG
	{
		OUTYY(("P(server_ratelimit_size:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->ratelimit_size))
			yyerror("memory size expected");
		free($2);
	}
	;
server_ip_ratelimit_slabs: VAR_IP_RATELIMIT_SLABS STRING_ARG
	{
		OUTYY(("P(server_ip_ratelimit_slabs:%s)\n", $2));
		if(atoi($2) == 0) {
			yyerror("number expected");
		} else {
			cfg_parser->cfg->ip_ratelimit_slabs = atoi($2);
			if(!is_pow2(cfg_parser->cfg->ip_ratelimit_slabs))
				yyerror("must be a power of 2");
		}
		free($2);
	}
	;
server_ratelimit_slabs: VAR_RATELIMIT_SLABS STRING_ARG
	{
		OUTYY(("P(server_ratelimit_slabs:%s)\n", $2));
		if(atoi($2) == 0) {
			yyerror("number expected");
		} else {
			cfg_parser->cfg->ratelimit_slabs = atoi($2);
			if(!is_pow2(cfg_parser->cfg->ratelimit_slabs))
				yyerror("must be a power of 2");
		}
		free($2);
	}
	;
server_ratelimit_for_domain: VAR_RATELIMIT_FOR_DOMAIN STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_ratelimit_for_domain:%s %s)\n", $2, $3));
		if(atoi($3) == 0 && strcmp($3, "0") != 0) {
			yyerror("number expected");
			free($2);
			free($3);
		} else {
			if(!cfg_str2list_insert(&cfg_parser->cfg->
				ratelimit_for_domain, $2, $3))
				fatal_exit("out of memory adding "
					"ratelimit-for-domain");
		}
	}
	;
server_ratelimit_below_domain: VAR_RATELIMIT_BELOW_DOMAIN STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_ratelimit_below_domain:%s %s)\n", $2, $3));
		if(atoi($3) == 0 && strcmp($3, "0") != 0) {
			yyerror("number expected");
			free($2);
			free($3);
		} else {
			if(!cfg_str2list_insert(&cfg_parser->cfg->
				ratelimit_below_domain, $2, $3))
				fatal_exit("out of memory adding "
					"ratelimit-below-domain");
		}
	}
	;
server_ip_ratelimit_factor: VAR_IP_RATELIMIT_FACTOR STRING_ARG
	{
		OUTYY(("P(server_ip_ratelimit_factor:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->ip_ratelimit_factor = atoi($2);
		free($2);
	}
	;
server_ratelimit_factor: VAR_RATELIMIT_FACTOR STRING_ARG
	{
		OUTYY(("P(server_ratelimit_factor:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->ratelimit_factor = atoi($2);
		free($2);
	}
	;
server_ip_ratelimit_backoff: VAR_IP_RATELIMIT_BACKOFF STRING_ARG
	{
		OUTYY(("P(server_ip_ratelimit_backoff:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ip_ratelimit_backoff =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_ratelimit_backoff: VAR_RATELIMIT_BACKOFF STRING_ARG
	{
		OUTYY(("P(server_ratelimit_backoff:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ratelimit_backoff =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_outbound_msg_retry: VAR_OUTBOUND_MSG_RETRY STRING_ARG
	{
		OUTYY(("P(server_outbound_msg_retry:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->outbound_msg_retry = atoi($2);
		free($2);
	}
	;
server_max_sent_count: VAR_MAX_SENT_COUNT STRING_ARG
	{
		OUTYY(("P(server_max_sent_count:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->max_sent_count = atoi($2);
		free($2);
	}
	;
server_max_query_restarts: VAR_MAX_QUERY_RESTARTS STRING_ARG
	{
		OUTYY(("P(server_max_query_restarts:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->max_query_restarts = atoi($2);
		free($2);
	}
	;
server_low_rtt: VAR_LOW_RTT STRING_ARG
	{
		OUTYY(("P(low-rtt option is deprecated, use fast-server-num instead)\n"));
		free($2);
	}
	;
server_fast_server_num: VAR_FAST_SERVER_NUM STRING_ARG
	{
		OUTYY(("P(server_fast_server_num:%s)\n", $2));
		if(atoi($2) <= 0)
			yyerror("number expected");
		else cfg_parser->cfg->fast_server_num = atoi($2);
		free($2);
	}
	;
server_fast_server_permil: VAR_FAST_SERVER_PERMIL STRING_ARG
	{
		OUTYY(("P(server_fast_server_permil:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->fast_server_permil = atoi($2);
		free($2);
	}
	;
server_qname_minimisation: VAR_QNAME_MINIMISATION STRING_ARG
	{
		OUTYY(("P(server_qname_minimisation:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->qname_minimisation =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_qname_minimisation_strict: VAR_QNAME_MINIMISATION_STRICT STRING_ARG
	{
		OUTYY(("P(server_qname_minimisation_strict:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->qname_minimisation_strict =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_pad_responses: VAR_PAD_RESPONSES STRING_ARG
	{
		OUTYY(("P(server_pad_responses:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->pad_responses =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_pad_responses_block_size: VAR_PAD_RESPONSES_BLOCK_SIZE STRING_ARG
	{
		OUTYY(("P(server_pad_responses_block_size:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else cfg_parser->cfg->pad_responses_block_size = atoi($2);
		free($2);
	}
	;
server_pad_queries: VAR_PAD_QUERIES STRING_ARG
	{
		OUTYY(("P(server_pad_queries:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->pad_queries =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_pad_queries_block_size: VAR_PAD_QUERIES_BLOCK_SIZE STRING_ARG
	{
		OUTYY(("P(server_pad_queries_block_size:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else cfg_parser->cfg->pad_queries_block_size = atoi($2);
		free($2);
	}
	;
server_ipsecmod_enabled: VAR_IPSECMOD_ENABLED STRING_ARG
	{
	#ifdef USE_IPSECMOD
		OUTYY(("P(server_ipsecmod_enabled:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ipsecmod_enabled = (strcmp($2, "yes")==0);
	#else
		OUTYY(("P(Compiled without IPsec module, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_ipsecmod_ignore_bogus: VAR_IPSECMOD_IGNORE_BOGUS STRING_ARG
	{
	#ifdef USE_IPSECMOD
		OUTYY(("P(server_ipsecmod_ignore_bogus:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ipsecmod_ignore_bogus = (strcmp($2, "yes")==0);
	#else
		OUTYY(("P(Compiled without IPsec module, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_ipsecmod_hook: VAR_IPSECMOD_HOOK STRING_ARG
	{
	#ifdef USE_IPSECMOD
		OUTYY(("P(server_ipsecmod_hook:%s)\n", $2));
		free(cfg_parser->cfg->ipsecmod_hook);
		cfg_parser->cfg->ipsecmod_hook = $2;
	#else
		OUTYY(("P(Compiled without IPsec module, ignoring)\n"));
		free($2);
	#endif
	}
	;
server_ipsecmod_max_ttl: VAR_IPSECMOD_MAX_TTL STRING_ARG
	{
	#ifdef USE_IPSECMOD
		OUTYY(("P(server_ipsecmod_max_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->ipsecmod_max_ttl = atoi($2);
		free($2);
	#else
		OUTYY(("P(Compiled without IPsec module, ignoring)\n"));
		free($2);
	#endif
	}
	;
server_ipsecmod_whitelist: VAR_IPSECMOD_WHITELIST STRING_ARG
	{
	#ifdef USE_IPSECMOD
		OUTYY(("P(server_ipsecmod_whitelist:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->ipsecmod_whitelist, $2))
			yyerror("out of memory");
	#else
		OUTYY(("P(Compiled without IPsec module, ignoring)\n"));
		free($2);
	#endif
	}
	;
server_ipsecmod_strict: VAR_IPSECMOD_STRICT STRING_ARG
	{
	#ifdef USE_IPSECMOD
		OUTYY(("P(server_ipsecmod_strict:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ipsecmod_strict = (strcmp($2, "yes")==0);
		free($2);
	#else
		OUTYY(("P(Compiled without IPsec module, ignoring)\n"));
		free($2);
	#endif
	}
	;
server_edns_client_string: VAR_EDNS_CLIENT_STRING STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_edns_client_string:%s %s)\n", $2, $3));
		if(!cfg_str2list_insert(
			&cfg_parser->cfg->edns_client_strings, $2, $3))
			fatal_exit("out of memory adding "
				"edns-client-string");
	}
	;
server_edns_client_string_opcode: VAR_EDNS_CLIENT_STRING_OPCODE STRING_ARG
	{
		OUTYY(("P(edns_client_string_opcode:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("option code expected");
		else if(atoi($2) > 65535 || atoi($2) < 0)
			yyerror("option code must be in interval [0, 65535]");
		else cfg_parser->cfg->edns_client_string_opcode = atoi($2);
		free($2);
	}
	;
server_ede: VAR_EDE STRING_ARG
	{
		OUTYY(("P(server_ede:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ede = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_dns_error_reporting: VAR_DNS_ERROR_REPORTING STRING_ARG
	{
		OUTYY(("P(server_dns_error_reporting:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dns_error_reporting = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_proxy_protocol_port: VAR_PROXY_PROTOCOL_PORT STRING_ARG
	{
		OUTYY(("P(server_proxy_protocol_port:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->proxy_protocol_port, $2))
			yyerror("out of memory");
	}
	;
stub_name: VAR_NAME STRING_ARG
	{
		OUTYY(("P(name:%s)\n", $2));
		if(cfg_parser->cfg->stubs->name)
			yyerror("stub name override, there must be one name "
				"for one stub-zone");
		free(cfg_parser->cfg->stubs->name);
		cfg_parser->cfg->stubs->name = $2;
	}
	;
stub_host: VAR_STUB_HOST STRING_ARG
	{
		OUTYY(("P(stub-host:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->stubs->hosts, $2))
			yyerror("out of memory");
	}
	;
stub_addr: VAR_STUB_ADDR STRING_ARG
	{
		OUTYY(("P(stub-addr:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->stubs->addrs, $2))
			yyerror("out of memory");
	}
	;
stub_first: VAR_STUB_FIRST STRING_ARG
	{
		OUTYY(("P(stub-first:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->stubs->isfirst=(strcmp($2, "yes")==0);
		free($2);
	}
	;
stub_no_cache: VAR_STUB_NO_CACHE STRING_ARG
	{
		OUTYY(("P(stub-no-cache:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->stubs->no_cache=(strcmp($2, "yes")==0);
		free($2);
	}
	;
stub_ssl_upstream: VAR_STUB_SSL_UPSTREAM STRING_ARG
	{
		OUTYY(("P(stub-ssl-upstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->stubs->ssl_upstream =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
stub_tcp_upstream: VAR_STUB_TCP_UPSTREAM STRING_ARG
        {
                OUTYY(("P(stub-tcp-upstream:%s)\n", $2));
                if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
                        yyerror("expected yes or no.");
                else cfg_parser->cfg->stubs->tcp_upstream =
                        (strcmp($2, "yes")==0);
                free($2);
        }
        ;
stub_prime: VAR_STUB_PRIME STRING_ARG
	{
		OUTYY(("P(stub-prime:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->stubs->isprime =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
forward_name: VAR_NAME STRING_ARG
	{
		OUTYY(("P(name:%s)\n", $2));
		if(cfg_parser->cfg->forwards->name)
			yyerror("forward name override, there must be one "
				"name for one forward-zone");
		free(cfg_parser->cfg->forwards->name);
		cfg_parser->cfg->forwards->name = $2;
	}
	;
forward_host: VAR_FORWARD_HOST STRING_ARG
	{
		OUTYY(("P(forward-host:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->forwards->hosts, $2))
			yyerror("out of memory");
	}
	;
forward_addr: VAR_FORWARD_ADDR STRING_ARG
	{
		OUTYY(("P(forward-addr:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->forwards->addrs, $2))
			yyerror("out of memory");
	}
	;
forward_first: VAR_FORWARD_FIRST STRING_ARG
	{
		OUTYY(("P(forward-first:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->forwards->isfirst=(strcmp($2, "yes")==0);
		free($2);
	}
	;
forward_no_cache: VAR_FORWARD_NO_CACHE STRING_ARG
	{
		OUTYY(("P(forward-no-cache:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->forwards->no_cache=(strcmp($2, "yes")==0);
		free($2);
	}
	;
forward_ssl_upstream: VAR_FORWARD_SSL_UPSTREAM STRING_ARG
	{
		OUTYY(("P(forward-ssl-upstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->forwards->ssl_upstream =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
forward_tcp_upstream: VAR_FORWARD_TCP_UPSTREAM STRING_ARG
        {
                OUTYY(("P(forward-tcp-upstream:%s)\n", $2));
                if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
                        yyerror("expected yes or no.");
                else cfg_parser->cfg->forwards->tcp_upstream =
                        (strcmp($2, "yes")==0);
                free($2);
        }
        ;
auth_name: VAR_NAME STRING_ARG
	{
		OUTYY(("P(name:%s)\n", $2));
		if(cfg_parser->cfg->auths->name)
			yyerror("auth name override, there must be one name "
				"for one auth-zone");
		free(cfg_parser->cfg->auths->name);
		cfg_parser->cfg->auths->name = $2;
	}
	;
auth_zonefile: VAR_ZONEFILE STRING_ARG
	{
		OUTYY(("P(zonefile:%s)\n", $2));
		free(cfg_parser->cfg->auths->zonefile);
		cfg_parser->cfg->auths->zonefile = $2;
	}
	;
auth_master: VAR_MASTER STRING_ARG
	{
		OUTYY(("P(master:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->auths->masters, $2))
			yyerror("out of memory");
	}
	;
auth_url: VAR_URL STRING_ARG
	{
		OUTYY(("P(url:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->auths->urls, $2))
			yyerror("out of memory");
	}
	;
auth_allow_notify: VAR_ALLOW_NOTIFY STRING_ARG
	{
		OUTYY(("P(allow-notify:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->auths->allow_notify,
			$2))
			yyerror("out of memory");
	}
	;
auth_zonemd_check: VAR_ZONEMD_CHECK STRING_ARG
	{
		OUTYY(("P(zonemd-check:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->auths->zonemd_check =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
auth_zonemd_reject_absence: VAR_ZONEMD_REJECT_ABSENCE STRING_ARG
	{
		OUTYY(("P(zonemd-reject-absence:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->auths->zonemd_reject_absence =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
auth_for_downstream: VAR_FOR_DOWNSTREAM STRING_ARG
	{
		OUTYY(("P(for-downstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->auths->for_downstream =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
auth_for_upstream: VAR_FOR_UPSTREAM STRING_ARG
	{
		OUTYY(("P(for-upstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->auths->for_upstream =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
auth_fallback_enabled: VAR_FALLBACK_ENABLED STRING_ARG
	{
		OUTYY(("P(fallback-enabled:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->auths->fallback_enabled =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
view_name: VAR_NAME STRING_ARG
	{
		OUTYY(("P(name:%s)\n", $2));
		if(cfg_parser->cfg->views->name)
			yyerror("view name override, there must be one "
				"name for one view");
		free(cfg_parser->cfg->views->name);
		cfg_parser->cfg->views->name = $2;
	}
	;
view_local_zone: VAR_LOCAL_ZONE STRING_ARG STRING_ARG
	{
		OUTYY(("P(view_local_zone:%s %s)\n", $2, $3));
		if(strcmp($3, "static")!=0 && strcmp($3, "deny")!=0 &&
		   strcmp($3, "refuse")!=0 && strcmp($3, "redirect")!=0 &&
		   strcmp($3, "transparent")!=0 && strcmp($3, "nodefault")!=0
		   && strcmp($3, "typetransparent")!=0
		   && strcmp($3, "always_transparent")!=0
		   && strcmp($3, "always_refuse")!=0
		   && strcmp($3, "always_nxdomain")!=0
		   && strcmp($3, "always_nodata")!=0
		   && strcmp($3, "always_deny")!=0
		   && strcmp($3, "always_null")!=0
		   && strcmp($3, "noview")!=0
		   && strcmp($3, "inform")!=0 && strcmp($3, "inform_deny")!=0
		   && strcmp($3, "inform_redirect") != 0
		   && strcmp($3, "ipset") != 0) {
			yyerror("local-zone type: expected static, deny, "
				"refuse, redirect, transparent, "
				"typetransparent, inform, inform_deny, "
				"inform_redirect, always_transparent, "
				"always_refuse, always_nxdomain, "
				"always_nodata, always_deny, always_null, "
				"noview, nodefault or ipset");
			free($2);
			free($3);
		} else if(strcmp($3, "nodefault")==0) {
			if(!cfg_strlist_insert(&cfg_parser->cfg->views->
				local_zones_nodefault, $2))
				fatal_exit("out of memory adding local-zone");
			free($3);
#ifdef USE_IPSET
		} else if(strcmp($3, "ipset")==0) {
			size_t len = strlen($2);
			/* Make sure to add the trailing dot.
			 * These are str compared to domain names. */
			if($2[len-1] != '.') {
				if(!($2 = realloc($2, len+2))) {
					fatal_exit("out of memory adding local-zone");
				}
				$2[len] = '.';
				$2[len+1] = 0;
			}
			if(!cfg_strlist_insert(&cfg_parser->cfg->views->
				local_zones_ipset, $2))
				fatal_exit("out of memory adding local-zone");
			free($3);
#endif
		} else {
			if(!cfg_str2list_insert(
				&cfg_parser->cfg->views->local_zones,
				$2, $3))
				fatal_exit("out of memory adding local-zone");
		}
	}
	;
view_response_ip: VAR_RESPONSE_IP STRING_ARG STRING_ARG
	{
		OUTYY(("P(view_response_ip:%s %s)\n", $2, $3));
		validate_respip_action($3);
		if(!cfg_str2list_insert(
			&cfg_parser->cfg->views->respip_actions, $2, $3))
			fatal_exit("out of memory adding per-view "
				"response-ip action");
	}
	;
view_response_ip_data: VAR_RESPONSE_IP_DATA STRING_ARG STRING_ARG
	{
		OUTYY(("P(view_response_ip_data:%s)\n", $2));
		if(!cfg_str2list_insert(
			&cfg_parser->cfg->views->respip_data, $2, $3))
			fatal_exit("out of memory adding response-ip-data");
	}
	;
view_local_data: VAR_LOCAL_DATA STRING_ARG
	{
		OUTYY(("P(view_local_data:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->views->local_data, $2)) {
			fatal_exit("out of memory adding local-data");
		}
	}
	;
view_local_data_ptr: VAR_LOCAL_DATA_PTR STRING_ARG
	{
		char* ptr;
		OUTYY(("P(view_local_data_ptr:%s)\n", $2));
		ptr = cfg_ptr_reverse($2);
		free($2);
		if(ptr) {
			if(!cfg_strlist_insert(&cfg_parser->cfg->views->
				local_data, ptr))
				fatal_exit("out of memory adding local-data");
		} else {
			yyerror("local-data-ptr could not be reversed");
		}
	}
	;
view_first: VAR_VIEW_FIRST STRING_ARG
	{
		OUTYY(("P(view-first:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->views->isfirst=(strcmp($2, "yes")==0);
		free($2);
	}
	;
rcstart: VAR_REMOTE_CONTROL
	{
		OUTYY(("\nP(remote-control:)\n"));
		cfg_parser->started_toplevel = 1;
	}
	;
contents_rc: contents_rc content_rc
	| ;
content_rc: rc_control_enable | rc_control_interface | rc_control_port |
	rc_server_key_file | rc_server_cert_file | rc_control_key_file |
	rc_control_cert_file | rc_control_use_cert
	;
rc_control_enable: VAR_CONTROL_ENABLE STRING_ARG
	{
		OUTYY(("P(control_enable:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->remote_control_enable =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
rc_control_port: VAR_CONTROL_PORT STRING_ARG
	{
		OUTYY(("P(control_port:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("control port number expected");
		else cfg_parser->cfg->control_port = atoi($2);
		free($2);
	}
	;
rc_control_interface: VAR_CONTROL_INTERFACE STRING_ARG
	{
		OUTYY(("P(control_interface:%s)\n", $2));
		if(!cfg_strlist_append(&cfg_parser->cfg->control_ifs, $2))
			yyerror("out of memory");
	}
	;
rc_control_use_cert: VAR_CONTROL_USE_CERT STRING_ARG
	{
		OUTYY(("P(control_use_cert:%s)\n", $2));
		cfg_parser->cfg->control_use_cert = (strcmp($2, "yes")==0);
		free($2);
	}
	;
rc_server_key_file: VAR_SERVER_KEY_FILE STRING_ARG
	{
		OUTYY(("P(rc_server_key_file:%s)\n", $2));
		free(cfg_parser->cfg->server_key_file);
		cfg_parser->cfg->server_key_file = $2;
	}
	;
rc_server_cert_file: VAR_SERVER_CERT_FILE STRING_ARG
	{
		OUTYY(("P(rc_server_cert_file:%s)\n", $2));
		free(cfg_parser->cfg->server_cert_file);
		cfg_parser->cfg->server_cert_file = $2;
	}
	;
rc_control_key_file: VAR_CONTROL_KEY_FILE STRING_ARG
	{
		OUTYY(("P(rc_control_key_file:%s)\n", $2));
		free(cfg_parser->cfg->control_key_file);
		cfg_parser->cfg->control_key_file = $2;
	}
	;
rc_control_cert_file: VAR_CONTROL_CERT_FILE STRING_ARG
	{
		OUTYY(("P(rc_control_cert_file:%s)\n", $2));
		free(cfg_parser->cfg->control_cert_file);
		cfg_parser->cfg->control_cert_file = $2;
	}
	;
dtstart: VAR_DNSTAP
	{
		OUTYY(("\nP(dnstap:)\n"));
		cfg_parser->started_toplevel = 1;
	}
	;
contents_dt: contents_dt content_dt
	| ;
content_dt: dt_dnstap_enable | dt_dnstap_socket_path | dt_dnstap_bidirectional |
	dt_dnstap_ip | dt_dnstap_tls | dt_dnstap_tls_server_name |
	dt_dnstap_tls_cert_bundle |
	dt_dnstap_tls_client_key_file | dt_dnstap_tls_client_cert_file |
	dt_dnstap_send_identity | dt_dnstap_send_version |
	dt_dnstap_identity | dt_dnstap_version |
	dt_dnstap_log_resolver_query_messages |
	dt_dnstap_log_resolver_response_messages |
	dt_dnstap_log_client_query_messages |
	dt_dnstap_log_client_response_messages |
	dt_dnstap_log_forwarder_query_messages |
	dt_dnstap_log_forwarder_response_messages |
	dt_dnstap_sample_rate
	;
dt_dnstap_enable: VAR_DNSTAP_ENABLE STRING_ARG
	{
		OUTYY(("P(dt_dnstap_enable:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap = (strcmp($2, "yes")==0);
		free($2);
	}
	;
dt_dnstap_bidirectional: VAR_DNSTAP_BIDIRECTIONAL STRING_ARG
	{
		OUTYY(("P(dt_dnstap_bidirectional:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_bidirectional =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
dt_dnstap_socket_path: VAR_DNSTAP_SOCKET_PATH STRING_ARG
	{
		OUTYY(("P(dt_dnstap_socket_path:%s)\n", $2));
		free(cfg_parser->cfg->dnstap_socket_path);
		cfg_parser->cfg->dnstap_socket_path = $2;
	}
	;
dt_dnstap_ip: VAR_DNSTAP_IP STRING_ARG
	{
		OUTYY(("P(dt_dnstap_ip:%s)\n", $2));
		free(cfg_parser->cfg->dnstap_ip);
		cfg_parser->cfg->dnstap_ip = $2;
	}
	;
dt_dnstap_tls: VAR_DNSTAP_TLS STRING_ARG
	{
		OUTYY(("P(dt_dnstap_tls:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_tls = (strcmp($2, "yes")==0);
		free($2);
	}
	;
dt_dnstap_tls_server_name: VAR_DNSTAP_TLS_SERVER_NAME STRING_ARG
	{
		OUTYY(("P(dt_dnstap_tls_server_name:%s)\n", $2));
		free(cfg_parser->cfg->dnstap_tls_server_name);
		cfg_parser->cfg->dnstap_tls_server_name = $2;
	}
	;
dt_dnstap_tls_cert_bundle: VAR_DNSTAP_TLS_CERT_BUNDLE STRING_ARG
	{
		OUTYY(("P(dt_dnstap_tls_cert_bundle:%s)\n", $2));
		free(cfg_parser->cfg->dnstap_tls_cert_bundle);
		cfg_parser->cfg->dnstap_tls_cert_bundle = $2;
	}
	;
dt_dnstap_tls_client_key_file: VAR_DNSTAP_TLS_CLIENT_KEY_FILE STRING_ARG
	{
		OUTYY(("P(dt_dnstap_tls_client_key_file:%s)\n", $2));
		free(cfg_parser->cfg->dnstap_tls_client_key_file);
		cfg_parser->cfg->dnstap_tls_client_key_file = $2;
	}
	;
dt_dnstap_tls_client_cert_file: VAR_DNSTAP_TLS_CLIENT_CERT_FILE STRING_ARG
	{
		OUTYY(("P(dt_dnstap_tls_client_cert_file:%s)\n", $2));
		free(cfg_parser->cfg->dnstap_tls_client_cert_file);
		cfg_parser->cfg->dnstap_tls_client_cert_file = $2;
	}
	;
dt_dnstap_send_identity: VAR_DNSTAP_SEND_IDENTITY STRING_ARG
	{
		OUTYY(("P(dt_dnstap_send_identity:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_send_identity = (strcmp($2, "yes")==0);
		free($2);
	}
	;
dt_dnstap_send_version: VAR_DNSTAP_SEND_VERSION STRING_ARG
	{
		OUTYY(("P(dt_dnstap_send_version:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_send_version = (strcmp($2, "yes")==0);
		free($2);
	}
	;
dt_dnstap_identity: VAR_DNSTAP_IDENTITY STRING_ARG
	{
		OUTYY(("P(dt_dnstap_identity:%s)\n", $2));
		free(cfg_parser->cfg->dnstap_identity);
		cfg_parser->cfg->dnstap_identity = $2;
	}
	;
dt_dnstap_version: VAR_DNSTAP_VERSION STRING_ARG
	{
		OUTYY(("P(dt_dnstap_version:%s)\n", $2));
		free(cfg_parser->cfg->dnstap_version);
		cfg_parser->cfg->dnstap_version = $2;
	}
	;
dt_dnstap_log_resolver_query_messages: VAR_DNSTAP_LOG_RESOLVER_QUERY_MESSAGES STRING_ARG
	{
		OUTYY(("P(dt_dnstap_log_resolver_query_messages:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_log_resolver_query_messages =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
dt_dnstap_log_resolver_response_messages: VAR_DNSTAP_LOG_RESOLVER_RESPONSE_MESSAGES STRING_ARG
	{
		OUTYY(("P(dt_dnstap_log_resolver_response_messages:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_log_resolver_response_messages =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
dt_dnstap_log_client_query_messages: VAR_DNSTAP_LOG_CLIENT_QUERY_MESSAGES STRING_ARG
	{
		OUTYY(("P(dt_dnstap_log_client_query_messages:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_log_client_query_messages =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
dt_dnstap_log_client_response_messages: VAR_DNSTAP_LOG_CLIENT_RESPONSE_MESSAGES STRING_ARG
	{
		OUTYY(("P(dt_dnstap_log_client_response_messages:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_log_client_response_messages =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
dt_dnstap_log_forwarder_query_messages: VAR_DNSTAP_LOG_FORWARDER_QUERY_MESSAGES STRING_ARG
	{
		OUTYY(("P(dt_dnstap_log_forwarder_query_messages:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_log_forwarder_query_messages =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
dt_dnstap_log_forwarder_response_messages: VAR_DNSTAP_LOG_FORWARDER_RESPONSE_MESSAGES STRING_ARG
	{
		OUTYY(("P(dt_dnstap_log_forwarder_response_messages:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_log_forwarder_response_messages =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
dt_dnstap_sample_rate: VAR_DNSTAP_SAMPLE_RATE STRING_ARG
	{
		OUTYY(("P(dt_dnstap_sample_rate:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else if(atoi($2) < 0)
			yyerror("dnstap sample rate too small");
		else	cfg_parser->cfg->dnstap_sample_rate = atoi($2);
		free($2);
	}
	;
pythonstart: VAR_PYTHON
	{
		OUTYY(("\nP(python:)\n"));
		cfg_parser->started_toplevel = 1;
	}
	;
contents_py: contents_py content_py
	| ;
content_py: py_script
	;
py_script: VAR_PYTHON_SCRIPT STRING_ARG
	{
		OUTYY(("P(python-script:%s)\n", $2));
		if(!cfg_strlist_append_ex(&cfg_parser->cfg->python_script, $2))
			yyerror("out of memory");
	}
	;
dynlibstart: VAR_DYNLIB
	{
		OUTYY(("\nP(dynlib:)\n"));
		cfg_parser->started_toplevel = 1;
	}
	;
contents_dl: contents_dl content_dl
	| ;
content_dl: dl_file
	;
dl_file: VAR_DYNLIB_FILE STRING_ARG
	{
		OUTYY(("P(dynlib-file:%s)\n", $2));
		if(!cfg_strlist_append_ex(&cfg_parser->cfg->dynlib_file, $2))
			yyerror("out of memory");
	}
	;
server_disable_dnssec_lame_check: VAR_DISABLE_DNSSEC_LAME_CHECK STRING_ARG
	{
		OUTYY(("P(disable_dnssec_lame_check:%s)\n", $2));
		if (strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->disable_dnssec_lame_check =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_log_identity: VAR_LOG_IDENTITY STRING_ARG
	{
		OUTYY(("P(server_log_identity:%s)\n", $2));
		free(cfg_parser->cfg->log_identity);
		cfg_parser->cfg->log_identity = $2;
	}
	;
server_response_ip: VAR_RESPONSE_IP STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_response_ip:%s %s)\n", $2, $3));
		validate_respip_action($3);
		if(!cfg_str2list_insert(&cfg_parser->cfg->respip_actions,
			$2, $3))
			fatal_exit("out of memory adding response-ip");
	}
	;
server_response_ip_data: VAR_RESPONSE_IP_DATA STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_response_ip_data:%s)\n", $2));
		if(!cfg_str2list_insert(&cfg_parser->cfg->respip_data,
			$2, $3))
			fatal_exit("out of memory adding response-ip-data");
	}
	;
dnscstart: VAR_DNSCRYPT
	{
		OUTYY(("\nP(dnscrypt:)\n"));
		cfg_parser->started_toplevel = 1;
	}
	;
contents_dnsc: contents_dnsc content_dnsc
	| ;
content_dnsc:
	dnsc_dnscrypt_enable | dnsc_dnscrypt_port | dnsc_dnscrypt_provider |
	dnsc_dnscrypt_secret_key | dnsc_dnscrypt_provider_cert |
	dnsc_dnscrypt_provider_cert_rotated |
	dnsc_dnscrypt_shared_secret_cache_size |
	dnsc_dnscrypt_shared_secret_cache_slabs |
	dnsc_dnscrypt_nonce_cache_size |
	dnsc_dnscrypt_nonce_cache_slabs
	;
dnsc_dnscrypt_enable: VAR_DNSCRYPT_ENABLE STRING_ARG
	{
		OUTYY(("P(dnsc_dnscrypt_enable:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnscrypt = (strcmp($2, "yes")==0);
		free($2);
	}
	;
dnsc_dnscrypt_port: VAR_DNSCRYPT_PORT STRING_ARG
	{
		OUTYY(("P(dnsc_dnscrypt_port:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("port number expected");
		else cfg_parser->cfg->dnscrypt_port = atoi($2);
		free($2);
	}
	;
dnsc_dnscrypt_provider: VAR_DNSCRYPT_PROVIDER STRING_ARG
	{
		OUTYY(("P(dnsc_dnscrypt_provider:%s)\n", $2));
		free(cfg_parser->cfg->dnscrypt_provider);
		cfg_parser->cfg->dnscrypt_provider = $2;
	}
	;
dnsc_dnscrypt_provider_cert: VAR_DNSCRYPT_PROVIDER_CERT STRING_ARG
	{
		OUTYY(("P(dnsc_dnscrypt_provider_cert:%s)\n", $2));
		if(cfg_strlist_find(cfg_parser->cfg->dnscrypt_provider_cert, $2))
			log_warn("dnscrypt-provider-cert %s is a duplicate", $2);
		if(!cfg_strlist_insert(&cfg_parser->cfg->dnscrypt_provider_cert, $2))
			fatal_exit("out of memory adding dnscrypt-provider-cert");
	}
	;
dnsc_dnscrypt_provider_cert_rotated: VAR_DNSCRYPT_PROVIDER_CERT_ROTATED STRING_ARG
	{
		OUTYY(("P(dnsc_dnscrypt_provider_cert_rotated:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->dnscrypt_provider_cert_rotated, $2))
			fatal_exit("out of memory adding dnscrypt-provider-cert-rotated");
	}
	;
dnsc_dnscrypt_secret_key: VAR_DNSCRYPT_SECRET_KEY STRING_ARG
	{
		OUTYY(("P(dnsc_dnscrypt_secret_key:%s)\n", $2));
		if(cfg_strlist_find(cfg_parser->cfg->dnscrypt_secret_key, $2))
			log_warn("dnscrypt-secret-key: %s is a duplicate", $2);
		if(!cfg_strlist_insert(&cfg_parser->cfg->dnscrypt_secret_key, $2))
			fatal_exit("out of memory adding dnscrypt-secret-key");
	}
	;
dnsc_dnscrypt_shared_secret_cache_size: VAR_DNSCRYPT_SHARED_SECRET_CACHE_SIZE STRING_ARG
  {
	OUTYY(("P(dnscrypt_shared_secret_cache_size:%s)\n", $2));
	if(!cfg_parse_memsize($2, &cfg_parser->cfg->dnscrypt_shared_secret_cache_size))
		yyerror("memory size expected");
	free($2);
  }
  ;
dnsc_dnscrypt_shared_secret_cache_slabs: VAR_DNSCRYPT_SHARED_SECRET_CACHE_SLABS STRING_ARG
  {
	OUTYY(("P(dnscrypt_shared_secret_cache_slabs:%s)\n", $2));
	if(atoi($2) == 0) {
		yyerror("number expected");
	} else {
		cfg_parser->cfg->dnscrypt_shared_secret_cache_slabs = atoi($2);
		if(!is_pow2(cfg_parser->cfg->dnscrypt_shared_secret_cache_slabs))
			yyerror("must be a power of 2");
	}
	free($2);
  }
  ;
dnsc_dnscrypt_nonce_cache_size: VAR_DNSCRYPT_NONCE_CACHE_SIZE STRING_ARG
  {
	OUTYY(("P(dnscrypt_nonce_cache_size:%s)\n", $2));
	if(!cfg_parse_memsize($2, &cfg_parser->cfg->dnscrypt_nonce_cache_size))
		yyerror("memory size expected");
	free($2);
  }
  ;
dnsc_dnscrypt_nonce_cache_slabs: VAR_DNSCRYPT_NONCE_CACHE_SLABS STRING_ARG
  {
	OUTYY(("P(dnscrypt_nonce_cache_slabs:%s)\n", $2));
	if(atoi($2) == 0) {
		yyerror("number expected");
	} else {
		cfg_parser->cfg->dnscrypt_nonce_cache_slabs = atoi($2);
		if(!is_pow2(cfg_parser->cfg->dnscrypt_nonce_cache_slabs))
			yyerror("must be a power of 2");
	}
	free($2);
  }
  ;
cachedbstart: VAR_CACHEDB
	{
		OUTYY(("\nP(cachedb:)\n"));
		cfg_parser->started_toplevel = 1;
	}
	;
contents_cachedb: contents_cachedb content_cachedb
	| ;
content_cachedb: cachedb_backend_name | cachedb_secret_seed |
	redis_server_host | redis_replica_server_host |
	redis_server_port | redis_replica_server_port |
	redis_timeout | redis_replica_timeout |
	redis_command_timeout | redis_replica_command_timeout |
	redis_connect_timeout | redis_replica_connect_timeout |
	redis_server_path | redis_replica_server_path |
	redis_server_password | redis_replica_server_password |
	redis_logical_db | redis_replica_logical_db |
	cachedb_no_store | redis_expire_records |
	cachedb_check_when_serve_expired
	;
cachedb_backend_name: VAR_CACHEDB_BACKEND STRING_ARG
	{
	#ifdef USE_CACHEDB
		OUTYY(("P(backend:%s)\n", $2));
		free(cfg_parser->cfg->cachedb_backend);
		cfg_parser->cfg->cachedb_backend = $2;
	#else
		OUTYY(("P(Compiled without cachedb, ignoring)\n"));
		free($2);
	#endif
	}
	;
cachedb_secret_seed: VAR_CACHEDB_SECRETSEED STRING_ARG
	{
	#ifdef USE_CACHEDB
		OUTYY(("P(secret-seed:%s)\n", $2));
		free(cfg_parser->cfg->cachedb_secret);
		cfg_parser->cfg->cachedb_secret = $2;
	#else
		OUTYY(("P(Compiled without cachedb, ignoring)\n"));
		free($2);
	#endif
	}
	;
cachedb_no_store: VAR_CACHEDB_NO_STORE STRING_ARG
	{
	#ifdef USE_CACHEDB
		OUTYY(("P(cachedb_no_store:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->cachedb_no_store = (strcmp($2, "yes")==0);
	#else
		OUTYY(("P(Compiled without cachedb, ignoring)\n"));
	#endif
		free($2);
	}
	;
cachedb_check_when_serve_expired: VAR_CACHEDB_CHECK_WHEN_SERVE_EXPIRED STRING_ARG
	{
	#ifdef USE_CACHEDB
		OUTYY(("P(cachedb_check_when_serve_expired:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->cachedb_check_when_serve_expired = (strcmp($2, "yes")==0);
	#else
		OUTYY(("P(Compiled without cachedb, ignoring)\n"));
	#endif
		free($2);
	}
	;
redis_server_host: VAR_CACHEDB_REDISHOST STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_server_host:%s)\n", $2));
		free(cfg_parser->cfg->redis_server_host);
		cfg_parser->cfg->redis_server_host = $2;
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
		free($2);
	#endif
	}
	;
redis_replica_server_host: VAR_CACHEDB_REDISREPLICAHOST STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_replica_server_host:%s)\n", $2));
		free(cfg_parser->cfg->redis_replica_server_host);
		cfg_parser->cfg->redis_replica_server_host = $2;
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
		free($2);
	#endif
	}
	;
redis_server_port: VAR_CACHEDB_REDISPORT STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		int port;
		OUTYY(("P(redis_server_port:%s)\n", $2));
		port = atoi($2);
		if(port == 0 || port < 0 || port > 65535)
			yyerror("valid redis server port number expected");
		else cfg_parser->cfg->redis_server_port = port;
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
	#endif
		free($2);
	}
	;
redis_replica_server_port: VAR_CACHEDB_REDISREPLICAPORT STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		int port;
		OUTYY(("P(redis_replica_server_port:%s)\n", $2));
		port = atoi($2);
		if(port == 0 || port < 0 || port > 65535)
			yyerror("valid redis server port number expected");
		else cfg_parser->cfg->redis_replica_server_port = port;
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
	#endif
		free($2);
	}
	;
redis_server_path: VAR_CACHEDB_REDISPATH STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_server_path:%s)\n", $2));
		free(cfg_parser->cfg->redis_server_path);
		cfg_parser->cfg->redis_server_path = $2;
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
		free($2);
	#endif
	}
	;
redis_replica_server_path: VAR_CACHEDB_REDISREPLICAPATH STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_replica_server_path:%s)\n", $2));
		free(cfg_parser->cfg->redis_replica_server_path);
		cfg_parser->cfg->redis_replica_server_path = $2;
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
		free($2);
	#endif
	}
	;
redis_server_password: VAR_CACHEDB_REDISPASSWORD STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_server_password:%s)\n", $2));
		free(cfg_parser->cfg->redis_server_password);
		cfg_parser->cfg->redis_server_password = $2;
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
		free($2);
	#endif
	}
	;
redis_replica_server_password: VAR_CACHEDB_REDISREPLICAPASSWORD STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_replica_server_password:%s)\n", $2));
		free(cfg_parser->cfg->redis_replica_server_password);
		cfg_parser->cfg->redis_replica_server_password = $2;
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
		free($2);
	#endif
	}
	;
redis_timeout: VAR_CACHEDB_REDISTIMEOUT STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_timeout:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("redis timeout value expected");
		else cfg_parser->cfg->redis_timeout = atoi($2);
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
	#endif
		free($2);
	}
	;
redis_replica_timeout: VAR_CACHEDB_REDISREPLICATIMEOUT STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_replica_timeout:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("redis timeout value expected");
		else cfg_parser->cfg->redis_replica_timeout = atoi($2);
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
	#endif
		free($2);
	}
	;
redis_command_timeout: VAR_CACHEDB_REDISCOMMANDTIMEOUT STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_command_timeout:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("redis command timeout value expected");
		else cfg_parser->cfg->redis_command_timeout = atoi($2);
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
	#endif
		free($2);
	}
	;
redis_replica_command_timeout: VAR_CACHEDB_REDISREPLICACOMMANDTIMEOUT STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_replica_command_timeout:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("redis command timeout value expected");
		else cfg_parser->cfg->redis_replica_command_timeout = atoi($2);
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
	#endif
		free($2);
	}
	;
redis_connect_timeout: VAR_CACHEDB_REDISCONNECTTIMEOUT STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_connect_timeout:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("redis connect timeout value expected");
		else cfg_parser->cfg->redis_connect_timeout = atoi($2);
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
	#endif
		free($2);
	}
	;
redis_replica_connect_timeout: VAR_CACHEDB_REDISREPLICACONNECTTIMEOUT STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_replica_connect_timeout:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("redis connect timeout value expected");
		else cfg_parser->cfg->redis_replica_connect_timeout = atoi($2);
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
	#endif
		free($2);
	}
	;
redis_expire_records: VAR_CACHEDB_REDISEXPIRERECORDS STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_expire_records:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->redis_expire_records = (strcmp($2, "yes")==0);
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
	#endif
		free($2);
	}
	;
redis_logical_db: VAR_CACHEDB_REDISLOGICALDB STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		int db;
		OUTYY(("P(redis_logical_db:%s)\n", $2));
		db = atoi($2);
		if((db == 0 && strcmp($2, "0") != 0) || db < 0)
			yyerror("valid redis logical database index expected");
		else cfg_parser->cfg->redis_logical_db = db;
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
	#endif
		free($2);
	}
	;
redis_replica_logical_db: VAR_CACHEDB_REDISREPLICALOGICALDB STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		int db;
		OUTYY(("P(redis_replica_logical_db:%s)\n", $2));
		db = atoi($2);
		if((db == 0 && strcmp($2, "0") != 0) || db < 0)
			yyerror("valid redis logical database index expected");
		else cfg_parser->cfg->redis_replica_logical_db = db;
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_tcp_connection_limit: VAR_TCP_CONNECTION_LIMIT STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_tcp_connection_limit:%s %s)\n", $2, $3));
		if (atoi($3) < 0)
			yyerror("positive number expected");
		else {
			if(!cfg_str2list_insert(&cfg_parser->cfg->tcp_connection_limits, $2, $3))
				fatal_exit("out of memory adding tcp connection limit");
		}
	}
	;
server_answer_cookie: VAR_ANSWER_COOKIE STRING_ARG
	{
		OUTYY(("P(server_answer_cookie:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->do_answer_cookie = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_cookie_secret: VAR_COOKIE_SECRET STRING_ARG
	{
		uint8_t secret[32];
		size_t secret_len = sizeof(secret);

		OUTYY(("P(server_cookie_secret:%s)\n", $2));
		if(sldns_str2wire_hex_buf($2, secret, &secret_len)
		|| (secret_len != 16))
			yyerror("expected 128 bit hex string");
		else {
			cfg_parser->cfg->cookie_secret_len = secret_len;
			memcpy(cfg_parser->cfg->cookie_secret, secret, sizeof(secret));
		}
		free($2);
	}
	;
server_cookie_secret_file: VAR_COOKIE_SECRET_FILE STRING_ARG
	{
		OUTYY(("P(cookie_secret_file:%s)\n", $2));
		free(cfg_parser->cfg->cookie_secret_file);
		cfg_parser->cfg->cookie_secret_file = $2;
	}
	;
server_iter_scrub_ns: VAR_ITER_SCRUB_NS STRING_ARG
	{
		OUTYY(("P(server_iter_scrub_ns:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->iter_scrub_ns = atoi($2);
		free($2);
	}
	;
server_iter_scrub_cname: VAR_ITER_SCRUB_CNAME STRING_ARG
	{
		OUTYY(("P(server_iter_scrub_cname:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->iter_scrub_cname = atoi($2);
		free($2);
	}
	;
server_max_global_quota: VAR_MAX_GLOBAL_QUOTA STRING_ARG
	{
		OUTYY(("P(server_max_global_quota:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->max_global_quota = atoi($2);
		free($2);
	}
	;
ipsetstart: VAR_IPSET
	{
		OUTYY(("\nP(ipset:)\n"));
		cfg_parser->started_toplevel = 1;
	}
	;
contents_ipset: contents_ipset content_ipset
	| ;
content_ipset: ipset_name_v4 | ipset_name_v6
	;
ipset_name_v4: VAR_IPSET_NAME_V4 STRING_ARG
	{
	#ifdef USE_IPSET
		OUTYY(("P(name-v4:%s)\n", $2));
		if(cfg_parser->cfg->ipset_name_v4)
			yyerror("ipset name v4 override, there must be one "
				"name for ip v4");
		free(cfg_parser->cfg->ipset_name_v4);
		cfg_parser->cfg->ipset_name_v4 = $2;
	#else
		OUTYY(("P(Compiled without ipset, ignoring)\n"));
		free($2);
	#endif
	}
	;
ipset_name_v6: VAR_IPSET_NAME_V6 STRING_ARG
	{
	#ifdef USE_IPSET
		OUTYY(("P(name-v6:%s)\n", $2));
		if(cfg_parser->cfg->ipset_name_v6)
			yyerror("ipset name v6 override, there must be one "
				"name for ip v6");
		free(cfg_parser->cfg->ipset_name_v6);
		cfg_parser->cfg->ipset_name_v6 = $2;
	#else
		OUTYY(("P(Compiled without ipset, ignoring)\n"));
		free($2);
	#endif
	}
	;
%%

/* parse helper routines could be here */
static void
validate_respip_action(const char* action)
{
	if(strcmp(action, "deny")!=0 &&
		strcmp(action, "redirect")!=0 &&
		strcmp(action, "inform")!=0 &&
		strcmp(action, "inform_deny")!=0 &&
		strcmp(action, "always_transparent")!=0 &&
		strcmp(action, "always_refuse")!=0 &&
		strcmp(action, "always_nxdomain")!=0)
	{
		yyerror("response-ip action: expected deny, redirect, "
			"inform, inform_deny, always_transparent, "
			"always_refuse or always_nxdomain");
	}
}

static void
validate_acl_action(const char* action)
{
	if(strcmp(action, "deny")!=0 &&
		strcmp(action, "refuse")!=0 &&
		strcmp(action, "deny_non_local")!=0 &&
		strcmp(action, "refuse_non_local")!=0 &&
		strcmp(action, "allow_setrd")!=0 &&
		strcmp(action, "allow")!=0 &&
		strcmp(action, "allow_snoop")!=0 &&
		strcmp(action, "allow_cookie")!=0)
	{
		yyerror("expected deny, refuse, deny_non_local, "
			"refuse_non_local, allow, allow_setrd, "
			"allow_snoop or allow_cookie as access control action");
	}
}
