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

    def __next__(self):
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

    def next(self):
        return self.__next__()

def get_task_by_pid(pid):
    for task in TaskList():
        if int(task['pid']) == pid:
            return task
    return None


class LxTaskByPidFunc(gdb.Function):
    """Find Linux task by PID and return the task_struct variable.

$lx_task_by_pid(PID): Given PID, iterate over all tasks of the target and
return that task_struct variable which PID matches."""

    def __init__(self):
        super(LxTaskByPidFunc, self).__init__("lx_task_by_pid")

    def invoke(self, pid):
        task = get_task_by_pid(pid)
        if task:
            return task.dereference()
        else:
            raise gdb.GdbError("No task of PID " + str(pid))


LxTaskByPidFunc()


thread_info_type = utils.CachedType("struct thread_info")

ia64_task_size = None


def get_thread_info(task):
    global thread_info_type
    thread_info_ptr_type = thread_info_type.get_type().pointer()
    if utils.is_target_arch("ia64"):
        global ia64_task_size
        if ia64_task_size is None:
            ia64_task_size = gdb.parse_and_eval("sizeof(struct task_struct)")
        thread_info_addr = task.address + ia64_task_size
        thread_info = thread_info_addr.cast(thread_info_ptr_type)
    else:
        thread_info = task['stack'].cast(thread_info_ptr_type)
    return thread_info.dereference()


class LxThreadInfoFunc (gdb.Function):
    """Calculate Linux thread_info from task variable.

$lx_thread_info(TASK): Given TASK, return the corresponding thread_info
variable."""

    def __init__(self):
        super(LxThreadInfoFunc, self).__init__("lx_thread_info")

    def invoke(self, task):
        return get_thread_info(task)


LxThreadInfoFunc()
