#!/usr/bin/env python
#
# Copyright (c) 2014 The FreeBSD Foundation
# All rights reserved.
#
# This software was developed by John-Mark Gurney under
# the sponsorship from the FreeBSD Foundation.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

from __future__ import print_function
import errno
import cryptodev
import itertools
import os
import struct
import unittest
from cryptodev import *
from glob import iglob

katdir = '/usr/local/share/nist-kat'

def katg(base, glob):
	assert os.path.exists(os.path.join(katdir, base)), "Please 'pkg install nist-kat'"
	return iglob(os.path.join(katdir, base, glob))

aesmodules = [ 'cryptosoft0', 'aesni0', 'ccr0', 'ccp0' ]
desmodules = [ 'cryptosoft0', ]
shamodules = [ 'cryptosoft0', 'aesni0', 'ccr0', 'ccp0' ]

def GenTestCase(cname):
	try:
		crid = cryptodev.Crypto.findcrid(cname)
	except IOError:
		return None

	class GendCryptoTestCase(unittest.TestCase):
		###############
		##### AES #####
		###############
		@unittest.skipIf(cname not in aesmodules, 'skipping AES on %s' % (cname))
		def test_xts(self):
			for i in katg('XTSTestVectors/format tweak value input - data unit seq no', '*.rsp'):
				self.runXTS(i, cryptodev.CRYPTO_AES_XTS)

		@unittest.skipIf(cname not in aesmodules, 'skipping AES on %s' % (cname))
		def test_cbc(self):
			for i in katg('KAT_AES', 'CBC[GKV]*.rsp'):
				self.runCBC(i)

		@unittest.skipIf(cname not in aesmodules, 'skipping AES on %s' % (cname))
		def test_gcm(self):
			for i in katg('gcmtestvectors', 'gcmEncrypt*'):
				self.runGCM(i, 'ENCRYPT')

			for i in katg('gcmtestvectors', 'gcmDecrypt*'):
				self.runGCM(i, 'DECRYPT')

		_gmacsizes = { 32: cryptodev.CRYPTO_AES_256_NIST_GMAC,
			24: cryptodev.CRYPTO_AES_192_NIST_GMAC,
			16: cryptodev.CRYPTO_AES_128_NIST_GMAC,
		}
		def runGCM(self, fname, mode):
			curfun = None
			if mode == 'ENCRYPT':
				swapptct = False
				curfun = Crypto.encrypt
			elif mode == 'DECRYPT':
				swapptct = True
				curfun = Crypto.decrypt
			else:
				raise RuntimeError('unknown mode: %r' % repr(mode))

			for bogusmode, lines in cryptodev.KATParser(fname,
			    [ 'Count', 'Key', 'IV', 'CT', 'AAD', 'Tag', 'PT', ]):
				for data in lines:
					curcnt = int(data['Count'])
					cipherkey = data['Key'].decode('hex')
					iv = data['IV'].decode('hex')
					aad = data['AAD'].decode('hex')
					tag = data['Tag'].decode('hex')
					if 'FAIL' not in data:
						pt = data['PT'].decode('hex')
					ct = data['CT'].decode('hex')

					if len(iv) != 12:
						# XXX - isn't supported
						continue

					try:
						c = Crypto(cryptodev.CRYPTO_AES_NIST_GCM_16,
						    cipherkey,
						    mac=self._gmacsizes[len(cipherkey)],
						    mackey=cipherkey, crid=crid)
					except EnvironmentError, e:
						# Can't test algorithms the driver does not support.
						if e.errno != errno.EOPNOTSUPP:
							raise
						continue

					if mode == 'ENCRYPT':
						try:
							rct, rtag = c.encrypt(pt, iv, aad)
						except EnvironmentError, e:
							# Can't test inputs the driver does not support.
							if e.errno != errno.EINVAL:
								raise
							continue
						rtag = rtag[:len(tag)]
						data['rct'] = rct.encode('hex')
						data['rtag'] = rtag.encode('hex')
						self.assertEqual(rct, ct, repr(data))
						self.assertEqual(rtag, tag, repr(data))
					else:
						if len(tag) != 16:
							continue
						args = (ct, iv, aad, tag)
						if 'FAIL' in data:
							self.assertRaises(IOError,
								c.decrypt, *args)
						else:
							try:
								rpt, rtag = c.decrypt(*args)
							except EnvironmentError, e:
								# Can't test inputs the driver does not support.
								if e.errno != errno.EINVAL:
									raise
								continue
							data['rpt'] = rpt.encode('hex')
							data['rtag'] = rtag.encode('hex')
							self.assertEqual(rpt, pt,
							    repr(data))

		def runCBC(self, fname):
			curfun = None
			for mode, lines in cryptodev.KATParser(fname,
			    [ 'COUNT', 'KEY', 'IV', 'PLAINTEXT', 'CIPHERTEXT', ]):
				if mode == 'ENCRYPT':
					swapptct = False
					curfun = Crypto.encrypt
				elif mode == 'DECRYPT':
					swapptct = True
					curfun = Crypto.decrypt
				else:
					raise RuntimeError('unknown mode: %r' % repr(mode))

				for data in lines:
					curcnt = int(data['COUNT'])
					cipherkey = data['KEY'].decode('hex')
					iv = data['IV'].decode('hex')
					pt = data['PLAINTEXT'].decode('hex')
					ct = data['CIPHERTEXT'].decode('hex')

					if swapptct:
						pt, ct = ct, pt
					# run the fun
					c = Crypto(cryptodev.CRYPTO_AES_CBC, cipherkey, crid=crid)
					r = curfun(c, pt, iv)
					self.assertEqual(r, ct)

		def runXTS(self, fname, meth):
			curfun = None
			for mode, lines in cryptodev.KATParser(fname,
			    [ 'COUNT', 'DataUnitLen', 'Key', 'DataUnitSeqNumber', 'PT',
			    'CT' ]):
				if mode == 'ENCRYPT':
					swapptct = False
					curfun = Crypto.encrypt
				elif mode == 'DECRYPT':
					swapptct = True
					curfun = Crypto.decrypt
				else:
					raise RuntimeError('unknown mode: %r' % repr(mode))

				for data in lines:
					curcnt = int(data['COUNT'])
					nbits = int(data['DataUnitLen'])
					cipherkey = data['Key'].decode('hex')
					iv = struct.pack('QQ', int(data['DataUnitSeqNumber']), 0)
					pt = data['PT'].decode('hex')
					ct = data['CT'].decode('hex')

					if nbits % 128 != 0:
						# XXX - mark as skipped
						continue
					if swapptct:
						pt, ct = ct, pt
					# run the fun
					try:
						c = Crypto(meth, cipherkey, crid=crid)
						r = curfun(c, pt, iv)
					except EnvironmentError, e:
						# Can't test hashes the driver does not support.
						if e.errno != errno.EOPNOTSUPP:
							raise
						continue
					self.assertEqual(r, ct)

		###############
		##### DES #####
		###############
		@unittest.skipIf(cname not in desmodules, 'skipping DES on %s' % (cname))
		def test_tdes(self):
			for i in katg('KAT_TDES', 'TCBC[a-z]*.rsp'):
				self.runTDES(i)

		def runTDES(self, fname):
			curfun = None
			for mode, lines in cryptodev.KATParser(fname,
			    [ 'COUNT', 'KEYs', 'IV', 'PLAINTEXT', 'CIPHERTEXT', ]):
				if mode == 'ENCRYPT':
					swapptct = False
					curfun = Crypto.encrypt
				elif mode == 'DECRYPT':
					swapptct = True
					curfun = Crypto.decrypt
				else:
					raise RuntimeError('unknown mode: %r' % repr(mode))

				for data in lines:
					curcnt = int(data['COUNT'])
					key = data['KEYs'] * 3
					cipherkey = key.decode('hex')
					iv = data['IV'].decode('hex')
					pt = data['PLAINTEXT'].decode('hex')
					ct = data['CIPHERTEXT'].decode('hex')

					if swapptct:
						pt, ct = ct, pt
					# run the fun
					c = Crypto(cryptodev.CRYPTO_3DES_CBC, cipherkey, crid=crid)
					r = curfun(c, pt, iv)
					self.assertEqual(r, ct)

		###############
		##### SHA #####
		###############
		@unittest.skipIf(cname not in shamodules, 'skipping SHA on %s' % str(cname))
		def test_sha(self):
			# SHA not available in software
			pass
			#for i in iglob('SHA1*'):
			#	self.runSHA(i)

		@unittest.skipIf(cname not in shamodules, 'skipping SHA on %s' % str(cname))
		def test_sha1hmac(self):
			for i in katg('hmactestvectors', 'HMAC.rsp'):
				self.runSHA1HMAC(i)

		def runSHA1HMAC(self, fname):
			for hashlength, lines in cryptodev.KATParser(fname,
			    [ 'Count', 'Klen', 'Tlen', 'Key', 'Msg', 'Mac' ]):
				# E.g., hashlength will be "L=20" (bytes)
				hashlen = int(hashlength.split("=")[1])

				blocksize = None
				if hashlen == 20:
					alg = cryptodev.CRYPTO_SHA1_HMAC
					blocksize = 64
				elif hashlen == 28:
					# Cryptodev doesn't support SHA-224
					# Slurp remaining input in section
					for data in lines:
						continue
					continue
				elif hashlen == 32:
					alg = cryptodev.CRYPTO_SHA2_256_HMAC
					blocksize = 64
				elif hashlen == 48:
					alg = cryptodev.CRYPTO_SHA2_384_HMAC
					blocksize = 128
				elif hashlen == 64:
					alg = cryptodev.CRYPTO_SHA2_512_HMAC
					blocksize = 128
				else:
					# Skip unsupported hashes
					# Slurp remaining input in section
					for data in lines:
						continue
					continue

				for data in lines:
					key = data['Key'].decode('hex')
					msg = data['Msg'].decode('hex')
					mac = data['Mac'].decode('hex')
					tlen = int(data['Tlen'])

					if len(key) > blocksize:
						continue

					try:
						c = Crypto(mac=alg, mackey=key,
						    crid=crid)
					except EnvironmentError, e:
						# Can't test hashes the driver does not support.
						if e.errno != errno.EOPNOTSUPP:
							raise
						continue

					_, r = c.encrypt(msg, iv="")

					# A limitation in cryptodev.py means we
					# can only store MACs up to 16 bytes.
					# That's good enough to validate the
					# correct behavior, more or less.
					maclen = min(tlen, 16)
					self.assertEqual(r[:maclen], mac[:maclen], "Actual: " + \
					    repr(r[:maclen].encode("hex")) + " Expected: " + repr(data))

	return GendCryptoTestCase

cryptosoft = GenTestCase('cryptosoft0')
aesni = GenTestCase('aesni0')
ccr = GenTestCase('ccr0')
ccp = GenTestCase('ccp0')

if __name__ == '__main__':
	unittest.main()
