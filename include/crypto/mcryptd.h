/*
 * Software async multibuffer crypto daemon headers
 *
 *    Author:
 *             Tim Chen <tim.c.chen@linux.intel.com>
 *
 *    Copyright (c) 2014, Intel Corporation.
 */

#ifndef _CRYPTO_MCRYPT_H
#define _CRYPTO_MCRYPT_H

#include <linux/crypto.h>
#include <linux/kernel.h>
#include <crypto/hash.h>

struct mcryptd_ahash {
	struct crypto_ahash base;
};

static inline struct mcryptd_ahash *__mcryptd_ahash_cast(
	struct crypto_ahash *tfm)
{
	return (struct mcryptd_ahash *)tfm;
}

struct mcryptd_cpu_queue {
	struct crypto_queue queue;
	struct work_struct work;
};

struct mcryptd_queue {
	struct mcryptd_cpu_queue __percpu *cpu_queue;
};

struct mcryptd_instance_ctx {
	struct crypto_spawn spawn;
	struct mcryptd_queue *queue;
};

struct mcryptd_hash_ctx {
	struct crypto_ahash *child;
	struct mcryptd_alg_state *alg_state;
};

struct mcryptd_tag {
	/* seq number of request */
	unsigned seq_num;
	/* arrival time of request */
	unsigned long arrival;
	unsigned long expire;
	int	cpu;
};

struct mcryptd_hash_request_ctx {
	struct list_head waiter;
	crypto_completion_t complete;
	struct mcryptd_tag tag;
	struct crypto_hash_walk walk;
	u8 *out;
	int flag;
	struct ahash_request areq;
};

struct mcryptd_ahash *mcryptd_alloc_ahash(const char *alg_name,
					u32 type, u32 mask);
struct crypto_ahash *mcryptd_ahash_child(struct mcryptd_ahash *tfm);
struct ahash_request *mcryptd_ahash_desc(struct ahash_request *req);
void mcryptd_free_ahash(struct mcryptd_ahash *tfm);
void mcryptd_flusher(struct work_struct *work);

enum mcryptd_req_type {
	MCRYPTD_NONE,
	MCRYPTD_UPDATE,
	MCRYPTD_FINUP,
	MCRYPTD_DIGEST,
	MCRYPTD_FINAL
};

struct mcryptd_alg_cstate {
	unsigned long next_flush;
	unsigned next_seq_num;
	bool	flusher_engaged;
	struct  delayed_work flush;
	int	cpu;
	struct  mcryptd_alg_state *alg_state;
	void	*mgr;
	spinlock_t work_lock;
	struct list_head work_list;
	struct list_head flush_list;
};

struct mcryptd_alg_state {
	struct mcryptd_alg_cstate __percpu *alg_cstate;
	unsigned long (*flusher)(struct mcryptd_alg_cstate *cstate);
};

/* return delay in jiffies from current time */
static inline unsigned long get_delay(unsigned long t)
{
	long delay;

	delay = (long) t - (long) jiffies;
	if (delay <= 0)
		return 0;
	else
		return (unsigned long) delay;
}

void mcryptd_arm_flusher(struct mcryptd_alg_cstate *cstate, unsigned long delay);

#endif
