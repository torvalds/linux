%{
/*
 * configlexer.lex - lexical analyzer for NSD config file
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved
 *
 * See LICENSE for the license.
 *
 */
/* because flex keeps having sign-unsigned compare problems that are unfixed*/
#if defined(__clang__)||(defined(__GNUC__)&&((__GNUC__ >4)||(defined(__GNUC_MINOR__)&&(__GNUC__ ==4)&&(__GNUC_MINOR__ >=2))))
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#ifdef HAVE_GLOB_H
# include <glob.h>
#endif

#include "options.h"
#include "configparser.h"

#if 0
#define LEXOUT(s)  printf s /* used ONLY when debugging */
#else
#define LEXOUT(s)
#endif

struct inc_state {
	char* filename;
	int line;
	YY_BUFFER_STATE buffer;
	struct inc_state* next;
};
static struct inc_state* config_include_stack = NULL;
static int inc_depth = 0;

static void config_start_include(const char* filename)
{
	FILE *input;
	struct inc_state* s;
	char* nm;
	if(inc_depth++ > 10000000) {
		c_error("too many include files");
		return;
	}
	if(strlen(filename) == 0) {
		c_error("empty include file name");
		return;
	}
	s = (struct inc_state*)malloc(sizeof(*s));
	if(!s) {
		c_error("include %s: malloc failure", filename);
		return;
	}
	nm = strdup(filename);
	if(!nm) {
		c_error("include %s: strdup failure", filename);
		free(s);
		return;
	}
	input = fopen(filename, "r");
	if(!input) {
		c_error("cannot open include file '%s': %s",
			filename, strerror(errno));
		free(s);
		free(nm);
		return;
	}
	LEXOUT(("switch_to_include_file(%s) ", filename));
	s->filename = cfg_parser->filename;
	s->line = cfg_parser->line;
	s->buffer = YY_CURRENT_BUFFER;
	s->next = config_include_stack;
	config_include_stack = s;

	cfg_parser->filename = nm;
	cfg_parser->line = 1;
	yy_switch_to_buffer(yy_create_buffer(input, YY_BUF_SIZE));
}

static void config_start_include_glob(const char* filename)
{
	/* check for wildcards */
#ifdef HAVE_GLOB
	glob_t g;
	int i, r, flags;
#endif /* HAVE_GLOB */
	if (cfg_parser->chroot) {
		int l = strlen(cfg_parser->chroot); /* chroot has trailing slash */
		if (strncmp(cfg_parser->chroot, filename, l) != 0) {
			c_error("include file '%s' is not relative to chroot '%s'",
				filename, cfg_parser->chroot);
			return;
		}
		filename += l - 1; /* strip chroot without trailing slash */
	}
#ifdef HAVE_GLOB
	if(!(!strchr(filename, '*') && !strchr(filename, '?') &&
		 !strchr(filename, '[') && !strchr(filename, '{') &&
		 !strchr(filename, '~'))) {
		 flags = 0
#ifdef GLOB_ERR
		 	 | GLOB_ERR
#endif
			 /* do not set GLOB_NOSORT so the results are sorted
			    and in a predictable order. */
#ifdef GLOB_BRACE
			 | GLOB_BRACE
#endif
#ifdef GLOB_TILDE
			 | GLOB_TILDE
#endif
		;
		memset(&g, 0, sizeof(g));
		r = glob(filename, flags, NULL, &g);
		if(r) {
			/* some error */
			globfree(&g);
			if(r == GLOB_NOMATCH)
				return; /* no matches for pattern */
			config_start_include(filename); /* let original deal with it */
			return;
		}
		/* process files found, if any */
		for(i=(int)g.gl_pathc-1; i>=0; i--) {
			config_start_include(g.gl_pathv[i]);
		}
		globfree(&g);
		return;
	}
#endif /* HAVE_GLOB */
	config_start_include(filename);
}

static void config_end_include(void)
{
	struct inc_state* s = config_include_stack;
	--inc_depth;
	if(!s) return;
	free(cfg_parser->filename);
	cfg_parser->filename = s->filename;
	cfg_parser->line = s->line;
	yy_delete_buffer(YY_CURRENT_BUFFER);
	yy_switch_to_buffer(s->buffer);
	config_include_stack = s->next;
	free(s);
}

#ifndef yy_set_bol /* compat definition, for flex 2.4.6 */
#define yy_set_bol(at_bol) \
        { \
	        if ( ! yy_current_buffer ) \
	                yy_current_buffer = yy_create_buffer( yyin, YY_BUF_SIZE ); \
	        yy_current_buffer->yy_ch_buf[0] = ((at_bol)?'\n':' '); \
        }
#endif

%}
%option noinput
%option nounput
%{
#ifndef YY_NO_UNPUT
#define YY_NO_UNPUT 1
#endif
#ifndef YY_NO_INPUT
#define YY_NO_INPUT 1
#endif
%}

SPACE   [ \t]
LETTER  [a-zA-Z]
UNQUOTEDLETTER [^\"\n\r \t\\]|\\.
NEWLINE [\r\n]
COMMENT \#
COLON 	\:
ANY     [^\"\n\r\\]|\\.

%x	quotedstring include include_quoted

%%
{SPACE}* 		{ LEXOUT(("SP ")); /* ignore */ }
{SPACE}*{COMMENT}.* 	{ LEXOUT(("comment(%s) ", yytext)); /* ignore */ }
server{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_SERVER;}
name{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_NAME;}
ip-address{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_IP_ADDRESS;}
interface{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_IP_ADDRESS;}
ip-transparent{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_IP_TRANSPARENT;}
ip-freebind{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_IP_FREEBIND;}
send-buffer-size{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_SEND_BUFFER_SIZE;}
receive-buffer-size{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_RECEIVE_BUFFER_SIZE;}
debug-mode{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DEBUG_MODE;}
use-systemd{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_USE_SYSTEMD;}
hide-version{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_HIDE_VERSION;}
hide-identity{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_HIDE_IDENTITY;}
drop-updates{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DROP_UPDATES; }
ip4-only{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_IP4_ONLY;}
ip6-only{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_IP6_ONLY;}
do-ip4{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_DO_IP4;}
do-ip6{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_DO_IP6;}
database{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_DATABASE;}
identity{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_IDENTITY;}
version{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_VERSION;}
nsid{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_NSID;}
logfile{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_LOGFILE;}
log-only-syslog{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_LOG_ONLY_SYSLOG;}
server-count{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_SERVER_COUNT;}
tcp-count{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_TCP_COUNT;}
tcp-reject-overflow{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_TCP_REJECT_OVERFLOW;}
tcp-query-count{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_TCP_QUERY_COUNT;}
tcp-timeout{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_TCP_TIMEOUT;}
tcp-mss{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_TCP_MSS;}
outgoing-tcp-mss{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_OUTGOING_TCP_MSS;}
ipv4-edns-size{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_IPV4_EDNS_SIZE;}
ipv6-edns-size{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_IPV6_EDNS_SIZE;}
pidfile{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_PIDFILE;}
port{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_PORT;}
reuseport{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_REUSEPORT;}
statistics{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_STATISTICS;}
chroot{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_CHROOT;}
username{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_USERNAME;}
zonesdir{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_ZONESDIR;}
zonelistfile{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_ZONELISTFILE;}
difffile{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_DIFFFILE;}
xfrdfile{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_XFRDFILE;}
xfrdir{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_XFRDIR;}
xfrd-reload-timeout{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_XFRD_RELOAD_TIMEOUT;}
verbosity{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_VERBOSITY;}
zone{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_ZONE;}
zonefile{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_ZONEFILE;}
zonestats{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_ZONESTATS;}
allow-notify{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_ALLOW_NOTIFY;}
size-limit-xfr{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_SIZE_LIMIT_XFR;}
request-xfr{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_REQUEST_XFR;}
notify{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_NOTIFY;}
notify-retry{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_NOTIFY_RETRY;}
provide-xfr{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_PROVIDE_XFR;}
allow-query{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_ALLOW_QUERY;}
outgoing-interface{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_OUTGOING_INTERFACE;}
allow-axfr-fallback{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_ALLOW_AXFR_FALLBACK;}
tls-auth{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_TLS_AUTH;}
auth-domain-name{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_TLS_AUTH_DOMAIN_NAME;}
client-cert{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_TLS_AUTH_CLIENT_CERT;}
client-key{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_TLS_AUTH_CLIENT_KEY;}
client-key-pw{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_TLS_AUTH_CLIENT_KEY_PW;}
key{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_KEY;}
algorithm{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_ALGORITHM;}
secret{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_SECRET;}
pattern{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_PATTERN;}
include-pattern{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_INCLUDE_PATTERN;}
remote-control{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_REMOTE_CONTROL;}
control-enable{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_CONTROL_ENABLE;}
control-interface{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_CONTROL_INTERFACE;}
control-port{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_CONTROL_PORT;}
server-key-file{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_SERVER_KEY_FILE;}
server-cert-file{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_SERVER_CERT_FILE;}
control-key-file{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_CONTROL_KEY_FILE;}
control-cert-file{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_CONTROL_CERT_FILE;}
metrics-enable{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_METRICS_ENABLE;}
metrics-interface{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_METRICS_INTERFACE;}
metrics-port{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_METRICS_PORT;}
metrics-path{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_METRICS_PATH;}
AXFR			{ LEXOUT(("v(%s) ", yytext)); return VAR_AXFR;}
UDP			{ LEXOUT(("v(%s) ", yytext)); return VAR_UDP;}
rrl-size{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_RRL_SIZE;}
rrl-ratelimit{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_RRL_RATELIMIT;}
rrl-slip{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_RRL_SLIP;}
rrl-ipv4-prefix-length{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_RRL_IPV4_PREFIX_LENGTH;}
rrl-ipv6-prefix-length{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_RRL_IPV6_PREFIX_LENGTH;}
rrl-whitelist-ratelimit{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_RRL_WHITELIST_RATELIMIT;}
rrl-whitelist{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_RRL_WHITELIST;}
reload-config{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_RELOAD_CONFIG; }
zonefiles-check{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_ZONEFILES_CHECK;}
zonefiles-write{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_ZONEFILES_WRITE;}
dnstap{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP;}
dnstap-enable{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP_ENABLE;}
dnstap-socket-path{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP_SOCKET_PATH; }
dnstap-ip{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP_IP; }
dnstap-tls{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP_TLS; }
dnstap-tls-server-name{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP_TLS_SERVER_NAME; }
dnstap-tls-cert-bundle{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP_TLS_CERT_BUNDLE; }
dnstap-tls-client-key-file{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP_TLS_CLIENT_KEY_FILE; }
dnstap-tls-client-cert-file{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP_TLS_CLIENT_CERT_FILE; }
dnstap-send-identity{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP_SEND_IDENTITY; }
dnstap-send-version{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP_SEND_VERSION; }
dnstap-identity{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP_IDENTITY; }
dnstap-version{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP_VERSION; }
dnstap-log-auth-query-messages{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP_LOG_AUTH_QUERY_MESSAGES; }
dnstap-log-auth-response-messages{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_DNSTAP_LOG_AUTH_RESPONSE_MESSAGES; }
log-time-ascii{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_LOG_TIME_ASCII;}
log-time-iso{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_LOG_TIME_ISO;}
round-robin{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_ROUND_ROBIN;}
minimal-responses{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_MINIMAL_RESPONSES;}
confine-to-zone{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_CONFINE_TO_ZONE;}
refuse-any{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_REFUSE_ANY;}
max-refresh-time{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_MAX_REFRESH_TIME;}
min-refresh-time{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_MIN_REFRESH_TIME;}
max-retry-time{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_MAX_RETRY_TIME;}
min-retry-time{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_MIN_RETRY_TIME;}
min-expire-time{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_MIN_EXPIRE_TIME;}
store-ixfr{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_STORE_IXFR;}
ixfr-size{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_IXFR_SIZE;}
ixfr-number{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_IXFR_NUMBER;}
create-ixfr{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_CREATE_IXFR;}
multi-master-check{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_MULTI_PRIMARY_CHECK;}
multi-primary-check{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_MULTI_PRIMARY_CHECK;}
tls-service-key{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_TLS_SERVICE_KEY;}
tls-service-ocsp{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_TLS_SERVICE_OCSP;}
tls-service-pem{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_TLS_SERVICE_PEM;}
tls-port{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_TLS_PORT;}
tls-auth-port{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_TLS_AUTH_PORT;}
tls-auth-xfr-only{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_TLS_AUTH_XFR_ONLY;}
tls-cert-bundle{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_TLS_CERT_BUNDLE; }
proxy-protocol-port{COLON} { LEXOUT(("v(%s) ", yytext)); return VAR_PROXY_PROTOCOL_PORT; }
answer-cookie{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_ANSWER_COOKIE;}
cookie-secret{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_COOKIE_SECRET;}
cookie-secret-file{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_COOKIE_SECRET_FILE;}
cookie-staging-secret{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_COOKIE_STAGING_SECRET;}
xfrd-tcp-max{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_XFRD_TCP_MAX;}
xfrd-tcp-pipeline{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_XFRD_TCP_PIPELINE;}
verify{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_VERIFY; }
enable{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_ENABLE; }
verify-zone{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_VERIFY_ZONE; }
verify-zones{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_VERIFY_ZONES; }
verifier{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_VERIFIER; }
verifier-count{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_VERIFIER_COUNT; }
verifier-feed-zone{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_VERIFIER_FEED_ZONE; }
verifier-timeout{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_VERIFIER_TIMEOUT; }
catalog{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_CATALOG; }
catalog-member-pattern{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_CATALOG_MEMBER_PATTERN; }
catalog-producer-zone{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_CATALOG_PRODUCER_ZONE; }
xdp-interface{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_XDP_INTERFACE; }
xdp-program-path{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_XDP_PROGRAM_PATH; }
xdp-program-load{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_XDP_PROGRAM_LOAD; }
xdp-bpffs-path{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_XDP_BPFFS_PATH; }
xdp-force-copy{COLON}		{ LEXOUT(("v(%s) ", yytext)); return VAR_XDP_FORCE_COPY; }
{NEWLINE}		{ LEXOUT(("NL\n")); cfg_parser->line++;}

servers={UNQUOTEDLETTER}*	{
	yyless(yyleng - (yyleng - 8));
	LEXOUT(("v(%s) ", yytext));
	return VAR_SERVERS;
}
bindtodevice={UNQUOTEDLETTER}*	{
	yyless(yyleng - (yyleng - 13));
	LEXOUT(("v(%s) ", yytext));
	return VAR_BINDTODEVICE;
}
setfib={UNQUOTEDLETTER}*	{
	yyless(yyleng - (yyleng - 7));
	LEXOUT(("v(%s) ", yytext));
	return VAR_SETFIB;
}

cpu-affinity{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_CPU_AFFINITY; }
xfrd-cpu-affinity{COLON}	{ LEXOUT(("v(%s) ", yytext)); return VAR_XFRD_CPU_AFFINITY; }
server-[1-9][0-9]*-cpu-affinity{COLON}	{
		char *str = yytext;
		LEXOUT(("v(%s) ", yytext));
		/* Skip server- */
		while (*str != '\0' && (*str < '0' || *str > '9')) {
			str++;
		}
		c_lval.llng = strtoll(str, NULL, 10);
		return VAR_SERVER_CPU_AFFINITY;
	}

	/* Quoted strings. Strip leading and ending quotes */
\"			{ BEGIN(quotedstring); LEXOUT(("QS ")); }
<quotedstring><<EOF>>   {
        c_error("EOF inside quoted string");
        BEGIN(INITIAL);
}
<quotedstring>{ANY}*    { LEXOUT(("STR(%s) ", yytext)); yymore(); }
<quotedstring>\n        { cfg_parser->line++; yymore(); }
<quotedstring>\" {
        LEXOUT(("QE "));
        BEGIN(INITIAL);
        yytext[yyleng - 1] = '\0';
	c_lval.str = region_strdup(cfg_parser->opt->region, yytext);
        return STRING;
}

	/* include: directive */
include{COLON}		{ LEXOUT(("v(%s) ", yytext)); BEGIN(include); }
<include><<EOF>>	{
        c_error("EOF inside include directive");
        BEGIN(INITIAL);
}
<include>{SPACE}*	{ LEXOUT(("ISP ")); /* ignore */ }
<include>{NEWLINE}	{ LEXOUT(("NL\n")); cfg_parser->line++;}
<include>\"		{ LEXOUT(("IQS ")); BEGIN(include_quoted); }
<include>{UNQUOTEDLETTER}*	{
	LEXOUT(("Iunquotedstr(%s) ", yytext));
	config_start_include_glob(yytext);
	BEGIN(INITIAL);
}
<include_quoted><<EOF>>	{
        c_error("EOF inside quoted string");
        BEGIN(INITIAL);
}
<include_quoted>{ANY}*	{ LEXOUT(("ISTR(%s) ", yytext)); yymore(); }
<include_quoted>{NEWLINE}	{ cfg_parser->line++; yymore(); }
<include_quoted>\"	{
	LEXOUT(("IQE "));
	yytext[yyleng - 1] = '\0';
	config_start_include_glob(yytext);
	BEGIN(INITIAL);
}
<INITIAL><<EOF>>	{
	yy_set_bol(1); /* Set beginning of line, so "^" rules match.  */
	if (!config_include_stack) {
		yyterminate();
	} else {
		fclose(yyin);
		config_end_include();
	}
}

{UNQUOTEDLETTER}*	{ LEXOUT(("unquotedstr(%s) ", yytext)); 
			c_lval.str = region_strdup(cfg_parser->opt->region, yytext); return STRING; }

%%
