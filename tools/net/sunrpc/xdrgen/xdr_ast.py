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

# (type_name, member_name) pairs whose variable-length array or
# optional-data list member is marked "pragma aggregate" -- codec
# emission streams the member through application hooks instead of
# iterating a materialized C array.
aggregate_members = set()

# Source position of each "pragma aggregate" marker, so a directive
# that cannot be honored is reported where it was written.
aggregate_member_meta = {}

# (type_name, member_name) pairs marked "pragma pages": the member's
# content resides in the pages of the Receive or Reply buffer, so
# the emitted codec captures or inserts those pages by reference
# instead of copying.
pages_members = set()

# The same pairs, keyed to the directive's identifier node so a
# diagnostic can point at the directive rather than at the type.
pages_member_meta = {}

# Names of types reachable from an RPC procedure argument, closed over
# the types their members reach. A "pragma pages" member of one is
# decoded in place rather than encoded by reference, and the two need
# different C representations; see pages_member_is_decoded().
argument_types = set()

# Names of types reachable from an RPC procedure result, closed the
# same way. A marked member of a type in both sets would need both
# representations at once, so the front end rejects it.
result_types = set()

# Typedefs of a variable-length opaque or string, mapped to the length
# bound. A "pragma pages" member declared through one ("path data;"
# with "typedef string path<NFS_MAXPATHLEN>") reaches the AST as a
# basic type reference, so the bound must be recovered from here.
varlen_object_typedefs = {}

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
        if isinstance(self.declaration, (_XdrVariableLengthOpaque, _XdrString)):
            varlen_object_typedefs[self.declaration.name] = self.declaration.maxsize
        if isinstance(self.declaration, _XdrBasic):
            new_type = self.declaration
            if isinstance(new_type.spec, _XdrDefinedType):
                if new_type.spec.type_name in pass_by_reference:
                    pass_by_reference.add(new_type.name)
                if new_type.spec.type_name in varlen_object_typedefs:
                    varlen_object_typedefs[new_type.name] = varlen_object_typedefs[
                        new_type.spec.type_name
                    ]
                max_widths[new_type.name] = self.max_width()
                symbolic_widths[new_type.name] = self.symbolic_width()


def pages_member_maxsize(field: _XdrDeclaration):
    """Return the length bound of a "pragma pages" member.

    Return None when FIELD is neither a variable-length opaque nor a
    string, whether declared inline or through a typedef.
    """
    if isinstance(field, (_XdrVariableLengthOpaque, _XdrString)):
        return field.maxsize
    if isinstance(field, _XdrBasic) and field.spec.type_name in varlen_object_typedefs:
        return varlen_object_typedefs[field.spec.type_name]
    return None


def pages_member_is_decoded(struct_name: str, member_name: str) -> bool:
    """Return True when a "pragma pages" member is decoded in place.

    A server decodes arguments and encodes results. A page-resident
    member of an argument type is decoded from the pages of the
    Receive buffer into an xdr_buf that captures the content by
    reference; one of a result type is encoded from the Reply buffer's
    pages and keeps the ordinary opaque or string representation.
    """
    return (struct_name, member_name) in pages_members and (
        struct_name in argument_types
    )


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
    number: int
    argument: _XdrTypeSpecifier
    result: _XdrTypeSpecifier


@dataclass
class _RpcVersion(_XdrAst):
    """RPC version definition"""

    name: str
    number: int
    procedures: List[_RpcProcedure]


@dataclass
class _RpcProgram(_XdrAst):
    """RPC program definition"""

    name: str
    number: int
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

        argument_types.add(argument.type_name)
        result_types.add(result.type_name)
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
            case "aggregate_directive":
                if children[2] is None:
                    raise XdrSemanticError(
                        "pragma aggregate requires a type name and a member name",
                        children[1],
                    )
                marked = (children[1].symbol, children[2].symbol)
                aggregate_members.add(marked)
                aggregate_member_meta[marked] = children[2]
            case "pages_directive":
                if children[2] is None:
                    raise XdrSemanticError(
                        "pragma pages requires a type name and a member name",
                        children[1],
                    )
                marked = (children[1].symbol, children[2].symbol)
                pages_members.add(marked)
                pages_member_meta[marked] = children[2]
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
    elif isinstance(value, _RpcProgram):
        yield value.name, value


def _check_rpc_scope_names(program: "_RpcProgram") -> None:
    """Enforce RFC 5531 Section 12.3 scoping within an RPC program.

    A version name and number are unique within the program and a
    procedure name and number are unique within its version.
    """
    version_names = set()
    version_numbers = set()
    for version in program.versions:
        if version.name in version_names:
            raise XdrSemanticError(
                f"duplicate version name '{version.name}'"
                f" in program '{program.name}'",
                version,
            )
        version_names.add(version.name)
        if version.number in version_numbers:
            raise XdrSemanticError(
                f"duplicate version number {version.number}"
                f" in program '{program.name}'",
                version,
            )
        version_numbers.add(version.number)
        procedure_names = set()
        procedure_numbers = set()
        for procedure in version.procedures:
            if procedure.name in procedure_names:
                raise XdrSemanticError(
                    f"duplicate procedure name '{procedure.name}'"
                    f" in version '{version.name}'",
                    procedure,
                )
            procedure_names.add(procedure.name)
            if procedure.number in procedure_numbers:
                raise XdrSemanticError(
                    f"duplicate procedure number {procedure.number}"
                    f" in version '{version.name}'",
                    procedure,
                )
            procedure_numbers.add(procedure.number)


def check_duplicate_definitions(root: "Specification") -> None:
    """Reject a spec that declares an identifier more than once.

    RFC 4506 Section 6.4 places constant and type identifiers in a
    single name space that must be unique within a specification.
    RFC 5531 Section 12.3 adds RPC program names to that name space
    and scopes version names and numbers to their program and
    procedure names and numbers to their version.
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
        if isinstance(definition.value, _RpcProgram):
            _check_rpc_scope_names(definition.value)


# RFC 5531 (Section 9) encodes program, version, and procedure numbers
# as unsigned 32-bit integers, so each must fall within [0, 2**32 - 1].
_RPC_NUMBER_MAX = 2**32 - 1


def _check_rpc_number(kind: str, number: int, scope: str, meta) -> None:
    """Reject one RPC number that is negative or wider than 32 bits."""
    if number < 0:
        raise XdrSemanticError(
            f"negative {kind} number {number} {scope}",
            meta,
        )
    if number > _RPC_NUMBER_MAX:
        raise XdrSemanticError(
            f"{kind} number {number} {scope} exceeds {_RPC_NUMBER_MAX}",
            meta,
        )


def check_rpc_number_range(root: "Specification") -> None:
    """Reject an out-of-range program, version, or procedure number.

    RFC 5531 assigns only unsigned constants to program, version, and
    procedure numbers (Section 12.3) and encodes each as an unsigned
    32-bit integer (Section 9). RFC 4506 Section 6.2 permits a signed
    decimal constant for XDR constants in general and sets no ceiling on
    magnitude, so the grammar accepts an out-of-range value; the range
    is enforced here instead. The parser retains no per-version or
    per-procedure source location, so a violation is reported against the
    program definition.
    """
    for definition in root.definitions:
        program = definition.value
        if not isinstance(program, _RpcProgram):
            continue
        _check_rpc_number(
            "program",
            program.number,
            f"in program '{program.name}'",
            definition.meta,
        )
        for version in program.versions:
            _check_rpc_number(
                "version",
                version.number,
                f"in program '{program.name}'",
                definition.meta,
            )
            for procedure in version.procedures:
                _check_rpc_number(
                    "procedure",
                    procedure.number,
                    f"in version '{version.name}'",
                    definition.meta,
                )


def _pages_candidate_members(value):
    """Yield (member_name, declaration, container) for a type's members.

    CONTAINER is the AST node holding the declaration; it decides how
    the member is rendered in the target language.
    """
    if isinstance(value, _XdrStruct):
        for field in value.fields:
            yield field.name, field, value
    elif isinstance(value, _XdrPointer):
        # The trailing field is the self-reference that makes this an
        # XDR pointer type; the emitter does not encode it.
        for field in value.fields[0:-1]:
            yield field.name, field, value
    elif isinstance(value, _XdrUnion):
        cases = list(value.cases)
        if value.default is not None:
            cases.append(value.default)
        for case in cases:
            if not isinstance(case.arm, _XdrVoid):
                yield case.arm.name, case.arm, value


def check_pages_directives(root: "Specification") -> None:
    """Reject a "pragma pages" directive that cannot be honored.

    Run in the front end so the diagnostic carries the directive's own
    source position rather than surfacing as a traceback from whichever
    emitter reaches the member. A directive that binds to nothing would
    otherwise degrade silently to the copying codec.
    """
    payloads = {}
    resolved = set()
    for definition in root.definitions:
        value = definition.value
        type_name = getattr(value, "name", None)
        members = dict(
            (name, (field, container))
            for name, field, container in _pages_candidate_members(value)
        )
        for marked in sorted(pages_members):
            if marked[0] != type_name:
                continue
            meta = pages_member_meta.get(marked)
            if marked[1] not in members:
                raise XdrSemanticError(
                    f"type '{type_name}' has no member '{marked[1]}'",
                    meta,
                )
            field, container = members[marked[1]]
            # A union arm declared directly as a string is generated as
            # a char *, with no length field for the page encoder to
            # read, and the union generator has no arm representation
            # for a direct opaque at all; inside a struct either
            # becomes a { len, data } object.
            if isinstance(container, _XdrUnion) and isinstance(
                field, (_XdrString, _XdrVariableLengthOpaque)
            ):
                raise XdrSemanticError(
                    f"union arm '{type_name}.{marked[1]}' is declared"
                    " directly as a string or opaque; declare it through"
                    " a typedef instead",
                    meta,
                )
            # A decoded member and an encoded one take different C
            # representations, and a type carries only one.
            if type_name in argument_types and type_name in result_types:
                raise XdrSemanticError(
                    f"'{type_name}' is reachable from both an RPC"
                    " argument and an RPC result; a page-resident"
                    " member needs distinct argument and result types",
                    meta,
                )
            # The union generator does not consult
            # pages_member_is_decoded(), so a marked arm would reach the
            # copying decoder and pull the payload through the bounded
            # scratch buffer.
            if isinstance(container, _XdrUnion) and type_name in argument_types:
                raise XdrSemanticError(
                    f"union arm '{type_name}.{marked[1]}' cannot be"
                    " decoded in place; declare a page-resident member"
                    " of an argument type in a struct instead",
                    meta,
                )
            if pages_member_maxsize(field) is None:
                raise XdrSemanticError(
                    f"'{type_name}.{marked[1]}' is neither a"
                    " variable-length opaque nor a string",
                    meta,
                )
            # svcxdr_encode_opaque_payload() consumes the whole page
            # vector and moves the stream into the tail; a second
            # payload would overwrite the first one's framing.
            first = payloads.get(type_name)
            if first is not None:
                raise XdrSemanticError(
                    f"'{type_name}' already marks member '{first}' as"
                    " page-resident; an encoder emits at most one"
                    " page-resident payload",
                    meta,
                )
            payloads[type_name] = marked[1]
            resolved.add(marked)

    for marked in sorted(pages_members - resolved):
        raise XdrSemanticError(
            f"pragma pages names unknown type '{marked[0]}'",
            pages_member_meta.get(marked),
        )

    _check_pages_containment(root, payloads)


def _member_type_names(value) -> list:
    """Return the type names a definition's members reference, one
    entry per member so a type used twice is counted twice. An array
    member's encoder emits the element type once per element, so its
    element type is counted twice as well."""
    if isinstance(value, _XdrStruct):
        fields = value.fields
    elif isinstance(value, _XdrPointer):
        # The trailing self-reference is list framing that the
        # caller's loop walks, not a member the pointer's own
        # encoder emits.
        fields = value.fields[0:-1]
    elif isinstance(value, _XdrUnion):
        fields = [case.arm for case in value.cases]
        if value.default:
            fields.append(value.default.arm)
    elif isinstance(value, _XdrTypedef):
        fields = [value.declaration]
    else:
        return []
    names = []
    for field in fields:
        spec = getattr(field, "spec", None)
        if spec is not None:
            names.append(spec.type_name)
            if isinstance(field, (_XdrFixedLengthArray, _XdrVariableLengthArray)):
                names.append(spec.type_name)
    return names


def _reachable_payloads(type_name, members, payloads, active) -> list:
    """Return the marked members an encoder for TYPE_NAME emits, one
    entry per path that reaches one, so a marked type held by two
    members appears twice."""
    if type_name in active:
        return []
    active.add(type_name)
    found = []
    if type_name in payloads:
        found.append((type_name, payloads[type_name]))
    for member_type in members.get(type_name, ()):
        found += _reachable_payloads(member_type, members, payloads, active)
    active.discard(type_name)
    return found


def _check_pages_containment(root: "Specification", payloads: dict) -> None:
    """Reject a type whose encoder reaches more than one page-resident
    payload through the types its members contain.

    The one-payload check in check_pages_directives() sees only the
    directly marked type. A type that holds two members of a marked
    type, or members of two marked types, would call
    svcxdr_encode_opaque_payload() once per member and overwrite the
    first payload's framing just as a second marked member would.
    """
    members = {}
    for definition in root.definitions:
        value = definition.value
        name = getattr(value, "name", None)
        if name is None and isinstance(value, _XdrTypedef):
            name = value.declaration.name
        if name is not None:
            members[name] = _member_type_names(value)

    for name in members:
        reached = _reachable_payloads(name, members, payloads, set())
        if len(reached) > 1:
            raise XdrSemanticError(
                f"'{name}' reaches {len(reached)} page-resident payloads"
                " through its members; an encoder emits at most one"
                " page-resident payload",
                pages_member_meta.get(reached[1]),
            )


def _named_type_definitions(root: "Specification") -> dict:
    """Return the specification's named type definitions, keyed by
    the name a member's type specifier refers to."""
    named = {}
    for definition in root.definitions:
        value = definition.value
        if isinstance(value, _XdrTypedef):
            named[value.declaration.name] = value
        elif isinstance(value, (_XdrStruct, _XdrUnion, _XdrPointer)):
            named[value.name] = value
    return named


def _unsized_member(type_name: str, named_types: dict, seen: set):
    """Return "<type>.<member>" for the first variable-length array or
    optional-data member reachable from TYPE_NAME, or None when the
    type's storage is entirely inline."""
    if type_name in seen:
        return None
    seen.add(type_name)
    value = named_types.get(type_name)
    if isinstance(value, _XdrTypedef):
        declaration = value.declaration
        if isinstance(declaration, (_XdrVariableLengthArray, _XdrOptionalData)):
            return declaration.name
        spec = getattr(declaration, "spec", None)
        if spec is None:
            return None
        return _unsized_member(spec.type_name, named_types, seen)
    if isinstance(value, _XdrStruct):
        fields = value.fields
    elif isinstance(value, _XdrPointer):
        # The trailing self-reference is list framing the element
        # decoder does not decode, so it needs no storage.
        fields = value.fields[0:-1]
    elif isinstance(value, _XdrUnion):
        fields = [case.arm for case in value.cases]
        if value.default:
            fields.append(value.default.arm)
    else:
        return None
    for field in fields:
        if isinstance(field, (_XdrVariableLengthArray, _XdrOptionalData)):
            return f"{type_name}.{field.name}"
        spec = getattr(field, "spec", None)
        if spec is None:
            continue
        found = _unsized_member(spec.type_name, named_types, seen)
        if found is not None:
            return found
    return None


def check_aggregate_directives(root: "Specification") -> None:
    """Reject a "pragma aggregate" directive that cannot be honored.

    As with the pages checks, this runs in the front end so a directive
    naming a missing type or member, or a member with no hook-driven
    codec, is reported at its own source position. Left to the
    emitters, an unbound directive would degrade silently to the
    materializing codec, and a malformed one would surface as a
    traceback from whichever emitter reached it.
    """
    if aggregate_members and header_name == "none":
        raise XdrSemanticError(
            "pragma aggregate derives its external hook symbols from the"
            " pragma header name, which this specification does not set",
            aggregate_member_meta.get(min(aggregate_members)),
        )

    named_types = _named_type_definitions(root)
    resolved = set()
    for definition in root.definitions:
        value = definition.value
        if not isinstance(value, _XdrStruct):
            continue
        fields = dict((field.name, field) for field in value.fields)
        element_type = None
        for marked in sorted(aggregate_members):
            if marked[0] != value.name:
                continue
            meta = aggregate_member_meta.get(marked)
            if marked[1] not in fields:
                raise XdrSemanticError(
                    f"type '{value.name}' has no member '{marked[1]}'",
                    meta,
                )
            field = fields[marked[1]]
            # Any other member form would emit hook prototypes that
            # nothing calls.
            if not isinstance(
                field, (_XdrVariableLengthArray, _XdrOptionalData)
            ):
                raise XdrSemanticError(
                    f"'{value.name}.{marked[1]}' is neither a"
                    " variable-length array nor an optional-data list",
                    meta,
                )
            if isinstance(field, _XdrVariableLengthArray):
                # The generated decoder hands each element to the
                # ordinary element decoder from a zero-filled local, so
                # a nested variable-length array or optional-data
                # member, which that decoder fills through a pointer
                # the caller was to supply, would be written through
                # NULL.
                nested = _unsized_member(
                    field.spec.type_name, named_types, set()
                )
                if nested is not None:
                    raise XdrSemanticError(
                        f"'{value.name}.{marked[1]}' has element type"
                        f" '{field.spec.type_name}', which reaches the"
                        f" variable-length or optional-data member"
                        f" '{nested}'; the aggregate decoder has no"
                        " storage for it",
                        meta,
                    )
                # The decoder hands the wire count to the begin hook
                # before any element is decoded, and a begin hook may
                # size storage from it, so the count needs a bound the
                # framing enforces.
                if field.maxsize == "0":
                    raise XdrSemanticError(
                        f"'{value.name}.{marked[1]}' declares no maximum"
                        " size; the aggregate decoder hands the element"
                        " count to the begin hook unbounded",
                        meta,
                    )
            else:
                # The optional-data encoder passes each element by
                # address and closes the list by encoding NULL, so the
                # element codec must be one that takes a pointer and
                # writes the value-follows framing itself.
                if not isinstance(
                    named_types.get(field.spec.type_name), _XdrPointer
                ):
                    raise XdrSemanticError(
                        f"'{value.name}.{marked[1]}' has element type"
                        f" '{field.spec.type_name}', which is not a pointer"
                        " type; an optional-data aggregate needs an element"
                        " codec that writes the value-follows framing",
                        meta,
                    )
                # The emitted optional-data decoder fails outright, so
                # an argument type carrying one could never be decoded.
                if value.name in argument_types:
                    raise XdrSemanticError(
                        f"'{value.name}' is reachable from an RPC argument,"
                        f" and the optional-data aggregate '{marked[1]}'"
                        " cannot be decoded",
                        meta,
                    )
            if element_type is None:
                element_type = field.spec.type_name
            elif field.spec.type_name != element_type:
                raise XdrSemanticError(
                    f"'{value.name}.{marked[1]}' has element type"
                    f" '{field.spec.type_name}', but '{value.name}' already"
                    f" marks a member of element type '{element_type}';"
                    " one hook set serves all of a type's marked members",
                    meta,
                )
            resolved.add(marked)

    for marked in sorted(aggregate_members - resolved):
        raise XdrSemanticError(
            f"pragma aggregate names unknown struct '{marked[0]}'",
            aggregate_member_meta.get(marked),
        )


def _referenced_type_names(value) -> set:
    """Return the type names an aggregate references through its
    members."""
    if isinstance(value, (_XdrStruct, _XdrPointer)):
        fields = value.fields
    elif isinstance(value, _XdrUnion):
        fields = [case.arm for case in value.cases]
        if value.default:
            fields.append(value.default.arm)
    else:
        return set()
    names = set()
    for field in fields:
        spec = getattr(field, "spec", None)
        if spec is not None:
            names.add(spec.type_name)
    return names


def _expand_procedure_types(root: "Specification") -> None:
    """Close argument_types and result_types over the types reachable
    from an RPC argument or result.

    A "pragma pages" member decoded in place can sit in a struct nested
    within an argument type (symlinkdata3 within SYMLINK3args), so the
    top-level argument types alone do not suffice.
    """
    references = {}
    for definition in root.definitions:
        value = definition.value
        name = getattr(value, "name", None)
        if name is not None:
            references[name] = _referenced_type_names(value)
        elif isinstance(value, _XdrTypedef):
            # An argument named through an alias reaches whatever
            # aggregate the alias resolves to.
            declaration = value.declaration
            spec = getattr(declaration, "spec", None)
            if spec is not None:
                references[declaration.name] = {spec.type_name}

    for types in (argument_types, result_types):
        worklist = list(types)
        while worklist:
            for referenced in references.get(worklist.pop(), ()):
                if referenced in references and referenced not in types:
                    types.add(referenced)
                    worklist.append(referenced)


def transform_parse_tree(parse_tree):
    """Transform productions into an abstract syntax tree"""
    ast = transformer.transform(parse_tree)
    ast.definitions = _merge_consecutive_passthru(ast.definitions)
    check_duplicate_definitions(ast)
    check_rpc_number_range(ast)
    # The pages checks consult argument_types and result_types, so
    # close those sets over the nested aggregates before the
    # directives are validated.
    _expand_procedure_types(ast)
    check_pages_directives(ast)
    check_aggregate_directives(ast)
    return ast


def get_header_name() -> str:
    """Return header name set by pragma header directive"""
    return header_name
