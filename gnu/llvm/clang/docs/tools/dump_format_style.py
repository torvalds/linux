#!/usr/bin/env python3
# A tool to parse the FormatStyle struct from Format.h and update the
# documentation in ../ClangFormatStyleOptions.rst automatically.
# Run from the directory in which this file is located to update the docs.

import inspect
import os
import re
import sys
from io import TextIOWrapper
from typing import Set

CLANG_DIR = os.path.join(os.path.dirname(__file__), "../..")
FORMAT_STYLE_FILE = os.path.join(CLANG_DIR, "include/clang/Format/Format.h")
INCLUDE_STYLE_FILE = os.path.join(
    CLANG_DIR, "include/clang/Tooling/Inclusions/IncludeStyle.h"
)
DOC_FILE = os.path.join(CLANG_DIR, "docs/ClangFormatStyleOptions.rst")

PLURALS_FILE = os.path.join(os.path.dirname(__file__), "plurals.txt")

plurals: Set[str] = set()
with open(PLURALS_FILE, "a+") as f:
    f.seek(0)
    plurals = set(f.read().splitlines())


def substitute(text, tag, contents):
    replacement = "\n.. START_%s\n\n%s\n\n.. END_%s\n" % (tag, contents, tag)
    pattern = r"\n\.\. START_%s\n.*\n\.\. END_%s\n" % (tag, tag)
    return re.sub(pattern, "%s", text, flags=re.S) % replacement


def register_plural(singular: str, plural: str):
    if plural not in plurals:
        if not hasattr(register_plural, "generated_new_plural"):
            print(
                "Plural generation: you can use "
                f"`git checkout -- {os.path.relpath(PLURALS_FILE)}` "
                "to reemit warnings or `git add` to include new plurals\n"
            )
        register_plural.generated_new_plural = True

        plurals.add(plural)
        with open(PLURALS_FILE, "a") as f:
            f.write(plural + "\n")
        cf = inspect.currentframe()
        lineno = ""
        if cf and cf.f_back:
            lineno = ":" + str(cf.f_back.f_lineno)
        print(
            f"{__file__}{lineno} check if plural of {singular} is {plural}",
            file=sys.stderr,
        )
    return plural


def pluralize(word: str):
    lword = word.lower()
    if len(lword) >= 2 and lword[-1] == "y" and lword[-2] not in "aeiou":
        return register_plural(word, word[:-1] + "ies")
    elif lword.endswith(("s", "sh", "ch", "x", "z")):
        return register_plural(word, word[:-1] + "es")
    elif lword.endswith("fe"):
        return register_plural(word, word[:-2] + "ves")
    elif lword.endswith("f") and not lword.endswith("ff"):
        return register_plural(word, word[:-1] + "ves")
    else:
        return register_plural(word, word + "s")


def to_yaml_type(typestr: str):
    if typestr == "bool":
        return "Boolean"
    elif typestr == "int":
        return "Integer"
    elif typestr == "unsigned":
        return "Unsigned"
    elif typestr == "std::string":
        return "String"

    match = re.match(r"std::vector<(.*)>$", typestr)
    if match:
        return "List of " + pluralize(to_yaml_type(match.group(1)))

    match = re.match(r"std::optional<(.*)>$", typestr)
    if match:
        return to_yaml_type(match.group(1))

    return typestr


def doxygen2rst(text):
    text = re.sub(r"<tt>\s*(.*?)\s*<\/tt>", r"``\1``", text)
    text = re.sub(r"\\c ([^ ,;\.]+)", r"``\1``", text)
    text = re.sub(r"\\\w+ ", "", text)
    return text


def indent(text, columns, indent_first_line=True):
    indent_str = " " * columns
    s = re.sub(r"\n([^\n])", "\n" + indent_str + "\\1", text, flags=re.S)
    if not indent_first_line or s.startswith("\n"):
        return s
    return indent_str + s


class Option(object):
    def __init__(self, name, opt_type, comment, version):
        self.name = name
        self.type = opt_type
        self.comment = comment.strip()
        self.enum = None
        self.nested_struct = None
        self.version = version

    def __str__(self):
        s = ".. _%s:\n\n**%s** (``%s``) " % (
            self.name,
            self.name,
            to_yaml_type(self.type),
        )
        if self.version:
            s += ":versionbadge:`clang-format %s` " % self.version
        s += ":ref:`¶ <%s>`\n%s" % (self.name, doxygen2rst(indent(self.comment, 2)))
        if self.enum and self.enum.values:
            s += indent("\n\nPossible values:\n\n%s\n" % self.enum, 2)
        if self.nested_struct:
            s += indent(
                "\n\nNested configuration flags:\n\n%s\n" % self.nested_struct, 2
            )
            s = s.replace("<option-name>", self.name)
        return s


class NestedStruct(object):
    def __init__(self, name, comment):
        self.name = name
        self.comment = comment.strip()
        self.values = []

    def __str__(self):
        return self.comment + "\n" + "\n".join(map(str, self.values))


class NestedField(object):
    def __init__(self, name, comment, version):
        self.name = name
        self.comment = comment.strip()
        self.version = version

    def __str__(self):
        if self.version:
            return "\n* ``%s`` :versionbadge:`clang-format %s`\n%s" % (
                self.name,
                self.version,
                doxygen2rst(indent(self.comment, 2, indent_first_line=False)),
            )
        return "\n* ``%s`` %s" % (
            self.name,
            doxygen2rst(indent(self.comment, 2, indent_first_line=False)),
        )


class Enum(object):
    def __init__(self, name, comment):
        self.name = name
        self.comment = comment.strip()
        self.values = []

    def __str__(self):
        return "\n".join(map(str, self.values))


class NestedEnum(object):
    def __init__(self, name, enumtype, comment, version, values):
        self.name = name
        self.comment = comment
        self.values = values
        self.type = enumtype
        self.version = version

    def __str__(self):
        s = ""
        if self.version:
            s = "\n* ``%s %s`` :versionbadge:`clang-format %s`\n\n%s" % (
                to_yaml_type(self.type),
                self.name,
                self.version,
                doxygen2rst(indent(self.comment, 2)),
            )
        else:
            s = "\n* ``%s %s``\n%s" % (
                to_yaml_type(self.type),
                self.name,
                doxygen2rst(indent(self.comment, 2)),
            )
        s += indent("\nPossible values:\n\n", 2)
        s += indent("\n".join(map(str, self.values)), 2)
        return s


class EnumValue(object):
    def __init__(self, name, comment, config):
        self.name = name
        self.comment = comment
        self.config = config

    def __str__(self):
        return "* ``%s`` (in configuration: ``%s``)\n%s" % (
            self.name,
            re.sub(".*_", "", self.config),
            doxygen2rst(indent(self.comment, 2)),
        )


class OptionsReader:
    def __init__(self, header: TextIOWrapper):
        self.header = header
        self.in_code_block = False
        self.code_indent = 0
        self.lineno = 0
        self.last_err_lineno = -1

    def __file_path(self):
        return os.path.relpath(self.header.name)

    def __print_line(self, line: str):
        print(f"{self.lineno:>6} | {line}", file=sys.stderr)

    def __warning(self, msg: str, line: str):
        print(f"{self.__file_path()}:{self.lineno}: warning: {msg}:", file=sys.stderr)
        self.__print_line(line)

    def __clean_comment_line(self, line: str):
        match = re.match(r"^/// (?P<indent> +)?\\code(\{.(?P<lang>\w+)\})?$", line)
        if match:
            if self.in_code_block:
                self.__warning("`\\code` in another `\\code`", line)
            self.in_code_block = True
            indent_str = match.group("indent")
            if not indent_str:
                indent_str = ""
            self.code_indent = len(indent_str)
            lang = match.group("lang")
            if not lang:
                lang = "c++"
            return f"\n{indent_str}.. code-block:: {lang}\n\n"

        endcode_match = re.match(r"^/// +\\endcode$", line)
        if endcode_match:
            if not self.in_code_block:
                self.__warning(
                    "no correct `\\code` found before this `\\endcode`", line
                )
            self.in_code_block = False
            return ""

        # check code block indentation
        if (
            self.in_code_block
            and not line == "///"
            and not line.startswith("///  " + " " * self.code_indent)
        ):
            if self.last_err_lineno == self.lineno - 1:
                self.__print_line(line)
            else:
                self.__warning("code block should be indented", line)
            self.last_err_lineno = self.lineno

        match = re.match(r"^/// \\warning$", line)
        if match:
            return "\n.. warning::\n\n"

        endwarning_match = re.match(r"^/// +\\endwarning$", line)
        if endwarning_match:
            return ""

        match = re.match(r"^/// \\note$", line)
        if match:
            return "\n.. note::\n\n"

        endnote_match = re.match(r"^/// +\\endnote$", line)
        if endnote_match:
            return ""
        return line[4:] + "\n"

    def read_options(self):
        class State:
            (
                BeforeStruct,
                Finished,
                InStruct,
                InNestedStruct,
                InNestedFieldComment,
                InFieldComment,
                InEnum,
                InEnumMemberComment,
                InNestedEnum,
                InNestedEnumMemberComment,
            ) = range(10)

        state = State.BeforeStruct

        options = []
        enums = {}
        nested_structs = {}
        comment = ""
        enum = None
        nested_struct = None
        version = None
        deprecated = False

        for line in self.header:
            self.lineno += 1
            line = line.strip()
            if state == State.BeforeStruct:
                if line in ("struct FormatStyle {", "struct IncludeStyle {"):
                    state = State.InStruct
            elif state == State.InStruct:
                if line.startswith("///"):
                    state = State.InFieldComment
                    comment = self.__clean_comment_line(line)
                elif line == "};":
                    state = State.Finished
                    break
            elif state == State.InFieldComment:
                if line.startswith(r"/// \version"):
                    match = re.match(r"/// \\version\s*(?P<version>[0-9.]+)*", line)
                    if match:
                        version = match.group("version")
                elif line.startswith("/// @deprecated"):
                    deprecated = True
                elif line.startswith("///"):
                    comment += self.__clean_comment_line(line)
                elif line.startswith("enum"):
                    state = State.InEnum
                    name = re.sub(r"enum\s+(\w+)\s*(:((\s*\w+)+)\s*)?\{", "\\1", line)
                    enum = Enum(name, comment)
                elif line.startswith("struct"):
                    state = State.InNestedStruct
                    name = re.sub(r"struct\s+(\w+)\s*\{", "\\1", line)
                    nested_struct = NestedStruct(name, comment)
                elif line.endswith(";"):
                    prefix = "// "
                    if line.startswith(prefix):
                        line = line[len(prefix) :]
                    state = State.InStruct
                    field_type, field_name = re.match(
                        r"([<>:\w(,\s)]+)\s+(\w+);", line
                    ).groups()
                    if deprecated:
                        field_type = "deprecated"
                        deprecated = False

                    if not version:
                        self.__warning(f"missing version for {field_name}", line)
                    option = Option(str(field_name), str(field_type), comment, version)
                    options.append(option)
                    version = None
                else:
                    raise Exception(
                        "Invalid format, expected comment, field or enum\n" + line
                    )
            elif state == State.InNestedStruct:
                if line.startswith("///"):
                    state = State.InNestedFieldComment
                    comment = self.__clean_comment_line(line)
                elif line == "};":
                    state = State.InStruct
                    nested_structs[nested_struct.name] = nested_struct
            elif state == State.InNestedFieldComment:
                if line.startswith(r"/// \version"):
                    match = re.match(r"/// \\version\s*(?P<version>[0-9.]+)*", line)
                    if match:
                        version = match.group("version")
                elif line.startswith("///"):
                    comment += self.__clean_comment_line(line)
                elif line.startswith("enum"):
                    state = State.InNestedEnum
                    name = re.sub(r"enum\s+(\w+)\s*(:((\s*\w+)+)\s*)?\{", "\\1", line)
                    enum = Enum(name, comment)
                else:
                    state = State.InNestedStruct
                    field_type, field_name = re.match(
                        r"([<>:\w(,\s)]+)\s+(\w+);", line
                    ).groups()
                    # if not version:
                    #    self.__warning(f"missing version for {field_name}", line)
                    if field_type in enums:
                        nested_struct.values.append(
                            NestedEnum(
                                field_name,
                                field_type,
                                comment,
                                version,
                                enums[field_type].values,
                            )
                        )
                    else:
                        nested_struct.values.append(
                            NestedField(field_type + " " + field_name, comment, version)
                        )
                    version = None
            elif state == State.InEnum:
                if line.startswith("///"):
                    state = State.InEnumMemberComment
                    comment = self.__clean_comment_line(line)
                elif line == "};":
                    state = State.InStruct
                    enums[enum.name] = enum
                else:
                    # Enum member without documentation. Must be documented where the enum
                    # is used.
                    pass
            elif state == State.InNestedEnum:
                if line.startswith("///"):
                    state = State.InNestedEnumMemberComment
                    comment = self.__clean_comment_line(line)
                elif line == "};":
                    state = State.InNestedStruct
                    enums[enum.name] = enum
                else:
                    # Enum member without documentation. Must be
                    # documented where the enum is used.
                    pass
            elif state == State.InEnumMemberComment:
                if line.startswith("///"):
                    comment += self.__clean_comment_line(line)
                else:
                    state = State.InEnum
                    val = line.replace(",", "")
                    pos = val.find(" // ")
                    if pos != -1:
                        config = val[pos + 4 :]
                        val = val[:pos]
                    else:
                        config = val
                    enum.values.append(EnumValue(val, comment, config))
            elif state == State.InNestedEnumMemberComment:
                if line.startswith("///"):
                    comment += self.__clean_comment_line(line)
                else:
                    state = State.InNestedEnum
                    val = line.replace(",", "")
                    pos = val.find(" // ")
                    if pos != -1:
                        config = val[pos + 4 :]
                        val = val[:pos]
                    else:
                        config = val
                    enum.values.append(EnumValue(val, comment, config))
        if state != State.Finished:
            raise Exception("Not finished by the end of file")

        for option in options:
            if option.type not in [
                "bool",
                "unsigned",
                "int",
                "std::string",
                "std::vector<std::string>",
                "std::vector<IncludeCategory>",
                "std::vector<RawStringFormat>",
                "std::optional<unsigned>",
                "deprecated",
            ]:
                if option.type in enums:
                    option.enum = enums[option.type]
                elif option.type in nested_structs:
                    option.nested_struct = nested_structs[option.type]
                else:
                    raise Exception("Unknown type: %s" % option.type)
        return options


with open(FORMAT_STYLE_FILE) as f:
    opts = OptionsReader(f).read_options()
with open(INCLUDE_STYLE_FILE) as f:
    opts += OptionsReader(f).read_options()

opts = sorted(opts, key=lambda x: x.name)
options_text = "\n\n".join(map(str, opts))

with open(DOC_FILE, encoding="utf-8") as f:
    contents = f.read()

contents = substitute(contents, "FORMAT_STYLE_OPTIONS", options_text)

with open(DOC_FILE, "wb") as output:
    output.write(contents.encode())
