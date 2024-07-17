#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) Akira Yokosawa, 2024
#
# For "make pdfdocs", reports of build errors of translations.pdf started
# arriving early 2024 [1, 2].  It turned out that Fedora and openSUSE
# tumbleweed have started deploying variable-font [3] format of "Noto CJK"
# fonts [4, 5].  For PDF, a LaTeX package named xeCJK is used for CJK
# (Chinese, Japanese, Korean) pages.  xeCJK requires XeLaTeX/XeTeX, which
# does not (and likely never will) understand variable fonts for historical
# reasons.
#
# The build error happens even when both of variable- and non-variable-format
# fonts are found on the build system.  To make matters worse, Fedora enlists
# variable "Noto CJK" fonts in the requirements of langpacks-ja, -ko, -zh_CN,
# -zh_TW, etc.  Hence developers who have interest in CJK pages are more
# likely to encounter the build errors.
#
# This script is invoked from the error path of "make pdfdocs" and emits
# suggestions if variable-font files of "Noto CJK" fonts are in the list of
# fonts accessible from XeTeX.
#
# References:
# [1]: https://lore.kernel.org/r/8734tqsrt7.fsf@meer.lwn.net/
# [2]: https://lore.kernel.org/r/1708585803.600323099@f111.i.mail.ru/
# [3]: https://en.wikipedia.org/wiki/Variable_font
# [4]: https://fedoraproject.org/wiki/Changes/Noto_CJK_Variable_Fonts
# [5]: https://build.opensuse.org/request/show/1157217
#
#===========================================================================
# Workarounds for building translations.pdf
#===========================================================================
#
# * Denylist "variable font" Noto CJK fonts.
#   - Create $HOME/deny-vf/fontconfig/fonts.conf from template below, with
#     tweaks if necessary.  Remove leading "# ".
#   - Path of fontconfig/fonts.conf can be overridden by setting an env
#     variable FONTS_CONF_DENY_VF.
#
#     * Template:
# -----------------------------------------------------------------
# <?xml version="1.0"?>
# <!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
# <fontconfig>
# <!--
#   Ignore variable-font glob (not to break xetex)
# -->
#     <selectfont>
#         <rejectfont>
#             <!--
#                 for Fedora
#             -->
#             <glob>/usr/share/fonts/google-noto-*-cjk-vf-fonts</glob>
#             <!--
#                 for openSUSE tumbleweed
#             -->
#             <glob>/usr/share/fonts/truetype/Noto*CJK*-VF.otf</glob>
#         </rejectfont>
#     </selectfont>
# </fontconfig>
# -----------------------------------------------------------------
#
#     The denylisting is activated for "make pdfdocs".
#
# * For skipping CJK pages in PDF
#   - Uninstall texlive-xecjk.
#     Denylisting is not needed in this case.
#
# * For printing CJK pages in PDF
#   - Need non-variable "Noto CJK" fonts.
#     * Fedora
#       - google-noto-sans-cjk-fonts
#       - google-noto-serif-cjk-fonts
#     * openSUSE tumbleweed
#       - Non-variable "Noto CJK" fonts are not available as distro packages
#         as of April, 2024.  Fetch a set of font files from upstream Noto
#         CJK Font released at:
#           https://github.com/notofonts/noto-cjk/tree/main/Sans#super-otc
#         and at:
#           https://github.com/notofonts/noto-cjk/tree/main/Serif#super-otc
#         , then uncompress and deploy them.
#       - Remember to update fontconfig cache by running fc-cache.
#
# !!! Caution !!!
#     Uninstalling "variable font" packages can be dangerous.
#     They might be depended upon by other packages important for your work.
#     Denylisting should be less invasive, as it is effective only while
#     XeLaTeX runs in "make pdfdocs".

# Default per-user fontconfig path (overridden by env variable)
: ${FONTS_CONF_DENY_VF:=$HOME/deny-vf}

export XDG_CONFIG_HOME=${FONTS_CONF_DENY_VF}

notocjkvffonts=`fc-list : file family variable | \
		grep 'variable=True' | \
		grep -E -e 'Noto (Sans|Sans Mono|Serif) CJK' | \
		sed -e 's/^/    /' -e 's/: Noto S.*$//' | sort | uniq`

if [ "x$notocjkvffonts" != "x" ] ; then
	echo '============================================================================='
	echo 'XeTeX is confused by "variable font" files listed below:'
	echo "$notocjkvffonts"
	echo
	echo 'For CJK pages in PDF, they need to be hidden from XeTeX by denylisting.'
	echo 'Or, CJK pages can be skipped by uninstalling texlive-xecjk.'
	echo
	echo 'For more info on denylisting, other options, and variable font, see header'
	echo 'comments of scripts/check-variable-fonts.sh.'
	echo '============================================================================='
fi

# As this script is invoked from Makefile's error path, always error exit
# regardless of whether any variable font is discovered or not.
exit 1
