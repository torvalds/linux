# This file is a minimal clang-format sublime-integration. To install:
# - Change 'binary' if clang-format is not on the path (see below).
# - Put this file into your sublime Packages directory, e.g. on Linux:
#     ~/.config/sublime-text-2/Packages/User/clang-format-sublime.py
# - Add a key binding:
#     { "keys": ["ctrl+shift+c"], "command": "clang_format" },
#
# With this integration you can press the bound key and clang-format will
# format the current lines and selections for all cursor positions. The lines
# or regions are extended to the next bigger syntactic entities.
#
# It operates on the current, potentially unsaved buffer and does not create
# or save any files. To revert a formatting, just undo.

from __future__ import absolute_import, division, print_function
import sublime
import sublime_plugin
import subprocess

# Change this to the full path if clang-format is not on the path.
binary = "clang-format"

# Change this to format according to other formatting styles. See the output of
# 'clang-format --help' for a list of supported styles. The default looks for
# a '.clang-format' or '_clang-format' file to indicate the style that should be
# used.
style = None


class ClangFormatCommand(sublime_plugin.TextCommand):
    def run(self, edit):
        encoding = self.view.encoding()
        if encoding == "Undefined":
            encoding = "utf-8"
        regions = []
        command = [binary]
        if style:
            command.extend(["-style", style])
        for region in self.view.sel():
            regions.append(region)
            region_offset = min(region.a, region.b)
            region_length = abs(region.b - region.a)
            command.extend(
                [
                    "-offset",
                    str(region_offset),
                    "-length",
                    str(region_length),
                    "-assume-filename",
                    str(self.view.file_name()),
                ]
            )
        old_viewport_position = self.view.viewport_position()
        buf = self.view.substr(sublime.Region(0, self.view.size()))
        p = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            stdin=subprocess.PIPE,
        )
        output, error = p.communicate(buf.encode(encoding))
        if error:
            print(error)
        self.view.replace(
            edit, sublime.Region(0, self.view.size()), output.decode(encoding)
        )
        self.view.sel().clear()
        for region in regions:
            self.view.sel().add(region)
        # FIXME: Without the 10ms delay, the viewport sometimes jumps.
        sublime.set_timeout(
            lambda: self.view.set_viewport_position(old_viewport_position, False), 10
        )
