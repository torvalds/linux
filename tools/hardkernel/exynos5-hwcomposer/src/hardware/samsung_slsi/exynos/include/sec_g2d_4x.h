/*
 * Copyright 2011, Samsung Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _SEC_G2D_DRIVER_H_
#define _SEC_G2D_DRIVER_H_

#define SEC_G2D_DEV_NAME        "/dev/fimg2d"
#define G2D_ALPHA_VALUE_MAX     (255)

/* ioctl commands */
#define FIMG2D_IOCTL_MAGIC      'F'
#define FIMG2D_BITBLT_BLIT      _IOWR(FIMG2D_IOCTL_MAGIC, 0, struct fimg2d_blit)
#define FIMG2D_BITBLT_SYNC      _IOW(FIMG2D_IOCTL_MAGIC, 1, int)
#define FIMG2D_BITBLT_VERSION   _IOR(FIMG2D_IOCTL_MAGIC, 2, struct fimg2d_version)


/**
 * @BLIT_SYNC: sync mode, to wait for blit done irq
 * @BLIT_ASYNC: async mode, not to wait for blit done irq
 *
 */
enum blit_sync {
    BLIT_SYNC,
    BLIT_ASYNC,
};

/**
 * @ADDR_PHYS: physical address
 * @ADDR_USER: user virtual address (physically Non-contiguous)
 * @ADDR_USER_RSVD: user virtual address (physically Contiguous)
 * @ADDR_DEVICE: specific device virtual address
 */
enum addr_space {
    ADDR_NONE,
    ADDR_PHYS,
    ADDR_KERN,
    ADDR_USER,
    ADDR_USER_RSVD,
    ADDR_DEVICE,
};

/**
 * Pixel order complies with little-endian style
 *
 * DO NOT CHANGE THIS ORDER
 */
enum pixel_order {
    AX_RGB = 0,
    RGB_AX,
    AX_BGR,
    BGR_AX,
    ARGB_ORDER_END,

    P1_CRY1CBY0,
    P1_CBY1CRY0,
    P1_Y1CRY0CB,
    P1_Y1CBY0CR,
    P1_ORDER_END,

    P2_CRCB,
    P2_CBCR,
    P2_ORDER_END,
};

/**
 * DO NOT CHANGE THIS ORDER
 */
enum color_format {
    CF_XRGB_8888 = 0,
    CF_ARGB_8888,
    CF_RGB_565,
    CF_XRGB_1555,
    CF_ARGB_1555,
    CF_XRGB_4444,
    CF_ARGB_4444,
    CF_RGB_888,
    CF_YCBCR_444,
    CF_YCBCR_422,
    CF_YCBCR_420,
    CF_A8,
    CF_L8,
    SRC_DST_FORMAT_END,

    CF_MSK_1BIT,
    CF_MSK_4BIT,
    CF_MSK_8BIT,
    CF_MSK_16BIT_565,
    CF_MSK_16BIT_1555,
    CF_MSK_16BIT_4444,
    CF_MSK_32BIT_8888,
    MSK_FORMAT_END,
};

enum rotation {
    ORIGIN,
    ROT_90,    /* clockwise */
    ROT_180,
    ROT_270,
    XFLIP,    /* x-axis flip */
    YFLIP,    /* y-axis flip */
};

/**
 * @NO_REPEAT: no effect
 * @REPEAT_NORMAL: repeat horizontally and vertically
 * @REPEAT_PAD: pad with pad color
 * @REPEAT_REFLECT: reflect horizontally and vertically
 * @REPEAT_CLAMP: pad with edge color of original image
 *
 * DO NOT CHANGE THIS ORDER
 */
enum repeat {
    NO_REPEAT = 0,
    REPEAT_NORMAL,    /* default setting */
    REPEAT_PAD,
    REPEAT_REFLECT, REPEAT_MIRROR = REPEAT_REFLECT,
    REPEAT_CLAMP,
};

enum scaling {
    NO_SCALING,
    SCALING_NEAREST,
    SCALING_BILINEAR,
};

/**
 * @SCALING_PIXELS: ratio in pixels
 * @SCALING_RATIO: ratio in fixed point 16
 */
enum scaling_factor {
    SCALING_PIXELS,
    SCALING_RATIO,
};

/**
 * premultiplied alpha
 */
enum premultiplied {
    PREMULTIPLIED,
    NON_PREMULTIPLIED,
};

/**
 * @TRANSP: discard bluescreen color
 * @BLUSCR: replace bluescreen color with background color
 */
enum bluescreen {
    OPAQUE,
    TRANSP,
    BLUSCR,
};

/**
 * DO NOT CHANGE THIS ORDER
 */
enum blit_op {
    BLIT_OP_SOLID_FILL = 0,

    BLIT_OP_CLR,
    BLIT_OP_SRC, BLIT_OP_SRC_COPY = BLIT_OP_SRC,
    BLIT_OP_DST,
    BLIT_OP_SRC_OVER,
    BLIT_OP_DST_OVER, BLIT_OP_OVER_REV = BLIT_OP_DST_OVER,
    BLIT_OP_SRC_IN,
    BLIT_OP_DST_IN, BLIT_OP_IN_REV = BLIT_OP_DST_IN,
    BLIT_OP_SRC_OUT,
    BLIT_OP_DST_OUT, BLIT_OP_OUT_REV = BLIT_OP_DST_OUT,
    BLIT_OP_SRC_ATOP,
    BLIT_OP_DST_ATOP, BLIT_OP_ATOP_REV = BLIT_OP_DST_ATOP,
    BLIT_OP_XOR,

    BLIT_OP_ADD,
    BLIT_OP_MULTIPLY,
    BLIT_OP_SCREEN,
    BLIT_OP_DARKEN,
    BLIT_OP_LIGHTEN,

    BLIT_OP_DISJ_SRC_OVER,
    BLIT_OP_DISJ_DST_OVER, BLIT_OP_SATURATE = BLIT_OP_DISJ_DST_OVER,
    BLIT_OP_DISJ_SRC_IN,
    BLIT_OP_DISJ_DST_IN, BLIT_OP_DISJ_IN_REV = BLIT_OP_DISJ_DST_IN,
    BLIT_OP_DISJ_SRC_OUT,
    BLIT_OP_DISJ_DST_OUT, BLIT_OP_DISJ_OUT_REV = BLIT_OP_DISJ_DST_OUT,
    BLIT_OP_DISJ_SRC_ATOP,
    BLIT_OP_DISJ_DST_ATOP, BLIT_OP_DISJ_ATOP_REV = BLIT_OP_DISJ_DST_ATOP,
    BLIT_OP_DISJ_XOR,

    BLIT_OP_CONJ_SRC_OVER,
    BLIT_OP_CONJ_DST_OVER, BLIT_OP_CONJ_OVER_REV = BLIT_OP_CONJ_DST_OVER,
    BLIT_OP_CONJ_SRC_IN,
    BLIT_OP_CONJ_DST_IN, BLIT_OP_CONJ_IN_REV = BLIT_OP_CONJ_DST_IN,
    BLIT_OP_CONJ_SRC_OUT,
    BLIT_OP_CONJ_DST_OUT, BLIT_OP_CONJ_OUT_REV = BLIT_OP_CONJ_DST_OUT,
    BLIT_OP_CONJ_SRC_ATOP,
    BLIT_OP_CONJ_DST_ATOP, BLIT_OP_CONJ_ATOP_REV = BLIT_OP_CONJ_DST_ATOP,
    BLIT_OP_CONJ_XOR,

    /* user select coefficient manually */
    BLIT_OP_USER_COEFF,

    BLIT_OP_USER_SRC_GA,

    /* Add new operation type here */

    /* end of blit operation */
    BLIT_OP_END,

    /* driver not supporting format */
    BLIT_OP_NOT_SUPPORTED
};

/**
 * @start: start address or unique id of image
 */
struct fimg2d_addr {
    enum addr_space type;
    unsigned long start;
};

struct fimg2d_rect {
    int x1;
    int y1;
    int x2;    /* x1 + width */
    int y2; /* y1 + height */
};

/**
 * pixels can be different from src, dst or clip rect
 */
struct fimg2d_scale {
    enum scaling mode;

    /* ratio in pixels */
    int src_w, src_h;
    int dst_w, dst_h;
};

struct fimg2d_clip {
    bool enable;
    int x1;
    int y1;
    int x2; /* x1 + width */
    int y2; /* y1 + height */
};

struct fimg2d_repeat {
    enum repeat mode;
    unsigned long pad_color;
};

/**
 * @bg_color: bg_color is valid only if bluescreen mode is BLUSCR.
 */
struct fimg2d_bluscr {
    enum bluescreen mode;
    unsigned long bs_color;
    unsigned long bg_color;
};

/**
 * @plane2: address info for CbCr in YCbCr 2plane mode
 * @rect: crop/clip rect
 * @need_cacheopr: true if cache coherency is required
 */
struct fimg2d_image {
    int width;
    int height;
    int stride;
    enum pixel_order order;
    enum color_format fmt;
    struct fimg2d_addr addr;
    struct fimg2d_addr plane2;
    struct fimg2d_rect rect;
    bool need_cacheopr;
};

/**
 * @solid_color:
 *         src color instead of src image / dst color instead of dst read image.
 *         color format and order must be ARGB8888(A is MSB).
 *         premultiplied format must be same to 'premult' of this struct.
 * @g_alpha: global(constant) alpha. 0xff is opaque, 0 is transparnet
 * @dither: dithering
 * @rotate: rotation degree in clockwise
 * @premult: alpha premultiplied mode for read & write
 * @scaling: common scaling info for src and mask image.
 * @repeat: repeat type (tile mode)
 * @bluscr: blue screen and transparent mode
 */
struct fimg2d_param {
    unsigned long solid_color;
    unsigned char g_alpha;
    bool dither;
    enum rotation rotate;
    enum premultiplied premult;
    struct fimg2d_scale scaling;
    struct fimg2d_repeat repeat;
    struct fimg2d_bluscr bluscr;
    struct fimg2d_clip clipping;
};

/**
 * @op: blit operation mode
 * @src: set when using src image
 * @msk: set when using mask image
 * @tmp: set when using 2-step blit at a single command
 * @dst: dst must not be null
 *         * tmp image must be the same to dst except memory address
 * @seq_no: user debugging info.
 *          for example, user can set sequence number or pid.
 */
struct fimg2d_blit {
    enum blit_op op;
    struct fimg2d_param param;
    struct fimg2d_image *src;
    struct fimg2d_image *msk;
    struct fimg2d_image *tmp;
    struct fimg2d_image *dst;
    enum blit_sync sync;
    unsigned int seq_no;
};

struct fimg2d_blit_raw {
    enum blit_op op;
    struct fimg2d_param param;
    struct fimg2d_image src;
    struct fimg2d_image msk;
    struct fimg2d_image tmp;
    struct fimg2d_image dst;
    enum blit_sync sync;
    unsigned int seq_no;
};

#ifdef __KERNEL__

/**
 * @pgd: base address of arm mmu pagetable
 * @ncmd: request count in blit command queue
 * @wait_q: conext wait queue head
 */
struct fimg2d_context {
    struct mm_struct *mm;
    atomic_t ncmd;
    wait_queue_head_t wait_q;
};

/**
 * @seq_no: used for debugging
 * @node: list head of blit command queue
 */
struct fimg2d_bltcmd {
    enum blit_op op;

    enum premultiplied premult;
    unsigned char g_alpha;
    bool dither;
    enum rotation rotate;
    struct fimg2d_scale scaling;
    struct fimg2d_repeat repeat;
    struct fimg2d_bluscr bluscr;
    struct fimg2d_clip clipping;

    bool srcen;
    bool dsten;
    bool msken;

    unsigned long solid_color;
    struct fimg2d_image src;
    struct fimg2d_image dst;
    struct fimg2d_image msk;

    struct fimg2d_rect src_rect;
    struct fimg2d_rect dst_rect;
    struct fimg2d_rect msk_rect;

    size_t size_all;
    struct fimg2d_cache src_cache;
    struct fimg2d_cache dst_cache;
    struct fimg2d_cache msk_cache;

    unsigned int seq_no;
    struct fimg2d_context *ctx;
    struct list_head node;
};

/**
 * @pwron: power status for runtime pm
 * @pwrlock: spinlock for runtime pm
 * @mem: resource platform device
 * @regs: base address of hardware
 * @dev: pointer to device struct
 * @err: true if hardware is timed out while blitting
 * @irq: irq number
 * @nctx: context count
 * @busy: 1 if hardware is running
 * @bltlock: spinlock for blit
 * @wait_q: blit wait queue head
 * @cmd_q: blit command queue
 * @workqueue: workqueue_struct for kfimg2dd
 */
struct fimg2d_control {
    atomic_t pwron;
    spinlock_t pwrlock;
    struct clk *clock;
    struct device *dev;
    struct resource *mem;
    void __iomem *regs;

    bool err;
    int irq;
    atomic_t nctx;
    atomic_t busy;
    atomic_t active;
    spinlock_t bltlock;
    wait_queue_head_t wait_q;
    struct list_head cmd_q;
    struct workqueue_struct *work_q;

    void (*blit)(struct fimg2d_control *info);
    void (*configure)(struct fimg2d_control *info, struct fimg2d_bltcmd *cmd);
    void (*run)(struct fimg2d_control *info);
    void (*stop)(struct fimg2d_control *info);
    void (*finalize)(struct fimg2d_control *info);
};

inline void fimg2d_enqueue(struct fimg2d_control *info,
            struct list_head *node, struct list_head *q);
inline void fimg2d_dequeue(struct fimg2d_control *info, struct list_head *node);
inline int fimg2d_queue_is_empty(struct list_head *q);
inline struct fimg2d_bltcmd * fimg2d_get_first_command(struct fimg2d_control *info);
int fimg2d_add_command(struct fimg2d_control *info, struct fimg2d_context *ctx,
            struct fimg2d_blit __user *u);
inline void fimg2d_add_context(struct fimg2d_control *info, struct fimg2d_context *ctx);
inline void fimg2d_del_context(struct fimg2d_control *info, struct fimg2d_context *ctx);
int fimg2d_register_ops(struct fimg2d_control *info);
#if defined(CONFIG_OUTER_CACHE) && defined(CONFIG_ARM)
void fimg2d_clean_pagetable(struct mm_struct *mm, unsigned long addr,
                                        unsigned long size);
#else
#define fimg2d_clean_pagetable(mm, addr, size) do { } while (0)
#endif

#endif /* __KERNEL__ */

#endif /*_SEC_G2D_DRIVER_H_*/
