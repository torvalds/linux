# SPDX-License-Identifier: GPL-2.0

from .utils import ip
import ctypes
import random
import string

libc = ctypes.cdll.LoadLibrary('libc.so.6')


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


class NetNSEnter:
    def __init__(self, ns_name):
        self.ns_path = f"/run/netns/{ns_name}"

    def __enter__(self):
        self.saved = open("/proc/thread-self/ns/net")
        with open(self.ns_path) as ns_file:
            libc.setns(ns_file.fileno(), 0)
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        libc.setns(self.saved.fileno(), 0)
        self.saved.close()
