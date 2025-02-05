// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DSA devlink handling
 */

#include <net/dsa.h>
#include <net/devlink.h>

#include "devlink.h"

static int dsa_devlink_info_get(struct devlink *dl,
				struct devlink_info_req *req,
				struct netlink_ext_ack *extack)
{
	struct dsa_switch *ds = dsa_devlink_to_ds(dl);

	if (ds->ops->devlink_info_get)
		return ds->ops->devlink_info_get(ds, req, extack);

	return -EOPNOTSUPP;
}

static int dsa_devlink_sb_pool_get(struct devlink *dl,
				   unsigned int sb_index, u16 pool_index,
				   struct devlink_sb_pool_info *pool_info)
{
	struct dsa_switch *ds = dsa_devlink_to_ds(dl);

	if (!ds->ops->devlink_sb_pool_get)
		return -EOPNOTSUPP;

	return ds->ops->devlink_sb_pool_get(ds, sb_index, pool_index,
					    pool_info);
}

static int dsa_devlink_sb_pool_set(struct devlink *dl, unsigned int sb_index,
				   u16 pool_index, u32 size,
				   enum devlink_sb_threshold_type threshold_type,
				   struct netlink_ext_ack *extack)
{
	struct dsa_switch *ds = dsa_devlink_to_ds(dl);

	if (!ds->ops->devlink_sb_pool_set)
		return -EOPNOTSUPP;

	return ds->ops->devlink_sb_pool_set(ds, sb_index, pool_index, size,
					    threshold_type, extack);
}

static int dsa_devlink_sb_port_pool_get(struct devlink_port *dlp,
					unsigned int sb_index, u16 pool_index,
					u32 *p_threshold)
{
	struct dsa_switch *ds = dsa_devlink_port_to_ds(dlp);
	int port = dsa_devlink_port_to_port(dlp);

	if (!ds->ops->devlink_sb_port_pool_get)
		return -EOPNOTSUPP;

	return ds->ops->devlink_sb_port_pool_get(ds, port, sb_index,
						 pool_index, p_threshold);
}

static int dsa_devlink_sb_port_pool_set(struct devlink_port *dlp,
					unsigned int sb_index, u16 pool_index,
					u32 threshold,
					struct netlink_ext_ack *extack)
{
	struct dsa_switch *ds = dsa_devlink_port_to_ds(dlp);
	int port = dsa_devlink_port_to_port(dlp);

	if (!ds->ops->devlink_sb_port_pool_set)
		return -EOPNOTSUPP;

	return ds->ops->devlink_sb_port_pool_set(ds, port, sb_index,
						 pool_index, threshold, extack);
}

static int
dsa_devlink_sb_tc_pool_bind_get(struct devlink_port *dlp,
				unsigned int sb_index, u16 tc_index,
				enum devlink_sb_pool_type pool_type,
				u16 *p_pool_index, u32 *p_threshold)
{
	struct dsa_switch *ds = dsa_devlink_port_to_ds(dlp);
	int port = dsa_devlink_port_to_port(dlp);

	if (!ds->ops->devlink_sb_tc_pool_bind_get)
		return -EOPNOTSUPP;

	return ds->ops->devlink_sb_tc_pool_bind_get(ds, port, sb_index,
						    tc_index, pool_type,
						    p_pool_index, p_threshold);
}

static int
dsa_devlink_sb_tc_pool_bind_set(struct devlink_port *dlp,
				unsigned int sb_index, u16 tc_index,
				enum devlink_sb_pool_type pool_type,
				u16 pool_index, u32 threshold,
				struct netlink_ext_ack *extack)
{
	struct dsa_switch *ds = dsa_devlink_port_to_ds(dlp);
	int port = dsa_devlink_port_to_port(dlp);

	if (!ds->ops->devlink_sb_tc_pool_bind_set)
		return -EOPNOTSUPP;

	return ds->ops->devlink_sb_tc_pool_bind_set(ds, port, sb_index,
						    tc_index, pool_type,
						    pool_index, threshold,
						    extack);
}

static int dsa_devlink_sb_occ_snapshot(struct devlink *dl,
				       unsigned int sb_index)
{
	struct dsa_switch *ds = dsa_devlink_to_ds(dl);

	if (!ds->ops->devlink_sb_occ_snapshot)
		return -EOPNOTSUPP;

	return ds->ops->devlink_sb_occ_snapshot(ds, sb_index);
}

static int dsa_devlink_sb_occ_max_clear(struct devlink *dl,
					unsigned int sb_index)
{
	struct dsa_switch *ds = dsa_devlink_to_ds(dl);

	if (!ds->ops->devlink_sb_occ_max_clear)
		return -EOPNOTSUPP;

	return ds->ops->devlink_sb_occ_max_clear(ds, sb_index);
}

static int dsa_devlink_sb_occ_port_pool_get(struct devlink_port *dlp,
					    unsigned int sb_index,
					    u16 pool_index, u32 *p_cur,
					    u32 *p_max)
{
	struct dsa_switch *ds = dsa_devlink_port_to_ds(dlp);
	int port = dsa_devlink_port_to_port(dlp);

	if (!ds->ops->devlink_sb_occ_port_pool_get)
		return -EOPNOTSUPP;

	return ds->ops->devlink_sb_occ_port_pool_get(ds, port, sb_index,
						     pool_index, p_cur, p_max);
}

static int
dsa_devlink_sb_occ_tc_port_bind_get(struct devlink_port *dlp,
				    unsigned int sb_index, u16 tc_index,
				    enum devlink_sb_pool_type pool_type,
				    u32 *p_cur, u32 *p_max)
{
	struct dsa_switch *ds = dsa_devlink_port_to_ds(dlp);
	int port = dsa_devlink_port_to_port(dlp);

	if (!ds->ops->devlink_sb_occ_tc_port_bind_get)
		return -EOPNOTSUPP;

	return ds->ops->devlink_sb_occ_tc_port_bind_get(ds, port,
							sb_index, tc_index,
							pool_type, p_cur,
							p_max);
}

static const struct devlink_ops dsa_devlink_ops = {
	.info_get			= dsa_devlink_info_get,
	.sb_pool_get			= dsa_devlink_sb_pool_get,
	.sb_pool_set			= dsa_devlink_sb_pool_set,
	.sb_port_pool_get		= dsa_devlink_sb_port_pool_get,
	.sb_port_pool_set		= dsa_devlink_sb_port_pool_set,
	.sb_tc_pool_bind_get		= dsa_devlink_sb_tc_pool_bind_get,
	.sb_tc_pool_bind_set		= dsa_devlink_sb_tc_pool_bind_set,
	.sb_occ_snapshot		= dsa_devlink_sb_occ_snapshot,
	.sb_occ_max_clear		= dsa_devlink_sb_occ_max_clear,
	.sb_occ_port_pool_get		= dsa_devlink_sb_occ_port_pool_get,
	.sb_occ_tc_port_bind_get	= dsa_devlink_sb_occ_tc_port_bind_get,
};

int dsa_devlink_param_get(struct devlink *dl, u32 id,
			  struct devlink_param_gset_ctx *ctx)
{
	struct dsa_switch *ds = dsa_devlink_to_ds(dl);

	if (!ds->ops->devlink_param_get)
		return -EOPNOTSUPP;

	return ds->ops->devlink_param_get(ds, id, ctx);
}
EXPORT_SYMBOL_GPL(dsa_devlink_param_get);

int dsa_devlink_param_set(struct devlink *dl, u32 id,
			  struct devlink_param_gset_ctx *ctx,
			  struct netlink_ext_ack *extack)
{
	struct dsa_switch *ds = dsa_devlink_to_ds(dl);

	if (!ds->ops->devlink_param_set)
		return -EOPNOTSUPP;

	return ds->ops->devlink_param_set(ds, id, ctx);
}
EXPORT_SYMBOL_GPL(dsa_devlink_param_set);

int dsa_devlink_params_register(struct dsa_switch *ds,
				const struct devlink_param *params,
				size_t params_count)
{
	return devlink_params_register(ds->devlink, params, params_count);
}
EXPORT_SYMBOL_GPL(dsa_devlink_params_register);

void dsa_devlink_params_unregister(struct dsa_switch *ds,
				   const struct devlink_param *params,
				   size_t params_count)
{
	devlink_params_unregister(ds->devlink, params, params_count);
}
EXPORT_SYMBOL_GPL(dsa_devlink_params_unregister);

int dsa_devlink_resource_register(struct dsa_switch *ds,
				  const char *resource_name,
				  u64 resource_size,
				  u64 resource_id,
				  u64 parent_resource_id,
				  const struct devlink_resource_size_params *size_params)
{
	int ret;

	devl_lock(ds->devlink);
	ret = devl_resource_register(ds->devlink, resource_name, resource_size,
				     resource_id, parent_resource_id,
				     size_params);
	devl_unlock(ds->devlink);

	return ret;
}
EXPORT_SYMBOL_GPL(dsa_devlink_resource_register);

void dsa_devlink_resources_unregister(struct dsa_switch *ds)
{
	devlink_resources_unregister(ds->devlink);
}
EXPORT_SYMBOL_GPL(dsa_devlink_resources_unregister);

void dsa_devlink_resource_occ_get_register(struct dsa_switch *ds,
					   u64 resource_id,
					   devlink_resource_occ_get_t *occ_get,
					   void *occ_get_priv)
{
	devl_lock(ds->devlink);
	devl_resource_occ_get_register(ds->devlink, resource_id, occ_get,
				       occ_get_priv);
	devl_unlock(ds->devlink);
}
EXPORT_SYMBOL_GPL(dsa_devlink_resource_occ_get_register);

void dsa_devlink_resource_occ_get_unregister(struct dsa_switch *ds,
					     u64 resource_id)
{
	devl_lock(ds->devlink);
	devl_resource_occ_get_unregister(ds->devlink, resource_id);
	devl_unlock(ds->devlink);
}
EXPORT_SYMBOL_GPL(dsa_devlink_resource_occ_get_unregister);

struct devlink_region *
dsa_devlink_region_create(struct dsa_switch *ds,
			  const struct devlink_region_ops *ops,
			  u32 region_max_snapshots, u64 region_size)
{
	return devlink_region_create(ds->devlink, ops, region_max_snapshots,
				     region_size);
}
EXPORT_SYMBOL_GPL(dsa_devlink_region_create);

struct devlink_region *
dsa_devlink_port_region_create(struct dsa_switch *ds,
			       int port,
			       const struct devlink_port_region_ops *ops,
			       u32 region_max_snapshots, u64 region_size)
{
	struct dsa_port *dp = dsa_to_port(ds, port);

	return devlink_port_region_create(&dp->devlink_port, ops,
					  region_max_snapshots,
					  region_size);
}
EXPORT_SYMBOL_GPL(dsa_devlink_port_region_create);

void dsa_devlink_region_destroy(struct devlink_region *region)
{
	devlink_region_destroy(region);
}
EXPORT_SYMBOL_GPL(dsa_devlink_region_destroy);

int dsa_port_devlink_setup(struct dsa_port *dp)
{
	struct devlink_port *dlp = &dp->devlink_port;
	struct dsa_switch_tree *dst = dp->ds->dst;
	struct devlink_port_attrs attrs = {};
	struct devlink *dl = dp->ds->devlink;
	struct dsa_switch *ds = dp->ds;
	const unsigned char *id;
	unsigned char len;
	int err;

	memset(dlp, 0, sizeof(*dlp));
	devlink_port_init(dl, dlp);

	if (ds->ops->port_setup) {
		err = ds->ops->port_setup(ds, dp->index);
		if (err)
			return err;
	}

	id = (const unsigned char *)&dst->index;
	len = sizeof(dst->index);

	attrs.phys.port_number = dp->index;
	memcpy(attrs.switch_id.id, id, len);
	attrs.switch_id.id_len = len;

	switch (dp->type) {
	case DSA_PORT_TYPE_UNUSED:
		attrs.flavour = DEVLINK_PORT_FLAVOUR_UNUSED;
		break;
	case DSA_PORT_TYPE_CPU:
		attrs.flavour = DEVLINK_PORT_FLAVOUR_CPU;
		break;
	case DSA_PORT_TYPE_DSA:
		attrs.flavour = DEVLINK_PORT_FLAVOUR_DSA;
		break;
	case DSA_PORT_TYPE_USER:
		attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
		break;
	}

	devlink_port_attrs_set(dlp, &attrs);
	err = devlink_port_register(dl, dlp, dp->index);
	if (err) {
		if (ds->ops->port_teardown)
			ds->ops->port_teardown(ds, dp->index);
		return err;
	}

	return 0;
}

void dsa_port_devlink_teardown(struct dsa_port *dp)
{
	struct devlink_port *dlp = &dp->devlink_port;
	struct dsa_switch *ds = dp->ds;

	devlink_port_unregister(dlp);

	if (ds->ops->port_teardown)
		ds->ops->port_teardown(ds, dp->index);

	devlink_port_fini(dlp);
}

void dsa_switch_devlink_register(struct dsa_switch *ds)
{
	devlink_register(ds->devlink);
}

void dsa_switch_devlink_unregister(struct dsa_switch *ds)
{
	devlink_unregister(ds->devlink);
}

int dsa_switch_devlink_alloc(struct dsa_switch *ds)
{
	struct dsa_devlink_priv *dl_priv;
	struct devlink *dl;

	/* Add the switch to devlink before calling setup, so that setup can
	 * add dpipe tables
	 */
	dl = devlink_alloc(&dsa_devlink_ops, sizeof(*dl_priv), ds->dev);
	if (!dl)
		return -ENOMEM;

	ds->devlink = dl;

	dl_priv = devlink_priv(ds->devlink);
	dl_priv->ds = ds;

	return 0;
}

void dsa_switch_devlink_free(struct dsa_switch *ds)
{
	devlink_free(ds->devlink);
	ds->devlink = NULL;
}
