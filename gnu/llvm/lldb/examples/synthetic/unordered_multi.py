import lldb

_map_capping_size = 255


class libcxx_hash_table_SynthProvider:
    def __init__(self, valobj, dict):
        self.valobj = valobj
        self.num_elements = None
        self.next_element = None
        self.bucket_count = None

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.num_elements = None
        self.next_element = None
        self.bucket_count = None
        try:
            # unordered_map is made up of a hash_map, which has 4 pieces in it:
            #   bucket list :
            #      array of buckets
            #   p1 (pair):
            #      first - pointer to first loaded element
            #   p2 (pair):
            #      first - number of elements
            #      second - hash function
            #   p3 (pair):
            #      first - max_load_factor
            #      second - equality operator function
            #
            # For display, we actually don't need to go inside the buckets, since 'p1' has a way to iterate over all
            # the elements directly.
            #
            # We will calculate other values about the map because they will be useful for the summary.
            #
            table = self.valobj.GetChildMemberWithName("__table_")

            bl_ptr = table.GetChildMemberWithName(
                "__bucket_list_"
            ).GetChildMemberWithName("__ptr_")
            self.bucket_array_ptr = bl_ptr.GetChildMemberWithName(
                "__first_"
            ).GetValueAsUnsigned(0)
            self.bucket_count = (
                bl_ptr.GetChildMemberWithName("__second_")
                .GetChildMemberWithName("__data_")
                .GetChildMemberWithName("__first_")
                .GetValueAsUnsigned(0)
            )
            logger >> "Bucket count = %r" % self.bucket_count

            self.begin_ptr = (
                table.GetChildMemberWithName("__p1_")
                .GetChildMemberWithName("__first_")
                .GetChildMemberWithName("__next_")
            )

            self.num_elements = (
                table.GetChildMemberWithName("__p2_")
                .GetChildMemberWithName("__first_")
                .GetValueAsUnsigned(0)
            )
            self.max_load_factor = (
                table.GetChildMemberWithName("__p3_")
                .GetChildMemberWithName("__first_")
                .GetValueAsUnsigned(0)
            )
            logger >> "Num elements = %r" % self.num_elements

            # save the pointers as we get them
            #   -- don't access this first element if num_element==0!
            self.elements_cache = []
            if self.num_elements:
                self.next_element = self.begin_ptr
            else:
                self.next_element = None
        except Exception as e:
            logger >> "Caught exception: %r" % e
            pass

    def num_children(self):
        global _map_capping_size
        num_elements = self.num_elements
        if num_elements is not None:
            if num_elements > _map_capping_size:
                num_elements = _map_capping_size
        return num_elements

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
        logger >> "Retrieving child " + str(index)
        if index < 0:
            return None
        if index >= self.num_children():
            return None

        # extend
        logger >> " : cache size starts with %d elements" % len(self.elements_cache)
        while index >= len(self.elements_cache):
            # if we hit the end before we get the index, give up:
            if not self.next_element:
                logger >> " : hit end of list"
                return None

            node = self.next_element.Dereference()

            value = node.GetChildMemberWithName("__value_")
            hash_value = node.GetChildMemberWithName("__hash_").GetValueAsUnsigned()
            self.elements_cache.append((value, hash_value))

            self.next_element = node.GetChildMemberWithName("__next_")
            if not self.next_element.GetValueAsUnsigned(0):
                self.next_element = None

        # hit the index! so we have the value
        logger >> " : cache size ends with %d elements" % len(self.elements_cache)
        value, hash_value = self.elements_cache[index]
        return self.valobj.CreateValueFromData(
            "[%d] <hash %d>" % (index, hash_value), value.GetData(), value.GetType()
        )


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand(
        'type synthetic add -l unordered_multi.libcxx_hash_table_SynthProvider -x "^(std::__1::)unordered_(multi)?(map|set)<.+> >$" -w libcxx'
    )
