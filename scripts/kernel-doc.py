#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.
#
# pylint: disable=C0103,R0915
#
# Converted from the kernel-doc script originally written in Perl
# under GPLv2, copyrighted since 1998 by the following authors:
#
#    Aditya Srivastava <yashsri421@gmail.com>
#    Akira Yokosawa <akiyks@gmail.com>
#    Alexander A. Klimov <grandmaster@al2klimov.de>
#    Alexander Lobakin <aleksander.lobakin@intel.com>
#    André Almeida <andrealmeid@igalia.com>
#    Andy Shevchenko <andriy.shevchenko@linux.intel.com>
#    Anna-Maria Behnsen <anna-maria@linutronix.de>
#    Armin Kuster <akuster@mvista.com>
#    Bart Van Assche <bart.vanassche@sandisk.com>
#    Ben Hutchings <ben@decadent.org.uk>
#    Borislav Petkov <bbpetkov@yahoo.de>
#    Chen-Yu Tsai <wenst@chromium.org>
#    Coco Li <lixiaoyan@google.com>
#    Conchúr Navid <conchur@web.de>
#    Daniel Santos <daniel.santos@pobox.com>
#    Danilo Cesar Lemes de Paula <danilo.cesar@collabora.co.uk>
#    Dan Luedtke <mail@danrl.de>
#    Donald Hunter <donald.hunter@gmail.com>
#    Gabriel Krisman Bertazi <krisman@collabora.co.uk>
#    Greg Kroah-Hartman <gregkh@linuxfoundation.org>
#    Harvey Harrison <harvey.harrison@gmail.com>
#    Horia Geanta <horia.geanta@freescale.com>
#    Ilya Dryomov <idryomov@gmail.com>
#    Jakub Kicinski <kuba@kernel.org>
#    Jani Nikula <jani.nikula@intel.com>
#    Jason Baron <jbaron@redhat.com>
#    Jason Gunthorpe <jgg@nvidia.com>
#    Jérémy Bobbio <lunar@debian.org>
#    Johannes Berg <johannes.berg@intel.com>
#    Johannes Weiner <hannes@cmpxchg.org>
#    Jonathan Cameron <Jonathan.Cameron@huawei.com>
#    Jonathan Corbet <corbet@lwn.net>
#    Jonathan Neuschäfer <j.neuschaefer@gmx.net>
#    Kamil Rytarowski <n54@gmx.com>
#    Kees Cook <kees@kernel.org>
#    Laurent Pinchart <laurent.pinchart@ideasonboard.com>
#    Levin, Alexander (Sasha Levin) <alexander.levin@verizon.com>
#    Linus Torvalds <torvalds@linux-foundation.org>
#    Lucas De Marchi <lucas.demarchi@profusion.mobi>
#    Mark Rutland <mark.rutland@arm.com>
#    Markus Heiser <markus.heiser@darmarit.de>
#    Martin Waitz <tali@admingilde.org>
#    Masahiro Yamada <masahiroy@kernel.org>
#    Matthew Wilcox <willy@infradead.org>
#    Mauro Carvalho Chehab <mchehab+huawei@kernel.org>
#    Michal Wajdeczko <michal.wajdeczko@intel.com>
#    Michael Zucchi
#    Mike Rapoport <rppt@linux.ibm.com>
#    Niklas Söderlund <niklas.soderlund@corigine.com>
#    Nishanth Menon <nm@ti.com>
#    Paolo Bonzini <pbonzini@redhat.com>
#    Pavan Kumar Linga <pavan.kumar.linga@intel.com>
#    Pavel Pisa <pisa@cmp.felk.cvut.cz>
#    Peter Maydell <peter.maydell@linaro.org>
#    Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>
#    Randy Dunlap <rdunlap@infradead.org>
#    Richard Kennedy <richard@rsk.demon.co.uk>
#    Rich Walker <rw@shadow.org.uk>
#    Rolf Eike Beer <eike-kernel@sf-tec.de>
#    Sakari Ailus <sakari.ailus@linux.intel.com>
#    Silvio Fricke <silvio.fricke@gmail.com>
#    Simon Huggins
#    Tim Waugh <twaugh@redhat.com>
#    Tomasz Warniełło <tomasz.warniello@gmail.com>
#    Utkarsh Tripathi <utripathi2002@gmail.com>
#    valdis.kletnieks@vt.edu <valdis.kletnieks@vt.edu>
#    Vegard Nossum <vegard.nossum@oracle.com>
#    Will Deacon <will.deacon@arm.com>
#    Yacine Belkadi <yacine.belkadi.1@gmail.com>
#    Yujie Liu <yujie.liu@intel.com>

"""
kernel_doc
==========

Print formatted kernel documentation to stdout

Read C language source or header FILEs, extract embedded
documentation comments, and print formatted documentation
to standard output.

The documentation comments are identified by the "/**"
opening comment mark.

See Documentation/doc-guide/kernel-doc.rst for the
documentation comment syntax.
"""

import argparse
import logging
import os
import sys

# Import Python modules

LIB_DIR = "lib/kdoc"
SRC_DIR = os.path.dirname(os.path.realpath(__file__))

sys.path.insert(0, os.path.join(SRC_DIR, LIB_DIR))

from kdoc_files import KernelFiles                      # pylint: disable=C0413
from kdoc_output import RestFormat, ManFormat           # pylint: disable=C0413

DESC = """
Read C language source or header FILEs, extract embedded documentation comments,
and print formatted documentation to standard output.

The documentation comments are identified by the "/**" opening comment mark.

See Documentation/doc-guide/kernel-doc.rst for the documentation comment syntax.
"""

EXPORT_FILE_DESC = """
Specify an additional FILE in which to look for EXPORT_SYMBOL information.

May be used multiple times.
"""

EXPORT_DESC = """
Only output documentation for the symbols that have been
exported using EXPORT_SYMBOL() and related macros in any input
FILE or -export-file FILE.
"""

INTERNAL_DESC = """
Only output documentation for the symbols that have NOT been
exported using EXPORT_SYMBOL() and related macros in any input
FILE or -export-file FILE.
"""

FUNCTION_DESC = """
Only output documentation for the given function or DOC: section
title. All other functions and DOC: sections are ignored.

May be used multiple times.
"""

NOSYMBOL_DESC = """
Exclude the specified symbol from the output documentation.

May be used multiple times.
"""

FILES_DESC = """
Header and C source files to be parsed.
"""

WARN_CONTENTS_BEFORE_SECTIONS_DESC = """
Warns if there are contents before sections (deprecated).

This option is kept just for backward-compatibility, but it does nothing,
neither here nor at the original Perl script.
"""


class MsgFormatter(logging.Formatter):
    """Helper class to format warnings on a similar way to kernel-doc.pl"""

    def format(self, record):
        record.levelname = record.levelname.capitalize()
        return logging.Formatter.format(self, record)

def main():
    """Main program"""

    parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter,
                                     description=DESC)

    # Normal arguments

    parser.add_argument("-v", "-verbose", "--verbose", action="store_true",
                        help="Verbose output, more warnings and other information.")

    parser.add_argument("-d", "-debug", "--debug", action="store_true",
                        help="Enable debug messages")

    parser.add_argument("-M", "-modulename", "--modulename",
                        default="Kernel API",
                        help="Allow setting a module name at the output.")

    parser.add_argument("-l", "-enable-lineno", "--enable_lineno",
                        action="store_true",
                        help="Enable line number output (only in ReST mode)")

    # Arguments to control the warning behavior

    parser.add_argument("-Wreturn", "--wreturn", action="store_true",
                        help="Warns about the lack of a return markup on functions.")

    parser.add_argument("-Wshort-desc", "-Wshort-description", "--wshort-desc",
                        action="store_true",
                        help="Warns if initial short description is missing")

    parser.add_argument("-Wcontents-before-sections",
                        "--wcontents-before-sections", action="store_true",
                        help=WARN_CONTENTS_BEFORE_SECTIONS_DESC)

    parser.add_argument("-Wall", "--wall", action="store_true",
                        help="Enable all types of warnings")

    parser.add_argument("-Werror", "--werror", action="store_true",
                        help="Treat warnings as errors.")

    parser.add_argument("-export-file", "--export-file", action='append',
                        help=EXPORT_FILE_DESC)

    # Output format mutually-exclusive group

    out_group = parser.add_argument_group("Output format selection (mutually exclusive)")

    out_fmt = out_group.add_mutually_exclusive_group()

    out_fmt.add_argument("-m", "-man", "--man", action="store_true",
                         help="Output troff manual page format.")
    out_fmt.add_argument("-r", "-rst", "--rst", action="store_true",
                         help="Output reStructuredText format (default).")
    out_fmt.add_argument("-N", "-none", "--none", action="store_true",
                         help="Do not output documentation, only warnings.")

    # Output selection mutually-exclusive group

    sel_group = parser.add_argument_group("Output selection (mutually exclusive)")
    sel_mut = sel_group.add_mutually_exclusive_group()

    sel_mut.add_argument("-e", "-export", "--export", action='store_true',
                         help=EXPORT_DESC)

    sel_mut.add_argument("-i", "-internal", "--internal", action='store_true',
                         help=INTERNAL_DESC)

    sel_mut.add_argument("-s", "-function", "--symbol", action='append',
                         help=FUNCTION_DESC)

    # Those are valid for all 3 types of filter
    parser.add_argument("-n", "-nosymbol", "--nosymbol", action='append',
                        help=NOSYMBOL_DESC)

    parser.add_argument("-D", "-no-doc-sections", "--no-doc-sections",
                        action='store_true', help="Don't outputt DOC sections")

    parser.add_argument("files", metavar="FILE",
                        nargs="+", help=FILES_DESC)

    args = parser.parse_args()

    if args.wall:
        args.wreturn = True
        args.wshort_desc = True
        args.wcontents_before_sections = True

    logger = logging.getLogger()

    if not args.debug:
        logger.setLevel(logging.INFO)
    else:
        logger.setLevel(logging.DEBUG)

    formatter = MsgFormatter('%(levelname)s: %(message)s')

    handler = logging.StreamHandler()
    handler.setFormatter(formatter)

    logger.addHandler(handler)

    python_ver = sys.version_info[:2]
    if python_ver < (3,6):
        logger.warning("Python 3.6 or later is required by kernel-doc")

        # Return 0 here to avoid breaking compilation
        sys.exit(0)

    if python_ver < (3,7):
        logger.warning("Python 3.7 or later is required for correct results")

    if args.man:
        out_style = ManFormat(modulename=args.modulename)
    elif args.none:
        out_style = None
    else:
        out_style = RestFormat()

    kfiles = KernelFiles(verbose=args.verbose,
                         out_style=out_style, werror=args.werror,
                         wreturn=args.wreturn, wshort_desc=args.wshort_desc,
                         wcontents_before_sections=args.wcontents_before_sections)

    kfiles.parse(args.files, export_file=args.export_file)

    for t in kfiles.msg(enable_lineno=args.enable_lineno, export=args.export,
                        internal=args.internal, symbol=args.symbol,
                        nosymbol=args.nosymbol, export_file=args.export_file,
                        no_doc_sections=args.no_doc_sections):
        msg = t[1]
        if msg:
            print(msg)

    error_count = kfiles.errors
    if not error_count:
        sys.exit(0)

    if args.werror:
        print(f"{error_count} warnings as errors")
        sys.exit(error_count)

    if args.verbose:
        print(f"{error_count} errors")

    if args.none:
        sys.exit(0)

    sys.exit(error_count)


# Call main method
if __name__ == "__main__":
    main()
