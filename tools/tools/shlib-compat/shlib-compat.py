#!/usr/bin/env python
#-
# Copyright (c) 2010 Gleb Kurtsou
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

from __future__ import print_function
import os
import sys
import re
import optparse

class Config(object):
    version = '0.1'
    # controlled by user
    verbose = 0
    dump = False
    no_dump = False
    version_filter = None
    symbol_filter = None
    alias_prefixes = []
    # misc opts
    objdump = 'objdump'
    dwarfdump = 'dwarfdump'
    # debug
    cmpcache_enabled = True
    dwarfcache_enabled = True
    w_alias = True
    w_cached = False
    w_symbol = True

    class FileConfig(object):
        filename = None
        out = sys.stdout
        def init(self, outname):
            if outname and outname != '-':
                self.out = open(outname, "w")

    origfile = FileConfig()
    newfile = FileConfig()

    exclude_sym_default = [
            '^__bss_start$',
            '^_edata$',
            '^_end$',
            '^_fini$',
            '^_init$',
            ]

    @classmethod
    def init(cls):
        cls.version_filter = StrFilter()
        cls.symbol_filter = StrFilter()

class App(object):
    result_code = 0

def warn(cond, msg):
    if cond:
        print("WARN: " + msg, file=sys.stderr)

# {{{ misc

class StrFilter(object):
    def __init__(self):
        self.exclude = []
        self.include = []

    def compile(self):
        self.re_exclude = [ re.compile(x) for x in self.exclude ]
        self.re_include = [ re.compile(x) for x in self.include ]

    def match(self, s):
        if len(self.re_include):
            matched = False
            for r in self.re_include:
                if r.match(s):
                    matched = True
                    break
            if not matched:
                return False
        for r in self.re_exclude:
            if r.match(s):
                return False
        return True

class Cache(object):

    class CacheStats(object):
        def __init__(self):
            self.hit = 0
            self.miss = 0

        def show(self, name):
            total = self.hit + self.miss
            if total == 0:
                ratio = '(undef)'
            else:
                ratio = '%f' % (self.hit/float(total))
            return '%s cache stats: hit: %d; miss: %d; ratio: %s' % \
                    (name, self.hit, self.miss, ratio)

    def __init__(self, enabled=True, stats=None):
        self.enabled = enabled
        self.items = {}
        if stats == None:
            self.stats = Cache.CacheStats()
        else:
            self.stats = stats

    def get(self, id):
        if self.enabled and id in self.items:
            self.stats.hit += 1
            return self.items[id]
        else:
            self.stats.miss += 1
            return None

    def put(self, id, obj):
        if self.enabled:
            if id in self.items and obj is not self.items[id]:
                #raise ValueError("Item is already cached: %d (%s, %s)" %
                #        (id, self.items[id], obj))
                warn(Config.w_cached, "Item is already cached: %d (%s, %s)" % \
                        (id, self.items[id], obj))
            self.items[id] = obj

    def replace(self, id, obj):
        if self.enabled:
            assert id in self.items
            self.items[id] = obj

class ListDiff(object):
    def __init__(self, orig, new):
        self.orig = set(orig)
        self.new = set(new)
        self.common = self.orig & self.new
        self.added = self.new - self.common
        self.removed = self.orig - self.common

class PrettyPrinter(object):
    def __init__(self):
        self.stack = []

    def run_nested(self, obj):
        ex = obj._pp_ex(self)
        self.stack.append(ex)

    def run(self, obj):
        self._result = obj._pp(self)
        return self._result

    def nested(self):
        return sorted(set(self.stack))

    def result(self):
        return self._result;

# }}}

#{{{ symbols and version maps

class Symbol(object):
    def __init__(self, name, offset, version, lib):
        self.name = name
        self.offset = offset
        self.version = version
        self.lib = lib
        self.definition = None

    @property
    def name_ver(self):
        return self.name + '@' + self.version

    def __repr__(self):
        return "Symbol(%s, 0x%x, %s)" % (self.name, self.offset, self.version)

class CommonSymbol(object):
    def __init__(self, origsym, newsym):
        if origsym.name != newsym.name or origsym.version != newsym.version:
            raise RuntimeError("Symbols have different names: %s",
                    [origsym, newsym])
        self.origsym = origsym
        self.newsym = newsym
        self.name = newsym.name
        self.version = newsym.version

    def __repr__(self):
        return "CommonSymbol(%s, %s)" % (self.name, self.version)

class SymbolAlias(object):
    def __init__(self, alias, prefix, offset):
        assert alias.startswith(prefix)
        self.alias = alias
        self.name = alias[len(prefix):]
        self.offset = offset

    def __repr__(self):
        return "SymbolAlias(%s, 0x%x)" % (self.alias, self.offset)


class VersionMap(object):
    def __init__(self, name):
        self.name = name
        self.symbols = {}

    def append(self, symbol):
        if (symbol.name in self.symbols):
            raise ValueError("Symbol is already defined %s@%s" %
                    (symbol.name, self.name))
        self.symbols[symbol.name] = symbol

    def names(self):
        return self.symbols.keys()

    def __repr__(self):
        return repr(self.symbols.values())

# }}}

# {{{ types and definitions

class Def(object):
    _is_alias = False

    def __init__(self, id, name, **kwargs):
        self.id = id
        self.name = name
        self.attrs = kwargs

    def __getattr__(self, attr):
        if attr not in self.attrs:
            raise AttributeError('%s in %s' % (attr, str(self)))
        return self.attrs[attr]

    def _name_opt(self, default=''):
        if not self.name:
            return default
        return self.name

    def _alias(self):
        if self._is_alias:
            return self.type._alias()
        return self

    def __cmp__(self, other):
        # TODO assert 'self' and 'other' belong to different libraries
        #print 'cmp defs: %s, %s' % (self, other)
        a = self._alias()
        try:
            b = other._alias()
        except AttributeError:
            return 1
        r = cmp(a.__class__, b.__class__)
        if r == 0:
            if a.id != 0 and b.id != 0:
                ind = (long(a.id) << 32) + b.id
                r = Dwarf.cmpcache.get(ind)
                if r != None:
                    return r
            else:
                ind = 0
            r = cmp(a.attrs, b.attrs)
            if ind != 0:
                Dwarf.cmpcache.put(ind, r)
        else:
            r = 0
            #raise RuntimeError('Comparing different classes: %s, %s' %
            #        (a.__class__.__name__, b.__class__.__name__))
        return r

    def __repr__(self):
        p = []
        if hasattr(self, 'name'):
            p.append("name=%s" % self.name)
        for (k, v) in self.attrs.items():
            if isinstance(v, Def):
                v = v.__class__.__name__ + '(...)'
            p.append("%s=%s" % (k, v))
        return self.__class__.__name__ + '(' + ', '.join(p) + ')'

    def _mapval(self, param, vals):
        if param not in vals.keys():
            raise NotImplementedError("Invalid value '%s': %s" %
                    (param, str(self)))
        return vals[param]

    def _pp_ex(self, pp):
        raise NotImplementedError('Extended pretty print not implemeted: %s' %
                str(self))

    def _pp(self, pp):
        raise NotImplementedError('Pretty print not implemeted: %s' % str(self))

class AnonymousDef(Def):
    def __init__(self, id, **kwargs):
        Def.__init__(self, id, None, **kwargs)

class Void(AnonymousDef):
    _instance = None

    def __new__(cls, *args, **kwargs):
        if not cls._instance:
            cls._instance = super(Void, cls).__new__(
                    cls, *args, **kwargs)
        return cls._instance

    def __init__(self):
        AnonymousDef.__init__(self, 0)

    def _pp(self, pp):
        return "void"

class VarArgs(AnonymousDef):
    def _pp(self, pp):
        return "..."

class PointerDef(AnonymousDef):
    def _pp(self, pp):
        t = pp.run(self.type)
        return "%s*" % (t,)

class BaseTypeDef(Def):
    inttypes = ['DW_ATE_signed', 'DW_ATE_unsigned', 'DW_ATE_unsigned_char']
    def _pp(self, pp):
        if self.encoding in self.inttypes:
            sign = '' if self.encoding == 'DW_ATE_signed' else 'u'
            bits = int(self.byte_size, 0) * 8
            return '%sint%s_t' % (sign, bits)
        elif self.encoding == 'DW_ATE_signed_char' and int(self.byte_size, 0) == 1:
            return 'char';
        elif self.encoding == 'DW_ATE_boolean' and int(self.byte_size, 0) == 1:
            return 'bool';
        elif self.encoding == 'DW_ATE_float':
            return self._mapval(int(self.byte_size, 0), {
                16: 'long double',
                8: 'double',
                4: 'float',
            })
        raise NotImplementedError('Invalid encoding: %s' % self)

class TypeAliasDef(Def):
    _is_alias = True
    def _pp(self, pp):
        alias = self._alias()
        # push typedef name
        if self.name and not alias.name:
            alias.name = 'T(%s)' % self.name
        # return type with modifiers
        return self.type._pp(pp)

class EnumerationTypeDef(Def):
    def _pp(self, pp):
        return 'enum ' + self._name_opt('UNKNOWN')

class ConstTypeDef(AnonymousDef):
    _is_alias = True
    def _pp(self, pp):
        return 'const ' + self.type._pp(pp)

class VolatileTypeDef(AnonymousDef):
    _is_alias = True
    def _pp(self, pp):
        return 'volatile ' + self.type._pp(pp)

class RestrictTypeDef(AnonymousDef):
    _is_alias = True
    def _pp(self, pp):
        return 'restrict ' + self.type._pp(pp)

class ArrayDef(AnonymousDef):
    def _pp(self, pp):
        t = pp.run(self.type)
        assert len(self.subranges) == 1
        try:
            sz = int(self.subranges[0].upper_bound) + 1
        except ValueError:
            s = re.sub(r'\(.+\)', '', self.subranges[0].upper_bound)
            sz = int(s) + 1
        return '%s[%s]' % (t, sz)

class ArraySubrangeDef(AnonymousDef):
    pass

class FunctionDef(Def):
    def _pp(self, pp):
        result = pp.run(self.result)
        if not self.params:
            params = "void"
        else:
            params = ', '.join([ pp.run(x) for x in self.params ])
        return "%s %s(%s);" % (result, self.name, params)

class FunctionTypeDef(Def):
    def _pp(self, pp):
        result = pp.run(self.result)
        if not self.params:
            params = "void"
        else:
            params = ', '.join([ pp.run(x) for x in self.params ])
        return "F(%s, %s, (%s))" % (self._name_opt(), result, params)

class ParameterDef(Def):
    def _pp(self, pp):
        t = pp.run(self.type)
        return "%s %s" % (t, self._name_opt())

class VariableDef(Def):
    def _pp(self, pp):
        t = pp.run(self.type)
        return "%s %s" % (t, self._name_opt())

# TODO
class StructForwardDef(Def):
    pass

class IncompleteDef(Def):
    def update(self, complete, cache=None):
        self.complete = complete
        complete.incomplete = self
        if cache != None:
            cached = cache.get(self.id)
            if cached != None and isinstance(cached, IncompleteDef):
                cache.replace(self.id, complete)

class StructIncompleteDef(IncompleteDef):
    def _pp(self, pp):
        return "struct %s" % (self.name,)

class UnionIncompleteDef(IncompleteDef):
    def _pp(self, pp):
        return "union %s" % (self.name,)

class StructDef(Def):
    def _pp_ex(self, pp, suffix=';'):
        members = [ pp.run(x) for x in self.members ]
        return "struct %s { %s }%s" % \
                (self._name_opt(), ' '.join(members), suffix)
    def _pp(self, pp):
        if self.name:
            pp.run_nested(self)
            return "struct %s" % (self.name,)
        else:
            return self._pp_ex(pp, suffix='')

class UnionDef(Def):
    def _pp_ex(self, pp, suffix=';'):
        members = [ pp.run(x) for x in self.members ]
        return "union %s { %s }%s" % \
                (self._name_opt(), ' '.join(members), suffix)
    def _pp(self, pp):
        if self.name:
            pp.run_nested(self)
            return "union %s" % (self.name,)
        else:
            return self._pp_ex(pp, suffix='')

class MemberDef(Def):
    def _pp(self, pp):
        t = pp.run(self.type)
        if self.bit_size:
            bits = ":%s" % self.bit_size
        else:
            bits = ""
        return "%s %s%s;" % (t, self._name_opt(), bits)

class Dwarf(object):

    cmpcache = Cache(enabled=Config.cmpcache_enabled)

    def __init__(self, dump):
        self.dump = dump

    def _build_optarg_type(self, praw):
        type = praw.optarg('type', Void())
        if type != Void():
            type = self.buildref(praw.unit, type)
        return type

    def build_subprogram(self, raw):
        if raw.optname == None:
            raw.setname('SUBPROGRAM_NONAME_' + raw.arg('low_pc'));
        params = [ self.build(x) for x in raw.nested ]
        result = self._build_optarg_type(raw)
        return FunctionDef(raw.id, raw.name, params=params, result=result)

    def build_variable(self, raw):
        type = self._build_optarg_type(raw)
        return VariableDef(raw.id, raw.optname, type=type)

    def build_subroutine_type(self, raw):
        params = [ self.build(x) for x in raw.nested ]
        result = self._build_optarg_type(raw)
        return FunctionTypeDef(raw.id, raw.optname, params=params, result=result)

    def build_formal_parameter(self, raw):
        type = self._build_optarg_type(raw)
        return ParameterDef(raw.id, raw.optname, type=type)

    def build_pointer_type(self, raw):
        type = self._build_optarg_type(raw)
        return PointerDef(raw.id, type=type)

    def build_member(self, raw):
        type = self.buildref(raw.unit, raw.arg('type'))
        return MemberDef(raw.id, raw.name, type=type,
                bit_size=raw.optarg('bit_size', None))

    def build_structure_type(self, raw):
        incomplete = raw.unit.incomplete.get(raw.id)
        if incomplete == None:
            incomplete = StructIncompleteDef(raw.id, raw.optname)
            raw.unit.incomplete.put(raw.id, incomplete)
        else:
            return incomplete
        members = [ self.build(x) for x in raw.nested ]
        byte_size = raw.optarg('byte_size', None)
        if byte_size == None:
            obj = StructForwardDef(raw.id, raw.name, members=members,
                    forcename=raw.name)
        obj = StructDef(raw.id, raw.optname, members=members,
                byte_size=byte_size)
        incomplete.update(obj, cache=raw.unit.cache)
        return obj

    def build_union_type(self, raw):
        incomplete = raw.unit.incomplete.get(raw.id)
        if incomplete == None:
            incomplete = UnionIncompleteDef(raw.id, raw.optname)
            raw.unit.incomplete.put(raw.id, incomplete)
        else:
            return incomplete
        members = [ self.build(x) for x in raw.nested ]
        byte_size = raw.optarg('byte_size', None)
        obj = UnionDef(raw.id, raw.optname, members=members,
                byte_size=byte_size)
        obj.incomplete = incomplete
        incomplete.complete = obj
        return obj

    def build_typedef(self, raw):
        type = self._build_optarg_type(raw)
        return TypeAliasDef(raw.id, raw.name, type=type)

    def build_const_type(self, raw):
        type = self._build_optarg_type(raw)
        return ConstTypeDef(raw.id, type=type)

    def build_volatile_type(self, raw):
        type = self._build_optarg_type(raw)
        return VolatileTypeDef(raw.id, type=type)

    def build_restrict_type(self, raw):
        type = self._build_optarg_type(raw)
        return RestrictTypeDef(raw.id, type=type)

    def build_enumeration_type(self, raw):
        # TODO handle DW_TAG_enumerator ???
        return EnumerationTypeDef(raw.id, name=raw.optname,
                byte_size=raw.arg('byte_size'))

    def build_base_type(self, raw):
        return BaseTypeDef(raw.id, raw.optname,
                byte_size=raw.arg('byte_size'), encoding=raw.arg('encoding'))

    def build_array_type(self, raw):
        type = self.buildref(raw.unit, raw.arg('type'))
        subranges = [ self.build(x) for x in raw.nested ]
        return ArrayDef(raw.id, type=type, subranges=subranges)

    def build_subrange_type(self, raw):
        type = self.buildref(raw.unit, raw.arg('type'))
        return ArraySubrangeDef(raw.id, type=type,
                upper_bound=raw.optarg('upper_bound', 0))

    def build_unspecified_parameters(self, raw):
        return VarArgs(raw.id)

    def _get_id(self, id):
        try:
            return int(id)
        except ValueError:
            if (id.startswith('<') and id.endswith('>')):
                return int(id[1:-1], 0)
            else:
                raise ValueError("Invalid dwarf id: %s" % id)

    def build(self, raw):
        obj = raw.unit.cache.get(raw.id)
        if obj != None:
            return obj
        builder_name = raw.tag.replace('DW_TAG_', 'build_')
        try:
            builder = getattr(self, builder_name)
        except AttributeError:
            raise AttributeError("Unknown dwarf tag: %s" % raw)
        obj = builder(raw)
        raw.unit.cache.put(obj.id, obj)
        return obj

    def buildref(self, unit, id):
        id = self._get_id(id)
        raw = unit.tags[id]
        obj = self.build(raw)
        return obj

# }}}

class Shlib(object):
    def __init__(self, libfile):
        self.libfile = libfile
        self.versions = {}
        self.alias_syms = {}

    def parse_objdump(self):
        objdump = ObjdumpParser(self.libfile)
        objdump.run()
        for p in objdump.dynamic_symbols:
            vername = p['ver']
            if vername.startswith('(') and vername.endswith(')'):
                vername = vername[1:-1]
            if not Config.version_filter.match(vername):
                continue
            if not Config.symbol_filter.match(p['symbol']):
                continue
            sym = Symbol(p['symbol'], p['offset'], vername, self)
            if vername not in self.versions:
                self.versions[vername] = VersionMap(vername)
            self.versions[vername].append(sym)
        if Config.alias_prefixes:
            self.local_offsetmap = objdump.local_offsetmap
            for p in objdump.local_symbols:
                for prefix in Config.alias_prefixes:
                    if not p['symbol'].startswith(prefix):
                        continue
                    alias = SymbolAlias(p['symbol'], prefix, p['offset'])
                    if alias.name in self.alias_syms:
                        prevalias = self.alias_syms[alias.name]
                        if alias.name != prevalias.name or \
                                alias.offset != prevalias.offset:
                            warn(Config.w_alias, "Symbol alias is " \
                                    "already defined: %s: %s at %08x -- %s at %08x" % \
                                    (alias.alias, alias.name,  alias.offset,
                                            prevalias.name, prevalias.offset))
                    self.alias_syms[alias.name] = alias

    def parse_dwarfdump(self):
        dwarfdump = DwarfdumpParser(self.libfile)
        def lookup(sym):
            raw = None
            try:
                raw = dwarfdump.offsetmap[sym.offset]
            except:
                try:
                    localnames = self.local_offsetmap[sym.offset]
                    localnames.sort(key=lambda x: -len(x))
                    for localname in localnames:
                        if localname not in self.alias_syms:
                            continue
                        alias = self.alias_syms[localname]
                        raw = dwarfdump.offsetmap[alias.offset]
                        break
                except:
                    pass
            return raw
        dwarfdump.run()
        dwarf = Dwarf(dwarfdump)
        for ver in self.versions.values():
            for sym in ver.symbols.values():
                raw = lookup(sym);
                if not raw:
                    warn(Config.w_symbol, "Symbol %s (%s) not found at offset 0x%x" % \
                            (sym.name_ver, self.libfile, sym.offset))
                    continue
                if Config.verbose >= 3:
                    print("Parsing symbol %s (%s)" % (sym.name_ver, self.libfile))
                sym.definition = dwarf.build(raw)

    def parse(self):
        if not os.path.isfile(self.libfile):
            print("No such file: %s" % self.libfile, file=sys.stderr)
            sys.exit(1)
        self.parse_objdump()
        self.parse_dwarfdump()

# {{{ parsers

class Parser(object):
    def __init__(self, proc):
        self.proc = proc
        self.parser = self.parse_begin

    def run(self):
        fd = os.popen(self.proc, 'r')
        while True:
            line = fd.readline()
            if (not line):
                break
            line = line.strip()
            if (line):
                self.parser(line)
        err = fd.close()
        if err:
            print("Execution failed: %s" % self.proc, file=sys.stderr)
            sys.exit(2)

    def parse_begin(self, line):
        print(line)

class ObjdumpParser(Parser):

    re_header = re.compile('(?P<table>\w*)\s*SYMBOL TABLE:')

    re_local_symbol = re.compile('(?P<offset>[0-9a-fA-F]+)\s+(?P<bind>\w+)\s+(?P<type>\w+)\s+(?P<section>[^\s]+)\s+(?P<foffset>[0-9a-fA-F]+)\s*(?P<symbol>[^\s]*)')
    re_lame_symbol = re.compile('(?P<offset>[0-9a-fA-F]+)\s+(?P<bind>\w+)\s+\*[A-Z]+\*')

    re_dynamic_symbol = re.compile('(?P<offset>[0-9a-fA-F]+)\s+(?P<bind>\w+)\s+(?P<type>\w+)\s+(?P<section>[^\s]+)\s+(?P<foffset>[0-9a-fA-F]+)\s*(?P<ver>[^\s]*)\s*(?P<symbol>[^\s]*)')

    def __init__(self, libfile):
        Parser.__init__(self, "%s -wtT %s" % (Config.objdump, libfile))
        self.dynamic_symbols = []
        self.local_symbols = []
        self.local_offsetmap = {}

    def parse_begin(self, line):
        self.parse_header(line)

    def add_symbol(self, table, symbol, offsetmap = None):
        offset = int(symbol['offset'], 16);
        symbol['offset'] = offset
        if (offset == 0):
            return
        table.append(symbol)
        if offsetmap != None:
            if offset not in offsetmap:
                offsetmap[offset] = [symbol['symbol']]
            else:
                offsetmap[offset].append(symbol['symbol'])

    def parse_header(self, line):
        m = self.re_header.match(line)
        if (m):
            table = m.group('table')
            if (table == "DYNAMIC"):
                self.parser = self.parse_dynamic
            elif table == '':
                self.parser = self.parse_local
            else:
                raise ValueError("Invalid symbol table: %s" % table)
            return True
        return False

    def parse_local(self, line):
        if (self.parse_header(line)):
            return
        if (self.re_lame_symbol.match(line)):
            return
        m = self.re_local_symbol.match(line)
        if (not m):
            return
            #raise ValueError("Invalid symbol definition: %s" % line)
        p = m.groupdict()
        if (p['symbol'] and p['symbol'].find('@') == -1):
            self.add_symbol(self.local_symbols, p, self.local_offsetmap);

    def parse_dynamic(self, line):
        if (self.parse_header(line)):
            return
        if (self.re_lame_symbol.match(line)):
            return
        m = self.re_dynamic_symbol.match(line)
        if (not m):
            raise ValueError("Invalid symbol definition: %s" % line)
        p = m.groupdict()
        if (p['symbol'] and p['ver']):
            self.add_symbol(self.dynamic_symbols, p);

class DwarfdumpParser(Parser):

    tagcache_stats = Cache.CacheStats()

    class Unit(object):
        def __init__(self):
            self.cache = Cache(enabled=Config.dwarfcache_enabled,
                    stats=DwarfdumpParser.tagcache_stats)
            self.incomplete = Cache()
            self.tags = {}

    class Tag(object):
        def __init__(self, unit, data):
            self.unit = unit
            self.id = int(data['id'], 0)
            self.level = int(data['level'])
            self.tag = data['tag']
            self.args = {}
            self.nested = []

        @property
        def name(self):
            return self.arg('name')

        @property
        def optname(self):
            return self.optarg('name', None)

        def setname(self, name):
            self.args['DW_AT_name'] = name

        def arg(self, a):
            name = 'DW_AT_' + a
            try:
                return self.args[name]
            except KeyError:
                raise KeyError("Argument '%s' not found in %s: %s" %
                        (name, self, self.args))

        def optarg(self, a, default):
            try:
                return self.arg(a)
            except KeyError:
                return default

        def __repr__(self):
            return "Tag(%d, %d, %s)" % (self.level, self.id, self.tag)

    re_header = re.compile('<(?P<level>\d+)><(?P<id>[0xX0-9a-fA-F]+(?:\+(0[xX])?[0-9a-fA-F]+)?)><(?P<tag>\w+)>')
    re_argname = re.compile('(?P<arg>\w+)<')
    re_argunknown = re.compile('<Unknown AT value \w+><[^<>]+>')

    skip_tags = set([
        'DW_TAG_lexical_block',
        'DW_TAG_inlined_subroutine',
        'DW_TAG_label',
        'DW_TAG_variable',
        ])

    external_tags = set([
        'DW_TAG_variable',
        ])

    def __init__(self, libfile):
        Parser.__init__(self, "%s -di %s" % (Config.dwarfdump, libfile))
        self.current_unit = None
        self.offsetmap = {}
        self.stack = []

    def parse_begin(self, line):
        if line == '.debug_info':
            self.parser = self.parse_debuginfo
        else:
            raise ValueError("Invalid dwarfdump header: %s" % line)

    def parse_argvalue(self, args):
        assert args.startswith('<')
        i = 1
        cnt = 1
        while i < len(args) and args[i]:
            if args[i] == '<':
                cnt += 1
            elif args[i] == '>':
                cnt -= 1
                if cnt == 0:
                    break
            i = i + 1
        value = args[1:i]
        args = args[i+1:]
        return (args, value)

    def parse_arg(self, tag, args):
        m = self.re_argname.match(args)
        if not m:
            m = self.re_argunknown.match(args)
            if not m:
                raise ValueError("Invalid dwarfdump: couldn't parse arguments: %s" %
                        args)
            args = args[len(m.group(0)):].lstrip()
            return args
        argname = m.group('arg')
        args = args[len(argname):]
        value = []
        while len(args) > 0 and args.startswith('<'):
            (args, v) = self.parse_argvalue(args)
            value.append(v)
        args = args.lstrip()
        if len(value) == 1:
            value = value[0]
        tag.args[argname] = value
        return args

    def parse_debuginfo(self, line):
        m = self.re_header.match(line)
        if not m:
            raise ValueError("Invalid dwarfdump: %s" % line)
        if m.group('level') == '0':
            self.current_unit = DwarfdumpParser.Unit()
            return
        tag = DwarfdumpParser.Tag(self.current_unit, m.groupdict())
        args = line[len(m.group(0)):].lstrip()
        while args:
            args = self.parse_arg(tag, args)
        tag.unit.tags[tag.id] = tag
        def parse_offset(tag):
            if 'DW_AT_low_pc' in tag.args:
                return int(tag.args['DW_AT_low_pc'], 16)
            elif 'DW_AT_location' in tag.args:
                location = tag.args['DW_AT_location']
                if location.startswith('DW_OP_addr'):
                    return int(location.replace('DW_OP_addr', ''), 16)
            return None
        offset = parse_offset(tag)
        if offset is not None and \
                (tag.tag not in DwarfdumpParser.skip_tags or \
                ('DW_AT_external' in tag.args and \
                tag.tag in DwarfdumpParser.external_tags)):
            if offset in self.offsetmap:
                raise ValueError("Dwarf dump parse error: " +
                        "symbol is already defined at offset 0x%x" % offset)
            self.offsetmap[offset] = tag
        if len(self.stack) > 0:
            prev = self.stack.pop()
            while prev.level >= tag.level and len(self.stack) > 0:
                prev = self.stack.pop()
            if prev.level < tag.level:
                assert prev.level == tag.level - 1
                # TODO check DW_AT_sibling ???
                if tag.tag not in DwarfdumpParser.skip_tags:
                    prev.nested.append(tag)
                self.stack.append(prev)
        self.stack.append(tag)
        assert len(self.stack) == tag.level

# }}}

def list_str(l):
    l = [ str(x) for x in l ]
    l.sort()
    return ', '.join(l)

def names_ver_str(vername, names):
    return list_str([ x + "@" + vername for x in names ])

def common_symbols(origlib, newlib):
    result = []
    verdiff = ListDiff(origlib.versions.keys(), newlib.versions.keys())
    if Config.verbose >= 1:
        print('Original versions:   ', list_str(verdiff.orig))
        print('New versions:        ', list_str(verdiff.new))
    for vername in verdiff.added:
        print('Added version:       ', vername)
        print('    Added symbols:   ', \
                names_ver_str(vername, newlib.versions[vername].names()))
    for vername in verdiff.removed:
        print('Removed version:     ', vername)
        print('    Removed symbols: ', \
                names_ver_str(vername, origlib.versions[vername].names()))
    added = []
    removed = []
    for vername in verdiff.common:
        origver = origlib.versions[vername]
        newver = newlib.versions[vername]
        namediff = ListDiff(origver.names(), newver.names())
        if namediff.added:
            added.append(names_ver_str(vername, namediff.added))
        if namediff.removed:
            removed.append(names_ver_str(vername, namediff.removed))
        commonver = VersionMap(vername)
        result.append(commonver)
        for n in namediff.common:
            sym = CommonSymbol(origver.symbols[n], newver.symbols[n])
            commonver.append(sym)
    if added:
        print('Added symbols:')
        for i in added:
            print('    ', i)
    if removed:
        print('Removed symbols:')
        for i in removed:
            print('    ', i)
    return result

def cmp_symbols(commonver):
    for ver in commonver:
        names = ver.names();
        names.sort()
        for symname in names:
            sym = ver.symbols[symname]
            missing = sym.origsym.definition is None or sym.newsym.definition is None
            match = not missing and sym.origsym.definition == sym.newsym.definition
            if not match:
                App.result_code = 1
            if Config.verbose >= 1 or not match:
                if missing:
                    print('%s: missing definition' % \
                            (sym.origsym.name_ver,))
                    continue
                print('%s: definitions %smatch' % \
                        (sym.origsym.name_ver, "" if match else "mis"))
                if Config.dump or (not match and not Config.no_dump):
                    for x in [(sym.origsym, Config.origfile),
                            (sym.newsym, Config.newfile)]:
                        xsym = x[0]
                        xout = x[1].out
                        if not xsym.definition:
                            print('\n// Definition not found: %s %s' % \
                                    (xsym.name_ver, xsym.lib.libfile), file=xout)
                            continue
                        print('\n// Definitions mismatch: %s %s' % \
                                (xsym.name_ver, xsym.lib.libfile), file=xout)
                        pp = PrettyPrinter()
                        pp.run(xsym.definition)
                        for i in pp.nested():
                            print(i, file=xout)
                        print(pp.result(), file=xout)

def dump_symbols(commonver):
    class SymbolDump(object):
        def __init__(self, io_conf):
            self.io_conf = io_conf
            self.pp = PrettyPrinter()
            self.res = []
        def run(self, sym):
            r = self.pp.run(sym.definition)
            self.res.append('/* %s@%s */ %s' % (sym.name, sym.version, r))
        def finish(self):
            print('\n// Symbol dump: version %s, library %s' % \
                    (ver.name, self.io_conf.filename), file=self.io_conf.out)
            for i in self.pp.nested():
                print(i, file=self.io_conf.out)
            print('', file=self.io_conf.out)
            for i in self.res:
                print(i, file=self.io_conf.out)
    for ver in commonver:
        names = sorted(ver.names());
        d_orig = SymbolDump(Config.origfile)
        d_new = SymbolDump(Config.newfile)
        for symname in names:
            sym = ver.symbols[symname]
            if not sym.origsym.definition or not sym.newsym.definition:
                # XXX
                warn(Config.w_symbol, 'Missing symbol definition: %s@%s' % \
                        (symname, ver.name))
                continue
            d_orig.run(sym.origsym)
            d_new.run(sym.newsym)
        d_orig.finish()
        d_new.finish()

if __name__ == '__main__':
    Config.init()
    parser = optparse.OptionParser(usage="usage: %prog origlib newlib",
            version="%prog " + Config.version)
    parser.add_option('-v', '--verbose', action='count',
            help="verbose mode, may be specified several times")
    parser.add_option('--alias-prefix', action='append',
            help="name prefix to try for symbol alias lookup", metavar="STR")
    parser.add_option('--dump', action='store_true',
            help="dump symbol definitions")
    parser.add_option('--no-dump', action='store_true',
            help="disable dump for mismatched symbols")
    parser.add_option('--out-orig', action='store',
            help="result output file for original library", metavar="ORIGFILE")
    parser.add_option('--out-new', action='store',
            help="result output file for new library", metavar="NEWFILE")
    parser.add_option('--dwarfdump', action='store',
            help="path to dwarfdump executable", metavar="DWARFDUMP")
    parser.add_option('--objdump', action='store',
            help="path to objdump executable", metavar="OBJDUMP")
    parser.add_option('--exclude-ver', action='append', metavar="RE")
    parser.add_option('--include-ver', action='append', metavar="RE")
    parser.add_option('--exclude-sym', action='append', metavar="RE")
    parser.add_option('--include-sym', action='append', metavar="RE")
    parser.add_option('--no-exclude-sym-default', action='store_true',
            help="don't exclude special symbols like _init, _end, __bss_start")
    for opt in ['alias', 'cached', 'symbol']:
        parser.add_option("--w-" + opt,
                action="store_true", dest="w_" + opt)
        parser.add_option("--w-no-" + opt,
                action="store_false", dest="w_" + opt)
    (opts, args) = parser.parse_args()

    if len(args) != 2:
        parser.print_help()
        sys.exit(-1)
    if opts.dwarfdump:
        Config.dwarfdump = opts.dwarfdump
    if opts.objdump:
        Config.objdump = opts.objdump
    if opts.out_orig:
        Config.origfile.init(opts.out_orig)
    if opts.out_new:
        Config.newfile.init(opts.out_new)
    if opts.no_dump:
        Config.dump = False
        Config.no_dump = True
    if opts.dump:
        Config.dump = True
        Config.no_dump = False
        Config.verbose = 1
    if opts.verbose:
        Config.verbose = opts.verbose
    if opts.alias_prefix:
        Config.alias_prefixes = opts.alias_prefix
        Config.alias_prefixes.sort(key=lambda x: -len(x))
    for (k, v) in ({ '_sym': Config.symbol_filter,
            '_ver': Config.version_filter }).items():
        for a in [ 'exclude', 'include' ]:
            opt = getattr(opts, a + k)
            if opt:
                getattr(v, a).extend(opt)
    if not opts.no_exclude_sym_default:
        Config.symbol_filter.exclude.extend(Config.exclude_sym_default)
    Config.version_filter.compile()
    Config.symbol_filter.compile()
    for w in ['w_alias', 'w_cached', 'w_symbol']:
        if hasattr(opts, w):
            v = getattr(opts, w)
            if v != None:
                setattr(Config, w, v)

    (Config.origfile.filename, Config.newfile.filename) = (args[0], args[1])

    origlib = Shlib(Config.origfile.filename)
    origlib.parse()
    newlib = Shlib(Config.newfile.filename)
    newlib.parse()

    commonver = common_symbols(origlib, newlib)
    if Config.dump:
        dump_symbols(commonver)
    cmp_symbols(commonver)
    if Config.verbose >= 4:
        print(Dwarf.cmpcache.stats.show('Cmp'))
        print(DwarfdumpParser.tagcache_stats.show('Dwarf tag'))

    sys.exit(App.result_code)
