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

#include <linux/list.h>
#include <dev/mlx5/fs.h>

#define MLX5_SET_CFG(p, f, v) MLX5_SET(create_flow_group_in, p, f, v)

enum {
	MLX5E_FULLMATCH = 0,
	MLX5E_ALLMULTI = 1,
	MLX5E_PROMISC = 2,
};

enum {
	MLX5E_UC = 0,
	MLX5E_MC_IPV4 = 1,
	MLX5E_MC_IPV6 = 2,
	MLX5E_MC_OTHER = 3,
};

enum {
	MLX5E_ACTION_NONE = 0,
	MLX5E_ACTION_ADD = 1,
	MLX5E_ACTION_DEL = 2,
};

struct mlx5e_eth_addr_hash_node {
	LIST_ENTRY(mlx5e_eth_addr_hash_node) hlist;
	u8	action;
	struct mlx5e_eth_addr_info ai;
};

static inline int
mlx5e_hash_eth_addr(const u8 * addr)
{
	return (addr[5]);
}

static void
mlx5e_add_eth_addr_to_hash(struct mlx5e_eth_addr_hash_head *hash,
    const u8 * addr)
{
	struct mlx5e_eth_addr_hash_node *hn;
	int ix = mlx5e_hash_eth_addr(addr);

	LIST_FOREACH(hn, &hash[ix], hlist) {
		if (bcmp(hn->ai.addr, addr, ETHER_ADDR_LEN) == 0) {
			if (hn->action == MLX5E_ACTION_DEL)
				hn->action = MLX5E_ACTION_NONE;
			return;
		}
	}

	hn = malloc(sizeof(*hn), M_MLX5EN, M_NOWAIT | M_ZERO);
	if (hn == NULL)
		return;

	ether_addr_copy(hn->ai.addr, addr);
	hn->action = MLX5E_ACTION_ADD;

	LIST_INSERT_HEAD(&hash[ix], hn, hlist);
}

static void
mlx5e_del_eth_addr_from_hash(struct mlx5e_eth_addr_hash_node *hn)
{
	LIST_REMOVE(hn, hlist);
	free(hn, M_MLX5EN);
}

static void
mlx5e_del_eth_addr_from_flow_table(struct mlx5e_priv *priv,
    struct mlx5e_eth_addr_info *ai)
{
	if (ai->tt_vec & (1 << MLX5E_TT_IPV6_IPSEC_ESP))
		mlx5_del_flow_rule(ai->ft_rule[MLX5E_TT_IPV6_IPSEC_ESP]);

	if (ai->tt_vec & (1 << MLX5E_TT_IPV4_IPSEC_ESP))
		mlx5_del_flow_rule(ai->ft_rule[MLX5E_TT_IPV4_IPSEC_ESP]);

	if (ai->tt_vec & (1 << MLX5E_TT_IPV6_IPSEC_AH))
		mlx5_del_flow_rule(ai->ft_rule[MLX5E_TT_IPV6_IPSEC_AH]);

	if (ai->tt_vec & (1 << MLX5E_TT_IPV4_IPSEC_AH))
		mlx5_del_flow_rule(ai->ft_rule[MLX5E_TT_IPV4_IPSEC_AH]);

	if (ai->tt_vec & (1 << MLX5E_TT_IPV6_TCP))
		mlx5_del_flow_rule(ai->ft_rule[MLX5E_TT_IPV6_TCP]);

	if (ai->tt_vec & (1 << MLX5E_TT_IPV4_TCP))
		mlx5_del_flow_rule(ai->ft_rule[MLX5E_TT_IPV4_TCP]);

	if (ai->tt_vec & (1 << MLX5E_TT_IPV6_UDP))
		mlx5_del_flow_rule(ai->ft_rule[MLX5E_TT_IPV6_UDP]);

	if (ai->tt_vec & (1 << MLX5E_TT_IPV4_UDP))
		mlx5_del_flow_rule(ai->ft_rule[MLX5E_TT_IPV4_UDP]);

	if (ai->tt_vec & (1 << MLX5E_TT_IPV6))
		mlx5_del_flow_rule(ai->ft_rule[MLX5E_TT_IPV6]);

	if (ai->tt_vec & (1 << MLX5E_TT_IPV4))
		mlx5_del_flow_rule(ai->ft_rule[MLX5E_TT_IPV4]);

	if (ai->tt_vec & (1 << MLX5E_TT_ANY))
		mlx5_del_flow_rule(ai->ft_rule[MLX5E_TT_ANY]);
}

static int
mlx5e_get_eth_addr_type(const u8 * addr)
{
	if (ETHER_IS_MULTICAST(addr) == 0)
		return (MLX5E_UC);

	if ((addr[0] == 0x01) &&
	    (addr[1] == 0x00) &&
	    (addr[2] == 0x5e) &&
	    !(addr[3] & 0x80))
		return (MLX5E_MC_IPV4);

	if ((addr[0] == 0x33) &&
	    (addr[1] == 0x33))
		return (MLX5E_MC_IPV6);

	return (MLX5E_MC_OTHER);
}

static	u32
mlx5e_get_tt_vec(struct mlx5e_eth_addr_info *ai, int type)
{
	int eth_addr_type;
	u32 ret;

	switch (type) {
	case MLX5E_FULLMATCH:
		eth_addr_type = mlx5e_get_eth_addr_type(ai->addr);
		switch (eth_addr_type) {
		case MLX5E_UC:
			ret =
			    (1 << MLX5E_TT_IPV4_TCP) |
			    (1 << MLX5E_TT_IPV6_TCP) |
			    (1 << MLX5E_TT_IPV4_UDP) |
			    (1 << MLX5E_TT_IPV6_UDP) |
			    (1 << MLX5E_TT_IPV4) |
			    (1 << MLX5E_TT_IPV6) |
			    (1 << MLX5E_TT_ANY) |
			    0;
			break;

		case MLX5E_MC_IPV4:
			ret =
			    (1 << MLX5E_TT_IPV4_UDP) |
			    (1 << MLX5E_TT_IPV4) |
			    0;
			break;

		case MLX5E_MC_IPV6:
			ret =
			    (1 << MLX5E_TT_IPV6_UDP) |
			    (1 << MLX5E_TT_IPV6) |
			    0;
			break;

		default:
			ret =
			    (1 << MLX5E_TT_ANY) |
			    0;
			break;
		}
		break;

	case MLX5E_ALLMULTI:
		ret =
		    (1 << MLX5E_TT_IPV4_UDP) |
		    (1 << MLX5E_TT_IPV6_UDP) |
		    (1 << MLX5E_TT_IPV4) |
		    (1 << MLX5E_TT_IPV6) |
		    (1 << MLX5E_TT_ANY) |
		    0;
		break;

	default:			/* MLX5E_PROMISC */
		ret =
		    (1 << MLX5E_TT_IPV4_TCP) |
		    (1 << MLX5E_TT_IPV6_TCP) |
		    (1 << MLX5E_TT_IPV4_UDP) |
		    (1 << MLX5E_TT_IPV6_UDP) |
		    (1 << MLX5E_TT_IPV4) |
		    (1 << MLX5E_TT_IPV6) |
		    (1 << MLX5E_TT_ANY) |
		    0;
		break;
	}

	return (ret);
}

static int
mlx5e_add_eth_addr_rule_sub(struct mlx5e_priv *priv,
    struct mlx5e_eth_addr_info *ai, int type,
    u32 *mc, u32 *mv)
{
	struct mlx5_flow_destination dest;
	u8 mc_enable = 0;
	struct mlx5_flow_rule **rule_p;
	struct mlx5_flow_table *ft = priv->fts.main.t;
	u8 *mc_dmac = MLX5_ADDR_OF(fte_match_param, mc,
				   outer_headers.dmac_47_16);
	u8 *mv_dmac = MLX5_ADDR_OF(fte_match_param, mv,
				   outer_headers.dmac_47_16);
	u32 *tirn = priv->tirn;
	u32 tt_vec;
	int err = 0;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_TIR;

	switch (type) {
	case MLX5E_FULLMATCH:
		mc_enable = MLX5_MATCH_OUTER_HEADERS;
		memset(mc_dmac, 0xff, ETH_ALEN);
		ether_addr_copy(mv_dmac, ai->addr);
		break;

	case MLX5E_ALLMULTI:
		mc_enable = MLX5_MATCH_OUTER_HEADERS;
		mc_dmac[0] = 0x01;
		mv_dmac[0] = 0x01;
		break;

	case MLX5E_PROMISC:
		break;
	default:
		break;
	}

	tt_vec = mlx5e_get_tt_vec(ai, type);

	if (tt_vec & BIT(MLX5E_TT_ANY)) {
		rule_p = &ai->ft_rule[MLX5E_TT_ANY];
		dest.tir_num = tirn[MLX5E_TT_ANY];
		*rule_p = mlx5_add_flow_rule(ft, mc_enable, mc, mv,
					     MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
					     MLX5_FS_ETH_FLOW_TAG, &dest);
		if (IS_ERR_OR_NULL(*rule_p))
			goto err_del_ai;
		ai->tt_vec |= BIT(MLX5E_TT_ANY);
	}

	mc_enable = MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ethertype);

	if (tt_vec & BIT(MLX5E_TT_IPV4)) {
		rule_p = &ai->ft_rule[MLX5E_TT_IPV4];
		dest.tir_num = tirn[MLX5E_TT_IPV4];
		MLX5_SET(fte_match_param, mv, outer_headers.ethertype,
			 ETHERTYPE_IP);
		*rule_p = mlx5_add_flow_rule(ft, mc_enable, mc, mv,
					     MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
					     MLX5_FS_ETH_FLOW_TAG, &dest);
		if (IS_ERR_OR_NULL(*rule_p))
			goto err_del_ai;
		ai->tt_vec |= BIT(MLX5E_TT_IPV4);
	}

	if (tt_vec & BIT(MLX5E_TT_IPV6)) {
		rule_p = &ai->ft_rule[MLX5E_TT_IPV6];
		dest.tir_num = tirn[MLX5E_TT_IPV6];
		MLX5_SET(fte_match_param, mv, outer_headers.ethertype,
			 ETHERTYPE_IPV6);
		*rule_p = mlx5_add_flow_rule(ft, mc_enable, mc, mv,
					     MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
					     MLX5_FS_ETH_FLOW_TAG, &dest);
		if (IS_ERR_OR_NULL(*rule_p))
			goto err_del_ai;
		ai->tt_vec |= BIT(MLX5E_TT_IPV6);
	}

	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ip_protocol);
	MLX5_SET(fte_match_param, mv, outer_headers.ip_protocol, IPPROTO_UDP);

	if (tt_vec & BIT(MLX5E_TT_IPV4_UDP)) {
		rule_p = &ai->ft_rule[MLX5E_TT_IPV4_UDP];
		dest.tir_num = tirn[MLX5E_TT_IPV4_UDP];
		MLX5_SET(fte_match_param, mv, outer_headers.ethertype,
			 ETHERTYPE_IP);
		*rule_p = mlx5_add_flow_rule(ft, mc_enable, mc, mv,
					     MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
					     MLX5_FS_ETH_FLOW_TAG, &dest);
		if (IS_ERR_OR_NULL(*rule_p))
			goto err_del_ai;
		ai->tt_vec |= BIT(MLX5E_TT_IPV4_UDP);
	}

	if (tt_vec & BIT(MLX5E_TT_IPV6_UDP)) {
		rule_p = &ai->ft_rule[MLX5E_TT_IPV6_UDP];
		dest.tir_num = tirn[MLX5E_TT_IPV6_UDP];
		MLX5_SET(fte_match_param, mv, outer_headers.ethertype,
			 ETHERTYPE_IPV6);
		*rule_p = mlx5_add_flow_rule(ft, mc_enable, mc, mv,
					     MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
					     MLX5_FS_ETH_FLOW_TAG, &dest);
		if (IS_ERR_OR_NULL(*rule_p))
			goto err_del_ai;
		ai->tt_vec |= BIT(MLX5E_TT_IPV6_UDP);
	}

	MLX5_SET(fte_match_param, mv, outer_headers.ip_protocol, IPPROTO_TCP);

	if (tt_vec & BIT(MLX5E_TT_IPV4_TCP)) {
		rule_p = &ai->ft_rule[MLX5E_TT_IPV4_TCP];
		dest.tir_num = tirn[MLX5E_TT_IPV4_TCP];
		MLX5_SET(fte_match_param, mv, outer_headers.ethertype,
			 ETHERTYPE_IP);
		*rule_p = mlx5_add_flow_rule(ft, mc_enable, mc, mv,
					     MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
					     MLX5_FS_ETH_FLOW_TAG, &dest);
		if (IS_ERR_OR_NULL(*rule_p))
			goto err_del_ai;
		ai->tt_vec |= BIT(MLX5E_TT_IPV4_TCP);
	}

	if (tt_vec & BIT(MLX5E_TT_IPV6_TCP)) {
		rule_p = &ai->ft_rule[MLX5E_TT_IPV6_TCP];
		dest.tir_num = tirn[MLX5E_TT_IPV6_TCP];
		MLX5_SET(fte_match_param, mv, outer_headers.ethertype,
			 ETHERTYPE_IPV6);
		*rule_p = mlx5_add_flow_rule(ft, mc_enable, mc, mv,
					     MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
					     MLX5_FS_ETH_FLOW_TAG, &dest);
		if (IS_ERR_OR_NULL(*rule_p))
			goto err_del_ai;

		ai->tt_vec |= BIT(MLX5E_TT_IPV6_TCP);
	}

	MLX5_SET(fte_match_param, mv, outer_headers.ip_protocol, IPPROTO_AH);

	if (tt_vec & BIT(MLX5E_TT_IPV4_IPSEC_AH)) {
		rule_p = &ai->ft_rule[MLX5E_TT_IPV4_IPSEC_AH];
		dest.tir_num = tirn[MLX5E_TT_IPV4_IPSEC_AH];
		MLX5_SET(fte_match_param, mv, outer_headers.ethertype,
			 ETHERTYPE_IP);
		*rule_p = mlx5_add_flow_rule(ft, mc_enable, mc, mv,
					     MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
					     MLX5_FS_ETH_FLOW_TAG, &dest);
		if (IS_ERR_OR_NULL(*rule_p))
			goto err_del_ai;
		ai->tt_vec |= BIT(MLX5E_TT_IPV4_IPSEC_AH);
	}

	if (tt_vec & BIT(MLX5E_TT_IPV6_IPSEC_AH)) {
		rule_p = &ai->ft_rule[MLX5E_TT_IPV6_IPSEC_AH];
		dest.tir_num = tirn[MLX5E_TT_IPV6_IPSEC_AH];
		MLX5_SET(fte_match_param, mv, outer_headers.ethertype,
			 ETHERTYPE_IPV6);
		*rule_p = mlx5_add_flow_rule(ft, mc_enable, mc, mv,
					     MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
					     MLX5_FS_ETH_FLOW_TAG, &dest);
		if (IS_ERR_OR_NULL(*rule_p))
			goto err_del_ai;
		ai->tt_vec |= BIT(MLX5E_TT_IPV6_IPSEC_AH);
	}

	MLX5_SET(fte_match_param, mv, outer_headers.ip_protocol, IPPROTO_ESP);

	if (tt_vec & BIT(MLX5E_TT_IPV4_IPSEC_ESP)) {
		rule_p = &ai->ft_rule[MLX5E_TT_IPV4_IPSEC_ESP];
		dest.tir_num = tirn[MLX5E_TT_IPV4_IPSEC_ESP];
		MLX5_SET(fte_match_param, mv, outer_headers.ethertype,
			 ETHERTYPE_IP);
		*rule_p = mlx5_add_flow_rule(ft, mc_enable, mc, mv,
					     MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
					     MLX5_FS_ETH_FLOW_TAG, &dest);
		if (IS_ERR_OR_NULL(*rule_p))
			goto err_del_ai;
		ai->tt_vec |= BIT(MLX5E_TT_IPV4_IPSEC_ESP);
	}

	if (tt_vec & BIT(MLX5E_TT_IPV6_IPSEC_ESP)) {
		rule_p = &ai->ft_rule[MLX5E_TT_IPV6_IPSEC_ESP];
		dest.tir_num = tirn[MLX5E_TT_IPV6_IPSEC_ESP];
		MLX5_SET(fte_match_param, mv, outer_headers.ethertype,
			 ETHERTYPE_IPV6);
		*rule_p = mlx5_add_flow_rule(ft, mc_enable, mc, mv,
					     MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
					     MLX5_FS_ETH_FLOW_TAG, &dest);
		if (IS_ERR_OR_NULL(*rule_p))
			goto err_del_ai;
		ai->tt_vec |= BIT(MLX5E_TT_IPV6_IPSEC_ESP);
	}

	return 0;

err_del_ai:
	err = PTR_ERR(*rule_p);
	*rule_p = NULL;
	mlx5e_del_eth_addr_from_flow_table(priv, ai);

	return err;
}

static int
mlx5e_add_eth_addr_rule(struct mlx5e_priv *priv,
    struct mlx5e_eth_addr_info *ai, int type)
{
	u32 *match_criteria;
	u32 *match_value;
	int err = 0;

	match_value	= mlx5_vzalloc(MLX5_ST_SZ_BYTES(fte_match_param));
	match_criteria	= mlx5_vzalloc(MLX5_ST_SZ_BYTES(fte_match_param));
	if (!match_value || !match_criteria) {
		if_printf(priv->ifp, "%s: alloc failed\n", __func__);
		err = -ENOMEM;
		goto add_eth_addr_rule_out;
	}
	err = mlx5e_add_eth_addr_rule_sub(priv, ai, type, match_criteria,
	    match_value);

add_eth_addr_rule_out:
	kvfree(match_criteria);
	kvfree(match_value);

	return (err);
}

static int mlx5e_vport_context_update_vlans(struct mlx5e_priv *priv)
{
	struct ifnet *ifp = priv->ifp;
	int max_list_size;
	int list_size;
	u16 *vlans;
	int vlan;
	int err;
	int i;

	list_size = 0;
	for_each_set_bit(vlan, priv->vlan.active_vlans, VLAN_N_VID)
		list_size++;

	max_list_size = 1 << MLX5_CAP_GEN(priv->mdev, log_max_vlan_list);

	if (list_size > max_list_size) {
		if_printf(ifp,
			    "ifnet vlans list size (%d) > (%d) max vport list size, some vlans will be dropped\n",
			    list_size, max_list_size);
		list_size = max_list_size;
	}

	vlans = kcalloc(list_size, sizeof(*vlans), GFP_KERNEL);
	if (!vlans)
		return -ENOMEM;

	i = 0;
	for_each_set_bit(vlan, priv->vlan.active_vlans, VLAN_N_VID) {
		if (i >= list_size)
			break;
		vlans[i++] = vlan;
	}

	err = mlx5_modify_nic_vport_vlans(priv->mdev, vlans, list_size);
	if (err)
		if_printf(ifp, "Failed to modify vport vlans list err(%d)\n",
			   err);

	kfree(vlans);
	return err;
}

enum mlx5e_vlan_rule_type {
	MLX5E_VLAN_RULE_TYPE_UNTAGGED,
	MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID,
	MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID,
	MLX5E_VLAN_RULE_TYPE_MATCH_VID,
};

static int
mlx5e_add_vlan_rule_sub(struct mlx5e_priv *priv,
    enum mlx5e_vlan_rule_type rule_type, u16 vid,
    u32 *mc, u32 *mv)
{
	struct mlx5_flow_table *ft = priv->fts.vlan.t;
	struct mlx5_flow_destination dest;
	u8 mc_enable = 0;
	struct mlx5_flow_rule **rule_p;
	int err = 0;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = priv->fts.main.t;

	mc_enable = MLX5_MATCH_OUTER_HEADERS;

	switch (rule_type) {
	case MLX5E_VLAN_RULE_TYPE_UNTAGGED:
		rule_p = &priv->vlan.untagged_ft_rule;
		MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.cvlan_tag);
		break;
	case MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID:
		rule_p = &priv->vlan.any_cvlan_ft_rule;
		MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.cvlan_tag);
		MLX5_SET(fte_match_param, mv, outer_headers.cvlan_tag, 1);
		break;
	case MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID:
		rule_p = &priv->vlan.any_svlan_ft_rule;
		MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.svlan_tag);
		MLX5_SET(fte_match_param, mv, outer_headers.svlan_tag, 1);
		break;
	default: /* MLX5E_VLAN_RULE_TYPE_MATCH_VID */
		rule_p = &priv->vlan.active_vlans_ft_rule[vid];
		MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.cvlan_tag);
		MLX5_SET(fte_match_param, mv, outer_headers.cvlan_tag, 1);
		MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.first_vid);
		MLX5_SET(fte_match_param, mv, outer_headers.first_vid, vid);
		mlx5e_vport_context_update_vlans(priv);
		break;
	}

	*rule_p = mlx5_add_flow_rule(ft, mc_enable, mc, mv,
				     MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
				     MLX5_FS_ETH_FLOW_TAG,
				     &dest);

	if (IS_ERR(*rule_p)) {
		err = PTR_ERR(*rule_p);
		*rule_p = NULL;
		if_printf(priv->ifp, "%s: add rule failed\n", __func__);
	}

	return (err);
}

static int
mlx5e_add_vlan_rule(struct mlx5e_priv *priv,
    enum mlx5e_vlan_rule_type rule_type, u16 vid)
{
	u32 *match_criteria;
	u32 *match_value;
	int err = 0;

	match_value	= mlx5_vzalloc(MLX5_ST_SZ_BYTES(fte_match_param));
	match_criteria	= mlx5_vzalloc(MLX5_ST_SZ_BYTES(fte_match_param));
	if (!match_value || !match_criteria) {
		if_printf(priv->ifp, "%s: alloc failed\n", __func__);
		err = -ENOMEM;
		goto add_vlan_rule_out;
	}

	err = mlx5e_add_vlan_rule_sub(priv, rule_type, vid, match_criteria,
				    match_value);

add_vlan_rule_out:
	kvfree(match_criteria);
	kvfree(match_value);

	return (err);
}

static void
mlx5e_del_vlan_rule(struct mlx5e_priv *priv,
    enum mlx5e_vlan_rule_type rule_type, u16 vid)
{
	switch (rule_type) {
	case MLX5E_VLAN_RULE_TYPE_UNTAGGED:
		if (priv->vlan.untagged_ft_rule) {
			mlx5_del_flow_rule(priv->vlan.untagged_ft_rule);
			priv->vlan.untagged_ft_rule = NULL;
		}
		break;
	case MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID:
		if (priv->vlan.any_cvlan_ft_rule) {
			mlx5_del_flow_rule(priv->vlan.any_cvlan_ft_rule);
			priv->vlan.any_cvlan_ft_rule = NULL;
		}
		break;
	case MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID:
		if (priv->vlan.any_svlan_ft_rule) {
			mlx5_del_flow_rule(priv->vlan.any_svlan_ft_rule);
			priv->vlan.any_svlan_ft_rule = NULL;
		}
		break;
	case MLX5E_VLAN_RULE_TYPE_MATCH_VID:
		if (priv->vlan.active_vlans_ft_rule[vid]) {
			mlx5_del_flow_rule(priv->vlan.active_vlans_ft_rule[vid]);
			priv->vlan.active_vlans_ft_rule[vid] = NULL;
		}
		mlx5e_vport_context_update_vlans(priv);
		break;
	default:
		break;
	}
}

static void
mlx5e_del_any_vid_rules(struct mlx5e_priv *priv)
{
	mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID, 0);
	mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID, 0);
}

static int
mlx5e_add_any_vid_rules(struct mlx5e_priv *priv)
{
	int err;

	err = mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID, 0);
	if (err)
		return (err);

	return (mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID, 0));
}

void
mlx5e_enable_vlan_filter(struct mlx5e_priv *priv)
{
	if (priv->vlan.filter_disabled) {
		priv->vlan.filter_disabled = false;
		if (priv->ifp->if_flags & IFF_PROMISC)
			return;
		if (test_bit(MLX5E_STATE_OPENED, &priv->state))
			mlx5e_del_any_vid_rules(priv);
	}
}

void
mlx5e_disable_vlan_filter(struct mlx5e_priv *priv)
{
	if (!priv->vlan.filter_disabled) {
		priv->vlan.filter_disabled = true;
		if (priv->ifp->if_flags & IFF_PROMISC)
			return;
		if (test_bit(MLX5E_STATE_OPENED, &priv->state))
			mlx5e_add_any_vid_rules(priv);
	}
}

void
mlx5e_vlan_rx_add_vid(void *arg, struct ifnet *ifp, u16 vid)
{
	struct mlx5e_priv *priv = arg;

	if (ifp != priv->ifp)
		return;

	PRIV_LOCK(priv);
	if (!test_and_set_bit(vid, priv->vlan.active_vlans) &&
	    test_bit(MLX5E_STATE_OPENED, &priv->state))
		mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_VID, vid);
	PRIV_UNLOCK(priv);
}

void
mlx5e_vlan_rx_kill_vid(void *arg, struct ifnet *ifp, u16 vid)
{
	struct mlx5e_priv *priv = arg;

	if (ifp != priv->ifp)
		return;

	PRIV_LOCK(priv);
	clear_bit(vid, priv->vlan.active_vlans);
	if (test_bit(MLX5E_STATE_OPENED, &priv->state))
		mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_VID, vid);
	PRIV_UNLOCK(priv);
}

int
mlx5e_add_all_vlan_rules(struct mlx5e_priv *priv)
{
	int err;
	int i;

	set_bit(0, priv->vlan.active_vlans);
	for_each_set_bit(i, priv->vlan.active_vlans, VLAN_N_VID) {
		err = mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_VID,
					  i);
		if (err)
			return (err);
	}

	err = mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_UNTAGGED, 0);
	if (err)
		return (err);

	if (priv->vlan.filter_disabled) {
		err = mlx5e_add_any_vid_rules(priv);
		if (err)
			return (err);
	}
	return (0);
}

void
mlx5e_del_all_vlan_rules(struct mlx5e_priv *priv)
{
	int i;

	if (priv->vlan.filter_disabled)
		mlx5e_del_any_vid_rules(priv);

	mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_UNTAGGED, 0);

	for_each_set_bit(i, priv->vlan.active_vlans, VLAN_N_VID)
		mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_VID, i);
	clear_bit(0, priv->vlan.active_vlans);
}

#define	mlx5e_for_each_hash_node(hn, tmp, hash, i) \
	for (i = 0; i < MLX5E_ETH_ADDR_HASH_SIZE; i++) \
		LIST_FOREACH_SAFE(hn, &(hash)[i], hlist, tmp)

static void
mlx5e_execute_action(struct mlx5e_priv *priv,
    struct mlx5e_eth_addr_hash_node *hn)
{
	switch (hn->action) {
	case MLX5E_ACTION_ADD:
		mlx5e_add_eth_addr_rule(priv, &hn->ai, MLX5E_FULLMATCH);
		hn->action = MLX5E_ACTION_NONE;
		break;

	case MLX5E_ACTION_DEL:
		mlx5e_del_eth_addr_from_flow_table(priv, &hn->ai);
		mlx5e_del_eth_addr_from_hash(hn);
		break;

	default:
		break;
	}
}

static void
mlx5e_sync_ifp_addr(struct mlx5e_priv *priv)
{
	struct ifnet *ifp = priv->ifp;
	struct ifaddr *ifa;
	struct ifmultiaddr *ifma;

	/* XXX adding this entry might not be needed */
	mlx5e_add_eth_addr_to_hash(priv->eth_addr.if_uc,
	    LLADDR((struct sockaddr_dl *)(ifp->if_addr->ifa_addr)));

	if_addr_rlock(ifp);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		mlx5e_add_eth_addr_to_hash(priv->eth_addr.if_uc,
		    LLADDR((struct sockaddr_dl *)ifa->ifa_addr));
	}
	if_addr_runlock(ifp);

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		mlx5e_add_eth_addr_to_hash(priv->eth_addr.if_mc,
		    LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
	}
	if_maddr_runlock(ifp);
}

static void mlx5e_fill_addr_array(struct mlx5e_priv *priv, int list_type,
				  u8 addr_array[][ETH_ALEN], int size)
{
	bool is_uc = (list_type == MLX5_NIC_VPORT_LIST_TYPE_UC);
	struct ifnet *ifp = priv->ifp;
	struct mlx5e_eth_addr_hash_node *hn;
	struct mlx5e_eth_addr_hash_head *addr_list;
	struct mlx5e_eth_addr_hash_node *tmp;
	int i = 0;
	int hi;

	addr_list = is_uc ? priv->eth_addr.if_uc : priv->eth_addr.if_mc;

	if (is_uc) /* Make sure our own address is pushed first */
		ether_addr_copy(addr_array[i++], IF_LLADDR(ifp));
	else if (priv->eth_addr.broadcast_enabled)
		ether_addr_copy(addr_array[i++], ifp->if_broadcastaddr);

	mlx5e_for_each_hash_node(hn, tmp, addr_list, hi) {
		if (ether_addr_equal(IF_LLADDR(ifp), hn->ai.addr))
			continue;
		if (i >= size)
			break;
		ether_addr_copy(addr_array[i++], hn->ai.addr);
	}
}

static void mlx5e_vport_context_update_addr_list(struct mlx5e_priv *priv,
						 int list_type)
{
	bool is_uc = (list_type == MLX5_NIC_VPORT_LIST_TYPE_UC);
	struct mlx5e_eth_addr_hash_node *hn;
	u8 (*addr_array)[ETH_ALEN] = NULL;
	struct mlx5e_eth_addr_hash_head *addr_list;
	struct mlx5e_eth_addr_hash_node *tmp;
	int max_size;
	int size;
	int err;
	int hi;

	size = is_uc ? 0 : (priv->eth_addr.broadcast_enabled ? 1 : 0);
	max_size = is_uc ?
		1 << MLX5_CAP_GEN(priv->mdev, log_max_current_uc_list) :
		1 << MLX5_CAP_GEN(priv->mdev, log_max_current_mc_list);

	addr_list = is_uc ? priv->eth_addr.if_uc : priv->eth_addr.if_mc;
	mlx5e_for_each_hash_node(hn, tmp, addr_list, hi)
		size++;

	if (size > max_size) {
		if_printf(priv->ifp,
			    "ifp %s list size (%d) > (%d) max vport list size, some addresses will be dropped\n",
			    is_uc ? "UC" : "MC", size, max_size);
		size = max_size;
	}

	if (size) {
		addr_array = kcalloc(size, ETH_ALEN, GFP_KERNEL);
		if (!addr_array) {
			err = -ENOMEM;
			goto out;
		}
		mlx5e_fill_addr_array(priv, list_type, addr_array, size);
	}

	err = mlx5_modify_nic_vport_mac_list(priv->mdev, list_type, addr_array, size);
out:
	if (err)
		if_printf(priv->ifp,
			   "Failed to modify vport %s list err(%d)\n",
			   is_uc ? "UC" : "MC", err);
	kfree(addr_array);
}

static void mlx5e_vport_context_update(struct mlx5e_priv *priv)
{
	struct mlx5e_eth_addr_db *ea = &priv->eth_addr;

	mlx5e_vport_context_update_addr_list(priv, MLX5_NIC_VPORT_LIST_TYPE_UC);
	mlx5e_vport_context_update_addr_list(priv, MLX5_NIC_VPORT_LIST_TYPE_MC);
	mlx5_modify_nic_vport_promisc(priv->mdev, 0,
				      ea->allmulti_enabled,
				      ea->promisc_enabled);
}

static void
mlx5e_apply_ifp_addr(struct mlx5e_priv *priv)
{
	struct mlx5e_eth_addr_hash_node *hn;
	struct mlx5e_eth_addr_hash_node *tmp;
	int i;

	mlx5e_for_each_hash_node(hn, tmp, priv->eth_addr.if_uc, i)
	    mlx5e_execute_action(priv, hn);

	mlx5e_for_each_hash_node(hn, tmp, priv->eth_addr.if_mc, i)
	    mlx5e_execute_action(priv, hn);
}

static void
mlx5e_handle_ifp_addr(struct mlx5e_priv *priv)
{
	struct mlx5e_eth_addr_hash_node *hn;
	struct mlx5e_eth_addr_hash_node *tmp;
	int i;

	mlx5e_for_each_hash_node(hn, tmp, priv->eth_addr.if_uc, i)
	    hn->action = MLX5E_ACTION_DEL;
	mlx5e_for_each_hash_node(hn, tmp, priv->eth_addr.if_mc, i)
	    hn->action = MLX5E_ACTION_DEL;

	if (test_bit(MLX5E_STATE_OPENED, &priv->state))
		mlx5e_sync_ifp_addr(priv);

	mlx5e_apply_ifp_addr(priv);
}

void
mlx5e_set_rx_mode_core(struct mlx5e_priv *priv)
{
	struct mlx5e_eth_addr_db *ea = &priv->eth_addr;
	struct ifnet *ndev = priv->ifp;

	bool rx_mode_enable = test_bit(MLX5E_STATE_OPENED, &priv->state);
	bool promisc_enabled = rx_mode_enable && (ndev->if_flags & IFF_PROMISC);
	bool allmulti_enabled = rx_mode_enable && (ndev->if_flags & IFF_ALLMULTI);
	bool broadcast_enabled = rx_mode_enable;

	bool enable_promisc = !ea->promisc_enabled && promisc_enabled;
	bool disable_promisc = ea->promisc_enabled && !promisc_enabled;
	bool enable_allmulti = !ea->allmulti_enabled && allmulti_enabled;
	bool disable_allmulti = ea->allmulti_enabled && !allmulti_enabled;
	bool enable_broadcast = !ea->broadcast_enabled && broadcast_enabled;
	bool disable_broadcast = ea->broadcast_enabled && !broadcast_enabled;

	/* update broadcast address */
	ether_addr_copy(priv->eth_addr.broadcast.addr,
	    priv->ifp->if_broadcastaddr);

	if (enable_promisc) {
		mlx5e_add_eth_addr_rule(priv, &ea->promisc, MLX5E_PROMISC);
		if (!priv->vlan.filter_disabled)
			mlx5e_add_any_vid_rules(priv);
	}
	if (enable_allmulti)
		mlx5e_add_eth_addr_rule(priv, &ea->allmulti, MLX5E_ALLMULTI);
	if (enable_broadcast)
		mlx5e_add_eth_addr_rule(priv, &ea->broadcast, MLX5E_FULLMATCH);

	mlx5e_handle_ifp_addr(priv);

	if (disable_broadcast)
		mlx5e_del_eth_addr_from_flow_table(priv, &ea->broadcast);
	if (disable_allmulti)
		mlx5e_del_eth_addr_from_flow_table(priv, &ea->allmulti);
	if (disable_promisc) {
		if (!priv->vlan.filter_disabled)
			mlx5e_del_any_vid_rules(priv);
		mlx5e_del_eth_addr_from_flow_table(priv, &ea->promisc);
	}

	ea->promisc_enabled = promisc_enabled;
	ea->allmulti_enabled = allmulti_enabled;
	ea->broadcast_enabled = broadcast_enabled;

	mlx5e_vport_context_update(priv);
}

void
mlx5e_set_rx_mode_work(struct work_struct *work)
{
	struct mlx5e_priv *priv =
	    container_of(work, struct mlx5e_priv, set_rx_mode_work);

	PRIV_LOCK(priv);
	if (test_bit(MLX5E_STATE_OPENED, &priv->state))
		mlx5e_set_rx_mode_core(priv);
	PRIV_UNLOCK(priv);
}

static void
mlx5e_destroy_groups(struct mlx5e_flow_table *ft)
{
	int i;

	for (i = ft->num_groups - 1; i >= 0; i--) {
		if (!IS_ERR_OR_NULL(ft->g[i]))
			mlx5_destroy_flow_group(ft->g[i]);
		ft->g[i] = NULL;
	}
	ft->num_groups = 0;
}

static void
mlx5e_destroy_flow_table(struct mlx5e_flow_table *ft)
{
	mlx5e_destroy_groups(ft);
	kfree(ft->g);
	mlx5_destroy_flow_table(ft->t);
	ft->t = NULL;
}

#define MLX5E_NUM_MAIN_GROUPS	10
#define MLX5E_MAIN_GROUP0_SIZE	BIT(4)
#define MLX5E_MAIN_GROUP1_SIZE	BIT(3)
#define MLX5E_MAIN_GROUP2_SIZE	BIT(1)
#define MLX5E_MAIN_GROUP3_SIZE	BIT(0)
#define MLX5E_MAIN_GROUP4_SIZE	BIT(14)
#define MLX5E_MAIN_GROUP5_SIZE	BIT(13)
#define MLX5E_MAIN_GROUP6_SIZE	BIT(11)
#define MLX5E_MAIN_GROUP7_SIZE	BIT(2)
#define MLX5E_MAIN_GROUP8_SIZE	BIT(1)
#define MLX5E_MAIN_GROUP9_SIZE	BIT(0)
#define MLX5E_MAIN_TABLE_SIZE	(MLX5E_MAIN_GROUP0_SIZE +\
				 MLX5E_MAIN_GROUP1_SIZE +\
				 MLX5E_MAIN_GROUP2_SIZE +\
				 MLX5E_MAIN_GROUP3_SIZE +\
				 MLX5E_MAIN_GROUP4_SIZE +\
				 MLX5E_MAIN_GROUP5_SIZE +\
				 MLX5E_MAIN_GROUP6_SIZE +\
				 MLX5E_MAIN_GROUP7_SIZE +\
				 MLX5E_MAIN_GROUP8_SIZE +\
				 MLX5E_MAIN_GROUP9_SIZE +\
				 0)

static int
mlx5e_create_main_groups_sub(struct mlx5e_flow_table *ft, u32 *in,
				      int inlen)
{
	u8 *mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	u8 *dmac = MLX5_ADDR_OF(create_flow_group_in, in,
				match_criteria.outer_headers.dmac_47_16);
	int err;
	int ix = 0;

	/* Tunnel rules need to be first in this list of groups */

	/* Start tunnel rules */
	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ethertype);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ip_protocol);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.udp_dport);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_MAIN_GROUP0_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;
	/* End Tunnel Rules */

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ethertype);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ip_protocol);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_MAIN_GROUP1_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ethertype);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_MAIN_GROUP2_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_MAIN_GROUP3_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ethertype);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ip_protocol);
	memset(dmac, 0xff, ETH_ALEN);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_MAIN_GROUP4_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ethertype);
	memset(dmac, 0xff, ETH_ALEN);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_MAIN_GROUP5_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	memset(dmac, 0xff, ETH_ALEN);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_MAIN_GROUP6_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ethertype);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ip_protocol);
	dmac[0] = 0x01;
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_MAIN_GROUP7_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ethertype);
	dmac[0] = 0x01;
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_MAIN_GROUP8_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	dmac[0] = 0x01;
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_MAIN_GROUP9_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	return (0);

err_destory_groups:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
	mlx5e_destroy_groups(ft);

	return (err);
}

static int
mlx5e_create_main_groups(struct mlx5e_flow_table *ft)
{
	u32 *in;
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	int err;

	in = mlx5_vzalloc(inlen);
	if (!in)
		return (-ENOMEM);

	err = mlx5e_create_main_groups_sub(ft, in, inlen);

	kvfree(in);
	return (err);
}

static int mlx5e_create_main_flow_table(struct mlx5e_priv *priv)
{
	struct mlx5e_flow_table *ft = &priv->fts.main;
	int err;

	ft->num_groups = 0;
	ft->t = mlx5_create_flow_table(priv->fts.ns, 0, "main",
				       MLX5E_MAIN_TABLE_SIZE);

	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return (err);
	}
	ft->g = kcalloc(MLX5E_NUM_MAIN_GROUPS, sizeof(*ft->g), GFP_KERNEL);
	if (!ft->g) {
		err = -ENOMEM;
		goto err_destroy_main_flow_table;
	}

	err = mlx5e_create_main_groups(ft);
	if (err)
		goto err_free_g;
	return (0);

err_free_g:
	kfree(ft->g);

err_destroy_main_flow_table:
	mlx5_destroy_flow_table(ft->t);
	ft->t = NULL;

	return (err);
}

static void mlx5e_destroy_main_flow_table(struct mlx5e_priv *priv)
{
	mlx5e_destroy_flow_table(&priv->fts.main);
}

#define MLX5E_NUM_VLAN_GROUPS	3
#define MLX5E_VLAN_GROUP0_SIZE	BIT(12)
#define MLX5E_VLAN_GROUP1_SIZE	BIT(1)
#define MLX5E_VLAN_GROUP2_SIZE	BIT(0)
#define MLX5E_VLAN_TABLE_SIZE	(MLX5E_VLAN_GROUP0_SIZE +\
				 MLX5E_VLAN_GROUP1_SIZE +\
				 MLX5E_VLAN_GROUP2_SIZE +\
				 0)

static int
mlx5e_create_vlan_groups_sub(struct mlx5e_flow_table *ft, u32 *in,
				      int inlen)
{
	int err;
	int ix = 0;
	u8 *mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.cvlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.first_vid);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_VLAN_GROUP0_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.cvlan_tag);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_VLAN_GROUP1_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.svlan_tag);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_VLAN_GROUP2_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	return (0);

err_destory_groups:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
	mlx5e_destroy_groups(ft);

	return (err);
}

static int
mlx5e_create_vlan_groups(struct mlx5e_flow_table *ft)
{
	u32 *in;
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	int err;

	in = mlx5_vzalloc(inlen);
	if (!in)
		return (-ENOMEM);

	err = mlx5e_create_vlan_groups_sub(ft, in, inlen);

	kvfree(in);
	return (err);
}

static int
mlx5e_create_vlan_flow_table(struct mlx5e_priv *priv)
{
	struct mlx5e_flow_table *ft = &priv->fts.vlan;
	int err;

	ft->num_groups = 0;
	ft->t = mlx5_create_flow_table(priv->fts.ns, 0, "vlan",
				       MLX5E_VLAN_TABLE_SIZE);

	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return (err);
	}
	ft->g = kcalloc(MLX5E_NUM_VLAN_GROUPS, sizeof(*ft->g), GFP_KERNEL);
	if (!ft->g) {
		err = -ENOMEM;
		goto err_destroy_vlan_flow_table;
	}

	err = mlx5e_create_vlan_groups(ft);
	if (err)
		goto err_free_g;

	return (0);

err_free_g:
	kfree(ft->g);

err_destroy_vlan_flow_table:
	mlx5_destroy_flow_table(ft->t);
	ft->t = NULL;

	return (err);
}

static void
mlx5e_destroy_vlan_flow_table(struct mlx5e_priv *priv)
{
	mlx5e_destroy_flow_table(&priv->fts.vlan);
}

#define MLX5E_NUM_INNER_RSS_GROUPS	3
#define MLX5E_INNER_RSS_GROUP0_SIZE	BIT(3)
#define MLX5E_INNER_RSS_GROUP1_SIZE	BIT(1)
#define MLX5E_INNER_RSS_GROUP2_SIZE	BIT(0)
#define MLX5E_INNER_RSS_TABLE_SIZE	(MLX5E_INNER_RSS_GROUP0_SIZE +\
					 MLX5E_INNER_RSS_GROUP1_SIZE +\
					 MLX5E_INNER_RSS_GROUP2_SIZE +\
					 0)

static int
mlx5e_create_inner_rss_groups_sub(struct mlx5e_flow_table *ft, u32 *in,
					   int inlen)
{
	u8 *mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	int err;
	int ix = 0;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_INNER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, inner_headers.ethertype);
	MLX5_SET_TO_ONES(fte_match_param, mc, inner_headers.ip_protocol);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_INNER_RSS_GROUP0_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_INNER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, inner_headers.ethertype);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_INNER_RSS_GROUP1_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_INNER_RSS_GROUP2_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destory_groups;
	ft->num_groups++;

	return (0);

err_destory_groups:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
	mlx5e_destroy_groups(ft);

	return (err);
}

static int
mlx5e_create_inner_rss_groups(struct mlx5e_flow_table *ft)
{
	u32 *in;
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	int err;

	in = mlx5_vzalloc(inlen);
	if (!in)
		return (-ENOMEM);

	err = mlx5e_create_inner_rss_groups_sub(ft, in, inlen);

	kvfree(in);
	return (err);
}

static int
mlx5e_create_inner_rss_flow_table(struct mlx5e_priv *priv)
{
	struct mlx5e_flow_table *ft = &priv->fts.inner_rss;
	int err;

	ft->num_groups = 0;
	ft->t = mlx5_create_flow_table(priv->fts.ns, 0, "inner_rss",
				       MLX5E_INNER_RSS_TABLE_SIZE);

	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return (err);
	}
	ft->g = kcalloc(MLX5E_NUM_INNER_RSS_GROUPS, sizeof(*ft->g),
			GFP_KERNEL);
	if (!ft->g) {
		err = -ENOMEM;
		goto err_destroy_inner_rss_flow_table;
	}

	err = mlx5e_create_inner_rss_groups(ft);
	if (err)
		goto err_free_g;

	return (0);

err_free_g:
	kfree(ft->g);

err_destroy_inner_rss_flow_table:
	mlx5_destroy_flow_table(ft->t);
	ft->t = NULL;

	return (err);
}

static void mlx5e_destroy_inner_rss_flow_table(struct mlx5e_priv *priv)
{
	mlx5e_destroy_flow_table(&priv->fts.inner_rss);
}

int
mlx5e_open_flow_table(struct mlx5e_priv *priv)
{
	int err;

	priv->fts.ns = mlx5_get_flow_namespace(priv->mdev,
					       MLX5_FLOW_NAMESPACE_KERNEL);

	err = mlx5e_create_vlan_flow_table(priv);
	if (err)
		return (err);

	err = mlx5e_create_main_flow_table(priv);
	if (err)
		goto err_destroy_vlan_flow_table;

	err = mlx5e_create_inner_rss_flow_table(priv);
	if (err)
		goto err_destroy_main_flow_table;

	return (0);

err_destroy_main_flow_table:
	mlx5e_destroy_main_flow_table(priv);
err_destroy_vlan_flow_table:
	mlx5e_destroy_vlan_flow_table(priv);

	return (err);
}

void
mlx5e_close_flow_table(struct mlx5e_priv *priv)
{
	mlx5e_destroy_inner_rss_flow_table(priv);
	mlx5e_destroy_main_flow_table(priv);
	mlx5e_destroy_vlan_flow_table(priv);
}
