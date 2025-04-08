#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright Thomas Gleixner <tglx@linutronix.de>

from argparse import ArgumentParser
from ply import lex, yacc
import locale
import traceback
import fnmatch
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

class dirinfo(object):
    def __init__(self):
        self.missing = 0
        self.total = 0
        self.files = []

    def update(self, fname, basedir, miss):
        self.total += 1
        self.missing += miss
        if miss:
            fname = './' + fname
            bdir = os.path.dirname(fname)
            if bdir == basedir.rstrip('/'):
                self.files.append(fname)

# Read the spdx data from the LICENSES directory
def read_spdxdata(repo):

    # The subdirectories of LICENSES in the kernel source
    # Note: exceptions needs to be parsed as last directory.
    license_dirs = [ "preferred", "dual", "deprecated", "exceptions" ]
    lictree = repo.head.commit.tree['LICENSES']

    spdx = SPDXdata()

    for d in license_dirs:
        for el in lictree[d].traverse():
            if not os.path.isfile(el.path):
                continue

            exception = None
            for l in open(el.path, encoding="utf-8").readlines():
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
                            raise SPDXException(None, 'Exception %s missing license %s' %(exception, lic))
                        spdx.exceptions[exception].append(lic)

                elif l.startswith("License-Text:"):
                    if exception:
                        if not len(spdx.exceptions[exception]):
                            raise SPDXException(el, 'Exception %s is missing SPDX-Licenses' %exception)
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
        self.excluded = 0
        self.spdx_valid = 0
        self.spdx_errors = 0
        self.spdx_dirs = {}
        self.dirdepth = -1
        self.basedir = '.'
        self.curline = 0
        self.deepest = 0

    def set_dirinfo(self, basedir, dirdepth):
        if dirdepth >= 0:
            self.basedir = basedir
            bdir = basedir.lstrip('./').rstrip('/')
            if bdir != '':
                parts = bdir.split('/')
            else:
                parts = []
            self.dirdepth = dirdepth + len(parts)

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
        fail = 1
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
                # Remove trailing xml comment closure
                if line.strip().endswith('-->'):
                    expr = expr.rstrip('-->').strip()
                # Remove trailing Jinja2 comment closure
                if line.strip().endswith('#}'):
                    expr = expr.rstrip('#}').strip()
                # Special case for SH magic boot code files
                if line.startswith('LIST \"'):
                    expr = expr.rstrip('\"').strip()
                # Remove j2 comment closure
                if line.startswith('{#'):
                    expr = expr.rstrip('#}').strip()
                self.parse(expr)
                self.spdx_valid += 1
                #
                # Should we check for more SPDX ids in the same file and
                # complain if there are any?
                #
                fail = 0
                break

        except ParserException as pe:
            if pe.tok:
                col = line.find(expr) + pe.tok.lexpos
                tok = pe.tok.value
                sys.stdout.write('%s: %d:%d %s: %s\n' %(fname, self.curline, col, pe.txt, tok))
            else:
                sys.stdout.write('%s: %d:0 %s\n' %(fname, self.curline, pe.txt))
            self.spdx_errors += 1

        if fname == '-':
            return

        base = os.path.dirname(fname)
        if self.dirdepth > 0:
            parts = base.split('/')
            i = 0
            base = '.'
            while i < self.dirdepth and i < len(parts) and len(parts[i]):
                base += '/' + parts[i]
                i += 1
        elif self.dirdepth == 0:
            base = self.basedir
        else:
            base = './' + base.rstrip('/')
        base += '/'

        di = self.spdx_dirs.get(base, dirinfo())
        di.update(fname, base, fail)
        self.spdx_dirs[base] = di

class pattern(object):
    def __init__(self, line):
        self.pattern = line
        self.match = self.match_file
        if line == '.*':
            self.match = self.match_dot
        elif line.endswith('/'):
            self.pattern = line[:-1]
            self.match = self.match_dir
        elif line.startswith('/'):
            self.pattern = line[1:]
            self.match = self.match_fn

    def match_dot(self, fpath):
        return os.path.basename(fpath).startswith('.')

    def match_file(self, fpath):
        return os.path.basename(fpath) == self.pattern

    def match_fn(self, fpath):
        return fnmatch.fnmatchcase(fpath, self.pattern)

    def match_dir(self, fpath):
        if self.match_fn(os.path.dirname(fpath)):
            return True
        return fpath.startswith(self.pattern)

def exclude_file(fpath):
    for rule in exclude_rules:
        if rule.match(fpath):
            return True
    return False

def scan_git_tree(tree, basedir, dirdepth):
    parser.set_dirinfo(basedir, dirdepth)
    for el in tree.traverse():
        if not os.path.isfile(el.path):
            continue
        if exclude_file(el.path):
            parser.excluded += 1
            continue
        with open(el.path, 'rb') as fd:
            parser.parse_lines(fd, args.maxlines, el.path)

def scan_git_subtree(tree, path, dirdepth):
    for p in path.strip('/').split('/'):
        tree = tree[p]
    scan_git_tree(tree, path.strip('/'), dirdepth)

def read_exclude_file(fname):
    rules = []
    if not fname:
        return rules
    with open(fname) as fd:
        for line in fd:
            line = line.strip()
            if line.startswith('#'):
                continue
            if not len(line):
                continue
            rules.append(pattern(line))
    return rules

if __name__ == '__main__':

    ap = ArgumentParser(description='SPDX expression checker')
    ap.add_argument('path', nargs='*', help='Check path or file. If not given full git tree scan. For stdin use "-"')
    ap.add_argument('-d', '--dirs', action='store_true',
                    help='Show [sub]directory statistics.')
    ap.add_argument('-D', '--depth', type=int, default=-1,
                    help='Directory depth for -d statistics. Default: unlimited')
    ap.add_argument('-e', '--exclude',
                    help='File containing file patterns to exclude. Default: scripts/spdxexclude')
    ap.add_argument('-f', '--files', action='store_true',
                    help='Show files without SPDX.')
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

        # Initialize the parser
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
        fname = args.exclude
        if not fname:
            fname = os.path.join(os.path.dirname(__file__), 'spdxexclude')
        exclude_rules = read_exclude_file(fname)
    except Exception as ex:
        sys.stderr.write('FAIL: Reading exclude file %s: %s\n' %(fname, ex))
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
                        scan_git_subtree(repo.head.reference.commit.tree, p,
                                         args.depth)
                    else:
                        sys.stderr.write('path %s does not exist\n' %p)
                        sys.exit(1)
            else:
                # Full git tree scan
                scan_git_tree(repo.head.commit.tree, '.', args.depth)

            ndirs = len(parser.spdx_dirs)
            dirsok = 0
            if ndirs:
                for di in parser.spdx_dirs.values():
                    if not di.missing:
                        dirsok += 1

            if args.verbose:
                sys.stderr.write('\n')
                sys.stderr.write('License files:     %12d\n' %spdx.license_files)
                sys.stderr.write('Exception files:   %12d\n' %spdx.exception_files)
                sys.stderr.write('License IDs        %12d\n' %len(spdx.licenses))
                sys.stderr.write('Exception IDs      %12d\n' %len(spdx.exceptions))
                sys.stderr.write('\n')
                sys.stderr.write('Files excluded:    %12d\n' %parser.excluded)
                sys.stderr.write('Files checked:     %12d\n' %parser.checked)
                sys.stderr.write('Lines checked:     %12d\n' %parser.lines_checked)
                if parser.checked:
                    pc = int(100 * parser.spdx_valid / parser.checked)
                    sys.stderr.write('Files with SPDX:   %12d %3d%%\n' %(parser.spdx_valid, pc))
                    missing = parser.checked - parser.spdx_valid
                    mpc = int(100 * missing / parser.checked)
                    sys.stderr.write('Files without SPDX:%12d %3d%%\n' %(missing, mpc))
                sys.stderr.write('Files with errors: %12d\n' %parser.spdx_errors)
                if ndirs:
                    sys.stderr.write('\n')
                    sys.stderr.write('Directories accounted: %8d\n' %ndirs)
                    pc = int(100 * dirsok / ndirs)
                    sys.stderr.write('Directories complete:  %8d %3d%%\n' %(dirsok, pc))

            if ndirs and ndirs != dirsok and args.dirs:
                if args.verbose:
                    sys.stderr.write('\n')
                sys.stderr.write('Incomplete directories: SPDX in Files\n')
                for f in sorted(parser.spdx_dirs.keys()):
                    di = parser.spdx_dirs[f]
                    if di.missing:
                        valid = di.total - di.missing
                        pc = int(100 * valid / di.total)
                        sys.stderr.write('    %-80s: %5d of %5d  %3d%%\n' %(f, valid, di.total, pc))

            if ndirs and ndirs != dirsok and args.files:
                if args.verbose or args.dirs:
                    sys.stderr.write('\n')
                sys.stderr.write('Files without SPDX:\n')
                for f in sorted(parser.spdx_dirs.keys()):
                    di = parser.spdx_dirs[f]
                    for f in sorted(di.files):
                        sys.stderr.write('    %s\n' %f)

            sys.exit(0)

    except Exception as ex:
        sys.stderr.write('FAIL: %s\n' %ex)
        sys.stderr.write('%s\n' %traceback.format_exc())
        sys.exit(1)
