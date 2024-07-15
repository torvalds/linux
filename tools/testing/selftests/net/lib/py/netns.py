# SPDX-License-Identifier: GPL-2.0

from .utils import ip
import random
import string


class NetNS:
    def __init__(self, name=None):
        if name:
            self.name = name
        else:
            self.name = ''.join(random.choice(string.ascii_lowercase) for _ in range(8))
        ip('netns add ' + self.name)

    def __del__(self):
        if self.name:
            ip('netns del ' + self.name)
            self.name = None

    def __enter__(self):
        return self

    def __exit__(self, ex_type, ex_value, ex_tb):
        self.__del__()

    def __str__(self):
        return self.name

    def __repr__(self):
        return f"NetNS({self.name})"
