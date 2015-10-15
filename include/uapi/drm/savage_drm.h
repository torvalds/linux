/* savage_drm.h -- Public header for the savage driver
 *
 * Copyright 2004  Felix Kuehling
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL FELIX KUEHLING BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __SAVAGE_DRM_H__
#define __SAVAGE_DRM_H__

#include <drm/drm.h>

#ifndef __SAVAGE_SAREA_DEFINES__
#define __SAVAGE_SAREA_DEFINES__

/* 2 heaps (1 for card, 1 for agp), each divided into up to 128
 * regions, subject to a minimum region size of (1<<16) == 64k.
 *
 * Clients may subdivide regions internally, but when sharing between
 * clients, the region size is the minimum granularity.
 */

#define SAVAGE_CARD_HEAP		0
#define SAVAGE_AGP_HEAP			1
#define SAVAGE_NR_TEX_HEAPS		2
#define SAVAGE_NR_TEX_REGIONS		16
#define SAVAGE_LOG_MIN_TEX_REGION_SIZE	16

#endif				/* __SAVAGE_SAREA_DEFINES__ */

typedef struct _drm_savage_sarea {
	/* LRU lists for texture memory in agp space and on the card.
	 */
	struct drm_tex_region texList[SAVAGE_NR_TEX_HEAPS][SAVAGE_NR_TEX_REGIONS +
						      1];
	unsigned int texAge[SAVAGE_NR_TEX_HEAPS];

	/* Mechanism to validate card state.
	 */
	int ctxOwner;
} drm_savage_sarea_t, *drm_savage_sarea_ptr;

/* Savage-specific ioctls
 */
#define DRM_SAVAGE_BCI_INIT		0x00
#define DRM_SAVAGE_BCI_CMDBUF           0x01
#define DRM_SAVAGE_BCI_EVENT_EMIT	0x02
#define DRM_SAVAGE_BCI_EVENT_WAIT	0x03

#define DRM_IOCTL_SAVAGE_BCI_INIT		DRM_IOW( DRM_COMMAND_BASE + DRM_SAVAGE_BCI_INIT, drm_savage_init_t)
#define DRM_IOCTL_SAVAGE_BCI_CMDBUF		DRM_IOW( DRM_COMMAND_BASE + DRM_SAVAGE_BCI_CMDBUF, drm_savage_cmdbuf_t)
#define DRM_IOCTL_SAVAGE_BCI_EVENT_EMIT	DRM_IOWR(DRM_COMMAND_BASE + DRM_SAVAGE_BCI_EVENT_EMIT, drm_savage_event_emit_t)
#define DRM_IOCTL_SAVAGE_BCI_EVENT_WAIT	DRM_IOW( DRM_COMMAND_BASE + DRM_SAVAGE_BCI_EVENT_WAIT, drm_savage_event_wait_t)

#define SAVAGE_DMA_PCI	1
#define SAVAGE_DMA_AGP	3
typedef struct drm_savage_init {
	enum {
		SAVAGE_INIT_BCI = 1,
		SAVAGE_CLEANUP_BCI = 2
	} func;
	unsigned int sarea_priv_offset;

	/* some parameters */
	unsigned int cob_size;
	unsigned int bci_threshold_lo, bci_threshold_hi;
	unsigned int dma_type;

	/* frame buffer layout */
	unsigned int fb_bpp;
	unsigned int front_offset, front_pitch;
	unsigned int back_offset, back_pitch;
	unsigned int depth_bpp;
	unsigned int depth_offset, depth_pitch;

	/* local textures */
	unsigned int texture_offset;
	unsigned int texture_size;

	/* physical locations of non-permanent maps */
	unsigned long status_offset;
	unsigned long buffers_offset;
	unsigned long agp_textures_offset;
	unsigned long cmd_dma_offset;
} drm_savage_init_t;

typedef union drm_savage_cmd_header drm_savage_cmd_header_t;
typedef struct drm_savage_cmdbuf {
	/* command buffer in client's address space */
	drm_savage_cmd_header_t __user *cmd_addr;
	unsigned int size;	/* size of the command buffer in 64bit units */

	unsigned int dma_idx;	/* DMA buffer index to use */
	int discard;		/* discard DMA buffer when done */
	/* vertex buffer in client's address space */
	unsigned int __user *vb_addr;
	unsigned int vb_size;	/* size of client vertex buffer in bytes */
	unsigned int vb_stride;	/* stride of vertices in 32bit words */
	/* boxes in client's address space */
	struct drm_clip_rect __user *box_addr;
	unsigned int nbox;	/* number of clipping boxes */
} drm_savage_cmdbuf_t;

#define SAVAGE_WAIT_2D  0x1	/* wait for 2D idle before updating event tag */
#define SAVAGE_WAIT_3D  0x2	/* wait for 3D idle before updating event tag */
#define SAVAGE_WAIT_IRQ 0x4	/* emit or wait for IRQ, not implemented yet */
typedef struct drm_savage_event {
	unsigned int count;
	unsigned int flags;
} drm_savage_event_emit_t, drm_savage_event_wait_t;

/* Commands for the cmdbuf ioctl
 */
#define SAVAGE_CMD_STATE	0	/* a range of state registers */
#define SAVAGE_CMD_DMA_PRIM	1	/* vertices from DMA buffer */
#define SAVAGE_CMD_VB_PRIM	2	/* vertices from client vertex buffer */
#define SAVAGE_CMD_DMA_IDX	3	/* indexed vertices from DMA buffer */
#define SAVAGE_CMD_VB_IDX	4	/* indexed vertices client vertex buffer */
#define SAVAGE_CMD_CLEAR	5	/* clear buffers */
#define SAVAGE_CMD_SWAP		6	/* swap buffers */

/* Primitive types
*/
#define SAVAGE_PRIM_TRILIST	0	/* triangle list */
#define SAVAGE_PRIM_TRISTRIP	1	/* triangle strip */
#define SAVAGE_PRIM_TRIFAN	2	/* triangle fan */
#define SAVAGE_PRIM_TRILIST_201	3	/* reorder verts for correct flat
					 * shading on s3d */

/* Skip flags (vertex format)
 */
#define SAVAGE_SKIP_Z		0x01
#define SAVAGE_SKIP_W		0x02
#define SAVAGE_SKIP_C0		0x04
#define SAVAGE_SKIP_C1		0x08
#define SAVAGE_SKIP_S0		0x10
#define SAVAGE_SKIP_T0		0x20
#define SAVAGE_SKIP_ST0		0x30
#define SAVAGE_SKIP_S1		0x40
#define SAVAGE_SKIP_T1		0x80
#define SAVAGE_SKIP_ST1		0xc0
#define SAVAGE_SKIP_ALL_S3D	0x3f
#define SAVAGE_SKIP_ALL_S4	0xff

/* Buffer names for clear command
 */
#define SAVAGE_FRONT		0x1
#define SAVAGE_BACK		0x2
#define SAVAGE_DEPTH		0x4

/* 64-bit command header
 */
union drm_savage_cmd_header {
	struct {
		unsigned char cmd;	/* command */
		unsigned char pad0;
		unsigned short pad1;
		unsigned short pad2;
		unsigned short pad3;
	} cmd;			/* generic */
	struct {
		unsigned char cmd;
		unsigned char global;	/* need idle engine? */
		unsigned short count;	/* number of consecutive registers */
		unsigned short start;	/* first register */
		unsigned short pad3;
	} state;		/* SAVAGE_CMD_STATE */
	struct {
		unsigned char cmd;
		unsigned char prim;	/* primitive type */
		unsigned short skip;	/* vertex format (skip flags) */
		unsigned short count;	/* number of vertices */
		unsigned short start;	/* first vertex in DMA/vertex buffer */
	} prim;			/* SAVAGE_CMD_DMA_PRIM, SAVAGE_CMD_VB_PRIM */
	struct {
		unsigned char cmd;
		unsigned char prim;
		unsigned short skip;
		unsigned short count;	/* number of indices that follow */
		unsigned short pad3;
	} idx;			/* SAVAGE_CMD_DMA_IDX, SAVAGE_CMD_VB_IDX */
	struct {
		unsigned char cmd;
		unsigned char pad0;
		unsigned short pad1;
		unsigned int flags;
	} clear0;		/* SAVAGE_CMD_CLEAR */
	struct {
		unsigned int mask;
		unsigned int value;
	} clear1;		/* SAVAGE_CMD_CLEAR data */
};

#endif
