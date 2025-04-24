#!/usr/bin/env python3
# pylint: disable=R0902,R0903,R0911,R0912,R0913,R0914,R0915,R0917,C0302
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.
# SPDX-License-Identifier: GPL-2.0

"""
Parse ABI documentation and produce results from it.
"""

from argparse import Namespace
import logging
import os
import re

from pprint import pformat
from random import randrange, seed

# Import Python modules

from helpers import AbiDebug, ABI_DIR


class AbiParser:
    """Main class to parse ABI files"""

    TAGS = r"(what|where|date|kernelversion|contact|description|users)"
    XREF = r"(?:^|\s|\()(\/(?:sys|config|proc|dev|kvd)\/[^,.:;\)\s]+)(?:[,.:;\)\s]|\Z)"

    def __init__(self, directory, logger=None,
                 enable_lineno=False, show_warnings=True, debug=0):
        """Stores arguments for the class and initialize class vars"""

        self.directory = directory
        self.enable_lineno = enable_lineno
        self.show_warnings = show_warnings
        self.debug = debug

        if not logger:
            self.log = logging.getLogger("get_abi")
        else:
            self.log = logger

        self.data = {}
        self.what_symbols = {}
        self.file_refs = {}
        self.what_refs = {}

        # Ignore files that contain such suffixes
        self.ignore_suffixes = (".rej", ".org", ".orig", ".bak", "~")

        # Regular expressions used on parser
        self.re_abi_dir = re.compile(r"(.*)" + ABI_DIR)
        self.re_tag = re.compile(r"(\S+)(:\s*)(.*)", re.I)
        self.re_valid = re.compile(self.TAGS)
        self.re_start_spc = re.compile(r"(\s*)(\S.*)")
        self.re_whitespace = re.compile(r"^\s+")

        # Regular used on print
        self.re_what = re.compile(r"(\/?(?:[\w\-]+\/?){1,2})")
        self.re_escape = re.compile(r"([\.\x01-\x08\x0e-\x1f\x21-\x2f\x3a-\x40\x7b-\xff])")
        self.re_unprintable = re.compile(r"([\x00-\x2f\x3a-\x40\x5b-\x60\x7b-\xff]+)")
        self.re_title_mark = re.compile(r"\n[\-\*\=\^\~]+\n")
        self.re_doc = re.compile(r"Documentation/(?!devicetree)(\S+)\.rst")
        self.re_abi = re.compile(r"(Documentation/ABI/)([\w\/\-]+)")
        self.re_xref_node = re.compile(self.XREF)

    def warn(self, fdata, msg, extra=None):
        """Displays a parse error if warning is enabled"""

        if not self.show_warnings:
            return

        msg = f"{fdata.fname}:{fdata.ln}: {msg}"
        if extra:
            msg += "\n\t\t" + extra

        self.log.warning(msg)

    def add_symbol(self, what, fname, ln=None, xref=None):
        """Create a reference table describing where each 'what' is located"""

        if what not in self.what_symbols:
            self.what_symbols[what] = {"file": {}}

        if fname not in self.what_symbols[what]["file"]:
            self.what_symbols[what]["file"][fname] = []

        if ln and ln not in self.what_symbols[what]["file"][fname]:
            self.what_symbols[what]["file"][fname].append(ln)

        if xref:
            self.what_symbols[what]["xref"] = xref

    def _parse_line(self, fdata, line):
        """Parse a single line of an ABI file"""

        new_what = False
        new_tag = False
        content = None

        match = self.re_tag.match(line)
        if match:
            new = match.group(1).lower()
            sep = match.group(2)
            content = match.group(3)

            match = self.re_valid.search(new)
            if match:
                new_tag = match.group(1)
            else:
                if fdata.tag == "description":
                    # New "tag" is actually part of description.
                    # Don't consider it a tag
                    new_tag = False
                elif fdata.tag != "":
                    self.warn(fdata, f"tag '{fdata.tag}' is invalid", line)

        if new_tag:
            # "where" is Invalid, but was a common mistake. Warn if found
            if new_tag == "where":
                self.warn(fdata, "tag 'Where' is invalid. Should be 'What:' instead")
                new_tag = "what"

            if new_tag == "what":
                fdata.space = None

                if content not in self.what_symbols:
                    self.add_symbol(what=content, fname=fdata.fname, ln=fdata.ln)

                if fdata.tag == "what":
                    fdata.what.append(content.strip("\n"))
                else:
                    if fdata.key:
                        if "description" not in self.data.get(fdata.key, {}):
                            self.warn(fdata, f"{fdata.key} doesn't have a description")

                        for w in fdata.what:
                            self.add_symbol(what=w, fname=fdata.fname,
                                            ln=fdata.what_ln, xref=fdata.key)

                    fdata.label = content
                    new_what = True

                    key = "abi_" + content.lower()
                    fdata.key = self.re_unprintable.sub("_", key).strip("_")

                    # Avoid duplicated keys but using a defined seed, to make
                    # the namespace identical if there aren't changes at the
                    # ABI symbols
                    seed(42)

                    while fdata.key in self.data:
                        char = randrange(0, 51) + ord("A")
                        if char > ord("Z"):
                            char += ord("a") - ord("Z") - 1

                        fdata.key += chr(char)

                    if fdata.key and fdata.key not in self.data:
                        self.data[fdata.key] = {
                            "what": [content],
                            "file": [fdata.file_ref],
                            "path": fdata.ftype,
                            "line_no": fdata.ln,
                        }

                    fdata.what = self.data[fdata.key]["what"]

                self.what_refs[content] = fdata.key
                fdata.tag = new_tag
                fdata.what_ln = fdata.ln

                if fdata.nametag["what"]:
                    t = (content, fdata.key)
                    if t not in fdata.nametag["symbols"]:
                        fdata.nametag["symbols"].append(t)

                return

            if fdata.tag and new_tag:
                fdata.tag = new_tag

                if new_what:
                    fdata.label = ""

                    if "description" in self.data[fdata.key]:
                        self.data[fdata.key]["description"] += "\n\n"

                    if fdata.file_ref not in self.data[fdata.key]["file"]:
                        self.data[fdata.key]["file"].append(fdata.file_ref)

                    if self.debug == AbiDebug.WHAT_PARSING:
                        self.log.debug("what: %s", fdata.what)

                if not fdata.what:
                    self.warn(fdata, "'What:' should come first:", line)
                    return

                if new_tag == "description":
                    fdata.space = None

                    if content:
                        sep = sep.replace(":", " ")

                        c = " " * len(new_tag) + sep + content
                        c = c.expandtabs()

                        match = self.re_start_spc.match(c)
                        if match:
                            # Preserve initial spaces for the first line
                            fdata.space = match.group(1)
                            content = match.group(2) + "\n"

                self.data[fdata.key][fdata.tag] = content

            return

        # Store any contents before tags at the database
        if not fdata.tag and "what" in fdata.nametag:
            fdata.nametag["description"] += line
            return

        if fdata.tag == "description":
            content = line.expandtabs()

            if self.re_whitespace.sub("", content) == "":
                self.data[fdata.key][fdata.tag] += "\n"
                return

            if fdata.space is None:
                match = self.re_start_spc.match(content)
                if match:
                    # Preserve initial spaces for the first line
                    fdata.space = match.group(1)

                    content = match.group(2) + "\n"
            else:
                if content.startswith(fdata.space):
                    content = content[len(fdata.space):]

                else:
                    fdata.space = ""

            if fdata.tag == "what":
                w = content.strip("\n")
                if w:
                    self.data[fdata.key][fdata.tag].append(w)
            else:
                self.data[fdata.key][fdata.tag] += content
            return

        content = line.strip()
        if fdata.tag:
            if fdata.tag == "what":
                w = content.strip("\n")
                if w:
                    self.data[fdata.key][fdata.tag].append(w)
            else:
                self.data[fdata.key][fdata.tag] += "\n" + content.rstrip("\n")
            return

        # Everything else is error
        if content:
            self.warn(fdata, "Unexpected content", line)

    def parse_readme(self, nametag, fname):
        """Parse ABI README file"""

        nametag["what"] = ["Introduction"]
        nametag["path"] = "README"
        with open(fname, "r", encoding="utf8", errors="backslashreplace") as fp:
            for line in fp:
                match = self.re_tag.match(line)
                if match:
                    new = match.group(1).lower()

                    match = self.re_valid.search(new)
                    if match:
                        nametag["description"] += "\n:" + line
                        continue

                nametag["description"] += line

    def parse_file(self, fname, path, basename):
        """Parse a single file"""

        ref = f"abi_file_{path}_{basename}"
        ref = self.re_unprintable.sub("_", ref).strip("_")

        # Store per-file state into a namespace variable. This will be used
        # by the per-line parser state machine and by the warning function.
        fdata = Namespace

        fdata.fname = fname
        fdata.name = basename

        pos = fname.find(ABI_DIR)
        if pos > 0:
            f = fname[pos:]
        else:
            f = fname

        fdata.file_ref = (f, ref)
        self.file_refs[f] = ref

        fdata.ln = 0
        fdata.what_ln = 0
        fdata.tag = ""
        fdata.label = ""
        fdata.what = []
        fdata.key = None
        fdata.xrefs = None
        fdata.space = None
        fdata.ftype = path.split("/")[0]

        fdata.nametag = {}
        fdata.nametag["what"] = [f"ABI file {path}/{basename}"]
        fdata.nametag["type"] = "File"
        fdata.nametag["path"] = fdata.ftype
        fdata.nametag["file"] = [fdata.file_ref]
        fdata.nametag["line_no"] = 1
        fdata.nametag["description"] = ""
        fdata.nametag["symbols"] = []

        self.data[ref] = fdata.nametag

        if self.debug & AbiDebug.WHAT_OPEN:
            self.log.debug("Opening file %s", fname)

        if basename == "README":
            self.parse_readme(fdata.nametag, fname)
            return

        with open(fname, "r", encoding="utf8", errors="backslashreplace") as fp:
            for line in fp:
                fdata.ln += 1

                self._parse_line(fdata, line)

            if "description" in fdata.nametag:
                fdata.nametag["description"] = fdata.nametag["description"].lstrip("\n")

            if fdata.key:
                if "description" not in self.data.get(fdata.key, {}):
                    self.warn(fdata, f"{fdata.key} doesn't have a description")

                for w in fdata.what:
                    self.add_symbol(what=w, fname=fname, xref=fdata.key)

    def _parse_abi(self, root=None):
        """Internal function to parse documentation ABI recursively"""

        if not root:
            root = self.directory

        with os.scandir(root) as obj:
            for entry in obj:
                name = os.path.join(root, entry.name)

                if entry.is_dir():
                    self._parse_abi(name)
                    continue

                if not entry.is_file():
                    continue

                basename = os.path.basename(name)

                if basename.startswith("."):
                    continue

                if basename.endswith(self.ignore_suffixes):
                    continue

                path = self.re_abi_dir.sub("", os.path.dirname(name))

                self.parse_file(name, path, basename)

    def parse_abi(self, root=None):
        """Parse documentation ABI"""

        self._parse_abi(root)

        if self.debug & AbiDebug.DUMP_ABI_STRUCTS:
            self.log.debug(pformat(self.data))

    def desc_txt(self, desc):
        """Print description as found inside ABI files"""

        desc = desc.strip(" \t\n")

        return desc + "\n\n"

    def xref(self, fname):
        """
        Converts a Documentation/ABI + basename into a ReST cross-reference
        """

        xref = self.file_refs.get(fname)
        if not xref:
            return None
        else:
            return xref

    def desc_rst(self, desc):
        """Enrich ReST output by creating cross-references"""

        # Remove title markups from the description
        # Having titles inside ABI files will only work if extra
        # care would be taken in order to strictly follow the same
        # level order for each markup.
        desc = self.re_title_mark.sub("\n\n", "\n" + desc)
        desc = desc.rstrip(" \t\n").lstrip("\n")

        # Python's regex performance for non-compiled expressions is a lot
        # than Perl, as Perl automatically caches them at their
        # first usage. Here, we'll need to do the same, as otherwise the
        # performance penalty is be high

        new_desc = ""
        for d in desc.split("\n"):
            if d == "":
                new_desc += "\n"
                continue

            # Use cross-references for doc files where needed
            d = self.re_doc.sub(r":doc:`/\1`", d)

            # Use cross-references for ABI generated docs where needed
            matches = self.re_abi.findall(d)
            for m in matches:
                abi = m[0] + m[1]

                xref = self.file_refs.get(abi)
                if not xref:
                    # This may happen if ABI is on a separate directory,
                    # like parsing ABI testing and symbol is at stable.
                    # The proper solution is to move this part of the code
                    # for it to be inside sphinx/kernel_abi.py
                    self.log.info("Didn't find ABI reference for '%s'", abi)
                else:
                    new = self.re_escape.sub(r"\\\1", m[1])
                    d = re.sub(fr"\b{abi}\b", f":ref:`{new} <{xref}>`", d)

            # Seek for cross reference symbols like /sys/...
            # Need to be careful to avoid doing it on a code block
            if d[0] not in [" ", "\t"]:
                matches = self.re_xref_node.findall(d)
                for m in matches:
                    # Finding ABI here is more complex due to wildcards
                    xref = self.what_refs.get(m)
                    if xref:
                        new = self.re_escape.sub(r"\\\1", m)
                        d = re.sub(fr"\b{m}\b", f":ref:`{new} <{xref}>`", d)

            new_desc += d + "\n"

        return new_desc + "\n\n"

    def doc(self, output_in_txt=False, show_symbols=True, show_file=True,
            filter_path=None):
        """Print ABI at stdout"""

        part = None
        for key, v in sorted(self.data.items(),
                             key=lambda x: (x[1].get("type", ""),
                                            x[1].get("what"))):

            wtype = v.get("type", "Symbol")
            file_ref = v.get("file")
            names = v.get("what", [""])

            if wtype == "File":
                if not show_file:
                    continue
            else:
                if not show_symbols:
                    continue

            if filter_path:
                if v.get("path") != filter_path:
                    continue

            msg = ""

            if wtype != "File":
                cur_part = names[0]
                if cur_part.find("/") >= 0:
                    match = self.re_what.match(cur_part)
                    if match:
                        symbol = match.group(1).rstrip("/")
                        cur_part = "Symbols under " + symbol

                if cur_part and cur_part != part:
                    part = cur_part
                    msg += part + "\n"+ "-" * len(part) +"\n\n"

                msg += f".. _{key}:\n\n"

                max_len = 0
                for i in range(0, len(names)):           # pylint: disable=C0200
                    names[i] = "**" + self.re_escape.sub(r"\\\1", names[i]) + "**"

                    max_len = max(max_len, len(names[i]))

                msg += "+-" + "-" * max_len + "-+\n"
                for name in names:
                    msg += f"| {name}" + " " * (max_len - len(name)) + " |\n"
                    msg += "+-" + "-" * max_len + "-+\n"
                msg += "\n"

            for ref in file_ref:
                if wtype == "File":
                    msg += f".. _{ref[1]}:\n\n"
                else:
                    base = os.path.basename(ref[0])
                    msg += f"Defined on file :ref:`{base} <{ref[1]}>`\n\n"

            if wtype == "File":
                msg += names[0] +"\n" + "-" * len(names[0]) +"\n\n"

            desc = v.get("description")
            if not desc and wtype != "File":
                msg += f"DESCRIPTION MISSING for {names[0]}\n\n"

            if desc:
                if output_in_txt:
                    msg += self.desc_txt(desc)
                else:
                    msg += self.desc_rst(desc)

            symbols = v.get("symbols")
            if symbols:
                msg += "Has the following ABI:\n\n"

                for w, label in symbols:
                    # Escape special chars from content
                    content = self.re_escape.sub(r"\\\1", w)

                    msg += f"- :ref:`{content} <{label}>`\n\n"

            users = v.get("users")
            if users and users.strip(" \t\n"):
                users = users.strip("\n").replace('\n', '\n\t')
                msg += f"Users:\n\t{users}\n\n"

            ln = v.get("line_no", 1)

            yield (msg, file_ref[0][0], ln)

    def check_issues(self):
        """Warn about duplicated ABI entries"""

        for what, v in self.what_symbols.items():
            files = v.get("file")
            if not files:
                # Should never happen if the parser works properly
                self.log.warning("%s doesn't have a file associated", what)
                continue

            if len(files) == 1:
                continue

            f = []
            for fname, lines in sorted(files.items()):
                if not lines:
                    f.append(f"{fname}")
                elif len(lines) == 1:
                    f.append(f"{fname}:{lines[0]}")
                else:
                    m = fname + "lines "
                    m += ", ".join(str(x) for x in lines)
                    f.append(m)

            self.log.warning("%s is defined %d times: %s", what, len(f), "; ".join(f))

    def search_symbols(self, expr):
        """ Searches for ABI symbols """

        regex = re.compile(expr, re.I)

        found_keys = 0
        for t in sorted(self.data.items(), key=lambda x: [0]):
            v = t[1]

            wtype = v.get("type", "")
            if wtype == "File":
                continue

            for what in v.get("what", [""]):
                if regex.search(what):
                    found_keys += 1

                    kernelversion = v.get("kernelversion", "").strip(" \t\n")
                    date = v.get("date", "").strip(" \t\n")
                    contact = v.get("contact", "").strip(" \t\n")
                    users = v.get("users", "").strip(" \t\n")
                    desc = v.get("description", "").strip(" \t\n")

                    files = []
                    for f in v.get("file", ()):
                        files.append(f[0])

                    what = str(found_keys) + ". " + what
                    title_tag = "-" * len(what)

                    print(f"\n{what}\n{title_tag}\n")

                    if kernelversion:
                        print(f"Kernel version:\t\t{kernelversion}")

                    if date:
                        print(f"Date:\t\t\t{date}")

                    if contact:
                        print(f"Contact:\t\t{contact}")

                    if users:
                        print(f"Users:\t\t\t{users}")

                    print("Defined on file(s):\t" + ", ".join(files))

                    if desc:
                        desc = desc.strip("\n")
                        print(f"\n{desc}\n")

        if not found_keys:
            print(f"Regular expression /{expr}/ not found.")
