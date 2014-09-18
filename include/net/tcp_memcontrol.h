#ifndef _TCP_MEMCG_H
#define _TCP_MEMCG_H

struct cg_proto *tcp_proto_cgroup(struct mem_cgroup *memcg);
int tcp_init_cgroup(struct mem_cgroup *memcg, struct cgroup_subsys *ss);
void tcp_destroy_cgroup(struct mem_cgroup *memcg);
#endif /* _TCP_MEMCG_H */
