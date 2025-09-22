class LookupDictionary(dict):
    """
    a dictionary which can lookup value by key, or keys by value
    """

    def __init__(self, items=[]):
        """items can be a list of pair_lists or a dictionary"""
        dict.__init__(self, items)

    def get_keys_for_value(self, value, fail_value=None):
        """find the key(s) as a list given a value"""
        list_result = [item[0] for item in self.items() if item[1] == value]
        if len(list_result) > 0:
            return list_result
        return fail_value

    def get_first_key_for_value(self, value, fail_value=None):
        """return the first key of this dictionary given the value"""
        list_result = [item[0] for item in self.items() if item[1] == value]
        if len(list_result) > 0:
            return list_result[0]
        return fail_value

    def get_value(self, key, fail_value=None):
        """find the value given a key"""
        if key in self:
            return self[key]
        return fail_value


class Enum(LookupDictionary):
    def __init__(self, initial_value=0, items=[]):
        """items can be a list of pair_lists or a dictionary"""
        LookupDictionary.__init__(self, items)
        self.value = initial_value

    def set_value(self, v):
        v_typename = typeof(v).__name__
        if v_typename == "str":
            if str in self:
                v = self[v]
            else:
                v = 0
        else:
            self.value = v

    def get_enum_value(self):
        return self.value

    def get_enum_name(self):
        return self.__str__()

    def __str__(self):
        s = self.get_first_key_for_value(self.value, None)
        if s is None:
            s = "%#8.8x" % self.value
        return s

    def __repr__(self):
        return self.__str__()
