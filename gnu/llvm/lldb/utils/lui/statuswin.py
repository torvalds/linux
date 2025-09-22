##===-- statuswin.py -----------------------------------------*- Python -*-===##
##
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
##
##===----------------------------------------------------------------------===##

import lldb
import lldbutil
import cui
import curses


class StatusWin(cui.TextWin):
    def __init__(self, x, y, w, h):
        super(StatusWin, self).__init__(x, y, w)

        self.keys = [  # ('F1', 'Help', curses.KEY_F1),
            ("F3", "Cycle-focus", curses.KEY_F3),
            ("F10", "Quit", curses.KEY_F10),
        ]

    def draw(self):
        self.win.addstr(0, 0, "")
        for key in self.keys:
            self.win.addstr("{0}".format(key[0]), curses.A_REVERSE)
            self.win.addstr(" {0} ".format(key[1]), curses.A_NORMAL)
        super(StatusWin, self).draw()

    def handleEvent(self, event):
        if isinstance(event, int):
            pass
        elif isinstance(event, lldb.SBEvent):
            if lldb.SBProcess.EventIsProcessEvent(event):
                state = lldb.SBProcess.GetStateFromEvent(event)
                status = lldbutil.state_type_to_str(state)
                self.win.erase()
                x = self.win.getmaxyx()[1] - len(status) - 1
                self.win.addstr(0, x, status)
        return
