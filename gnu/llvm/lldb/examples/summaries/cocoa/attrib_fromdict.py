"""
Objective-C runtime wrapper for use by LLDB Python formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""


class AttributesDictionary:
    def __init__(self, allow_reset=True):
        # need to do it this way to prevent endless recursion
        self.__dict__["_dictionary"] = {}
        self.__dict__["_allow_reset"] = allow_reset

    def __getattr__(self, name):
        if not self._check_exists(name):
            return None
        value = self._dictionary[name]
        return value

    def _set_impl(self, name, value):
        self._dictionary[name] = value

    def _check_exists(self, name):
        return name in self._dictionary

    def __setattr__(self, name, value):
        if self._allow_reset:
            self._set_impl(name, value)
        else:
            self.set_if_necessary(name, value)

    def set_if_necessary(self, name, value):
        if not self._check_exists(name):
            self._set_impl(name, value)
            return True
        return False

    def __len__(self):
        return len(self._dictionary)
