"""
LLDB Formatters for LLVM data types.

Load into LLDB with 'command script import /path/to/lldbDataFormatters.py'
"""
from __future__ import annotations

import collections
import lldb
import json


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("type category define -e llvm -l c++")
    debugger.HandleCommand(
        "type synthetic add -w llvm "
        f"-l {__name__}.SmallVectorSynthProvider "
        '-x "^llvm::SmallVectorImpl<.+>$"'
    )
    debugger.HandleCommand(
        "type summary add -w llvm "
        '-e -s "size=${svar%#}" '
        '-x "^llvm::SmallVectorImpl<.+>$"'
    )
    debugger.HandleCommand(
        "type synthetic add -w llvm "
        f"-l {__name__}.SmallVectorSynthProvider "
        '-x "^llvm::SmallVector<.+,.+>$"'
    )
    debugger.HandleCommand(
        "type summary add -w llvm "
        '-e -s "size=${svar%#}" '
        '-x "^llvm::SmallVector<.+,.+>$"'
    )
    debugger.HandleCommand(
        "type synthetic add -w llvm "
        f"-l {__name__}.ArrayRefSynthProvider "
        '-x "^llvm::ArrayRef<.+>$"'
    )
    debugger.HandleCommand(
        "type summary add -w llvm "
        '-e -s "size=${svar%#}" '
        '-x "^llvm::ArrayRef<.+>$"'
    )
    debugger.HandleCommand(
        "type synthetic add -w llvm "
        f"-l {__name__}.OptionalSynthProvider "
        '-x "^llvm::Optional<.+>$"'
    )
    debugger.HandleCommand(
        "type summary add -w llvm "
        f"-e -F {__name__}.OptionalSummaryProvider "
        '-x "^llvm::Optional<.+>$"'
    )
    debugger.HandleCommand(
        "type summary add -w llvm "
        f"-F {__name__}.SmallStringSummaryProvider "
        '-x "^llvm::SmallString<.+>$"'
    )
    debugger.HandleCommand(
        "type summary add -w llvm "
        f"-F {__name__}.StringRefSummaryProvider "
        "llvm::StringRef"
    )
    debugger.HandleCommand(
        "type summary add -w llvm "
        f"-F {__name__}.ConstStringSummaryProvider "
        "lldb_private::ConstString"
    )

    # The synthetic providers for PointerIntPair and PointerUnion are disabled
    # because of a few issues. One example is template arguments that are
    # non-pointer types that instead specialize PointerLikeTypeTraits.
    # debugger.HandleCommand(
    #     "type synthetic add -w llvm "
    #     f"-l {__name__}.PointerIntPairSynthProvider "
    #     '-x "^llvm::PointerIntPair<.+>$"'
    # )
    # debugger.HandleCommand(
    #     "type synthetic add -w llvm "
    #     f"-l {__name__}.PointerUnionSynthProvider "
    #     '-x "^llvm::PointerUnion<.+>$"'
    # )

    debugger.HandleCommand(
        "type summary add -w llvm "
        f"-e -F {__name__}.DenseMapSummary "
        '-x "^llvm::DenseMap<.+>$"'
    )
    debugger.HandleCommand(
        "type synthetic add -w llvm "
        f"-l {__name__}.DenseMapSynthetic "
        '-x "^llvm::DenseMap<.+>$"'
    )


# Pretty printer for llvm::SmallVector/llvm::SmallVectorImpl
class SmallVectorSynthProvider:
    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self.update()  # initialize this provider

    def num_children(self):
        return self.size.GetValueAsUnsigned(0)

    def get_child_index(self, name):
        try:
            return int(name.lstrip("[").rstrip("]"))
        except:
            return -1

    def get_child_at_index(self, index):
        # Do bounds checking.
        if index < 0:
            return None
        if index >= self.num_children():
            return None

        offset = index * self.type_size
        return self.begin.CreateChildAtOffset(
            "[" + str(index) + "]", offset, self.data_type
        )

    def update(self):
        self.begin = self.valobj.GetChildMemberWithName("BeginX")
        self.size = self.valobj.GetChildMemberWithName("Size")
        the_type = self.valobj.GetType()
        # If this is a reference type we have to dereference it to get to the
        # template parameter.
        if the_type.IsReferenceType():
            the_type = the_type.GetDereferencedType()

        if the_type.IsPointerType():
            the_type = the_type.GetPointeeType()

        self.data_type = the_type.GetTemplateArgumentType(0)
        self.type_size = self.data_type.GetByteSize()
        assert self.type_size != 0


class ArrayRefSynthProvider:
    """Provider for llvm::ArrayRef"""

    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self.update()  # initialize this provider

    def num_children(self):
        return self.length

    def get_child_index(self, name):
        try:
            return int(name.lstrip("[").rstrip("]"))
        except:
            return -1

    def get_child_at_index(self, index):
        if index < 0 or index >= self.num_children():
            return None
        offset = index * self.type_size
        return self.data.CreateChildAtOffset(
            "[" + str(index) + "]", offset, self.data_type
        )

    def update(self):
        self.data = self.valobj.GetChildMemberWithName("Data")
        length_obj = self.valobj.GetChildMemberWithName("Length")
        self.length = length_obj.GetValueAsUnsigned(0)
        self.data_type = self.data.GetType().GetPointeeType()
        self.type_size = self.data_type.GetByteSize()
        assert self.type_size != 0


def GetOptionalValue(valobj):
    storage = valobj.GetChildMemberWithName("Storage")
    if not storage:
        storage = valobj

    failure = 2
    hasVal = storage.GetChildMemberWithName("hasVal").GetValueAsUnsigned(failure)
    if hasVal == failure:
        return "<could not read llvm::Optional>"

    if hasVal == 0:
        return None

    underlying_type = storage.GetType().GetTemplateArgumentType(0)
    storage = storage.GetChildMemberWithName("value")
    return storage.Cast(underlying_type)


def OptionalSummaryProvider(valobj, internal_dict):
    val = GetOptionalValue(valobj)
    if val is None:
        return "None"
    if val.summary:
        return val.summary
    return ""


class OptionalSynthProvider:
    """Provides deref support to llvm::Optional<T>"""

    def __init__(self, valobj, internal_dict):
        self.valobj = valobj

    def num_children(self):
        return self.valobj.num_children

    def get_child_index(self, name):
        if name == "$$dereference$$":
            return self.valobj.num_children
        return self.valobj.GetIndexOfChildWithName(name)

    def get_child_at_index(self, index):
        if index < self.valobj.num_children:
            return self.valobj.GetChildAtIndex(index)
        return GetOptionalValue(self.valobj) or lldb.SBValue()


def SmallStringSummaryProvider(valobj, internal_dict):
    # The underlying SmallVector base class is the first child.
    vector = valobj.GetChildAtIndex(0)
    num_elements = vector.GetNumChildren()
    res = '"'
    for i in range(num_elements):
        c = vector.GetChildAtIndex(i)
        if c:
            res += chr(c.GetValueAsUnsigned())
    res += '"'
    return res


def StringRefSummaryProvider(valobj, internal_dict):
    if valobj.GetNumChildren() == 2:
        # StringRef's are also used to point at binary blobs in memory,
        # so filter out suspiciously long strings.
        max_length = 1024
        actual_length = valobj.GetChildAtIndex(1).GetValueAsUnsigned()
        truncate = actual_length > max_length
        length = min(max_length, actual_length)
        if length == 0:
            return '""'

        data = valobj.GetChildAtIndex(0).GetPointeeData(item_count=length)
        error = lldb.SBError()
        string = data.ReadRawData(error, 0, data.GetByteSize()).decode()
        if error.Fail():
            return "<error: %s>" % error.description

        # json.dumps conveniently escapes the string for us.
        string = json.dumps(string)
        if truncate:
            string += "..."
        return string
    return None


def ConstStringSummaryProvider(valobj, internal_dict):
    if valobj.GetNumChildren() == 1:
        return valobj.GetChildAtIndex(0).GetSummary()
    return ""


def get_expression_path(val):
    stream = lldb.SBStream()
    if not val.GetExpressionPath(stream):
        return None
    return stream.GetData()


class PointerIntPairSynthProvider:
    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self.update()

    def num_children(self):
        return 2

    def get_child_index(self, name):
        if name == "Pointer":
            return 0
        if name == "Int":
            return 1
        return None

    def get_child_at_index(self, index):
        expr_path = get_expression_path(self.valobj)
        if index == 0:
            return self.valobj.CreateValueFromExpression(
                "Pointer", f"({self.pointer_ty.name}){expr_path}.getPointer()"
            )
        if index == 1:
            return self.valobj.CreateValueFromExpression(
                "Int", f"({self.int_ty.name}){expr_path}.getInt()"
            )
        return None

    def update(self):
        self.pointer_ty = self.valobj.GetType().GetTemplateArgumentType(0)
        self.int_ty = self.valobj.GetType().GetTemplateArgumentType(2)


def parse_template_parameters(typename):
    """
    LLDB doesn't support template parameter packs, so let's parse them manually.
    """
    result = []
    start = typename.find("<")
    end = typename.rfind(">")
    if start < 1 or end < 2 or end - start < 2:
        return result

    nesting_level = 0
    current_parameter_start = start + 1

    for i in range(start + 1, end + 1):
        c = typename[i]
        if c == "<":
            nesting_level += 1
        elif c == ">":
            nesting_level -= 1
        elif c == "," and nesting_level == 0:
            result.append(typename[current_parameter_start:i].strip())
            current_parameter_start = i + 1

    result.append(typename[current_parameter_start:i].strip())

    return result


class PointerUnionSynthProvider:
    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self.update()

    def num_children(self):
        return 1

    def get_child_index(self, name):
        if name == "Ptr":
            return 0
        return None

    def get_child_at_index(self, index):
        if index != 0:
            return None
        ptr_type_name = self.template_args[self.active_type_tag]
        return self.valobj.CreateValueFromExpression(
            "Ptr", f"({ptr_type_name}){self.val_expr_path}.getPointer()"
        )

    def update(self):
        self.pointer_int_pair = self.valobj.GetChildMemberWithName("Val")
        self.val_expr_path = get_expression_path(
            self.valobj.GetChildMemberWithName("Val")
        )
        self.active_type_tag = self.valobj.CreateValueFromExpression(
            "", f"(int){self.val_expr_path}.getInt()"
        ).GetValueAsSigned()
        self.template_args = parse_template_parameters(self.valobj.GetType().name)


def DenseMapSummary(valobj: lldb.SBValue, _) -> str:
    raw_value = valobj.GetNonSyntheticValue()
    num_entries = raw_value.GetChildMemberWithName("NumEntries").unsigned
    num_tombstones = raw_value.GetChildMemberWithName("NumTombstones").unsigned

    summary = f"size={num_entries}"
    if num_tombstones == 1:
        # The heuristic to identify valid entries does not handle the case of a
        # single tombstone. The summary calls attention to this.
        summary = f"tombstones=1, {summary}"
    return summary


class DenseMapSynthetic:
    valobj: lldb.SBValue

    # The indexes into `Buckets` that contain valid map entries.
    child_buckets: list[int]

    def __init__(self, valobj: lldb.SBValue, _) -> None:
        self.valobj = valobj

    def num_children(self) -> int:
        return len(self.child_buckets)

    def get_child_at_index(self, child_index: int) -> lldb.SBValue:
        bucket_index = self.child_buckets[child_index]
        entry = self.valobj.GetValueForExpressionPath(f".Buckets[{bucket_index}]")

        # By default, DenseMap instances use DenseMapPair to hold key-value
        # entries. When the entry is a DenseMapPair, unwrap it to expose the
        # children as simple std::pair values.
        #
        # This entry type is customizable (a template parameter). For other
        # types, expose the entry type as is.
        if entry.type.name.startswith("llvm::detail::DenseMapPair<"):
            entry = entry.GetChildAtIndex(0)

        return entry.Clone(f"[{child_index}]")

    def update(self):
        self.child_buckets = []

        num_entries = self.valobj.GetChildMemberWithName("NumEntries").unsigned
        if num_entries == 0:
            return

        buckets = self.valobj.GetChildMemberWithName("Buckets")
        num_buckets = self.valobj.GetChildMemberWithName("NumBuckets").unsigned

        # Bucket entries contain one of the following:
        #   1. Valid key-value
        #   2. Empty key
        #   3. Tombstone key (a deleted entry)
        #
        # NumBuckets is always greater than NumEntries. The empty key, and
        # potentially the tombstone key, will occur multiple times. A key that
        # is repeated is either the empty key or the tombstone key.

        # For each key, collect a list of buckets it appears in.
        key_buckets: dict[str, list[int]] = collections.defaultdict(list)
        for index in range(num_buckets):
            key = buckets.GetValueForExpressionPath(f"[{index}].first")
            key_buckets[str(key.data)].append(index)

        # Heuristic: This is not a multi-map, any repeated (non-unique) keys are
        # either the the empty key or the tombstone key. Populate child_buckets
        # with the indexes of entries containing unique keys.
        for indexes in key_buckets.values():
            if len(indexes) == 1:
                self.child_buckets.append(indexes[0])
