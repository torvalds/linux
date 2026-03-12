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
    - ask for one slot by calling::

        claim = os.read(reader, 1)

    - when the job finshes, call::

        os.write(writer, b"+")  # os.write(writer, claim)

Here, the goal is different: This script aims to get the remaining number
of slots available, using all of them to run a command which handle tasks in
parallel. To to that, it has a loop that ends only after there are no
slots left. It then increments the number by one, in order to allow a
call equivalent to ``make -j$((claim+1))``, e.g. having a parent make creating
$claim child to do the actual work.

The end goal here is to keep the total number of build tasks under the
limit established by the initial ``make -j$n_proc`` call.

See:
    https://www.gnu.org/software/make/manual/html_node/POSIX-Jobserver.html#POSIX-Jobserver
"""

import errno
import os
import subprocess
import sys

def warn(text, *args):
    print(f'WARNING: {text}', *args, file = sys.stderr)

class JobserverExec:
    """
    Claim all slots from make using POSIX Jobserver.

    The main methods here are:

    - open(): reserves all slots;
    - close(): method returns all used slots back to make;
    - run(): executes a command setting PARALLELISM=<available slots jobs + 1>.
    """

    def __init__(self):
        """Initialize internal vars."""
        self.claim = 0
        self.jobs = b""
        self.reader = None
        self.writer = None
        self.is_open = False

    def open(self):
        """Reserve all available slots to be claimed later on."""

        if self.is_open:
            return
        self.is_open = True  # We only try once
        self.claim = None
        #
        # Check the make flags for "--jobserver=R,W"
        # Note that GNU Make has used --jobserver-fds and --jobserver-auth
        # so this handles all of them.
        #
        flags = os.environ.get('MAKEFLAGS', '')
        opts = [x for x in flags.split(" ") if x.startswith("--jobserver")]
        if not opts:
            return
        #
        # Separate out the provided file descriptors
        #
        split_opt = opts[-1].split('=', 1)
        if len(split_opt) != 2:
            warn('unparseable option:', opts[-1])
            return
        fds = split_opt[1]
        #
        # As of GNU Make 4.4, we'll be looking for a named pipe
        # identified as fifo:path
        #
        if fds.startswith('fifo:'):
            path = fds[len('fifo:'):]
            try:
                self.reader = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
                self.writer = os.open(path, os.O_WRONLY)
            except (OSError, IOError):
                warn('unable to open jobserver pipe', path)
                return
        #
        # Otherwise look for integer file-descriptor numbers.
        #
        else:
            split_fds = fds.split(',')
            if len(split_fds) != 2:
                warn('malformed jobserver file descriptors:', fds)
                return
            try:
                self.reader = int(split_fds[0])
                self.writer = int(split_fds[1])
            except ValueError:
                warn('non-integer jobserver file-descriptors:', fds)
                return
            try:
                #
                # Open a private copy of reader to avoid setting nonblocking
                # on an unexpecting process with the same reader fd.
                #
                self.reader = os.open(f"/proc/self/fd/{self.reader}",
                                      os.O_RDONLY | os.O_NONBLOCK)
            except (IOError, OSError) as e:
                warn('Unable to reopen jobserver read-side pipe:', repr(e))
                return
        #
        # OK, we have the channel to the job server; read out as many jobserver
        # slots as possible.
        #
        while True:
            try:
                slot = os.read(self.reader, 8)
                if not slot:
                    #
                    # Something went wrong.  Clear self.jobs to avoid writing
                    # weirdness back to the jobserver and give up.
                    self.jobs = b""
                    warn("unexpected empty token from jobserver;"
                         " possible invalid '--jobserver-auth=' setting")
                    self.claim = None
                    return
            except (OSError, IOError) as e:
                #
                # If there is nothing more to read then we are done.
                #
                if e.errno == errno.EWOULDBLOCK:
                    break
                #
                # Anything else says that something went weird; give back
                # the jobs and give up.
                #
                if self.jobs:
                    os.write(self.writer, self.jobs)
                    self.claim = None
                    warn('error reading from jobserver pipe', repr(e))
                    return
            self.jobs += slot
        #
        # Add a bump for our caller's reserveration, since we're just going
        # to sit here blocked on our child.
        #
        self.claim = len(self.jobs) + 1

    def close(self):
        """Return all reserved slots to Jobserver."""

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
