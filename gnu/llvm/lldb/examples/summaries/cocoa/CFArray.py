"""
LLDB AppKit formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
# example summary provider for NSArray
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

# much less functional than the other two cases below
# just runs code to get to the count and then returns
# no children


class NSArrayKVC_SynthProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, dict, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    def num_children(self):
        logger = lldb.formatters.Logger.Logger()
        stream = lldb.SBStream()
        self.valobj.GetExpressionPath(stream)
        num_children_vo = self.valobj.CreateValueFromExpression(
            "count", "(int)[" + stream.GetData() + " count]"
        )
        if num_children_vo.IsValid():
            return num_children_vo.GetValueAsUnsigned(0)
        return "<variable is not NSArray>"


# much less functional than the other two cases below
# just runs code to get to the count and then returns
# no children


class NSArrayCF_SynthProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, dict, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        if not (self.sys_params.types_cache.ulong):
            self.sys_params.types_cache.ulong = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeUnsignedLong
            )
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    def num_children(self):
        logger = lldb.formatters.Logger.Logger()
        num_children_vo = self.valobj.CreateChildAtOffset(
            "count", self.sys_params.cfruntime_size, self.sys_params.types_cache.ulong
        )
        return num_children_vo.GetValueAsUnsigned(0)


class NSArrayI_SynthProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, dict, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        if not (self.sys_params.types_cache.long):
            self.sys_params.types_cache.long = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeLong
            )
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    # skip the isa pointer and get at the size
    def num_children(self):
        logger = lldb.formatters.Logger.Logger()
        count = self.valobj.CreateChildAtOffset(
            "count", self.sys_params.pointer_size, self.sys_params.types_cache.long
        )
        return count.GetValueAsUnsigned(0)


class NSArrayM_SynthProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, dict, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        if not (self.sys_params.types_cache.long):
            self.sys_params.types_cache.long = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeLong
            )
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    # skip the isa pointer and get at the size
    def num_children(self):
        logger = lldb.formatters.Logger.Logger()
        count = self.valobj.CreateChildAtOffset(
            "count", self.sys_params.pointer_size, self.sys_params.types_cache.long
        )
        return count.GetValueAsUnsigned(0)


# this is the actual synth provider, but is just a wrapper that checks
# whether valobj is an instance of __NSArrayI or __NSArrayM and sets up an
# appropriate backend layer to do the computations


class NSArray_SynthProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, dict):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.adjust_for_architecture()
        self.error = False
        self.wrapper = self.make_wrapper()
        self.invalid = self.wrapper is None

    def num_children(self):
        logger = lldb.formatters.Logger.Logger()
        if self.wrapper is None:
            return 0
        return self.wrapper.num_children()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        if self.wrapper is None:
            return
        self.wrapper.update()

    # this code acts as our defense against NULL and uninitialized
    # NSArray pointers, which makes it much longer than it would be otherwise
    def make_wrapper(self):
        logger = lldb.formatters.Logger.Logger()
        if self.valobj.GetValueAsUnsigned() == 0:
            self.error = True
            return lldb.runtime.objc.objc_runtime.InvalidPointer_Description(True)
        else:
            global statistics
            (
                class_data,
                wrapper,
            ) = lldb.runtime.objc.objc_runtime.Utilities.prepare_class_detection(
                self.valobj, statistics
            )
            if wrapper:
                self.error = True
                return wrapper

        name_string = class_data.class_name()

        logger >> "Class name is " + str(name_string)

        if name_string == "__NSArrayI":
            wrapper = NSArrayI_SynthProvider(self.valobj, dict, class_data.sys_params)
            statistics.metric_hit("code_notrun", self.valobj.GetName())
        elif name_string == "__NSArrayM":
            wrapper = NSArrayM_SynthProvider(self.valobj, dict, class_data.sys_params)
            statistics.metric_hit("code_notrun", self.valobj.GetName())
        elif name_string == "__NSCFArray":
            wrapper = NSArrayCF_SynthProvider(self.valobj, dict, class_data.sys_params)
            statistics.metric_hit("code_notrun", self.valobj.GetName())
        else:
            wrapper = NSArrayKVC_SynthProvider(self.valobj, dict, class_data.sys_params)
            statistics.metric_hit(
                "unknown_class", str(self.valobj.GetName()) + " seen as " + name_string
            )
        return wrapper


def CFArray_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    provider = NSArray_SynthProvider(valobj, dict)
    if not provider.invalid:
        if provider.error:
            return provider.wrapper.message()
        try:
            summary = int(provider.num_children())
        except:
            summary = None
        logger >> "provider gave me " + str(summary)
        if summary is None:
            summary = "<variable is not NSArray>"
        elif isinstance(summary, str):
            pass
        else:
            # we format it like it were a CFString to make it look the same as
            # the summary from Xcode
            summary = (
                '@"' + str(summary) + (" objects" if summary != 1 else " object") + '"'
            )
        return summary
    return "Summary Unavailable"


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand(
        "type summary add -F CFArray.CFArray_SummaryProvider NSArray CFArrayRef CFMutableArrayRef"
    )
