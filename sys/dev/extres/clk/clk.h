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
 *
 * $FreeBSD$
 */

#ifndef _DEV_EXTRES_CLK_H_
#define _DEV_EXTRES_CLK_H_
#include "opt_platform.h"

#include <sys/kobj.h>
#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#endif
#include "clknode_if.h"

#define CLKNODE_IDX_NONE	-1		/* Not-selected index */

/* clknode flags. */
#define	CLK_NODE_STATIC_STRINGS	0x00000001	/* Static name strings */
#define	CLK_NODE_GLITCH_FREE	0x00000002	/* Freq can change w/o stop */
#define	CLK_NODE_CANNOT_STOP	0x00000004	/* Clock cannot be disabled */

/* Flags passed to clk_set_freq() and clknode_set_freq(). */
#define	CLK_SET_ROUND(x)	((x) & (CLK_SET_ROUND_UP | CLK_SET_ROUND_DOWN))
#define	CLK_SET_ROUND_EXACT	0
#define	CLK_SET_ROUND_UP	0x00000001
#define	CLK_SET_ROUND_DOWN	0x00000002
#define	CLK_SET_ROUND_ANY	(CLK_SET_ROUND_UP | CLK_SET_ROUND_DOWN)

#define	CLK_SET_USER_MASK	0x0000FFFF
#define	CLK_SET_DRYRUN		0x00010000

typedef struct clk *clk_t;

/* Initialization parameters for clocknode creation. */
struct clknode_init_def {
	const char	*name;
	intptr_t	id;
	const char	**parent_names;
	int		parent_cnt;
	int		flags;
};

/*
 * Shorthands for constructing method tables.
 */
#define	CLKNODEMETHOD		KOBJMETHOD
#define	CLKNODEMETHOD_END	KOBJMETHOD_END
#define	clknode_method_t	kobj_method_t
#define	clknode_class_t		kobj_class_t
DECLARE_CLASS(clknode_class);

/*
 *  Clock domain functions.
 */
struct clkdom *clkdom_create(device_t dev);
int clkdom_finit(struct clkdom *clkdom);
void clkdom_dump(struct clkdom * clkdom);
void clkdom_unlock(struct clkdom *clkdom);
void clkdom_xlock(struct clkdom *clkdom);

/*
 * Clock providers interface.
 */
struct clkdom *clkdom_get_by_dev(const device_t dev);

struct clknode *clknode_create(struct clkdom *clkdom,
    clknode_class_t clknode_class, const struct clknode_init_def *def);
struct clknode *clknode_register(struct clkdom *cldom, struct clknode *clk);
#ifdef FDT
typedef int clknode_ofw_mapper_func(struct clkdom *clkdom, uint32_t ncells,
    phandle_t *cells, struct clknode **clk);
void clkdom_set_ofw_mapper(struct clkdom *clkdom, clknode_ofw_mapper_func *cmp);
#endif

void clknode_init_parent_idx(struct clknode *clknode, int idx);
int clknode_set_parent_by_idx(struct clknode *clk, int idx);
int clknode_set_parent_by_name(struct clknode *clk, const char *name);
const char *clknode_get_name(struct clknode *clk);
const char **clknode_get_parent_names(struct clknode *clk);
int clknode_get_parents_num(struct clknode *clk);
int clknode_get_parent_idx(struct clknode *clk);
struct clknode *clknode_get_parent(struct clknode *clk);
int clknode_get_flags(struct clknode *clk);
void *clknode_get_softc(struct clknode *clk);
device_t clknode_get_device(struct clknode *clk);
struct clknode *clknode_find_by_name(const char *name);
struct clknode *clknode_find_by_id(struct clkdom *clkdom, intptr_t id);
int clknode_get_freq(struct clknode *clknode, uint64_t *freq);
int clknode_set_freq(struct clknode *clknode, uint64_t freq, int flags,
    int enablecnt);
int clknode_enable(struct clknode *clknode);
int clknode_disable(struct clknode *clknode);
int clknode_stop(struct clknode *clknode, int depth);

/*
 * Clock consumers interface.
 */
int clk_get_by_name(device_t dev, const char *name, clk_t *clk);
int clk_get_by_id(device_t dev, struct clkdom *clkdom, intptr_t id, clk_t *clk);
int clk_release(clk_t clk);
int clk_get_freq(clk_t clk, uint64_t *freq);
int clk_set_freq(clk_t clk, uint64_t freq, int flags);
int clk_test_freq(clk_t clk, uint64_t freq, int flags);
int clk_enable(clk_t clk);
int clk_disable(clk_t clk);
int clk_stop(clk_t clk);
int clk_get_parent(clk_t clk, clk_t *parent);
int clk_set_parent_by_clk(clk_t clk, clk_t parent);
const char *clk_get_name(clk_t clk);

#ifdef FDT
int clk_set_assigned(device_t dev, phandle_t node);
int clk_get_by_ofw_index(device_t dev, phandle_t node, int idx, clk_t *clk);
int clk_get_by_ofw_index_prop(device_t dev, phandle_t cnode, const char *prop, int idx, clk_t *clk);
int clk_get_by_ofw_name(device_t dev, phandle_t node, const char *name,
     clk_t *clk);
int clk_parse_ofw_out_names(device_t dev, phandle_t node,
    const char ***out_names, uint32_t **indices);
int clk_parse_ofw_clk_name(device_t dev, phandle_t node, const char **name);
#endif

#endif /* _DEV_EXTRES_CLK_H_ */
