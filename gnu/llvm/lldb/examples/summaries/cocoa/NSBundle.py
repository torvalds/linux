"""
LLDB AppKit formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
# example summary provider for NSBundle
# the real summary is now C++ code built into LLDB
import lldb
import ctypes
import lldb.runtime.objc.objc_runtime
import lldb.formatters.metrics
import NSURL
import lldb.formatters.Logger

statistics = lldb.formatters.metrics.Metrics()
statistics.add_metric("invalid_isa")
statistics.add_metric("invalid_pointer")
statistics.add_metric("unknown_class")
statistics.add_metric("code_notrun")

# despite the similary to synthetic children providers, these classes are not
# trying to provide anything but a summary for an NSURL, so they need not
# obey the interface specification for synthetic children providers


class NSBundleKnown_SummaryProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        if not (self.sys_params.types_cache.NSString):
            self.sys_params.types_cache.NSString = (
                self.valobj.GetTarget().FindFirstType("NSString").GetPointerType()
            )
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    # we need to skip the ISA, plus four other values
    # that are luckily each a pointer in size
    # which makes our computation trivial :-)
    def offset(self):
        logger = lldb.formatters.Logger.Logger()
        return 5 * self.sys_params.pointer_size

    def url_text(self):
        logger = lldb.formatters.Logger.Logger()
        global statistics
        text = self.valobj.CreateChildAtOffset(
            "text", self.offset(), self.sys_params.types_cache.NSString
        )
        my_string = text.GetSummary()
        if (my_string is None) or (my_string == ""):
            statistics.metric_hit(
                "unknown_class",
                str(self.valobj.GetName()) + " triggered unknown pointer location",
            )
            return NSBundleUnknown_SummaryProvider(
                self.valobj, self.sys_params
            ).url_text()
        else:
            statistics.metric_hit("code_notrun", self.valobj)
            return my_string


class NSBundleUnknown_SummaryProvider:
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

    def url_text(self):
        logger = lldb.formatters.Logger.Logger()
        stream = lldb.SBStream()
        self.valobj.GetExpressionPath(stream)
        expr = "(NSString*)[" + stream.GetData() + " bundlePath]"
        url_text_vo = self.valobj.CreateValueFromExpression("path", expr)
        if url_text_vo.IsValid():
            return url_text_vo.GetSummary()
        return "<variable is not NSBundle>"


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

    if name_string == "NSBundle":
        wrapper = NSBundleKnown_SummaryProvider(valobj, class_data.sys_params)
        # [NSBundle mainBundle] does return an object that is
        # not correctly filled out for our purposes, so we still
        # end up having to run code in that case
        # statistics.metric_hit('code_notrun',valobj)
    else:
        wrapper = NSBundleUnknown_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit(
            "unknown_class", valobj.GetName() + " seen as " + name_string
        )
    return wrapper


def NSBundle_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    provider = GetSummary_Impl(valobj)
    if provider is not None:
        if isinstance(
            provider, lldb.runtime.objc.objc_runtime.SpecialSituation_Description
        ):
            return provider.message()
        try:
            summary = provider.url_text()
        except:
            summary = None
        logger >> "got summary " + str(summary)
        if summary is None or summary == "":
            summary = "<variable is not NSBundle>"
        return summary
    return "Summary Unavailable"


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand(
        "type summary add -F NSBundle.NSBundle_SummaryProvider NSBundle"
    )
