"""
LLDB AppKit formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
# example summary provider for NSDate
# the real summary is now C++ code built into LLDB
import lldb
import ctypes
import lldb.runtime.objc.objc_runtime
import lldb.formatters.metrics
import struct
import time
import datetime
import CFString
import lldb.formatters.Logger

statistics = lldb.formatters.metrics.Metrics()
statistics.add_metric("invalid_isa")
statistics.add_metric("invalid_pointer")
statistics.add_metric("unknown_class")
statistics.add_metric("code_notrun")

# Python promises to start counting time at midnight on Jan 1st on the epoch year
# hence, all we need to know is the epoch year
python_epoch = time.gmtime(0).tm_year

osx_epoch = datetime.date(2001, 1, 1).timetuple()


def mkgmtime(t):
    logger = lldb.formatters.Logger.Logger()
    return time.mktime(t) - time.timezone


osx_epoch = mkgmtime(osx_epoch)


def osx_to_python_time(osx):
    logger = lldb.formatters.Logger.Logger()
    if python_epoch <= 2001:
        return osx + osx_epoch
    else:
        return osx - osx_epoch


# represent a struct_time as a string in the format used by Xcode


def xcode_format_time(X):
    logger = lldb.formatters.Logger.Logger()
    return time.strftime("%Y-%m-%d %H:%M:%S %Z", X)


# represent a count-since-epoch as a string in the format used by Xcode


def xcode_format_count(X):
    logger = lldb.formatters.Logger.Logger()
    return xcode_format_time(time.localtime(X))


# despite the similary to synthetic children providers, these classes are not
# trying to provide anything but the summary for NSDate, so they need not
# obey the interface specification for synthetic children providers


class NSTaggedDate_SummaryProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, info_bits, data, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        self.update()
        # NSDate is not using its info_bits for info like NSNumber is
        # so we need to regroup info_bits and data
        self.data = (data << 8) | (info_bits << 4)

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    def value(self):
        logger = lldb.formatters.Logger.Logger()
        # the value of the date-time object is wrapped into the pointer value
        # unfortunately, it is made as a time-delta after Jan 1 2001 midnight GMT
        # while all Python knows about is the "epoch", which is a platform-dependent
        # year (1970 of *nix) whose Jan 1 at midnight is taken as reference
        value_double = struct.unpack("d", struct.pack("Q", self.data))[0]
        if value_double == -63114076800.0:
            return "0001-12-30 00:00:00 +0000"
        return xcode_format_count(osx_to_python_time(value_double))


class NSUntaggedDate_SummaryProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        if not (self.sys_params.types_cache.double):
            self.sys_params.types_cache.double = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeDouble
            )
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    def offset(self):
        logger = lldb.formatters.Logger.Logger()
        return self.sys_params.pointer_size

    def value(self):
        logger = lldb.formatters.Logger.Logger()
        value = self.valobj.CreateChildAtOffset(
            "value", self.offset(), self.sys_params.types_cache.double
        )
        value_double = struct.unpack("d", struct.pack("Q", value.GetData().uint64[0]))[
            0
        ]
        if value_double == -63114076800.0:
            return "0001-12-30 00:00:00 +0000"
        return xcode_format_count(osx_to_python_time(value_double))


class NSCalendarDate_SummaryProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        if not (self.sys_params.types_cache.double):
            self.sys_params.types_cache.double = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeDouble
            )
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    def offset(self):
        logger = lldb.formatters.Logger.Logger()
        return 2 * self.sys_params.pointer_size

    def value(self):
        logger = lldb.formatters.Logger.Logger()
        value = self.valobj.CreateChildAtOffset(
            "value", self.offset(), self.sys_params.types_cache.double
        )
        value_double = struct.unpack("d", struct.pack("Q", value.GetData().uint64[0]))[
            0
        ]
        return xcode_format_count(osx_to_python_time(value_double))


class NSTimeZoneClass_SummaryProvider:
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

    def offset(self):
        logger = lldb.formatters.Logger.Logger()
        return self.sys_params.pointer_size

    def timezone(self):
        logger = lldb.formatters.Logger.Logger()
        tz_string = self.valobj.CreateChildAtOffset(
            "tz_name", self.offset(), self.sys_params.types_cache.voidptr
        )
        return CFString.CFString_SummaryProvider(tz_string, None)


class NSUnknownDate_SummaryProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    def value(self):
        logger = lldb.formatters.Logger.Logger()
        stream = lldb.SBStream()
        self.valobj.GetExpressionPath(stream)
        expr = "(NSString*)[" + stream.GetData() + " description]"
        num_children_vo = self.valobj.CreateValueFromExpression("str", expr)
        if num_children_vo.IsValid():
            return num_children_vo.GetSummary()
        return "<variable is not NSDate>"


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

    if (
        name_string == "NSDate"
        or name_string == "__NSDate"
        or name_string == "__NSTaggedDate"
    ):
        if class_data.is_tagged():
            wrapper = NSTaggedDate_SummaryProvider(
                valobj,
                class_data.info_bits(),
                class_data.value(),
                class_data.sys_params,
            )
            statistics.metric_hit("code_notrun", valobj)
        else:
            wrapper = NSUntaggedDate_SummaryProvider(valobj, class_data.sys_params)
            statistics.metric_hit("code_notrun", valobj)
    elif name_string == "NSCalendarDate":
        wrapper = NSCalendarDate_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit("code_notrun", valobj)
    elif name_string == "__NSTimeZone":
        wrapper = NSTimeZoneClass_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit("code_notrun", valobj)
    else:
        wrapper = NSUnknownDate_SummaryProvider(valobj)
        statistics.metric_hit(
            "unknown_class", valobj.GetName() + " seen as " + name_string
        )
    return wrapper


def NSDate_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    provider = GetSummary_Impl(valobj)
    if provider is not None:
        if isinstance(
            provider, lldb.runtime.objc.objc_runtime.SpecialSituation_Description
        ):
            return provider.message()
        try:
            summary = provider.value()
        except:
            summary = None
        if summary is None:
            summary = "<variable is not NSDate>"
        return str(summary)
    return "Summary Unavailable"


def NSTimeZone_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    provider = GetSummary_Impl(valobj)
    if provider is not None:
        if isinstance(
            provider, lldb.runtime.objc.objc_runtime.SpecialSituation_Description
        ):
            return provider.message()
        try:
            summary = provider.timezone()
        except:
            summary = None
        logger >> "got summary " + str(summary)
        if summary is None:
            summary = "<variable is not NSTimeZone>"
        return str(summary)
    return "Summary Unavailable"


def CFAbsoluteTime_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    try:
        value_double = struct.unpack("d", struct.pack("Q", valobj.GetData().uint64[0]))[
            0
        ]
        return xcode_format_count(osx_to_python_time(value_double))
    except:
        return "Summary Unavailable"


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand("type summary add -F NSDate.NSDate_SummaryProvider NSDate")
    debugger.HandleCommand(
        "type summary add -F NSDate.CFAbsoluteTime_SummaryProvider CFAbsoluteTime"
    )
    debugger.HandleCommand(
        "type summary add -F NSDate.NSTimeZone_SummaryProvider NSTimeZone CFTimeZoneRef"
    )
