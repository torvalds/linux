/*	$OpenBSD: radiusd_standard.c,v 1.6 2024/07/02 00:33:51 yasuoka Exp $	*/

/*
 * Copyright (c) 2013, 2023 Internet Initiative Japan Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/types.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <radius.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "radiusd.h"
#include "radiusd_module.h"

TAILQ_HEAD(attrs,attr);

struct attr {
	uint8_t			 type;
	uint32_t		 vendor;
	uint32_t		 vtype;
	TAILQ_ENTRY(attr)	 next;
};

struct module_standard {
	struct module_base	*base;
	bool			 strip_atmark_realm;
	bool			 strip_nt_domain;
	struct attrs		 remove_reqattrs;
	struct attrs		 remove_resattrs;
};

struct radius_const_str {
	const unsigned	 constval;
	const char	*label;
};

static void	 radius_const_print(FILE *, RADIUS_PACKET *, uint8_t,
		    const char *, struct radius_const_str *);
static void	 module_standard_config_set(void *, const char *, int,
		    char * const *);
static void	 module_standard_reqdeco(void *, u_int, const u_char *, size_t);
static void	 module_standard_resdeco(void *, u_int, const u_char *, size_t,
		    const u_char *, size_t);
static void	 module_accounting_request(void *, u_int, const u_char *,
		    size_t);
static void	 radius_u32_print(FILE *, RADIUS_PACKET *, uint8_t,
		    const char *);
static void	 radius_str_print(FILE *, RADIUS_PACKET *, uint8_t,
		    const char *);
static void	 radius_ipv4_print(FILE *, RADIUS_PACKET *, uint8_t,
		    const char *);
static void	 radius_ipv6_print(FILE *, RADIUS_PACKET *, uint8_t,
		    const char *);

static struct radius_const_str
		 nas_port_type_consts[], tunnel_type_consts[],
		 service_type_consts[], framed_protocol_consts[],
		 acct_status_type_consts[], acct_authentic_consts[],
		 terminate_cause_consts[], tunnel_medium_type_consts[];

int
main(int argc, char *argv[])
{
	struct module_standard module_standard;
	struct module_handlers handlers = {
		.config_set = module_standard_config_set,
		.request_decoration = module_standard_reqdeco,
		.response_decoration = module_standard_resdeco,
		.accounting_request = module_accounting_request
	};
	struct attr		*attr;

	memset(&module_standard, 0, sizeof(module_standard));
	TAILQ_INIT(&module_standard.remove_reqattrs);
	TAILQ_INIT(&module_standard.remove_resattrs);

	if ((module_standard.base = module_create(
	    STDIN_FILENO, &module_standard, &handlers)) == NULL)
		err(1, "Could not create a module instance");

	module_drop_privilege(module_standard.base, 0);
	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	module_load(module_standard.base);

	openlog(NULL, LOG_PID, LOG_DAEMON);

	while (module_run(module_standard.base) == 0)
		;

	module_destroy(module_standard.base);
	while ((attr = TAILQ_FIRST(&module_standard.remove_reqattrs)) != NULL) {
		TAILQ_REMOVE(&module_standard.remove_reqattrs, attr, next);
		freezero(attr, sizeof(struct attr));
	}
	while ((attr = TAILQ_FIRST(&module_standard.remove_resattrs)) != NULL) {
		TAILQ_REMOVE(&module_standard.remove_resattrs, attr, next);
		freezero(attr, sizeof(struct attr));
	}

	exit(EXIT_SUCCESS);
}

static void
module_standard_config_set(void *ctx, const char *name, int argc,
    char * const * argv)
{
	struct module_standard	*module = ctx;
	struct attr		*attr;
	const char		*errmsg = "none";
	const char		*errstr;

	if (strcmp(name, "strip-atmark-realm") == 0) {
		SYNTAX_ASSERT(argc == 1,
		    "`strip-atmark-realm' must have only one argment");
		if (strcmp(argv[0], "true") == 0)
			module->strip_atmark_realm = true;
		else if (strcmp(argv[0], "false") == 0)
			module->strip_atmark_realm = false;
		else
			SYNTAX_ASSERT(0,
			    "`strip-atmark-realm' must `true' or `false'");
	} else if (strcmp(name, "strip-nt-domain") == 0) {
		SYNTAX_ASSERT(argc == 1,
		    "`strip-nt-domain' must have only one argment");
		if (strcmp(argv[0], "true") == 0)
			module->strip_nt_domain = true;
		else if (strcmp(argv[0], "false") == 0)
			module->strip_nt_domain = false;
		else
			SYNTAX_ASSERT(0,
			    "`strip-nt-domain' must `true' or `false'");
	} else if (strcmp(name, "remove-request-attribute") == 0 ||
	    strcmp(name, "remove-response-attribute") == 0) {
		struct attrs		*attrs;

		if (strcmp(name, "remove-request-attribute") == 0) {
			SYNTAX_ASSERT(argc == 1 || argc == 2,
			    "`remove-request-attribute' must have one or two "
			    "argment");
			attrs = &module->remove_reqattrs;
		} else {
			SYNTAX_ASSERT(argc == 1 || argc == 2,
			    "`remove-response-attribute' must have one or two "
			    "argment");
			attrs = &module->remove_resattrs;
		}
		if ((attr = calloc(1, sizeof(struct attr))) == NULL) {
			module_send_message(module->base, IMSG_NG,
			    "Out of memory: %s", strerror(errno));
		}
		if (argc == 1) {
			attr->type = strtonum(argv[0], 0, 255, &errstr);
			if (errstr == NULL &&
			    attr->type != RADIUS_TYPE_VENDOR_SPECIFIC) {
				TAILQ_INSERT_TAIL(attrs, attr, next);
				attr = NULL;
			}
		} else {
			attr->type = RADIUS_TYPE_VENDOR_SPECIFIC;
			attr->vendor = strtonum(argv[0], 0, UINT32_MAX,
			    &errstr);
			if (errstr == NULL)
				attr->vtype = strtonum(argv[1], 0, 255,
				    &errstr);
			if (errstr == NULL) {
				TAILQ_INSERT_TAIL(attrs, attr, next);
				attr = NULL;
			}
		}
		freezero(attr, sizeof(struct attr));
		if (strcmp(name, "remove-request-attribute") == 0)
			SYNTAX_ASSERT(attr == NULL,
			    "wrong number for `remove-request-attribute`");
		else
			SYNTAX_ASSERT(attr == NULL,
			    "wrong number for `remove-response-attribute`");
	} else if (strncmp(name, "_", 1) == 0)
		/* nothing */; /* ignore all internal messages */
	else {
		module_send_message(module->base, IMSG_NG,
		    "Unknown config parameter name `%s'", name);
		return;
	}
	module_send_message(module->base, IMSG_OK, NULL);
	return;

 syntax_error:
	module_send_message(module->base, IMSG_NG, "%s", errmsg);
}

/* request message decoration */
static void
module_standard_reqdeco(void *ctx, u_int q_id, const u_char *pkt, size_t pktlen)
{
	struct module_standard	*module = ctx;
	RADIUS_PACKET		*radpkt = NULL;
	int			 changed = 0;
	char			*ch, *username, buf[256];
	struct attr		*attr;

	if (module->strip_atmark_realm || module->strip_nt_domain) {
		if ((radpkt = radius_convert_packet(pkt, pktlen)) == NULL) {
			syslog(LOG_ERR,
			    "%s: radius_convert_packet() failed: %m", __func__);
			module_stop(module->base);
			return;
		}

		username = buf;
		if (radius_get_string_attr(radpkt, RADIUS_TYPE_USER_NAME,
		    username, sizeof(buf)) != 0) {
			syslog(LOG_WARNING,
			    "standard: q=%u could not get User-Name attribute",
			    q_id);
			goto skip;
		}

		if (module->strip_atmark_realm &&
		    (ch = strrchr(username, '@')) != NULL) {
			*ch = '\0';
			changed++;
		}
		if (module->strip_nt_domain &&
		    (ch = strchr(username, '\\')) != NULL) {
			username = ch + 1;
			changed++;
		}
		if (changed > 0) {
			radius_del_attr_all(radpkt, RADIUS_TYPE_USER_NAME);
			radius_put_string_attr(radpkt,
			    RADIUS_TYPE_USER_NAME, username);
		}
	}
 skip:
	TAILQ_FOREACH(attr, &module->remove_reqattrs, next) {
		if (radpkt == NULL &&
		    (radpkt = radius_convert_packet(pkt, pktlen)) == NULL) {
			syslog(LOG_ERR,
			    "%s: radius_convert_packet() failed: %m", __func__);
			module_stop(module->base);
			return;
		}
		if (attr->type != RADIUS_TYPE_VENDOR_SPECIFIC)
			radius_del_attr_all(radpkt, attr->type);
		else
			radius_del_vs_attr_all(radpkt, attr->vendor,
			    attr->vtype);
	}
	if (radpkt == NULL) {
		pkt = NULL;
		pktlen = 0;
	} else {
		pkt = radius_get_data(radpkt);
		pktlen = radius_get_length(radpkt);
	}
	if (module_reqdeco_done(module->base, q_id, pkt, pktlen) == -1) {
		syslog(LOG_ERR, "%s: module_reqdeco_done() failed: %m",
		    __func__);
		module_stop(module->base);
	}
	if (radpkt != NULL)
		radius_delete_packet(radpkt);
}

/* response message decoration */
static void
module_standard_resdeco(void *ctx, u_int q_id, const u_char *req, size_t reqlen,
    const u_char *res, size_t reslen)
{
	struct module_standard	*module = ctx;
	RADIUS_PACKET		*radres = NULL;
	struct attr		*attr;

	TAILQ_FOREACH(attr, &module->remove_resattrs, next) {
		if (radres == NULL &&
		    (radres = radius_convert_packet(res, reslen)) == NULL) {
			syslog(LOG_ERR,
			    "%s: radius_convert_packet() failed: %m", __func__);
			module_stop(module->base);
			return;
		}
		if (attr->type != RADIUS_TYPE_VENDOR_SPECIFIC)
			radius_del_attr_all(radres, attr->type);
		else
			radius_del_vs_attr_all(radres, attr->vendor,
			    attr->vtype);
	}
	if (radres == NULL) {
		res = NULL;
		reslen = 0;
	} else {
		res = radius_get_data(radres);
		reslen = radius_get_length(radres);
	}
	if (module_resdeco_done(module->base, q_id, res, reslen) == -1) {
		syslog(LOG_ERR, "%s: module_resdeco_done() failed: %m",
		    __func__);
		module_stop(module->base);
	}
	if (radres != NULL)
		radius_delete_packet(radres);
}

static void
module_accounting_request(void *ctx, u_int query_id, const u_char *pkt,
    size_t pktlen)
{
	RADIUS_PACKET		*radpkt = NULL;
	struct module_standard	*module = ctx;
	FILE			*fp;
	char			*buf = NULL;
	size_t			 size = 0;

	if ((radpkt = radius_convert_packet(pkt, pktlen)) == NULL) {
		syslog(LOG_ERR,
		    "%s: radius_convert_packet() failed: %m", __func__);
		module_stop(module->base);
		return;
	}

	if ((fp = open_memstream(&buf, &size)) == NULL) {
		syslog(LOG_ERR, "%s: open_memstream() failed: %m", __func__);
		module_stop(module->base);
		goto out;
	}
	radius_const_print(fp, radpkt, RADIUS_TYPE_ACCT_STATUS_TYPE,
	    "Acct-Status-Type", acct_status_type_consts);

	radius_ipv4_print(fp, radpkt, RADIUS_TYPE_NAS_IP_ADDRESS,
	    "NAS-IP-Address");
	radius_ipv6_print(fp, radpkt, RADIUS_TYPE_NAS_IPV6_ADDRESS,
	    "NAS-IPv6-Address");
	radius_const_print(fp, radpkt, RADIUS_TYPE_NAS_PORT_TYPE,
	    "NAS-Port-Type",  nas_port_type_consts);
	radius_u32_print(fp, radpkt, RADIUS_TYPE_NAS_PORT, "NAS-Port");
	radius_str_print(fp, radpkt, RADIUS_TYPE_NAS_IDENTIFIER,
	    "NAS-Identifier");
	radius_str_print(fp, radpkt, RADIUS_TYPE_CALLING_STATION_ID,
	    "Calling-Station-ID");
	radius_str_print(fp, radpkt, RADIUS_TYPE_CALLED_STATION_ID,
	    "Called-Station-ID");

	radius_const_print(fp, radpkt, RADIUS_TYPE_TUNNEL_MEDIUM_TYPE,
	    "Tunnel-Medium-Type", tunnel_medium_type_consts);
	radius_str_print(fp, radpkt, RADIUS_TYPE_TUNNEL_CLIENT_ENDPOINT,
	    "Tunnel-Client-Endpoint");
	radius_str_print(fp, radpkt, RADIUS_TYPE_TUNNEL_SERVER_ENDPOINT,
	    "Tunnel-Server-Endpoint");
	radius_str_print(fp, radpkt, RADIUS_TYPE_TUNNEL_ASSIGNMENT_ID,
	    "Tunnel-Assignment-ID");
	radius_str_print(fp, radpkt, RADIUS_TYPE_ACCT_TUNNEL_CONNECTION,
	    "Acct-Tunnel-Connection");

	radius_u32_print(fp, radpkt, RADIUS_TYPE_ACCT_SESSION_TIME,
	    "Acct-Session-Time");
	radius_const_print(fp, radpkt,
	    RADIUS_TYPE_TUNNEL_TYPE, "Tunnel-Type", tunnel_type_consts);
	radius_str_print(fp, radpkt, RADIUS_TYPE_USER_NAME, "User-Name");
	radius_const_print(fp, radpkt,
	    RADIUS_TYPE_SERVICE_TYPE, "Service-Type", service_type_consts);
	radius_const_print(fp, radpkt, RADIUS_TYPE_FRAMED_PROTOCOL,
	    "Framed-Protocol", framed_protocol_consts);
	radius_ipv4_print(fp, radpkt, RADIUS_TYPE_FRAMED_IP_ADDRESS,
	    "Framed-IP-Address");
	radius_u32_print(fp, radpkt, RADIUS_TYPE_ACCT_DELAY_TIME,
	    "Acct-Delay-Time");
	radius_u32_print(fp, radpkt, RADIUS_TYPE_ACCT_INPUT_OCTETS,
	    "Acct-Input-Octets");
	radius_u32_print(fp, radpkt, RADIUS_TYPE_ACCT_OUTPUT_OCTETS,
	    "Acct-Output-Octets");
	radius_str_print(fp, radpkt, RADIUS_TYPE_ACCT_SESSION_ID,
	    "Acct-Session-ID");
	radius_const_print(fp, radpkt, RADIUS_TYPE_ACCT_AUTHENTIC,
	    "Acct-Authentic", acct_authentic_consts);
	radius_u32_print(fp, radpkt, RADIUS_TYPE_ACCT_SESSION_TIME,
	    "Acct-Sesion-Time");
	radius_u32_print(fp, radpkt, RADIUS_TYPE_ACCT_INPUT_PACKETS,
	    "Acct-Input-Packets");
	radius_u32_print(fp, radpkt, RADIUS_TYPE_ACCT_OUTPUT_PACKETS,
	    "Acct-Output-Packets");
	radius_const_print(fp, radpkt, RADIUS_TYPE_ACCT_TERMINATE_CAUSE,
	    "Acct-Terminate-Cause", terminate_cause_consts);
	radius_u32_print(fp, radpkt, RADIUS_TYPE_ACCT_INPUT_GIGAWORDS,
	    "Acct-Input-Gigawords");
	radius_u32_print(fp, radpkt, RADIUS_TYPE_ACCT_OUTPUT_GIGAWORDS,
	    "Acct-Output-Gigawords");

	fputc('\0', fp);
	fclose(fp);
	syslog(LOG_INFO, "Accounting q=%u %s", query_id, buf + 1);
 out:
	radius_delete_packet(radpkt);
	freezero(buf, size);
}

/***********************************************************************
 * print RADIUS attribute
 ***********************************************************************/
static void
radius_const_print(FILE *fout, RADIUS_PACKET *radpkt, uint8_t attr_type,
    const char *attr_name, struct radius_const_str *consts)
{
	struct radius_const_str *const_;
	uint32_t		 u32val;

	if (radius_get_uint32_attr(radpkt, attr_type, &u32val) != 0)
		return;

	for (const_ = consts; const_->label != NULL; const_++) {
		if (const_->constval == u32val)
			break;
	}

	fprintf(fout, " %s=%s(%u)", attr_name, (const_ != NULL)? const_->label
	    : "unknown", (unsigned)u32val);
}

static void
radius_u32_print(FILE *fout, RADIUS_PACKET *radpkt, uint8_t attr_type,
    const char *attr_name)
{
	uint32_t		 u32val;

	if (radius_get_uint32_attr(radpkt, attr_type, &u32val) != 0)
		return;
	fprintf(fout, " %s=%u", attr_name, u32val);
}

static void
radius_str_print(FILE *fout, RADIUS_PACKET *radpkt, uint8_t attr_type,
    const char *attr_name)
{
	char			 strval[256];

	if (radius_get_string_attr(radpkt, attr_type, strval, sizeof(strval))
	    != 0)
		return;
	fprintf(fout, " %s=%s", attr_name, strval);
}

static void
radius_ipv4_print(FILE *fout, RADIUS_PACKET *radpkt, uint8_t attr_type,
    const char *attr_name)
{
	struct in_addr		 ipv4;
	char			 buf[128];

	if (radius_get_ipv4_attr(radpkt, attr_type, &ipv4) != 0)
		return;
	fprintf(fout, " %s=%s", attr_name,
	    inet_ntop(AF_INET, &ipv4, buf, sizeof(buf)));
}

static void
radius_ipv6_print(FILE *fout, RADIUS_PACKET *radpkt, uint8_t attr_type,
    const char *attr_name)
{
	struct in6_addr		 ipv6;
	char			 buf[128];

	if (radius_get_ipv6_attr(radpkt, attr_type, &ipv6) != 0)
		return;

	fprintf(fout, " %s=%s", attr_name,
	    inet_ntop(AF_INET6, &ipv6, buf, sizeof(buf)));
}

static struct radius_const_str nas_port_type_consts[] = {
    { RADIUS_NAS_PORT_TYPE_ASYNC,		"\"Async\"" },
    { RADIUS_NAS_PORT_TYPE_SYNC,		"\"Sync\"" },
    { RADIUS_NAS_PORT_TYPE_ISDN_SYNC,		"\"ISDN Sync\"" },
    { RADIUS_NAS_PORT_TYPE_ISDN_ASYNC_V120,	"\"ISDN Async V.120\"" },
    { RADIUS_NAS_PORT_TYPE_ISDN_ASYNC_V110,	"\"ISDN Async V.110\"" },
    { RADIUS_NAS_PORT_TYPE_VIRTUAL,		"\"Virtual\"" },
    { RADIUS_NAS_PORT_TYPE_PIAFS,		"\"PIAFS\"" },
    { RADIUS_NAS_PORT_TYPE_HDLC_CLEAR_CHANNEL,	"\"HDLC Clear Channel\"" },
    { RADIUS_NAS_PORT_TYPE_X_25,		"\"X.25\"" },
    { RADIUS_NAS_PORT_TYPE_X_75,		"\"X.75\"" },
    { RADIUS_NAS_PORT_TYPE_G3_FAX,		"\"G.3 Fax\"" },
    { RADIUS_NAS_PORT_TYPE_SDSL,		"\"SDSL\"" },
    { RADIUS_NAS_PORT_TYPE_ADSL_CAP,		"\"ADSL-CAP\"" },
    { RADIUS_NAS_PORT_TYPE_ADSL_DMT,		"\"ADSL-DMT\"" },
    { RADIUS_NAS_PORT_TYPE_IDSL,		"\"IDSL\"" },
    { RADIUS_NAS_PORT_TYPE_ETHERNET,		"\"Ethernet\"" },
    { RADIUS_NAS_PORT_TYPE_XDSL,		"\"xDSL\"" },
    { RADIUS_NAS_PORT_TYPE_CABLE,		"\"Cable\"" },
    { RADIUS_NAS_PORT_TYPE_WIRELESS,		"\"Wireless\"" },
    { RADIUS_NAS_PORT_TYPE_WIRELESS_802_11,	"\"Wireless - IEEE 802.11\"" },
    { 0, NULL }
};

static struct radius_const_str tunnel_type_consts[] = {
    { RADIUS_TUNNEL_TYPE_PPTP,		"PPTP" },
    { RADIUS_TUNNEL_TYPE_L2F,		"L2F" },
    { RADIUS_TUNNEL_TYPE_L2TP,		"L2TP" },
    { RADIUS_TUNNEL_TYPE_ATMP,		"ATMP" },
    { RADIUS_TUNNEL_TYPE_VTP,		"VTP" },
    { RADIUS_TUNNEL_TYPE_AH,		"AH" },
    { RADIUS_TUNNEL_TYPE_IP,		"IP" },
    { RADIUS_TUNNEL_TYPE_MOBILE,	"MIN-IP-IP" },
    { RADIUS_TUNNEL_TYPE_ESP,		"ESP" },
    { RADIUS_TUNNEL_TYPE_GRE,		"GRE" },
    { RADIUS_TUNNEL_TYPE_VDS,		"DVS" },
    { 0, NULL }
};

static struct radius_const_str service_type_consts[] = {
    { RADIUS_SERVICE_TYPE_LOGIN,		"\"Login\"" },
    { RADIUS_SERVICE_TYPE_FRAMED,		"\"Framed\"" },
    { RADIUS_SERVICE_TYPE_CB_LOGIN,		"\"Callback Login\"" },
    { RADIUS_SERVICE_TYPE_CB_FRAMED,		"\"Callback Framed\"" },
    { RADIUS_SERVICE_TYPE_OUTBOUND,		"\"Outbound\"" },
    { RADIUS_SERVICE_TYPE_ADMINISTRATIVE,	"\"Administrative\"" },
    { RADIUS_SERVICE_TYPE_NAS_PROMPT,		"\"NAS Propmt\"" },
/* there had been a typo in radius.h */
#if !defined(RADIUS_SERVICE_TYPE_CB_NAS_PROMPT) && \
    defined(RADIUS_SERVICE_TYPE_CB_NAS_PROMPTi)
#define RADIUS_SERVICE_TYPE_CB_NAS_PROMPT RADIUS_SERVICE_TYPE_CB_NAS_PROMPTi
#endif
    { RADIUS_SERVICE_TYPE_AUTHENTICAT_ONLY,	"\"Authenticat Only\"" },
    { RADIUS_SERVICE_TYPE_CB_NAS_PROMPT,	"\"Callback NAS Prompt\"" },
    { RADIUS_SERVICE_TYPE_CALL_CHECK,		"\"Call Check\"" },
    { RADIUS_SERVICE_TYPE_CB_ADMINISTRATIVE,	"\"Callback Administrative\"" },
    { 0, NULL }
};

static struct radius_const_str framed_protocol_consts[] = {
    { RADIUS_FRAMED_PROTOCOL_PPP,		"PPP" },
    { RADIUS_FRAMED_PROTOCOL_SLIP,		"SLIP" },
    { RADIUS_FRAMED_PROTOCOL_ARAP,		"ARAP" },
    { RADIUS_FRAMED_PROTOCOL_GANDALF,		"Gandalf" },
    { RADIUS_FRAMED_PROTOCOL_XYLOGICS,		"Xylogics" },
    { RADIUS_FRAMED_PROTOCOL_X75,		"X.75" },
    { 0, NULL }
};

static struct radius_const_str acct_status_type_consts[] = {
    { RADIUS_ACCT_STATUS_TYPE_START,		"Start" },
    { RADIUS_ACCT_STATUS_TYPE_STOP,		"Stop" },
    { RADIUS_ACCT_STATUS_TYPE_INTERIM_UPDATE,	"Interim-Update" },
    { RADIUS_ACCT_STATUS_TYPE_ACCT_ON,		"Accounting-On" },
    { RADIUS_ACCT_STATUS_TYPE_ACCT_OFF,		"Accounting-Off" },
    { 0, NULL }
};

static struct radius_const_str acct_authentic_consts[] = {
    { RADIUS_ACCT_AUTHENTIC_RADIUS,		"RADIUS" },
    { RADIUS_ACCT_AUTHENTIC_LOCAL,		"Local" },
    { RADIUS_ACCT_AUTHENTIC_REMOTE,		"Remote" },
    { 0, NULL }
};

static struct radius_const_str terminate_cause_consts[] = {
    { RADIUS_TERMNATE_CAUSE_USER_REQUEST,	"\"User Request\"" },
    { RADIUS_TERMNATE_CAUSE_LOST_CARRIER,	"\"Lost Carrier\"" },
    { RADIUS_TERMNATE_CAUSE_LOST_SERVICE,	"\"Lost Service\"" },
    { RADIUS_TERMNATE_CAUSE_IDLE_TIMEOUT,	"\"Idle Timeout\"" },
    { RADIUS_TERMNATE_CAUSE_SESSION_TIMEOUT,	"\"Session Timeout\"" },
    { RADIUS_TERMNATE_CAUSE_ADMIN_RESET,	"\"Admin Reset\"" },
    { RADIUS_TERMNATE_CAUSE_ADMIN_REBOOT,	"\"Admin Reboot\"" },
    { RADIUS_TERMNATE_CAUSE_PORT_ERROR,		"\"Port Error\"" },
    { RADIUS_TERMNATE_CAUSE_NAS_ERROR,		"\"NAS Error\"" },
    { RADIUS_TERMNATE_CAUSE_NAS_RESET,		"\"NAS Request\"" },
    { RADIUS_TERMNATE_CAUSE_NAS_REBOOT,		"\"NAS Reboot\"" },
    { RADIUS_TERMNATE_CAUSE_PORT_UNNEEDED,	"\"Port Unneeded\"" },
    { RADIUS_TERMNATE_CAUSE_PORT_PREEMPTED,	"\"Port Preempted\"" },
    { RADIUS_TERMNATE_CAUSE_PORT_SUSPENDED,	"\"Port Suspended\"" },
    { RADIUS_TERMNATE_CAUSE_SERVICE_UNAVAIL,	"\"Service Unavailable\"" },
    { RADIUS_TERMNATE_CAUSE_CALLBACK,		"\"Callback\"" },
    { RADIUS_TERMNATE_CAUSE_USER_ERROR,		"\"User Error\"" },
    { RADIUS_TERMNATE_CAUSE_HOST_REQUEST,	"\"Host Request\"" },
    { 0, NULL }
};

static struct radius_const_str tunnel_medium_type_consts[] = {
    { RADIUS_TUNNEL_MEDIUM_TYPE_IPV4,		"IPv4" },
    { RADIUS_TUNNEL_MEDIUM_TYPE_IPV6,		"IPv6" },
    { RADIUS_TUNNEL_MEDIUM_TYPE_NSAP,		"NSAP" },
    { RADIUS_TUNNEL_MEDIUM_TYPE_HDLC,		"HDLC" },
    { RADIUS_TUNNEL_MEDIUM_TYPE_BBN1822,	"BBN1822" },
    { RADIUS_TUNNEL_MEDIUM_TYPE_802,		"802" },
    { RADIUS_TUNNEL_MEDIUM_TYPE_E163,		"E.163" },
    { RADIUS_TUNNEL_MEDIUM_TYPE_E164,		"E.164" },
    { 0, NULL }
};
