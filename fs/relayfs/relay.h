#ifndef _RELAY_H
#define _RELAY_H

extern int relayfs_remove(struct dentry *dentry);
extern int relay_buf_empty(struct rchan_buf *buf);
extern void relay_destroy_channel(struct kref *kref);

#endif /* _RELAY_H */
