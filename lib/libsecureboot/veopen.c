/*-
 * Copyright (c) 2017-2018, Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/queue.h>

#include "libsecureboot-priv.h"


struct fingerprint_info {
	char		*fi_prefix;	/**< manifest entries relative to */
	char		*fi_skip;	/**< manifest entries prefixed with  */
	const char 	*fi_data;	/**< manifest data */
	size_t		fi_prefix_len;	/**< length of prefix */
	size_t		fi_skip_len;	/**< length of skip */
	dev_t		fi_dev;		/**< device id  */
	LIST_ENTRY(fingerprint_info) entries;
};

static LIST_HEAD(, fingerprint_info) fi_list;

static void
fingerprint_info_init(void)
{
	static int once;

	if (once)
		return;
	LIST_INIT(&fi_list);
	once = 1;
}

/**
 * @brief
 * add manifest data to list
 *
 * list is kept sorted by longest prefix.
 *
 * @param[in] prefix
 *	path that all manifest entries are resolved via
 *
 * @param[in] skip
 *	optional prefix within manifest entries which should be skipped
 *
 * @param[in] data
 *	manifest data
 */
void
fingerprint_info_add(const char *filename, const char *prefix,
    const char *skip, const char *data, struct stat *stp)
{
	struct fingerprint_info *fip, *nfip, *lfip;
	char *cp;
	int n;

	fingerprint_info_init();
	nfip = malloc(sizeof(struct fingerprint_info));
	if (prefix) {
		nfip->fi_prefix = strdup(prefix);
	} else {
		if (!filename) {
			free(nfip);
			return;
		}
		nfip->fi_prefix = strdup(filename);
		cp = strrchr(nfip->fi_prefix, '/');
		if (cp)
			*cp = '\0';
		else {
			free(nfip->fi_prefix);
			free(nfip);
			return;
		}
	}
	/* collapse any trailing ..[/] */
	n = 0;
	while ((cp = strrchr(nfip->fi_prefix, '/')) != NULL) {
		if (cp[1] == '\0') {	/* trailing "/" */
			*cp = '\0';
			continue;
		}
		if (strcmp(&cp[1], "..") == 0) {
			n++;
			*cp = '\0';
			continue;
		}
		if (n > 0) {
			n--;
			*cp = '\0';
		}
		if (n == 0)
			break;
	}
#ifdef UNIT_TEST
	nfip->fi_dev = 0;
#else
	nfip->fi_dev = stp->st_dev;
#endif
	nfip->fi_data = data;
	nfip->fi_prefix_len = strlen(nfip->fi_prefix);
	if (skip) {
		nfip->fi_skip_len = strlen(skip);
		if (nfip->fi_skip_len)
			nfip->fi_skip = strdup(skip);
		else
			nfip->fi_skip = NULL;
	} else {
		nfip->fi_skip = NULL;
		nfip->fi_skip_len = 0;
	}

	if (LIST_EMPTY(&fi_list)) {
		LIST_INSERT_HEAD(&fi_list, nfip, entries);
		DEBUG_PRINTF(4, ("inserted %zu %s at head\n",
			nfip->fi_prefix_len, nfip->fi_prefix));
		return;
	}
	LIST_FOREACH(fip, &fi_list, entries) {
		if (nfip->fi_prefix_len >= fip->fi_prefix_len) {
			LIST_INSERT_BEFORE(fip, nfip, entries);
			DEBUG_PRINTF(4, ("inserted %zu %s before %zu %s\n",
				nfip->fi_prefix_len, nfip->fi_prefix,
				fip->fi_prefix_len, fip->fi_prefix));
			return;
		}
		lfip = fip;
	}
	LIST_INSERT_AFTER(lfip, nfip, entries);
	DEBUG_PRINTF(4, ("inserted %zu %s after %zu %s\n",
		nfip->fi_prefix_len, nfip->fi_prefix,
		lfip->fi_prefix_len, lfip->fi_prefix));
}

#ifdef MANIFEST_SKIP_MAYBE
/*
 * Deal with old incompatible boot/manifest
 * if fp[-1] is '/' and start of entry matches
 * MANIFEST_SKIP_MAYBE, we want it.
 */
static char *
maybe_skip(char *fp, struct fingerprint_info *fip, size_t *nplenp)
{
	char *tp;

	tp = fp - sizeof(MANIFEST_SKIP_MAYBE);

	if (tp >= fip->fi_data) {
		DEBUG_PRINTF(3, ("maybe: %.48s\n", tp));
		if ((tp == fip->fi_data || tp[-1] == '\n') &&
		    strncmp(tp, MANIFEST_SKIP_MAYBE,
			sizeof(MANIFEST_SKIP_MAYBE) - 1) == 0) {
			fp = tp;
			*nplenp += sizeof(MANIFEST_SKIP_MAYBE);
		}
	}
	return (fp);
}
#endif

char *
fingerprint_info_lookup(int fd, const char *path)
{
	char pbuf[MAXPATHLEN+1];
	char nbuf[MAXPATHLEN+1];
	struct stat st;
	struct fingerprint_info *fip;
	char *cp, *ep, *fp, *np;
	const char *prefix;
	size_t n, plen, nlen, nplen;
	dev_t dev = 0;

	fingerprint_info_init();

	n = strlcpy(pbuf, path, sizeof(pbuf));
	if (n >= sizeof(pbuf))
		return (NULL);
#ifndef UNIT_TEST
	if (fstat(fd, &st) == 0)
		dev = st.st_dev;
#endif
	/*
	 * get the first entry - it will have longest prefix
	 * so we can can work out how to initially split path
	 */
	fip = LIST_FIRST(&fi_list);
	if (!fip)
		return (NULL);
	prefix = pbuf;
	ep = NULL;
	cp = &pbuf[fip->fi_prefix_len];
	do {
		if (ep) {
			*ep = '/';
			cp -= 2;
			if (cp < pbuf)
				break;
		}
		nlen = plen = 0;	/* keep gcc quiet */
		if (cp > pbuf) {
			for ( ; cp >= pbuf && *cp != '/'; cp--)
				;	/* nothing */
			if (cp > pbuf) {
				ep = cp++;
				*ep = '\0';
			} else {
				cp = pbuf;
			}
			if (ep) {
				plen = ep - pbuf;
				nlen = n - plen - 1;
			}
		}
		if (cp == pbuf) {
			prefix = "/";
			plen = 1;
			if (*cp == '/') {
				nlen = n - 1;
				cp++;
			} else
				nlen = n;
			ep = NULL;
		}

		DEBUG_PRINTF(2, ("looking for %s %zu %s\n", prefix, plen, cp));

		LIST_FOREACH(fip, &fi_list, entries) {
			DEBUG_PRINTF(4, ("at %zu %s\n",
				fip->fi_prefix_len, fip->fi_prefix));

			if (fip->fi_prefix_len < plen) {
				DEBUG_PRINTF(3, ("skipping prefix=%s %zu %zu\n",
					fip->fi_prefix, fip->fi_prefix_len,
					plen));
				break;
			}
			if (fip->fi_prefix_len == plen) {
				if (fip->fi_dev != 0 && fip->fi_dev != dev) {
					DEBUG_PRINTF(3, (
						"skipping dev=%ld != %ld\n",
						(long)fip->fi_dev,
						(long)dev));
					continue;
				}
				if (strcmp(prefix, fip->fi_prefix)) {
					DEBUG_PRINTF(3, (
						"skipping prefix=%s\n",
						fip->fi_prefix));
					continue;
				}
				DEBUG_PRINTF(3, ("checking prefix=%s\n",
					fip->fi_prefix));
				if (fip->fi_skip_len) {
					np = nbuf;
					nplen = snprintf(nbuf, sizeof(nbuf),
					    "%s/%s",
					    fip->fi_skip, cp);
					nplen = MIN(nplen, sizeof(nbuf) - 1);
				} else {
					np = cp;
					nplen = nlen;
				}
				DEBUG_PRINTF(3, ("lookup: '%s'\n", np));
				if (!(fp = strstr(fip->fi_data, np)))
					continue;
#ifdef MANIFEST_SKIP_MAYBE
				if (fip->fi_skip_len == 0 &&
				    fp > fip->fi_data && fp[-1] == '/') {
					fp = maybe_skip(fp, fip, &nplen);
				}
#endif
				/*
				 * when we find a match:
				 * fp[nplen] will be space and
				 * fp will be fip->fi_data or
				 * fp[-1] will be \n
				 */
				if (!((fp == fip->fi_data || fp[-1] == '\n') &&
					fp[nplen] == ' ')) {
					do {
						fp++;
						fp = strstr(fp, np);
						if (fp) {
#ifdef MANIFEST_SKIP_MAYBE
							if (fip->fi_skip_len == 0 &&
							    fp > fip->fi_data &&
							    fp[-1] == '/') {
								fp = maybe_skip(fp, fip, &nplen);
							}
#endif
							DEBUG_PRINTF(3,
							    ("fp[-1]=%#x fp[%zu]=%#x fp=%.78s\n",
								fp[-1], nplen,
								fp[nplen],
								fp));
						}
					} while (fp != NULL &&
					    !(fp[-1] == '\n' &&
						fp[nplen] == ' '));
					if (!fp)
						continue;
				}
				DEBUG_PRINTF(2, ("found %.78s\n", fp));
				/* we have a match! */
				for (cp = &fp[nplen]; *cp == ' '; cp++)
					; /* nothing */
				return (cp);
			} else {
				DEBUG_PRINTF(3,
				    ("Ignoring prefix=%s\n", fip->fi_prefix));
			}
		}
	} while (cp > &pbuf[1]);

	return (NULL);
}

static int
verify_fingerprint(int fd, const char *path, const char *cp, off_t off)
{
	unsigned char buf[PAGE_SIZE];
	const br_hash_class *md;
	br_hash_compat_context mctx;
	size_t hlen;
	int n;

	if (strncmp(cp, "sha256=", 7) == 0) {
		md = &br_sha256_vtable;
		hlen = br_sha256_SIZE;
		cp += 7;
#ifdef VE_SHA1_SUPPORT
	} else if (strncmp(cp, "sha1=", 5) == 0) {
		md = &br_sha1_vtable;
		hlen = br_sha1_SIZE;
		cp += 5;
#endif
#ifdef VE_SHA384_SUPPORT
	} else if (strncmp(cp, "sha384=", 7) == 0) {
		md = &br_sha384_vtable;
		hlen = br_sha384_SIZE;
		cp += 7;
#endif
#ifdef VE_SHA512_SUPPORT
	} else if (strncmp(cp, "sha512=", 7) == 0) {
		md = &br_sha512_vtable;
		hlen = br_sha512_SIZE;
		cp += 7;
#endif
	} else {
		ve_error_set("%s: no supported fingerprint", path);
		return (VE_FINGERPRINT_UNKNOWN);
	}

	md->init(&mctx.vtable);
	if (off)
		lseek(fd, 0, SEEK_SET);
	do {
		n = read(fd, buf, sizeof(buf));
		if (n < 0)
			return (n);
		if (n > 0)
			md->update(&mctx.vtable, buf, n);
	} while (n > 0);
	lseek(fd, off, SEEK_SET);
	return (ve_check_hash(&mctx, md, path, cp, hlen));
}


/**
 * @brief
 * verify an open file
 *
 * @param[in] fd
 *	open descriptor
 *
 * @param[in] path
 *	pathname to open
 *
 * @param[in] off
 *	current offset
 *
 * @return 0, VE_FINGERPRINT_OK or VE_FINGERPRINT_NONE, VE_FINGERPRINT_WRONG
 */
int
verify_fd(int fd, const char *path, off_t off, struct stat *stp)
{
	struct stat st;
	char *cp;
	int rc;

	if (!stp) {
		if (fstat(fd, &st) == 0)
			stp = &st;
	}
	if (stp && !S_ISREG(stp->st_mode))
		return (0);		/* not relevant */
	cp = fingerprint_info_lookup(fd, path);
	if (!cp) {
		ve_error_set("%s: no entry", path);
		return (VE_FINGERPRINT_NONE);
	}
	rc = verify_fingerprint(fd, path, cp, off);
	switch (rc) {
	case VE_FINGERPRINT_OK:
	case VE_FINGERPRINT_UNKNOWN:
		return (rc);
	default:
		return (VE_FINGERPRINT_WRONG);
	}
}

/**
 * @brief
 * open a file if it can be verified
 *
 * @param[in] path
 *	pathname to open
 *
 * @param[in] flags
 *	flags for open
 *
 * @return fd or VE_FINGERPRINT_NONE, VE_FINGERPRINT_WRONG
 */
int
verify_open(const char *path, int flags)
{
	int fd;
	int rc;

	if ((fd = open(path, flags)) >= 0) {
		if ((rc = verify_fd(fd, path, 0, NULL)) < 0) {
			close(fd);
			fd = rc;
		}
	}
	return (fd);
}
