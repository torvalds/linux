/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2013 Bryan Drewery <bdrewery@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/sbuf.h>
#include <sys/wait.h>

#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fetch.h>
#include <paths.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ucl.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "dns_utils.h"
#include "config.h"

struct sig_cert {
	char *name;
	unsigned char *sig;
	int siglen;
	unsigned char *cert;
	int certlen;
	bool trusted;
};

struct pubkey {
	unsigned char *sig;
	int siglen;
};

typedef enum {
	HASH_UNKNOWN,
	HASH_SHA256,
} hash_t;

struct fingerprint {
	hash_t type;
	char *name;
	char hash[BUFSIZ];
	STAILQ_ENTRY(fingerprint) next;
};

STAILQ_HEAD(fingerprint_list, fingerprint);

static int
extract_pkg_static(int fd, char *p, int sz)
{
	struct archive *a;
	struct archive_entry *ae;
	char *end;
	int ret, r;

	ret = -1;
	a = archive_read_new();
	if (a == NULL) {
		warn("archive_read_new");
		return (ret);
	}
	archive_read_support_filter_all(a);
	archive_read_support_format_tar(a);

	if (lseek(fd, 0, 0) == -1) {
		warn("lseek");
		goto cleanup;
	}

	if (archive_read_open_fd(a, fd, 4096) != ARCHIVE_OK) {
		warnx("archive_read_open_fd: %s", archive_error_string(a));
		goto cleanup;
	}

	ae = NULL;
	while ((r = archive_read_next_header(a, &ae)) == ARCHIVE_OK) {
		end = strrchr(archive_entry_pathname(ae), '/');
		if (end == NULL)
			continue;

		if (strcmp(end, "/pkg-static") == 0) {
			r = archive_read_extract(a, ae,
			    ARCHIVE_EXTRACT_OWNER | ARCHIVE_EXTRACT_PERM |
			    ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_ACL |
			    ARCHIVE_EXTRACT_FFLAGS | ARCHIVE_EXTRACT_XATTR);
			strlcpy(p, archive_entry_pathname(ae), sz);
			break;
		}
	}

	if (r == ARCHIVE_OK)
		ret = 0;
	else
		warnx("failed to extract pkg-static: %s",
		    archive_error_string(a));

cleanup:
	archive_read_free(a);
	return (ret);

}

static int
install_pkg_static(const char *path, const char *pkgpath, bool force)
{
	int pstat;
	pid_t pid;

	switch ((pid = fork())) {
	case -1:
		return (-1);
	case 0:
		if (force)
			execl(path, "pkg-static", "add", "-f", pkgpath,
			    (char *)NULL);
		else
			execl(path, "pkg-static", "add", pkgpath,
			    (char *)NULL);
		_exit(1);
	default:
		break;
	}

	while (waitpid(pid, &pstat, 0) == -1)
		if (errno != EINTR)
			return (-1);

	if (WEXITSTATUS(pstat))
		return (WEXITSTATUS(pstat));
	else if (WIFSIGNALED(pstat))
		return (128 & (WTERMSIG(pstat)));
	return (pstat);
}

static int
fetch_to_fd(const char *url, char *path)
{
	struct url *u;
	struct dns_srvinfo *mirrors, *current;
	struct url_stat st;
	FILE *remote;
	/* To store _https._tcp. + hostname + \0 */
	int fd;
	int retry, max_retry;
	ssize_t r;
	char buf[10240];
	char zone[MAXHOSTNAMELEN + 13];
	static const char *mirror_type = NULL;

	max_retry = 3;
	current = mirrors = NULL;
	remote = NULL;

	if (mirror_type == NULL && config_string(MIRROR_TYPE, &mirror_type)
	    != 0) {
		warnx("No MIRROR_TYPE defined");
		return (-1);
	}

	if ((fd = mkstemp(path)) == -1) {
		warn("mkstemp()");
		return (-1);
	}

	retry = max_retry;

	if ((u = fetchParseURL(url)) == NULL) {
		warn("fetchParseURL('%s')", url);
		return (-1);
	}

	while (remote == NULL) {
		if (retry == max_retry) {
			if (strcmp(u->scheme, "file") != 0 &&
			    strcasecmp(mirror_type, "srv") == 0) {
				snprintf(zone, sizeof(zone),
				    "_%s._tcp.%s", u->scheme, u->host);
				mirrors = dns_getsrvinfo(zone);
				current = mirrors;
			}
		}

		if (mirrors != NULL) {
			strlcpy(u->host, current->host, sizeof(u->host));
			u->port = current->port;
		}

		remote = fetchXGet(u, &st, "");
		if (remote == NULL) {
			--retry;
			if (retry <= 0)
				goto fetchfail;
			if (mirrors == NULL) {
				sleep(1);
			} else {
				current = current->next;
				if (current == NULL)
					current = mirrors;
			}
		}
	}

	while ((r = fread(buf, 1, sizeof(buf), remote)) > 0) {
		if (write(fd, buf, r) != r) {
			warn("write()");
			goto fetchfail;
		}
	}

	if (r != 0) {
		warn("An error occurred while fetching pkg(8)");
		goto fetchfail;
	}

	if (ferror(remote))
		goto fetchfail;

	goto cleanup;

fetchfail:
	if (fd != -1) {
		close(fd);
		fd = -1;
		unlink(path);
	}

cleanup:
	if (remote != NULL)
		fclose(remote);

	return fd;
}

static struct fingerprint *
parse_fingerprint(ucl_object_t *obj)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	const char *function, *fp, *key;
	struct fingerprint *f;
	hash_t fct = HASH_UNKNOWN;

	function = fp = NULL;

	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		if (cur->type != UCL_STRING)
			continue;
		if (strcasecmp(key, "function") == 0) {
			function = ucl_object_tostring(cur);
			continue;
		}
		if (strcasecmp(key, "fingerprint") == 0) {
			fp = ucl_object_tostring(cur);
			continue;
		}
	}

	if (fp == NULL || function == NULL)
		return (NULL);

	if (strcasecmp(function, "sha256") == 0)
		fct = HASH_SHA256;

	if (fct == HASH_UNKNOWN) {
		warnx("Unsupported hashing function: %s", function);
		return (NULL);
	}

	f = calloc(1, sizeof(struct fingerprint));
	f->type = fct;
	strlcpy(f->hash, fp, sizeof(f->hash));

	return (f);
}

static void
free_fingerprint_list(struct fingerprint_list* list)
{
	struct fingerprint *fingerprint, *tmp;

	STAILQ_FOREACH_SAFE(fingerprint, list, next, tmp) {
		free(fingerprint->name);
		free(fingerprint);
	}
	free(list);
}

static struct fingerprint *
load_fingerprint(const char *dir, const char *filename)
{
	ucl_object_t *obj = NULL;
	struct ucl_parser *p = NULL;
	struct fingerprint *f;
	char path[MAXPATHLEN];

	f = NULL;

	snprintf(path, MAXPATHLEN, "%s/%s", dir, filename);

	p = ucl_parser_new(0);
	if (!ucl_parser_add_file(p, path)) {
		warnx("%s: %s", path, ucl_parser_get_error(p));
		ucl_parser_free(p);
		return (NULL);
	}

	obj = ucl_parser_get_object(p);

	if (obj->type == UCL_OBJECT)
		f = parse_fingerprint(obj);

	if (f != NULL)
		f->name = strdup(filename);

	ucl_object_unref(obj);
	ucl_parser_free(p);

	return (f);
}

static struct fingerprint_list *
load_fingerprints(const char *path, int *count)
{
	DIR *d;
	struct dirent *ent;
	struct fingerprint *finger;
	struct fingerprint_list *fingerprints;

	*count = 0;

	fingerprints = calloc(1, sizeof(struct fingerprint_list));
	if (fingerprints == NULL)
		return (NULL);
	STAILQ_INIT(fingerprints);

	if ((d = opendir(path)) == NULL) {
		free(fingerprints);

		return (NULL);
	}

	while ((ent = readdir(d))) {
		if (strcmp(ent->d_name, ".") == 0 ||
		    strcmp(ent->d_name, "..") == 0)
			continue;
		finger = load_fingerprint(path, ent->d_name);
		if (finger != NULL) {
			STAILQ_INSERT_TAIL(fingerprints, finger, next);
			++(*count);
		}
	}

	closedir(d);

	return (fingerprints);
}

static void
sha256_hash(unsigned char hash[SHA256_DIGEST_LENGTH],
    char out[SHA256_DIGEST_LENGTH * 2 + 1])
{
	int i;

	for (i = 0; i < SHA256_DIGEST_LENGTH; i++)
		sprintf(out + (i * 2), "%02x", hash[i]);

	out[SHA256_DIGEST_LENGTH * 2] = '\0';
}

static void
sha256_buf(char *buf, size_t len, char out[SHA256_DIGEST_LENGTH * 2 + 1])
{
	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256_CTX sha256;

	out[0] = '\0';

	SHA256_Init(&sha256);
	SHA256_Update(&sha256, buf, len);
	SHA256_Final(hash, &sha256);
	sha256_hash(hash, out);
}

static int
sha256_fd(int fd, char out[SHA256_DIGEST_LENGTH * 2 + 1])
{
	int my_fd;
	FILE *fp;
	char buffer[BUFSIZ];
	unsigned char hash[SHA256_DIGEST_LENGTH];
	size_t r;
	int ret;
	SHA256_CTX sha256;

	my_fd = -1;
	fp = NULL;
	r = 0;
	ret = 1;

	out[0] = '\0';

	/* Duplicate the fd so that fclose(3) does not close it. */
	if ((my_fd = dup(fd)) == -1) {
		warnx("dup");
		goto cleanup;
	}

	if ((fp = fdopen(my_fd, "rb")) == NULL) {
		warnx("fdopen");
		goto cleanup;
	}

	SHA256_Init(&sha256);

	while ((r = fread(buffer, 1, BUFSIZ, fp)) > 0)
		SHA256_Update(&sha256, buffer, r);

	if (ferror(fp) != 0) {
		warnx("fread");
		goto cleanup;
	}

	SHA256_Final(hash, &sha256);
	sha256_hash(hash, out);
	ret = 0;

cleanup:
	if (fp != NULL)
		fclose(fp);
	else if (my_fd != -1)
		close(my_fd);
	(void)lseek(fd, 0, SEEK_SET);

	return (ret);
}

static EVP_PKEY *
load_public_key_file(const char *file)
{
	EVP_PKEY *pkey;
	BIO *bp;
	char errbuf[1024];

	bp = BIO_new_file(file, "r");
	if (!bp)
		errx(EXIT_FAILURE, "Unable to read %s", file);

	if ((pkey = PEM_read_bio_PUBKEY(bp, NULL, NULL, NULL)) == NULL)
		warnx("ici: %s", ERR_error_string(ERR_get_error(), errbuf));

	BIO_free(bp);

	return (pkey);
}

static EVP_PKEY *
load_public_key_buf(const unsigned char *cert, int certlen)
{
	EVP_PKEY *pkey;
	BIO *bp;
	char errbuf[1024];

	bp = BIO_new_mem_buf(__DECONST(void *, cert), certlen);

	if ((pkey = PEM_read_bio_PUBKEY(bp, NULL, NULL, NULL)) == NULL)
		warnx("%s", ERR_error_string(ERR_get_error(), errbuf));

	BIO_free(bp);

	return (pkey);
}

static bool
rsa_verify_cert(int fd, const char *sigfile, const unsigned char *key,
    int keylen, unsigned char *sig, int siglen)
{
	EVP_MD_CTX *mdctx;
	EVP_PKEY *pkey;
	char sha256[(SHA256_DIGEST_LENGTH * 2) + 2];
	char errbuf[1024];
	bool ret;

	pkey = NULL;
	mdctx = NULL;
	ret = false;

	SSL_load_error_strings();

	/* Compute SHA256 of the package. */
	if (lseek(fd, 0, 0) == -1) {
		warn("lseek");
		goto cleanup;
	}
	if ((sha256_fd(fd, sha256)) == -1) {
		warnx("Error creating SHA256 hash for package");
		goto cleanup;
	}

	if (sigfile != NULL) {
		if ((pkey = load_public_key_file(sigfile)) == NULL) {
			warnx("Error reading public key");
			goto cleanup;
		}
	} else {
		if ((pkey = load_public_key_buf(key, keylen)) == NULL) {
			warnx("Error reading public key");
			goto cleanup;
		}
	}

	/* Verify signature of the SHA256(pkg) is valid. */
	if ((mdctx = EVP_MD_CTX_create()) == NULL) {
		warnx("%s", ERR_error_string(ERR_get_error(), errbuf));
		goto error;
	}

	if (EVP_DigestVerifyInit(mdctx, NULL, EVP_sha256(), NULL, pkey) != 1) {
		warnx("%s", ERR_error_string(ERR_get_error(), errbuf));
		goto error;
	}
	if (EVP_DigestVerifyUpdate(mdctx, sha256, strlen(sha256)) != 1) {
		warnx("%s", ERR_error_string(ERR_get_error(), errbuf));
		goto error;
	}

	if (EVP_DigestVerifyFinal(mdctx, sig, siglen) != 1) {
		warnx("%s", ERR_error_string(ERR_get_error(), errbuf));
		goto error;
	}

	ret = true;
	printf("done\n");
	goto cleanup;

error:
	printf("failed\n");

cleanup:
	if (pkey)
		EVP_PKEY_free(pkey);
	if (mdctx)
		EVP_MD_CTX_destroy(mdctx);
	ERR_free_strings();

	return (ret);
}

static struct pubkey *
read_pubkey(int fd)
{
	struct pubkey *pk;
	struct sbuf *sig;
	char buf[4096];
	int r;

	if (lseek(fd, 0, 0) == -1) {
		warn("lseek");
		return (NULL);
	}

	sig = sbuf_new_auto();

	while ((r = read(fd, buf, sizeof(buf))) >0) {
		sbuf_bcat(sig, buf, r);
	}

	sbuf_finish(sig);
	pk = calloc(1, sizeof(struct pubkey));
	pk->siglen = sbuf_len(sig);
	pk->sig = calloc(1, pk->siglen);
	memcpy(pk->sig, sbuf_data(sig), pk->siglen);
	sbuf_delete(sig);

	return (pk);
}

static struct sig_cert *
parse_cert(int fd) {
	int my_fd;
	struct sig_cert *sc;
	FILE *fp;
	struct sbuf *buf, *sig, *cert;
	char *line;
	size_t linecap;
	ssize_t linelen;

	buf = NULL;
	my_fd = -1;
	sc = NULL;
	line = NULL;
	linecap = 0;

	if (lseek(fd, 0, 0) == -1) {
		warn("lseek");
		return (NULL);
	}

	/* Duplicate the fd so that fclose(3) does not close it. */
	if ((my_fd = dup(fd)) == -1) {
		warnx("dup");
		return (NULL);
	}

	if ((fp = fdopen(my_fd, "rb")) == NULL) {
		warn("fdopen");
		close(my_fd);
		return (NULL);
	}

	sig = sbuf_new_auto();
	cert = sbuf_new_auto();

	while ((linelen = getline(&line, &linecap, fp)) > 0) {
		if (strcmp(line, "SIGNATURE\n") == 0) {
			buf = sig;
			continue;
		} else if (strcmp(line, "CERT\n") == 0) {
			buf = cert;
			continue;
		} else if (strcmp(line, "END\n") == 0) {
			break;
		}
		if (buf != NULL)
			sbuf_bcat(buf, line, linelen);
	}

	fclose(fp);

	/* Trim out unrelated trailing newline */
	sbuf_setpos(sig, sbuf_len(sig) - 1);

	sbuf_finish(sig);
	sbuf_finish(cert);

	sc = calloc(1, sizeof(struct sig_cert));
	sc->siglen = sbuf_len(sig);
	sc->sig = calloc(1, sc->siglen);
	memcpy(sc->sig, sbuf_data(sig), sc->siglen);

	sc->certlen = sbuf_len(cert);
	sc->cert = strdup(sbuf_data(cert));

	sbuf_delete(sig);
	sbuf_delete(cert);

	return (sc);
}

static bool
verify_pubsignature(int fd_pkg, int fd_sig)
{
	struct pubkey *pk;
	const char *pubkey;
	bool ret;

	pk = NULL;
	pubkey = NULL;
	ret = false;
	if (config_string(PUBKEY, &pubkey) != 0) {
		warnx("No CONFIG_PUBKEY defined");
		goto cleanup;
	}

	if ((pk = read_pubkey(fd_sig)) == NULL) {
		warnx("Error reading signature");
		goto cleanup;
	}

	/* Verify the signature. */
	printf("Verifying signature with public key %s... ", pubkey);
	if (rsa_verify_cert(fd_pkg, pubkey, NULL, 0, pk->sig,
	    pk->siglen) == false) {
		fprintf(stderr, "Signature is not valid\n");
		goto cleanup;
	}

	ret = true;

cleanup:
	if (pk) {
		free(pk->sig);
		free(pk);
	}

	return (ret);
}

static bool
verify_signature(int fd_pkg, int fd_sig)
{
	struct fingerprint_list *trusted, *revoked;
	struct fingerprint *fingerprint;
	struct sig_cert *sc;
	bool ret;
	int trusted_count, revoked_count;
	const char *fingerprints;
	char path[MAXPATHLEN];
	char hash[SHA256_DIGEST_LENGTH * 2 + 1];

	sc = NULL;
	trusted = revoked = NULL;
	ret = false;

	/* Read and parse fingerprints. */
	if (config_string(FINGERPRINTS, &fingerprints) != 0) {
		warnx("No CONFIG_FINGERPRINTS defined");
		goto cleanup;
	}

	snprintf(path, MAXPATHLEN, "%s/trusted", fingerprints);
	if ((trusted = load_fingerprints(path, &trusted_count)) == NULL) {
		warnx("Error loading trusted certificates");
		goto cleanup;
	}

	if (trusted_count == 0 || trusted == NULL) {
		fprintf(stderr, "No trusted certificates found.\n");
		goto cleanup;
	}

	snprintf(path, MAXPATHLEN, "%s/revoked", fingerprints);
	if ((revoked = load_fingerprints(path, &revoked_count)) == NULL) {
		warnx("Error loading revoked certificates");
		goto cleanup;
	}

	/* Read certificate and signature in. */
	if ((sc = parse_cert(fd_sig)) == NULL) {
		warnx("Error parsing certificate");
		goto cleanup;
	}
	/* Explicitly mark as non-trusted until proven otherwise. */
	sc->trusted = false;

	/* Parse signature and pubkey out of the certificate */
	sha256_buf(sc->cert, sc->certlen, hash);

	/* Check if this hash is revoked */
	if (revoked != NULL) {
		STAILQ_FOREACH(fingerprint, revoked, next) {
			if (strcasecmp(fingerprint->hash, hash) == 0) {
				fprintf(stderr, "The package was signed with "
				    "revoked certificate %s\n",
				    fingerprint->name);
				goto cleanup;
			}
		}
	}

	STAILQ_FOREACH(fingerprint, trusted, next) {
		if (strcasecmp(fingerprint->hash, hash) == 0) {
			sc->trusted = true;
			sc->name = strdup(fingerprint->name);
			break;
		}
	}

	if (sc->trusted == false) {
		fprintf(stderr, "No trusted fingerprint found matching "
		    "package's certificate\n");
		goto cleanup;
	}

	/* Verify the signature. */
	printf("Verifying signature with trusted certificate %s... ", sc->name);
	if (rsa_verify_cert(fd_pkg, NULL, sc->cert, sc->certlen, sc->sig,
	    sc->siglen) == false) {
		fprintf(stderr, "Signature is not valid\n");
		goto cleanup;
	}

	ret = true;

cleanup:
	if (trusted)
		free_fingerprint_list(trusted);
	if (revoked)
		free_fingerprint_list(revoked);
	if (sc) {
		free(sc->cert);
		free(sc->sig);
		free(sc->name);
		free(sc);
	}

	return (ret);
}

static int
bootstrap_pkg(bool force)
{
	int fd_pkg, fd_sig;
	int ret;
	char url[MAXPATHLEN];
	char tmppkg[MAXPATHLEN];
	char tmpsig[MAXPATHLEN];
	const char *packagesite;
	const char *signature_type;
	char pkgstatic[MAXPATHLEN];

	fd_sig = -1;
	ret = -1;

	if (config_string(PACKAGESITE, &packagesite) != 0) {
		warnx("No PACKAGESITE defined");
		return (-1);
	}

	if (config_string(SIGNATURE_TYPE, &signature_type) != 0) {
		warnx("Error looking up SIGNATURE_TYPE");
		return (-1);
	}

	printf("Bootstrapping pkg from %s, please wait...\n", packagesite);

	/* Support pkg+http:// for PACKAGESITE which is the new format
	   in 1.2 to avoid confusion on why http://pkg.FreeBSD.org has
	   no A record. */
	if (strncmp(URL_SCHEME_PREFIX, packagesite,
	    strlen(URL_SCHEME_PREFIX)) == 0)
		packagesite += strlen(URL_SCHEME_PREFIX);
	snprintf(url, MAXPATHLEN, "%s/Latest/pkg.txz", packagesite);

	snprintf(tmppkg, MAXPATHLEN, "%s/pkg.txz.XXXXXX",
	    getenv("TMPDIR") ? getenv("TMPDIR") : _PATH_TMP);

	if ((fd_pkg = fetch_to_fd(url, tmppkg)) == -1)
		goto fetchfail;

	if (signature_type != NULL &&
	    strcasecmp(signature_type, "NONE") != 0) {
		if (strcasecmp(signature_type, "FINGERPRINTS") == 0) {

			snprintf(tmpsig, MAXPATHLEN, "%s/pkg.txz.sig.XXXXXX",
			    getenv("TMPDIR") ? getenv("TMPDIR") : _PATH_TMP);
			snprintf(url, MAXPATHLEN, "%s/Latest/pkg.txz.sig",
			    packagesite);

			if ((fd_sig = fetch_to_fd(url, tmpsig)) == -1) {
				fprintf(stderr, "Signature for pkg not "
				    "available.\n");
				goto fetchfail;
			}

			if (verify_signature(fd_pkg, fd_sig) == false)
				goto cleanup;
		} else if (strcasecmp(signature_type, "PUBKEY") == 0) {

			snprintf(tmpsig, MAXPATHLEN,
			    "%s/pkg.txz.pubkeysig.XXXXXX",
			    getenv("TMPDIR") ? getenv("TMPDIR") : _PATH_TMP);
			snprintf(url, MAXPATHLEN, "%s/Latest/pkg.txz.pubkeysig",
			    packagesite);

			if ((fd_sig = fetch_to_fd(url, tmpsig)) == -1) {
				fprintf(stderr, "Signature for pkg not "
				    "available.\n");
				goto fetchfail;
			}

			if (verify_pubsignature(fd_pkg, fd_sig) == false)
				goto cleanup;
		} else {
			warnx("Signature type %s is not supported for "
			    "bootstrapping.", signature_type);
			goto cleanup;
		}
	}

	if ((ret = extract_pkg_static(fd_pkg, pkgstatic, MAXPATHLEN)) == 0)
		ret = install_pkg_static(pkgstatic, tmppkg, force);

	goto cleanup;

fetchfail:
	warnx("Error fetching %s: %s", url, fetchLastErrString);
	fprintf(stderr, "A pre-built version of pkg could not be found for "
	    "your system.\n");
	fprintf(stderr, "Consider changing PACKAGESITE or installing it from "
	    "ports: 'ports-mgmt/pkg'.\n");

cleanup:
	if (fd_sig != -1) {
		close(fd_sig);
		unlink(tmpsig);
	}

	if (fd_pkg != -1) {
		close(fd_pkg);
		unlink(tmppkg);
	}

	return (ret);
}

static const char confirmation_message[] =
"The package management tool is not yet installed on your system.\n"
"Do you want to fetch and install it now? [y/N]: ";

static const char non_interactive_message[] =
"The package management tool is not yet installed on your system.\n"
"Please set ASSUME_ALWAYS_YES=yes environment variable to be able to bootstrap "
"in non-interactive (stdin not being a tty)\n";

static int
pkg_query_yes_no(void)
{
	int ret, c;

	fflush(stdout);
	c = getchar();

	if (c == 'y' || c == 'Y')
		ret = 1;
	else
		ret = 0;

	while (c != '\n' && c != EOF)
		c = getchar();

	return (ret);
}

static int
bootstrap_pkg_local(const char *pkgpath, bool force)
{
	char path[MAXPATHLEN];
	char pkgstatic[MAXPATHLEN];
	const char *signature_type;
	int fd_pkg, fd_sig, ret;

	fd_sig = -1;
	ret = -1;

	fd_pkg = open(pkgpath, O_RDONLY);
	if (fd_pkg == -1)
		err(EXIT_FAILURE, "Unable to open %s", pkgpath);

	if (config_string(SIGNATURE_TYPE, &signature_type) != 0) {
		warnx("Error looking up SIGNATURE_TYPE");
		goto cleanup;
	}
	if (signature_type != NULL &&
	    strcasecmp(signature_type, "NONE") != 0) {
		if (strcasecmp(signature_type, "FINGERPRINTS") == 0) {

			snprintf(path, sizeof(path), "%s.sig", pkgpath);

			if ((fd_sig = open(path, O_RDONLY)) == -1) {
				fprintf(stderr, "Signature for pkg not "
				    "available.\n");
				goto cleanup;
			}

			if (verify_signature(fd_pkg, fd_sig) == false)
				goto cleanup;

		} else if (strcasecmp(signature_type, "PUBKEY") == 0) {

			snprintf(path, sizeof(path), "%s.pubkeysig", pkgpath);

			if ((fd_sig = open(path, O_RDONLY)) == -1) {
				fprintf(stderr, "Signature for pkg not "
				    "available.\n");
				goto cleanup;
			}

			if (verify_pubsignature(fd_pkg, fd_sig) == false)
				goto cleanup;

		} else {
			warnx("Signature type %s is not supported for "
			    "bootstrapping.", signature_type);
			goto cleanup;
		}
	}

	if ((ret = extract_pkg_static(fd_pkg, pkgstatic, MAXPATHLEN)) == 0)
		ret = install_pkg_static(pkgstatic, pkgpath, force);

cleanup:
	close(fd_pkg);
	if (fd_sig != -1)
		close(fd_sig);

	return (ret);
}

int
main(int argc, char *argv[])
{
	char pkgpath[MAXPATHLEN];
	const char *pkgarg;
	int i;
	bool bootstrap_only, force, yes;

	bootstrap_only = false;
	force = false;
	pkgarg = NULL;
	yes = false;

	snprintf(pkgpath, MAXPATHLEN, "%s/sbin/pkg",
	    getenv("LOCALBASE") ? getenv("LOCALBASE") : _LOCALBASE);

	if (argc > 1 && strcmp(argv[1], "bootstrap") == 0) {
		bootstrap_only = true;
		if (argc == 3 && strcmp(argv[2], "-f") == 0)
			force = true;
	}

	if ((bootstrap_only && force) || access(pkgpath, X_OK) == -1) {
		/* 
		 * To allow 'pkg -N' to be used as a reliable test for whether
		 * a system is configured to use pkg, don't bootstrap pkg
		 * when that argument is given as argv[1].
		 */
		if (argv[1] != NULL && strcmp(argv[1], "-N") == 0)
			errx(EXIT_FAILURE, "pkg is not installed");

		config_init();

		if (argc > 1 && strcmp(argv[1], "add") == 0) {
			if (argc > 2 && strcmp(argv[2], "-f") == 0) {
				force = true;
				pkgarg = argv[3];
			} else
				pkgarg = argv[2];
			if (pkgarg == NULL) {
				fprintf(stderr, "Path to pkg.txz required\n");
				exit(EXIT_FAILURE);
			}
			if (access(pkgarg, R_OK) == -1) {
				fprintf(stderr, "No such file: %s\n", pkgarg);
				exit(EXIT_FAILURE);
			}
			if (bootstrap_pkg_local(pkgarg, force) != 0)
				exit(EXIT_FAILURE);
			exit(EXIT_SUCCESS);
		}
		/*
		 * Do not ask for confirmation if either of stdin or stdout is
		 * not tty. Check the environment to see if user has answer
		 * tucked in there already.
		 */
		config_bool(ASSUME_ALWAYS_YES, &yes);
		if (!yes) {
			for (i = 1; i < argc; i++) {
				if (strcmp(argv[i], "-y") == 0 ||
				    strcmp(argv[i], "--yes") == 0) {
					yes = true;
					break;
				}
			}
		}
		if (!yes) {
			if (!isatty(fileno(stdin))) {
				fprintf(stderr, non_interactive_message);
				exit(EXIT_FAILURE);
			}

			printf("%s", confirmation_message);
			if (pkg_query_yes_no() == 0)
				exit(EXIT_FAILURE);
		}
		if (bootstrap_pkg(force) != 0)
			exit(EXIT_FAILURE);
		config_finish();

		if (bootstrap_only)
			exit(EXIT_SUCCESS);
	} else if (bootstrap_only) {
		printf("pkg already bootstrapped at %s\n", pkgpath);
		exit(EXIT_SUCCESS);
	}

	execv(pkgpath, argv);

	/* NOT REACHED */
	return (EXIT_FAILURE);
}
