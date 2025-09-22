import lldb.formatters.Logger

# C++ STL formatters for LLDB
# As there are many versions of the libstdc++, you are encouraged to look at the STL
# implementation for your platform before relying on these formatters to do the right
# thing for your setup


def ForwardListSummaryProvider(valobj, dict):
    list_capping_size = valobj.GetTarget().GetMaximumNumberOfChildrenToDisplay()
    text = "size=" + str(valobj.GetNumChildren())
    if valobj.GetNumChildren() > list_capping_size:
        return "(capped) " + text
    else:
        return text


def StdOptionalSummaryProvider(valobj, dict):
    has_value = valobj.GetNumChildren() > 0
    # We add wrapping spaces for consistency with the libcxx formatter
    return " Has Value=" + ("true" if has_value else "false") + " "


class StdOptionalSynthProvider:
    def __init__(self, valobj, dict):
        self.valobj = valobj

    def update(self):
        try:
            self.payload = self.valobj.GetChildMemberWithName("_M_payload")
            self.value = self.payload.GetChildMemberWithName("_M_payload")
            self.has_value = (
                self.payload.GetChildMemberWithName("_M_engaged").GetValueAsUnsigned(0)
                != 0
            )
        except:
            self.has_value = False
        return False

    def num_children(self):
        return 1 if self.has_value else 0

    def get_child_index(self, name):
        return 0

    def get_child_at_index(self, index):
        # some versions of libstdcpp have an additional _M_value child with the actual value
        possible_value = self.value.GetChildMemberWithName("_M_value")
        if possible_value.IsValid():
            return possible_value.Clone("Value")
        return self.value.Clone("Value")


"""
 This formatter can be applied to all
 unordered map-like structures (unordered_map, unordered_multimap, unordered_set, unordered_multiset)
"""


class StdUnorderedMapSynthProvider:
    def __init__(self, valobj, dict):
        self.valobj = valobj
        self.count = None
        self.kind = self.get_object_kind(valobj)

    def get_object_kind(self, valobj):
        type_name = valobj.GetTypeName()
        return "set" if "set" in type_name else "map"

    def extract_type(self):
        type = self.valobj.GetType()
        # type of std::pair<key, value> is the first template
        # argument type of the 4th template argument to std::map and
        # 3rd template argument for std::set. That's why
        # we need to know kind of the object
        template_arg_num = 4 if self.kind == "map" else 3
        allocator_type = type.GetTemplateArgumentType(template_arg_num)
        data_type = allocator_type.GetTemplateArgumentType(0)
        return data_type

    def update(self):
        # preemptively setting this to None - we might end up changing our mind
        # later
        self.count = None
        try:
            self.head = self.valobj.GetChildMemberWithName("_M_h")
            self.before_begin = self.head.GetChildMemberWithName("_M_before_begin")
            self.next = self.before_begin.GetChildMemberWithName("_M_nxt")
            self.data_type = self.extract_type()
            self.skip_size = self.next.GetType().GetByteSize()
            self.data_size = self.data_type.GetByteSize()
            if (not self.data_type.IsValid()) or (not self.next.IsValid()):
                self.count = 0
        except:
            self.count = 0
        return False

    def get_child_index(self, name):
        try:
            return int(name.lstrip("[").rstrip("]"))
        except:
            return -1

    def get_child_at_index(self, index):
        logger = lldb.formatters.Logger.Logger()
        logger >> "Being asked to fetch child[" + str(index) + "]"
        if index < 0:
            return None
        if index >= self.num_children():
            return None
        try:
            offset = index
            current = self.next
            while offset > 0:
                current = current.GetChildMemberWithName("_M_nxt")
                offset = offset - 1
            return current.CreateChildAtOffset(
                "[" + str(index) + "]", self.skip_size, self.data_type
            )

        except:
            logger >> "Cannot get child"
            return None

    def num_children(self):
        if self.count is None:
            self.count = self.num_children_impl()
        return self.count

    def num_children_impl(self):
        logger = lldb.formatters.Logger.Logger()
        try:
            count = self.head.GetChildMemberWithName(
                "_M_element_count"
            ).GetValueAsUnsigned(0)
            return count
        except:
            logger >> "Could not determine the size"
            return 0


class AbstractListSynthProvider:
    def __init__(self, valobj, dict, has_prev):
        """
        :param valobj: The value object of the list
        :param dict: A dict with metadata provided by LLDB
        :param has_prev: Whether the list supports a 'prev' pointer besides a 'next' one
        """
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.count = None
        self.has_prev = has_prev
        self.list_capping_size = (
            self.valobj.GetTarget().GetMaximumNumberOfChildrenToDisplay()
        )
        logger >> "Providing synthetic children for a list named " + str(
            valobj.GetName()
        )

    def next_node(self, node):
        logger = lldb.formatters.Logger.Logger()
        return node.GetChildMemberWithName("_M_next")

    def is_valid(self, node):
        logger = lldb.formatters.Logger.Logger()
        valid = self.value(self.next_node(node)) != self.get_end_of_list_address()
        if valid:
            logger >> "%s is valid" % str(self.valobj.GetName())
        else:
            logger >> "synthetic value is not valid"
        return valid

    def value(self, node):
        logger = lldb.formatters.Logger.Logger()
        value = node.GetValueAsUnsigned()
        logger >> "synthetic value for {}: {}".format(str(self.valobj.GetName()), value)
        return value

    # Floyd's cycle-finding algorithm
    # try to detect if this list has a loop
    def has_loop(self):
        global _list_uses_loop_detector
        logger = lldb.formatters.Logger.Logger()
        if not _list_uses_loop_detector:
            logger >> "Asked not to use loop detection"
            return False
        slow = self.next
        fast1 = self.next
        fast2 = self.next
        while self.is_valid(slow):
            slow_value = self.value(slow)
            fast1 = self.next_node(fast2)
            fast2 = self.next_node(fast1)
            if self.value(fast1) == slow_value or self.value(fast2) == slow_value:
                return True
            slow = self.next_node(slow)
        return False

    def num_children(self):
        logger = lldb.formatters.Logger.Logger()
        if self.count is None:
            # libstdc++ 6.0.21 added dedicated count field.
            count_child = self.node.GetChildMemberWithName("_M_data")
            if count_child and count_child.IsValid():
                self.count = count_child.GetValueAsUnsigned(0)
            if self.count is None:
                self.count = self.num_children_impl()
        return self.count

    def num_children_impl(self):
        logger = lldb.formatters.Logger.Logger()
        try:
            # After a std::list has been initialized, both next and prev will
            # be non-NULL
            next_val = self.next.GetValueAsUnsigned(0)
            if next_val == 0:
                return 0
            if self.has_loop():
                return 0
            if self.has_prev:
                prev_val = self.prev.GetValueAsUnsigned(0)
                if prev_val == 0:
                    return 0
                if next_val == self.node_address:
                    return 0
                if next_val == prev_val:
                    return 1
            size = 1
            current = self.next
            while (
                current.GetChildMemberWithName("_M_next").GetValueAsUnsigned(0)
                != self.get_end_of_list_address()
            ):
                current = current.GetChildMemberWithName("_M_next")
                if not current.IsValid():
                    break
                size = size + 1
                if size >= self.list_capping_size:
                    break

            return size
        except:
            logger >> "Error determining the size"
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
            offset = index
            current = self.next
            while offset > 0:
                current = current.GetChildMemberWithName("_M_next")
                offset = offset - 1
            # C++ lists store the data of a node after its pointers. In the case of a forward list, there's just one pointer (next), and
            # in the case of a double-linked list, there's an additional pointer (prev).
            return current.CreateChildAtOffset(
                "[" + str(index) + "]",
                (2 if self.has_prev else 1) * current.GetType().GetByteSize(),
                self.data_type,
            )
        except:
            return None

    def extract_type(self):
        logger = lldb.formatters.Logger.Logger()
        list_type = self.valobj.GetType().GetUnqualifiedType()
        if list_type.IsReferenceType():
            list_type = list_type.GetDereferencedType()
        if list_type.GetNumberOfTemplateArguments() > 0:
            return list_type.GetTemplateArgumentType(0)
        return lldb.SBType()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        # preemptively setting this to None - we might end up changing our mind
        # later
        self.count = None
        try:
            self.impl = self.valobj.GetChildMemberWithName("_M_impl")
            self.data_type = self.extract_type()
            if (not self.data_type.IsValid()) or (not self.impl.IsValid()):
                self.count = 0
            elif not self.updateNodes():
                self.count = 0
            else:
                self.data_size = self.data_type.GetByteSize()
        except:
            self.count = 0
        return False

    """
    Method is used to extract the list pointers into the variables (e.g self.node, self.next, and optionally to self.prev)
    and is mandatory to be overriden in each AbstractListSynthProvider subclass.
    This should return True or False depending on wheter it found valid data.
    """

    def updateNodes(self):
        raise NotImplementedError

    def has_children(self):
        return True

    """
     Method is used to identify if a node traversal has reached its end
     and is mandatory to be overriden in each AbstractListSynthProvider subclass
    """

    def get_end_of_list_address(self):
        raise NotImplementedError


class StdForwardListSynthProvider(AbstractListSynthProvider):
    def __init__(self, valobj, dict):
        has_prev = False
        super().__init__(valobj, dict, has_prev)

    def updateNodes(self):
        self.node = self.impl.GetChildMemberWithName("_M_head")
        self.next = self.node.GetChildMemberWithName("_M_next")
        if (not self.node.IsValid()) or (not self.next.IsValid()):
            return False
        return True

    def get_end_of_list_address(self):
        return 0


class StdListSynthProvider(AbstractListSynthProvider):
    def __init__(self, valobj, dict):
        has_prev = True
        super().__init__(valobj, dict, has_prev)

    def updateNodes(self):
        self.node_address = self.valobj.AddressOf().GetValueAsUnsigned(0)
        self.node = self.impl.GetChildMemberWithName("_M_node")
        self.prev = self.node.GetChildMemberWithName("_M_prev")
        self.next = self.node.GetChildMemberWithName("_M_next")
        if (
            self.node_address == 0
            or (not self.node.IsValid())
            or (not self.next.IsValid())
            or (not self.prev.IsValid())
        ):
            return False
        return True

    def get_end_of_list_address(self):
        return self.node_address


class StdVectorSynthProvider:
    class StdVectorImplementation(object):
        def __init__(self, valobj):
            self.valobj = valobj
            self.count = None

        def num_children(self):
            if self.count is None:
                self.count = self.num_children_impl()
            return self.count

        def num_children_impl(self):
            try:
                start_val = self.start.GetValueAsUnsigned(0)
                finish_val = self.finish.GetValueAsUnsigned(0)
                end_val = self.end.GetValueAsUnsigned(0)
                # Before a vector has been constructed, it will contain bad values
                # so we really need to be careful about the length we return since
                # uninitialized data can cause us to return a huge number. We need
                # to also check for any of the start, finish or end of storage values
                # being zero (NULL). If any are, then this vector has not been
                # initialized yet and we should return zero

                # Make sure nothing is NULL
                if start_val == 0 or finish_val == 0 or end_val == 0:
                    return 0
                # Make sure start is less than finish
                if start_val >= finish_val:
                    return 0
                # Make sure finish is less than or equal to end of storage
                if finish_val > end_val:
                    return 0

                # if we have a struct (or other data type that the compiler pads to native word size)
                # this check might fail, unless the sizeof() we get is itself incremented to take the
                # padding bytes into account - on current clang it looks like
                # this is the case
                num_children = finish_val - start_val
                if (num_children % self.data_size) != 0:
                    return 0
                else:
                    num_children = num_children // self.data_size
                return num_children
            except:
                return 0

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
            # preemptively setting this to None - we might end up changing our
            # mind later
            self.count = None
            try:
                impl = self.valobj.GetChildMemberWithName("_M_impl")
                self.start = impl.GetChildMemberWithName("_M_start")
                self.finish = impl.GetChildMemberWithName("_M_finish")
                self.end = impl.GetChildMemberWithName("_M_end_of_storage")
                self.data_type = self.start.GetType().GetPointeeType()
                self.data_size = self.data_type.GetByteSize()
                # if any of these objects is invalid, it means there is no
                # point in trying to fetch anything
                if (
                    self.start.IsValid()
                    and self.finish.IsValid()
                    and self.end.IsValid()
                    and self.data_type.IsValid()
                ):
                    self.count = None
                else:
                    self.count = 0
            except:
                self.count = 0
            return False

    class StdVBoolImplementation(object):
        def __init__(self, valobj, bool_type):
            self.valobj = valobj
            self.bool_type = bool_type
            self.valid = False

        def num_children(self):
            if self.valid:
                start = self.start_p.GetValueAsUnsigned(0)
                finish = self.finish_p.GetValueAsUnsigned(0)
                offset = self.offset.GetValueAsUnsigned(0)
                if finish >= start:
                    return (finish - start) * 8 + offset
            return 0

        def get_child_at_index(self, index):
            if index >= self.num_children():
                return None
            element_type = self.start_p.GetType().GetPointeeType()
            element_bits = 8 * element_type.GetByteSize()
            element_offset = (index // element_bits) * element_type.GetByteSize()
            bit_offset = index % element_bits
            element = self.start_p.CreateChildAtOffset(
                "[" + str(index) + "]", element_offset, element_type
            )
            bit = element.GetValueAsUnsigned(0) & (1 << bit_offset)
            if bit != 0:
                value_expr = "(bool)true"
            else:
                value_expr = "(bool)false"
            return self.valobj.CreateValueFromExpression("[%d]" % index, value_expr)

        def update(self):
            try:
                m_impl = self.valobj.GetChildMemberWithName("_M_impl")
                self.m_start = m_impl.GetChildMemberWithName("_M_start")
                self.m_finish = m_impl.GetChildMemberWithName("_M_finish")
                self.start_p = self.m_start.GetChildMemberWithName("_M_p")
                self.finish_p = self.m_finish.GetChildMemberWithName("_M_p")
                self.offset = self.m_finish.GetChildMemberWithName("_M_offset")
                if (
                    self.offset.IsValid()
                    and self.start_p.IsValid()
                    and self.finish_p.IsValid()
                ):
                    self.valid = True
                else:
                    self.valid = False
            except:
                self.valid = False
            return False

    def __init__(self, valobj, dict):
        logger = lldb.formatters.Logger.Logger()
        first_template_arg_type = valobj.GetType().GetTemplateArgumentType(0)
        if str(first_template_arg_type.GetName()) == "bool":
            self.impl = self.StdVBoolImplementation(valobj, first_template_arg_type)
        else:
            self.impl = self.StdVectorImplementation(valobj)
        logger >> "Providing synthetic children for a vector named " + str(
            valobj.GetName()
        )

    def num_children(self):
        return self.impl.num_children()

    def get_child_index(self, name):
        try:
            return int(name.lstrip("[").rstrip("]"))
        except:
            return -1

    def get_child_at_index(self, index):
        return self.impl.get_child_at_index(index)

    def update(self):
        return self.impl.update()

    def has_children(self):
        return True

    """
     This formatter can be applied to all
     map-like structures (map, multimap, set, multiset)
    """


class StdMapLikeSynthProvider:
    def __init__(self, valobj, dict):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.count = None
        self.kind = self.get_object_kind(valobj)
        (
            logger
            >> "Providing synthetic children for a "
            + self.kind
            + " named "
            + str(valobj.GetName())
        )

    def get_object_kind(self, valobj):
        type_name = valobj.GetTypeName()
        for kind in ["multiset", "multimap", "set", "map"]:
            if kind in type_name:
                return kind
        return type_name

    # we need this function as a temporary workaround for rdar://problem/10801549
    # which prevents us from extracting the std::pair<K,V> SBType out of the template
    # arguments for _Rep_Type _M_t in the object itself - because we have to make up the
    # typename and then find it, we may hit the situation were std::string has multiple
    # names but only one is actually referenced in the debug information. hence, we need
    # to replace the longer versions of std::string with the shorter one in order to be able
    # to find the type name
    def fixup_class_name(self, class_name):
        logger = lldb.formatters.Logger.Logger()
        if (
            class_name
            == "std::basic_string<char, std::char_traits<char>, std::allocator<char> >"
        ):
            return "std::basic_string<char>", True
        if (
            class_name
            == "basic_string<char, std::char_traits<char>, std::allocator<char> >"
        ):
            return "std::basic_string<char>", True
        if (
            class_name
            == "std::basic_string<char, std::char_traits<char>, std::allocator<char> >"
        ):
            return "std::basic_string<char>", True
        if (
            class_name
            == "basic_string<char, std::char_traits<char>, std::allocator<char> >"
        ):
            return "std::basic_string<char>", True
        return class_name, False

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        # preemptively setting this to None - we might end up changing our mind
        # later
        self.count = None
        try:
            # we will set this to True if we find out that discovering a node in the object takes more steps than the overall size of the RB tree
            # if this gets set to True, then we will merrily return None for
            # any child from that moment on
            self.garbage = False
            self.Mt = self.valobj.GetChildMemberWithName("_M_t")
            self.Mimpl = self.Mt.GetChildMemberWithName("_M_impl")
            self.Mheader = self.Mimpl.GetChildMemberWithName("_M_header")
            if not self.Mheader.IsValid():
                self.count = 0
            else:
                map_type = self.valobj.GetType()
                if map_type.IsReferenceType():
                    logger >> "Dereferencing type"
                    map_type = map_type.GetDereferencedType()

                # Get the type of std::pair<key, value>. It is the first template
                # argument type of the 4th template argument to std::map.
                allocator_type = map_type.GetTemplateArgumentType(3)
                self.data_type = allocator_type.GetTemplateArgumentType(0)
                if not self.data_type:
                    # GCC does not emit DW_TAG_template_type_parameter for
                    # std::allocator<...>. For such a case, get the type of
                    # std::pair from a member of std::map.
                    rep_type = self.valobj.GetChildMemberWithName("_M_t").GetType()
                    self.data_type = (
                        rep_type.GetTypedefedType().GetTemplateArgumentType(1)
                    )

                # from libstdc++ implementation of _M_root for rbtree
                self.Mroot = self.Mheader.GetChildMemberWithName("_M_parent")
                self.data_size = self.data_type.GetByteSize()
                self.skip_size = self.Mheader.GetType().GetByteSize()
        except:
            self.count = 0
        return False

    def num_children(self):
        logger = lldb.formatters.Logger.Logger()
        if self.count is None:
            self.count = self.num_children_impl()
        return self.count

    def num_children_impl(self):
        logger = lldb.formatters.Logger.Logger()
        try:
            root_ptr_val = self.node_ptr_value(self.Mroot)
            if root_ptr_val == 0:
                return 0
            count = self.Mimpl.GetChildMemberWithName(
                "_M_node_count"
            ).GetValueAsUnsigned(0)
            logger >> "I have " + str(count) + " children available"
            return count
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
        logger >> "Being asked to fetch child[" + str(index) + "]"
        if index < 0:
            return None
        if index >= self.num_children():
            return None
        if self.garbage:
            logger >> "Returning None since we are a garbage tree"
            return None
        try:
            offset = index
            current = self.left(self.Mheader)
            while offset > 0:
                current = self.increment_node(current)
                offset = offset - 1
            # skip all the base stuff and get at the data
            return current.CreateChildAtOffset(
                "[" + str(index) + "]", self.skip_size, self.data_type
            )
        except:
            return None

    # utility functions
    def node_ptr_value(self, node):
        logger = lldb.formatters.Logger.Logger()
        return node.GetValueAsUnsigned(0)

    def right(self, node):
        logger = lldb.formatters.Logger.Logger()
        return node.GetChildMemberWithName("_M_right")

    def left(self, node):
        logger = lldb.formatters.Logger.Logger()
        return node.GetChildMemberWithName("_M_left")

    def parent(self, node):
        logger = lldb.formatters.Logger.Logger()
        return node.GetChildMemberWithName("_M_parent")

    # from libstdc++ implementation of iterator for rbtree
    def increment_node(self, node):
        logger = lldb.formatters.Logger.Logger()
        max_steps = self.num_children()
        if self.node_ptr_value(self.right(node)) != 0:
            x = self.right(node)
            max_steps -= 1
            while self.node_ptr_value(self.left(x)) != 0:
                x = self.left(x)
                max_steps -= 1
                logger >> str(max_steps) + " more to go before giving up"
                if max_steps <= 0:
                    self.garbage = True
                    return None
            return x
        else:
            x = node
            y = self.parent(x)
            max_steps -= 1
            while self.node_ptr_value(x) == self.node_ptr_value(self.right(y)):
                x = y
                y = self.parent(y)
                max_steps -= 1
                logger >> str(max_steps) + " more to go before giving up"
                if max_steps <= 0:
                    self.garbage = True
                    return None
            if self.node_ptr_value(self.right(x)) != self.node_ptr_value(y):
                x = y
            return x

    def has_children(self):
        return True


_list_uses_loop_detector = True


class StdDequeSynthProvider:
    def __init__(self, valobj, d):
        self.valobj = valobj
        self.pointer_size = self.valobj.GetProcess().GetAddressByteSize()
        self.count = None
        self.block_size = -1
        self.element_size = -1
        self.find_block_size()

    def find_block_size(self):
        # in order to use the deque we must have the block size, or else
        # it's impossible to know what memory addresses are valid
        self.element_type = self.valobj.GetType().GetTemplateArgumentType(0)
        if not self.element_type.IsValid():
            return
        self.element_size = self.element_type.GetByteSize()
        # The block size (i.e. number of elements per subarray) is defined in
        # this piece of code, so we need to replicate it.
        #
        # #define _GLIBCXX_DEQUE_BUF_SIZE 512
        #
        # return (__size < _GLIBCXX_DEQUE_BUF_SIZE
        #   ? size_t(_GLIBCXX_DEQUE_BUF_SIZE / __size) : size_t(1));
        if self.element_size < 512:
            self.block_size = 512 // self.element_size
        else:
            self.block_size = 1

    def num_children(self):
        if self.count is None:
            return 0
        return self.count

    def has_children(self):
        return True

    def get_child_index(self, name):
        try:
            return int(name.lstrip("[").rstrip("]"))
        except:
            return -1

    def get_child_at_index(self, index):
        if index < 0 or self.count is None:
            return None
        if index >= self.num_children():
            return None
        try:
            name = "[" + str(index) + "]"
            # We first look for the element in the first subarray,
            # which might be incomplete.
            if index < self.first_node_size:
                # The following statement is valid because self.first_elem is the pointer
                # to the first element
                return self.first_elem.CreateChildAtOffset(
                    name, index * self.element_size, self.element_type
                )

            # Now the rest of the subarrays except for maybe the last one
            # are going to be complete, so the final expression is simpler
            i, j = divmod(index - self.first_node_size, self.block_size)

            # We first move to the beginning of the node/subarray were our element is
            node = self.start_node.CreateChildAtOffset(
                "",
                (1 + i) * self.valobj.GetProcess().GetAddressByteSize(),
                self.element_type.GetPointerType(),
            )
            return node.CreateChildAtOffset(
                name, j * self.element_size, self.element_type
            )

        except:
            return None

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.count = 0
        try:
            # A deque is effectively a two-dim array, with fixed width.
            # However, only a subset of this memory contains valid data
            # since a deque may have some slack at the front and back in
            # order to have O(1) insertion at both ends.
            # The rows in active use are delimited by '_M_start' and
            # '_M_finish'.
            #
            # To find the elements that are actually constructed, the 'start'
            # variable tells which element in this NxM array is the 0th
            # one.
            if self.block_size < 0 or self.element_size < 0:
                return False

            count = 0

            impl = self.valobj.GetChildMemberWithName("_M_impl")

            # we calculate the size of the first node (i.e. first internal array)
            self.start = impl.GetChildMemberWithName("_M_start")
            self.start_node = self.start.GetChildMemberWithName("_M_node")
            first_node_address = self.start_node.GetValueAsUnsigned(0)
            first_node_last_elem = self.start.GetChildMemberWithName(
                "_M_last"
            ).GetValueAsUnsigned(0)
            self.first_elem = self.start.GetChildMemberWithName("_M_cur")
            first_node_first_elem = self.first_elem.GetValueAsUnsigned(0)

            finish = impl.GetChildMemberWithName("_M_finish")
            last_node_address = finish.GetChildMemberWithName(
                "_M_node"
            ).GetValueAsUnsigned(0)
            last_node_first_elem = finish.GetChildMemberWithName(
                "_M_first"
            ).GetValueAsUnsigned(0)
            last_node_last_elem = finish.GetChildMemberWithName(
                "_M_cur"
            ).GetValueAsUnsigned(0)

            if (
                first_node_first_elem == 0
                or first_node_last_elem == 0
                or first_node_first_elem > first_node_last_elem
            ):
                return False
            if (
                last_node_first_elem == 0
                or last_node_last_elem == 0
                or last_node_first_elem > last_node_last_elem
            ):
                return False

            if last_node_address == first_node_address:
                self.first_node_size = (
                    last_node_last_elem - first_node_first_elem
                ) // self.element_size
                count += self.first_node_size
            else:
                self.first_node_size = (
                    first_node_last_elem - first_node_first_elem
                ) // self.element_size
                count += self.first_node_size

                # we calculate the size of the last node
                finish = impl.GetChildMemberWithName("_M_finish")
                last_node_address = finish.GetChildMemberWithName(
                    "_M_node"
                ).GetValueAsUnsigned(0)
                count += (
                    last_node_last_elem - last_node_first_elem
                ) // self.element_size

                # we calculate the size of the intermediate nodes
                num_intermediate_nodes = (
                    last_node_address - first_node_address - 1
                ) // self.valobj.GetProcess().GetAddressByteSize()
                count += self.block_size * num_intermediate_nodes
            self.count = count
        except:
            pass
        return False


def VariantSummaryProvider(valobj, dict):
    raw_obj = valobj.GetNonSyntheticValue()
    index_obj = raw_obj.GetChildMemberWithName("_M_index")
    data_obj = raw_obj.GetChildMemberWithName("_M_u")
    if not (index_obj and index_obj.IsValid() and data_obj and data_obj.IsValid()):
        return "<Can't find _M_index or _M_u>"

    def get_variant_npos_value(index_byte_size):
        if index_byte_size == 1:
            return 0xFF
        elif index_byte_size == 2:
            return 0xFFFF
        else:
            return 0xFFFFFFFF

    npos_value = get_variant_npos_value(index_obj.GetByteSize())
    index = index_obj.GetValueAsUnsigned(0)
    if index == npos_value:
        return " No Value"

    # Strip references and typedefs.
    variant_type = raw_obj.GetType().GetCanonicalType().GetDereferencedType()
    template_arg_count = variant_type.GetNumberOfTemplateArguments()

    # Invalid index can happen when the variant is not initialized yet.
    if index >= template_arg_count:
        return " <Invalid>"

    active_type = variant_type.GetTemplateArgumentType(index)
    return f" Active Type = {active_type.GetDisplayTypeName()} "


class VariantSynthProvider:
    def __init__(self, valobj, dict):
        self.raw_obj = valobj.GetNonSyntheticValue()
        self.is_valid = False
        self.index = None
        self.data_obj = None

    def update(self):
        try:
            self.index = self.raw_obj.GetChildMemberWithName(
                "_M_index"
            ).GetValueAsSigned(-1)
            self.is_valid = self.index != -1
            self.data_obj = self.raw_obj.GetChildMemberWithName("_M_u")
        except:
            self.is_valid = False
        return False

    def has_children(self):
        return True

    def num_children(self):
        return 1 if self.is_valid else 0

    def get_child_index(self, name):
        return 0

    def get_child_at_index(self, index):
        if not self.is_valid:
            return None
        cur = 0
        node = self.data_obj
        while cur < self.index:
            node = node.GetChildMemberWithName("_M_rest")
            cur += 1

        # _M_storage's type depends on variant field's type "_Type".
        #  1. if '_Type' is literal type: _Type _M_storage.
        #  2. otherwise, __gnu_cxx::__aligned_membuf<_Type> _M_storage.
        #
        # For 2. we have to cast it to underlying template _Type.

        value = node.GetChildMemberWithName("_M_first").GetChildMemberWithName(
            "_M_storage"
        )
        template_type = value.GetType().GetTemplateArgumentType(0)

        # Literal type will return None for GetTemplateArgumentType(0)
        if (
            template_type
            and "__gnu_cxx::__aligned_membuf" in value.GetType().GetDisplayTypeName()
            and template_type.IsValid()
        ):
            value = value.Cast(template_type)

        if value.IsValid():
            return value.Clone("Value")
        return None
