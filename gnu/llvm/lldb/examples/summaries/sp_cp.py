"""
Summary and synthetic providers for LLDB-specific shared pointers

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""


class SharedPtr_SyntheticChildrenProvider:
    def __init__(self, valobj, dict):
        self.valobj = valobj
        self.update()

    def update(self):
        pass

    def num_children(self):
        return 1

    def get_child_index(self, name):
        if name == "ptr":
            return 0
        if name == "count":
            return 1
        return None

    def get_child_at_index(self, index):
        if index == 0:
            return self.valobj.GetChildMemberWithName("_M_ptr")
        if index == 1:
            return (
                self.valobj.GetChildMemberWithName("_M_refcount")
                .GetChildMemberWithName("_M_pi")
                .GetChildMemberWithName("_M_use_count")
            )
        return None


def SharedPtr_SummaryProvider(valobj, dict):
    return "use = " + str(valobj.GetChildMemberWithName("count").GetValueAsUnsigned())


class ValueObjectSP_SyntheticChildrenProvider:
    def __init__(self, valobj, dict):
        self.valobj = valobj
        self.update()

    def update(self):
        pass

    def num_children(self):
        return 1

    def get_child_index(self, name):
        if name == "ptr":
            return 0
        if name == "count":
            return 1
        return None

    def get_child_at_index(self, index):
        if index == 0:
            return self.valobj.GetChildMemberWithName("ptr_")
        if index == 1:
            return self.valobj.GetChildMemberWithName("cntrl_").GetChildMemberWithName(
                "shared_owners_"
            )
        return None


def ValueObjectSP_SummaryProvider(valobj, dict):
    return "use = " + str(
        1 + valobj.GetChildMemberWithName("count").GetValueAsUnsigned()
    )


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand(
        'type summary add -x ".*ValueObjectSP" --expand -F sp_cp.ValueObjectSP_SummaryProvider'
    )
    debugger.HandleCommand(
        'type synthetic add -x ".*ValueObjectSP" -l sp_cp.ValueObjectSP_SyntheticChildrenProvider'
    )
    debugger.HandleCommand(
        'type summary add -x ".*SP" --expand -F sp_cp.SharedPtr_SummaryProvider'
    )
    debugger.HandleCommand(
        'type synthetic add -x ".*SP" -l sp_cp.SharedPtr_SyntheticChildrenProvider'
    )
