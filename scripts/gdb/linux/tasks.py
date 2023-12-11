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

from linux import utils, lists


task_type = utils.CachedType("struct task_struct")


def task_lists():
    task_ptr_type = task_type.get_type().pointer()
    init_task = gdb.parse_and_eval("init_task").address
    t = init_task

    while True:
        thread_head = t['signal']['thread_head']
        for thread in lists.list_for_each_entry(thread_head, task_ptr_type, 'thread_node'):
            yield thread

        t = utils.container_of(t['tasks']['next'],
                               task_ptr_type, "tasks")
        if t == init_task:
            return


def get_task_by_pid(pid):
    for task in task_lists():
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


class LxPs(gdb.Command):
    """Dump Linux tasks."""

    def __init__(self):
        super(LxPs, self).__init__("lx-ps", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        gdb.write("{:>10} {:>12} {:>7}\n".format("TASK", "PID", "COMM"))
        for task in task_lists():
            gdb.write("{} {:^5} {}\n".format(
                task.format_string().split()[0],
                task["pid"].format_string(),
                task["comm"].string()))


LxPs()


thread_info_type = utils.CachedType("struct thread_info")

ia64_task_size = None


def get_thread_info(task):
    thread_info_ptr_type = thread_info_type.get_type().pointer()
    if utils.is_target_arch("ia64"):
        global ia64_task_size
        if ia64_task_size is None:
            ia64_task_size = gdb.parse_and_eval("sizeof(struct task_struct)")
        thread_info_addr = task.address + ia64_task_size
        thread_info = thread_info_addr.cast(thread_info_ptr_type)
    else:
        if task.type.fields()[0].type == thread_info_type.get_type():
            return task['thread_info']
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


class LxThreadInfoByPidFunc (gdb.Function):
    """Calculate Linux thread_info from task variable found by pid

$lx_thread_info_by_pid(PID): Given PID, return the corresponding thread_info
variable."""

    def __init__(self):
        super(LxThreadInfoByPidFunc, self).__init__("lx_thread_info_by_pid")

    def invoke(self, pid):
        task = get_task_by_pid(pid)
        if task:
            return get_thread_info(task.dereference())
        else:
            raise gdb.GdbError("No task of PID " + str(pid))


LxThreadInfoByPidFunc()
