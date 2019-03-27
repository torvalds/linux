# $FreeBSD$
# $Id: dpkt.py 114 2005-09-11 15:15:12Z dugsong $

"""fast, simple packet creation / parsing, with definitions for the
basic TCP/IP protocols.
"""

__author__ = 'Dug Song <dugsong@monkey.org>'
__copyright__ = 'Copyright (c) 2004 Dug Song'
__license__ = 'BSD'
__url__ = 'http://monkey.org/~dugsong/dpkt/'
__version__ = '1.2'

try:
    from itertools import izip as _it_izip
except ImportError:
    _it_izip = zip
    
from struct import calcsize as _st_calcsize, \
     pack as _st_pack, unpack as _st_unpack, error as _st_error
from re import compile as _re_compile

intchr = _re_compile(r"(?P<int>[0-9]+)(?P<chr>.)")

class MetaPacket(type):
    def __new__(cls, clsname, clsbases, clsdict):
        if '__hdr__' in clsdict:
            st = clsdict['__hdr__']
            clsdict['__hdr_fields__'] = [ x[0] for x in st ]
            clsdict['__hdr_fmt__'] = clsdict.get('__byte_order__', '>') + \
                ''.join([ x[1] for x in st ])
            clsdict['__hdr_len__'] = _st_calcsize(clsdict['__hdr_fmt__'])
            clsdict['__hdr_defaults__'] = \
                dict(zip(clsdict['__hdr_fields__'], [ x[2] for x in st ]))
            clsdict['__slots__'] = clsdict['__hdr_fields__']
        return type.__new__(cls, clsname, clsbases, clsdict)
                        
class Packet(object):
    """Packet class

    __hdr__ should be defined as a list of (name, structfmt, default) tuples
    __byte_order__ can be set to override the default ('>')
    """
    __metaclass__ = MetaPacket
    data = ''
    
    def __init__(self, *args, **kwargs):
        """Packet constructor with ([buf], [field=val,...]) prototype.

        Arguments:

        buf -- packet buffer to unpack

        Optional keyword arguments correspond to packet field names.
        """
        if args:
            self.unpack(args[0])
        else:
            for k in self.__hdr_fields__:
                setattr(self, k, self.__hdr_defaults__[k])
            for k, v in kwargs.iteritems():
                setattr(self, k, v)

    def __len__(self):
        return self.__hdr_len__ + len(self.data)

    def __repr__(self):
        l = [ '%s=%r' % (k, getattr(self, k))
              for k in self.__hdr_defaults__
              if getattr(self, k) != self.__hdr_defaults__[k] ]
        if self.data:
            l.append('data=%r' % self.data)
        return '%s(%s)' % (self.__class__.__name__, ', '.join(l))

    def __str__(self):
        return self.pack_hdr() + str(self.data)
    
    def pack_hdr(self):
        """Return packed header string."""
        try:
            return _st_pack(self.__hdr_fmt__,
                            *[ getattr(self, k) for k in self.__hdr_fields__ ])
        except _st_error:
            vals = []
            for k in self.__hdr_fields__:
                v = getattr(self, k)
                if isinstance(v, tuple):
                    vals.extend(v)
                else:
                    vals.append(v)
            return _st_pack(self.__hdr_fmt__, *vals)
    
    def unpack(self, buf):
        """Unpack packet header fields from buf, and set self.data."""

        res = list(_st_unpack(self.__hdr_fmt__, buf[:self.__hdr_len__]))
        for e, k in enumerate(self.__slots__):
            sfmt = self.__hdr__[e][1]
            mat = intchr.match(sfmt)
            if mat and mat.group('chr') != 's':
                cnt = int(mat.group('int'))
                setattr(self, k, list(res[:cnt]))
                del res[:cnt]
            else:
                if sfmt[-1] == 's':
                    i = res[0].find('\x00')
                    if i != -1:
                        res[0] = res[0][:i]
                setattr(self, k, res[0])
                del res[0]
        assert len(res) == 0
        self.data = buf[self.__hdr_len__:]

# XXX - ''.join([(len(`chr(x)`)==3) and chr(x) or '.' for x in range(256)])
__vis_filter = """................................ !"#$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[.]^_`abcdefghijklmnopqrstuvwxyz{|}~................................................................................................................................."""

def hexdump(buf, length=16):
    """Return a hexdump output string of the given buffer."""
    n = 0
    res = []
    while buf:
        line, buf = buf[:length], buf[length:]
        hexa = ' '.join(['%02x' % ord(x) for x in line])
        line = line.translate(__vis_filter)
        res.append('  %04d:  %-*s %s' % (n, length * 3, hexa, line))
        n += length
    return '\n'.join(res)

def in_cksum_add(s, buf):
    """in_cksum_add(cksum, buf) -> cksum

    Return accumulated Internet checksum.
    """
    nleft = len(buf)
    i = 0
    while nleft > 1:
        s += ord(buf[i]) * 256 + ord(buf[i+1])
        i += 2
        nleft -= 2
    if nleft:
        s += ord(buf[i]) * 256
    return s

def in_cksum_done(s):
    """Fold and return Internet checksum."""
    while (s >> 16):
        s = (s >> 16) + (s & 0xffff)
    return (~s & 0xffff)

def in_cksum(buf):
    """Return computed Internet checksum."""
    return in_cksum_done(in_cksum_add(0, buf))

try:
    import psyco
    psyco.bind(in_cksum)
    psyco.bind(Packet)
except ImportError:
    pass

