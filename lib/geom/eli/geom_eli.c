/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <opencrypto/cryptodev.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgeom.h>
#include <paths.h>
#include <readpassphrase.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <geom/eli/g_eli.h>
#include <geom/eli/pkcs5v2.h>

#include "core/geom.h"
#include "misc/subr.h"


uint32_t lib_version = G_LIB_VERSION;
uint32_t version = G_ELI_VERSION;

#define	GELI_BACKUP_DIR	"/var/backups/"
#define	GELI_ENC_ALGO	"aes"
#define	BUFSIZE		1024

/*
 * Passphrase cached when attaching multiple providers, in order to be more
 * user-friendly if they are using the same passphrase.
 */
static char cached_passphrase[BUFSIZE] = "";

static void eli_main(struct gctl_req *req, unsigned flags);
static void eli_init(struct gctl_req *req);
static void eli_attach(struct gctl_req *req);
static void eli_configure(struct gctl_req *req);
static void eli_setkey(struct gctl_req *req);
static void eli_delkey(struct gctl_req *req);
static void eli_resume(struct gctl_req *req);
static void eli_kill(struct gctl_req *req);
static void eli_backup(struct gctl_req *req);
static void eli_restore(struct gctl_req *req);
static void eli_resize(struct gctl_req *req);
static void eli_version(struct gctl_req *req);
static void eli_clear(struct gctl_req *req);
static void eli_dump(struct gctl_req *req);

static int eli_backup_create(struct gctl_req *req, const char *prov,
    const char *file);

/*
 * Available commands:
 *
 * init [-bdgPTv] [-a aalgo] [-B backupfile] [-e ealgo] [-i iterations] [-l keylen] [-J newpassfile] [-K newkeyfile] [-s sectorsize] [-V version] prov ...
 * label - alias for 'init'
 * attach [-Cdprv] [-n keyno] [-j passfile] [-k keyfile] prov ...
 * detach [-fl] prov ...
 * stop - alias for 'detach'
 * onetime [-d] [-a aalgo] [-e ealgo] [-l keylen] prov
 * configure [-bBgGtT] prov ...
 * setkey [-pPv] [-n keyno] [-j passfile] [-J newpassfile] [-k keyfile] [-K newkeyfile] prov
 * delkey [-afv] [-n keyno] prov
 * suspend [-v] -a | prov ...
 * resume [-pv] [-j passfile] [-k keyfile] prov
 * kill [-av] [prov ...]
 * backup [-v] prov file
 * restore [-fv] file prov
 * resize [-v] -s oldsize prov
 * version [prov ...]
 * clear [-v] prov ...
 * dump [-v] prov ...
 */
struct g_command class_commands[] = {
	{ "init", G_FLAG_VERBOSE, eli_main,
	    {
		{ 'a', "aalgo", "", G_TYPE_STRING },
		{ 'b', "boot", NULL, G_TYPE_BOOL },
		{ 'B', "backupfile", "", G_TYPE_STRING },
		{ 'd', "displaypass", NULL, G_TYPE_BOOL },
		{ 'e', "ealgo", "", G_TYPE_STRING },
		{ 'g', "geliboot", NULL, G_TYPE_BOOL },
		{ 'i', "iterations", "-1", G_TYPE_NUMBER },
		{ 'J', "newpassfile", G_VAL_OPTIONAL, G_TYPE_STRING | G_TYPE_MULTI },
		{ 'K', "newkeyfile", G_VAL_OPTIONAL, G_TYPE_STRING | G_TYPE_MULTI },
		{ 'l', "keylen", "0", G_TYPE_NUMBER },
		{ 'P', "nonewpassphrase", NULL, G_TYPE_BOOL },
		{ 's', "sectorsize", "0", G_TYPE_NUMBER },
		{ 'T', "notrim", NULL, G_TYPE_BOOL },
		{ 'V', "mdversion", "-1", G_TYPE_NUMBER },
		G_OPT_SENTINEL
	    },
	    "[-bdgPTv] [-a aalgo] [-B backupfile] [-e ealgo] [-i iterations] [-l keylen] [-J newpassfile] [-K newkeyfile] [-s sectorsize] [-V version] prov ..."
	},
	{ "label", G_FLAG_VERBOSE, eli_main,
	    {
		{ 'a', "aalgo", "", G_TYPE_STRING },
		{ 'b', "boot", NULL, G_TYPE_BOOL },
		{ 'B', "backupfile", "", G_TYPE_STRING },
		{ 'd', "displaypass", NULL, G_TYPE_BOOL },
		{ 'e', "ealgo", "", G_TYPE_STRING },
		{ 'g', "geliboot", NULL, G_TYPE_BOOL },
		{ 'i', "iterations", "-1", G_TYPE_NUMBER },
		{ 'J', "newpassfile", G_VAL_OPTIONAL, G_TYPE_STRING | G_TYPE_MULTI },
		{ 'K', "newkeyfile", G_VAL_OPTIONAL, G_TYPE_STRING | G_TYPE_MULTI },
		{ 'l', "keylen", "0", G_TYPE_NUMBER },
		{ 'P', "nonewpassphrase", NULL, G_TYPE_BOOL },
		{ 's', "sectorsize", "0", G_TYPE_NUMBER },
		{ 'V', "mdversion", "-1", G_TYPE_NUMBER },
		G_OPT_SENTINEL
	    },
	    "- an alias for 'init'"
	},
	{ "attach", G_FLAG_VERBOSE | G_FLAG_LOADKLD, eli_main,
	    {
		{ 'C', "dryrun", NULL, G_TYPE_BOOL },
		{ 'd', "detach", NULL, G_TYPE_BOOL },
		{ 'j', "passfile", G_VAL_OPTIONAL, G_TYPE_STRING | G_TYPE_MULTI },
		{ 'k', "keyfile", G_VAL_OPTIONAL, G_TYPE_STRING | G_TYPE_MULTI },
		{ 'n', "keyno", "-1", G_TYPE_NUMBER },
		{ 'p', "nopassphrase", NULL, G_TYPE_BOOL },
		{ 'r', "readonly", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-Cdprv] [-n keyno] [-j passfile] [-k keyfile] prov ..."
	},
	{ "detach", 0, NULL,
	    {
		{ 'f', "force", NULL, G_TYPE_BOOL },
		{ 'l', "last", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-fl] prov ..."
	},
	{ "stop", 0, NULL,
	    {
		{ 'f', "force", NULL, G_TYPE_BOOL },
		{ 'l', "last", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "- an alias for 'detach'"
	},
	{ "onetime", G_FLAG_VERBOSE | G_FLAG_LOADKLD, NULL,
	    {
		{ 'a', "aalgo", "", G_TYPE_STRING },
		{ 'd', "detach", NULL, G_TYPE_BOOL },
		{ 'e', "ealgo", GELI_ENC_ALGO, G_TYPE_STRING },
		{ 'l', "keylen", "0", G_TYPE_NUMBER },
		{ 's', "sectorsize", "0", G_TYPE_NUMBER },
		{ 'T', "notrim", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-dT] [-a aalgo] [-e ealgo] [-l keylen] [-s sectorsize] prov"
	},
	{ "configure", G_FLAG_VERBOSE, eli_main,
	    {
		{ 'b', "boot", NULL, G_TYPE_BOOL },
		{ 'B', "noboot", NULL, G_TYPE_BOOL },
		{ 'd', "displaypass", NULL, G_TYPE_BOOL },
		{ 'D', "nodisplaypass", NULL, G_TYPE_BOOL },
		{ 'g', "geliboot", NULL, G_TYPE_BOOL },
		{ 'G', "nogeliboot", NULL, G_TYPE_BOOL },
		{ 't', "trim", NULL, G_TYPE_BOOL },
		{ 'T', "notrim", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-bBdDgGtT] prov ..."
	},
	{ "setkey", G_FLAG_VERBOSE, eli_main,
	    {
		{ 'i', "iterations", "-1", G_TYPE_NUMBER },
		{ 'j', "passfile", G_VAL_OPTIONAL, G_TYPE_STRING | G_TYPE_MULTI },
		{ 'J', "newpassfile", G_VAL_OPTIONAL, G_TYPE_STRING | G_TYPE_MULTI },
		{ 'k', "keyfile", G_VAL_OPTIONAL, G_TYPE_STRING | G_TYPE_MULTI },
		{ 'K', "newkeyfile", G_VAL_OPTIONAL, G_TYPE_STRING | G_TYPE_MULTI },
		{ 'n', "keyno", "-1", G_TYPE_NUMBER },
		{ 'p', "nopassphrase", NULL, G_TYPE_BOOL },
		{ 'P', "nonewpassphrase", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-pPv] [-n keyno] [-i iterations] [-j passfile] [-J newpassfile] [-k keyfile] [-K newkeyfile] prov"
	},
	{ "delkey", G_FLAG_VERBOSE, eli_main,
	    {
		{ 'a', "all", NULL, G_TYPE_BOOL },
		{ 'f', "force", NULL, G_TYPE_BOOL },
		{ 'n', "keyno", "-1", G_TYPE_NUMBER },
		G_OPT_SENTINEL
	    },
	    "[-afv] [-n keyno] prov"
	},
	{ "suspend", G_FLAG_VERBOSE, NULL,
	    {
		{ 'a', "all", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-v] -a | prov ..."
	},
	{ "resume", G_FLAG_VERBOSE, eli_main,
	    {
		{ 'j', "passfile", G_VAL_OPTIONAL, G_TYPE_STRING | G_TYPE_MULTI },
		{ 'k', "keyfile", G_VAL_OPTIONAL, G_TYPE_STRING | G_TYPE_MULTI },
		{ 'p', "nopassphrase", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-pv] [-j passfile] [-k keyfile] prov"
	},
	{ "kill", G_FLAG_VERBOSE, eli_main,
	    {
		{ 'a', "all", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-av] [prov ...]"
	},
	{ "backup", G_FLAG_VERBOSE, eli_main, G_NULL_OPTS,
	    "[-v] prov file"
	},
	{ "restore", G_FLAG_VERBOSE, eli_main,
	    {
		{ 'f', "force", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-fv] file prov"
	},
	{ "resize", G_FLAG_VERBOSE, eli_main,
	    {
		{ 's', "oldsize", NULL, G_TYPE_NUMBER },
		G_OPT_SENTINEL
	    },
	    "[-v] -s oldsize prov"
	},
	{ "version", G_FLAG_LOADKLD, eli_main, G_NULL_OPTS,
	    "[prov ...]"
	},
	{ "clear", G_FLAG_VERBOSE, eli_main, G_NULL_OPTS,
	    "[-v] prov ..."
	},
	{ "dump", G_FLAG_VERBOSE, eli_main, G_NULL_OPTS,
	    "[-v] prov ..."
	},
	G_CMD_SENTINEL
};

static int verbose = 0;

static int
eli_protect(struct gctl_req *req)
{
	struct rlimit rl;

	/* Disable core dumps. */
	rl.rlim_cur = 0;
	rl.rlim_max = 0;
	if (setrlimit(RLIMIT_CORE, &rl) == -1) {
		gctl_error(req, "Cannot disable core dumps: %s.",
		    strerror(errno));
		return (-1);
	}
	/* Disable swapping. */
	if (mlockall(MCL_FUTURE) == -1) {
		gctl_error(req, "Cannot lock memory: %s.", strerror(errno));
		return (-1);
	}
	return (0);
}

static void
eli_main(struct gctl_req *req, unsigned int flags)
{
	const char *name;

	if (eli_protect(req) == -1)
		return;

	if ((flags & G_FLAG_VERBOSE) != 0)
		verbose = 1;

	name = gctl_get_ascii(req, "verb");
	if (name == NULL) {
		gctl_error(req, "No '%s' argument.", "verb");
		return;
	}
	if (strcmp(name, "init") == 0 || strcmp(name, "label") == 0)
		eli_init(req);
	else if (strcmp(name, "attach") == 0)
		eli_attach(req);
	else if (strcmp(name, "configure") == 0)
		eli_configure(req);
	else if (strcmp(name, "setkey") == 0)
		eli_setkey(req);
	else if (strcmp(name, "delkey") == 0)
		eli_delkey(req);
	else if (strcmp(name, "resume") == 0)
		eli_resume(req);
	else if (strcmp(name, "kill") == 0)
		eli_kill(req);
	else if (strcmp(name, "backup") == 0)
		eli_backup(req);
	else if (strcmp(name, "restore") == 0)
		eli_restore(req);
	else if (strcmp(name, "resize") == 0)
		eli_resize(req);
	else if (strcmp(name, "version") == 0)
		eli_version(req);
	else if (strcmp(name, "dump") == 0)
		eli_dump(req);
	else if (strcmp(name, "clear") == 0)
		eli_clear(req);
	else
		gctl_error(req, "Unknown command: %s.", name);
}

static bool
eli_is_attached(const char *prov)
{
	char name[MAXPATHLEN];

	/*
	 * Not the best way to do it, but the easiest.
	 * We try to open provider and check if it is a GEOM provider
	 * by asking about its sectorsize.
	 */
	snprintf(name, sizeof(name), "%s%s", prov, G_ELI_SUFFIX);
	return (g_get_sectorsize(name) > 0);
}

static int
eli_genkey_files(struct gctl_req *req, bool new, const char *type,
    struct hmac_ctx *ctxp, char *passbuf, size_t passbufsize)
{
	char *p, buf[BUFSIZE], argname[16];
	const char *file;
	int error, fd, i;
	ssize_t done;

	assert((strcmp(type, "keyfile") == 0 && ctxp != NULL &&
	    passbuf == NULL && passbufsize == 0) ||
	    (strcmp(type, "passfile") == 0 && ctxp == NULL &&
	    passbuf != NULL && passbufsize > 0));
	assert(strcmp(type, "keyfile") == 0 || passbuf[0] == '\0');

	for (i = 0; ; i++) {
		snprintf(argname, sizeof(argname), "%s%s%d",
		    new ? "new" : "", type, i);

		/* No more {key,pass}files? */
		if (!gctl_has_param(req, argname))
			return (i);

		file = gctl_get_ascii(req, "%s", argname);
		assert(file != NULL);

		if (strcmp(file, "-") == 0)
			fd = STDIN_FILENO;
		else {
			fd = open(file, O_RDONLY);
			if (fd == -1) {
				gctl_error(req, "Cannot open %s %s: %s.",
				    type, file, strerror(errno));
				return (-1);
			}
		}
		if (strcmp(type, "keyfile") == 0) {
			while ((done = read(fd, buf, sizeof(buf))) > 0)
				g_eli_crypto_hmac_update(ctxp, buf, done);
		} else /* if (strcmp(type, "passfile") == 0) */ {
			assert(strcmp(type, "passfile") == 0);

			while ((done = read(fd, buf, sizeof(buf) - 1)) > 0) {
				buf[done] = '\0';
				p = strchr(buf, '\n');
				if (p != NULL) {
					*p = '\0';
					done = p - buf;
				}
				if (strlcat(passbuf, buf, passbufsize) >=
				    passbufsize) {
					gctl_error(req,
					    "Passphrase in %s too long.", file);
					explicit_bzero(buf, sizeof(buf));
					return (-1);
				}
				if (p != NULL)
					break;
			}
		}
		error = errno;
		if (strcmp(file, "-") != 0)
			close(fd);
		explicit_bzero(buf, sizeof(buf));
		if (done == -1) {
			gctl_error(req, "Cannot read %s %s: %s.",
			    type, file, strerror(error));
			return (-1);
		}
	}
	/* NOTREACHED */
}

static int
eli_genkey_passphrase_prompt(struct gctl_req *req, bool new, char *passbuf,
    size_t passbufsize)
{
	char *p;

	for (;;) {
		p = readpassphrase(
		    new ? "Enter new passphrase: " : "Enter passphrase: ",
		    passbuf, passbufsize, RPP_ECHO_OFF | RPP_REQUIRE_TTY);
		if (p == NULL) {
			explicit_bzero(passbuf, passbufsize);
			gctl_error(req, "Cannot read passphrase: %s.",
			    strerror(errno));
			return (-1);
		}

		if (new) {
			char tmpbuf[BUFSIZE];

			p = readpassphrase("Reenter new passphrase: ",
			    tmpbuf, sizeof(tmpbuf),
			    RPP_ECHO_OFF | RPP_REQUIRE_TTY);
			if (p == NULL) {
				explicit_bzero(passbuf, passbufsize);
				gctl_error(req,
				    "Cannot read passphrase: %s.",
				    strerror(errno));
				return (-1);
			}

			if (strcmp(passbuf, tmpbuf) != 0) {
				explicit_bzero(passbuf, passbufsize);
				fprintf(stderr, "They didn't match.\n");
				continue;
			}
			explicit_bzero(tmpbuf, sizeof(tmpbuf));
		}
		return (0);
	}
	/* NOTREACHED */
}

static int
eli_genkey_passphrase(struct gctl_req *req, struct g_eli_metadata *md, bool new,
    struct hmac_ctx *ctxp)
{
	char passbuf[BUFSIZE];
	bool nopassphrase;
	int nfiles;

	/*
	 * Return error if the 'do not use passphrase' flag was given but a
	 * passfile was provided.
	 */
	nopassphrase =
	    gctl_get_int(req, new ? "nonewpassphrase" : "nopassphrase");
	if (nopassphrase) {
		if (gctl_has_param(req, new ? "newpassfile0" : "passfile0")) {
			gctl_error(req,
			    "Options -%c and -%c are mutually exclusive.",
			    new ? 'J' : 'j', new ? 'P' : 'p');
			return (-1);
		}
		return (0);
	}

	/*
	 * Return error if using a provider which does not require a passphrase
	 * but the 'do not use passphrase' flag was not given.
	 */
	if (!new && md->md_iterations == -1) {
		gctl_error(req, "Missing -p flag.");
		return (-1);
	}
	passbuf[0] = '\0';

	/* Use cached passphrase if defined. */
	if (strlen(cached_passphrase) > 0) {
		strlcpy(passbuf, cached_passphrase, sizeof(passbuf));
	} else {
		nfiles = eli_genkey_files(req, new, "passfile", NULL, passbuf,
		    sizeof(passbuf));
		if (nfiles == -1) {
			return (-1);
		} else if (nfiles == 0) {
			if (eli_genkey_passphrase_prompt(req, new, passbuf,
			    sizeof(passbuf)) == -1) {
				return (-1);
			}
		}
		/* Cache the passphrase for other providers. */
		strlcpy(cached_passphrase, passbuf, sizeof(cached_passphrase));
	}
	/*
	 * Field md_iterations equal to -1 means "choose some sane
	 * value for me".
	 */
	if (md->md_iterations == -1) {
		assert(new);
		if (verbose)
			printf("Calculating number of iterations...\n");
		md->md_iterations = pkcs5v2_calculate(2000000);
		assert(md->md_iterations > 0);
		if (verbose) {
			printf("Done, using %d iterations.\n",
			    md->md_iterations);
		}
	}
	/*
	 * If md_iterations is equal to 0, user doesn't want PKCS#5v2.
	 */
	if (md->md_iterations == 0) {
		g_eli_crypto_hmac_update(ctxp, md->md_salt,
		    sizeof(md->md_salt));
		g_eli_crypto_hmac_update(ctxp, passbuf, strlen(passbuf));
	} else /* if (md->md_iterations > 0) */ {
		unsigned char dkey[G_ELI_USERKEYLEN];

		pkcs5v2_genkey(dkey, sizeof(dkey), md->md_salt,
		    sizeof(md->md_salt), passbuf, md->md_iterations);
		g_eli_crypto_hmac_update(ctxp, dkey, sizeof(dkey));
		explicit_bzero(dkey, sizeof(dkey));
	}
	explicit_bzero(passbuf, sizeof(passbuf));

	return (0);
}

static unsigned char *
eli_genkey(struct gctl_req *req, struct g_eli_metadata *md, unsigned char *key,
    bool new)
{
	struct hmac_ctx ctx;
	bool nopassphrase;
	int nfiles;

	nopassphrase =
	    gctl_get_int(req, new ? "nonewpassphrase" : "nopassphrase");

	g_eli_crypto_hmac_init(&ctx, NULL, 0);

	nfiles = eli_genkey_files(req, new, "keyfile", &ctx, NULL, 0);
	if (nfiles == -1)
		return (NULL);
	else if (nfiles == 0 && nopassphrase) {
		gctl_error(req, "No key components given.");
		return (NULL);
	}

	if (eli_genkey_passphrase(req, md, new, &ctx) == -1)
		return (NULL);

	g_eli_crypto_hmac_final(&ctx, key, 0);

	return (key);
}

static int
eli_metadata_read(struct gctl_req *req, const char *prov,
    struct g_eli_metadata *md)
{
	unsigned char sector[sizeof(struct g_eli_metadata)];
	int error;

	if (g_get_sectorsize(prov) == 0) {
		int fd;

		/* This is a file probably. */
		fd = open(prov, O_RDONLY);
		if (fd == -1) {
			gctl_error(req, "Cannot open %s: %s.", prov,
			    strerror(errno));
			return (-1);
		}
		if (read(fd, sector, sizeof(sector)) != sizeof(sector)) {
			gctl_error(req, "Cannot read metadata from %s: %s.",
			    prov, strerror(errno));
			close(fd);
			return (-1);
		}
		close(fd);
	} else {
		/* This is a GEOM provider. */
		error = g_metadata_read(prov, sector, sizeof(sector),
		    G_ELI_MAGIC);
		if (error != 0) {
			gctl_error(req, "Cannot read metadata from %s: %s.",
			    prov, strerror(error));
			return (-1);
		}
	}
	error = eli_metadata_decode(sector, md);
	switch (error) {
	case 0:
		break;
	case EOPNOTSUPP:
		gctl_error(req,
		    "Provider's %s metadata version %u is too new.\n"
		    "geli: The highest supported version is %u.",
		    prov, (unsigned int)md->md_version, G_ELI_VERSION);
		return (-1);
	case EINVAL:
		gctl_error(req, "Inconsistent provider's %s metadata.", prov);
		return (-1);
	default:
		gctl_error(req,
		    "Unexpected error while decoding provider's %s metadata: %s.",
		    prov, strerror(error));
		return (-1);
	}
	return (0);
}

static int
eli_metadata_store(struct gctl_req *req, const char *prov,
    struct g_eli_metadata *md)
{
	unsigned char sector[sizeof(struct g_eli_metadata)];
	int error;

	eli_metadata_encode(md, sector);
	if (g_get_sectorsize(prov) == 0) {
		int fd;

		/* This is a file probably. */
		fd = open(prov, O_WRONLY | O_TRUNC);
		if (fd == -1) {
			gctl_error(req, "Cannot open %s: %s.", prov,
			    strerror(errno));
			explicit_bzero(sector, sizeof(sector));
			return (-1);
		}
		if (write(fd, sector, sizeof(sector)) != sizeof(sector)) {
			gctl_error(req, "Cannot write metadata to %s: %s.",
			    prov, strerror(errno));
			explicit_bzero(sector, sizeof(sector));
			close(fd);
			return (-1);
		}
		close(fd);
	} else {
		/* This is a GEOM provider. */
		error = g_metadata_store(prov, sector, sizeof(sector));
		if (error != 0) {
			gctl_error(req, "Cannot write metadata to %s: %s.",
			    prov, strerror(errno));
			explicit_bzero(sector, sizeof(sector));
			return (-1);
		}
	}
	explicit_bzero(sector, sizeof(sector));
	return (0);
}

static void
eli_init(struct gctl_req *req)
{
	struct g_eli_metadata md;
	struct gctl_req *r;
	unsigned char sector[sizeof(struct g_eli_metadata)] __aligned(4);
	unsigned char key[G_ELI_USERKEYLEN];
	char backfile[MAXPATHLEN];
	const char *str, *prov;
	unsigned int secsize, version;
	off_t mediasize;
	intmax_t val;
	int error, i, nargs, nparams, param;
	const int one = 1;

	nargs = gctl_get_int(req, "nargs");
	if (nargs <= 0) {
		gctl_error(req, "Too few arguments.");
		return;
	}

	/* Start generating metadata for provider(s) being initialized. */
	explicit_bzero(&md, sizeof(md));
	strlcpy(md.md_magic, G_ELI_MAGIC, sizeof(md.md_magic));
	val = gctl_get_intmax(req, "mdversion");
	if (val == -1) {
		version = G_ELI_VERSION;
	} else if (val < 0 || val > G_ELI_VERSION) {
		gctl_error(req,
		    "Invalid version specified should be between %u and %u.",
		    G_ELI_VERSION_00, G_ELI_VERSION);
		return;
	} else {
		version = val;
	}
	md.md_version = version;
	md.md_flags = 0;
	if (gctl_get_int(req, "boot"))
		md.md_flags |= G_ELI_FLAG_BOOT;
	if (gctl_get_int(req, "geliboot"))
		md.md_flags |= G_ELI_FLAG_GELIBOOT;
	if (gctl_get_int(req, "displaypass"))
		md.md_flags |= G_ELI_FLAG_GELIDISPLAYPASS;
	if (gctl_get_int(req, "notrim"))
		md.md_flags |= G_ELI_FLAG_NODELETE;
	md.md_ealgo = CRYPTO_ALGORITHM_MIN - 1;
	str = gctl_get_ascii(req, "aalgo");
	if (*str != '\0') {
		if (version < G_ELI_VERSION_01) {
			gctl_error(req,
			    "Data authentication is supported starting from version %u.",
			    G_ELI_VERSION_01);
			return;
		}
		md.md_aalgo = g_eli_str2aalgo(str);
		if (md.md_aalgo >= CRYPTO_ALGORITHM_MIN &&
		    md.md_aalgo <= CRYPTO_ALGORITHM_MAX) {
			md.md_flags |= G_ELI_FLAG_AUTH;
		} else {
			/*
			 * For backward compatibility, check if the -a option
			 * was used to provide encryption algorithm.
			 */
			md.md_ealgo = g_eli_str2ealgo(str);
			if (md.md_ealgo < CRYPTO_ALGORITHM_MIN ||
			    md.md_ealgo > CRYPTO_ALGORITHM_MAX) {
				gctl_error(req,
				    "Invalid authentication algorithm.");
				return;
			} else {
				fprintf(stderr, "warning: The -e option, not "
				    "the -a option is now used to specify "
				    "encryption algorithm to use.\n");
			}
		}
	}
	if (md.md_ealgo < CRYPTO_ALGORITHM_MIN ||
	    md.md_ealgo > CRYPTO_ALGORITHM_MAX) {
		str = gctl_get_ascii(req, "ealgo");
		if (*str == '\0') {
			if (version < G_ELI_VERSION_05)
				str = "aes-cbc";
			else
				str = GELI_ENC_ALGO;
		}
		md.md_ealgo = g_eli_str2ealgo(str);
		if (md.md_ealgo < CRYPTO_ALGORITHM_MIN ||
		    md.md_ealgo > CRYPTO_ALGORITHM_MAX) {
			gctl_error(req, "Invalid encryption algorithm.");
			return;
		}
		if (md.md_ealgo == CRYPTO_CAMELLIA_CBC &&
		    version < G_ELI_VERSION_04) {
			gctl_error(req,
			    "Camellia-CBC algorithm is supported starting from version %u.",
			    G_ELI_VERSION_04);
			return;
		}
		if (md.md_ealgo == CRYPTO_AES_XTS &&
		    version < G_ELI_VERSION_05) {
			gctl_error(req,
			    "AES-XTS algorithm is supported starting from version %u.",
			    G_ELI_VERSION_05);
			return;
		}
	}
	val = gctl_get_intmax(req, "keylen");
	md.md_keylen = val;
	md.md_keylen = g_eli_keylen(md.md_ealgo, md.md_keylen);
	if (md.md_keylen == 0) {
		gctl_error(req, "Invalid key length.");
		return;
	}

	val = gctl_get_intmax(req, "iterations");
	if (val != -1) {
		int nonewpassphrase;

		/*
		 * Don't allow to set iterations when there will be no
		 * passphrase.
		 */
		nonewpassphrase = gctl_get_int(req, "nonewpassphrase");
		if (nonewpassphrase) {
			gctl_error(req,
			    "Options -i and -P are mutually exclusive.");
			return;
		}
	}
	md.md_iterations = val;

	val = gctl_get_intmax(req, "sectorsize");
	if (val > sysconf(_SC_PAGE_SIZE)) {
		fprintf(stderr,
		    "warning: Using sectorsize bigger than the page size!\n");
	}

	md.md_keys = 0x01;

	/*
	 * Determine number of parameters in the parent geom request before the
	 * nargs parameter and list of providers.
	 */
	nparams = req->narg - nargs - 1;

	/* Create new child request for each provider and issue to kernel */
	for (i = 0; i < nargs; i++) {
		r = gctl_get_handle();

		/* Copy each parameter from the parent request to the child */
		for (param = 0; param < nparams; param++) {
			gctl_ro_param(r, req->arg[param].name,
			    req->arg[param].len, req->arg[param].value);
		}

		/* Add a single provider to the parameter list of the child */
		gctl_ro_param(r, "nargs", sizeof(one), &one);
		prov = gctl_get_ascii(req, "arg%d", i);
		gctl_ro_param(r, "arg0", -1, prov);

		mediasize = g_get_mediasize(prov);
		secsize = g_get_sectorsize(prov);
		if (mediasize == 0 || secsize == 0) {
			gctl_error(r, "Cannot get information about %s: %s.",
			    prov, strerror(errno));
			goto out;
		}

		md.md_provsize = mediasize;

		val = gctl_get_intmax(r, "sectorsize");
		if (val == 0) {
			md.md_sectorsize = secsize;
		} else {
			if (val < 0 || (val % secsize) != 0 || !powerof2(val)) {
				gctl_error(r, "Invalid sector size.");
				goto out;
			}
			md.md_sectorsize = val;
		}

		/* Use different salt and Master Key for each provider. */
		arc4random_buf(md.md_salt, sizeof(md.md_salt));
		arc4random_buf(md.md_mkeys, sizeof(md.md_mkeys));

		/* Generate user key. */
		if (eli_genkey(r, &md, key, true) == NULL) {
			/*
			 * Error generating key - details added to geom request
			 * by eli_genkey().
			 */
			goto out;
		}

		/* Encrypt the first and the only Master Key. */
		error = g_eli_mkey_encrypt(md.md_ealgo, key, md.md_keylen,
		    md.md_mkeys);
		if (error != 0) {
			gctl_error(r, "Cannot encrypt Master Key: %s.",
			    strerror(error));
			goto out;
		}

		/* Convert metadata to on-disk format. */
		eli_metadata_encode(&md, sector);

		/* Store metadata to disk. */
		error = g_metadata_store(prov, sector, sizeof(sector));
		if (error != 0) {
			gctl_error(r, "Cannot store metadata on %s: %s.", prov,
			    strerror(error));
			goto out;
		}
		if (verbose)
			printf("Metadata value stored on %s.\n", prov);

		/* Backup metadata to a file. */
		const char *p = prov;
		unsigned int j;

		/*
		 * Check if provider string includes the devfs mountpoint
		 * (typically /dev/).
		 */
		if (strncmp(p, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0) {
			/* Skip forward to the device filename only. */
			p += sizeof(_PATH_DEV) - 1;
		}

		str = gctl_get_ascii(r, "backupfile");
		if (str[0] != '\0') {
			/* Backupfile given by the user, just copy it. */
			strlcpy(backfile, str, sizeof(backfile));

			/* If multiple providers have been initialized in one
			 * command, and the backup filename has been specified
			 * as anything other than "none", make the backup
			 * filename unique for each provider. */
			if (nargs > 1 && strcmp(backfile, "none") != 0) {
				/*
				 * Replace first occurrence of "PROV" with
				 * provider name.
				 */
				str = strnstr(backfile, "PROV",
				    sizeof(backfile));
				if (str != NULL) {
					char suffix[MAXPATHLEN];
					j = str - backfile;
					strlcpy(suffix, &backfile[j+4],
					    sizeof(suffix));
					backfile[j] = '\0';
					strlcat(backfile, p, sizeof(backfile));
					strlcat(backfile, suffix,
					    sizeof(backfile));
				} else {
					/*
					 * "PROV" not found in backfile, append
					 * provider name.
					 */
					strlcat(backfile, "-",
					    sizeof(backfile));
					strlcat(backfile, p, sizeof(backfile));
				}
			}
		} else {
			/* Generate filename automatically. */
			snprintf(backfile, sizeof(backfile), "%s%s.eli",
			    GELI_BACKUP_DIR, p);
			/* Replace all / with _. */
			for (j = strlen(GELI_BACKUP_DIR); backfile[j] != '\0';
			    j++) {
				if (backfile[j] == '/')
					backfile[j] = '_';
			}
		}
		if (strcmp(backfile, "none") != 0 &&
		    eli_backup_create(r, prov, backfile) == 0) {
			printf("\nMetadata backup for provider %s can be found "
			    "in %s\n", prov, backfile);
			printf("and can be restored with the following "
			    "command:\n");
			printf("\n\t# geli restore %s %s\n\n", backfile, prov);
		}

out:
		/*
		 * Print error for this request, and set parent request error
		 * message.
		 */
		if (r->error != NULL && r->error[0] != '\0') {
			warnx("%s", r->error);
			gctl_error(req, "There was an error with at least one "
			    "provider.");
		}

		gctl_free(r);

		/*
		 * Erase sensitive and provider specific data from memory.
		 */
		explicit_bzero(key, sizeof(key));
		explicit_bzero(sector, sizeof(sector));
		explicit_bzero(&md.md_provsize, sizeof(md.md_provsize));
		explicit_bzero(&md.md_sectorsize, sizeof(md.md_sectorsize));
		explicit_bzero(&md.md_salt, sizeof(md.md_salt));
		explicit_bzero(&md.md_mkeys, sizeof(md.md_mkeys));
	}

	/* Clear the cached metadata, including keys. */
	explicit_bzero(&md, sizeof(md));
}

static void
eli_attach(struct gctl_req *req)
{
	struct g_eli_metadata md;
	struct gctl_req *r;
	const char *prov;
	off_t mediasize;
	int i, nargs, nparams, param;
	const int one = 1;

	nargs = gctl_get_int(req, "nargs");
	if (nargs <= 0) {
		gctl_error(req, "Too few arguments.");
		return;
	}

	unsigned char key[G_ELI_USERKEYLEN];

	/*
	 * Determine number of parameters in the parent geom request before the
	 * nargs parameter and list of providers.
	 */
	nparams = req->narg - nargs - 1;

	/* Create new child request for each provider and issue to kernel */
	for (i = 0; i < nargs; i++) {
		r = gctl_get_handle();

		/* Copy each parameter from the parent request to the child */
		for (param = 0; param < nparams; param++) {
			gctl_ro_param(r, req->arg[param].name,
			    req->arg[param].len, req->arg[param].value);
		}

		/* Add a single provider to the parameter list of the child */
		gctl_ro_param(r, "nargs", sizeof(one), &one);
		prov = gctl_get_ascii(req, "arg%d", i);
		gctl_ro_param(r, "arg0", -1, prov);

		if (eli_metadata_read(r, prov, &md) == -1) {
			/*
			 * Error reading metadata - details added to geom
			 * request by eli_metadata_read().
			 */
			goto out;
		}

		mediasize = g_get_mediasize(prov);
		if (md.md_provsize != (uint64_t)mediasize) {
			gctl_error(r, "Provider size mismatch.");
			goto out;
		}

		if (eli_genkey(r, &md, key, false) == NULL) {
			/*
			 * Error generating key - details added to geom request
			 * by eli_genkey().
			 */
			goto out;
		}

		gctl_ro_param(r, "key", sizeof(key), key);

		if (gctl_issue(r) == NULL) {
			if (verbose)
				printf("Attached to %s.\n", prov);
		}

out:
		/*
		 * Print error for this request, and set parent request error
		 * message.
		 */
		if (r->error != NULL && r->error[0] != '\0') {
			warnx("%s", r->error);
			gctl_error(req, "There was an error with at least one "
			    "provider.");
		}

		gctl_free(r);

		/* Clear sensitive data from memory. */
		explicit_bzero(key, sizeof(key));
	}

	/* Clear sensitive data from memory. */
	explicit_bzero(cached_passphrase, sizeof(cached_passphrase));
}

static void
eli_configure_detached(struct gctl_req *req, const char *prov, int boot,
    int geliboot, int displaypass, int trim)
{
	struct g_eli_metadata md;
	bool changed = 0;

	if (eli_metadata_read(req, prov, &md) == -1)
		return;

	if (boot == 1 && (md.md_flags & G_ELI_FLAG_BOOT)) {
		if (verbose)
			printf("BOOT flag already configured for %s.\n", prov);
	} else if (boot == 0 && !(md.md_flags & G_ELI_FLAG_BOOT)) {
		if (verbose)
			printf("BOOT flag not configured for %s.\n", prov);
	} else if (boot >= 0) {
		if (boot)
			md.md_flags |= G_ELI_FLAG_BOOT;
		else
			md.md_flags &= ~G_ELI_FLAG_BOOT;
		changed = 1;
	}

	if (geliboot == 1 && (md.md_flags & G_ELI_FLAG_GELIBOOT)) {
		if (verbose)
			printf("GELIBOOT flag already configured for %s.\n", prov);
	} else if (geliboot == 0 && !(md.md_flags & G_ELI_FLAG_GELIBOOT)) {
		if (verbose)
			printf("GELIBOOT flag not configured for %s.\n", prov);
	} else if (geliboot >= 0) {
		if (geliboot)
			md.md_flags |= G_ELI_FLAG_GELIBOOT;
		else
			md.md_flags &= ~G_ELI_FLAG_GELIBOOT;
		changed = 1;
	}

	if (displaypass == 1 && (md.md_flags & G_ELI_FLAG_GELIDISPLAYPASS)) {
		if (verbose)
			printf("GELIDISPLAYPASS flag already configured for %s.\n", prov);
	} else if (displaypass == 0 &&
	    !(md.md_flags & G_ELI_FLAG_GELIDISPLAYPASS)) {
		if (verbose)
			printf("GELIDISPLAYPASS flag not configured for %s.\n", prov);
	} else if (displaypass >= 0) {
		if (displaypass)
			md.md_flags |= G_ELI_FLAG_GELIDISPLAYPASS;
		else
			md.md_flags &= ~G_ELI_FLAG_GELIDISPLAYPASS;
		changed = 1;
	}

	if (trim == 0 && (md.md_flags & G_ELI_FLAG_NODELETE)) {
		if (verbose)
			printf("TRIM disable flag already configured for %s.\n", prov);
	} else if (trim == 1 && !(md.md_flags & G_ELI_FLAG_NODELETE)) {
		if (verbose)
			printf("TRIM disable flag not configured for %s.\n", prov);
	} else if (trim >= 0) {
		if (trim)
			md.md_flags &= ~G_ELI_FLAG_NODELETE;
		else
			md.md_flags |= G_ELI_FLAG_NODELETE;
		changed = 1;
	}

	if (changed)
		eli_metadata_store(req, prov, &md);
	explicit_bzero(&md, sizeof(md));
}

static void
eli_configure(struct gctl_req *req)
{
	const char *prov;
	bool boot, noboot, geliboot, nogeliboot, displaypass, nodisplaypass;
	bool trim, notrim;
	int doboot, dogeliboot, dodisplaypass, dotrim;
	int i, nargs;

	nargs = gctl_get_int(req, "nargs");
	if (nargs == 0) {
		gctl_error(req, "Too few arguments.");
		return;
	}

	boot = gctl_get_int(req, "boot");
	noboot = gctl_get_int(req, "noboot");
	geliboot = gctl_get_int(req, "geliboot");
	nogeliboot = gctl_get_int(req, "nogeliboot");
	displaypass = gctl_get_int(req, "displaypass");
	nodisplaypass = gctl_get_int(req, "nodisplaypass");
	trim = gctl_get_int(req, "trim");
	notrim = gctl_get_int(req, "notrim");

	doboot = -1;
	if (boot && noboot) {
		gctl_error(req, "Options -b and -B are mutually exclusive.");
		return;
	}
	if (boot)
		doboot = 1;
	else if (noboot)
		doboot = 0;

	dogeliboot = -1;
	if (geliboot && nogeliboot) {
		gctl_error(req, "Options -g and -G are mutually exclusive.");
		return;
	}
	if (geliboot)
		dogeliboot = 1;
	else if (nogeliboot)
		dogeliboot = 0;

	dodisplaypass = -1;
	if (displaypass && nodisplaypass) {
		gctl_error(req, "Options -d and -D are mutually exclusive.");
		return;
	}
	if (displaypass)
		dodisplaypass = 1;
	else if (nodisplaypass)
		dodisplaypass = 0;

	dotrim = -1;
	if (trim && notrim) {
		gctl_error(req, "Options -t and -T are mutually exclusive.");
		return;
	}
	if (trim)
		dotrim = 1;
	else if (notrim)
		dotrim = 0;

	if (doboot == -1 && dogeliboot == -1 && dodisplaypass == -1 &&
	    dotrim == -1) {
		gctl_error(req, "No option given.");
		return;
	}

	/* First attached providers. */
	gctl_issue(req);
	/* Now the rest. */
	for (i = 0; i < nargs; i++) {
		prov = gctl_get_ascii(req, "arg%d", i);
		if (!eli_is_attached(prov)) {
			eli_configure_detached(req, prov, doboot, dogeliboot,
			    dodisplaypass, dotrim);
		}
	}
}

static void
eli_setkey_attached(struct gctl_req *req, struct g_eli_metadata *md)
{
	unsigned char key[G_ELI_USERKEYLEN];
	intmax_t val, old = 0;
	int error;

	val = gctl_get_intmax(req, "iterations");
	/* Check if iterations number should be changed. */
	if (val != -1)
		md->md_iterations = val;
	else
		old = md->md_iterations;

	/* Generate key for Master Key encryption. */
	if (eli_genkey(req, md, key, true) == NULL) {
		explicit_bzero(key, sizeof(key));
		return;
	}
	/*
	 * If number of iterations has changed, but wasn't given as a
	 * command-line argument, update the request.
	 */
	if (val == -1 && md->md_iterations != old) {
		error = gctl_change_param(req, "iterations", sizeof(intmax_t),
		    &md->md_iterations);
		assert(error == 0);
	}

	gctl_ro_param(req, "key", sizeof(key), key);
	gctl_issue(req);
	explicit_bzero(key, sizeof(key));
}

static void
eli_setkey_detached(struct gctl_req *req, const char *prov,
 struct g_eli_metadata *md)
{
	unsigned char key[G_ELI_USERKEYLEN], mkey[G_ELI_DATAIVKEYLEN];
	unsigned char *mkeydst;
	unsigned int nkey;
	intmax_t val;
	int error;

	if (md->md_keys == 0) {
		gctl_error(req, "No valid keys on %s.", prov);
		return;
	}

	/* Generate key for Master Key decryption. */
	if (eli_genkey(req, md, key, false) == NULL) {
		explicit_bzero(key, sizeof(key));
		return;
	}

	/* Decrypt Master Key. */
	error = g_eli_mkey_decrypt_any(md, key, mkey, &nkey);
	explicit_bzero(key, sizeof(key));
	if (error != 0) {
		explicit_bzero(md, sizeof(*md));
		if (error == -1)
			gctl_error(req, "Wrong key for %s.", prov);
		else /* if (error > 0) */ {
			gctl_error(req, "Cannot decrypt Master Key: %s.",
			    strerror(error));
		}
		return;
	}
	if (verbose)
		printf("Decrypted Master Key %u.\n", nkey);

	val = gctl_get_intmax(req, "keyno");
	if (val != -1)
		nkey = val;
#if 0
	else
		; /* Use the key number which was found during decryption. */
#endif
	if (nkey >= G_ELI_MAXMKEYS) {
		gctl_error(req, "Invalid '%s' argument.", "keyno");
		return;
	}

	val = gctl_get_intmax(req, "iterations");
	/* Check if iterations number should and can be changed. */
	if (val != -1 && md->md_iterations == -1) {
		md->md_iterations = val;
	} else if (val != -1 && val != md->md_iterations) {
		if (bitcount32(md->md_keys) != 1) {
			gctl_error(req, "To be able to use '-i' option, only "
			    "one key can be defined.");
			return;
		}
		if (md->md_keys != (1 << nkey)) {
			gctl_error(req, "Only already defined key can be "
			    "changed when '-i' option is used.");
			return;
		}
		md->md_iterations = val;
	}

	mkeydst = md->md_mkeys + nkey * G_ELI_MKEYLEN;
	md->md_keys |= (1 << nkey);

	bcopy(mkey, mkeydst, sizeof(mkey));
	explicit_bzero(mkey, sizeof(mkey));

	/* Generate key for Master Key encryption. */
	if (eli_genkey(req, md, key, true) == NULL) {
		explicit_bzero(key, sizeof(key));
		explicit_bzero(md, sizeof(*md));
		return;
	}

	/* Encrypt the Master-Key with the new key. */
	error = g_eli_mkey_encrypt(md->md_ealgo, key, md->md_keylen, mkeydst);
	explicit_bzero(key, sizeof(key));
	if (error != 0) {
		explicit_bzero(md, sizeof(*md));
		gctl_error(req, "Cannot encrypt Master Key: %s.",
		    strerror(error));
		return;
	}

	/* Store metadata with fresh key. */
	eli_metadata_store(req, prov, md);
	explicit_bzero(md, sizeof(*md));
}

static void
eli_setkey(struct gctl_req *req)
{
	struct g_eli_metadata md;
	const char *prov;
	int nargs;

	nargs = gctl_get_int(req, "nargs");
	if (nargs != 1) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}
	prov = gctl_get_ascii(req, "arg0");

	if (eli_metadata_read(req, prov, &md) == -1)
		return;

	if (eli_is_attached(prov))
		eli_setkey_attached(req, &md);
	else
		eli_setkey_detached(req, prov, &md);

	if (req->error == NULL || req->error[0] == '\0') {
		printf("Note, that the master key encrypted with old keys "
		    "and/or passphrase may still exists in a metadata backup "
		    "file.\n");
	}
}

static void
eli_delkey_attached(struct gctl_req *req, const char *prov __unused)
{

	gctl_issue(req);
}

static void
eli_delkey_detached(struct gctl_req *req, const char *prov)
{
	struct g_eli_metadata md;
	unsigned char *mkeydst;
	unsigned int nkey;
	intmax_t val;
	bool all, force;

	if (eli_metadata_read(req, prov, &md) == -1)
		return;

	all = gctl_get_int(req, "all");
	if (all)
		arc4random_buf(md.md_mkeys, sizeof(md.md_mkeys));
	else {
		force = gctl_get_int(req, "force");
		val = gctl_get_intmax(req, "keyno");
		if (val == -1) {
			gctl_error(req, "Key number has to be specified.");
			return;
		}
		nkey = val;
		if (nkey >= G_ELI_MAXMKEYS) {
			gctl_error(req, "Invalid '%s' argument.", "keyno");
			return;
		}
		if (!(md.md_keys & (1 << nkey)) && !force) {
			gctl_error(req, "Master Key %u is not set.", nkey);
			return;
		}
		md.md_keys &= ~(1 << nkey);
		if (md.md_keys == 0 && !force) {
			gctl_error(req, "This is the last Master Key. Use '-f' "
			    "option if you really want to remove it.");
			return;
		}
		mkeydst = md.md_mkeys + nkey * G_ELI_MKEYLEN;
		arc4random_buf(mkeydst, G_ELI_MKEYLEN);
	}

	eli_metadata_store(req, prov, &md);
	explicit_bzero(&md, sizeof(md));
}

static void
eli_delkey(struct gctl_req *req)
{
	const char *prov;
	int nargs;

	nargs = gctl_get_int(req, "nargs");
	if (nargs != 1) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}
	prov = gctl_get_ascii(req, "arg0");

	if (eli_is_attached(prov))
		eli_delkey_attached(req, prov);
	else
		eli_delkey_detached(req, prov);
}

static void
eli_resume(struct gctl_req *req)
{
	struct g_eli_metadata md;
	unsigned char key[G_ELI_USERKEYLEN];
	const char *prov;
	off_t mediasize;
	int nargs;

	nargs = gctl_get_int(req, "nargs");
	if (nargs != 1) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}
	prov = gctl_get_ascii(req, "arg0");

	if (eli_metadata_read(req, prov, &md) == -1)
		return;

	mediasize = g_get_mediasize(prov);
	if (md.md_provsize != (uint64_t)mediasize) {
		gctl_error(req, "Provider size mismatch.");
		return;
	}

	if (eli_genkey(req, &md, key, false) == NULL) {
		explicit_bzero(key, sizeof(key));
		return;
	}

	gctl_ro_param(req, "key", sizeof(key), key);
	if (gctl_issue(req) == NULL) {
		if (verbose)
			printf("Resumed %s.\n", prov);
	}
	explicit_bzero(key, sizeof(key));
}

static int
eli_trash_metadata(struct gctl_req *req, const char *prov, int fd, off_t offset)
{
	unsigned int overwrites;
	unsigned char *sector;
	ssize_t size;
	int error;

	size = sizeof(overwrites);
	if (sysctlbyname("kern.geom.eli.overwrites", &overwrites, &size,
	    NULL, 0) == -1 || overwrites == 0) {
		overwrites = G_ELI_OVERWRITES;
	}

	size = g_sectorsize(fd);
	if (size <= 0) {
		gctl_error(req, "Cannot obtain provider sector size %s: %s.",
		    prov, strerror(errno));
		return (-1);
	}
	sector = malloc(size);
	if (sector == NULL) {
		gctl_error(req, "Cannot allocate %zd bytes of memory.", size);
		return (-1);
	}

	error = 0;
	do {
		arc4random_buf(sector, size);
		if (pwrite(fd, sector, size, offset) != size) {
			if (error == 0)
				error = errno;
		}
		(void)g_flush(fd);
	} while (--overwrites > 0);
	free(sector);
	if (error != 0) {
		gctl_error(req, "Cannot trash metadata on provider %s: %s.",
		    prov, strerror(error));
		return (-1);
	}
	return (0);
}

static void
eli_kill_detached(struct gctl_req *req, const char *prov)
{
	off_t offset;
	int fd;

	/*
	 * NOTE: Maybe we should verify if this is geli provider first,
	 *       but 'kill' command is quite critical so better don't waste
	 *       the time.
	 */
#if 0
	error = g_metadata_read(prov, (unsigned char *)&md, sizeof(md),
	    G_ELI_MAGIC);
	if (error != 0) {
		gctl_error(req, "Cannot read metadata from %s: %s.", prov,
		    strerror(error));
		return;
	}
#endif

	fd = g_open(prov, 1);
	if (fd == -1) {
		gctl_error(req, "Cannot open provider %s: %s.", prov,
		    strerror(errno));
		return;
	}
	offset = g_mediasize(fd) - g_sectorsize(fd);
	if (offset <= 0) {
		gctl_error(req,
		    "Cannot obtain media size or sector size for provider %s: %s.",
		    prov, strerror(errno));
		(void)g_close(fd);
		return;
	}
	(void)eli_trash_metadata(req, prov, fd, offset);
	(void)g_close(fd);
}

static void
eli_kill(struct gctl_req *req)
{
	const char *prov;
	int i, nargs, all;

	nargs = gctl_get_int(req, "nargs");
	all = gctl_get_int(req, "all");
	if (!all && nargs == 0) {
		gctl_error(req, "Too few arguments.");
		return;
	}
	/*
	 * How '-a' option combine with a list of providers:
	 * Delete Master Keys from all attached providers:
	 * geli kill -a
	 * Delete Master Keys from all attached providers and from
	 * detached da0 and da1:
	 * geli kill -a da0 da1
	 * Delete Master Keys from (attached or detached) da0 and da1:
	 * geli kill da0 da1
	 */

	/* First detached providers. */
	for (i = 0; i < nargs; i++) {
		prov = gctl_get_ascii(req, "arg%d", i);
		if (!eli_is_attached(prov))
			eli_kill_detached(req, prov);
	}
	/* Now attached providers. */
	gctl_issue(req);
}

static int
eli_backup_create(struct gctl_req *req, const char *prov, const char *file)
{
	unsigned char *sector;
	ssize_t secsize;
	int error, filefd, ret;

	ret = -1;
	filefd = -1;
	sector = NULL;
	secsize = 0;

	secsize = g_get_sectorsize(prov);
	if (secsize == 0) {
		gctl_error(req, "Cannot get informations about %s: %s.", prov,
		    strerror(errno));
		goto out;
	}
	sector = malloc(secsize);
	if (sector == NULL) {
		gctl_error(req, "Cannot allocate memory.");
		goto out;
	}
	/* Read metadata from the provider. */
	error = g_metadata_read(prov, sector, secsize, G_ELI_MAGIC);
	if (error != 0) {
		gctl_error(req, "Unable to read metadata from %s: %s.", prov,
		    strerror(error));
		goto out;
	}

	filefd = open(file, O_WRONLY | O_TRUNC | O_CREAT, 0600);
	if (filefd == -1) {
		gctl_error(req, "Unable to open %s: %s.", file,
		    strerror(errno));
		goto out;
	}
	/* Write metadata to the destination file. */
	if (write(filefd, sector, secsize) != secsize) {
		gctl_error(req, "Unable to write to %s: %s.", file,
		    strerror(errno));
		(void)close(filefd);
		(void)unlink(file);
		goto out;
	}
	(void)fsync(filefd);
	(void)close(filefd);
	/* Success. */
	ret = 0;
out:
	if (sector != NULL) {
		explicit_bzero(sector, secsize);
		free(sector);
	}
	return (ret);
}

static void
eli_backup(struct gctl_req *req)
{
	const char *file, *prov;
	int nargs;

	nargs = gctl_get_int(req, "nargs");
	if (nargs != 2) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}
	prov = gctl_get_ascii(req, "arg0");
	file = gctl_get_ascii(req, "arg1");

	eli_backup_create(req, prov, file);
}

static void
eli_restore(struct gctl_req *req)
{
	struct g_eli_metadata md;
	const char *file, *prov;
	off_t mediasize;
	int nargs;

	nargs = gctl_get_int(req, "nargs");
	if (nargs != 2) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}
	file = gctl_get_ascii(req, "arg0");
	prov = gctl_get_ascii(req, "arg1");

	/* Read metadata from the backup file. */
	if (eli_metadata_read(req, file, &md) == -1)
		return;
	/* Obtain provider's mediasize. */
	mediasize = g_get_mediasize(prov);
	if (mediasize == 0) {
		gctl_error(req, "Cannot get informations about %s: %s.", prov,
		    strerror(errno));
		return;
	}
	/* Check if the provider size has changed since we did the backup. */
	if (md.md_provsize != (uint64_t)mediasize) {
		if (gctl_get_int(req, "force")) {
			md.md_provsize = mediasize;
		} else {
			gctl_error(req, "Provider size mismatch: "
			    "wrong backup file?");
			return;
		}
	}
	/* Write metadata to the provider. */
	(void)eli_metadata_store(req, prov, &md);
}

static void
eli_resize(struct gctl_req *req)
{
	struct g_eli_metadata md;
	const char *prov;
	unsigned char *sector;
	ssize_t secsize;
	off_t mediasize, oldsize;
	int error, nargs, provfd;

	nargs = gctl_get_int(req, "nargs");
	if (nargs != 1) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}
	prov = gctl_get_ascii(req, "arg0");

	provfd = -1;
	sector = NULL;
	secsize = 0;

	provfd = g_open(prov, 1);
	if (provfd == -1) {
		gctl_error(req, "Cannot open %s: %s.", prov, strerror(errno));
		goto out;
	}

	mediasize = g_mediasize(provfd);
	secsize = g_sectorsize(provfd);
	if (mediasize == -1 || secsize == -1) {
		gctl_error(req, "Cannot get information about %s: %s.", prov,
		    strerror(errno));
		goto out;
	}

	sector = malloc(secsize);
	if (sector == NULL) {
		gctl_error(req, "Cannot allocate memory.");
		goto out;
	}

	oldsize = gctl_get_intmax(req, "oldsize");
	if (oldsize < 0 || oldsize > mediasize) {
		gctl_error(req, "Invalid oldsize: Out of range.");
		goto out;
	}
	if (oldsize == mediasize) {
		gctl_error(req, "Size hasn't changed.");
		goto out;
	}

	/* Read metadata from the 'oldsize' offset. */
	if (pread(provfd, sector, secsize, oldsize - secsize) != secsize) {
		gctl_error(req, "Cannot read old metadata: %s.",
		    strerror(errno));
		goto out;
	}

	/* Check if this sector contains geli metadata. */
	error = eli_metadata_decode(sector, &md);
	switch (error) {
	case 0:
		break;
	case EOPNOTSUPP:
		gctl_error(req,
		    "Provider's %s metadata version %u is too new.\n"
		    "geli: The highest supported version is %u.",
		    prov, (unsigned int)md.md_version, G_ELI_VERSION);
		goto out;
	case EINVAL:
		gctl_error(req, "Inconsistent provider's %s metadata.", prov);
		goto out;
	default:
		gctl_error(req,
		    "Unexpected error while decoding provider's %s metadata: %s.",
		    prov, strerror(error));
		goto out;
	}

	/*
	 * If the old metadata doesn't have a correct provider size, refuse
	 * to resize.
	 */
	if (md.md_provsize != (uint64_t)oldsize) {
		gctl_error(req, "Provider size mismatch at oldsize.");
		goto out;
	}

	/*
	 * Update the old metadata with the current provider size and write
	 * it back to the correct place on the provider.
	 */
	md.md_provsize = mediasize;
	/* Write metadata to the provider. */
	(void)eli_metadata_store(req, prov, &md);
	/* Now trash the old metadata. */
	(void)eli_trash_metadata(req, prov, provfd, oldsize - secsize);
out:
	if (provfd != -1)
		(void)g_close(provfd);
	if (sector != NULL) {
		explicit_bzero(sector, secsize);
		free(sector);
	}
}

static void
eli_version(struct gctl_req *req)
{
	struct g_eli_metadata md;
	const char *name;
	unsigned int version;
	int error, i, nargs;

	nargs = gctl_get_int(req, "nargs");

	if (nargs == 0) {
		unsigned int kernver;
		ssize_t size;

		size = sizeof(kernver);
		if (sysctlbyname("kern.geom.eli.version", &kernver, &size,
		    NULL, 0) == -1) {
			warn("Unable to obtain GELI kernel version");
		} else {
			printf("kernel: %u\n", kernver);
		}
		printf("userland: %u\n", G_ELI_VERSION);
		return;
	}

	for (i = 0; i < nargs; i++) {
		name = gctl_get_ascii(req, "arg%d", i);
		error = g_metadata_read(name, (unsigned char *)&md,
		    sizeof(md), G_ELI_MAGIC);
		if (error != 0) {
			warn("%s: Unable to read metadata: %s.", name,
			    strerror(error));
			gctl_error(req, "Not fully done.");
			continue;
		}
		version = le32dec(&md.md_version);
		printf("%s: %u\n", name, version);
	}
}

static void
eli_clear(struct gctl_req *req)
{
	const char *name;
	int error, i, nargs;

	nargs = gctl_get_int(req, "nargs");
	if (nargs < 1) {
		gctl_error(req, "Too few arguments.");
		return;
	}

	for (i = 0; i < nargs; i++) {
		name = gctl_get_ascii(req, "arg%d", i);
		error = g_metadata_clear(name, G_ELI_MAGIC);
		if (error != 0) {
			fprintf(stderr, "Cannot clear metadata on %s: %s.\n",
			    name, strerror(error));
			gctl_error(req, "Not fully done.");
			continue;
		}
		if (verbose)
			printf("Metadata cleared on %s.\n", name);
	}
}

static void
eli_dump(struct gctl_req *req)
{
	struct g_eli_metadata md;
	const char *name;
	int i, nargs;

	nargs = gctl_get_int(req, "nargs");
	if (nargs < 1) {
		gctl_error(req, "Too few arguments.");
		return;
	}

	for (i = 0; i < nargs; i++) {
		name = gctl_get_ascii(req, "arg%d", i);
		if (eli_metadata_read(NULL, name, &md) == -1) {
			gctl_error(req, "Not fully done.");
			continue;
		}
		printf("Metadata on %s:\n", name);
		eli_metadata_dump(&md);
		printf("\n");
	}
}
