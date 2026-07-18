# SPDX-License-Identifier: GPL-2.0

import ctypes
import os
import random
import string
import subprocess
import time
from pathlib import Path

from .utils import ip

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


class UserNetNS:
    """Network namespace owned by a non-init user namespace."""

    def __init__(self):
        self.name = ''.join(
            random.choice(string.ascii_lowercase) for _ in range(8))
        self.user_ns_path = f"/run/userns/{self.name}"
        self.net_ns_path = f"/run/netns/{self.name}"
        self._user_mounted = False
        self._net_mounted = False

        os.makedirs("/run/userns", exist_ok=True)
        os.makedirs("/run/netns", exist_ok=True)

        Path(self.user_ns_path).touch()
        Path(self.net_ns_path).touch()

        with subprocess.Popen(
                ["unshare", "--user", "--net", "--map-root-user",
                 "sleep", "infinity"]) as proc:
            try:
                pid = proc.pid
                init_user = os.readlink("/proc/self/ns/user")
                for _ in range(200):
                    try:
                        if os.readlink(f"/proc/{pid}/ns/user") != init_user:
                            break
                    except OSError:
                        pass
                    time.sleep(0.01)
                else:
                    raise RuntimeError("unshare child did not create userns")

                subprocess.run(["mount", "--bind", f"/proc/{pid}/ns/user",
                                self.user_ns_path], check=True)
                self._user_mounted = True
                subprocess.run(["mount", "--bind", f"/proc/{pid}/ns/net",
                                self.net_ns_path], check=True)
                self._net_mounted = True
            finally:
                proc.kill()

    def __del__(self):
        if self._net_mounted:
            subprocess.run(["umount", self.net_ns_path], check=False)
            self._net_mounted = False
        if self._user_mounted:
            subprocess.run(["umount", self.user_ns_path], check=False)
            self._user_mounted = False
        for path in (self.net_ns_path, self.user_ns_path):
            try:
                os.unlink(path)
            except OSError:
                pass

    def __enter__(self):
        return self

    def __exit__(self, ex_type, ex_value, ex_tb):
        self.__del__()

    def __str__(self):
        return self.name

    def __repr__(self):
        return f"UserNetNS({self.name})"


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
