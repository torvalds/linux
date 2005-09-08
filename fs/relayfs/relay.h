#ifndef _RELAY_H
#define _RELAY_H

struct dentry *relayfs_create_file(const char *name,
				   struct dentry *parent,
				   int mode,
				   struct rchan *chan);
extern int relayfs_remove(struct dentry *dentry);
extern int relay_buf_empty(struct rchan_buf *buf);
extern void relay_destroy_channel(struct kref *kref);

#endif /* _RELAY_H */
