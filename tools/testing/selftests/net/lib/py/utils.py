# SPDX-License-Identifier: GPL-2.0

import json as _json
import subprocess

class cmd:
    def __init__(self, comm, shell=True, fail=True, ns=None, background=False, host=None):
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
            self.process(terminate=False, fail=fail)

    def process(self, terminate=True, fail=None):
        if terminate:
            self.proc.terminate()
        stdout, stderr = self.proc.communicate(timeout=5)
        self.stdout = stdout.decode("utf-8")
        self.stderr = stderr.decode("utf-8")
        self.proc.stdout.close()
        self.proc.stderr.close()
        self.ret = self.proc.returncode

        if self.proc.returncode != 0 and fail:
            if len(stderr) > 0 and stderr[-1] == "\n":
                stderr = stderr[:-1]
            raise Exception("Command failed: %s\nSTDOUT: %s\nSTDERR: %s" %
                            (self.proc.args, stdout, stderr))


def ip(args, json=None, ns=None, host=None):
    cmd_str = "ip "
    if json:
        cmd_str += '-j '
    cmd_str += args
    cmd_obj = cmd(cmd_str, ns=ns, host=host)
    if json:
        return _json.loads(cmd_obj.stdout)
    return cmd_obj
