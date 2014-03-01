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
	struct rb_node *n;

	if (client->osdc.osdmap == NULL)
		return 0;
	seq_printf(s, "epoch %d\n", client->osdc.osdmap->epoch);
	seq_printf(s, "flags%s%s\n",
		   (client->osdc.osdmap->flags & CEPH_OSDMAP_NEARFULL) ?
		   " NEARFULL" : "",
		   (client->osdc.osdmap->flags & CEPH_OSDMAP_FULL) ?
		   " FULL" : "");
	for (n = rb_first(&client->osdc.osdmap->pg_pools); n; n = rb_next(n)) {
		struct ceph_pg_pool_info *pool =
			rb_entry(n, struct ceph_pg_pool_info, node);
		seq_printf(s, "pg_pool %llu pg_num %d / %d\n",
			   (unsigned long long)pool->id, pool->pg_num,
			   pool->pg_num_mask);
	}
	for (i = 0; i < client->osdc.osdmap->max_osd; i++) {
		struct ceph_entity_addr *addr =
			&client->osdc.osdmap->osd_addr[i];
		int state = client->osdc.osdmap->osd_state[i];
		char sb[64];

		seq_printf(s, "\tosd%d\t%s\t%3d%%\t(%s)\n",
			   i, ceph_pr_addr(&addr->in_addr),
			   ((client->osdc.osdmap->osd_weight[i]*100) >> 16),
			   ceph_osdmap_state_str(sb, sizeof(sb), state));
	}
	return 0;
}

static int monc_show(struct seq_file *s, void *p)
{
	struct ceph_client *client = s->private;
	struct ceph_mon_generic_request *req;
	struct ceph_mon_client *monc = &client->monc;
	struct rb_node *rp;

	mutex_lock(&monc->mutex);

	if (monc->have_mdsmap)
		seq_printf(s, "have mdsmap %u\n", (unsigned int)monc->have_mdsmap);
	if (monc->have_osdmap)
		seq_printf(s, "have osdmap %u\n", (unsigned int)monc->have_osdmap);
	if (monc->want_next_osdmap)
		seq_printf(s, "want next osdmap\n");

	for (rp = rb_first(&monc->generic_request_tree); rp; rp = rb_next(rp)) {
		__u16 op;
		req = rb_entry(rp, struct ceph_mon_generic_request, node);
		op = le16_to_cpu(req->request->hdr.type);
		if (op == CEPH_MSG_STATFS)
			seq_printf(s, "%lld statfs\n", req->tid);
		else
			seq_printf(s, "%lld unknown\n", req->tid);
	}

	mutex_unlock(&monc->mutex);
	return 0;
}

static int osdc_show(struct seq_file *s, void *pp)
{
	struct ceph_client *client = s->private;
	struct ceph_osd_client *osdc = &client->osdc;
	struct rb_node *p;

	mutex_lock(&osdc->request_mutex);
	for (p = rb_first(&osdc->requests); p; p = rb_next(p)) {
		struct ceph_osd_request *req;
		unsigned int i;
		int opcode;

		req = rb_entry(p, struct ceph_osd_request, r_node);

		seq_printf(s, "%lld\tosd%d\t%lld.%x\t", req->r_tid,
			   req->r_osd ? req->r_osd->o_osd : -1,
			   req->r_pgid.pool, req->r_pgid.seed);

		seq_printf(s, "%.*s", req->r_base_oid.name_len,
			   req->r_base_oid.name);

		if (req->r_reassert_version.epoch)
			seq_printf(s, "\t%u'%llu",
			   (unsigned int)le32_to_cpu(req->r_reassert_version.epoch),
			   le64_to_cpu(req->r_reassert_version.version));
		else
			seq_printf(s, "\t");

		for (i = 0; i < req->r_num_ops; i++) {
			opcode = req->r_ops[i].op;
			seq_printf(s, "\t%s", ceph_osd_op_name(opcode));
		}

		seq_printf(s, "\n");
	}
	mutex_unlock(&osdc->request_mutex);
	return 0;
}

CEPH_DEFINE_SHOW_FUNC(monmap_show)
CEPH_DEFINE_SHOW_FUNC(osdmap_show)
CEPH_DEFINE_SHOW_FUNC(monc_show)
CEPH_DEFINE_SHOW_FUNC(osdc_show)

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

	return 0;

out:
	ceph_debugfs_client_cleanup(client);
	return ret;
}

void ceph_debugfs_client_cleanup(struct ceph_client *client)
{
	dout("ceph_debugfs_client_cleanup %p\n", client);
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
