# Synthetic children provider example for class MaskedData
# to use me:
# command script import ./example.py --allow-reload
# type synthetic add MaskedData --python-class
# example.MaskedData_SyntheticChildrenProvider


class MaskedData_SyntheticChildrenProvider:
    def __init__(self, valobj, dict):
        # remember the SBValue since you will not have another chance to get it
        # :-)
        self.valobj = valobj

    def num_children(self):
        # you could perform calculations involving the SBValue and/or its children to determine this value
        # here, we have an hardcoded value - but since you have stored the SBValue you could use it to
        # help figure out the correct thing to return here. if you return a number N, you should be prepared to
        # answer questions about N children
        return 4

    def has_children(self):
        # we simply say True here because we know we have 4 children
        # in general, you want to make this calculation as simple as possible
        # and return True if in doubt (you can always return num_children == 0
        # later)
        return True

    def get_child_index(self, name):
        # given a name, return its index
        # you can return None if you don't know the answer for a given name
        if name == "value":
            return 0
        # here, we are using a reserved C++ keyword as a child name - we could not do that in the source code
        # but we are free to use the names we like best in the synthetic children provider class
        # we are also not respecting the order of declaration in the C++ class itself - as long as
        # we are consistent, we can do that freely
        if name == "operator":
            return 1
        if name == "mask":
            return 2
        # this member does not exist in the original class - we will compute its value and show it to the user
        # when returning synthetic children, there is no need to only stick to
        # what already exists in memory
        if name == "apply()":
            return 3
        return None  # no clue, just say none

    def get_child_at_index(self, index):
        # precautionary measures
        if index < 0:
            return None
        if index > self.num_children():
            return None
        if not self.valobj.IsValid():
            return None
        if index == 0:
            return self.valobj.GetChildMemberWithName("value")
        if index == 1:
            # fetch the value of the operator
            op_chosen = self.valobj.GetChildMemberWithName("oper").GetValueAsUnsigned()
            # if it is a known value, return a descriptive string for it
            # we are not doing this in the most efficient possible way, but the code is very readable
            # and easy to maintain - if you change the values on the C++ side,
            # the same changes must be made here
            if op_chosen == 0:
                return self.valobj.CreateValueFromExpression(
                    "operator", '(const char*)"none"'
                )
            elif op_chosen == 1:
                return self.valobj.CreateValueFromExpression(
                    "operator", '(const char*)"AND"'
                )
            elif op_chosen == 2:
                return self.valobj.CreateValueFromExpression(
                    "operator", '(const char*)"OR"'
                )
            elif op_chosen == 3:
                return self.valobj.CreateValueFromExpression(
                    "operator", '(const char*)"XOR"'
                )
            elif op_chosen == 4:
                return self.valobj.CreateValueFromExpression(
                    "operator", '(const char*)"NAND"'
                )
            elif op_chosen == 5:
                return self.valobj.CreateValueFromExpression(
                    "operator", '(const char*)"NOR"'
                )
            else:
                return self.valobj.CreateValueFromExpression(
                    "operator", '(const char*)"unknown"'
                )  # something else
        if index == 2:
            return self.valobj.GetChildMemberWithName("mask")
        if index == 3:
            # for this, we must fetch all the other elements
            # in an efficient implementation, we would be caching this data for
            # efficiency
            value = self.valobj.GetChildMemberWithName("value").GetValueAsUnsigned()
            operator = self.valobj.GetChildMemberWithName("oper").GetValueAsUnsigned()
            mask = self.valobj.GetChildMemberWithName("mask").GetValueAsUnsigned()
            # compute the masked value according to the operator
            if operator == 1:
                value = value & mask
            elif operator == 2:
                value = value | mask
            elif operator == 3:
                value = value ^ mask
            elif operator == 4:
                value = ~(value & mask)
            elif operator == 5:
                value = ~(value | mask)
            else:
                pass
            value &= (
                0xFFFFFFFF  # make sure Python does not extend our values to 64-bits
            )
            # return it - again, not the most efficient possible way. we should actually be pushing the computed value
            # into an SBData, and using the SBData to create an SBValue - this
            # has the advantage of readability
            return self.valobj.CreateValueFromExpression(
                "apply()", "(uint32_t)(" + str(value) + ")"
            )

    def update(self):
        # we do not do anything special in update - but this would be the right place to lookup
        # the data we use in get_child_at_index and cache it
        pass
