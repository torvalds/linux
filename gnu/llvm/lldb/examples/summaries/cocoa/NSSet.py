"""
LLDB AppKit formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
# summary provider for NSSet
import lldb
import ctypes
import lldb.runtime.objc.objc_runtime
import lldb.formatters.metrics
import CFBag
import lldb.formatters.Logger

statistics = lldb.formatters.metrics.Metrics()
statistics.add_metric("invalid_isa")
statistics.add_metric("invalid_pointer")
statistics.add_metric("unknown_class")
statistics.add_metric("code_notrun")

# despite the similary to synthetic children providers, these classes are not
# trying to provide anything but the port number of an NSMachPort, so they need not
# obey the interface specification for synthetic children providers


class NSCFSet_SummaryProvider:
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
            else:
                self.sys_params.types_cache.NSUInteger = (
                    self.valobj.GetType().GetBasicType(lldb.eBasicTypeUnsignedInt)
                )
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    # one pointer is the ISA
    # then we have one other internal pointer, plus
    # 4 bytes worth of flags. hence, these values
    def offset(self):
        logger = lldb.formatters.Logger.Logger()
        if self.sys_params.is_64_bit:
            return 20
        else:
            return 12

    def count(self):
        logger = lldb.formatters.Logger.Logger()
        vcount = self.valobj.CreateChildAtOffset(
            "count", self.offset(), self.sys_params.types_cache.NSUInteger
        )
        return vcount.GetValueAsUnsigned(0)


class NSSetUnknown_SummaryProvider:
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
        return "<variable is not NSSet>"


class NSSetI_SummaryProvider:
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
            else:
                self.sys_params.types_cache.NSUInteger = (
                    self.valobj.GetType().GetBasicType(lldb.eBasicTypeUnsignedInt)
                )
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    # we just need to skip the ISA and the count immediately follows
    def offset(self):
        logger = lldb.formatters.Logger.Logger()
        return self.sys_params.pointer_size

    def count(self):
        logger = lldb.formatters.Logger.Logger()
        num_children_vo = self.valobj.CreateChildAtOffset(
            "count", self.offset(), self.sys_params.types_cache.NSUInteger
        )
        value = num_children_vo.GetValueAsUnsigned(0)
        if value is not None:
            # the MSB on immutable sets seems to be taken by some other data
            # not sure if it is a bug or some weird sort of feature, but masking it out
            # gets the count right (unless, of course, someone's dictionaries grow
            #                       too large - but I have not tested this)
            if self.sys_params.is_64_bit:
                value = value & ~0xFF00000000000000
            else:
                value = value & ~0xFF000000
        return value


class NSSetM_SummaryProvider:
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
            else:
                self.sys_params.types_cache.NSUInteger = (
                    self.valobj.GetType().GetBasicType(lldb.eBasicTypeUnsignedInt)
                )
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    # we just need to skip the ISA and the count immediately follows
    def offset(self):
        logger = lldb.formatters.Logger.Logger()
        return self.sys_params.pointer_size

    def count(self):
        logger = lldb.formatters.Logger.Logger()
        num_children_vo = self.valobj.CreateChildAtOffset(
            "count", self.offset(), self.sys_params.types_cache.NSUInteger
        )
        return num_children_vo.GetValueAsUnsigned(0)


class NSCountedSet_SummaryProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        if not (self.sys_params.types_cache.voidptr):
            self.sys_params.types_cache.voidptr = (
                self.valobj.GetType().GetBasicType(lldb.eBasicTypeVoid).GetPointerType()
            )
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    # an NSCountedSet is implemented using a CFBag whose pointer just follows
    # the ISA
    def offset(self):
        logger = lldb.formatters.Logger.Logger()
        return self.sys_params.pointer_size

    def count(self):
        logger = lldb.formatters.Logger.Logger()
        cfbag_vo = self.valobj.CreateChildAtOffset(
            "bag_impl", self.offset(), self.sys_params.types_cache.voidptr
        )
        return CFBag.CFBagRef_SummaryProvider(cfbag_vo, self.sys_params).length()


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

    if name_string == "__NSCFSet":
        wrapper = NSCFSet_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit("code_notrun", valobj)
    elif name_string == "__NSSetI":
        wrapper = NSSetI_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit("code_notrun", valobj)
    elif name_string == "__NSSetM":
        wrapper = NSSetM_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit("code_notrun", valobj)
    elif name_string == "NSCountedSet":
        wrapper = NSCountedSet_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit("code_notrun", valobj)
    else:
        wrapper = NSSetUnknown_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit(
            "unknown_class", valobj.GetName() + " seen as " + name_string
        )
    return wrapper


def NSSet_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    provider = GetSummary_Impl(valobj)
    if provider is not None:
        try:
            summary = provider.count()
        except:
            summary = None
        if summary is None:
            summary = "<variable is not NSSet>"
        if isinstance(summary, str):
            return summary
        else:
            summary = str(summary) + (" objects" if summary != 1 else " object")
        return summary
    return "Summary Unavailable"


def NSSet_SummaryProvider2(valobj, dict):
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
        # for some reason, one needs to clear some bits for the count returned
        # to be correct when using directly CF*SetRef as compared to NS*Set
        # this only happens on 64bit, and the bit mask was derived through
        # experimentation (if counts start looking weird, then most probably
        #                  the mask needs to be changed)
        if summary is None:
            summary = "<variable is not CFSet>"
        if isinstance(summary, str):
            return summary
        else:
            if provider.sys_params.is_64_bit:
                summary = summary & ~0x1FFF000000000000
            summary = '@"' + str(summary) + (' values"' if summary != 1 else ' value"')
        return summary
    return "Summary Unavailable"


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand("type summary add -F NSSet.NSSet_SummaryProvider NSSet")
    debugger.HandleCommand(
        "type summary add -F NSSet.NSSet_SummaryProvider2 CFSetRef CFMutableSetRef"
    )
