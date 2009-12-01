#include <linux/rbtree.h>
#include <linux/list.h>
#include <unistd.h>
#include "symbol.h"

struct thread {
	struct rb_node		rb_node;
	struct list_head	maps;
	pid_t			pid;
	char			shortname[3];
	char			*comm;
};

int thread__set_comm(struct thread *self, const char *comm);
struct thread *
threads__findnew(pid_t pid, struct rb_root *threads, struct thread **last_match);
struct thread *
register_idle_thread(struct rb_root *threads, struct thread **last_match);
void thread__insert_map(struct thread *self, struct map *map);
int thread__fork(struct thread *self, struct thread *parent);
struct map *thread__find_map(struct thread *self, u64 ip);
size_t threads__fprintf(FILE *fp, struct rb_root *threads);
