/*	$OpenBSD: print.c,v 1.69 2025/09/15 11:52:07 job Exp $ */
/*
 * Copyright (c) 2021 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2019 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <sys/socket.h>
#include <arpa/inet.h>

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <openssl/evp.h>

#include "extern.h"
#include "json.h"

static const char *
pretty_key_id(const char *hex)
{
	static char buf[128];	/* bigger than SHA_DIGEST_LENGTH * 3 */
	size_t i;

	for (i = 0; i < sizeof(buf) && *hex != '\0'; i++) {
		if (i % 3 == 2)
			buf[i] = ':';
		else
			buf[i] = *hex++;
	}
	if (i == sizeof(buf))
		memcpy(buf + sizeof(buf) - 4, "...", 4);
	else
		buf[i] = '\0';
	return buf;
}

char *
nid2str(int nid)
{
	static char buf[128];
	const char *name;

	if ((name = OBJ_nid2ln(nid)) == NULL)
		name = OBJ_nid2sn(nid);
	if (name == NULL)
		name = "unknown";

	snprintf(buf, sizeof(buf), "nid %d (%s)", nid, name);

	return buf;
}

const char *
purpose2str(enum cert_purpose purpose)
{
	switch (purpose) {
	case CERT_PURPOSE_INVALID:
		return "invalid cert";
	case CERT_PURPOSE_TA:
		return "TA cert";
	case CERT_PURPOSE_CA:
		return "CA cert";
	case CERT_PURPOSE_EE:
		return "EE cert";
	case CERT_PURPOSE_BGPSEC_ROUTER:
		return "BGPsec Router cert";
	default:
		return "unknown certificate purpose";
	}
}

char *
time2str(time_t t)
{
	static char buf[64];
	struct tm tm;

	if (gmtime_r(&t, &tm) == NULL)
		return "could not convert time";

	strftime(buf, sizeof(buf), "%a %d %b %Y %T %z", &tm);

	return buf;
}

void
tal_print(const struct tal *p)
{
	char			*ski;
	const unsigned char	*der;
	X509_PUBKEY		*pubkey;
	size_t			 i;

	der = p->pkey;
	if ((pubkey = d2i_X509_PUBKEY(NULL, &der, p->pkeysz)) == NULL)
		errx(1, "d2i_X509_PUBKEY failed");

	if ((ski = x509_pubkey_get_ski(pubkey, p->descr)) == NULL)
		errx(1, "x509_pubkey_get_ski failed");

	if (outformats & FORMAT_JSON) {
		json_do_string("type", "tal");
		json_do_string("name", p->descr);
		json_do_string("ski", ski);
		json_do_array("trust_anchor_locations");
		for (i = 0; i < p->num_uris; i++)
			json_do_string("tal", p->uri[i]);
		json_do_end();
	} else {
		printf("Trust anchor name:        %s\n", p->descr);
		printf("Subject key identifier:   %s\n", pretty_key_id(ski));
		printf("Trust anchor locations:   ");
		for (i = 0; i < p->num_uris; i++) {
			if (i > 0)
				printf("%26s", "");
			printf("%s\n", p->uri[i]);
		}
	}

	X509_PUBKEY_free(pubkey);
	free(ski);
}

void
x509_print(const X509 *x)
{
	const ASN1_INTEGER	*xserial;
	const X509_NAME		*xissuer;
	char			*issuer = NULL;
	char			*serial = NULL;

	if ((xissuer = X509_get_issuer_name(x)) == NULL) {
		warnx("X509_get_issuer_name failed");
		goto out;
	}

	if ((issuer = X509_NAME_oneline(xissuer, NULL, 0)) == NULL) {
		warnx("X509_NAME_oneline failed");
		goto out;
	}

	if ((xserial = X509_get0_serialNumber(x)) == NULL) {
		warnx("X509_get0_serialNumber failed");
		goto out;
	}

	if ((serial = x509_convert_seqnum(__func__, "serial number",
	    xserial)) == NULL)
		goto out;

	if (outformats & FORMAT_JSON) {
		json_do_string("cert_issuer", issuer);
		json_do_string("cert_serial", serial);
	} else {
		printf("Certificate issuer:       %s\n", issuer);
		printf("Certificate serial:       %s\n", serial);
	}

 out:
	free(issuer);
	free(serial);
}

static void
as_resources_print(struct cert_as *ases, size_t num_ases)
{
	size_t i;

	for (i = 0; i < num_ases; i++) {
		if (outformats & FORMAT_JSON)
			json_do_object("resource", 1);
		switch (ases[i].type) {
		case CERT_AS_ID:
			if (outformats & FORMAT_JSON) {
				json_do_uint("asid", ases[i].id);
			} else {
				if (i > 0)
					printf("%26s", "");
				printf("AS: %u", ases[i].id);
			}
			break;
		case CERT_AS_INHERIT:
			if (outformats & FORMAT_JSON) {
				json_do_bool("asid_inherit", 1);
			} else {
				if (i > 0)
					printf("%26s", "");
				printf("AS: inherit");
			}
			break;
		case CERT_AS_RANGE:
			if (outformats & FORMAT_JSON) {
				json_do_object("asrange", 1);
				json_do_uint("min", ases[i].range.min);
				json_do_uint("max", ases[i].range.max);
				json_do_end();
			} else {
				if (i > 0)
					printf("%26s", "");
				printf("AS: %u -- %u", ases[i].range.min,
				    ases[i].range.max);
			}
			break;
		}
		if (outformats & FORMAT_JSON)
			json_do_end();
		else
			printf("\n");
	}
}

static void
ip_resources_print(struct cert_ip *ips, size_t num_ips, size_t num_ases)
{
	char buf1[64], buf2[64];
	size_t i;
	int sockt;

	for (i = 0; i < num_ips; i++) {
		if (outformats & FORMAT_JSON)
			json_do_object("resource", 1);
		switch (ips[i].type) {
		case CERT_IP_INHERIT:
			if (outformats & FORMAT_JSON) {
				json_do_bool("ip_inherit", 1);
			} else {
				if (i > 0 || num_ases > 0)
					printf("%26s", "");
				printf("IP: inherit");
			}
			break;
		case CERT_IP_ADDR:
			ip_addr_print(&ips[i].ip, ips[i].afi, buf1,
			    sizeof(buf1));
			if (outformats & FORMAT_JSON) {
				json_do_string("ip_prefix", buf1);
			} else {
				if (i > 0 || num_ases > 0)
					printf("%26s", "");
				printf("IP: %s", buf1);
			}
			break;
		case CERT_IP_RANGE:
			sockt = (ips[i].afi == AFI_IPV4) ?
			    AF_INET : AF_INET6;
			inet_ntop(sockt, ips[i].min, buf1, sizeof(buf1));
			inet_ntop(sockt, ips[i].max, buf2, sizeof(buf2));
			if (outformats & FORMAT_JSON) {
				json_do_object("ip_range", 1);
				json_do_string("min", buf1);
				json_do_string("max", buf2);
				json_do_end();
			} else {
				if (i > 0 || num_ases > 0)
					printf("%26s", "");
				printf("IP: %s -- %s", buf1, buf2);
			}
			break;
		}
		if (outformats & FORMAT_JSON)
			json_do_end();
		else
			printf("\n");
	}
}

void
cert_print(const struct cert *p)
{
	if (outformats & FORMAT_JSON) {
		if (p->pubkey != NULL)
			json_do_string("type", "router_key");
		else
			json_do_string("type", "ca_cert");
		json_do_string("ski", p->ski);
		if (p->aki != NULL)
			json_do_string("aki", p->aki);
		x509_print(p->x509);
		if (p->aia != NULL)
			json_do_string("aia", p->aia);
		if (p->mft != NULL)
			json_do_string("manifest", p->mft);
		if (p->repo != NULL)
			json_do_string("carepository", p->repo);
		if (p->notify != NULL)
			json_do_string("notify_url", p->notify);
		if (p->pubkey != NULL)
			json_do_string("router_key", p->pubkey);
		json_do_int("valid_since", p->notbefore);
		json_do_int("valid_until", p->notafter);
		if (p->expires)
			json_do_int("expires", p->expires);
		json_do_array("subordinate_resources");
	} else {
		printf("Subject key identifier:   %s\n", pretty_key_id(p->ski));
		if (p->aki != NULL)
			printf("Authority key identifier: %s\n",
			    pretty_key_id(p->aki));
		x509_print(p->x509);
		if (p->aia != NULL)
			printf("Authority info access:    %s\n", p->aia);
		if (p->mft != NULL)
			printf("Manifest:                 %s\n", p->mft);
		if (p->repo != NULL)
			printf("caRepository:             %s\n", p->repo);
		if (p->notify != NULL)
			printf("Notify URL:               %s\n", p->notify);
		if (p->pubkey != NULL) {
			printf("BGPsec ECDSA public key:  %s\n",
			    p->pubkey);
			printf("Router key not before:    %s\n",
			    time2str(p->notbefore));
			printf("Router key not after:     %s\n",
			    time2str(p->notafter));
		} else {
			printf("Certificate not before:   %s\n",
			    time2str(p->notbefore));
			printf("Certificate not after:    %s\n",
			    time2str(p->notafter));
		}
		printf("Subordinate resources:    ");
	}

	as_resources_print(p->ases, p->num_ases);
	ip_resources_print(p->ips, p->num_ips, p->num_ases);

	if (outformats & FORMAT_JSON)
		json_do_end();
}

static char *
crl_parse_number(const X509_CRL *x509_crl)
{
	ASN1_INTEGER	*aint = NULL;
	int		 crit;
	char		*s = NULL;

	aint = X509_CRL_get_ext_d2i(x509_crl, NID_crl_number, &crit, NULL);
	if (aint == NULL) {
		if (crit != -1)
			warnx("%s: RFC 6487, section 5: "
			    "failed to parse CRL number", __func__);
		else
			warnx("%s: RFC 6487, section 5: missing CRL number",
			    __func__);
		goto out;
	}
	if (crit != 0) {
		warnx("%s: RFC 6487, section 5: CRL number not non-critical",
		    __func__);
		goto out;
	}

	s = x509_convert_seqnum(__func__, "CRL Number", aint);

 out:
	ASN1_INTEGER_free(aint);
	return s;
}

void
crl_print(const struct crl *p)
{
	STACK_OF(X509_REVOKED)	*revlist;
	X509_REVOKED *rev;
	X509_NAME *xissuer;
	int i;
	char *issuer, *serial;
	time_t t;

	if (outformats & FORMAT_JSON) {
		json_do_string("type", "crl");
		json_do_string("aki", p->aki);
	} else
		printf("Authority key identifier: %s\n", pretty_key_id(p->aki));

	xissuer = X509_CRL_get_issuer(p->x509_crl);
	issuer = X509_NAME_oneline(xissuer, NULL, 0);
	if (issuer != NULL) {
		char *number;

		if ((number = crl_parse_number(p->x509_crl)) != NULL) {
			if (outformats & FORMAT_JSON) {
				json_do_string("crl_issuer", issuer);
				json_do_string("crl_serial", number);
			} else {
				printf("CRL issuer:               %s\n",
				    issuer);
				printf("CRL serial number:        %s\n",
				    number);
			}
			free(number);
		}
	}
	free(issuer);

	if (outformats & FORMAT_JSON) {
		json_do_int("valid_since", p->thisupdate);
		json_do_int("valid_until", p->nextupdate);
		json_do_array("revoked_certs");
	} else {
		printf("CRL this update:          %s\n",
		    time2str(p->thisupdate));
		printf("CRL next update:          %s\n",
		    time2str(p->nextupdate));
		printf("Revoked Certificates:\n");
	}

	revlist = X509_CRL_get_REVOKED(p->x509_crl);
	for (i = 0; i < sk_X509_REVOKED_num(revlist); i++) {
		rev = sk_X509_REVOKED_value(revlist, i);
		serial = x509_convert_seqnum(__func__, "serial number",
		    X509_REVOKED_get0_serialNumber(rev));
		if (!x509_get_time(X509_REVOKED_get0_revocationDate(rev), &t))
			errx(1, "x509_get_time() failed - malformed ASN.1?");
		if (serial != NULL) {
			if (outformats & FORMAT_JSON) {
				json_do_object("cert", 1);
				json_do_string("serial", serial);
				json_do_string("date", time2str(t));
				json_do_end();
			} else
				printf("%25s Serial: %8s   Revocation Date: %s"
				    "\n", "", serial, time2str(t));
		}
		free(serial);
	}

	if (outformats & FORMAT_JSON)
		json_do_end();
	else if (i == 0)
		printf("No Revoked Certificates\n");
}

void
mft_print(const struct cert *c, const struct mft *p)
{
	size_t i;
	char *hash;

	if (outformats & FORMAT_JSON) {
		json_do_string("type", "manifest");
		json_do_string("ski", c->ski);
		x509_print(c->x509);
		json_do_string("aki", c->aki);
		json_do_string("aia", c->aia);
		json_do_string("sia", c->signedobj);
		json_do_string("manifest_number", p->seqnum);
		json_do_int("signing_time", p->signtime);
		json_do_int("valid_since", p->thisupdate);
		json_do_int("valid_until", p->nextupdate);
		if (p->expires)
			json_do_int("expires", p->expires);
	} else {
		printf("Subject key identifier:   %s\n", pretty_key_id(c->ski));
		printf("Authority key identifier: %s\n", pretty_key_id(c->aki));
		x509_print(c->x509);
		printf("Authority info access:    %s\n", c->aia);
		printf("Subject info access:      %s\n", c->signedobj);
		printf("Manifest number:          %s\n", p->seqnum);
		printf("Signing time:             %s\n", time2str(p->signtime));
		printf("Manifest this update:     %s\n",
		    time2str(p->thisupdate));
		printf("Manifest next update:     %s\n",
		    time2str(p->nextupdate));
		printf("Files and hashes:         ");
	}

	if (outformats & FORMAT_JSON)
		json_do_array("filesandhashes");
	for (i = 0; i < p->filesz; i++) {
		if (base64_encode(p->files[i].hash, sizeof(p->files[i].hash),
		    &hash) == -1)
			errx(1, "base64_encode failure");

		if (outformats & FORMAT_JSON) {
			json_do_object("filehash", 1);
			json_do_string("filename", p->files[i].file);
			json_do_string("hash", hash);
			json_do_end();
		} else {
			if (i > 0)
				printf("%26s", "");
			printf("%zu: %s (hash: %s)\n", i + 1, p->files[i].file,
			    hash);
		}

		free(hash);
	}
	if (outformats & FORMAT_JSON)
		json_do_end();
}

void
roa_print(const struct cert *c, const struct roa *p)
{
	char	 buf[128];
	size_t	 i;

	if (outformats & FORMAT_JSON) {
		json_do_string("type", "roa");
		json_do_string("ski", c->ski);
		x509_print(c->x509);
		json_do_string("aki", c->aki);
		json_do_string("aia", c->aia);
		json_do_string("sia", c->signedobj);
		json_do_int("signing_time", p->signtime);
		json_do_int("valid_since", c->notbefore);
		json_do_int("valid_until", c->notafter);
		if (p->expires)
			json_do_int("expires", p->expires);
	} else {
		printf("Subject key identifier:   %s\n", pretty_key_id(c->ski));
		x509_print(c->x509);
		printf("Authority key identifier: %s\n", pretty_key_id(c->aki));
		printf("Authority info access:    %s\n", c->aia);
		printf("Subject info access:      %s\n", c->signedobj);
		printf("Signing time:             %s\n", time2str(p->signtime));
		printf("ROA not before:           %s\n",
		    time2str(c->notbefore));
		printf("ROA not after:            %s\n", time2str(c->notafter));
		printf("asID:                     %u\n", p->asid);
		printf("IP address blocks:        ");
	}

	if (outformats & FORMAT_JSON)
		json_do_array("vrps");
	for (i = 0; i < p->num_ips; i++) {
		ip_addr_print(&p->ips[i].addr, p->ips[i].afi, buf, sizeof(buf));

		if (outformats & FORMAT_JSON) {
			json_do_object("vrp", 1);
			json_do_string("prefix", buf);
			json_do_uint("asid", p->asid);
			json_do_uint("maxlen", p->ips[i].maxlength);
			json_do_end();
		} else {
			if (i > 0)
				printf("%26s", "");
			printf("%s maxlen: %hhu\n", buf, p->ips[i].maxlength);
		}
	}
	if (outformats & FORMAT_JSON)
		json_do_end();
}

void
spl_print(const struct cert *c, const struct spl *s)
{
	char	 buf[128];
	size_t	 i;

	if (outformats & FORMAT_JSON) {
		json_do_string("type", "spl");
		json_do_string("ski", c->ski);
		x509_print(c->x509);
		json_do_string("aki", c->aki);
		json_do_string("aia", c->aia);
		json_do_string("sia", c->signedobj);
		json_do_int("signing_time", s->signtime);
		json_do_int("valid_since", c->notbefore);
		json_do_int("valid_until", c->notafter);
		if (s->expires)
			json_do_int("expires", s->expires);
		json_do_int("asid", s->asid);
	} else {
		printf("Subject key identifier:   %s\n", pretty_key_id(c->ski));
		x509_print(c->x509);
		printf("Authority key identifier: %s\n", pretty_key_id(c->aki));
		printf("Authority info access:    %s\n", c->aia);
		printf("Subject info access:      %s\n", c->signedobj);
		printf("Signing time:             %s\n", time2str(s->signtime));
		printf("SPL not before:           %s\n",
		    time2str(c->notbefore));
		printf("SPL not after:            %s\n", time2str(c->notafter));
		printf("asID:                     %u\n", s->asid);
		printf("Originated IP Prefixes:   ");
	}

	if (outformats & FORMAT_JSON)
		json_do_array("prefixes");
	for (i = 0; i < s->num_prefixes; i++) {
		ip_addr_print(&s->prefixes[i].prefix, s->prefixes[i].afi, buf,
		    sizeof(buf));

		if (outformats & FORMAT_JSON) {
			json_do_string("prefix", buf);
		} else {
			if (i > 0)
				printf("%26s", "");
			printf("%s\n", buf);
		}
	}
	if (outformats & FORMAT_JSON)
		json_do_end();
}

void
gbr_print(const struct cert *c, const struct gbr *p)
{
	if (outformats & FORMAT_JSON) {
		json_do_string("type", "gbr");
		json_do_string("ski", c->ski);
		x509_print(c->x509);
		json_do_string("aki", c->aki);
		json_do_string("aia", c->aia);
		json_do_string("sia", c->signedobj);
		json_do_int("signing_time", p->signtime);
		json_do_int("valid_since", c->notbefore);
		json_do_int("valid_until", c->notafter);
		if (p->expires)
			json_do_int("expires", p->expires);
		json_do_string("vcard", p->vcard);
	} else {
		printf("Subject key identifier:   %s\n", pretty_key_id(c->ski));
		x509_print(c->x509);
		printf("Authority key identifier: %s\n", pretty_key_id(c->aki));
		printf("Authority info access:    %s\n", c->aia);
		printf("Subject info access:      %s\n", c->signedobj);
		printf("Signing time:             %s\n", time2str(p->signtime));
		printf("GBR not before:           %s\n",
		    time2str(c->notbefore));
		printf("GBR not after:            %s\n", time2str(c->notafter));
		printf("vcard:\n%s", p->vcard);
	}
}

void
rsc_print(const struct cert *c, const struct rsc *p)
{
	char	*hash;
	size_t	 i;

	if (outformats & FORMAT_JSON) {
		json_do_string("type", "rsc");
		json_do_string("ski", c->ski);
		x509_print(c->x509);
		json_do_string("aki", c->aki);
		json_do_string("aia", c->aia);
		json_do_int("signing_time", p->signtime);
		json_do_int("valid_since", c->notbefore);
		json_do_int("valid_until", c->notafter);
		if (p->expires)
			json_do_int("expires", c->expires);
		json_do_array("signed_with_resources");
	} else {
		printf("Subject key identifier:   %s\n", pretty_key_id(c->ski));
		printf("Authority key identifier: %s\n", pretty_key_id(c->aki));
		x509_print(c->x509);
		printf("Authority info access:    %s\n", c->aia);
		printf("Signing time:             %s\n", time2str(p->signtime));
		printf("RSC not before:           %s\n",
		    time2str(c->notbefore));
		printf("RSC not after:            %s\n", time2str(c->notafter));
		printf("Signed with resources:    ");
	}

	as_resources_print(p->ases, p->num_ases);
	ip_resources_print(p->ips, p->num_ips, p->num_ases);

	if (outformats & FORMAT_JSON) {
		json_do_end();
		json_do_array("filenamesandhashes");
	} else
		printf("Filenames and hashes:     ");

	for (i = 0; i < p->num_files; i++) {
		if (base64_encode(p->files[i].hash, sizeof(p->files[i].hash),
		    &hash) == -1)
			errx(1, "base64_encode failure");

		if (outformats & FORMAT_JSON) {
			json_do_object("filehash", 1);
			if (p->files[i].filename)
				json_do_string("filename",
				    p->files[i].filename);
			json_do_string("hash_digest", hash);
			json_do_end();
		} else {
			if (i > 0)
				printf("%26s", "");
			printf("%zu: %s (hash: %s)\n", i + 1,
			    p->files[i].filename ? p->files[i].filename
			    : "no filename", hash);
		}

		free(hash);
	}

	if (outformats & FORMAT_JSON)
		json_do_end();
}

void
aspa_print(const struct cert *c, const struct aspa *p)
{
	size_t	i;

	if (outformats & FORMAT_JSON) {
		json_do_string("type", "aspa");
		json_do_string("ski", c->ski);
		x509_print(c->x509);
		json_do_string("aki", c->aki);
		json_do_string("aia", c->aia);
		json_do_string("sia", c->signedobj);
		json_do_int("signing_time", p->signtime);
		json_do_int("valid_since", c->notbefore);
		json_do_int("valid_until", c->notafter);
		if (p->expires)
			json_do_int("expires", p->expires);
		json_do_uint("customer_asid", p->custasid);
		json_do_array("providers");
	} else {
		printf("Subject key identifier:   %s\n", pretty_key_id(c->ski));
		x509_print(c->x509);
		printf("Authority key identifier: %s\n", pretty_key_id(c->aki));
		printf("Authority info access:    %s\n", c->aia);
		printf("Subject info access:      %s\n", c->signedobj);
		printf("Signing time:             %s\n", time2str(p->signtime));
		printf("ASPA not before:          %s\n",
		    time2str(c->notbefore));
		printf("ASPA not after:           %s\n", time2str(c->notafter));
		printf("Customer ASID:            %u\n", p->custasid);
		printf("Providers:                ");
	}

	for (i = 0; i < p->num_providers; i++) {
		if (outformats & FORMAT_JSON)
			json_do_uint("asid", p->providers[i]);
		else {
			if (i > 0)
				printf("%26s", "");
			printf("AS: %u\n", p->providers[i]);
		}
	}

	if (outformats & FORMAT_JSON)
		json_do_end();
}

static void
takey_print(char *name, const struct takey *t)
{
	char	*spki = NULL;
	size_t	 i, j = 0;

	if (base64_encode(t->pubkey, t->pubkeysz, &spki) != 0)
		errx(1, "base64_encode failed in %s", __func__);

	if (outformats & FORMAT_JSON) {
		json_do_object("takey", 0);
		json_do_string("name", name);
		json_do_array("comments");
		for (i = 0; i < t->num_comments; i++)
			json_do_string("comment", t->comments[i]);
		json_do_end();
		json_do_array("uris");
		for (i = 0; i < t->num_uris; i++)
			json_do_string("uri", t->uris[i]);
		json_do_end();
		json_do_string("spki", spki);
		json_do_end();
	} else {
		printf("TAL derived from the '%s' Trust Anchor Key:\n\n", name);

		for (i = 0; i < t->num_comments; i++)
			printf("\t# %s\n", t->comments[i]);
		for (i = 0; i < t->num_uris; i++)
			printf("\t%s\n", t->uris[i]);
		printf("\n\t");
		for (i = 0; i < strlen(spki); i++) {
			printf("%c", spki[i]);
			if ((++j % 64) == 0)
				printf("\n\t");
		}
		printf("\n\n");
	}

	free(spki);
}

void
tak_print(const struct cert *c, const struct tak *p)
{
	if (outformats & FORMAT_JSON) {
		json_do_string("type", "tak");
		json_do_string("ski", c->ski);
		x509_print(c->x509);
		json_do_string("aki", c->aki);
		json_do_string("aia", c->aia);
		json_do_string("sia", c->signedobj);
		json_do_int("signing_time", p->signtime);
		json_do_int("valid_since", c->notbefore);
		json_do_int("valid_until", c->notafter);
		if (p->expires)
			json_do_int("expires", p->expires);
		json_do_array("takeys");
	} else {
		printf("Subject key identifier:   %s\n", pretty_key_id(c->ski));
		x509_print(c->x509);
		printf("Authority key identifier: %s\n", pretty_key_id(c->aki));
		printf("Authority info access:    %s\n", c->aia);
		printf("Subject info access:      %s\n", c->signedobj);
		printf("Signing time:             %s\n", time2str(p->signtime));
		printf("TAK not before:           %s\n",
		    time2str(c->notbefore));
		printf("TAK not after:            %s\n", time2str(c->notafter));
	}

	takey_print("current", p->current);
	if (p->predecessor != NULL)
		takey_print("predecessor", p->predecessor);
	if (p->successor != NULL)
		takey_print("successor", p->successor);

	if (outformats & FORMAT_JSON)
		json_do_end();
}

void
geofeed_print(const struct cert *c, const struct geofeed *p)
{
	char	 buf[128];
	size_t	 i;

	if (outformats & FORMAT_JSON) {
		json_do_string("type", "geofeed");
		json_do_string("ski", c->ski);
		x509_print(c->x509);
		json_do_string("aki", c->aki);
		json_do_string("aia", c->aia);
		json_do_int("signing_time", p->signtime);
		json_do_int("valid_since", c->notbefore);
		json_do_int("valid_until", c->notafter);
		if (p->expires)
			json_do_int("expires", p->expires);
		json_do_array("records");
	} else {
		printf("Subject key identifier:   %s\n", pretty_key_id(c->ski));
		x509_print(c->x509);
		printf("Authority key identifier: %s\n", pretty_key_id(c->aki));
		printf("Authority info access:    %s\n", c->aia);
		printf("Signing time:             %s\n", time2str(p->signtime));
		printf("Geofeed not before:       %s\n",
		    time2str(c->notbefore));
		printf("Geofeed not after:        %s\n", time2str(c->notafter));
		printf("Geofeed CSV records:      ");
	}

	for (i = 0; i < p->num_geoips; i++) {
		if (p->geoips[i].ip->type != CERT_IP_ADDR)
			continue;

		ip_addr_print(&p->geoips[i].ip->ip, p->geoips[i].ip->afi, buf,
		    sizeof(buf));
		if (outformats & FORMAT_JSON) {
			json_do_object("geoip", 1);
			json_do_string("prefix", buf);
			json_do_string("location", p->geoips[i].loc);
			json_do_end();
		} else {
			if (i > 0)
				printf("%26s", "");
			printf("IP: %s (%s)\n", buf, p->geoips[i].loc);
		}
	}

	if (outformats & FORMAT_JSON)
		json_do_end();
}

static void
print_ccr_mftstate(struct ccr *ccr)
{
	char *aki, *hash;
	struct ccr_mft *ccr_mft;

	if (base64_encode(ccr->mfts_hash, SHA256_DIGEST_LENGTH, &hash) == -1)
		errx(1, "base64_encode");

	if (outformats & FORMAT_JSON) {
		json_do_object("manifest_state", 0);
		json_do_int("most_recent_update", ccr->most_recent_update);
		json_do_string("hash", hash);
		json_do_array("refs");
	} else {
		printf("Manifest state hash:      %s\n", hash);
		printf("Manifest last update:     %s\n",
		    time2str(ccr->most_recent_update));
		printf("Manifest references:\n");
	}
	free(hash);

	RB_FOREACH(ccr_mft, ccr_mft_tree, &ccr->mfts) {
		if (base64_encode(ccr_mft->hash, SHA256_DIGEST_LENGTH, &hash)
		    == -1)
			errx(1, "base64_encode");
		aki = hex_encode(ccr_mft->aki, SHA_DIGEST_LENGTH);

		if (outformats & FORMAT_JSON) {
			json_do_object("ref", 1);
			json_do_string("hash", hash);
			json_do_uint("size", ccr_mft->size);
			json_do_string("aki", aki);
			json_do_string("seqnum", ccr_mft->seqnum);
			json_do_int("thisupdate", ccr_mft->thisupdate);
			json_do_string("sia", ccr_mft->sia);
			json_do_end();
		} else {
			printf("%26shash:%s size:%zu aki:%s seqnum:%s "
			    "thisupdate:%lld sia:%s\n", "", hash,
			    ccr_mft->size, aki, ccr_mft->seqnum,
			    (long long)ccr_mft->thisupdate, ccr_mft->sia);
		}

		free(aki);
		free(hash);
	}
	if (outformats & FORMAT_JSON) {
		json_do_end();
		json_do_end();
	}
}

static void
print_ccr_roastate(struct ccr *ccr)
{
	char buf[64], *hash;
	struct vrp *vrp;

	if (base64_encode(ccr->vrps_hash, SHA256_DIGEST_LENGTH, &hash) == -1)
		errx(1, "base64_encode");

	if (outformats & FORMAT_JSON) {
		json_do_object("roapayload_state", 0);
		json_do_string("hash", hash);
		json_do_array("vrps");
	} else {
		printf("ROA payload state hash:   %s\n", hash);
		printf("ROA payload entries:\n");
	}
	free(hash);

	RB_FOREACH(vrp, ccr_vrp_tree, &ccr->vrps) {
		ip_addr_print(&vrp->addr, vrp->afi, buf, sizeof(buf));

		if (outformats & FORMAT_JSON) {
			json_do_object("vrp", 1);
			json_do_string("prefix", buf);
			json_do_int("asn", vrp->asid);
			if (vrp->maxlength)
				json_do_int("maxlen", vrp->maxlength);
			json_do_end();
		} else {
			printf("%26s%s", "", buf);
			if (vrp->maxlength)
				printf("-%hhu", vrp->maxlength);
			printf(" AS %u\n", vrp->asid);
		}

	}
	if (outformats & FORMAT_JSON) {
		json_do_end();
		json_do_end();
	}
}

static void
print_ccr_aspastate(struct ccr *ccr)
{
	char *hash;
	struct vap *vap;
	size_t i;

	if (base64_encode(ccr->vaps_hash, SHA256_DIGEST_LENGTH, &hash) == -1)
		errx(1, "base64_encode");

	if (outformats & FORMAT_JSON) {
		json_do_object("aspapayload_state", 0);
		json_do_string("hash", hash);
		json_do_array("vaps");
	} else {
		printf("ASPA payload state hash:  %s\n", hash);
		printf("ASPA payload entries:\n");
	}
	free(hash);

	RB_FOREACH(vap, vap_tree, &ccr->vaps) {
		if (outformats & FORMAT_JSON) {
			json_do_object("vap", 1);
			json_do_uint("customer_asid", vap->custasid);
			json_do_array("providers");
		} else {
			printf("%26s", "");
			printf("customer: %d providers: ", vap->custasid);
		}

		for (i = 0; i < vap->num_providers; i++) {
			if (outformats & FORMAT_JSON)
				json_do_uint("provider", vap->providers[i]);
			else {
				if (i > 0)
					printf(", ");
				printf("%u", vap->providers[i]);
			}
		}
		if (outformats & FORMAT_JSON) {
			json_do_end();
			json_do_end();
		} else
			printf("\n");
	}

	if (outformats & FORMAT_JSON) {
		json_do_end();
		json_do_end();
	}
}

static void
print_ccr_tastate(struct ccr *ccr)
{
	char *hash, *ski;
	struct ccr_tas_ski *cts;
	int i = 0;

	if (base64_encode(ccr->tas_hash, SHA256_DIGEST_LENGTH, &hash) == -1)
		errx(1, "base64_encode");

	if (outformats & FORMAT_JSON) {
		json_do_object("trustanchor_state", 0);
		json_do_string("hash", hash);
		json_do_array("skis");
	} else {
		printf("Trust anchor state hash:  %s\n", hash);
		printf("Trust anchor keyids:      ");
	}

	free(hash);

	RB_FOREACH(cts, ccr_tas_tree, &ccr->tas) {
		ski = hex_encode(cts->keyid, sizeof(cts->keyid));

		if (outformats & FORMAT_JSON) {
			json_do_string("ski", ski);
		} else {
			if (++i > 1)
				printf(", ");
			printf("%s", ski);
		}

		free(ski);
	}

	if (outformats & FORMAT_JSON) {
		json_do_end();
		json_do_end();
	} else {
		printf("\n");
	}
}

static void
print_ccr_rkstate(struct ccr *ccr)
{
	char *hash;
	struct brk *brk;

	if (base64_encode(ccr->brks_hash, SHA256_DIGEST_LENGTH, &hash) == -1)
		errx(1, "base64_encode");

	if (outformats & FORMAT_JSON) {
		json_do_object("routerkey_state", 0);
		json_do_string("hash", hash);
		json_do_array("routerkeys");
		RB_FOREACH(brk, brk_tree, &ccr->brks) {
			json_do_object("brk", 0);
			json_do_int("asn", brk->asid);
			json_do_string("ski", brk->ski);
			json_do_string("pubkey", brk->pubkey);
			json_do_end(); /* brk */
		}
		json_do_end(); /* routerkeys */
		json_do_end(); /* routerkey_state */
	} else {
		printf("Router key state hash:    %s\n", hash);
		printf("Router keys:\n");
		RB_FOREACH(brk, brk_tree, &ccr->brks) {
			printf("%26s", "");
			printf("asid:%d ", brk->asid);
			printf("ski:%s ", brk->ski);
			printf("pubkey:%s\n", brk->pubkey);
		}
	}

	free(hash);
}

void
ccr_print(struct ccr *ccr)
{
	if (outformats & FORMAT_JSON) {
		json_do_string("type", "ccr");
		json_do_int("produced_at", ccr->producedat);
	} else {
		printf("CCR produced at:          %s\n",
		    time2str(ccr->producedat));
	}

	if (ccr->mfts_hash != NULL)
		print_ccr_mftstate(ccr);

	if (ccr->vrps_hash != NULL)
		print_ccr_roastate(ccr);

	if (ccr->vaps_hash != NULL)
		print_ccr_aspastate(ccr);

	if (ccr->tas_hash != NULL)
		print_ccr_tastate(ccr);

	if (ccr->brks_hash != NULL)
		print_ccr_rkstate(ccr);
}
