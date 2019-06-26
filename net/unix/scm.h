#ifndef NET_UNIX_SCM_H
#define NET_UNIX_SCM_H

extern struct list_head gc_inflight_list;
extern spinlock_t unix_gc_lock;

int unix_attach_fds(struct scm_cookie *scm, struct sk_buff *skb);
void unix_detach_fds(struct scm_cookie *scm, struct sk_buff *skb);

#endif
