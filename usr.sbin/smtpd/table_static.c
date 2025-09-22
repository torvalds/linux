/*	$OpenBSD: table_static.c,v 1.35 2024/05/14 13:28:08 op Exp $	*/

/*
 * Copyright (c) 2013 Eric Faurot <eric@openbsd.org>
 * Copyright (c) 2012 Gilles Chehade <gilles@poolp.org>
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

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "smtpd.h"
#include "log.h"

struct table_static_priv {
	int		 type;
	struct dict	 dict;
	void		*iter;
};

/* static backend */
static int table_static_config(struct table *);
static int table_static_add(struct table *, const char *, const char *);
static void table_static_dump(struct table *);
static int table_static_update(struct table *);
static int table_static_open(struct table *);
static int table_static_lookup(struct table *, enum table_service, const char *,
    char **);
static int table_static_fetch(struct table *, enum table_service, char **);
static void table_static_close(struct table *);

struct table_backend table_backend_static = {
	.name = "static",
	.services = K_ALIAS|K_CREDENTIALS|K_DOMAIN|K_NETADDR|K_USERINFO|
	K_SOURCE|K_MAILADDR|K_ADDRNAME|K_MAILADDRMAP|K_RELAYHOST|
	K_STRING|K_REGEX,
	.config = table_static_config,
	.add = table_static_add,
	.dump = table_static_dump,
	.open = table_static_open,
	.update = table_static_update,
	.close = table_static_close,
	.lookup = table_static_lookup,
	.fetch = table_static_fetch
};

static struct keycmp {
	enum table_service	service;
	int		       (*func)(const char *, const char *);
} keycmp[] = {
	{ K_DOMAIN, table_domain_match },
	{ K_NETADDR, table_netaddr_match },
	{ K_MAILADDR, table_mailaddr_match },
	{ K_REGEX, table_regex_match },
};


static void
table_static_priv_free(struct table_static_priv *priv)
{
	void *p;

	while (dict_poproot(&priv->dict, (void **)&p))
		if (p != priv)
			free(p);
	free(priv);
}

static int
table_static_priv_add(struct table_static_priv *priv, const char *key, const char *val)
{
	char lkey[1024];
	void *old, *new = NULL;

	if (!lowercase(lkey, key, sizeof lkey)) {
		errno = ENAMETOOLONG;
		return (-1);
	}

	if (val) {
		new = strdup(val);
		if (new == NULL)
			return (-1);
	}

	/* use priv if value is null, so we can detect duplicate entries */
	old = dict_set(&priv->dict, lkey, new ? new : priv);
	if (old) {
		if (old != priv)
			free(old);
		return (1);
	}

	return (0);
}

static int
table_static_priv_load(struct table_static_priv *priv, const char *path)
{
	FILE	*fp;
	char	*line = NULL;
	int	 lineno = 0;
	size_t	 linesize = 0;
	char	*keyp;
	char	*valp;
	int	 malformed, ret = 0;

	if ((fp = fopen(path, "r")) == NULL) {
		log_warn("%s: fopen", path);
		return 0;
	}

	while (parse_table_line(fp, &line, &linesize, &priv->type,
	    &keyp, &valp, &malformed) != -1) {
		lineno++;
		if (malformed) {
			log_warnx("%s:%d invalid map entry",
			    path, lineno);
			goto end;
		}
		if (keyp == NULL)
			continue;
		table_static_priv_add(priv, keyp, valp);
	}

	if (ferror(fp)) {
		log_warn("%s: getline", path);
		goto end;
	}

	/* Accept empty alias files; treat them as hashes */
	if (priv->type == T_NONE)
		priv->type = T_HASH;

	ret = 1;
end:
	free(line);
	fclose(fp);
	return ret;
}

static int
table_static_config(struct table *t)
{
	struct table_static_priv *priv, *old;

	/* already up, and no config file? ok */
	if (t->t_handle && *t->t_config == '\0')
		return 1;

	/* new config */
	priv = calloc(1, sizeof(*priv));
	if (priv == NULL)
		return 0;
	priv->type = t->t_type;
	dict_init(&priv->dict);
	
	if (*t->t_config) {
		/* load the config file */
		if (table_static_priv_load(priv, t->t_config) == 0) {
			table_static_priv_free(priv);
			return 0;
		}
	}

	if ((old = t->t_handle))
		table_static_priv_free(old);
	t->t_handle = priv;
	t->t_type = priv->type;

	return 1;
}

static int
table_static_add(struct table *table, const char *key, const char *val)
{
	struct table_static_priv *priv = table->t_handle;
	int r;

	/* cannot add to a table read from a file */
	if (*table->t_config)
		return 0;

	if (table->t_type == T_NONE)
		table->t_type = val ? T_HASH : T_LIST;
	else if (table->t_type == T_LIST && val)
		return 0;
	else if (table->t_type == T_HASH && val == NULL)
		return 0;

	if (priv == NULL) {
		if (table_static_config(table) == 0)
			return 0;
		priv = table->t_handle;
	}

	r = table_static_priv_add(priv, key, val);
	if (r == -1)
		return 0;
	return 1;
}

static void
table_static_dump(struct table *table)
{
	struct table_static_priv *priv = table->t_handle;
	const char *key;
	char *value;
	void *iter;

	iter = NULL;
	while (dict_iter(&priv->dict, &iter, &key, (void**)&value)) {
		if (value && (void*)value != (void*)priv)
			log_debug("	\"%s\" -> \"%s\"", key, value);
		else
			log_debug("	\"%s\"", key);
	}
}

static int
table_static_update(struct table *table)
{
	if (table_static_config(table) == 1) {
		log_info("info: Table \"%s\" successfully updated", table->t_name);
		return 1;
	}

	log_info("info: Failed to update table \"%s\"", table->t_name);
	return 0;
}

static int
table_static_open(struct table *table)
{
	if (table->t_handle == NULL)
		return table_static_config(table);
	return 1;
}

static void
table_static_close(struct table *table)
{
	struct table_static_priv *priv = table->t_handle;

	if (priv)
		table_static_priv_free(priv);
	table->t_handle = NULL;
}

static int
table_static_lookup(struct table *table, enum table_service service, const char *key,
    char **dst)
{
	struct table_static_priv *priv = table->t_handle;
	char	       *line;
	int		ret;
	int	       (*match)(const char *, const char *) = NULL;
	size_t		i;
	void	       *iter;
	const char     *k;
	char	       *v;

	for (i = 0; i < nitems(keycmp); ++i)
		if (keycmp[i].service == service)
			match = keycmp[i].func;

	line = NULL;
	iter = NULL;
	ret = 0;
	while (dict_iter(&priv->dict, &iter, &k, (void **)&v)) {
		if (match) {
			if (match(key, k)) {
				line = v;
				ret = 1;
			}
		}
		else {
			if (strcmp(key, k) == 0) {
				line = v;
				ret = 1;
			}
		}
		if (ret)
			break;
	}

	if (dst == NULL)
		return ret ? 1 : 0;

	if (ret == 0)
		return 0;

	*dst = strdup(line);
	if (*dst == NULL)
		return -1;

	return 1;
}

static int
table_static_fetch(struct table *t, enum table_service service, char **dst)
{
	struct table_static_priv *priv = t->t_handle;
	const char *k;

	if (!dict_iter(&priv->dict, &priv->iter, &k, (void **)NULL)) {
		priv->iter = NULL;
		if (!dict_iter(&priv->dict, &priv->iter, &k, (void **)NULL))
			return 0;
	}

	*dst = strdup(k);
	if (*dst == NULL)
		return -1;

	return 1;
}
