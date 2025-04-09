/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BPF_BIO_H
#define _LINUX_BPF_BIO_H

#include <linux/bio.h>
#include <linux/bpf.h>
#include <linux/bpf_types.h>
#include <linux/jump_label.h>

/* Return values for submit_bio_hook:
 * 0: Continue with normal bio submission
 * 1: Skip normal bio submission (BPF program handled it)
 * negative: Error code
 */
struct bpf_bio_ops {
    /* Called before submitting a bio */
    int (*submit_bio_hook)(struct bio *bio);
    
    /* Called when a bio completes */
    void (*bio_endio_hook)(struct bio *bio);
};

#ifdef CONFIG_BPF_SYSCALL
extern struct static_key_false bpf_bio_ops_enabled;
extern const struct bpf_bio_ops *bpf_bio_ops;

int bpf_bio_ops_init(void);
void bpf_bio_ops_unregister(void);
#else
static inline int bpf_bio_ops_init(void) { return 0; }
static inline void bpf_bio_ops_unregister(void) {}
#endif /* CONFIG_BPF_SYSCALL */

#endif /* _LINUX_BPF_BIO_H */ 