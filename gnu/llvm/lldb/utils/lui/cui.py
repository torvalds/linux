##===-- cui.py -----------------------------------------------*- Python -*-===##
##
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
##
##===----------------------------------------------------------------------===##

import curses
import curses.ascii
import threading


class CursesWin(object):
    def __init__(self, x, y, w, h):
        self.win = curses.newwin(h, w, y, x)
        self.focus = False

    def setFocus(self, focus):
        self.focus = focus

    def getFocus(self):
        return self.focus

    def canFocus(self):
        return True

    def handleEvent(self, event):
        return

    def draw(self):
        return


class TextWin(CursesWin):
    def __init__(self, x, y, w):
        super(TextWin, self).__init__(x, y, w, 1)
        self.win.bkgd(curses.color_pair(1))
        self.text = ""
        self.reverse = False

    def canFocus(self):
        return False

    def draw(self):
        w = self.win.getmaxyx()[1]
        text = self.text
        if len(text) > w:
            # trunc_length = len(text) - w
            text = text[-w + 1 :]
        if self.reverse:
            self.win.addstr(0, 0, text, curses.A_REVERSE)
        else:
            self.win.addstr(0, 0, text)
        self.win.noutrefresh()

    def setReverse(self, reverse):
        self.reverse = reverse

    def setText(self, text):
        self.text = text


class TitledWin(CursesWin):
    def __init__(self, x, y, w, h, title):
        super(TitledWin, self).__init__(x, y + 1, w, h - 1)
        self.title = title
        self.title_win = TextWin(x, y, w)
        self.title_win.setText(title)
        self.draw()

    def setTitle(self, title):
        self.title_win.setText(title)

    def draw(self):
        self.title_win.setReverse(self.getFocus())
        self.title_win.draw()
        self.win.noutrefresh()


class ListWin(CursesWin):
    def __init__(self, x, y, w, h):
        super(ListWin, self).__init__(x, y, w, h)
        self.items = []
        self.selected = 0
        self.first_drawn = 0
        self.win.leaveok(True)

    def draw(self):
        if len(self.items) == 0:
            self.win.erase()
            return

        h, w = self.win.getmaxyx()

        allLines = []
        firstSelected = -1
        lastSelected = -1
        for i, item in enumerate(self.items):
            lines = self.items[i].split("\n")
            lines = lines if lines[len(lines) - 1] != "" else lines[:-1]
            if len(lines) == 0:
                lines = [""]

            if i == self.getSelected():
                firstSelected = len(allLines)
            allLines.extend(lines)
            if i == self.selected:
                lastSelected = len(allLines) - 1

        if firstSelected < self.first_drawn:
            self.first_drawn = firstSelected
        elif lastSelected >= self.first_drawn + h:
            self.first_drawn = lastSelected - h + 1

        self.win.erase()

        begin = self.first_drawn
        end = begin + h

        y = 0
        for i, line in list(enumerate(allLines))[begin:end]:
            attr = curses.A_NORMAL
            if i >= firstSelected and i <= lastSelected:
                attr = curses.A_REVERSE
                line = "{0:{width}}".format(line, width=w - 1)

            # Ignore the error we get from drawing over the bottom-right char.
            try:
                self.win.addstr(y, 0, line[:w], attr)
            except curses.error:
                pass
            y += 1
        self.win.noutrefresh()

    def getSelected(self):
        if self.items:
            return self.selected
        return -1

    def setSelected(self, selected):
        self.selected = selected
        if self.selected < 0:
            self.selected = 0
        elif self.selected >= len(self.items):
            self.selected = len(self.items) - 1

    def handleEvent(self, event):
        if isinstance(event, int):
            if len(self.items) > 0:
                if event == curses.KEY_UP:
                    self.setSelected(self.selected - 1)
                if event == curses.KEY_DOWN:
                    self.setSelected(self.selected + 1)
                if event == curses.ascii.NL:
                    self.handleSelect(self.selected)

    def addItem(self, item):
        self.items.append(item)

    def clearItems(self):
        self.items = []

    def handleSelect(self, index):
        return


class InputHandler(threading.Thread):
    def __init__(self, screen, queue):
        super(InputHandler, self).__init__()
        self.screen = screen
        self.queue = queue

    def run(self):
        while True:
            c = self.screen.getch()
            self.queue.put(c)


class CursesUI(object):
    """Responsible for updating the console UI with curses."""

    def __init__(self, screen, event_queue):
        self.screen = screen
        self.event_queue = event_queue

        curses.start_color()
        curses.init_pair(1, curses.COLOR_WHITE, curses.COLOR_BLUE)
        curses.init_pair(2, curses.COLOR_YELLOW, curses.COLOR_BLACK)
        curses.init_pair(3, curses.COLOR_RED, curses.COLOR_BLACK)
        self.screen.bkgd(curses.color_pair(1))
        self.screen.clear()

        self.input_handler = InputHandler(self.screen, self.event_queue)
        self.input_handler.daemon = True

        self.focus = 0

        self.screen.refresh()

    def focusNext(self):
        self.wins[self.focus].setFocus(False)
        old = self.focus
        while True:
            self.focus += 1
            if self.focus >= len(self.wins):
                self.focus = 0
            if self.wins[self.focus].canFocus():
                break
        self.wins[self.focus].setFocus(True)

    def handleEvent(self, event):
        if isinstance(event, int):
            if event == curses.KEY_F3:
                self.focusNext()

    def eventLoop(self):
        self.input_handler.start()
        self.wins[self.focus].setFocus(True)

        while True:
            self.screen.noutrefresh()

            for i, win in enumerate(self.wins):
                if i != self.focus:
                    win.draw()
            # Draw the focused window last so that the cursor shows up.
            if self.wins:
                self.wins[self.focus].draw()
            curses.doupdate()  # redraw the physical screen

            event = self.event_queue.get()

            for win in self.wins:
                if isinstance(event, int):
                    if win.getFocus() or not win.canFocus():
                        win.handleEvent(event)
                else:
                    win.handleEvent(event)
            self.handleEvent(event)


class CursesEditLine(object):
    """Embed an 'editline'-compatible prompt inside a CursesWin."""

    def __init__(self, win, history, enterCallback, tabCompleteCallback):
        self.win = win
        self.history = history
        self.enterCallback = enterCallback
        self.tabCompleteCallback = tabCompleteCallback

        self.prompt = ""
        self.content = ""
        self.index = 0
        self.startx = -1
        self.starty = -1

    def draw(self, prompt=None):
        if not prompt:
            prompt = self.prompt
        (h, w) = self.win.getmaxyx()
        if (len(prompt) + len(self.content)) / w + self.starty >= h - 1:
            self.win.scroll(1)
            self.starty -= 1
            if self.starty < 0:
                raise RuntimeError("Input too long; aborting")
        (y, x) = (self.starty, self.startx)

        self.win.move(y, x)
        self.win.clrtobot()
        self.win.addstr(y, x, prompt)
        remain = self.content
        self.win.addstr(remain[: w - len(prompt)])
        remain = remain[w - len(prompt) :]
        while remain != "":
            y += 1
            self.win.addstr(y, 0, remain[:w])
            remain = remain[w:]

        length = self.index + len(prompt)
        self.win.move(self.starty + length / w, length % w)

    def showPrompt(self, y, x, prompt=None):
        self.content = ""
        self.index = 0
        self.startx = x
        self.starty = y
        self.draw(prompt)

    def handleEvent(self, event):
        if not isinstance(event, int):
            return  # not handled
        key = event

        if self.startx == -1:
            raise RuntimeError("Trying to handle input without prompt")

        if key == curses.ascii.NL:
            self.enterCallback(self.content)
        elif key == curses.ascii.TAB:
            self.tabCompleteCallback(self.content)
        elif curses.ascii.isprint(key):
            self.content = (
                self.content[: self.index] + chr(key) + self.content[self.index :]
            )
            self.index += 1
        elif key == curses.KEY_BACKSPACE or key == curses.ascii.BS:
            if self.index > 0:
                self.index -= 1
                self.content = (
                    self.content[: self.index] + self.content[self.index + 1 :]
                )
        elif key == curses.KEY_DC or key == curses.ascii.DEL or key == curses.ascii.EOT:
            self.content = self.content[: self.index] + self.content[self.index + 1 :]
        elif key == curses.ascii.VT:  # CTRL-K
            self.content = self.content[: self.index]
        elif key == curses.KEY_LEFT or key == curses.ascii.STX:  # left or CTRL-B
            if self.index > 0:
                self.index -= 1
        elif key == curses.KEY_RIGHT or key == curses.ascii.ACK:  # right or CTRL-F
            if self.index < len(self.content):
                self.index += 1
        elif key == curses.ascii.SOH:  # CTRL-A
            self.index = 0
        elif key == curses.ascii.ENQ:  # CTRL-E
            self.index = len(self.content)
        elif key == curses.KEY_UP or key == curses.ascii.DLE:  # up or CTRL-P
            self.content = self.history.previous(self.content)
            self.index = len(self.content)
        elif key == curses.KEY_DOWN or key == curses.ascii.SO:  # down or CTRL-N
            self.content = self.history.next()
            self.index = len(self.content)
        self.draw()
