# SPDX-License-Identifier: GPL-2.0

import errno
import json as _json
import random
import re
import socket
import subprocess
import time


class CmdExitFailure(Exception):
    def __init__(self, msg, cmd_obj):
        super().__init__(msg)
        self.cmd = cmd_obj


class cmd:
    def __init__(self, comm, shell=True, fail=True, ns=None, background=False, host=None, timeout=5):
        if ns:
            comm = f'ip netns exec {ns} ' + comm

        self.stdout = None
        self.stderr = None
        self.ret = None

        self.comm = comm
        if host:
            self.proc = host.cmd(comm)
        else:
            self.proc = subprocess.Popen(comm, shell=shell, stdout=subprocess.PIPE,
                                         stderr=subprocess.PIPE)
        if not background:
            self.process(terminate=False, fail=fail, timeout=timeout)

    def process(self, terminate=True, fail=None, timeout=5):
        if fail is None:
            fail = not terminate

        if terminate:
            self.proc.terminate()
        stdout, stderr = self.proc.communicate(timeout)
        self.stdout = stdout.decode("utf-8")
        self.stderr = stderr.decode("utf-8")
        self.proc.stdout.close()
        self.proc.stderr.close()
        self.ret = self.proc.returncode

        if self.proc.returncode != 0 and fail:
            if len(stderr) > 0 and stderr[-1] == "\n":
                stderr = stderr[:-1]
            raise CmdExitFailure("Command failed: %s\nSTDOUT: %s\nSTDERR: %s" %
                                 (self.proc.args, stdout, stderr), self)


class bkg(cmd):
    def __init__(self, comm, shell=True, fail=None, ns=None, host=None,
                 exit_wait=False):
        super().__init__(comm, background=True,
                         shell=shell, fail=fail, ns=ns, host=host)
        self.terminate = not exit_wait
        self.check_fail = fail

    def __enter__(self):
        return self

    def __exit__(self, ex_type, ex_value, ex_tb):
        return self.process(terminate=self.terminate, fail=self.check_fail)


global_defer_queue = []


class defer:
    def __init__(self, func, *args, **kwargs):
        global global_defer_queue

        if not callable(func):
            raise Exception("defer created with un-callable object, did you call the function instead of passing its name?")

        self.func = func
        self.args = args
        self.kwargs = kwargs

        self._queue =  global_defer_queue
        self._queue.append(self)

    def __enter__(self):
        return self

    def __exit__(self, ex_type, ex_value, ex_tb):
        return self.exec()

    def exec_only(self):
        self.func(*self.args, **self.kwargs)

    def cancel(self):
        self._queue.remove(self)

    def exec(self):
        self.cancel()
        self.exec_only()


def tool(name, args, json=None, ns=None, host=None):
    cmd_str = name + ' '
    if json:
        cmd_str += '--json '
    cmd_str += args
    cmd_obj = cmd(cmd_str, ns=ns, host=host)
    if json:
        return _json.loads(cmd_obj.stdout)
    return cmd_obj


def ip(args, json=None, ns=None, host=None):
    if ns:
        args = f'-netns {ns} ' + args
    return tool('ip', args, json=json, host=host)


def ethtool(args, json=None, ns=None, host=None):
    return tool('ethtool', args, json=json, ns=ns, host=host)


def rand_port():
    """
    Get a random unprivileged port, try to make sure it's not already used.
    """
    for _ in range(1000):
        port = random.randint(10000, 65535)
        try:
            with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
                s.bind(("", port))
            return port
        except OSError as e:
            if e.errno != errno.EADDRINUSE:
                raise
    raise Exception("Can't find any free unprivileged port")


def wait_port_listen(port, proto="tcp", ns=None, host=None, sleep=0.005, deadline=5):
    end = time.monotonic() + deadline

    pattern = f":{port:04X} .* "
    if proto == "tcp": # for tcp protocol additionally check the socket state
        pattern += "0A"
    pattern = re.compile(pattern)

    while True:
        data = cmd(f'cat /proc/net/{proto}*', ns=ns, host=host, shell=True).stdout
        for row in data.split("\n"):
            if pattern.search(row):
                return
        if time.monotonic() > end:
            raise Exception("Waiting for port listen timed out")
        time.sleep(sleep)
