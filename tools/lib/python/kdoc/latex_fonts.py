#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) Akira Yokosawa, 2024
#
# Ported to Python by (c) Mauro Carvalho Chehab, 2025

"""
Detect problematic Noto CJK variable fonts.

For "make pdfdocs", reports of build errors of translations.pdf started
arriving early 2024 [1, 2].  It turned out that Fedora and openSUSE
tumbleweed have started deploying variable-font [3] format of "Noto CJK"
fonts [4, 5].  For PDF, a LaTeX package named xeCJK is used for CJK
(Chinese, Japanese, Korean) pages.  xeCJK requires XeLaTeX/XeTeX, which
does not (and likely never will) understand variable fonts for historical
reasons.

The build error happens even when both of variable- and non-variable-format
fonts are found on the build system.  To make matters worse, Fedora enlists
variable "Noto CJK" fonts in the requirements of langpacks-ja, -ko, -zh_CN,
-zh_TW, etc.  Hence developers who have interest in CJK pages are more
likely to encounter the build errors.

This script is invoked from the error path of "make pdfdocs" and emits
suggestions if variable-font files of "Noto CJK" fonts are in the list of
fonts accessible from XeTeX.

References:
[1]: https://lore.kernel.org/r/8734tqsrt7.fsf@meer.lwn.net/
[2]: https://lore.kernel.org/r/1708585803.600323099@f111.i.mail.ru/
[3]: https://en.wikipedia.org/wiki/Variable_font
[4]: https://fedoraproject.org/wiki/Changes/Noto_CJK_Variable_Fonts
[5]: https://build.opensuse.org/request/show/1157217

#===========================================================================
Workarounds for building translations.pdf
#===========================================================================

* Denylist "variable font" Noto CJK fonts.
  - Create $HOME/deny-vf/fontconfig/fonts.conf from template below, with
    tweaks if necessary.  Remove leading "".
  - Path of fontconfig/fonts.conf can be overridden by setting an env
    variable FONTS_CONF_DENY_VF.

    * Template:
-----------------------------------------------------------------
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
<!--
  Ignore variable-font glob (not to break xetex)
-->
    <selectfont>
        <rejectfont>
            <!--
                for Fedora
            -->
            <glob>/usr/share/fonts/google-noto-*-cjk-vf-fonts</glob>
            <!--
                for openSUSE tumbleweed
            -->
            <glob>/usr/share/fonts/truetype/Noto*CJK*-VF.otf</glob>
        </rejectfont>
    </selectfont>
</fontconfig>
-----------------------------------------------------------------

    The denylisting is activated for "make pdfdocs".

* For skipping CJK pages in PDF
  - Uninstall texlive-xecjk.
    Denylisting is not needed in this case.

* For printing CJK pages in PDF
  - Need non-variable "Noto CJK" fonts.
    * Fedora
      - google-noto-sans-cjk-fonts
      - google-noto-serif-cjk-fonts
    * openSUSE tumbleweed
      - Non-variable "Noto CJK" fonts are not available as distro packages
        as of April, 2024.  Fetch a set of font files from upstream Noto
        CJK Font released at:
          https://github.com/notofonts/noto-cjk/tree/main/Sans#super-otc
        and at:
          https://github.com/notofonts/noto-cjk/tree/main/Serif#super-otc
        , then uncompress and deploy them.
      - Remember to update fontconfig cache by running fc-cache.

!!! Caution !!!
    Uninstalling "variable font" packages can be dangerous.
    They might be depended upon by other packages important for your work.
    Denylisting should be less invasive, as it is effective only while
    XeLaTeX runs in "make pdfdocs".
"""

import os
import re
import subprocess
import textwrap
import sys

class LatexFontChecker:
    """
    Detect problems with CJK variable fonts that affect PDF builds for
    translations.
    """

    def __init__(self, deny_vf=None):
        if not deny_vf:
            deny_vf = os.environ.get('FONTS_CONF_DENY_VF', "~/deny-vf")

        self.environ = os.environ.copy()
        self.environ['XDG_CONFIG_HOME'] = os.path.expanduser(deny_vf)

        self.re_cjk = re.compile(r"([^:]+):\s*Noto\s+(Sans|Sans Mono|Serif) CJK")

    def description(self):
        return __doc__

    def get_noto_cjk_vf_fonts(self):
        """Get Noto CJK fonts"""

        cjk_fonts = set()
        cmd = ["fc-list", ":", "file", "family", "variable"]
        try:
            result = subprocess.run(cmd,stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE,
                                    universal_newlines=True,
                                    env=self.environ,
                                    check=True)

        except subprocess.CalledProcessError as exc:
            sys.exit(f"Error running fc-list: {repr(exc)}")

        for line in result.stdout.splitlines():
            if 'variable=True' not in line:
                continue

            match = self.re_cjk.search(line)
            if match:
                cjk_fonts.add(match.group(1))

        return sorted(cjk_fonts)

    def check(self):
        """Check for problems with CJK fonts"""

        fonts = textwrap.indent("\n".join(self.get_noto_cjk_vf_fonts()), "    ")
        if not fonts:
            return None

        rel_file = os.path.relpath(__file__, os.getcwd())

        msg = "=" * 77 + "\n"
        msg += 'XeTeX is confused by "variable font" files listed below:\n'
        msg += fonts + "\n"
        msg += textwrap.dedent(f"""
                For CJK pages in PDF, they need to be hidden from XeTeX by denylisting.
                Or, CJK pages can be skipped by uninstalling texlive-xecjk.

                For more info on denylisting, other options, and variable font, run:

                    tools/docs/check-variable-fonts.py -h
            """)
        msg += "=" * 77

        return msg
