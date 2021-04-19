#
# gdb helper commands and functions for Linux kernel debugging
#
#  list tools
#
# Copyright (c) Thiebaud Weksteen, 2015
#
# Authors:
#  Thiebaud Weksteen <thiebaud@weksteen.fr>
#
# This work is licensed under the terms of the GNU GPL version 2.
#

import gdb

from linux import utils

list_head = utils.CachedType("struct list_head")
hlist_head = utils.CachedType("struct hlist_head")
hlist_node = utils.CachedType("struct hlist_node")


def list_for_each(head):
    if head.type == list_head.get_type().pointer():
        head = head.dereference()
    elif head.type != list_head.get_type():
        raise TypeError("Must be struct list_head not {}"
                           .format(head.type))

    if head['next'] == 0:
        gdb.write("list_for_each: Uninitialized list '{}' treated as empty\n"
                     .format(head.address))
        return

    node = head['next'].dereference()
    while node.address != head.address:
        yield node.address
        node = node['next'].dereference()


def list_for_each_entry(head, gdbtype, member):
    for node in list_for_each(head):
        yield utils.container_of(node, gdbtype, member)


def hlist_for_each(head):
    if head.type == hlist_head.get_type().pointer():
        head = head.dereference()
    elif head.type != hlist_head.get_type():
        raise TypeError("Must be struct hlist_head not {}"
                           .format(head.type))

    node = head['first'].dereference()
    while node.address:
        yield node.address
        node = node['next'].dereference()


def hlist_for_each_entry(head, gdbtype, member):
    for node in hlist_for_each(head):
        yield utils.container_of(node, gdbtype, member)


def list_check(head):
    nb = 0
    if (head.type == list_head.get_type().pointer()):
        head = head.dereference()
    elif (head.type != list_head.get_type()):
        raise gdb.GdbError('argument must be of type (struct list_head [*])')
    c = head
    try:
        gdb.write("Starting with: {}\n".format(c))
    except gdb.MemoryError:
        gdb.write('head is not accessible\n')
        return
    while True:
        p = c['prev'].dereference()
        n = c['next'].dereference()
        try:
            if p['next'] != c.address:
                gdb.write('prev.next != current: '
                          'current@{current_addr}={current} '
                          'prev@{p_addr}={p}\n'.format(
                              current_addr=c.address,
                              current=c,
                              p_addr=p.address,
                              p=p,
                          ))
                return
        except gdb.MemoryError:
            gdb.write('prev is not accessible: '
                      'current@{current_addr}={current}\n'.format(
                          current_addr=c.address,
                          current=c
                      ))
            return
        try:
            if n['prev'] != c.address:
                gdb.write('next.prev != current: '
                          'current@{current_addr}={current} '
                          'next@{n_addr}={n}\n'.format(
                              current_addr=c.address,
                              current=c,
                              n_addr=n.address,
                              n=n,
                          ))
                return
        except gdb.MemoryError:
            gdb.write('next is not accessible: '
                      'current@{current_addr}={current}\n'.format(
                          current_addr=c.address,
                          current=c
                      ))
            return
        c = n
        nb += 1
        if c == head:
            gdb.write("list is consistent: {} node(s)\n".format(nb))
            return


class LxListChk(gdb.Command):
    """Verify a list consistency"""

    def __init__(self):
        super(LxListChk, self).__init__("lx-list-check", gdb.COMMAND_DATA,
                                        gdb.COMPLETE_EXPRESSION)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        if len(argv) != 1:
            raise gdb.GdbError("lx-list-check takes one argument")
        list_check(gdb.parse_and_eval(argv[0]))


LxListChk()
