/*	$OpenBSD: output-ometric.c,v 1.15 2025/07/08 14:19:21 job Exp $ */
/*
 * Copyright (c) 2022 Claudio Jeker <claudio@openbsd.org>
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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"
#include "ometric.h"
#include "version.h"

static struct ometric *rpki_info, *rpki_completion_time, *rpki_duration;
static struct ometric *rpki_repo, *rpki_obj, *rpki_ta_obj;
static struct ometric *rpki_repo_obj, *rpki_repo_duration;
static struct ometric *rpki_repo_state, *rpki_repo_proto;

static const char * const repo_states[2] = { "failed", "synced" };
static const char * const repo_protos[3] = { "rrdp", "rsync", "https" };

static void
set_common_stats(const struct repotalstats *in, struct ometric *metric,
    struct olabels *ol)
{
	ometric_set_int_with_labels(metric, in->certs,
	    OKV("type", "state"), OKV("cert", "valid"), ol);
	ometric_set_int_with_labels(metric, in->certs_fail,
	    OKV("type", "state"), OKV("cert", "failed parse"), ol);
	ometric_set_int_with_labels(metric, in->certs_nonfunc,
	    OKV("type", "state"), OKV("cert", "non-functional"), ol);

	ometric_set_int_with_labels(metric, in->mfts,
	    OKV("type", "state"), OKV("manifest", "valid"), ol);
	ometric_set_int_with_labels(metric, in->mfts_fail,
	    OKV("type", "state"), OKV("manifest", "failed parse"), ol);
	ometric_set_int_with_labels(metric, in->mfts_gap,
	    OKV("type", "state"), OKV("manifest", "sequence gap"), ol);

	ometric_set_int_with_labels(metric, in->roas,
	    OKV("type", "state"), OKV("roa", "valid"), ol);
	ometric_set_int_with_labels(metric, in->roas_fail,
	    OKV("type", "state"), OKV("roa", "failed parse"), ol);
	ometric_set_int_with_labels(metric, in->roas_invalid,
	    OKV("type", "state"), OKV("roa", "invalid"), ol);

	ometric_set_int_with_labels(metric, in->aspas,
	    OKV("type", "state"), OKV("aspa", "valid"), ol);
	ometric_set_int_with_labels(metric, in->aspas_fail,
	    OKV("type", "state"), OKV("aspa", "failed parse"), ol);
	ometric_set_int_with_labels(metric, in->aspas_invalid,
	    OKV("type", "state"), OKV("aspa", "invalid"), ol);

	ometric_set_int_with_labels(metric, in->brks,
	    OKV("type", "state"), OKV("router_key", "valid"), ol);
	ometric_set_int_with_labels(metric, in->crls,
	    OKV("type", "state"), OKV("crl", "valid"), ol);
	ometric_set_int_with_labels(metric, in->gbrs,
	    OKV("type", "state"), OKV("gbr", "valid"), ol);
	ometric_set_int_with_labels(metric, in->taks,
	    OKV("type", "state"), OKV("tak", "valid"), ol);

	ometric_set_int_with_labels(metric, in->vrps,
	    OKV("type", "state"), OKV("vrp", "total"), ol);
	ometric_set_int_with_labels(metric, in->vrps_uniqs,
	    OKV("type", "state"), OKV("vrp", "unique"), ol);

	ometric_set_int_with_labels(metric, in->vaps,
	    OKV("type", "state"), OKV("vap", "total"), ol);
	ometric_set_int_with_labels(metric, in->vaps_uniqs,
	    OKV("type", "state"), OKV("vap", "unique"), ol);
	ometric_set_int_with_labels(metric, in->vaps_pas,
	    OKV("type", "state"), OKV("vap providers", "total"), ol);
	ometric_set_int_with_labels(metric, in->vaps_overflowed,
	    OKV("type", "state"), OKV("vap overflowed"), ol);

	if (experimental) {
		ometric_set_int_with_labels(metric, in->spls,
		    OKV("type", "state"), OKV("spl", "valid"), ol);
		ometric_set_int_with_labels(metric, in->spls_fail,
		    OKV("type", "state"), OKV("spl", "failed parse"), ol);
		ometric_set_int_with_labels(metric, in->spls_invalid,
		    OKV("type", "state"), OKV("spl", "invalid"), ol);
	}

	ometric_set_int_with_labels(metric, in->vsps,
	    OKV("type", "state"), OKV("vsp", "total"), ol);
	ometric_set_int_with_labels(metric, in->vsps_uniqs,
	    OKV("type", "state"), OKV("vsp", "unique"), ol);
}

static void
ta_stats(int id)
{
	struct olabels *ol;
	const char *keys[2] = { "name", NULL };
	const char *values[2];

	values[0] = taldescs[id];
	values[1] = NULL;

	ol = olabels_new(keys, values);
	set_common_stats(&talstats[id], rpki_ta_obj, ol);
	olabels_free(ol);
}

static void
repo_tal_stats(const struct repo *rp, const struct repotalstats *in, void *arg)
{
	struct olabels *ol;
	const char *keys[4] = { "name", "carepo", "notify", NULL };
	const char *values[4];
	int talid = *(int *)arg;

	values[0] = taldescs[talid];
	repo_fetch_uris(rp, &values[1], &values[2]);
	values[3] = NULL;

	ol = olabels_new(keys, values);
	set_common_stats(in, rpki_repo_obj, ol);
	olabels_free(ol);
}

static void
repo_stats(const struct repo *rp, const struct repostats *in, void *arg)
{
	struct olabels *ol;
	const char *keys[3] = { "carepo", "notify", NULL };
	const char *values[3];

	repo_fetch_uris(rp, &values[0], &values[1]);
	values[2] = NULL;

	ol = olabels_new(keys, values);
	ometric_set_timespec(rpki_repo_duration, &in->sync_time, ol);

	ometric_set_int_with_labels(rpki_repo_obj, in->new_files,
	    OKV("type", "state"), OKV("files", "new"), ol);
	ometric_set_int_with_labels(rpki_repo_obj, in->del_files,
	    OKV("type", "state"), OKV("files", "deleted"), ol);
	ometric_set_int_with_labels(rpki_repo_obj, in->extra_files,
	    OKV("type", "state"), OKV("files", "extra"), ol);
	ometric_set_int_with_labels(rpki_repo_obj, in->del_extra_files,
	    OKV("type", "state"), OKV("files", "deleted_extra"), ol);
	ometric_set_int_with_labels(rpki_repo_obj, in->del_dirs,
	    OKV("type", "state"), OKV("dirs", "deleted"), ol);

	ometric_set_state(rpki_repo_state, repo_states[repo_synced(rp)], ol);
	if (repo_synced(rp))
		ometric_set_state(rpki_repo_proto, repo_proto(rp), ol);
	olabels_free(ol);
}

int
output_ometric(FILE *out, struct validation_data *vd, struct stats *st)
{
	struct olabels *ol;
	const char *keys[4] = { "nodename", "domainname", "release", NULL };
	const char *values[4];
	char hostname[HOST_NAME_MAX + 1];
	char *domainname;
	struct timespec now_time;
	int rv, i;

	rpki_info = ometric_new(OMT_INFO, "rpki_client",
	    "rpki-client information");
	rpki_completion_time = ometric_new(OMT_GAUGE,
	    "rpki_client_job_completion_time",
	    "end of this run as epoch timestamp");

	rpki_repo = ometric_new(OMT_GAUGE, "rpki_client_repository",
	    "total number of repositories");
	rpki_obj = ometric_new(OMT_GAUGE, "rpki_client_objects",
	    "total number of objects");

	rpki_duration = ometric_new(OMT_GAUGE, "rpki_client_duration",
	    "duration in seconds");

	rpki_ta_obj = ometric_new(OMT_GAUGE, "rpki_client_ta_objects",
	    "total number of objects per TAL");
	rpki_repo_obj = ometric_new(OMT_GAUGE, "rpki_client_repository_objects",
	    "total number of objects per repository");
	rpki_repo_duration = ometric_new(OMT_GAUGE,
	    "rpki_client_repository_duration",
	    "duration used to sync this repository in seconds");
	rpki_repo_state = ometric_new_state(repo_states,
	    sizeof(repo_states) / sizeof(repo_states[0]),
	    "rpki_client_repository_state",
	    "repository state");
	rpki_repo_proto = ometric_new_state(repo_protos,
	    sizeof(repo_protos) / sizeof(repo_protos[0]),
	    "rpki_client_repository_protos",
	    "used protocol to sync repository");

	/*
	 * Dump statistics
	 */
	if (gethostname(hostname, sizeof(hostname)))
		err(1, "gethostname");
	if ((domainname = strchr(hostname, '.')))
		*domainname++ = '\0';

	values[0] = hostname;
	values[1] = domainname;
	values[2] = RPKI_VERSION;
	values[3] = NULL;

	ol = olabels_new(keys, values);
	ometric_set_info(rpki_info, NULL, NULL, ol);
	olabels_free(ol);

	for (i = 0; i < talsz; i++) {
		repo_tal_stats_collect(repo_tal_stats, i, &i);
		ta_stats(i);
	}
	repo_stats_collect(repo_stats, NULL);
	set_common_stats(&st->repo_tal_stats, rpki_obj, NULL);

	ometric_set_int_with_labels(rpki_repo, st->rsync_repos,
	    OKV("type", "state"), OKV("rsync", "synced"), NULL);
	ometric_set_int_with_labels(rpki_repo, st->rsync_fails,
	    OKV("type", "state"), OKV("rsync", "failed"), NULL);
	ometric_set_int_with_labels(rpki_repo, st->http_repos,
	    OKV("type", "state"), OKV("http", "synced"), NULL);
	ometric_set_int_with_labels(rpki_repo, st->http_fails,
	    OKV("type", "state"), OKV("http", "failed"), NULL);
	ometric_set_int_with_labels(rpki_repo, st->rrdp_repos,
	    OKV("type", "state"), OKV("rrdp", "synced"), NULL);
	ometric_set_int_with_labels(rpki_repo, st->rrdp_fails,
	    OKV("type", "state"), OKV("rrdp", "failed"), NULL);

	ometric_set_timespec_with_labels(rpki_duration, &st->elapsed_time,
	    OKV("type"), OKV("elapsed"), NULL);
	ometric_set_timespec_with_labels(rpki_duration, &st->user_time,
	    OKV("type"), OKV("user"), NULL);
	ometric_set_timespec_with_labels(rpki_duration, &st->system_time,
	    OKV("type"), OKV("system"), NULL);

	clock_gettime(CLOCK_REALTIME, &now_time);
	ometric_set_timespec(rpki_completion_time, &now_time, NULL);

	rv = ometric_output_all(out);
	ometric_free_all();

	return rv;
}
