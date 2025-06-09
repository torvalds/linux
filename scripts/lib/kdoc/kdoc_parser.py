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

import re
from pprint import pformat

from kdoc_re import NestedMatch, KernRe


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
doc_sect = doc_com + \
            KernRe(r'\s*(\@[.\w]+|\@\.\.\.|description|context|returns?|notes?|examples?)\s*:([^:].*)?$',
                flags=re.I, cache=False)

doc_content = doc_com_body + KernRe(r'(.*)', cache=False)
doc_block = doc_com + KernRe(r'DOC:\s*(.*)?', cache=False)
doc_inline_start = KernRe(r'^\s*/\*\*\s*$', cache=False)
doc_inline_sect = KernRe(r'\s*\*\s*(@\s*[\w][\w\.]*\s*):(.*)', cache=False)
doc_inline_end = KernRe(r'^\s*\*/\s*$', cache=False)
doc_inline_oneline = KernRe(r'^\s*/\*\*\s*(@[\w\s]+):\s*(.*)\s*\*/\s*$', cache=False)
attribute = KernRe(r"__attribute__\s*\(\([a-z0-9,_\*\s\(\)]*\)\)",
               flags=re.I | re.S, cache=False)

export_symbol = KernRe(r'^\s*EXPORT_SYMBOL(_GPL)?\s*\(\s*(\w+)\s*\)\s*', cache=False)
export_symbol_ns = KernRe(r'^\s*EXPORT_SYMBOL_NS(_GPL)?\s*\(\s*(\w+)\s*,\s*"\S+"\)\s*', cache=False)

type_param = KernRe(r"\@(\w*((\.\w+)|(->\w+))*(\.\.\.)?)", cache=False)

class state:
    """
    State machine enums
    """

    # Parser states
    NORMAL        = 0        # normal code
    NAME          = 1        # looking for function name
    BODY_MAYBE    = 2        # body - or maybe more description
    BODY          = 3        # the body of the comment
    BODY_WITH_BLANK_LINE = 4 # the body which has a blank line
    PROTO         = 5        # scanning prototype
    DOCBLOCK      = 6        # documentation block
    INLINE        = 7        # gathering doc outside main block

    name = [
        "NORMAL",
        "NAME",
        "BODY_MAYBE",
        "BODY",
        "BODY_WITH_BLANK_LINE",
        "PROTO",
        "DOCBLOCK",
        "INLINE",
    ]

    # Inline documentation state
    INLINE_NA     = 0 # not applicable ($state != INLINE)
    INLINE_NAME   = 1 # looking for member name (@foo:)
    INLINE_TEXT   = 2 # looking for member documentation
    INLINE_END    = 3 # done
    INLINE_ERROR  = 4 # error - Comment without header was found.
                      # Spit a warning as it's not
                      # proper kernel-doc and ignore the rest.

    inline_name = [
        "",
        "_NAME",
        "_TEXT",
        "_END",
        "_ERROR",
    ]

SECTION_DEFAULT = "Description"  # default section

class KernelEntry:

    def __init__(self, config, ln):
        self.config = config

        self.contents = ""
        self.function = ""
        self.sectcheck = ""
        self.struct_actual = ""
        self.prototype = ""

        self.warnings = []

        self.parameterlist = []
        self.parameterdescs = {}
        self.parametertypes = {}
        self.parameterdesc_start_lines = {}

        self.section_start_lines = {}
        self.sectionlist = []
        self.sections = {}

        self.anon_struct_union = False

        self.leading_space = None

        # State flags
        self.brcount = 0

        self.in_doc_sect = False
        self.declaration_start_line = ln + 1

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

    def dump_section(self, start_new=True):
        """
        Dumps section contents to arrays/hashes intended for that purpose.
        """

        name = self.section
        contents = self.contents

        if type_param.match(name):
            name = type_param.group(1)

            self.parameterdescs[name] = contents
            self.parameterdesc_start_lines[name] = self.new_start_line

            self.sectcheck += name + " "
            self.new_start_line = 0

        elif name == "@...":
            name = "..."
            self.parameterdescs[name] = contents
            self.sectcheck += name + " "
            self.parameterdesc_start_lines[name] = self.new_start_line
            self.new_start_line = 0

        else:
            if name in self.sections and self.sections[name] != "":
                # Only warn on user-specified duplicate section names
                if name != SECTION_DEFAULT:
                    self.emit_msg(self.new_start_line,
                                  f"duplicate section name '{name}'\n")
                self.sections[name] += contents
            else:
                self.sections[name] = contents
                self.sectionlist.append(name)
                self.section_start_lines[name] = self.new_start_line
                self.new_start_line = 0

#        self.config.log.debug("Section: %s : %s", name, pformat(vars(self)))

        if start_new:
            self.section = SECTION_DEFAULT
            self.contents = ""


class KernelDoc:
    """
    Read a C language source or header FILE and extract embedded
    documentation comments.
    """

    # Section names

    section_intro = "Introduction"
    section_context = "Context"
    section_return = "Return"

    undescribed = "-- undescribed --"

    def __init__(self, config, fname):
        """Initialize internal variables"""

        self.fname = fname
        self.config = config

        # Initial state for the state machines
        self.state = state.NORMAL
        self.inline_doc_state = state.INLINE_NA

        # Store entry currently being processed
        self.entry = None

        # Place all potential outputs into an array
        self.entries = []

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

        # The implementation here is different than the original kernel-doc:
        # instead of checking for output filters or actually output anything,
        # it just stores the declaration content at self.entries, as the
        # output will happen on a separate class.
        #
        # For now, we're keeping the same name of the function just to make
        # easier to compare the source code of both scripts

        args["declaration_start_line"] = self.entry.declaration_start_line
        args["type"] = dtype
        args["warnings"] = self.entry.warnings

        # TODO: use colletions.OrderedDict to remove sectionlist

        sections = args.get('sections', {})
        sectionlist = args.get('sectionlist', [])

        # Drop empty sections
        # TODO: improve empty sections logic to emit warnings
        for section in ["Description", "Return"]:
            if section in sectionlist:
                if not sections[section].rstrip():
                    del sections[section]
                    sectionlist.remove(section)

        self.entries.append((name, args))

        self.config.log.debug("Output: %s:%s = %s", dtype, name, pformat(args))

    def reset_state(self, ln):
        """
        Ancillary routine to create a new entry. It initializes all
        variables used by the state machine.
        """

        self.entry = KernelEntry(self.config, ln)

        # State flags
        self.state = state.NORMAL
        self.inline_doc_state = state.INLINE_NA

    def push_parameter(self, ln, decl_type, param, dtype,
                       org_arg, declaration_name):
        """
        Store parameters and their descriptions at self.entry.
        """

        if self.entry.anon_struct_union and dtype == "" and param == "}":
            return  # Ignore the ending }; from anonymous struct/union

        self.entry.anon_struct_union = False

        param = KernRe(r'[\[\)].*').sub('', param, count=1)

        if dtype == "" and param.endswith("..."):
            if KernRe(r'\w\.\.\.$').search(param):
                # For named variable parameters of the form `x...`,
                # remove the dots
                param = param[:-3]
            else:
                # Handles unnamed variable parameters
                param = "..."

            if param not in self.entry.parameterdescs or \
                not self.entry.parameterdescs[param]:

                self.entry.parameterdescs[param] = "variable arguments"

        elif dtype == "" and (not param or param == "void"):
            param = "void"
            self.entry.parameterdescs[param] = "no arguments"

        elif dtype == "" and param in ["struct", "union"]:
            # Handle unnamed (anonymous) union or struct
            dtype = param
            param = "{unnamed_" + param + "}"
            self.entry.parameterdescs[param] = "anonymous\n"
            self.entry.anon_struct_union = True

        # Handle cache group enforcing variables: they do not need
        # to be described in header files
        elif "__cacheline_group" in param:
            # Ignore __cacheline_group_begin and __cacheline_group_end
            return

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

    def save_struct_actual(self, actual):
        """
        Strip all spaces from the actual param so that it looks like
        one string item.
        """

        actual = KernRe(r'\s*').sub("", actual, count=1)

        self.entry.struct_actual += actual + " "

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
            # Strip comments
            arg = KernRe(r'\/\*.*\*\/').sub('', arg)

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

            elif KernRe(r'\(.+\)\s*\(').search(arg):
                # Pointer-to-function

                arg = arg.replace('#', ',')

                r = KernRe(r'[^\(]+\(\*?\s*([\w\[\]\.]*)\s*\)')
                if r.match(arg):
                    param = r.group(1)
                else:
                    self.emit_msg(ln, f"Invalid param: {arg}")
                    param = arg

                dtype = KernRe(r'([^\(]+\(\*?)\s*' + re.escape(param)).sub(r'\1', arg)
                self.save_struct_actual(param)
                self.push_parameter(ln, decl_type, param, dtype,
                                    arg, declaration_name)

            elif KernRe(r'\(.+\)\s*\[').search(arg):
                # Array-of-pointers

                arg = arg.replace('#', ',')
                r = KernRe(r'[^\(]+\(\s*\*\s*([\w\[\]\.]*?)\s*(\s*\[\s*[\w]+\s*\]\s*)*\)')
                if r.match(arg):
                    param = r.group(1)
                else:
                    self.emit_msg(ln, f"Invalid param: {arg}")
                    param = arg

                dtype = KernRe(r'([^\(]+\(\*?)\s*' + re.escape(param)).sub(r'\1', arg)

                self.save_struct_actual(param)
                self.push_parameter(ln, decl_type, param, dtype,
                                    arg, declaration_name)

            elif arg:
                arg = KernRe(r'\s*:\s*').sub(":", arg)
                arg = KernRe(r'\s*\[').sub('[', arg)

                args = KernRe(r'\s*,\s*').split(arg)
                if args[0] and '*' in args[0]:
                    args[0] = re.sub(r'(\*+)\s*', r' \1', args[0])

                first_arg = []
                r = KernRe(r'^(.*\s+)(.*?\[.*\].*)$')
                if args[0] and r.match(args[0]):
                    args.pop(0)
                    first_arg.extend(r.group(1))
                    first_arg.append(r.group(2))
                else:
                    first_arg = KernRe(r'\s+').split(args.pop(0))

                args.insert(0, first_arg.pop())
                dtype = ' '.join(first_arg)

                for param in args:
                    if KernRe(r'^(\*+)\s*(.*)').match(param):
                        r = KernRe(r'^(\*+)\s*(.*)')
                        if not r.match(param):
                            self.emit_msg(ln, f"Invalid param: {param}")
                            continue

                        param = r.group(1)

                        self.save_struct_actual(r.group(2))
                        self.push_parameter(ln, decl_type, r.group(2),
                                            f"{dtype} {r.group(1)}",
                                            arg, declaration_name)

                    elif KernRe(r'(.*?):(\w+)').search(param):
                        r = KernRe(r'(.*?):(\w+)')
                        if not r.match(param):
                            self.emit_msg(ln, f"Invalid param: {param}")
                            continue

                        if dtype != "":  # Skip unnamed bit-fields
                            self.save_struct_actual(r.group(1))
                            self.push_parameter(ln, decl_type, r.group(1),
                                                f"{dtype}:{r.group(2)}",
                                                arg, declaration_name)
                    else:
                        self.save_struct_actual(param)
                        self.push_parameter(ln, decl_type, param, dtype,
                                            arg, declaration_name)

    def check_sections(self, ln, decl_name, decl_type, sectcheck, prmscheck):
        """
        Check for errors inside sections, emitting warnings if not found
        parameters are described.
        """

        sects = sectcheck.split()
        prms = prmscheck.split()
        err = False

        for sx in range(len(sects)):                  # pylint: disable=C0200
            err = True
            for px in range(len(prms)):               # pylint: disable=C0200
                prm_clean = prms[px]
                prm_clean = KernRe(r'\[.*\]').sub('', prm_clean)
                prm_clean = attribute.sub('', prm_clean)

                # ignore array size in a parameter string;
                # however, the original param string may contain
                # spaces, e.g.:  addr[6 + 2]
                # and this appears in @prms as "addr[6" since the
                # parameter list is split at spaces;
                # hence just ignore "[..." for the sections check;
                prm_clean = KernRe(r'\[.*').sub('', prm_clean)

                if prm_clean == sects[sx]:
                    err = False
                    break

            if err:
                if decl_type == 'function':
                    dname = f"{decl_type} parameter"
                else:
                    dname = f"{decl_type} member"

                self.emit_msg(ln,
                              f"Excess {dname} '{sects[sx]}' description in '{decl_name}'")

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

    def dump_struct(self, ln, proto):
        """
        Store an entry for an struct or union
        """

        type_pattern = r'(struct|union)'

        qualifiers = [
            "__attribute__",
            "__packed",
            "__aligned",
            "____cacheline_aligned_in_smp",
            "____cacheline_aligned",
        ]

        definition_body = r'\{(.*)\}\s*' + "(?:" + '|'.join(qualifiers) + ")?"
        struct_members = KernRe(type_pattern + r'([^\{\};]+)(\{)([^\{\}]*)(\})([^\{\}\;]*)(\;)')

        # Extract struct/union definition
        members = None
        declaration_name = None
        decl_type = None

        r = KernRe(type_pattern + r'\s+(\w+)\s*' + definition_body)
        if r.search(proto):
            decl_type = r.group(1)
            declaration_name = r.group(2)
            members = r.group(3)
        else:
            r = KernRe(r'typedef\s+' + type_pattern + r'\s*' + definition_body + r'\s*(\w+)\s*;')

            if r.search(proto):
                decl_type = r.group(1)
                declaration_name = r.group(3)
                members = r.group(2)

        if not members:
            self.emit_msg(ln, f"{proto} error: Cannot parse struct or union!")
            return

        if self.entry.identifier != declaration_name:
            self.emit_msg(ln,
                          f"expecting prototype for {decl_type} {self.entry.identifier}. Prototype was for {decl_type} {declaration_name} instead\n")
            return

        args_pattern = r'([^,)]+)'

        sub_prefixes = [
            (KernRe(r'\/\*\s*private:.*?\/\*\s*public:.*?\*\/', re.S | re.I), ''),
            (KernRe(r'\/\*\s*private:.*', re.S | re.I), ''),

            # Strip comments
            (KernRe(r'\/\*.*?\*\/', re.S), ''),

            # Strip attributes
            (attribute, ' '),
            (KernRe(r'\s*__aligned\s*\([^;]*\)', re.S), ' '),
            (KernRe(r'\s*__counted_by\s*\([^;]*\)', re.S), ' '),
            (KernRe(r'\s*__counted_by_(le|be)\s*\([^;]*\)', re.S), ' '),
            (KernRe(r'\s*__packed\s*', re.S), ' '),
            (KernRe(r'\s*CRYPTO_MINALIGN_ATTR', re.S), ' '),
            (KernRe(r'\s*____cacheline_aligned_in_smp', re.S), ' '),
            (KernRe(r'\s*____cacheline_aligned', re.S), ' '),

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

            (KernRe(r'\bstruct_group\s*\(([^,]*,)', re.S), r'STRUCT_GROUP('),
            (KernRe(r'\bstruct_group_attr\s*\(([^,]*,){2}', re.S), r'STRUCT_GROUP('),
            (KernRe(r'\bstruct_group_tagged\s*\(([^,]*),([^,]*),', re.S), r'struct \1 \2; STRUCT_GROUP('),
            (KernRe(r'\b__struct_group\s*\(([^,]*,){3}', re.S), r'STRUCT_GROUP('),

            # Replace macros
            #
            # TODO: use NestedMatch for FOO($1, $2, ...) matches
            #
            # it is better to also move those to the NestedMatch logic,
            # to ensure that parenthesis will be properly matched.

            (KernRe(r'__ETHTOOL_DECLARE_LINK_MODE_MASK\s*\(([^\)]+)\)', re.S), r'DECLARE_BITMAP(\1, __ETHTOOL_LINK_MODE_MASK_NBITS)'),
            (KernRe(r'DECLARE_PHY_INTERFACE_MASK\s*\(([^\)]+)\)', re.S), r'DECLARE_BITMAP(\1, PHY_INTERFACE_MODE_MAX)'),
            (KernRe(r'DECLARE_BITMAP\s*\(' + args_pattern + r',\s*' + args_pattern + r'\)', re.S), r'unsigned long \1[BITS_TO_LONGS(\2)]'),
            (KernRe(r'DECLARE_HASHTABLE\s*\(' + args_pattern + r',\s*' + args_pattern + r'\)', re.S), r'unsigned long \1[1 << ((\2) - 1)]'),
            (KernRe(r'DECLARE_KFIFO\s*\(' + args_pattern + r',\s*' + args_pattern + r',\s*' + args_pattern + r'\)', re.S), r'\2 *\1'),
            (KernRe(r'DECLARE_KFIFO_PTR\s*\(' + args_pattern + r',\s*' + args_pattern + r'\)', re.S), r'\2 *\1'),
            (KernRe(r'(?:__)?DECLARE_FLEX_ARRAY\s*\(' + args_pattern + r',\s*' + args_pattern + r'\)', re.S), r'\1 \2[]'),
            (KernRe(r'DEFINE_DMA_UNMAP_ADDR\s*\(' + args_pattern + r'\)', re.S), r'dma_addr_t \1'),
            (KernRe(r'DEFINE_DMA_UNMAP_LEN\s*\(' + args_pattern + r'\)', re.S), r'__u32 \1'),
        ]

        # Regexes here are guaranteed to have the end limiter matching
        # the start delimiter. Yet, right now, only one replace group
        # is allowed.

        sub_nested_prefixes = [
            (re.compile(r'\bSTRUCT_GROUP\('), r'\1'),
        ]

        for search, sub in sub_prefixes:
            members = search.sub(sub, members)

        nested = NestedMatch()

        for search, sub in sub_nested_prefixes:
            members = nested.sub(search, sub, members)

        # Keeps the original declaration as-is
        declaration = members

        # Split nested struct/union elements
        #
        # This loop was simpler at the original kernel-doc perl version, as
        #   while ($members =~ m/$struct_members/) { ... }
        # reads 'members' string on each interaction.
        #
        # Python behavior is different: it parses 'members' only once,
        # creating a list of tuples from the first interaction.
        #
        # On other words, this won't get nested structs.
        #
        # So, we need to have an extra loop on Python to override such
        # re limitation.

        while True:
            tuples = struct_members.findall(members)
            if not tuples:
                break

            for t in tuples:
                newmember = ""
                maintype = t[0]
                s_ids = t[5]
                content = t[3]

                oldmember = "".join(t)

                for s_id in s_ids.split(','):
                    s_id = s_id.strip()

                    newmember += f"{maintype} {s_id}; "
                    s_id = KernRe(r'[:\[].*').sub('', s_id)
                    s_id = KernRe(r'^\s*\**(\S+)\s*').sub(r'\1', s_id)

                    for arg in content.split(';'):
                        arg = arg.strip()

                        if not arg:
                            continue

                        r = KernRe(r'^([^\(]+\(\*?\s*)([\w\.]*)(\s*\).*)')
                        if r.match(arg):
                            # Pointer-to-function
                            dtype = r.group(1)
                            name = r.group(2)
                            extra = r.group(3)

                            if not name:
                                continue

                            if not s_id:
                                # Anonymous struct/union
                                newmember += f"{dtype}{name}{extra}; "
                            else:
                                newmember += f"{dtype}{s_id}.{name}{extra}; "

                        else:
                            arg = arg.strip()
                            # Handle bitmaps
                            arg = KernRe(r':\s*\d+\s*').sub('', arg)

                            # Handle arrays
                            arg = KernRe(r'\[.*\]').sub('', arg)

                            # Handle multiple IDs
                            arg = KernRe(r'\s*,\s*').sub(',', arg)

                            r = KernRe(r'(.*)\s+([\S+,]+)')

                            if r.search(arg):
                                dtype = r.group(1)
                                names = r.group(2)
                            else:
                                newmember += f"{arg}; "
                                continue

                            for name in names.split(','):
                                name = KernRe(r'^\s*\**(\S+)\s*').sub(r'\1', name).strip()

                                if not name:
                                    continue

                                if not s_id:
                                    # Anonymous struct/union
                                    newmember += f"{dtype} {name}; "
                                else:
                                    newmember += f"{dtype} {s_id}.{name}; "

                members = members.replace(oldmember, newmember)

        # Ignore other nested elements, like enums
        members = re.sub(r'(\{[^\{\}]*\})', '', members)

        self.create_parameter_list(ln, decl_type, members, ';',
                                   declaration_name)
        self.check_sections(ln, declaration_name, decl_type,
                            self.entry.sectcheck, self.entry.struct_actual)

        # Adjust declaration for better display
        declaration = KernRe(r'([\{;])').sub(r'\1\n', declaration)
        declaration = KernRe(r'\}\s+;').sub('};', declaration)

        # Better handle inlined enums
        while True:
            r = KernRe(r'(enum\s+\{[^\}]+),([^\n])')
            if not r.search(declaration):
                break

            declaration = r.sub(r'\1,\n\2', declaration)

        def_args = declaration.split('\n')
        level = 1
        declaration = ""
        for clause in def_args:

            clause = clause.strip()
            clause = KernRe(r'\s+').sub(' ', clause, count=1)

            if not clause:
                continue

            if '}' in clause and level > 1:
                level -= 1

            if not KernRe(r'^\s*#').match(clause):
                declaration += "\t" * level

            declaration += "\t" + clause + "\n"
            if "{" in clause and "}" not in clause:
                level += 1

        self.output_declaration(decl_type, declaration_name,
                                struct=declaration_name,
                                definition=declaration,
                                parameterlist=self.entry.parameterlist,
                                parameterdescs=self.entry.parameterdescs,
                                parametertypes=self.entry.parametertypes,
                                parameterdesc_start_lines=self.entry.parameterdesc_start_lines,
                                sectionlist=self.entry.sectionlist,
                                sections=self.entry.sections,
                                section_start_lines=self.entry.section_start_lines,
                                purpose=self.entry.declaration_purpose)

    def dump_enum(self, ln, proto):
        """
        Stores an enum inside self.entries array.
        """

        # Ignore members marked private
        proto = KernRe(r'\/\*\s*private:.*?\/\*\s*public:.*?\*\/', flags=re.S).sub('', proto)
        proto = KernRe(r'\/\*\s*private:.*}', flags=re.S).sub('}', proto)

        # Strip comments
        proto = KernRe(r'\/\*.*?\*\/', flags=re.S).sub('', proto)

        # Strip #define macros inside enums
        proto = KernRe(r'#\s*((define|ifdef|if)\s+|endif)[^;]*;', flags=re.S).sub('', proto)

        members = None
        declaration_name = None

        r = KernRe(r'typedef\s+enum\s*\{(.*)\}\s*(\w*)\s*;')
        if r.search(proto):
            declaration_name = r.group(2)
            members = r.group(1).rstrip()
        else:
            r = KernRe(r'enum\s+(\w*)\s*\{(.*)\}')
            if r.match(proto):
                declaration_name = r.group(1)
                members = r.group(2).rstrip()

        if not members:
            self.emit_msg(ln, f"{proto}: error: Cannot parse enum!")
            return

        if self.entry.identifier != declaration_name:
            if self.entry.identifier == "":
                self.emit_msg(ln,
                              f"{proto}: wrong kernel-doc identifier on prototype")
            else:
                self.emit_msg(ln,
                              f"expecting prototype for enum {self.entry.identifier}. Prototype was for enum {declaration_name} instead")
            return

        if not declaration_name:
            declaration_name = "(anonymous)"

        member_set = set()

        members = KernRe(r'\([^;]*?[\)]').sub('', members)

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

        for k in self.entry.parameterdescs:
            if k not in member_set:
                self.emit_msg(ln,
                              f"Excess enum value '%{k}' description in '{declaration_name}'")

        self.output_declaration('enum', declaration_name,
                                enum=declaration_name,
                                parameterlist=self.entry.parameterlist,
                                parameterdescs=self.entry.parameterdescs,
                                parameterdesc_start_lines=self.entry.parameterdesc_start_lines,
                                sectionlist=self.entry.sectionlist,
                                sections=self.entry.sections,
                                section_start_lines=self.entry.section_start_lines,
                                purpose=self.entry.declaration_purpose)

    def dump_declaration(self, ln, prototype):
        """
        Stores a data declaration inside self.entries array.
        """

        if self.entry.decl_type == "enum":
            self.dump_enum(ln, prototype)
            return

        if self.entry.decl_type == "typedef":
            self.dump_typedef(ln, prototype)
            return

        if self.entry.decl_type in ["union", "struct"]:
            self.dump_struct(ln, prototype)
            return

        self.output_declaration(self.entry.decl_type, prototype,
                                entry=self.entry)

    def dump_function(self, ln, prototype):
        """
        Stores a function of function macro inside self.entries array.
        """

        func_macro = False
        return_type = ''
        decl_type = 'function'

        # Prefixes that would be removed
        sub_prefixes = [
            (r"^static +", "", 0),
            (r"^extern +", "", 0),
            (r"^asmlinkage +", "", 0),
            (r"^inline +", "", 0),
            (r"^__inline__ +", "", 0),
            (r"^__inline +", "", 0),
            (r"^__always_inline +", "", 0),
            (r"^noinline +", "", 0),
            (r"^__FORTIFY_INLINE +", "", 0),
            (r"__init +", "", 0),
            (r"__init_or_module +", "", 0),
            (r"__deprecated +", "", 0),
            (r"__flatten +", "", 0),
            (r"__meminit +", "", 0),
            (r"__must_check +", "", 0),
            (r"__weak +", "", 0),
            (r"__sched +", "", 0),
            (r"_noprof", "", 0),
            (r"__printf\s*\(\s*\d*\s*,\s*\d*\s*\) +", "", 0),
            (r"__(?:re)?alloc_size\s*\(\s*\d+\s*(?:,\s*\d+\s*)?\) +", "", 0),
            (r"__diagnose_as\s*\(\s*\S+\s*(?:,\s*\d+\s*)*\) +", "", 0),
            (r"DECL_BUCKET_PARAMS\s*\(\s*(\S+)\s*,\s*(\S+)\s*\)", r"\1, \2", 0),
            (r"__attribute_const__ +", "", 0),

            # It seems that Python support for re.X is broken:
            # At least for me (Python 3.13), this didn't work
#            (r"""
#              __attribute__\s*\(\(
#                (?:
#                    [\w\s]+          # attribute name
#                    (?:\([^)]*\))?   # attribute arguments
#                    \s*,?            # optional comma at the end
#                )+
#              \)\)\s+
#             """, "", re.X),

            # So, remove whitespaces and comments from it
            (r"__attribute__\s*\(\((?:[\w\s]+(?:\([^)]*\))?\s*,?)+\)\)\s+", "", 0),
        ]

        for search, sub, flags in sub_prefixes:
            prototype = KernRe(search, flags).sub(sub, prototype)

        # Macros are a special case, as they change the prototype format
        new_proto = KernRe(r"^#\s*define\s+").sub("", prototype)
        if new_proto != prototype:
            is_define_proto = True
            prototype = new_proto
        else:
            is_define_proto = False

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

        name = r'[a-zA-Z0-9_~:]+'
        prototype_end1 = r'[^\(]*'
        prototype_end2 = r'[^\{]*'
        prototype_end = fr'\(({prototype_end1}|{prototype_end2})\)'

        # Besides compiling, Perl qr{[\w\s]+} works as a non-capturing group.
        # So, this needs to be mapped in Python with (?:...)? or (?:...)+

        type1 = r'(?:[\w\s]+)?'
        type2 = r'(?:[\w\s]+\*+)+'

        found = False

        if is_define_proto:
            r = KernRe(r'^()(' + name + r')\s+')

            if r.search(prototype):
                return_type = ''
                declaration_name = r.group(2)
                func_macro = True

                found = True

        if not found:
            patterns = [
                rf'^()({name})\s*{prototype_end}',
                rf'^({type1})\s+({name})\s*{prototype_end}',
                rf'^({type2})\s*({name})\s*{prototype_end}',
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
        if not found:
            self.emit_msg(ln,
                          f"cannot understand function prototype: '{prototype}'")
            return

        if self.entry.identifier != declaration_name:
            self.emit_msg(ln,
                          f"expecting prototype for {self.entry.identifier}(). Prototype was for {declaration_name}() instead")
            return

        prms = " ".join(self.entry.parameterlist)
        self.check_sections(ln, declaration_name, "function",
                            self.entry.sectcheck, prms)

        self.check_return_section(ln, declaration_name, return_type)

        if 'typedef' in return_type:
            self.output_declaration(decl_type, declaration_name,
                                    function=declaration_name,
                                    typedef=True,
                                    functiontype=return_type,
                                    parameterlist=self.entry.parameterlist,
                                    parameterdescs=self.entry.parameterdescs,
                                    parametertypes=self.entry.parametertypes,
                                    parameterdesc_start_lines=self.entry.parameterdesc_start_lines,
                                    sectionlist=self.entry.sectionlist,
                                    sections=self.entry.sections,
                                    section_start_lines=self.entry.section_start_lines,
                                    purpose=self.entry.declaration_purpose,
                                    func_macro=func_macro)
        else:
            self.output_declaration(decl_type, declaration_name,
                                    function=declaration_name,
                                    typedef=False,
                                    functiontype=return_type,
                                    parameterlist=self.entry.parameterlist,
                                    parameterdescs=self.entry.parameterdescs,
                                    parametertypes=self.entry.parametertypes,
                                    parameterdesc_start_lines=self.entry.parameterdesc_start_lines,
                                    sectionlist=self.entry.sectionlist,
                                    sections=self.entry.sections,
                                    section_start_lines=self.entry.section_start_lines,
                                    purpose=self.entry.declaration_purpose,
                                    func_macro=func_macro)

    def dump_typedef(self, ln, proto):
        """
        Stores a typedef inside self.entries array.
        """

        typedef_type = r'((?:\s+[\w\*]+\b){0,7}\s+(?:\w+\b|\*+))\s*'
        typedef_ident = r'\*?\s*(\w\S+)\s*'
        typedef_args = r'\s*\((.*)\);'

        typedef1 = KernRe(r'typedef' + typedef_type + r'\(' + typedef_ident + r'\)' + typedef_args)
        typedef2 = KernRe(r'typedef' + typedef_type + typedef_ident + typedef_args)

        # Strip comments
        proto = KernRe(r'/\*.*?\*/', flags=re.S).sub('', proto)

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

            decl_type = 'function'
            self.create_parameter_list(ln, decl_type, args, ',', declaration_name)

            self.output_declaration(decl_type, declaration_name,
                                    function=declaration_name,
                                    typedef=True,
                                    functiontype=return_type,
                                    parameterlist=self.entry.parameterlist,
                                    parameterdescs=self.entry.parameterdescs,
                                    parametertypes=self.entry.parametertypes,
                                    parameterdesc_start_lines=self.entry.parameterdesc_start_lines,
                                    sectionlist=self.entry.sectionlist,
                                    sections=self.entry.sections,
                                    section_start_lines=self.entry.section_start_lines,
                                    purpose=self.entry.declaration_purpose)
            return

        # Handle nested parentheses or brackets
        r = KernRe(r'(\(*.\)\s*|\[*.\]\s*);$')
        while r.search(proto):
            proto = r.sub('', proto)

        # Parse simple typedefs
        r = KernRe(r'typedef.*\s+(\w+)\s*;')
        if r.match(proto):
            declaration_name = r.group(1)

            if self.entry.identifier != declaration_name:
                self.emit_msg(ln,
                              f"expecting prototype for typedef {self.entry.identifier}. Prototype was for typedef {declaration_name} instead\n")
                return

            self.output_declaration('typedef', declaration_name,
                                    typedef=declaration_name,
                                    sectionlist=self.entry.sectionlist,
                                    sections=self.entry.sections,
                                    section_start_lines=self.entry.section_start_lines,
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

        # Note: it accepts only one EXPORT_SYMBOL* per line, as having
        # multiple export lines would violate Kernel coding style.

        if export_symbol.search(line):
            symbol = export_symbol.group(2)
            function_set.add(symbol)
            return

        if export_symbol_ns.search(line):
            symbol = export_symbol_ns.group(2)
            function_set.add(symbol)

    def process_normal(self, ln, line):
        """
        STATE_NORMAL: looking for the /** to begin everything.
        """

        if not doc_start.match(line):
            return

        # start a new entry
        self.reset_state(ln)
        self.entry.in_doc_sect = False

        # next line is always the function name
        self.state = state.NAME

    def process_name(self, ln, line):
        """
        STATE_NAME: Looking for the "name - description" line
        """

        if doc_block.search(line):
            self.entry.new_start_line = ln

            if not doc_block.group(1):
                self.entry.section = self.section_intro
            else:
                self.entry.section = doc_block.group(1)

            self.entry.identifier = self.entry.section
            self.state = state.DOCBLOCK
            return

        if doc_decl.search(line):
            self.entry.identifier = doc_decl.group(1)
            self.entry.is_kernel_comment = False

            decl_start = str(doc_com)       # comment block asterisk
            fn_type = r"(?:\w+\s*\*\s*)?"  # type (for non-functions)
            parenthesis = r"(?:\(\w*\))?"   # optional parenthesis on function
            decl_end = r"(?:[-:].*)"         # end of the name part

            # test for pointer declaration type, foo * bar() - desc
            r = KernRe(fr"^{decl_start}([\w\s]+?){parenthesis}?\s*{decl_end}?$")
            if r.search(line):
                self.entry.identifier = r.group(1)

            # Test for data declaration
            r = KernRe(r"^\s*\*?\s*(struct|union|enum|typedef)\b\s*(\w*)")
            if r.search(line):
                self.entry.decl_type = r.group(1)
                self.entry.identifier = r.group(2)
                self.entry.is_kernel_comment = True
            else:
                # Look for foo() or static void foo() - description;
                # or misspelt identifier

                r1 = KernRe(fr"^{decl_start}{fn_type}(\w+)\s*{parenthesis}\s*{decl_end}?$")
                r2 = KernRe(fr"^{decl_start}{fn_type}(\w+[^-:]*){parenthesis}\s*{decl_end}$")

                for r in [r1, r2]:
                    if r.search(line):
                        self.entry.identifier = r.group(1)
                        self.entry.decl_type = "function"

                        r = KernRe(r"define\s+")
                        self.entry.identifier = r.sub("", self.entry.identifier)
                        self.entry.is_kernel_comment = True
                        break

            self.entry.identifier = self.entry.identifier.strip(" ")

            self.state = state.BODY

            # if there's no @param blocks need to set up default section here
            self.entry.section = SECTION_DEFAULT
            self.entry.new_start_line = ln + 1

            r = KernRe("[-:](.*)")
            if r.search(line):
                # strip leading/trailing/multiple spaces
                self.entry.descr = r.group(1).strip(" ")

                r = KernRe(r"\s+")
                self.entry.descr = r.sub(" ", self.entry.descr)
                self.entry.declaration_purpose = self.entry.descr
                self.state = state.BODY_MAYBE
            else:
                self.entry.declaration_purpose = ""

            if not self.entry.is_kernel_comment:
                self.emit_msg(ln,
                              f"This comment starts with '/**', but isn't a kernel-doc comment. Refer Documentation/doc-guide/kernel-doc.rst\n{line}")
                self.state = state.NORMAL

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

            return

        # Failed to find an identifier. Emit a warning
        self.emit_msg(ln, f"Cannot find identifier on line:\n{line}")

    def process_body(self, ln, line):
        """
        STATE_BODY and STATE_BODY_MAYBE: the bulk of a kerneldoc comment.
        """

        if self.state == state.BODY_WITH_BLANK_LINE:
            r = KernRe(r"\s*\*\s?\S")
            if r.match(line):
                self.dump_section()
                self.entry.section = SECTION_DEFAULT
                self.entry.new_start_line = ln
                self.entry.contents = ""

        if doc_sect.search(line):
            self.entry.in_doc_sect = True
            newsection = doc_sect.group(1)

            if newsection.lower() in ["description", "context"]:
                newsection = newsection.title()

            # Special case: @return is a section, not a param description
            if newsection.lower() in ["@return", "@returns",
                                      "return", "returns"]:
                newsection = "Return"

            # Perl kernel-doc has a check here for contents before sections.
            # the logic there is always false, as in_doc_sect variable is
            # always true. So, just don't implement Wcontents_before_sections

            # .title()
            newcontents = doc_sect.group(2)
            if not newcontents:
                newcontents = ""

            if self.entry.contents.strip("\n"):
                self.dump_section()

            self.entry.new_start_line = ln
            self.entry.section = newsection
            self.entry.leading_space = None

            self.entry.contents = newcontents.lstrip()
            if self.entry.contents:
                self.entry.contents += "\n"

            self.state = state.BODY
            return

        if doc_end.search(line):
            self.dump_section()

            # Look for doc_com + <text> + doc_end:
            r = KernRe(r'\s*\*\s*[a-zA-Z_0-9:\.]+\*/')
            if r.match(line):
                self.emit_msg(ln, f"suspicious ending line: {line}")

            self.entry.prototype = ""
            self.entry.new_start_line = ln + 1

            self.state = state.PROTO
            return

        if doc_content.search(line):
            cont = doc_content.group(1)

            if cont == "":
                if self.entry.section == self.section_context:
                    self.dump_section()

                    self.entry.new_start_line = ln
                    self.state = state.BODY
                else:
                    if self.entry.section != SECTION_DEFAULT:
                        self.state = state.BODY_WITH_BLANK_LINE
                    else:
                        self.state = state.BODY

                    self.entry.contents += "\n"

            elif self.state == state.BODY_MAYBE:

                # Continued declaration purpose
                self.entry.declaration_purpose = self.entry.declaration_purpose.rstrip()
                self.entry.declaration_purpose += " " + cont

                r = KernRe(r"\s+")
                self.entry.declaration_purpose = r.sub(' ',
                                                       self.entry.declaration_purpose)

            else:
                if self.entry.section.startswith('@') or        \
                   self.entry.section == self.section_context:
                    if self.entry.leading_space is None:
                        r = KernRe(r'^(\s+)')
                        if r.match(cont):
                            self.entry.leading_space = len(r.group(1))
                        else:
                            self.entry.leading_space = 0

                    # Double-check if leading space are realy spaces
                    pos = 0
                    for i in range(0, self.entry.leading_space):
                        if cont[i] != " ":
                            break
                        pos += 1

                    cont = cont[pos:]

                    # NEW LOGIC:
                    # In case it is different, update it
                    if self.entry.leading_space != pos:
                        self.entry.leading_space = pos

                self.entry.contents += cont + "\n"
            return

        # Unknown line, ignore
        self.emit_msg(ln, f"bad line: {line}")

    def process_inline(self, ln, line):
        """STATE_INLINE: docbook comments within a prototype."""

        if self.inline_doc_state == state.INLINE_NAME and \
           doc_inline_sect.search(line):
            self.entry.section = doc_inline_sect.group(1)
            self.entry.new_start_line = ln

            self.entry.contents = doc_inline_sect.group(2).lstrip()
            if self.entry.contents != "":
                self.entry.contents += "\n"

            self.inline_doc_state = state.INLINE_TEXT
            # Documentation block end */
            return

        if doc_inline_end.search(line):
            if self.entry.contents not in ["", "\n"]:
                self.dump_section()

            self.state = state.PROTO
            self.inline_doc_state = state.INLINE_NA
            return

        if doc_content.search(line):
            if self.inline_doc_state == state.INLINE_TEXT:
                self.entry.contents += doc_content.group(1) + "\n"
                if not self.entry.contents.strip(" ").rstrip("\n"):
                    self.entry.contents = ""

            elif self.inline_doc_state == state.INLINE_NAME:
                self.emit_msg(ln,
                              f"Incorrect use of kernel-doc format: {line}")

                self.inline_doc_state = state.INLINE_ERROR

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
        r = KernRe(r"\/\/.*$", re.S)
        line = r.sub('', line)

        if KernRe(r'\s*#\s*define').match(line):
            self.entry.prototype = line
        elif line.startswith('#'):
            # Strip other macros like #ifdef/#ifndef/#endif/...
            pass
        else:
            r = KernRe(r'([^\{]*)')
            if r.match(line):
                self.entry.prototype += r.group(1) + " "

        if '{' in line or ';' in line or KernRe(r'\s*#\s*define').match(line):
            # strip comments
            r = KernRe(r'/\*.*?\*/')
            self.entry.prototype = r.sub('', self.entry.prototype)

            # strip newlines/cr's
            r = KernRe(r'[\r\n]+')
            self.entry.prototype = r.sub(' ', self.entry.prototype)

            # strip leading spaces
            r = KernRe(r'^\s+')
            self.entry.prototype = r.sub('', self.entry.prototype)

            # Handle self.entry.prototypes for function pointers like:
            #       int (*pcs_config)(struct foo)

            r = KernRe(r'^(\S+\s+)\(\s*\*(\S+)\)')
            self.entry.prototype = r.sub(r'\1\2', self.entry.prototype)

            if 'SYSCALL_DEFINE' in self.entry.prototype:
                self.entry.prototype = self.syscall_munge(ln,
                                                          self.entry.prototype)

            r = KernRe(r'TRACE_EVENT|DEFINE_EVENT|DEFINE_SINGLE_EVENT')
            if r.search(self.entry.prototype):
                self.entry.prototype = self.tracepoint_munge(ln,
                                                             self.entry.prototype)

            self.dump_function(ln, self.entry.prototype)
            self.reset_state(ln)

    def process_proto_type(self, ln, line):
        """Ancillary routine to process a type"""

        # Strip newlines/cr's.
        line = KernRe(r'[\r\n]+', re.S).sub(' ', line)

        # Strip leading spaces
        line = KernRe(r'^\s+', re.S).sub('', line)

        # Strip trailing spaces
        line = KernRe(r'\s+$', re.S).sub('', line)

        # Strip C99-style comments to the end of the line
        line = KernRe(r"\/\/.*$", re.S).sub('', line)

        # To distinguish preprocessor directive from regular declaration later.
        if line.startswith('#'):
            line += ";"

        r = KernRe(r'([^\{\};]*)([\{\};])(.*)')
        while True:
            if r.search(line):
                if self.entry.prototype:
                    self.entry.prototype += " "
                self.entry.prototype += r.group(1) + r.group(2)

                self.entry.brcount += r.group(2).count('{')
                self.entry.brcount -= r.group(2).count('}')

                self.entry.brcount = max(self.entry.brcount, 0)

                if r.group(2) == ';' and self.entry.brcount == 0:
                    self.dump_declaration(ln, self.entry.prototype)
                    self.reset_state(ln)
                    break

                line = r.group(3)
            else:
                self.entry.prototype += line
                break

    def process_proto(self, ln, line):
        """STATE_PROTO: reading a function/whatever prototype."""

        if doc_inline_oneline.search(line):
            self.entry.section = doc_inline_oneline.group(1)
            self.entry.contents = doc_inline_oneline.group(2)

            if self.entry.contents != "":
                self.entry.contents += "\n"
                self.dump_section(start_new=False)

        elif doc_inline_start.search(line):
            self.state = state.INLINE
            self.inline_doc_state = state.INLINE_NAME

        elif self.entry.decl_type == 'function':
            self.process_proto_function(ln, line)

        else:
            self.process_proto_type(ln, line)

    def process_docblock(self, ln, line):
        """STATE_DOCBLOCK: within a DOC: block."""

        if doc_end.search(line):
            self.dump_section()
            self.output_declaration("doc", self.entry.identifier,
                                    sectionlist=self.entry.sectionlist,
                                    sections=self.entry.sections,
                                    section_start_lines=self.entry.section_start_lines)
            self.reset_state(ln)

        elif doc_content.search(line):
            self.entry.contents += doc_content.group(1) + "\n"

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

    def parse_kdoc(self):
        """
        Open and process each line of a C source file.
        The parsing is controlled via a state machine, and the line is passed
        to a different process function depending on the state. The process
        function may update the state as needed.

        Besides parsing kernel-doc tags, it also parses export symbols.
        """

        cont = False
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
                            cont = True

                            if not prev_ln:
                                prev_ln = ln

                            continue

                        if cont:
                            ln = prev_ln
                            line = prev + line
                            prev = ""
                            cont = False
                            prev_ln = None

                    self.config.log.debug("%d %s%s: %s",
                                          ln, state.name[self.state],
                                          state.inline_name[self.inline_doc_state],
                                          line)

                    # This is an optimization over the original script.
                    # There, when export_file was used for the same file,
                    # it was read twice. Here, we use the already-existing
                    # loop to parse exported symbols as well.
                    #
                    # TODO: It should be noticed that not all states are
                    # needed here. On a future cleanup, process export only
                    # at the states that aren't handling comment markups.
                    self.process_export(export_table, line)

                    # Hand this line to the appropriate state handler
                    if self.state == state.NORMAL:
                        self.process_normal(ln, line)
                    elif self.state == state.NAME:
                        self.process_name(ln, line)
                    elif self.state in [state.BODY, state.BODY_MAYBE,
                                        state.BODY_WITH_BLANK_LINE]:
                        self.process_body(ln, line)
                    elif self.state == state.INLINE:  # scanning for inline parameters
                        self.process_inline(ln, line)
                    elif self.state == state.PROTO:
                        self.process_proto(ln, line)
                    elif self.state == state.DOCBLOCK:
                        self.process_docblock(ln, line)
        except OSError:
            self.config.log.error(f"Error: Cannot open file {self.fname}")

        return export_table, self.entries
