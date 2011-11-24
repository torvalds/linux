#!/usr/bin/env python
#
# Convert firmware AXF (ELF) file to bin format
#
# $Id: axfbin.py 15628 2010-06-11 14:23:05Z frgi $
import sys
import os
import struct
from getopt import getopt
from cStringIO import StringIO
import re
import time

mib_table_data = ""

class S:
    def __repr__(self):
        r = []
        for k, v in self.__dict__.items():
            r.append('%s=%s' % (k, repr(v)))
        return 'S(' + ', '.join(r) + ')'

def pread(f, offset, len):
    pos = f.tell()
    f.seek(offset)
    buf = f.read(len)
    f.seek(pos)
    return buf
    

def pread_unpack(f, offset, data):
    len = struct.calcsize(data)
    return struct.unpack(data, pread(f, offset, len))

def read_unpack(f, data):
    len = struct.calcsize(data)
    return struct.unpack(data, f.read(len))
    

def Elf32_Ehdr(f, offset):
    z = pread_unpack(f, offset, '<16sHHIIIIIHHHHHH')
    r = S()
    r.e_ident = z[0]
    r.e_type = z[1]
    r.e_machine = z[2]

    r.e_version = z[3]
    r.e_entry = z[4]
    r.e_phoff = z[5]
    r.e_shoff = z[6]
    r.e_flags = z[7]
    r.e_ehsize = z[8]
    r.e_phentsize = z[9]
    r.e_phnum = z[10]
    r.e_shentsize = z[11]
    r.e_shnum = z[12]
    r.e_shstrndx = z[13]
    return r

EM_ARM = 40
EM_NORC = 0x625

def Elf32_Phdr(f, offset):
    z = pread_unpack(f, offset, '<IIIIIIII')
    r = S()
    r.p_type = z[0]
    r.p_offset = z[1]
    r.p_vaddr = z[2]
    r.p_paddr = z[3]
    r.p_filesz = z[4]
    r.p_memsz = z[5]
    r.p_flags = z[6]
    r.p_align = z[7]
    return r

SHT_NULL     = 0    
SHT_PROGBITS = 1
SHT_SYMTAB   = 2
SHT_STRTAB   = 3
SHT_RELA     = 4
SHT_HASH     = 5
SHT_DYNAMIC  = 6
SHT_NOTE     = 7
SHT_NOBITS   = 8
SHT_REL      = 9
SHT_SHLIB    = 10
SHT_DYNSYM   = 11

def Elf32_Shdr(f, offset):
    z = pread_unpack(f, offset, '<IIIIIIIIII')
    r = S()
    r.sh_name = z[0]
    r.sh_type = z[1]
    r.sh_flags = z[2]
    r.sh_addr = z[3]
    r.sh_offset = z[4]
    r.sh_size = z[5]
    r.sh_link = z[6]
    r.sh_info = z[7]
    r.sh_addralign = z[8]
    r.sh_entsize = z[9]
    return r

def Elf32_Sym(f):
    z = read_unpack(f, '<IIIBBH')
    r = S()
    r.st_name = z[0]
    r.st_value = z[1]
    r.st_size = z[2]
    r.st_info = z[3]
    r.st_other = z[4]
    r.st_shndx = z[5]
    return r

SHN_LORESERVE = 0xff00
SHN_HIRESERVE = 0xffff

def hexdump(data):
    printable = ''.join(map(chr, range(33, 127)))
    data_len = len(data)
    dump = []
    while data:
        r = data[:8]
        data = data[8:]
        s = '   '
        s += ', '.join(map(lambda x: '0x%02x' % x, map(ord, r)))
        if data:
            s += ', '
        else:
            s += ' ' * (2 + 6 * (8 - len(r)))
        cm = ''
        for c in r:
            if c in printable:
                cm += c
            else:
                cm += '.'
        if '*/' in cm:
            cm = cm.replace('*/', './')
        if '/*' in cm:
            cm = cm.replace('/*', '/.')
        s += '/* %s */' % cm
        dump.append(s)
    return '\n'.join(dump)

def load_section(f, shdr):
    return pread(f, shdr.sh_offset, shdr.sh_size)

def zstr(tab, index):
    l = tab[index:].index('\0')
    return tab[index:index+l]

BM_SIZE = 16

PAYLOAD_MAXSIZE = 64 - 4

arm_descr = S()
arm_descr.start_exec_addr = 0x46000
arm_descr.bm_base         = 0x46100
arm_descr.trig_swi_addr   = 0x72040
arm_descr.trig_swi_data   = 0xffffffff
arm_descr.product_version = None

norc_rom4_descr = S()
norc_rom4_descr.start_exec_addr = 0x4fffc
norc_rom4_descr.bm_base         = 0
norc_rom4_descr.trig_swi_addr   = 0xfffc1030
norc_rom4_descr.trig_swi_data   = 2
norc_rom4_descr.product_version = 'Release nrx2_rom_4 Build 6294 Feb 14 2010 14:19:17 (BB=NRX2:3,RF=NRX600:4,BTCOEX,Compiler Version 4.1.2 Nanoradio.7)\0'

norc_rom3_descr = S()
norc_rom3_descr.start_exec_addr = 0x46000
norc_rom3_descr.bm_base         = 0x4ff90
norc_rom3_descr.trig_swi_addr   = 0xfffc1030
norc_rom3_descr.trig_swi_data   = 2
norc_rom3_descr.product_version = 'Release _release Build 5507 Oct  8 2009 17:23:03 (BB=NRX2:3,RF=NRX600:4,BTCOEX,Compiler Version 4.1.2 Nanoradio.5)\0'


id = '$Id: axfbin.py 15628 2010-06-11 14:23:05Z frgi $'.split()
try:
    script_name = id[1]
    script_revision = id[2]
except:
    print 'unknown script revision, did you get this from trac?'
    script_name = 'axfbin.py'
    script_revision = '0'
script_name = script_name.replace(',v','') ## fix for CVS
    

new_format = True
do_byteswap = False
scatter_dlm = False


if sys.argv[0]:
    template = os.path.join(os.path.dirname(sys.argv[0]), 'template')
else:
    template = 'template'

dlm_choices = []

opts, args = getopt(sys.argv[1:], 'r:d:b:v:o:s', ['old-format', 'template=', 'scatter-dlm'])

if len(args):
    input = args[0]
else:
    input = 'x_mac.axf'

radio = None
baseband = None
sw_version = None
output = None

for o, a in opts:
    if o == '-o':
        output = a
    elif o == '-r':
        radio = a
    elif o == '-b':
        baseband = a
    elif o == '-v':
        sw_version = a
    elif o == '-s':
        do_byteswap = True
    elif o == '--old-format':
        new_format = False
    elif o == '-d':
        dlm_choices.append(a)
    elif o == '--template':
        template = a
    elif o == '--scatter-dlm':
        scatter_dlm = True
    else:
        print >> sys.stderr, "unknown option %s" % o
        sys.exit(1)

if not output:
    print >> sys.stderr, "please specify output filename (-o)"
    sys.exit(1)
    
base, ext = os.path.splitext(output)

c_source = False
if ext == '.c' or ext == '.h':
    c_source = True
    template = open(template).read()

f = open(input, 'rb')

ehdr = Elf32_Ehdr(f, 0)

### find product_version symbol
product_version = None
patch_version = None
is_xtest = False

## parse all section headers
shdrs = []
for i in range(ehdr.e_shnum):
    shdrs.append(Elf32_Shdr(f, ehdr.e_shoff + i * ehdr.e_shentsize))

if ehdr.e_phoff == ehdr.e_shoff:
    ### WTF ADS!!
    print 'Detected broken stripped firmware image, attempting repair'
    broken_count = (ehdr.e_phentsize * ehdr.e_phnum + ehdr.e_shentsize - 1) / ehdr.e_shentsize
    for shdr in shdrs[:broken_count]:
        shdr.sh_name = 0
        shdr.sh_type = SHT_NULL
        shdr.sh_flags = 0
        shdr.sh_addr = 0
        shdr.sh_offset = 0
        shdr.sh_size = 0
        shdr.sh_link = 0
        shdr.sh_info = 0
        shdr.sh_addralign = 0
        shdr.sh_entsize = 0

if ehdr.e_machine == EM_ARM:
    use_descr = arm_descr
elif ehdr.e_machine == EM_NORC:
    use_descr = norc_rom4_descr ## default

## load section header string table
shstrtab = load_section(f, shdrs[ehdr.e_shstrndx])

strtab = None
symtab = None

## find .strtab and .symtab sections
for s in shdrs:
    name = zstr(shstrtab, s.sh_name)
    if name == '.strtab':
        strtab = load_section(f, s)
    if name == '.symtab':
        symtab = load_section(f, s)


if symtab and strtab:
    ## walk symbol table until we find product_version
    st = StringIO(symtab)
    while st.tell() < len(symtab):
        sym = Elf32_Sym(st)
        if sym.st_info != 0x11: ## STB_GLOBAL | STT_OBJECT
            continue
        name = zstr(strtab, sym.st_name)
        if name == 'patch_version':
            shdr = shdrs[sym.st_shndx]
            patch_version = pread(f, sym.st_value - shdr.sh_addr + shdr.sh_offset, sym.st_size)
            continue
        if name == 'product_version':
            if SHN_LORESERVE <= sym.st_shndx <= SHN_HIRESERVE:
                if ehdr.e_machine == EM_NORC:
                    # icky, but there is no version information in patch
                    # files at this time
                    if sym.st_value == 0x1ac2a:
                        use_descr = norc_rom3_descr
            else:
                shdr = shdrs[sym.st_shndx]
                product_version = pread(f, sym.st_value - shdr.sh_addr + shdr.sh_offset, sym.st_size)
            continue
    st.close()

if patch_version is not None:
    patch_version = zstr(patch_version, 0)
    m = re.match(r'@\(#\) Patch NRX2_ROM_VERSION_(?P<release>\w+).*', patch_version)
    if m:
        sw_version = m.group(1)
        sw_version = '.'.join(sw_version.split('_'))

if not product_version and use_descr.product_version:
    product_version = use_descr.product_version

if product_version:
    product_version = zstr(product_version, 0)
    print product_version
    m = re.match(r'Release (?P<release>\w+).*\(HW=(?P<radio>\d+):(?P<bb>\d+)(?P<var>[^)]*)\)', product_version)
    if not m:
        # NRX600
        m = re.match(r'Release (?P<release>\w+).*\(BB=(?P<bb>[a-zA-Z0-9:]+),RF=(?P<radio>[a-zA-Z0-9:]+)(?P<var>[^)]*)\)', product_version)

    if m:
        if not radio:
            radio = m.group('radio')
            if radio == '2':
                radio = 'nrx510a'
            elif radio == '3':
                radio = 'nrx511b'
            elif radio == '4':
                radio = 'nrx511c'
            print 'radio version appears to be %s (override with -r)' % radio
        if not baseband:
            baseband = m.group('bb')
            if baseband == '2':
                baseband = 'nrx701b'
            elif baseband == '3':
                baseband = 'nrx701c'
            print 'baseband version appears to be %s (override with -b)' % baseband
        if not sw_version:
            sw_version = m.group('release')
            m2 = re.search(r'(\d+(_\d+)+)', product_version)
            if m2:
                sw_version = '.'.join(m2.group(1).split('_'))
            print 'software version appears to be %s (override with -v)' % sw_version
            if sw_version == 'nrx2_rom_4' or sw_version == '_release':
                sw_version = None
        if m.group('var'):
            var = m.group('var').split(',')
            if 'P' in var:
                is_xtest = True
        
## this is for the boilerplate
tmp = ''
if product_version:
    tmp += '    "%s"\n' % product_version
if patch_version :
    tmp += '    "%s"\n' % patch_version
product_version = tmp

if c_source:
    do_exit = False
    if not radio:
        print >> sys.stderr, "please specify radio version (-r)"
        do_exit = True
    if not baseband:
        print >> sys.stderr, "please specify baseband version (-b)"
        do_exit = True
    if not sw_version:
        print >> sys.stderr, "please specify software version (-v)"
        do_exit = True
    if do_exit:
        sys.exit(1)
    

def fix_version(s, code):
    if not s:
        return s
    if s.startswith('nrx') or s.startswith('NRX'):
        return s
    if not s[0].isdigit():
        s = code + s
    s = 'nrx' + s
    return s

radio = fix_version(radio, '511')
baseband = fix_version(baseband, '701')

def bm_descr(buf_ptr, ctrl, size, exp_size, next):
    return struct.pack('<IIHHI', buf_ptr, ctrl, size, exp_size, next)

def load_header(bm_base):
    return bm_descr(bm_base + BM_SIZE, 0, 3 * BM_SIZE, 0, bm_base + BM_SIZE) + \
        bm_descr(bm_base + 3 * BM_SIZE + 8, 0, 2, 0, bm_base + 2 * BM_SIZE) + \
        bm_descr(bm_base + 3 * BM_SIZE + 0, 0, 4, 0, bm_base + 3 * BM_SIZE) + \
        bm_descr(0, 0, 0, 0, bm_base + BM_SIZE)


if use_descr.bm_base != 0:
    bin = load_header(use_descr.bm_base)
else:
    bin = ''

def make_data_header(f, addr, offset, data_len):
    s = ''
    while data_len > 0:
        len = min(data_len, PAYLOAD_MAXSIZE)
        s += struct.pack('<HI', len, addr)
        s += pread(f, offset, len)
        offset += len
        addr += len
        data_len -= len
    return s
        

def is_dlm_segment(ehdr, phdr):
    return ehdr.e_machine == EM_ARM and phdr.p_paddr >= 0x1000000
def is_mib_table(ehdr, phdr):
    return phdr.p_paddr == 0x40000

dlm_groups = {}

DLM_GROUP_MASK  = 0xf0000000
DLM_GROUP_SHIFT = 28
DLM_ID_MASK     = 0x0f000000
DLM_ID_SHIFT    = 24
DLM_ADDR_MASK   =  0x00ffffff

def write_phdr_to_file(f, phdr, filename):
    global base
    global mib_table_data 
    filename = base + '_' + filename
    print 'writing %u byte segment to "%s"' % (phdr.p_filesz, filename)
    data = pread(f, phdr.p_offset, phdr.p_filesz)
    open(filename, 'wb').write(data)
    mib_table_data = hexdump(data)
    
    
for i in range(ehdr.e_phnum):
    phdr = Elf32_Phdr(f, ehdr.e_phoff + i * ehdr.e_phentsize)
    if is_dlm_segment(ehdr, phdr):
        ## this is a dynamic load module
        ## we normally don't include this in
        ## the binary image, but see below
        g = (phdr.p_paddr & DLM_GROUP_MASK) >> DLM_GROUP_SHIFT
        i = (phdr.p_paddr & DLM_ID_MASK) >> DLM_ID_SHIFT
        if not dlm_groups.has_key(g):
            dlm_groups[g] = {}
        dlm_groups[g][i] = phdr
        ## mask out group and id
        if scatter_dlm:
            write_phdr_to_file(f, phdr, 'dlm_%08x' % phdr.p_paddr)
        phdr.p_vaddr &= DLM_ADDR_MASK
        phdr.p_paddr &= DLM_ADDR_MASK
    elif is_mib_table(ehdr, phdr):
        if scatter_dlm:
            write_phdr_to_file(f, phdr, 'mib_table')
        bin += make_data_header(f, phdr.p_paddr, phdr.p_offset, phdr.p_filesz)
    else:
        bin += make_data_header(f, phdr.p_paddr, phdr.p_offset, phdr.p_filesz)

if dlm_groups:
    ## there are DLM groups segments in the firmware
    ## try to figure out if we should include any of them
    dlm_shdr = {}

    ## traverse section headers looking for sections matching the
    ## program headers, this is not really necessary, but is done to
    ## get a name for each DLM

    for shdr in shdrs:
        if shdr.sh_type != SHT_PROGBITS:
            continue
        for dlm_group in sorted(dlm_groups.keys()):
            for dlm_id, phdr in sorted(dlm_groups[dlm_group].items()):
                if shdr.sh_offset == phdr.p_offset:
                    ## this section had the same file offset as a DLM
                    ## phdr, so include it in the list of DLM shdrs,
                    ## and look up its name
                    dlm_shdr[(dlm_group, dlm_id)] = shdr
                    shdr.sh_namestr = zstr(shstrtab, shdr.sh_name)

    ## now walk all DLM groups/ids, and figure out if any has been
    ## selected for inclusion
    for dlm_group in sorted(dlm_groups.keys()):
        selected = None
        for dlm_id, phdr in sorted(dlm_groups[dlm_group].items()):
            ix = '%u.%u' % (dlm_group, dlm_id)
            # if auto is selected for inclusion, pick first (lowest
            # numbered) DLM id from each group
            if 'auto' in dlm_choices and selected is None:
                selected = (dlm_id, ix, phdr)
            if ix in dlm_choices:
                selected = (dlm_id, ix, phdr)
                break
            shdr = dlm_shdr.get((dlm_group, dlm_id), None)
            if shdr and shdr.sh_namestr in dlm_choices:
                selected = (dlm_id, shdr.sh_namestr, phdr)
                break
        # print options for each group
        print 'DLM group %u' % dlm_group
        for dlm_id, phdr in sorted(dlm_groups[dlm_group].items()):
            s = '%u:' % dlm_id
            shdr = dlm_shdr.get((dlm_group, dlm_id), None)
            s += ' select with -d %u.%u' % (dlm_group, dlm_id)
            if shdr:
                s += ' or -d %s' % shdr.sh_namestr
            if selected and selected[2] == phdr:
                s += ' (selected)'
            print ' ', s
        # include in binary blob
        if selected:
            phdr = selected[2]
            bin += make_data_header(f, phdr.p_paddr,
                                    phdr.p_offset, phdr.p_filesz)

    
bin += struct.pack('<HII', 4, use_descr.start_exec_addr, ehdr.e_entry)
bin += struct.pack('<HII', 4, use_descr.trig_swi_addr, use_descr.trig_swi_data)

f.close()

## replace chars that can't be part of a symbol
def fix_symbol(s):
    return ''.join(map(lambda x: ['_',x][x.isalnum()], s))

if is_xtest:
    fw_name = 'firmware_%s_test' % radio
    variant = '-test'
else:
    fw_name = 'firmware_%s' % radio
    variant = ''

fw_name = fix_symbol(fw_name)

from array import array
def byteswap(data):
    assert len(data) % 2 == 0, 'need even sized data'
    a = array('H', data)
    a.byteswap()
    return a.tostring()

if do_byteswap:
    bin = byteswap(bin)
    byteorder = 1
else:
    byteorder = 0

print 'radio = %(radio)s, baseband = %(baseband)s, version = %(sw_version)s' % globals()
if not c_source:
    f = open(output, 'wb')
    f.write(bin)
    f.close()
else:
    fw_data = hexdump(bin)

    f = open(output, 'w')
    print >>f, template % globals()
    f.close()

