/*-
 * Copyright 2016 Michal Meloun <mmel@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/kobj.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/sx.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif
#include <dev/extres/clk/clk.h>

SYSCTL_NODE(_hw, OID_AUTO, clock, CTLFLAG_RD, NULL, "Clocks");

MALLOC_DEFINE(M_CLOCK, "clocks", "Clock framework");

/* Forward declarations. */
struct clk;
struct clknodenode;
struct clkdom;

typedef TAILQ_HEAD(clknode_list, clknode) clknode_list_t;
typedef TAILQ_HEAD(clkdom_list, clkdom) clkdom_list_t;

/* Default clock methods. */
static int clknode_method_init(struct clknode *clk, device_t dev);
static int clknode_method_recalc_freq(struct clknode *clk, uint64_t *freq);
static int clknode_method_set_freq(struct clknode *clk, uint64_t fin,
    uint64_t *fout, int flags, int *stop);
static int clknode_method_set_gate(struct clknode *clk, bool enable);
static int clknode_method_set_mux(struct clknode *clk, int idx);

/*
 * Clock controller methods.
 */
static clknode_method_t clknode_methods[] = {
	CLKNODEMETHOD(clknode_init,		clknode_method_init),
	CLKNODEMETHOD(clknode_recalc_freq,	clknode_method_recalc_freq),
	CLKNODEMETHOD(clknode_set_freq,		clknode_method_set_freq),
	CLKNODEMETHOD(clknode_set_gate,		clknode_method_set_gate),
	CLKNODEMETHOD(clknode_set_mux,		clknode_method_set_mux),

	CLKNODEMETHOD_END
};
DEFINE_CLASS_0(clknode, clknode_class, clknode_methods, 0);

/*
 * Clock node - basic element for modeling SOC clock graph.  It holds the clock
 * provider's data about the clock, and the links for the clock's membership in
 * various lists.
 */
struct clknode {
	KOBJ_FIELDS;

	/* Clock nodes topology. */
	struct clkdom 		*clkdom;	/* Owning clock domain */
	TAILQ_ENTRY(clknode)	clkdom_link;	/* Domain list entry */
	TAILQ_ENTRY(clknode)	clklist_link;	/* Global list entry */

	/* String based parent list. */
	const char		**parent_names;	/* Array of parent names */
	int			parent_cnt;	/* Number of parents */
	int			parent_idx;	/* Parent index or -1 */

	/* Cache for already resolved names. */
	struct clknode		**parents;	/* Array of potential parents */
	struct clknode		*parent;	/* Current parent */

	/* Parent/child relationship links. */
	clknode_list_t		children;	/* List of our children */
	TAILQ_ENTRY(clknode)	sibling_link; 	/* Our entry in parent's list */

	/* Details of this device. */
	void			*softc;		/* Instance softc */
	const char		*name;		/* Globally unique name */
	intptr_t		id;		/* Per domain unique id */
	int			flags;		/* CLK_FLAG_*  */
	struct sx		lock;		/* Lock for this clock */
	int			ref_cnt;	/* Reference counter */
	int			enable_cnt;	/* Enabled counter */

	/* Cached values. */
	uint64_t		freq;		/* Actual frequency */

	struct sysctl_ctx_list	sysctl_ctx;
};

/*
 *  Per consumer data, information about how a consumer is using a clock node.
 *  A pointer to this structure is used as a handle in the consumer interface.
 */
struct clk {
	device_t		dev;
	struct clknode		*clknode;
	int			enable_cnt;
};

/*
 * Clock domain - a group of clocks provided by one clock device.
 */
struct clkdom {
	device_t 		dev; 	/* Link to provider device */
	TAILQ_ENTRY(clkdom)	link;		/* Global domain list entry */
	clknode_list_t		clknode_list;	/* All clocks in the domain */

#ifdef FDT
	clknode_ofw_mapper_func	*ofw_mapper;	/* Find clock using FDT xref */
#endif
};

/*
 * The system-wide list of clock domains.
 */
static clkdom_list_t clkdom_list = TAILQ_HEAD_INITIALIZER(clkdom_list);

/*
 * Each clock node is linked on a system-wide list and can be searched by name.
 */
static clknode_list_t clknode_list = TAILQ_HEAD_INITIALIZER(clknode_list);

/*
 * Locking - we use three levels of locking:
 * - First, topology lock is taken.  This one protect all lists.
 * - Second level is per clknode lock.  It protects clknode data.
 * - Third level is outside of this file, it protect clock device registers.
 * First two levels use sleepable locks; clock device can use mutex or sx lock.
 */
static struct sx		clk_topo_lock;
SX_SYSINIT(clock_topology, &clk_topo_lock, "Clock topology lock");

#define CLK_TOPO_SLOCK()	sx_slock(&clk_topo_lock)
#define CLK_TOPO_XLOCK()	sx_xlock(&clk_topo_lock)
#define CLK_TOPO_UNLOCK()	sx_unlock(&clk_topo_lock)
#define CLK_TOPO_ASSERT()	sx_assert(&clk_topo_lock, SA_LOCKED)
#define CLK_TOPO_XASSERT()	sx_assert(&clk_topo_lock, SA_XLOCKED)

#define CLKNODE_SLOCK(_sc)	sx_slock(&((_sc)->lock))
#define CLKNODE_XLOCK(_sc)	sx_xlock(&((_sc)->lock))
#define CLKNODE_UNLOCK(_sc)	sx_unlock(&((_sc)->lock))

static void clknode_adjust_parent(struct clknode *clknode, int idx);

enum clknode_sysctl_type {
	CLKNODE_SYSCTL_PARENT,
	CLKNODE_SYSCTL_PARENTS_LIST,
	CLKNODE_SYSCTL_CHILDREN_LIST,
};

static int clknode_sysctl(SYSCTL_HANDLER_ARGS);
static int clkdom_sysctl(SYSCTL_HANDLER_ARGS);

/*
 * Default clock methods for base class.
 */
static int
clknode_method_init(struct clknode *clknode, device_t dev)
{

	return (0);
}

static int
clknode_method_recalc_freq(struct clknode *clknode, uint64_t *freq)
{

	return (0);
}

static int
clknode_method_set_freq(struct clknode *clknode, uint64_t fin,  uint64_t *fout,
   int flags, int *stop)
{

	*stop = 0;
	return (0);
}

static int
clknode_method_set_gate(struct clknode *clk, bool enable)
{

	return (0);
}

static int
clknode_method_set_mux(struct clknode *clk, int idx)
{

	return (0);
}

/*
 * Internal functions.
 */

/*
 * Duplicate an array of parent names.
 *
 * Compute total size and allocate a single block which holds both the array of
 * pointers to strings and the copied strings themselves.  Returns a pointer to
 * the start of the block where the array of copied string pointers lives.
 *
 * XXX Revisit this, no need for the DECONST stuff.
 */
static const char **
strdup_list(const char **names, int num)
{
	size_t len, slen;
	const char **outptr, *ptr;
	int i;

	len = sizeof(char *) * num;
	for (i = 0; i < num; i++) {
		if (names[i] == NULL)
			continue;
		slen = strlen(names[i]);
		if (slen == 0)
			panic("Clock parent names array have empty string");
		len += slen + 1;
	}
	outptr = malloc(len, M_CLOCK, M_WAITOK | M_ZERO);
	ptr = (char *)(outptr + num);
	for (i = 0; i < num; i++) {
		if (names[i] == NULL)
			continue;
		outptr[i] = ptr;
		slen = strlen(names[i]) + 1;
		bcopy(names[i], __DECONST(void *, outptr[i]), slen);
		ptr += slen;
	}
	return (outptr);
}

/*
 * Recompute the cached frequency for this node and all its children.
 */
static int
clknode_refresh_cache(struct clknode *clknode, uint64_t freq)
{
	int rv;
	struct clknode *entry;

	CLK_TOPO_XASSERT();

	/* Compute generated frequency. */
	rv = CLKNODE_RECALC_FREQ(clknode, &freq);
	if (rv != 0) {
		 /* XXX If an error happens while refreshing children
		  * this leaves the world in a  partially-updated state.
		  * Panic for now.
		  */
		panic("clknode_refresh_cache failed for '%s'\n",
		    clknode->name);
		return (rv);
	}
	/* Refresh cache for this node. */
	clknode->freq = freq;

	/* Refresh cache for all children. */
	TAILQ_FOREACH(entry, &(clknode->children), sibling_link) {
		rv = clknode_refresh_cache(entry, freq);
		if (rv != 0)
			return (rv);
	}
	return (0);
}

/*
 * Public interface.
 */

struct clknode *
clknode_find_by_name(const char *name)
{
	struct clknode *entry;

	CLK_TOPO_ASSERT();

	TAILQ_FOREACH(entry, &clknode_list, clklist_link) {
		if (strcmp(entry->name, name) == 0)
			return (entry);
	}
	return (NULL);
}

struct clknode *
clknode_find_by_id(struct clkdom *clkdom, intptr_t id)
{
	struct clknode *entry;

	CLK_TOPO_ASSERT();

	TAILQ_FOREACH(entry, &clkdom->clknode_list, clkdom_link) {
		if (entry->id ==  id)
			return (entry);
	}

	return (NULL);
}

/* -------------------------------------------------------------------------- */
/*
 * Clock domain functions
 */

/* Find clock domain associated to device in global list. */
struct clkdom *
clkdom_get_by_dev(const device_t dev)
{
	struct clkdom *entry;

	CLK_TOPO_ASSERT();

	TAILQ_FOREACH(entry, &clkdom_list, link) {
		if (entry->dev == dev)
			return (entry);
	}
	return (NULL);
}


#ifdef FDT
/* Default DT mapper. */
static int
clknode_default_ofw_map(struct clkdom *clkdom, uint32_t ncells,
    phandle_t *cells, struct clknode **clk)
{

	CLK_TOPO_ASSERT();

	if (ncells == 0)
		*clk = clknode_find_by_id(clkdom, 1);
	else if (ncells == 1)
		*clk = clknode_find_by_id(clkdom, cells[0]);
	else
		return  (ERANGE);

	if (*clk == NULL)
		return (ENXIO);
	return (0);
}
#endif

/*
 * Create a clock domain.  Returns with the topo lock held.
 */
struct clkdom *
clkdom_create(device_t dev)
{
	struct clkdom *clkdom;

	clkdom = malloc(sizeof(struct clkdom), M_CLOCK, M_WAITOK | M_ZERO);
	clkdom->dev = dev;
	TAILQ_INIT(&clkdom->clknode_list);
#ifdef FDT
	clkdom->ofw_mapper = clknode_default_ofw_map;
#endif

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	  SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	  OID_AUTO, "clocks",
	  CTLTYPE_STRING | CTLFLAG_RD,
		    clkdom, 0, clkdom_sysctl,
		    "A",
		    "Clock list for the domain");

	return (clkdom);
}

void
clkdom_unlock(struct clkdom *clkdom)
{

	CLK_TOPO_UNLOCK();
}

void
clkdom_xlock(struct clkdom *clkdom)
{

	CLK_TOPO_XLOCK();
}

/*
 * Finalize initialization of clock domain.  Releases topo lock.
 *
 * XXX Revisit failure handling.
 */
int
clkdom_finit(struct clkdom *clkdom)
{
	struct clknode *clknode;
	int i, rv;
#ifdef FDT
	phandle_t node;


	if ((node = ofw_bus_get_node(clkdom->dev)) == -1) {
		device_printf(clkdom->dev,
		    "%s called on not ofw based device\n", __func__);
		return (ENXIO);
	}
#endif
	rv = 0;

	/* Make clock domain globally visible. */
	CLK_TOPO_XLOCK();
	TAILQ_INSERT_TAIL(&clkdom_list, clkdom, link);
#ifdef FDT
	OF_device_register_xref(OF_xref_from_node(node), clkdom->dev);
#endif

	/* Register all clock names into global list. */
	TAILQ_FOREACH(clknode, &clkdom->clknode_list, clkdom_link) {
		TAILQ_INSERT_TAIL(&clknode_list, clknode, clklist_link);
	}
	/*
	 * At this point all domain nodes must be registered and all
	 * parents must be valid.
	 */
	TAILQ_FOREACH(clknode, &clkdom->clknode_list, clkdom_link) {
		if (clknode->parent_cnt == 0)
			continue;
		for (i = 0; i < clknode->parent_cnt; i++) {
			if (clknode->parents[i] != NULL)
				continue;
			if (clknode->parent_names[i] == NULL)
				continue;
			clknode->parents[i] = clknode_find_by_name(
			    clknode->parent_names[i]);
			if (clknode->parents[i] == NULL) {
				device_printf(clkdom->dev,
				    "Clock %s have unknown parent: %s\n",
				    clknode->name, clknode->parent_names[i]);
				rv = ENODEV;
			}
		}

		/* If parent index is not set yet... */
		if (clknode->parent_idx == CLKNODE_IDX_NONE) {
			device_printf(clkdom->dev,
			    "Clock %s have not set parent idx\n",
			    clknode->name);
			rv = ENXIO;
			continue;
		}
		if (clknode->parents[clknode->parent_idx] == NULL) {
			device_printf(clkdom->dev,
			    "Clock %s have unknown parent(idx %d): %s\n",
			    clknode->name, clknode->parent_idx,
			    clknode->parent_names[clknode->parent_idx]);
			rv = ENXIO;
			continue;
		}
		clknode_adjust_parent(clknode, clknode->parent_idx);
	}
	CLK_TOPO_UNLOCK();
	return (rv);
}

/* Dump clock domain. */
void
clkdom_dump(struct clkdom * clkdom)
{
	struct clknode *clknode;
	int rv;
	uint64_t freq;

	CLK_TOPO_SLOCK();
	TAILQ_FOREACH(clknode, &clkdom->clknode_list, clkdom_link) {
		rv = clknode_get_freq(clknode, &freq);
		printf("Clock: %s, parent: %s(%d), freq: %ju\n", clknode->name,
		    clknode->parent == NULL ? "(NULL)" : clknode->parent->name,
		    clknode->parent_idx,
		    (uintmax_t)((rv == 0) ? freq: rv));
	}
	CLK_TOPO_UNLOCK();
}

/*
 * Create and initialize clock object, but do not register it.
 */
struct clknode *
clknode_create(struct clkdom * clkdom, clknode_class_t clknode_class,
    const struct clknode_init_def *def)
{
	struct clknode *clknode;
	struct sysctl_oid *clknode_oid;

	KASSERT(def->name != NULL, ("clock name is NULL"));
	KASSERT(def->name[0] != '\0', ("clock name is empty"));
#ifdef   INVARIANTS
	CLK_TOPO_SLOCK();
	if (clknode_find_by_name(def->name) != NULL)
		panic("Duplicated clock registration: %s\n", def->name);
	CLK_TOPO_UNLOCK();
#endif

	/* Create object and initialize it. */
	clknode = malloc(sizeof(struct clknode), M_CLOCK, M_WAITOK | M_ZERO);
	kobj_init((kobj_t)clknode, (kobj_class_t)clknode_class);
	sx_init(&clknode->lock, "Clocknode lock");

	/* Allocate softc if required. */
	if (clknode_class->size > 0) {
		clknode->softc = malloc(clknode_class->size,
		    M_CLOCK, M_WAITOK | M_ZERO);
	}

	/* Prepare array for ptrs to parent clocks. */
	clknode->parents = malloc(sizeof(struct clknode *) * def->parent_cnt,
	    M_CLOCK, M_WAITOK | M_ZERO);

	/* Copy all strings unless they're flagged as static. */
	if (def->flags & CLK_NODE_STATIC_STRINGS) {
		clknode->name = def->name;
		clknode->parent_names = def->parent_names;
	} else {
		clknode->name = strdup(def->name, M_CLOCK);
		clknode->parent_names =
		    strdup_list(def->parent_names, def->parent_cnt);
	}

	/* Rest of init. */
	clknode->id = def->id;
	clknode->clkdom = clkdom;
	clknode->flags = def->flags;
	clknode->parent_cnt = def->parent_cnt;
	clknode->parent = NULL;
	clknode->parent_idx = CLKNODE_IDX_NONE;
	TAILQ_INIT(&clknode->children);

	sysctl_ctx_init(&clknode->sysctl_ctx);
	clknode_oid = SYSCTL_ADD_NODE(&clknode->sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_hw_clock),
	    OID_AUTO, clknode->name,
	    CTLFLAG_RD, 0, "A clock node");

	SYSCTL_ADD_U64(&clknode->sysctl_ctx,
	    SYSCTL_CHILDREN(clknode_oid),
	    OID_AUTO, "frequency",
	    CTLFLAG_RD, &clknode->freq, 0, "The clock frequency");
	SYSCTL_ADD_PROC(&clknode->sysctl_ctx,
	    SYSCTL_CHILDREN(clknode_oid),
	    OID_AUTO, "parent",
	    CTLTYPE_STRING | CTLFLAG_RD,
	    clknode, CLKNODE_SYSCTL_PARENT, clknode_sysctl,
	    "A",
	    "The clock parent");
	SYSCTL_ADD_PROC(&clknode->sysctl_ctx,
	    SYSCTL_CHILDREN(clknode_oid),
	    OID_AUTO, "parents",
	    CTLTYPE_STRING | CTLFLAG_RD,
	    clknode, CLKNODE_SYSCTL_PARENTS_LIST, clknode_sysctl,
	    "A",
	    "The clock parents list");
	SYSCTL_ADD_PROC(&clknode->sysctl_ctx,
	    SYSCTL_CHILDREN(clknode_oid),
	    OID_AUTO, "childrens",
	    CTLTYPE_STRING | CTLFLAG_RD,
	    clknode, CLKNODE_SYSCTL_CHILDREN_LIST, clknode_sysctl,
	    "A",
	    "The clock childrens list");
	SYSCTL_ADD_INT(&clknode->sysctl_ctx,
	    SYSCTL_CHILDREN(clknode_oid),
	    OID_AUTO, "enable_cnt",
	    CTLFLAG_RD, &clknode->enable_cnt, 0, "The clock enable counter");

	return (clknode);
}

/*
 * Register clock object into clock domain hierarchy.
 */
struct clknode *
clknode_register(struct clkdom * clkdom, struct clknode *clknode)
{
	int rv;

	rv = CLKNODE_INIT(clknode, clknode_get_device(clknode));
	if (rv != 0) {
		printf(" CLKNODE_INIT failed: %d\n", rv);
		return (NULL);
	}

	TAILQ_INSERT_TAIL(&clkdom->clknode_list, clknode, clkdom_link);

	return (clknode);
}

/*
 * Clock providers interface.
 */

/*
 * Reparent clock node.
 */
static void
clknode_adjust_parent(struct clknode *clknode, int idx)
{

	CLK_TOPO_XASSERT();

	if (clknode->parent_cnt == 0)
		return;
	if ((idx == CLKNODE_IDX_NONE) || (idx >= clknode->parent_cnt))
		panic("%s: Invalid parent index %d for clock %s",
		    __func__, idx, clknode->name);

	if (clknode->parents[idx] == NULL)
		panic("%s: Invalid parent index %d for clock %s",
		    __func__, idx, clknode->name);

	/* Remove me from old children list. */
	if (clknode->parent != NULL) {
		TAILQ_REMOVE(&clknode->parent->children, clknode, sibling_link);
	}

	/* Insert into children list of new parent. */
	clknode->parent_idx = idx;
	clknode->parent = clknode->parents[idx];
	TAILQ_INSERT_TAIL(&clknode->parent->children, clknode, sibling_link);
}

/*
 * Set parent index - init function.
 */
void
clknode_init_parent_idx(struct clknode *clknode, int idx)
{

	if (clknode->parent_cnt == 0) {
		clknode->parent_idx = CLKNODE_IDX_NONE;
		clknode->parent = NULL;
		return;
	}
	if ((idx == CLKNODE_IDX_NONE) ||
	    (idx >= clknode->parent_cnt) ||
	    (clknode->parent_names[idx] == NULL))
		panic("%s: Invalid parent index %d for clock %s",
		    __func__, idx, clknode->name);
	clknode->parent_idx = idx;
}

int
clknode_set_parent_by_idx(struct clknode *clknode, int idx)
{
	int rv;
	uint64_t freq;
	int  oldidx;

	/* We have exclusive topology lock, node lock is not needed. */
	CLK_TOPO_XASSERT();

	if (clknode->parent_cnt == 0)
		return (0);

	if (clknode->parent_idx == idx)
		return (0);

	oldidx = clknode->parent_idx;
	clknode_adjust_parent(clknode, idx);
	rv = CLKNODE_SET_MUX(clknode, idx);
	if (rv != 0) {
		clknode_adjust_parent(clknode, oldidx);
		return (rv);
	}
	rv = clknode_get_freq(clknode->parent, &freq);
	if (rv != 0)
		return (rv);
	rv = clknode_refresh_cache(clknode, freq);
	return (rv);
}

int
clknode_set_parent_by_name(struct clknode *clknode, const char *name)
{
	int rv;
	uint64_t freq;
	int  oldidx, idx;

	/* We have exclusive topology lock, node lock is not needed. */
	CLK_TOPO_XASSERT();

	if (clknode->parent_cnt == 0)
		return (0);

	/*
	 * If this node doesnt have mux, then passthrough request to parent.
	 * This feature is used in clock domain initialization and allows us to
	 * set clock source and target frequency on the tail node of the clock
	 * chain.
	 */
	if (clknode->parent_cnt == 1) {
		rv = clknode_set_parent_by_name(clknode->parent, name);
		return (rv);
	}

	for (idx = 0; idx < clknode->parent_cnt; idx++) {
		if (clknode->parent_names[idx] == NULL)
			continue;
		if (strcmp(clknode->parent_names[idx], name) == 0)
			break;
	}
	if (idx >= clknode->parent_cnt) {
		return (ENXIO);
	}
	if (clknode->parent_idx == idx)
		return (0);

	oldidx = clknode->parent_idx;
	clknode_adjust_parent(clknode, idx);
	rv = CLKNODE_SET_MUX(clknode, idx);
	if (rv != 0) {
		clknode_adjust_parent(clknode, oldidx);
		CLKNODE_UNLOCK(clknode);
		return (rv);
	}
	rv = clknode_get_freq(clknode->parent, &freq);
	if (rv != 0)
		return (rv);
	rv = clknode_refresh_cache(clknode, freq);
	return (rv);
}

struct clknode *
clknode_get_parent(struct clknode *clknode)
{

	return (clknode->parent);
}

const char *
clknode_get_name(struct clknode *clknode)
{

	return (clknode->name);
}

const char **
clknode_get_parent_names(struct clknode *clknode)
{

	return (clknode->parent_names);
}

int
clknode_get_parents_num(struct clknode *clknode)
{

	return (clknode->parent_cnt);
}

int
clknode_get_parent_idx(struct clknode *clknode)
{

	return (clknode->parent_idx);
}

int
clknode_get_flags(struct clknode *clknode)
{

	return (clknode->flags);
}


void *
clknode_get_softc(struct clknode *clknode)
{

	return (clknode->softc);
}

device_t
clknode_get_device(struct clknode *clknode)
{

	return (clknode->clkdom->dev);
}

#ifdef FDT
void
clkdom_set_ofw_mapper(struct clkdom * clkdom, clknode_ofw_mapper_func *map)
{

	clkdom->ofw_mapper = map;
}
#endif

/*
 * Real consumers executive
 */
int
clknode_get_freq(struct clknode *clknode, uint64_t *freq)
{
	int rv;

	CLK_TOPO_ASSERT();

	/* Use cached value, if it exists. */
	*freq  = clknode->freq;
	if (*freq != 0)
		return (0);

	/* Get frequency from parent, if the clock has a parent. */
	if (clknode->parent_cnt > 0) {
		rv = clknode_get_freq(clknode->parent, freq);
		if (rv != 0) {
			return (rv);
		}
	}

	/* And recalculate my output frequency. */
	CLKNODE_XLOCK(clknode);
	rv = CLKNODE_RECALC_FREQ(clknode, freq);
	if (rv != 0) {
		CLKNODE_UNLOCK(clknode);
		printf("Cannot get frequency for clk: %s, error: %d\n",
		    clknode->name, rv);
		return (rv);
	}

	/* Save new frequency to cache. */
	clknode->freq = *freq;
	CLKNODE_UNLOCK(clknode);
	return (0);
}

int
clknode_set_freq(struct clknode *clknode, uint64_t freq, int flags,
    int enablecnt)
{
	int rv, done;
	uint64_t parent_freq;

	/* We have exclusive topology lock, node lock is not needed. */
	CLK_TOPO_XASSERT();

	/* Check for no change */
	if (clknode->freq == freq)
		return (0);

	parent_freq = 0;

	/*
	 * We can set frequency only if
	 *   clock is disabled
	 * OR
	 *   clock is glitch free and is enabled by calling consumer only
	 */
	if ((flags & CLK_SET_DRYRUN) == 0 &&
	    clknode->enable_cnt > 1 &&
	    clknode->enable_cnt > enablecnt &&
	    (clknode->flags & CLK_NODE_GLITCH_FREE) == 0) {
		return (EBUSY);
	}

	/* Get frequency from parent, if the clock has a parent. */
	if (clknode->parent_cnt > 0) {
		rv = clknode_get_freq(clknode->parent, &parent_freq);
		if (rv != 0) {
			return (rv);
		}
	}

	/* Set frequency for this clock. */
	rv = CLKNODE_SET_FREQ(clknode, parent_freq, &freq, flags, &done);
	if (rv != 0) {
		printf("Cannot set frequency for clk: %s, error: %d\n",
		    clknode->name, rv);
		if ((flags & CLK_SET_DRYRUN) == 0)
			clknode_refresh_cache(clknode, parent_freq);
		return (rv);
	}

	if (done) {
		/* Success - invalidate frequency cache for all children. */
		if ((flags & CLK_SET_DRYRUN) == 0) {
			clknode->freq = freq;
			/* Clock might have reparent during set_freq */
			if (clknode->parent_cnt > 0) {
				rv = clknode_get_freq(clknode->parent,
				    &parent_freq);
				if (rv != 0) {
					return (rv);
				}
			}
			clknode_refresh_cache(clknode, parent_freq);
		}
	} else if (clknode->parent != NULL) {
		/* Nothing changed, pass request to parent. */
		rv = clknode_set_freq(clknode->parent, freq, flags, enablecnt);
	} else {
		/* End of chain without action. */
		printf("Cannot set frequency for clk: %s, end of chain\n",
		    clknode->name);
		rv = ENXIO;
	}

	return (rv);
}

int
clknode_enable(struct clknode *clknode)
{
	int rv;

	CLK_TOPO_ASSERT();

	/* Enable clock for each node in chain, starting from source. */
	if (clknode->parent_cnt > 0) {
		rv = clknode_enable(clknode->parent);
		if (rv != 0) {
			return (rv);
		}
	}

	/* Handle this node */
	CLKNODE_XLOCK(clknode);
	if (clknode->enable_cnt == 0) {
		rv = CLKNODE_SET_GATE(clknode, 1);
		if (rv != 0) {
			CLKNODE_UNLOCK(clknode);
			return (rv);
		}
	}
	clknode->enable_cnt++;
	CLKNODE_UNLOCK(clknode);
	return (0);
}

int
clknode_disable(struct clknode *clknode)
{
	int rv;

	CLK_TOPO_ASSERT();
	rv = 0;

	CLKNODE_XLOCK(clknode);
	/* Disable clock for each node in chain, starting from consumer. */
	if ((clknode->enable_cnt == 1) &&
	    ((clknode->flags & CLK_NODE_CANNOT_STOP) == 0)) {
		rv = CLKNODE_SET_GATE(clknode, 0);
		if (rv != 0) {
			CLKNODE_UNLOCK(clknode);
			return (rv);
		}
	}
	clknode->enable_cnt--;
	CLKNODE_UNLOCK(clknode);

	if (clknode->parent_cnt > 0) {
		rv = clknode_disable(clknode->parent);
	}
	return (rv);
}

int
clknode_stop(struct clknode *clknode, int depth)
{
	int rv;

	CLK_TOPO_ASSERT();
	rv = 0;

	CLKNODE_XLOCK(clknode);
	/* The first node cannot be enabled. */
	if ((clknode->enable_cnt != 0) && (depth == 0)) {
		CLKNODE_UNLOCK(clknode);
		return (EBUSY);
	}
	/* Stop clock for each node in chain, starting from consumer. */
	if ((clknode->enable_cnt == 0) &&
	    ((clknode->flags & CLK_NODE_CANNOT_STOP) == 0)) {
		rv = CLKNODE_SET_GATE(clknode, 0);
		if (rv != 0) {
			CLKNODE_UNLOCK(clknode);
			return (rv);
		}
	}
	CLKNODE_UNLOCK(clknode);

	if (clknode->parent_cnt > 0)
		rv = clknode_stop(clknode->parent, depth + 1);
	return (rv);
}

/* --------------------------------------------------------------------------
 *
 * Clock consumers interface.
 *
 */
/* Helper function for clk_get*() */
static clk_t
clk_create(struct clknode *clknode, device_t dev)
{
	struct clk *clk;

	CLK_TOPO_ASSERT();

	clk =  malloc(sizeof(struct clk), M_CLOCK, M_WAITOK);
	clk->dev = dev;
	clk->clknode = clknode;
	clk->enable_cnt = 0;
	clknode->ref_cnt++;

	return (clk);
}

int
clk_get_freq(clk_t clk, uint64_t *freq)
{
	int rv;
	struct clknode *clknode;

	clknode = clk->clknode;
	KASSERT(clknode->ref_cnt > 0,
	   ("Attempt to access unreferenced clock: %s\n", clknode->name));

	CLK_TOPO_SLOCK();
	rv = clknode_get_freq(clknode, freq);
	CLK_TOPO_UNLOCK();
	return (rv);
}

int
clk_set_freq(clk_t clk, uint64_t freq, int flags)
{
	int rv;
	struct clknode *clknode;

	flags &= CLK_SET_USER_MASK;
	clknode = clk->clknode;
	KASSERT(clknode->ref_cnt > 0,
	   ("Attempt to access unreferenced clock: %s\n", clknode->name));

	CLK_TOPO_XLOCK();
	rv = clknode_set_freq(clknode, freq, flags, clk->enable_cnt);
	CLK_TOPO_UNLOCK();
	return (rv);
}

int
clk_test_freq(clk_t clk, uint64_t freq, int flags)
{
	int rv;
	struct clknode *clknode;

	flags &= CLK_SET_USER_MASK;
	clknode = clk->clknode;
	KASSERT(clknode->ref_cnt > 0,
	   ("Attempt to access unreferenced clock: %s\n", clknode->name));

	CLK_TOPO_XLOCK();
	rv = clknode_set_freq(clknode, freq, flags | CLK_SET_DRYRUN, 0);
	CLK_TOPO_UNLOCK();
	return (rv);
}

int
clk_get_parent(clk_t clk, clk_t *parent)
{
	struct clknode *clknode;
	struct clknode *parentnode;

	clknode = clk->clknode;
	KASSERT(clknode->ref_cnt > 0,
	   ("Attempt to access unreferenced clock: %s\n", clknode->name));

	CLK_TOPO_SLOCK();
	parentnode = clknode_get_parent(clknode);
	if (parentnode == NULL) {
		CLK_TOPO_UNLOCK();
		return (ENODEV);
	}
	*parent = clk_create(parentnode, clk->dev);
	CLK_TOPO_UNLOCK();
	return (0);
}

int
clk_set_parent_by_clk(clk_t clk, clk_t parent)
{
	int rv;
	struct clknode *clknode;
	struct clknode *parentnode;

	clknode = clk->clknode;
	parentnode = parent->clknode;
	KASSERT(clknode->ref_cnt > 0,
	   ("Attempt to access unreferenced clock: %s\n", clknode->name));
	KASSERT(parentnode->ref_cnt > 0,
	   ("Attempt to access unreferenced clock: %s\n", clknode->name));
	CLK_TOPO_XLOCK();
	rv = clknode_set_parent_by_name(clknode, parentnode->name);
	CLK_TOPO_UNLOCK();
	return (rv);
}

int
clk_enable(clk_t clk)
{
	int rv;
	struct clknode *clknode;

	clknode = clk->clknode;
	KASSERT(clknode->ref_cnt > 0,
	   ("Attempt to access unreferenced clock: %s\n", clknode->name));
	CLK_TOPO_SLOCK();
	rv = clknode_enable(clknode);
	if (rv == 0)
		clk->enable_cnt++;
	CLK_TOPO_UNLOCK();
	return (rv);
}

int
clk_disable(clk_t clk)
{
	int rv;
	struct clknode *clknode;

	clknode = clk->clknode;
	KASSERT(clknode->ref_cnt > 0,
	   ("Attempt to access unreferenced clock: %s\n", clknode->name));
	KASSERT(clk->enable_cnt > 0,
	   ("Attempt to disable already disabled clock: %s\n", clknode->name));
	CLK_TOPO_SLOCK();
	rv = clknode_disable(clknode);
	if (rv == 0)
		clk->enable_cnt--;
	CLK_TOPO_UNLOCK();
	return (rv);
}

int
clk_stop(clk_t clk)
{
	int rv;
	struct clknode *clknode;

	clknode = clk->clknode;
	KASSERT(clknode->ref_cnt > 0,
	   ("Attempt to access unreferenced clock: %s\n", clknode->name));
	KASSERT(clk->enable_cnt == 0,
	   ("Attempt to stop already enabled clock: %s\n", clknode->name));

	CLK_TOPO_SLOCK();
	rv = clknode_stop(clknode, 0);
	CLK_TOPO_UNLOCK();
	return (rv);
}

int
clk_release(clk_t clk)
{
	struct clknode *clknode;

	clknode = clk->clknode;
	KASSERT(clknode->ref_cnt > 0,
	   ("Attempt to access unreferenced clock: %s\n", clknode->name));
	CLK_TOPO_SLOCK();
	while (clk->enable_cnt > 0) {
		clknode_disable(clknode);
		clk->enable_cnt--;
	}
	CLKNODE_XLOCK(clknode);
	clknode->ref_cnt--;
	CLKNODE_UNLOCK(clknode);
	CLK_TOPO_UNLOCK();

	free(clk, M_CLOCK);
	return (0);
}

const char *
clk_get_name(clk_t clk)
{
	const char *name;
	struct clknode *clknode;

	clknode = clk->clknode;
	KASSERT(clknode->ref_cnt > 0,
	   ("Attempt to access unreferenced clock: %s\n", clknode->name));
	name = clknode_get_name(clknode);
	return (name);
}

int
clk_get_by_name(device_t dev, const char *name, clk_t *clk)
{
	struct clknode *clknode;

	CLK_TOPO_SLOCK();
	clknode = clknode_find_by_name(name);
	if (clknode == NULL) {
		CLK_TOPO_UNLOCK();
		return (ENODEV);
	}
	*clk = clk_create(clknode, dev);
	CLK_TOPO_UNLOCK();
	return (0);
}

int
clk_get_by_id(device_t dev, struct clkdom *clkdom, intptr_t id, clk_t *clk)
{
	struct clknode *clknode;

	CLK_TOPO_SLOCK();

	clknode = clknode_find_by_id(clkdom, id);
	if (clknode == NULL) {
		CLK_TOPO_UNLOCK();
		return (ENODEV);
	}
	*clk = clk_create(clknode, dev);
	CLK_TOPO_UNLOCK();

	return (0);
}

#ifdef FDT

static void
clk_set_assigned_parent(device_t dev, clk_t clk, int idx)
{
	clk_t parent;
	const char *pname;
	int rv;

	rv = clk_get_by_ofw_index_prop(dev, 0,
	    "assigned-clock-parents", idx, &parent);
	if (rv != 0) {
		device_printf(dev,
		    "cannot get parent at idx %d\n", idx);
		return;
	}

	pname = clk_get_name(parent);
	rv = clk_set_parent_by_clk(clk, parent);
	if (rv != 0)
		device_printf(dev,
		    "Cannot set parent %s for clock %s\n",
		    pname, clk_get_name(clk));
	else if (bootverbose)
		device_printf(dev, "Set %s as the parent of %s\n",
		    pname, clk_get_name(clk));
	clk_release(parent);
}

static void
clk_set_assigned_rates(device_t dev, clk_t clk, uint32_t freq)
{
	int rv;

	rv = clk_set_freq(clk, freq, CLK_SET_ROUND_DOWN | CLK_SET_ROUND_UP);
	if (rv != 0) {
		device_printf(dev, "Failed to set %s to a frequency of %u\n",
		    clk_get_name(clk), freq);
		return;
	}
	if (bootverbose)
		device_printf(dev, "Set %s to %u\n",
		    clk_get_name(clk), freq);
}

int
clk_set_assigned(device_t dev, phandle_t node)
{
	clk_t clk;
	uint32_t *rates;
	int rv, nclocks, nrates, nparents, i;

	rv = ofw_bus_parse_xref_list_get_length(node,
	    "assigned-clocks", "#clock-cells", &nclocks);

	if (rv != 0) {
		if (rv != ENOENT)
			device_printf(dev,
			    "cannot parse assigned-clock property\n");
		return (rv);
	}

	nrates = OF_getencprop_alloc_multi(node, "assigned-clock-rates",
	    sizeof(*rates), (void **)&rates);
	if (nrates <= 0)
		nrates = 0;

	if (ofw_bus_parse_xref_list_get_length(node,
	    "assigned-clock-parents", "#clock-cells", &nparents) != 0)
		nparents = -1;
	for (i = 0; i < nclocks; i++) {
		/* First get the clock we are supposed to modify */
		rv = clk_get_by_ofw_index_prop(dev, 0, "assigned-clocks",
		    i, &clk);
		if (rv != 0) {
			if (bootverbose)
				device_printf(dev,
				    "cannot get assigned clock at idx %d\n",
				    i);
			continue;
		}

		/* First set it's parent if needed */
		if (i <= nparents)
			clk_set_assigned_parent(dev, clk, i);

		/* Then set a new frequency */
		if (i <= nrates && rates[i] != 0)
			clk_set_assigned_rates(dev, clk, rates[i]);

		clk_release(clk);
	}

	return (0);
}

int
clk_get_by_ofw_index_prop(device_t dev, phandle_t cnode, const char *prop, int idx, clk_t *clk)
{
	phandle_t parent, *cells;
	device_t clockdev;
	int ncells, rv;
	struct clkdom *clkdom;
	struct clknode *clknode;

	*clk = NULL;
	if (cnode <= 0)
		cnode = ofw_bus_get_node(dev);
	if (cnode <= 0) {
		device_printf(dev, "%s called on not ofw based device\n",
		 __func__);
		return (ENXIO);
	}


	rv = ofw_bus_parse_xref_list_alloc(cnode, prop, "#clock-cells", idx,
	    &parent, &ncells, &cells);
	if (rv != 0) {
		return (rv);
	}

	clockdev = OF_device_from_xref(parent);
	if (clockdev == NULL) {
		rv = ENODEV;
		goto done;
	}

	CLK_TOPO_SLOCK();
	clkdom = clkdom_get_by_dev(clockdev);
	if (clkdom == NULL){
		CLK_TOPO_UNLOCK();
		rv = ENXIO;
		goto done;
	}

	rv = clkdom->ofw_mapper(clkdom, ncells, cells, &clknode);
	if (rv == 0) {
		*clk = clk_create(clknode, dev);
	}
	CLK_TOPO_UNLOCK();

done:
	if (cells != NULL)
		OF_prop_free(cells);
	return (rv);
}

int
clk_get_by_ofw_index(device_t dev, phandle_t cnode, int idx, clk_t *clk)
{
	return (clk_get_by_ofw_index_prop(dev, cnode, "clocks", idx, clk));
}

int
clk_get_by_ofw_name(device_t dev, phandle_t cnode, const char *name, clk_t *clk)
{
	int rv, idx;

	if (cnode <= 0)
		cnode = ofw_bus_get_node(dev);
	if (cnode <= 0) {
		device_printf(dev, "%s called on not ofw based device\n",
		 __func__);
		return (ENXIO);
	}
	rv = ofw_bus_find_string_index(cnode, "clock-names", name, &idx);
	if (rv != 0)
		return (rv);
	return (clk_get_by_ofw_index(dev, cnode, idx, clk));
}

/* --------------------------------------------------------------------------
 *
 * Support functions for parsing various clock related OFW things.
 */

/*
 * Get "clock-output-names" and  (optional) "clock-indices" lists.
 * Both lists are alocated using M_OFWPROP specifier.
 *
 * Returns number of items or 0.
 */
int
clk_parse_ofw_out_names(device_t dev, phandle_t node, const char ***out_names,
	uint32_t **indices)
{
	int name_items, rv;

	*out_names = NULL;
	*indices = NULL;
	if (!OF_hasprop(node, "clock-output-names"))
		return (0);
	rv = ofw_bus_string_list_to_array(node, "clock-output-names",
	    out_names);
	if (rv <= 0)
		return (0);
	name_items = rv;

	if (!OF_hasprop(node, "clock-indices"))
		return (name_items);
	rv = OF_getencprop_alloc_multi(node, "clock-indices", sizeof (uint32_t),
	    (void **)indices);
	if (rv != name_items) {
		device_printf(dev, " Size of 'clock-output-names' and "
		    "'clock-indices' differs\n");
		OF_prop_free(*out_names);
		OF_prop_free(*indices);
		return (0);
	}
	return (name_items);
}

/*
 * Get output clock name for single output clock node.
 */
int
clk_parse_ofw_clk_name(device_t dev, phandle_t node, const char **name)
{
	const char **out_names;
	const char  *tmp_name;
	int rv;

	*name = NULL;
	if (!OF_hasprop(node, "clock-output-names")) {
		tmp_name  = ofw_bus_get_name(dev);
		if (tmp_name == NULL)
			return (ENXIO);
		*name = strdup(tmp_name, M_OFWPROP);
		return (0);
	}
	rv = ofw_bus_string_list_to_array(node, "clock-output-names",
	    &out_names);
	if (rv != 1) {
		OF_prop_free(out_names);
		device_printf(dev, "Malformed 'clock-output-names' property\n");
		return (ENXIO);
	}
	*name = strdup(out_names[0], M_OFWPROP);
	OF_prop_free(out_names);
	return (0);
}
#endif

static int
clkdom_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct clkdom *clkdom = arg1;
	struct clknode *clknode;
	struct sbuf *sb;
	int ret;

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	CLK_TOPO_SLOCK();
	TAILQ_FOREACH(clknode, &clkdom->clknode_list, clkdom_link) {
		sbuf_printf(sb, "%s ", clknode->name);
	}
	CLK_TOPO_UNLOCK();

	ret = sbuf_finish(sb);
	sbuf_delete(sb);
	return (ret);
}

static int
clknode_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct clknode *clknode, *children;
	enum clknode_sysctl_type type = arg2;
	struct sbuf *sb;
	const char **parent_names;
	int ret, i;

	clknode = arg1;
	sb = sbuf_new_for_sysctl(NULL, NULL, 512, req);
	if (sb == NULL)
		return (ENOMEM);

	CLK_TOPO_SLOCK();
	switch (type) {
	case CLKNODE_SYSCTL_PARENT:
		if (clknode->parent)
			sbuf_printf(sb, "%s", clknode->parent->name);
		break;
	case CLKNODE_SYSCTL_PARENTS_LIST:
		parent_names = clknode_get_parent_names(clknode);
		for (i = 0; i < clknode->parent_cnt; i++)
			sbuf_printf(sb, "%s ", parent_names[i]);
		break;
	case CLKNODE_SYSCTL_CHILDREN_LIST:
		TAILQ_FOREACH(children, &(clknode->children), sibling_link) {
			sbuf_printf(sb, "%s ", children->name);
		}
		break;
	}
	CLK_TOPO_UNLOCK();

	ret = sbuf_finish(sb);
	sbuf_delete(sb);
	return (ret);
}
