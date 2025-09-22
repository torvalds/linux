/*	$Id: main.c,v 1.57 2025/09/16 15:06:02 sthen Exp $ */
/*
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/socket.h>

#include <ctype.h>
#include <err.h>
#include <libgen.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"
#include "parse.h"

#define WWW_DIR "/var/www/acme"
#define CONF_FILE "/etc/acme-client.conf"

int		 verbose;
enum comp 	 proccomp;

int
main(int argc, char *argv[])
{
	const char	 **alts = NULL;
	char		 *certdir = NULL;
	char		 *chngdir = NULL, *auth = NULL;
	char		 *conffile = CONF_FILE;
	char		 *tmps, *tmpsd;
	int		  key_fds[2], acct_fds[2], chng_fds[2], cert_fds[2];
	int		  file_fds[2], dns_fds[2], rvk_fds[2];
	int		  force = 0;
	int		  c, rc, revocate = 0;
	int		  popts = 0;
	pid_t		  pids[COMP__MAX];
	size_t		  i, altsz, ne;

	struct acme_conf	*conf = NULL;
	struct authority_c	*authority = NULL;
	struct domain_c		*domain = NULL;
	struct altname_c	*ac;

	if (setlocale(LC_CTYPE, "C") == NULL)
		errx(1, "setlocale");

	while ((c = getopt(argc, argv, "Fnrvf:")) != -1)
		switch (c) {
		case 'F':
			force = 1;
			break;
		case 'f':
			if ((conffile = strdup(optarg)) == NULL)
				err(EXIT_FAILURE, "strdup");
			break;
		case 'n':
			popts |= ACME_OPT_CHECK;
			break;
		case 'r':
			revocate = 1;
			break;
		case 'v':
			verbose = verbose ? 2 : 1;
			popts |= ACME_OPT_VERBOSE;
			break;
		default:
			goto usage;
		}

	if (getuid() != 0)
		errx(EXIT_FAILURE, "must be run as root");

	/* parse config file */
	if ((conf = parse_config(conffile, popts)) == NULL)
		return EXIT_FAILURE;

	argc -= optind;
	argv += optind;
	if (argc != 1)
		goto usage;

	if ((domain = domain_find_handle(conf, argv[0])) == NULL)
		errx(EXIT_FAILURE, "domain %s not found", argv[0]);

	argc--;
	argv++;

	/*
	 * The parser enforces that at least cert or fullchain is set.
	 * XXX Test if cert, chain and fullchain have the same dirname?
	 */
	tmps = domain->cert ? domain->cert : domain->fullchain;
	if ((tmps = strdup(tmps)) == NULL)
		err(EXIT_FAILURE, "strdup");
	if ((tmpsd = dirname(tmps)) == NULL)
		err(EXIT_FAILURE, "dirname");
	if ((certdir = strdup(tmpsd)) == NULL)
		err(EXIT_FAILURE, "strdup");	
	free(tmps);
	tmps = tmpsd = NULL;


	/* chain or fullchain can be relative paths according */
	if (domain->chain && domain->chain[0] != '/') {
		if (asprintf(&tmps, "%s/%s", certdir, domain->chain) == -1)
			err(EXIT_FAILURE, "asprintf");
		free(domain->chain);
		domain->chain = tmps;
		tmps = NULL;
	}
	if (domain->fullchain && domain->fullchain[0] != '/') {
		if (asprintf(&tmps, "%s/%s", certdir, domain->fullchain) == -1)
			err(EXIT_FAILURE, "asprintf");
		free(domain->fullchain);
		domain->fullchain = tmps;
		tmps = NULL;
	}

	if ((auth = domain->auth) == NULL) {
		/* use the first authority from the config as default XXX */
		authority = authority_find0(conf);
		if (authority == NULL)
			errx(EXIT_FAILURE, "no authorities configured");
	} else {
		authority = authority_find(conf, auth);
		if (authority == NULL)
			errx(EXIT_FAILURE, "authority %s not found", auth);
	}

	if ((chngdir = domain->challengedir) == NULL)
		if ((chngdir = strdup(WWW_DIR)) == NULL)
			err(EXIT_FAILURE, "strdup");

	/*
	 * Do some quick checks to see if our paths exist.
	 * This will be done in the children, but we might as well check
	 * now before the fork.
	 * XXX maybe use conf_check_file() from parse.y
	 */

	ne = 0;

	if (access(certdir, R_OK) == -1) {
		warnx("%s: cert directory must exist", certdir);
		ne++;
	}

	if (access(chngdir, R_OK) == -1) {
		warnx("%s: challenge directory must exist", chngdir);
		ne++;
	}

	if (ne > 0)
		return EXIT_FAILURE;

	if (popts & ACME_OPT_CHECK)
		return EXIT_SUCCESS;

	/* Set the zeroth altname as our domain. */
	altsz = domain->altname_count + 1;
	alts = calloc(altsz, sizeof(char *));
	if (alts == NULL)
		err(EXIT_FAILURE, "calloc");
	alts[0] = domain->domain;
	i = 1;
	/* XXX get rid of alts[] later */
	TAILQ_FOREACH(ac, &domain->altname_list, entry)
		alts[i++] = ac->domain;

	/*
	 * Open channels between our components.
	 */

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, key_fds) == -1)
		err(EXIT_FAILURE, "socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, acct_fds) == -1)
		err(EXIT_FAILURE, "socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, chng_fds) == -1)
		err(EXIT_FAILURE, "socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, cert_fds) == -1)
		err(EXIT_FAILURE, "socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, file_fds) == -1)
		err(EXIT_FAILURE, "socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, dns_fds) == -1)
		err(EXIT_FAILURE, "socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, rvk_fds) == -1)
		err(EXIT_FAILURE, "socketpair");

	signal(SIGPIPE, SIG_IGN);

	/* Start with the network-touching process. */

	if ((pids[COMP_NET] = fork()) == -1)
		err(EXIT_FAILURE, "fork");

	if (pids[COMP_NET] == 0) {
		proccomp = COMP_NET;
		close(key_fds[0]);
		close(acct_fds[0]);
		close(chng_fds[0]);
		close(cert_fds[0]);
		close(file_fds[0]);
		close(file_fds[1]);
		close(dns_fds[0]);
		close(rvk_fds[0]);
		c = netproc(key_fds[1], acct_fds[1],
		    chng_fds[1], cert_fds[1],
		    dns_fds[1], rvk_fds[1],
		    revocate, authority,
		    (const char *const *)alts, altsz,
		    domain->profile);
		exit(c ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	close(key_fds[1]);
	close(acct_fds[1]);
	close(chng_fds[1]);
	close(cert_fds[1]);
	close(dns_fds[1]);
	close(rvk_fds[1]);

	/* Now the key-touching component. */

	if ((pids[COMP_KEY] = fork()) == -1)
		err(EXIT_FAILURE, "fork");

	if (pids[COMP_KEY] == 0) {
		proccomp = COMP_KEY;
		close(cert_fds[0]);
		close(dns_fds[0]);
		close(rvk_fds[0]);
		close(acct_fds[0]);
		close(chng_fds[0]);
		close(file_fds[0]);
		close(file_fds[1]);
		c = keyproc(key_fds[0], domain->key,
		    (const char **)alts, altsz,
		    domain->keytype);
		exit(c ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	close(key_fds[0]);

	/* The account-touching component. */

	if ((pids[COMP_ACCOUNT] = fork()) == -1)
		err(EXIT_FAILURE, "fork");

	if (pids[COMP_ACCOUNT] == 0) {
		proccomp = COMP_ACCOUNT;
		close(cert_fds[0]);
		close(dns_fds[0]);
		close(rvk_fds[0]);
		close(chng_fds[0]);
		close(file_fds[0]);
		close(file_fds[1]);
		c = acctproc(acct_fds[0], authority->account,
		    authority->keytype);
		exit(c ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	close(acct_fds[0]);

	/* The challenge-accepting component. */

	if ((pids[COMP_CHALLENGE] = fork()) == -1)
		err(EXIT_FAILURE, "fork");

	if (pids[COMP_CHALLENGE] == 0) {
		proccomp = COMP_CHALLENGE;
		close(cert_fds[0]);
		close(dns_fds[0]);
		close(rvk_fds[0]);
		close(file_fds[0]);
		close(file_fds[1]);
		c = chngproc(chng_fds[0], chngdir);
		exit(c ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	close(chng_fds[0]);

	/* The certificate-handling component. */

	if ((pids[COMP_CERT] = fork()) == -1)
		err(EXIT_FAILURE, "fork");

	if (pids[COMP_CERT] == 0) {
		proccomp = COMP_CERT;
		close(dns_fds[0]);
		close(rvk_fds[0]);
		close(file_fds[1]);
		c = certproc(cert_fds[0], file_fds[0]);
		exit(c ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	close(cert_fds[0]);
	close(file_fds[0]);

	/* The certificate-handling component. */

	if ((pids[COMP_FILE] = fork()) == -1)
		err(EXIT_FAILURE, "fork");

	if (pids[COMP_FILE] == 0) {
		proccomp = COMP_FILE;
		close(dns_fds[0]);
		close(rvk_fds[0]);
		c = fileproc(file_fds[1], certdir, domain->cert, domain->chain,
		    domain->fullchain);
		/*
		 * This is different from the other processes in that it
		 * can return 2 if the certificates were updated.
		 */
		exit(c > 1 ? 2 : (c ? EXIT_SUCCESS : EXIT_FAILURE));
	}

	close(file_fds[1]);

	/* The DNS lookup component. */

	if ((pids[COMP_DNS] = fork()) == -1)
		err(EXIT_FAILURE, "fork");

	if (pids[COMP_DNS] == 0) {
		proccomp = COMP_DNS;
		close(rvk_fds[0]);
		c = dnsproc(dns_fds[0]);
		exit(c ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	close(dns_fds[0]);

	/* The expiration component. */

	if ((pids[COMP_REVOKE] = fork()) == -1)
		err(EXIT_FAILURE, "fork");

	if (pids[COMP_REVOKE] == 0) {
		proccomp = COMP_REVOKE;
		c = revokeproc(rvk_fds[0], domain->cert != NULL ? domain->cert :
		    domain->fullchain, force, revocate,
		    (const char *const *)alts, altsz);
		exit(c ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	close(rvk_fds[0]);

	/* Jail: sandbox, file-system, user. */

	if (pledge("stdio", NULL) == -1)
		err(EXIT_FAILURE, "pledge");

	/*
	 * Collect our subprocesses.
	 * Require that they both have exited cleanly.
	 */

	rc = checkexit(pids[COMP_KEY], COMP_KEY) +
	    checkexit(pids[COMP_CERT], COMP_CERT) +
	    checkexit(pids[COMP_NET], COMP_NET) +
	    checkexit_ext(&c, pids[COMP_FILE], COMP_FILE) +
	    checkexit(pids[COMP_ACCOUNT], COMP_ACCOUNT) +
	    checkexit(pids[COMP_CHALLENGE], COMP_CHALLENGE) +
	    checkexit(pids[COMP_DNS], COMP_DNS) +
	    checkexit(pids[COMP_REVOKE], COMP_REVOKE);

	return rc != COMP__MAX ? EXIT_FAILURE : (c == 2 ? EXIT_SUCCESS : 2);
usage:
	fprintf(stderr,
	    "usage: acme-client [-Fnrv] [-f configfile] handle\n");
	return EXIT_FAILURE;
}
