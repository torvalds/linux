/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_KEXEC_INTERNAL_H
#define LINUX_KEXEC_INTERNAL_H

#include <linux/kexec.h>

struct kexec_segment;

struct kimage *do_kimage_alloc_init(void);
int sanity_check_segment_list(struct kimage *image);
void kimage_free_page_list(struct list_head *list);
void kimage_free(struct kimage *image);
int kimage_load_segment(struct kimage *image, struct kexec_segment *segment);
void kimage_terminate(struct kimage *image);
int kimage_is_destination_range(struct kimage *image,
				unsigned long start, unsigned long end);

/*
 * Whatever is used to serialize accesses to the kexec_crash_image needs to be
 * NMI safe, as __crash_kexec() can happen during nmi_panic(), so here we use a
 * "simple" atomic variable that is acquired with a cmpxchg().
 */
extern atomic_t __kexec_lock;
static inline bool kexec_trylock(void)
{
	int old = 0;
	return atomic_try_cmpxchg_acquire(&__kexec_lock, &old, 1);
}
static inline void kexec_unlock(void)
{
	atomic_set_release(&__kexec_lock, 0);
}

#ifdef CONFIG_KEXEC_FILE
#include <linux/purgatory.h>
void kimage_file_post_load_cleanup(struct kimage *image);
extern char kexec_purgatory[];
extern size_t kexec_purgatory_size;
#else /* CONFIG_KEXEC_FILE */
static inline void kimage_file_post_load_cleanup(struct kimage *image) { }
#endif /* CONFIG_KEXEC_FILE */
#endif /* LINUX_KEXEC_INTERNAL_H */
