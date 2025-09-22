/* $OpenBSD: conf.c,v 1.108 2025/04/30 03:53:21 tb Exp $	 */
/* $EOM: conf.c,v 1.48 2000/12/04 02:04:29 angelos Exp $	 */

/*
 * Copyright (c) 1998, 1999, 2000, 2001 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 2000, 2001, 2002 Håkan Olsson.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "app.h"
#include "conf.h"
#include "log.h"
#include "monitor.h"
#include "util.h"

static char    *conf_get_trans_str(int, char *, char *);
static void     conf_load_defaults(int);
#if 0
static int      conf_find_trans_xf(int, char *);
#endif

struct conf_trans {
	TAILQ_ENTRY(conf_trans) link;
	int	 trans;
	enum conf_op {
		CONF_SET, CONF_REMOVE, CONF_REMOVE_SECTION
	}	 op;
	char	*section;
	char	*tag;
	char	*value;
	int	 override;
	int	 is_default;
};

#define CONF_SECT_MAX 256

TAILQ_HEAD(conf_trans_head, conf_trans) conf_trans_queue;

struct conf_binding {
	LIST_ENTRY(conf_binding) link;
	char	*section;
	char	*tag;
	char	*value;
	int	 is_default;
};

char	*conf_path = CONFIG_FILE;
LIST_HEAD(conf_bindings, conf_binding) conf_bindings[256];

static char	*conf_addr;
static __inline__ u_int8_t
conf_hash(char *s)
{
	u_int8_t hash = 0;

	while (*s) {
		hash = ((hash << 1) | (hash >> 7)) ^ tolower((unsigned char)*s);
		s++;
	}
	return hash;
}

/*
 * Insert a tag-value combination from LINE (the equal sign is at POS)
 */
static int
conf_remove_now(char *section, char *tag)
{
	struct conf_binding *cb, *next;

	for (cb = LIST_FIRST(&conf_bindings[conf_hash(section)]); cb;
	    cb = next) {
		next = LIST_NEXT(cb, link);
		if (strcasecmp(cb->section, section) == 0 &&
		    strcasecmp(cb->tag, tag) == 0) {
			LIST_REMOVE(cb, link);
			LOG_DBG((LOG_MISC, 95, "[%s]:%s->%s removed", section,
			    tag, cb->value));
			free(cb->section);
			free(cb->tag);
			free(cb->value);
			free(cb);
			return 0;
		}
	}
	return 1;
}

static int
conf_remove_section_now(char *section)
{
	struct conf_binding *cb, *next;
	int	unseen = 1;

	for (cb = LIST_FIRST(&conf_bindings[conf_hash(section)]); cb;
	    cb = next) {
		next = LIST_NEXT(cb, link);
		if (strcasecmp(cb->section, section) == 0) {
			unseen = 0;
			LIST_REMOVE(cb, link);
			LOG_DBG((LOG_MISC, 95, "[%s]:%s->%s removed", section,
			    cb->tag, cb->value));
			free(cb->section);
			free(cb->tag);
			free(cb->value);
			free(cb);
		}
	}
	return unseen;
}

/*
 * Insert a tag-value combination from LINE (the equal sign is at POS)
 * into SECTION of our configuration database.
 */
static int
conf_set_now(char *section, char *tag, char *value, int override,
    int is_default)
{
	struct conf_binding *node = 0;

	if (override)
		conf_remove_now(section, tag);
	else if (conf_get_str(section, tag)) {
		if (!is_default)
			log_print("conf_set_now: duplicate tag [%s]:%s, "
			    "ignoring...\n", section, tag);
		return 1;
	}
	node = calloc(1, sizeof *node);
	if (!node) {
		log_error("conf_set_now: calloc (1, %lu) failed",
		    (unsigned long)sizeof *node);
		return 1;
	}
	node->section = node->tag = node->value = NULL;
	if ((node->section = strdup(section)) == NULL)
		goto fail;
	if ((node->tag = strdup(tag)) == NULL)
		goto fail;
	if ((node->value = strdup(value)) == NULL)
		goto fail;
	node->is_default = is_default;

	LIST_INSERT_HEAD(&conf_bindings[conf_hash(section)], node, link);
	LOG_DBG((LOG_MISC, 95, "conf_set_now: [%s]:%s->%s", node->section,
	    node->tag, node->value));
	return 0;
fail:
	free(node->value);
	free(node->tag);
	free(node->section);
	free(node);
	return 1;
}

/*
 * Parse the line LINE of SZ bytes.  Skip Comments, recognize section
 * headers and feed tag-value pairs into our configuration database.
 */
static void
conf_parse_line(int trans, char *line, int ln, size_t sz)
{
	char	*val;
	size_t	 i;
	int	 j;
	static char *section = 0;

	/* Lines starting with '#' or ';' are comments.  */
	if (*line == '#' || *line == ';')
		return;

	/* '[section]' parsing...  */
	if (*line == '[') {
		for (i = 1; i < sz; i++)
			if (line[i] == ']')
				break;
		free(section);
		if (i == sz) {
			log_print("conf_parse_line: %d:"
			    "unmatched ']', ignoring until next section", ln);
			section = 0;
			return;
		}
		section = malloc(i);
		if (!section) {
			log_print("conf_parse_line: %d: malloc (%lu) failed",
			    ln, (unsigned long)i);
			return;
		}
		strlcpy(section, line + 1, i);
		return;
	}
	/* Deal with assignments.  */
	for (i = 0; i < sz; i++)
		if (line[i] == '=') {
			/* If no section, we are ignoring the lines.  */
			if (!section) {
				log_print("conf_parse_line: %d: ignoring line "
				    "due to no section", ln);
				return;
			}
			line[strcspn(line, " \t=")] = '\0';
			val = line + i + 1 + strspn(line + i + 1, " \t");
			/* Skip trailing whitespace, if any */
			for (j = sz - (val - line) - 1; j > 0 &&
			    isspace((unsigned char)val[j]); j--)
				val[j] = '\0';
			/* XXX Perhaps should we not ignore errors?  */
			conf_set(trans, section, line, val, 0, 0);
			return;
		}
	/* Other non-empty lines are weird.  */
	i = strspn(line, " \t");
	if (line[i])
		log_print("conf_parse_line: %d: syntax error", ln);
}

/* Parse the mapped configuration file.  */
static void
conf_parse(int trans, char *buf, size_t sz)
{
	char	*cp = buf;
	char	*bufend = buf + sz;
	char	*line;
	int	ln = 1;

	line = cp;
	while (cp < bufend) {
		if (*cp == '\n') {
			/* Check for escaped newlines.  */
			if (cp > buf && *(cp - 1) == '\\')
				*(cp - 1) = *cp = ' ';
			else {
				*cp = '\0';
				conf_parse_line(trans, line, ln, cp - line);
				line = cp + 1;
			}
			ln++;
		}
		cp++;
	}
	if (cp != line)
		log_print("conf_parse: last line unterminated, ignored.");
}

/*
 * Auto-generate default configuration values for the transforms and
 * suites the user wants.
 *
 * Resulting section names can be:
 *  For main mode:
 *     {BLF,3DES,CAST,AES,AES-{128,192,256}-{MD5,SHA,SHA2-{256,384,512}} \
 *         [-GRP{1,2,5,14-21,25-30}][-{DSS,RSA_SIG}]
 *  For quick mode:
 *     QM-{proto}[-TRP]-{cipher}[-{hash}][-PFS[-{group}]]-SUITE
 *     where
 *       {proto}  = ESP, AH
 *       {cipher} = 3DES, CAST, BLF, AES, AES-{128,192,256}, AESCTR
 *       {hash}   = MD5, SHA, RIPEMD, SHA2-{256,384,512}
 *       {group}  = GRP{1,2,5,14-21,25-30}
 *
 * DH group defaults to MODP_1024.
 *
 * XXX We may want to support USE_TRIPLEDES, etc...
 * XXX No EC2N DH support here yet.
 */

/* Find the value for a section+tag in the transaction list.  */
static char *
conf_get_trans_str(int trans, char *section, char *tag)
{
	struct conf_trans *node, *nf = 0;

	for (node = TAILQ_FIRST(&conf_trans_queue); node;
	    node = TAILQ_NEXT(node, link))
		if (node->trans == trans && strcasecmp(section, node->section)
		    == 0 && strcasecmp(tag, node->tag) == 0) {
			if (!nf)
				nf = node;
			else if (node->override)
				nf = node;
		}
	return nf ? nf->value : 0;
}

#if 0
/* XXX Currently unused.  */
static int
conf_find_trans_xf(int phase, char *xf)
{
	struct conf_trans *node;
	char	*p;

	/* Find the relevant transforms and suites, if any.  */
	for (node = TAILQ_FIRST(&conf_trans_queue); node;
	    node = TAILQ_NEXT(node, link))
		if ((phase == 1 && strcmp("Transforms", node->tag) == 0) ||
		    (phase == 2 && strcmp("Suites", node->tag) == 0)) {
			p = node->value;
			while ((p = strstr(p, xf)) != NULL)
				if (*(p + strlen(p)) &&
				    *(p + strlen(p)) != ',')
					p += strlen(p);
				else
					return 1;
		}
	return 0;
}
#endif

static void
conf_load_defaults_mm(int tr, char *mme, char *mmh, char *mma, char *dhg,
    char *mme_p, char *mma_p, char *dhg_p, char *mmh_p)
{
	char sect[CONF_SECT_MAX];

	snprintf(sect, sizeof sect, "%s%s%s%s", mme_p, mmh_p, dhg_p, mma_p);

	LOG_DBG((LOG_MISC, 95, "conf_load_defaults_mm: main mode %s", sect));

	conf_set(tr, sect, "ENCRYPTION_ALGORITHM", mme, 0, 1);
	if (strcmp(mme, "BLOWFISH_CBC") == 0)
		conf_set(tr, sect, "KEY_LENGTH", CONF_DFLT_VAL_BLF_KEYLEN, 0,
		    1);
        else if (strcmp(mme_p, "AES-128") == 0)
                conf_set(tr, sect, "KEY_LENGTH", "128,128:128", 0, 1);
        else if (strcmp(mme_p, "AES-192") == 0)
                conf_set(tr, sect, "KEY_LENGTH", "192,192:192", 0, 1);
        else if (strcmp(mme_p, "AES-256") == 0)
                conf_set(tr, sect, "KEY_LENGTH", "256,256:256", 0, 1);
	else if (strcmp(mme, "AES_CBC") == 0)
		conf_set(tr, sect, "KEY_LENGTH", CONF_DFLT_VAL_AES_KEYLEN, 0,
		    1);

	conf_set(tr, sect, "HASH_ALGORITHM", mmh, 0, 1);
	conf_set(tr, sect, "AUTHENTICATION_METHOD", mma, 0, 1);
	conf_set(tr, sect, "GROUP_DESCRIPTION", dhg, 0, 1);
	conf_set(tr, sect, "Life", CONF_DFLT_TAG_LIFE_MAIN_MODE, 0, 1);
}

static void
conf_load_defaults_qm(int tr, char *qme, char *qmh, char *dhg, char *qme_p,
    char *qmh_p, char *qm_ah_id, char *dhg_p, int proto, int mode, int pfs)
{
	char sect[CONF_SECT_MAX], tmp[CONF_SECT_MAX];

	/* Helper #defines, incl abbreviations.  */
#define PROTO(x)  ((x) ? "AH" : "ESP")
#define PFS(x)    ((x) ? "-PFS" : "")
#define MODE(x)   ((x) ? "TRANSPORT" : "TUNNEL")
#define MODE_p(x) ((x) ? "-TRP" : "")

	/* For AH a hash must be present and no encryption is allowed */
	if (proto == 1 && (strcmp(qmh, "NONE") == 0 ||
	    strcmp(qme, "NONE") != 0))
		return;

	/* For ESP encryption must be provided, an empty hash is ok. */
	if (proto == 0 && strcmp(qme, "NONE") == 0)
		return;

	/* When PFS is disabled no DH group must be specified. */
	if (pfs == 0 && strcmp(dhg_p, ""))
		return;

	/* For GCM no additional authentication must be specified */
	if (proto == 0 && strcmp(qmh, "NONE") != 0 &&
	    (strcmp(qme, "AES_GCM_16") == 0 || strcmp(qme, "AES_GMAC") == 0))
		return;

	snprintf(tmp, sizeof tmp, "QM-%s%s%s%s%s%s", PROTO(proto),
	    MODE_p(mode), qme_p, qmh_p, PFS(pfs), dhg_p);

	strlcpy(sect, tmp, CONF_SECT_MAX);
	strlcat(sect, "-SUITE",	CONF_SECT_MAX);

	LOG_DBG((LOG_MISC, 95, "conf_load_defaults_qm: quick mode %s", sect));

	conf_set(tr, sect, "Protocols", tmp, 0, 1);
	snprintf(sect, sizeof sect, "IPSEC_%s", PROTO(proto));
	conf_set(tr, tmp, "PROTOCOL_ID", sect, 0, 1);
	strlcpy(sect, tmp, CONF_SECT_MAX);
	strlcat(sect, "-XF", CONF_SECT_MAX);
	conf_set(tr, tmp, "Transforms", sect, 0, 1);

	/*
	 * XXX For now, defaults
	 * contain one xf per protocol.
	 */
	if (proto == 0)
		conf_set(tr, sect, "TRANSFORM_ID", qme, 0, 1);
	else
		conf_set(tr, sect, "TRANSFORM_ID", qm_ah_id, 0, 1);
	if (strcmp(qme ,"BLOWFISH") == 0)
		conf_set(tr, sect, "KEY_LENGTH", CONF_DFLT_VAL_BLF_KEYLEN, 0,
			 1);
	else if (strcmp(qme_p, "-AES-128") == 0 ||
	    strcmp(qme_p, "-AESCTR-128") == 0 ||
	    strcmp(qme_p, "-AESGCM-128") == 0 ||
	    strcmp(qme_p, "-AESGMAC-128") == 0)
		conf_set(tr, sect, "KEY_LENGTH", "128,128:128", 0, 1);
	else if (strcmp(qme_p, "-AES-192") == 0 ||
	    strcmp(qme_p, "-AESCTR-192") == 0 ||
	    strcmp(qme_p, "-AESGCM-192") == 0 ||
	    strcmp(qme_p, "-AESGMAC-192") == 0)
		conf_set(tr, sect, "KEY_LENGTH", "192,192:192", 0, 1);
	else if (strcmp(qme_p, "-AES-256") == 0 ||
	    strcmp(qme_p, "-AESCTR-256") == 0 ||
	    strcmp(qme_p, "-AESGCM-256") == 0 ||
	    strcmp(qme_p, "-AESGMAC-256") == 0)
		conf_set(tr, sect, "KEY_LENGTH", "256,256:256", 0, 1);
	else if	(strcmp(qme, "AES") == 0)
		conf_set(tr, sect, "KEY_LENGTH", CONF_DFLT_VAL_AES_KEYLEN, 0,
			 1);

	conf_set(tr, sect, "ENCAPSULATION_MODE", MODE(mode), 0, 1);
	if (strcmp(qmh, "NONE")) {
		conf_set(tr, sect, "AUTHENTICATION_ALGORITHM", qmh, 0, 1);

		/* XXX Another shortcut to keep length down */
		if (pfs)
			conf_set(tr, sect, "GROUP_DESCRIPTION", dhg, 0, 1);
	}

	/* XXX Lifetimes depending on enc/auth strength? */
	conf_set(tr, sect, "Life", CONF_DFLT_TAG_LIFE_QUICK_MODE, 0, 1);
}

static void
conf_load_defaults(int tr)
{
	int	 enc, auth, hash, group, proto, mode, pfs;
	char	*dflt;

	char	*mm_auth[] = {"PRE_SHARED", "DSS", "RSA_SIG", 0};
	char	*mm_auth_p[] = {"", "-DSS", "-RSA_SIG", 0};
	char	*mm_hash[] = {"MD5", "SHA", "SHA2_256", "SHA2_384", "SHA2_512",
		     0};
	char	*mm_hash_p[] = {"-MD5", "-SHA", "-SHA2-256", "-SHA2-384",
		    "-SHA2-512", "", 0 };
	char	*mm_enc[] = {"BLOWFISH_CBC", "3DES_CBC", "CAST_CBC",
		    "AES_CBC", "AES_CBC", "AES_CBC", "AES_CBC", 0};
	char	*mm_enc_p[] = {"BLF", "3DES", "CAST", "AES", "AES-128",
		    "AES-192", "AES-256", 0};
	char	*dhgroup[] = {"MODP_1024", "MODP_768", "MODP_1024",
		    "MODP_1536", "MODP_2048", "MODP_3072", "MODP_4096",
		    "MODP_6144", "MODP_8192",
		    "ECP_256", "ECP_384", "ECP_521", "ECP_224",
		    "BP_224", "BP_256", "BP_384", "BP_512", 0};
	char	*dhgroup_p[] = {"", "-GRP1", "-GRP2", "-GRP5", "-GRP14",
		    "-GRP15", "-GRP16", "-GRP17", "-GRP18", "-GRP19", "-GRP20",
		    "-GRP21", "-GRP26", "-GRP27", "-GRP28", "-GRP29",
		    "-GRP30", 0};
	char	*qm_enc[] = {"3DES", "CAST", "BLOWFISH", "AES",
		    "AES", "AES", "AES", "AES_CTR", "AES_CTR", "AES_CTR",
		    "AES_CTR", "AES_GCM_16",
		    "AES_GCM_16", "AES_GCM_16", "AES_GMAC", "AES_GMAC",
		    "AES_GMAC", "NULL", "NONE", 0};
	char	*qm_enc_p[] = {"-3DES", "-CAST", "-BLF", "-AES",
		    "-AES-128", "-AES-192", "-AES-256", "-AESCTR",
		    "-AESCTR-128", "-AESCTR-192", "-AESCTR-256",
		    "-AESGCM-128", "-AESGCM-192", "-AESGCM-256",
		    "-AESGMAC-128", "-AESGMAC-192", "-AESGMAC-256", "-NULL",
		    "", 0};
	char	*qm_hash[] = {"HMAC_MD5", "HMAC_SHA", "HMAC_RIPEMD",
		    "HMAC_SHA2_256", "HMAC_SHA2_384", "HMAC_SHA2_512", "NONE",
		    0};
	char	*qm_hash_p[] = {"-MD5", "-SHA", "-RIPEMD", "-SHA2-256",
		    "-SHA2-384", "-SHA2-512", "", 0};
	char	*qm_ah_id[] = {"MD5", "SHA", "RIPEMD", "SHA2_256", "SHA2_384",
		    "SHA2_512", "", 0};

	/* General and X509 defaults */
	conf_set(tr, "General", "Retransmits", CONF_DFLT_RETRANSMITS, 0, 1);
	conf_set(tr, "General", "Exchange-max-time", CONF_DFLT_EXCH_MAX_TIME,
	    0, 1);
	conf_set(tr, "General", "Use-Keynote", CONF_DFLT_USE_KEYNOTE, 0, 1);
	conf_set(tr, "General", "Policy-file", CONF_DFLT_POLICY_FILE, 0, 1);
	conf_set(tr, "General", "Pubkey-directory", CONF_DFLT_PUBKEY_DIR, 0,
	    1);

	conf_set(tr, "X509-certificates", "CA-directory",
	    CONF_DFLT_X509_CA_DIR, 0, 1);
	conf_set(tr, "X509-certificates", "Cert-directory",
	    CONF_DFLT_X509_CERT_DIR, 0, 1);
	conf_set(tr, "X509-certificates", "Private-key",
	    CONF_DFLT_X509_PRIVATE_KEY, 0, 1);
	conf_set(tr, "X509-certificates", "Private-key-directory",
	    CONF_DFLT_X509_PRIVATE_KEY_DIR, 0, 1);
	conf_set(tr, "X509-certificates", "CRL-directory",
	    CONF_DFLT_X509_CRL_DIR, 0, 1);

	conf_set(tr, "KeyNote", "Credential-directory",
	    CONF_DFLT_KEYNOTE_CRED_DIR, 0, 1);

	conf_set(tr, "General", "Delete-SAs", CONF_DFLT_DELETE_SAS, 0, 1);

	/* Lifetimes. XXX p1/p2 vs main/quick mode may be unclear.  */
	dflt = conf_get_trans_str(tr, "General", "Default-phase-1-lifetime");
	conf_set(tr, CONF_DFLT_TAG_LIFE_MAIN_MODE, "LIFE_TYPE",
	    CONF_DFLT_TYPE_LIFE_MAIN_MODE, 0, 1);
	conf_set(tr, CONF_DFLT_TAG_LIFE_MAIN_MODE, "LIFE_DURATION",
	    (dflt ? dflt : CONF_DFLT_VAL_LIFE_MAIN_MODE), 0, 1);

	dflt = conf_get_trans_str(tr, "General", "Default-phase-2-lifetime");
	conf_set(tr, CONF_DFLT_TAG_LIFE_QUICK_MODE, "LIFE_TYPE",
	    CONF_DFLT_TYPE_LIFE_QUICK_MODE, 0, 1);
	conf_set(tr, CONF_DFLT_TAG_LIFE_QUICK_MODE, "LIFE_DURATION",
	    (dflt ? dflt : CONF_DFLT_VAL_LIFE_QUICK_MODE), 0, 1);

	/* Default Phase-1 Configuration section */
	conf_set(tr, CONF_DFLT_TAG_PHASE1_CONFIG, "EXCHANGE_TYPE",
	    CONF_DFLT_PHASE1_EXCH_TYPE, 0, 1);
	conf_set(tr, CONF_DFLT_TAG_PHASE1_CONFIG, "Transforms",
	    CONF_DFLT_PHASE1_TRANSFORMS, 0, 1);

	/* Main modes */
	for (enc = 0; mm_enc[enc]; enc++)
		for (hash = 0; mm_hash[hash]; hash++)
			for (auth = 0; mm_auth[auth]; auth++)
				for (group = 0; dhgroup_p[group]; group++)
					conf_load_defaults_mm (tr, mm_enc[enc],
					    mm_hash[hash], mm_auth[auth],
					    dhgroup[group], mm_enc_p[enc],
					    mm_auth_p[auth], dhgroup_p[group],
					    mm_hash_p[hash]);

	/* Setup a default Phase 1 entry */
	conf_set(tr, "Phase 1", "Default", "Default-phase-1", 0, 1);
	conf_set(tr, "Default-phase-1", "Phase", "1", 0, 1);
	conf_set(tr, "Default-phase-1", "Configuration",
	    "Default-phase-1-configuration", 0, 1);
	dflt = conf_get_trans_str(tr, "General", "Default-phase-1-ID");
	if (dflt)
		conf_set(tr, "Default-phase-1", "ID", dflt, 0, 1);

	/* Quick modes */
	for (enc = 0; qm_enc[enc]; enc++)
		for (proto = 0; proto < 2; proto++)
			for (mode = 0; mode < 2; mode++)
				for (pfs = 0; pfs < 2; pfs++)
					for (hash = 0; qm_hash[hash]; hash++)
						for (group = 0;
						    dhgroup_p[group]; group++)
							conf_load_defaults_qm(
							    tr, qm_enc[enc],
							    qm_hash[hash],
							    dhgroup[group],
							    qm_enc_p[enc],
							    qm_hash_p[hash],
							    qm_ah_id[hash],
							    dhgroup_p[group],
							    proto, mode, pfs);
}

void
conf_init(void)
{
	unsigned int i;

	for (i = 0; i < sizeof conf_bindings / sizeof conf_bindings[0]; i++)
		LIST_INIT(&conf_bindings[i]);
	TAILQ_INIT(&conf_trans_queue);
	conf_reinit();
}

/* Open the config file and map it into our address space, then parse it.  */
void
conf_reinit(void)
{
	struct conf_binding *cb = 0;
	int	 fd, trans;
	unsigned int i;
	size_t	 sz;
	char	*new_conf_addr = 0;

	fd = monitor_open(conf_path, O_RDONLY, 0);
	if (fd == -1 || check_file_secrecy_fd(fd, conf_path, &sz) == -1) {
		if (fd == -1 && errno != ENOENT)
			log_error("conf_reinit: open(\"%s\", O_RDONLY, 0) "
			    "failed", conf_path);
		if (fd != -1)
			close(fd);

		trans = conf_begin();
	} else {
		new_conf_addr = malloc(sz);
		if (!new_conf_addr) {
			log_error("conf_reinit: malloc (%lu) failed",
			    (unsigned long)sz);
			goto fail;
		}
		/* XXX I assume short reads won't happen here.  */
		if (read(fd, new_conf_addr, sz) != (int)sz) {
			log_error("conf_reinit: read (%d, %p, %lu) failed",
			    fd, new_conf_addr, (unsigned long)sz);
			goto fail;
		}
		close(fd);

		trans = conf_begin();

		/* XXX Should we not care about errors and rollback?  */
		conf_parse(trans, new_conf_addr, sz);
	}

	/* Load default configuration values.  */
	conf_load_defaults(trans);

	/* Free potential existing configuration.  */
	if (conf_addr) {
		for (i = 0; i < sizeof conf_bindings / sizeof conf_bindings[0];
		    i++)
			for (cb = LIST_FIRST(&conf_bindings[i]); cb;
			    cb = LIST_FIRST(&conf_bindings[i]))
				conf_remove_now(cb->section, cb->tag);
		free(conf_addr);
	}
	conf_end(trans, 1);
	conf_addr = new_conf_addr;
	return;

fail:
	free(new_conf_addr);
	close(fd);
}

/*
 * Return the numeric value denoted by TAG in section SECTION or DEF
 * if that tag does not exist.
 */
int
conf_get_num(char *section, char *tag, int def)
{
	char	*value = conf_get_str(section, tag);

	if (value)
		return atoi(value);
	return def;
}

/*
 * Return the socket endpoint address denoted by TAG in SECTION as a
 * struct sockaddr.  It is the callers responsibility to deallocate
 * this structure when it is finished with it.
 */
struct sockaddr *
conf_get_address(char *section, char *tag)
{
	char	*value = conf_get_str(section, tag);
	struct sockaddr *sa;

	if (!value)
		return 0;
	if (text2sockaddr(value, 0, &sa, 0, 0) == -1)
		return 0;
	return sa;
}

/* Validate X according to the range denoted by TAG in section SECTION.  */
int
conf_match_num(char *section, char *tag, int x)
{
	char	*value = conf_get_str(section, tag);
	int	 val, min, max, n;

	if (!value)
		return 0;
	n = sscanf(value, "%d,%d:%d", &val, &min, &max);
	switch (n) {
	case 1:
		LOG_DBG((LOG_MISC, 95, "conf_match_num: %s:%s %d==%d?",
		    section, tag, val, x));
		return x == val;
	case 3:
		LOG_DBG((LOG_MISC, 95, "conf_match_num: %s:%s %d<=%d<=%d?",
		    section, tag, min, x, max));
		return min <= x && max >= x;
	default:
		log_error("conf_match_num: section %s tag %s: invalid number "
		    "spec %s", section, tag, value);
	}
	return 0;
}

/* Return the string value denoted by TAG in section SECTION.  */
char *
conf_get_str(char *section, char *tag)
{
	struct conf_binding *cb;

	for (cb = LIST_FIRST(&conf_bindings[conf_hash(section)]); cb;
	    cb = LIST_NEXT(cb, link))
		if (strcasecmp(section, cb->section) == 0 &&
		    strcasecmp(tag, cb->tag) == 0) {
			LOG_DBG((LOG_MISC, 95, "conf_get_str: [%s]:%s->%s",
			    section, tag, cb->value));
			return cb->value;
		}
	LOG_DBG((LOG_MISC, 95,
	    "conf_get_str: configuration value not found [%s]:%s", section,
	    tag));
	return 0;
}

/*
 * Build a list of string values out of the comma separated value denoted by
 * TAG in SECTION.
 */
struct conf_list *
conf_get_list(char *section, char *tag)
{
	char	*liststr = 0, *p, *field, *t;
	struct conf_list *list = 0;
	struct conf_list_node *node = 0;

	list = malloc(sizeof *list);
	if (!list)
		goto cleanup;
	TAILQ_INIT(&list->fields);
	list->cnt = 0;
	liststr = conf_get_str(section, tag);
	if (!liststr)
		goto cleanup;
	liststr = strdup(liststr);
	if (!liststr)
		goto cleanup;
	p = liststr;
	while ((field = strsep(&p, ",")) != NULL) {
		/* Skip leading whitespace */
		while (isspace((unsigned char)*field))
			field++;
		/* Skip trailing whitespace */
		if (p)
			for (t = p - 1; t > field && isspace((unsigned char)*t); t--)
				*t = '\0';
		if (*field == '\0') {
			log_print("conf_get_list: empty field, ignoring...");
			continue;
		}
		list->cnt++;
		node = calloc(1, sizeof *node);
		if (!node)
			goto cleanup;
		node->field = strdup(field);
		if (!node->field)
			goto cleanup;
		TAILQ_INSERT_TAIL(&list->fields, node, link);
	}
	free(liststr);
	return list;

cleanup:
	free(node);
	if (list)
		conf_free_list(list);
	free(liststr);
	return 0;
}

struct conf_list *
conf_get_tag_list(char *section)
{
	struct conf_list *list = 0;
	struct conf_list_node *node = 0;
	struct conf_binding *cb;

	list = malloc(sizeof *list);
	if (!list)
		goto cleanup;
	TAILQ_INIT(&list->fields);
	list->cnt = 0;
	for (cb = LIST_FIRST(&conf_bindings[conf_hash(section)]); cb;
	    cb = LIST_NEXT(cb, link))
		if (strcasecmp(section, cb->section) == 0) {
			list->cnt++;
			node = calloc(1, sizeof *node);
			if (!node)
				goto cleanup;
			node->field = strdup(cb->tag);
			if (!node->field)
				goto cleanup;
			TAILQ_INSERT_TAIL(&list->fields, node, link);
		}
	return list;

cleanup:
	free(node);
	if (list)
		conf_free_list(list);
	return 0;
}

void
conf_free_list(struct conf_list *list)
{
	struct conf_list_node *node = TAILQ_FIRST(&list->fields);

	while (node) {
		TAILQ_REMOVE(&list->fields, node, link);
		free(node->field);
		free(node);
		node = TAILQ_FIRST(&list->fields);
	}
	free(list);
}

int
conf_begin(void)
{
	static int	seq = 0;

	return ++seq;
}

static int
conf_trans_node(int transaction, enum conf_op op, char *section, char *tag,
    char *value, int override, int is_default)
{
	struct conf_trans *node;

	node = calloc(1, sizeof *node);
	if (!node) {
		log_error("conf_trans_node: calloc (1, %lu) failed",
		    (unsigned long)sizeof *node);
		return 1;
	}
	node->trans = transaction;
	node->op = op;
	node->override = override;
	node->is_default = is_default;
	if (section && (node->section = strdup(section)) == NULL)
		goto fail;
	if (tag && (node->tag = strdup(tag)) == NULL)
		goto fail;
	if (value && (node->value = strdup(value)) == NULL)
		goto fail;
	TAILQ_INSERT_TAIL(&conf_trans_queue, node, link);
	return 0;

fail:
	free(node->section);
	free(node->tag);
	free(node->value);
	free(node);
	return 1;
}

/* Queue a set operation.  */
int
conf_set(int transaction, char *section, char *tag, char *value, int override,
    int is_default)
{
	return conf_trans_node(transaction, CONF_SET, section, tag, value,
	    override, is_default);
}

/* Queue a remove operation.  */
int
conf_remove(int transaction, char *section, char *tag)
{
	return conf_trans_node(transaction, CONF_REMOVE, section, tag, NULL,
	    0, 0);
}

/* Queue a remove section operation.  */
int
conf_remove_section(int transaction, char *section)
{
	return conf_trans_node(transaction, CONF_REMOVE_SECTION, section, NULL,
	    NULL, 0, 0);
}

/* Execute all queued operations for this transaction.  Cleanup.  */
int
conf_end(int transaction, int commit)
{
	struct conf_trans *node, *next;

	for (node = TAILQ_FIRST(&conf_trans_queue); node; node = next) {
		next = TAILQ_NEXT(node, link);
		if (node->trans == transaction) {
			if (commit)
				switch (node->op) {
				case CONF_SET:
					conf_set_now(node->section, node->tag,
					    node->value, node->override,
					    node->is_default);
					break;
				case CONF_REMOVE:
					conf_remove_now(node->section,
					    node->tag);
					break;
				case CONF_REMOVE_SECTION:
					conf_remove_section_now(node->section);
					break;
				default:
					log_print("conf_end: unknown "
					    "operation: %d", node->op);
				}
			TAILQ_REMOVE(&conf_trans_queue, node, link);
			free(node->section);
			free(node->tag);
			free(node->value);
			free(node);
		}
	}
	return 0;
}

/*
 * Dump running configuration upon SIGUSR1.
 * Configuration is "stored in reverse order", so reverse it again.
 */
struct dumper {
	char	*s, *v;
	struct dumper *next;
};

static void
conf_report_dump(struct dumper *node)
{
	/* Recursive, cleanup when we're done.  */

	if (node->next)
		conf_report_dump(node->next);

	if (node->v)
		LOG_DBG((LOG_REPORT, 0, "%s=\t%s", node->s, node->v));
	else if (node->s) {
		LOG_DBG((LOG_REPORT, 0, "%s", node->s));
		if (strlen(node->s) > 0)
			free(node->s);
	}
	free(node);
}

void
conf_report(void)
{
	struct conf_binding *cb, *last = 0;
	unsigned int	i;
	char           *current_section = NULL;
	struct dumper  *dumper, *dnode;

	dumper = dnode = calloc(1, sizeof *dumper);
	if (!dumper)
		goto mem_fail;

	LOG_DBG((LOG_REPORT, 0, "conf_report: dumping running configuration"));

	for (i = 0; i < sizeof conf_bindings / sizeof conf_bindings[0]; i++)
		for (cb = LIST_FIRST(&conf_bindings[i]); cb;
		    cb = LIST_NEXT(cb, link)) {
			if (!cb->is_default) {
				/* Dump this entry.  */
				if (!current_section || strcmp(cb->section,
				    current_section)) {
					if (current_section) {
						if (asprintf(&dnode->s, "[%s]",
						    current_section) == -1)
							goto mem_fail;
						dnode->next = calloc(1,
						    sizeof(struct dumper));
						dnode = dnode->next;
						if (!dnode)
							goto mem_fail;

						dnode->s = "";
						dnode->next = calloc(1,
						    sizeof(struct dumper));
						dnode = dnode->next;
						if (!dnode)
							goto mem_fail;
					}
					current_section = cb->section;
				}
				dnode->s = cb->tag;
				dnode->v = cb->value;
				dnode->next = calloc(1, sizeof(struct dumper));
				dnode = dnode->next;
				if (!dnode)
					goto mem_fail;
				last = cb;
			}
		}

	if (last)
		if (asprintf(&dnode->s, "[%s]", last->section) == -1)
			goto mem_fail;
	conf_report_dump(dumper);

	return;

mem_fail:
	log_error("conf_report: malloc/calloc failed");
	while ((dnode = dumper) != 0) {
		dumper = dumper->next;
		free(dnode->s);
		free(dnode);
	}
}
