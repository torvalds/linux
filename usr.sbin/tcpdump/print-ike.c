/*	$OpenBSD: print-ike.c,v 1.41 2022/12/28 21:30:19 jmc Exp $	*/

/*
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2001 Håkan Olsson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Format and print ike (isakmp) packets.
 *	By Tero Kivinen <kivinen@ssh.fi>, Tero Mononen <tmo@ssh.fi>,
 *         Tatu Ylonen <ylo@ssh.fi> and Timo J. Rinne <tri@ssh.fi>
 *         in co-operation with SSH Communications Security, Espoo, Finland
 */

#include <sys/time.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"
#include "ike.h"

struct isakmp_header {
	u_int8_t	init_cookie[8];
	u_int8_t	resp_cookie[8];
	u_int8_t	next_payload;
	u_int8_t	version;
	u_int8_t	exgtype;
	u_int8_t	flags;
	u_int8_t	msgid[4];
	u_int32_t	length;
	u_int8_t	payloads[0];
};

struct sa_payload {
	u_int8_t	next_payload;
	u_int8_t	reserved;
	u_int16_t	payload_length;
	u_int32_t	doi;
	u_int8_t	situation[0];
};

struct proposal_payload {
	u_int8_t	next_payload;
	u_int8_t	reserved;
	u_int16_t	payload_length;
	u_int8_t	nprop;
	u_int8_t	proto;
	u_int8_t	spi_size;
	u_int8_t	nspis;
	u_int8_t	spi[0];
};

struct transform_payload {
	u_int8_t	next_payload;
	u_int8_t	reserved;
	u_int16_t	payload_length;
	u_int8_t	ntrans;
	u_int8_t	transform;
	u_int16_t	reserved2;
	u_int8_t	attribute[0];
};

struct ke_payload {
	u_int8_t	next_payload;
	u_int8_t	reserved;
	u_int16_t	payload_length;
	u_int8_t	data[0];
};

struct id_payload {
	u_int8_t	next_payload;
	u_int8_t	reserved;
	u_int16_t	payload_length;
	u_int8_t	type;
	u_int8_t	id_data[3];
	u_int8_t	data[0];
};

struct notification_payload {
	u_int8_t	next_payload;
	u_int8_t	reserved;
	u_int16_t	payload_length;
	u_int32_t	doi;
	u_int8_t	protocol_id;
  	u_int8_t	spi_size;
  	u_int16_t	type;
	u_int8_t	data[0];
};

struct delete_payload {
	u_int8_t	next_payload;
	u_int8_t	reserved;
	u_int16_t	payload_length;
	u_int32_t	doi;
	u_int8_t	proto;
	u_int8_t	spi_size;
	u_int16_t	nspis;
	u_int8_t	spi[0];
};

struct vendor_payload {
	u_int8_t	next_payload;
	u_int8_t	reserved;
	u_int16_t	payload_length;
	u_int8_t	vid[0];
};

struct attribute_payload {
	u_int8_t	next_payload;
	u_int8_t	reserved;
	u_int16_t	payload_length;
	u_int8_t	type;
	u_int8_t	reserved2;
	u_int16_t	id;
};

static void ike_pl_print(u_int8_t, u_int8_t *, u_int8_t);

int ike_tab_level = 0;
u_int8_t xform_proto;

static const char *ike[] = IKE_PROTO_INITIALIZER;

#define SMALL_TABS 4
#define SPACES "                                                   "
const char *
ike_tab_offset(void)
{
	const char *p, *endline;
	static const char line[] = SPACES;

	endline = line + sizeof line - 1;
	p = endline - SMALL_TABS * (ike_tab_level);

	return (p > line ? p : line);
}

static char *
ike_get_cookie (u_int8_t *ic, u_int8_t *rc)
{
	static char cookie_jar[35];
	int i;

	for (i = 0; i < 8; i++)
		snprintf(cookie_jar + i*2, sizeof(cookie_jar) - i*2,
		    "%02x", *(ic + i));
	strlcat(cookie_jar, "->", sizeof(cookie_jar));
	for (i = 0; i < 8; i++)
		snprintf(cookie_jar + 18 + i*2, sizeof(cookie_jar) - 18 - i*2,
		    "%02x", *(rc + i));
	return cookie_jar;
}

/*
 * Print isakmp requests
 */
void
ike_print (const u_int8_t *cp, u_int length)
{
	struct isakmp_header *ih;
	const u_int8_t *ep;
	u_int8_t *payload, next_payload;
	int encrypted;
	static const char *exgtypes[] = IKE_EXCHANGE_TYPES_INITIALIZER;

	encrypted = 0;

#ifdef TCHECK
#undef TCHECK
#endif
#define TCHECK(var, l) if ((u_int8_t *)&(var) > ep - l) goto trunc

	ih = (struct isakmp_header *)cp;

	if (length < sizeof (struct isakmp_header))
		goto trunc;

	/* 'ep' points to the end of available data. */
	ep = snapend;

	printf("isakmp v%u.%u", ih->version >> 4, ih->version & 0xf);

	printf(" exchange ");
	if (ih->exgtype < (sizeof exgtypes/sizeof exgtypes[0]))
		printf("%s", exgtypes[ih->exgtype]);
	else
		printf("%d (unknown)", ih->exgtype);

	if (ih->flags & FLAGS_ENCRYPTION) {
		printf(" encrypted");
		encrypted = 1;
	}

	if (ih->flags & FLAGS_COMMIT) {
		printf(" commit");
	}

	printf("\n\tcookie: %s", ike_get_cookie (ih->init_cookie,
						 ih->resp_cookie));

	TCHECK(ih->msgid, sizeof(ih->msgid));
	printf(" msgid: %02x%02x%02x%02x", ih->msgid[0], ih->msgid[1],
	    ih->msgid[2], ih->msgid[3]);

	TCHECK(ih->length, sizeof(ih->length));
	printf(" len: %d", ntohl(ih->length));

	if (ih->version > IKE_VERSION_2) {
		printf(" new version");
		return;
	}

	payload = ih->payloads;
	next_payload = ih->next_payload;

	/* if encrypted, then open special file for encryption keys */
	if (encrypted) {
		/* decrypt XXX */
		return;
	}

	/* if verbose, print payload data */
	if (vflag)
		ike_pl_print(next_payload, payload, ISAKMP_DOI);

	return;

trunc:
	printf(" [|isakmp]");
}

void
ike_pl_sa_print (u_int8_t *buf, int len)
{
	struct sa_payload *sp = (struct sa_payload *)buf;
	u_int32_t sit_ipsec, doi;

	if (len < sizeof(struct sa_payload)) {
		printf(" [|payload]");
		return;
	}

	doi = ntohl(sp->doi);
	printf(" DOI: %d", doi);

	if (doi == IPSEC_DOI) {
		if ((sp->situation + sizeof(u_int32_t)) > (buf + len)) {
			printf(" [|payload]");
			return;
		}
		printf("(IPSEC) situation: ");
		sit_ipsec = ntohl(*(u_int32_t *)sp->situation);
		if (sit_ipsec & IKE_SITUATION_IDENTITY_ONLY)
			printf("IDENTITY_ONLY ");
		if (sit_ipsec & IKE_SITUATION_SECRECY)
			printf("SECRECY ");
		if (sit_ipsec & IKE_SITUATION_INTEGRITY)
			printf("INTEGRITY ");
		if ((sit_ipsec & IKE_SITUATION_MASK) == 0)
			printf("0x%x (unknown)", sit_ipsec);
		ike_pl_print (PAYLOAD_PROPOSAL, buf +
		    sizeof(struct sa_payload) + sizeof(u_int32_t), IPSEC_DOI);
	} else
		printf(" situation: (unknown)");
}

int
ike_attribute_print (u_int8_t *buf, u_int8_t doi, int maxlen)
{
	static char *attrs[] = IKE_ATTR_INITIALIZER;
	static char *attr_enc[] = IKE_ATTR_ENCRYPT_INITIALIZER;
	static char *attr_hash[] = IKE_ATTR_HASH_INITIALIZER;
	static char *attr_auth[] = IKE_ATTR_AUTH_INITIALIZER;
	static char *attr_gdesc[] = IKE_ATTR_GROUP_DESC_INITIALIZER;
	static char *attr_gtype[] = IKE_ATTR_GROUP_INITIALIZER;
	static char *attr_ltype[] = IKE_ATTR_SA_DURATION_INITIALIZER;
	static char *ipsec_attrs[] = IPSEC_ATTR_INITIALIZER;
	static char *ipsec_attr_auth[] = IPSEC_ATTR_AUTH_INITIALIZER;
	static char *ipsec_attr_ltype[] = IPSEC_ATTR_DURATION_INITIALIZER;

	u_int8_t af   = buf[0] >> 7;
	u_int16_t type = (buf[0] & 0x7f) << 8 | buf[1];
	u_int16_t len  = buf[2] << 8 | buf[3], val;

	if (doi == ISAKMP_DOI)
		printf("\n\t%sattribute %s = ", ike_tab_offset(),
		    (type < sizeof attrs / sizeof attrs[0] ?
		    attrs[type] : "<unknown>"));
	else
		printf("\n\t%sattribute %s = ", ike_tab_offset(),
		    (type < (sizeof ipsec_attrs / sizeof ipsec_attrs[0]) ?
		    ipsec_attrs[type] : "<unknown>"));

	if ((af == 1 && maxlen < 4) || (af == 0 && maxlen < (len + 4))) {
		printf("\n\t%s[|attr]", ike_tab_offset());
		return maxlen;
	}

	if (af == 0) {
		/* AF=0; print the variable length attribute value */
		for (val = 0; val < len; val++)
			printf("%02x", *(buf + 4 + val));
		return len + 4;
	}

	val = len;	/* For AF=1, this field is the "VALUE" */
	len = 4; 	/* and with AF=1, length is always 4 */

#define CASE_PRINT(TYPE, var) \
	case TYPE : \
		if (val < sizeof var / sizeof var [0]) \
			printf("%s", var [val]); \
		else \
			printf("%d (unknown)", val); \
		break;

	if (doi == ISAKMP_DOI)
		switch(type) {
			CASE_PRINT(IKE_ATTR_ENCRYPTION_ALGORITHM, attr_enc);
			CASE_PRINT(IKE_ATTR_HASH_ALGORITHM, attr_hash);
			CASE_PRINT(IKE_ATTR_AUTHENTICATION_METHOD, attr_auth);
			CASE_PRINT(IKE_ATTR_GROUP_DESC, attr_gdesc);
			CASE_PRINT(IKE_ATTR_GROUP_TYPE, attr_gtype);
			CASE_PRINT(IKE_ATTR_LIFE_TYPE, attr_ltype);
		default:
			printf("%d", val);
		}
	else
		switch(type) {
			CASE_PRINT(IPSEC_ATTR_SA_LIFE_TYPE, ipsec_attr_ltype);
			CASE_PRINT(IPSEC_ATTR_AUTHENTICATION_ALGORITHM,
			    ipsec_attr_auth);
			case IPSEC_ATTR_ENCAPSULATION_MODE:
				printf("%s", tok2str(ipsec_attr_encap,
				    "%d", val));
				break;
		default:
			printf("%d", val);
		}

#undef CASE_PRINT
	return len;
}

void
ike_pl_transform_print (u_int8_t *buf, int len, u_int8_t doi)
{
	struct transform_payload *tp = (struct transform_payload *)buf;
	const char *ah[] = IPSEC_AH_INITIALIZER;
	const char *esp[] = IPSEC_ESP_INITIALIZER;
	const char *ipcomp[] = IPCOMP_INITIALIZER;
	u_int8_t *attr = tp->attribute;

	if (len < sizeof(struct transform_payload)) {
		printf(" [|payload]");
		return;
	}

	printf("\n\t%stransform: %u ID: ", ike_tab_offset(), tp->ntrans);

	switch (doi) {
	case ISAKMP_DOI:
		if (tp->transform < (sizeof ike / sizeof ike[0]))
			printf("%s", ike[tp->transform]);
		else
			printf("%d(unknown)", tp->transform);
		break;

	default: /* IPSEC_DOI */
		switch (xform_proto) { /* from ike_proposal_print */
		case PROTO_IPSEC_AH:
			if (tp->transform < (sizeof ah / sizeof ah[0]))
				printf("%s", ah[tp->transform]);
			else
				printf("%d(unknown)", tp->transform);
			break;
		case PROTO_IPSEC_ESP:
			if (tp->transform < (sizeof esp / sizeof esp[0]))
				printf("%s", esp[tp->transform]);
			else
				printf("%d(unknown)", tp->transform);
			break;
		case PROTO_IPCOMP:
			if (tp->transform < (sizeof ipcomp / sizeof ipcomp[0]))
				printf("%s", ipcomp[tp->transform]);
			else
				printf("%d(unknown)", tp->transform);
			break;
		default:
			printf("%d(unknown)", tp->transform);
		}
		break;
	}

	ike_tab_level++;
	while ((int)(attr - buf) < len) /* Skip last 'NONE' attr */
		attr += ike_attribute_print(attr, doi, len - (attr - buf));
	ike_tab_level--;
}

void
ike_pl_proposal_print (u_int8_t *buf, int len, u_int8_t doi)
{
	struct proposal_payload *pp = (struct proposal_payload *)buf;
	int i;

	if (len < sizeof(struct proposal_payload)) {
		printf(" [|payload]");
		return;
	}

	printf(" proposal: %d proto: %s spisz: %d xforms: %d",
	    pp->nprop, (pp->proto < (sizeof ike / sizeof ike[0]) ?
	    ike[pp->proto] : "(unknown)"), pp->spi_size, pp->nspis);

	xform_proto = pp->proto;

	if (pp->spi_size) {
		if ((pp->spi + pp->spi_size) > (buf + len)) {
			printf(" [|payload]");
			return;
		}
		if (pp->proto == PROTO_IPCOMP)
			printf(" CPI: 0x");
		else
			printf(" SPI: 0x");
		for (i = 0; i < pp->spi_size; i++)
			printf("%02x", pp->spi[i]);
	}

	/* Reset to sane value. */
	if (pp->proto == PROTO_ISAKMP)
		doi = ISAKMP_DOI;
	else
		doi = IPSEC_DOI;

	if (pp->nspis > 0)
		ike_pl_print(PAYLOAD_TRANSFORM, pp->spi + pp->spi_size, doi);
}

void
ike_pl_ke_print (u_int8_t *buf, int len, u_int8_t doi)
{
	if (len < sizeof(struct ke_payload)) {
		printf(" [|payload]");
		return;
	}

	if (doi != IPSEC_DOI)
		return;

	/* XXX ... */
}

void
ipsec_id_print (u_int8_t *buf, int len, u_int8_t doi)
{
	struct id_payload *ip = (struct id_payload *)buf;
	static const char *idtypes[] = IPSEC_ID_TYPE_INITIALIZER;
	char ntop_buf[INET6_ADDRSTRLEN];
	struct in_addr in;
	u_int8_t *p;

	if (len < sizeof (struct id_payload)) {
		printf(" [|payload]");
		return;
	}

	if (doi != ISAKMP_DOI)
		return;

	/* Don't print proto+port unless actually used */
	if (ip->id_data[0] | ip->id_data[1] | ip->id_data[2])
		printf(" proto: %d port: %d", ip->id_data[0],
		    (ip->id_data[1] << 8) + ip->id_data[2]);

	printf(" type: %s = ", ip->type < (sizeof idtypes/sizeof idtypes[0]) ?
	    idtypes[ip->type] : "<unknown>");

	switch (ip->type) {
	case IPSEC_ID_IPV4_ADDR:
		if ((ip->data + sizeof in) > (buf + len)) {
			printf(" [|payload]");
			return;
		}
		memcpy (&in.s_addr, ip->data, sizeof in);
		printf("%s", inet_ntoa (in));
		break;

	case IPSEC_ID_IPV4_ADDR_SUBNET:
	case IPSEC_ID_IPV4_ADDR_RANGE:
		if ((ip->data + 2 * (sizeof in)) > (buf + len)) {
			printf(" [|payload]");
			return;
		}
		memcpy (&in.s_addr, ip->data, sizeof in);
		printf("%s%s", inet_ntoa (in),
		    ip->type == IPSEC_ID_IPV4_ADDR_SUBNET ? "/" : "-");
		memcpy (&in.s_addr, ip->data + sizeof in, sizeof in);
		printf("%s", inet_ntoa (in));
		break;

	case IPSEC_ID_IPV6_ADDR:
		if ((ip->data + sizeof ntop_buf) > (buf + len)) {
			printf(" [|payload]");
			return;
		}
		printf("%s", inet_ntop (AF_INET6, ip->data, ntop_buf,
		    sizeof ntop_buf));
		break;

	case IPSEC_ID_IPV6_ADDR_SUBNET:
	case IPSEC_ID_IPV6_ADDR_RANGE:
		if ((ip->data + 2 * sizeof ntop_buf) > (buf + len)) {
			printf(" [|payload]");
			return;
		}
		printf("%s%s", inet_ntop (AF_INET6, ip->data, ntop_buf,
		    sizeof ntop_buf),
		    ip->type == IPSEC_ID_IPV6_ADDR_SUBNET ? "/" : "-");
		printf("%s", inet_ntop (AF_INET6, ip->data + sizeof ntop_buf,
		    ntop_buf, sizeof ntop_buf));
		break;

	case IPSEC_ID_FQDN:
	case IPSEC_ID_USER_FQDN:
		printf("\"");
		for (p = ip->data; (int)(p - buf) < len; p++)
			printf("%c",(isprint(*p) ? *p : '.'));
		printf("\"");
		break;

	case IPSEC_ID_DER_ASN1_DN:
	case IPSEC_ID_DER_ASN1_GN:
	case IPSEC_ID_KEY_ID:
	default:
		printf("\"(not shown)\"");
		break;
	}
}

void
ike_pl_delete_print (u_int8_t *buf, int len)
{
  	struct delete_payload *dp = (struct delete_payload *)buf;
	u_int32_t doi;
	u_int16_t s, nspis;
	u_int8_t *data;

	if (len < sizeof (struct delete_payload)) {
		printf(" [|payload]");
		return;
	}

	doi   = ntohl(dp->doi);
	nspis = ntohs(dp->nspis);

	if (doi != ISAKMP_DOI && doi != IPSEC_DOI) {
		printf(" (unknown DOI)");
		return;
	}

	printf(" DOI: %u(%s) proto: %s nspis: %u", doi,
	    doi == ISAKMP_DOI ? "ISAKMP" : "IPSEC",
	    dp->proto < (sizeof ike / sizeof ike[0]) ? ike[dp->proto] :
	    "(unknown)", nspis);

	if ((dp->spi + nspis * dp->spi_size) > (buf + len)) {
		printf(" [|payload]");
		return;
	}

	for (s = 0; s < nspis; s++) {
		data = dp->spi + s * dp->spi_size;
		if (dp->spi_size == 16)
			printf("\n\t%scookie: %s", ike_tab_offset(),
			    ike_get_cookie(&data[0], &data[8]));
		else
			printf("\n\t%sSPI: 0x%08x", ike_tab_offset(),
			    data[0]<<24 | data[1]<<16 | data[2]<<8 | data[3]);
	}
}

void
ike_pl_notification_print (u_int8_t *buf, int len)
{
  	static const char *nftypes[] = IKE_NOTIFY_TYPES_INITIALIZER;
  	struct notification_payload *np = (struct notification_payload *)buf;
	u_int32_t *replay, *seq;
	u_int32_t doi;
	u_int16_t type;
	u_int8_t *attr;

	if (len < sizeof (struct notification_payload)) {
		printf(" [|payload]");
		return;
	}

	doi  = ntohl (np->doi);
	type = ntohs (np->type);

	if (doi != ISAKMP_DOI && doi != IPSEC_DOI) {
		printf(" (unknown DOI)");
		return;
	}

	printf("\n\t%snotification: ", ike_tab_offset());

	if (type > 0 && type < (sizeof nftypes / sizeof nftypes[0])) {
		printf("%s", nftypes[type]);
		return;
	}
	switch (type) {

	case NOTIFY_IPSEC_RESPONDER_LIFETIME:
		printf("RESPONDER LIFETIME ");
		if (np->spi_size == 16)
			printf("(%s)", ike_get_cookie (&np->data[0],
			    &np->data[8]));
		else
			printf("SPI: 0x%08x", np->data[0]<<24 |
			    np->data[1]<<16 | np->data[2]<<8 | np->data[3]);
		attr = &np->data[np->spi_size];
		ike_tab_level++;
		while ((int)(attr - buf) < len - 4)  /* Skip last 'NONE' attr */
			attr += ike_attribute_print(attr, IPSEC_DOI,
			    len - (attr-buf));
		ike_tab_level--;
		break;

	case NOTIFY_IPSEC_REPLAY_STATUS:
		replay = (u_int32_t *)&np->data[np->spi_size];
		printf("REPLAY STATUS [%sabled] ", *replay ? "en" : "dis");
		if (np->spi_size == 16)
			printf("(%s)", ike_get_cookie (&np->data[0],
			    &np->data[8]));
		else
			printf("SPI: 0x%08x", np->data[0]<<24 |
			    np->data[1]<<16 | np->data[2]<<8 | np->data[3]);
		break;

	case NOTIFY_IPSEC_INITIAL_CONTACT:
		printf("INITIAL CONTACT (%s)", ike_get_cookie (&np->data[0],
		    &np->data[8]));
		break;

	case NOTIFY_STATUS_DPD_R_U_THERE:
	case NOTIFY_STATUS_DPD_R_U_THERE_ACK:
		printf("STATUS_DPD_R_U_THERE%s ",
		    type == NOTIFY_STATUS_DPD_R_U_THERE ? "" : "_ACK");
		if (np->spi_size != 16 ||
		    len < sizeof(struct notification_payload) +
		    sizeof(u_int32_t))
			printf("[bad notify]");
		else {
			seq = (u_int32_t *)&np->data[np->spi_size];
			printf("seq %u", ntohl(*seq));
		}
		break;
		

	default:
	  	printf("%d (unknown)", type);
		break;
	}
}

void
ike_pl_vendor_print (u_int8_t *buf, int len, u_int8_t doi)
{
	struct vendor_payload *vp = (struct vendor_payload *)buf;
	u_int8_t *p;
	int i;

	if (len < sizeof(struct vendor_payload)) {
		printf(" [|payload]");
		return;
	}

	for (i = 0; i < sizeof vendor_ids / sizeof vendor_ids[0]; i ++)
		if (memcmp(vp->vid, vendor_ids[i].vid,
		    vendor_ids[i].len) == 0) {
			printf (" (supports %s)", vendor_ids[i].name);
			return;
		}

	if (doi != IPSEC_DOI)
		return;

	printf(" \"");
	for (p = vp->vid; (int)(p - buf) < len; p++)
		printf("%c", (isprint(*p) ? *p : '.'));
	printf("\"");
}

/* IKE mode-config. */
int
ike_cfg_attribute_print (u_int8_t *buf, int attr_type, int maxlen)
{
	static char *attrs[] = IKE_CFG_ATTRIBUTE_INITIALIZER;
	char ntop_buf[INET6_ADDRSTRLEN];
	struct in_addr in;

	u_int8_t af = buf[0] >> 7;
	u_int16_t type = (buf[0] & 0x7f) << 8 | buf[1];
	u_int16_t len = af ? 2 : buf[2] << 8 | buf[3], p;
	u_int8_t *val = af ? buf + 2 : buf + 4;

	printf("\n\t%sattribute %s = ", ike_tab_offset(),
	    type < (sizeof attrs / sizeof attrs[0]) ? attrs[type] :
	    "<unknown>");

	if ((af == 1 && maxlen < 4) ||
	    (af == 0 && maxlen < (len + 4))) {
		printf("\n\t%s[|attr]", ike_tab_offset());
		return maxlen;
	}

	/* XXX The 2nd term is for bug compatibility with PGPnet.  */
	if (len == 0 || (af && !val[0] && !val[1])) {
		printf("<none>");
		return 4;
	}

	/* XXX Generally lengths are not checked well below.  */
	switch (type) {
	case IKE_CFG_ATTR_INTERNAL_IP4_ADDRESS:
	case IKE_CFG_ATTR_INTERNAL_IP4_NETMASK:
	case IKE_CFG_ATTR_INTERNAL_IP4_DNS:
	case IKE_CFG_ATTR_INTERNAL_IP4_NBNS:
	case IKE_CFG_ATTR_INTERNAL_IP4_DHCP:
		memcpy (&in.s_addr, val, sizeof in);
		printf("%s", inet_ntoa (in));
		break;

	case IKE_CFG_ATTR_INTERNAL_IP6_ADDRESS:
	case IKE_CFG_ATTR_INTERNAL_IP6_NETMASK:
	case IKE_CFG_ATTR_INTERNAL_IP6_DNS:
	case IKE_CFG_ATTR_INTERNAL_IP6_NBNS:
	case IKE_CFG_ATTR_INTERNAL_IP6_DHCP:
		printf("%s", inet_ntop (AF_INET6, val, ntop_buf,
		    sizeof ntop_buf));
		break;

	case IKE_CFG_ATTR_INTERNAL_IP4_SUBNET:
		memcpy(&in.s_addr, val, sizeof in);
		printf("%s/", inet_ntoa (in));
		memcpy(&in.s_addr, val + sizeof in, sizeof in);
		printf("%s", inet_ntoa (in));
		break;

	case IKE_CFG_ATTR_INTERNAL_IP6_SUBNET:
		printf("%s/%u", inet_ntop (AF_INET6, val, ntop_buf,
		    sizeof ntop_buf), val[16]);
		break;

	case IKE_CFG_ATTR_INTERNAL_ADDRESS_EXPIRY:
		printf("%u seconds",
		    val[0] << 24 | val[1] << 16 | val[2] << 8 | val[3]);
		break;

	case IKE_CFG_ATTR_APPLICATION_VERSION:
		for (p = 0; p < len; p++)
			printf("%c", isprint(val[p]) ? val[p] : '.');
		break;

	case IKE_CFG_ATTR_SUPPORTED_ATTRIBUTES:
		printf("<%d attributes>", len / 2);
		ike_tab_level++;
		for (p = 0; p < len; p += 2) {
			type = (val[p] << 8 | val[p + 1]) & 0x7fff;
			printf("\n\t%s%s", ike_tab_offset(),
			    type < (sizeof attrs/sizeof attrs[0]) ?
			    attrs[type] : "<unknown>");
		}
		ike_tab_level--;
		break;

	default:
		break;
	}
	return af ? 4 : len + 4;
}

void
ike_pl_attribute_print (u_int8_t *buf, int len)
{
	struct attribute_payload *ap = (struct attribute_payload *)buf;
	static const char *pl_attr[] = IKE_CFG_ATTRIBUTE_TYPE_INITIALIZER;
	u_int8_t *attr = buf + sizeof(struct attribute_payload);

	if (len < sizeof(struct attribute_payload)) {
		printf(" [|payload]");
		return;
	}

	printf(" type: %s Id: %d",
	    ap->type < (sizeof pl_attr/sizeof pl_attr[0]) ? pl_attr[ap->type] :
	    "<unknown>", ap->id);

	while ((int)(attr - buf) < len)
		attr += ike_cfg_attribute_print(attr, ap->type,
		    len - (attr - buf));
}

void
ike_pl_print (u_int8_t type, u_int8_t *buf, u_int8_t doi)
{
	static const char *pltypes[] = IKE_PAYLOAD_TYPES_INITIALIZER;
	static const char *plprivtypes[] = 
	    IKE_PRIVATE_PAYLOAD_TYPES_INITIALIZER;
	static const char *plv2types[] = IKEV2_PAYLOAD_TYPES_INITIALIZER;
	u_int8_t next_type;
	u_int16_t this_len;

	if (&buf[4] > snapend) {
		goto pltrunc;
	}

	next_type = buf[0];
	this_len = buf[2]<<8 | buf[3];

	if (type < PAYLOAD_PRIVATE_MIN && type >= PAYLOAD_IKEV2_SA)
		printf("\n\t%spayload: %s len: %hu", ike_tab_offset(),
		    plv2types[type - PAYLOAD_IKEV2_SA], this_len);
	else if (type < PAYLOAD_PRIVATE_MIN || type >= PAYLOAD_PRIVATE_MAX)
		printf("\n\t%spayload: %s len: %hu", ike_tab_offset(),
		    (type < (sizeof pltypes/sizeof pltypes[0]) ?
			pltypes[type] : "<unknown>"), this_len);
	else
		printf("\n\t%spayload: %s len: %hu", ike_tab_offset(),
		    plprivtypes[type - PAYLOAD_PRIVATE_MIN], this_len);

	if ((type < PAYLOAD_RESERVED_MIN &&
	    (type < sizeof(min_payload_lengths)/sizeof(min_payload_lengths[0]) &&
	    this_len < min_payload_lengths[type])) ||
	    this_len == 0)
		goto pltrunc;

	if ((type > PAYLOAD_PRIVATE_MIN && type < PAYLOAD_PRIVATE_MAX &&
	    this_len < min_priv_payload_lengths[type - PAYLOAD_PRIVATE_MIN]) ||
	    this_len == 0)
		goto pltrunc;
	    
	if (buf + this_len > snapend)
		goto pltrunc;

	ike_tab_level++;
	switch (type) {
	case PAYLOAD_NONE:
		return;

	case PAYLOAD_SA:
		ike_pl_sa_print(buf, this_len);
		break;

	case PAYLOAD_PROPOSAL:
		ike_pl_proposal_print(buf, this_len, doi);
		break;

	case PAYLOAD_TRANSFORM:
		ike_pl_transform_print(buf, this_len, doi);
		break;

	case PAYLOAD_KE:
		ike_pl_ke_print(buf, this_len, doi);
		break;

	case PAYLOAD_ID:
		/* Should only happen with IPsec DOI */
		ipsec_id_print(buf, this_len, doi);
		break;

	case PAYLOAD_CERT:
	case PAYLOAD_CERTREQUEST:
	case PAYLOAD_HASH:
	case PAYLOAD_SIG:
	case PAYLOAD_NONCE:
		break;

	case PAYLOAD_DELETE:
		ike_pl_delete_print(buf, this_len);
		break;

	case PAYLOAD_NOTIFICATION:
	  	ike_pl_notification_print(buf, this_len);
		break;

	case PAYLOAD_VENDOR:
		ike_pl_vendor_print(buf, this_len, doi);
		break;

	case PAYLOAD_ATTRIBUTE:
		ike_pl_attribute_print(buf, this_len);
		break;

	case PAYLOAD_SAK:
	case PAYLOAD_SAT:
	case PAYLOAD_KD:
	case PAYLOAD_SEQ:
	case PAYLOAD_POP:
	case PAYLOAD_NAT_D:
		break;

	case PAYLOAD_NAT_OA:
		/* RFC3947 NAT-OA uses a subset of the ID payload */
		ipsec_id_print(buf, this_len, doi);
		break;

	case PAYLOAD_NAT_D_DRAFT:
		break;

	case PAYLOAD_NAT_OA_DRAFT:
		ipsec_id_print(buf, this_len, doi);
		break;

	default:
		break;
	}
	ike_tab_level--;

	if (next_type)  /* Recurse over next payload */
		ike_pl_print(next_type, buf + this_len, doi);

	return;

pltrunc:
	if (doi == ISAKMP_DOI)
		printf(" [|isakmp]");
	else
		printf(" [|ipsec]");
}
