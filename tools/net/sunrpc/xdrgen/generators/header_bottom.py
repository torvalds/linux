#!/usr/bin/env python3
# ex: set filetype=python:

"""Generate header bottom boilerplate"""

import os.path
import time

from generators import Boilerplate, header_guard_infix
from generators import create_jinja2_environment, get_jinja2_template
from xdr_ast import Specification


class XdrHeaderBottomGenerator(Boilerplate):
    """Generate header boilerplate"""

    def __init__(self, language: str, peer: str):
        """Initialize an instance of this class"""
        self.environment = create_jinja2_environment(language, "header_bottom")
        self.peer = peer

    def emit_declaration(self, filename: str, root: Specification) -> None:
        """Emit the bottom header guard"""
        template = get_jinja2_template(self.environment, "declaration", "header")
        print(template.render(infix=header_guard_infix(filename)))

    def emit_definition(self, filename: str, root: Specification) -> None:
        """Emit the bottom header guard"""
        template = get_jinja2_template(self.environment, "definition", "header")
        print(template.render(infix=header_guard_infix(filename)))

    def emit_source(self, filename: str, root: Specification) -> None:
        pass
