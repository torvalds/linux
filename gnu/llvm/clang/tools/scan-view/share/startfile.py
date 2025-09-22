#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""Utility for opening a file using the default application in a cross-platform
manner. Modified from http://code.activestate.com/recipes/511443/.
"""

__version__ = "1.1x"
__all__ = ["open"]

import os
import sys
import webbrowser
import subprocess

_controllers = {}
_open = None


class BaseController(object):
    """Base class for open program controllers."""

    def __init__(self, name):
        self.name = name

    def open(self, filename):
        raise NotImplementedError


class Controller(BaseController):
    """Controller for a generic open program."""

    def __init__(self, *args):
        super(Controller, self).__init__(os.path.basename(args[0]))
        self.args = list(args)

    def _invoke(self, cmdline):
        if sys.platform[:3] == "win":
            closefds = False
            startupinfo = subprocess.STARTUPINFO()
            startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        else:
            closefds = True
            startupinfo = None

        if (
            os.environ.get("DISPLAY")
            or sys.platform[:3] == "win"
            or sys.platform == "darwin"
        ):
            inout = file(os.devnull, "r+")
        else:
            # for TTY programs, we need stdin/out
            inout = None

        # if possible, put the child precess in separate process group,
        # so keyboard interrupts don't affect child precess as well as
        # Python
        setsid = getattr(os, "setsid", None)
        if not setsid:
            setsid = getattr(os, "setpgrp", None)

        pipe = subprocess.Popen(
            cmdline,
            stdin=inout,
            stdout=inout,
            stderr=inout,
            close_fds=closefds,
            preexec_fn=setsid,
            startupinfo=startupinfo,
        )

        # It is assumed that this kind of tools (gnome-open, kfmclient,
        # exo-open, xdg-open and open for OSX) immediately exit after launching
        # the specific application
        returncode = pipe.wait()
        if hasattr(self, "fixreturncode"):
            returncode = self.fixreturncode(returncode)
        return not returncode

    def open(self, filename):
        if isinstance(filename, basestring):
            cmdline = self.args + [filename]
        else:
            # assume it is a sequence
            cmdline = self.args + filename
        try:
            return self._invoke(cmdline)
        except OSError:
            return False


# Platform support for Windows
if sys.platform[:3] == "win":

    class Start(BaseController):
        """Controller for the win32 start program through os.startfile."""

        def open(self, filename):
            try:
                os.startfile(filename)
            except WindowsError:
                # [Error 22] No application is associated with the specified
                # file for this operation: '<URL>'
                return False
            else:
                return True

    _controllers["windows-default"] = Start("start")
    _open = _controllers["windows-default"].open


# Platform support for MacOS
elif sys.platform == "darwin":
    _controllers["open"] = Controller("open")
    _open = _controllers["open"].open


# Platform support for Unix
else:

    try:
        from commands import getoutput
    except ImportError:
        from subprocess import getoutput

    # @WARNING: use the private API of the webbrowser module
    from webbrowser import _iscommand

    class KfmClient(Controller):
        """Controller for the KDE kfmclient program."""

        def __init__(self, kfmclient="kfmclient"):
            super(KfmClient, self).__init__(kfmclient, "exec")
            self.kde_version = self.detect_kde_version()

        def detect_kde_version(self):
            kde_version = None
            try:
                info = getoutput("kde-config --version")

                for line in info.splitlines():
                    if line.startswith("KDE"):
                        kde_version = line.split(":")[-1].strip()
                        break
            except (OSError, RuntimeError):
                pass

            return kde_version

        def fixreturncode(self, returncode):
            if returncode is not None and self.kde_version > "3.5.4":
                return returncode
            else:
                return os.EX_OK

    def detect_desktop_environment():
        """Checks for known desktop environments

        Return the desktop environments name, lowercase (kde, gnome, xfce)
        or "generic"

        """

        desktop_environment = "generic"

        if os.environ.get("KDE_FULL_SESSION") == "true":
            desktop_environment = "kde"
        elif os.environ.get("GNOME_DESKTOP_SESSION_ID"):
            desktop_environment = "gnome"
        else:
            try:
                info = getoutput("xprop -root _DT_SAVE_MODE")
                if ' = "xfce4"' in info:
                    desktop_environment = "xfce"
            except (OSError, RuntimeError):
                pass

        return desktop_environment

    def register_X_controllers():
        if _iscommand("kfmclient"):
            _controllers["kde-open"] = KfmClient()

        for command in ("gnome-open", "exo-open", "xdg-open"):
            if _iscommand(command):
                _controllers[command] = Controller(command)

    def get():
        controllers_map = {
            "gnome": "gnome-open",
            "kde": "kde-open",
            "xfce": "exo-open",
        }

        desktop_environment = detect_desktop_environment()

        try:
            controller_name = controllers_map[desktop_environment]
            return _controllers[controller_name].open

        except KeyError:
            if "xdg-open" in _controllers:
                return _controllers["xdg-open"].open
            else:
                return webbrowser.open

    if os.environ.get("DISPLAY"):
        register_X_controllers()
    _open = get()


def open(filename):
    """Open a file or a URL in the registered default application."""

    return _open(filename)
