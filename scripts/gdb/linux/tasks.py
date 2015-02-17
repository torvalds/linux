#
# gdb helper commands and functions for Linux kernel debugging
#
#  task & thread tools
#
# Copyright (c) Siemens AG, 2011-2013
#
# Authors:
#  Jan Kiszka <jan.kiszka@siemens.com>
#
# This work is licensed under the terms of the GNU GPL version 2.
#

import gdb

from linux import utils


task_type = utils.CachedType("struct task_struct")


class TaskList:
    def __init__(self):
        global task_type
        self.task_ptr_type = task_type.get_type().pointer()
        self.init_task = gdb.parse_and_eval("init_task")
        self.curr_group = self.init_task.address
        self.curr_task = None

    def __iter__(self):
        return self

    def next(self):
        t = self.curr_task
        if not t or t == self.curr_group:
            self.curr_group = \
                utils.container_of(self.curr_group['tasks']['next'],
                                   self.task_ptr_type, "tasks")
            if self.curr_group == self.init_task.address:
                raise StopIteration
            t = self.curr_task = self.curr_group
        else:
            self.curr_task = \
                utils.container_of(t['thread_group']['next'],
                                   self.task_ptr_type, "thread_group")
        return t
