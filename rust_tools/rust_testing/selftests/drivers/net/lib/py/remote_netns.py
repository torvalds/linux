# SPDX-License-Identifier: GPL-2.0

import os
import subprocess

from lib.py import cmd


class Remote:
    def __init__(self, name, dir_path):
        self.name = name
        self.dir_path = dir_path

    def cmd(self, comm):
        return subprocess.Popen(["ip", "netns", "exec", self.name, "bash", "-c", comm],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def deploy(self, what):
        if os.path.isabs(what):
            return what
        return os.path.abspath(self.dir_path + "/" + what)
