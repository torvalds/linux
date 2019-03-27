/*-
 * Copyright (c) 2013-2015, Mellanox Technologies, Ltd.  All rights reserved.
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

#include "mlx5_ib.h"

#include <dev/mlx5/cmd.h>

static const char *mlx5_ib_cong_params_desc[] = {
	MLX5_IB_CONG_PARAMS(MLX5_IB_STATS_DESC)
};

static const char *mlx5_ib_cong_stats_desc[] = {
	MLX5_IB_CONG_STATS(MLX5_IB_STATS_DESC)
};

#define	MLX5_IB_INDEX(field) (__offsetof(struct mlx5_ib_congestion, field) / sizeof(u64))
#define	MLX5_IB_FLD_MAX(type, field) ((1ULL << __mlx5_bit_sz(type, field)) - 1ULL)
#define	MLX5_IB_SET_CLIPPED(type, ptr, field, var) do { \
  /* rangecheck */					\
  if ((var) > MLX5_IB_FLD_MAX(type, field))		\
	(var) = MLX5_IB_FLD_MAX(type, field);		\
  /* set value */					\
  MLX5_SET(type, ptr, field, var);			\
} while (0)

#define	CONG_LOCK(dev) sx_xlock(&(dev)->congestion.lock)
#define	CONG_UNLOCK(dev) sx_xunlock(&(dev)->congestion.lock)
#define	CONG_LOCKED(dev) sx_xlocked(&(dev)->congestion.lock)

#define	MLX5_IB_RP_CLAMP_TGT_RATE_ATTR			BIT(1)
#define	MLX5_IB_RP_CLAMP_TGT_RATE_ATI_ATTR		BIT(2)
#define	MLX5_IB_RP_TIME_RESET_ATTR			BIT(3)
#define	MLX5_IB_RP_BYTE_RESET_ATTR			BIT(4)
#define	MLX5_IB_RP_THRESHOLD_ATTR			BIT(5)
#define	MLX5_IB_RP_AI_RATE_ATTR				BIT(7)
#define	MLX5_IB_RP_HAI_RATE_ATTR			BIT(8)
#define	MLX5_IB_RP_MIN_DEC_FAC_ATTR			BIT(9)
#define	MLX5_IB_RP_MIN_RATE_ATTR			BIT(10)
#define	MLX5_IB_RP_RATE_TO_SET_ON_FIRST_CNP_ATTR	BIT(11)
#define	MLX5_IB_RP_DCE_TCP_G_ATTR			BIT(12)
#define	MLX5_IB_RP_DCE_TCP_RTT_ATTR			BIT(13)
#define	MLX5_IB_RP_RATE_REDUCE_MONITOR_PERIOD_ATTR	BIT(14)
#define	MLX5_IB_RP_INITIAL_ALPHA_VALUE_ATTR		BIT(15)
#define	MLX5_IB_RP_GD_ATTR				BIT(16)

#define	MLX5_IB_NP_CNP_DSCP_ATTR			BIT(3)
#define	MLX5_IB_NP_CNP_PRIO_MODE_ATTR			BIT(4)

enum mlx5_ib_cong_node_type {
	MLX5_IB_RROCE_ECN_RP = 1,
	MLX5_IB_RROCE_ECN_NP = 2,
};

static enum mlx5_ib_cong_node_type
mlx5_ib_param_to_node(u32 index)
{

	if (index >= MLX5_IB_INDEX(rp_clamp_tgt_rate) &&
	    index <= MLX5_IB_INDEX(rp_gd))
		return MLX5_IB_RROCE_ECN_RP;
	else
		return MLX5_IB_RROCE_ECN_NP;
}

static u64
mlx5_get_cc_param_val(void *field, u32 index)
{

	switch (index) {
	case MLX5_IB_INDEX(rp_clamp_tgt_rate):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				clamp_tgt_rate);
	case MLX5_IB_INDEX(rp_clamp_tgt_rate_ati):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				clamp_tgt_rate_after_time_inc);
	case MLX5_IB_INDEX(rp_time_reset):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_time_reset);
	case MLX5_IB_INDEX(rp_byte_reset):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_byte_reset);
	case MLX5_IB_INDEX(rp_threshold):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_threshold);
	case MLX5_IB_INDEX(rp_ai_rate):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_ai_rate);
	case MLX5_IB_INDEX(rp_hai_rate):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_hai_rate);
	case MLX5_IB_INDEX(rp_min_dec_fac):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_min_dec_fac);
	case MLX5_IB_INDEX(rp_min_rate):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_min_rate);
	case MLX5_IB_INDEX(rp_rate_to_set_on_first_cnp):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rate_to_set_on_first_cnp);
	case MLX5_IB_INDEX(rp_dce_tcp_g):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				dce_tcp_g);
	case MLX5_IB_INDEX(rp_dce_tcp_rtt):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				dce_tcp_rtt);
	case MLX5_IB_INDEX(rp_rate_reduce_monitor_period):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rate_reduce_monitor_period);
	case MLX5_IB_INDEX(rp_initial_alpha_value):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				initial_alpha_value);
	case MLX5_IB_INDEX(rp_gd):
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_gd);
	case MLX5_IB_INDEX(np_cnp_dscp):
		return MLX5_GET(cong_control_r_roce_ecn_np, field,
				cnp_dscp);
	case MLX5_IB_INDEX(np_cnp_prio_mode):
		return MLX5_GET(cong_control_r_roce_ecn_np, field,
				cnp_prio_mode);
	case MLX5_IB_INDEX(np_cnp_prio):
		return MLX5_GET(cong_control_r_roce_ecn_np, field,
				cnp_802p_prio);
	default:
		return 0;
	}
}

static void
mlx5_ib_set_cc_param_mask_val(void *field, u32 index,
    u64 var, u32 *attr_mask)
{

	switch (index) {
	case MLX5_IB_INDEX(rp_clamp_tgt_rate):
		*attr_mask |= MLX5_IB_RP_CLAMP_TGT_RATE_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 clamp_tgt_rate, var);
		break;
	case MLX5_IB_INDEX(rp_clamp_tgt_rate_ati):
		*attr_mask |= MLX5_IB_RP_CLAMP_TGT_RATE_ATI_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 clamp_tgt_rate_after_time_inc, var);
		break;
	case MLX5_IB_INDEX(rp_time_reset):
		*attr_mask |= MLX5_IB_RP_TIME_RESET_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 rpg_time_reset, var);
		break;
	case MLX5_IB_INDEX(rp_byte_reset):
		*attr_mask |= MLX5_IB_RP_BYTE_RESET_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 rpg_byte_reset, var);
		break;
	case MLX5_IB_INDEX(rp_threshold):
		*attr_mask |= MLX5_IB_RP_THRESHOLD_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 rpg_threshold, var);
		break;
	case MLX5_IB_INDEX(rp_ai_rate):
		*attr_mask |= MLX5_IB_RP_AI_RATE_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 rpg_ai_rate, var);
		break;
	case MLX5_IB_INDEX(rp_hai_rate):
		*attr_mask |= MLX5_IB_RP_HAI_RATE_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 rpg_hai_rate, var);
		break;
	case MLX5_IB_INDEX(rp_min_dec_fac):
		*attr_mask |= MLX5_IB_RP_MIN_DEC_FAC_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 rpg_min_dec_fac, var);
		break;
	case MLX5_IB_INDEX(rp_min_rate):
		*attr_mask |= MLX5_IB_RP_MIN_RATE_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 rpg_min_rate, var);
		break;
	case MLX5_IB_INDEX(rp_rate_to_set_on_first_cnp):
		*attr_mask |= MLX5_IB_RP_RATE_TO_SET_ON_FIRST_CNP_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 rate_to_set_on_first_cnp, var);
		break;
	case MLX5_IB_INDEX(rp_dce_tcp_g):
		*attr_mask |= MLX5_IB_RP_DCE_TCP_G_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 dce_tcp_g, var);
		break;
	case MLX5_IB_INDEX(rp_dce_tcp_rtt):
		*attr_mask |= MLX5_IB_RP_DCE_TCP_RTT_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 dce_tcp_rtt, var);
		break;
	case MLX5_IB_INDEX(rp_rate_reduce_monitor_period):
		*attr_mask |= MLX5_IB_RP_RATE_REDUCE_MONITOR_PERIOD_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 rate_reduce_monitor_period, var);
		break;
	case MLX5_IB_INDEX(rp_initial_alpha_value):
		*attr_mask |= MLX5_IB_RP_INITIAL_ALPHA_VALUE_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 initial_alpha_value, var);
		break;
	case MLX5_IB_INDEX(rp_gd):
		*attr_mask |= MLX5_IB_RP_GD_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_rp, field,
			 rpg_gd, var);
		break;
	case MLX5_IB_INDEX(np_cnp_dscp):
		*attr_mask |= MLX5_IB_NP_CNP_DSCP_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_np, field, cnp_dscp, var);
		break;
	case MLX5_IB_INDEX(np_cnp_prio_mode):
		*attr_mask |= MLX5_IB_NP_CNP_PRIO_MODE_ATTR;
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_np, field, cnp_prio_mode, var);
		break;
	case MLX5_IB_INDEX(np_cnp_prio):
		*attr_mask |= MLX5_IB_NP_CNP_PRIO_MODE_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_np, field, cnp_prio_mode, 0);
		MLX5_IB_SET_CLIPPED(cong_control_r_roce_ecn_np, field, cnp_802p_prio, var);
		break;
	default:
		break;
	}
}

static int
mlx5_ib_get_all_cc_params(struct mlx5_ib_dev *dev)
{
	int outlen = MLX5_ST_SZ_BYTES(query_cong_params_out);
	enum mlx5_ib_cong_node_type node = 0;
	void *out;
	void *field;
	u32 x;
	int err = 0;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	/* get the current values */
	for (x = 0; x != MLX5_IB_CONG_PARAMS_NUM; x++) {
		if (node != mlx5_ib_param_to_node(x)) {
			node = mlx5_ib_param_to_node(x);

			err = mlx5_cmd_query_cong_params(dev->mdev, node, out, outlen);
			if (err)
				break;
		}
		field = MLX5_ADDR_OF(query_cong_params_out, out, congestion_parameters);
		dev->congestion.arg[x] = mlx5_get_cc_param_val(field, x);
	}
	kfree(out);
	return err;
}

static int
mlx5_ib_set_cc_params(struct mlx5_ib_dev *dev, u32 index, u64 var)
{
	int inlen = MLX5_ST_SZ_BYTES(modify_cong_params_in);
	enum mlx5_ib_cong_node_type node;
	u32 attr_mask = 0;
	void *field;
	void *in;
	int err;

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_cong_params_in, in, opcode,
		 MLX5_CMD_OP_MODIFY_CONG_PARAMS);

	node = mlx5_ib_param_to_node(index);
	MLX5_SET(modify_cong_params_in, in, cong_protocol, node);

	field = MLX5_ADDR_OF(modify_cong_params_in, in, congestion_parameters);
	mlx5_ib_set_cc_param_mask_val(field, index, var, &attr_mask);

	field = MLX5_ADDR_OF(modify_cong_params_in, in, field_select);
	MLX5_SET(field_select_r_roce_rp, field, field_select_r_roce_rp,
		 attr_mask);

	err = mlx5_cmd_modify_cong_params(dev->mdev, in, inlen);
	kfree(in);

	return err;
}

static int
mlx5_ib_cong_params_handler(SYSCTL_HANDLER_ARGS)
{
	struct mlx5_ib_dev *dev = arg1;
	u64 value;
	int error;

	CONG_LOCK(dev);
	value = dev->congestion.arg[arg2];
	if (req != NULL) {
		error = sysctl_handle_64(oidp, &value, 0, req);
		if (error || req->newptr == NULL ||
		    value == dev->congestion.arg[arg2])
			goto done;

		/* assign new value */
		dev->congestion.arg[arg2] = value;
	} else {
		error = 0;
	}
	if (!MLX5_CAP_GEN(dev->mdev, cc_modify_allowed))
		error = EPERM;
	else {
		error = -mlx5_ib_set_cc_params(dev, MLX5_IB_INDEX(arg[arg2]),
		    dev->congestion.arg[arg2]);
	}
done:
	CONG_UNLOCK(dev);

	return (error);
}

#define	MLX5_GET_UNALIGNED_64(t,p,f) \
    (((u64)MLX5_GET(t,p,f##_high) << 32) | MLX5_GET(t,p,f##_low))

static void
mlx5_ib_read_cong_stats(struct work_struct *work)
{
	struct mlx5_ib_dev *dev =
	    container_of(work, struct mlx5_ib_dev, congestion.dwork.work);
	const int outlen = MLX5_ST_SZ_BYTES(query_cong_statistics_out);
	void *out;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		goto done;

	CONG_LOCK(dev);
	if (mlx5_cmd_query_cong_counter(dev->mdev, 0, out, outlen))
		memset(out, 0, outlen);

	dev->congestion.syndrome =
	    MLX5_GET(query_cong_statistics_out, out, syndrome);
	dev->congestion.rp_cur_flows =
	    MLX5_GET(query_cong_statistics_out, out, rp_cur_flows);
	dev->congestion.sum_flows =
	    MLX5_GET(query_cong_statistics_out, out, sum_flows);
	dev->congestion.rp_cnp_ignored =
	    MLX5_GET_UNALIGNED_64(query_cong_statistics_out, out, rp_cnp_ignored);
	dev->congestion.rp_cnp_handled =
	    MLX5_GET_UNALIGNED_64(query_cong_statistics_out, out, rp_cnp_handled);
	dev->congestion.time_stamp =
	    MLX5_GET_UNALIGNED_64(query_cong_statistics_out, out, time_stamp);
	dev->congestion.accumulators_period =
	    MLX5_GET(query_cong_statistics_out, out, accumulators_period);
	dev->congestion.np_ecn_marked_roce_packets =
	    MLX5_GET_UNALIGNED_64(query_cong_statistics_out, out, np_ecn_marked_roce_packets);
	dev->congestion.np_cnp_sent =
	    MLX5_GET_UNALIGNED_64(query_cong_statistics_out, out, np_cnp_sent);

	CONG_UNLOCK(dev);
	kfree(out);

done:
	schedule_delayed_work(&dev->congestion.dwork, hz);
}

void
mlx5_ib_cleanup_congestion(struct mlx5_ib_dev *dev)
{

	while (cancel_delayed_work_sync(&dev->congestion.dwork))
		;
	sysctl_ctx_free(&dev->congestion.ctx);
	sx_destroy(&dev->congestion.lock);
}

int
mlx5_ib_init_congestion(struct mlx5_ib_dev *dev)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *parent;
	struct sysctl_oid *node;
	int err;
	u32 x;

	ctx = &dev->congestion.ctx;
	sysctl_ctx_init(ctx);
	sx_init(&dev->congestion.lock, "mlx5ibcong");
	INIT_DELAYED_WORK(&dev->congestion.dwork, mlx5_ib_read_cong_stats);

	if (!MLX5_CAP_GEN(dev->mdev, cc_query_allowed))
		return (0);

	err = mlx5_ib_get_all_cc_params(dev);
	if (err)
		return (err);

	parent = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(dev->ib_dev.dev.kobj.oidp),
	    OID_AUTO, "cong", CTLFLAG_RW, NULL, "Congestion control");
	if (parent == NULL)
		return (-ENOMEM);

	node = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(parent),
	    OID_AUTO, "conf", CTLFLAG_RW, NULL, "Configuration");
	if (node == NULL) {
		sysctl_ctx_free(&dev->congestion.ctx);
		return (-ENOMEM);
	}

	for (x = 0; x != MLX5_IB_CONG_PARAMS_NUM; x++) {
		SYSCTL_ADD_PROC(ctx,
		    SYSCTL_CHILDREN(node), OID_AUTO,
		    mlx5_ib_cong_params_desc[2 * x],
		    CTLTYPE_U64 | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
		    dev, x, &mlx5_ib_cong_params_handler, "QU",
		    mlx5_ib_cong_params_desc[2 * x + 1]);
	}

	node = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(parent),
	    OID_AUTO, "stats", CTLFLAG_RD, NULL, "Statistics");
	if (node == NULL) {
		sysctl_ctx_free(&dev->congestion.ctx);
		return (-ENOMEM);
	}

	for (x = 0; x != MLX5_IB_CONG_STATS_NUM; x++) {
		/* read-only SYSCTLs */
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO,
		    mlx5_ib_cong_stats_desc[2 * x],
		    CTLFLAG_RD | CTLFLAG_MPSAFE,
		    &dev->congestion.arg[x + MLX5_IB_CONG_PARAMS_NUM],
		    0, mlx5_ib_cong_stats_desc[2 * x + 1]);
	}
	schedule_delayed_work(&dev->congestion.dwork, hz);
	return (0);
}
