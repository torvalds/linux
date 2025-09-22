/*	$OpenBSD: constraints.c,v 1.18 2023/12/13 05:59:50 tb Exp $	*/
/*
 * Copyright (c) 2020 Bob Beck <beck@openbsd.org>
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

#include <err.h>
#include <string.h>

#include <openssl/safestack.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include "x509_internal.h"

#define FAIL(msg, ...)							\
do {									\
	fprintf(stderr, "[%s:%d] FAIL: ", __FILE__, __LINE__);		\
	fprintf(stderr, msg, ##__VA_ARGS__);				\
} while(0)

unsigned char *valid_hostnames[] = {
	"openbsd.org",
	"op3nbsd.org",
	"org",
	"3openbsd.com",
	"3-0penb-d.c-m",
	"a",
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.com",
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
	"open_bsd.org", /* because this is liberal */
	NULL,
};

unsigned char *valid_sandns_names[] = {
	"*.ca",
	"*.op3nbsd.org",
	"c*.openbsd.org",
	"foo.*.d*.c*.openbsd.org",
	NULL,
};

unsigned char *valid_domain_constraints[] = {
	"",
	".ca",
	".op3nbsd.org",
	".aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
	"www.openbsd.org",
	NULL,
};

unsigned char *valid_mbox_names[] = {
	"\"!#$%&\\\"*+-/=?\002^_`{|}~.\"@openbsd.org",
	"beck@openbsd.org",
	"beck@openbsd.org",
	"beck@op3nbsd.org",
	"beck@org",
	"beck@3openbsd.com",
	"beck@3-0penb-d.c-m",
	"bec@a",
	"beck@aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.com",
	"beck@aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
	"beck@open_bsd.org", /* because this is liberal */
	NULL,
};

unsigned char *invalid_hostnames[] = {
	"openbsd.org.",
	"openbsd..org",
	"openbsd.org-",
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.com",
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.a",
	"-p3nbsd.org",
	"openbs-.org",
	"openbsd\n.org",
	"open\177bsd.org",
	"open\255bsd.org",
	"*.openbsd.org",
	NULL,
};

unsigned char *invalid_sandns_names[] = {
	"",
	".",
	"*.a",
	"*.",
	"*.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.com",
	".aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.a",
	"*.-p3nbsd.org",
	"*.*..openbsd.org",
	"*..openbsd.org",
	".openbsd.org",
	"c*c.openbsd.org",
	NULL,
};

unsigned char *invalid_mbox_names[] = {
	"beck@aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.com",
	"beck@aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.a",
	"beck@.-openbsd.org",
	"beck@.openbsd.org.",
	"beck@.a",
	"beck@.",
	"beck@",
	"beck@.ca",
	"@openbsd.org",
	NULL,
};

unsigned char *invalid_domain_constraints[] = {
	".",
	".a",
	"..",
	".aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.com",
	".aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa."
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.a",
	".-p3nbsd.org",
	"..openbsd.org",
	NULL,
};

unsigned char *invaliduri[] = {
	"https://-www.openbsd.org",
	"https://.www.openbsd.org/",
	"https://www.ope|nbsd.org%",
	"https://www.openbsd.org.#",
	"https://192.168.1.1./",
	"https://192.168.1.1|/",
	"https://.192.168.1.1/",
	"https://192.168..1.1/",
	"https://.2001:0DB8:AC10:FE01::/",
	"https://.2001:0DB8:AC10:FE01::|/",
	"///",
	"//",
	"/",
	"",
	NULL,
};

unsigned char *validuri[] = {
	"https://www.openbsd.org/meep/meep/meep/",
	"https://192.168.1.1/",
	"https://2001:0DB8:AC10:FE01::/",
	"https://192.168.1/",  /* Not an IP, but valid component */
	"https://999.999.999.999/", /* Not an IP, but valid component */
	NULL,
};

static int
test_valid_hostnames(void)
{
	int i, failure = 0;

	for (i = 0; valid_hostnames[i] != NULL; i++) {
		CBS cbs;
		CBS_init(&cbs, valid_hostnames[i], strlen(valid_hostnames[i]));
		if (!x509_constraints_valid_host(&cbs, 0)) {
			FAIL("Valid hostname '%s' rejected\n",
			    valid_hostnames[i]);
			failure = 1;
			goto done;
		}
		CBS_init(&cbs, valid_hostnames[i], strlen(valid_hostnames[i]));
		if (!x509_constraints_valid_sandns(&cbs)) {
			FAIL("Valid sandns '%s' rejected\n",
			    valid_hostnames[i]);
			failure = 1;
			goto done;
		}
	}

 done:
	return failure;
}

static int
test_valid_sandns_names(void)
{
	int i, failure = 0;
	for (i = 0; valid_sandns_names[i] != NULL; i++) {
		CBS cbs;
		CBS_init(&cbs, valid_sandns_names[i],
		    strlen(valid_sandns_names[i]));
		if (!x509_constraints_valid_sandns(&cbs)) {
			FAIL("Valid dnsname '%s' rejected\n",
			    valid_sandns_names[i]);
			failure = 1;
			goto done;
		}
	}

 done:
	return failure;
}

static int
test_valid_domain_constraints(void)
{
	int i, failure = 0;
	for (i = 0; valid_domain_constraints[i] != NULL; i++) {
		CBS cbs;
		CBS_init(&cbs, valid_domain_constraints[i],
		    strlen(valid_domain_constraints[i]));
		if (!x509_constraints_valid_domain_constraint(&cbs)) {
			FAIL("Valid dnsname '%s' rejected\n",
			    valid_domain_constraints[i]);
			failure = 1;
			goto done;
		}
	}

 done:
	return failure;
}

static int
test_valid_mbox_names(void)
{
	struct x509_constraints_name name = {0};
	int i, failure = 0;
	for (i = 0; valid_mbox_names[i] != NULL; i++) {
		CBS cbs;
		CBS_init(&cbs, valid_mbox_names[i],
		    strlen(valid_mbox_names[i]));
		if (!x509_constraints_parse_mailbox(&cbs, &name)) {
			FAIL("Valid mailbox name '%s' rejected\n",
			    valid_mbox_names[i]);
			failure = 1;
			goto done;
		}
		free(name.name);
		name.name = NULL;
		free(name.local);
		name.local = NULL;
	}

 done:
	return failure;
}

static int
test_invalid_hostnames(void)
{
	int i, failure = 0;
	char *nulhost = "www.openbsd.org\0";
	CBS cbs;

	for (i = 0; invalid_hostnames[i] != NULL; i++) {
		CBS_init(&cbs, invalid_hostnames[i],
		    strlen(invalid_hostnames[i]));
		if (x509_constraints_valid_host(&cbs, 0)) {
			FAIL("Invalid hostname '%s' accepted\n",
			    invalid_hostnames[i]);
			failure = 1;
			goto done;
		}
	}
	CBS_init(&cbs, nulhost, strlen(nulhost) + 1);
	if (x509_constraints_valid_host(&cbs, 0)) {
		FAIL("hostname with NUL byte accepted\n");
		failure = 1;
		goto done;
	}
	CBS_init(&cbs, nulhost, strlen(nulhost) + 1);
	if (x509_constraints_valid_sandns(&cbs)) {
		FAIL("sandns with NUL byte accepted\n");
		failure = 1;
		goto done;
	}

 done:
	return failure;
}

static int
test_invalid_sandns_names(void)
{
	int i, failure = 0;
	for (i = 0; invalid_sandns_names[i] != NULL; i++) {
		CBS cbs;
		CBS_init(&cbs, invalid_sandns_names[i],
		    strlen(invalid_sandns_names[i]));
		if (x509_constraints_valid_sandns(&cbs)) {
			FAIL("Valid dnsname '%s' rejected\n",
			    invalid_sandns_names[i]);
			failure = 1;
			goto done;
		}
	}

 done:
	return failure;
}

static int
test_invalid_mbox_names(void)
{
	int i, failure = 0;
	struct x509_constraints_name name = {0};
	for (i = 0; invalid_mbox_names[i] != NULL; i++) {
		CBS cbs;
		CBS_init(&cbs, invalid_mbox_names[i],
		    strlen(invalid_mbox_names[i]));
		if (x509_constraints_parse_mailbox(&cbs, &name)) {
			FAIL("invalid mailbox name '%s' accepted\n",
			    invalid_mbox_names[i]);
			failure = 1;
			goto done;
		}
		free(name.name);
		name.name = NULL;
		free(name.local);
		name.local = NULL;
	}

 done:
	return failure;
}

static int
test_invalid_domain_constraints(void)
{
	int i, failure = 0;
	for (i = 0; invalid_domain_constraints[i] != NULL; i++) {
		CBS cbs;
		CBS_init(&cbs, invalid_domain_constraints[i],
		    strlen(invalid_domain_constraints[i]));
		if (x509_constraints_valid_domain_constraint(&cbs)) {
			FAIL("invalid dnsname '%s' accepted\n",
			    invalid_domain_constraints[i]);
			failure = 1;
			goto done;
		}
	}

 done:
	return failure;
}

static int
test_invalid_uri(void)
{
	int j, failure = 0;
	char *hostpart = NULL;

	for (j = 0; invaliduri[j] != NULL; j++) {
		if (x509_constraints_uri_host(invaliduri[j],
		    strlen(invaliduri[j]), &hostpart) != 0) {
			FAIL("invalid URI '%s' accepted\n",
			    invaliduri[j]);
			failure = 1;
			goto done;
		}
		free(hostpart);
		hostpart = NULL;
	}

 done:
	return failure;
}

static int
test_valid_uri(void)
{
	int j, failure = 0;
	char *hostpart = NULL;

	for (j = 0; validuri[j] != NULL; j++) {
		if (x509_constraints_uri_host(validuri[j],
		    strlen(invaliduri[j]), &hostpart) == 0) {
			FAIL("Valid URI '%s' NOT accepted\n",
			    validuri[j]);
			failure = 1;
			goto done;
		}
		free(hostpart);
		hostpart = NULL;
	}

 done:
	return failure;
}

static int
test_constraints1(void)
{
	char *c;
	size_t cl;
	char *d;
	size_t dl;
	int failure = 0;
	int error = 0;
	int i, j;
	unsigned char *constraints[] = {
		".org",
		".openbsd.org",
		"www.openbsd.org",
		NULL,
	};
	unsigned char *failing[] = {
		".ca",
		"openbsd.ca",
		"org",
		NULL,
	};
	unsigned char *matching[] = {
		"www.openbsd.org",
		NULL,
	};
	unsigned char *matchinguri[] = {
		"https://www.openbsd.org",
		"https://www.openbsd.org/",
		"https://www.openbsd.org?",
		"https://www.openbsd.org#",
		"herp://beck@www.openbsd.org:",
		"spiffe://beck@www.openbsd.org/this/is/so/spiffe/",
		NULL,
	};
	unsigned char *failinguri[] = {
		"https://www.openbsd.ca",
		"https://www.freebsd.com/",
		"https://www.openbsd.net?",
		"https://org#",
		"herp://beck@org:",
		"///",
		"//",
		"/",
		"",
		NULL,
	};
	unsigned char *noauthority[] = {
		"urn:open62541.server.application",
		NULL,
	};
	for (i = 0; constraints[i] != NULL; i++) {
		char *constraint = constraints[i];
		size_t clen = strlen(constraints[i]);
		for (j = 0; matching[j] != NULL; j++) {
			if (!x509_constraints_domain(matching[j],
			    strlen(matching[j]), constraint, clen)) {
				FAIL("constraint '%s' should have matched"
				    " '%s'\n",
				    constraint, matching[j]);
				failure = 1;
				goto done;
			}
		}
		for (j = 0; matchinguri[j] != NULL; j++) {
			error = 0;
			if (!x509_constraints_uri(matchinguri[j],
			    strlen(matchinguri[j]), constraint, clen, &error)) {
				FAIL("constraint '%s' should have matched URI"
				    " '%s' (error %d)\n",
				    constraint, matchinguri[j], error);
				failure = 1;
				goto done;
			}
		}
		for (j = 0; failing[j] != NULL; j++) {
			if (x509_constraints_domain(failing[j],
			    strlen(failing[j]), constraint, clen)) {
				FAIL("constraint '%s' should not have matched"
				    " '%s'\n",
				    constraint, failing[j]);
				failure = 1;
				goto done;
			}
		}
		for (j = 0; failinguri[j] != NULL; j++) {
			error = 0;
			if (x509_constraints_uri(failinguri[j],
			    strlen(failinguri[j]), constraint, clen, &error)) {
				FAIL("constraint '%s' should not have matched URI"
				    " '%s' (error %d)\n",
				    constraint, failinguri[j], error);
				failure = 1;
				goto done;
			}
		}
		for (j = 0; noauthority[j] != NULL; j++) {
			char *hostpart = NULL;
			error = 0;
			if (!x509_constraints_uri_host(noauthority[j],
			    strlen(noauthority[j]), NULL) ||
			    !x509_constraints_uri_host(noauthority[j],
			    strlen(noauthority[j]), &hostpart)) {
				FAIL("name '%s' should parse as a URI",
				    noauthority[j]);
				failure = 1;
				free(hostpart);
				goto done;
			}
			free(hostpart);

			if (x509_constraints_uri(noauthority[j],
			    strlen(noauthority[j]), constraint, clen, &error)) {
				FAIL("constraint '%s' should not have matched URI"
				    " '%s' (error %d)\n",
				    constraint, failinguri[j], error);
				failure = 1;
				goto done;
			}
		}
	}
	c = ".openbsd.org";
	cl = strlen(".openbsd.org");
	d = "*.openbsd.org";
	dl = strlen("*.openbsd.org");
	if (!x509_constraints_domain(d, dl, c, cl)) {
		FAIL("constraint '%s' should have matched '%s'\n",
		    c, d);
		failure = 1;
		goto done;
	}
	c = "www.openbsd.org";
	cl = strlen("www.openbsd.org");
	if (x509_constraints_domain(d, dl, c, cl)) {
		FAIL("constraint '%s' should not have matched '%s'\n",
		    c, d);
		failure = 1;
		goto done;
	}
	c = "";
	cl = 0;
	if (!x509_constraints_domain(d, dl, c, cl)) {
		FAIL("constraint '%s' should have matched '%s'\n",
		    c, d);
		failure = 1;
		goto done;
	}

 done:
	return failure;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= test_valid_hostnames();
	failed |= test_invalid_hostnames();
	failed |= test_valid_sandns_names();
	failed |= test_invalid_sandns_names();
	failed |= test_valid_mbox_names();
	failed |= test_invalid_mbox_names();
	failed |= test_valid_domain_constraints();
	failed |= test_invalid_domain_constraints();
	failed |= test_invalid_uri();
	failed |= test_valid_uri();
	failed |= test_constraints1();

	return (failed);
}
