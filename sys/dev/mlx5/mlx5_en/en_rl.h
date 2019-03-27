/*-
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __MLX5_EN_RL_H__
#define __MLX5_EN_RL_H__

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/proc.h>
#include <sys/condvar.h>
#include <sys/interrupt.h>
#include <sys/unistd.h>

#include <sys/queue.h>

#define	MLX5E_RL_MAX_WORKERS		128	/* limited by Toeplitz hash */
#define	MLX5E_RL_MAX_TX_RATES		(64 * 1024)	/* software limit */
#define	MLX5E_RL_DEF_SQ_PER_WORKER	(12 * 1024)	/* software limit */
#define	MLX5E_RL_MAX_SQS		(120 * 1024)	/* software limit */

#define	MLX5E_RL_TX_COAL_USEC_DEFAULT	32
#define	MLX5E_RL_TX_COAL_PKTS_DEFAULT	4
#define	MLX5E_RL_TX_COAL_MODE_DEFAULT	0
#define	MLX5E_RL_TX_COMP_FACT_DEFAULT	1

#define	MLX5E_RL_WORKER_LOCK(rlw)		mtx_lock(&(rlw)->mtx)
#define	MLX5E_RL_WORKER_UNLOCK(rlw)		mtx_unlock(&(rlw)->mtx)

#define	MLX5E_RL_RLOCK(rl) sx_slock(&(rl)->rl_sxlock)
#define	MLX5E_RL_RUNLOCK(rl) sx_sunlock(&(rl)->rl_sxlock)

#define	MLX5E_RL_WLOCK(rl) sx_xlock(&(rl)->rl_sxlock)
#define	MLX5E_RL_WUNLOCK(rl) sx_xunlock(&(rl)->rl_sxlock)

#define	MLX5E_RL_PARAMS(m) \
  m(+1, u64 tx_queue_size, "tx_queue_size", "Default send queue size") \
  m(+1, u64 tx_coalesce_usecs, "tx_coalesce_usecs", "Limit in usec for joining TX packets") \
  m(+1, u64 tx_coalesce_pkts, "tx_coalesce_pkts", "Maximum number of TX packets to join") \
  m(+1, u64 tx_coalesce_mode, "tx_coalesce_mode", "0: EQE mode 1: CQE mode") \
  m(+1, u64 tx_completion_fact, "tx_completion_fact", "1..MAX: Completion event ratio") \
  m(+1, u64 tx_completion_fact_max, "tx_completion_fact_max", "Maximum completion event ratio") \
  m(+1, u64 tx_worker_threads_max, "tx_worker_threads_max", "Max number of TX worker threads") \
  m(+1, u64 tx_worker_threads_def, "tx_worker_threads_def", "Default number of TX worker threads") \
  m(+1, u64 tx_channels_per_worker_max, "tx_channels_per_worker_max", "Max number of TX channels per worker") \
  m(+1, u64 tx_channels_per_worker_def, "tx_channels_per_worker_def", "Default number of TX channels per worker") \
  m(+1, u64 tx_rates_max, "tx_rates_max", "Max number of TX rates") \
  m(+1, u64 tx_rates_def, "tx_rates_def", "Default number of TX rates") \
  m(+1, u64 tx_limit_min, "tx_limit_min", "Minimum TX rate in bits/s") \
  m(+1, u64 tx_limit_max, "tx_limit_max", "Maximum TX rate in bits/s") \
  m(+1, u64 tx_burst_size, "tx_burst_size", "Current burst size in number of packets. A value of zero means use firmware default.") \
  m(+1, u64 tx_burst_size_max, "tx_burst_size_max", "Maximum burst size in number of packets") \
  m(+1, u64 tx_burst_size_min, "tx_burst_size_min", "Minimum burst size in number of packets")

#define	MLX5E_RL_PARAMS_NUM (0 MLX5E_RL_PARAMS(MLX5E_STATS_COUNT))

#define MLX5E_RL_STATS(m) \
  m(+1, u64 tx_allocate_resource_failure, "tx_allocate_resource_failure", "Number of times firmware resource allocation failed") \
  m(+1, u64 tx_add_new_rate_failure, "tx_add_new_rate_failure", "Number of times adding a new firmware rate failed") \
  m(+1, u64 tx_modify_rate_failure, "tx_modify_rate_failure", "Number of times modifying a firmware rate failed") \
  m(+1, u64 tx_active_connections, "tx_active_connections", "Number of active connections") \
  m(+1, u64 tx_open_queues, "tx_open_queues", "Number of open TX queues") \
  m(+1, u64 tx_available_resource_failure, "tx_available_resource_failure", "Number of times TX resources were not available")

#define MLX5E_RL_STATS_NUM (0 MLX5E_RL_STATS(MLX5E_STATS_COUNT))

#define	MLX5E_RL_TABLE_PARAMS(m) \
  m(+1, u64 tx_limit_add, "tx_limit_add", "Add TX rate limit in bits/s to empty slot") \
  m(+1, u64 tx_limit_clr, "tx_limit_clr", "Clear all TX rates in table") \
  m(+1, u64 tx_allowed_deviation, "tx_allowed_deviation", "Relative rate deviation allowed in 1/1000") \
  m(+1, u64 tx_allowed_deviation_min, "tx_allowed_deviation_min", "Minimum allowed rate deviation in 1/1000") \
  m(+1, u64 tx_allowed_deviation_max, "tx_allowed_deviation_max", "Maximum allowed rate deviation in 1/1000")

#define	MLX5E_RL_TABLE_PARAMS_NUM (0 MLX5E_RL_TABLE_PARAMS(MLX5E_STATS_COUNT))

#define	MLX5E_RL_PARAMS_INDEX(n)			\
    (__offsetof(struct mlx5e_rl_params, n) / sizeof(uint64_t))

struct mlx5e_priv;

/* Indicates channel's state */
enum {
	MLX5E_RL_ST_FREE,
	MLX5E_RL_ST_USED,
	MLX5E_RL_ST_MODIFY,
	MLX5E_RL_ST_DESTROY,
};

struct mlx5e_rl_stats {
	u64	arg [0];
	MLX5E_RL_STATS(MLX5E_STATS_VAR)
};

struct mlx5e_rl_params {
	u64	arg [0];
	MLX5E_RL_PARAMS(MLX5E_STATS_VAR)
	u64	table_arg [0];
	MLX5E_RL_TABLE_PARAMS(MLX5E_STATS_VAR)
};

struct mlx5e_rl_channel_param {
	struct mlx5e_sq_param sq;
	struct mlx5e_cq_param cq;
};

struct mlx5e_rl_channel {
	struct mlx5e_snd_tag tag;
	STAILQ_ENTRY(mlx5e_rl_channel) entry;
	struct mlx5e_sq * volatile sq;
	struct mlx5e_rl_worker *worker;
	uint64_t new_rate;
	uint64_t init_rate;
	uint64_t last_rate;
	uint16_t last_burst;
	uint16_t state;
};

struct mlx5e_rl_worker {
	struct mtx mtx;
	struct cv cv;
	STAILQ_HEAD(, mlx5e_rl_channel) index_list_head;
	STAILQ_HEAD(, mlx5e_rl_channel) process_head;
	struct mlx5e_priv *priv;
	struct mlx5e_rl_channel *channels;
	unsigned worker_done;
};

struct mlx5e_rl_priv_data {
	struct sx rl_sxlock;
	struct sysctl_ctx_list ctx;
	struct mlx5e_rl_channel_param chan_param;
	struct mlx5e_rl_params param;
	struct mlx5e_rl_stats stats;
	struct mlx5_uar sq_uar;
	struct mlx5e_rl_worker *workers;
	struct mlx5e_priv *priv;
	uint64_t *rate_limit_table;
	unsigned opened;
	uint32_t tisn;
};

int mlx5e_rl_init(struct mlx5e_priv *priv);
void mlx5e_rl_cleanup(struct mlx5e_priv *priv);
void mlx5e_rl_refresh_sq_inline(struct mlx5e_rl_priv_data *rl);
if_snd_tag_alloc_t mlx5e_rl_snd_tag_alloc;
if_snd_tag_modify_t mlx5e_rl_snd_tag_modify;
if_snd_tag_query_t mlx5e_rl_snd_tag_query;
if_snd_tag_free_t mlx5e_rl_snd_tag_free;

#endif		/* __MLX5_EN_RL_H__ */
