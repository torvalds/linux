#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
#
# pylint: disable=C0103,C0209
#
#

"""
Interacts with the POSIX jobserver during the Kernel build time.

A "normal" jobserver task, like the one initiated by a make subrocess would do:

    - open read/write file descriptors to communicate with the job server;
    - ask for one slot by calling:
        claim = os.read(reader, 1)
    - when the job finshes, call:
        os.write(writer, b"+")  # os.write(writer, claim)

Here, the goal is different: This script aims to get the remaining number
of slots available, using all of them to run a command which handle tasks in
parallel. To to that, it has a loop that ends only after there are no
slots left. It then increments the number by one, in order to allow a
call equivalent to make -j$((claim+1)), e.g. having a parent make creating
$claim child to do the actual work.

The end goal here is to keep the total number of build tasks under the
limit established by the initial make -j$n_proc call.

See:
    https://www.gnu.org/software/make/manual/html_node/POSIX-Jobserver.html#POSIX-Jobserver
"""

import errno
import os
import subprocess
import sys

class JobserverExec:
    """
    Claim all slots from make using POSIX Jobserver.

    The main methods here are:
    - open(): reserves all slots;
    - close(): method returns all used slots back to make;
    - run(): executes a command setting PARALLELISM=<available slots jobs + 1>
    """

    def __init__(self):
        """Initialize internal vars"""
        self.claim = 0
        self.jobs = b""
        self.reader = None
        self.writer = None
        self.is_open = False

    def open(self):
        """Reserve all available slots to be claimed later on"""

        if self.is_open:
            return

        try:
            # Fetch the make environment options.
            flags = os.environ["MAKEFLAGS"]
            # Look for "--jobserver=R,W"
            # Note that GNU Make has used --jobserver-fds and --jobserver-auth
            # so this handles all of them.
            opts = [x for x in flags.split(" ") if x.startswith("--jobserver")]

            # Parse out R,W file descriptor numbers and set them nonblocking.
            # If the MAKEFLAGS variable contains multiple instances of the
            # --jobserver-auth= option, the last one is relevant.
            fds = opts[-1].split("=", 1)[1]

            # Starting with GNU Make 4.4, named pipes are used for reader
            # and writer.
            # Example argument: --jobserver-auth=fifo:/tmp/GMfifo8134
            _, _, path = fds.partition("fifo:")

            if path:
                self.reader = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
                self.writer = os.open(path, os.O_WRONLY)
            else:
                self.reader, self.writer = [int(x) for x in fds.split(",", 1)]
                # Open a private copy of reader to avoid setting nonblocking
                # on an unexpecting process with the same reader fd.
                self.reader = os.open("/proc/self/fd/%d" % (self.reader),
                                      os.O_RDONLY | os.O_NONBLOCK)

            # Read out as many jobserver slots as possible
            while True:
                try:
                    slot = os.read(self.reader, 8)
                    self.jobs += slot
                except (OSError, IOError) as e:
                    if e.errno == errno.EWOULDBLOCK:
                        # Stop at the end of the jobserver queue.
                        break
                    # If something went wrong, give back the jobs.
                    if self.jobs:
                        os.write(self.writer, self.jobs)
                    raise e

            # Add a bump for our caller's reserveration, since we're just going
            # to sit here blocked on our child.
            self.claim = len(self.jobs) + 1

        except (KeyError, IndexError, ValueError, OSError, IOError):
            # Any missing environment strings or bad fds should result in just
            # not being parallel.
            self.claim = None

        self.is_open = True

    def close(self):
        """Return all reserved slots to Jobserver"""

        if not self.is_open:
            return

        # Return all the reserved slots.
        if len(self.jobs):
            os.write(self.writer, self.jobs)

        self.is_open = False

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc_value, exc_traceback):
        self.close()

    def run(self, cmd, *args, **pwargs):
        """
        Run a command setting PARALLELISM env variable to the number of
        available job slots (claim) + 1, e.g. it will reserve claim slots
        to do the actual build work, plus one to monitor its children.
        """
        self.open()             # Ensure that self.claim is set

        # We can only claim parallelism if there was a jobserver (i.e. a
        # top-level "-jN" argument) and there were no other failures. Otherwise
        # leave out the environment variable and let the child figure out what
        # is best.
        if self.claim:
            os.environ["PARALLELISM"] = str(self.claim)

        return subprocess.call(cmd, *args, **pwargs)
