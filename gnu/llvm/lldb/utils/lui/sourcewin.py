##===-- sourcewin.py -----------------------------------------*- Python -*-===##
##
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
##
##===----------------------------------------------------------------------===##

import cui
import curses
import lldb
import lldbutil
import re
import os


class SourceWin(cui.TitledWin):
    def __init__(self, driver, x, y, w, h):
        super(SourceWin, self).__init__(x, y, w, h, "Source")
        self.sourceman = driver.getSourceManager()
        self.sources = {}

        self.filename = None
        self.pc_line = None
        self.viewline = 0

        self.breakpoints = {}

        self.win.scrollok(1)

        self.markerPC = ":) "
        self.markerBP = "B> "
        self.markerNone = "   "

        try:
            from pygments.formatters import TerminalFormatter

            self.formatter = TerminalFormatter()
        except ImportError:
            # self.win.addstr("\nWarning: no 'pygments' library found. Syntax highlighting is disabled.")
            self.lexer = None
            self.formatter = None
            pass

        # FIXME: syntax highlight broken
        self.formatter = None
        self.lexer = None

    def handleEvent(self, event):
        if isinstance(event, int):
            self.handleKey(event)
            return

        if isinstance(event, lldb.SBEvent):
            if lldb.SBBreakpoint.EventIsBreakpointEvent(event):
                self.handleBPEvent(event)

            if lldb.SBProcess.EventIsProcessEvent(
                event
            ) and not lldb.SBProcess.GetRestartedFromEvent(event):
                process = lldb.SBProcess.GetProcessFromEvent(event)
                if not process.IsValid():
                    return
                if process.GetState() == lldb.eStateStopped:
                    self.refreshSource(process)
                elif process.GetState() == lldb.eStateExited:
                    self.notifyExited(process)

    def notifyExited(self, process):
        self.win.erase()
        target = lldbutil.get_description(process.GetTarget())
        pid = process.GetProcessID()
        ec = process.GetExitStatus()
        self.win.addstr(
            "\nProcess %s [%d] has exited with exit-code %d" % (target, pid, ec)
        )

    def pageUp(self):
        if self.viewline > 0:
            self.viewline = self.viewline - 1
            self.refreshSource()

    def pageDown(self):
        if self.viewline < len(self.content) - self.height + 1:
            self.viewline = self.viewline + 1
            self.refreshSource()
        pass

    def handleKey(self, key):
        if key == curses.KEY_DOWN:
            self.pageDown()
        elif key == curses.KEY_UP:
            self.pageUp()

    def updateViewline(self):
        half = self.height / 2
        if self.pc_line < half:
            self.viewline = 0
        else:
            self.viewline = self.pc_line - half + 1

        if self.viewline < 0:
            raise Exception(
                "negative viewline: pc=%d viewline=%d" % (self.pc_line, self.viewline)
            )

    def refreshSource(self, process=None):
        (self.height, self.width) = self.win.getmaxyx()

        if process is not None:
            loc = process.GetSelectedThread().GetSelectedFrame().GetLineEntry()
            f = loc.GetFileSpec()
            self.pc_line = loc.GetLine()

            if not f.IsValid():
                self.win.addstr(0, 0, "Invalid source file")
                return

            self.filename = f.GetFilename()
            path = os.path.join(f.GetDirectory(), self.filename)
            self.setTitle(path)
            self.content = self.getContent(path)
            self.updateViewline()

        if self.filename is None:
            return

        if self.formatter is not None:
            from pygments.lexers import get_lexer_for_filename

            self.lexer = get_lexer_for_filename(self.filename)

        bps = (
            []
            if not self.filename in self.breakpoints
            else self.breakpoints[self.filename]
        )
        self.win.erase()
        if self.content:
            self.formatContent(self.content, self.pc_line, bps)

    def getContent(self, path):
        content = []
        if path in self.sources:
            content = self.sources[path]
        else:
            if os.path.exists(path):
                with open(path) as x:
                    content = x.readlines()
                self.sources[path] = content
        return content

    def formatContent(self, content, pc_line, breakpoints):
        source = ""
        count = 1
        self.win.erase()
        end = min(len(content), self.viewline + self.height)
        for i in range(self.viewline, end):
            line_num = i + 1
            marker = self.markerNone
            attr = curses.A_NORMAL
            if line_num == pc_line:
                attr = curses.A_REVERSE
            if line_num in breakpoints:
                marker = self.markerBP
            line = "%s%3d %s" % (marker, line_num, self.highlight(content[i]))
            if len(line) >= self.width:
                line = line[0 : self.width - 1] + "\n"
            self.win.addstr(line, attr)
            source += line
            count = count + 1
        return source

    def highlight(self, source):
        if self.lexer and self.formatter:
            from pygments import highlight

            return highlight(source, self.lexer, self.formatter)
        else:
            return source

    def addBPLocations(self, locations):
        for path in locations:
            lines = locations[path]
            if path in self.breakpoints:
                self.breakpoints[path].update(lines)
            else:
                self.breakpoints[path] = lines

    def removeBPLocations(self, locations):
        for path in locations:
            lines = locations[path]
            if path in self.breakpoints:
                self.breakpoints[path].difference_update(lines)
            else:
                raise "Removing locations that were never added...no good"

    def handleBPEvent(self, event):
        def getLocations(event):
            locs = {}

            bp = lldb.SBBreakpoint.GetBreakpointFromEvent(event)

            if bp.IsInternal():
                # don't show anything for internal breakpoints
                return

            for location in bp:
                # hack! getting the LineEntry via SBBreakpointLocation.GetAddress.GetLineEntry does not work good for
                # inlined frames, so we get the description (which does take
                # into account inlined functions) and parse it.
                desc = lldbutil.get_description(location, lldb.eDescriptionLevelFull)
                match = re.search("at\ ([^:]+):([\d]+)", desc)
                try:
                    path = match.group(1)
                    line = int(match.group(2).strip())
                except ValueError as e:
                    # bp loc unparsable
                    continue

                if path in locs:
                    locs[path].add(line)
                else:
                    locs[path] = set([line])
            return locs

        event_type = lldb.SBBreakpoint.GetBreakpointEventTypeFromEvent(event)
        if (
            event_type == lldb.eBreakpointEventTypeEnabled
            or event_type == lldb.eBreakpointEventTypeAdded
            or event_type == lldb.eBreakpointEventTypeLocationsResolved
            or event_type == lldb.eBreakpointEventTypeLocationsAdded
        ):
            self.addBPLocations(getLocations(event))
        elif (
            event_type == lldb.eBreakpointEventTypeRemoved
            or event_type == lldb.eBreakpointEventTypeLocationsRemoved
            or event_type == lldb.eBreakpointEventTypeDisabled
        ):
            self.removeBPLocations(getLocations(event))
        elif (
            event_type == lldb.eBreakpointEventTypeCommandChanged
            or event_type == lldb.eBreakpointEventTypeConditionChanged
            or event_type == lldb.eBreakpointEventTypeIgnoreChanged
            or event_type == lldb.eBreakpointEventTypeThreadChanged
            or event_type == lldb.eBreakpointEventTypeInvalidType
        ):
            # no-op
            pass
        self.refreshSource()
