/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __Q6_ASM_H__
#define __Q6_ASM_H__

#define MAX_SESSIONS	8

typedef void (*q6asm_cb) (uint32_t opcode, uint32_t token,
			  void *payload, void *priv);
struct audio_client;
struct audio_client *q6asm_audio_client_alloc(struct device *dev,
					      q6asm_cb cb, void *priv,
					      int session_id, int perf_mode);
void q6asm_audio_client_free(struct audio_client *ac);
int q6asm_get_session_id(struct audio_client *ac);
int q6asm_map_memory_regions(unsigned int dir,
			     struct audio_client *ac,
			     phys_addr_t phys,
			     size_t bufsz, unsigned int bufcnt);
int q6asm_unmap_memory_regions(unsigned int dir, struct audio_client *ac);
#endif /* __Q6_ASM_H__ */
