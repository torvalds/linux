#ifndef _I830_DRM_H_
#define _I830_DRM_H_

/* WARNING: These defines must be the same as what the Xserver uses.
 * if you change them, you must change the defines in the Xserver.
 *
 * KW: Actually, you can't ever change them because doing so would
 * break backwards compatibility.
 */

#ifndef _I830_DEFINES_
#define _I830_DEFINES_

#define I830_DMA_BUF_ORDER		12
#define I830_DMA_BUF_SZ			(1<<I830_DMA_BUF_ORDER)
#define I830_DMA_BUF_NR			256
#define I830_NR_SAREA_CLIPRECTS		8

/* Each region is a minimum of 64k, and there are at most 64 of them.
 */
#define I830_NR_TEX_REGIONS 64
#define I830_LOG_MIN_TEX_REGION_SIZE 16

/* KW: These aren't correct but someone set them to two and then
 * released the module.  Now we can't change them as doing so would
 * break backwards compatibility.
 */
#define I830_TEXTURE_COUNT	2
#define I830_TEXBLEND_COUNT	I830_TEXTURE_COUNT

#define I830_TEXBLEND_SIZE	12	/* (4 args + op) * 2 + COLOR_FACTOR */

#define I830_UPLOAD_CTX			0x1
#define I830_UPLOAD_BUFFERS		0x2
#define I830_UPLOAD_CLIPRECTS		0x4
#define I830_UPLOAD_TEX0_IMAGE		0x100	/* handled clientside */
#define I830_UPLOAD_TEX0_CUBE		0x200	/* handled clientside */
#define I830_UPLOAD_TEX1_IMAGE		0x400	/* handled clientside */
#define I830_UPLOAD_TEX1_CUBE		0x800	/* handled clientside */
#define I830_UPLOAD_TEX2_IMAGE		0x1000	/* handled clientside */
#define I830_UPLOAD_TEX2_CUBE		0x2000	/* handled clientside */
#define I830_UPLOAD_TEX3_IMAGE		0x4000	/* handled clientside */
#define I830_UPLOAD_TEX3_CUBE		0x8000	/* handled clientside */
#define I830_UPLOAD_TEX_N_IMAGE(n)	(0x100 << (n * 2))
#define I830_UPLOAD_TEX_N_CUBE(n)	(0x200 << (n * 2))
#define I830_UPLOAD_TEXIMAGE_MASK	0xff00
#define I830_UPLOAD_TEX0			0x10000
#define I830_UPLOAD_TEX1			0x20000
#define I830_UPLOAD_TEX2			0x40000
#define I830_UPLOAD_TEX3			0x80000
#define I830_UPLOAD_TEX_N(n)		(0x10000 << (n))
#define I830_UPLOAD_TEX_MASK		0xf0000
#define I830_UPLOAD_TEXBLEND0		0x100000
#define I830_UPLOAD_TEXBLEND1		0x200000
#define I830_UPLOAD_TEXBLEND2		0x400000
#define I830_UPLOAD_TEXBLEND3		0x800000
#define I830_UPLOAD_TEXBLEND_N(n)	(0x100000 << (n))
#define I830_UPLOAD_TEXBLEND_MASK	0xf00000
#define I830_UPLOAD_TEX_PALETTE_N(n)    (0x1000000 << (n))
#define I830_UPLOAD_TEX_PALETTE_SHARED	0x4000000
#define I830_UPLOAD_STIPPLE		0x8000000

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

#define I830_DESTREG_CBUFADDR 0
#define I830_DESTREG_DBUFADDR 1
#define I830_DESTREG_DV0 2
#define I830_DESTREG_DV1 3
#define I830_DESTREG_SENABLE 4
#define I830_DESTREG_SR0 5
#define I830_DESTREG_SR1 6
#define I830_DESTREG_SR2 7
#define I830_DESTREG_DR0 8
#define I830_DESTREG_DR1 9
#define I830_DESTREG_DR2 10
#define I830_DESTREG_DR3 11
#define I830_DESTREG_DR4 12
#define I830_DEST_SETUP_SIZE 13

/* Context state
 */
#define I830_CTXREG_STATE1		0
#define I830_CTXREG_STATE2		1
#define I830_CTXREG_STATE3		2
#define I830_CTXREG_STATE4		3
#define I830_CTXREG_STATE5		4
#define I830_CTXREG_IALPHAB		5
#define I830_CTXREG_STENCILTST		6
#define I830_CTXREG_ENABLES_1		7
#define I830_CTXREG_ENABLES_2		8
#define I830_CTXREG_AA			9
#define I830_CTXREG_FOGCOLOR		10
#define I830_CTXREG_BLENDCOLR0		11
#define I830_CTXREG_BLENDCOLR		12	/* Dword 1 of 2 dword command */
#define I830_CTXREG_VF			13
#define I830_CTXREG_VF2			14
#define I830_CTXREG_MCSB0		15
#define I830_CTXREG_MCSB1		16
#define I830_CTX_SETUP_SIZE		17

/* 1.3: Stipple state
 */
#define I830_STPREG_ST0 0
#define I830_STPREG_ST1 1
#define I830_STP_SETUP_SIZE 2

/* Texture state (per tex unit)
 */

#define I830_TEXREG_MI0	0	/* GFX_OP_MAP_INFO (6 dwords) */
#define I830_TEXREG_MI1	1
#define I830_TEXREG_MI2	2
#define I830_TEXREG_MI3	3
#define I830_TEXREG_MI4	4
#define I830_TEXREG_MI5	5
#define I830_TEXREG_MF	6	/* GFX_OP_MAP_FILTER */
#define I830_TEXREG_MLC	7	/* GFX_OP_MAP_LOD_CTL */
#define I830_TEXREG_MLL	8	/* GFX_OP_MAP_LOD_LIMITS */
#define I830_TEXREG_MCS	9	/* GFX_OP_MAP_COORD_SETS */
#define I830_TEX_SETUP_SIZE 10

#define I830_TEXREG_TM0LI      0	/* load immediate 2 texture map n */
#define I830_TEXREG_TM0S0      1
#define I830_TEXREG_TM0S1      2
#define I830_TEXREG_TM0S2      3
#define I830_TEXREG_TM0S3      4
#define I830_TEXREG_TM0S4      5
#define I830_TEXREG_NOP0       6	/* noop */
#define I830_TEXREG_NOP1       7	/* noop */
#define I830_TEXREG_NOP2       8	/* noop */
#define __I830_TEXREG_MCS      9	/* GFX_OP_MAP_COORD_SETS -- shared */
#define __I830_TEX_SETUP_SIZE   10

#define I830_FRONT   0x1
#define I830_BACK    0x2
#define I830_DEPTH   0x4

#endif				/* _I830_DEFINES_ */

typedef struct _drm_i830_init {
	enum {
		I830_INIT_DMA = 0x01,
		I830_CLEANUP_DMA = 0x02
	} func;
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
	unsigned int back_pitch;
	unsigned int depth_pitch;
	unsigned int cpp;
} drm_i830_init_t;

/* Warning: If you change the SAREA structure you must change the Xserver
 * structure as well */

typedef struct _drm_i830_tex_region {
	unsigned char next, prev;	/* indices to form a circular LRU  */
	unsigned char in_use;	/* owned by a client, or free? */
	int age;		/* tracked by clients to update local LRU's */
} drm_i830_tex_region_t;

typedef struct _drm_i830_sarea {
	unsigned int ContextState[I830_CTX_SETUP_SIZE];
	unsigned int BufferState[I830_DEST_SETUP_SIZE];
	unsigned int TexState[I830_TEXTURE_COUNT][I830_TEX_SETUP_SIZE];
	unsigned int TexBlendState[I830_TEXBLEND_COUNT][I830_TEXBLEND_SIZE];
	unsigned int TexBlendStateWordsUsed[I830_TEXBLEND_COUNT];
	unsigned int Palette[2][256];
	unsigned int dirty;

	unsigned int nbox;
	struct drm_clip_rect boxes[I830_NR_SAREA_CLIPRECTS];

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

	drm_i830_tex_region_t texList[I830_NR_TEX_REGIONS + 1];
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

	int perf_boxes;		/* performance boxes to be displayed */

	/* Here's the state for texunits 2,3:
	 */
	unsigned int TexState2[I830_TEX_SETUP_SIZE];
	unsigned int TexBlendState2[I830_TEXBLEND_SIZE];
	unsigned int TexBlendStateWordsUsed2;

	unsigned int TexState3[I830_TEX_SETUP_SIZE];
	unsigned int TexBlendState3[I830_TEXBLEND_SIZE];
	unsigned int TexBlendStateWordsUsed3;

	unsigned int StippleState[I830_STP_SETUP_SIZE];
} drm_i830_sarea_t;

/* Flags for perf_boxes
 */
#define I830_BOX_RING_EMPTY    0x1	/* populated by kernel */
#define I830_BOX_FLIP          0x2	/* populated by kernel */
#define I830_BOX_WAIT          0x4	/* populated by kernel & client */
#define I830_BOX_TEXTURE_LOAD  0x8	/* populated by kernel */
#define I830_BOX_LOST_CONTEXT  0x10	/* populated by client */

/* I830 specific ioctls
 * The device specific ioctl range is 0x40 to 0x79.
 */
#define DRM_I830_INIT	0x00
#define DRM_I830_VERTEX	0x01
#define DRM_I830_CLEAR	0x02
#define DRM_I830_FLUSH	0x03
#define DRM_I830_GETAGE	0x04
#define DRM_I830_GETBUF	0x05
#define DRM_I830_SWAP	0x06
#define DRM_I830_COPY	0x07
#define DRM_I830_DOCOPY	0x08
#define DRM_I830_FLIP	0x09
#define DRM_I830_IRQ_EMIT	0x0a
#define DRM_I830_IRQ_WAIT	0x0b
#define DRM_I830_GETPARAM	0x0c
#define DRM_I830_SETPARAM	0x0d

#define DRM_IOCTL_I830_INIT		DRM_IOW( DRM_COMMAND_BASE + DRM_IOCTL_I830_INIT, drm_i830_init_t)
#define DRM_IOCTL_I830_VERTEX		DRM_IOW( DRM_COMMAND_BASE + DRM_IOCTL_I830_VERTEX, drm_i830_vertex_t)
#define DRM_IOCTL_I830_CLEAR		DRM_IOW( DRM_COMMAND_BASE + DRM_IOCTL_I830_CLEAR, drm_i830_clear_t)
#define DRM_IOCTL_I830_FLUSH		DRM_IO ( DRM_COMMAND_BASE + DRM_IOCTL_I830_FLUSH)
#define DRM_IOCTL_I830_GETAGE		DRM_IO ( DRM_COMMAND_BASE + DRM_IOCTL_I830_GETAGE)
#define DRM_IOCTL_I830_GETBUF		DRM_IOWR(DRM_COMMAND_BASE + DRM_IOCTL_I830_GETBUF, drm_i830_dma_t)
#define DRM_IOCTL_I830_SWAP		DRM_IO ( DRM_COMMAND_BASE + DRM_IOCTL_I830_SWAP)
#define DRM_IOCTL_I830_COPY		DRM_IOW( DRM_COMMAND_BASE + DRM_IOCTL_I830_COPY, drm_i830_copy_t)
#define DRM_IOCTL_I830_DOCOPY		DRM_IO ( DRM_COMMAND_BASE + DRM_IOCTL_I830_DOCOPY)
#define DRM_IOCTL_I830_FLIP		DRM_IO ( DRM_COMMAND_BASE + DRM_IOCTL_I830_FLIP)
#define DRM_IOCTL_I830_IRQ_EMIT         DRM_IOWR(DRM_COMMAND_BASE + DRM_IOCTL_I830_IRQ_EMIT, drm_i830_irq_emit_t)
#define DRM_IOCTL_I830_IRQ_WAIT         DRM_IOW( DRM_COMMAND_BASE + DRM_IOCTL_I830_IRQ_WAIT, drm_i830_irq_wait_t)
#define DRM_IOCTL_I830_GETPARAM         DRM_IOWR(DRM_COMMAND_BASE + DRM_IOCTL_I830_GETPARAM, drm_i830_getparam_t)
#define DRM_IOCTL_I830_SETPARAM         DRM_IOWR(DRM_COMMAND_BASE + DRM_IOCTL_I830_SETPARAM, drm_i830_setparam_t)

typedef struct _drm_i830_clear {
	int clear_color;
	int clear_depth;
	int flags;
	unsigned int clear_colormask;
	unsigned int clear_depthmask;
} drm_i830_clear_t;

/* These may be placeholders if we have more cliprects than
 * I830_NR_SAREA_CLIPRECTS.  In that case, the client sets discard to
 * false, indicating that the buffer will be dispatched again with a
 * new set of cliprects.
 */
typedef struct _drm_i830_vertex {
	int idx;		/* buffer index */
	int used;		/* nr bytes in use */
	int discard;		/* client is finished with the buffer? */
} drm_i830_vertex_t;

typedef struct _drm_i830_copy_t {
	int idx;		/* buffer index */
	int used;		/* nr bytes in use */
	void __user *address;	/* Address to copy from */
} drm_i830_copy_t;

typedef struct drm_i830_dma {
	void __user *virtual;
	int request_idx;
	int request_size;
	int granted;
} drm_i830_dma_t;

/* 1.3: Userspace can request & wait on irq's:
 */
typedef struct drm_i830_irq_emit {
	int __user *irq_seq;
} drm_i830_irq_emit_t;

typedef struct drm_i830_irq_wait {
	int irq_seq;
} drm_i830_irq_wait_t;

/* 1.3: New ioctl to query kernel params:
 */
#define I830_PARAM_IRQ_ACTIVE            1

typedef struct drm_i830_getparam {
	int param;
	int __user *value;
} drm_i830_getparam_t;

/* 1.3: New ioctl to set kernel params:
 */
#define I830_SETPARAM_USE_MI_BATCHBUFFER_START            1

typedef struct drm_i830_setparam {
	int param;
	int value;
} drm_i830_setparam_t;

#endif				/* _I830_DRM_H_ */
