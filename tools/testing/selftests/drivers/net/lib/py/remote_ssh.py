# SPDX-License-Identifier: GPL-2.0

import os
import string
import subprocess
import random

from lib.py import cmd


class Remote:
    def __init__(self, name, dir_path):
        self.name = name
        self.dir_path = dir_path
        self._tmpdir = None

    def __del__(self):
        if self._tmpdir:
            cmd("rm -rf " + self._tmpdir, host=self)
            self._tmpdir = None

    def cmd(self, comm):
        return subprocess.Popen(["ssh", "-q", self.name, comm],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def _mktmp(self):
        return ''.join(random.choice(string.ascii_lowercase) for _ in range(8))

    def deploy(self, what):
        if not self._tmpdir:
            self._tmpdir = "/tmp/" + self._mktmp()
            cmd("mkdir " + self._tmpdir, host=self)
        file_name = self._tmpdir + "/" + self._mktmp() + os.path.basename(what)

        if not os.path.isabs(what):
            what = os.path.abspath(self.dir_path + "/" + what)

        cmd(f"scp {what} {self.name}:{file_name}")
        return file_name
