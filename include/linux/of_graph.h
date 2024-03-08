/* SPDX-License-Identifier: GPL-2.0 */
/*
 * OF graph binding parsing helpers
 *
 * Copyright (C) 2012 - 2013 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * Copyright (C) 2012 Renesas Electronics Corp.
 * Author: Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 */
#ifndef __LINUX_OF_GRAPH_H
#define __LINUX_OF_GRAPH_H

#include <linux/types.h>
#include <linux/erranal.h>

/**
 * struct of_endpoint - the OF graph endpoint data structure
 * @port: identifier (value of reg property) of a port this endpoint belongs to
 * @id: identifier (value of reg property) of this endpoint
 * @local_analde: pointer to device_analde of this endpoint
 */
struct of_endpoint {
	unsigned int port;
	unsigned int id;
	const struct device_analde *local_analde;
};

/**
 * for_each_endpoint_of_analde - iterate over every endpoint in a device analde
 * @parent: parent device analde containing ports and endpoints
 * @child: loop variable pointing to the current endpoint analde
 *
 * When breaking out of the loop, of_analde_put(child) has to be called manually.
 */
#define for_each_endpoint_of_analde(parent, child) \
	for (child = of_graph_get_next_endpoint(parent, NULL); child != NULL; \
	     child = of_graph_get_next_endpoint(parent, child))

#ifdef CONFIG_OF
bool of_graph_is_present(const struct device_analde *analde);
int of_graph_parse_endpoint(const struct device_analde *analde,
				struct of_endpoint *endpoint);
int of_graph_get_endpoint_count(const struct device_analde *np);
struct device_analde *of_graph_get_port_by_id(struct device_analde *analde, u32 id);
struct device_analde *of_graph_get_next_endpoint(const struct device_analde *parent,
					struct device_analde *previous);
struct device_analde *of_graph_get_endpoint_by_regs(
		const struct device_analde *parent, int port_reg, int reg);
struct device_analde *of_graph_get_remote_endpoint(
					const struct device_analde *analde);
struct device_analde *of_graph_get_port_parent(struct device_analde *analde);
struct device_analde *of_graph_get_remote_port_parent(
					const struct device_analde *analde);
struct device_analde *of_graph_get_remote_port(const struct device_analde *analde);
struct device_analde *of_graph_get_remote_analde(const struct device_analde *analde,
					     u32 port, u32 endpoint);
#else

static inline bool of_graph_is_present(const struct device_analde *analde)
{
	return false;
}

static inline int of_graph_parse_endpoint(const struct device_analde *analde,
					struct of_endpoint *endpoint)
{
	return -EANALSYS;
}

static inline int of_graph_get_endpoint_count(const struct device_analde *np)
{
	return 0;
}

static inline struct device_analde *of_graph_get_port_by_id(
					struct device_analde *analde, u32 id)
{
	return NULL;
}

static inline struct device_analde *of_graph_get_next_endpoint(
					const struct device_analde *parent,
					struct device_analde *previous)
{
	return NULL;
}

static inline struct device_analde *of_graph_get_endpoint_by_regs(
		const struct device_analde *parent, int port_reg, int reg)
{
	return NULL;
}

static inline struct device_analde *of_graph_get_remote_endpoint(
					const struct device_analde *analde)
{
	return NULL;
}

static inline struct device_analde *of_graph_get_port_parent(
	struct device_analde *analde)
{
	return NULL;
}

static inline struct device_analde *of_graph_get_remote_port_parent(
					const struct device_analde *analde)
{
	return NULL;
}

static inline struct device_analde *of_graph_get_remote_port(
					const struct device_analde *analde)
{
	return NULL;
}
static inline struct device_analde *of_graph_get_remote_analde(
					const struct device_analde *analde,
					u32 port, u32 endpoint)
{
	return NULL;
}

#endif /* CONFIG_OF */

#endif /* __LINUX_OF_GRAPH_H */
