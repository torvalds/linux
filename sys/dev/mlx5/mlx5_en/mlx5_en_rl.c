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

#include "en.h"

#ifdef RATELIMIT

static int mlx5e_rl_open_workers(struct mlx5e_priv *);
static void mlx5e_rl_close_workers(struct mlx5e_priv *);
static int mlx5e_rl_sysctl_show_rate_table(SYSCTL_HANDLER_ARGS);
static void mlx5e_rl_sysctl_add_u64_oid(struct mlx5e_rl_priv_data *, unsigned x,
    struct sysctl_oid *, const char *name, const char *desc);
static void mlx5e_rl_sysctl_add_stats_u64_oid(struct mlx5e_rl_priv_data *rl, unsigned x,
      struct sysctl_oid *node, const char *name, const char *desc);
static int mlx5e_rl_tx_limit_add(struct mlx5e_rl_priv_data *, uint64_t value);
static int mlx5e_rl_tx_limit_clr(struct mlx5e_rl_priv_data *, uint64_t value);

static void
mlx5e_rl_build_sq_param(struct mlx5e_rl_priv_data *rl,
    struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);
	uint8_t log_sq_size = order_base_2(rl->param.tx_queue_size);

	MLX5_SET(wq, wq, log_wq_sz, log_sq_size);
	MLX5_SET(wq, wq, log_wq_stride, ilog2(MLX5_SEND_WQE_BB));
	MLX5_SET(wq, wq, pd, rl->priv->pdn);

	param->wq.buf_numa_node = 0;
	param->wq.db_numa_node = 0;
	param->wq.linear = 1;
}

static void
mlx5e_rl_build_cq_param(struct mlx5e_rl_priv_data *rl,
    struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;
	uint8_t log_sq_size = order_base_2(rl->param.tx_queue_size);

	MLX5_SET(cqc, cqc, log_cq_size, log_sq_size);
	MLX5_SET(cqc, cqc, cq_period, rl->param.tx_coalesce_usecs);
	MLX5_SET(cqc, cqc, cq_max_count, rl->param.tx_coalesce_pkts);

	switch (rl->param.tx_coalesce_mode) {
	case 0:
		MLX5_SET(cqc, cqc, cq_period_mode, MLX5_CQ_PERIOD_MODE_START_FROM_EQE);
		break;
	default:
		if (MLX5_CAP_GEN(rl->priv->mdev, cq_period_start_from_cqe))
			MLX5_SET(cqc, cqc, cq_period_mode, MLX5_CQ_PERIOD_MODE_START_FROM_CQE);
		else
			MLX5_SET(cqc, cqc, cq_period_mode, MLX5_CQ_PERIOD_MODE_START_FROM_EQE);
		break;
	}
}

static void
mlx5e_rl_build_channel_param(struct mlx5e_rl_priv_data *rl,
    struct mlx5e_rl_channel_param *cparam)
{
	memset(cparam, 0, sizeof(*cparam));

	mlx5e_rl_build_sq_param(rl, &cparam->sq);
	mlx5e_rl_build_cq_param(rl, &cparam->cq);
}

static int
mlx5e_rl_create_sq(struct mlx5e_priv *priv, struct mlx5e_sq *sq,
    struct mlx5e_sq_param *param, int ix)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	void *sqc = param->sqc;
	void *sqc_wq = MLX5_ADDR_OF(sqc, sqc, wq);
	int err;

	/* Create DMA descriptor TAG */
	if ((err = -bus_dma_tag_create(
	    bus_get_dma_tag(mdev->pdev->dev.bsddev),
	    1,				/* any alignment */
	    0,				/* no boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MLX5E_MAX_TX_PAYLOAD_SIZE,	/* maxsize */
	    MLX5E_MAX_TX_MBUF_FRAGS,	/* nsegments */
	    MLX5E_MAX_TX_MBUF_SIZE,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockfuncarg */
	    &sq->dma_tag)))
		goto done;

	/* use shared UAR */
	sq->uar = priv->rl.sq_uar;

	err = mlx5_wq_cyc_create(mdev, &param->wq, sqc_wq, &sq->wq,
	    &sq->wq_ctrl);
	if (err)
		goto err_free_dma_tag;

	sq->wq.db = &sq->wq.db[MLX5_SND_DBR];
	/*
	 * The sq->bf_buf_size variable is intentionally left zero so
	 * that the doorbell writes will occur at the same memory
	 * location.
	 */

	err = mlx5e_alloc_sq_db(sq);
	if (err)
		goto err_sq_wq_destroy;

	sq->mkey_be = cpu_to_be32(priv->mr.key);
	sq->ifp = priv->ifp;
	sq->priv = priv;

	mlx5e_update_sq_inline(sq);

	return (0);

err_sq_wq_destroy:
	mlx5_wq_destroy(&sq->wq_ctrl);
err_free_dma_tag:
	bus_dma_tag_destroy(sq->dma_tag);
done:
	return (err);
}

static void
mlx5e_rl_destroy_sq(struct mlx5e_sq *sq)
{

	mlx5e_free_sq_db(sq);
	mlx5_wq_destroy(&sq->wq_ctrl);
}

static int
mlx5e_rl_open_sq(struct mlx5e_priv *priv, struct mlx5e_sq *sq,
    struct mlx5e_sq_param *param, int ix)
{
	int err;

	err = mlx5e_rl_create_sq(priv, sq, param, ix);
	if (err)
		return (err);

	err = mlx5e_enable_sq(sq, param, priv->rl.tisn);
	if (err)
		goto err_destroy_sq;

	err = mlx5e_modify_sq(sq, MLX5_SQC_STATE_RST, MLX5_SQC_STATE_RDY);
	if (err)
		goto err_disable_sq;

	return (0);

err_disable_sq:
	mlx5e_disable_sq(sq);
err_destroy_sq:
	mlx5e_rl_destroy_sq(sq);

	return (err);
}

static void
mlx5e_rl_chan_mtx_init(struct mlx5e_priv *priv, struct mlx5e_sq *sq)
{
	mtx_init(&sq->lock, "mlx5tx-rl", NULL, MTX_DEF);
	mtx_init(&sq->comp_lock, "mlx5comp-rl", NULL, MTX_DEF);

	callout_init_mtx(&sq->cev_callout, &sq->lock, 0);

	sq->cev_factor = priv->rl.param.tx_completion_fact;

	/* ensure the TX completion event factor is not zero */
	if (sq->cev_factor == 0)
		sq->cev_factor = 1;
}

static int
mlx5e_rl_open_channel(struct mlx5e_rl_worker *rlw, int eq_ix,
    struct mlx5e_rl_channel_param *cparam,
    struct mlx5e_sq *volatile *ppsq)
{
	struct mlx5e_priv *priv = rlw->priv;
	struct mlx5e_sq *sq;
	int err;

	sq = malloc(sizeof(*sq), M_MLX5EN, M_WAITOK | M_ZERO);

	/* init mutexes */
	mlx5e_rl_chan_mtx_init(priv, sq);

	/* open TX completion queue */
	err = mlx5e_open_cq(priv, &cparam->cq, &sq->cq,
	    &mlx5e_tx_cq_comp, eq_ix);
	if (err)
		goto err_free;

	err = mlx5e_rl_open_sq(priv, sq, &cparam->sq, eq_ix);
	if (err)
		goto err_close_tx_cq;

	/* store TX channel pointer */
	*ppsq = sq;

	/* poll TX queue initially */
	sq->cq.mcq.comp(&sq->cq.mcq);

	return (0);

err_close_tx_cq:
	mlx5e_close_cq(&sq->cq);

err_free:
	/* destroy mutexes */
	mtx_destroy(&sq->lock);
	mtx_destroy(&sq->comp_lock);
	free(sq, M_MLX5EN);
	atomic_add_64(&priv->rl.stats.tx_allocate_resource_failure, 1ULL);
	return (err);
}

static void
mlx5e_rl_close_channel(struct mlx5e_sq *volatile *ppsq)
{
	struct mlx5e_sq *sq = *ppsq;

	/* check if channel is already closed */
	if (sq == NULL)
		return;
	/* ensure channel pointer is no longer used */
	*ppsq = NULL;

	/* teardown and destroy SQ */
	mlx5e_drain_sq(sq);
	mlx5e_disable_sq(sq);
	mlx5e_rl_destroy_sq(sq);

	/* close CQ */
	mlx5e_close_cq(&sq->cq);

	/* destroy mutexes */
	mtx_destroy(&sq->lock);
	mtx_destroy(&sq->comp_lock);

	free(sq, M_MLX5EN);
}

static void
mlx5e_rl_sync_tx_completion_fact(struct mlx5e_rl_priv_data *rl)
{
	/*
	 * Limit the maximum distance between completion events to
	 * half of the currently set TX queue size.
	 *
	 * The maximum number of queue entries a single IP packet can
	 * consume is given by MLX5_SEND_WQE_MAX_WQEBBS.
	 *
	 * The worst case max value is then given as below:
	 */
	uint64_t max = rl->param.tx_queue_size /
	    (2 * MLX5_SEND_WQE_MAX_WQEBBS);

	/*
	 * Update the maximum completion factor value in case the
	 * tx_queue_size field changed. Ensure we don't overflow
	 * 16-bits.
	 */
	if (max < 1)
		max = 1;
	else if (max > 65535)
		max = 65535;
	rl->param.tx_completion_fact_max = max;

	/*
	 * Verify that the current TX completion factor is within the
	 * given limits:
	 */
	if (rl->param.tx_completion_fact < 1)
		rl->param.tx_completion_fact = 1;
	else if (rl->param.tx_completion_fact > max)
		rl->param.tx_completion_fact = max;
}

static int
mlx5e_rl_modify_sq(struct mlx5e_sq *sq, uint16_t rl_index)
{
	struct mlx5e_priv *priv = sq->priv;
	struct mlx5_core_dev *mdev = priv->mdev;

	void *in;
	void *sqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_sq_in);
	in = mlx5_vzalloc(inlen);
	if (in == NULL)
		return (-ENOMEM);

	sqc = MLX5_ADDR_OF(modify_sq_in, in, ctx);

	MLX5_SET(modify_sq_in, in, sqn, sq->sqn);
	MLX5_SET(modify_sq_in, in, sq_state, MLX5_SQC_STATE_RDY);
	MLX5_SET64(modify_sq_in, in, modify_bitmask, 1);
	MLX5_SET(sqc, sqc, state, MLX5_SQC_STATE_RDY);
	MLX5_SET(sqc, sqc, packet_pacing_rate_limit_index, rl_index);

	err = mlx5_core_modify_sq(mdev, in, inlen);

	kvfree(in);

	return (err);
}

/*
 * This function will search the configured rate limit table for the
 * best match to avoid that a single socket based application can
 * allocate all the available hardware rates. If the user selected
 * rate deviates too much from the closes rate available in the rate
 * limit table, unlimited rate will be selected.
 */
static uint64_t
mlx5e_rl_find_best_rate_locked(struct mlx5e_rl_priv_data *rl, uint64_t user_rate)
{
	uint64_t distance = -1ULL;
	uint64_t diff;
	uint64_t retval = 0;		/* unlimited */
	uint64_t x;

	/* search for closest rate */
	for (x = 0; x != rl->param.tx_rates_def; x++) {
		uint64_t rate = rl->rate_limit_table[x];
		if (rate == 0)
			continue;

		if (rate > user_rate)
			diff = rate - user_rate;
		else
			diff = user_rate - rate;

		/* check if distance is smaller than previous rate */
		if (diff < distance) {
			distance = diff;
			retval = rate;
		}
	}

	/* range check for multiplication below */
	if (user_rate > rl->param.tx_limit_max)
		user_rate = rl->param.tx_limit_max;

	/* fallback to unlimited, if rate deviates too much */
	if (distance > howmany(user_rate *
	    rl->param.tx_allowed_deviation, 1000ULL))
		retval = 0;

	return (retval);
}

/*
 * This function sets the requested rate for a rate limit channel, in
 * bits per second. The requested rate will be filtered through the
 * find best rate function above.
 */
static int
mlx5e_rlw_channel_set_rate_locked(struct mlx5e_rl_worker *rlw,
    struct mlx5e_rl_channel *channel, uint64_t rate)
{
	struct mlx5e_rl_priv_data *rl = &rlw->priv->rl;
	struct mlx5e_sq *sq;
	uint64_t temp;
	uint16_t index;
	uint16_t burst;
	int error;

	if (rate != 0) {
		MLX5E_RL_WORKER_UNLOCK(rlw);

		MLX5E_RL_RLOCK(rl);

		/* get current burst size in bytes */
		temp = rl->param.tx_burst_size *
		    MLX5E_SW2HW_MTU(rlw->priv->ifp->if_mtu);

		/* limit burst size to 64K currently */
		if (temp > 65535)
			temp = 65535;
		burst = temp;

		/* find best rate */
		rate = mlx5e_rl_find_best_rate_locked(rl, rate);

		MLX5E_RL_RUNLOCK(rl);

		if (rate == 0) {
			/* rate doesn't exist, fallback to unlimited */
			index = 0;
			rate = 0;
			atomic_add_64(&rlw->priv->rl.stats.tx_modify_rate_failure, 1ULL);
		} else {
			/* get a reference on the new rate */
			error = -mlx5_rl_add_rate(rlw->priv->mdev,
			    howmany(rate, 1000), burst, &index);

			if (error != 0) {
				/* adding rate failed, fallback to unlimited */
				index = 0;
				rate = 0;
				atomic_add_64(&rlw->priv->rl.stats.tx_add_new_rate_failure, 1ULL);
			}
		}
		MLX5E_RL_WORKER_LOCK(rlw);
	} else {
		index = 0;
		burst = 0;	/* default */
	}

	/* atomically swap rates */
	temp = channel->last_rate;
	channel->last_rate = rate;
	rate = temp;

	/* atomically swap burst size */
	temp = channel->last_burst;
	channel->last_burst = burst;
	burst = temp;

	MLX5E_RL_WORKER_UNLOCK(rlw);
	/* put reference on the old rate, if any */
	if (rate != 0) {
		mlx5_rl_remove_rate(rlw->priv->mdev,
		    howmany(rate, 1000), burst);
	}

	/* set new rate, if SQ is running */
	sq = channel->sq;
	if (sq != NULL && READ_ONCE(sq->running) != 0) {
		error = mlx5e_rl_modify_sq(sq, index);
		if (error != 0)
			atomic_add_64(&rlw->priv->rl.stats.tx_modify_rate_failure, 1ULL);
	} else
		error = 0;
	MLX5E_RL_WORKER_LOCK(rlw);

	return (-error);
}

static void
mlx5e_rl_worker(void *arg)
{
	struct thread *td;
	struct mlx5e_rl_worker *rlw = arg;
	struct mlx5e_rl_channel *channel;
	struct mlx5e_priv *priv;
	unsigned ix;
	uint64_t x;
	int error;

	/* set thread priority */
	td = curthread;

	thread_lock(td);
	sched_prio(td, PI_SWI(SWI_NET));
	thread_unlock(td);

	priv = rlw->priv;

	/* compute completion vector */
	ix = (rlw - priv->rl.workers) %
	    priv->mdev->priv.eq_table.num_comp_vectors;

	/* TODO bind to CPU */

	/* open all the SQs */
	MLX5E_RL_WORKER_LOCK(rlw);
	for (x = 0; x < priv->rl.param.tx_channels_per_worker_def; x++) {
		struct mlx5e_rl_channel *channel = rlw->channels + x;

#if !defined(HAVE_RL_PRE_ALLOCATE_CHANNELS)
		if (channel->state == MLX5E_RL_ST_FREE)
			continue;
#endif
		MLX5E_RL_WORKER_UNLOCK(rlw);

		MLX5E_RL_RLOCK(&priv->rl);
		error = mlx5e_rl_open_channel(rlw, ix,
		    &priv->rl.chan_param, &channel->sq);
		MLX5E_RL_RUNLOCK(&priv->rl);

		MLX5E_RL_WORKER_LOCK(rlw);
		if (error != 0) {
			if_printf(priv->ifp,
			    "mlx5e_rl_open_channel failed: %d\n", error);
			break;
		}
		mlx5e_rlw_channel_set_rate_locked(rlw, channel, channel->init_rate);
	}
	while (1) {
		if (STAILQ_FIRST(&rlw->process_head) == NULL) {
			/* check if we are tearing down */
			if (rlw->worker_done != 0)
				break;
			cv_wait(&rlw->cv, &rlw->mtx);
		}
		/* check if we are tearing down */
		if (rlw->worker_done != 0)
			break;
		channel = STAILQ_FIRST(&rlw->process_head);
		if (channel != NULL) {
			STAILQ_REMOVE_HEAD(&rlw->process_head, entry);

			switch (channel->state) {
			case MLX5E_RL_ST_MODIFY:
				channel->state = MLX5E_RL_ST_USED;
				MLX5E_RL_WORKER_UNLOCK(rlw);

				/* create channel by demand */
				if (channel->sq == NULL) {
					MLX5E_RL_RLOCK(&priv->rl);
					error = mlx5e_rl_open_channel(rlw, ix,
					    &priv->rl.chan_param, &channel->sq);
					MLX5E_RL_RUNLOCK(&priv->rl);

					if (error != 0) {
						if_printf(priv->ifp,
						    "mlx5e_rl_open_channel failed: %d\n", error);
					} else {
						atomic_add_64(&rlw->priv->rl.stats.tx_open_queues, 1ULL);
					}
				} else {
					mlx5e_resume_sq(channel->sq);
				}

				MLX5E_RL_WORKER_LOCK(rlw);
				/* convert from bytes/s to bits/s and set new rate */
				error = mlx5e_rlw_channel_set_rate_locked(rlw, channel,
				    channel->new_rate * 8ULL);
				if (error != 0) {
					if_printf(priv->ifp,
					    "mlx5e_rlw_channel_set_rate_locked failed: %d\n",
					    error);
				}
				break;

			case MLX5E_RL_ST_DESTROY:
				error = mlx5e_rlw_channel_set_rate_locked(rlw, channel, 0);
				if (error != 0) {
					if_printf(priv->ifp,
					    "mlx5e_rlw_channel_set_rate_locked failed: %d\n",
					    error);
				}
				if (channel->sq != NULL) {
					/*
					 * Make sure all packets are
					 * transmitted before SQ is
					 * returned to free list:
					 */
					MLX5E_RL_WORKER_UNLOCK(rlw);
					mlx5e_drain_sq(channel->sq);
					MLX5E_RL_WORKER_LOCK(rlw);
				}
				/* put the channel back into the free list */
				STAILQ_INSERT_HEAD(&rlw->index_list_head, channel, entry);
				channel->state = MLX5E_RL_ST_FREE;
				atomic_add_64(&priv->rl.stats.tx_active_connections, -1ULL);
				break;
			default:
				/* NOP */
				break;
			}
		}
	}

	/* close all the SQs */
	for (x = 0; x < priv->rl.param.tx_channels_per_worker_def; x++) {
		struct mlx5e_rl_channel *channel = rlw->channels + x;

		/* update the initial rate */
		channel->init_rate = channel->last_rate;

		/* make sure we free up the rate resource */
		mlx5e_rlw_channel_set_rate_locked(rlw, channel, 0);

		if (channel->sq != NULL) {
			MLX5E_RL_WORKER_UNLOCK(rlw);
			mlx5e_rl_close_channel(&channel->sq);
			atomic_add_64(&rlw->priv->rl.stats.tx_open_queues, -1ULL);
			MLX5E_RL_WORKER_LOCK(rlw);
		}
	}

	rlw->worker_done = 0;
	cv_broadcast(&rlw->cv);
	MLX5E_RL_WORKER_UNLOCK(rlw);

	kthread_exit();
}

static int
mlx5e_rl_open_tis(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(create_tis_in)];
	void *tisc = MLX5_ADDR_OF(create_tis_in, in, ctx);

	memset(in, 0, sizeof(in));

	MLX5_SET(tisc, tisc, prio, 0);
	MLX5_SET(tisc, tisc, transport_domain, priv->tdn);

	return (mlx5_core_create_tis(mdev, in, sizeof(in), &priv->rl.tisn));
}

static void
mlx5e_rl_close_tis(struct mlx5e_priv *priv)
{
	mlx5_core_destroy_tis(priv->mdev, priv->rl.tisn);
}

static void
mlx5e_rl_set_default_params(struct mlx5e_rl_params *param,
    struct mlx5_core_dev *mdev)
{
	/* ratelimit workers */
	param->tx_worker_threads_def = mdev->priv.eq_table.num_comp_vectors;
	param->tx_worker_threads_max = MLX5E_RL_MAX_WORKERS;

	/* range check */
	if (param->tx_worker_threads_def == 0 ||
	    param->tx_worker_threads_def > param->tx_worker_threads_max)
		param->tx_worker_threads_def = param->tx_worker_threads_max;

	/* ratelimit channels */
	param->tx_channels_per_worker_def = MLX5E_RL_MAX_SQS /
	    param->tx_worker_threads_def;
	param->tx_channels_per_worker_max = MLX5E_RL_MAX_SQS;

	/* range check */
	if (param->tx_channels_per_worker_def > MLX5E_RL_DEF_SQ_PER_WORKER)
		param->tx_channels_per_worker_def = MLX5E_RL_DEF_SQ_PER_WORKER;

	/* set default burst size */
	param->tx_burst_size = 4;	/* MTUs */

	/*
	 * Set maximum burst size
	 *
	 * The burst size is multiplied by the MTU and clamped to the
	 * range 0 ... 65535 bytes inclusivly before fed into the
	 * firmware.
	 *
	 * NOTE: If the burst size or MTU is changed only ratelimit
	 * connections made after the change will use the new burst
	 * size.
	 */
	param->tx_burst_size_max = 255;

	/* get firmware rate limits in 1000bit/s and convert them to bit/s */
	param->tx_limit_min = mdev->priv.rl_table.min_rate * 1000ULL;
	param->tx_limit_max = mdev->priv.rl_table.max_rate * 1000ULL;

	/* ratelimit table size */
	param->tx_rates_max = mdev->priv.rl_table.max_size;

	/* range check */
	if (param->tx_rates_max > MLX5E_RL_MAX_TX_RATES)
		param->tx_rates_max = MLX5E_RL_MAX_TX_RATES;

	/* set default number of rates */
	param->tx_rates_def = param->tx_rates_max;

	/* set maximum allowed rate deviation */
	if (param->tx_limit_max != 0) {
		/*
		 * Make sure the deviation multiplication doesn't
		 * overflow unsigned 64-bit:
		 */
		param->tx_allowed_deviation_max = -1ULL /
		    param->tx_limit_max;
	}
	/* set default rate deviation */
	param->tx_allowed_deviation = 50;	/* 5.0% */

	/* channel parameters */
	param->tx_queue_size = (1 << MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE);
	param->tx_coalesce_usecs = MLX5E_RL_TX_COAL_USEC_DEFAULT;
	param->tx_coalesce_pkts = MLX5E_RL_TX_COAL_PKTS_DEFAULT;
	param->tx_coalesce_mode = MLX5E_RL_TX_COAL_MODE_DEFAULT;
	param->tx_completion_fact = MLX5E_RL_TX_COMP_FACT_DEFAULT;
}

static const char *mlx5e_rl_params_desc[] = {
	MLX5E_RL_PARAMS(MLX5E_STATS_DESC)
};

static const char *mlx5e_rl_table_params_desc[] = {
	MLX5E_RL_TABLE_PARAMS(MLX5E_STATS_DESC)
};

static const char *mlx5e_rl_stats_desc[] = {
	MLX5E_RL_STATS(MLX5E_STATS_DESC)
};

int
mlx5e_rl_init(struct mlx5e_priv *priv)
{
	struct mlx5e_rl_priv_data *rl = &priv->rl;
	struct sysctl_oid *node;
	struct sysctl_oid *stats;
	char buf[64];
	uint64_t i;
	uint64_t j;
	int error;

	/* check if there is support for packet pacing */
	if (!MLX5_CAP_GEN(priv->mdev, qos) || !MLX5_CAP_QOS(priv->mdev, packet_pacing))
		return (0);

	rl->priv = priv;

	sysctl_ctx_init(&rl->ctx);

	sx_init(&rl->rl_sxlock, "ratelimit-sxlock");

	/* allocate shared UAR for SQs */
	error = mlx5_alloc_map_uar(priv->mdev, &rl->sq_uar);
	if (error)
		goto done;

	/* open own TIS domain for ratelimit SQs */
	error = mlx5e_rl_open_tis(priv);
	if (error)
		goto err_uar;

	/* setup default value for parameters */
	mlx5e_rl_set_default_params(&rl->param, priv->mdev);

	/* update the completion factor */
	mlx5e_rl_sync_tx_completion_fact(rl);

	/* create root node */
	node = SYSCTL_ADD_NODE(&rl->ctx,
	    SYSCTL_CHILDREN(priv->sysctl_ifnet), OID_AUTO,
	    "rate_limit", CTLFLAG_RW, NULL, "Rate limiting support");

	if (node != NULL) {
		/* create SYSCTLs */
		for (i = 0; i != MLX5E_RL_PARAMS_NUM; i++) {
			mlx5e_rl_sysctl_add_u64_oid(rl,
			    MLX5E_RL_PARAMS_INDEX(arg[i]),
			    node, mlx5e_rl_params_desc[2 * i],
			    mlx5e_rl_params_desc[2 * i + 1]);
		}

		stats = SYSCTL_ADD_NODE(&rl->ctx, SYSCTL_CHILDREN(node),
		    OID_AUTO, "stats", CTLFLAG_RD, NULL,
		    "Rate limiting statistics");
		if (stats != NULL) {
			/* create SYSCTLs */
			for (i = 0; i != MLX5E_RL_STATS_NUM; i++) {
				mlx5e_rl_sysctl_add_stats_u64_oid(rl, i,
				    stats, mlx5e_rl_stats_desc[2 * i],
				    mlx5e_rl_stats_desc[2 * i + 1]);
			}
		}
	}

	/* allocate workers array */
	rl->workers = malloc(sizeof(rl->workers[0]) *
	    rl->param.tx_worker_threads_def, M_MLX5EN, M_WAITOK | M_ZERO);

	/* allocate rate limit array */
	rl->rate_limit_table = malloc(sizeof(rl->rate_limit_table[0]) *
	    rl->param.tx_rates_def, M_MLX5EN, M_WAITOK | M_ZERO);

	if (node != NULL) {
		/* create more SYSCTls */
		SYSCTL_ADD_PROC(&rl->ctx, SYSCTL_CHILDREN(node), OID_AUTO,
		    "tx_rate_show", CTLTYPE_STRING | CTLFLAG_RD |
		    CTLFLAG_MPSAFE, rl, 0, &mlx5e_rl_sysctl_show_rate_table,
		    "A", "Show table of all configured TX rates");

		/* try to fetch rate table from kernel environment */
		for (i = 0; i != rl->param.tx_rates_def; i++) {
			/* compute path for tunable */
			snprintf(buf, sizeof(buf), "dev.mce.%d.rate_limit.tx_rate_add_%d",
			    device_get_unit(priv->mdev->pdev->dev.bsddev), (int)i);
			if (TUNABLE_QUAD_FETCH(buf, &j))
				mlx5e_rl_tx_limit_add(rl, j);
		}

		/* setup rate table sysctls */
		for (i = 0; i != MLX5E_RL_TABLE_PARAMS_NUM; i++) {
			mlx5e_rl_sysctl_add_u64_oid(rl,
			    MLX5E_RL_PARAMS_INDEX(table_arg[i]),
			    node, mlx5e_rl_table_params_desc[2 * i],
			    mlx5e_rl_table_params_desc[2 * i + 1]);
		}
	}

	for (j = 0; j < rl->param.tx_worker_threads_def; j++) {
		struct mlx5e_rl_worker *rlw = rl->workers + j;

		rlw->priv = priv;

		cv_init(&rlw->cv, "mlx5-worker-cv");
		mtx_init(&rlw->mtx, "mlx5-worker-mtx", NULL, MTX_DEF);
		STAILQ_INIT(&rlw->index_list_head);
		STAILQ_INIT(&rlw->process_head);

		rlw->channels = malloc(sizeof(rlw->channels[0]) *
		    rl->param.tx_channels_per_worker_def, M_MLX5EN, M_WAITOK | M_ZERO);

		MLX5E_RL_WORKER_LOCK(rlw);
		for (i = 0; i < rl->param.tx_channels_per_worker_def; i++) {
			struct mlx5e_rl_channel *channel = rlw->channels + i;
			channel->worker = rlw;
			channel->tag.m_snd_tag.ifp = priv->ifp;
			channel->tag.type = IF_SND_TAG_TYPE_RATE_LIMIT;
			STAILQ_INSERT_TAIL(&rlw->index_list_head, channel, entry);
		}
		MLX5E_RL_WORKER_UNLOCK(rlw);
	}

	PRIV_LOCK(priv);
	error = mlx5e_rl_open_workers(priv);
	PRIV_UNLOCK(priv);

	if (error != 0) {
		if_printf(priv->ifp,
		    "mlx5e_rl_open_workers failed: %d\n", error);
	}

	return (0);

err_uar:
	mlx5_unmap_free_uar(priv->mdev, &rl->sq_uar);
done:
	sysctl_ctx_free(&rl->ctx);
	sx_destroy(&rl->rl_sxlock);
	return (error);
}

static int
mlx5e_rl_open_workers(struct mlx5e_priv *priv)
{
	struct mlx5e_rl_priv_data *rl = &priv->rl;
	struct thread *rl_thread = NULL;
	struct proc *rl_proc = NULL;
	uint64_t j;
	int error;

	if (priv->gone || rl->opened)
		return (-EINVAL);

	MLX5E_RL_WLOCK(rl);
	/* compute channel parameters once */
	mlx5e_rl_build_channel_param(rl, &rl->chan_param);
	MLX5E_RL_WUNLOCK(rl);

	for (j = 0; j < rl->param.tx_worker_threads_def; j++) {
		struct mlx5e_rl_worker *rlw = rl->workers + j;

		/* start worker thread */
		error = kproc_kthread_add(mlx5e_rl_worker, rlw, &rl_proc, &rl_thread,
		    RFHIGHPID, 0, "mlx5-ratelimit", "mlx5-rl-worker-thread-%d", (int)j);
		if (error != 0) {
			if_printf(rl->priv->ifp,
			    "kproc_kthread_add failed: %d\n", error);
			rlw->worker_done = 1;
		}
	}

	rl->opened = 1;

	return (0);
}

static void
mlx5e_rl_close_workers(struct mlx5e_priv *priv)
{
	struct mlx5e_rl_priv_data *rl = &priv->rl;
	uint64_t y;

	if (rl->opened == 0)
		return;

	/* tear down worker threads simultaneously */
	for (y = 0; y < rl->param.tx_worker_threads_def; y++) {
		struct mlx5e_rl_worker *rlw = rl->workers + y;

		/* tear down worker before freeing SQs */
		MLX5E_RL_WORKER_LOCK(rlw);
		if (rlw->worker_done == 0) {
			rlw->worker_done = 1;
			cv_broadcast(&rlw->cv);
		} else {
			/* XXX thread not started */
			rlw->worker_done = 0;
		}
		MLX5E_RL_WORKER_UNLOCK(rlw);
	}

	/* wait for worker threads to exit */
	for (y = 0; y < rl->param.tx_worker_threads_def; y++) {
		struct mlx5e_rl_worker *rlw = rl->workers + y;

		/* tear down worker before freeing SQs */
		MLX5E_RL_WORKER_LOCK(rlw);
		while (rlw->worker_done != 0)
			cv_wait(&rlw->cv, &rlw->mtx);
		MLX5E_RL_WORKER_UNLOCK(rlw);
	}

	rl->opened = 0;
}

static void
mlx5e_rl_reset_rates(struct mlx5e_rl_priv_data *rl)
{
	unsigned x;

	MLX5E_RL_WLOCK(rl);
	for (x = 0; x != rl->param.tx_rates_def; x++)
		rl->rate_limit_table[x] = 0;
	MLX5E_RL_WUNLOCK(rl);
}

void
mlx5e_rl_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_rl_priv_data *rl = &priv->rl;
	uint64_t y;

	/* check if there is support for packet pacing */
	if (!MLX5_CAP_GEN(priv->mdev, qos) || !MLX5_CAP_QOS(priv->mdev, packet_pacing))
		return;

	/* TODO check if there is support for packet pacing */

	sysctl_ctx_free(&rl->ctx);

	PRIV_LOCK(priv);
	mlx5e_rl_close_workers(priv);
	PRIV_UNLOCK(priv);

	mlx5e_rl_reset_rates(rl);

	/* free shared UAR for SQs */
	mlx5_unmap_free_uar(priv->mdev, &rl->sq_uar);

	/* close TIS domain */
	mlx5e_rl_close_tis(priv);

	for (y = 0; y < rl->param.tx_worker_threads_def; y++) {
		struct mlx5e_rl_worker *rlw = rl->workers + y;

		cv_destroy(&rlw->cv);
		mtx_destroy(&rlw->mtx);
		free(rlw->channels, M_MLX5EN);
	}
	free(rl->rate_limit_table, M_MLX5EN);
	free(rl->workers, M_MLX5EN);
	sx_destroy(&rl->rl_sxlock);
}

static void
mlx5e_rlw_queue_channel_locked(struct mlx5e_rl_worker *rlw,
    struct mlx5e_rl_channel *channel)
{
	STAILQ_INSERT_TAIL(&rlw->process_head, channel, entry);
	cv_broadcast(&rlw->cv);
}

static void
mlx5e_rl_free(struct mlx5e_rl_worker *rlw, struct mlx5e_rl_channel *channel)
{
	if (channel == NULL)
		return;

	MLX5E_RL_WORKER_LOCK(rlw);
	switch (channel->state) {
	case MLX5E_RL_ST_MODIFY:
		channel->state = MLX5E_RL_ST_DESTROY;
		break;
	case MLX5E_RL_ST_USED:
		channel->state = MLX5E_RL_ST_DESTROY;
		mlx5e_rlw_queue_channel_locked(rlw, channel);
		break;
	default:
		break;
	}
	MLX5E_RL_WORKER_UNLOCK(rlw);
}

static int
mlx5e_rl_modify(struct mlx5e_rl_worker *rlw, struct mlx5e_rl_channel *channel, uint64_t rate)
{

	MLX5E_RL_WORKER_LOCK(rlw);
	channel->new_rate = rate;
	switch (channel->state) {
	case MLX5E_RL_ST_USED:
		channel->state = MLX5E_RL_ST_MODIFY;
		mlx5e_rlw_queue_channel_locked(rlw, channel);
		break;
	default:
		break;
	}
	MLX5E_RL_WORKER_UNLOCK(rlw);

	return (0);
}

static int
mlx5e_rl_query(struct mlx5e_rl_worker *rlw, struct mlx5e_rl_channel *channel,
    union if_snd_tag_query_params *params)
{
	int retval;

	MLX5E_RL_WORKER_LOCK(rlw);
	switch (channel->state) {
	case MLX5E_RL_ST_USED:
		params->rate_limit.max_rate = channel->last_rate;
		params->rate_limit.queue_level = mlx5e_sq_queue_level(channel->sq);
		retval = 0;
		break;
	case MLX5E_RL_ST_MODIFY:
		params->rate_limit.max_rate = channel->last_rate;
		params->rate_limit.queue_level = mlx5e_sq_queue_level(channel->sq);
		retval = EBUSY;
		break;
	default:
		retval = EINVAL;
		break;
	}
	MLX5E_RL_WORKER_UNLOCK(rlw);

	return (retval);
}

static int
mlx5e_find_available_tx_ring_index(struct mlx5e_rl_worker *rlw,
    struct mlx5e_rl_channel **pchannel)
{
	struct mlx5e_rl_channel *channel;
	int retval = ENOMEM;

	MLX5E_RL_WORKER_LOCK(rlw);
	/* Check for available channel in free list */
	if ((channel = STAILQ_FIRST(&rlw->index_list_head)) != NULL) {
		retval = 0;
		/* Remove head index from available list */
		STAILQ_REMOVE_HEAD(&rlw->index_list_head, entry);
		channel->state = MLX5E_RL_ST_USED;
		atomic_add_64(&rlw->priv->rl.stats.tx_active_connections, 1ULL);
	} else {
		atomic_add_64(&rlw->priv->rl.stats.tx_available_resource_failure, 1ULL);
	}
	MLX5E_RL_WORKER_UNLOCK(rlw);

	*pchannel = channel;
#ifdef RATELIMIT_DEBUG
	if_printf(rlw->priv->ifp, "Channel pointer for rate limit connection is %p\n", channel);
#endif
	return (retval);
}

int
mlx5e_rl_snd_tag_alloc(struct ifnet *ifp,
    union if_snd_tag_alloc_params *params,
    struct m_snd_tag **ppmt)
{
	struct mlx5e_rl_channel *channel;
	struct mlx5e_rl_worker *rlw;
	struct mlx5e_priv *priv;
	int error;

	priv = ifp->if_softc;

	/* check if there is support for packet pacing or if device is going away */
	if (!MLX5_CAP_GEN(priv->mdev, qos) ||
	    !MLX5_CAP_QOS(priv->mdev, packet_pacing) || priv->gone ||
	    params->rate_limit.hdr.type != IF_SND_TAG_TYPE_RATE_LIMIT)
		return (EOPNOTSUPP);

	/* compute worker thread this TCP connection belongs to */
	rlw = priv->rl.workers + ((params->rate_limit.hdr.flowid % 128) %
	    priv->rl.param.tx_worker_threads_def);

	error = mlx5e_find_available_tx_ring_index(rlw, &channel);
	if (error != 0)
		goto done;

	error = mlx5e_rl_modify(rlw, channel, params->rate_limit.max_rate);
	if (error != 0) {
		mlx5e_rl_free(rlw, channel);
		goto done;
	}

	/* store pointer to mbuf tag */
	*ppmt = &channel->tag.m_snd_tag;
done:
	return (error);
}


int
mlx5e_rl_snd_tag_modify(struct m_snd_tag *pmt, union if_snd_tag_modify_params *params)
{
	struct mlx5e_rl_channel *channel =
	    container_of(pmt, struct mlx5e_rl_channel, tag.m_snd_tag);

	return (mlx5e_rl_modify(channel->worker, channel, params->rate_limit.max_rate));
}

int
mlx5e_rl_snd_tag_query(struct m_snd_tag *pmt, union if_snd_tag_query_params *params)
{
	struct mlx5e_rl_channel *channel =
	    container_of(pmt, struct mlx5e_rl_channel, tag.m_snd_tag);

	return (mlx5e_rl_query(channel->worker, channel, params));
}

void
mlx5e_rl_snd_tag_free(struct m_snd_tag *pmt)
{
	struct mlx5e_rl_channel *channel =
	    container_of(pmt, struct mlx5e_rl_channel, tag.m_snd_tag);

	mlx5e_rl_free(channel->worker, channel);
}

static int
mlx5e_rl_sysctl_show_rate_table(SYSCTL_HANDLER_ARGS)
{
	struct mlx5e_rl_priv_data *rl = arg1;
	struct mlx5e_priv *priv = rl->priv;
	struct sbuf sbuf;
	unsigned x;
	int error;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);

	PRIV_LOCK(priv);

	sbuf_new_for_sysctl(&sbuf, NULL, 128 * rl->param.tx_rates_def, req);

	sbuf_printf(&sbuf,
	    "\n\n" "\t" "ENTRY" "\t" "BURST" "\t" "RATE [bit/s]\n"
	    "\t" "--------------------------------------------\n");

	MLX5E_RL_RLOCK(rl);
	for (x = 0; x != rl->param.tx_rates_def; x++) {
		if (rl->rate_limit_table[x] == 0)
			continue;

		sbuf_printf(&sbuf, "\t" "%3u" "\t" "%3u" "\t" "%lld\n",
		    x, (unsigned)rl->param.tx_burst_size,
		    (long long)rl->rate_limit_table[x]);
	}
	MLX5E_RL_RUNLOCK(rl);

	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);

	PRIV_UNLOCK(priv);

	return (error);
}

static int
mlx5e_rl_refresh_channel_params(struct mlx5e_rl_priv_data *rl)
{
	uint64_t x;
	uint64_t y;

	MLX5E_RL_WLOCK(rl);
	/* compute channel parameters once */
	mlx5e_rl_build_channel_param(rl, &rl->chan_param);
	MLX5E_RL_WUNLOCK(rl);

	for (y = 0; y != rl->param.tx_worker_threads_def; y++) {
		struct mlx5e_rl_worker *rlw = rl->workers + y;

		for (x = 0; x != rl->param.tx_channels_per_worker_def; x++) {
			struct mlx5e_rl_channel *channel;
			struct mlx5e_sq *sq;

			channel = rlw->channels + x;
			sq = channel->sq;

			if (sq == NULL)
				continue;

			if (MLX5_CAP_GEN(rl->priv->mdev, cq_period_mode_modify)) {
				mlx5_core_modify_cq_moderation_mode(rl->priv->mdev, &sq->cq.mcq,
				    rl->param.tx_coalesce_usecs,
				    rl->param.tx_coalesce_pkts,
				    rl->param.tx_coalesce_mode);
			} else {
				mlx5_core_modify_cq_moderation(rl->priv->mdev, &sq->cq.mcq,
				    rl->param.tx_coalesce_usecs,
				    rl->param.tx_coalesce_pkts);
			}
		}
	}
	return (0);
}

void
mlx5e_rl_refresh_sq_inline(struct mlx5e_rl_priv_data *rl)
{
	uint64_t x;
	uint64_t y;

	for (y = 0; y != rl->param.tx_worker_threads_def; y++) {
		struct mlx5e_rl_worker *rlw = rl->workers + y;

		for (x = 0; x != rl->param.tx_channels_per_worker_def; x++) {
			struct mlx5e_rl_channel *channel;
			struct mlx5e_sq *sq;

			channel = rlw->channels + x;
			sq = channel->sq;

			if (sq == NULL)
				continue;

			mtx_lock(&sq->lock);
			mlx5e_update_sq_inline(sq);
			mtx_unlock(&sq->lock);
		}
	}
}

static int
mlx5e_rl_tx_limit_add(struct mlx5e_rl_priv_data *rl, uint64_t value)
{
	unsigned x;
	int error;

	if (value < 1000 ||
	    mlx5_rl_is_in_range(rl->priv->mdev, howmany(value, 1000), 0) == 0)
		return (EINVAL);

	MLX5E_RL_WLOCK(rl);
	error = ENOMEM;

	/* check if rate already exists */
	for (x = 0; x != rl->param.tx_rates_def; x++) {
		if (rl->rate_limit_table[x] != value)
			continue;
		error = EEXIST;
		break;
	}

	/* check if there is a free rate entry */
	if (x == rl->param.tx_rates_def) {
		for (x = 0; x != rl->param.tx_rates_def; x++) {
			if (rl->rate_limit_table[x] != 0)
				continue;
			rl->rate_limit_table[x] = value;
			error = 0;
			break;
		}
	}
	MLX5E_RL_WUNLOCK(rl);

	return (error);
}

static int
mlx5e_rl_tx_limit_clr(struct mlx5e_rl_priv_data *rl, uint64_t value)
{
	unsigned x;
	int error;

	if (value == 0)
		return (EINVAL);

	MLX5E_RL_WLOCK(rl);

	/* check if rate already exists */
	for (x = 0; x != rl->param.tx_rates_def; x++) {
		if (rl->rate_limit_table[x] != value)
			continue;
		/* free up rate */
		rl->rate_limit_table[x] = 0;
		break;
	}

	/* check if there is a free rate entry */
	if (x == rl->param.tx_rates_def)
		error = ENOENT;
	else
		error = 0;
	MLX5E_RL_WUNLOCK(rl);

	return (error);
}

static int
mlx5e_rl_sysctl_handler(SYSCTL_HANDLER_ARGS)
{
	struct mlx5e_rl_priv_data *rl = arg1;
	struct mlx5e_priv *priv = rl->priv;
	unsigned mode_modify;
	unsigned was_opened;
	uint64_t value;
	uint64_t old;
	int error;

	PRIV_LOCK(priv);

	MLX5E_RL_RLOCK(rl);
	value = rl->param.arg[arg2];
	MLX5E_RL_RUNLOCK(rl);

	if (req != NULL) {
		old = value;
		error = sysctl_handle_64(oidp, &value, 0, req);
		if (error || req->newptr == NULL ||
		    value == rl->param.arg[arg2])
			goto done;
	} else {
		old = 0;
		error = 0;
	}

	/* check if device is gone */
	if (priv->gone) {
		error = ENXIO;
		goto done;
	}
	was_opened = rl->opened;
	mode_modify = MLX5_CAP_GEN(priv->mdev, cq_period_mode_modify);

	switch (MLX5E_RL_PARAMS_INDEX(arg[arg2])) {
	case MLX5E_RL_PARAMS_INDEX(tx_worker_threads_def):
		if (value > rl->param.tx_worker_threads_max)
			value = rl->param.tx_worker_threads_max;
		else if (value < 1)
			value = 1;

		/* store new value */
		rl->param.arg[arg2] = value;
		break;

	case MLX5E_RL_PARAMS_INDEX(tx_channels_per_worker_def):
		if (value > rl->param.tx_channels_per_worker_max)
			value = rl->param.tx_channels_per_worker_max;
		else if (value < 1)
			value = 1;

		/* store new value */
		rl->param.arg[arg2] = value;
		break;

	case MLX5E_RL_PARAMS_INDEX(tx_rates_def):
		if (value > rl->param.tx_rates_max)
			value = rl->param.tx_rates_max;
		else if (value < 1)
			value = 1;

		/* store new value */
		rl->param.arg[arg2] = value;
		break;

	case MLX5E_RL_PARAMS_INDEX(tx_coalesce_usecs):
		/* range check */
		if (value < 1)
			value = 0;
		else if (value > MLX5E_FLD_MAX(cqc, cq_period))
			value = MLX5E_FLD_MAX(cqc, cq_period);

		/* store new value */
		rl->param.arg[arg2] = value;

		/* check to avoid down and up the network interface */
		if (was_opened)
			error = mlx5e_rl_refresh_channel_params(rl);
		break;

	case MLX5E_RL_PARAMS_INDEX(tx_coalesce_pkts):
		/* import TX coal pkts */
		if (value < 1)
			value = 0;
		else if (value > MLX5E_FLD_MAX(cqc, cq_max_count))
			value = MLX5E_FLD_MAX(cqc, cq_max_count);

		/* store new value */
		rl->param.arg[arg2] = value;

		/* check to avoid down and up the network interface */
		if (was_opened)
			error = mlx5e_rl_refresh_channel_params(rl);
		break;

	case MLX5E_RL_PARAMS_INDEX(tx_coalesce_mode):
		/* network interface must be down */
		if (was_opened != 0 && mode_modify == 0)
			mlx5e_rl_close_workers(priv);

		/* import TX coalesce mode */
		if (value != 0)
			value = 1;

		/* store new value */
		rl->param.arg[arg2] = value;

		/* restart network interface, if any */
		if (was_opened != 0) {
			if (mode_modify == 0)
				mlx5e_rl_open_workers(priv);
			else
				error = mlx5e_rl_refresh_channel_params(rl);
		}
		break;

	case MLX5E_RL_PARAMS_INDEX(tx_queue_size):
		/* network interface must be down */
		if (was_opened)
			mlx5e_rl_close_workers(priv);

		/* import TX queue size */
		if (value < (1 << MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE))
			value = (1 << MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE);
		else if (value > priv->params_ethtool.tx_queue_size_max)
			value = priv->params_ethtool.tx_queue_size_max;

		/* store actual TX queue size */
		value = 1ULL << order_base_2(value);

		/* store new value */
		rl->param.arg[arg2] = value;

		/* verify TX completion factor */
		mlx5e_rl_sync_tx_completion_fact(rl);

		/* restart network interface, if any */
		if (was_opened)
			mlx5e_rl_open_workers(priv);
		break;

	case MLX5E_RL_PARAMS_INDEX(tx_completion_fact):
		/* network interface must be down */
		if (was_opened)
			mlx5e_rl_close_workers(priv);

		/* store new value */
		rl->param.arg[arg2] = value;

		/* verify parameter */
		mlx5e_rl_sync_tx_completion_fact(rl);

		/* restart network interface, if any */
		if (was_opened)
			mlx5e_rl_open_workers(priv);
		break;

	case MLX5E_RL_PARAMS_INDEX(tx_limit_add):
		error = mlx5e_rl_tx_limit_add(rl, value);
		break;

	case MLX5E_RL_PARAMS_INDEX(tx_limit_clr):
		error = mlx5e_rl_tx_limit_clr(rl, value);
		break;

	case MLX5E_RL_PARAMS_INDEX(tx_allowed_deviation):
		/* range check */
		if (value > rl->param.tx_allowed_deviation_max)
			value = rl->param.tx_allowed_deviation_max;
		else if (value < rl->param.tx_allowed_deviation_min)
			value = rl->param.tx_allowed_deviation_min;

		MLX5E_RL_WLOCK(rl);
		rl->param.arg[arg2] = value;
		MLX5E_RL_WUNLOCK(rl);
		break;

	case MLX5E_RL_PARAMS_INDEX(tx_burst_size):
		/* range check */
		if (value > rl->param.tx_burst_size_max)
			value = rl->param.tx_burst_size_max;
		else if (value < rl->param.tx_burst_size_min)
			value = rl->param.tx_burst_size_min;

		MLX5E_RL_WLOCK(rl);
		rl->param.arg[arg2] = value;
		MLX5E_RL_WUNLOCK(rl);
		break;

	default:
		break;
	}
done:
	PRIV_UNLOCK(priv);
	return (error);
}

static void
mlx5e_rl_sysctl_add_u64_oid(struct mlx5e_rl_priv_data *rl, unsigned x,
    struct sysctl_oid *node, const char *name, const char *desc)
{
	/*
	 * NOTE: In FreeBSD-11 and newer the CTLFLAG_RWTUN flag will
	 * take care of loading default sysctl value from the kernel
	 * environment, if any:
	 */
	if (strstr(name, "_max") != 0 || strstr(name, "_min") != 0) {
		/* read-only SYSCTLs */
		SYSCTL_ADD_PROC(&rl->ctx, SYSCTL_CHILDREN(node), OID_AUTO,
		    name, CTLTYPE_U64 | CTLFLAG_RD |
		    CTLFLAG_MPSAFE, rl, x, &mlx5e_rl_sysctl_handler, "QU", desc);
	} else {
		if (strstr(name, "_def") != 0) {
#ifdef RATELIMIT_DEBUG
			/* tunable read-only advanced SYSCTLs */
			SYSCTL_ADD_PROC(&rl->ctx, SYSCTL_CHILDREN(node), OID_AUTO,
			    name, CTLTYPE_U64 | CTLFLAG_RDTUN |
			    CTLFLAG_MPSAFE, rl, x, &mlx5e_rl_sysctl_handler, "QU", desc);
#endif
		} else {
			/* read-write SYSCTLs */
			SYSCTL_ADD_PROC(&rl->ctx, SYSCTL_CHILDREN(node), OID_AUTO,
			    name, CTLTYPE_U64 | CTLFLAG_RWTUN |
			    CTLFLAG_MPSAFE, rl, x, &mlx5e_rl_sysctl_handler, "QU", desc);
		}
	}
}

static void
mlx5e_rl_sysctl_add_stats_u64_oid(struct mlx5e_rl_priv_data *rl, unsigned x,
    struct sysctl_oid *node, const char *name, const char *desc)
{
	/* read-only SYSCTLs */
	SYSCTL_ADD_U64(&rl->ctx, SYSCTL_CHILDREN(node), OID_AUTO, name,
	    CTLFLAG_RD, &rl->stats.arg[x], 0, desc);
}

#endif
