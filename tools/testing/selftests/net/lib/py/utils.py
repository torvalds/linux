# SPDX-License-Identifier: GPL-2.0

import errno
import json as _json
import os
import random
import re
import select
import socket
import subprocess
import time


class CmdExitFailure(Exception):
    def __init__(self, msg, cmd_obj):
        super().__init__(msg)
        self.cmd = cmd_obj


def fd_read_timeout(fd, timeout):
    rlist, _, _ = select.select([fd], [], [], timeout)
    if rlist:
        return os.read(fd, 1024)
    else:
        raise TimeoutError("Timeout waiting for fd read")


class cmd:
    """
    Execute a command on local or remote host.

    Use bkg() instead to run a command in the background.
    """
    def __init__(self, comm, shell=True, fail=True, ns=None, background=False,
                 host=None, timeout=5, ksft_wait=None):
        if ns:
            comm = f'ip netns exec {ns} ' + comm

        self.stdout = None
        self.stderr = None
        self.ret = None
        self.ksft_term_fd = None

        self.comm = comm
        if host:
            self.proc = host.cmd(comm)
        else:
            # ksft_wait lets us wait for the background process to fully start,
            # we pass an FD to the child process, and wait for it to write back.
            # Similarly term_fd tells child it's time to exit.
            pass_fds = ()
            env = os.environ.copy()
            if ksft_wait is not None:
                rfd, ready_fd = os.pipe()
                wait_fd, self.ksft_term_fd = os.pipe()
                pass_fds = (ready_fd, wait_fd, )
                env["KSFT_READY_FD"] = str(ready_fd)
                env["KSFT_WAIT_FD"]  = str(wait_fd)

            self.proc = subprocess.Popen(comm, shell=shell, stdout=subprocess.PIPE,
                                         stderr=subprocess.PIPE, pass_fds=pass_fds,
                                         env=env)
            if ksft_wait is not None:
                os.close(ready_fd)
                os.close(wait_fd)
                msg = fd_read_timeout(rfd, ksft_wait)
                os.close(rfd)
                if not msg:
                    raise Exception("Did not receive ready message")
        if not background:
            self.process(terminate=False, fail=fail, timeout=timeout)

    def process(self, terminate=True, fail=None, timeout=5):
        if fail is None:
            fail = not terminate

        if self.ksft_term_fd:
            os.write(self.ksft_term_fd, b"1")
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
    """
    Run a command in the background.

    Examples usage:

    Run a command on remote host, and wait for it to finish.
    This is usually paired with wait_port_listen() to make sure
    the command has initialized:

        with bkg("socat ...", exit_wait=True, host=cfg.remote) as nc:
            ...

    Run a command and expect it to let us know that it's ready
    by writing to a special file descriptor passed via KSFT_READY_FD.
    Command will be terminated when we exit the context manager:

        with bkg("my_binary", ksft_wait=5):
    """
    def __init__(self, comm, shell=True, fail=None, ns=None, host=None,
                 exit_wait=False, ksft_wait=None):
        super().__init__(comm, background=True,
                         shell=shell, fail=fail, ns=ns, host=host,
                         ksft_wait=ksft_wait)
        self.terminate = not exit_wait and not ksft_wait
        self.check_fail = fail

        if shell and self.terminate:
            print("# Warning: combining shell and terminate is risky!")
            print("#          SIGTERM may not reach the child on zsh/ksh!")

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


def rand_port(type=socket.SOCK_STREAM):
    """
    Get a random unprivileged port.
    """
    with socket.socket(socket.AF_INET6, type) as s:
        s.bind(("", 0))
        return s.getsockname()[1]


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
