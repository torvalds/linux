#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.
#
# pylint: disable=C0301,C0302,R0904,R0912,R0913,R0914,R0915,R0917,R1702

"""
kdoc_parser
===========

Read a C language source or header FILE and extract embedded
documentation comments
"""

import sys
import re
from pprint import pformat

from kdoc_re import NestedMatch, KernRe
from kdoc_item import KdocItem

#
# Regular expressions used to parse kernel-doc markups at KernelDoc class.
#
# Let's declare them in lowercase outside any class to make easier to
# convert from the python script.
#
# As those are evaluated at the beginning, no need to cache them
#

# Allow whitespace at end of comment start.
doc_start = KernRe(r'^/\*\*\s*$', cache=False)

doc_end = KernRe(r'\*/', cache=False)
doc_com = KernRe(r'\s*\*\s*', cache=False)
doc_com_body = KernRe(r'\s*\* ?', cache=False)
doc_decl = doc_com + KernRe(r'(\w+)', cache=False)

# @params and a strictly limited set of supported section names
# Specifically:
#   Match @word:
#         @...:
#         @{section-name}:
# while trying to not match literal block starts like "example::"
#
known_section_names = 'description|context|returns?|notes?|examples?'
known_sections = KernRe(known_section_names, flags = re.I)
doc_sect = doc_com + \
    KernRe(r'\s*(@[.\w]+|@\.\.\.|' + known_section_names + r')\s*:([^:].*)?$',
           flags=re.I, cache=False)

doc_content = doc_com_body + KernRe(r'(.*)', cache=False)
doc_inline_start = KernRe(r'^\s*/\*\*\s*$', cache=False)
doc_inline_sect = KernRe(r'\s*\*\s*(@\s*[\w][\w\.]*\s*):(.*)', cache=False)
doc_inline_end = KernRe(r'^\s*\*/\s*$', cache=False)
doc_inline_oneline = KernRe(r'^\s*/\*\*\s*(@[\w\s]+):\s*(.*)\s*\*/\s*$', cache=False)

export_symbol = KernRe(r'^\s*EXPORT_SYMBOL(_GPL)?\s*\(\s*(\w+)\s*\)\s*', cache=False)
export_symbol_ns = KernRe(r'^\s*EXPORT_SYMBOL_NS(_GPL)?\s*\(\s*(\w+)\s*,\s*"\S+"\)\s*', cache=False)

type_param = KernRe(r"@(\w*((\.\w+)|(->\w+))*(\.\.\.)?)", cache=False)

#
# Tests for the beginning of a kerneldoc block in its various forms.
#
doc_block = doc_com + KernRe(r'DOC:\s*(.*)?', cache=False)
doc_begin_data = KernRe(r"^\s*\*?\s*(struct|union|enum|typedef)\b\s*(\w*)", cache = False)
doc_begin_func = KernRe(str(doc_com) +			# initial " * '
                        r"(?:\w+\s*\*\s*)?" + 		# type (not captured)
                        r'(?:define\s+)?' + 		# possible "define" (not captured)
                        r'(\w+)\s*(?:\(\w*\))?\s*' +	# name and optional "(...)"
                        r'(?:[-:].*)?$',		# description (not captured)
                        cache = False)

#
# Here begins a long set of transformations to turn structure member prefixes
# and macro invocations into something we can parse and generate kdoc for.
#
struct_args_pattern = r'([^,)]+)'

struct_xforms = [
    # Strip attributes
    (KernRe(r"__attribute__\s*\(\([a-z0-9,_\*\s\(\)]*\)\)", flags=re.I | re.S, cache=False), ' '),
    (KernRe(r'\s*__aligned\s*\([^;]*\)', re.S), ' '),
    (KernRe(r'\s*__counted_by\s*\([^;]*\)', re.S), ' '),
    (KernRe(r'\s*__counted_by_(le|be)\s*\([^;]*\)', re.S), ' '),
    (KernRe(r'\s*__packed\s*', re.S), ' '),
    (KernRe(r'\s*CRYPTO_MINALIGN_ATTR', re.S), ' '),
    (KernRe(r'\s*____cacheline_aligned_in_smp', re.S), ' '),
    (KernRe(r'\s*____cacheline_aligned', re.S), ' '),
    (KernRe(r'\s*__cacheline_group_(begin|end)\([^\)]+\);'), ''),
    #
    # Unwrap struct_group macros based on this definition:
    # __struct_group(TAG, NAME, ATTRS, MEMBERS...)
    # which has variants like: struct_group(NAME, MEMBERS...)
    # Only MEMBERS arguments require documentation.
    #
    # Parsing them happens on two steps:
    #
    # 1. drop struct group arguments that aren't at MEMBERS,
    #    storing them as STRUCT_GROUP(MEMBERS)
    #
    # 2. remove STRUCT_GROUP() ancillary macro.
    #
    # The original logic used to remove STRUCT_GROUP() using an
    # advanced regex:
    #
    #   \bSTRUCT_GROUP(\(((?:(?>[^)(]+)|(?1))*)\))[^;]*;
    #
    # with two patterns that are incompatible with
    # Python re module, as it has:
    #
    #   - a recursive pattern: (?1)
    #   - an atomic grouping: (?>...)
    #
    # I tried a simpler version: but it didn't work either:
    #   \bSTRUCT_GROUP\(([^\)]+)\)[^;]*;
    #
    # As it doesn't properly match the end parenthesis on some cases.
    #
    # So, a better solution was crafted: there's now a NestedMatch
    # class that ensures that delimiters after a search are properly
    # matched. So, the implementation to drop STRUCT_GROUP() will be
    # handled in separate.
    #
    (KernRe(r'\bstruct_group\s*\(([^,]*,)', re.S), r'STRUCT_GROUP('),
    (KernRe(r'\bstruct_group_attr\s*\(([^,]*,){2}', re.S), r'STRUCT_GROUP('),
    (KernRe(r'\bstruct_group_tagged\s*\(([^,]*),([^,]*),', re.S), r'struct \1 \2; STRUCT_GROUP('),
    (KernRe(r'\b__struct_group\s*\(([^,]*,){3}', re.S), r'STRUCT_GROUP('),
    #
    # Replace macros
    #
    # TODO: use NestedMatch for FOO($1, $2, ...) matches
    #
    # it is better to also move those to the NestedMatch logic,
    # to ensure that parenthesis will be properly matched.
    #
    (KernRe(r'__ETHTOOL_DECLARE_LINK_MODE_MASK\s*\(([^\)]+)\)', re.S),
     r'DECLARE_BITMAP(\1, __ETHTOOL_LINK_MODE_MASK_NBITS)'),
    (KernRe(r'DECLARE_PHY_INTERFACE_MASK\s*\(([^\)]+)\)', re.S),
     r'DECLARE_BITMAP(\1, PHY_INTERFACE_MODE_MAX)'),
    (KernRe(r'DECLARE_BITMAP\s*\(' + struct_args_pattern + r',\s*' + struct_args_pattern + r'\)',
            re.S), r'unsigned long \1[BITS_TO_LONGS(\2)]'),
    (KernRe(r'DECLARE_HASHTABLE\s*\(' + struct_args_pattern + r',\s*' + struct_args_pattern + r'\)',
            re.S), r'unsigned long \1[1 << ((\2) - 1)]'),
    (KernRe(r'DECLARE_KFIFO\s*\(' + struct_args_pattern + r',\s*' + struct_args_pattern +
            r',\s*' + struct_args_pattern + r'\)', re.S), r'\2 *\1'),
    (KernRe(r'DECLARE_KFIFO_PTR\s*\(' + struct_args_pattern + r',\s*' +
            struct_args_pattern + r'\)', re.S), r'\2 *\1'),
    (KernRe(r'(?:__)?DECLARE_FLEX_ARRAY\s*\(' + struct_args_pattern + r',\s*' +
            struct_args_pattern + r'\)', re.S), r'\1 \2[]'),
    (KernRe(r'DEFINE_DMA_UNMAP_ADDR\s*\(' + struct_args_pattern + r'\)', re.S), r'dma_addr_t \1'),
    (KernRe(r'DEFINE_DMA_UNMAP_LEN\s*\(' + struct_args_pattern + r'\)', re.S), r'__u32 \1'),
]
#
# Regexes here are guaranteed to have the end limiter matching
# the start delimiter. Yet, right now, only one replace group
# is allowed.
#
struct_nested_prefixes = [
    (re.compile(r'\bSTRUCT_GROUP\('), r'\1'),
]

#
# Transforms for function prototypes
#
function_xforms  = [
    (KernRe(r"^static +"), ""),
    (KernRe(r"^extern +"), ""),
    (KernRe(r"^asmlinkage +"), ""),
    (KernRe(r"^inline +"), ""),
    (KernRe(r"^__inline__ +"), ""),
    (KernRe(r"^__inline +"), ""),
    (KernRe(r"^__always_inline +"), ""),
    (KernRe(r"^noinline +"), ""),
    (KernRe(r"^__FORTIFY_INLINE +"), ""),
    (KernRe(r"__init +"), ""),
    (KernRe(r"__init_or_module +"), ""),
    (KernRe(r"__deprecated +"), ""),
    (KernRe(r"__flatten +"), ""),
    (KernRe(r"__meminit +"), ""),
    (KernRe(r"__must_check +"), ""),
    (KernRe(r"__weak +"), ""),
    (KernRe(r"__sched +"), ""),
    (KernRe(r"_noprof"), ""),
    (KernRe(r"__printf\s*\(\s*\d*\s*,\s*\d*\s*\) +"), ""),
    (KernRe(r"__(?:re)?alloc_size\s*\(\s*\d+\s*(?:,\s*\d+\s*)?\) +"), ""),
    (KernRe(r"__diagnose_as\s*\(\s*\S+\s*(?:,\s*\d+\s*)*\) +"), ""),
    (KernRe(r"DECL_BUCKET_PARAMS\s*\(\s*(\S+)\s*,\s*(\S+)\s*\)"), r"\1, \2"),
    (KernRe(r"__attribute_const__ +"), ""),
    (KernRe(r"__attribute__\s*\(\((?:[\w\s]+(?:\([^)]*\))?\s*,?)+\)\)\s+"), ""),
]

#
# Apply a set of transforms to a block of text.
#
def apply_transforms(xforms, text):
    for search, subst in xforms:
        text = search.sub(subst, text)
    return text

#
# A little helper to get rid of excess white space
#
multi_space = KernRe(r'\s\s+')
def trim_whitespace(s):
    return multi_space.sub(' ', s.strip())

#
# Remove struct/enum members that have been marked "private".
#
def trim_private_members(text):
    #
    # First look for a "public:" block that ends a private region, then
    # handle the "private until the end" case.
    #
    text = KernRe(r'/\*\s*private:.*?/\*\s*public:.*?\*/', flags=re.S).sub('', text)
    text = KernRe(r'/\*\s*private:.*', flags=re.S).sub('', text)
    #
    # We needed the comments to do the above, but now we can take them out.
    #
    return KernRe(r'\s*/\*.*?\*/\s*', flags=re.S).sub('', text).strip()

class state:
    """
    State machine enums
    """

    # Parser states
    NORMAL        = 0        # normal code
    NAME          = 1        # looking for function name
    DECLARATION   = 2        # We have seen a declaration which might not be done
    BODY          = 3        # the body of the comment
    SPECIAL_SECTION = 4      # doc section ending with a blank line
    PROTO         = 5        # scanning prototype
    DOCBLOCK      = 6        # documentation block
    INLINE_NAME   = 7        # gathering doc outside main block
    INLINE_TEXT   = 8	     # reading the body of inline docs

    name = [
        "NORMAL",
        "NAME",
        "DECLARATION",
        "BODY",
        "SPECIAL_SECTION",
        "PROTO",
        "DOCBLOCK",
        "INLINE_NAME",
        "INLINE_TEXT",
    ]


SECTION_DEFAULT = "Description"  # default section

class KernelEntry:

    def __init__(self, config, ln):
        self.config = config

        self._contents = []
        self.prototype = ""

        self.warnings = []

        self.parameterlist = []
        self.parameterdescs = {}
        self.parametertypes = {}
        self.parameterdesc_start_lines = {}

        self.section_start_lines = {}
        self.sections = {}

        self.anon_struct_union = False

        self.leading_space = None

        # State flags
        self.brcount = 0
        self.declaration_start_line = ln + 1

    #
    # Management of section contents
    #
    def add_text(self, text):
        self._contents.append(text)

    def contents(self):
        return '\n'.join(self._contents) + '\n'

    # TODO: rename to emit_message after removal of kernel-doc.pl
    def emit_msg(self, log_msg, warning=True):
        """Emit a message"""

        if not warning:
            self.config.log.info(log_msg)
            return

        # Delegate warning output to output logic, as this way it
        # will report warnings/info only for symbols that are output

        self.warnings.append(log_msg)
        return

    #
    # Begin a new section.
    #
    def begin_section(self, line_no, title = SECTION_DEFAULT, dump = False):
        if dump:
            self.dump_section(start_new = True)
        self.section = title
        self.new_start_line = line_no

    def dump_section(self, start_new=True):
        """
        Dumps section contents to arrays/hashes intended for that purpose.
        """
        #
        # If we have accumulated no contents in the default ("description")
        # section, don't bother.
        #
        if self.section == SECTION_DEFAULT and not self._contents:
            return
        name = self.section
        contents = self.contents()

        if type_param.match(name):
            name = type_param.group(1)

            self.parameterdescs[name] = contents
            self.parameterdesc_start_lines[name] = self.new_start_line

            self.new_start_line = 0

        else:
            if name in self.sections and self.sections[name] != "":
                # Only warn on user-specified duplicate section names
                if name != SECTION_DEFAULT:
                    self.emit_msg(self.new_start_line,
                                  f"duplicate section name '{name}'\n")
                # Treat as a new paragraph - add a blank line
                self.sections[name] += '\n' + contents
            else:
                self.sections[name] = contents
                self.section_start_lines[name] = self.new_start_line
                self.new_start_line = 0

#        self.config.log.debug("Section: %s : %s", name, pformat(vars(self)))

        if start_new:
            self.section = SECTION_DEFAULT
            self._contents = []


class KernelDoc:
    """
    Read a C language source or header FILE and extract embedded
    documentation comments.
    """

    # Section names

    section_context = "Context"
    section_return = "Return"

    undescribed = "-- undescribed --"

    def __init__(self, config, fname):
        """Initialize internal variables"""

        self.fname = fname
        self.config = config

        # Initial state for the state machines
        self.state = state.NORMAL

        # Store entry currently being processed
        self.entry = None

        # Place all potential outputs into an array
        self.entries = []

        #
        # We need Python 3.7 for its "dicts remember the insertion
        # order" guarantee
        #
        if sys.version_info.major == 3 and sys.version_info.minor < 7:
            self.emit_msg(0,
                          'Python 3.7 or later is required for correct results')

    def emit_msg(self, ln, msg, warning=True):
        """Emit a message"""

        log_msg = f"{self.fname}:{ln} {msg}"

        if self.entry:
            self.entry.emit_msg(log_msg, warning)
            return

        if warning:
            self.config.log.warning(log_msg)
        else:
            self.config.log.info(log_msg)

    def dump_section(self, start_new=True):
        """
        Dumps section contents to arrays/hashes intended for that purpose.
        """

        if self.entry:
            self.entry.dump_section(start_new)

    # TODO: rename it to store_declaration after removal of kernel-doc.pl
    def output_declaration(self, dtype, name, **args):
        """
        Stores the entry into an entry array.

        The actual output and output filters will be handled elsewhere
        """

        item = KdocItem(name, dtype, self.entry.declaration_start_line, **args)
        item.warnings = self.entry.warnings

        # Drop empty sections
        # TODO: improve empty sections logic to emit warnings
        sections = self.entry.sections
        for section in ["Description", "Return"]:
            if section in sections and not sections[section].rstrip():
                del sections[section]
        item.set_sections(sections, self.entry.section_start_lines)
        item.set_params(self.entry.parameterlist, self.entry.parameterdescs,
                        self.entry.parametertypes,
                        self.entry.parameterdesc_start_lines)
        self.entries.append(item)

        self.config.log.debug("Output: %s:%s = %s", dtype, name, pformat(args))

    def reset_state(self, ln):
        """
        Ancillary routine to create a new entry. It initializes all
        variables used by the state machine.
        """

        self.entry = KernelEntry(self.config, ln)

        # State flags
        self.state = state.NORMAL

    def push_parameter(self, ln, decl_type, param, dtype,
                       org_arg, declaration_name):
        """
        Store parameters and their descriptions at self.entry.
        """

        if self.entry.anon_struct_union and dtype == "" and param == "}":
            return  # Ignore the ending }; from anonymous struct/union

        self.entry.anon_struct_union = False

        param = KernRe(r'[\[\)].*').sub('', param, count=1)

        #
        # Look at various "anonymous type" cases.
        #
        if dtype == '':
            if param.endswith("..."):
                if len(param) > 3: # there is a name provided, use that
                    param = param[:-3]
                if not self.entry.parameterdescs.get(param):
                    self.entry.parameterdescs[param] = "variable arguments"

            elif (not param) or param == "void":
                param = "void"
                self.entry.parameterdescs[param] = "no arguments"

            elif param in ["struct", "union"]:
                # Handle unnamed (anonymous) union or struct
                dtype = param
                param = "{unnamed_" + param + "}"
                self.entry.parameterdescs[param] = "anonymous\n"
                self.entry.anon_struct_union = True

        # Warn if parameter has no description
        # (but ignore ones starting with # as these are not parameters
        # but inline preprocessor statements)
        if param not in self.entry.parameterdescs and not param.startswith("#"):
            self.entry.parameterdescs[param] = self.undescribed

            if "." not in param:
                if decl_type == 'function':
                    dname = f"{decl_type} parameter"
                else:
                    dname = f"{decl_type} member"

                self.emit_msg(ln,
                              f"{dname} '{param}' not described in '{declaration_name}'")

        # Strip spaces from param so that it is one continuous string on
        # parameterlist. This fixes a problem where check_sections()
        # cannot find a parameter like "addr[6 + 2]" because it actually
        # appears as "addr[6", "+", "2]" on the parameter list.
        # However, it's better to maintain the param string unchanged for
        # output, so just weaken the string compare in check_sections()
        # to ignore "[blah" in a parameter string.

        self.entry.parameterlist.append(param)
        org_arg = KernRe(r'\s\s+').sub(' ', org_arg)
        self.entry.parametertypes[param] = org_arg


    def create_parameter_list(self, ln, decl_type, args,
                              splitter, declaration_name):
        """
        Creates a list of parameters, storing them at self.entry.
        """

        # temporarily replace all commas inside function pointer definition
        arg_expr = KernRe(r'(\([^\),]+),')
        while arg_expr.search(args):
            args = arg_expr.sub(r"\1#", args)

        for arg in args.split(splitter):
            # Ignore argument attributes
            arg = KernRe(r'\sPOS0?\s').sub(' ', arg)

            # Strip leading/trailing spaces
            arg = arg.strip()
            arg = KernRe(r'\s+').sub(' ', arg, count=1)

            if arg.startswith('#'):
                # Treat preprocessor directive as a typeless variable just to fill
                # corresponding data structures "correctly". Catch it later in
                # output_* subs.

                # Treat preprocessor directive as a typeless variable
                self.push_parameter(ln, decl_type, arg, "",
                                    "", declaration_name)
            #
            # The pointer-to-function case.
            #
            elif KernRe(r'\(.+\)\s*\(').search(arg):
                arg = arg.replace('#', ',')
                r = KernRe(r'[^\(]+\(\*?\s*'  # Everything up to "(*"
                           r'([\w\[\].]*)'    # Capture the name and possible [array]
                           r'\s*\)')	      # Make sure the trailing ")" is there
                if r.match(arg):
                    param = r.group(1)
                else:
                    self.emit_msg(ln, f"Invalid param: {arg}")
                    param = arg
                dtype = arg.replace(param, '')
                self.push_parameter(ln, decl_type, param, dtype, arg, declaration_name)
            #
            # The array-of-pointers case.  Dig the parameter name out from the middle
            # of the declaration.
            #
            elif KernRe(r'\(.+\)\s*\[').search(arg):
                r = KernRe(r'[^\(]+\(\s*\*\s*'		# Up to "(" and maybe "*"
                           r'([\w.]*?)'			# The actual pointer name
                           r'\s*(\[\s*\w+\s*\]\s*)*\)') # The [array portion]
                if r.match(arg):
                    param = r.group(1)
                else:
                    self.emit_msg(ln, f"Invalid param: {arg}")
                    param = arg
                dtype = arg.replace(param, '')
                self.push_parameter(ln, decl_type, param, dtype, arg, declaration_name)
            elif arg:
                #
                # Clean up extraneous spaces and split the string at commas; the first
                # element of the resulting list will also include the type information.
                #
                arg = KernRe(r'\s*:\s*').sub(":", arg)
                arg = KernRe(r'\s*\[').sub('[', arg)
                args = KernRe(r'\s*,\s*').split(arg)
                args[0] = re.sub(r'(\*+)\s*', r' \1', args[0])
                #
                # args[0] has a string of "type a".  If "a" includes an [array]
                # declaration, we want to not be fooled by any white space inside
                # the brackets, so detect and handle that case specially.
                #
                r = KernRe(r'^([^[\]]*\s+)(.*)$')
                if r.match(args[0]):
                    args[0] = r.group(2)
                    dtype = r.group(1)
                else:
                    # No space in args[0]; this seems wrong but preserves previous behavior
                    dtype = ''

                bitfield_re = KernRe(r'(.*?):(\w+)')
                for param in args:
                    #
                    # For pointers, shift the star(s) from the variable name to the
                    # type declaration.
                    #
                    r = KernRe(r'^(\*+)\s*(.*)')
                    if r.match(param):
                        self.push_parameter(ln, decl_type, r.group(2),
                                            f"{dtype} {r.group(1)}",
                                            arg, declaration_name)
                    #
                    # Perform a similar shift for bitfields.
                    #
                    elif bitfield_re.search(param):
                        if dtype != "":  # Skip unnamed bit-fields
                            self.push_parameter(ln, decl_type, bitfield_re.group(1),
                                                f"{dtype}:{bitfield_re.group(2)}",
                                                arg, declaration_name)
                    else:
                        self.push_parameter(ln, decl_type, param, dtype,
                                            arg, declaration_name)

    def check_sections(self, ln, decl_name, decl_type):
        """
        Check for errors inside sections, emitting warnings if not found
        parameters are described.
        """
        for section in self.entry.sections:
            if section not in self.entry.parameterlist and \
               not known_sections.search(section):
                if decl_type == 'function':
                    dname = f"{decl_type} parameter"
                else:
                    dname = f"{decl_type} member"
                self.emit_msg(ln,
                              f"Excess {dname} '{section}' description in '{decl_name}'")

    def check_return_section(self, ln, declaration_name, return_type):
        """
        If the function doesn't return void, warns about the lack of a
        return description.
        """

        if not self.config.wreturn:
            return

        # Ignore an empty return type (It's a macro)
        # Ignore functions with a "void" return type (but not "void *")
        if not return_type or KernRe(r'void\s*\w*\s*$').search(return_type):
            return

        if not self.entry.sections.get("Return", None):
            self.emit_msg(ln,
                          f"No description found for return value of '{declaration_name}'")

    #
    # Split apart a structure prototype; returns (struct|union, name, members) or None
    #
    def split_struct_proto(self, proto):
        type_pattern = r'(struct|union)'
        qualifiers = [
            "__attribute__",
            "__packed",
            "__aligned",
            "____cacheline_aligned_in_smp",
            "____cacheline_aligned",
        ]
        definition_body = r'\{(.*)\}\s*' + "(?:" + '|'.join(qualifiers) + ")?"

        r = KernRe(type_pattern + r'\s+(\w+)\s*' + definition_body)
        if r.search(proto):
            return (r.group(1), r.group(2), r.group(3))
        else:
            r = KernRe(r'typedef\s+' + type_pattern + r'\s*' + definition_body + r'\s*(\w+)\s*;')
            if r.search(proto):
                return (r.group(1), r.group(3), r.group(2))
        return None
    #
    # Rewrite the members of a structure or union for easier formatting later on.
    # Among other things, this function will turn a member like:
    #
    #  struct { inner_members; } foo;
    #
    # into:
    #
    #  struct foo; inner_members;
    #
    def rewrite_struct_members(self, members):
        #
        # Process struct/union members from the most deeply nested outward.  The
        # trick is in the ^{ below - it prevents a match of an outer struct/union
        # until the inner one has been munged (removing the "{" in the process).
        #
        struct_members = KernRe(r'(struct|union)'   # 0: declaration type
                                r'([^\{\};]+)' 	    # 1: possible name
                                r'(\{)'
                                r'([^\{\}]*)'       # 3: Contents of declaration
                                r'(\})'
                                r'([^\{\};]*)(;)')  # 5: Remaining stuff after declaration
        tuples = struct_members.findall(members)
        while tuples:
            for t in tuples:
                newmember = ""
                oldmember = "".join(t) # Reconstruct the original formatting
                dtype, name, lbr, content, rbr, rest, semi = t
                #
                # Pass through each field name, normalizing the form and formatting.
                #
                for s_id in rest.split(','):
                    s_id = s_id.strip()
                    newmember += f"{dtype} {s_id}; "
                    #
                    # Remove bitfield/array/pointer info, getting the bare name.
                    #
                    s_id = KernRe(r'[:\[].*').sub('', s_id)
                    s_id = KernRe(r'^\s*\**(\S+)\s*').sub(r'\1', s_id)
                    #
                    # Pass through the members of this inner structure/union.
                    #
                    for arg in content.split(';'):
                        arg = arg.strip()
                        #
                        # Look for (type)(*name)(args) - pointer to function
                        #
                        r = KernRe(r'^([^\(]+\(\*?\s*)([\w.]*)(\s*\).*)')
                        if r.match(arg):
                            dtype, name, extra = r.group(1), r.group(2), r.group(3)
                            # Pointer-to-function
                            if not s_id:
                                # Anonymous struct/union
                                newmember += f"{dtype}{name}{extra}; "
                            else:
                                newmember += f"{dtype}{s_id}.{name}{extra}; "
                        #
                        # Otherwise a non-function member.
                        #
                        else:
                            #
                            # Remove bitmap and array portions and spaces around commas
                            #
                            arg = KernRe(r':\s*\d+\s*').sub('', arg)
                            arg = KernRe(r'\[.*\]').sub('', arg)
                            arg = KernRe(r'\s*,\s*').sub(',', arg)
                            #
                            # Look for a normal decl - "type name[,name...]"
                            #
                            r = KernRe(r'(.*)\s+([\S+,]+)')
                            if r.search(arg):
                                for name in r.group(2).split(','):
                                    name = KernRe(r'^\s*\**(\S+)\s*').sub(r'\1', name)
                                    if not s_id:
                                        # Anonymous struct/union
                                        newmember += f"{r.group(1)} {name}; "
                                    else:
                                        newmember += f"{r.group(1)} {s_id}.{name}; "
                            else:
                                newmember += f"{arg}; "
                #
                # At the end of the s_id loop, replace the original declaration with
                # the munged version.
                #
                members = members.replace(oldmember, newmember)
            #
            # End of the tuple loop - search again and see if there are outer members
            # that now turn up.
            #
            tuples = struct_members.findall(members)
        return members

    #
    # Format the struct declaration into a standard form for inclusion in the
    # resulting docs.
    #
    def format_struct_decl(self, declaration):
        #
        # Insert newlines, get rid of extra spaces.
        #
        declaration = KernRe(r'([\{;])').sub(r'\1\n', declaration)
        declaration = KernRe(r'\}\s+;').sub('};', declaration)
        #
        # Format inline enums with each member on its own line.
        #
        r = KernRe(r'(enum\s+\{[^\}]+),([^\n])')
        while r.search(declaration):
            declaration = r.sub(r'\1,\n\2', declaration)
        #
        # Now go through and supply the right number of tabs
        # for each line.
        #
        def_args = declaration.split('\n')
        level = 1
        declaration = ""
        for clause in def_args:
            clause = KernRe(r'\s+').sub(' ', clause.strip(), count=1)
            if clause:
                if '}' in clause and level > 1:
                    level -= 1
                if not clause.startswith('#'):
                    declaration += "\t" * level
                declaration += "\t" + clause + "\n"
                if "{" in clause and "}" not in clause:
                    level += 1
        return declaration


    def dump_struct(self, ln, proto):
        """
        Store an entry for an struct or union
        """
        #
        # Do the basic parse to get the pieces of the declaration.
        #
        struct_parts = self.split_struct_proto(proto)
        if not struct_parts:
            self.emit_msg(ln, f"{proto} error: Cannot parse struct or union!")
            return
        decl_type, declaration_name, members = struct_parts

        if self.entry.identifier != declaration_name:
            self.emit_msg(ln, f"expecting prototype for {decl_type} {self.entry.identifier}. "
                          f"Prototype was for {decl_type} {declaration_name} instead\n")
            return
        #
        # Go through the list of members applying all of our transformations.
        #
        members = trim_private_members(members)
        members = apply_transforms(struct_xforms, members)

        nested = NestedMatch()
        for search, sub in struct_nested_prefixes:
            members = nested.sub(search, sub, members)
        #
        # Deal with embedded struct and union members, and drop enums entirely.
        #
        declaration = members
        members = self.rewrite_struct_members(members)
        members = re.sub(r'(\{[^\{\}]*\})', '', members)
        #
        # Output the result and we are done.
        #
        self.create_parameter_list(ln, decl_type, members, ';',
                                   declaration_name)
        self.check_sections(ln, declaration_name, decl_type)
        self.output_declaration(decl_type, declaration_name,
                                definition=self.format_struct_decl(declaration),
                                purpose=self.entry.declaration_purpose)

    def dump_enum(self, ln, proto):
        """
        Stores an enum inside self.entries array.
        """
        #
        # Strip preprocessor directives.  Note that this depends on the
        # trailing semicolon we added in process_proto_type().
        #
        proto = KernRe(r'#\s*((define|ifdef|if)\s+|endif)[^;]*;', flags=re.S).sub('', proto)
        #
        # Parse out the name and members of the enum.  Typedef form first.
        #
        r = KernRe(r'typedef\s+enum\s*\{(.*)\}\s*(\w*)\s*;')
        if r.search(proto):
            declaration_name = r.group(2)
            members = trim_private_members(r.group(1))
        #
        # Failing that, look for a straight enum
        #
        else:
            r = KernRe(r'enum\s+(\w*)\s*\{(.*)\}')
            if r.match(proto):
                declaration_name = r.group(1)
                members = trim_private_members(r.group(2))
        #
        # OK, this isn't going to work.
        #
            else:
                self.emit_msg(ln, f"{proto}: error: Cannot parse enum!")
                return
        #
        # Make sure we found what we were expecting.
        #
        if self.entry.identifier != declaration_name:
            if self.entry.identifier == "":
                self.emit_msg(ln,
                              f"{proto}: wrong kernel-doc identifier on prototype")
            else:
                self.emit_msg(ln,
                              f"expecting prototype for enum {self.entry.identifier}. "
                              f"Prototype was for enum {declaration_name} instead")
            return

        if not declaration_name:
            declaration_name = "(anonymous)"
        #
        # Parse out the name of each enum member, and verify that we
        # have a description for it.
        #
        member_set = set()
        members = KernRe(r'\([^;)]*\)').sub('', members)
        for arg in members.split(','):
            if not arg:
                continue
            arg = KernRe(r'^\s*(\w+).*').sub(r'\1', arg)
            self.entry.parameterlist.append(arg)
            if arg not in self.entry.parameterdescs:
                self.entry.parameterdescs[arg] = self.undescribed
                self.emit_msg(ln,
                              f"Enum value '{arg}' not described in enum '{declaration_name}'")
            member_set.add(arg)
        #
        # Ensure that every described member actually exists in the enum.
        #
        for k in self.entry.parameterdescs:
            if k not in member_set:
                self.emit_msg(ln,
                              f"Excess enum value '%{k}' description in '{declaration_name}'")

        self.output_declaration('enum', declaration_name,
                                purpose=self.entry.declaration_purpose)

    def dump_declaration(self, ln, prototype):
        """
        Stores a data declaration inside self.entries array.
        """

        if self.entry.decl_type == "enum":
            self.dump_enum(ln, prototype)
        elif self.entry.decl_type == "typedef":
            self.dump_typedef(ln, prototype)
        elif self.entry.decl_type in ["union", "struct"]:
            self.dump_struct(ln, prototype)
        else:
            # This would be a bug
            self.emit_message(ln, f'Unknown declaration type: {self.entry.decl_type}')

    def dump_function(self, ln, prototype):
        """
        Stores a function of function macro inside self.entries array.
        """

        found = func_macro = False
        return_type = ''
        decl_type = 'function'
        #
        # Apply the initial transformations.
        #
        prototype = apply_transforms(function_xforms, prototype)
        #
        # If we have a macro, remove the "#define" at the front.
        #
        new_proto = KernRe(r"^#\s*define\s+").sub("", prototype)
        if new_proto != prototype:
            prototype = new_proto
            #
            # Dispense with the simple "#define A B" case here; the key
            # is the space after the name of the symbol being defined.
            # NOTE that the seemingly misnamed "func_macro" indicates a
            # macro *without* arguments.
            #
            r = KernRe(r'^(\w+)\s+')
            if r.search(prototype):
                return_type = ''
                declaration_name = r.group(1)
                func_macro = True
                found = True

        # Yes, this truly is vile.  We are looking for:
        # 1. Return type (may be nothing if we're looking at a macro)
        # 2. Function name
        # 3. Function parameters.
        #
        # All the while we have to watch out for function pointer parameters
        # (which IIRC is what the two sections are for), C types (these
        # regexps don't even start to express all the possibilities), and
        # so on.
        #
        # If you mess with these regexps, it's a good idea to check that
        # the following functions' documentation still comes out right:
        # - parport_register_device (function pointer parameters)
        # - atomic_set (macro)
        # - pci_match_device, __copy_to_user (long return type)

        name = r'\w+'
        type1 = r'(?:[\w\s]+)?'
        type2 = r'(?:[\w\s]+\*+)+'
        #
        # Attempt to match first on (args) with no internal parentheses; this
        # lets us easily filter out __acquires() and other post-args stuff.  If
        # that fails, just grab the rest of the line to the last closing
        # parenthesis.
        #
        proto_args = r'\(([^\(]*|.*)\)'
        #
        # (Except for the simple macro case) attempt to split up the prototype
        # in the various ways we understand.
        #
        if not found:
            patterns = [
                rf'^()({name})\s*{proto_args}',
                rf'^({type1})\s+({name})\s*{proto_args}',
                rf'^({type2})\s*({name})\s*{proto_args}',
            ]

            for p in patterns:
                r = KernRe(p)
                if r.match(prototype):
                    return_type = r.group(1)
                    declaration_name = r.group(2)
                    args = r.group(3)
                    self.create_parameter_list(ln, decl_type, args, ',',
                                               declaration_name)
                    found = True
                    break
        #
        # Parsing done; make sure that things are as we expect.
        #
        if not found:
            self.emit_msg(ln,
                          f"cannot understand function prototype: '{prototype}'")
            return
        if self.entry.identifier != declaration_name:
            self.emit_msg(ln, f"expecting prototype for {self.entry.identifier}(). "
                          f"Prototype was for {declaration_name}() instead")
            return
        self.check_sections(ln, declaration_name, "function")
        self.check_return_section(ln, declaration_name, return_type)
        #
        # Store the result.
        #
        self.output_declaration(decl_type, declaration_name,
                                typedef=('typedef' in return_type),
                                functiontype=return_type,
                                purpose=self.entry.declaration_purpose,
                                func_macro=func_macro)


    def dump_typedef(self, ln, proto):
        """
        Stores a typedef inside self.entries array.
        """
        #
        # We start by looking for function typedefs.
        #
        typedef_type = r'typedef((?:\s+[\w*]+\b){0,7}\s+(?:\w+\b|\*+))\s*'
        typedef_ident = r'\*?\s*(\w\S+)\s*'
        typedef_args = r'\s*\((.*)\);'

        typedef1 = KernRe(typedef_type + r'\(' + typedef_ident + r'\)' + typedef_args)
        typedef2 = KernRe(typedef_type + typedef_ident + typedef_args)

        # Parse function typedef prototypes
        for r in [typedef1, typedef2]:
            if not r.match(proto):
                continue

            return_type = r.group(1).strip()
            declaration_name = r.group(2)
            args = r.group(3)

            if self.entry.identifier != declaration_name:
                self.emit_msg(ln,
                              f"expecting prototype for typedef {self.entry.identifier}. Prototype was for typedef {declaration_name} instead\n")
                return

            self.create_parameter_list(ln, 'function', args, ',', declaration_name)

            self.output_declaration('function', declaration_name,
                                    typedef=True,
                                    functiontype=return_type,
                                    purpose=self.entry.declaration_purpose)
            return
        #
        # Not a function, try to parse a simple typedef.
        #
        r = KernRe(r'typedef.*\s+(\w+)\s*;')
        if r.match(proto):
            declaration_name = r.group(1)

            if self.entry.identifier != declaration_name:
                self.emit_msg(ln,
                              f"expecting prototype for typedef {self.entry.identifier}. Prototype was for typedef {declaration_name} instead\n")
                return

            self.output_declaration('typedef', declaration_name,
                                    purpose=self.entry.declaration_purpose)
            return

        self.emit_msg(ln, "error: Cannot parse typedef!")

    @staticmethod
    def process_export(function_set, line):
        """
        process EXPORT_SYMBOL* tags

        This method doesn't use any variable from the class, so declare it
        with a staticmethod decorator.
        """

        # We support documenting some exported symbols with different
        # names.  A horrible hack.
        suffixes = [ '_noprof' ]

        # Note: it accepts only one EXPORT_SYMBOL* per line, as having
        # multiple export lines would violate Kernel coding style.

        if export_symbol.search(line):
            symbol = export_symbol.group(2)
        elif export_symbol_ns.search(line):
            symbol = export_symbol_ns.group(2)
        else:
            return False
        #
        # Found an export, trim out any special suffixes
        #
        for suffix in suffixes:
            # Be backward compatible with Python < 3.9
            if symbol.endswith(suffix):
                symbol = symbol[:-len(suffix)]
        function_set.add(symbol)
        return True

    def process_normal(self, ln, line):
        """
        STATE_NORMAL: looking for the /** to begin everything.
        """

        if not doc_start.match(line):
            return

        # start a new entry
        self.reset_state(ln)

        # next line is always the function name
        self.state = state.NAME

    def process_name(self, ln, line):
        """
        STATE_NAME: Looking for the "name - description" line
        """
        #
        # Check for a DOC: block and handle them specially.
        #
        if doc_block.search(line):

            if not doc_block.group(1):
                self.entry.begin_section(ln, "Introduction")
            else:
                self.entry.begin_section(ln, doc_block.group(1))

            self.entry.identifier = self.entry.section
            self.state = state.DOCBLOCK
        #
        # Otherwise we're looking for a normal kerneldoc declaration line.
        #
        elif doc_decl.search(line):
            self.entry.identifier = doc_decl.group(1)

            # Test for data declaration
            if doc_begin_data.search(line):
                self.entry.decl_type = doc_begin_data.group(1)
                self.entry.identifier = doc_begin_data.group(2)
            #
            # Look for a function description
            #
            elif doc_begin_func.search(line):
                self.entry.identifier = doc_begin_func.group(1)
                self.entry.decl_type = "function"
            #
            # We struck out.
            #
            else:
                self.emit_msg(ln,
                              f"This comment starts with '/**', but isn't a kernel-doc comment. Refer Documentation/doc-guide/kernel-doc.rst\n{line}")
                self.state = state.NORMAL
                return
            #
            # OK, set up for a new kerneldoc entry.
            #
            self.state = state.BODY
            self.entry.identifier = self.entry.identifier.strip(" ")
            # if there's no @param blocks need to set up default section here
            self.entry.begin_section(ln + 1)
            #
            # Find the description portion, which *should* be there but
            # isn't always.
            # (We should be able to capture this from the previous parsing - someday)
            #
            r = KernRe("[-:](.*)")
            if r.search(line):
                self.entry.declaration_purpose = trim_whitespace(r.group(1))
                self.state = state.DECLARATION
            else:
                self.entry.declaration_purpose = ""

            if not self.entry.declaration_purpose and self.config.wshort_desc:
                self.emit_msg(ln,
                              f"missing initial short description on line:\n{line}")

            if not self.entry.identifier and self.entry.decl_type != "enum":
                self.emit_msg(ln,
                              f"wrong kernel-doc identifier on line:\n{line}")
                self.state = state.NORMAL

            if self.config.verbose:
                self.emit_msg(ln,
                              f"Scanning doc for {self.entry.decl_type} {self.entry.identifier}",
                                  warning=False)
        #
        # Failed to find an identifier. Emit a warning
        #
        else:
            self.emit_msg(ln, f"Cannot find identifier on line:\n{line}")

    #
    # Helper function to determine if a new section is being started.
    #
    def is_new_section(self, ln, line):
        if doc_sect.search(line):
            self.state = state.BODY
            #
            # Pick out the name of our new section, tweaking it if need be.
            #
            newsection = doc_sect.group(1)
            if newsection.lower() == 'description':
                newsection = 'Description'
            elif newsection.lower() == 'context':
                newsection = 'Context'
                self.state = state.SPECIAL_SECTION
            elif newsection.lower() in ["@return", "@returns",
                                        "return", "returns"]:
                newsection = "Return"
                self.state = state.SPECIAL_SECTION
            elif newsection[0] == '@':
                self.state = state.SPECIAL_SECTION
            #
            # Initialize the contents, and get the new section going.
            #
            newcontents = doc_sect.group(2)
            if not newcontents:
                newcontents = ""
            self.dump_section()
            self.entry.begin_section(ln, newsection)
            self.entry.leading_space = None

            self.entry.add_text(newcontents.lstrip())
            return True
        return False

    #
    # Helper function to detect (and effect) the end of a kerneldoc comment.
    #
    def is_comment_end(self, ln, line):
        if doc_end.search(line):
            self.dump_section()

            # Look for doc_com + <text> + doc_end:
            r = KernRe(r'\s*\*\s*[a-zA-Z_0-9:.]+\*/')
            if r.match(line):
                self.emit_msg(ln, f"suspicious ending line: {line}")

            self.entry.prototype = ""
            self.entry.new_start_line = ln + 1

            self.state = state.PROTO
            return True
        return False


    def process_decl(self, ln, line):
        """
        STATE_DECLARATION: We've seen the beginning of a declaration
        """
        if self.is_new_section(ln, line) or self.is_comment_end(ln, line):
            return
        #
        # Look for anything with the " * " line beginning.
        #
        if doc_content.search(line):
            cont = doc_content.group(1)
            #
            # A blank line means that we have moved out of the declaration
            # part of the comment (without any "special section" parameter
            # descriptions).
            #
            if cont == "":
                self.state = state.BODY
            #
            # Otherwise we have more of the declaration section to soak up.
            #
            else:
                self.entry.declaration_purpose = \
                    trim_whitespace(self.entry.declaration_purpose + ' ' + cont)
        else:
            # Unknown line, ignore
            self.emit_msg(ln, f"bad line: {line}")


    def process_special(self, ln, line):
        """
        STATE_SPECIAL_SECTION: a section ending with a blank line
        """
        #
        # If we have hit a blank line (only the " * " marker), then this
        # section is done.
        #
        if KernRe(r"\s*\*\s*$").match(line):
            self.entry.begin_section(ln, dump = True)
            self.state = state.BODY
            return
        #
        # Not a blank line, look for the other ways to end the section.
        #
        if self.is_new_section(ln, line) or self.is_comment_end(ln, line):
            return
        #
        # OK, we should have a continuation of the text for this section.
        #
        if doc_content.search(line):
            cont = doc_content.group(1)
            #
            # If the lines of text after the first in a special section have
            # leading white space, we need to trim it out or Sphinx will get
            # confused.  For the second line (the None case), see what we
            # find there and remember it.
            #
            if self.entry.leading_space is None:
                r = KernRe(r'^(\s+)')
                if r.match(cont):
                    self.entry.leading_space = len(r.group(1))
                else:
                    self.entry.leading_space = 0
            #
            # Otherwise, before trimming any leading chars, be *sure*
            # that they are white space.  We should maybe warn if this
            # isn't the case.
            #
            for i in range(0, self.entry.leading_space):
                if cont[i] != " ":
                    self.entry.leading_space = i
                    break
            #
            # Add the trimmed result to the section and we're done.
            #
            self.entry.add_text(cont[self.entry.leading_space:])
        else:
            # Unknown line, ignore
            self.emit_msg(ln, f"bad line: {line}")

    def process_body(self, ln, line):
        """
        STATE_BODY: the bulk of a kerneldoc comment.
        """
        if self.is_new_section(ln, line) or self.is_comment_end(ln, line):
            return

        if doc_content.search(line):
            cont = doc_content.group(1)
            self.entry.add_text(cont)
        else:
            # Unknown line, ignore
            self.emit_msg(ln, f"bad line: {line}")

    def process_inline_name(self, ln, line):
        """STATE_INLINE_NAME: beginning of docbook comments within a prototype."""

        if doc_inline_sect.search(line):
            self.entry.begin_section(ln, doc_inline_sect.group(1))
            self.entry.add_text(doc_inline_sect.group(2).lstrip())
            self.state = state.INLINE_TEXT
        elif doc_inline_end.search(line):
            self.dump_section()
            self.state = state.PROTO
        elif doc_content.search(line):
            self.emit_msg(ln, f"Incorrect use of kernel-doc format: {line}")
            self.state = state.PROTO
        # else ... ??

    def process_inline_text(self, ln, line):
        """STATE_INLINE_TEXT: docbook comments within a prototype."""

        if doc_inline_end.search(line):
            self.dump_section()
            self.state = state.PROTO
        elif doc_content.search(line):
            self.entry.add_text(doc_content.group(1))
        # else ... ??

    def syscall_munge(self, ln, proto):         # pylint: disable=W0613
        """
        Handle syscall definitions
        """

        is_void = False

        # Strip newlines/CR's
        proto = re.sub(r'[\r\n]+', ' ', proto)

        # Check if it's a SYSCALL_DEFINE0
        if 'SYSCALL_DEFINE0' in proto:
            is_void = True

        # Replace SYSCALL_DEFINE with correct return type & function name
        proto = KernRe(r'SYSCALL_DEFINE.*\(').sub('long sys_', proto)

        r = KernRe(r'long\s+(sys_.*?),')
        if r.search(proto):
            proto = KernRe(',').sub('(', proto, count=1)
        elif is_void:
            proto = KernRe(r'\)').sub('(void)', proto, count=1)

        # Now delete all of the odd-numbered commas in the proto
        # so that argument types & names don't have a comma between them
        count = 0
        length = len(proto)

        if is_void:
            length = 0  # skip the loop if is_void

        for ix in range(length):
            if proto[ix] == ',':
                count += 1
                if count % 2 == 1:
                    proto = proto[:ix] + ' ' + proto[ix + 1:]

        return proto

    def tracepoint_munge(self, ln, proto):
        """
        Handle tracepoint definitions
        """

        tracepointname = None
        tracepointargs = None

        # Match tracepoint name based on different patterns
        r = KernRe(r'TRACE_EVENT\((.*?),')
        if r.search(proto):
            tracepointname = r.group(1)

        r = KernRe(r'DEFINE_SINGLE_EVENT\((.*?),')
        if r.search(proto):
            tracepointname = r.group(1)

        r = KernRe(r'DEFINE_EVENT\((.*?),(.*?),')
        if r.search(proto):
            tracepointname = r.group(2)

        if tracepointname:
            tracepointname = tracepointname.lstrip()

        r = KernRe(r'TP_PROTO\((.*?)\)')
        if r.search(proto):
            tracepointargs = r.group(1)

        if not tracepointname or not tracepointargs:
            self.emit_msg(ln,
                          f"Unrecognized tracepoint format:\n{proto}\n")
        else:
            proto = f"static inline void trace_{tracepointname}({tracepointargs})"
            self.entry.identifier = f"trace_{self.entry.identifier}"

        return proto

    def process_proto_function(self, ln, line):
        """Ancillary routine to process a function prototype"""

        # strip C99-style comments to end of line
        line = KernRe(r"//.*$", re.S).sub('', line)
        #
        # Soak up the line's worth of prototype text, stopping at { or ; if present.
        #
        if KernRe(r'\s*#\s*define').match(line):
            self.entry.prototype = line
        elif not line.startswith('#'):   # skip other preprocessor stuff
            r = KernRe(r'([^\{]*)')
            if r.match(line):
                self.entry.prototype += r.group(1) + " "
        #
        # If we now have the whole prototype, clean it up and declare victory.
        #
        if '{' in line or ';' in line or KernRe(r'\s*#\s*define').match(line):
            # strip comments and surrounding spaces
            self.entry.prototype = KernRe(r'/\*.*\*/').sub('', self.entry.prototype).strip()
            #
            # Handle self.entry.prototypes for function pointers like:
            #       int (*pcs_config)(struct foo)
            # by turning it into
            #	    int pcs_config(struct foo)
            #
            r = KernRe(r'^(\S+\s+)\(\s*\*(\S+)\)')
            self.entry.prototype = r.sub(r'\1\2', self.entry.prototype)
            #
            # Handle special declaration syntaxes
            #
            if 'SYSCALL_DEFINE' in self.entry.prototype:
                self.entry.prototype = self.syscall_munge(ln,
                                                          self.entry.prototype)
            else:
                r = KernRe(r'TRACE_EVENT|DEFINE_EVENT|DEFINE_SINGLE_EVENT')
                if r.search(self.entry.prototype):
                    self.entry.prototype = self.tracepoint_munge(ln,
                                                                 self.entry.prototype)
            #
            # ... and we're done
            #
            self.dump_function(ln, self.entry.prototype)
            self.reset_state(ln)

    def process_proto_type(self, ln, line):
        """Ancillary routine to process a type"""

        # Strip C99-style comments and surrounding whitespace
        line = KernRe(r"//.*$", re.S).sub('', line).strip()
        if not line:
            return # nothing to see here

        # To distinguish preprocessor directive from regular declaration later.
        if line.startswith('#'):
            line += ";"
        #
        # Split the declaration on any of { } or ;, and accumulate pieces
        # until we hit a semicolon while not inside {brackets}
        #
        r = KernRe(r'(.*?)([{};])')
        for chunk in r.split(line):
            if chunk:  # Ignore empty matches
                self.entry.prototype += chunk
                #
                # This cries out for a match statement ... someday after we can
                # drop Python 3.9 ...
                #
                if chunk == '{':
                    self.entry.brcount += 1
                elif chunk == '}':
                    self.entry.brcount -= 1
                elif chunk == ';' and self.entry.brcount <= 0:
                    self.dump_declaration(ln, self.entry.prototype)
                    self.reset_state(ln)
                    return
        #
        # We hit the end of the line while still in the declaration; put
        # in a space to represent the newline.
        #
        self.entry.prototype += ' '

    def process_proto(self, ln, line):
        """STATE_PROTO: reading a function/whatever prototype."""

        if doc_inline_oneline.search(line):
            self.entry.begin_section(ln, doc_inline_oneline.group(1))
            self.entry.add_text(doc_inline_oneline.group(2))
            self.dump_section()

        elif doc_inline_start.search(line):
            self.state = state.INLINE_NAME

        elif self.entry.decl_type == 'function':
            self.process_proto_function(ln, line)

        else:
            self.process_proto_type(ln, line)

    def process_docblock(self, ln, line):
        """STATE_DOCBLOCK: within a DOC: block."""

        if doc_end.search(line):
            self.dump_section()
            self.output_declaration("doc", self.entry.identifier)
            self.reset_state(ln)

        elif doc_content.search(line):
            self.entry.add_text(doc_content.group(1))

    def parse_export(self):
        """
        Parses EXPORT_SYMBOL* macros from a single Kernel source file.
        """

        export_table = set()

        try:
            with open(self.fname, "r", encoding="utf8",
                      errors="backslashreplace") as fp:

                for line in fp:
                    self.process_export(export_table, line)

        except IOError:
            return None

        return export_table

    #
    # The state/action table telling us which function to invoke in
    # each state.
    #
    state_actions = {
        state.NORMAL:			process_normal,
        state.NAME:			process_name,
        state.BODY:			process_body,
        state.DECLARATION:		process_decl,
        state.SPECIAL_SECTION:		process_special,
        state.INLINE_NAME:		process_inline_name,
        state.INLINE_TEXT:		process_inline_text,
        state.PROTO:			process_proto,
        state.DOCBLOCK:			process_docblock,
        }

    def parse_kdoc(self):
        """
        Open and process each line of a C source file.
        The parsing is controlled via a state machine, and the line is passed
        to a different process function depending on the state. The process
        function may update the state as needed.

        Besides parsing kernel-doc tags, it also parses export symbols.
        """

        prev = ""
        prev_ln = None
        export_table = set()

        try:
            with open(self.fname, "r", encoding="utf8",
                      errors="backslashreplace") as fp:
                for ln, line in enumerate(fp):

                    line = line.expandtabs().strip("\n")

                    # Group continuation lines on prototypes
                    if self.state == state.PROTO:
                        if line.endswith("\\"):
                            prev += line.rstrip("\\")
                            if not prev_ln:
                                prev_ln = ln
                            continue

                        if prev:
                            ln = prev_ln
                            line = prev + line
                            prev = ""
                            prev_ln = None

                    self.config.log.debug("%d %s: %s",
                                          ln, state.name[self.state],
                                          line)

                    # This is an optimization over the original script.
                    # There, when export_file was used for the same file,
                    # it was read twice. Here, we use the already-existing
                    # loop to parse exported symbols as well.
                    #
                    if (self.state != state.NORMAL) or \
                       not self.process_export(export_table, line):
                        # Hand this line to the appropriate state handler
                        self.state_actions[self.state](self, ln, line)

        except OSError:
            self.config.log.error(f"Error: Cannot open file {self.fname}")

        return export_table, self.entries
