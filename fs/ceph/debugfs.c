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

#include "super.h"

#ifdef CONFIG_DEBUG_FS

#include "mds_client.h"

static int mdsmap_show(struct seq_file *s, void *p)
{
	int i;
	struct ceph_fs_client *fsc = s->private;

	if (fsc->mdsc == NULL || fsc->mdsc->mdsmap == NULL)
		return 0;
	seq_printf(s, "epoch %d\n", fsc->mdsc->mdsmap->m_epoch);
	seq_printf(s, "root %d\n", fsc->mdsc->mdsmap->m_root);
	seq_printf(s, "session_timeout %d\n",
		       fsc->mdsc->mdsmap->m_session_timeout);
	seq_printf(s, "session_autoclose %d\n",
		       fsc->mdsc->mdsmap->m_session_autoclose);
	for (i = 0; i < fsc->mdsc->mdsmap->m_max_mds; i++) {
		struct ceph_entity_addr *addr =
			&fsc->mdsc->mdsmap->m_info[i].addr;
		int state = fsc->mdsc->mdsmap->m_info[i].state;

		seq_printf(s, "\tmds%d\t%s\t(%s)\n", i,
			       ceph_pr_addr(&addr->in_addr),
			       ceph_mds_state_name(state));
	}
	return 0;
}

/*
 * mdsc debugfs
 */
static int mdsc_show(struct seq_file *s, void *p)
{
	struct ceph_fs_client *fsc = s->private;
	struct ceph_mds_client *mdsc = fsc->mdsc;
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

static int caps_show(struct seq_file *s, void *p)
{
	struct ceph_fs_client *fsc = s->private;
	int total, avail, used, reserved, min;

	ceph_reservation_status(fsc, &total, &avail, &used, &reserved, &min);
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
	struct ceph_fs_client *fsc = s->private;
	struct ceph_mds_client *mdsc = fsc->mdsc;
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

CEPH_DEFINE_SHOW_FUNC(mdsmap_show)
CEPH_DEFINE_SHOW_FUNC(mdsc_show)
CEPH_DEFINE_SHOW_FUNC(caps_show)
CEPH_DEFINE_SHOW_FUNC(dentry_lru_show)


/*
 * debugfs
 */
static int congestion_kb_set(void *data, u64 val)
{
	struct ceph_fs_client *fsc = (struct ceph_fs_client *)data;

	fsc->mount_options->congestion_kb = (int)val;
	return 0;
}

static int congestion_kb_get(void *data, u64 *val)
{
	struct ceph_fs_client *fsc = (struct ceph_fs_client *)data;

	*val = (u64)fsc->mount_options->congestion_kb;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(congestion_kb_fops, congestion_kb_get,
			congestion_kb_set, "%llu\n");


void ceph_fs_debugfs_cleanup(struct ceph_fs_client *fsc)
{
	dout("ceph_fs_debugfs_cleanup\n");
	debugfs_remove(fsc->debugfs_bdi);
	debugfs_remove(fsc->debugfs_congestion_kb);
	debugfs_remove(fsc->debugfs_mdsmap);
	debugfs_remove(fsc->debugfs_caps);
	debugfs_remove(fsc->debugfs_mdsc);
	debugfs_remove(fsc->debugfs_dentry_lru);
}

int ceph_fs_debugfs_init(struct ceph_fs_client *fsc)
{
	char name[100];
	int err = -ENOMEM;

	dout("ceph_fs_debugfs_init\n");
	fsc->debugfs_congestion_kb =
		debugfs_create_file("writeback_congestion_kb",
				    0600,
				    fsc->client->debugfs_dir,
				    fsc,
				    &congestion_kb_fops);
	if (!fsc->debugfs_congestion_kb)
		goto out;

	dout("a\n");

	snprintf(name, sizeof(name), "../../bdi/%s",
		 dev_name(fsc->backing_dev_info.dev));
	fsc->debugfs_bdi =
		debugfs_create_symlink("bdi",
				       fsc->client->debugfs_dir,
				       name);
	if (!fsc->debugfs_bdi)
		goto out;

	dout("b\n");
	fsc->debugfs_mdsmap = debugfs_create_file("mdsmap",
					0600,
					fsc->client->debugfs_dir,
					fsc,
					&mdsmap_show_fops);
	if (!fsc->debugfs_mdsmap)
		goto out;

	dout("ca\n");
	fsc->debugfs_mdsc = debugfs_create_file("mdsc",
						0600,
						fsc->client->debugfs_dir,
						fsc,
						&mdsc_show_fops);
	if (!fsc->debugfs_mdsc)
		goto out;

	dout("da\n");
	fsc->debugfs_caps = debugfs_create_file("caps",
						   0400,
						   fsc->client->debugfs_dir,
						   fsc,
						   &caps_show_fops);
	if (!fsc->debugfs_caps)
		goto out;

	dout("ea\n");
	fsc->debugfs_dentry_lru = debugfs_create_file("dentry_lru",
					0600,
					fsc->client->debugfs_dir,
					fsc,
					&dentry_lru_show_fops);
	if (!fsc->debugfs_dentry_lru)
		goto out;

	return 0;

out:
	ceph_fs_debugfs_cleanup(fsc);
	return err;
}


#else  /* CONFIG_DEBUG_FS */

int ceph_fs_debugfs_init(struct ceph_fs_client *fsc)
{
	return 0;
}

void ceph_fs_debugfs_cleanup(struct ceph_fs_client *fsc)
{
}

#endif  /* CONFIG_DEBUG_FS */
