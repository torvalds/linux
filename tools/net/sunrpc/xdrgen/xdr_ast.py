#!/usr/bin/env python3
# ex: set filetype=python:

"""Define and implement the Abstract Syntax Tree for the XDR language."""

import sys
from typing import List
from dataclasses import dataclass, KW_ONLY

from lark import ast_utils, Transformer
from lark.tree import Meta

this_module = sys.modules[__name__]

big_endian = []
excluded_apis = []
header_name = "none"
public_apis = []
structs = set()
pass_by_reference = set()

constants = {}


def xdr_quadlen(val: str) -> int:
    """Return integer XDR width of an XDR type"""
    if val in constants:
        octets = constants[val]
    else:
        octets = int(val)
    return int((octets + 3) / 4)


symbolic_widths = {
    "void": ["XDR_void"],
    "bool": ["XDR_bool"],
    "short": ["XDR_short"],
    "unsigned_short": ["XDR_unsigned_short"],
    "int": ["XDR_int"],
    "unsigned_int": ["XDR_unsigned_int"],
    "long": ["XDR_long"],
    "unsigned_long": ["XDR_unsigned_long"],
    "hyper": ["XDR_hyper"],
    "unsigned_hyper": ["XDR_unsigned_hyper"],
}

# Numeric XDR widths are tracked in a dictionary that is keyed
# by type_name because sometimes a caller has nothing more than
# the type_name to use to figure out the numeric width.
max_widths = {
    "void": 0,
    "bool": 1,
    "short": 1,
    "unsigned_short": 1,
    "int": 1,
    "unsigned_int": 1,
    "long": 1,
    "unsigned_long": 1,
    "hyper": 2,
    "unsigned_hyper": 2,
}


@dataclass
class _XdrAst(ast_utils.Ast):
    """Base class for the XDR abstract syntax tree"""

    # Source position of the construct's declared identifier, when
    # the transformer records one, so semantic diagnostics can point
    # at the exact declaration. The KW_ONLY marker makes the fields
    # keyword-only, so they never disturb the positional child
    # ordering lark uses to build each node; 0 means the position was
    # not recorded.
    _: KW_ONLY
    line: int = 0
    column: int = 0


@dataclass
class _XdrIdentifier(_XdrAst):
    """Corresponds to 'identifier' in the XDR language grammar"""

    symbol: str


@dataclass
class _XdrValue(_XdrAst):
    """Corresponds to 'value' in the XDR language grammar"""

    value: str


@dataclass
class _XdrConstantValue(_XdrAst):
    """Corresponds to 'constant' in the XDR language grammar"""

    value: int


@dataclass
class _XdrTypeSpecifier(_XdrAst):
    """Corresponds to 'type_specifier' in the XDR language grammar"""

    type_name: str
    c_classifier: str = ""


@dataclass
class _XdrDefinedType(_XdrTypeSpecifier):
    """Corresponds to a type defined by the input specification"""

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        return [get_header_name().upper() + "_" + self.type_name + "_sz"]

    def __post_init__(self):
        if self.type_name in structs:
            self.c_classifier = "struct "
        symbolic_widths[self.type_name] = self.symbolic_width()


@dataclass
class _XdrBuiltInType(_XdrTypeSpecifier):
    """Corresponds to a built-in XDR type"""

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        return symbolic_widths[self.type_name]


@dataclass
class _XdrDeclaration(_XdrAst):
    """Base class of XDR type declarations"""


@dataclass
class _XdrFixedLengthOpaque(_XdrDeclaration):
    """A fixed-length opaque declaration"""

    name: str
    size: str
    template: str = "fixed_length_opaque"

    def max_width(self) -> int:
        """Return width of type in XDR_UNITS"""
        return xdr_quadlen(self.size)

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        return ["XDR_QUADLEN(" + self.size + ")"]

    def __post_init__(self):
        max_widths[self.name] = self.max_width()
        symbolic_widths[self.name] = self.symbolic_width()


@dataclass
class _XdrVariableLengthOpaque(_XdrDeclaration):
    """A variable-length opaque declaration"""

    name: str
    maxsize: str
    template: str = "variable_length_opaque"

    def max_width(self) -> int:
        """Return width of type in XDR_UNITS"""
        return 1 + xdr_quadlen(self.maxsize)

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        widths = ["XDR_unsigned_int"]
        if self.maxsize != "0":
            widths.append("XDR_QUADLEN(" + self.maxsize + ")")
        return widths

    def __post_init__(self):
        max_widths[self.name] = self.max_width()
        symbolic_widths[self.name] = self.symbolic_width()


@dataclass
class _XdrString(_XdrDeclaration):
    """A (NUL-terminated) variable-length string declaration"""

    name: str
    maxsize: str
    template: str = "string"

    def max_width(self) -> int:
        """Return width of type in XDR_UNITS"""
        return 1 + xdr_quadlen(self.maxsize)

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        widths = ["XDR_unsigned_int"]
        if self.maxsize != "0":
            widths.append("XDR_QUADLEN(" + self.maxsize + ")")
        return widths

    def __post_init__(self):
        max_widths[self.name] = self.max_width()
        symbolic_widths[self.name] = self.symbolic_width()


@dataclass
class _XdrFixedLengthArray(_XdrDeclaration):
    """A fixed-length array declaration"""

    name: str
    spec: _XdrTypeSpecifier
    size: str
    template: str = "fixed_length_array"

    def max_width(self) -> int:
        """Return width of type in XDR_UNITS"""
        return xdr_quadlen(self.size) * max_widths[self.spec.type_name]

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        item_width = " + ".join(symbolic_widths[self.spec.type_name])
        return ["(" + self.size + " * (" + item_width + "))"]

    def __post_init__(self):
        max_widths[self.name] = self.max_width()
        symbolic_widths[self.name] = self.symbolic_width()


@dataclass
class _XdrVariableLengthArray(_XdrDeclaration):
    """A variable-length array declaration"""

    name: str
    spec: _XdrTypeSpecifier
    maxsize: str
    template: str = "variable_length_array"

    def max_width(self) -> int:
        """Return width of type in XDR_UNITS"""
        return 1 + (xdr_quadlen(self.maxsize) * max_widths[self.spec.type_name])

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        widths = ["XDR_unsigned_int"]
        if self.maxsize != "0":
            item_width = " + ".join(symbolic_widths[self.spec.type_name])
            widths.append("(" + self.maxsize + " * (" + item_width + "))")
        return widths

    def __post_init__(self):
        max_widths[self.name] = self.max_width()
        symbolic_widths[self.name] = self.symbolic_width()


@dataclass
class _XdrOptionalData(_XdrDeclaration):
    """An 'optional_data' declaration"""

    name: str
    spec: _XdrTypeSpecifier
    template: str = "optional_data"

    def max_width(self) -> int:
        """Return width of type in XDR_UNITS"""
        return 1

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        return ["XDR_bool"]

    def __post_init__(self):
        structs.add(self.name)
        pass_by_reference.add(self.name)
        max_widths[self.name] = self.max_width()
        symbolic_widths[self.name] = self.symbolic_width()


@dataclass
class _XdrBasic(_XdrDeclaration):
    """A 'basic' declaration"""

    name: str
    spec: _XdrTypeSpecifier
    template: str = "basic"

    def max_width(self) -> int:
        """Return width of type in XDR_UNITS"""
        return max_widths[self.spec.type_name]

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        return symbolic_widths[self.spec.type_name]

    def __post_init__(self):
        max_widths[self.name] = self.max_width()
        symbolic_widths[self.name] = self.symbolic_width()


@dataclass
class _XdrVoid(_XdrDeclaration):
    """A void declaration"""

    name: str = "void"
    template: str = "void"

    def max_width(self) -> int:
        """Return width of type in XDR_UNITS"""
        return 0

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        return []


@dataclass
class _XdrConstant(_XdrAst):
    """Corresponds to 'constant_def' in the grammar"""

    name: str
    value: str

    def __post_init__(self):
        if self.value not in constants:
            constants[self.name] = int(self.value, 0)


@dataclass
class _XdrEnumerator(_XdrAst):
    """An 'identifier = value' enumerator"""

    name: str
    value: str

    def __post_init__(self):
        if self.value not in constants:
            constants[self.name] = int(self.value, 0)


@dataclass
class _XdrEnum(_XdrAst):
    """An XDR enum definition"""

    name: str
    enumerators: List[_XdrEnumerator]

    def max_width(self) -> int:
        """Return width of type in XDR_UNITS"""
        return 1

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        return ["XDR_int"]

    def __post_init__(self):
        max_widths[self.name] = self.max_width()
        symbolic_widths[self.name] = self.symbolic_width()


@dataclass
class _XdrStruct(_XdrAst):
    """An XDR struct definition"""

    name: str
    fields: List[_XdrDeclaration]

    def max_width(self) -> int:
        """Return width of type in XDR_UNITS"""
        width = 0
        for field in self.fields:
            width += field.max_width()
        return width

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        widths = []
        for field in self.fields:
            widths += field.symbolic_width()
        return widths

    def __post_init__(self):
        structs.add(self.name)
        pass_by_reference.add(self.name)
        max_widths[self.name] = self.max_width()
        symbolic_widths[self.name] = self.symbolic_width()


@dataclass
class _XdrPointer(_XdrAst):
    """An XDR pointer definition"""

    name: str
    fields: List[_XdrDeclaration]

    def max_width(self) -> int:
        """Return width of type in XDR_UNITS"""
        width = 1
        for field in self.fields[0:-1]:
            width += field.max_width()
        return width

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        widths = []
        widths += ["XDR_bool"]
        for field in self.fields[0:-1]:
            widths += field.symbolic_width()
        return widths

    def __post_init__(self):
        structs.add(self.name)
        pass_by_reference.add(self.name)
        max_widths[self.name] = self.max_width()
        symbolic_widths[self.name] = self.symbolic_width()


@dataclass
class _XdrTypedef(_XdrAst):
    """An XDR typedef"""

    declaration: _XdrDeclaration

    def max_width(self) -> int:
        """Return width of type in XDR_UNITS"""
        return self.declaration.max_width()

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        return self.declaration.symbolic_width()

    def __post_init__(self):
        if isinstance(self.declaration, _XdrBasic):
            new_type = self.declaration
            if isinstance(new_type.spec, _XdrDefinedType):
                if new_type.spec.type_name in pass_by_reference:
                    pass_by_reference.add(new_type.name)
                max_widths[new_type.name] = self.max_width()
                symbolic_widths[new_type.name] = self.symbolic_width()


@dataclass
class _XdrCaseSpec(_XdrAst):
    """One case in an XDR union"""

    values: List[str]
    arm: _XdrDeclaration
    template: str = "case_spec"


@dataclass
class _XdrDefaultSpec(_XdrAst):
    """Default case in an XDR union"""

    arm: _XdrDeclaration
    template: str = "default_spec"


@dataclass
class _XdrUnion(_XdrAst):
    """An XDR union"""

    name: str
    discriminant: _XdrDeclaration
    cases: List[_XdrCaseSpec]
    default: _XdrDeclaration

    def max_width(self) -> int:
        """Return width of type in XDR_UNITS"""
        max_width = 0
        for case in self.cases:
            if case.arm.max_width() > max_width:
                max_width = case.arm.max_width()
        if self.default:
            if self.default.arm.max_width() > max_width:
                max_width = self.default.arm.max_width()
        return 1 + max_width

    def symbolic_width(self) -> List:
        """Return list containing XDR width of type's components"""
        max_width = 0
        for case in self.cases:
            if case.arm.max_width() > max_width:
                max_width = case.arm.max_width()
                width = case.arm.symbolic_width()
        if self.default:
            if self.default.arm.max_width() > max_width:
                max_width = self.default.arm.max_width()
                width = self.default.arm.symbolic_width()
        return symbolic_widths[self.discriminant.name] + width

    def __post_init__(self):
        structs.add(self.name)
        pass_by_reference.add(self.name)
        max_widths[self.name] = self.max_width()
        symbolic_widths[self.name] = self.symbolic_width()


@dataclass
class _RpcProcedure(_XdrAst):
    """RPC procedure definition"""

    name: str
    number: str
    argument: _XdrTypeSpecifier
    result: _XdrTypeSpecifier


@dataclass
class _RpcVersion(_XdrAst):
    """RPC version definition"""

    name: str
    number: str
    procedures: List[_RpcProcedure]


@dataclass
class _RpcProgram(_XdrAst):
    """RPC program definition"""

    name: str
    number: str
    versions: List[_RpcVersion]


@dataclass
class _Pragma(_XdrAst):
    """Empty class for pragma directives"""


@dataclass
class _XdrPassthru(_XdrAst):
    """Passthrough line to emit verbatim in output"""

    content: str


@dataclass
class Definition(_XdrAst, ast_utils.WithMeta):
    """Corresponds to 'definition' in the grammar"""

    meta: Meta
    value: _XdrAst


@dataclass
class Specification(_XdrAst, ast_utils.AsList):
    """Corresponds to 'specification' in the grammar"""

    definitions: List[Definition]


class ParseToAst(Transformer):
    """Functions that transform productions into AST nodes"""

    def identifier(self, children):
        """Instantiate one _XdrIdentifier object"""
        token = children[0]
        return _XdrIdentifier(token.value, line=token.line, column=token.column)

    def value(self, children):
        """Instantiate one _XdrValue object"""
        if isinstance(children[0], _XdrIdentifier):
            return _XdrValue(children[0].symbol)
        return _XdrValue(children[0].children[0].value)

    def constant(self, children):
        """Instantiate one _XdrConstantValue object"""
        match children[0].data:
            case "decimal_constant":
                value = int(children[0].children[0].value, base=10)
            case "hexadecimal_constant":
                value = int(children[0].children[0].value, base=16)
            case "octal_constant":
                value = int(children[0].children[0].value, base=8)
        return _XdrConstantValue(value)

    def type_specifier(self, children):
        """Instantiate one _XdrTypeSpecifier object"""
        if isinstance(children[0], _XdrIdentifier):
            name = children[0].symbol
            return _XdrDefinedType(type_name=name)

        name = children[0].data.value
        return _XdrBuiltInType(type_name=name)

    def constant_def(self, children):
        """Instantiate one _XdrConstant object"""
        ident = children[0]
        value = children[1].value
        return _XdrConstant(ident.symbol, value, line=ident.line, column=ident.column)

    def enum(self, children):
        """Instantiate one _XdrEnum object"""
        name_ident = children[0]

        i = 0
        enumerators = []
        body = children[1]
        while i < len(body.children):
            ident = body.children[i]
            value = body.children[i + 1].value
            enumerators.append(
                _XdrEnumerator(
                    ident.symbol, value, line=ident.line, column=ident.column
                )
            )
            i = i + 2

        return _XdrEnum(
            name_ident.symbol,
            enumerators,
            line=name_ident.line,
            column=name_ident.column,
        )

    def fixed_length_opaque(self, children):
        """Instantiate one _XdrFixedLengthOpaque declaration object"""
        ident = children[0]
        size = children[1].value

        return _XdrFixedLengthOpaque(
            ident.symbol, size, line=ident.line, column=ident.column
        )

    def variable_length_opaque(self, children):
        """Instantiate one _XdrVariableLengthOpaque declaration object"""
        ident = children[0]
        if children[1] is not None:
            maxsize = children[1].value
        else:
            maxsize = "0"

        return _XdrVariableLengthOpaque(
            ident.symbol, maxsize, line=ident.line, column=ident.column
        )

    def string(self, children):
        """Instantiate one _XdrString declaration object"""
        ident = children[0]
        if children[1] is not None:
            maxsize = children[1].value
        else:
            maxsize = "0"

        return _XdrString(ident.symbol, maxsize, line=ident.line, column=ident.column)

    def fixed_length_array(self, children):
        """Instantiate one _XdrFixedLengthArray declaration object"""
        spec = children[0]
        ident = children[1]
        size = children[2].value

        return _XdrFixedLengthArray(
            ident.symbol, spec, size, line=ident.line, column=ident.column
        )

    def variable_length_array(self, children):
        """Instantiate one _XdrVariableLengthArray declaration object"""
        spec = children[0]
        ident = children[1]
        if children[2] is not None:
            maxsize = children[2].value
        else:
            maxsize = "0"

        return _XdrVariableLengthArray(
            ident.symbol, spec, maxsize, line=ident.line, column=ident.column
        )

    def optional_data(self, children):
        """Instantiate one _XdrOptionalData declaration object"""
        spec = children[0]
        ident = children[1]

        return _XdrOptionalData(
            ident.symbol, spec, line=ident.line, column=ident.column
        )

    def basic(self, children):
        """Instantiate one _XdrBasic object"""
        spec = children[0]
        ident = children[1]

        return _XdrBasic(ident.symbol, spec, line=ident.line, column=ident.column)

    def void(self, children):
        """Instantiate one _XdrVoid declaration object"""

        return _XdrVoid()

    def struct(self, children):
        """Instantiate one _XdrStruct object"""
        ident = children[0]
        name = ident.symbol
        fields = children[1].children
        pos = {"line": ident.line, "column": ident.column}

        last_field = fields[-1]
        if (
            isinstance(last_field, _XdrOptionalData)
            and name == last_field.spec.type_name
        ):
            return _XdrPointer(name, fields, **pos)

        return _XdrStruct(name, fields, **pos)

    def typedef(self, children):
        """Instantiate one _XdrTypedef object"""
        new_type = children[0]

        return _XdrTypedef(new_type)

    def case_spec(self, children):
        """Instantiate one _XdrCaseSpec object"""
        values = []
        for item in children[0:-1]:
            values.append(item.value)
        arm = children[-1]

        return _XdrCaseSpec(values, arm)

    def default_spec(self, children):
        """Instantiate one _XdrDefaultSpec object"""
        arm = children[0]

        return _XdrDefaultSpec(arm)

    def union(self, children):
        """Instantiate one _XdrUnion object"""
        ident = children[0]

        body = children[1]
        discriminant = body.children[0].children[0]
        cases = body.children[1:-1]
        default = body.children[-1]

        return _XdrUnion(
            ident.symbol,
            discriminant,
            cases,
            default,
            line=ident.line,
            column=ident.column,
        )

    def procedure_def(self, children):
        """Instantiate one _RpcProcedure object"""
        result = children[0]
        ident = children[1]
        argument = children[2]
        number = children[3].value

        return _RpcProcedure(
            ident.symbol,
            number,
            argument,
            result,
            line=ident.line,
            column=ident.column,
        )

    def version_def(self, children):
        """Instantiate one _RpcVersion object"""
        ident = children[0]
        number = children[-1].value
        procedures = children[1:-1]

        return _RpcVersion(
            ident.symbol, number, procedures, line=ident.line, column=ident.column
        )

    def program_def(self, children):
        """Instantiate one _RpcProgram object"""
        ident = children[0]
        number = children[-1].value
        versions = children[1:-1]

        return _RpcProgram(
            ident.symbol, number, versions, line=ident.line, column=ident.column
        )

    def pragma_def(self, children):
        """Instantiate one _Pragma object"""
        directive = children[0].children[0].data
        match directive:
            case "big_endian_directive":
                big_endian.append(children[1].symbol)
            case "exclude_directive":
                excluded_apis.append(children[1].symbol)
            case "header_directive":
                global header_name
                header_name = children[1].symbol
            case "public_directive":
                public_apis.append(children[1].symbol)
            case _:
                raise NotImplementedError("Directive not supported")
        return _Pragma()

    def passthru_def(self, children):
        """Instantiate one _XdrPassthru object"""
        token = children[0]
        content = token.value[1:]
        return _XdrPassthru(content)


transformer = ast_utils.create_transformer(this_module, ParseToAst())


def _merge_consecutive_passthru(definitions: List[Definition]) -> List[Definition]:
    """Merge consecutive passthru definitions into single nodes"""
    result = []
    i = 0
    while i < len(definitions):
        if isinstance(definitions[i].value, _XdrPassthru):
            lines = [definitions[i].value.content]
            meta = definitions[i].meta
            j = i + 1
            while j < len(definitions) and isinstance(
                definitions[j].value, _XdrPassthru
            ):
                lines.append(definitions[j].value.content)
                j += 1
            merged = _XdrPassthru("\n".join(lines))
            result.append(Definition(meta, merged))
            i = j
        else:
            result.append(definitions[i])
            i += 1
    return result


def _meta_line(meta) -> int:
    """Return the 1-based source line for a node's meta, or 0 if unknown"""
    try:
        return meta.line
    except AttributeError:
        return 0


class XdrSemanticError(Exception):
    """A specification that parses but violates an XDR semantic rule.

    Detection lives in the language-independent front end because a
    duplicate name is malformed XDR regardless of the output language.
    """

    def __init__(self, message: str, meta):
        super().__init__(message)
        self.message = message
        self.line = _meta_line(meta)
        self.column = getattr(meta, "column", 0)


def _introduced_names(value):
    """Yield (name, node) for each identifier a definition introduces."""
    if isinstance(value, (_XdrStruct, _XdrUnion, _XdrPointer)):
        yield value.name, value
    elif isinstance(value, _XdrEnum):
        yield value.name, value
        for enumerator in value.enumerators:
            yield enumerator.name, enumerator
    elif isinstance(value, _XdrTypedef):
        yield value.declaration.name, value.declaration
    elif isinstance(value, _XdrConstant):
        yield value.name, value


def check_duplicate_definitions(root: "Specification") -> None:
    """Reject a spec that declares an identifier more than once.

    RFC 4506 Section 6.4 places constant and type identifiers in a
    single name space that must be unique within a specification.
    """
    seen = {}
    for definition in root.definitions:
        for name, node in _introduced_names(definition.value):
            where = node if node.line else definition.meta
            first = seen.get(name)
            if first is not None:
                raise XdrSemanticError(
                    f"duplicate identifier '{name}'"
                    f" (first declared at line {_meta_line(first)})",
                    where,
                )
            seen[name] = where


def transform_parse_tree(parse_tree):
    """Transform productions into an abstract syntax tree"""
    ast = transformer.transform(parse_tree)
    ast.definitions = _merge_consecutive_passthru(ast.definitions)
    check_duplicate_definitions(ast)
    return ast


def get_header_name() -> str:
    """Return header name set by pragma header directive"""
    return header_name
