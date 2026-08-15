#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Draw the timer migration tree.

1) Boot with trace_event==tmigr_connect_cpu_parent,tmigr_connect_child_parent
2) ./timer_migration_tree.py < /sys/kernel/tracing/trace
"""

import re, sys
from ete3 import Tree

class Node:
	def __init__(self, group):
		self.group = group
		self.children = []
		self.parent = None
		self.num_children = 0
		self.groupmask = 0
		self.lvl = -1

	def set_groupmask(self, groupmask):
		self.groupmask = groupmask

	def set_parent(self, parent):
		self.parent = parent

	def add_child(self, child):
		self.children.append(child)

	def set_lvl(self, lvl):
		self.lvl = lvl

	def set_numa(self, numa):
		self.numa = numa

	def set_num_children(self, num_children):
		self.num_children = num_children

	def __repr__(self):
		if self.parent:
			parent_grp = self.parent.group
		else:
			parent_grp = "-"
		return "Group: %s mask: %s parent: %s lvl: %d numa: %d num_children: %d" % (self.group, self.groupmask, parent_grp, self.lvl, self.numa, self.num_children)

hierarchies = { }

def get_hierarchy(capacity):
	if capacity not in hierarchies:
		hierarchies[capacity] = {}
	return hierarchies[capacity]

def get_node(capacity, group):
	hier = get_hierarchy(capacity)
	if group in hier:
		return hier[group]
	else:
		n = Node(group)
		hier[group] = n
		return n

def tmigr_connect_cpu_parent(ts, line):
	s = re.search("tmigr_connect_cpu_parent: cpu=([0-9]+) groupmask=([0-9a-zA-Z]+) parent=([0-9a-zA-Z]+) lvl=([0-9]+) numa=([-]?[0-9]+) capacity=([-]?[0-9]+) num_children=([0-9]+)", line)
	if s is None:
		return False
	(cpu, groupmask, parent, lvl, numa, capacity, num_children) = (int(s.group(1)), s.group(2), s.group(3), int(s.group(4)), int(s.group(5)), int(s.group(6)), int(s.group(7)))
	n = get_node(capacity, cpu)
	p = get_node(capacity, parent)
	n.set_parent(p)
	n.set_groupmask(groupmask)
	n.set_lvl(-1)
	p.set_lvl(lvl)
	p.set_numa(numa)
	n.set_numa(numa)
	p.set_num_children(num_children)
	p.add_child(n)

def tmigr_connect_child_parent(ts, line):
	s = re.search("tmigr_connect_child_parent: group=([0-9a-zA-Z]+) groupmask=([0-9a-zA-Z]+) parent=([0-9a-zA-Z]+) lvl=([0-9]+) numa=([-]?[0-9]+) capacity=([-]?[0-9]+) num_children=([0-9]+)", line)
	if s is None:
		return False
	(group, groupmask, parent, lvl, numa, capacity, num_children) = (s.group(1), s.group(2), s.group(3), int(s.group(4)), int(s.group(5)), int(s.group(6)), int(s.group(7)))
	n = get_node(capacity, group)
	p = get_node(capacity, parent)
	n.set_parent(p)
	n.set_groupmask(groupmask)
	p.set_lvl(lvl)
	p.set_numa(numa)
	p.set_num_children(num_children)
	p.add_child(n)

def populate(enode, node):
	enode = enode.add_child(name = node.group)
	enode.add_feature("groupmask", "m:%s" % node.groupmask)
	enode.add_feature("lvl", "lvl:%d" % node.lvl)
	enode.add_feature("numa", "node %d" % node.numa)
	enode.add_feature("num_children", "c=%d" % node.num_children)
	for child in node.children:
		populate(enode, child)

if __name__ == "__main__":
	for line in sys.stdin:
		s = re.search("([0-9]+[.][0-9]{6}): (.+?)$", line, re.S)
		if s is not None:
			if tmigr_connect_cpu_parent(float(s.group(1)), s.group(2)):
				continue
			if tmigr_connect_child_parent(float(s.group(1)), s.group(2)):
				continue

	for cap in hierarchies:
		h = hierarchies[cap]
		print("Tree for capacity %d" % cap)
		for k in h:
			n = h[k]
			while n.parent != None:
				n = n.parent
			root = Tree()
			populate(root, n)
			print(root.get_ascii(show_internal=True, attributes=["name", "numa", "lvl"]))
			break
