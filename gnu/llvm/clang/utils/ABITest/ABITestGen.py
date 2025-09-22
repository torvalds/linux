#!/usr/bin/env python

from __future__ import absolute_import, division, print_function
from pprint import pprint
import random, atexit, time
from random import randrange
import re

from Enumeration import *
from TypeGen import *

####


class TypePrinter(object):
    def __init__(
        self,
        output,
        outputHeader=None,
        outputTests=None,
        outputDriver=None,
        headerName=None,
        info=None,
    ):
        self.output = output
        self.outputHeader = outputHeader
        self.outputTests = outputTests
        self.outputDriver = outputDriver
        self.writeBody = outputHeader or outputTests or outputDriver
        self.types = {}
        self.testValues = {}
        self.testReturnValues = {}
        self.layoutTests = []
        self.declarations = set()

        if info:
            for f in (
                self.output,
                self.outputHeader,
                self.outputTests,
                self.outputDriver,
            ):
                if f:
                    print(info, file=f)

        if self.writeBody:
            print("#include <stdio.h>\n", file=self.output)
            if self.outputTests:
                print("#include <stdio.h>", file=self.outputTests)
                print("#include <string.h>", file=self.outputTests)
                print("#include <assert.h>\n", file=self.outputTests)

        if headerName:
            for f in (self.output, self.outputTests, self.outputDriver):
                if f is not None:
                    print('#include "%s"\n' % (headerName,), file=f)

        if self.outputDriver:
            print("#include <stdio.h>", file=self.outputDriver)
            print("#include <stdlib.h>\n", file=self.outputDriver)
            print("int main(int argc, char **argv) {", file=self.outputDriver)
            print("  int index = -1;", file=self.outputDriver)
            print("  if (argc > 1) index = atoi(argv[1]);", file=self.outputDriver)

    def finish(self):
        if self.layoutTests:
            print("int main(int argc, char **argv) {", file=self.output)
            print("  int index = -1;", file=self.output)
            print("  if (argc > 1) index = atoi(argv[1]);", file=self.output)
            for i, f in self.layoutTests:
                print("  if (index == -1 || index == %d)" % i, file=self.output)
                print("    %s();" % f, file=self.output)
            print("  return 0;", file=self.output)
            print("}", file=self.output)

        if self.outputDriver:
            print('  printf("DONE\\n");', file=self.outputDriver)
            print("  return 0;", file=self.outputDriver)
            print("}", file=self.outputDriver)

    def addDeclaration(self, decl):
        if decl in self.declarations:
            return False

        self.declarations.add(decl)
        if self.outputHeader:
            print(decl, file=self.outputHeader)
        else:
            print(decl, file=self.output)
            if self.outputTests:
                print(decl, file=self.outputTests)
        return True

    def getTypeName(self, T):
        name = self.types.get(T)
        if name is None:
            # Reserve slot
            self.types[T] = None
            self.types[T] = name = T.getTypeName(self)
        return name

    def writeLayoutTest(self, i, ty):
        tyName = self.getTypeName(ty)
        tyNameClean = tyName.replace(" ", "_").replace("*", "star")
        fnName = "test_%s" % tyNameClean

        print("void %s(void) {" % fnName, file=self.output)
        self.printSizeOfType("    %s" % fnName, tyName, ty, self.output)
        self.printAlignOfType("    %s" % fnName, tyName, ty, self.output)
        self.printOffsetsOfType("    %s" % fnName, tyName, ty, self.output)
        print("}", file=self.output)
        print(file=self.output)

        self.layoutTests.append((i, fnName))

    def writeFunction(self, i, FT):
        args = ", ".join(
            ["%s arg%d" % (self.getTypeName(t), i) for i, t in enumerate(FT.argTypes)]
        )
        if not args:
            args = "void"

        if FT.returnType is None:
            retvalName = None
            retvalTypeName = "void"
        else:
            retvalTypeName = self.getTypeName(FT.returnType)
            if self.writeBody or self.outputTests:
                retvalName = self.getTestReturnValue(FT.returnType)

        fnName = "fn%d" % (FT.index,)
        if self.outputHeader:
            print("%s %s(%s);" % (retvalTypeName, fnName, args), file=self.outputHeader)
        elif self.outputTests:
            print("%s %s(%s);" % (retvalTypeName, fnName, args), file=self.outputTests)

        print("%s %s(%s)" % (retvalTypeName, fnName, args), end=" ", file=self.output)
        if self.writeBody:
            print("{", file=self.output)

            for i, t in enumerate(FT.argTypes):
                self.printValueOfType("    %s" % fnName, "arg%d" % i, t)

            if retvalName is not None:
                print("  return %s;" % (retvalName,), file=self.output)
            print("}", file=self.output)
        else:
            print("{}", file=self.output)
        print(file=self.output)

        if self.outputDriver:
            print("  if (index == -1 || index == %d) {" % i, file=self.outputDriver)
            print("    extern void test_%s(void);" % fnName, file=self.outputDriver)
            print("    test_%s();" % fnName, file=self.outputDriver)
            print("   }", file=self.outputDriver)

        if self.outputTests:
            if self.outputHeader:
                print("void test_%s(void);" % (fnName,), file=self.outputHeader)

            if retvalName is None:
                retvalTests = None
            else:
                retvalTests = self.getTestValuesArray(FT.returnType)
            tests = [self.getTestValuesArray(ty) for ty in FT.argTypes]
            print("void test_%s(void) {" % (fnName,), file=self.outputTests)

            if retvalTests is not None:
                print(
                    '  printf("%s: testing return.\\n");' % (fnName,),
                    file=self.outputTests,
                )
                print(
                    "  for (int i=0; i<%d; ++i) {" % (retvalTests[1],),
                    file=self.outputTests,
                )
                args = ", ".join(["%s[%d]" % (t, randrange(l)) for t, l in tests])
                print("    %s RV;" % (retvalTypeName,), file=self.outputTests)
                print(
                    "    %s = %s[i];" % (retvalName, retvalTests[0]),
                    file=self.outputTests,
                )
                print("    RV = %s(%s);" % (fnName, args), file=self.outputTests)
                self.printValueOfType(
                    "  %s_RV" % fnName,
                    "RV",
                    FT.returnType,
                    output=self.outputTests,
                    indent=4,
                )
                self.checkTypeValues(
                    "RV",
                    "%s[i]" % retvalTests[0],
                    FT.returnType,
                    output=self.outputTests,
                    indent=4,
                )
                print("  }", file=self.outputTests)

            if tests:
                print(
                    '  printf("%s: testing arguments.\\n");' % (fnName,),
                    file=self.outputTests,
                )
            for i, (array, length) in enumerate(tests):
                for j in range(length):
                    args = ["%s[%d]" % (t, randrange(l)) for t, l in tests]
                    args[i] = "%s[%d]" % (array, j)
                    print(
                        "  %s(%s);"
                        % (
                            fnName,
                            ", ".join(args),
                        ),
                        file=self.outputTests,
                    )
            print("}", file=self.outputTests)

    def getTestReturnValue(self, type):
        typeName = self.getTypeName(type)
        info = self.testReturnValues.get(typeName)
        if info is None:
            name = "%s_retval" % (typeName.replace(" ", "_").replace("*", "star"),)
            print("%s %s;" % (typeName, name), file=self.output)
            if self.outputHeader:
                print("extern %s %s;" % (typeName, name), file=self.outputHeader)
            elif self.outputTests:
                print("extern %s %s;" % (typeName, name), file=self.outputTests)
            info = self.testReturnValues[typeName] = name
        return info

    def getTestValuesArray(self, type):
        typeName = self.getTypeName(type)
        info = self.testValues.get(typeName)
        if info is None:
            name = "%s_values" % (typeName.replace(" ", "_").replace("*", "star"),)
            print("static %s %s[] = {" % (typeName, name), file=self.outputTests)
            length = 0
            for item in self.getTestValues(type):
                print("\t%s," % (item,), file=self.outputTests)
                length += 1
            print("};", file=self.outputTests)
            info = self.testValues[typeName] = (name, length)
        return info

    def getTestValues(self, t):
        if isinstance(t, BuiltinType):
            if t.name == "float":
                for i in ["0.0", "-1.0", "1.0"]:
                    yield i + "f"
            elif t.name == "double":
                for i in ["0.0", "-1.0", "1.0"]:
                    yield i
            elif t.name in ("void *"):
                yield "(void*) 0"
                yield "(void*) -1"
            else:
                yield "(%s) 0" % (t.name,)
                yield "(%s) -1" % (t.name,)
                yield "(%s) 1" % (t.name,)
        elif isinstance(t, EnumType):
            for i in range(0, len(t.enumerators)):
                yield "enum%dval%d_%d" % (t.index, i, t.unique_id)
        elif isinstance(t, RecordType):
            nonPadding = [f for f in t.fields if not f.isPaddingBitField()]

            if not nonPadding:
                yield "{ }"
                return

            # FIXME: Use designated initializers to access non-first
            # fields of unions.
            if t.isUnion:
                for v in self.getTestValues(nonPadding[0]):
                    yield "{ %s }" % v
                return

            fieldValues = [list(v) for v in map(self.getTestValues, nonPadding)]
            for i, values in enumerate(fieldValues):
                for v in values:
                    elements = [random.choice(fv) for fv in fieldValues]
                    elements[i] = v
                    yield "{ %s }" % (", ".join(elements))

        elif isinstance(t, ComplexType):
            for t in self.getTestValues(t.elementType):
                yield "%s + %s * 1i" % (t, t)
        elif isinstance(t, ArrayType):
            values = list(self.getTestValues(t.elementType))
            if not values:
                yield "{ }"
            for i in range(t.numElements):
                for v in values:
                    elements = [random.choice(values) for i in range(t.numElements)]
                    elements[i] = v
                    yield "{ %s }" % (", ".join(elements))
        else:
            raise NotImplementedError('Cannot make tests values of type: "%s"' % (t,))

    def printSizeOfType(self, prefix, name, t, output=None, indent=2):
        print(
            '%*sprintf("%s: sizeof(%s) = %%ld\\n", (long)sizeof(%s));'
            % (indent, "", prefix, name, name),
            file=output,
        )

    def printAlignOfType(self, prefix, name, t, output=None, indent=2):
        print(
            '%*sprintf("%s: __alignof__(%s) = %%ld\\n", (long)__alignof__(%s));'
            % (indent, "", prefix, name, name),
            file=output,
        )

    def printOffsetsOfType(self, prefix, name, t, output=None, indent=2):
        if isinstance(t, RecordType):
            for i, f in enumerate(t.fields):
                if f.isBitField():
                    continue
                fname = "field%d" % i
                print(
                    '%*sprintf("%s: __builtin_offsetof(%s, %s) = %%ld\\n", (long)__builtin_offsetof(%s, %s));'
                    % (indent, "", prefix, name, fname, name, fname),
                    file=output,
                )

    def printValueOfType(self, prefix, name, t, output=None, indent=2):
        if output is None:
            output = self.output
        if isinstance(t, BuiltinType):
            value_expr = name
            if t.name.split(" ")[-1] == "_Bool":
                # Hack to work around PR5579.
                value_expr = "%s ? 2 : 0" % name

            if t.name.endswith("long long"):
                code = "lld"
            elif t.name.endswith("long"):
                code = "ld"
            elif t.name.split(" ")[-1] in ("_Bool", "char", "short", "int", "unsigned"):
                code = "d"
            elif t.name in ("float", "double"):
                code = "f"
            elif t.name == "long double":
                code = "Lf"
            else:
                code = "p"
            print(
                '%*sprintf("%s: %s = %%%s\\n", %s);'
                % (indent, "", prefix, name, code, value_expr),
                file=output,
            )
        elif isinstance(t, EnumType):
            print(
                '%*sprintf("%s: %s = %%d\\n", %s);' % (indent, "", prefix, name, name),
                file=output,
            )
        elif isinstance(t, RecordType):
            if not t.fields:
                print(
                    '%*sprintf("%s: %s (empty)\\n");' % (indent, "", prefix, name),
                    file=output,
                )
            for i, f in enumerate(t.fields):
                if f.isPaddingBitField():
                    continue
                fname = "%s.field%d" % (name, i)
                self.printValueOfType(prefix, fname, f, output=output, indent=indent)
        elif isinstance(t, ComplexType):
            self.printValueOfType(
                prefix,
                "(__real %s)" % name,
                t.elementType,
                output=output,
                indent=indent,
            )
            self.printValueOfType(
                prefix,
                "(__imag %s)" % name,
                t.elementType,
                output=output,
                indent=indent,
            )
        elif isinstance(t, ArrayType):
            for i in range(t.numElements):
                # Access in this fashion as a hackish way to portably
                # access vectors.
                if t.isVector:
                    self.printValueOfType(
                        prefix,
                        "((%s*) &%s)[%d]" % (t.elementType, name, i),
                        t.elementType,
                        output=output,
                        indent=indent,
                    )
                else:
                    self.printValueOfType(
                        prefix,
                        "%s[%d]" % (name, i),
                        t.elementType,
                        output=output,
                        indent=indent,
                    )
        else:
            raise NotImplementedError('Cannot print value of type: "%s"' % (t,))

    def checkTypeValues(self, nameLHS, nameRHS, t, output=None, indent=2):
        prefix = "foo"
        if output is None:
            output = self.output
        if isinstance(t, BuiltinType):
            print("%*sassert(%s == %s);" % (indent, "", nameLHS, nameRHS), file=output)
        elif isinstance(t, EnumType):
            print("%*sassert(%s == %s);" % (indent, "", nameLHS, nameRHS), file=output)
        elif isinstance(t, RecordType):
            for i, f in enumerate(t.fields):
                if f.isPaddingBitField():
                    continue
                self.checkTypeValues(
                    "%s.field%d" % (nameLHS, i),
                    "%s.field%d" % (nameRHS, i),
                    f,
                    output=output,
                    indent=indent,
                )
                if t.isUnion:
                    break
        elif isinstance(t, ComplexType):
            self.checkTypeValues(
                "(__real %s)" % nameLHS,
                "(__real %s)" % nameRHS,
                t.elementType,
                output=output,
                indent=indent,
            )
            self.checkTypeValues(
                "(__imag %s)" % nameLHS,
                "(__imag %s)" % nameRHS,
                t.elementType,
                output=output,
                indent=indent,
            )
        elif isinstance(t, ArrayType):
            for i in range(t.numElements):
                # Access in this fashion as a hackish way to portably
                # access vectors.
                if t.isVector:
                    self.checkTypeValues(
                        "((%s*) &%s)[%d]" % (t.elementType, nameLHS, i),
                        "((%s*) &%s)[%d]" % (t.elementType, nameRHS, i),
                        t.elementType,
                        output=output,
                        indent=indent,
                    )
                else:
                    self.checkTypeValues(
                        "%s[%d]" % (nameLHS, i),
                        "%s[%d]" % (nameRHS, i),
                        t.elementType,
                        output=output,
                        indent=indent,
                    )
        else:
            raise NotImplementedError('Cannot print value of type: "%s"' % (t,))


import sys


def main():
    from optparse import OptionParser, OptionGroup

    parser = OptionParser("%prog [options] {indices}")
    parser.add_option(
        "",
        "--mode",
        dest="mode",
        help="autogeneration mode (random or linear) [default %default]",
        type="choice",
        choices=("random", "linear"),
        default="linear",
    )
    parser.add_option(
        "",
        "--count",
        dest="count",
        help="autogenerate COUNT functions according to MODE",
        type=int,
        default=0,
    )
    parser.add_option(
        "",
        "--min",
        dest="minIndex",
        metavar="N",
        help="start autogeneration with the Nth function type  [default %default]",
        type=int,
        default=0,
    )
    parser.add_option(
        "",
        "--max",
        dest="maxIndex",
        metavar="N",
        help="maximum index for random autogeneration  [default %default]",
        type=int,
        default=10000000,
    )
    parser.add_option(
        "",
        "--seed",
        dest="seed",
        help="random number generator seed [default %default]",
        type=int,
        default=1,
    )
    parser.add_option(
        "",
        "--use-random-seed",
        dest="useRandomSeed",
        help="use random value for initial random number generator seed",
        action="store_true",
        default=False,
    )
    parser.add_option(
        "",
        "--skip",
        dest="skipTests",
        help="add a test index to skip",
        type=int,
        action="append",
        default=[],
    )
    parser.add_option(
        "-o",
        "--output",
        dest="output",
        metavar="FILE",
        help="write output to FILE  [default %default]",
        type=str,
        default="-",
    )
    parser.add_option(
        "-O",
        "--output-header",
        dest="outputHeader",
        metavar="FILE",
        help="write header file for output to FILE  [default %default]",
        type=str,
        default=None,
    )
    parser.add_option(
        "-T",
        "--output-tests",
        dest="outputTests",
        metavar="FILE",
        help="write function tests to FILE  [default %default]",
        type=str,
        default=None,
    )
    parser.add_option(
        "-D",
        "--output-driver",
        dest="outputDriver",
        metavar="FILE",
        help="write test driver to FILE  [default %default]",
        type=str,
        default=None,
    )
    parser.add_option(
        "",
        "--test-layout",
        dest="testLayout",
        metavar="FILE",
        help="test structure layout",
        action="store_true",
        default=False,
    )

    group = OptionGroup(parser, "Type Enumeration Options")
    # Builtins - Ints
    group.add_option(
        "",
        "--no-char",
        dest="useChar",
        help="do not generate char types",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-short",
        dest="useShort",
        help="do not generate short types",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-int",
        dest="useInt",
        help="do not generate int types",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-long",
        dest="useLong",
        help="do not generate long types",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-long-long",
        dest="useLongLong",
        help="do not generate long long types",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-unsigned",
        dest="useUnsigned",
        help="do not generate unsigned integer types",
        action="store_false",
        default=True,
    )

    # Other builtins
    group.add_option(
        "",
        "--no-bool",
        dest="useBool",
        help="do not generate bool types",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-float",
        dest="useFloat",
        help="do not generate float types",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-double",
        dest="useDouble",
        help="do not generate double types",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-long-double",
        dest="useLongDouble",
        help="do not generate long double types",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-void-pointer",
        dest="useVoidPointer",
        help="do not generate void* types",
        action="store_false",
        default=True,
    )

    # Enumerations
    group.add_option(
        "",
        "--no-enums",
        dest="useEnum",
        help="do not generate enum types",
        action="store_false",
        default=True,
    )

    # Derived types
    group.add_option(
        "",
        "--no-array",
        dest="useArray",
        help="do not generate record types",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-complex",
        dest="useComplex",
        help="do not generate complex types",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-record",
        dest="useRecord",
        help="do not generate record types",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-union",
        dest="recordUseUnion",
        help="do not generate union types",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-vector",
        dest="useVector",
        help="do not generate vector types",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-bit-field",
        dest="useBitField",
        help="do not generate bit-field record members",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--no-builtins",
        dest="useBuiltins",
        help="do not use any types",
        action="store_false",
        default=True,
    )

    # Tuning
    group.add_option(
        "",
        "--no-function-return",
        dest="functionUseReturn",
        help="do not generate return types for functions",
        action="store_false",
        default=True,
    )
    group.add_option(
        "",
        "--vector-types",
        dest="vectorTypes",
        help="comma separated list of vector types (e.g., v2i32) [default %default]",
        action="store",
        type=str,
        default="v2i16, v1i64, v2i32, v4i16, v8i8, v2f32, v2i64, v4i32, v8i16, v16i8, v2f64, v4f32, v16f32",
        metavar="N",
    )
    group.add_option(
        "",
        "--bit-fields",
        dest="bitFields",
        help="comma separated list 'type:width' bit-field specifiers [default %default]",
        action="store",
        type=str,
        default=("char:0,char:4,int:0,unsigned:1,int:1,int:4,int:13,int:24"),
    )
    group.add_option(
        "",
        "--max-args",
        dest="functionMaxArgs",
        help="maximum number of arguments per function [default %default]",
        action="store",
        type=int,
        default=4,
        metavar="N",
    )
    group.add_option(
        "",
        "--max-array",
        dest="arrayMaxSize",
        help="maximum array size [default %default]",
        action="store",
        type=int,
        default=4,
        metavar="N",
    )
    group.add_option(
        "",
        "--max-record",
        dest="recordMaxSize",
        help="maximum number of fields per record [default %default]",
        action="store",
        type=int,
        default=4,
        metavar="N",
    )
    group.add_option(
        "",
        "--max-record-depth",
        dest="recordMaxDepth",
        help="maximum nested structure depth [default %default]",
        action="store",
        type=int,
        default=None,
        metavar="N",
    )
    parser.add_option_group(group)
    (opts, args) = parser.parse_args()

    if not opts.useRandomSeed:
        random.seed(opts.seed)

    # Construct type generator
    builtins = []
    if opts.useBuiltins:
        ints = []
        if opts.useChar:
            ints.append(("char", 1))
        if opts.useShort:
            ints.append(("short", 2))
        if opts.useInt:
            ints.append(("int", 4))
        # FIXME: Wrong size.
        if opts.useLong:
            ints.append(("long", 4))
        if opts.useLongLong:
            ints.append(("long long", 8))
        if opts.useUnsigned:
            ints = [("unsigned %s" % i, s) for i, s in ints] + [
                ("signed %s" % i, s) for i, s in ints
            ]
        builtins.extend(ints)

        if opts.useBool:
            builtins.append(("_Bool", 1))
        if opts.useFloat:
            builtins.append(("float", 4))
        if opts.useDouble:
            builtins.append(("double", 8))
        if opts.useLongDouble:
            builtins.append(("long double", 16))
        # FIXME: Wrong size.
        if opts.useVoidPointer:
            builtins.append(("void*", 4))

    btg = FixedTypeGenerator([BuiltinType(n, s) for n, s in builtins])

    bitfields = []
    for specifier in opts.bitFields.split(","):
        if not specifier.strip():
            continue
        name, width = specifier.strip().split(":", 1)
        bitfields.append(BuiltinType(name, None, int(width)))
    bftg = FixedTypeGenerator(bitfields)

    charType = BuiltinType("char", 1)
    shortType = BuiltinType("short", 2)
    intType = BuiltinType("int", 4)
    longlongType = BuiltinType("long long", 8)
    floatType = BuiltinType("float", 4)
    doubleType = BuiltinType("double", 8)
    sbtg = FixedTypeGenerator([charType, intType, floatType, doubleType])

    atg = AnyTypeGenerator()
    artg = AnyTypeGenerator()

    def makeGenerator(atg, subgen, subfieldgen, useRecord, useArray, useBitField):
        atg.addGenerator(btg)
        if useBitField and opts.useBitField:
            atg.addGenerator(bftg)
        if useRecord and opts.useRecord:
            assert subgen
            atg.addGenerator(
                RecordTypeGenerator(
                    subfieldgen, opts.recordUseUnion, opts.recordMaxSize
                )
            )
        if opts.useComplex:
            # FIXME: Allow overriding builtins here
            atg.addGenerator(ComplexTypeGenerator(sbtg))
        if useArray and opts.useArray:
            assert subgen
            atg.addGenerator(ArrayTypeGenerator(subgen, opts.arrayMaxSize))
        if opts.useVector:
            vTypes = []
            for i, t in enumerate(opts.vectorTypes.split(",")):
                m = re.match("v([1-9][0-9]*)([if][1-9][0-9]*)", t.strip())
                if not m:
                    parser.error("Invalid vector type: %r" % t)
                count, kind = m.groups()
                count = int(count)
                type = {
                    "i8": charType,
                    "i16": shortType,
                    "i32": intType,
                    "i64": longlongType,
                    "f32": floatType,
                    "f64": doubleType,
                }.get(kind)
                if not type:
                    parser.error("Invalid vector type: %r" % t)
                vTypes.append(ArrayType(i, True, type, count * type.size))

            atg.addGenerator(FixedTypeGenerator(vTypes))
        if opts.useEnum:
            atg.addGenerator(EnumTypeGenerator([None, "-1", "1", "1u"], 1, 4))

    if opts.recordMaxDepth is None:
        # Fully recursive, just avoid top-level arrays.
        subFTG = AnyTypeGenerator()
        subTG = AnyTypeGenerator()
        atg = AnyTypeGenerator()
        makeGenerator(subFTG, atg, atg, True, True, True)
        makeGenerator(subTG, atg, subFTG, True, True, False)
        makeGenerator(atg, subTG, subFTG, True, False, False)
    else:
        # Make a chain of type generators, each builds smaller
        # structures.
        base = AnyTypeGenerator()
        fbase = AnyTypeGenerator()
        makeGenerator(base, None, None, False, False, False)
        makeGenerator(fbase, None, None, False, False, True)
        for i in range(opts.recordMaxDepth):
            n = AnyTypeGenerator()
            fn = AnyTypeGenerator()
            makeGenerator(n, base, fbase, True, True, False)
            makeGenerator(fn, base, fbase, True, True, True)
            base = n
            fbase = fn
        atg = AnyTypeGenerator()
        makeGenerator(atg, base, fbase, True, False, False)

    if opts.testLayout:
        ftg = atg
    else:
        ftg = FunctionTypeGenerator(atg, opts.functionUseReturn, opts.functionMaxArgs)

    # Override max,min,count if finite
    if opts.maxIndex is None:
        if ftg.cardinality is aleph0:
            opts.maxIndex = 10000000
        else:
            opts.maxIndex = ftg.cardinality
    opts.maxIndex = min(opts.maxIndex, ftg.cardinality)
    opts.minIndex = max(0, min(opts.maxIndex - 1, opts.minIndex))
    if not opts.mode == "random":
        opts.count = min(opts.count, opts.maxIndex - opts.minIndex)

    if opts.output == "-":
        output = sys.stdout
    else:
        output = open(opts.output, "w")
        atexit.register(lambda: output.close())

    outputHeader = None
    if opts.outputHeader:
        outputHeader = open(opts.outputHeader, "w")
        atexit.register(lambda: outputHeader.close())

    outputTests = None
    if opts.outputTests:
        outputTests = open(opts.outputTests, "w")
        atexit.register(lambda: outputTests.close())

    outputDriver = None
    if opts.outputDriver:
        outputDriver = open(opts.outputDriver, "w")
        atexit.register(lambda: outputDriver.close())

    info = ""
    info += "// %s\n" % (" ".join(sys.argv),)
    info += "// Generated: %s\n" % (time.strftime("%Y-%m-%d %H:%M"),)
    info += "// Cardinality of function generator: %s\n" % (ftg.cardinality,)
    info += "// Cardinality of type generator: %s\n" % (atg.cardinality,)

    if opts.testLayout:
        info += "\n#include <stdio.h>"

    P = TypePrinter(
        output,
        outputHeader=outputHeader,
        outputTests=outputTests,
        outputDriver=outputDriver,
        headerName=opts.outputHeader,
        info=info,
    )

    def write(N):
        try:
            FT = ftg.get(N)
        except RuntimeError as e:
            if e.args[0] == "maximum recursion depth exceeded":
                print(
                    "WARNING: Skipped %d, recursion limit exceeded (bad arguments?)"
                    % (N,),
                    file=sys.stderr,
                )
                return
            raise
        if opts.testLayout:
            P.writeLayoutTest(N, FT)
        else:
            P.writeFunction(N, FT)

    if args:
        [write(int(a)) for a in args]

    skipTests = set(opts.skipTests)
    for i in range(opts.count):
        if opts.mode == "linear":
            index = opts.minIndex + i
        else:
            index = opts.minIndex + int(
                (opts.maxIndex - opts.minIndex) * random.random()
            )
        if index in skipTests:
            continue
        write(index)

    P.finish()


if __name__ == "__main__":
    main()
