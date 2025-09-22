"""
LLDB AppKit formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
# example summary provider for NS(Mutable)IndexSet
# the real summary is now C++ code built into LLDB
import lldb
import ctypes
import lldb.runtime.objc.objc_runtime
import lldb.formatters.metrics
import lldb.formatters.Logger

statistics = lldb.formatters.metrics.Metrics()
statistics.add_metric("invalid_isa")
statistics.add_metric("invalid_pointer")
statistics.add_metric("unknown_class")
statistics.add_metric("code_notrun")

# despite the similary to synthetic children providers, these classes are not
# trying to provide anything but the count of values for an NSIndexSet, so they need not
# obey the interface specification for synthetic children providers


class NSIndexSetClass_SummaryProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        if not (self.sys_params.types_cache.NSUInteger):
            if self.sys_params.is_64_bit:
                self.sys_params.types_cache.NSUInteger = (
                    self.valobj.GetType().GetBasicType(lldb.eBasicTypeUnsignedLong)
                )
                self.sys_params.types_cache.uint32 = self.valobj.GetType().GetBasicType(
                    lldb.eBasicTypeUnsignedInt
                )
            else:
                self.sys_params.types_cache.NSUInteger = (
                    self.valobj.GetType().GetBasicType(lldb.eBasicTypeUnsignedInt)
                )
                self.sys_params.types_cache.uint32 = self.valobj.GetType().GetBasicType(
                    lldb.eBasicTypeUnsignedInt
                )
        if not (self.sys_params.types_cache.uint32):
            self.sys_params.types_cache.uint32 = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeUnsignedInt
            )
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    # NS(Mutable)IndexSet works in one of two modes: when having a compact block of data (e.g. a Range)
    # the count is stored in the set itself, 3 pointers into it
    # otherwise, it will store a pointer to an additional data structure (2 pointers into itself) and this
    # additional structure will contain the count two pointers deep
    # a bunch of flags allow us to detect an empty set, vs. a one-range set,
    # vs. a multi-range set
    def count(self):
        logger = lldb.formatters.Logger.Logger()
        mode_chooser_vo = self.valobj.CreateChildAtOffset(
            "mode_chooser",
            self.sys_params.pointer_size,
            self.sys_params.types_cache.uint32,
        )
        mode_chooser = mode_chooser_vo.GetValueAsUnsigned(0)
        if self.sys_params.is_64_bit:
            mode_chooser = mode_chooser & 0x00000000FFFFFFFF
        # empty set
        if mode_chooser & 0x01 == 1:
            return 0
        # single range
        if mode_chooser & 0x02 == 2:
            mode = 1
        # multi range
        else:
            mode = 2
        if mode == 1:
            count_vo = self.valobj.CreateChildAtOffset(
                "count",
                3 * self.sys_params.pointer_size,
                self.sys_params.types_cache.NSUInteger,
            )
        else:
            count_ptr = self.valobj.CreateChildAtOffset(
                "count_ptr",
                2 * self.sys_params.pointer_size,
                self.sys_params.types_cache.NSUInteger,
            )
            count_vo = self.valobj.CreateValueFromAddress(
                "count",
                count_ptr.GetValueAsUnsigned() + 2 * self.sys_params.pointer_size,
                self.sys_params.types_cache.NSUInteger,
            )
        return count_vo.GetValueAsUnsigned(0)


class NSIndexSetUnknown_SummaryProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    def count(self):
        logger = lldb.formatters.Logger.Logger()
        stream = lldb.SBStream()
        self.valobj.GetExpressionPath(stream)
        expr = "(int)[" + stream.GetData() + " count]"
        num_children_vo = self.valobj.CreateValueFromExpression("count", expr)
        if num_children_vo.IsValid():
            return num_children_vo.GetValueAsUnsigned(0)
        return "<variable is not NSIndexSet>"


def GetSummary_Impl(valobj):
    logger = lldb.formatters.Logger.Logger()
    global statistics
    (
        class_data,
        wrapper,
    ) = lldb.runtime.objc.objc_runtime.Utilities.prepare_class_detection(
        valobj, statistics
    )
    if wrapper:
        return wrapper

    name_string = class_data.class_name()
    logger >> "class name is: " + str(name_string)

    if name_string == "NSIndexSet" or name_string == "NSMutableIndexSet":
        wrapper = NSIndexSetClass_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit("code_notrun", valobj)
    else:
        wrapper = NSIndexSetUnknown_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit(
            "unknown_class", valobj.GetName() + " seen as " + name_string
        )
    return wrapper


def NSIndexSet_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    provider = GetSummary_Impl(valobj)
    if provider is not None:
        if isinstance(
            provider, lldb.runtime.objc.objc_runtime.SpecialSituation_Description
        ):
            return provider.message()
        try:
            summary = provider.count()
        except:
            summary = None
        logger >> "got summary " + str(summary)
        if summary is None:
            summary = "<variable is not NSIndexSet>"
        if isinstance(summary, str):
            return summary
        else:
            summary = str(summary) + (" indexes" if summary != 1 else " index")
        return summary
    return "Summary Unavailable"


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand(
        "type summary add -F NSIndexSet.NSIndexSet_SummaryProvider NSIndexSet NSMutableIndexSet"
    )
