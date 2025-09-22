/*	$OpenBSD: output.c,v 1.42 2025/08/23 09:13:14 job Exp $ */
/*
 * Copyright (c) 2019 Theo de Raadt <deraadt@openbsd.org>
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

/*-
 * Copyright (C) 2009 Gabor Kovesdan <gabor@FreeBSD.org>
 * Copyright (C) 2012 Oleg Moskalenko <mom040267@gmail.com>
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

#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#include "extern.h"

int		 outformats;

static char	 output_tmpname[PATH_MAX];
static char	 output_name[PATH_MAX];

static const struct outputs {
	int	 format;
	char	*name;
	int	(*fn)(FILE *, struct validation_data *, struct stats *);
} outputs[] = {
	{ FORMAT_OPENBGPD, "openbgpd", output_bgpd },
	{ FORMAT_BIRD, "bird", output_bird },
	{ FORMAT_CSV, "csv", output_csv },
	{ FORMAT_JSON, "json", output_json },
	{ FORMAT_OMETRIC, "metrics", output_ometric },
	{ FORMAT_CCR, "rpki.ccr", output_ccr_der },
	{ 0, NULL, NULL }
};

static FILE	*output_createtmp(char *);
static void	 output_cleantmp(void);
static int	 output_finish(FILE *);
static void	 sig_handler(int);
static void	 set_signal_handler(void);

/*
 * Detect & reject so-called "AS0 TALs".
 * AS0 TALs are TALs where for each and every subordinate ROA the asID field
 * set to 0. Such TALs introduce operational risk, as they change the fail-safe
 * from 'fail-open' to 'fail-closed'. Some context:
 *     https://lists.afrinic.net/pipermail/rpd/2021/013312.html
 *     https://lists.afrinic.net/pipermail/rpd/2021/013314.html
 */
static void
prune_as0_tals(struct vrp_tree *vrps)
{
	struct vrp *v, *tv;
	int talid;
	int has_vrps[TALSZ_MAX] = { 0 };
	int is_as0_tal[TALSZ_MAX] = { 0 };

	for (talid = 0; talid < talsz; talid++)
		is_as0_tal[talid] = 1;

	RB_FOREACH(v, vrp_tree, vrps) {
		has_vrps[v->talid] = 1;
		if (v->asid != 0)
			is_as0_tal[v->talid] = 0;
	}

	for (talid = 0; talid < talsz; talid++) {
		if (is_as0_tal[talid] && has_vrps[talid]) {
			warnx("%s: Detected AS0 TAL, pruning associated VRPs",
			    taldescs[talid]);
		}
	}

	RB_FOREACH_SAFE(v, vrp_tree, vrps, tv) {
		if (is_as0_tal[v->talid]) {
			RB_REMOVE(vrp_tree, vrps, v);
			free(v);
		}
	}

	/* XXX: update talstats? */
}

int
outputfiles(struct validation_data *vd, struct stats *st)
{
	int i, rc = 0;

	atexit(output_cleantmp);
	set_signal_handler();

	if (excludeas0)
		prune_as0_tals(&vd->vrps);

	for (i = 0; outputs[i].name; i++) {
		FILE *fout;

		if (!(outformats & outputs[i].format))
			continue;

		fout = output_createtmp(outputs[i].name);
		if (fout == NULL) {
			warn("cannot create %s", outputs[i].name);
			rc = 1;
			continue;
		}
		if ((*outputs[i].fn)(fout, vd, st) != 0) {
			warn("output for %s format failed", outputs[i].name);
			fclose(fout);
			output_cleantmp();
			rc = 1;
			continue;
		}
		if (output_finish(fout) != 0) {
			warn("finish for %s format failed", outputs[i].name);
			output_cleantmp();
			rc = 1;
			continue;
		}
	}

	return rc;
}

static FILE *
output_createtmp(char *name)
{
	FILE *f;
	int fd, r;

	if (strlcpy(output_name, name, sizeof output_name) >=
	    sizeof output_name)
		err(1, "path too long");
	r = snprintf(output_tmpname, sizeof output_tmpname,
	    "%s.XXXXXXXXXXX", output_name);
	if (r < 0 || r > (int)sizeof(output_tmpname))
		err(1, "path too long");
	fd = mkostemp(output_tmpname, O_CLOEXEC);
	if (fd == -1)
		err(1, "mkostemp: %s", output_tmpname);
	(void) fchmod(fd, 0644);
	f = fdopen(fd, "w");
	if (f == NULL)
		err(1, "fdopen");
	return f;
}

static int
output_finish(FILE *out)
{
	if (fclose(out) != 0)
		return -1;
	if (rename(output_tmpname, output_name) == -1)
		return -1;
	output_tmpname[0] = '\0';
	return 0;
}

static void
output_cleantmp(void)
{
	if (*output_tmpname)
		unlink(output_tmpname);
	output_tmpname[0] = '\0';
}

/*
 * Signal handler that clears the temporary files.
 */
static void
sig_handler(int sig)
{
	output_cleantmp();
	_exit(2);
}

/*
 * Set signal handler on panic signals.
 */
static void
set_signal_handler(void)
{
	struct sigaction sa;
	int i, signals[] = {SIGTERM, SIGHUP, SIGINT, SIGUSR1, SIGUSR2,
	    SIGPIPE, SIGXCPU, SIGXFSZ, 0};

	memset(&sa, 0, sizeof(sa));
	sigfillset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = sig_handler;

	for (i = 0; signals[i] != 0; i++) {
		if (sigaction(signals[i], &sa, NULL) == -1) {
			warn("sigaction(%s)", strsignal(signals[i]));
			continue;
		}
	}
}

int
outputheader(FILE *out, struct validation_data *vd, struct stats *st)
{
	char		hn[NI_MAXHOST], tbuf[80];
	struct tm	*tp;
	time_t		t;
	int		i;

	time(&t);
	tp = gmtime(&t);
	strftime(tbuf, sizeof tbuf, "%a %b %e %H:%M:%S UTC %Y", tp);

	gethostname(hn, sizeof hn);

	if (fprintf(out,
	    "# Generated on host %s at %s\n"
	    "# Processing time %lld seconds (%llds user, %llds system)\n"
	    "# CCR manifest hash: %s\n"
	    "# CCR validated ROA payloads hash: %s\n"
	    "# CCR validated ASPA payloads hash: %s\n"
	    "# Route Origin Authorizations: %u (%u failed parse, %u invalid)\n"
	    "# BGPsec Router Certificates: %u\n"
	    "# Certificates: %u (%u invalid, %u non-functional)\n",
	    hn, tbuf, (long long)st->elapsed_time.tv_sec,
	    (long long)st->user_time.tv_sec, (long long)st->system_time.tv_sec,
	    vd->ccr.mfts_hash, vd->ccr.vrps_hash, vd->ccr.vaps_hash,
	    st->repo_tal_stats.roas, st->repo_tal_stats.roas_fail,
	    st->repo_tal_stats.roas_invalid, st->repo_tal_stats.brks,
	    st->repo_tal_stats.certs, st->repo_tal_stats.certs_fail,
	    st->repo_tal_stats.certs_nonfunc) < 0)
		return -1;

	if (fprintf(out,
	    "# Trust Anchor Locators: %u (%u invalid) [", st->tals,
	    talsz - st->tals) < 0)
		return -1;
	for (i = 0; i < talsz; i++)
		if (fprintf(out, " %s", tals[i]) < 0)
			return -1;

	if (fprintf(out,
	    " ]\n"
	    "# Manifests: %u (%u failed parse)\n"
	    "# Certificate revocation lists: %u\n"
	    "# Ghostbuster records: %u\n"
	    "# Repositories: %u\n"
	    "# VRP Entries: %u (%u unique)\n",
	    st->repo_tal_stats.mfts, st->repo_tal_stats.mfts_fail,
	    st->repo_tal_stats.crls,
	    st->repo_tal_stats.gbrs,
	    st->repos,
	    st->repo_tal_stats.vrps, st->repo_tal_stats.vrps_uniqs) < 0)
		return -1;
	return 0;
}
