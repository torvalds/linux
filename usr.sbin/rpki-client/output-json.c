/*	$OpenBSD: output-json.c,v 1.57 2025/09/17 12:10:08 job Exp $ */
/*
 * Copyright (c) 2019 Claudio Jeker <claudio@openbsd.org>
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

#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <netdb.h>

#include "extern.h"
#include "json.h"

static void
outputheader_json(struct validation_data *vd, struct stats *st)
{
	char		 hn[NI_MAXHOST], tbuf[26];
	struct tm	*tp;
	time_t		 t;
	int		 i;

	time(&t);
	tp = gmtime(&t);
	strftime(tbuf, sizeof tbuf, "%FT%TZ", tp);

	gethostname(hn, sizeof hn);

	json_do_object("metadata", 0);

	json_do_string("buildmachine", hn);
	json_do_string("buildtime", tbuf);
	json_do_int("elapsedtime", st->elapsed_time.tv_sec);
	json_do_int("usertime", st->user_time.tv_sec);
	json_do_int("systemtime", st->system_time.tv_sec);

	json_do_string("ccr_mfts_hash", vd->ccr.mfts_hash);
	json_do_string("ccr_vrps_hash", vd->ccr.vrps_hash);
	json_do_string("ccr_vaps_hash", vd->ccr.vaps_hash);
	json_do_string("ccr_brks_hash", vd->ccr.brks_hash);
	json_do_string("ccr_tas_hash", vd->ccr.tas_hash);

	json_do_int("roas", st->repo_tal_stats.roas);
	json_do_int("failedroas", st->repo_tal_stats.roas_fail);
	json_do_int("invalidroas", st->repo_tal_stats.roas_invalid);
	if (experimental) {
		json_do_int("spls", st->repo_tal_stats.spls);
		json_do_int("failedspls", st->repo_tal_stats.spls_fail);
		json_do_int("invalidspls", st->repo_tal_stats.spls_invalid);
	}
	json_do_int("aspas", st->repo_tal_stats.aspas);
	json_do_int("failedaspas", st->repo_tal_stats.aspas_fail);
	json_do_int("invalidaspas", st->repo_tal_stats.aspas_invalid);
	json_do_int("bgpsec_pubkeys", st->repo_tal_stats.brks);
	json_do_int("certificates", st->repo_tal_stats.certs);
	json_do_int("invalidcertificates", st->repo_tal_stats.certs_fail);
	json_do_int("nonfunctionalcas", st->repo_tal_stats.certs_nonfunc);
	json_do_int("taks", st->repo_tal_stats.taks);
	json_do_int("tals", st->tals);
	json_do_int("invalidtals", talsz - st->tals);

	json_do_array("talfiles");
	for (i = 0; i < talsz; i++)
		json_do_string("name", tals[i]);
	json_do_end();

	json_do_int("manifests", st->repo_tal_stats.mfts);
	json_do_int("failedmanifests", st->repo_tal_stats.mfts_fail);
	json_do_int("crls", st->repo_tal_stats.crls);
	json_do_int("gbrs", st->repo_tal_stats.gbrs);
	json_do_int("repositories", st->repos);
	json_do_int("vrps", st->repo_tal_stats.vrps);
	json_do_int("uniquevrps", st->repo_tal_stats.vrps_uniqs);
	json_do_int("vsps", st->repo_tal_stats.vsps);
	json_do_int("uniquevsps", st->repo_tal_stats.vsps_uniqs);
	json_do_int("vaps", st->repo_tal_stats.vaps);
	json_do_int("uniquevaps", st->repo_tal_stats.vaps_uniqs);
	json_do_int("cachedir_new_files", st->repo_stats.new_files);
	json_do_int("cachedir_del_files", st->repo_stats.del_files);
	json_do_int("cachedir_del_dirs", st->repo_stats.del_dirs);
	json_do_int("cachedir_superfluous_files", st->repo_stats.extra_files);
	json_do_int("cachedir_del_superfluous_files",
	    st->repo_stats.del_extra_files);

	json_do_end();
}

static void
print_vap(struct vap *v)
{
	size_t i;

	if (v->overflowed)
		return;

	json_do_object("aspa", 1);
	json_do_int("customer_asid", v->custasid);
	json_do_int("expires", v->expires);

	json_do_array("providers");
	for (i = 0; i < v->num_providers; i++)
		json_do_int("provider", v->providers[i]);

	json_do_end();
}

static void
output_aspa(struct vap_tree *vaps)
{
	struct vap	*v;

	json_do_array("aspas");
	RB_FOREACH(v, vap_tree, vaps)
		print_vap(v);
	json_do_end();
}

static void
output_spl(struct vsp_tree *vsps)
{
	struct vsp	*vsp;
	char		 buf[64];
	size_t		 i;

	json_do_array("signedprefixlists");
	RB_FOREACH(vsp, vsp_tree, vsps) {
		json_do_object("vsp", 1);
		json_do_int("origin_as", vsp->asid);
		json_do_array("prefixes");
		for (i = 0; i < vsp->num_prefixes; i++) {
			ip_addr_print(&vsp->prefixes[i].prefix,
			    vsp->prefixes[i].afi, buf, sizeof(buf));
			json_do_string("prefix", buf);
		}
		json_do_end();
		json_do_int("expires", vsp->expires);
		json_do_string("ta", taldescs[vsp->talid]);
		json_do_end();
	}
	json_do_end();
}

int
output_json(FILE *out, struct validation_data *vd, struct stats *st)
{
	char			 buf[64];
	struct vrp		*v;
	struct brk		*b;
	struct nonfunc_ca	*nca;

	json_do_start(out);
	outputheader_json(vd, st);

	json_do_array("roas");
	RB_FOREACH(v, vrp_tree, &vd->vrps) {
		ip_addr_print(&v->addr, v->afi, buf, sizeof(buf));

		json_do_object("roa", 1);
		json_do_int("asn", v->asid);
		json_do_string("prefix", buf);
		json_do_int("maxLength", v->maxlength);
		json_do_string("ta", taldescs[v->talid]);
		json_do_int("expires", v->expires);
		json_do_end();
	}
	json_do_end();

	json_do_array("bgpsec_keys");
	RB_FOREACH(b, brk_tree, &vd->brks) {
		json_do_object("brks", 0);
		json_do_int("asn", b->asid);
		json_do_string("ski", b->ski);
		json_do_string("pubkey", b->pubkey);
		json_do_string("ta", taldescs[b->talid]);
		json_do_int("expires", b->expires);
		json_do_end();
	}
	json_do_end();

	json_do_array("nonfunc_cas");
	RB_FOREACH(nca, nca_tree, &vd->ncas) {
		json_do_object("nca", 1);
		json_do_string("location", nca->location);
		json_do_string("ta", taldescs[nca->talid]);
		json_do_string("caRepository", nca->carepo);
		json_do_string("rpkiManifest", nca->mfturi);
		json_do_string("ski", nca->ski);
		json_do_end();
	}
	json_do_end();

	if (!excludeaspa)
		output_aspa(&vd->vaps);

	if (experimental)
		output_spl(&vd->vsps);

	return json_do_finish();
}
