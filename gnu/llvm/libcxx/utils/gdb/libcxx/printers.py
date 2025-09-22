# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##
"""GDB pretty-printers for libc++.

These should work for objects compiled with either the stable ABI or the unstable ABI.
"""

from __future__ import print_function

import re
import gdb

# One under-documented feature of the gdb pretty-printer API
# is that clients can call any other member of the API
# before they call to_string.
# Therefore all self.FIELDs must be set in the pretty-printer's
# __init__ function.

_void_pointer_type = gdb.lookup_type("void").pointer()


_long_int_type = gdb.lookup_type("unsigned long long")

_libcpp_big_endian = False


def addr_as_long(addr):
    return int(addr.cast(_long_int_type))


# The size of a pointer in bytes.
_pointer_size = _void_pointer_type.sizeof


def _remove_cxx_namespace(typename):
    """Removed libc++ specific namespace from the type.

    Arguments:
      typename(string): A type, such as std::__u::something.

    Returns:
      A string without the libc++ specific part, such as std::something.
    """

    return re.sub("std::__.*?::", "std::", typename)


def _remove_generics(typename):
    """Remove generics part of the type. Assumes typename is not empty.

    Arguments:
      typename(string): A type such as std::my_collection<element>.

    Returns:
      The prefix up to the generic part, such as std::my_collection.
    """

    match = re.match("^([^<]+)", typename)
    return match.group(1)


def _cc_field(node):
    """Previous versions of libcxx had inconsistent field naming naming. Handle
    both types.
    """
    try:
        return node["__value_"]["__cc_"]
    except:
        return node["__value_"]["__cc"]


def _data_field(node):
    """Previous versions of libcxx had inconsistent field naming naming. Handle
    both types.
    """
    try:
        return node["__data_"]
    except:
        return node["__data"]


def _size_field(node):
    """Previous versions of libcxx had inconsistent field naming naming. Handle
    both types.
    """
    try:
        return node["__size_"]
    except:
        return node["__size"]


# Some common substitutions on the types to reduce visual clutter (A user who
# wants to see the actual details can always use print/r).
_common_substitutions = [
    (
        "std::basic_string<char, std::char_traits<char>, std::allocator<char> >",
        "std::string",
    ),
    ("std::basic_string_view<char, std::char_traits<char> >", "std::string_view"),
]


def _prettify_typename(gdb_type):
    """Returns a pretty name for the type, or None if no name can be found.

    Arguments:
      gdb_type(gdb.Type): A type object.

    Returns:
      A string, without type_defs, libc++ namespaces, and common substitutions
      applied.
    """

    type_without_typedefs = gdb_type.strip_typedefs()
    typename = (
        type_without_typedefs.name
        or type_without_typedefs.tag
        or str(type_without_typedefs)
    )
    result = _remove_cxx_namespace(typename)
    for find_str, subst_str in _common_substitutions:
        result = re.sub(find_str, subst_str, result)
    return result


def _typename_for_nth_generic_argument(gdb_type, n):
    """Returns a pretty string for the nth argument of the given type.

    Arguments:
      gdb_type(gdb.Type): A type object, such as the one for std::map<int, int>
      n: The (zero indexed) index of the argument to return.

    Returns:
      A string for the nth argument, such a "std::string"
    """
    element_type = gdb_type.template_argument(n)
    return _prettify_typename(element_type)


def _typename_with_n_generic_arguments(gdb_type, n):
    """Return a string for the type with the first n (1, ...) generic args."""

    base_type = _remove_generics(_prettify_typename(gdb_type))
    arg_list = [base_type]
    template = "%s<"
    for i in range(n):
        arg_list.append(_typename_for_nth_generic_argument(gdb_type, i))
        template += "%s, "
    result = (template[:-2] + ">") % tuple(arg_list)
    return result


def _typename_with_first_generic_argument(gdb_type):
    return _typename_with_n_generic_arguments(gdb_type, 1)


class StdTuplePrinter(object):
    """Print a std::tuple."""

    class _Children(object):
        """Class to iterate over the tuple's children."""

        def __init__(self, val):
            self.val = val
            self.child_iter = iter(self.val["__base_"].type.fields())
            self.count = 0

        def __iter__(self):
            return self

        def __next__(self):
            # child_iter raises StopIteration when appropriate.
            field_name = next(self.child_iter)
            child = self.val["__base_"][field_name]["__value_"]
            self.count += 1
            return ("[%d]" % self.count, child)

        next = __next__  # Needed for GDB built against Python 2.7.

    def __init__(self, val):
        self.val = val

    def to_string(self):
        typename = _remove_generics(_prettify_typename(self.val.type))
        if not self.val.type.fields():
            return "empty %s" % typename
        return "%s containing" % typename

    def children(self):
        if not self.val.type.fields():
            return iter(())
        return self._Children(self.val)


def _get_base_subobject(child_class_value, index=0):
    """Returns the object's value in the form of the parent class at index.

    This function effectively casts the child_class_value to the base_class's
    type, but the type-to-cast to is stored in the field at index, and once
    we know the field, we can just return the data.

    Args:
      child_class_value: the value to cast
      index: the parent class index

    Raises:
      Exception: field at index was not a base-class field.
    """

    field = child_class_value.type.fields()[index]
    if not field.is_base_class:
        raise Exception("Not a base-class field.")
    return child_class_value[field]


def _value_of_pair_first(value):
    """Convenience for _get_base_subobject, for the common case."""
    return _get_base_subobject(value, 0)["__value_"]


class StdStringPrinter(object):
    """Print a std::string."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        """Build a python string from the data whether stored inline or separately."""
        value_field = _value_of_pair_first(self.val["__r_"])
        short_field = value_field["__s"]
        short_size = short_field["__size_"]
        if short_field["__is_long_"]:
            long_field = value_field["__l"]
            data = long_field["__data_"]
            size = long_field["__size_"]
        else:
            data = short_field["__data_"]
            size = short_field["__size_"]
        return data.lazy_string(length=size)

    def display_hint(self):
        return "string"


class StdStringViewPrinter(object):
    """Print a std::string_view."""

    def __init__(self, val):
        self.val = val

    def display_hint(self):
        return "string"

    def to_string(self):  # pylint: disable=g-bad-name
        """GDB calls this to compute the pretty-printed form."""

        ptr = _data_field(self.val)
        ptr = ptr.cast(ptr.type.target().strip_typedefs().pointer())
        size = _size_field(self.val)
        return ptr.lazy_string(length=size)


class StdUniquePtrPrinter(object):
    """Print a std::unique_ptr."""

    def __init__(self, val):
        self.val = val
        self.addr = _value_of_pair_first(self.val["__ptr_"])
        self.pointee_type = self.val.type.template_argument(0)

    def to_string(self):
        typename = _remove_generics(_prettify_typename(self.val.type))
        if not self.addr:
            return "%s is nullptr" % typename
        return "%s<%s> containing" % (
            typename,
            _remove_generics(_prettify_typename(self.pointee_type)),
        )

    def __iter__(self):
        if self.addr:
            yield "__ptr_", self.addr.cast(self.pointee_type.pointer())

    def children(self):
        return self


class StdSharedPointerPrinter(object):
    """Print a std::shared_ptr."""

    def __init__(self, val):
        self.val = val
        self.addr = self.val["__ptr_"]

    def to_string(self):
        """Returns self as a string."""
        typename = _remove_generics(_prettify_typename(self.val.type))
        pointee_type = _remove_generics(
            _prettify_typename(self.val.type.template_argument(0))
        )
        if not self.addr:
            return "%s is nullptr" % typename
        refcount = self.val["__cntrl_"]
        if refcount != 0:
            try:
                usecount = refcount["__shared_owners_"] + 1
                weakcount = refcount["__shared_weak_owners_"]
                if usecount == 0:
                    state = "expired, weak %d" % weakcount
                else:
                    state = "count %d, weak %d" % (usecount, weakcount)
            except:
                # Debug info for a class with virtual functions is emitted
                # in the same place as its key function. That means that
                # for std::shared_ptr, __shared_owners_ is emitted into
                # into libcxx.[so|a] itself, rather than into the shared_ptr
                # instantiation point. So if libcxx.so was built without
                # debug info, these fields will be missing.
                state = "count ?, weak ? (libc++ missing debug info)"
        return "%s<%s> %s containing" % (typename, pointee_type, state)

    def __iter__(self):
        if self.addr:
            yield "__ptr_", self.addr

    def children(self):
        return self


class StdVectorPrinter(object):
    """Print a std::vector."""

    class _VectorBoolIterator(object):
        """Class to iterate over the bool vector's children."""

        def __init__(self, begin, size, bits_per_word):
            self.item = begin
            self.size = size
            self.bits_per_word = bits_per_word
            self.count = 0
            self.offset = 0

        def __iter__(self):
            return self

        def __next__(self):
            """Retrieve the next element."""

            self.count += 1
            if self.count > self.size:
                raise StopIteration
            entry = self.item.dereference()
            if entry & (1 << self.offset):
                outbit = 1
            else:
                outbit = 0
            self.offset += 1
            if self.offset >= self.bits_per_word:
                self.item += 1
                self.offset = 0
            return ("[%d]" % self.count, outbit)

        next = __next__  # Needed for GDB built against Python 2.7.

    class _VectorIterator(object):
        """Class to iterate over the non-bool vector's children."""

        def __init__(self, begin, end):
            self.item = begin
            self.end = end
            self.count = 0

        def __iter__(self):
            return self

        def __next__(self):
            self.count += 1
            if self.item == self.end:
                raise StopIteration
            entry = self.item.dereference()
            self.item += 1
            return ("[%d]" % self.count, entry)

        next = __next__  # Needed for GDB built against Python 2.7.

    def __init__(self, val):
        """Set val, length, capacity, and iterator for bool and normal vectors."""
        self.val = val
        self.typename = _remove_generics(_prettify_typename(val.type))
        begin = self.val["__begin_"]
        if self.val.type.template_argument(0).code == gdb.TYPE_CODE_BOOL:
            self.typename += "<bool>"
            self.length = self.val["__size_"]
            bits_per_word = self.val["__bits_per_word"]
            self.capacity = (
                _value_of_pair_first(self.val["__cap_alloc_"]) * bits_per_word
            )
            self.iterator = self._VectorBoolIterator(begin, self.length, bits_per_word)
        else:
            end = self.val["__end_"]
            self.length = end - begin
            self.capacity = (
                _get_base_subobject(self.val["__end_cap_"])["__value_"] - begin
            )
            self.iterator = self._VectorIterator(begin, end)

    def to_string(self):
        return "%s of length %d, capacity %d" % (
            self.typename,
            self.length,
            self.capacity,
        )

    def children(self):
        return self.iterator

    def display_hint(self):
        return "array"


class StdBitsetPrinter(object):
    """Print a std::bitset."""

    def __init__(self, val):
        self.val = val
        self.n_words = int(self.val["__n_words"])
        self.bits_per_word = int(self.val["__bits_per_word"])
        self.bit_count = self.val.type.template_argument(0)
        if self.n_words == 1:
            self.values = [int(self.val["__first_"])]
        else:
            self.values = [
                int(self.val["__first_"][index]) for index in range(self.n_words)
            ]

    def to_string(self):
        typename = _prettify_typename(self.val.type)
        return "%s" % typename

    def _list_it(self):
        for bit in range(self.bit_count):
            word = bit // self.bits_per_word
            word_bit = bit % self.bits_per_word
            if self.values[word] & (1 << word_bit):
                yield ("[%d]" % bit, 1)

    def __iter__(self):
        return self._list_it()

    def children(self):
        return self


class StdDequePrinter(object):
    """Print a std::deque."""

    def __init__(self, val):
        self.val = val
        self.size = int(_value_of_pair_first(val["__size_"]))
        self.start_ptr = self.val["__map_"]["__begin_"]
        self.first_block_start_index = int(self.val["__start_"])
        self.node_type = self.start_ptr.type
        self.block_size = self._calculate_block_size(val.type.template_argument(0))

    def _calculate_block_size(self, element_type):
        """Calculates the number of elements in a full block."""
        size = element_type.sizeof
        # Copied from struct __deque_block_size implementation of libcxx.
        return 4096 / size if size < 256 else 16

    def _bucket_it(self, start_addr, start_index, end_index):
        for i in range(start_index, end_index):
            yield i, (start_addr.dereference() + i).dereference()

    def _list_it(self):
        """Primary iteration worker."""
        num_emitted = 0
        current_addr = self.start_ptr
        start_index = self.first_block_start_index
        while num_emitted < self.size:
            end_index = min(start_index + self.size - num_emitted, self.block_size)
            for _, elem in self._bucket_it(current_addr, start_index, end_index):
                yield "", elem
            num_emitted += end_index - start_index
            current_addr = gdb.Value(addr_as_long(current_addr) + _pointer_size).cast(
                self.node_type
            )
            start_index = 0

    def to_string(self):
        typename = _remove_generics(_prettify_typename(self.val.type))
        if self.size:
            return "%s with %d elements" % (typename, self.size)
        return "%s is empty" % typename

    def __iter__(self):
        return self._list_it()

    def children(self):
        return self

    def display_hint(self):
        return "array"


class StdListPrinter(object):
    """Print a std::list."""

    def __init__(self, val):
        self.val = val
        size_alloc_field = self.val["__size_alloc_"]
        self.size = int(_value_of_pair_first(size_alloc_field))
        dummy_node = self.val["__end_"]
        self.nodetype = gdb.lookup_type(
            re.sub(
                "__list_node_base", "__list_node", str(dummy_node.type.strip_typedefs())
            )
        ).pointer()
        self.first_node = dummy_node["__next_"]

    def to_string(self):
        typename = _remove_generics(_prettify_typename(self.val.type))
        if self.size:
            return "%s with %d elements" % (typename, self.size)
        return "%s is empty" % typename

    def _list_iter(self):
        current_node = self.first_node
        for _ in range(self.size):
            yield "", current_node.cast(self.nodetype).dereference()["__value_"]
            current_node = current_node.dereference()["__next_"]

    def __iter__(self):
        return self._list_iter()

    def children(self):
        return self if self.nodetype else iter(())

    def display_hint(self):
        return "array"


class StdQueueOrStackPrinter(object):
    """Print a std::queue or std::stack."""

    def __init__(self, val):
        self.val = val
        self.underlying = val["c"]

    def to_string(self):
        typename = _remove_generics(_prettify_typename(self.val.type))
        return "%s wrapping" % typename

    def children(self):
        return iter([("", self.underlying)])

    def display_hint(self):
        return "array"


class StdPriorityQueuePrinter(object):
    """Print a std::priority_queue."""

    def __init__(self, val):
        self.val = val
        self.underlying = val["c"]

    def to_string(self):
        # TODO(tamur): It would be nice to print the top element. The technical
        # difficulty is that, the implementation refers to the underlying
        # container, which is a generic class. libstdcxx pretty printers do not
        # print the top element.
        typename = _remove_generics(_prettify_typename(self.val.type))
        return "%s wrapping" % typename

    def children(self):
        return iter([("", self.underlying)])

    def display_hint(self):
        return "array"


class RBTreeUtils(object):
    """Utility class for std::(multi)map, and std::(multi)set and iterators."""

    def __init__(self, cast_type, root):
        self.cast_type = cast_type
        self.root = root

    def left_child(self, node):
        result = node.cast(self.cast_type).dereference()["__left_"]
        return result

    def right_child(self, node):
        result = node.cast(self.cast_type).dereference()["__right_"]
        return result

    def parent(self, node):
        """Return the parent of node, if it exists."""
        # If this is the root, then from the algorithm's point of view, it has no
        # parent.
        if node == self.root:
            return None

        # We don't have enough information to tell if this is the end_node (which
        # doesn't have a __parent_ field), or the root (which doesn't have a parent
        # from the algorithm's point of view), so cast_type may not be correct for
        # this particular node. Use heuristics.

        # The end_node's left child is the root. Note that when printing interators
        # in isolation, the root is unknown.
        if self.left_child(node) == self.root:
            return None

        parent = node.cast(self.cast_type).dereference()["__parent_"]
        # If the value at the offset of __parent_ doesn't look like a valid pointer,
        # then assume that node is the end_node (and therefore has no parent).
        # End_node type has a pointer embedded, so should have pointer alignment.
        if addr_as_long(parent) % _void_pointer_type.alignof:
            return None
        # This is ugly, but the only other option is to dereference an invalid
        # pointer.  0x8000 is fairly arbitrary, but has had good results in
        # practice.  If there was a way to tell if a pointer is invalid without
        # actually dereferencing it and spewing error messages, that would be ideal.
        if parent < 0x8000:
            return None
        return parent

    def is_left_child(self, node):
        parent = self.parent(node)
        return parent is not None and self.left_child(parent) == node

    def is_right_child(self, node):
        parent = self.parent(node)
        return parent is not None and self.right_child(parent) == node


class AbstractRBTreePrinter(object):
    """Abstract super class for std::(multi)map, and std::(multi)set."""

    def __init__(self, val):
        self.val = val
        tree = self.val["__tree_"]
        self.size = int(_value_of_pair_first(tree["__pair3_"]))
        dummy_root = tree["__pair1_"]
        root = _value_of_pair_first(dummy_root)["__left_"]
        cast_type = self._init_cast_type(val.type)
        self.util = RBTreeUtils(cast_type, root)

    def _get_key_value(self, node):
        """Subclasses should override to return a list of values to yield."""
        raise NotImplementedError

    def _traverse(self):
        """Traverses the binary search tree in order."""
        current = self.util.root
        skip_left_child = False
        while True:
            if not skip_left_child and self.util.left_child(current):
                current = self.util.left_child(current)
                continue
            skip_left_child = False
            for key_value in self._get_key_value(current):
                yield "", key_value
            right_child = self.util.right_child(current)
            if right_child:
                current = right_child
                continue
            while self.util.is_right_child(current):
                current = self.util.parent(current)
            if self.util.is_left_child(current):
                current = self.util.parent(current)
                skip_left_child = True
                continue
            break

    def __iter__(self):
        return self._traverse()

    def children(self):
        return self if self.util.cast_type and self.size > 0 else iter(())

    def to_string(self):
        typename = _remove_generics(_prettify_typename(self.val.type))
        if self.size:
            return "%s with %d elements" % (typename, self.size)
        return "%s is empty" % typename


class StdMapPrinter(AbstractRBTreePrinter):
    """Print a std::map or std::multimap."""

    def _init_cast_type(self, val_type):
        map_it_type = gdb.lookup_type(
            str(val_type.strip_typedefs()) + "::iterator"
        ).strip_typedefs()
        tree_it_type = map_it_type.template_argument(0)
        node_ptr_type = tree_it_type.template_argument(1)
        return node_ptr_type

    def display_hint(self):
        return "map"

    def _get_key_value(self, node):
        key_value = _cc_field(node.cast(self.util.cast_type).dereference())
        return [key_value["first"], key_value["second"]]


class StdSetPrinter(AbstractRBTreePrinter):
    """Print a std::set."""

    def _init_cast_type(self, val_type):
        set_it_type = gdb.lookup_type(
            str(val_type.strip_typedefs()) + "::iterator"
        ).strip_typedefs()
        node_ptr_type = set_it_type.template_argument(1)
        return node_ptr_type

    def display_hint(self):
        return "array"

    def _get_key_value(self, node):
        key_value = node.cast(self.util.cast_type).dereference()["__value_"]
        return [key_value]


class AbstractRBTreeIteratorPrinter(object):
    """Abstract super class for std::(multi)map, and std::(multi)set iterator."""

    def _initialize(self, val, typename):
        self.typename = typename
        self.val = val
        self.addr = self.val["__ptr_"]
        cast_type = self.val.type.template_argument(1)
        self.util = RBTreeUtils(cast_type, None)
        if self.addr:
            self.node = self.addr.cast(cast_type).dereference()

    def _is_valid_node(self):
        if not self.util.parent(self.addr):
            return False
        return self.util.is_left_child(self.addr) or self.util.is_right_child(self.addr)

    def to_string(self):
        if not self.addr:
            return "%s is nullptr" % self.typename
        return "%s " % self.typename

    def _get_node_value(self, node):
        raise NotImplementedError

    def __iter__(self):
        addr_str = "[%s]" % str(self.addr)
        if not self._is_valid_node():
            yield addr_str, " end()"
        else:
            yield addr_str, self._get_node_value(self.node)

    def children(self):
        return self if self.addr else iter(())


class MapIteratorPrinter(AbstractRBTreeIteratorPrinter):
    """Print a std::(multi)map iterator."""

    def __init__(self, val):
        self._initialize(val["__i_"], _remove_generics(_prettify_typename(val.type)))

    def _get_node_value(self, node):
        return _cc_field(node)


class SetIteratorPrinter(AbstractRBTreeIteratorPrinter):
    """Print a std::(multi)set iterator."""

    def __init__(self, val):
        self._initialize(val, _remove_generics(_prettify_typename(val.type)))

    def _get_node_value(self, node):
        return node["__value_"]


class StdFposPrinter(object):
    """Print a std::fpos or std::streampos."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        typename = _remove_generics(_prettify_typename(self.val.type))
        offset = self.val["__off_"]
        state = self.val["__st_"]

        state_fields = []
        if state.type.code == gdb.TYPE_CODE_STRUCT:
            state_fields = [f.name for f in state.type.fields()]

        state_string = ""
        if "__count" in state_fields and "__value" in state_fields:
            count = state["__count"]
            value = state["__value"]["__wch"]
            state_string = " with state: {count:%s value:%s}" % (count, value)

        return "%s with stream offset:%s%s" % (typename, offset, state_string)


class AbstractUnorderedCollectionPrinter(object):
    """Abstract super class for std::unordered_(multi)[set|map]."""

    def __init__(self, val):
        self.val = val
        self.table = val["__table_"]
        self.sentinel = self.table["__p1_"]
        self.size = int(_value_of_pair_first(self.table["__p2_"]))
        node_base_type = self.sentinel.type.template_argument(0)
        self.cast_type = node_base_type.template_argument(0)

    def _list_it(self, sentinel_ptr):
        next_ptr = _value_of_pair_first(sentinel_ptr)["__next_"]
        while str(next_ptr.cast(_void_pointer_type)) != "0x0":
            next_val = next_ptr.cast(self.cast_type).dereference()
            for key_value in self._get_key_value(next_val):
                yield "", key_value
            next_ptr = next_val["__next_"]

    def to_string(self):
        typename = _remove_generics(_prettify_typename(self.val.type))
        if self.size:
            return "%s with %d elements" % (typename, self.size)
        return "%s is empty" % typename

    def _get_key_value(self, node):
        """Subclasses should override to return a list of values to yield."""
        raise NotImplementedError

    def children(self):
        return self if self.cast_type and self.size > 0 else iter(())

    def __iter__(self):
        return self._list_it(self.sentinel)


class StdUnorderedSetPrinter(AbstractUnorderedCollectionPrinter):
    """Print a std::unordered_(multi)set."""

    def _get_key_value(self, node):
        return [node["__value_"]]

    def display_hint(self):
        return "array"


class StdUnorderedMapPrinter(AbstractUnorderedCollectionPrinter):
    """Print a std::unordered_(multi)map."""

    def _get_key_value(self, node):
        key_value = _cc_field(node)
        return [key_value["first"], key_value["second"]]

    def display_hint(self):
        return "map"


class AbstractHashMapIteratorPrinter(object):
    """Abstract class for unordered collection iterators."""

    def _initialize(self, val, addr):
        self.val = val
        self.typename = _remove_generics(_prettify_typename(self.val.type))
        self.addr = addr
        if self.addr:
            self.node = self.addr.cast(self.cast_type).dereference()

    def _get_key_value(self):
        """Subclasses should override to return a list of values to yield."""
        raise NotImplementedError

    def to_string(self):
        if not self.addr:
            return "%s = end()" % self.typename
        return "%s " % self.typename

    def children(self):
        return self if self.addr else iter(())

    def __iter__(self):
        for key_value in self._get_key_value():
            yield "", key_value


class StdUnorderedSetIteratorPrinter(AbstractHashMapIteratorPrinter):
    """Print a std::(multi)set iterator."""

    def __init__(self, val):
        self.cast_type = val.type.template_argument(0)
        self._initialize(val, val["__node_"])

    def _get_key_value(self):
        return [self.node["__value_"]]

    def display_hint(self):
        return "array"


class StdUnorderedMapIteratorPrinter(AbstractHashMapIteratorPrinter):
    """Print a std::(multi)map iterator."""

    def __init__(self, val):
        self.cast_type = val.type.template_argument(0).template_argument(0)
        self._initialize(val, val["__i_"]["__node_"])

    def _get_key_value(self):
        key_value = _cc_field(self.node)
        return [key_value["first"], key_value["second"]]

    def display_hint(self):
        return "map"


def _remove_std_prefix(typename):
    match = re.match("^std::(.+)", typename)
    return match.group(1) if match is not None else ""


class LibcxxPrettyPrinter(object):
    """PrettyPrinter object so gdb-commands like 'info pretty-printers' work."""

    def __init__(self, name):
        super(LibcxxPrettyPrinter, self).__init__()
        self.name = name
        self.enabled = True

        self.lookup = {
            "basic_string": StdStringPrinter,
            "string": StdStringPrinter,
            "string_view": StdStringViewPrinter,
            "tuple": StdTuplePrinter,
            "unique_ptr": StdUniquePtrPrinter,
            "shared_ptr": StdSharedPointerPrinter,
            "weak_ptr": StdSharedPointerPrinter,
            "bitset": StdBitsetPrinter,
            "deque": StdDequePrinter,
            "list": StdListPrinter,
            "queue": StdQueueOrStackPrinter,
            "stack": StdQueueOrStackPrinter,
            "priority_queue": StdPriorityQueuePrinter,
            "map": StdMapPrinter,
            "multimap": StdMapPrinter,
            "set": StdSetPrinter,
            "multiset": StdSetPrinter,
            "vector": StdVectorPrinter,
            "__map_iterator": MapIteratorPrinter,
            "__map_const_iterator": MapIteratorPrinter,
            "__tree_iterator": SetIteratorPrinter,
            "__tree_const_iterator": SetIteratorPrinter,
            "fpos": StdFposPrinter,
            "unordered_set": StdUnorderedSetPrinter,
            "unordered_multiset": StdUnorderedSetPrinter,
            "unordered_map": StdUnorderedMapPrinter,
            "unordered_multimap": StdUnorderedMapPrinter,
            "__hash_map_iterator": StdUnorderedMapIteratorPrinter,
            "__hash_map_const_iterator": StdUnorderedMapIteratorPrinter,
            "__hash_iterator": StdUnorderedSetIteratorPrinter,
            "__hash_const_iterator": StdUnorderedSetIteratorPrinter,
        }

        self.subprinters = []
        for name, subprinter in self.lookup.items():
            # Subprinters and names are used only for the rarely used command "info
            # pretty" (and related), so the name of the first data structure it prints
            # is a reasonable choice.
            if subprinter not in self.subprinters:
                subprinter.name = name
                self.subprinters.append(subprinter)

    def __call__(self, val):
        """Return the pretty printer for a val, if the type is supported."""

        # Do not handle any type that is not a struct/class.
        if val.type.strip_typedefs().code != gdb.TYPE_CODE_STRUCT:
            return None

        # Don't attempt types known to be inside libstdcxx.
        typename = val.type.name or val.type.tag or str(val.type)
        match = re.match("^std::(__.*?)::", typename)
        if match is not None and match.group(1) in [
            "__cxx1998",
            "__debug",
            "__7",
            "__g",
        ]:
            return None

        # Handle any using declarations or other typedefs.
        typename = _prettify_typename(val.type)
        if not typename:
            return None
        without_generics = _remove_generics(typename)
        lookup_name = _remove_std_prefix(without_generics)
        if lookup_name in self.lookup:
            return self.lookup[lookup_name](val)
        return None


_libcxx_printer_name = "libcxx_pretty_printer"


# These are called for every binary object file, which could be thousands in
# certain pathological cases. Limit our pretty printers to the progspace.
def _register_libcxx_printers(event):
    progspace = event.new_objfile.progspace
    # It would be ideal to get the endianness at print time, but
    # gdb.execute clears gdb's internal wrap buffer, removing any values
    # already generated as part of a larger data structure, and there is
    # no python api to get the endianness. Mixed-endianness debugging
    # rare enough that this workaround should be adequate.
    _libcpp_big_endian = "big endian" in gdb.execute("show endian", to_string=True)

    if not getattr(progspace, _libcxx_printer_name, False):
        print("Loading libc++ pretty-printers.")
        gdb.printing.register_pretty_printer(
            progspace, LibcxxPrettyPrinter(_libcxx_printer_name)
        )
        setattr(progspace, _libcxx_printer_name, True)


def _unregister_libcxx_printers(event):
    progspace = event.progspace
    if getattr(progspace, _libcxx_printer_name, False):
        for printer in progspace.pretty_printers:
            if getattr(printer, "name", "none") == _libcxx_printer_name:
                progspace.pretty_printers.remove(printer)
                setattr(progspace, _libcxx_printer_name, False)
                break


def register_libcxx_printer_loader():
    """Register event handlers to load libc++ pretty-printers."""
    gdb.events.new_objfile.connect(_register_libcxx_printers)
    gdb.events.clear_objfiles.connect(_unregister_libcxx_printers)
