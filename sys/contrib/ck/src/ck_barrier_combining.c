/*
 * Copyright 2011-2015 Samy Al Bahra.
 * Copyright 2011 David Joseph.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <ck_barrier.h>
#include <ck_cc.h>
#include <ck_pr.h>
#include <ck_spinlock.h>

struct ck_barrier_combining_queue {
	struct ck_barrier_combining_group *head;
	struct ck_barrier_combining_group *tail;
};

static struct ck_barrier_combining_group *
ck_barrier_combining_queue_dequeue(struct ck_barrier_combining_queue *queue)
{
	struct ck_barrier_combining_group *front = NULL;

	if (queue->head != NULL) {
		front = queue->head;
		queue->head = queue->head->next;
	}

	return front;
}

static void
ck_barrier_combining_insert(struct ck_barrier_combining_group *parent,
    struct ck_barrier_combining_group *tnode,
    struct ck_barrier_combining_group **child)
{

	*child = tnode;
	tnode->parent = parent;

	/*
	 * After inserting, we must increment the parent group's count for
	 * number of threads expected to reach it; otherwise, the
	 * barrier may end prematurely.
	 */
	parent->k++;
	return;
}

/*
 * This implementation of software combining tree barriers
 * uses level order traversal to insert new thread groups
 * into the barrier's tree. We use a queue to implement this
 * traversal.
 */
static void
ck_barrier_combining_queue_enqueue(struct ck_barrier_combining_queue *queue,
    struct ck_barrier_combining_group *node_value)
{

	node_value->next = NULL;
	if (queue->head == NULL) {
		queue->head = queue->tail = node_value;
		return;
	}

	queue->tail->next = node_value;
	queue->tail = node_value;

	return;
}


void
ck_barrier_combining_group_init(struct ck_barrier_combining *root,
    struct ck_barrier_combining_group *tnode,
    unsigned int nthr)
{
	struct ck_barrier_combining_group *node;
	struct ck_barrier_combining_queue queue;

	queue.head = queue.tail = NULL;

	tnode->k = nthr;
	tnode->count = 0;
	tnode->sense = 0;
	tnode->left = tnode->right = NULL;

	/*
	 * Finds the first available node for linkage into the combining
	 * tree. The use of a spinlock is excusable as this is a one-time
	 * initialization cost.
	 */
	ck_spinlock_fas_lock(&root->mutex);
	ck_barrier_combining_queue_enqueue(&queue, root->root);
	while (queue.head != NULL) {
		node = ck_barrier_combining_queue_dequeue(&queue);

		/* If the left child is free, link the group there. */
		if (node->left == NULL) {
			ck_barrier_combining_insert(node, tnode, &node->left);
			goto leave;
		}

		/* If the right child is free, link the group there. */
		if (node->right == NULL) {
			ck_barrier_combining_insert(node, tnode, &node->right);
			goto leave;
		}

		/*
		 * If unsuccessful, try inserting as a child of the children of the
		 * current node.
		 */
		ck_barrier_combining_queue_enqueue(&queue, node->left);
		ck_barrier_combining_queue_enqueue(&queue, node->right);
	}

leave:
	ck_spinlock_fas_unlock(&root->mutex);
	return;
}

void
ck_barrier_combining_init(struct ck_barrier_combining *root,
    struct ck_barrier_combining_group *init_root)
{

	init_root->k = 0;
	init_root->count = 0;
	init_root->sense = 0;
	init_root->parent = init_root->left = init_root->right = NULL;
	ck_spinlock_fas_init(&root->mutex);
	root->root = init_root;
	return;
}

static void
ck_barrier_combining_aux(struct ck_barrier_combining *barrier,
    struct ck_barrier_combining_group *tnode,
    unsigned int sense)
{

	/*
	 * If this is the last thread in the group, it moves on to the parent group.
	 * Otherwise, it spins on this group's sense.
	 */
	if (ck_pr_faa_uint(&tnode->count, 1) == tnode->k - 1) {
		/*
		 * If we are and will be the last thread entering the barrier for the
		 * current group then signal the parent group if one exists.
		 */
		if (tnode->parent != NULL)
			ck_barrier_combining_aux(barrier, tnode->parent, sense);

		/*
		 * Once the thread returns from its parent(s), it reinitializes the group's
		 * arrival count and signals other threads to continue by flipping the group
		 * sense. Order of these operations is not important since we assume a static
		 * number of threads are members of a barrier for the lifetime of the barrier.
		 * Since count is explicitly reinitialized, it is guaranteed that at any point
		 * tnode->count is equivalent to tnode->k if and only if that many threads
		 * are at the barrier.
		 */
		ck_pr_store_uint(&tnode->count, 0);
		ck_pr_fence_store();
		ck_pr_store_uint(&tnode->sense, ~tnode->sense);
	} else {
		while (sense != ck_pr_load_uint(&tnode->sense))
			ck_pr_stall();
	}
	ck_pr_fence_memory();

	return;
}

void
ck_barrier_combining(struct ck_barrier_combining *barrier,
    struct ck_barrier_combining_group *tnode,
    struct ck_barrier_combining_state *state)
{

	ck_barrier_combining_aux(barrier, tnode, state->sense);

	/* Reverse the execution context's sense for the next barrier. */
	state->sense = ~state->sense;
	return;
}
