#ifndef LINUX_VIRTIO_H
#define LINUX_VIRTIO_H
#include <linux/scatterlist.h>
#include <linux/kernel.h>

struct device {
	void *parent;
};

struct virtio_device {
	struct device dev;
	u64 features;
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

/* Interfaces exported by virtio_ring. */
int virtqueue_add_sgs(struct virtqueue *vq,
		      struct scatterlist *sgs[],
		      unsigned int out_sgs,
		      unsigned int in_sgs,
		      void *data,
		      gfp_t gfp);

int virtqueue_add_outbuf(struct virtqueue *vq,
			 struct scatterlist sg[], unsigned int num,
			 void *data,
			 gfp_t gfp);

int virtqueue_add_inbuf(struct virtqueue *vq,
			struct scatterlist sg[], unsigned int num,
			void *data,
			gfp_t gfp);

bool virtqueue_kick(struct virtqueue *vq);

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
				      bool (*notify)(struct virtqueue *vq),
				      void (*callback)(struct virtqueue *vq),
				      const char *name);
void vring_del_virtqueue(struct virtqueue *vq);

#endif
