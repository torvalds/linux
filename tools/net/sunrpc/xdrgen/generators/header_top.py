#!/usr/bin/env python3
# ex: set filetype=python:

"""Generate header top boilerplate"""

import os.path
import time

from generators import Boilerplate, header_guard_infix
from generators import create_jinja2_environment, get_jinja2_template
from xdr_ast import Specification


class XdrHeaderTopGenerator(Boilerplate):
    """Generate header boilerplate"""

    def __init__(self, language: str, peer: str):
        """Initialize an instance of this class"""
        self.environment = create_jinja2_environment(language, "header_top")
        self.peer = peer

    def emit_declaration(self, filename: str, root: Specification) -> None:
        """Emit the top header guard"""
        template = get_jinja2_template(self.environment, "declaration", "header")
        print(
            template.render(
                infix=header_guard_infix(filename),
                filename=filename,
                mtime=time.ctime(os.path.getmtime(filename)),
            )
        )

    def emit_definition(self, filename: str, root: Specification) -> None:
        """Emit the top header guard"""
        template = get_jinja2_template(self.environment, "definition", "header")
        print(
            template.render(
                infix=header_guard_infix(filename),
                filename=filename,
                mtime=time.ctime(os.path.getmtime(filename)),
            )
        )

    def emit_source(self, filename: str, root: Specification) -> None:
        pass
