#include "ceph_debug.h"

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "super.h"
#include "mds_client.h"
#include "mon_client.h"
#include "auth.h"

#ifdef CONFIG_DEBUG_FS

/*
 * Implement /sys/kernel/debug/ceph fun
 *
 * /sys/kernel/debug/ceph/client*  - an instance of the ceph client
 *      .../osdmap      - current osdmap
 *      .../mdsmap      - current mdsmap
 *      .../monmap      - current monmap
 *      .../osdc        - active osd requests
 *      .../mdsc        - active mds requests
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
			   pr_addr(&inst->addr.in_addr));
	}
	return 0;
}

static int mdsmap_show(struct seq_file *s, void *p)
{
	int i;
	struct ceph_client *client = s->private;

	if (client->mdsc.mdsmap == NULL)
		return 0;
	seq_printf(s, "epoch %d\n", client->mdsc.mdsmap->m_epoch);
	seq_printf(s, "root %d\n", client->mdsc.mdsmap->m_root);
	seq_printf(s, "session_timeout %d\n",
		       client->mdsc.mdsmap->m_session_timeout);
	seq_printf(s, "session_autoclose %d\n",
		       client->mdsc.mdsmap->m_session_autoclose);
	for (i = 0; i < client->mdsc.mdsmap->m_max_mds; i++) {
		struct ceph_entity_addr *addr =
			&client->mdsc.mdsmap->m_info[i].addr;
		int state = client->mdsc.mdsmap->m_info[i].state;

		seq_printf(s, "\tmds%d\t%s\t(%s)\n", i, pr_addr(&addr->in_addr),
			       ceph_mds_state_name(state));
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
		seq_printf(s, "pg_pool %d pg_num %d / %d, lpg_num %d / %d\n",
			   pool->id, pool->v.pg_num, pool->pg_num_mask,
			   pool->v.lpg_num, pool->lpg_num_mask);
	}
	for (i = 0; i < client->osdc.osdmap->max_osd; i++) {
		struct ceph_entity_addr *addr =
			&client->osdc.osdmap->osd_addr[i];
		int state = client->osdc.osdmap->osd_state[i];
		char sb[64];

		seq_printf(s, "\tosd%d\t%s\t%3d%%\t(%s)\n",
			   i, pr_addr(&addr->in_addr),
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
		seq_printf(s, "have mdsmap %u\n", (unsigned)monc->have_mdsmap);
	if (monc->have_osdmap)
		seq_printf(s, "have osdmap %u\n", (unsigned)monc->have_osdmap);
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

static int mdsc_show(struct seq_file *s, void *p)
{
	struct ceph_client *client = s->private;
	struct ceph_mds_client *mdsc = &client->mdsc;
	struct ceph_mds_request *req;
	struct rb_node *rp;
	int pathlen;
	u64 pathbase;
	char *path;

	mutex_lock(&mdsc->mutex);
	for (rp = rb_first(&mdsc->request_tree); rp; rp = rb_next(rp)) {
		req = rb_entry(rp, struct ceph_mds_request, r_node);

		if (req->r_request)
			seq_printf(s, "%lld\tmds%d\t", req->r_tid, req->r_mds);
		else
			seq_printf(s, "%lld\t(no request)\t", req->r_tid);

		seq_printf(s, "%s", ceph_mds_op_name(req->r_op));

		if (req->r_got_unsafe)
			seq_printf(s, "\t(unsafe)");
		else
			seq_printf(s, "\t");

		if (req->r_inode) {
			seq_printf(s, " #%llx", ceph_ino(req->r_inode));
		} else if (req->r_dentry) {
			path = ceph_mdsc_build_path(req->r_dentry, &pathlen,
						    &pathbase, 0);
			if (IS_ERR(path))
				path = NULL;
			spin_lock(&req->r_dentry->d_lock);
			seq_printf(s, " #%llx/%.*s (%s)",
				   ceph_ino(req->r_dentry->d_parent->d_inode),
				   req->r_dentry->d_name.len,
				   req->r_dentry->d_name.name,
				   path ? path : "");
			spin_unlock(&req->r_dentry->d_lock);
			kfree(path);
		} else if (req->r_path1) {
			seq_printf(s, " #%llx/%s", req->r_ino1.ino,
				   req->r_path1);
		}

		if (req->r_old_dentry) {
			path = ceph_mdsc_build_path(req->r_old_dentry, &pathlen,
						    &pathbase, 0);
			if (IS_ERR(path))
				path = NULL;
			spin_lock(&req->r_old_dentry->d_lock);
			seq_printf(s, " #%llx/%.*s (%s)",
			   ceph_ino(req->r_old_dentry->d_parent->d_inode),
				   req->r_old_dentry->d_name.len,
				   req->r_old_dentry->d_name.name,
				   path ? path : "");
			spin_unlock(&req->r_old_dentry->d_lock);
			kfree(path);
		} else if (req->r_path2) {
			if (req->r_ino2.ino)
				seq_printf(s, " #%llx/%s", req->r_ino2.ino,
					   req->r_path2);
			else
				seq_printf(s, " %s", req->r_path2);
		}

		seq_printf(s, "\n");
	}
	mutex_unlock(&mdsc->mutex);

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
		struct ceph_osd_request_head *head;
		struct ceph_osd_op *op;
		int num_ops;
		int opcode, olen;
		int i;

		req = rb_entry(p, struct ceph_osd_request, r_node);

		seq_printf(s, "%lld\tosd%d\t%d.%x\t", req->r_tid,
			   req->r_osd ? req->r_osd->o_osd : -1,
			   le32_to_cpu(req->r_pgid.pool),
			   le16_to_cpu(req->r_pgid.ps));

		head = req->r_request->front.iov_base;
		op = (void *)(head + 1);

		num_ops = le16_to_cpu(head->num_ops);
		olen = le32_to_cpu(head->object_len);
		seq_printf(s, "%.*s", olen,
			   (const char *)(head->ops + num_ops));

		if (req->r_reassert_version.epoch)
			seq_printf(s, "\t%u'%llu",
			   (unsigned)le32_to_cpu(req->r_reassert_version.epoch),
			   le64_to_cpu(req->r_reassert_version.version));
		else
			seq_printf(s, "\t");

		for (i = 0; i < num_ops; i++) {
			opcode = le16_to_cpu(op->op);
			seq_printf(s, "\t%s", ceph_osd_op_name(opcode));
			op++;
		}

		seq_printf(s, "\n");
	}
	mutex_unlock(&osdc->request_mutex);
	return 0;
}

static int caps_show(struct seq_file *s, void *p)
{
	struct ceph_client *client = s->private;
	int total, avail, used, reserved, min;

	ceph_reservation_status(client, &total, &avail, &used, &reserved, &min);
	seq_printf(s, "total\t\t%d\n"
		   "avail\t\t%d\n"
		   "used\t\t%d\n"
		   "reserved\t%d\n"
		   "min\t%d\n",
		   total, avail, used, reserved, min);
	return 0;
}

static int dentry_lru_show(struct seq_file *s, void *ptr)
{
	struct ceph_client *client = s->private;
	struct ceph_mds_client *mdsc = &client->mdsc;
	struct ceph_dentry_info *di;

	spin_lock(&mdsc->dentry_lru_lock);
	list_for_each_entry(di, &mdsc->dentry_lru, lru) {
		struct dentry *dentry = di->dentry;
		seq_printf(s, "%p %p\t%.*s\n",
			   di, dentry, dentry->d_name.len, dentry->d_name.name);
	}
	spin_unlock(&mdsc->dentry_lru_lock);

	return 0;
}

#define DEFINE_SHOW_FUNC(name)						\
static int name##_open(struct inode *inode, struct file *file)		\
{									\
	struct seq_file *sf;						\
	int ret;							\
									\
	ret = single_open(file, name, NULL);				\
	sf = file->private_data;					\
	sf->private = inode->i_private;					\
	return ret;							\
}									\
									\
static const struct file_operations name##_fops = {			\
	.open		= name##_open,					\
	.read		= seq_read,					\
	.llseek		= seq_lseek,					\
	.release	= single_release,				\
};

DEFINE_SHOW_FUNC(monmap_show)
DEFINE_SHOW_FUNC(mdsmap_show)
DEFINE_SHOW_FUNC(osdmap_show)
DEFINE_SHOW_FUNC(monc_show)
DEFINE_SHOW_FUNC(mdsc_show)
DEFINE_SHOW_FUNC(osdc_show)
DEFINE_SHOW_FUNC(dentry_lru_show)
DEFINE_SHOW_FUNC(caps_show)

static int congestion_kb_set(void *data, u64 val)
{
	struct ceph_client *client = (struct ceph_client *)data;

	if (client)
		client->mount_args->congestion_kb = (int)val;

	return 0;
}

static int congestion_kb_get(void *data, u64 *val)
{
	struct ceph_client *client = (struct ceph_client *)data;

	if (client)
		*val = (u64)client->mount_args->congestion_kb;

	return 0;
}


DEFINE_SIMPLE_ATTRIBUTE(congestion_kb_fops, congestion_kb_get,
			congestion_kb_set, "%llu\n");

int __init ceph_debugfs_init(void)
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
	int ret = 0;
	char name[80];

	snprintf(name, sizeof(name), "%pU.client%lld", &client->fsid,
		 client->monc.auth->global_id);

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

	client->mdsc.debugfs_file = debugfs_create_file("mdsc",
						      0600,
						      client->debugfs_dir,
						      client,
						      &mdsc_show_fops);
	if (!client->mdsc.debugfs_file)
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

	client->debugfs_mdsmap = debugfs_create_file("mdsmap",
					0600,
					client->debugfs_dir,
					client,
					&mdsmap_show_fops);
	if (!client->debugfs_mdsmap)
		goto out;

	client->debugfs_osdmap = debugfs_create_file("osdmap",
					0600,
					client->debugfs_dir,
					client,
					&osdmap_show_fops);
	if (!client->debugfs_osdmap)
		goto out;

	client->debugfs_dentry_lru = debugfs_create_file("dentry_lru",
					0600,
					client->debugfs_dir,
					client,
					&dentry_lru_show_fops);
	if (!client->debugfs_dentry_lru)
		goto out;

	client->debugfs_caps = debugfs_create_file("caps",
						   0400,
						   client->debugfs_dir,
						   client,
						   &caps_show_fops);
	if (!client->debugfs_caps)
		goto out;

	client->debugfs_congestion_kb =
		debugfs_create_file("writeback_congestion_kb",
				    0600,
				    client->debugfs_dir,
				    client,
				    &congestion_kb_fops);
	if (!client->debugfs_congestion_kb)
		goto out;

	sprintf(name, "../../bdi/%s", dev_name(client->sb->s_bdi->dev));
	client->debugfs_bdi = debugfs_create_symlink("bdi", client->debugfs_dir,
						     name);

	return 0;

out:
	ceph_debugfs_client_cleanup(client);
	return ret;
}

void ceph_debugfs_client_cleanup(struct ceph_client *client)
{
	debugfs_remove(client->debugfs_bdi);
	debugfs_remove(client->debugfs_caps);
	debugfs_remove(client->debugfs_dentry_lru);
	debugfs_remove(client->debugfs_osdmap);
	debugfs_remove(client->debugfs_mdsmap);
	debugfs_remove(client->debugfs_monmap);
	debugfs_remove(client->osdc.debugfs_file);
	debugfs_remove(client->mdsc.debugfs_file);
	debugfs_remove(client->monc.debugfs_file);
	debugfs_remove(client->debugfs_congestion_kb);
	debugfs_remove(client->debugfs_dir);
}

#else  /* CONFIG_DEBUG_FS */

int __init ceph_debugfs_init(void)
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
