# SPDX-License-Identifier: GPL-2.0

import os
import shlex
from pathlib import Path
from lib.py import ip
from lib.py import NetdevSimDev


def _load_env_file(src_path):
    env = os.environ.copy()

    src_dir = Path(src_path).parent.resolve()
    if not (src_dir / "net.config").exists():
        return env

    lexer = shlex.shlex(open((src_dir / "net.config").as_posix(), 'r').read())
    k = None
    for token in lexer:
        if k is None:
            k = token
            env[k] = ""
        elif token == "=":
            pass
        else:
            env[k] = token
            k = None
    return env


class NetDrvEnv:
    """
    Class for a single NIC / host env, with no remote end
    """
    def __init__(self, src_path):
        self._ns = None

        self.env = _load_env_file(src_path)

        if 'NETIF' in self.env:
            self.dev = ip("link show dev " + self.env['NETIF'], json=True)[0]
        else:
            self._ns = NetdevSimDev()
            self.dev = self._ns.nsims[0].dev
        self.ifindex = self.dev['ifindex']

    def __enter__(self):
        return self

    def __exit__(self, ex_type, ex_value, ex_tb):
        """
        __exit__ gets called at the end of a "with" block.
        """
        self.__del__()

    def __del__(self):
        if self._ns:
            self._ns.remove()
            self._ns = None


