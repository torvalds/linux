#ifndef _BUFFERS_H
#define _BUFFERS_H

/* This inspired by rtai/shmem */
#define FIX_SIZE(x) (((x) - 1) & PAGE_MASK) + PAGE_SIZE

extern int relay_mmap_buf(struct rchan_buf *buf, struct vm_area_struct *vma);
extern struct rchan_buf *relay_create_buf(struct rchan *chan);
extern void relay_destroy_buf(struct rchan_buf *buf);
extern void relay_remove_buf(struct kref *kref);

#endif/* _BUFFERS_H */
