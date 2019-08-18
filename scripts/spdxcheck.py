#!/usr/bin/env python
# SPDX-License-Identifier: GPL-2.0
# Copyright Thomas Gleixner <tglx@linutronix.de>

from argparse import ArgumentParser
from ply import lex, yacc
import locale
import traceback
import sys
import git
import re
import os

class ParserException(Exception):
    def __init__(self, tok, txt):
        self.tok = tok
        self.txt = txt

class SPDXException(Exception):
    def __init__(self, el, txt):
        self.el = el
        self.txt = txt

class SPDXdata(object):
    def __init__(self):
        self.license_files = 0
        self.exception_files = 0
        self.licenses = [ ]
        self.exceptions = { }

# Read the spdx data from the LICENSES directory
def read_spdxdata(repo):

    # The subdirectories of LICENSES in the kernel source
    license_dirs = [ "preferred", "other", "exceptions" ]
    lictree = repo.head.commit.tree['LICENSES']

    spdx = SPDXdata()

    for d in license_dirs:
        for el in lictree[d].traverse():
            if not os.path.isfile(el.path):
                continue

            exception = None
            for l in open(el.path).readlines():
                if l.startswith('Valid-License-Identifier:'):
                    lid = l.split(':')[1].strip().upper()
                    if lid in spdx.licenses:
                        raise SPDXException(el, 'Duplicate License Identifier: %s' %lid)
                    else:
                        spdx.licenses.append(lid)

                elif l.startswith('SPDX-Exception-Identifier:'):
                    exception = l.split(':')[1].strip().upper()
                    spdx.exceptions[exception] = []

                elif l.startswith('SPDX-Licenses:'):
                    for lic in l.split(':')[1].upper().strip().replace(' ', '').replace('\t', '').split(','):
                        if not lic in spdx.licenses:
                            raise SPDXException(None, 'Exception %s missing license %s' %(ex, lic))
                        spdx.exceptions[exception].append(lic)

                elif l.startswith("License-Text:"):
                    if exception:
                        if not len(spdx.exceptions[exception]):
                            raise SPDXException(el, 'Exception %s is missing SPDX-Licenses' %excid)
                        spdx.exception_files += 1
                    else:
                        spdx.license_files += 1
                    break
    return spdx

class id_parser(object):

    reserved = [ 'AND', 'OR', 'WITH' ]
    tokens = [ 'LPAR', 'RPAR', 'ID', 'EXC' ] + reserved

    precedence = ( ('nonassoc', 'AND', 'OR'), )

    t_ignore = ' \t'

    def __init__(self, spdx):
        self.spdx = spdx
        self.lasttok = None
        self.lastid = None
        self.lexer = lex.lex(module = self, reflags = re.UNICODE)
        # Initialize the parser. No debug file and no parser rules stored on disk
        # The rules are small enough to be generated on the fly
        self.parser = yacc.yacc(module = self, write_tables = False, debug = False)
        self.lines_checked = 0
        self.checked = 0
        self.spdx_valid = 0
        self.spdx_errors = 0
        self.curline = 0
        self.deepest = 0

    # Validate License and Exception IDs
    def validate(self, tok):
        id = tok.value.upper()
        if tok.type == 'ID':
            if not id in self.spdx.licenses:
                raise ParserException(tok, 'Invalid License ID')
            self.lastid = id
        elif tok.type == 'EXC':
            if id not in self.spdx.exceptions:
                raise ParserException(tok, 'Invalid Exception ID')
            if self.lastid not in self.spdx.exceptions[id]:
                raise ParserException(tok, 'Exception not valid for license %s' %self.lastid)
            self.lastid = None
        elif tok.type != 'WITH':
            self.lastid = None

    # Lexer functions
    def t_RPAR(self, tok):
        r'\)'
        self.lasttok = tok.type
        return tok

    def t_LPAR(self, tok):
        r'\('
        self.lasttok = tok.type
        return tok

    def t_ID(self, tok):
        r'[A-Za-z.0-9\-+]+'

        if self.lasttok == 'EXC':
            print(tok)
            raise ParserException(tok, 'Missing parentheses')

        tok.value = tok.value.strip()
        val = tok.value.upper()

        if val in self.reserved:
            tok.type = val
        elif self.lasttok == 'WITH':
            tok.type = 'EXC'

        self.lasttok = tok.type
        self.validate(tok)
        return tok

    def t_error(self, tok):
        raise ParserException(tok, 'Invalid token')

    def p_expr(self, p):
        '''expr : ID
                | ID WITH EXC
                | expr AND expr
                | expr OR expr
                | LPAR expr RPAR'''
        pass

    def p_error(self, p):
        if not p:
            raise ParserException(None, 'Unfinished license expression')
        else:
            raise ParserException(p, 'Syntax error')

    def parse(self, expr):
        self.lasttok = None
        self.lastid = None
        self.parser.parse(expr, lexer = self.lexer)

    def parse_lines(self, fd, maxlines, fname):
        self.checked += 1
        self.curline = 0
        try:
            for line in fd:
                line = line.decode(locale.getpreferredencoding(False), errors='ignore')
                self.curline += 1
                if self.curline > maxlines:
                    break
                self.lines_checked += 1
                if line.find("SPDX-License-Identifier:") < 0:
                    continue
                expr = line.split(':')[1].strip()
                # Remove trailing comment closure
                if line.strip().endswith('*/'):
                    expr = expr.rstrip('*/').strip()
                # Special case for SH magic boot code files
                if line.startswith('LIST \"'):
                    expr = expr.rstrip('\"').strip()
                self.parse(expr)
                self.spdx_valid += 1
                #
                # Should we check for more SPDX ids in the same file and
                # complain if there are any?
                #
                break

        except ParserException as pe:
            if pe.tok:
                col = line.find(expr) + pe.tok.lexpos
                tok = pe.tok.value
                sys.stdout.write('%s: %d:%d %s: %s\n' %(fname, self.curline, col, pe.txt, tok))
            else:
                sys.stdout.write('%s: %d:0 %s\n' %(fname, self.curline, col, pe.txt))
            self.spdx_errors += 1

def scan_git_tree(tree):
    for el in tree.traverse():
        # Exclude stuff which would make pointless noise
        # FIXME: Put this somewhere more sensible
        if el.path.startswith("LICENSES"):
            continue
        if el.path.find("license-rules.rst") >= 0:
            continue
        if not os.path.isfile(el.path):
            continue
        with open(el.path, 'rb') as fd:
            parser.parse_lines(fd, args.maxlines, el.path)

def scan_git_subtree(tree, path):
    for p in path.strip('/').split('/'):
        tree = tree[p]
    scan_git_tree(tree)

if __name__ == '__main__':

    ap = ArgumentParser(description='SPDX expression checker')
    ap.add_argument('path', nargs='*', help='Check path or file. If not given full git tree scan. For stdin use "-"')
    ap.add_argument('-m', '--maxlines', type=int, default=15,
                    help='Maximum number of lines to scan in a file. Default 15')
    ap.add_argument('-v', '--verbose', action='store_true', help='Verbose statistics output')
    args = ap.parse_args()

    # Sanity check path arguments
    if '-' in args.path and len(args.path) > 1:
        sys.stderr.write('stdin input "-" must be the only path argument\n')
        sys.exit(1)

    try:
        # Use git to get the valid license expressions
        repo = git.Repo(os.getcwd())
        assert not repo.bare

        # Initialize SPDX data
        spdx = read_spdxdata(repo)

        # Initilize the parser
        parser = id_parser(spdx)

    except SPDXException as se:
        if se.el:
            sys.stderr.write('%s: %s\n' %(se.el.path, se.txt))
        else:
            sys.stderr.write('%s\n' %se.txt)
        sys.exit(1)

    except Exception as ex:
        sys.stderr.write('FAIL: %s\n' %ex)
        sys.stderr.write('%s\n' %traceback.format_exc())
        sys.exit(1)

    try:
        if len(args.path) and args.path[0] == '-':
            stdin = os.fdopen(sys.stdin.fileno(), 'rb')
            parser.parse_lines(stdin, args.maxlines, '-')
        else:
            if args.path:
                for p in args.path:
                    if os.path.isfile(p):
                        parser.parse_lines(open(p, 'rb'), args.maxlines, p)
                    elif os.path.isdir(p):
                        scan_git_subtree(repo.head.reference.commit.tree, p)
                    else:
                        sys.stderr.write('path %s does not exist\n' %p)
                        sys.exit(1)
            else:
                # Full git tree scan
                scan_git_tree(repo.head.commit.tree)

            if args.verbose:
                sys.stderr.write('\n')
                sys.stderr.write('License files:     %12d\n' %spdx.license_files)
                sys.stderr.write('Exception files:   %12d\n' %spdx.exception_files)
                sys.stderr.write('License IDs        %12d\n' %len(spdx.licenses))
                sys.stderr.write('Exception IDs      %12d\n' %len(spdx.exceptions))
                sys.stderr.write('\n')
                sys.stderr.write('Files checked:     %12d\n' %parser.checked)
                sys.stderr.write('Lines checked:     %12d\n' %parser.lines_checked)
                sys.stderr.write('Files with SPDX:   %12d\n' %parser.spdx_valid)
                sys.stderr.write('Files with errors: %12d\n' %parser.spdx_errors)

            sys.exit(0)

    except Exception as ex:
        sys.stderr.write('FAIL: %s\n' %ex)
        sys.stderr.write('%s\n' %traceback.format_exc())
        sys.exit(1)
