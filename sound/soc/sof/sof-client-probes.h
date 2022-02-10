/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOF_CLIENT_PROBES_H
#define __SOF_CLIENT_PROBES_H

struct snd_compr_stream;
struct snd_compr_tstamp;
struct snd_compr_params;
struct sof_client_dev;
struct snd_soc_dai;

/*
 * Callbacks used on platforms where the control for audio is split between
 * DSP and host, like HDA.
 */
struct sof_probes_host_ops {
	int (*assign)(struct sof_client_dev *cdev, struct snd_compr_stream *cstream,
		      struct snd_soc_dai *dai, u32 *stream_id);
	int (*free)(struct sof_client_dev *cdev, struct snd_compr_stream *cstream,
		    struct snd_soc_dai *dai);
	int (*set_params)(struct sof_client_dev *cdev, struct snd_compr_stream *cstream,
			  struct snd_compr_params *params,
			  struct snd_soc_dai *dai);
	int (*trigger)(struct sof_client_dev *cdev, struct snd_compr_stream *cstream,
		       int cmd, struct snd_soc_dai *dai);
	int (*pointer)(struct sof_client_dev *cdev, struct snd_compr_stream *cstream,
		       struct snd_compr_tstamp *tstamp,
		       struct snd_soc_dai *dai);
};

#endif
