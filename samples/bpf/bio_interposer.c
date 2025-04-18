// SPDX-License-Identifier: GPL-2.0
/* Sample BPF program for bio interposition
 *
 * Copyright (c) 2023 Your Name <your.email@example.com>
 */

#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/bpf_types.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <linux/bio.h>

struct bpf_bio_context {
    __u64 sector;            /* bi_iter.bi_sector */
    __u32 size;              /* bi_iter.bi_size */
    __u32 op;                /* bio_op(bio) */
    __u32 dev_id;            /* Unique device identifier */
    __u32 flags;             /* bio->bi_opf & ~REQ_OP_MASK */
};

/* Map to store I/O statistics per operation type */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8);
    __type(key, __u32);      /* Operation type */
    __type(value, __u64);    /* Count */
} io_stats SEC(".maps");

/* Map to store I/O latency per operation type */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8);
    __type(key, __u32);      /* Operation type */
    __type(value, __u64);    /* Total latency in ns */
} io_latency SEC(".maps");

/* Map to track bio submission time for latency calculation */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 1024);
    __type(key, void *);     /* bio pointer */
    __type(value, __u64);    /* ktime in ns */
} bio_start_time SEC(".maps");

/* Filter specific devices */
#define TARGET_DEVICE_ID 8    /* Change to match your device */

/* Function to get current time in nanoseconds */
static __u64 get_time_ns(void)
{
    return bpf_ktime_get_ns();
}

/* Implementation of the BPF hook for submit_bio */
SEC("struct_ops/submit_bio_hook")
int bpf_bio_submit_hook(struct bio *bio)
{
    __u32 op;
    __u64 *count;
    __u64 ts = get_time_ns();
    struct bpf_bio_context ctx = {};
    
    /* Get the bio context */
    bpf_core_read(&ctx.sector, sizeof(ctx.sector), &bio->bi_iter.bi_sector);
    bpf_core_read(&ctx.size, sizeof(ctx.size), &bio->bi_iter.bi_size);
    bpf_core_read(&ctx.op, sizeof(ctx.op), bio);   /* Assuming bio_op is accessible */
    bpf_core_read(&ctx.flags, sizeof(ctx.flags), &bio->bi_opf);
    
    /* Filter by device if needed */
    bpf_core_read(&ctx.dev_id, sizeof(ctx.dev_id), bio);
    if (TARGET_DEVICE_ID != 0 && ctx.dev_id != TARGET_DEVICE_ID) {
        return 0; /* Continue with normal submission for non-target devices */
    }
    
    /* Store start time for latency tracking */
    bpf_map_update_elem(&bio_start_time, &bio, &ts, BPF_ANY);
    
    /* Update statistics */
    op = ctx.op;
    count = bpf_map_lookup_elem(&io_stats, &op);
    if (count) {
        (*count)++;
    } else {
        __u64 one = 1;
        bpf_map_update_elem(&io_stats, &op, &one, BPF_ANY);
    }
    
    /* Example: you could modify I/O here, block certain operations, etc. */
    
    /* Continue with normal bio submission */
    return 0;
}

/* Implementation of the BPF hook for bio_endio */
SEC("struct_ops/bio_endio_hook")
void bpf_bio_endio_hook(struct bio *bio)
{
    __u32 op;
    __u64 *start_time;
    __u64 *total_latency;
    __u64 now = get_time_ns();
    __u64 latency;
    struct bpf_bio_context ctx = {};
    
    /* Get the bio context */
    bpf_core_read(&ctx.op, sizeof(ctx.op), bio);  /* Assuming bio_op is accessible */
    
    /* Calculate latency if we have the start time */
    start_time = bpf_map_lookup_elem(&bio_start_time, &bio);
    if (start_time) {
        latency = now - *start_time;
        
        /* Update latency statistics */
        op = ctx.op;
        total_latency = bpf_map_lookup_elem(&io_latency, &op);
        if (total_latency) {
            (*total_latency) += latency;
        } else {
            bpf_map_update_elem(&io_latency, &op, &latency, BPF_ANY);
        }
        
        /* Remove the entry from the start time map */
        bpf_map_delete_elem(&bio_start_time, &bio);
    }
    
    /* Example: you could log or analyze completion status here */
}

/* Structure definition for BPF struct_ops */
struct bpf_bio_ops {
    int (*submit_bio_hook)(struct bio *bio);
    void (*bio_endio_hook)(struct bio *bio);
} bio_ops SEC(".struct_ops") = {
    .submit_bio_hook = bpf_bio_submit_hook,
    .bio_endio_hook = bpf_bio_endio_hook,
};

char LICENSE[] SEC("license") = "GPL"; 