/*	$OpenBSD: stat_ramstat.c,v 1.12 2021/06/14 17:58:16 eric Exp $	*/

/*
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

#include <string.h>

#include "smtpd.h"

static void	ramstat_init(void);
static void	ramstat_close(void);
static void	ramstat_increment(const char *, size_t);
static void	ramstat_decrement(const char *, size_t);
static void	ramstat_set(const char *, const struct stat_value *);
static int	ramstat_iter(void **, char **, struct stat_value *);

struct ramstat_entry {
	RB_ENTRY(ramstat_entry)	entry;
	char			key[STAT_KEY_SIZE];
	struct stat_value	value;
};
RB_HEAD(stats_tree, ramstat_entry)	stats;
RB_PROTOTYPE(stats_tree, ramstat_entry, entry, ramstat_entry_cmp);

struct stat_backend	stat_backend_ramstat = {
	ramstat_init,
	ramstat_close,
	ramstat_increment,
	ramstat_decrement,
	ramstat_set,
	ramstat_iter
};

static void
ramstat_init(void)
{
	log_trace(TRACE_STAT, "ramstat: init");

	RB_INIT(&stats);

	/* ramstat_set() should be called for each key we want
	 * to have displayed by smtpctl show stats at startup.
	 */
	ramstat_set("uptime", stat_timestamp(env->sc_uptime));
}

static void
ramstat_close(void)
{
	log_trace(TRACE_STAT, "ramstat: close");
}

static void
ramstat_increment(const char *name, size_t val)
{
	struct ramstat_entry	*np, lk;

	log_trace(TRACE_STAT, "ramstat: increment: %s", name);
	(void)strlcpy(lk.key, name, sizeof (lk.key));
	np = RB_FIND(stats_tree, &stats, &lk);
	if (np == NULL) {
		np = xcalloc(1, sizeof *np);
		(void)strlcpy(np->key, name, sizeof (np->key));
		RB_INSERT(stats_tree, &stats, np);
	}
	log_trace(TRACE_STAT, "ramstat: %s (%p): %zd -> %zd",
	    name, name, np->value.u.counter, np->value.u.counter + val);
	np->value.u.counter += val;
}

static void
ramstat_decrement(const char *name, size_t val)
{
	struct ramstat_entry	*np, lk;

	log_trace(TRACE_STAT, "ramstat: decrement: %s", name);
	(void)strlcpy(lk.key, name, sizeof (lk.key));
	np = RB_FIND(stats_tree, &stats, &lk);
	if (np == NULL) {
		np = xcalloc(1, sizeof *np);
		(void)strlcpy(np->key, name, sizeof (np->key));
		RB_INSERT(stats_tree, &stats, np);
	}
	log_trace(TRACE_STAT, "ramstat: %s (%p): %zd -> %zd",
	    name, name, np->value.u.counter, np->value.u.counter - val);
	np->value.u.counter -= val;
}

static void
ramstat_set(const char *name, const struct stat_value *val)
{
	struct ramstat_entry	*np, lk;

	log_trace(TRACE_STAT, "ramstat: set: %s", name);
	(void)strlcpy(lk.key, name, sizeof (lk.key));
	np = RB_FIND(stats_tree, &stats, &lk);
	if (np == NULL) {
		np = xcalloc(1, sizeof *np);
		(void)strlcpy(np->key, name, sizeof (np->key));
		RB_INSERT(stats_tree, &stats, np);
	}
	log_trace(TRACE_STAT, "ramstat: %s: n/a -> n/a", name);
	np->value = *val;
}

static int
ramstat_iter(void **iter, char **name, struct stat_value *val)
{
	struct ramstat_entry *np;

	log_trace(TRACE_STAT, "ramstat: iter");
	if (RB_EMPTY(&stats))
		return 0;

	if (*iter == NULL)
		np = RB_MIN(stats_tree, &stats);
	else
		np = RB_NEXT(stats_tree, &stats, *iter);

	*iter = np;
	if (np == NULL)
		return 0;

	*name = np->key;
	*val  = np->value;
	return 1;
}


static int
ramstat_entry_cmp(struct ramstat_entry *e1, struct ramstat_entry *e2)
{
	return strcmp(e1->key, e2->key);
}

RB_GENERATE(stats_tree, ramstat_entry, entry, ramstat_entry_cmp);
