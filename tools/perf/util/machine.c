#include "machine.h"
#include "map.h"
#include "thread.h"
#include <stdbool.h>

static struct thread *__machine__findnew_thread(struct machine *machine, pid_t pid,
						bool create)
{
	struct rb_node **p = &machine->threads.rb_node;
	struct rb_node *parent = NULL;
	struct thread *th;

	/*
	 * Font-end cache - PID lookups come in blocks,
	 * so most of the time we dont have to look up
	 * the full rbtree:
	 */
	if (machine->last_match && machine->last_match->pid == pid)
		return machine->last_match;

	while (*p != NULL) {
		parent = *p;
		th = rb_entry(parent, struct thread, rb_node);

		if (th->pid == pid) {
			machine->last_match = th;
			return th;
		}

		if (pid < th->pid)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	if (!create)
		return NULL;

	th = thread__new(pid);
	if (th != NULL) {
		rb_link_node(&th->rb_node, parent, p);
		rb_insert_color(&th->rb_node, &machine->threads);
		machine->last_match = th;
	}

	return th;
}

struct thread *machine__findnew_thread(struct machine *machine, pid_t pid)
{
	return __machine__findnew_thread(machine, pid, true);
}

struct thread *machine__find_thread(struct machine *machine, pid_t pid)
{
	return __machine__findnew_thread(machine, pid, false);
}
