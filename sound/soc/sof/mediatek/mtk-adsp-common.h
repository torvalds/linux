/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */

#ifndef __MEDIATEK_ADSP_COMMON_H__
#define __MEDIATEK_ADSP_COMMON_H__

#define EXCEPT_MAX_HDR_SIZE	0x400
#define MTK_ADSP_STACK_DUMP_SIZE 32

void mtk_adsp_dump(struct snd_sof_dev *sdev, u32 flags);
int mtk_adsp_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg);
void mtk_adsp_handle_reply(struct mtk_adsp_ipc *ipc);
void mtk_adsp_handle_request(struct mtk_adsp_ipc *ipc);
int mtk_adsp_get_bar_index(struct snd_sof_dev *sdev, u32 type);
int mtk_adsp_stream_pcm_hw_params(struct snd_sof_dev *sdev,
				  struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_sof_platform_stream_params *platform_params);
snd_pcm_uframes_t mtk_adsp_stream_pcm_pointer(struct snd_sof_dev *sdev,
					      struct snd_pcm_substream *substream);
#endif
