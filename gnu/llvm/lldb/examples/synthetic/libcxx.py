import lldb
import lldb.formatters.Logger

# libcxx STL formatters for LLDB
# These formatters are based upon the implementation of libc++ that
# ships with current releases of OS X - They will not work for other implementations
# of the standard C++ library - and they are bound to use the
# libc++-specific namespace

# the std::string summary is just an example for your convenience
# the actual summary that LLDB uses is C++ code inside the debugger's own core

# this could probably be made more efficient but since it only reads a handful of bytes at a time
# we probably don't need to worry too much about this for the time being


def make_string(F, L):
    strval = ""
    G = F.GetData().uint8
    for X in range(L):
        V = G[X]
        if V == 0:
            break
        strval = strval + chr(V % 256)
    return '"' + strval + '"'


# if we ever care about big-endian, these two functions might need to change


def is_short_string(value):
    return True if (value & 1) == 0 else False


def extract_short_size(value):
    return (value >> 1) % 256


# some of the members of libc++ std::string are anonymous or have internal names that convey
# no external significance - we access them by index since this saves a name lookup that would add
# no information for readers of the code, but when possible try to use
# meaningful variable names


def stdstring_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    r = valobj.GetChildAtIndex(0)
    B = r.GetChildAtIndex(0)
    first = B.GetChildAtIndex(0)
    D = first.GetChildAtIndex(0)
    l = D.GetChildAtIndex(0)
    s = D.GetChildAtIndex(1)
    D20 = s.GetChildAtIndex(0)
    size_mode = D20.GetChildAtIndex(0).GetValueAsUnsigned(0)
    if is_short_string(size_mode):
        size = extract_short_size(size_mode)
        return make_string(s.GetChildAtIndex(1), size)
    else:
        data_ptr = l.GetChildAtIndex(2)
        size_vo = l.GetChildAtIndex(1)
        # the NULL terminator must be accounted for
        size = size_vo.GetValueAsUnsigned(0) + 1
        if size <= 1 or size is None:  # should never be the case
            return '""'
        try:
            data = data_ptr.GetPointeeData(0, size)
        except:
            return '""'
        error = lldb.SBError()
        strval = data.GetString(error, 0)
        if error.Fail():
            return "<error:" + error.GetCString() + ">"
        else:
            return '"' + strval + '"'


class stdvector_SynthProvider:
    def __init__(self, valobj, dict):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj

    def num_children(self):
        logger = lldb.formatters.Logger.Logger()
        try:
            start_val = self.start.GetValueAsUnsigned(0)
            finish_val = self.finish.GetValueAsUnsigned(0)
            # Before a vector has been constructed, it will contain bad values
            # so we really need to be careful about the length we return since
            # uninitialized data can cause us to return a huge number. We need
            # to also check for any of the start, finish or end of storage values
            # being zero (NULL). If any are, then this vector has not been
            # initialized yet and we should return zero

            # Make sure nothing is NULL
            if start_val == 0 or finish_val == 0:
                return 0
            # Make sure start is less than finish
            if start_val >= finish_val:
                return 0

            num_children = finish_val - start_val
            if (num_children % self.data_size) != 0:
                return 0
            else:
                num_children = num_children / self.data_size
            return num_children
        except:
            return 0

    def get_child_index(self, name):
        logger = lldb.formatters.Logger.Logger()
        try:
            return int(name.lstrip("[").rstrip("]"))
        except:
            return -1

    def get_child_at_index(self, index):
        logger = lldb.formatters.Logger.Logger()
        logger >> "Retrieving child " + str(index)
        if index < 0:
            return None
        if index >= self.num_children():
            return None
        try:
            offset = index * self.data_size
            return self.start.CreateChildAtOffset(
                "[" + str(index) + "]", offset, self.data_type
            )
        except:
            return None

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        try:
            self.start = self.valobj.GetChildMemberWithName("__begin_")
            self.finish = self.valobj.GetChildMemberWithName("__end_")
            # the purpose of this field is unclear, but it is the only field whose type is clearly T* for a vector<T>
            # if this ends up not being correct, we can use the APIs to get at
            # template arguments
            data_type_finder = self.valobj.GetChildMemberWithName(
                "__end_cap_"
            ).GetChildMemberWithName("__first_")
            self.data_type = data_type_finder.GetType().GetPointeeType()
            self.data_size = self.data_type.GetByteSize()
        except:
            pass

    def has_children(self):
        return True


# Just an example: the actual summary is produced by a summary string:
# size=${svar%#}


def stdvector_SummaryProvider(valobj, dict):
    prov = stdvector_SynthProvider(valobj, None)
    return "size=" + str(prov.num_children())


class stdlist_entry:
    def __init__(self, entry):
        logger = lldb.formatters.Logger.Logger()
        self.entry = entry

    def _next_impl(self):
        logger = lldb.formatters.Logger.Logger()
        return stdlist_entry(self.entry.GetChildMemberWithName("__next_"))

    def _prev_impl(self):
        logger = lldb.formatters.Logger.Logger()
        return stdlist_entry(self.entry.GetChildMemberWithName("__prev_"))

    def _value_impl(self):
        logger = lldb.formatters.Logger.Logger()
        return self.entry.GetValueAsUnsigned(0)

    def _isnull_impl(self):
        logger = lldb.formatters.Logger.Logger()
        return self._value_impl() == 0

    def _sbvalue_impl(self):
        logger = lldb.formatters.Logger.Logger()
        return self.entry

    next = property(_next_impl, None)
    value = property(_value_impl, None)
    is_null = property(_isnull_impl, None)
    sbvalue = property(_sbvalue_impl, None)


class stdlist_iterator:
    def increment_node(self, node):
        logger = lldb.formatters.Logger.Logger()
        if node.is_null:
            return None
        return node.next

    def __init__(self, node):
        logger = lldb.formatters.Logger.Logger()
        # we convert the SBValue to an internal node object on entry
        self.node = stdlist_entry(node)

    def value(self):
        logger = lldb.formatters.Logger.Logger()
        return self.node.sbvalue  # and return the SBValue back on exit

    def next(self):
        logger = lldb.formatters.Logger.Logger()
        node = self.increment_node(self.node)
        if node is not None and node.sbvalue.IsValid() and not (node.is_null):
            self.node = node
            return self.value()
        else:
            return None

    def advance(self, N):
        logger = lldb.formatters.Logger.Logger()
        if N < 0:
            return None
        if N == 0:
            return self.value()
        if N == 1:
            return self.next()
        while N > 0:
            self.next()
            N = N - 1
        return self.value()


class stdlist_SynthProvider:
    def __init__(self, valobj, dict):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.count = None

    def next_node(self, node):
        logger = lldb.formatters.Logger.Logger()
        return node.GetChildMemberWithName("__next_")

    def value(self, node):
        logger = lldb.formatters.Logger.Logger()
        return node.GetValueAsUnsigned()

    # Floyd's cycle-finding algorithm
    # try to detect if this list has a loop
    def has_loop(self):
        global _list_uses_loop_detector
        logger = lldb.formatters.Logger.Logger()
        if not _list_uses_loop_detector:
            logger >> "Asked not to use loop detection"
            return False
        slow = stdlist_entry(self.head)
        fast1 = stdlist_entry(self.head)
        fast2 = stdlist_entry(self.head)
        while slow.next.value != self.node_address:
            slow_value = slow.value
            fast1 = fast2.next
            fast2 = fast1.next
            if fast1.value == slow_value or fast2.value == slow_value:
                return True
            slow = slow.next
        return False

    def num_children(self):
        global _list_capping_size
        logger = lldb.formatters.Logger.Logger()
        if self.count is None:
            self.count = self.num_children_impl()
            if self.count > _list_capping_size:
                self.count = _list_capping_size
        return self.count

    def num_children_impl(self):
        global _list_capping_size
        logger = lldb.formatters.Logger.Logger()
        try:
            next_val = self.head.GetValueAsUnsigned(0)
            prev_val = self.tail.GetValueAsUnsigned(0)
            # After a std::list has been initialized, both next and prev will
            # be non-NULL
            if next_val == 0 or prev_val == 0:
                return 0
            if next_val == self.node_address:
                return 0
            if next_val == prev_val:
                return 1
            if self.has_loop():
                return 0
            size = 2
            current = stdlist_entry(self.head)
            while current.next.value != self.node_address:
                size = size + 1
                current = current.next
                if size > _list_capping_size:
                    return _list_capping_size
            return size - 1
        except:
            return 0

    def get_child_index(self, name):
        logger = lldb.formatters.Logger.Logger()
        try:
            return int(name.lstrip("[").rstrip("]"))
        except:
            return -1

    def get_child_at_index(self, index):
        logger = lldb.formatters.Logger.Logger()
        logger >> "Fetching child " + str(index)
        if index < 0:
            return None
        if index >= self.num_children():
            return None
        try:
            current = stdlist_iterator(self.head)
            current = current.advance(index)
            # we do not return __value_ because then all our children would be named __value_
            # we need to make a copy of __value__ with the right name -
            # unfortunate
            obj = current.GetChildMemberWithName("__value_")
            obj_data = obj.GetData()
            return self.valobj.CreateValueFromData(
                "[" + str(index) + "]", obj_data, self.data_type
            )
        except:
            return None

    def extract_type(self):
        logger = lldb.formatters.Logger.Logger()
        list_type = self.valobj.GetType().GetUnqualifiedType()
        if list_type.IsReferenceType():
            list_type = list_type.GetDereferencedType()
        if list_type.GetNumberOfTemplateArguments() > 0:
            data_type = list_type.GetTemplateArgumentType(0)
        else:
            data_type = None
        return data_type

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.count = None
        try:
            impl = self.valobj.GetChildMemberWithName("__end_")
            self.node_address = self.valobj.AddressOf().GetValueAsUnsigned(0)
            self.head = impl.GetChildMemberWithName("__next_")
            self.tail = impl.GetChildMemberWithName("__prev_")
            self.data_type = self.extract_type()
            self.data_size = self.data_type.GetByteSize()
        except:
            pass

    def has_children(self):
        return True


# Just an example: the actual summary is produced by a summary string:
# size=${svar%#}
def stdlist_SummaryProvider(valobj, dict):
    prov = stdlist_SynthProvider(valobj, None)
    return "size=" + str(prov.num_children())


# a tree node - this class makes the syntax in the actual iterator nicer
# to read and maintain


class stdmap_iterator_node:
    def _left_impl(self):
        logger = lldb.formatters.Logger.Logger()
        return stdmap_iterator_node(self.node.GetChildMemberWithName("__left_"))

    def _right_impl(self):
        logger = lldb.formatters.Logger.Logger()
        return stdmap_iterator_node(self.node.GetChildMemberWithName("__right_"))

    def _parent_impl(self):
        logger = lldb.formatters.Logger.Logger()
        return stdmap_iterator_node(self.node.GetChildMemberWithName("__parent_"))

    def _value_impl(self):
        logger = lldb.formatters.Logger.Logger()
        return self.node.GetValueAsUnsigned(0)

    def _sbvalue_impl(self):
        logger = lldb.formatters.Logger.Logger()
        return self.node

    def _null_impl(self):
        logger = lldb.formatters.Logger.Logger()
        return self.value == 0

    def __init__(self, node):
        logger = lldb.formatters.Logger.Logger()
        self.node = node

    left = property(_left_impl, None)
    right = property(_right_impl, None)
    parent = property(_parent_impl, None)
    value = property(_value_impl, None)
    is_null = property(_null_impl, None)
    sbvalue = property(_sbvalue_impl, None)


# a Python implementation of the tree iterator used by libc++


class stdmap_iterator:
    def tree_min(self, x):
        logger = lldb.formatters.Logger.Logger()
        steps = 0
        if x.is_null:
            return None
        while not x.left.is_null:
            x = x.left
            steps += 1
            if steps > self.max_count:
                logger >> "Returning None - we overflowed"
                return None
        return x

    def tree_max(self, x):
        logger = lldb.formatters.Logger.Logger()
        if x.is_null:
            return None
        while not x.right.is_null:
            x = x.right
        return x

    def tree_is_left_child(self, x):
        logger = lldb.formatters.Logger.Logger()
        if x.is_null:
            return None
        return True if x.value == x.parent.left.value else False

    def increment_node(self, node):
        logger = lldb.formatters.Logger.Logger()
        if node.is_null:
            return None
        if not node.right.is_null:
            return self.tree_min(node.right)
        steps = 0
        while not self.tree_is_left_child(node):
            steps += 1
            if steps > self.max_count:
                logger >> "Returning None - we overflowed"
                return None
            node = node.parent
        return node.parent

    def __init__(self, node, max_count=0):
        logger = lldb.formatters.Logger.Logger()
        # we convert the SBValue to an internal node object on entry
        self.node = stdmap_iterator_node(node)
        self.max_count = max_count

    def value(self):
        logger = lldb.formatters.Logger.Logger()
        return self.node.sbvalue  # and return the SBValue back on exit

    def next(self):
        logger = lldb.formatters.Logger.Logger()
        node = self.increment_node(self.node)
        if node is not None and node.sbvalue.IsValid() and not (node.is_null):
            self.node = node
            return self.value()
        else:
            return None

    def advance(self, N):
        logger = lldb.formatters.Logger.Logger()
        if N < 0:
            return None
        if N == 0:
            return self.value()
        if N == 1:
            return self.next()
        while N > 0:
            if self.next() is None:
                return None
            N = N - 1
        return self.value()


class stdmap_SynthProvider:
    def __init__(self, valobj, dict):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.pointer_size = self.valobj.GetProcess().GetAddressByteSize()
        self.count = None

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.count = None
        try:
            # we will set this to True if we find out that discovering a node in the map takes more steps than the overall size of the RB tree
            # if this gets set to True, then we will merrily return None for
            # any child from that moment on
            self.garbage = False
            self.tree = self.valobj.GetChildMemberWithName("__tree_")
            self.root_node = self.tree.GetChildMemberWithName("__begin_node_")
            # this data is either lazily-calculated, or cannot be inferred at this moment
            # we still need to mark it as None, meaning "please set me ASAP"
            self.data_type = None
            self.data_size = None
            self.skip_size = None
        except:
            pass

    def num_children(self):
        global _map_capping_size
        logger = lldb.formatters.Logger.Logger()
        if self.count is None:
            self.count = self.num_children_impl()
            if self.count > _map_capping_size:
                self.count = _map_capping_size
        return self.count

    def num_children_impl(self):
        logger = lldb.formatters.Logger.Logger()
        try:
            return (
                self.valobj.GetChildMemberWithName("__tree_")
                .GetChildMemberWithName("__pair3_")
                .GetChildMemberWithName("__first_")
                .GetValueAsUnsigned()
            )
        except:
            return 0

    def has_children(self):
        return True

    def get_data_type(self):
        logger = lldb.formatters.Logger.Logger()
        if self.data_type is None or self.data_size is None:
            if self.num_children() == 0:
                return False
            deref = self.root_node.Dereference()
            if not (deref.IsValid()):
                return False
            value = deref.GetChildMemberWithName("__value_")
            if not (value.IsValid()):
                return False
            self.data_type = value.GetType()
            self.data_size = self.data_type.GetByteSize()
            self.skip_size = None
            return True
        else:
            return True

    def get_value_offset(self, node):
        logger = lldb.formatters.Logger.Logger()
        if self.skip_size is None:
            node_type = node.GetType()
            fields_count = node_type.GetNumberOfFields()
            for i in range(fields_count):
                field = node_type.GetFieldAtIndex(i)
                if field.GetName() == "__value_":
                    self.skip_size = field.GetOffsetInBytes()
                    break
        return self.skip_size is not None

    def get_child_index(self, name):
        logger = lldb.formatters.Logger.Logger()
        try:
            return int(name.lstrip("[").rstrip("]"))
        except:
            return -1

    def get_child_at_index(self, index):
        logger = lldb.formatters.Logger.Logger()
        logger >> "Retrieving child " + str(index)
        if index < 0:
            return None
        if index >= self.num_children():
            return None
        if self.garbage:
            logger >> "Returning None since this tree is garbage"
            return None
        try:
            iterator = stdmap_iterator(self.root_node, max_count=self.num_children())
            # the debug info for libc++ std::map is such that __begin_node_ has a very nice and useful type
            # out of which we can grab the information we need - every other node has a less informative
            # type which omits all value information and only contains housekeeping information for the RB tree
            # hence, we need to know if we are at a node != 0, so that we can
            # still get at the data
            need_to_skip = index > 0
            current = iterator.advance(index)
            if current is None:
                logger >> "Tree is garbage - returning None"
                self.garbage = True
                return None
            if self.get_data_type():
                if not (need_to_skip):
                    current = current.Dereference()
                    obj = current.GetChildMemberWithName("__value_")
                    obj_data = obj.GetData()
                    # make sure we have a valid offset for the next items
                    self.get_value_offset(current)
                    # we do not return __value_ because then we would end up with a child named
                    # __value_ instead of [0]
                    return self.valobj.CreateValueFromData(
                        "[" + str(index) + "]", obj_data, self.data_type
                    )
                else:
                    # FIXME we need to have accessed item 0 before accessing
                    # any other item!
                    if self.skip_size is None:
                        (
                            logger
                            >> "You asked for item > 0 before asking for item == 0, I will fetch 0 now then retry"
                        )
                        if self.get_child_at_index(0):
                            return self.get_child_at_index(index)
                        else:
                            (
                                logger
                                >> "item == 0 could not be found. sorry, nothing can be done here."
                            )
                            return None
                    return current.CreateChildAtOffset(
                        "[" + str(index) + "]", self.skip_size, self.data_type
                    )
            else:
                (
                    logger
                    >> "Unable to infer data-type - returning None (should mark tree as garbage here?)"
                )
                return None
        except Exception as err:
            logger >> "Hit an exception: " + str(err)
            return None


# Just an example: the actual summary is produced by a summary string:
# size=${svar%#}


def stdmap_SummaryProvider(valobj, dict):
    prov = stdmap_SynthProvider(valobj, None)
    return "size=" + str(prov.num_children())


class stddeque_SynthProvider:
    def __init__(self, valobj, d):
        logger = lldb.formatters.Logger.Logger()
        logger.write("init")
        self.valobj = valobj
        self.pointer_size = self.valobj.GetProcess().GetAddressByteSize()
        self.count = None
        try:
            self.find_block_size()
        except:
            self.block_size = -1
            self.element_size = -1
        logger.write(
            "block_size=%d, element_size=%d" % (self.block_size, self.element_size)
        )

    def find_block_size(self):
        # in order to use the deque we must have the block size, or else
        # it's impossible to know what memory addresses are valid
        obj_type = self.valobj.GetType()
        if obj_type.IsReferenceType():
            obj_type = obj_type.GetDereferencedType()
        elif obj_type.IsPointerType():
            obj_type = obj_type.GetPointeeType()
        self.element_type = obj_type.GetTemplateArgumentType(0)
        self.element_size = self.element_type.GetByteSize()
        # The code says this, but there must be a better way:
        # template <class _Tp, class _Allocator>
        # class __deque_base {
        #    static const difference_type __block_size = sizeof(value_type) < 256 ? 4096 / sizeof(value_type) : 16;
        # }
        if self.element_size < 256:
            self.block_size = 4096 // self.element_size
        else:
            self.block_size = 16

    def num_children(self):
        logger = lldb.formatters.Logger.Logger()
        if self.count is None:
            return 0
        return self.count

    def has_children(self):
        return True

    def get_child_index(self, name):
        logger = lldb.formatters.Logger.Logger()
        try:
            return int(name.lstrip("[").rstrip("]"))
        except:
            return -1

    def get_child_at_index(self, index):
        logger = lldb.formatters.Logger.Logger()
        logger.write("Fetching child " + str(index))
        if index < 0 or self.count is None:
            return None
        if index >= self.num_children():
            return None
        try:
            i, j = divmod(self.start + index, self.block_size)

            return self.first.CreateValueFromExpression(
                "[" + str(index) + "]",
                "*(*(%s + %d) + %d)" % (self.map_begin.get_expr_path(), i, j),
            )
        except:
            return None

    def _get_value_of_compressed_pair(self, pair):
        value = pair.GetChildMemberWithName("__value_")
        if not value.IsValid():
            # pre-r300140 member name
            value = pair.GetChildMemberWithName("__first_")
        return value.GetValueAsUnsigned(0)

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        try:
            # A deque is effectively a two-dim array, with fixed width.
            # 'map' contains pointers to the rows of this array. The
            # full memory area allocated by the deque is delimited
            # by 'first' and 'end_cap'. However, only a subset of this
            # memory contains valid data since a deque may have some slack
            # at the front and back in order to have O(1) insertion at
            # both ends. The rows in active use are delimited by
            # 'begin' and 'end'.
            #
            # To find the elements that are actually constructed, the 'start'
            # variable tells which element in this NxM array is the 0th
            # one, and the 'size' element gives the number of elements
            # in the deque.
            count = self._get_value_of_compressed_pair(
                self.valobj.GetChildMemberWithName("__size_")
            )
            # give up now if we cant access memory reliably
            if self.block_size < 0:
                logger.write("block_size < 0")
                return
            map_ = self.valobj.GetChildMemberWithName("__map_")
            start = self.valobj.GetChildMemberWithName("__start_").GetValueAsUnsigned(0)
            first = map_.GetChildMemberWithName("__first_")
            map_first = first.GetValueAsUnsigned(0)
            self.map_begin = map_.GetChildMemberWithName("__begin_")
            map_begin = self.map_begin.GetValueAsUnsigned(0)
            map_end = map_.GetChildMemberWithName("__end_").GetValueAsUnsigned(0)
            map_endcap = self._get_value_of_compressed_pair(
                map_.GetChildMemberWithName("__end_cap_")
            )

            # check consistency
            if not map_first <= map_begin <= map_end <= map_endcap:
                logger.write("map pointers are not monotonic")
                return
            total_rows, junk = divmod(map_endcap - map_first, self.pointer_size)
            if junk:
                logger.write("endcap-first doesnt align correctly")
                return
            active_rows, junk = divmod(map_end - map_begin, self.pointer_size)
            if junk:
                logger.write("end-begin doesnt align correctly")
                return
            start_row, junk = divmod(map_begin - map_first, self.pointer_size)
            if junk:
                logger.write("begin-first doesnt align correctly")
                return

            logger.write(
                "update success: count=%r, start=%r, first=%r" % (count, start, first)
            )
            # if consistent, save all we really need:
            self.count = count
            self.start = start
            self.first = first
        except:
            self.count = None
            self.start = None
            self.map_first = None
            self.map_begin = None
        return False


class stdsharedptr_SynthProvider:
    def __init__(self, valobj, d):
        logger = lldb.formatters.Logger.Logger()
        logger.write("init")
        self.valobj = valobj
        # self.element_ptr_type = self.valobj.GetType().GetTemplateArgumentType(0).GetPointerType()
        self.ptr = None
        self.cntrl = None
        process = valobj.GetProcess()
        self.endianness = process.GetByteOrder()
        self.pointer_size = process.GetAddressByteSize()
        self.count_type = valobj.GetType().GetBasicType(lldb.eBasicTypeUnsignedLong)

    def num_children(self):
        return 1

    def has_children(self):
        return True

    def get_child_index(self, name):
        if name == "__ptr_":
            return 0
        if name == "count":
            return 1
        if name == "weak_count":
            return 2
        return -1

    def get_child_at_index(self, index):
        if index == 0:
            return self.ptr
        if index == 1:
            if self.cntrl is None:
                count = 0
            else:
                count = (
                    1
                    + self.cntrl.GetChildMemberWithName(
                        "__shared_owners_"
                    ).GetValueAsSigned()
                )
            return self.valobj.CreateValueFromData(
                "count",
                lldb.SBData.CreateDataFromUInt64Array(
                    self.endianness, self.pointer_size, [count]
                ),
                self.count_type,
            )
        if index == 2:
            if self.cntrl is None:
                count = 0
            else:
                count = (
                    1
                    + self.cntrl.GetChildMemberWithName(
                        "__shared_weak_owners_"
                    ).GetValueAsSigned()
                )
            return self.valobj.CreateValueFromData(
                "weak_count",
                lldb.SBData.CreateDataFromUInt64Array(
                    self.endianness, self.pointer_size, [count]
                ),
                self.count_type,
            )
        return None

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.ptr = self.valobj.GetChildMemberWithName(
            "__ptr_"
        )  # .Cast(self.element_ptr_type)
        cntrl = self.valobj.GetChildMemberWithName("__cntrl_")
        if cntrl.GetValueAsUnsigned(0):
            self.cntrl = cntrl.Dereference()
        else:
            self.cntrl = None


# we can use two different categories for old and new formatters - type names are different enough that we should make no confusion
# talking with libc++ developer: "std::__1::class_name is set in stone
# until we decide to change the ABI. That shouldn't happen within a 5 year
# time frame"


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand(
        'type summary add -F libcxx.stdstring_SummaryProvider "std::__1::string" -w libcxx'
    )
    debugger.HandleCommand(
        'type summary add -F libcxx.stdstring_SummaryProvider "std::__1::basic_string<char, class std::__1::char_traits<char>, class std::__1::allocator<char> >" -w libcxx'
    )
    debugger.HandleCommand(
        'type synthetic add -l libcxx.stdvector_SynthProvider -x "^(std::__1::)vector<.+>$" -w libcxx'
    )
    debugger.HandleCommand(
        'type summary add -F libcxx.stdvector_SummaryProvider -e -x "^(std::__1::)vector<.+>$" -w libcxx'
    )
    debugger.HandleCommand(
        'type synthetic add -l libcxx.stdlist_SynthProvider -x "^(std::__1::)list<.+>$" -w libcxx'
    )
    debugger.HandleCommand(
        'type summary add -F libcxx.stdlist_SummaryProvider -e -x "^(std::__1::)list<.+>$" -w libcxx'
    )
    debugger.HandleCommand(
        'type synthetic add -l libcxx.stdmap_SynthProvider -x "^(std::__1::)map<.+> >$" -w libcxx'
    )
    debugger.HandleCommand(
        'type summary add -F libcxx.stdmap_SummaryProvider -e -x "^(std::__1::)map<.+> >$" -w libcxx'
    )
    debugger.HandleCommand("type category enable libcxx")
    debugger.HandleCommand(
        'type synthetic add -l libcxx.stddeque_SynthProvider -x "^(std::__1::)deque<.+>$" -w libcxx'
    )
    debugger.HandleCommand(
        'type synthetic add -l libcxx.stdsharedptr_SynthProvider -x "^(std::__1::)shared_ptr<.+>$" -w libcxx'
    )
    # turns out the structs look the same, so weak_ptr can be handled the same!
    debugger.HandleCommand(
        'type synthetic add -l libcxx.stdsharedptr_SynthProvider -x "^(std::__1::)weak_ptr<.+>$" -w libcxx'
    )


_map_capping_size = 255
_list_capping_size = 255
_list_uses_loop_detector = True
