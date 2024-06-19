// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NET3:	Garbage Collector For AF_UNIX sockets
 *
 * Garbage Collector:
 *	Copyright (C) Barak A. Pearlmutter.
 *
 * Chopped about by Alan Cox 22/3/96 to make it fit the AF_UNIX socket problem.
 * If it doesn't work blame me, it worked when Barak sent it.
 *
 * Assumptions:
 *
 *  - object w/ a bit
 *  - free list
 *
 * Current optimizations:
 *
 *  - explicit stack instead of recursion
 *  - tail recurse on first born instead of immediate push/pop
 *  - we gather the stuff that should not be killed into tree
 *    and stack is just a path from root to the current pointer.
 *
 *  Future optimizations:
 *
 *  - don't just push entire root set; process in place
 *
 *  Fixes:
 *	Alan Cox	07 Sept	1997	Vmalloc internal stack as needed.
 *					Cope with changing max_files.
 *	Al Viro		11 Oct 1998
 *		Graph may have cycles. That is, we can send the descriptor
 *		of foo to bar and vice versa. Current code chokes on that.
 *		Fix: move SCM_RIGHTS ones into the separate list and then
 *		skb_free() them all instead of doing explicit fput's.
 *		Another problem: since fput() may block somebody may
 *		create a new unix_socket when we are in the middle of sweep
 *		phase. Fix: revert the logic wrt MARKED. Mark everything
 *		upon the beginning and unmark non-junk ones.
 *
 *		[12 Oct 1998] AAARGH! New code purges all SCM_RIGHTS
 *		sent to connect()'ed but still not accept()'ed sockets.
 *		Fixed. Old code had slightly different problem here:
 *		extra fput() in situation when we passed the descriptor via
 *		such socket and closed it (descriptor). That would happen on
 *		each unix_gc() until the accept(). Since the struct file in
 *		question would go to the free list and might be reused...
 *		That might be the reason of random oopses on filp_close()
 *		in unrelated processes.
 *
 *	AV		28 Feb 1999
 *		Kill the explicit allocation of stack. Now we keep the tree
 *		with root in dummy + pointer (gc_current) to one of the nodes.
 *		Stack is represented as path from gc_current to dummy. Unmark
 *		now means "add to tree". Push == "make it a son of gc_current".
 *		Pop == "move gc_current to parent". We keep only pointers to
 *		parents (->gc_tree).
 *	AV		1 Mar 1999
 *		Damn. Added missing check for ->dead in listen queues scanning.
 *
 *	Miklos Szeredi 25 Jun 2007
 *		Reimplement with a cycle collecting algorithm. This should
 *		solve several problems with the previous code, like being racy
 *		wrt receive and holding up unrelated socket operations.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <linux/net.h>
#include <linux/fs.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/file.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/wait.h>

#include <net/sock.h>
#include <net/af_unix.h>
#include <net/scm.h>
#include <net/tcp_states.h>

struct unix_sock *unix_get_socket(struct file *filp)
{
	struct inode *inode = file_inode(filp);

	/* Socket ? */
	if (S_ISSOCK(inode->i_mode) && !(filp->f_mode & FMODE_PATH)) {
		struct socket *sock = SOCKET_I(inode);
		const struct proto_ops *ops;
		struct sock *sk = sock->sk;

		ops = READ_ONCE(sock->ops);

		/* PF_UNIX ? */
		if (sk && ops && ops->family == PF_UNIX)
			return unix_sk(sk);
	}

	return NULL;
}

static struct unix_vertex *unix_edge_successor(struct unix_edge *edge)
{
	/* If an embryo socket has a fd,
	 * the listener indirectly holds the fd's refcnt.
	 */
	if (edge->successor->listener)
		return unix_sk(edge->successor->listener)->vertex;

	return edge->successor->vertex;
}

static bool unix_graph_maybe_cyclic;
static bool unix_graph_grouped;

static void unix_update_graph(struct unix_vertex *vertex)
{
	/* If the receiver socket is not inflight, no cyclic
	 * reference could be formed.
	 */
	if (!vertex)
		return;

	unix_graph_maybe_cyclic = true;
	unix_graph_grouped = false;
}

static LIST_HEAD(unix_unvisited_vertices);

enum unix_vertex_index {
	UNIX_VERTEX_INDEX_MARK1,
	UNIX_VERTEX_INDEX_MARK2,
	UNIX_VERTEX_INDEX_START,
};

static unsigned long unix_vertex_unvisited_index = UNIX_VERTEX_INDEX_MARK1;

static void unix_add_edge(struct scm_fp_list *fpl, struct unix_edge *edge)
{
	struct unix_vertex *vertex = edge->predecessor->vertex;

	if (!vertex) {
		vertex = list_first_entry(&fpl->vertices, typeof(*vertex), entry);
		vertex->index = unix_vertex_unvisited_index;
		vertex->out_degree = 0;
		INIT_LIST_HEAD(&vertex->edges);
		INIT_LIST_HEAD(&vertex->scc_entry);

		list_move_tail(&vertex->entry, &unix_unvisited_vertices);
		edge->predecessor->vertex = vertex;
	}

	vertex->out_degree++;
	list_add_tail(&edge->vertex_entry, &vertex->edges);

	unix_update_graph(unix_edge_successor(edge));
}

static void unix_del_edge(struct scm_fp_list *fpl, struct unix_edge *edge)
{
	struct unix_vertex *vertex = edge->predecessor->vertex;

	if (!fpl->dead)
		unix_update_graph(unix_edge_successor(edge));

	list_del(&edge->vertex_entry);
	vertex->out_degree--;

	if (!vertex->out_degree) {
		edge->predecessor->vertex = NULL;
		list_move_tail(&vertex->entry, &fpl->vertices);
	}
}

static void unix_free_vertices(struct scm_fp_list *fpl)
{
	struct unix_vertex *vertex, *next_vertex;

	list_for_each_entry_safe(vertex, next_vertex, &fpl->vertices, entry) {
		list_del(&vertex->entry);
		kfree(vertex);
	}
}

static DEFINE_SPINLOCK(unix_gc_lock);
unsigned int unix_tot_inflight;

void unix_add_edges(struct scm_fp_list *fpl, struct unix_sock *receiver)
{
	int i = 0, j = 0;

	spin_lock(&unix_gc_lock);

	if (!fpl->count_unix)
		goto out;

	do {
		struct unix_sock *inflight = unix_get_socket(fpl->fp[j++]);
		struct unix_edge *edge;

		if (!inflight)
			continue;

		edge = fpl->edges + i++;
		edge->predecessor = inflight;
		edge->successor = receiver;

		unix_add_edge(fpl, edge);
	} while (i < fpl->count_unix);

	receiver->scm_stat.nr_unix_fds += fpl->count_unix;
	WRITE_ONCE(unix_tot_inflight, unix_tot_inflight + fpl->count_unix);
out:
	WRITE_ONCE(fpl->user->unix_inflight, fpl->user->unix_inflight + fpl->count);

	spin_unlock(&unix_gc_lock);

	fpl->inflight = true;

	unix_free_vertices(fpl);
}

void unix_del_edges(struct scm_fp_list *fpl)
{
	struct unix_sock *receiver;
	int i = 0;

	spin_lock(&unix_gc_lock);

	if (!fpl->count_unix)
		goto out;

	do {
		struct unix_edge *edge = fpl->edges + i++;

		unix_del_edge(fpl, edge);
	} while (i < fpl->count_unix);

	if (!fpl->dead) {
		receiver = fpl->edges[0].successor;
		receiver->scm_stat.nr_unix_fds -= fpl->count_unix;
	}
	WRITE_ONCE(unix_tot_inflight, unix_tot_inflight - fpl->count_unix);
out:
	WRITE_ONCE(fpl->user->unix_inflight, fpl->user->unix_inflight - fpl->count);

	spin_unlock(&unix_gc_lock);

	fpl->inflight = false;
}

void unix_update_edges(struct unix_sock *receiver)
{
	/* nr_unix_fds is only updated under unix_state_lock().
	 * If it's 0 here, the embryo socket is not part of the
	 * inflight graph, and GC will not see it, so no lock needed.
	 */
	if (!receiver->scm_stat.nr_unix_fds) {
		receiver->listener = NULL;
	} else {
		spin_lock(&unix_gc_lock);
		unix_update_graph(unix_sk(receiver->listener)->vertex);
		receiver->listener = NULL;
		spin_unlock(&unix_gc_lock);
	}
}

int unix_prepare_fpl(struct scm_fp_list *fpl)
{
	struct unix_vertex *vertex;
	int i;

	if (!fpl->count_unix)
		return 0;

	for (i = 0; i < fpl->count_unix; i++) {
		vertex = kmalloc(sizeof(*vertex), GFP_KERNEL);
		if (!vertex)
			goto err;

		list_add(&vertex->entry, &fpl->vertices);
	}

	fpl->edges = kvmalloc_array(fpl->count_unix, sizeof(*fpl->edges),
				    GFP_KERNEL_ACCOUNT);
	if (!fpl->edges)
		goto err;

	return 0;

err:
	unix_free_vertices(fpl);
	return -ENOMEM;
}

void unix_destroy_fpl(struct scm_fp_list *fpl)
{
	if (fpl->inflight)
		unix_del_edges(fpl);

	kvfree(fpl->edges);
	unix_free_vertices(fpl);
}

static bool unix_vertex_dead(struct unix_vertex *vertex)
{
	struct unix_edge *edge;
	struct unix_sock *u;
	long total_ref;

	list_for_each_entry(edge, &vertex->edges, vertex_entry) {
		struct unix_vertex *next_vertex = unix_edge_successor(edge);

		/* The vertex's fd can be received by a non-inflight socket. */
		if (!next_vertex)
			return false;

		/* The vertex's fd can be received by an inflight socket in
		 * another SCC.
		 */
		if (next_vertex->scc_index != vertex->scc_index)
			return false;
	}

	/* No receiver exists out of the same SCC. */

	edge = list_first_entry(&vertex->edges, typeof(*edge), vertex_entry);
	u = edge->predecessor;
	total_ref = file_count(u->sk.sk_socket->file);

	/* If not close()d, total_ref > out_degree. */
	if (total_ref != vertex->out_degree)
		return false;

	return true;
}

enum unix_recv_queue_lock_class {
	U_RECVQ_LOCK_NORMAL,
	U_RECVQ_LOCK_EMBRYO,
};

static void unix_collect_queue(struct unix_sock *u, struct sk_buff_head *hitlist)
{
	skb_queue_splice_init(&u->sk.sk_receive_queue, hitlist);

#if IS_ENABLED(CONFIG_AF_UNIX_OOB)
	if (u->oob_skb) {
		WARN_ON_ONCE(skb_unref(u->oob_skb));
		u->oob_skb = NULL;
	}
#endif
}

static void unix_collect_skb(struct list_head *scc, struct sk_buff_head *hitlist)
{
	struct unix_vertex *vertex;

	list_for_each_entry_reverse(vertex, scc, scc_entry) {
		struct sk_buff_head *queue;
		struct unix_edge *edge;
		struct unix_sock *u;

		edge = list_first_entry(&vertex->edges, typeof(*edge), vertex_entry);
		u = edge->predecessor;
		queue = &u->sk.sk_receive_queue;

		spin_lock(&queue->lock);

		if (u->sk.sk_state == TCP_LISTEN) {
			struct sk_buff *skb;

			skb_queue_walk(queue, skb) {
				struct sk_buff_head *embryo_queue = &skb->sk->sk_receive_queue;

				/* listener -> embryo order, the inversion never happens. */
				spin_lock_nested(&embryo_queue->lock, U_RECVQ_LOCK_EMBRYO);
				unix_collect_queue(unix_sk(skb->sk), hitlist);
				spin_unlock(&embryo_queue->lock);
			}
		} else {
			unix_collect_queue(u, hitlist);
		}

		spin_unlock(&queue->lock);
	}
}

static bool unix_scc_cyclic(struct list_head *scc)
{
	struct unix_vertex *vertex;
	struct unix_edge *edge;

	/* SCC containing multiple vertices ? */
	if (!list_is_singular(scc))
		return true;

	vertex = list_first_entry(scc, typeof(*vertex), scc_entry);

	/* Self-reference or a embryo-listener circle ? */
	list_for_each_entry(edge, &vertex->edges, vertex_entry) {
		if (unix_edge_successor(edge) == vertex)
			return true;
	}

	return false;
}

static LIST_HEAD(unix_visited_vertices);
static unsigned long unix_vertex_grouped_index = UNIX_VERTEX_INDEX_MARK2;

static void __unix_walk_scc(struct unix_vertex *vertex, unsigned long *last_index,
			    struct sk_buff_head *hitlist)
{
	LIST_HEAD(vertex_stack);
	struct unix_edge *edge;
	LIST_HEAD(edge_stack);

next_vertex:
	/* Push vertex to vertex_stack and mark it as on-stack
	 * (index >= UNIX_VERTEX_INDEX_START).
	 * The vertex will be popped when finalising SCC later.
	 */
	list_add(&vertex->scc_entry, &vertex_stack);

	vertex->index = *last_index;
	vertex->scc_index = *last_index;
	(*last_index)++;

	/* Explore neighbour vertices (receivers of the current vertex's fd). */
	list_for_each_entry(edge, &vertex->edges, vertex_entry) {
		struct unix_vertex *next_vertex = unix_edge_successor(edge);

		if (!next_vertex)
			continue;

		if (next_vertex->index == unix_vertex_unvisited_index) {
			/* Iterative deepening depth first search
			 *
			 *   1. Push a forward edge to edge_stack and set
			 *      the successor to vertex for the next iteration.
			 */
			list_add(&edge->stack_entry, &edge_stack);

			vertex = next_vertex;
			goto next_vertex;

			/*   2. Pop the edge directed to the current vertex
			 *      and restore the ancestor for backtracking.
			 */
prev_vertex:
			edge = list_first_entry(&edge_stack, typeof(*edge), stack_entry);
			list_del_init(&edge->stack_entry);

			next_vertex = vertex;
			vertex = edge->predecessor->vertex;

			/* If the successor has a smaller scc_index, two vertices
			 * are in the same SCC, so propagate the smaller scc_index
			 * to skip SCC finalisation.
			 */
			vertex->scc_index = min(vertex->scc_index, next_vertex->scc_index);
		} else if (next_vertex->index != unix_vertex_grouped_index) {
			/* Loop detected by a back/cross edge.
			 *
			 * The successor is on vertex_stack, so two vertices are in
			 * the same SCC.  If the successor has a smaller *scc_index*,
			 * propagate it to skip SCC finalisation.
			 */
			vertex->scc_index = min(vertex->scc_index, next_vertex->scc_index);
		} else {
			/* The successor was already grouped as another SCC */
		}
	}

	if (vertex->index == vertex->scc_index) {
		struct list_head scc;
		bool scc_dead = true;

		/* SCC finalised.
		 *
		 * If the scc_index was not updated, all the vertices above on
		 * vertex_stack are in the same SCC.  Group them using scc_entry.
		 */
		__list_cut_position(&scc, &vertex_stack, &vertex->scc_entry);

		list_for_each_entry_reverse(vertex, &scc, scc_entry) {
			/* Don't restart DFS from this vertex in unix_walk_scc(). */
			list_move_tail(&vertex->entry, &unix_visited_vertices);

			/* Mark vertex as off-stack. */
			vertex->index = unix_vertex_grouped_index;

			if (scc_dead)
				scc_dead = unix_vertex_dead(vertex);
		}

		if (scc_dead)
			unix_collect_skb(&scc, hitlist);
		else if (!unix_graph_maybe_cyclic)
			unix_graph_maybe_cyclic = unix_scc_cyclic(&scc);

		list_del(&scc);
	}

	/* Need backtracking ? */
	if (!list_empty(&edge_stack))
		goto prev_vertex;
}

static void unix_walk_scc(struct sk_buff_head *hitlist)
{
	unsigned long last_index = UNIX_VERTEX_INDEX_START;

	unix_graph_maybe_cyclic = false;

	/* Visit every vertex exactly once.
	 * __unix_walk_scc() moves visited vertices to unix_visited_vertices.
	 */
	while (!list_empty(&unix_unvisited_vertices)) {
		struct unix_vertex *vertex;

		vertex = list_first_entry(&unix_unvisited_vertices, typeof(*vertex), entry);
		__unix_walk_scc(vertex, &last_index, hitlist);
	}

	list_replace_init(&unix_visited_vertices, &unix_unvisited_vertices);
	swap(unix_vertex_unvisited_index, unix_vertex_grouped_index);

	unix_graph_grouped = true;
}

static void unix_walk_scc_fast(struct sk_buff_head *hitlist)
{
	unix_graph_maybe_cyclic = false;

	while (!list_empty(&unix_unvisited_vertices)) {
		struct unix_vertex *vertex;
		struct list_head scc;
		bool scc_dead = true;

		vertex = list_first_entry(&unix_unvisited_vertices, typeof(*vertex), entry);
		list_add(&scc, &vertex->scc_entry);

		list_for_each_entry_reverse(vertex, &scc, scc_entry) {
			list_move_tail(&vertex->entry, &unix_visited_vertices);

			if (scc_dead)
				scc_dead = unix_vertex_dead(vertex);
		}

		if (scc_dead)
			unix_collect_skb(&scc, hitlist);
		else if (!unix_graph_maybe_cyclic)
			unix_graph_maybe_cyclic = unix_scc_cyclic(&scc);

		list_del(&scc);
	}

	list_replace_init(&unix_visited_vertices, &unix_unvisited_vertices);
}

static bool gc_in_progress;

static void __unix_gc(struct work_struct *work)
{
	struct sk_buff_head hitlist;
	struct sk_buff *skb;

	spin_lock(&unix_gc_lock);

	if (!unix_graph_maybe_cyclic) {
		spin_unlock(&unix_gc_lock);
		goto skip_gc;
	}

	__skb_queue_head_init(&hitlist);

	if (unix_graph_grouped)
		unix_walk_scc_fast(&hitlist);
	else
		unix_walk_scc(&hitlist);

	spin_unlock(&unix_gc_lock);

	skb_queue_walk(&hitlist, skb) {
		if (UNIXCB(skb).fp)
			UNIXCB(skb).fp->dead = true;
	}

	__skb_queue_purge(&hitlist);
skip_gc:
	WRITE_ONCE(gc_in_progress, false);
}

static DECLARE_WORK(unix_gc_work, __unix_gc);

void unix_gc(void)
{
	WRITE_ONCE(gc_in_progress, true);
	queue_work(system_unbound_wq, &unix_gc_work);
}

#define UNIX_INFLIGHT_TRIGGER_GC 16000
#define UNIX_INFLIGHT_SANE_USER (SCM_MAX_FD * 8)

void wait_for_unix_gc(struct scm_fp_list *fpl)
{
	/* If number of inflight sockets is insane,
	 * force a garbage collect right now.
	 *
	 * Paired with the WRITE_ONCE() in unix_inflight(),
	 * unix_notinflight(), and __unix_gc().
	 */
	if (READ_ONCE(unix_tot_inflight) > UNIX_INFLIGHT_TRIGGER_GC &&
	    !READ_ONCE(gc_in_progress))
		unix_gc();

	/* Penalise users who want to send AF_UNIX sockets
	 * but whose sockets have not been received yet.
	 */
	if (!fpl || !fpl->count_unix ||
	    READ_ONCE(fpl->user->unix_inflight) < UNIX_INFLIGHT_SANE_USER)
		return;

	if (READ_ONCE(gc_in_progress))
		flush_work(&unix_gc_work);
}
