#ifndef _I810_DRM_H_
#define _I810_DRM_H_

#include <drm/drm.h>

/* WARNING: These defines must be the same as what the Xserver uses.
 * if you change them, you must change the defines in the Xserver.
 */

#ifndef _I810_DEFINES_
#define _I810_DEFINES_

#define I810_DMA_BUF_ORDER		12
#define I810_DMA_BUF_SZ 		(1<<I810_DMA_BUF_ORDER)
#define I810_DMA_BUF_NR 		256
#define I810_NR_SAREA_CLIPRECTS 	8

/* Each region is a minimum of 64k, and there are at most 64 of them.
 */
#define I810_NR_TEX_REGIONS 64
#define I810_LOG_MIN_TEX_REGION_SIZE 16
#endif

#define I810_UPLOAD_TEX0IMAGE  0x1	/* handled clientside */
#define I810_UPLOAD_TEX1IMAGE  0x2	/* handled clientside */
#define I810_UPLOAD_CTX        0x4
#define I810_UPLOAD_BUFFERS    0x8
#define I810_UPLOAD_TEX0       0x10
#define I810_UPLOAD_TEX1       0x20
#define I810_UPLOAD_CLIPRECTS  0x40

/* Indices into buf.Setup where various bits of state are mirrored per
 * context and per buffer.  These can be fired at the card as a unit,
 * or in a piecewise fashion as required.
 */

/* Destbuffer state
 *    - backbuffer linear offset and pitch -- invarient in the current dri
 *    - zbuffer linear offset and pitch -- also invarient
 *    - drawing origin in back and depth buffers.
 *
 * Keep the depth/back buffer state here to accommodate private buffers
 * in the future.
 */
#define I810_DESTREG_DI0  0	/* CMD_OP_DESTBUFFER_INFO (2 dwords) */
#define I810_DESTREG_DI1  1
#define I810_DESTREG_DV0  2	/* GFX_OP_DESTBUFFER_VARS (2 dwords) */
#define I810_DESTREG_DV1  3
#define I810_DESTREG_DR0  4	/* GFX_OP_DRAWRECT_INFO (4 dwords) */
#define I810_DESTREG_DR1  5
#define I810_DESTREG_DR2  6
#define I810_DESTREG_DR3  7
#define I810_DESTREG_DR4  8
#define I810_DEST_SETUP_SIZE 10

/* Context state
 */
#define I810_CTXREG_CF0   0	/* GFX_OP_COLOR_FACTOR */
#define I810_CTXREG_CF1   1
#define I810_CTXREG_ST0   2	/* GFX_OP_STIPPLE */
#define I810_CTXREG_ST1   3
#define I810_CTXREG_VF    4	/* GFX_OP_VERTEX_FMT */
#define I810_CTXREG_MT    5	/* GFX_OP_MAP_TEXELS */
#define I810_CTXREG_MC0   6	/* GFX_OP_MAP_COLOR_STAGES - stage 0 */
#define I810_CTXREG_MC1   7	/* GFX_OP_MAP_COLOR_STAGES - stage 1 */
#define I810_CTXREG_MC2   8	/* GFX_OP_MAP_COLOR_STAGES - stage 2 */
#define I810_CTXREG_MA0   9	/* GFX_OP_MAP_ALPHA_STAGES - stage 0 */
#define I810_CTXREG_MA1   10	/* GFX_OP_MAP_ALPHA_STAGES - stage 1 */
#define I810_CTXREG_MA2   11	/* GFX_OP_MAP_ALPHA_STAGES - stage 2 */
#define I810_CTXREG_SDM   12	/* GFX_OP_SRC_DEST_MONO */
#define I810_CTXREG_FOG   13	/* GFX_OP_FOG_COLOR */
#define I810_CTXREG_B1    14	/* GFX_OP_BOOL_1 */
#define I810_CTXREG_B2    15	/* GFX_OP_BOOL_2 */
#define I810_CTXREG_LCS   16	/* GFX_OP_LINEWIDTH_CULL_SHADE_MODE */
#define I810_CTXREG_PV    17	/* GFX_OP_PV_RULE -- Invarient! */
#define I810_CTXREG_ZA    18	/* GFX_OP_ZBIAS_ALPHAFUNC */
#define I810_CTXREG_AA    19	/* GFX_OP_ANTIALIAS */
#define I810_CTX_SETUP_SIZE 20

/* Texture state (per tex unit)
 */
#define I810_TEXREG_MI0  0	/* GFX_OP_MAP_INFO (4 dwords) */
#define I810_TEXREG_MI1  1
#define I810_TEXREG_MI2  2
#define I810_TEXREG_MI3  3
#define I810_TEXREG_MF   4	/* GFX_OP_MAP_FILTER */
#define I810_TEXREG_MLC  5	/* GFX_OP_MAP_LOD_CTL */
#define I810_TEXREG_MLL  6	/* GFX_OP_MAP_LOD_LIMITS */
#define I810_TEXREG_MCS  7	/* GFX_OP_MAP_COORD_SETS ??? */
#define I810_TEX_SETUP_SIZE 8

/* Flags for clear ioctl
 */
#define I810_FRONT   0x1
#define I810_BACK    0x2
#define I810_DEPTH   0x4

typedef enum _drm_i810_init_func {
	I810_INIT_DMA = 0x01,
	I810_CLEANUP_DMA = 0x02,
	I810_INIT_DMA_1_4 = 0x03
} drm_i810_init_func_t;

/* This is the init structure after v1.2 */
typedef struct _drm_i810_init {
	drm_i810_init_func_t func;
	unsigned int mmio_offset;
	unsigned int buffers_offset;
	int sarea_priv_offset;
	unsigned int ring_start;
	unsigned int ring_end;
	unsigned int ring_size;
	unsigned int front_offset;
	unsigned int back_offset;
	unsigned int depth_offset;
	unsigned int overlay_offset;
	unsigned int overlay_physical;
	unsigned int w;
	unsigned int h;
	unsigned int pitch;
	unsigned int pitch_bits;
} drm_i810_init_t;

/* This is the init structure prior to v1.2 */
typedef struct _drm_i810_pre12_init {
	drm_i810_init_func_t func;
	unsigned int mmio_offset;
	unsigned int buffers_offset;
	int sarea_priv_offset;
	unsigned int ring_start;
	unsigned int ring_end;
	unsigned int ring_size;
	unsigned int front_offset;
	unsigned int back_offset;
	unsigned int depth_offset;
	unsigned int w;
	unsigned int h;
	unsigned int pitch;
	unsigned int pitch_bits;
} drm_i810_pre12_init_t;

/* Warning: If you change the SAREA structure you must change the Xserver
 * structure as well */

typedef struct _drm_i810_tex_region {
	unsigned char next, prev;	/* indices to form a circular LRU  */
	unsigned char in_use;	/* owned by a client, or free? */
	int age;		/* tracked by clients to update local LRU's */
} drm_i810_tex_region_t;

typedef struct _drm_i810_sarea {
	unsigned int ContextState[I810_CTX_SETUP_SIZE];
	unsigned int BufferState[I810_DEST_SETUP_SIZE];
	unsigned int TexState[2][I810_TEX_SETUP_SIZE];
	unsigned int dirty;

	unsigned int nbox;
	struct drm_clip_rect boxes[I810_NR_SAREA_CLIPRECTS];

	/* Maintain an LRU of contiguous regions of texture space.  If
	 * you think you own a region of texture memory, and it has an
	 * age different to the one you set, then you are mistaken and
	 * it has been stolen by another client.  If global texAge
	 * hasn't changed, there is no need to walk the list.
	 *
	 * These regions can be used as a proxy for the fine-grained
	 * texture information of other clients - by maintaining them
	 * in the same lru which is used to age their own textures,
	 * clients have an approximate lru for the whole of global
	 * texture space, and can make informed decisions as to which
	 * areas to kick out.  There is no need to choose whether to
	 * kick out your own texture or someone else's - simply eject
	 * them all in LRU order.
	 */

	drm_i810_tex_region_t texList[I810_NR_TEX_REGIONS + 1];
	/* Last elt is sentinal */
	int texAge;		/* last time texture was uploaded */
	int last_enqueue;	/* last time a buffer was enqueued */
	int last_dispatch;	/* age of the most recently dispatched buffer */
	int last_quiescent;	/*  */
	int ctxOwner;		/* last context to upload state */

	int vertex_prim;

	int pf_enabled;		/* is pageflipping allowed? */
	int pf_active;
	int pf_current_page;	/* which buffer is being displayed? */
} drm_i810_sarea_t;

/* WARNING: If you change any of these defines, make sure to change the
 * defines in the Xserver file (xf86drmMga.h)
 */

/* i810 specific ioctls
 * The device specific ioctl range is 0x40 to 0x79.
 */
#define DRM_I810_INIT		0x00
#define DRM_I810_VERTEX		0x01
#define DRM_I810_CLEAR		0x02
#define DRM_I810_FLUSH		0x03
#define DRM_I810_GETAGE		0x04
#define DRM_I810_GETBUF		0x05
#define DRM_I810_SWAP		0x06
#define DRM_I810_COPY		0x07
#define DRM_I810_DOCOPY		0x08
#define DRM_I810_OV0INFO	0x09
#define DRM_I810_FSTATUS	0x0a
#define DRM_I810_OV0FLIP	0x0b
#define DRM_I810_MC		0x0c
#define DRM_I810_RSTATUS	0x0d
#define DRM_I810_FLIP		0x0e

#define DRM_IOCTL_I810_INIT		DRM_IOW( DRM_COMMAND_BASE + DRM_I810_INIT, drm_i810_init_t)
#define DRM_IOCTL_I810_VERTEX		DRM_IOW( DRM_COMMAND_BASE + DRM_I810_VERTEX, drm_i810_vertex_t)
#define DRM_IOCTL_I810_CLEAR		DRM_IOW( DRM_COMMAND_BASE + DRM_I810_CLEAR, drm_i810_clear_t)
#define DRM_IOCTL_I810_FLUSH		DRM_IO(  DRM_COMMAND_BASE + DRM_I810_FLUSH)
#define DRM_IOCTL_I810_GETAGE		DRM_IO(  DRM_COMMAND_BASE + DRM_I810_GETAGE)
#define DRM_IOCTL_I810_GETBUF		DRM_IOWR(DRM_COMMAND_BASE + DRM_I810_GETBUF, drm_i810_dma_t)
#define DRM_IOCTL_I810_SWAP		DRM_IO(  DRM_COMMAND_BASE + DRM_I810_SWAP)
#define DRM_IOCTL_I810_COPY		DRM_IOW( DRM_COMMAND_BASE + DRM_I810_COPY, drm_i810_copy_t)
#define DRM_IOCTL_I810_DOCOPY		DRM_IO(  DRM_COMMAND_BASE + DRM_I810_DOCOPY)
#define DRM_IOCTL_I810_OV0INFO		DRM_IOR( DRM_COMMAND_BASE + DRM_I810_OV0INFO, drm_i810_overlay_t)
#define DRM_IOCTL_I810_FSTATUS		DRM_IO ( DRM_COMMAND_BASE + DRM_I810_FSTATUS)
#define DRM_IOCTL_I810_OV0FLIP		DRM_IO ( DRM_COMMAND_BASE + DRM_I810_OV0FLIP)
#define DRM_IOCTL_I810_MC		DRM_IOW( DRM_COMMAND_BASE + DRM_I810_MC, drm_i810_mc_t)
#define DRM_IOCTL_I810_RSTATUS		DRM_IO ( DRM_COMMAND_BASE + DRM_I810_RSTATUS)
#define DRM_IOCTL_I810_FLIP             DRM_IO ( DRM_COMMAND_BASE + DRM_I810_FLIP)

typedef struct _drm_i810_clear {
	int clear_color;
	int clear_depth;
	int flags;
} drm_i810_clear_t;

/* These may be placeholders if we have more cliprects than
 * I810_NR_SAREA_CLIPRECTS.  In that case, the client sets discard to
 * false, indicating that the buffer will be dispatched again with a
 * new set of cliprects.
 */
typedef struct _drm_i810_vertex {
	int idx;		/* buffer index */
	int used;		/* nr bytes in use */
	int discard;		/* client is finished with the buffer? */
} drm_i810_vertex_t;

typedef struct _drm_i810_copy_t {
	int idx;		/* buffer index */
	int used;		/* nr bytes in use */
	void *address;		/* Address to copy from */
} drm_i810_copy_t;

#define PR_TRIANGLES         (0x0<<18)
#define PR_TRISTRIP_0        (0x1<<18)
#define PR_TRISTRIP_1        (0x2<<18)
#define PR_TRIFAN            (0x3<<18)
#define PR_POLYGON           (0x4<<18)
#define PR_LINES             (0x5<<18)
#define PR_LINESTRIP         (0x6<<18)
#define PR_RECTS             (0x7<<18)
#define PR_MASK              (0x7<<18)

typedef struct drm_i810_dma {
	void *virtual;
	int request_idx;
	int request_size;
	int granted;
} drm_i810_dma_t;

typedef struct _drm_i810_overlay_t {
	unsigned int offset;	/* Address of the Overlay Regs */
	unsigned int physical;
} drm_i810_overlay_t;

typedef struct _drm_i810_mc {
	int idx;		/* buffer index */
	int used;		/* nr bytes in use */
	int num_blocks;		/* number of GFXBlocks */
	int *length;		/* List of lengths for GFXBlocks (FUTURE) */
	unsigned int last_render;	/* Last Render Request */
} drm_i810_mc_t;

#endif				/* _I810_DRM_H_ */
