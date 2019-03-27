/*-
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
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
#include <net/sff8472.h>

void
mlx5e_create_stats(struct sysctl_ctx_list *ctx,
    struct sysctl_oid_list *parent, const char *buffer,
    const char **desc, unsigned num, u64 * arg)
{
	struct sysctl_oid *node;
	unsigned x;

	sysctl_ctx_init(ctx);

	node = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO,
	    buffer, CTLFLAG_RD, NULL, "Statistics");
	if (node == NULL)
		return;
	for (x = 0; x != num; x++) {
		SYSCTL_ADD_UQUAD(ctx, SYSCTL_CHILDREN(node), OID_AUTO,
		    desc[2 * x], CTLFLAG_RD, arg + x, desc[2 * x + 1]);
	}
}

static void
mlx5e_ethtool_sync_tx_completion_fact(struct mlx5e_priv *priv)
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
	uint64_t max = priv->params_ethtool.tx_queue_size /
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
	priv->params_ethtool.tx_completion_fact_max = max;

	/*
	 * Verify that the current TX completion factor is within the
	 * given limits:
	 */
	if (priv->params_ethtool.tx_completion_fact < 1)
		priv->params_ethtool.tx_completion_fact = 1;
	else if (priv->params_ethtool.tx_completion_fact > max)
		priv->params_ethtool.tx_completion_fact = max;
}

static int
mlx5e_getmaxrate(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 max_bw_unit[IEEE_8021QAZ_MAX_TCS];
	u8 max_bw_value[IEEE_8021QAZ_MAX_TCS];
	int err;
	int i;

	PRIV_LOCK(priv);
	err = -mlx5_query_port_tc_rate_limit(mdev, max_bw_value, max_bw_unit);
	if (err)
		goto done;

	for (i = 0; i <= mlx5_max_tc(mdev); i++) {
		switch (max_bw_unit[i]) {
		case MLX5_100_MBPS_UNIT:
			priv->params_ethtool.max_bw_value[i] = max_bw_value[i] * MLX5E_100MB;
			break;
		case MLX5_GBPS_UNIT:
			priv->params_ethtool.max_bw_value[i] = max_bw_value[i] * MLX5E_1GB;
			break;
		case MLX5_BW_NO_LIMIT:
			priv->params_ethtool.max_bw_value[i] = 0;
			break;
		default:
			priv->params_ethtool.max_bw_value[i] = -1;
			WARN_ONCE(true, "non-supported BW unit");
			break;
		}
	}
done:
	PRIV_UNLOCK(priv);
	return (err);
}

static int
mlx5e_get_max_alloc(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;
	int x;

	PRIV_LOCK(priv);
	err = -mlx5_query_port_tc_bw_alloc(mdev, priv->params_ethtool.max_bw_share);
	if (err == 0) {
		/* set default value */
		for (x = 0; x != IEEE_8021QAZ_MAX_TCS; x++) {
			priv->params_ethtool.max_bw_share[x] =
			    100 / IEEE_8021QAZ_MAX_TCS;
		}
		err = -mlx5_set_port_tc_bw_alloc(mdev,
		    priv->params_ethtool.max_bw_share);
	}
	PRIV_UNLOCK(priv);

	return (err);
}

static int
mlx5e_get_dscp(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	if (MLX5_CAP_GEN(mdev, qcam_reg) == 0 ||
	    MLX5_CAP_QCAM_REG(mdev, qpts) == 0 ||
	    MLX5_CAP_QCAM_REG(mdev, qpdpm) == 0)
		return (EOPNOTSUPP);

	PRIV_LOCK(priv);
	err = -mlx5_query_dscp2prio(mdev, priv->params_ethtool.dscp2prio);
	if (err)
		goto done;

	err = -mlx5_query_trust_state(mdev, &priv->params_ethtool.trust_state);
	if (err)
		goto done;
done:
	PRIV_UNLOCK(priv);
	return (err);
}

static void
mlx5e_tc_get_parameters(struct mlx5e_priv *priv,
    u64 *new_bw_value, u8 *max_bw_value, u8 *max_bw_unit)
{
	const u64 upper_limit_mbps = 255 * MLX5E_100MB;
	const u64 upper_limit_gbps = 255 * MLX5E_1GB;
	u64 temp;
	int i;

	memset(max_bw_value, 0, IEEE_8021QAZ_MAX_TCS);
	memset(max_bw_unit, 0, IEEE_8021QAZ_MAX_TCS);

	for (i = 0; i <= mlx5_max_tc(priv->mdev); i++) {
		temp = (new_bw_value != NULL) ?
		    new_bw_value[i] : priv->params_ethtool.max_bw_value[i];

		if (!temp) {
			max_bw_unit[i] = MLX5_BW_NO_LIMIT;
		} else if (temp > upper_limit_gbps) {
			max_bw_unit[i] = MLX5_BW_NO_LIMIT;
		} else if (temp <= upper_limit_mbps) {
			max_bw_value[i] = howmany(temp, MLX5E_100MB);
			max_bw_unit[i]  = MLX5_100_MBPS_UNIT;
		} else {
			max_bw_value[i] = howmany(temp, MLX5E_1GB);
			max_bw_unit[i]  = MLX5_GBPS_UNIT;
		}
	}
}

static int
mlx5e_tc_maxrate_handler(SYSCTL_HANDLER_ARGS)
{
	struct mlx5e_priv *priv = arg1;
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 max_bw_unit[IEEE_8021QAZ_MAX_TCS];
	u8 max_bw_value[IEEE_8021QAZ_MAX_TCS];
	u64 new_bw_value[IEEE_8021QAZ_MAX_TCS];
	u8 max_rates = mlx5_max_tc(mdev) + 1;
	u8 x;
	int err;

	PRIV_LOCK(priv);
	err = SYSCTL_OUT(req, priv->params_ethtool.max_bw_value,
	    sizeof(priv->params_ethtool.max_bw_value[0]) * max_rates);
	if (err || !req->newptr)
		goto done;
	err = SYSCTL_IN(req, new_bw_value,
	    sizeof(new_bw_value[0]) * max_rates);
	if (err)
		goto done;

	/* range check input value */
	for (x = 0; x != max_rates; x++) {
		if (new_bw_value[x] % MLX5E_100MB) {
			err = ERANGE;
			goto done;
		}
	}

	mlx5e_tc_get_parameters(priv, new_bw_value, max_bw_value, max_bw_unit);

	err = -mlx5_modify_port_tc_rate_limit(mdev, max_bw_value, max_bw_unit);
	if (err)
		goto done;

	memcpy(priv->params_ethtool.max_bw_value, new_bw_value,
	    sizeof(priv->params_ethtool.max_bw_value));
done:
	PRIV_UNLOCK(priv);
	return (err);
}

static int
mlx5e_tc_rate_share_handler(SYSCTL_HANDLER_ARGS)
{
	struct mlx5e_priv *priv = arg1;
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 max_bw_share[IEEE_8021QAZ_MAX_TCS];
	u8 max_rates = mlx5_max_tc(mdev) + 1;
	int i;
	int err;
	int sum;

	PRIV_LOCK(priv);
	err = SYSCTL_OUT(req, priv->params_ethtool.max_bw_share, max_rates);
	if (err || !req->newptr)
		goto done;
	err = SYSCTL_IN(req, max_bw_share, max_rates);
	if (err)
		goto done;

	/* range check input value */
	for (sum = i = 0; i != max_rates; i++) {
		if (max_bw_share[i] < 1 || max_bw_share[i] > 100) {
			err = ERANGE;
			goto done;
		}
		sum += max_bw_share[i];
	}

	/* sum of values should be as close to 100 as possible */
	if (sum < (100 - max_rates + 1) || sum > 100) {
		err = ERANGE;
		goto done;
	}

	err = -mlx5_set_port_tc_bw_alloc(mdev, max_bw_share);
	if (err)
		goto done;

	memcpy(priv->params_ethtool.max_bw_share, max_bw_share,
	    sizeof(priv->params_ethtool.max_bw_share));
done:
	PRIV_UNLOCK(priv);
	return (err);
}

static int
mlx5e_get_prio_tc(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err = 0;
	int i;

	PRIV_LOCK(priv);
	if (!MLX5_CAP_GEN(priv->mdev, ets)) {
		PRIV_UNLOCK(priv);
		return (EOPNOTSUPP);
	}

	for (i = 0; i <= mlx5_max_tc(priv->mdev); i++) {
		err = -mlx5_query_port_prio_tc(mdev, i, &(priv->params_ethtool.prio_tc[i]));
		if (err)
			break;
	}
	PRIV_UNLOCK(priv);
	return (err);
}

static int
mlx5e_prio_to_tc_handler(SYSCTL_HANDLER_ARGS)
{
	struct mlx5e_priv *priv = arg1;
	int prio_index = arg2;
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;
	uint8_t result;

	PRIV_LOCK(priv);
	result = priv->params_ethtool.prio_tc[prio_index];
	err = sysctl_handle_8(oidp, &result, 0, req);
	if (err || !req->newptr ||
	    result == priv->params_ethtool.prio_tc[prio_index])
		goto done;

	if (result > mlx5_max_tc(mdev)) {
		err = ERANGE;
		goto done;
	}

	err = -mlx5_set_port_prio_tc(mdev, prio_index, result);
	if (err)
		goto done;

	priv->params_ethtool.prio_tc[prio_index] = result;

done:
	PRIV_UNLOCK(priv);
	return (err);
}

static int
mlx5e_trust_state_handler(SYSCTL_HANDLER_ARGS)
{
	struct mlx5e_priv *priv = arg1;
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;
	u8 result;

	PRIV_LOCK(priv);
	result = priv->params_ethtool.trust_state;
	err = sysctl_handle_8(oidp, &result, 0, req);
	if (err || !req->newptr ||
	    result == priv->params_ethtool.trust_state)
		goto done;

	switch (result) {
	case MLX5_QPTS_TRUST_PCP:
	case MLX5_QPTS_TRUST_DSCP:
		break;
	case MLX5_QPTS_TRUST_BOTH:
		if (!MLX5_CAP_QCAM_FEATURE(mdev, qpts_trust_both)) {
			err = EOPNOTSUPP;
			goto done;
		}
		break;
	default:
		err = ERANGE;
		goto done;
	}

	err = -mlx5_set_trust_state(mdev, result);
	if (err)
		goto done;

	priv->params_ethtool.trust_state = result;

	/* update inline mode */
	mlx5e_refresh_sq_inline(priv);
#ifdef RATELIMIT
	mlx5e_rl_refresh_sq_inline(&priv->rl);
#endif
done:
	PRIV_UNLOCK(priv);
	return (err);
}

static int
mlx5e_dscp_prio_handler(SYSCTL_HANDLER_ARGS)
{
	struct mlx5e_priv *priv = arg1;
	int prio_index = arg2;
	struct mlx5_core_dev *mdev = priv->mdev;
	uint8_t dscp2prio[MLX5_MAX_SUPPORTED_DSCP];
	uint8_t x;
	int err;

	PRIV_LOCK(priv);
	err = SYSCTL_OUT(req, priv->params_ethtool.dscp2prio + prio_index,
	    sizeof(priv->params_ethtool.dscp2prio) / 8);
	if (err || !req->newptr)
		goto done;

	memcpy(dscp2prio, priv->params_ethtool.dscp2prio, sizeof(dscp2prio));
	err = SYSCTL_IN(req, dscp2prio + prio_index, sizeof(dscp2prio) / 8);
	if (err)
		goto done;
	for (x = 0; x != MLX5_MAX_SUPPORTED_DSCP; x++) {
		if (dscp2prio[x] > 7) {
			err = ERANGE;
			goto done;
		}
	}
	err = -mlx5_set_dscp2prio(mdev, dscp2prio);
	if (err)
		goto done;

	/* update local array */
	memcpy(priv->params_ethtool.dscp2prio, dscp2prio,
	    sizeof(priv->params_ethtool.dscp2prio));
done:
	PRIV_UNLOCK(priv);
	return (err);
}

#define	MLX5_PARAM_OFFSET(n)				\
    __offsetof(struct mlx5e_priv, params_ethtool.n)

static int
mlx5e_ethtool_handler(SYSCTL_HANDLER_ARGS)
{
	struct mlx5e_priv *priv = arg1;
	uint64_t value;
	int mode_modify;
	int was_opened;
	int error;

	PRIV_LOCK(priv);
	value = priv->params_ethtool.arg[arg2];
	if (req != NULL) {
		error = sysctl_handle_64(oidp, &value, 0, req);
		if (error || req->newptr == NULL ||
		    value == priv->params_ethtool.arg[arg2])
			goto done;

		/* assign new value */
		priv->params_ethtool.arg[arg2] = value;
	} else {
		error = 0;
	}
	/* check if device is gone */
	if (priv->gone) {
		error = ENXIO;
		goto done;
	}
	was_opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	mode_modify = MLX5_CAP_GEN(priv->mdev, cq_period_mode_modify);

	switch (MLX5_PARAM_OFFSET(arg[arg2])) {
	case MLX5_PARAM_OFFSET(rx_coalesce_usecs):
		/* import RX coal time */
		if (priv->params_ethtool.rx_coalesce_usecs < 1)
			priv->params_ethtool.rx_coalesce_usecs = 0;
		else if (priv->params_ethtool.rx_coalesce_usecs >
		    MLX5E_FLD_MAX(cqc, cq_period)) {
			priv->params_ethtool.rx_coalesce_usecs =
			    MLX5E_FLD_MAX(cqc, cq_period);
		}
		priv->params.rx_cq_moderation_usec =
		    priv->params_ethtool.rx_coalesce_usecs;

		/* check to avoid down and up the network interface */
		if (was_opened)
			error = mlx5e_refresh_channel_params(priv);
		break;

	case MLX5_PARAM_OFFSET(rx_coalesce_pkts):
		/* import RX coal pkts */
		if (priv->params_ethtool.rx_coalesce_pkts < 1)
			priv->params_ethtool.rx_coalesce_pkts = 0;
		else if (priv->params_ethtool.rx_coalesce_pkts >
		    MLX5E_FLD_MAX(cqc, cq_max_count)) {
			priv->params_ethtool.rx_coalesce_pkts =
			    MLX5E_FLD_MAX(cqc, cq_max_count);
		}
		priv->params.rx_cq_moderation_pkts =
		    priv->params_ethtool.rx_coalesce_pkts;

		/* check to avoid down and up the network interface */
		if (was_opened)
			error = mlx5e_refresh_channel_params(priv);
		break;

	case MLX5_PARAM_OFFSET(tx_coalesce_usecs):
		/* import TX coal time */
		if (priv->params_ethtool.tx_coalesce_usecs < 1)
			priv->params_ethtool.tx_coalesce_usecs = 0;
		else if (priv->params_ethtool.tx_coalesce_usecs >
		    MLX5E_FLD_MAX(cqc, cq_period)) {
			priv->params_ethtool.tx_coalesce_usecs =
			    MLX5E_FLD_MAX(cqc, cq_period);
		}
		priv->params.tx_cq_moderation_usec =
		    priv->params_ethtool.tx_coalesce_usecs;

		/* check to avoid down and up the network interface */
		if (was_opened)
			error = mlx5e_refresh_channel_params(priv);
		break;

	case MLX5_PARAM_OFFSET(tx_coalesce_pkts):
		/* import TX coal pkts */
		if (priv->params_ethtool.tx_coalesce_pkts < 1)
			priv->params_ethtool.tx_coalesce_pkts = 0;
		else if (priv->params_ethtool.tx_coalesce_pkts >
		    MLX5E_FLD_MAX(cqc, cq_max_count)) {
			priv->params_ethtool.tx_coalesce_pkts =
			    MLX5E_FLD_MAX(cqc, cq_max_count);
		}
		priv->params.tx_cq_moderation_pkts =
		    priv->params_ethtool.tx_coalesce_pkts;

		/* check to avoid down and up the network interface */
		if (was_opened)
			error = mlx5e_refresh_channel_params(priv);
		break;

	case MLX5_PARAM_OFFSET(tx_queue_size):
		/* network interface must be down */
		if (was_opened)
			mlx5e_close_locked(priv->ifp);

		/* import TX queue size */
		if (priv->params_ethtool.tx_queue_size <
		    (1 << MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE)) {
			priv->params_ethtool.tx_queue_size =
			    (1 << MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE);
		} else if (priv->params_ethtool.tx_queue_size >
		    priv->params_ethtool.tx_queue_size_max) {
			priv->params_ethtool.tx_queue_size =
			    priv->params_ethtool.tx_queue_size_max;
		}
		/* store actual TX queue size */
		priv->params.log_sq_size =
		    order_base_2(priv->params_ethtool.tx_queue_size);
		priv->params_ethtool.tx_queue_size =
		    1 << priv->params.log_sq_size;

		/* verify TX completion factor */
		mlx5e_ethtool_sync_tx_completion_fact(priv);

		/* restart network interface, if any */
		if (was_opened)
			mlx5e_open_locked(priv->ifp);
		break;

	case MLX5_PARAM_OFFSET(rx_queue_size):
		/* network interface must be down */
		if (was_opened)
			mlx5e_close_locked(priv->ifp);

		/* import RX queue size */
		if (priv->params_ethtool.rx_queue_size <
		    (1 << MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE)) {
			priv->params_ethtool.rx_queue_size =
			    (1 << MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE);
		} else if (priv->params_ethtool.rx_queue_size >
		    priv->params_ethtool.rx_queue_size_max) {
			priv->params_ethtool.rx_queue_size =
			    priv->params_ethtool.rx_queue_size_max;
		}
		/* store actual RX queue size */
		priv->params.log_rq_size =
		    order_base_2(priv->params_ethtool.rx_queue_size);
		priv->params_ethtool.rx_queue_size =
		    1 << priv->params.log_rq_size;

		/* update least number of RX WQEs */
		priv->params.min_rx_wqes = min(
		    priv->params_ethtool.rx_queue_size - 1,
		    MLX5E_PARAMS_DEFAULT_MIN_RX_WQES);

		/* restart network interface, if any */
		if (was_opened)
			mlx5e_open_locked(priv->ifp);
		break;

	case MLX5_PARAM_OFFSET(channels_rsss):
		/* network interface must be down */
		if (was_opened)
			mlx5e_close_locked(priv->ifp);

		/* import number of channels */
		if (priv->params_ethtool.channels_rsss < 1)
			priv->params_ethtool.channels_rsss = 1;
		else if (priv->params_ethtool.channels_rsss > 128)
			priv->params_ethtool.channels_rsss = 128;

		priv->params.channels_rsss = priv->params_ethtool.channels_rsss;

		/* restart network interface, if any */
		if (was_opened)
			mlx5e_open_locked(priv->ifp);
		break;

	case MLX5_PARAM_OFFSET(channels):
		/* network interface must be down */
		if (was_opened)
			mlx5e_close_locked(priv->ifp);

		/* import number of channels */
		if (priv->params_ethtool.channels < 1)
			priv->params_ethtool.channels = 1;
		else if (priv->params_ethtool.channels >
		    (u64) priv->mdev->priv.eq_table.num_comp_vectors) {
			priv->params_ethtool.channels =
			    (u64) priv->mdev->priv.eq_table.num_comp_vectors;
		}
		priv->params.num_channels = priv->params_ethtool.channels;

		/* restart network interface, if any */
		if (was_opened)
			mlx5e_open_locked(priv->ifp);
		break;

	case MLX5_PARAM_OFFSET(rx_coalesce_mode):
		/* network interface must be down */
		if (was_opened != 0 && mode_modify == 0)
			mlx5e_close_locked(priv->ifp);

		/* import RX coalesce mode */
		if (priv->params_ethtool.rx_coalesce_mode != 0)
			priv->params_ethtool.rx_coalesce_mode = 1;
		priv->params.rx_cq_moderation_mode =
		    priv->params_ethtool.rx_coalesce_mode;

		/* restart network interface, if any */
		if (was_opened != 0) {
			if (mode_modify == 0)
				mlx5e_open_locked(priv->ifp);
			else
				error = mlx5e_refresh_channel_params(priv);
		}
		break;

	case MLX5_PARAM_OFFSET(tx_coalesce_mode):
		/* network interface must be down */
		if (was_opened != 0 && mode_modify == 0)
			mlx5e_close_locked(priv->ifp);

		/* import TX coalesce mode */
		if (priv->params_ethtool.tx_coalesce_mode != 0)
			priv->params_ethtool.tx_coalesce_mode = 1;
		priv->params.tx_cq_moderation_mode =
		    priv->params_ethtool.tx_coalesce_mode;

		/* restart network interface, if any */
		if (was_opened != 0) {
			if (mode_modify == 0)
				mlx5e_open_locked(priv->ifp);
			else
				error = mlx5e_refresh_channel_params(priv);
		}
		break;

	case MLX5_PARAM_OFFSET(hw_lro):
		/* network interface must be down */
		if (was_opened)
			mlx5e_close_locked(priv->ifp);

		/* import HW LRO mode */
		if (priv->params_ethtool.hw_lro != 0 &&
		    MLX5_CAP_ETH(priv->mdev, lro_cap)) {
			priv->params_ethtool.hw_lro = 1;
			/* check if feature should actually be enabled */
			if (priv->ifp->if_capenable & IFCAP_LRO) {
				priv->params.hw_lro_en = true;
			} else {
				priv->params.hw_lro_en = false;

				if_printf(priv->ifp, "To enable HW LRO "
				    "please also enable LRO via ifconfig(8).\n");
			}
		} else {
			/* return an error if HW does not support this feature */
			if (priv->params_ethtool.hw_lro != 0)
				error = EINVAL;
			priv->params.hw_lro_en = false;
			priv->params_ethtool.hw_lro = 0;
		}
		/* restart network interface, if any */
		if (was_opened)
			mlx5e_open_locked(priv->ifp);
		break;

	case MLX5_PARAM_OFFSET(cqe_zipping):
		/* network interface must be down */
		if (was_opened)
			mlx5e_close_locked(priv->ifp);

		/* import CQE zipping mode */
		if (priv->params_ethtool.cqe_zipping &&
		    MLX5_CAP_GEN(priv->mdev, cqe_compression)) {
			priv->params.cqe_zipping_en = true;
			priv->params_ethtool.cqe_zipping = 1;
		} else {
			priv->params.cqe_zipping_en = false;
			priv->params_ethtool.cqe_zipping = 0;
		}
		/* restart network interface, if any */
		if (was_opened)
			mlx5e_open_locked(priv->ifp);
		break;

	case MLX5_PARAM_OFFSET(tx_completion_fact):
		/* network interface must be down */
		if (was_opened)
			mlx5e_close_locked(priv->ifp);

		/* verify parameter */
		mlx5e_ethtool_sync_tx_completion_fact(priv);

		/* restart network interface, if any */
		if (was_opened)
			mlx5e_open_locked(priv->ifp);
		break;

	case MLX5_PARAM_OFFSET(modify_tx_dma):
		/* check if network interface is opened */
		if (was_opened) {
			priv->params_ethtool.modify_tx_dma =
			    priv->params_ethtool.modify_tx_dma ? 1 : 0;
			/* modify tx according to value */
			mlx5e_modify_tx_dma(priv, value != 0);
		} else {
			/* if closed force enable tx */
			priv->params_ethtool.modify_tx_dma = 0;
		}
		break;

	case MLX5_PARAM_OFFSET(modify_rx_dma):
		/* check if network interface is opened */
		if (was_opened) {
			priv->params_ethtool.modify_rx_dma =
			    priv->params_ethtool.modify_rx_dma ? 1 : 0;
			/* modify rx according to value */
			mlx5e_modify_rx_dma(priv, value != 0);
		} else {
			/* if closed force enable rx */
			priv->params_ethtool.modify_rx_dma = 0;
		}
		break;

	case MLX5_PARAM_OFFSET(diag_pci_enable):
		priv->params_ethtool.diag_pci_enable =
		    priv->params_ethtool.diag_pci_enable ? 1 : 0;

		error = -mlx5_core_set_diagnostics_full(priv->mdev,
		    priv->params_ethtool.diag_pci_enable,
		    priv->params_ethtool.diag_general_enable);
		break;

	case MLX5_PARAM_OFFSET(diag_general_enable):
		priv->params_ethtool.diag_general_enable =
		    priv->params_ethtool.diag_general_enable ? 1 : 0;

		error = -mlx5_core_set_diagnostics_full(priv->mdev,
		    priv->params_ethtool.diag_pci_enable,
		    priv->params_ethtool.diag_general_enable);
		break;

	case MLX5_PARAM_OFFSET(mc_local_lb):
		priv->params_ethtool.mc_local_lb =
		    priv->params_ethtool.mc_local_lb ? 1 : 0;

		if (MLX5_CAP_GEN(priv->mdev, disable_local_lb)) {
			error = mlx5_nic_vport_modify_local_lb(priv->mdev,
			    MLX5_LOCAL_MC_LB, priv->params_ethtool.mc_local_lb);
		} else {
			error = EOPNOTSUPP;
		}
		break;

	case MLX5_PARAM_OFFSET(uc_local_lb):
		priv->params_ethtool.uc_local_lb =
		    priv->params_ethtool.uc_local_lb ? 1 : 0;

		if (MLX5_CAP_GEN(priv->mdev, disable_local_lb)) {
			error = mlx5_nic_vport_modify_local_lb(priv->mdev,
			    MLX5_LOCAL_UC_LB, priv->params_ethtool.uc_local_lb);
		} else {
			error = EOPNOTSUPP;
		}
		break;

	default:
		break;
	}
done:
	PRIV_UNLOCK(priv);
	return (error);
}

/*
 * Read the first three bytes of the eeprom in order to get the needed info
 * for the whole reading.
 * Byte 0 - Identifier byte
 * Byte 1 - Revision byte
 * Byte 2 - Status byte
 */
static int
mlx5e_get_eeprom_info(struct mlx5e_priv *priv, struct mlx5e_eeprom *eeprom)
{
	struct mlx5_core_dev *dev = priv->mdev;
	u32 data = 0;
	int size_read = 0;
	int ret;

	ret = mlx5_query_module_num(dev, &eeprom->module_num);
	if (ret) {
		if_printf(priv->ifp, "%s:%d: Failed query module error=%d\n",
		    __func__, __LINE__, ret);
		return (ret);
	}

	/* Read the first three bytes to get Identifier, Revision and Status */
	ret = mlx5_query_eeprom(dev, eeprom->i2c_addr, eeprom->page_num,
	    eeprom->device_addr, MLX5E_EEPROM_INFO_BYTES, eeprom->module_num, &data,
	    &size_read);
	if (ret) {
		if_printf(priv->ifp, "%s:%d: Failed query eeprom module error=0x%x\n",
		    __func__, __LINE__, ret);
		return (ret);
	}

	switch (data & MLX5_EEPROM_IDENTIFIER_BYTE_MASK) {
	case SFF_8024_ID_QSFP:
		eeprom->type = MLX5E_ETH_MODULE_SFF_8436;
		eeprom->len = MLX5E_ETH_MODULE_SFF_8436_LEN;
		break;
	case SFF_8024_ID_QSFPPLUS:
	case SFF_8024_ID_QSFP28:
		if ((data & MLX5_EEPROM_IDENTIFIER_BYTE_MASK) == SFF_8024_ID_QSFP28 ||
		    ((data & MLX5_EEPROM_REVISION_ID_BYTE_MASK) >> 8) >= 0x3) {
			eeprom->type = MLX5E_ETH_MODULE_SFF_8636;
			eeprom->len = MLX5E_ETH_MODULE_SFF_8636_LEN;
		} else {
			eeprom->type = MLX5E_ETH_MODULE_SFF_8436;
			eeprom->len = MLX5E_ETH_MODULE_SFF_8436_LEN;
		}
		if ((data & MLX5_EEPROM_PAGE_3_VALID_BIT_MASK) == 0)
			eeprom->page_valid = 1;
		break;
	case SFF_8024_ID_SFP:
		eeprom->type = MLX5E_ETH_MODULE_SFF_8472;
		eeprom->len = MLX5E_ETH_MODULE_SFF_8472_LEN;
		break;
	default:
		if_printf(priv->ifp, "%s:%d: Not recognized cable type = 0x%x(%s)\n",
		    __func__, __LINE__, data & MLX5_EEPROM_IDENTIFIER_BYTE_MASK,
		    sff_8024_id[data & MLX5_EEPROM_IDENTIFIER_BYTE_MASK]);
		return (EINVAL);
	}
	return (0);
}

/* Read both low and high pages of the eeprom */
static int
mlx5e_get_eeprom(struct mlx5e_priv *priv, struct mlx5e_eeprom *ee)
{
	struct mlx5_core_dev *dev = priv->mdev;
	int size_read = 0;
	int ret;

	if (ee->len == 0)
		return (EINVAL);

	/* Read low page of the eeprom */
	while (ee->device_addr < ee->len) {
		ret = mlx5_query_eeprom(dev, ee->i2c_addr, ee->page_num, ee->device_addr,
		    ee->len - ee->device_addr, ee->module_num,
		    ee->data + (ee->device_addr / 4), &size_read);
		if (ret) {
			if_printf(priv->ifp, "%s:%d: Failed reading eeprom, "
			    "error = 0x%02x\n", __func__, __LINE__, ret);
			return (ret);
		}
		ee->device_addr += size_read;
	}

	/* Read high page of the eeprom */
	if (ee->page_valid) {
		ee->device_addr = MLX5E_EEPROM_HIGH_PAGE_OFFSET;
		ee->page_num = MLX5E_EEPROM_HIGH_PAGE;
		size_read = 0;
		while (ee->device_addr < MLX5E_EEPROM_PAGE_LENGTH) {
			ret = mlx5_query_eeprom(dev, ee->i2c_addr, ee->page_num,
			    ee->device_addr, MLX5E_EEPROM_PAGE_LENGTH - ee->device_addr,
			    ee->module_num, ee->data + (ee->len / 4) +
			    ((ee->device_addr - MLX5E_EEPROM_HIGH_PAGE_OFFSET) / 4),
			    &size_read);
			if (ret) {
				if_printf(priv->ifp, "%s:%d: Failed reading eeprom, "
				    "error = 0x%02x\n", __func__, __LINE__, ret);
				return (ret);
			}
			ee->device_addr += size_read;
		}
	}
	return (0);
}

static void
mlx5e_print_eeprom(struct mlx5e_eeprom *eeprom)
{
	int row;
	int index_in_row;
	int byte_to_write = 0;
	int line_length = 16;

	printf("\nOffset\t\tValues\n");
	printf("------\t\t------");
	while (byte_to_write < eeprom->len) {
		printf("\n0x%04X\t\t", byte_to_write);
		for (index_in_row = 0; index_in_row < line_length; index_in_row++) {
			printf("%02X ", ((u8 *)eeprom->data)[byte_to_write]);
			byte_to_write++;
		}
	}

	if (eeprom->page_valid) {
		row = MLX5E_EEPROM_HIGH_PAGE_OFFSET;
		printf("\n\nUpper Page 0x03\n");
		printf("\nOffset\t\tValues\n");
		printf("------\t\t------");
		while (row < MLX5E_EEPROM_PAGE_LENGTH) {
			printf("\n0x%04X\t\t", row);
			for (index_in_row = 0; index_in_row < line_length; index_in_row++) {
				printf("%02X ", ((u8 *)eeprom->data)[byte_to_write]);
				byte_to_write++;
				row++;
			}
		}
	}
}

/*
 * Read cable EEPROM module information by first inspecting the first
 * three bytes to get the initial information for a whole reading.
 * Information will be printed to dmesg.
 */
static int
mlx5e_read_eeprom(SYSCTL_HANDLER_ARGS)
{
	struct mlx5e_priv *priv = arg1;
	struct mlx5e_eeprom eeprom;
	int error;
	int result = 0;

	PRIV_LOCK(priv);
	error = sysctl_handle_int(oidp, &result, 0, req);
	if (error || !req->newptr)
		goto done;

	/* Check if device is gone */
	if (priv->gone) {
		error = ENXIO;
		goto done;
	}

	if (result == 1) {
		eeprom.i2c_addr = MLX5E_I2C_ADDR_LOW;
		eeprom.device_addr = 0;
		eeprom.page_num = MLX5E_EEPROM_LOW_PAGE;
		eeprom.page_valid = 0;

		/* Read three first bytes to get important info */
		error = mlx5e_get_eeprom_info(priv, &eeprom);
		if (error) {
			if_printf(priv->ifp, "%s:%d: Failed reading eeprom's "
			    "initial information\n", __func__, __LINE__);
			error = 0;
			goto done;
		}
		/*
		 * Allocate needed length buffer and additional space for
		 * page 0x03
		 */
		eeprom.data = malloc(eeprom.len + MLX5E_EEPROM_PAGE_LENGTH,
		    M_MLX5EN, M_WAITOK | M_ZERO);

		/* Read the whole eeprom information */
		error = mlx5e_get_eeprom(priv, &eeprom);
		if (error) {
			if_printf(priv->ifp, "%s:%d: Failed reading eeprom\n",
			    __func__, __LINE__);
			error = 0;
			/*
			 * Continue printing partial information in case of
			 * an error
			 */
		}
		mlx5e_print_eeprom(&eeprom);
		free(eeprom.data, M_MLX5EN);
	}
done:
	PRIV_UNLOCK(priv);
	return (error);
}

static const char *mlx5e_params_desc[] = {
	MLX5E_PARAMS(MLX5E_STATS_DESC)
};

static const char *mlx5e_port_stats_debug_desc[] = {
	MLX5E_PORT_STATS_DEBUG(MLX5E_STATS_DESC)
};

static int
mlx5e_ethtool_debug_channel_info(SYSCTL_HANDLER_ARGS)
{
	struct mlx5e_priv *priv;
	struct sbuf sb;
	struct mlx5e_channel *c;
	struct mlx5e_sq *sq;
	struct mlx5e_rq *rq;
	int error, i, tc;

	priv = arg1;
	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	if (sbuf_new_for_sysctl(&sb, NULL, 128, req) == NULL)
		return (ENOMEM);
	sbuf_clear_flags(&sb, SBUF_INCLUDENUL);

	PRIV_LOCK(priv);
	if (test_bit(MLX5E_STATE_OPENED, &priv->state) == 0)
		goto out;
	for (i = 0; i < priv->params.num_channels; i++) {
		c = &priv->channel[i];
		rq = &c->rq;
		sbuf_printf(&sb, "channel %d rq %d cq %d\n",
		    c->ix, rq->rqn, rq->cq.mcq.cqn);
		for (tc = 0; tc < c->num_tc; tc++) {
			sq = &c->sq[tc];
			sbuf_printf(&sb, "channel %d tc %d sq %d cq %d\n",
			    c->ix, tc, sq->sqn, sq->cq.mcq.cqn);
		}
	}
out:
	PRIV_UNLOCK(priv);
	error = sbuf_finish(&sb);
	sbuf_delete(&sb);
	return (error);
}

static int
mlx5e_ethtool_debug_stats(SYSCTL_HANDLER_ARGS)
{
	struct mlx5e_priv *priv = arg1;
	int sys_debug;
	int error;

	PRIV_LOCK(priv);
	sys_debug = priv->sysctl_debug;
	error = sysctl_handle_int(oidp, &sys_debug, 0, req);
	if (error != 0 || !req->newptr)
		goto done;
	sys_debug = sys_debug ? 1 : 0;
	if (sys_debug == priv->sysctl_debug)
		goto done;

	if ((priv->sysctl_debug = sys_debug)) {
		mlx5e_create_stats(&priv->stats.port_stats_debug.ctx,
		    SYSCTL_CHILDREN(priv->sysctl_ifnet), "debug_stats",
		    mlx5e_port_stats_debug_desc, MLX5E_PORT_STATS_DEBUG_NUM,
		    priv->stats.port_stats_debug.arg);
		SYSCTL_ADD_PROC(&priv->stats.port_stats_debug.ctx,
		    SYSCTL_CHILDREN(priv->sysctl_ifnet), OID_AUTO,
		    "hw_ctx_debug",
		    CTLFLAG_RD | CTLFLAG_MPSAFE | CTLTYPE_STRING, priv, 0,
		    mlx5e_ethtool_debug_channel_info, "S", "");
	} else {
		sysctl_ctx_free(&priv->stats.port_stats_debug.ctx);
	}
done:
	PRIV_UNLOCK(priv);
	return (error);
}

static void
mlx5e_create_diagnostics(struct mlx5e_priv *priv)
{
	struct mlx5_core_diagnostics_entry entry;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *node;
	int x;

	/* sysctl context we are using */
	ctx = &priv->sysctl_ctx;

	/* create root node */
	node = SYSCTL_ADD_NODE(ctx,
	    SYSCTL_CHILDREN(priv->sysctl_ifnet), OID_AUTO,
	    "diagnostics", CTLFLAG_RD, NULL, "Diagnostics");
	if (node == NULL)
		return;

	/* create PCI diagnostics */
	for (x = 0; x != MLX5_CORE_PCI_DIAGNOSTICS_NUM; x++) {
		entry = mlx5_core_pci_diagnostics_table[x];
		if (mlx5_core_supports_diagnostics(priv->mdev, entry.counter_id) == 0)
			continue;
		SYSCTL_ADD_UQUAD(ctx, SYSCTL_CHILDREN(node), OID_AUTO,
		    entry.desc, CTLFLAG_RD, priv->params_pci.array + x,
		    "PCI diagnostics counter");
	}

	/* create general diagnostics */
	for (x = 0; x != MLX5_CORE_GENERAL_DIAGNOSTICS_NUM; x++) {
		entry = mlx5_core_general_diagnostics_table[x];
		if (mlx5_core_supports_diagnostics(priv->mdev, entry.counter_id) == 0)
			continue;
		SYSCTL_ADD_UQUAD(ctx, SYSCTL_CHILDREN(node), OID_AUTO,
		    entry.desc, CTLFLAG_RD, priv->params_general.array + x,
		    "General diagnostics counter");
	}
}

void
mlx5e_create_ethtool(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct sysctl_oid *node, *qos_node;
	const char *pnameunit;
	unsigned x;
	int i;

	/* set some defaults */
	priv->params_ethtool.tx_queue_size_max = 1 << MLX5E_PARAMS_MAXIMUM_LOG_SQ_SIZE;
	priv->params_ethtool.rx_queue_size_max = 1 << MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE;
	priv->params_ethtool.tx_queue_size = 1 << priv->params.log_sq_size;
	priv->params_ethtool.rx_queue_size = 1 << priv->params.log_rq_size;
	priv->params_ethtool.channels = priv->params.num_channels;
	priv->params_ethtool.channels_rsss = priv->params.channels_rsss;
	priv->params_ethtool.coalesce_pkts_max = MLX5E_FLD_MAX(cqc, cq_max_count);
	priv->params_ethtool.coalesce_usecs_max = MLX5E_FLD_MAX(cqc, cq_period);
	priv->params_ethtool.rx_coalesce_mode = priv->params.rx_cq_moderation_mode;
	priv->params_ethtool.rx_coalesce_usecs = priv->params.rx_cq_moderation_usec;
	priv->params_ethtool.rx_coalesce_pkts = priv->params.rx_cq_moderation_pkts;
	priv->params_ethtool.tx_coalesce_mode = priv->params.tx_cq_moderation_mode;
	priv->params_ethtool.tx_coalesce_usecs = priv->params.tx_cq_moderation_usec;
	priv->params_ethtool.tx_coalesce_pkts = priv->params.tx_cq_moderation_pkts;
	priv->params_ethtool.hw_lro = priv->params.hw_lro_en;
	priv->params_ethtool.cqe_zipping = priv->params.cqe_zipping_en;
	mlx5e_ethtool_sync_tx_completion_fact(priv);

	/* get default values for local loopback, if any */
	if (MLX5_CAP_GEN(priv->mdev, disable_local_lb)) {
		int err;
		u8 val;

		err = mlx5_nic_vport_query_local_lb(priv->mdev, MLX5_LOCAL_MC_LB, &val);
		if (err == 0)
			priv->params_ethtool.mc_local_lb = val;

		err = mlx5_nic_vport_query_local_lb(priv->mdev, MLX5_LOCAL_UC_LB, &val);
		if (err == 0)
			priv->params_ethtool.uc_local_lb = val;
	}

	/* create root node */
	node = SYSCTL_ADD_NODE(&priv->sysctl_ctx,
	    SYSCTL_CHILDREN(priv->sysctl_ifnet), OID_AUTO,
	    "conf", CTLFLAG_RW, NULL, "Configuration");
	if (node == NULL)
		return;
	for (x = 0; x != MLX5E_PARAMS_NUM; x++) {
		/* check for read-only parameter */
		if (strstr(mlx5e_params_desc[2 * x], "_max") != NULL ||
		    strstr(mlx5e_params_desc[2 * x], "_mtu") != NULL) {
			SYSCTL_ADD_PROC(&priv->sysctl_ctx, SYSCTL_CHILDREN(node), OID_AUTO,
			    mlx5e_params_desc[2 * x], CTLTYPE_U64 | CTLFLAG_RD |
			    CTLFLAG_MPSAFE, priv, x, &mlx5e_ethtool_handler, "QU",
			    mlx5e_params_desc[2 * x + 1]);
		} else {
#if (__FreeBSD_version < 1100000)
			char path[64];
#endif
			/*
			 * NOTE: In FreeBSD-11 and newer the
			 * CTLFLAG_RWTUN flag will take care of
			 * loading default sysctl value from the
			 * kernel environment, if any:
			 */
			SYSCTL_ADD_PROC(&priv->sysctl_ctx, SYSCTL_CHILDREN(node), OID_AUTO,
			    mlx5e_params_desc[2 * x], CTLTYPE_U64 | CTLFLAG_RWTUN |
			    CTLFLAG_MPSAFE, priv, x, &mlx5e_ethtool_handler, "QU",
			    mlx5e_params_desc[2 * x + 1]);

#if (__FreeBSD_version < 1100000)
			/* compute path for sysctl */
			snprintf(path, sizeof(path), "dev.mce.%d.conf.%s",
			    device_get_unit(priv->mdev->pdev->dev.bsddev),
			    mlx5e_params_desc[2 * x]);

			/* try to fetch tunable, if any */
			if (TUNABLE_QUAD_FETCH(path, &priv->params_ethtool.arg[x]))
				mlx5e_ethtool_handler(NULL, priv, x, NULL);
#endif
		}
	}

	SYSCTL_ADD_PROC(&priv->sysctl_ctx, SYSCTL_CHILDREN(node), OID_AUTO,
	    "debug_stats", CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, priv,
	    0, &mlx5e_ethtool_debug_stats, "I", "Extended debug statistics");

	pnameunit = device_get_nameunit(priv->mdev->pdev->dev.bsddev);

	SYSCTL_ADD_STRING(&priv->sysctl_ctx, SYSCTL_CHILDREN(node),
	    OID_AUTO, "device_name", CTLFLAG_RD,
	    __DECONST(void *, pnameunit), 0,
	    "PCI device name");

	/* EEPROM support */
	SYSCTL_ADD_PROC(&priv->sysctl_ctx, SYSCTL_CHILDREN(node), OID_AUTO, "eeprom_info",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, priv, 0,
	    mlx5e_read_eeprom, "I", "EEPROM information");

	/* Diagnostics support */
	mlx5e_create_diagnostics(priv);

	/* create qos node */
	qos_node = SYSCTL_ADD_NODE(&priv->sysctl_ctx,
	    SYSCTL_CHILDREN(node), OID_AUTO,
	    "qos", CTLFLAG_RW, NULL, "Quality Of Service configuration");
	if (qos_node == NULL)
		return;

	/* Priority rate limit support */
	if (mlx5e_getmaxrate(priv) == 0) {
		SYSCTL_ADD_PROC(&priv->sysctl_ctx, SYSCTL_CHILDREN(qos_node),
		    OID_AUTO, "tc_max_rate", CTLTYPE_U64 | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
		    priv, 0, mlx5e_tc_maxrate_handler, "QU",
		    "Max rate for priority, specified in kilobits, where kilo=1000, "
		    "max_rate must be divisible by 100000");
	}

	/* Bandwidth limiting by ratio */
	if (mlx5e_get_max_alloc(priv) == 0) {
		SYSCTL_ADD_PROC(&priv->sysctl_ctx, SYSCTL_CHILDREN(qos_node),
		    OID_AUTO, "tc_rate_share", CTLTYPE_U8 | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
		    priv, 0, mlx5e_tc_rate_share_handler, "QU",
		    "Specify bandwidth ratio from 1 to 100 "
		    "for the available traffic classes");
	}

	/* Priority to traffic class mapping */
	if (mlx5e_get_prio_tc(priv) == 0) {
		for (i = 0; i <= mlx5_max_tc(mdev); i++) {
			char name[32];
			snprintf(name, sizeof(name), "prio_%d_to_tc", i);
			SYSCTL_ADD_PROC(&priv->sysctl_ctx, SYSCTL_CHILDREN(qos_node),
				OID_AUTO, name, CTLTYPE_U8 | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
				priv, i, mlx5e_prio_to_tc_handler, "CU",
				"Set priority to traffic class");
		}
	}

	/* DSCP support */
	if (mlx5e_get_dscp(priv) == 0) {
		for (i = 0; i != MLX5_MAX_SUPPORTED_DSCP; i += 8) {
			char name[32];
			snprintf(name, sizeof(name), "dscp_%d_%d_prio", i, i + 7);
			SYSCTL_ADD_PROC(&priv->sysctl_ctx, SYSCTL_CHILDREN(qos_node),
				OID_AUTO, name, CTLTYPE_U8 | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
				priv, i, mlx5e_dscp_prio_handler, "CU",
				"Set DSCP to priority mapping, 0..7");
		}
#define	A	"Set trust state, 1:PCP 2:DSCP"
#define	B	" 3:BOTH"
		SYSCTL_ADD_PROC(&priv->sysctl_ctx, SYSCTL_CHILDREN(qos_node),
		    OID_AUTO, "trust_state", CTLTYPE_U8 | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
		    priv, 0, mlx5e_trust_state_handler, "CU",
		    MLX5_CAP_QCAM_FEATURE(mdev, qpts_trust_both) ?
		    A B : A);
#undef B
#undef A
	}
}
