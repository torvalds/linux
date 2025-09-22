#!/usr/bin/env python
##===-- lui.py -----------------------------------------------*- Python -*-===##
##
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
##
##===----------------------------------------------------------------------===##


import curses

import lldb
import lldbutil

from optparse import OptionParser
import os
import signal
import sys

try:
    import queue
except ImportError:
    import Queue as queue

import debuggerdriver
import cui

import breakwin
import commandwin
import eventwin
import sourcewin
import statuswin

event_queue = None


def handle_args(driver, argv):
    parser = OptionParser()
    parser.add_option(
        "-p", "--attach", dest="pid", help="Attach to specified Process ID", type="int"
    )
    parser.add_option(
        "-c", "--core", dest="core", help="Load specified core file", type="string"
    )

    (options, args) = parser.parse_args(argv)

    if options.pid is not None:
        try:
            pid = int(options.pid)
            driver.attachProcess(ui, pid)
        except ValueError:
            print("Error: expecting integer PID, got '%s'" % options.pid)
    elif options.core is not None:
        if not os.path.exists(options.core):
            raise Exception("Specified core file '%s' does not exist." % options.core)
        driver.loadCore(options.core)
    elif len(args) == 2:
        if not os.path.isfile(args[1]):
            raise Exception("Specified target '%s' does not exist" % args[1])
        driver.createTarget(args[1])
    elif len(args) > 2:
        if not os.path.isfile(args[1]):
            raise Exception("Specified target '%s' does not exist" % args[1])
        driver.createTarget(args[1], args[2:])


def sigint_handler(signal, frame):
    global debugger
    debugger.terminate()


class LLDBUI(cui.CursesUI):
    def __init__(self, screen, event_queue, driver):
        super(LLDBUI, self).__init__(screen, event_queue)

        self.driver = driver

        h, w = self.screen.getmaxyx()

        command_win_height = 20
        break_win_width = 60

        self.status_win = statuswin.StatusWin(0, h - 1, w, 1)
        h -= 1
        self.command_win = commandwin.CommandWin(
            driver, 0, h - command_win_height, w, command_win_height
        )
        h -= command_win_height
        self.source_win = sourcewin.SourceWin(driver, 0, 0, w - break_win_width - 1, h)
        self.break_win = breakwin.BreakWin(
            driver, w - break_win_width, 0, break_win_width, h
        )

        self.wins = [
            self.status_win,
            # self.event_win,
            self.source_win,
            self.break_win,
            self.command_win,
        ]

        self.focus = len(self.wins) - 1  # index of command window;

    def handleEvent(self, event):
        # hack
        if isinstance(event, int):
            if event == curses.KEY_F10:
                self.driver.terminate()
            if event == 20:  # ctrl-T

                def foo(cmd):
                    ret = lldb.SBCommandReturnObject()
                    self.driver.getCommandInterpreter().HandleCommand(cmd, ret)

                foo("target create a.out")
                foo("b main")
                foo("run")
        super(LLDBUI, self).handleEvent(event)


def main(screen):
    signal.signal(signal.SIGINT, sigint_handler)

    global event_queue
    event_queue = queue.Queue()

    global debugger
    debugger = lldb.SBDebugger.Create()

    driver = debuggerdriver.createDriver(debugger, event_queue)
    view = LLDBUI(screen, event_queue, driver)

    driver.start()

    # hack to avoid hanging waiting for prompts!
    driver.handleCommand("settings set auto-confirm true")

    handle_args(driver, sys.argv)
    view.eventLoop()


if __name__ == "__main__":
    try:
        curses.wrapper(main)
    except KeyboardInterrupt:
        exit()
