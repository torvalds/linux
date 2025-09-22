#!/usr/bin/env python

# Source: http://code.activestate.com/recipes/475116/, with
# modifications by Daniel Dunbar.

import sys, re, time


def to_bytes(str):
    # Encode to UTF-8 to get binary data.
    return str.encode("utf-8")


class TerminalController:
    """
    A class that can be used to portably generate formatted output to
    a terminal.

    `TerminalController` defines a set of instance variables whose
    values are initialized to the control sequence necessary to
    perform a given action.  These can be simply included in normal
    output to the terminal:

        >>> term = TerminalController()
        >>> print('This is '+term.GREEN+'green'+term.NORMAL)

    Alternatively, the `render()` method can used, which replaces
    '${action}' with the string required to perform 'action':

        >>> term = TerminalController()
        >>> print(term.render('This is ${GREEN}green${NORMAL}'))

    If the terminal doesn't support a given action, then the value of
    the corresponding instance variable will be set to ''.  As a
    result, the above code will still work on terminals that do not
    support color, except that their output will not be colored.
    Also, this means that you can test whether the terminal supports a
    given action by simply testing the truth value of the
    corresponding instance variable:

        >>> term = TerminalController()
        >>> if term.CLEAR_SCREEN:
        ...     print('This terminal supports clearning the screen.')

    Finally, if the width and height of the terminal are known, then
    they will be stored in the `COLS` and `LINES` attributes.
    """

    # Cursor movement:
    BOL = ""  #: Move the cursor to the beginning of the line
    UP = ""  #: Move the cursor up one line
    DOWN = ""  #: Move the cursor down one line
    LEFT = ""  #: Move the cursor left one char
    RIGHT = ""  #: Move the cursor right one char

    # Deletion:
    CLEAR_SCREEN = ""  #: Clear the screen and move to home position
    CLEAR_EOL = ""  #: Clear to the end of the line.
    CLEAR_BOL = ""  #: Clear to the beginning of the line.
    CLEAR_EOS = ""  #: Clear to the end of the screen

    # Output modes:
    BOLD = ""  #: Turn on bold mode
    BLINK = ""  #: Turn on blink mode
    DIM = ""  #: Turn on half-bright mode
    REVERSE = ""  #: Turn on reverse-video mode
    NORMAL = ""  #: Turn off all modes

    # Cursor display:
    HIDE_CURSOR = ""  #: Make the cursor invisible
    SHOW_CURSOR = ""  #: Make the cursor visible

    # Terminal size:
    COLS = None  #: Width of the terminal (None for unknown)
    LINES = None  #: Height of the terminal (None for unknown)

    # Foreground colors:
    BLACK = BLUE = GREEN = CYAN = RED = MAGENTA = YELLOW = WHITE = ""

    # Background colors:
    BG_BLACK = BG_BLUE = BG_GREEN = BG_CYAN = ""
    BG_RED = BG_MAGENTA = BG_YELLOW = BG_WHITE = ""

    _STRING_CAPABILITIES = """
    BOL=cr UP=cuu1 DOWN=cud1 LEFT=cub1 RIGHT=cuf1
    CLEAR_SCREEN=clear CLEAR_EOL=el CLEAR_BOL=el1 CLEAR_EOS=ed BOLD=bold
    BLINK=blink DIM=dim REVERSE=rev UNDERLINE=smul NORMAL=sgr0
    HIDE_CURSOR=cinvis SHOW_CURSOR=cnorm""".split()
    _COLORS = """BLACK BLUE GREEN CYAN RED MAGENTA YELLOW WHITE""".split()
    _ANSICOLORS = "BLACK RED GREEN YELLOW BLUE MAGENTA CYAN WHITE".split()

    def __init__(self, term_stream=sys.stdout):
        """
        Create a `TerminalController` and initialize its attributes
        with appropriate values for the current terminal.
        `term_stream` is the stream that will be used for terminal
        output; if this stream is not a tty, then the terminal is
        assumed to be a dumb terminal (i.e., have no capabilities).
        """
        # Curses isn't available on all platforms
        try:
            import curses
        except:
            return

        # If the stream isn't a tty, then assume it has no capabilities.
        if not term_stream.isatty():
            return

        # Check the terminal type.  If we fail, then assume that the
        # terminal has no capabilities.
        try:
            curses.setupterm()
        except:
            return

        # Look up numeric capabilities.
        self.COLS = curses.tigetnum("cols")
        self.LINES = curses.tigetnum("lines")
        self.XN = curses.tigetflag("xenl")

        # Look up string capabilities.
        for capability in self._STRING_CAPABILITIES:
            (attrib, cap_name) = capability.split("=")
            setattr(self, attrib, self._tigetstr(cap_name) or "")

        # Colors
        set_fg = self._tigetstr("setf")
        if set_fg:
            for i, color in zip(range(len(self._COLORS)), self._COLORS):
                setattr(self, color, self._tparm(set_fg, i))
        set_fg_ansi = self._tigetstr("setaf")
        if set_fg_ansi:
            for i, color in zip(range(len(self._ANSICOLORS)), self._ANSICOLORS):
                setattr(self, color, self._tparm(set_fg_ansi, i))
        set_bg = self._tigetstr("setb")
        if set_bg:
            for i, color in zip(range(len(self._COLORS)), self._COLORS):
                setattr(self, "BG_" + color, self._tparm(set_bg, i))
        set_bg_ansi = self._tigetstr("setab")
        if set_bg_ansi:
            for i, color in zip(range(len(self._ANSICOLORS)), self._ANSICOLORS):
                setattr(self, "BG_" + color, self._tparm(set_bg_ansi, i))

    def _tparm(self, arg, index):
        import curses

        return curses.tparm(to_bytes(arg), index).decode("utf-8") or ""

    def _tigetstr(self, cap_name):
        # String capabilities can include "delays" of the form "$<2>".
        # For any modern terminal, we should be able to just ignore
        # these, so strip them out.
        import curses

        cap = curses.tigetstr(cap_name)
        if cap is None:
            cap = ""
        else:
            cap = cap.decode("utf-8")
        return re.sub(r"\$<\d+>[/*]?", "", cap)

    def render(self, template):
        """
        Replace each $-substitutions in the given template string with
        the corresponding terminal control string (if it's defined) or
        '' (if it's not).
        """
        return re.sub(r"\$\$|\${\w+}", self._render_sub, template)

    def _render_sub(self, match):
        s = match.group()
        if s == "$$":
            return s
        else:
            return getattr(self, s[2:-1])


#######################################################################
# Example use case: progress bar
#######################################################################


class SimpleProgressBar:
    """
    A simple progress bar which doesn't need any terminal support.

    This prints out a progress bar like:
      'Header:  0.. 10.. 20.. ...'
    """

    def __init__(self, header):
        self.header = header
        self.atIndex = None

    def update(self, percent, message):
        if self.atIndex is None:
            sys.stdout.write(self.header)
            self.atIndex = 0

        next = int(percent * 50)
        if next == self.atIndex:
            return

        for i in range(self.atIndex, next):
            idx = i % 5
            if idx == 0:
                sys.stdout.write("%2d" % (i * 2))
            elif idx == 1:
                pass  # Skip second char
            elif idx < 4:
                sys.stdout.write(".")
            else:
                sys.stdout.write(" ")
        sys.stdout.flush()
        self.atIndex = next

    def clear(self, interrupted):
        if self.atIndex is not None and not interrupted:
            sys.stdout.write("\n")
            sys.stdout.flush()
            self.atIndex = None


class ProgressBar:
    """
    A 3-line progress bar, which looks like::

                                Header
        20% [===========----------------------------------]
                           progress message

    The progress bar is colored, if the terminal supports color
    output; and adjusts to the width of the terminal.
    """

    BAR = "%s${%s}[${BOLD}%s%s${NORMAL}${%s}]${NORMAL}%s"
    HEADER = "${BOLD}${CYAN}%s${NORMAL}\n\n"

    def __init__(self, term, header, useETA=True):
        self.term = term
        if not (self.term.CLEAR_EOL and self.term.UP and self.term.BOL):
            raise ValueError(
                "Terminal isn't capable enough -- you "
                "should use a simpler progress dispaly."
            )
        self.BOL = self.term.BOL  # BoL from col#79
        self.XNL = "\n"  # Newline from col#79
        if self.term.COLS:
            self.width = self.term.COLS
            if not self.term.XN:
                self.BOL = self.term.UP + self.term.BOL
                self.XNL = ""  # Cursor must be fed to the next line
        else:
            self.width = 75
        self.barColor = "GREEN"
        self.header = self.term.render(self.HEADER % header.center(self.width))
        self.cleared = 1  #: true if we haven't drawn the bar yet.
        self.useETA = useETA
        if self.useETA:
            self.startTime = time.time()
        # self.update(0, '')

    def update(self, percent, message):
        if self.cleared:
            sys.stdout.write(self.header)
            self.cleared = 0
        prefix = "%3d%% " % (percent * 100,)
        suffix = ""
        if self.useETA:
            elapsed = time.time() - self.startTime
            if percent > 0.0001 and elapsed > 1:
                total = elapsed / percent
                eta = total - elapsed
                h = eta // 3600.0
                m = (eta // 60) % 60
                s = eta % 60
                suffix = " ETA: %02d:%02d:%02d" % (h, m, s)
        barWidth = self.width - len(prefix) - len(suffix) - 2
        n = int(barWidth * percent)
        if len(message) < self.width:
            message = message + " " * (self.width - len(message))
        else:
            message = "... " + message[-(self.width - 4) :]
        bc = self.barColor
        bar = self.BAR % (prefix, bc, "=" * n, "-" * (barWidth - n), bc, suffix)
        bar = self.term.render(bar)
        sys.stdout.write(
            self.BOL
            + self.term.UP
            + self.term.CLEAR_EOL
            + bar
            + self.XNL
            + self.term.CLEAR_EOL
            + message
        )
        if not self.term.XN:
            sys.stdout.flush()

    def clear(self, interrupted):
        if not self.cleared:
            sys.stdout.write(
                self.BOL
                + self.term.CLEAR_EOL
                + self.term.UP
                + self.term.CLEAR_EOL
                + self.term.UP
                + self.term.CLEAR_EOL
            )
            if interrupted:  # ^C creates extra line. Gobble it up!
                sys.stdout.write(self.term.UP + self.term.CLEAR_EOL)
                sys.stdout.write("^C")
            sys.stdout.flush()
            self.cleared = 1


def test():
    tc = TerminalController()
    p = ProgressBar(tc, "Tests")
    for i in range(101):
        p.update(i / 100.0, str(i))
        time.sleep(0.3)


if __name__ == "__main__":
    test()
