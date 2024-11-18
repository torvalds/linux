#!/usr/bin/env python3
# ex: set filetype=python:

"""Define and implement the Abstract Syntax Tree for the XDR language."""

import sys
from typing import List
from dataclasses import dataclass

from lark import ast_utils, Transformer
from lark.tree import Meta

this_module = sys.modules[__name__]

excluded_apis = []
header_name = "none"
public_apis = []
enums = set()
structs = set()
pass_by_reference = set()


@dataclass
class _XdrAst(ast_utils.Ast):
    """Base class for the XDR abstract syntax tree"""


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
    c_classifier: str


@dataclass
class _XdrDefinedType(_XdrTypeSpecifier):
    """Corresponds to a type defined by the input specification"""


@dataclass
class _XdrBuiltInType(_XdrTypeSpecifier):
    """Corresponds to a built-in XDR type"""


@dataclass
class _XdrDeclaration(_XdrAst):
    """Base class of XDR type declarations"""


@dataclass
class _XdrFixedLengthOpaque(_XdrDeclaration):
    """A fixed-length opaque declaration"""

    name: str
    size: str
    template: str = "fixed_length_opaque"


@dataclass
class _XdrVariableLengthOpaque(_XdrDeclaration):
    """A variable-length opaque declaration"""

    name: str
    maxsize: str
    template: str = "variable_length_opaque"


@dataclass
class _XdrVariableLengthString(_XdrDeclaration):
    """A (NUL-terminated) variable-length string declaration"""

    name: str
    maxsize: str
    template: str = "variable_length_string"


@dataclass
class _XdrFixedLengthArray(_XdrDeclaration):
    """A fixed-length array declaration"""

    name: str
    spec: _XdrTypeSpecifier
    size: str
    template: str = "fixed_length_array"


@dataclass
class _XdrVariableLengthArray(_XdrDeclaration):
    """A variable-length array declaration"""

    name: str
    spec: _XdrTypeSpecifier
    maxsize: str
    template: str = "variable_length_array"


@dataclass
class _XdrOptionalData(_XdrDeclaration):
    """An 'optional_data' declaration"""

    name: str
    spec: _XdrTypeSpecifier
    template: str = "optional_data"


@dataclass
class _XdrBasic(_XdrDeclaration):
    """A 'basic' declaration"""

    name: str
    spec: _XdrTypeSpecifier
    template: str = "basic"


@dataclass
class _XdrVoid(_XdrDeclaration):
    """A void declaration"""

    template: str = "void"


@dataclass
class _XdrConstant(_XdrAst):
    """Corresponds to 'constant_def' in the grammar"""

    name: str
    value: str


@dataclass
class _XdrEnumerator(_XdrAst):
    """An 'identifier = value' enumerator"""

    name: str
    value: str


@dataclass
class _XdrEnum(_XdrAst):
    """An XDR enum definition"""

    name: str
    minimum: int
    maximum: int
    enumerators: List[_XdrEnumerator]


@dataclass
class _XdrStruct(_XdrAst):
    """An XDR struct definition"""

    name: str
    fields: List[_XdrDeclaration]


@dataclass
class _XdrPointer(_XdrAst):
    """An XDR pointer definition"""

    name: str
    fields: List[_XdrDeclaration]


@dataclass
class _XdrTypedef(_XdrAst):
    """An XDR typedef"""

    declaration: _XdrDeclaration


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
        return _XdrIdentifier(children[0].value)

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
        """Instantiate one type_specifier object"""
        c_classifier = ""
        if isinstance(children[0], _XdrIdentifier):
            name = children[0].symbol
            if name in enums:
                c_classifier = "enum "
            if name in structs:
                c_classifier = "struct "
            return _XdrDefinedType(
                type_name=name,
                c_classifier=c_classifier,
            )

        token = children[0].data
        return _XdrBuiltInType(
            type_name=token.value,
            c_classifier=c_classifier,
        )

    def constant_def(self, children):
        """Instantiate one _XdrConstant object"""
        name = children[0].symbol
        value = children[1].value
        return _XdrConstant(name, value)

    # cel: Python can compute a min() and max() for the enumerator values
    #      so that the generated code can perform proper range checking.
    def enum(self, children):
        """Instantiate one _XdrEnum object"""
        enum_name = children[0].symbol
        enums.add(enum_name)

        i = 0
        enumerators = []
        body = children[1]
        while i < len(body.children):
            name = body.children[i].symbol
            value = body.children[i + 1].value
            enumerators.append(_XdrEnumerator(name, value))
            i = i + 2

        return _XdrEnum(enum_name, 0, 0, enumerators)

    def fixed_length_opaque(self, children):
        """Instantiate one _XdrFixedLengthOpaque declaration object"""
        name = children[0].symbol
        size = children[1].value

        return _XdrFixedLengthOpaque(name, size)

    def variable_length_opaque(self, children):
        """Instantiate one _XdrVariableLengthOpaque declaration object"""
        name = children[0].symbol
        if children[1] is not None:
            maxsize = children[1].value
        else:
            maxsize = "0"

        return _XdrVariableLengthOpaque(name, maxsize)

    def variable_length_string(self, children):
        """Instantiate one _XdrVariableLengthString declaration object"""
        name = children[0].symbol
        if children[1] is not None:
            maxsize = children[1].value
        else:
            maxsize = "0"

        return _XdrVariableLengthString(name, maxsize)

    def fixed_length_array(self, children):
        """Instantiate one _XdrFixedLengthArray declaration object"""
        spec = children[0]
        name = children[1].symbol
        size = children[2].value

        return _XdrFixedLengthArray(name, spec, size)

    def variable_length_array(self, children):
        """Instantiate one _XdrVariableLengthArray declaration object"""
        spec = children[0]
        name = children[1].symbol
        if children[2] is not None:
            maxsize = children[2].value
        else:
            maxsize = "0"

        return _XdrVariableLengthArray(name, spec, maxsize)

    def optional_data(self, children):
        """Instantiate one _XdrOptionalData declaration object"""
        spec = children[0]
        name = children[1].symbol
        structs.add(name)
        pass_by_reference.add(name)

        return _XdrOptionalData(name, spec)

    def basic(self, children):
        """Instantiate one _XdrBasic object"""
        spec = children[0]
        name = children[1].symbol

        return _XdrBasic(name, spec)

    def void(self, children):
        """Instantiate one _XdrVoid declaration object"""

        return _XdrVoid()

    def struct(self, children):
        """Instantiate one _XdrStruct object"""
        name = children[0].symbol
        structs.add(name)
        pass_by_reference.add(name)
        fields = children[1].children

        last_field = fields[-1]
        if (
            isinstance(last_field, _XdrOptionalData)
            and name == last_field.spec.type_name
        ):
            return _XdrPointer(name, fields)

        return _XdrStruct(name, fields)

    def typedef(self, children):
        """Instantiate one _XdrTypedef object"""
        new_type = children[0]
        if isinstance(new_type, _XdrBasic) and isinstance(
            new_type.spec, _XdrDefinedType
        ):
            if new_type.spec.type_name in pass_by_reference:
                pass_by_reference.add(new_type.name)

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
        name = children[0].symbol
        structs.add(name)
        pass_by_reference.add(name)

        body = children[1]
        discriminant = body.children[0].children[0]
        cases = body.children[1:-1]
        default = body.children[-1]

        return _XdrUnion(name, discriminant, cases, default)

    def procedure_def(self, children):
        """Instantiate one _RpcProcedure object"""
        result = children[0]
        name = children[1].symbol
        argument = children[2]
        number = children[3].value

        return _RpcProcedure(name, number, argument, result)

    def version_def(self, children):
        """Instantiate one _RpcVersion object"""
        name = children[0].symbol
        number = children[-1].value
        procedures = children[1:-1]

        return _RpcVersion(name, number, procedures)

    def program_def(self, children):
        """Instantiate one _RpcProgram object"""
        name = children[0].symbol
        number = children[-1].value
        versions = children[1:-1]

        return _RpcProgram(name, number, versions)

    def pragma_def(self, children):
        """Instantiate one _Pragma object"""
        directive = children[0].children[0].data
        match directive:
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


transformer = ast_utils.create_transformer(this_module, ParseToAst())


def transform_parse_tree(parse_tree):
    """Transform productions into an abstract syntax tree"""

    return transformer.transform(parse_tree)


def get_header_name() -> str:
    """Return header name set by pragma header directive"""
    return header_name
