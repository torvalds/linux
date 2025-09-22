##===-- eventwin.py ------------------------------------------*- Python -*-===##
##
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
##
##===----------------------------------------------------------------------===##

import cui
import lldb
import lldbutil


class EventWin(cui.TitledWin):
    def __init__(self, x, y, w, h):
        super(EventWin, self).__init__(x, y, w, h, "LLDB Event Log")
        self.win.scrollok(1)
        super(EventWin, self).draw()

    def handleEvent(self, event):
        if isinstance(event, lldb.SBEvent):
            self.win.scroll()
            h = self.win.getmaxyx()[0]
            self.win.addstr(h - 1, 0, lldbutil.get_description(event))
        return
