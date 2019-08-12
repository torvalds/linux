// SPDX-License-Identifier: GPL-2.0
/* Display a menu with individual samples to browse with perf script */
#include "hist.h"
#include "evsel.h"
#include "hists.h"
#include "sort.h"
#include "config.h"
#include "time-utils.h"
#include <linux/time64.h>
#include <linux/zalloc.h>

static u64 context_len = 10 * NSEC_PER_MSEC;

static int res_sample_config(const char *var, const char *value, void *data __maybe_unused)
{
	if (!strcmp(var, "samples.context"))
		return perf_config_u64(&context_len, var, value);
	return 0;
}

void res_sample_init(void)
{
	perf_config(res_sample_config, NULL);
}

int res_sample_browse(struct res_sample *res_samples, int num_res,
		      struct perf_evsel *evsel, enum rstype rstype)
{
	char **names;
	int i, n;
	int choice;
	char *cmd;
	char pbuf[256], tidbuf[32], cpubuf[32];
	const char *perf = perf_exe(pbuf, sizeof pbuf);
	char trange[128], tsample[64];
	struct res_sample *r;
	char extra_format[256];

	names = calloc(num_res, sizeof(char *));
	if (!names)
		return -1;
	for (i = 0; i < num_res; i++) {
		char tbuf[64];

		timestamp__scnprintf_nsec(res_samples[i].time, tbuf, sizeof tbuf);
		if (asprintf(&names[i], "%s: CPU %d tid %d", tbuf,
			     res_samples[i].cpu, res_samples[i].tid) < 0) {
			while (--i >= 0)
				zfree(&names[i]);
			free(names);
			return -1;
		}
	}
	choice = ui__popup_menu(num_res, names);
	for (i = 0; i < num_res; i++)
		zfree(&names[i]);
	free(names);

	if (choice < 0 || choice >= num_res)
		return -1;
	r = &res_samples[choice];

	n = timestamp__scnprintf_nsec(r->time - context_len, trange, sizeof trange);
	trange[n++] = ',';
	timestamp__scnprintf_nsec(r->time + context_len, trange + n, sizeof trange - n);

	timestamp__scnprintf_nsec(r->time, tsample, sizeof tsample);

	attr_to_script(extra_format, &evsel->attr);

	if (asprintf(&cmd, "%s script %s%s --time %s %s%s %s%s --ns %s %s %s %s %s | less +/%s",
		     perf,
		     input_name ? "-i " : "",
		     input_name ? input_name : "",
		     trange,
		     r->cpu >= 0 ? "--cpu " : "",
		     r->cpu >= 0 ? (sprintf(cpubuf, "%d", r->cpu), cpubuf) : "",
		     r->tid ? "--tid " : "",
		     r->tid ? (sprintf(tidbuf, "%d", r->tid), tidbuf) : "",
		     extra_format,
		     rstype == A_ASM ? "-F +insn --xed" :
		     rstype == A_SOURCE ? "-F +srcline,+srccode" : "",
		     symbol_conf.inline_name ? "--inline" : "",
		     "--show-lost-events ",
		     r->tid ? "--show-switch-events --show-task-events " : "",
		     tsample) < 0)
		return -1;
	run_script(cmd);
	free(cmd);
	return 0;
}
