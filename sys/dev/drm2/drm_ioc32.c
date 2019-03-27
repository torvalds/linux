/*-
 * Copyright (C) Paul Mackerras 2005.
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
 * THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef COMPAT_FREEBSD32

#include <dev/drm2/drmP.h>
#include <dev/drm2/drm.h>

#define DRM_IOCTL_VERSION32		DRM_IOWR(0x00, drm_version32_t)
#define DRM_IOCTL_GET_UNIQUE32		DRM_IOWR(0x01, drm_unique32_t)
#define DRM_IOCTL_GET_MAP32		DRM_IOWR(0x04, drm_map32_t)
#define DRM_IOCTL_GET_CLIENT32		DRM_IOWR(0x05, drm_client32_t)
#define DRM_IOCTL_GET_STATS32		DRM_IOR( 0x06, drm_stats32_t)

#define DRM_IOCTL_SET_UNIQUE32		DRM_IOW( 0x10, drm_unique32_t)
#define DRM_IOCTL_ADD_MAP32		DRM_IOWR(0x15, drm_map32_t)
#define DRM_IOCTL_ADD_BUFS32		DRM_IOWR(0x16, drm_buf_desc32_t)
#define DRM_IOCTL_MARK_BUFS32		DRM_IOW( 0x17, drm_buf_desc32_t)
#define DRM_IOCTL_INFO_BUFS32		DRM_IOWR(0x18, drm_buf_info32_t)
#define DRM_IOCTL_MAP_BUFS32		DRM_IOWR(0x19, drm_buf_map32_t)
#define DRM_IOCTL_FREE_BUFS32		DRM_IOW( 0x1a, drm_buf_free32_t)

#define DRM_IOCTL_RM_MAP32		DRM_IOW( 0x1b, drm_map32_t)

#define DRM_IOCTL_SET_SAREA_CTX32	DRM_IOW( 0x1c, drm_ctx_priv_map32_t)
#define DRM_IOCTL_GET_SAREA_CTX32	DRM_IOWR(0x1d, drm_ctx_priv_map32_t)

#define DRM_IOCTL_RES_CTX32		DRM_IOWR(0x26, drm_ctx_res32_t)
#define DRM_IOCTL_DMA32			DRM_IOWR(0x29, drm_dma32_t)

#define DRM_IOCTL_AGP_ENABLE32		DRM_IOW( 0x32, drm_agp_mode32_t)
#define DRM_IOCTL_AGP_INFO32		DRM_IOR( 0x33, drm_agp_info32_t)
#define DRM_IOCTL_AGP_ALLOC32		DRM_IOWR(0x34, drm_agp_buffer32_t)
#define DRM_IOCTL_AGP_FREE32		DRM_IOW( 0x35, drm_agp_buffer32_t)
#define DRM_IOCTL_AGP_BIND32		DRM_IOW( 0x36, drm_agp_binding32_t)
#define DRM_IOCTL_AGP_UNBIND32		DRM_IOW( 0x37, drm_agp_binding32_t)

#define DRM_IOCTL_SG_ALLOC32		DRM_IOW( 0x38, drm_scatter_gather32_t)
#define DRM_IOCTL_SG_FREE32		DRM_IOW( 0x39, drm_scatter_gather32_t)

#define DRM_IOCTL_UPDATE_DRAW32		DRM_IOW( 0x3f, drm_update_draw32_t)

#define DRM_IOCTL_WAIT_VBLANK32		DRM_IOWR(0x3a, drm_wait_vblank32_t)

typedef struct drm_version_32 {
	int version_major;	  /**< Major version */
	int version_minor;	  /**< Minor version */
	int version_patchlevel;	   /**< Patch level */
	u32 name_len;		  /**< Length of name buffer */
	u32 name;		  /**< Name of driver */
	u32 date_len;		  /**< Length of date buffer */
	u32 date;		  /**< User-space buffer to hold date */
	u32 desc_len;		  /**< Length of desc buffer */
	u32 desc;		  /**< User-space buffer to hold desc */
} drm_version32_t;

static int compat_drm_version(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_version32_t *v32 = data;
	struct drm_version version;
	int err;

	version.name_len = v32->name_len;
	version.name = (void *)(unsigned long)v32->name;
	version.date_len = v32->date_len;
	version.date = (void *)(unsigned long)v32->date;
	version.desc_len = v32->desc_len;
	version.desc = (void *)(unsigned long)v32->desc;

	err = drm_version(dev, (void *)&version, file_priv);
	if (err)
		return err;

	v32->version_major = version.version_major;
	v32->version_minor = version.version_minor;
	v32->version_patchlevel = version.version_patchlevel;
	v32->name_len = version.name_len;
	v32->date_len = version.date_len;
	v32->desc_len = version.desc_len;

	return 0;
}

typedef struct drm_unique32 {
	u32 unique_len;	/**< Length of unique */
	u32 unique;	/**< Unique name for driver instantiation */
} drm_unique32_t;

static int compat_drm_getunique(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_unique32_t *uq32 = data;
	struct drm_unique u;
	int err;

	u.unique_len = uq32->unique_len;
	u.unique = (void *)(unsigned long)uq32->unique;

	err = drm_getunique(dev, (void *)&u, file_priv);
	if (err)
		return err;

	uq32->unique_len = u.unique_len;

	return 0;
}

static int compat_drm_setunique(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_unique32_t *uq32 = data;
	struct drm_unique u;

	u.unique_len = uq32->unique_len;
	u.unique = (void *)(unsigned long)uq32->unique;

	return drm_setunique(dev, (void *)&u, file_priv);
}

typedef struct drm_map32 {
	u32 offset;		/**< Requested physical address (0 for SAREA)*/
	u32 size;		/**< Requested physical size (bytes) */
	enum drm_map_type type;	/**< Type of memory to map */
	enum drm_map_flags flags;	/**< Flags */
	u32 handle;		/**< User-space: "Handle" to pass to mmap() */
	int mtrr;		/**< MTRR slot used */
} drm_map32_t;

static int compat_drm_getmap(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_map32_t *m32 = data;
	struct drm_map map;
	int err;
	void *handle;

	map.offset = (unsigned long)m32->offset;

	err = drm_getmap(dev, (void *)&map, file_priv);
	if (err)
		return err;

	m32->offset = map.offset;
	m32->size = map.size;
	m32->type = map.type;
	m32->flags = map.flags;
	handle = map.handle;
	m32->mtrr = map.mtrr;

	m32->handle = (unsigned long)handle;

	return 0;

}

static int compat_drm_addmap(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_map32_t *m32 = data;
	struct drm_map map;
	int err;
	void *handle;

	map.offset = (unsigned long)m32->offset;
	map.size = (unsigned long)m32->size;
	map.type = m32->type;
	map.flags = m32->flags;

	err = drm_addmap_ioctl(dev, (void *)&map, file_priv);
	if (err)
		return err;

	m32->offset = map.offset;
	m32->mtrr = map.mtrr;
	handle = map.handle;

	m32->handle = (unsigned long)handle;
	if (m32->handle != (unsigned long)handle)
		DRM_DEBUG("compat_drm_addmap truncated handle"
				   " %p for type %d offset %x\n",
				   handle, m32->type, m32->offset);

	return 0;
}

static int compat_drm_rmmap(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_map32_t *m32 = data;
	struct drm_map map;

	map.handle = (void *)(unsigned long)m32->handle;

	return drm_rmmap_ioctl(dev, (void *)&map, file_priv);
}

typedef struct drm_client32 {
	int idx;	/**< Which client desired? */
	int auth;	/**< Is client authenticated? */
	u32 pid;	/**< Process ID */
	u32 uid;	/**< User ID */
	u32 magic;	/**< Magic */
	u32 iocs;	/**< Ioctl count */
} drm_client32_t;

static int compat_drm_getclient(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_client32_t *c32 = data;
	struct drm_client client;
	int err;

	client.idx = c32->idx;

	err = drm_getclient(dev, (void *)&client, file_priv);
	if (err)
		return err;

	c32->idx = client.idx;
	c32->auth = client.auth;
	c32->pid = client.pid;
	c32->uid = client.uid;
	c32->magic = client.magic;
	c32->iocs = client.iocs;

	return 0;
}

typedef struct drm_stats32 {
	u32 count;
	struct {
		u32 value;
		enum drm_stat_type type;
	} data[15];
} drm_stats32_t;

static int compat_drm_getstats(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_stats32_t *s32 = data;
	struct drm_stats stats;
	int i, err;

	err = drm_getstats(dev, (void *)&stats, file_priv);
	if (err)
		return err;

	s32->count = stats.count;
	for (i = 0; i < stats.count; i++) {
		s32->data[i].value = stats.data[i].value;
		s32->data[i].type = stats.data[i].type;
	}

	return 0;
}

typedef struct drm_buf_desc32 {
	int count;		 /**< Number of buffers of this size */
	int size;		 /**< Size in bytes */
	int low_mark;		 /**< Low water mark */
	int high_mark;		 /**< High water mark */
	int flags;
	u32 agp_start;		 /**< Start address in the AGP aperture */
} drm_buf_desc32_t;

static int compat_drm_addbufs(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_buf_desc32_t *b32 = data;
	struct drm_buf_desc buf;
	int err;

	buf.count = b32->count;
	buf.size = b32->size;
	buf.low_mark = b32->low_mark;
	buf.high_mark = b32->high_mark;
	buf.flags = b32->flags;
	buf.agp_start = (unsigned long)b32->agp_start;

	err = drm_addbufs(dev, (void *)&buf, file_priv);
	if (err)
		return err;

	b32->count = buf.count;
	b32->size = buf.size;
	b32->low_mark = buf.low_mark;
	b32->high_mark = buf.high_mark;
	b32->flags = buf.flags;
	b32->agp_start = buf.agp_start;

	return 0;
}

static int compat_drm_markbufs(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_buf_desc32_t *b32 = data;
	struct drm_buf_desc buf;

	buf.size = b32->size;
	buf.low_mark = b32->low_mark;
	buf.high_mark = b32->high_mark;

	return drm_markbufs(dev, (void *)&buf, file_priv);
}

typedef struct drm_buf_info32 {
	int count;		/**< Entries in list */
	u32 list;
} drm_buf_info32_t;

static int compat_drm_infobufs(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_buf_info32_t *req32 = data;
	drm_buf_desc32_t *to;
	struct drm_buf_info *request;
	struct drm_buf_desc *list;
	size_t nbytes;
	int i, err;
	int count, actual;

	count = req32->count;
	to = (drm_buf_desc32_t *)(unsigned long)req32->list;
	if (count < 0)
		count = 0;

	nbytes = sizeof(*request) + count * sizeof(struct drm_buf_desc);
	request = malloc(nbytes, DRM_MEM_BUFLISTS, M_ZERO | M_NOWAIT);
	if (!request)
		return -ENOMEM;
	list = (struct drm_buf_desc *) (request + 1);

	request->count = count;
	request->list = list;

	err = drm_infobufs(dev, (void *)request, file_priv);
	if (err)
		return err;

	actual = request->count;
	if (count >= actual)
		for (i = 0; i < actual; ++i) {
			to[i].count = list[i].count;
			to[i].size = list[i].size;
			to[i].low_mark = list[i].low_mark;
			to[i].high_mark = list[i].high_mark;
			to[i].flags = list[i].flags;
		}

	req32->count = actual;

	return 0;
}

typedef struct drm_buf_pub32 {
	int idx;		/**< Index into the master buffer list */
	int total;		/**< Buffer size */
	int used;		/**< Amount of buffer in use (for DMA) */
	u32 address;		/**< Address of buffer */
} drm_buf_pub32_t;

typedef struct drm_buf_map32 {
	int count;		/**< Length of the buffer list */
	u32 virtual;		/**< Mmap'd area in user-virtual */
	u32 list;		/**< Buffer information */
} drm_buf_map32_t;

static int compat_drm_mapbufs(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_buf_map32_t *req32 = data;
	drm_buf_pub32_t *list32;
	struct drm_buf_map *request;
	struct drm_buf_pub *list;
	int i, err;
	int count, actual;
	size_t nbytes;

	count = req32->count;
	list32 = (void *)(unsigned long)req32->list;

	if (count < 0)
		return -EINVAL;
	nbytes = sizeof(*request) + count * sizeof(struct drm_buf_pub);
	request = malloc(nbytes, DRM_MEM_BUFLISTS, M_ZERO | M_NOWAIT);
	if (!request)
		return -ENOMEM;
	list = (struct drm_buf_pub *) (request + 1);

	request->count = count;
	request->list = list;

	err = drm_mapbufs(dev, (void *)request, file_priv);
	if (err)
		return err;

	actual = request->count;
	if (count >= actual)
		for (i = 0; i < actual; ++i) {
			list32[i].idx = list[i].idx;
			list32[i].total = list[i].total;
			list32[i].used = list[i].used;
			list32[i].address = (unsigned long)list[i].address;
		}

	req32->count = actual;
	req32->virtual = (unsigned long)request->virtual;

	return 0;
}

typedef struct drm_buf_free32 {
	int count;
	u32 list;
} drm_buf_free32_t;

static int compat_drm_freebufs(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_buf_free32_t *req32 = data;
	struct drm_buf_free request;

	request.count = req32->count;
	request.list = (int *)(unsigned long)req32->list;

	return drm_freebufs(dev, (void *)&request, file_priv);
}

typedef struct drm_ctx_priv_map32 {
	unsigned int ctx_id;	 /**< Context requesting private mapping */
	u32 handle;		/**< Handle of map */
} drm_ctx_priv_map32_t;

static int compat_drm_setsareactx(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_ctx_priv_map32_t *req32 = data;
	struct drm_ctx_priv_map request;

	request.ctx_id = req32->ctx_id;
	request.handle = (void *)(unsigned long)req32->handle;

	return drm_setsareactx(dev, (void *)&request, file_priv);
}

static int compat_drm_getsareactx(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_ctx_priv_map32_t *req32 = data;
	struct drm_ctx_priv_map request;
	int err;

	request.ctx_id = req32->ctx_id;

	err = drm_getsareactx(dev, (void *)&request, file_priv);
	if (err)
		return err;

	req32->handle = (unsigned long)request.handle;

	return 0;
}

typedef struct drm_ctx_res32 {
	int count;
	u32 contexts;
} drm_ctx_res32_t;

static int compat_drm_resctx(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_ctx_res32_t *res32 = data;
	struct drm_ctx_res res;
	int err;

	res.count = res32->count;
	res.contexts = (struct drm_ctx __user *)(unsigned long)res32->contexts;

	err = drm_resctx(dev, (void *)&res, file_priv);
	if (err)
		return err;

	res32->count = res.count;

	return 0;
}

typedef struct drm_dma32 {
	int context;		  /**< Context handle */
	int send_count;		  /**< Number of buffers to send */
	u32 send_indices;	  /**< List of handles to buffers */
	u32 send_sizes;		  /**< Lengths of data to send */
	enum drm_dma_flags flags;		  /**< Flags */
	int request_count;	  /**< Number of buffers requested */
	int request_size;	  /**< Desired size for buffers */
	u32 request_indices;	  /**< Buffer information */
	u32 request_sizes;
	int granted_count;	  /**< Number of buffers granted */
} drm_dma32_t;

static int compat_drm_dma(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_dma32_t *d32 = data;
	struct drm_dma d;
	int err;

	if (!dev->driver->dma_ioctl) {
		DRM_DEBUG("DMA ioctl on driver with no dma handler\n");
		return -EINVAL;
	}

	d.context = d32->context;
	d.send_count = d32->send_count;
	d.send_indices = (int *)(unsigned long)d32->send_indices;
	d.send_sizes = (int *)(unsigned long)d32->send_sizes;
	d.flags = d32->flags;
	d.request_count = d32->request_count;
	d.request_indices = (int *)(unsigned long)d32->request_indices;
	d.request_sizes = (int *)(unsigned long)d32->request_sizes;

	err = dev->driver->dma_ioctl(dev, (void *)&d, file_priv);
	if (err)
		return err;

	d32->request_size = d.request_size;
	d32->granted_count = d.granted_count;

	return 0;
}

#if __OS_HAS_AGP
typedef struct drm_agp_mode32 {
	u32 mode;	/**< AGP mode */
} drm_agp_mode32_t;

static int compat_drm_agp_enable(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_agp_mode32_t *m32 = data;
	struct drm_agp_mode mode;

	mode.mode = m32->mode;

	return drm_agp_enable_ioctl(dev, (void *)&mode, file_priv);
}

typedef struct drm_agp_info32 {
	int agp_version_major;
	int agp_version_minor;
	u32 mode;
	u32 aperture_base;	/* physical address */
	u32 aperture_size;	/* bytes */
	u32 memory_allowed;	/* bytes */
	u32 memory_used;

	/* PCI information */
	unsigned short id_vendor;
	unsigned short id_device;
} drm_agp_info32_t;

static int compat_drm_agp_info(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_agp_info32_t *i32 = data;
	struct drm_agp_info info;
	int err;

	err = drm_agp_info_ioctl(dev, (void *)&info, file_priv);
	if (err)
		return err;

	i32->agp_version_major = info.agp_version_major;
	i32->agp_version_minor = info.agp_version_minor;
	i32->mode = info.mode;
	i32->aperture_base = info.aperture_base;
	i32->aperture_size = info.aperture_size;
	i32->memory_allowed = info.memory_allowed;
	i32->memory_used = info.memory_used;
	i32->id_vendor = info.id_vendor;
	i32->id_device = info.id_device;

	return 0;
}

typedef struct drm_agp_buffer32 {
	u32 size;	/**< In bytes -- will round to page boundary */
	u32 handle;	/**< Used for binding / unbinding */
	u32 type;	/**< Type of memory to allocate */
	u32 physical;	/**< Physical used by i810 */
} drm_agp_buffer32_t;

static int compat_drm_agp_alloc(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_agp_buffer32_t *req32 = data;
	struct drm_agp_buffer request;
	int err;

	request.size = req32->size;
	request.type = req32->type;

	err = drm_agp_alloc_ioctl(dev, (void *)&request, file_priv);
	if (err)
		return err;

	req32->handle = request.handle;
	req32->physical = request.physical;

	return 0;
}

static int compat_drm_agp_free(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_agp_buffer32_t *req32 = data;
	struct drm_agp_buffer request;

	request.handle = req32->handle;

	return drm_agp_free_ioctl(dev, (void *)&request, file_priv);
}

typedef struct drm_agp_binding32 {
	u32 handle;	/**< From drm_agp_buffer */
	u32 offset;	/**< In bytes -- will round to page boundary */
} drm_agp_binding32_t;

static int compat_drm_agp_bind(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_agp_binding32_t *req32 = data;
	struct drm_agp_binding request;

	request.handle = req32->handle;
	request.offset = req32->offset;

	return drm_agp_bind_ioctl(dev, (void *)&request, file_priv);
}

static int compat_drm_agp_unbind(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_agp_binding32_t *req32 = data;
	struct drm_agp_binding request;

	request.handle = req32->handle;

	return drm_agp_unbind_ioctl(dev, (void *)&request, file_priv);
}
#endif				/* __OS_HAS_AGP */

typedef struct drm_scatter_gather32 {
	u32 size;	/**< In bytes -- will round to page boundary */
	u32 handle;	/**< Used for mapping / unmapping */
} drm_scatter_gather32_t;

static int compat_drm_sg_alloc(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_scatter_gather32_t *req32 = data;
	struct drm_scatter_gather request;
	int err;

	request.size = (unsigned long)req32->size;

	err = drm_sg_alloc_ioctl(dev, (void *)&request, file_priv);
	if (err)
		return err;

	/* XXX not sure about the handle conversion here... */
	req32->handle = (unsigned long)request.handle >> PAGE_SHIFT;

	return 0;
}

static int compat_drm_sg_free(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_scatter_gather32_t *req32 = data;
	struct drm_scatter_gather request;

	request.handle = (unsigned long)req32->handle << PAGE_SHIFT;

	return drm_sg_free(dev, (void *)&request, file_priv);
}

#if defined(CONFIG_X86) || defined(CONFIG_IA64)
typedef struct drm_update_draw32 {
	drm_drawable_t handle;
	unsigned int type;
	unsigned int num;
	/* 64-bit version has a 32-bit pad here */
	u64 data;	/**< Pointer */
} __attribute__((packed)) drm_update_draw32_t;
#endif

struct drm_wait_vblank_request32 {
	enum drm_vblank_seq_type type;
	unsigned int sequence;
	u32 signal;
};

struct drm_wait_vblank_reply32 {
	enum drm_vblank_seq_type type;
	unsigned int sequence;
	s32 tval_sec;
	s32 tval_usec;
};

typedef union drm_wait_vblank32 {
	struct drm_wait_vblank_request32 request;
	struct drm_wait_vblank_reply32 reply;
} drm_wait_vblank32_t;

static int compat_drm_wait_vblank(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	drm_wait_vblank32_t *req32 = data;
	union drm_wait_vblank request;
	int err;

	request.request.type = req32->request.type;
	request.request.sequence = req32->request.sequence;
	request.request.signal = req32->request.signal;

	err = drm_wait_vblank(dev, (void *)&request, file_priv);
	if (err)
		return err;

	req32->reply.type = request.reply.type;
	req32->reply.sequence = request.reply.sequence;
	req32->reply.tval_sec = request.reply.tval_sec;
	req32->reply.tval_usec = request.reply.tval_usec;

	return 0;
}

struct drm_ioctl_desc drm_compat_ioctls[256] = {
	DRM_IOCTL_DEF(DRM_IOCTL_VERSION32, compat_drm_version, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_UNIQUE32, compat_drm_getunique, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_MAP32, compat_drm_getmap, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_CLIENT32, compat_drm_getclient, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_STATS32, compat_drm_getstats, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_SET_UNIQUE32, compat_drm_setunique, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_ADD_MAP32, compat_drm_addmap, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_ADD_BUFS32, compat_drm_addbufs, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_MARK_BUFS32, compat_drm_markbufs, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_INFO_BUFS32, compat_drm_infobufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_MAP_BUFS32, compat_drm_mapbufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_FREE_BUFS32, compat_drm_freebufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_RM_MAP32, compat_drm_rmmap, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_SET_SAREA_CTX32, compat_drm_setsareactx, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_SAREA_CTX32, compat_drm_getsareactx, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_RES_CTX32, compat_drm_resctx, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_DMA32, compat_drm_dma, DRM_AUTH),
#if __OS_HAS_AGP
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_ENABLE32, compat_drm_agp_enable, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_INFO32, compat_drm_agp_info, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_ALLOC32, compat_drm_agp_alloc, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_FREE32, compat_drm_agp_free, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_BIND32, compat_drm_agp_bind, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_UNBIND32, compat_drm_agp_unbind, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
#endif
	DRM_IOCTL_DEF(DRM_IOCTL_SG_ALLOC32, compat_drm_sg_alloc, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_SG_FREE32, compat_drm_sg_free, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
#if defined(CONFIG_X86) || defined(CONFIG_IA64)
	DRM_IOCTL_DEF(DRM_IOCTL_UPDATE_DRAW32, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
#endif
	DRM_IOCTL_DEF(DRM_IOCTL_WAIT_VBLANK32, compat_drm_wait_vblank, DRM_UNLOCKED),
};

#endif
