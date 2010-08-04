/*
 * Copyright 2005 Stephane Marchesin.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __NOUVEAU_DRM_H__
#define __NOUVEAU_DRM_H__

#define NOUVEAU_DRM_HEADER_PATCHLEVEL 16

struct drm_nouveau_channel_alloc {
	uint32_t     fb_ctxdma_handle;
	uint32_t     tt_ctxdma_handle;

	int          channel;
	uint32_t     pushbuf_domains;

	/* Notifier memory */
	uint32_t     notifier_handle;

	/* DRM-enforced subchannel assignments */
	struct {
		uint32_t handle;
		uint32_t grclass;
	} subchan[8];
	uint32_t nr_subchan;
};

struct drm_nouveau_channel_free {
	int channel;
};

struct drm_nouveau_grobj_alloc {
	int      channel;
	uint32_t handle;
	int      class;
};

struct drm_nouveau_notifierobj_alloc {
	uint32_t channel;
	uint32_t handle;
	uint32_t size;
	uint32_t offset;
};

struct drm_nouveau_gpuobj_free {
	int      channel;
	uint32_t handle;
};

/* FIXME : maybe unify {GET,SET}PARAMs */
#define NOUVEAU_GETPARAM_PCI_VENDOR      3
#define NOUVEAU_GETPARAM_PCI_DEVICE      4
#define NOUVEAU_GETPARAM_BUS_TYPE        5
#define NOUVEAU_GETPARAM_FB_PHYSICAL     6
#define NOUVEAU_GETPARAM_AGP_PHYSICAL    7
#define NOUVEAU_GETPARAM_FB_SIZE         8
#define NOUVEAU_GETPARAM_AGP_SIZE        9
#define NOUVEAU_GETPARAM_PCI_PHYSICAL    10
#define NOUVEAU_GETPARAM_CHIPSET_ID      11
#define NOUVEAU_GETPARAM_VM_VRAM_BASE    12
#define NOUVEAU_GETPARAM_GRAPH_UNITS     13
#define NOUVEAU_GETPARAM_PTIMER_TIME     14
struct drm_nouveau_getparam {
	uint64_t param;
	uint64_t value;
};

struct drm_nouveau_setparam {
	uint64_t param;
	uint64_t value;
};

#define NOUVEAU_GEM_DOMAIN_CPU       (1 << 0)
#define NOUVEAU_GEM_DOMAIN_VRAM      (1 << 1)
#define NOUVEAU_GEM_DOMAIN_GART      (1 << 2)
#define NOUVEAU_GEM_DOMAIN_MAPPABLE  (1 << 3)

struct drm_nouveau_gem_info {
	uint32_t handle;
	uint32_t domain;
	uint64_t size;
	uint64_t offset;
	uint64_t map_handle;
	uint32_t tile_mode;
	uint32_t tile_flags;
};

struct drm_nouveau_gem_new {
	struct drm_nouveau_gem_info info;
	uint32_t channel_hint;
	uint32_t align;
};

#define NOUVEAU_GEM_MAX_BUFFERS 1024
struct drm_nouveau_gem_pushbuf_bo_presumed {
	uint32_t valid;
	uint32_t domain;
	uint64_t offset;
};

struct drm_nouveau_gem_pushbuf_bo {
	uint64_t user_priv;
	uint32_t handle;
	uint32_t read_domains;
	uint32_t write_domains;
	uint32_t valid_domains;
	struct drm_nouveau_gem_pushbuf_bo_presumed presumed;
};

#define NOUVEAU_GEM_RELOC_LOW  (1 << 0)
#define NOUVEAU_GEM_RELOC_HIGH (1 << 1)
#define NOUVEAU_GEM_RELOC_OR   (1 << 2)
#define NOUVEAU_GEM_MAX_RELOCS 1024
struct drm_nouveau_gem_pushbuf_reloc {
	uint32_t reloc_bo_index;
	uint32_t reloc_bo_offset;
	uint32_t bo_index;
	uint32_t flags;
	uint32_t data;
	uint32_t vor;
	uint32_t tor;
};

#define NOUVEAU_GEM_MAX_PUSH 512
struct drm_nouveau_gem_pushbuf_push {
	uint32_t bo_index;
	uint32_t pad;
	uint64_t offset;
	uint64_t length;
};

struct drm_nouveau_gem_pushbuf {
	uint32_t channel;
	uint32_t nr_buffers;
	uint64_t buffers;
	uint32_t nr_relocs;
	uint32_t nr_push;
	uint64_t relocs;
	uint64_t push;
	uint32_t suffix0;
	uint32_t suffix1;
	uint64_t vram_available;
	uint64_t gart_available;
};

#define NOUVEAU_GEM_CPU_PREP_NOWAIT                                  0x00000001
#define NOUVEAU_GEM_CPU_PREP_NOBLOCK                                 0x00000002
#define NOUVEAU_GEM_CPU_PREP_WRITE                                   0x00000004
struct drm_nouveau_gem_cpu_prep {
	uint32_t handle;
	uint32_t flags;
};

struct drm_nouveau_gem_cpu_fini {
	uint32_t handle;
};

enum nouveau_bus_type {
	NV_AGP     = 0,
	NV_PCI     = 1,
	NV_PCIE    = 2,
};

struct drm_nouveau_sarea {
};

#define DRM_NOUVEAU_GETPARAM           0x00
#define DRM_NOUVEAU_SETPARAM           0x01
#define DRM_NOUVEAU_CHANNEL_ALLOC      0x02
#define DRM_NOUVEAU_CHANNEL_FREE       0x03
#define DRM_NOUVEAU_GROBJ_ALLOC        0x04
#define DRM_NOUVEAU_NOTIFIEROBJ_ALLOC  0x05
#define DRM_NOUVEAU_GPUOBJ_FREE        0x06
#define DRM_NOUVEAU_GEM_NEW            0x40
#define DRM_NOUVEAU_GEM_PUSHBUF        0x41
#define DRM_NOUVEAU_GEM_CPU_PREP       0x42
#define DRM_NOUVEAU_GEM_CPU_FINI       0x43
#define DRM_NOUVEAU_GEM_INFO           0x44

#endif /* __NOUVEAU_DRM_H__ */
