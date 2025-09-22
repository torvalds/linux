# Formatters for classes that derive from Message.
#
# Usage:
#   command script import ./example.py
#   type summary add --expand --recognizer-function --python-function example.message_summary example.is_message_type
#   type synth add --recognizer-function --python-class example.MessageChildProvider example.is_message_type

import sys


def is_message_type(t, internal_dict):
    for base in t.get_bases_array():
        if base.GetName() == "Message":
            return True
    return False


def message_summary(value, internal_dict):
    # Could have used a summary string as well. All the work is done by the child
    # provider.
    return "Message"


class MessageChildProvider:
    def __init__(self, value, internal_dict):
        self.value = value
        self.synthetic_children = self._analyze_children(value)

    def has_children(self):
        return self.num_children() > 0

    def num_children(self):
        return len(self.synthetic_children)

    def get_child_index(self, name):
        for index, child in enumerate(self.synthetic_children):
            if child.GetName() == name:
                return index
        return None

    def get_child_at_index(self, index):
        return self.synthetic_children[index]

    def _rename_sbvalue(self, value):
        # We want to display the field with its original name without a trailing
        # underscore. So we create a new SBValue with the same type and address but
        # a different name.
        name = value.GetName()
        assert name.endswith("_")
        new_name = name[:-1]
        return value.CreateValueFromAddress(
            new_name, value.GetLoadAddress(), value.GetType()
        )

    def _analyze_children(self, value):
        result = []
        for i in range(value.GetNumChildren()):
            child = value.GetChildAtIndex(i)
            child_name = child.GetName()
            if child_name.startswith("_"):
                continue  # Internal field, skip
            # Normal field. Check presence bit.
            presence_bit = value.GetChildMemberWithName("_has_" + child_name)
            if presence_bit.GetValueAsUnsigned() != 0:
                result.append(self._rename_sbvalue(child))
        return result
