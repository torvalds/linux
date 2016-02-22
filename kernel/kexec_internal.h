#ifndef LINUX_KEXEC_INTERNAL_H
#define LINUX_KEXEC_INTERNAL_H

#include <linux/kexec.h>

struct kimage *do_kimage_alloc_init(void);
int sanity_check_segment_list(struct kimage *image);
void kimage_free_page_list(struct list_head *list);
void kimage_free(struct kimage *image);
int kimage_load_segment(struct kimage *image, struct kexec_segment *segment);
void kimage_terminate(struct kimage *image);
int kimage_is_destination_range(struct kimage *image,
				unsigned long start, unsigned long end);

extern struct mutex kexec_mutex;

#ifdef CONFIG_KEXEC_FILE
struct kexec_sha_region {
	unsigned long start;
	unsigned long len;
};

/*
 * Keeps track of buffer parameters as provided by caller for requesting
 * memory placement of buffer.
 */
struct kexec_buf {
	struct kimage *image;
	char *buffer;
	unsigned long bufsz;
	unsigned long mem;
	unsigned long memsz;
	unsigned long buf_align;
	unsigned long buf_min;
	unsigned long buf_max;
	bool top_down;		/* allocate from top of memory hole */
};

void kimage_file_post_load_cleanup(struct kimage *image);
#else /* CONFIG_KEXEC_FILE */
static inline void kimage_file_post_load_cleanup(struct kimage *image) { }
#endif /* CONFIG_KEXEC_FILE */
#endif /* LINUX_KEXEC_INTERNAL_H */
