/*-
 * Copyright (c) 2017 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/kernel.h>

struct linux_kmem_rcu {
	struct rcu_head rcu_head;
	struct linux_kmem_cache *cache;
};

#define	LINUX_KMEM_TO_RCU(c, m)					\
	((struct linux_kmem_rcu *)((char *)(m) +		\
	(c)->cache_size - sizeof(struct linux_kmem_rcu)))

#define	LINUX_RCU_TO_KMEM(r)					\
	((void *)((char *)(r) + sizeof(struct linux_kmem_rcu) - \
	(r)->cache->cache_size))

static int
linux_kmem_ctor(void *mem, int size, void *arg, int flags)
{
	struct linux_kmem_cache *c = arg;

	if (unlikely(c->cache_flags & SLAB_TYPESAFE_BY_RCU)) {
		struct linux_kmem_rcu *rcu = LINUX_KMEM_TO_RCU(c, mem);

		/* duplicate cache pointer */
		rcu->cache = c;
	}

	/* check for constructor */
	if (likely(c->cache_ctor != NULL))
		c->cache_ctor(mem);

	return (0);
}

static void
linux_kmem_cache_free_rcu_callback(struct rcu_head *head)
{
	struct linux_kmem_rcu *rcu =
	    container_of(head, struct linux_kmem_rcu, rcu_head);

	uma_zfree(rcu->cache->cache_zone, LINUX_RCU_TO_KMEM(rcu));
}

struct linux_kmem_cache *
linux_kmem_cache_create(const char *name, size_t size, size_t align,
    unsigned flags, linux_kmem_ctor_t *ctor)
{
	struct linux_kmem_cache *c;

	c = malloc(sizeof(*c), M_KMALLOC, M_WAITOK);

	if (flags & SLAB_HWCACHE_ALIGN)
		align = UMA_ALIGN_CACHE;
	else if (align != 0)
		align--;

	if (flags & SLAB_TYPESAFE_BY_RCU) {
		/* make room for RCU structure */
		size = ALIGN(size, sizeof(void *));
		size += sizeof(struct linux_kmem_rcu);

		/* create cache_zone */
		c->cache_zone = uma_zcreate(name, size,
		    linux_kmem_ctor, NULL, NULL, NULL,
		    align, UMA_ZONE_ZINIT);
	} else {
		/* create cache_zone */
		c->cache_zone = uma_zcreate(name, size,
		    ctor ? linux_kmem_ctor : NULL, NULL,
		    NULL, NULL, align, 0);
	}

	c->cache_flags = flags;
	c->cache_ctor = ctor;
	c->cache_size = size;
	return (c);
}

void
linux_kmem_cache_free_rcu(struct linux_kmem_cache *c, void *m)
{
	struct linux_kmem_rcu *rcu = LINUX_KMEM_TO_RCU(c, m);

	call_rcu(&rcu->rcu_head, linux_kmem_cache_free_rcu_callback);
}

void
linux_kmem_cache_destroy(struct linux_kmem_cache *c)
{
	if (unlikely(c->cache_flags & SLAB_TYPESAFE_BY_RCU)) {
		/* make sure all free callbacks have been called */
		rcu_barrier();
	}

	uma_zdestroy(c->cache_zone);
	free(c, M_KMALLOC);
}
