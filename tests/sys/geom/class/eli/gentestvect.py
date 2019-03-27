#!/usr/bin/env python
# $FreeBSD$

from hashlib import pbkdf2_hmac
import hashlib
import itertools
import string

#From: https://stackoverflow.com/questions/14945095/how-to-escape-string-for-generated-c
def cstring(s, encoding='ascii'):
	if isinstance(s, unicode):
		s = s.encode(encoding)

	result = ''
	for c in s:
		if not (32 <= ord(c) < 127) or c in ('\\', '"'):
			result += '\\%03o' % ord(c)
		else:
			result += c

	return '"' + result + '"'

intarr = lambda y: ', '.join(map(lambda x: str(ord(x)), y))

_randfd = open('/dev/urandom', 'rb')
_maketrans = string.maketrans('', '')
def randgen(l, delchrs=None):
	if delchrs is None:
		return _randfd.read(l)

	s = ''
	while len(s) < l:
		s += string.translate(_randfd.read(l - len(s)), _maketrans,
		    delchrs)
	return s

def printhmacres(salt, passwd, itr, hmacout):
	print '\t{ %s, %d, %s, %d, %s, %d },' % (cstring(salt), len(salt),
	    cstring(passwd), itr, cstring(hmacout), len(hmacout))

if __name__ == '__main__':
	import sys

	if len(sys.argv) == 1:
		hashfun = 'sha512'
	else:
		hashfun = sys.argv[1]

	if hashfun not in hashlib.algorithms:
		print 'Invalid hash function: %s' % `hashfun`
		sys.exit(1)

	print '/* Test Vectors for PBKDF2-%s */' % hashfun.upper()
	print '\t/* salt, saltlen, passwd, itr, hmacout, hmacoutlen */'
	for saltl in xrange(8, 64, 8):
		for itr in itertools.chain(xrange(100, 1000, 100), xrange(1000,
		    10000, 1000)):
			for passlen in xrange(8, 80, 8):
				salt = randgen(saltl)
				passwd = randgen(passlen, '\x00')
				hmacout = pbkdf2_hmac(hashfun, passwd, salt,
				    itr)
				printhmacres(salt, passwd, itr, hmacout)
