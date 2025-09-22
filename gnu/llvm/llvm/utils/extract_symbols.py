#!/usr/bin/env python

"""A tool for extracting a list of symbols to export

When exporting symbols from a dll or exe we either need to mark the symbols in
the source code as __declspec(dllexport) or supply a list of symbols to the
linker. This program automates the latter by inspecting the symbol tables of a
list of link inputs and deciding which of those symbols need to be exported.

We can't just export all the defined symbols, as there's a limit of 65535
exported symbols and in clang we go way over that, particularly in a debug
build. Therefore a large part of the work is pruning symbols either which can't
be imported, or which we think are things that have definitions in public header
files (i.e. template instantiations) and we would get defined in the thing
importing these symbols anyway.
"""

from __future__ import print_function
import sys
import re
import os
import subprocess
import multiprocessing
import argparse
import platform

# Define a function which extracts a list of pairs of (symbols, is_def) from a
# library using llvm-nm becuase it can work both with regular and bitcode files.
# We use subprocess.Popen and yield a symbol at a time instead of using
# subprocess.check_output and returning a list as, especially on Windows, waiting
# for the entire output to be ready can take a significant amount of time.
def nm_get_symbols(tool, lib):
    # '-P' means the output is in portable format,
    # '-g' means we only get global symbols,
    # '-Xany' enforce handling both 32- and 64-bit objects on AIX,
    # '--no-demangle' ensure that C++ symbol names are not demangled; note
    #   that llvm-nm do not demangle by default, but the system nm on AIX does
    #   that, so the behavior may change in the future,
    # '-p' do not waste time sorting the symbols.
    cmd = [tool, "-P", "-g", "-Xany", "--no-demangle", "-p"]
    process = subprocess.Popen(
        cmd + [lib],
        bufsize=1,
        stdout=subprocess.PIPE,
        stdin=subprocess.PIPE,
        universal_newlines=True,
    )
    process.stdin.close()
    for line in process.stdout:
        # Look for external symbols that are defined in some section
        # The POSIX format is:
        #   name   type   value   size
        # The -P flag displays the size field for symbols only when applicable,
        # so the last field is optional. There's no space after the value field,
        # but \s+ match newline also, so \s+\S* will match the optional size field.
        match = re.match("^(\S+)\s+[BDGRSTuVW]\s+\S+\s+\S*$", line)
        if match:
            yield (match.group(1), True)
        # Look for undefined symbols, which have type U and may or may not
        # (depending on which nm is being used) have value and size.
        match = re.match("^(\S+)\s+U\s+(\S+\s+\S*)?$", line)
        if match:
            yield (match.group(1), False)
    process.wait()


# Define a function which determines if the target is 32-bit Windows (as that's
# where calling convention name decoration happens).
def readobj_is_32bit_windows(tool, lib):
    output = subprocess.check_output(
        [tool, "--file-header", lib], universal_newlines=True
    )
    for line in output.splitlines():
        match = re.match("Format: (\S+)", line)
        if match:
            return match.group(1) == "COFF-i386"
    return False


# MSVC mangles names to ?<identifier_mangling>@<type_mangling>. By examining the
# identifier/type mangling we can decide which symbols could possibly be
# required and which we can discard.
def should_keep_microsoft_symbol(symbol, calling_convention_decoration):
    # Keep unmangled (i.e. extern "C") names
    if not "?" in symbol:
        if calling_convention_decoration:
            # Remove calling convention decoration from names
            match = re.match("[_@]([^@]+)", symbol)
            if match:
                symbol = match.group(1)
        # Discard floating point/SIMD constants.
        if symbol.startswith(("__xmm@", "__ymm@", "__real@")):
            return None
        return symbol
    # Deleting destructors start with ?_G or ?_E and can be discarded because
    # link.exe gives you a warning telling you they can't be exported if you
    # don't
    elif symbol.startswith("??_G") or symbol.startswith("??_E"):
        return None
    # An anonymous namespace is mangled as ?A(maybe hex number)@. Any symbol
    # that mentions an anonymous namespace can be discarded, as the anonymous
    # namespace doesn't exist outside of that translation unit.
    elif re.search("\?A(0x\w+)?@", symbol):
        return None
    # Skip X86GenMnemonicTables functions, they are not exposed from llvm/include/.
    elif re.match("\?is[A-Z0-9]*@X86@llvm", symbol):
        return None
    # Keep mangled llvm:: and clang:: function symbols. How we detect these is a
    # bit of a mess and imprecise, but that avoids having to completely demangle
    # the symbol name. The outermost namespace is at the end of the identifier
    # mangling, and the identifier mangling is followed by the type mangling, so
    # we look for (llvm|clang)@@ followed by something that looks like a
    # function type mangling. To spot a function type we use (this is derived
    # from clang/lib/AST/MicrosoftMangle.cpp):
    # <function-type> ::= <function-class> <this-cvr-qualifiers>
    #                     <calling-convention> <return-type>
    #                     <argument-list> <throw-spec>
    # <function-class> ::= [A-Z]
    # <this-cvr-qualifiers> ::= [A-Z0-9_]*
    # <calling-convention> ::= [A-JQ]
    # <return-type> ::= .+
    # <argument-list> ::= X   (void)
    #                 ::= .+@ (list of types)
    #                 ::= .*Z (list of types, varargs)
    # <throw-spec> ::= exceptions are not allowed
    elif re.search("(llvm|clang)@@[A-Z][A-Z0-9_]*[A-JQ].+(X|.+@|.*Z)$", symbol):
        return symbol
    return None


# Itanium manglings are of the form _Z<identifier_mangling><type_mangling>. We
# demangle the identifier mangling to identify symbols that can be safely
# discarded.
def should_keep_itanium_symbol(symbol, calling_convention_decoration):
    # Start by removing any calling convention decoration (which we expect to
    # see on all symbols, even mangled C++ symbols)
    if calling_convention_decoration and symbol.startswith("_"):
        symbol = symbol[1:]
    # Keep unmangled names
    if not symbol.startswith("_") and not symbol.startswith("."):
        return symbol
    # Discard manglings that aren't nested names
    match = re.match("\.?_Z(T[VTIS])?(N.+)", symbol)
    if not match:
        return None
    # Demangle the name. If the name is too complex then we don't need to keep
    # it, but it the demangling fails then keep the symbol just in case.
    try:
        names, _ = parse_itanium_nested_name(match.group(2))
    except TooComplexName:
        return None
    if not names:
        return symbol
    # Keep llvm:: and clang:: names
    elif names[0][0] == "4llvm" or names[0][0] == "5clang":
        return symbol
    # Discard everything else
    else:
        return None


# Certain kinds of complex manglings we assume cannot be part of a public
# interface, and we handle them by raising an exception.
class TooComplexName(Exception):
    pass


# Parse an itanium mangled name from the start of a string and return a
# (name, rest of string) pair.
def parse_itanium_name(arg):
    # Check for a normal name
    match = re.match("(\d+)(.+)", arg)
    if match:
        n = int(match.group(1))
        name = match.group(1) + match.group(2)[:n]
        rest = match.group(2)[n:]
        return name, rest
    # Check for constructor/destructor names
    match = re.match("([CD][123])(.+)", arg)
    if match:
        return match.group(1), match.group(2)
    # Assume that a sequence of characters that doesn't end a nesting is an
    # operator (this is very imprecise, but appears to be good enough)
    match = re.match("([^E]+)(.+)", arg)
    if match:
        return match.group(1), match.group(2)
    # Anything else: we can't handle it
    return None, arg


# Parse an itanium mangled template argument list from the start of a string
# and throw it away, returning the rest of the string.
def skip_itanium_template(arg):
    # A template argument list starts with I
    assert arg.startswith("I"), arg
    tmp = arg[1:]
    while tmp:
        # Check for names
        match = re.match("(\d+)(.+)", tmp)
        if match:
            n = int(match.group(1))
            tmp = match.group(2)[n:]
            continue
        # Check for substitutions
        match = re.match("S[A-Z0-9]*_(.+)", tmp)
        if match:
            tmp = match.group(1)
        # Start of a template
        elif tmp.startswith("I"):
            tmp = skip_itanium_template(tmp)
        # Start of a nested name
        elif tmp.startswith("N"):
            _, tmp = parse_itanium_nested_name(tmp)
        # Start of an expression: assume that it's too complicated
        elif tmp.startswith("L") or tmp.startswith("X"):
            raise TooComplexName
        # End of the template
        elif tmp.startswith("E"):
            return tmp[1:]
        # Something else: probably a type, skip it
        else:
            tmp = tmp[1:]
    return None


# Parse an itanium mangled nested name and transform it into a list of pairs of
# (name, is_template), returning (list, rest of string).
def parse_itanium_nested_name(arg):
    # A nested name starts with N
    assert arg.startswith("N"), arg
    ret = []

    # Skip past the N, and possibly a substitution
    match = re.match("NS[A-Z0-9]*_(.+)", arg)
    if match:
        tmp = match.group(1)
    else:
        tmp = arg[1:]

    # Skip past CV-qualifiers and ref qualifiers
    match = re.match("[rVKRO]*(.+)", tmp)
    if match:
        tmp = match.group(1)

    # Repeatedly parse names from the string until we reach the end of the
    # nested name
    while tmp:
        # An E ends the nested name
        if tmp.startswith("E"):
            return ret, tmp[1:]
        # Parse a name
        name_part, tmp = parse_itanium_name(tmp)
        if not name_part:
            # If we failed then we don't know how to demangle this
            return None, None
        is_template = False
        # If this name is a template record that, then skip the template
        # arguments
        if tmp.startswith("I"):
            tmp = skip_itanium_template(tmp)
            is_template = True
        # Add the name to the list
        ret.append((name_part, is_template))

    # If we get here then something went wrong
    return None, None


# Parse a microsoft mangled symbol and return a list of pairs of
# (name, is_template). This is very rudimentary and does just enough
# in order to determine if the first or second component is a template.
def parse_microsoft_mangling(arg):
    # If the name doesn't start with ? this isn't a mangled name
    if not arg.startswith("?"):
        return [(arg, False)]
    arg = arg[1:]
    components = []
    while len(arg) > 0:
        # If we see an empty component we've reached the end
        if arg.startswith("@"):
            return components
        # Check for a simple name
        match = re.match("(\w+)@(.+)", arg)
        if match:
            components.append((match.group(1), False))
            arg = match.group(2)
            continue
        # Check for a special function name
        match = re.match("(\?_?\w)(.+)", arg)
        if match:
            components.append((match.group(1), False))
            arg = match.group(2)
            continue
        # Check for a template name
        match = re.match("\?\$(\w+)@[^@]+@(.+)", arg)
        if match:
            components.append((match.group(1), True))
            arg = match.group(2)
            continue
        # Some other kind of name that we can't handle
        components.append((arg, False))
        return components
    return components


def extract_symbols(arg):
    llvm_nm_path, should_keep_symbol, calling_convention_decoration, lib = arg
    symbol_defs = dict()
    symbol_refs = set()
    for (symbol, is_def) in nm_get_symbols(llvm_nm_path, lib):
        symbol = should_keep_symbol(symbol, calling_convention_decoration)
        if symbol:
            if is_def:
                symbol_defs[symbol] = 1 + symbol_defs.setdefault(symbol, 0)
            else:
                symbol_refs.add(symbol)
    return (symbol_defs, symbol_refs)


def get_template_name(sym, mangling):
    # Parse the mangling into a list of (name, is_template)
    try:
        if mangling == "microsoft":
            names = parse_microsoft_mangling(sym)
        else:
            match = re.match("\.?_Z(T[VTIS])?(N.+)", sym)
            if match:
                names, _ = parse_itanium_nested_name(match.group(2))
            else:
                names = None
    except TooComplexName:
        return None

    if not names:
        return None

    # If any component is a template then return it
    for name, is_template in names:
        if is_template:
            return name

    # Not a template
    return None


def parse_tool_path(parser, tool, val):
    try:
        # Close std streams as we don't want any output and we don't
        # want the process to wait for something on stdin.
        p = subprocess.Popen(
            [val],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            stdin=subprocess.PIPE,
            universal_newlines=True,
        )
        p.stdout.close()
        p.stderr.close()
        p.stdin.close()
        p.wait()
        return val
    except Exception:
        parser.error(f"Invalid path for {tool}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Extract symbols to export from libraries"
    )
    parser.add_argument(
        "--mangling",
        choices=["itanium", "microsoft"],
        required=True,
        help="expected symbol mangling scheme",
    )
    parser.add_argument(
        "--nm",
        metavar="path",
        type=lambda x: parse_tool_path(parser, "nm", x),
        help="path to the llvm-nm executable",
    )
    parser.add_argument(
        "--readobj",
        metavar="path",
        type=lambda x: parse_tool_path(parser, "readobj", x),
        help="path to the llvm-readobj executable",
    )
    parser.add_argument(
        "libs",
        metavar="lib",
        type=str,
        nargs="+",
        help="libraries to extract symbols from",
    )
    parser.add_argument("-o", metavar="file", type=str, help="output to file")
    args = parser.parse_args()

    # How we determine which symbols to keep and which to discard depends on
    # the mangling scheme
    if args.mangling == "microsoft":
        should_keep_symbol = should_keep_microsoft_symbol
    else:
        should_keep_symbol = should_keep_itanium_symbol

    # Get the list of libraries to extract symbols from
    libs = list()
    for lib in args.libs:
        # When invoked by cmake the arguments are the cmake target names of the
        # libraries, so we need to add .lib/.a to the end and maybe lib to the
        # start to get the filename. Also allow objects.
        suffixes = [".lib", ".a", ".obj", ".o"]
        if not any([lib.endswith(s) for s in suffixes]):
            for s in suffixes:
                if os.path.exists(lib + s):
                    lib = lib + s
                    break
                if os.path.exists("lib" + lib + s):
                    lib = "lib" + lib + s
                    break
        if not any([lib.endswith(s) for s in suffixes]):
            print("Don't know what to do with argument " + lib, file=sys.stderr)
            exit(1)
        libs.append(lib)

    # Check if calling convention decoration is used by inspecting the first
    # library in the list
    calling_convention_decoration = readobj_is_32bit_windows(args.readobj, libs[0])

    # Extract symbols from libraries in parallel. This is a huge time saver when
    # doing a debug build, as there are hundreds of thousands of symbols in each
    # library.
    # FIXME: On AIX, the default pool size can be too big for a logical
    #        partition's allocated memory, and can lead to an out of memory
    #        IO error. We are setting the pool size to 8 to avoid such
    #        errors at the moment, and will look for a graceful solution later.
    pool = multiprocessing.Pool(8) if platform.system() == "AIX" \
                                   else multiprocessing.Pool()
    try:
        # Only one argument can be passed to the mapping function, and we can't
        # use a lambda or local function definition as that doesn't work on
        # windows, so create a list of tuples which duplicates the arguments
        # that are the same in all calls.
        vals = [
            (args.nm, should_keep_symbol, calling_convention_decoration, x)
            for x in libs
        ]
        # Do an async map then wait for the result to make sure that
        # KeyboardInterrupt gets caught correctly (see
        # http://bugs.python.org/issue8296)
        result = pool.map_async(extract_symbols, vals)
        pool.close()
        libs_symbols = result.get(3600)
    except KeyboardInterrupt:
        # On Ctrl-C terminate everything and exit
        pool.terminate()
        pool.join()
        exit(1)

    # Merge everything into a single dict
    symbol_defs = dict()
    symbol_refs = set()
    for (this_lib_defs, this_lib_refs) in libs_symbols:
        for k, v in list(this_lib_defs.items()):
            symbol_defs[k] = v + symbol_defs.setdefault(k, 0)
        for sym in list(this_lib_refs):
            symbol_refs.add(sym)

    # Find which template instantiations are referenced at least once.
    template_instantiation_refs = set()
    for sym in list(symbol_refs):
        template = get_template_name(sym, args.mangling)
        if template:
            template_instantiation_refs.add(template)

    # Print symbols which both:
    #  * Appear in exactly one input, as symbols defined in multiple
    #    objects/libraries are assumed to have public definitions.
    #  * Are not a template instantiation that isn't referenced anywhere. This
    #    is because we need to export any explicitly instantiated templates,
    #    and we expect those to be referenced in some object.
    if args.o:
        outfile = open(args.o, "w")
    else:
        outfile = sys.stdout
    for k, v in list(symbol_defs.items()):
        # On AIX, export function descriptors instead of function entries.
        if platform.system() == "AIX" and k.startswith("."):
            continue
        template = get_template_name(k, args.mangling)
        if v == 1 and (not template or template in template_instantiation_refs):
            print(k, file=outfile)
