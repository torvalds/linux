/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOF_CLIENT_PROBES_H
#define __SOF_CLIENT_PROBES_H

struct snd_compr_stream;
struct snd_compr_tstamp64;
struct snd_compr_params;
struct sof_client_dev;
struct snd_soc_dai;

/*
 * Callbacks used on platforms where the control for audio is split between
 * DSP and host, like HDA.
 */
struct sof_probes_host_ops {
	int (*startup)(struct sof_client_dev *cdev, struct snd_compr_stream *cstream,
		       struct snd_soc_dai *dai, u32 *stream_id);
	int (*shutdown)(struct sof_client_dev *cdev, struct snd_compr_stream *cstream,
			struct snd_soc_dai *dai);
	int (*set_params)(struct sof_client_dev *cdev, struct snd_compr_stream *cstream,
			  struct snd_compr_params *params,
			  struct snd_soc_dai *dai);
	int (*trigger)(struct sof_client_dev *cdev, struct snd_compr_stream *cstream,
		       int cmd, struct snd_soc_dai *dai);
	int (*pointer)(struct sof_client_dev *cdev, struct snd_compr_stream *cstream,
		       struct snd_compr_tstamp64 *tstamp,
		       struct snd_soc_dai *dai);
};

struct sof_probe_point_desc {
	unsigned int buffer_id;
	unsigned int purpose;
	unsigned int stream_tag;
} __packed;

enum sof_probe_info_type {
	PROBES_INFO_ACTIVE_PROBES,
	PROBES_INFO_AVAILABE_PROBES,
};

struct sof_probes_ipc_ops {
	int (*init)(struct sof_client_dev *cdev, u32 stream_tag,
		    size_t buffer_size);
	int (*deinit)(struct sof_client_dev *cdev);
	int (*points_info)(struct sof_client_dev *cdev,
			   struct sof_probe_point_desc **desc,
			   size_t *num_desc, enum sof_probe_info_type type);
	int (*point_print)(struct sof_client_dev *cdev, char *buf, size_t size,
			   struct sof_probe_point_desc *desc);
	int (*points_add)(struct sof_client_dev *cdev,
			  struct sof_probe_point_desc *desc,
			  size_t num_desc);
	int (*points_remove)(struct sof_client_dev *cdev,
			     unsigned int *buffer_id, size_t num_buffer_id);
};

extern const struct sof_probes_ipc_ops ipc3_probe_ops;
extern const struct sof_probes_ipc_ops ipc4_probe_ops;

struct sof_probes_priv {
	struct dentry *dfs_points;
	struct dentry *dfs_points_remove;
	u32 extractor_stream_tag;
	struct snd_soc_card card;
	void *ipc_priv;

	const struct sof_probes_host_ops *host_ops;
	const struct sof_probes_ipc_ops *ipc_ops;
};

#endif
