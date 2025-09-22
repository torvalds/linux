#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##

import re
import select
import socket
import subprocess
import tempfile
import threading
from typing import List


def _get_cpu_count() -> int:
    # Determine the number of cores by listing a /sys directory. Older devices
    # lack `nproc`. Even if a static toybox binary is pushed to the device, it may
    # return an incorrect value. (e.g. On a Nexus 7 running Android 5.0, toybox
    # nproc returns 1 even though the device has 4 CPUs.)
    job = subprocess.run(["adb", "shell", "ls /sys/devices/system/cpu"],
                         encoding="utf8", check=False,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if job.returncode == 1:
        # Maybe adb is missing, maybe ANDROID_SERIAL needs to be defined, maybe the
        # /sys subdir isn't there. Most errors will be handled later, just use one
        # job. (N.B. The adb command still succeeds even if ls fails on older
        # devices that lack the shell_v2 adb feature.)
        return 1
    # Make sure there are no CR characters in the output. Pre-shell_v2, the adb
    # stdout comes from a master pty so newlines are CRLF-delimited. On Windows,
    # LF might also get expanded to CRLF.
    cpu_listing = job.stdout.replace('\r', '\n')

    # Count lines that match "cpu${DIGITS}".
    result = len([line for line in cpu_listing.splitlines()
                  if re.match(r'cpu(\d)+$', line)])

    # Restrict the result to something reasonable.
    if result < 1:
        result = 1
    if result > 1024:
        result = 1024

    return result


def _job_limit_socket_thread(temp_dir: tempfile.TemporaryDirectory,
                             server: socket.socket, job_count: int) -> None:
    """Service the job limit server socket, accepting only as many connections
    as there should be concurrent jobs.
    """
    clients: List[socket.socket] = []
    while True:
        rlist = list(clients)
        if len(clients) < job_count:
            rlist.append(server)
        rlist, _, _ = select.select(rlist, [], [])
        for sock in rlist:
            if sock == server:
                new_client, _ = server.accept()
                new_client.send(b"x")
                clients.append(new_client)
            else:
                sock.close()
                clients.remove(sock)


def adb_job_limit_socket() -> str:
    """An Android device can frequently have many fewer cores than the host
    (e.g. 4 versus 128). We want to exploit all the device cores without
    overburdening it.

    Create a Unix domain socket that only allows as many connections as CPUs on
    the Android device.
    """

    # Create the job limit server socket.
    temp_dir = tempfile.TemporaryDirectory(prefix="libcxx_")
    sock_addr = temp_dir.name + "/adb_job.sock"
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(sock_addr)
    server.listen(1)

    # Spawn a thread to service the socket. As a daemon thread, its existence
    # won't prevent interpreter shutdown. The temp dir will still be removed on
    # shutdown.
    cpu_count = _get_cpu_count()
    threading.Thread(target=_job_limit_socket_thread,
                     args=(temp_dir, server, cpu_count),
                     daemon=True).start()

    return sock_addr
