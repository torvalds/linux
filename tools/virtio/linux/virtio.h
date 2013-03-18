#ifndef LINUX_VIRTIO_H
#define LINUX_VIRTIO_H

#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <linux/types.h>
#include <errno.h>

typedef unsigned long long dma_addr_t;

struct scatterlist {
	unsigned long	page_link;
	unsigned int	offset;
	unsigned int	length;
	dma_addr_t	dma_address;
};

struct page {
	unsigned long long dummy;
};

#define BUG_ON(__BUG_ON_cond) assert(!(__BUG_ON_cond))

/* Physical == Virtual */
#define virt_to_phys(p) ((unsigned long)p)
#define phys_to_virt(a) ((void *)(unsigned long)(a))
/* Page address: Virtual / 4K */
#define virt_to_page(p) ((struct page*)((virt_to_phys(p) / 4096) * \
					sizeof(struct page)))
#define offset_in_page(p) (((unsigned long)p) % 4096)
#define sg_phys(sg) ((sg->page_link & ~0x3) / sizeof(struct page) * 4096 + \
		     sg->offset)
static inline void sg_mark_end(struct scatterlist *sg)
{
	/*
	 * Set termination bit, clear potential chain bit
	 */
	sg->page_link |= 0x02;
	sg->page_link &= ~0x01;
}
static inline void sg_init_table(struct scatterlist *sgl, unsigned int nents)
{
	memset(sgl, 0, sizeof(*sgl) * nents);
	sg_mark_end(&sgl[nents - 1]);
}
static inline void sg_assign_page(struct scatterlist *sg, struct page *page)
{
	unsigned long page_link = sg->page_link & 0x3;

	/*
	 * In order for the low bit stealing approach to work, pages
	 * must be aligned at a 32-bit boundary as a minimum.
	 */
	BUG_ON((unsigned long) page & 0x03);
	sg->page_link = page_link | (unsigned long) page;
}

static inline void sg_set_page(struct scatterlist *sg, struct page *page,
			       unsigned int len, unsigned int offset)
{
	sg_assign_page(sg, page);
	sg->offset = offset;
	sg->length = len;
}

static inline void sg_set_buf(struct scatterlist *sg, const void *buf,
			      unsigned int buflen)
{
	sg_set_page(sg, virt_to_page(buf), buflen, offset_in_page(buf));
}

static inline void sg_init_one(struct scatterlist *sg, const void *buf, unsigned int buflen)
{
	sg_init_table(sg, 1);
	sg_set_buf(sg, buf, buflen);
}

typedef __u16 u16;

typedef enum {
	GFP_KERNEL,
	GFP_ATOMIC,
	__GFP_HIGHMEM,
	__GFP_HIGH
} gfp_t;
typedef enum {
	IRQ_NONE,
	IRQ_HANDLED
} irqreturn_t;

static inline void *kmalloc(size_t s, gfp_t gfp)
{
	return malloc(s);
}

static inline void kfree(void *p)
{
	free(p);
}

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define uninitialized_var(x) x = x

# ifndef likely
#  define likely(x)	(__builtin_expect(!!(x), 1))
# endif
# ifndef unlikely
#  define unlikely(x)	(__builtin_expect(!!(x), 0))
# endif

#define pr_err(format, ...) fprintf (stderr, format, ## __VA_ARGS__)
#ifdef DEBUG
#define pr_debug(format, ...) fprintf (stderr, format, ## __VA_ARGS__)
#else
#define pr_debug(format, ...) do {} while (0)
#endif
#define dev_err(dev, format, ...) fprintf (stderr, format, ## __VA_ARGS__)
#define dev_warn(dev, format, ...) fprintf (stderr, format, ## __VA_ARGS__)

/* TODO: empty stubs for now. Broken but enough for virtio_ring.c */
#define list_add_tail(a, b) do {} while (0)
#define list_del(a) do {} while (0)

#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#define BITS_PER_BYTE		8
#define BITS_PER_LONG (sizeof(long) * BITS_PER_BYTE)
#define BIT_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))
/* TODO: Not atomic as it should be:
 * we don't use this for anything important. */
static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p &= ~mask;
}

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
        return 1UL & (addr[BIT_WORD(nr)] >> (nr & (BITS_PER_LONG-1)));
}

/* The only feature we care to support */
#define virtio_has_feature(dev, feature) \
	test_bit((feature), (dev)->features)
/* end of stubs */

struct virtio_device {
	void *dev;
	unsigned long features[1];
};

struct virtqueue {
	/* TODO: commented as list macros are empty stubs for now.
	 * Broken but enough for virtio_ring.c
	 * struct list_head list; */
	void (*callback)(struct virtqueue *vq);
	const char *name;
	struct virtio_device *vdev;
        unsigned int index;
        unsigned int num_free;
	void *priv;
};

#define EXPORT_SYMBOL_GPL(__EXPORT_SYMBOL_GPL_name) \
	void __EXPORT_SYMBOL_GPL##__EXPORT_SYMBOL_GPL_name() { \
}
#define MODULE_LICENSE(__MODULE_LICENSE_value) \
	const char *__MODULE_LICENSE_name = __MODULE_LICENSE_value

#define CONFIG_SMP

#if defined(__i386__) || defined(__x86_64__)
#define barrier() asm volatile("" ::: "memory")
#define mb() __sync_synchronize()

#define smp_mb()	mb()
# define smp_rmb()	barrier()
# define smp_wmb()	barrier()
/* Weak barriers should be used. If not - it's a bug */
# define rmb()	abort()
# define wmb()	abort()
#else
#error Please fill in barrier macros
#endif

/* Interfaces exported by virtio_ring. */
int virtqueue_add_buf(struct virtqueue *vq,
		      struct scatterlist sg[],
		      unsigned int out_num,
		      unsigned int in_num,
		      void *data,
		      gfp_t gfp);

void virtqueue_kick(struct virtqueue *vq);

void *virtqueue_get_buf(struct virtqueue *vq, unsigned int *len);

void virtqueue_disable_cb(struct virtqueue *vq);

bool virtqueue_enable_cb(struct virtqueue *vq);
bool virtqueue_enable_cb_delayed(struct virtqueue *vq);

void *virtqueue_detach_unused_buf(struct virtqueue *vq);
struct virtqueue *vring_new_virtqueue(unsigned int index,
				      unsigned int num,
				      unsigned int vring_align,
				      struct virtio_device *vdev,
				      bool weak_barriers,
				      void *pages,
				      void (*notify)(struct virtqueue *vq),
				      void (*callback)(struct virtqueue *vq),
				      const char *name);
void vring_del_virtqueue(struct virtqueue *vq);

#endif
