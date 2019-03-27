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

static struct ib_ah *create_ib_ah(struct mlx5_ib_dev *dev,
				  struct mlx5_ib_ah *ah,
				  struct ib_ah_attr *ah_attr,
				  enum rdma_link_layer ll)
{
	if (ah_attr->ah_flags & IB_AH_GRH) {
		memcpy(ah->av.rgid, &ah_attr->grh.dgid, 16);
		ah->av.grh_gid_fl = cpu_to_be32(ah_attr->grh.flow_label |
						(1 << 30) |
						ah_attr->grh.sgid_index << 20);
		ah->av.hop_limit = ah_attr->grh.hop_limit;
		ah->av.tclass = ah_attr->grh.traffic_class;
	}

	ah->av.stat_rate_sl = (ah_attr->static_rate << 4);

	if (ll == IB_LINK_LAYER_ETHERNET) {
		memcpy(ah->av.rmac, ah_attr->dmac, sizeof(ah_attr->dmac));
		ah->av.udp_sport =
			mlx5_get_roce_udp_sport(dev,
						ah_attr->port_num,
						ah_attr->grh.sgid_index);
		ah->av.stat_rate_sl |= (ah_attr->sl & 0x7) << 1;
	} else {
		ah->av.rlid = cpu_to_be16(ah_attr->dlid);
		ah->av.fl_mlid = ah_attr->src_path_bits & 0x7f;
		ah->av.stat_rate_sl |= (ah_attr->sl & 0xf);
	}

	return &ah->ibah;
}

struct ib_ah *mlx5_ib_create_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr,
				struct ib_udata *udata)

{
	struct mlx5_ib_ah *ah;
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	enum rdma_link_layer ll;

	ll = pd->device->get_link_layer(pd->device, ah_attr->port_num);

	if (ll == IB_LINK_LAYER_ETHERNET && !(ah_attr->ah_flags & IB_AH_GRH))
		return ERR_PTR(-EINVAL);

	if (ll == IB_LINK_LAYER_ETHERNET && udata) {
		int err;
		struct mlx5_ib_create_ah_resp resp = {};
		u32 min_resp_len = offsetof(typeof(resp), dmac) +
				   sizeof(resp.dmac);

		if (udata->outlen < min_resp_len)
			return ERR_PTR(-EINVAL);

		resp.response_length = min_resp_len;

		err = ib_resolve_eth_dmac(pd->device, ah_attr);
		if (err)
			return ERR_PTR(err);

		memcpy(resp.dmac, ah_attr->dmac, ETH_ALEN);
		err = ib_copy_to_udata(udata, &resp, resp.response_length);
		if (err)
			return ERR_PTR(err);
	}

	ah = kzalloc(sizeof(*ah), GFP_ATOMIC);
	if (!ah)
		return ERR_PTR(-ENOMEM);

	return create_ib_ah(dev, ah, ah_attr, ll); /* never fails */
}

int mlx5_ib_query_ah(struct ib_ah *ibah, struct ib_ah_attr *ah_attr)
{
	struct mlx5_ib_ah *ah = to_mah(ibah);
	u32 tmp;

	memset(ah_attr, 0, sizeof(*ah_attr));

	tmp = be32_to_cpu(ah->av.grh_gid_fl);
	if (tmp & (1 << 30)) {
		ah_attr->ah_flags = IB_AH_GRH;
		ah_attr->grh.sgid_index = (tmp >> 20) & 0xff;
		ah_attr->grh.flow_label = tmp & 0xfffff;
		memcpy(&ah_attr->grh.dgid, ah->av.rgid, 16);
		ah_attr->grh.hop_limit = ah->av.hop_limit;
		ah_attr->grh.traffic_class = ah->av.tclass;
	}
	ah_attr->dlid = be16_to_cpu(ah->av.rlid);
	ah_attr->static_rate = ah->av.stat_rate_sl >> 4;
	ah_attr->sl = ah->av.stat_rate_sl & 0xf;

	return 0;
}

int mlx5_ib_destroy_ah(struct ib_ah *ah)
{
	kfree(to_mah(ah));
	return 0;
}
