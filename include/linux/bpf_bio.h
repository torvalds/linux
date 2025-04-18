/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BPF_BIO_H
#define _LINUX_BPF_BIO_H

#include <linux/bio.h>
#include <linux/bpf.h>
#include <linux/jump_label.h>
#include <linux/blkdev.h>

/**
 * Return values for submit_bio_hook:
 * 0: Continue with normal bio submission
 * 1: Skip normal bio submission (BPF program handled it)
 * negative: Error code
 */

/**
 * struct bpf_bio_context - Context information for BPF programs
 * @sector: Starting sector for this bio
 * @size: Size of the I/O in bytes
 * @op: Operation type (READ, WRITE, etc.)
 * @dev_id: Device ID associated with the bio
 * @flags: Operation flags
 * 
 * This context is passed to BPF programs to provide information
 * about the bio without exposing the actual bio structure.
 */
struct bpf_bio_context {
    u64 sector;            /* bi_iter.bi_sector */
    u32 size;              /* bi_iter.bi_size */
    u32 op;                /* bio_op(bio) */
    u32 dev_id;            /* Unique device identifier */
    u32 flags;             /* bio->bi_opf & ~REQ_OP_MASK */
};

/**
 * struct bpf_bio_ops - BPF operations for block I/O handling
 * @submit_bio_hook: Called before submitting a bio
 * @bio_endio_hook: Called when a bio completes
 *
 * This struct contains the operations that can be implemented by BPF programs
 * for interposing on block I/O operations.
 */
struct bpf_bio_ops {
    /* Called before submitting a bio */
    int (*submit_bio_hook)(struct bio *bio);
    
    /* Called when a bio completes */
    void (*bio_endio_hook)(struct bio *bio);
};

/* Helper functions for internal kernel use */
void bpf_bio_get_context(struct bio *bio, struct bpf_bio_context *ctx);
void bpf_bio_set_status(struct bio *bio, int status);
void bpf_bio_resubmit(struct bio *bio);

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