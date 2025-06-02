// SPDX-License-Identifier: GPL-2.0
#ifdef HAVE_BPF_SKEL
#include "timerlat.h"
#include "timerlat_bpf.h"
#include "timerlat.skel.h"

static struct timerlat_bpf *bpf;

/*
 * timerlat_bpf_init - load and initialize BPF program to collect timerlat data
 */
int timerlat_bpf_init(struct timerlat_params *params)
{
	int err;

	debug_msg("Loading BPF program\n");

	bpf = timerlat_bpf__open();
	if (!bpf)
		return 1;

	/* Pass common options */
	bpf->rodata->output_divisor = params->output_divisor;
	bpf->rodata->entries = params->entries;
	bpf->rodata->irq_threshold = params->stop_us;
	bpf->rodata->thread_threshold = params->stop_total_us;
	bpf->rodata->aa_only = params->aa_only;

	if (params->entries != 0) {
		/* Pass histogram options */
		bpf->rodata->bucket_size = params->bucket_size;

		/* Set histogram array sizes */
		bpf_map__set_max_entries(bpf->maps.hist_irq, params->entries);
		bpf_map__set_max_entries(bpf->maps.hist_thread, params->entries);
		bpf_map__set_max_entries(bpf->maps.hist_user, params->entries);
	} else {
		/* No entries, disable histogram */
		bpf_map__set_autocreate(bpf->maps.hist_irq, false);
		bpf_map__set_autocreate(bpf->maps.hist_thread, false);
		bpf_map__set_autocreate(bpf->maps.hist_user, false);
	}

	if (params->aa_only) {
		/* Auto-analysis only, disable summary */
		bpf_map__set_autocreate(bpf->maps.summary_irq, false);
		bpf_map__set_autocreate(bpf->maps.summary_thread, false);
		bpf_map__set_autocreate(bpf->maps.summary_user, false);
	}

	/* Load and verify BPF program */
	err = timerlat_bpf__load(bpf);
	if (err) {
		timerlat_bpf__destroy(bpf);
		return err;
	}

	return 0;
}

/*
 * timerlat_bpf_attach - attach BPF program to collect timerlat data
 */
int timerlat_bpf_attach(void)
{
	debug_msg("Attaching BPF program\n");

	return timerlat_bpf__attach(bpf);
}

/*
 * timerlat_bpf_detach - detach BPF program to collect timerlat data
 */
void timerlat_bpf_detach(void)
{
	timerlat_bpf__detach(bpf);
}

/*
 * timerlat_bpf_detach - destroy BPF program to collect timerlat data
 */
void timerlat_bpf_destroy(void)
{
	timerlat_bpf__destroy(bpf);
}

static int handle_rb_event(void *ctx, void *data, size_t data_sz)
{
	return 0;
}

/*
 * timerlat_bpf_wait - wait until tracing is stopped or signal
 */
int timerlat_bpf_wait(int timeout)
{
	struct ring_buffer *rb;
	int retval;

	rb = ring_buffer__new(bpf_map__fd(bpf->maps.signal_stop_tracing),
			      handle_rb_event, NULL, NULL);
	retval = ring_buffer__poll(rb, timeout * 1000);
	ring_buffer__free(rb);

	return retval;
}

static int get_value(struct bpf_map *map_irq,
		     struct bpf_map *map_thread,
		     struct bpf_map *map_user,
		     int key,
		     long long *value_irq,
		     long long *value_thread,
		     long long *value_user,
		     int cpus)
{
	int err;

	err = bpf_map__lookup_elem(map_irq, &key,
				   sizeof(unsigned int), value_irq,
				   sizeof(long long) * cpus, 0);
	if (err)
		return err;
	err = bpf_map__lookup_elem(map_thread, &key,
				   sizeof(unsigned int), value_thread,
				   sizeof(long long) * cpus, 0);
	if (err)
		return err;
	err = bpf_map__lookup_elem(map_user, &key,
				   sizeof(unsigned int), value_user,
				   sizeof(long long) * cpus, 0);
	if (err)
		return err;
	return 0;
}

/*
 * timerlat_bpf_get_hist_value - get value from BPF hist map
 */
int timerlat_bpf_get_hist_value(int key,
				long long *value_irq,
				long long *value_thread,
				long long *value_user,
				int cpus)
{
	return get_value(bpf->maps.hist_irq,
			 bpf->maps.hist_thread,
			 bpf->maps.hist_user,
			 key, value_irq, value_thread, value_user, cpus);
}

/*
 * timerlat_bpf_get_summary_value - get value from BPF summary map
 */
int timerlat_bpf_get_summary_value(enum summary_field key,
				   long long *value_irq,
				   long long *value_thread,
				   long long *value_user,
				   int cpus)
{
	return get_value(bpf->maps.summary_irq,
			 bpf->maps.summary_thread,
			 bpf->maps.summary_user,
			 key, value_irq, value_thread, value_user, cpus);
}
#endif /* HAVE_BPF_SKEL */
