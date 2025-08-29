#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2025 by Mauro Carvalho Chehab <mchehab@kernel.org>.

"""
Ancillary argparse HelpFormatter class that works on a similar way as
argparse.RawDescriptionHelpFormatter, e.g. description maintains line
breaks, but it also implement transformations to the help text. The
actual transformations ar given by enrich_text(), if the output is tty.

Currently, the follow transformations are done:

    - Positional arguments are shown in upper cases;
    - if output is TTY, ``var`` and positional arguments are shown prepended
      by an ANSI SGR code. This is usually translated to bold. On some
      terminals, like, konsole, this is translated into a colored bold text.
"""

import argparse
import re
import sys

class EnrichFormatter(argparse.HelpFormatter):
    """
    Better format the output, making easier to identify the positional args
    and how they're used at the __doc__ description.
    """
    def __init__(self, *args, **kwargs):
        """Initialize class and check if is TTY"""
        super().__init__(*args, **kwargs)
        self._tty = sys.stdout.isatty()

    def enrich_text(self, text):
        """Handle ReST markups (currently, only ``foo``)"""
        if self._tty and text:
            # Replace ``text`` with ANSI SGR (bold)
            return re.sub(r'\`\`(.+?)\`\`',
                          lambda m: f'\033[1m{m.group(1)}\033[0m', text)
        return text

    def _fill_text(self, text, width, indent):
        """Enrich descriptions with markups on it"""
        enriched = self.enrich_text(text)
        return "\n".join(indent + line for line in enriched.splitlines())

    def _format_usage(self, usage, actions, groups, prefix):
        """Enrich positional arguments at usage: line"""

        prog = self._prog
        parts = []

        for action in actions:
            if action.option_strings:
                opt = action.option_strings[0]
                if action.nargs != 0:
                    opt += f" {action.dest.upper()}"
                parts.append(f"[{opt}]")
            else:
                # Positional argument
                parts.append(self.enrich_text(f"``{action.dest.upper()}``"))

        usage_text = f"{prefix or 'usage: '} {prog} {' '.join(parts)}\n"
        return usage_text

    def _format_action_invocation(self, action):
        """Enrich argument names"""
        if not action.option_strings:
            return self.enrich_text(f"``{action.dest.upper()}``")

        return ", ".join(action.option_strings)
