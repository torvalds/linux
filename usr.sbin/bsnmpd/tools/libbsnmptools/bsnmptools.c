/*-
 * Copyright (c) 2005-2006 The FreeBSD Project
 * All rights reserved.
 *
 * Author: Shteryana Shopova <syrinx@FreeBSD.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
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
 *
 * Helper functions for snmp client tools
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <bsnmp/asn1.h>
#include <bsnmp/snmp.h>
#include <bsnmp/snmpclient.h>
#include "bsnmptc.h"
#include "bsnmptools.h"

/* Internal varibale to turn on library debugging for testing and to
 * find bugs. It is not exported via the header file.
 * XXX should we cover it by some #ifdef BSNMPTOOLS_DEBUG? */
int _bsnmptools_debug = 0;

/* Default files to import mapping from if none explicitly provided. */
#define	bsnmpd_defs		"/usr/share/snmp/defs/tree.def"
#define	mibII_defs		"/usr/share/snmp/defs/mibII_tree.def"

/*
 * The .iso.org.dod oid that has to be prepended to every OID when requesting
 * a value.
 */
const struct asn_oid IsoOrgDod_OID = {
	3, { 1, 3, 6 }
};


#define	SNMP_ERR_UNKNOWN	0

/*
 * An array of error strings corresponding to error definitions from libbsnmp.
 */
static const struct {
	const char *str;
	int32_t error;
} error_strings[] = {
	{ "Unknown", SNMP_ERR_UNKNOWN },
	{ "Too big ", SNMP_ERR_TOOBIG },
	{ "No such Name", SNMP_ERR_NOSUCHNAME },
	{ "Bad Value", SNMP_ERR_BADVALUE },
	{ "Readonly", SNMP_ERR_READONLY },
	{ "General error", SNMP_ERR_GENERR },
	{ "No access", SNMP_ERR_NO_ACCESS },
	{ "Wrong type", SNMP_ERR_WRONG_TYPE },
	{ "Wrong length", SNMP_ERR_WRONG_LENGTH },
	{ "Wrong encoding", SNMP_ERR_WRONG_ENCODING },
	{ "Wrong value", SNMP_ERR_WRONG_VALUE },
	{ "No creation", SNMP_ERR_NO_CREATION },
	{ "Inconsistent value", SNMP_ERR_INCONS_VALUE },
	{ "Resource unavailable", SNMP_ERR_RES_UNAVAIL },
	{ "Commit failed", SNMP_ERR_COMMIT_FAILED },
	{ "Undo failed", SNMP_ERR_UNDO_FAILED },
	{ "Authorization error", SNMP_ERR_AUTH_ERR },
	{ "Not writable", SNMP_ERR_NOT_WRITEABLE },
	{ "Inconsistent name", SNMP_ERR_INCONS_NAME },
	{ NULL, 0 }
};

/* This one and any following are exceptions. */
#define	SNMP_SYNTAX_UNKNOWN	SNMP_SYNTAX_NOSUCHOBJECT

static const struct {
	const char *str;
	enum snmp_syntax stx;
} syntax_strings[] = {
	{ "Null", SNMP_SYNTAX_NULL },
	{ "Integer", SNMP_SYNTAX_INTEGER },
	{ "OctetString", SNMP_SYNTAX_OCTETSTRING },
	{ "OID", SNMP_SYNTAX_OID },
	{ "IpAddress", SNMP_SYNTAX_IPADDRESS },
	{ "Counter32", SNMP_SYNTAX_COUNTER },
	{ "Gauge", SNMP_SYNTAX_GAUGE },
	{ "TimeTicks", SNMP_SYNTAX_TIMETICKS },
	{ "Counter64", SNMP_SYNTAX_COUNTER64 },
	{ "Unknown", SNMP_SYNTAX_UNKNOWN },
};

int
snmptool_init(struct snmp_toolinfo *snmptoolctx)
{
	char *str;
	size_t slen;

	memset(snmptoolctx, 0, sizeof(struct snmp_toolinfo));
	snmptoolctx->objects = 0;
	snmptoolctx->mappings = NULL;
	snmptoolctx->flags = SNMP_PDU_GET;	/* XXX */
	SLIST_INIT(&snmptoolctx->filelist);
	snmp_client_init(&snmp_client);
	SET_MAXREP(snmptoolctx, SNMP_MAX_REPETITIONS);

	if (add_filename(snmptoolctx, bsnmpd_defs, &IsoOrgDod_OID, 0) < 0)
		warnx("Error adding file %s to list", bsnmpd_defs);

	if (add_filename(snmptoolctx, mibII_defs, &IsoOrgDod_OID, 0) < 0)
		warnx("Error adding file %s to list", mibII_defs);

	/* Read the environment */
	if ((str = getenv("SNMPAUTH")) != NULL) {
		slen = strlen(str);
		if (slen == strlen("md5") && strcasecmp(str, "md5") == 0)
			snmp_client.user.auth_proto = SNMP_AUTH_HMAC_MD5;
		else if (slen == strlen("sha")&& strcasecmp(str, "sha") == 0)
			snmp_client.user.auth_proto = SNMP_AUTH_HMAC_SHA;
		else if (slen != 0)
			warnx("Bad authentication type - %s in SNMPAUTH", str);
	}

	if ((str = getenv("SNMPPRIV")) != NULL) {
		slen = strlen(str);
		if (slen == strlen("des") && strcasecmp(str, "des") == 0)
			snmp_client.user.priv_proto = SNMP_PRIV_DES;
		else if (slen == strlen("aes")&& strcasecmp(str, "aes") == 0)
			snmp_client.user.priv_proto = SNMP_PRIV_AES;
		else if (slen != 0)
			warnx("Bad privacy type - %s in SNMPPRIV", str);
	}

	if ((str = getenv("SNMPUSER")) != NULL) {
		if ((slen = strlen(str)) > sizeof(snmp_client.user.sec_name)) {
			warnx("Username too long - %s in SNMPUSER", str);
			return (-1);
		}
		if (slen > 0) {
			strlcpy(snmp_client.user.sec_name, str,
			    sizeof(snmp_client.user.sec_name));
			snmp_client.version = SNMP_V3;
		}
	}

	if ((str = getenv("SNMPPASSWD")) != NULL) {
		if ((slen = strlen(str)) > MAXSTR)
			slen = MAXSTR - 1;
		if ((snmptoolctx->passwd = malloc(slen + 1)) == NULL) {
			warn("malloc() failed");
			return (-1);
		}
		if (slen > 0)
			strlcpy(snmptoolctx->passwd, str, slen + 1);
	}

	return (0);
}

#define	OBJECT_IDX_LIST(o)	o->info->table_idx->index_list

/*
 * Walk through the file list and import string<->oid mappings from each file.
 */
int32_t
snmp_import_all(struct snmp_toolinfo *snmptoolctx)
{
	int32_t fc;
	struct fname *tmp;

	if (snmptoolctx == NULL)
		return (-1);

	if (ISSET_NUMERIC(snmptoolctx))
		return (0);

	if ((snmptoolctx->mappings = snmp_mapping_init()) == NULL)
		return (-1);

	fc = 0;
	if (SLIST_EMPTY(&snmptoolctx->filelist)) {
		warnx("No files to read OID <-> string conversions from");
		return (-1);
	} else {
		SLIST_FOREACH(tmp, &snmptoolctx->filelist, link) {
			if (tmp->done)
				continue;
			if (snmp_import_file(snmptoolctx, tmp) < 0) {
				fc = -1;
				break;
			}
			fc++;
		}
	}

	snmp_mapping_dump(snmptoolctx);
	return (fc);
}

/*
 * Add a filename to the file list - the initial idea of keeping a list with all
 * files to read OIDs from was that an application might want to have loaded in
 * memory the OIDs from a single file only and when done with them read the OIDs
 * from another file. This is not used yet but might be a good idea at some
 * point. Size argument is number of bytes in string including trailing '\0',
 * not string length.
 */
int32_t
add_filename(struct snmp_toolinfo *snmptoolctx, const char *filename,
    const struct asn_oid *cut, int32_t done)
{
	char *fstring;
	struct fname *entry;

	if (snmptoolctx == NULL)
		return (-1);

	/* Make sure file was not in list. */
	SLIST_FOREACH(entry, &snmptoolctx->filelist, link) {
		if (strncmp(entry->name, filename, strlen(entry->name)) == 0)
			return (0);
	}

	if ((fstring = strdup(filename)) == NULL) {
		warn("strdup() failed");
		return (-1);
	}

	if ((entry = calloc(1, sizeof(struct fname))) == NULL) {
		warn("calloc() failed");
		free(fstring);
		return (-1);
	}

	if (cut != NULL)
		asn_append_oid(&(entry->cut), cut);
	entry->name = fstring;
	entry->done = done;
	SLIST_INSERT_HEAD(&snmptoolctx->filelist, entry, link);

	return (1);
}

void
free_filelist(struct snmp_toolinfo *snmptoolctx)
{
	struct fname *f;

	if (snmptoolctx == NULL)
		return; /* XXX error handling */

	while ((f = SLIST_FIRST(&snmptoolctx->filelist)) != NULL) {
		SLIST_REMOVE_HEAD(&snmptoolctx->filelist, link);
		if (f->name)
			free(f->name);
		free(f);
	}
}

static char
isvalid_fchar(char c, int pos)
{
	if (isalpha(c)|| c == '/'|| c == '_' || c == '.' || c == '~' ||
	    (pos != 0 && isdigit(c))){
		return (c);
	}

	if (c == '\0')
		return (0);

	if (!isascii(c) || !isprint(c))
		warnx("Unexpected character %#2x", (u_int) c);
	else
		warnx("Illegal character '%c'", c);

	return (-1);
}

/*
 * Re-implement getsubopt from scratch, because the second argument is broken
 * and will not compile with WARNS=5.
 * Copied from src/contrib/bsnmp/snmpd/main.c.
 */
static int
getsubopt1(char **arg, const char *const *options, char **valp, char **optp)
{
	static const char *const delim = ",\t ";
	u_int i;
	char *ptr;

	*optp = NULL;

	/* Skip leading junk. */
	for (ptr = *arg; *ptr != '\0'; ptr++)
		if (strchr(delim, *ptr) == NULL)
			break;
	if (*ptr == '\0') {
		*arg = ptr;
		return (-1);
	}
	*optp = ptr;

	/* Find the end of the option. */
	while (*++ptr != '\0')
		if (strchr(delim, *ptr) != NULL || *ptr == '=')
			break;

	if (*ptr != '\0') {
		if (*ptr == '=') {
			*ptr++ = '\0';
			*valp = ptr;
			while (*ptr != '\0' && strchr(delim, *ptr) == NULL)
				ptr++;
			if (*ptr != '\0')
				*ptr++ = '\0';
		} else
			*ptr++ = '\0';
	}

	*arg = ptr;

	for (i = 0; *options != NULL; options++, i++)
		if (strcmp(*optp, *options) == 0)
			return (i);
	return (-1);
}

static int32_t
parse_path(char *value)
{
	int32_t i, len;

	if (value == NULL)
		return (-1);

	for (len = 0; len < MAXPATHLEN; len++) {
		i = isvalid_fchar(*(value + len), len) ;

		if (i == 0)
			break;
		else if (i < 0)
			return (-1);
	}

	if (len >= MAXPATHLEN || value[len] != '\0') {
		warnx("Bad pathname - '%s'", value);
		return (-1);
	}

	return (len);
}

static int32_t
parse_flist(struct snmp_toolinfo *snmptoolctx, char *value, char *path,
    const struct asn_oid *cut)
{
	int32_t namelen;
	char filename[MAXPATHLEN + 1];

	if (value == NULL)
		return (-1);

	do {
		memset(filename, 0, MAXPATHLEN + 1);

		if (isalpha(*value) && (path == NULL || path[0] == '\0')) {
			strlcpy(filename, SNMP_DEFS_DIR, MAXPATHLEN + 1);
			namelen = strlen(SNMP_DEFS_DIR);
		} else if (path != NULL){
			strlcpy(filename, path, MAXPATHLEN + 1);
			namelen = strlen(path);
		} else
			namelen = 0;

		for ( ; namelen < MAXPATHLEN; value++) {
			if (isvalid_fchar(*value, namelen) > 0) {
				filename[namelen++] = *value;
				continue;
			}

			if (*value == ',' )
				value++;
			else if (*value == '\0')
				;
			else {
				if (!isascii(*value) || !isprint(*value))
					warnx("Unexpected character %#2x in"
					    " filename", (u_int) *value);
				else
					warnx("Illegal character '%c' in"
					    " filename", *value);
				return (-1);
			}

			filename[namelen]='\0';
			break;
		}

		if ((namelen == MAXPATHLEN) && (filename[MAXPATHLEN] != '\0')) {
			warnx("Filename %s too long", filename);
			return (-1);
		}

		if (add_filename(snmptoolctx, filename, cut, 0) < 0) {
			warnx("Error adding file %s to list", filename);
			return (-1);
		}
	} while (*value != '\0');

	return(1);
}

static int32_t
parse_ascii(char *ascii, uint8_t *binstr, size_t binlen)
{
	char dptr[3];
	size_t count;
	int32_t alen, i, saved_errno;
	uint32_t val;

	/* Filter 0x at the beginning */
	if ((alen = strlen(ascii)) > 2 && ascii[0] == '0' && ascii[1] == 'x')
		i = 2;
	else
		i = 0;

	saved_errno = errno;
	errno = 0;
	for (count = 0; i < alen; i += 2) {
		/* XXX: consider strlen(ascii) % 2 != 0 */
		dptr[0] = ascii[i];
		dptr[1] = ascii[i + 1];
		dptr[2] = '\0';
		if ((val = strtoul(dptr, NULL, 16)) > 0xFF || errno != 0) {
			errno = saved_errno;
			return (-1);
		}
		binstr[count] = (uint8_t) val;
		if (++count >= binlen) {
			warnx("Key %s too long - truncating to %zu octets",
			    ascii, binlen);
			break;
		}
	}

	return (count);
}

/*
 * Functions to parse common input options for client tools and fill in the
 * snmp_client structure.
 */
int32_t
parse_authentication(struct snmp_toolinfo *snmptoolctx __unused, char *opt_arg)
{
	int32_t count, subopt;
	char *val, *option;
	const char *const subopts[] = {
		"proto",
		"key",
		NULL
	};

	assert(opt_arg != NULL);
	count = 1;
	while ((subopt = getsubopt1(&opt_arg, subopts, &val, &option)) != EOF) {
		switch (subopt) {
		case 0:
			if (val == NULL) {
				warnx("Suboption 'proto' requires an argument");
				return (-1);
			}
			if (strlen(val) != 3) {
				warnx("Unknown auth protocol - %s", val);
				return (-1);
			}
			if (strncasecmp("md5", val, strlen("md5")) == 0)
				snmp_client.user.auth_proto =
				    SNMP_AUTH_HMAC_MD5;
			else if (strncasecmp("sha", val, strlen("sha")) == 0)
				snmp_client.user.auth_proto =
				    SNMP_AUTH_HMAC_SHA;
			else {
				warnx("Unknown auth protocol - %s", val);
				return (-1);
			}
			break;
		case 1:
			if (val == NULL) {
				warnx("Suboption 'key' requires an argument");
				return (-1);
			}
			if (parse_ascii(val, snmp_client.user.auth_key,
			    SNMP_AUTH_KEY_SIZ) < 0) {
				warnx("Bad authentication key- %s", val);
				return (-1);
			}
			break;
		default:
			warnx("Unknown suboption - '%s'", suboptarg);
			return (-1);
		}
		count += 1;
	}
	return (2/* count */);
}

int32_t
parse_privacy(struct snmp_toolinfo *snmptoolctx __unused, char *opt_arg)
{
	int32_t count, subopt;
	char *val, *option;
	const char *const subopts[] = {
		"proto",
		"key",
		NULL
	};

	assert(opt_arg != NULL);
	count = 1;
	while ((subopt = getsubopt1(&opt_arg, subopts, &val, &option)) != EOF) {
		switch (subopt) {
		case 0:
			if (val == NULL) {
				warnx("Suboption 'proto' requires an argument");
				return (-1);
			}
			if (strlen(val) != 3) {
				warnx("Unknown privacy protocol - %s", val);
				return (-1);
			}
			if (strncasecmp("aes", val, strlen("aes")) == 0)
				snmp_client.user.priv_proto = SNMP_PRIV_AES;
			else if (strncasecmp("des", val, strlen("des")) == 0)
				snmp_client.user.priv_proto = SNMP_PRIV_DES;
			else {
				warnx("Unknown privacy protocol - %s", val);
				return (-1);
			}
			break;
		case 1:
			if (val == NULL) {
				warnx("Suboption 'key' requires an argument");
				return (-1);
			}
			if (parse_ascii(val, snmp_client.user.priv_key,
			    SNMP_PRIV_KEY_SIZ) < 0) {
				warnx("Bad privacy key- %s", val);
				return (-1);
			}
			break;
		default:
			warnx("Unknown suboption - '%s'", suboptarg);
			return (-1);
		}
		count += 1;
	}
	return (2/* count */);
}

int32_t
parse_context(struct snmp_toolinfo *snmptoolctx __unused, char *opt_arg)
{
	int32_t count, subopt;
	char *val, *option;
	const char *const subopts[] = {
		"context",
		"context-engine",
		NULL
	};

	assert(opt_arg != NULL);
	count = 1;
	while ((subopt = getsubopt1(&opt_arg, subopts, &val, &option)) != EOF) {
		switch (subopt) {
		case 0:
			if (val == NULL) {
				warnx("Suboption 'context' - no argument");
				return (-1);
			}
			strlcpy(snmp_client.cname, val, SNMP_CONTEXT_NAME_SIZ);
			break;
		case 1:
			if (val == NULL) {
				warnx("Suboption 'context-engine' - no argument");
				return (-1);
			}
			if ((int32_t)(snmp_client.clen = parse_ascii(val,
			    snmp_client.cengine, SNMP_ENGINE_ID_SIZ)) == -1) {
				warnx("Bad EngineID - %s", val);
				return (-1);
			}
			break;
		default:
			warnx("Unknown suboption - '%s'", suboptarg);
			return (-1);
		}
		count += 1;
	}
	return (2/* count */);
}

int32_t
parse_user_security(struct snmp_toolinfo *snmptoolctx __unused, char *opt_arg)
{
	int32_t count, subopt, saved_errno;
	char *val, *option;
	const char *const subopts[] = {
		"engine",
		"engine-boots",
		"engine-time",
		"name",
		NULL
	};

	assert(opt_arg != NULL);
	count = 1;
	while ((subopt = getsubopt1(&opt_arg, subopts, &val, &option)) != EOF) {
		switch (subopt) {
		case 0:
			if (val == NULL) {
				warnx("Suboption 'engine' - no argument");
				return (-1);
			}
			snmp_client.engine.engine_len = parse_ascii(val,
			    snmp_client.engine.engine_id, SNMP_ENGINE_ID_SIZ);
			if ((int32_t)snmp_client.engine.engine_len == -1) {
				warnx("Bad EngineID - %s", val);
				return (-1);
			}
			break;
		case 1:
			if (val == NULL) {
				warnx("Suboption 'engine-boots' - no argument");
				return (-1);
			}
			saved_errno = errno;
			errno = 0;
			snmp_client.engine.engine_boots = strtoul(val, NULL, 10);
			if (errno != 0) {
				warn("Bad 'engine-boots' value %s", val);
				errno = saved_errno;
				return (-1);
			}
			errno = saved_errno;
			break;
		case 2:
			if (val == NULL) {
				warnx("Suboption 'engine-time' - no argument");
				return (-1);
			}
			saved_errno = errno;
			errno = 0;
			snmp_client.engine.engine_time = strtoul(val, NULL, 10);
			if (errno != 0) {
				warn("Bad 'engine-time' value %s", val);
				errno = saved_errno;
				return (-1);
			}
			errno = saved_errno;
			break;
		case 3:
			strlcpy(snmp_client.user.sec_name, val,
			    SNMP_ADM_STR32_SIZ);
			break;
		default:
			warnx("Unknown suboption - '%s'", suboptarg);
			return (-1);
		}
		count += 1;
	}
	return (2/* count */);
}

int32_t
parse_file(struct snmp_toolinfo *snmptoolctx, char *opt_arg)
{
	assert(opt_arg != NULL);

	if (parse_flist(snmptoolctx, opt_arg, NULL, &IsoOrgDod_OID) < 0)
		return (-1);

	return (2);
}

int32_t
parse_include(struct snmp_toolinfo *snmptoolctx, char *opt_arg)
{
	char path[MAXPATHLEN + 1];
	int32_t cut_dflt, len, subopt;
	struct asn_oid cut;
	char *val, *option;
	const char *const subopts[] = {
		"cut",
		"path",
		"file",
		NULL
	};

#define	INC_CUT		0
#define	INC_PATH	1
#define	INC_LIST	2

	assert(opt_arg != NULL);

	/* if (opt == 'i')
		free_filelist(snmptoolctx, ); */
	/*
	 * This function should be called only after getopt(3) - otherwise if
	 * no previous validation of opt_arg strlen() may not return what is
	 * expected.
	 */

	path[0] = '\0';
	memset(&cut, 0, sizeof(struct asn_oid));
	cut_dflt = -1;

	while ((subopt = getsubopt1(&opt_arg, subopts, &val, &option)) != EOF) {
		switch (subopt) {
		    case INC_CUT:
			if (val == NULL) {
				warnx("Suboption 'cut' requires an argument");
				return (-1);
			} else {
				if (snmp_parse_numoid(val, &cut) < 0)
					return (-1);
			}
			cut_dflt = 1;
			break;

		    case INC_PATH:
			if ((len = parse_path(val)) < 0)
				return (-1);
			strlcpy(path, val, len + 1);
			break;

		    case INC_LIST:
			if (val == NULL)
				return (-1);
			if (cut_dflt == -1)
				len = parse_flist(snmptoolctx, val, path, &IsoOrgDod_OID);
			else
				len = parse_flist(snmptoolctx, val, path, &cut);
			if (len < 0)
				return (-1);
			break;

		    default:
			warnx("Unknown suboption - '%s'", suboptarg);
			return (-1);
		}
	}

	/* XXX: Fix me - returning two is wrong here */
	return (2);
}

int32_t
parse_server(char *opt_arg)
{
	assert(opt_arg != NULL);

	if (snmp_parse_server(&snmp_client, opt_arg) < 0)
		return (-1);

	if (snmp_client.trans > SNMP_TRANS_UDP && snmp_client.chost == NULL) {
		if ((snmp_client.chost = malloc(strlen(SNMP_DEFAULT_LOCAL) + 1))
		    == NULL) {
			syslog(LOG_ERR, "malloc() failed: %s", strerror(errno));
			return (-1);
		}
		strcpy(snmp_client.chost, SNMP_DEFAULT_LOCAL);
	}

	return (2);
}

int32_t
parse_timeout(char *opt_arg)
{
	int32_t v, saved_errno;

	assert(opt_arg != NULL);

	saved_errno = errno;
	errno = 0;

	v = strtol(opt_arg, NULL, 10);
	if (errno != 0) {
		warn("Error parsing timeout value");
		errno = saved_errno;
		return (-1);
	}

	snmp_client.timeout.tv_sec = v;
	errno = saved_errno;
	return (2);
}

int32_t
parse_retry(char *opt_arg)
{
	uint32_t v;
	int32_t saved_errno;

	assert(opt_arg != NULL);

	saved_errno = errno;
	errno = 0;

	v = strtoul(opt_arg, NULL, 10);
	if (errno != 0) {
		warn("Error parsing retries count");
		errno = saved_errno;
		return (-1);
	}

	snmp_client.retries = v;
	errno = saved_errno;
	return (2);
}

int32_t
parse_version(char *opt_arg)
{
	uint32_t v;
	int32_t saved_errno;

	assert(opt_arg != NULL);

	saved_errno = errno;
	errno = 0;

	v = strtoul(opt_arg, NULL, 10);
	if (errno != 0) {
		warn("Error parsing version");
		errno = saved_errno;
		return (-1);
	}

	switch (v) {
		case 1:
			snmp_client.version = SNMP_V1;
			break;
		case 2:
			snmp_client.version = SNMP_V2c;
			break;
		case 3:
			snmp_client.version = SNMP_V3;
			break;
		default:
			warnx("Unsupported SNMP version - %u", v);
			errno = saved_errno;
			return (-1);
	}

	errno = saved_errno;
	return (2);
}

int32_t
parse_local_path(char *opt_arg)
{
	assert(opt_arg != NULL);

	if (sizeof(opt_arg) > sizeof(SNMP_LOCAL_PATH)) {
		warnx("Filename too long - %s", opt_arg);
		return (-1);
	}

	strlcpy(snmp_client.local_path, opt_arg, sizeof(SNMP_LOCAL_PATH));
	return (2);
}

int32_t
parse_buflen(char *opt_arg)
{
	uint32_t size;
	int32_t saved_errno;

	assert(opt_arg != NULL);

	saved_errno = errno;
	errno = 0;

	size = strtoul(opt_arg, NULL, 10);
	if (errno != 0) {
		warn("Error parsing buffer size");
		errno = saved_errno;
		return (-1);
	}

	if (size > MAX_BUFF_SIZE) {
		warnx("Buffer size too big - %d max allowed", MAX_BUFF_SIZE);
		errno = saved_errno;
		return (-1);
	}

	snmp_client.txbuflen = snmp_client.rxbuflen = size;
	errno = saved_errno;
	return (2);
}

int32_t
parse_debug(void)
{
	snmp_client.dump_pdus = 1;
	return (1);
}

int32_t
parse_discovery(struct snmp_toolinfo *snmptoolctx)
{
	SET_EDISCOVER(snmptoolctx);
	snmp_client.version = SNMP_V3;
	return (1);
}

int32_t
parse_local_key(struct snmp_toolinfo *snmptoolctx)
{
	SET_LOCALKEY(snmptoolctx);
	snmp_client.version = SNMP_V3;
	return (1);
}

int32_t
parse_num_oids(struct snmp_toolinfo *snmptoolctx)
{
	SET_NUMERIC(snmptoolctx);
	return (1);
}

int32_t
parse_output(struct snmp_toolinfo *snmptoolctx, char *opt_arg)
{
	assert(opt_arg != NULL);

	if (strlen(opt_arg) > strlen("verbose")) {
		warnx( "Invalid output option - %s",opt_arg);
		return (-1);
	}

	if (strncasecmp(opt_arg, "short", strlen(opt_arg)) == 0)
		SET_OUTPUT(snmptoolctx, OUTPUT_SHORT);
	else if (strncasecmp(opt_arg, "verbose", strlen(opt_arg)) == 0)
		SET_OUTPUT(snmptoolctx, OUTPUT_VERBOSE);
	else if (strncasecmp(opt_arg,"tabular", strlen(opt_arg)) == 0)
		SET_OUTPUT(snmptoolctx, OUTPUT_TABULAR);
	else if (strncasecmp(opt_arg, "quiet", strlen(opt_arg)) == 0)
		SET_OUTPUT(snmptoolctx, OUTPUT_QUIET);
	else {
		warnx( "Invalid output option - %s", opt_arg);
		return (-1);
	}

	return (2);
}

int32_t
parse_errors(struct snmp_toolinfo *snmptoolctx)
{
	SET_RETRY(snmptoolctx);
	return (1);
}

int32_t
parse_skip_access(struct snmp_toolinfo *snmptoolctx)
{
	SET_ERRIGNORE(snmptoolctx);
	return (1);
}

char *
snmp_parse_suboid(char *str, struct asn_oid *oid)
{
	char *endptr;
	asn_subid_t suboid;

	if (*str == '.')
		str++;

	if (*str < '0' || *str > '9')
		return (str);

	do {
		suboid = strtoul(str, &endptr, 10);
		if ((asn_subid_t) suboid > ASN_MAXID) {
			warnx("Suboid %u > ASN_MAXID", suboid);
			return (NULL);
		}
		if (snmp_suboid_append(oid, suboid) < 0)
			return (NULL);
		str = endptr + 1;
	} while (*endptr == '.');

	return (endptr);
}

static char *
snmp_int2asn_oid(char *str, struct asn_oid *oid)
{
	char *endptr;
	int32_t v, saved_errno;

	saved_errno = errno;
	errno = 0;

	v = strtol(str, &endptr, 10);
	if (errno != 0) {
		warn("Integer value %s not supported", str);
		errno = saved_errno;
		return (NULL);
	}
	errno = saved_errno;

	if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
		return (NULL);

	return (endptr);
}

/* It is a bit weird to have a table indexed by OID but still... */
static char *
snmp_oid2asn_oid(struct snmp_toolinfo *snmptoolctx, char *str,
    struct asn_oid *oid)
{
	int32_t i;
	char string[MAXSTR + 1], *endptr;
	struct snmp_object obj;

	for (i = 0; i < MAXSTR; i++)
		if (isalpha (*(str + i)) == 0)
			break;

	endptr = str + i;
	memset(&obj, 0, sizeof(struct snmp_object));
	if (i == 0) {
		if ((endptr = snmp_parse_suboid(str, &(obj.val.var))) == NULL)
			return (NULL);
		if (snmp_suboid_append(oid, (asn_subid_t) obj.val.var.len) < 0)
			return (NULL);
	} else {
		strlcpy(string, str, i + 1);
		if (snmp_lookup_enumoid(snmptoolctx, &obj, string) < 0) {
			warnx("Unknown string - %s", string);
			return (NULL);
		}
	}

	asn_append_oid(oid, &(obj.val.var));
	return (endptr);
}

static char *
snmp_ip2asn_oid(char *str, struct asn_oid *oid)
{
	uint32_t v;
	int32_t i;
	char *endptr, *ptr;

	ptr = str;

	for (i = 0; i < 4; i++) {
		v = strtoul(ptr, &endptr, 10);
		if (v > 0xff)
			return (NULL);
		if (*endptr != '.' && strchr("],\0", *endptr) == NULL && i != 3)
			return (NULL);
		if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
			return (NULL);
		ptr = endptr + 1;
	}

	return (endptr);
}

/* 32-bit counter, gauge, timeticks. */
static char *
snmp_uint2asn_oid(char *str, struct asn_oid *oid)
{
	char *endptr;
	uint32_t v;
	int32_t saved_errno;

	saved_errno = errno;
	errno = 0;

	v = strtoul(str, &endptr, 10);
	if (errno != 0) {
		warn("Integer value %s not supported", str);
		errno = saved_errno;
		return (NULL);
	}
	errno = saved_errno;
	if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
		return (NULL);

	return (endptr);
}

static char *
snmp_cnt64_2asn_oid(char *str, struct asn_oid *oid)
{
	char *endptr;
	uint64_t v;
	int32_t saved_errno;

	saved_errno = errno;
	errno = 0;

	v = strtoull(str, &endptr, 10);

	if (errno != 0) {
		warn("Integer value %s not supported", str);
		errno = saved_errno;
		return (NULL);
	}
	errno = saved_errno;
	if (snmp_suboid_append(oid, (asn_subid_t) (v & 0xffffffff)) < 0)
		return (NULL);

	if (snmp_suboid_append(oid, (asn_subid_t) (v >> 32)) < 0)
		return (NULL);

	return (endptr);
}

enum snmp_syntax
parse_syntax(char *str)
{
	int32_t i;

	for (i = 0; i < SNMP_SYNTAX_UNKNOWN; i++) {
		if (strncmp(syntax_strings[i].str, str,
		    strlen(syntax_strings[i].str)) == 0)
			return (syntax_strings[i].stx);
	}

	return (SNMP_SYNTAX_NULL);
}

static char *
snmp_parse_subindex(struct snmp_toolinfo *snmptoolctx, char *str,
    struct index *idx, struct snmp_object *object)
{
	char *ptr;
	int32_t i;
	enum snmp_syntax stx;
	char syntax[MAX_CMD_SYNTAX_LEN];

	ptr = str;
	if (GET_OUTPUT(snmptoolctx) == OUTPUT_VERBOSE) {
		for (i = 0; i < MAX_CMD_SYNTAX_LEN ; i++) {
			if (*(ptr + i) == ':')
				break;
		}

		if (i >= MAX_CMD_SYNTAX_LEN) {
			warnx("Unknown syntax in OID - %s", str);
			return (NULL);
		}
		/* Expect a syntax string here. */
		if ((stx = parse_syntax(str)) <= SNMP_SYNTAX_NULL) {
			warnx("Invalid  syntax - %s",syntax);
			return (NULL);
		}

		if (stx != idx->syntax && !ISSET_ERRIGNORE(snmptoolctx)) {
			warnx("Syntax mismatch - %d expected, %d given",
			    idx->syntax, stx);
			return (NULL);
		}
		/*
		 * That is where the suboid started + the syntax length + one
		 * character for ':'.
		 */
		ptr = str + i + 1;
	} else
		stx = idx->syntax;

	switch (stx) {
		case SNMP_SYNTAX_INTEGER:
			return (snmp_int2asn_oid(ptr, &(object->val.var)));
		case SNMP_SYNTAX_OID:
			return (snmp_oid2asn_oid(snmptoolctx, ptr,
			    &(object->val.var)));
		case SNMP_SYNTAX_IPADDRESS:
			return (snmp_ip2asn_oid(ptr, &(object->val.var)));
		case SNMP_SYNTAX_COUNTER:
			/* FALLTHROUGH */
		case SNMP_SYNTAX_GAUGE:
			/* FALLTHROUGH */
		case SNMP_SYNTAX_TIMETICKS:
			return (snmp_uint2asn_oid(ptr, &(object->val.var)));
		case SNMP_SYNTAX_COUNTER64:
			return (snmp_cnt64_2asn_oid(ptr, &(object->val.var)));
		case SNMP_SYNTAX_OCTETSTRING:
			return (snmp_tc2oid(idx->tc, ptr, &(object->val.var)));
		default:
			/* NOTREACHED */
			break;
	}

	return (NULL);
}

char *
snmp_parse_index(struct snmp_toolinfo *snmptoolctx, char *str,
    struct snmp_object *object)
{
	char *ptr;
	struct index *temp;

	if (object->info->table_idx == NULL)
		return (NULL);

	ptr = NULL;
	STAILQ_FOREACH(temp, &(OBJECT_IDX_LIST(object)), link) {
		if ((ptr = snmp_parse_subindex(snmptoolctx, str, temp, object))
		    == NULL)
			return (NULL);

		if (*ptr != ',' && *ptr != ']')
			return (NULL);
		str = ptr + 1;
	}

	if (ptr == NULL || *ptr != ']') {
		warnx("Mismatching index - %s", str);
		return (NULL);
	}

	return (ptr + 1);
}

/*
 * Fill in the struct asn_oid member of snmp_value with suboids from input.
 * If an error occurs - print message on stderr and return (-1).
 * If all is ok - return the length of the oid.
 */
int32_t
snmp_parse_numoid(char *argv, struct asn_oid *var)
{
	char *endptr, *str;
	asn_subid_t suboid;

	str = argv;

	if (*str == '.')
		str++;

	do {
		if (var->len == ASN_MAXOIDLEN) {
			warnx("Oid too long - %u", var->len);
			return (-1);
		}

		suboid = strtoul(str, &endptr, 10);
		if (suboid > ASN_MAXID) {
			warnx("Oid too long - %u", var->len);
			return (-1);
		}

		var->subs[var->len++] = suboid;
		str = endptr + 1;
	} while ( *endptr == '.');

	if (*endptr != '\0') {
		warnx("Invalid oid string - %s", argv);
		return (-1);
	}

	return (var->len);
}

/* Append a length 1 suboid to an asn_oid structure. */
int32_t
snmp_suboid_append(struct asn_oid *var, asn_subid_t suboid)
{
	if (var == NULL)
		return (-1);

	if (var->len >= ASN_MAXOIDLEN) {
		warnx("Oid too long - %u", var->len);
		return (-1);
	}

	var->subs[var->len++] = suboid;

	return (1);
}

/* Pop the last suboid from an asn_oid structure. */
int32_t
snmp_suboid_pop(struct asn_oid *var)
{
	asn_subid_t suboid;

	if (var == NULL)
		return (-1);

	if (var->len < 1)
		return (-1);

	suboid = var->subs[--(var->len)];
	var->subs[var->len] = 0;

	return (suboid);
}

/*
 * Parse the command-line provided string into an OID - alocate memory for a new
 * snmp object, fill in its fields and insert it in the object list. A
 * (snmp_verify_inoid_f) function must be provided to validate the input string.
 */
int32_t
snmp_object_add(struct snmp_toolinfo *snmptoolctx, snmp_verify_inoid_f func,
    char *string)
{
	struct snmp_object *obj;

	if (snmptoolctx == NULL)
		return (-1);

	/* XXX-BZ does that chack make sense? */
	if (snmptoolctx->objects >= SNMP_MAX_BINDINGS) {
		warnx("Too many bindings in PDU - %u", snmptoolctx->objects + 1);
		return (-1);
	}

	if ((obj = calloc(1, sizeof(struct snmp_object))) == NULL) {
		syslog(LOG_ERR, "malloc() failed: %s", strerror(errno));
		return (-1);
	}

	if (func(snmptoolctx, obj, string) < 0) {
		warnx("Invalid OID - %s", string);
		free(obj);
		return (-1);
	}

	snmptoolctx->objects++;
	SLIST_INSERT_HEAD(&snmptoolctx->snmp_objectlist, obj, link);

	return (1);
}

/* Given an OID, find it in the object list and remove it. */
int32_t
snmp_object_remove(struct snmp_toolinfo *snmptoolctx, struct asn_oid *oid)
{
	struct snmp_object *temp;

	if (SLIST_EMPTY(&snmptoolctx->snmp_objectlist)) {
		warnx("Object list already empty");
		return (-1);
	}


	SLIST_FOREACH(temp, &snmptoolctx->snmp_objectlist, link)
		if (asn_compare_oid(&(temp->val.var), oid) == 0)
			break;

	if (temp == NULL) {
		warnx("No such object in list");
		return (-1);
	}

	SLIST_REMOVE(&snmptoolctx->snmp_objectlist, temp, snmp_object, link);
	if (temp->val.syntax == SNMP_SYNTAX_OCTETSTRING &&
	    temp->val.v.octetstring.octets != NULL)
		free(temp->val.v.octetstring.octets);
	free(temp);

	return (1);
}

static void
snmp_object_freeall(struct snmp_toolinfo *snmptoolctx)
{
	struct snmp_object *o;

	while ((o = SLIST_FIRST(&snmptoolctx->snmp_objectlist)) != NULL) {
		SLIST_REMOVE_HEAD(&snmptoolctx->snmp_objectlist, link);

		if (o->val.syntax == SNMP_SYNTAX_OCTETSTRING &&
		    o->val.v.octetstring.octets != NULL)
			free(o->val.v.octetstring.octets);
		free(o);
	}
}

/* Do all possible memory release before exit. */
void
snmp_tool_freeall(struct snmp_toolinfo *snmptoolctx)
{
	if (snmp_client.chost != NULL) {
		free(snmp_client.chost);
		snmp_client.chost = NULL;
	}

	if (snmp_client.cport != NULL) {
		free(snmp_client.cport);
		snmp_client.cport = NULL;
	}

	snmp_mapping_free(snmptoolctx);
	free_filelist(snmptoolctx);
	snmp_object_freeall(snmptoolctx);

	if (snmptoolctx->passwd != NULL) {
		free(snmptoolctx->passwd);
		snmptoolctx->passwd = NULL;
	}
}

/*
 * Fill all variables from the object list into a PDU. (snmp_verify_vbind_f)
 * function should check whether the variable is consistent in this PDU
 * (e.g do not add non-leaf OIDs to a GET PDU, or OIDs with read access only to
 * a SET PDU) - might be NULL though. (snmp_add_vbind_f) function is the
 * function actually adds the variable to the PDU and must not be NULL.
 */
int32_t
snmp_pdu_add_bindings(struct snmp_toolinfo *snmptoolctx,
    snmp_verify_vbind_f vfunc, snmp_add_vbind_f afunc,
    struct snmp_pdu *pdu, int32_t maxcount)
{
	int32_t nbindings, abind;
	struct snmp_object *obj;

	if (pdu == NULL || afunc == NULL)
		return (-1);

	/* Return 0 in case of no more work todo. */
	if (SLIST_EMPTY(&snmptoolctx->snmp_objectlist))
		return (0);

	if (maxcount < 0 || maxcount > SNMP_MAX_BINDINGS) {
		warnx("maxcount out of range: <0 || >SNMP_MAX_BINDINGS");
		return (-1);
	}

	nbindings = 0;
	SLIST_FOREACH(obj, &snmptoolctx->snmp_objectlist, link) {
		if ((vfunc != NULL) && (vfunc(snmptoolctx, pdu, obj) < 0)) {
			nbindings = -1;
			break;
		}
		if ((abind = afunc(pdu, obj)) < 0) {
			nbindings = -1;
			break;
		}

		if (abind > 0) {
			/* Do not put more varbindings than requested. */
			if (++nbindings >= maxcount)
				break;
		}
	}

	return (nbindings);
}

/*
 * Locate an object in the object list and set a corresponding error status.
 */
int32_t
snmp_object_seterror(struct snmp_toolinfo *snmptoolctx,
    struct snmp_value *err_value, int32_t error_status)
{
	struct snmp_object *obj;

	if (SLIST_EMPTY(&snmptoolctx->snmp_objectlist) || err_value == NULL)
		return (-1);

	SLIST_FOREACH(obj, &snmptoolctx->snmp_objectlist, link)
		if (asn_compare_oid(&(err_value->var), &(obj->val.var)) == 0) {
			obj->error = error_status;
			return (1);
		}

	return (0);
}

/*
 * Check a PDU received in response to a SNMP_PDU_GET/SNMP_PDU_GETBULK request
 * but don't compare syntaxes - when sending a request PDU they must be null.
 * This is a (almost) complete copy of snmp_pdu_check() - with matching syntaxes
 * checks and some other checks skipped.
 */
int32_t
snmp_parse_get_resp(struct snmp_pdu *resp, struct snmp_pdu *req)
{
	uint32_t i;

	for (i = 0; i < req->nbindings; i++) {
		if (asn_compare_oid(&req->bindings[i].var,
		    &resp->bindings[i].var) != 0) {
			warnx("Bad OID in response");
			return (-1);
		}

		if (snmp_client.version != SNMP_V1 && (resp->bindings[i].syntax
		    == SNMP_SYNTAX_NOSUCHOBJECT || resp->bindings[i].syntax ==
		    SNMP_SYNTAX_NOSUCHINSTANCE))
			return (0);
	}

	return (1);
}

int32_t
snmp_parse_getbulk_resp(struct snmp_pdu *resp, struct snmp_pdu *req)
{
	int32_t N, R, M, r;

	if (req->error_status > (int32_t) resp->nbindings) {
		warnx("Bad number of bindings in response");
		return (-1);
	}

	for (N = 0; N < req->error_status; N++) {
		if (asn_is_suboid(&req->bindings[N].var,
		    &resp->bindings[N].var) == 0)
			return (0);
		if (resp->bindings[N].syntax == SNMP_SYNTAX_ENDOFMIBVIEW)
			return (0);
	}

	for (R = N , r = N; R  < (int32_t) req->nbindings; R++) {
		for (M = 0; M < req->error_index && (r + M) <
		    (int32_t) resp->nbindings; M++) {
			if (asn_is_suboid(&req->bindings[R].var,
			    &resp->bindings[r + M].var) == 0)
				return (0);

			if (resp->bindings[r + M].syntax ==
			    SNMP_SYNTAX_ENDOFMIBVIEW) {
				M++;
				break;
			}
		}
		r += M;
	}

	return (0);
}

int32_t
snmp_parse_getnext_resp(struct snmp_pdu *resp, struct snmp_pdu *req)
{
	uint32_t i;

	for (i = 0; i < req->nbindings; i++) {
		if (asn_is_suboid(&req->bindings[i].var, &resp->bindings[i].var)
		    == 0)
			return (0);

		if (resp->version != SNMP_V1 && resp->bindings[i].syntax ==
		    SNMP_SYNTAX_ENDOFMIBVIEW)
			return (0);
	}

	return (1);
}

/*
 * Should be called to check a response to get/getnext/getbulk.
 */
int32_t
snmp_parse_resp(struct snmp_pdu *resp, struct snmp_pdu *req)
{
	if (resp == NULL || req == NULL)
		return (-2);

	if (resp->version != req->version) {
		warnx("Response has wrong version");
		return (-1);
	}

	if (resp->error_status == SNMP_ERR_NOSUCHNAME) {
		warnx("Error - No Such Name");
		return (0);
	}

	if (resp->error_status != SNMP_ERR_NOERROR) {
		warnx("Error %d in response", resp->error_status);
		return (-1);
	}

	if (resp->nbindings != req->nbindings && req->type != SNMP_PDU_GETBULK){
		warnx("Bad number of bindings in response");
		return (-1);
	}

	switch (req->type) {
		case SNMP_PDU_GET:
			return (snmp_parse_get_resp(resp,req));
		case SNMP_PDU_GETBULK:
			return (snmp_parse_getbulk_resp(resp,req));
		case SNMP_PDU_GETNEXT:
			return (snmp_parse_getnext_resp(resp,req));
		default:
			/* NOTREACHED */
			break;
	}

	return (-2);
}

static void
snmp_output_octetstring(struct snmp_toolinfo *snmptoolctx, enum snmp_tc tc,
    uint32_t len, uint8_t *octets)
{
	char *buf;

	if (len == 0 || octets == NULL)
		return;

	if (GET_OUTPUT(snmptoolctx) == OUTPUT_VERBOSE)
		fprintf(stdout, "%s : ",
		    syntax_strings[SNMP_SYNTAX_OCTETSTRING].str);

	if ((buf = snmp_oct2tc(tc, len, (char *) octets)) != NULL) {
		fprintf(stdout, "%s", buf);
		free(buf);
	}
}

static void
snmp_output_octetindex(struct snmp_toolinfo *snmptoolctx, enum snmp_tc tc,
    struct asn_oid *oid)
{
	uint32_t i;
	uint8_t *s;

	if ((s = malloc(oid->subs[0] + 1)) == NULL)
		syslog(LOG_ERR, "malloc failed - %s", strerror(errno));
	else {
		for (i = 0; i < oid->subs[0]; i++)
			s[i] = (u_char) (oid->subs[i + 1]);

		snmp_output_octetstring(snmptoolctx, tc, oid->subs[0], s);
		free(s);
	}
}

/*
 * Check and output syntax type and value.
 */
static void
snmp_output_oid_value(struct snmp_toolinfo *snmptoolctx, struct asn_oid *oid)
{
	char oid_string[ASN_OIDSTRLEN];
	struct snmp_object obj;

	if (GET_OUTPUT(snmptoolctx) == OUTPUT_VERBOSE)
		fprintf(stdout, "%s : ", syntax_strings[SNMP_SYNTAX_OID].str);

	if(!ISSET_NUMERIC(snmptoolctx)) {
		memset(&obj, 0, sizeof(struct snmp_object));
		asn_append_oid(&(obj.val.var), oid);

		if (snmp_lookup_enumstring(snmptoolctx, &obj) > 0)
			fprintf(stdout, "%s" , obj.info->string);
		else if (snmp_lookup_oidstring(snmptoolctx, &obj) > 0)
			fprintf(stdout, "%s" , obj.info->string);
		else if (snmp_lookup_nodestring(snmptoolctx, &obj) > 0)
			fprintf(stdout, "%s" , obj.info->string);
		else {
			(void) asn_oid2str_r(oid, oid_string);
			fprintf(stdout, "%s", oid_string);
		}
	} else {
		(void) asn_oid2str_r(oid, oid_string);
		fprintf(stdout, "%s", oid_string);
	}
}

static void
snmp_output_int(struct snmp_toolinfo *snmptoolctx, struct enum_pairs *enums,
    int32_t int_val)
{
	char *string;

	if (GET_OUTPUT(snmptoolctx) == OUTPUT_VERBOSE)
		fprintf(stdout, "%s : ",
		    syntax_strings[SNMP_SYNTAX_INTEGER].str);

	if (enums != NULL && (string = enum_string_lookup(enums, int_val))
	    != NULL)
		fprintf(stdout, "%s", string);
	else
		fprintf(stdout, "%d", int_val);
}

static void
snmp_output_ipaddress(struct snmp_toolinfo *snmptoolctx, uint8_t *ip)
{
	if (GET_OUTPUT(snmptoolctx) == OUTPUT_VERBOSE)
		fprintf(stdout, "%s : ",
		    syntax_strings[SNMP_SYNTAX_IPADDRESS].str);

	fprintf(stdout, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

static void
snmp_output_counter(struct snmp_toolinfo *snmptoolctx, uint32_t counter)
{
	if (GET_OUTPUT(snmptoolctx) == OUTPUT_VERBOSE)
		fprintf(stdout, "%s : ",
		    syntax_strings[SNMP_SYNTAX_COUNTER].str);

	fprintf(stdout, "%u", counter);
}

static void
snmp_output_gauge(struct snmp_toolinfo *snmptoolctx, uint32_t gauge)
{
	if (GET_OUTPUT(snmptoolctx) == OUTPUT_VERBOSE)
		fprintf(stdout, "%s : ", syntax_strings[SNMP_SYNTAX_GAUGE].str);

	fprintf(stdout, "%u", gauge);
}

static void
snmp_output_ticks(struct snmp_toolinfo *snmptoolctx, uint32_t ticks)
{
	if (GET_OUTPUT(snmptoolctx) == OUTPUT_VERBOSE)
		fprintf(stdout, "%s : ",
		    syntax_strings[SNMP_SYNTAX_TIMETICKS].str);

	fprintf(stdout, "%u", ticks);
}

static void
snmp_output_counter64(struct snmp_toolinfo *snmptoolctx, uint64_t counter64)
{
	if (GET_OUTPUT(snmptoolctx) == OUTPUT_VERBOSE)
		fprintf(stdout, "%s : ",
		    syntax_strings[SNMP_SYNTAX_COUNTER64].str);

	fprintf(stdout,"%ju", counter64);
}

int32_t
snmp_output_numval(struct snmp_toolinfo *snmptoolctx, struct snmp_value *val,
    struct snmp_oid2str *entry)
{
	if (val == NULL)
		return (-1);

	if (GET_OUTPUT(snmptoolctx) != OUTPUT_QUIET)
		fprintf(stdout, " = ");

	switch (val->syntax) {
	    case SNMP_SYNTAX_INTEGER:
		if (entry != NULL)
			snmp_output_int(snmptoolctx, entry->snmp_enum,
			    val->v.integer);
		else
			snmp_output_int(snmptoolctx, NULL, val->v.integer);
		break;

	    case SNMP_SYNTAX_OCTETSTRING:
		if (entry != NULL)
			snmp_output_octetstring(snmptoolctx, entry->tc,
			    val->v.octetstring.len, val->v.octetstring.octets);
		else
			snmp_output_octetstring(snmptoolctx, SNMP_STRING,
			    val->v.octetstring.len, val->v.octetstring.octets);
		break;

	    case SNMP_SYNTAX_OID:
		snmp_output_oid_value(snmptoolctx, &(val->v.oid));
		break;

	    case SNMP_SYNTAX_IPADDRESS:
		snmp_output_ipaddress(snmptoolctx, val->v.ipaddress);
		break;

	    case SNMP_SYNTAX_COUNTER:
		snmp_output_counter(snmptoolctx, val->v.uint32);
		break;

	    case SNMP_SYNTAX_GAUGE:
		snmp_output_gauge(snmptoolctx, val->v.uint32);
		break;

	    case SNMP_SYNTAX_TIMETICKS:
		snmp_output_ticks(snmptoolctx, val->v.uint32);
		break;

	    case SNMP_SYNTAX_COUNTER64:
		snmp_output_counter64(snmptoolctx, val->v.counter64);
		break;

	    case SNMP_SYNTAX_NOSUCHOBJECT:
		fprintf(stdout, "No Such Object\n");
		return (val->syntax);

	    case SNMP_SYNTAX_NOSUCHINSTANCE:
		fprintf(stdout, "No Such Instance\n");
		return (val->syntax);

	    case SNMP_SYNTAX_ENDOFMIBVIEW:
		fprintf(stdout, "End of Mib View\n");
		return (val->syntax);

	    case SNMP_SYNTAX_NULL:
		/* NOTREACHED */
		fprintf(stdout, "agent returned NULL Syntax\n");
		return (val->syntax);

	    default:
		/* NOTREACHED - If here - then all went completely wrong. */
		fprintf(stdout, "agent returned unknown syntax\n");
		return (-1);
	}

	fprintf(stdout, "\n");

	return (0);
}

static int32_t
snmp_fill_object(struct snmp_toolinfo *snmptoolctx, struct snmp_object *obj,
    struct snmp_value *val)
{
	int32_t rc;
	asn_subid_t suboid;

	if (obj == NULL || val == NULL)
		return (-1);

	if ((suboid = snmp_suboid_pop(&(val->var))) > ASN_MAXID)
		return (-1);

	memset(obj, 0, sizeof(struct snmp_object));
	asn_append_oid(&(obj->val.var), &(val->var));
	obj->val.syntax = val->syntax;

	if (obj->val.syntax > 0)
		rc = snmp_lookup_leafstring(snmptoolctx, obj);
	else
		rc = snmp_lookup_nonleaf_string(snmptoolctx, obj);

	(void) snmp_suboid_append(&(val->var), suboid);
	(void) snmp_suboid_append(&(obj->val.var), suboid);

	return (rc);
}

static int32_t
snmp_output_index(struct snmp_toolinfo *snmptoolctx, struct index *stx,
    struct asn_oid *oid)
{
	uint8_t ip[4];
	uint32_t bytes = 1;
	uint64_t cnt64;
	struct asn_oid temp, out;

	if (oid->len < bytes)
		return (-1);

	memset(&temp, 0, sizeof(struct asn_oid));
	asn_append_oid(&temp, oid);

	switch (stx->syntax) {
	    case SNMP_SYNTAX_INTEGER:
		snmp_output_int(snmptoolctx, stx->snmp_enum, temp.subs[0]);
		break;

	    case SNMP_SYNTAX_OCTETSTRING:
		if ((temp.subs[0] > temp.len -1 ) || (temp.subs[0] >
		    ASN_MAXOCTETSTRING))
			return (-1);
		snmp_output_octetindex(snmptoolctx, stx->tc, &temp);
		bytes += temp.subs[0];
		break;

	    case SNMP_SYNTAX_OID:
		if ((temp.subs[0] > temp.len -1) || (temp.subs[0] >
		    ASN_MAXOIDLEN))
			return (-1);

		bytes += temp.subs[0];
		memset(&out, 0, sizeof(struct asn_oid));
		asn_slice_oid(&out, &temp, 1, bytes);
		snmp_output_oid_value(snmptoolctx, &out);
		break;

	    case SNMP_SYNTAX_IPADDRESS:
		if (temp.len < 4)
			return (-1);
		for (bytes = 0; bytes < 4; bytes++)
			ip[bytes] = temp.subs[bytes];

		snmp_output_ipaddress(snmptoolctx, ip);
		bytes = 4;
		break;

	    case SNMP_SYNTAX_COUNTER:
		snmp_output_counter(snmptoolctx, temp.subs[0]);
		break;

	    case SNMP_SYNTAX_GAUGE:
		snmp_output_gauge(snmptoolctx, temp.subs[0]);
		break;

	    case SNMP_SYNTAX_TIMETICKS:
		snmp_output_ticks(snmptoolctx, temp.subs[0]);
		break;

	    case SNMP_SYNTAX_COUNTER64:
		if (oid->len < 2)
			return (-1);
		bytes = 2;
		memcpy(&cnt64, temp.subs, bytes);
		snmp_output_counter64(snmptoolctx, cnt64);
		break;

	    default:
		return (-1);
	}

	return (bytes);
}

static int32_t
snmp_output_object(struct snmp_toolinfo *snmptoolctx, struct snmp_object *o)
{
	int32_t i, first, len;
	struct asn_oid oid;
	struct index *temp;

	if (ISSET_NUMERIC(snmptoolctx))
		return (-1);

	if (o->info->table_idx == NULL) {
		fprintf(stdout,"%s.%d", o->info->string,
		    o->val.var.subs[o->val.var.len - 1]);
		return (1);
	}

	fprintf(stdout,"%s[", o->info->string);
	memset(&oid, 0, sizeof(struct asn_oid));

	len = 1;
	asn_slice_oid(&oid, &(o->val.var), (o->info->table_idx->var.len + len),
	    o->val.var.len);

	first = 1;
	STAILQ_FOREACH(temp, &(OBJECT_IDX_LIST(o)), link) {
		if(first)
			first = 0;
		else
			fprintf(stdout, ", ");
		if ((i = snmp_output_index(snmptoolctx, temp, &oid)) < 0)
			break;
		len += i;
		memset(&oid, 0, sizeof(struct asn_oid));
		asn_slice_oid(&oid, &(o->val.var),
		    (o->info->table_idx->var.len + len), o->val.var.len + 1);
	}

	fprintf(stdout,"]");
	return (1);
}

void
snmp_output_err_resp(struct snmp_toolinfo *snmptoolctx, struct snmp_pdu *pdu)
{
	struct snmp_object *object;
	char buf[ASN_OIDSTRLEN];

	if (pdu == NULL || (pdu->error_index > (int32_t) pdu->nbindings)) {
		fprintf(stdout, "Invalid error index in PDU\n");
		return;
	}

	if ((object = calloc(1, sizeof(struct snmp_object))) == NULL) {
		fprintf(stdout, "calloc: %s", strerror(errno));
		return;
	}

	fprintf(stdout, "Agent %s:%s returned error \n", snmp_client.chost,
	    snmp_client.cport);

	if (!ISSET_NUMERIC(snmptoolctx) && (snmp_fill_object(snmptoolctx, object,
	    &(pdu->bindings[pdu->error_index - 1])) > 0))
		snmp_output_object(snmptoolctx, object);
	else {
		asn_oid2str_r(&(pdu->bindings[pdu->error_index - 1].var), buf);
		fprintf(stdout,"%s", buf);
	}

	fprintf(stdout," caused error - ");
	if ((pdu->error_status > 0) && (pdu->error_status <=
	    SNMP_ERR_INCONS_NAME))
		fprintf(stdout, "%s\n", error_strings[pdu->error_status].str);
	else
		fprintf(stdout,"%s\n", error_strings[SNMP_ERR_UNKNOWN].str);

	free(object);
	object = NULL;
}

int32_t
snmp_output_resp(struct snmp_toolinfo *snmptoolctx, struct snmp_pdu *pdu,
    struct asn_oid *root)
{
	struct snmp_object *object;
	char p[ASN_OIDSTRLEN];
	int32_t error;
	uint32_t i;

	if ((object = calloc(1, sizeof(struct snmp_object))) == NULL)
		return (-1);

	i = error = 0;
	while (i < pdu->nbindings) {
		if (root != NULL && !(asn_is_suboid(root,
		    &(pdu->bindings[i].var))))
			break;

		if (GET_OUTPUT(snmptoolctx) != OUTPUT_QUIET) {
			if (!ISSET_NUMERIC(snmptoolctx) &&
			    (snmp_fill_object(snmptoolctx, object,
			    &(pdu->bindings[i])) > 0))
				snmp_output_object(snmptoolctx, object);
			else {
				asn_oid2str_r(&(pdu->bindings[i].var), p);
				fprintf(stdout, "%s", p);
			}
		}
		error |= snmp_output_numval(snmptoolctx, &(pdu->bindings[i]),
		    object->info);
		i++;
	}

	free(object);
	object = NULL;

	if (error)
		return (-1);

	return (i);
}

void
snmp_output_engine(void)
{
	uint32_t i;
	char *cptr, engine[2 * SNMP_ENGINE_ID_SIZ + 2];

	cptr = engine;
	for (i = 0; i < snmp_client.engine.engine_len; i++)
		cptr += sprintf(cptr, "%.2x", snmp_client.engine.engine_id[i]);
	*cptr++ = '\0';

	fprintf(stdout, "Engine ID 0x%s\n", engine);
	fprintf(stdout, "Boots : %u\t\tTime : %d\n",
	    snmp_client.engine.engine_boots,
	    snmp_client.engine.engine_time);
}

void
snmp_output_keys(void)
{
	uint32_t i, keylen = 0;
	char *cptr, extkey[2 * SNMP_AUTH_KEY_SIZ + 2];

	fprintf(stdout, "Localized keys for %s\n", snmp_client.user.sec_name);
	if (snmp_client.user.auth_proto == SNMP_AUTH_HMAC_MD5) {
		fprintf(stdout, "MD5 : 0x");
		keylen = SNMP_AUTH_HMACMD5_KEY_SIZ;
	} else if (snmp_client.user.auth_proto == SNMP_AUTH_HMAC_SHA) {
		fprintf(stdout, "SHA : 0x");
		keylen = SNMP_AUTH_HMACSHA_KEY_SIZ;
	}
	if (snmp_client.user.auth_proto != SNMP_AUTH_NOAUTH) {
		cptr = extkey;
		for (i = 0; i < keylen; i++)
			cptr += sprintf(cptr, "%.2x",
			    snmp_client.user.auth_key[i]);
		*cptr++ = '\0';
		fprintf(stdout, "%s\n", extkey);
	}

	if (snmp_client.user.priv_proto == SNMP_PRIV_DES) {
		fprintf(stdout, "DES : 0x");
		keylen = SNMP_PRIV_DES_KEY_SIZ;
	} else if (snmp_client.user.priv_proto == SNMP_PRIV_AES) {
		fprintf(stdout, "AES : 0x");
		keylen = SNMP_PRIV_AES_KEY_SIZ;
	}
	if (snmp_client.user.priv_proto != SNMP_PRIV_NOPRIV) {
		cptr = extkey;
		for (i = 0; i < keylen; i++)
			cptr += sprintf(cptr, "%.2x",
			    snmp_client.user.priv_key[i]);
		*cptr++ = '\0';
		fprintf(stdout, "%s\n", extkey);
	}
}
