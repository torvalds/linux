#include <linux/ceph/ceph_debug.h>

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <linux/ceph/libceph.h>
#include <linux/ceph/mon_client.h>
#include <linux/ceph/auth.h>
#include <linux/ceph/debugfs.h>

#ifdef CONFIG_DEBUG_FS

/*
 * Implement /sys/kernel/debug/ceph fun
 *
 * /sys/kernel/debug/ceph/client*  - an instance of the ceph client
 *      .../osdmap      - current osdmap
 *      .../monmap      - current monmap
 *      .../osdc        - active osd requests
 *      .../monc        - mon client state
 *      .../client_options - libceph-only (i.e. not rbd or cephfs) options
 *      .../dentry_lru  - dump contents of dentry lru
 *      .../caps        - expose cap (reservation) stats
 *      .../bdi         - symlink to ../../bdi/something
 */

static struct dentry *ceph_debugfs_dir;

static int monmap_show(struct seq_file *s, void *p)
{
	int i;
	struct ceph_client *client = s->private;

	if (client->monc.monmap == NULL)
		return 0;

	seq_printf(s, "epoch %d\n", client->monc.monmap->epoch);
	for (i = 0; i < client->monc.monmap->num_mon; i++) {
		struct ceph_entity_inst *inst =
			&client->monc.monmap->mon_inst[i];

		seq_printf(s, "\t%s%lld\t%s\n",
			   ENTITY_NAME(inst->name),
			   ceph_pr_addr(&inst->addr.in_addr));
	}
	return 0;
}

static int osdmap_show(struct seq_file *s, void *p)
{
	int i;
	struct ceph_client *client = s->private;
	struct ceph_osdmap *map = client->osdc.osdmap;
	struct rb_node *n;

	if (map == NULL)
		return 0;

	seq_printf(s, "epoch %d\n", map->epoch);
	seq_printf(s, "flags%s%s\n",
		   (map->flags & CEPH_OSDMAP_NEARFULL) ?  " NEARFULL" : "",
		   (map->flags & CEPH_OSDMAP_FULL) ?  " FULL" : "");

	for (n = rb_first(&map->pg_pools); n; n = rb_next(n)) {
		struct ceph_pg_pool_info *pi =
			rb_entry(n, struct ceph_pg_pool_info, node);

		seq_printf(s, "pool %lld '%s' type %d size %d min_size %d pg_num %u pg_num_mask %d flags 0x%llx lfor %u read_tier %lld write_tier %lld\n",
			   pi->id, pi->name, pi->type, pi->size, pi->min_size,
			   pi->pg_num, pi->pg_num_mask, pi->flags,
			   pi->last_force_request_resend, pi->read_tier,
			   pi->write_tier);
	}
	for (i = 0; i < map->max_osd; i++) {
		struct ceph_entity_addr *addr = &map->osd_addr[i];
		int state = map->osd_state[i];
		char sb[64];

		seq_printf(s, "osd%d\t%s\t%3d%%\t(%s)\t%3d%%\n",
			   i, ceph_pr_addr(&addr->in_addr),
			   ((map->osd_weight[i]*100) >> 16),
			   ceph_osdmap_state_str(sb, sizeof(sb), state),
			   ((ceph_get_primary_affinity(map, i)*100) >> 16));
	}
	for (n = rb_first(&map->pg_temp); n; n = rb_next(n)) {
		struct ceph_pg_mapping *pg =
			rb_entry(n, struct ceph_pg_mapping, node);

		seq_printf(s, "pg_temp %llu.%x [", pg->pgid.pool,
			   pg->pgid.seed);
		for (i = 0; i < pg->pg_temp.len; i++)
			seq_printf(s, "%s%d", (i == 0 ? "" : ","),
				   pg->pg_temp.osds[i]);
		seq_printf(s, "]\n");
	}
	for (n = rb_first(&map->primary_temp); n; n = rb_next(n)) {
		struct ceph_pg_mapping *pg =
			rb_entry(n, struct ceph_pg_mapping, node);

		seq_printf(s, "primary_temp %llu.%x %d\n", pg->pgid.pool,
			   pg->pgid.seed, pg->primary_temp.osd);
	}

	return 0;
}

static int monc_show(struct seq_file *s, void *p)
{
	struct ceph_client *client = s->private;
	struct ceph_mon_generic_request *req;
	struct ceph_mon_client *monc = &client->monc;
	struct rb_node *rp;
	int i;

	mutex_lock(&monc->mutex);

	for (i = 0; i < ARRAY_SIZE(monc->subs); i++) {
		seq_printf(s, "have %s %u", ceph_sub_str[i],
			   monc->subs[i].have);
		if (monc->subs[i].want)
			seq_printf(s, " want %llu%s",
				   le64_to_cpu(monc->subs[i].item.start),
				   (monc->subs[i].item.flags &
					CEPH_SUBSCRIBE_ONETIME ?  "" : "+"));
		seq_putc(s, '\n');
	}

	for (rp = rb_first(&monc->generic_request_tree); rp; rp = rb_next(rp)) {
		__u16 op;
		req = rb_entry(rp, struct ceph_mon_generic_request, node);
		op = le16_to_cpu(req->request->hdr.type);
		if (op == CEPH_MSG_STATFS)
			seq_printf(s, "%llu statfs\n", req->tid);
		else if (op == CEPH_MSG_MON_GET_VERSION)
			seq_printf(s, "%llu mon_get_version", req->tid);
		else
			seq_printf(s, "%llu unknown\n", req->tid);
	}

	mutex_unlock(&monc->mutex);
	return 0;
}

static void dump_target(struct seq_file *s, struct ceph_osd_request_target *t)
{
	int i;

	seq_printf(s, "osd%d\t%llu.%x\t[", t->osd, t->pgid.pool, t->pgid.seed);
	for (i = 0; i < t->up.size; i++)
		seq_printf(s, "%s%d", (!i ? "" : ","), t->up.osds[i]);
	seq_printf(s, "]/%d\t[", t->up.primary);
	for (i = 0; i < t->acting.size; i++)
		seq_printf(s, "%s%d", (!i ? "" : ","), t->acting.osds[i]);
	seq_printf(s, "]/%d\t%*pE\t0x%x", t->acting.primary,
		   t->target_oid.name_len, t->target_oid.name, t->flags);
	if (t->paused)
		seq_puts(s, "\tP");
}

static void dump_request(struct seq_file *s, struct ceph_osd_request *req)
{
	int i;

	seq_printf(s, "%llu\t", req->r_tid);
	dump_target(s, &req->r_t);

	seq_printf(s, "\t%d\t%u'%llu", req->r_attempts,
		   le32_to_cpu(req->r_replay_version.epoch),
		   le64_to_cpu(req->r_replay_version.version));

	for (i = 0; i < req->r_num_ops; i++) {
		struct ceph_osd_req_op *op = &req->r_ops[i];

		seq_printf(s, "%s%s", (i == 0 ? "\t" : ","),
			   ceph_osd_op_name(op->op));
		if (op->op == CEPH_OSD_OP_WATCH)
			seq_printf(s, "-%s",
				   ceph_osd_watch_op_name(op->watch.op));
	}

	seq_putc(s, '\n');
}

static void dump_requests(struct seq_file *s, struct ceph_osd *osd)
{
	struct rb_node *n;

	mutex_lock(&osd->lock);
	for (n = rb_first(&osd->o_requests); n; n = rb_next(n)) {
		struct ceph_osd_request *req =
		    rb_entry(n, struct ceph_osd_request, r_node);

		dump_request(s, req);
	}

	mutex_unlock(&osd->lock);
}

static void dump_linger_request(struct seq_file *s,
				struct ceph_osd_linger_request *lreq)
{
	seq_printf(s, "%llu\t", lreq->linger_id);
	dump_target(s, &lreq->t);

	seq_printf(s, "\t%u\t%s%s/%d\n", lreq->register_gen,
		   lreq->is_watch ? "W" : "N", lreq->committed ? "C" : "",
		   lreq->last_error);
}

static void dump_linger_requests(struct seq_file *s, struct ceph_osd *osd)
{
	struct rb_node *n;

	mutex_lock(&osd->lock);
	for (n = rb_first(&osd->o_linger_requests); n; n = rb_next(n)) {
		struct ceph_osd_linger_request *lreq =
		    rb_entry(n, struct ceph_osd_linger_request, node);

		dump_linger_request(s, lreq);
	}

	mutex_unlock(&osd->lock);
}

static int osdc_show(struct seq_file *s, void *pp)
{
	struct ceph_client *client = s->private;
	struct ceph_osd_client *osdc = &client->osdc;
	struct rb_node *n;

	down_read(&osdc->lock);
	seq_printf(s, "REQUESTS %d homeless %d\n",
		   atomic_read(&osdc->num_requests),
		   atomic_read(&osdc->num_homeless));
	for (n = rb_first(&osdc->osds); n; n = rb_next(n)) {
		struct ceph_osd *osd = rb_entry(n, struct ceph_osd, o_node);

		dump_requests(s, osd);
	}
	dump_requests(s, &osdc->homeless_osd);

	seq_puts(s, "LINGER REQUESTS\n");
	for (n = rb_first(&osdc->osds); n; n = rb_next(n)) {
		struct ceph_osd *osd = rb_entry(n, struct ceph_osd, o_node);

		dump_linger_requests(s, osd);
	}
	dump_linger_requests(s, &osdc->homeless_osd);

	up_read(&osdc->lock);
	return 0;
}

static int client_options_show(struct seq_file *s, void *p)
{
	struct ceph_client *client = s->private;
	int ret;

	ret = ceph_print_client_options(s, client);
	if (ret)
		return ret;

	seq_putc(s, '\n');
	return 0;
}

CEPH_DEFINE_SHOW_FUNC(monmap_show)
CEPH_DEFINE_SHOW_FUNC(osdmap_show)
CEPH_DEFINE_SHOW_FUNC(monc_show)
CEPH_DEFINE_SHOW_FUNC(osdc_show)
CEPH_DEFINE_SHOW_FUNC(client_options_show)

int ceph_debugfs_init(void)
{
	ceph_debugfs_dir = debugfs_create_dir("ceph", NULL);
	if (!ceph_debugfs_dir)
		return -ENOMEM;
	return 0;
}

void ceph_debugfs_cleanup(void)
{
	debugfs_remove(ceph_debugfs_dir);
}

int ceph_debugfs_client_init(struct ceph_client *client)
{
	int ret = -ENOMEM;
	char name[80];

	snprintf(name, sizeof(name), "%pU.client%lld", &client->fsid,
		 client->monc.auth->global_id);

	dout("ceph_debugfs_client_init %p %s\n", client, name);

	BUG_ON(client->debugfs_dir);
	client->debugfs_dir = debugfs_create_dir(name, ceph_debugfs_dir);
	if (!client->debugfs_dir)
		goto out;

	client->monc.debugfs_file = debugfs_create_file("monc",
						      0600,
						      client->debugfs_dir,
						      client,
						      &monc_show_fops);
	if (!client->monc.debugfs_file)
		goto out;

	client->osdc.debugfs_file = debugfs_create_file("osdc",
						      0600,
						      client->debugfs_dir,
						      client,
						      &osdc_show_fops);
	if (!client->osdc.debugfs_file)
		goto out;

	client->debugfs_monmap = debugfs_create_file("monmap",
					0600,
					client->debugfs_dir,
					client,
					&monmap_show_fops);
	if (!client->debugfs_monmap)
		goto out;

	client->debugfs_osdmap = debugfs_create_file("osdmap",
					0600,
					client->debugfs_dir,
					client,
					&osdmap_show_fops);
	if (!client->debugfs_osdmap)
		goto out;

	client->debugfs_options = debugfs_create_file("client_options",
					0600,
					client->debugfs_dir,
					client,
					&client_options_show_fops);
	if (!client->debugfs_options)
		goto out;

	return 0;

out:
	ceph_debugfs_client_cleanup(client);
	return ret;
}

void ceph_debugfs_client_cleanup(struct ceph_client *client)
{
	dout("ceph_debugfs_client_cleanup %p\n", client);
	debugfs_remove(client->debugfs_options);
	debugfs_remove(client->debugfs_osdmap);
	debugfs_remove(client->debugfs_monmap);
	debugfs_remove(client->osdc.debugfs_file);
	debugfs_remove(client->monc.debugfs_file);
	debugfs_remove(client->debugfs_dir);
}

#else  /* CONFIG_DEBUG_FS */

int ceph_debugfs_init(void)
{
	return 0;
}

void ceph_debugfs_cleanup(void)
{
}

int ceph_debugfs_client_init(struct ceph_client *client)
{
	return 0;
}

void ceph_debugfs_client_cleanup(struct ceph_client *client)
{
}

#endif  /* CONFIG_DEBUG_FS */

EXPORT_SYMBOL(ceph_debugfs_init);
EXPORT_SYMBOL(ceph_debugfs_cleanup);
