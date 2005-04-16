#ifndef _ASM_I386_NODE_H_
#define _ASM_I386_NODE_H_

#include <linux/device.h>
#include <linux/mmzone.h>
#include <linux/node.h>
#include <linux/topology.h>
#include <linux/nodemask.h>

struct i386_node {
	struct node node;
};
extern struct i386_node node_devices[MAX_NUMNODES];

static inline int arch_register_node(int num){
	int p_node;
	struct node *parent = NULL;

	if (!node_online(num))
		return 0;
	p_node = parent_node(num);

	if (p_node != num)
		parent = &node_devices[p_node].node;

	return register_node(&node_devices[num].node, num, parent);
}

#endif /* _ASM_I386_NODE_H_ */
