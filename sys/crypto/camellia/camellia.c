/* camellia.h ver 1.1.0
 *
 * Copyright (c) 2006
 * NTT (Nippon Telegraph and Telephone Corporation) . All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer as
 *   the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NTT ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL NTT BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Algorithm Specification 
 *  http://info.isl.ntt.co.jp/crypt/eng/camellia/specifications.html
 */

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/endian.h>
#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <string.h>
#include <assert.h>
#define KASSERT(exp, msg) assert(exp)
#endif

#include <crypto/camellia/camellia.h>


/* key constants */

#define CAMELLIA_SIGMA1L (0xA09E667FL)
#define CAMELLIA_SIGMA1R (0x3BCC908BL)
#define CAMELLIA_SIGMA2L (0xB67AE858L)
#define CAMELLIA_SIGMA2R (0x4CAA73B2L)
#define CAMELLIA_SIGMA3L (0xC6EF372FL)
#define CAMELLIA_SIGMA3R (0xE94F82BEL)
#define CAMELLIA_SIGMA4L (0x54FF53A5L)
#define CAMELLIA_SIGMA4R (0xF1D36F1CL)
#define CAMELLIA_SIGMA5L (0x10E527FAL)
#define CAMELLIA_SIGMA5R (0xDE682D1DL)
#define CAMELLIA_SIGMA6L (0xB05688C2L)
#define CAMELLIA_SIGMA6R (0xB3E6C1FDL)

/*
 *  macros
 */
#define GETU32(pt) (((uint32_t)(pt)[0] << 24)		\
		     ^ ((uint32_t)(pt)[1] << 16)	\
		     ^ ((uint32_t)(pt)[2] <<  8)	\
		     ^ ((uint32_t)(pt)[3]))

#define PUTU32(ct, st) {(ct)[0] = (uint8_t)((st) >> 24);	\
			(ct)[1] = (uint8_t)((st) >> 16);	\
			(ct)[2] = (uint8_t)((st) >>  8);	\
			(ct)[3] = (uint8_t)(st);}

#define SUBL(INDEX) (subkey[(INDEX)*2+1])
#define SUBR(INDEX) (subkey[(INDEX)*2])

#define CAMELLIA_RR8(x) (((x) >> 8) + ((x) << 24))
#define CAMELLIA_RL1(x) (((x) << 1) + ((x) >> 31))
#define CAMELLIA_RL8(x) (((x) << 8) + ((x) >> 24))

#define CAMELLIA_ROLDQ(ll, lr, rl, rr, w0, w1, bits)	\
    do {						\
	w0 = ll;					\
	ll = (ll << bits) + (lr >> (32 - bits));	\
	lr = (lr << bits) + (rl >> (32 - bits));	\
	rl = (rl << bits) + (rr >> (32 - bits));	\
	rr = (rr << bits) + (w0 >> (32 - bits));	\
    } while(0)

#define CAMELLIA_ROLDQo32(ll, lr, rl, rr, w0, w1, bits)	\
    do {						\
	w0 = ll;					\
	w1 = lr;					\
	ll = (lr << (bits - 32)) + (rl >> (64 - bits));	\
	lr = (rl << (bits - 32)) + (rr >> (64 - bits));	\
	rl = (rr << (bits - 32)) + (w0 >> (64 - bits));	\
	rr = (w0 << (bits - 32)) + (w1 >> (64 - bits));	\
    } while(0)

#define CAMELLIA_SP1110(INDEX) (camellia_sp1110[(INDEX)])
#define CAMELLIA_SP0222(INDEX) (camellia_sp0222[(INDEX)])
#define CAMELLIA_SP3033(INDEX) (camellia_sp3033[(INDEX)])
#define CAMELLIA_SP4404(INDEX) (camellia_sp4404[(INDEX)])

#define CAMELLIA_F(xl, xr, kl, kr, yl, yr, il, ir, t0, t1)	\
    do {							\
	il = xl ^ kl;						\
	ir = xr ^ kr;						\
	t0 = il >> 16;						\
	t1 = ir >> 16;						\
	yl = CAMELLIA_SP1110(ir & 0xff)				\
	    ^ CAMELLIA_SP0222((t1 >> 8) & 0xff)			\
	    ^ CAMELLIA_SP3033(t1 & 0xff)			\
	    ^ CAMELLIA_SP4404((ir >> 8) & 0xff);		\
	yr = CAMELLIA_SP1110((t0 >> 8) & 0xff)			\
	    ^ CAMELLIA_SP0222(t0 & 0xff)			\
	    ^ CAMELLIA_SP3033((il >> 8) & 0xff)			\
	    ^ CAMELLIA_SP4404(il & 0xff);			\
	yl ^= yr;						\
	yr = CAMELLIA_RR8(yr);					\
	yr ^= yl;						\
    } while(0)


#define CAMELLIA_FLS(ll, lr, rl, rr, kll, klr, krl, krr, t0, t1, t2, t3) \
    do {								\
	t0 = kll;							\
	t2 = krr;							\
	t0 &= ll;							\
	t2 |= rr;							\
	rl ^= t2;							\
	lr ^= CAMELLIA_RL1(t0);						\
	t3 = krl;							\
	t1 = klr;							\
	t3 &= rl;							\
	t1 |= lr;							\
	ll ^= t1;							\
	rr ^= CAMELLIA_RL1(t3);						\
    } while(0)

#define CAMELLIA_ROUNDSM(xl, xr, kl, kr, yl, yr, il, ir, t0, t1)	\
    do {								\
	ir =  CAMELLIA_SP1110(xr & 0xff);				\
	il =  CAMELLIA_SP1110((xl>>24) & 0xff);				\
	ir ^= CAMELLIA_SP0222((xr>>24) & 0xff);				\
	il ^= CAMELLIA_SP0222((xl>>16) & 0xff);				\
	ir ^= CAMELLIA_SP3033((xr>>16) & 0xff);				\
	il ^= CAMELLIA_SP3033((xl>>8) & 0xff);				\
	ir ^= CAMELLIA_SP4404((xr>>8) & 0xff);				\
	il ^= CAMELLIA_SP4404(xl & 0xff);				\
	il ^= kl;							\
	ir ^= kr;							\
	ir ^= il;							\
	il = CAMELLIA_RR8(il);						\
	il ^= ir;							\
	yl ^= ir;							\
	yr ^= il;							\
    } while(0)


static const uint32_t camellia_sp1110[256] = {
    0x70707000,0x82828200,0x2c2c2c00,0xececec00,
    0xb3b3b300,0x27272700,0xc0c0c000,0xe5e5e500,
    0xe4e4e400,0x85858500,0x57575700,0x35353500,
    0xeaeaea00,0x0c0c0c00,0xaeaeae00,0x41414100,
    0x23232300,0xefefef00,0x6b6b6b00,0x93939300,
    0x45454500,0x19191900,0xa5a5a500,0x21212100,
    0xededed00,0x0e0e0e00,0x4f4f4f00,0x4e4e4e00,
    0x1d1d1d00,0x65656500,0x92929200,0xbdbdbd00,
    0x86868600,0xb8b8b800,0xafafaf00,0x8f8f8f00,
    0x7c7c7c00,0xebebeb00,0x1f1f1f00,0xcecece00,
    0x3e3e3e00,0x30303000,0xdcdcdc00,0x5f5f5f00,
    0x5e5e5e00,0xc5c5c500,0x0b0b0b00,0x1a1a1a00,
    0xa6a6a600,0xe1e1e100,0x39393900,0xcacaca00,
    0xd5d5d500,0x47474700,0x5d5d5d00,0x3d3d3d00,
    0xd9d9d900,0x01010100,0x5a5a5a00,0xd6d6d600,
    0x51515100,0x56565600,0x6c6c6c00,0x4d4d4d00,
    0x8b8b8b00,0x0d0d0d00,0x9a9a9a00,0x66666600,
    0xfbfbfb00,0xcccccc00,0xb0b0b000,0x2d2d2d00,
    0x74747400,0x12121200,0x2b2b2b00,0x20202000,
    0xf0f0f000,0xb1b1b100,0x84848400,0x99999900,
    0xdfdfdf00,0x4c4c4c00,0xcbcbcb00,0xc2c2c200,
    0x34343400,0x7e7e7e00,0x76767600,0x05050500,
    0x6d6d6d00,0xb7b7b700,0xa9a9a900,0x31313100,
    0xd1d1d100,0x17171700,0x04040400,0xd7d7d700,
    0x14141400,0x58585800,0x3a3a3a00,0x61616100,
    0xdedede00,0x1b1b1b00,0x11111100,0x1c1c1c00,
    0x32323200,0x0f0f0f00,0x9c9c9c00,0x16161600,
    0x53535300,0x18181800,0xf2f2f200,0x22222200,
    0xfefefe00,0x44444400,0xcfcfcf00,0xb2b2b200,
    0xc3c3c300,0xb5b5b500,0x7a7a7a00,0x91919100,
    0x24242400,0x08080800,0xe8e8e800,0xa8a8a800,
    0x60606000,0xfcfcfc00,0x69696900,0x50505000,
    0xaaaaaa00,0xd0d0d000,0xa0a0a000,0x7d7d7d00,
    0xa1a1a100,0x89898900,0x62626200,0x97979700,
    0x54545400,0x5b5b5b00,0x1e1e1e00,0x95959500,
    0xe0e0e000,0xffffff00,0x64646400,0xd2d2d200,
    0x10101000,0xc4c4c400,0x00000000,0x48484800,
    0xa3a3a300,0xf7f7f700,0x75757500,0xdbdbdb00,
    0x8a8a8a00,0x03030300,0xe6e6e600,0xdadada00,
    0x09090900,0x3f3f3f00,0xdddddd00,0x94949400,
    0x87878700,0x5c5c5c00,0x83838300,0x02020200,
    0xcdcdcd00,0x4a4a4a00,0x90909000,0x33333300,
    0x73737300,0x67676700,0xf6f6f600,0xf3f3f300,
    0x9d9d9d00,0x7f7f7f00,0xbfbfbf00,0xe2e2e200,
    0x52525200,0x9b9b9b00,0xd8d8d800,0x26262600,
    0xc8c8c800,0x37373700,0xc6c6c600,0x3b3b3b00,
    0x81818100,0x96969600,0x6f6f6f00,0x4b4b4b00,
    0x13131300,0xbebebe00,0x63636300,0x2e2e2e00,
    0xe9e9e900,0x79797900,0xa7a7a700,0x8c8c8c00,
    0x9f9f9f00,0x6e6e6e00,0xbcbcbc00,0x8e8e8e00,
    0x29292900,0xf5f5f500,0xf9f9f900,0xb6b6b600,
    0x2f2f2f00,0xfdfdfd00,0xb4b4b400,0x59595900,
    0x78787800,0x98989800,0x06060600,0x6a6a6a00,
    0xe7e7e700,0x46464600,0x71717100,0xbababa00,
    0xd4d4d400,0x25252500,0xababab00,0x42424200,
    0x88888800,0xa2a2a200,0x8d8d8d00,0xfafafa00,
    0x72727200,0x07070700,0xb9b9b900,0x55555500,
    0xf8f8f800,0xeeeeee00,0xacacac00,0x0a0a0a00,
    0x36363600,0x49494900,0x2a2a2a00,0x68686800,
    0x3c3c3c00,0x38383800,0xf1f1f100,0xa4a4a400,
    0x40404000,0x28282800,0xd3d3d300,0x7b7b7b00,
    0xbbbbbb00,0xc9c9c900,0x43434300,0xc1c1c100,
    0x15151500,0xe3e3e300,0xadadad00,0xf4f4f400,
    0x77777700,0xc7c7c700,0x80808000,0x9e9e9e00,
};

static const uint32_t camellia_sp0222[256] = {
    0x00e0e0e0,0x00050505,0x00585858,0x00d9d9d9,
    0x00676767,0x004e4e4e,0x00818181,0x00cbcbcb,
    0x00c9c9c9,0x000b0b0b,0x00aeaeae,0x006a6a6a,
    0x00d5d5d5,0x00181818,0x005d5d5d,0x00828282,
    0x00464646,0x00dfdfdf,0x00d6d6d6,0x00272727,
    0x008a8a8a,0x00323232,0x004b4b4b,0x00424242,
    0x00dbdbdb,0x001c1c1c,0x009e9e9e,0x009c9c9c,
    0x003a3a3a,0x00cacaca,0x00252525,0x007b7b7b,
    0x000d0d0d,0x00717171,0x005f5f5f,0x001f1f1f,
    0x00f8f8f8,0x00d7d7d7,0x003e3e3e,0x009d9d9d,
    0x007c7c7c,0x00606060,0x00b9b9b9,0x00bebebe,
    0x00bcbcbc,0x008b8b8b,0x00161616,0x00343434,
    0x004d4d4d,0x00c3c3c3,0x00727272,0x00959595,
    0x00ababab,0x008e8e8e,0x00bababa,0x007a7a7a,
    0x00b3b3b3,0x00020202,0x00b4b4b4,0x00adadad,
    0x00a2a2a2,0x00acacac,0x00d8d8d8,0x009a9a9a,
    0x00171717,0x001a1a1a,0x00353535,0x00cccccc,
    0x00f7f7f7,0x00999999,0x00616161,0x005a5a5a,
    0x00e8e8e8,0x00242424,0x00565656,0x00404040,
    0x00e1e1e1,0x00636363,0x00090909,0x00333333,
    0x00bfbfbf,0x00989898,0x00979797,0x00858585,
    0x00686868,0x00fcfcfc,0x00ececec,0x000a0a0a,
    0x00dadada,0x006f6f6f,0x00535353,0x00626262,
    0x00a3a3a3,0x002e2e2e,0x00080808,0x00afafaf,
    0x00282828,0x00b0b0b0,0x00747474,0x00c2c2c2,
    0x00bdbdbd,0x00363636,0x00222222,0x00383838,
    0x00646464,0x001e1e1e,0x00393939,0x002c2c2c,
    0x00a6a6a6,0x00303030,0x00e5e5e5,0x00444444,
    0x00fdfdfd,0x00888888,0x009f9f9f,0x00656565,
    0x00878787,0x006b6b6b,0x00f4f4f4,0x00232323,
    0x00484848,0x00101010,0x00d1d1d1,0x00515151,
    0x00c0c0c0,0x00f9f9f9,0x00d2d2d2,0x00a0a0a0,
    0x00555555,0x00a1a1a1,0x00414141,0x00fafafa,
    0x00434343,0x00131313,0x00c4c4c4,0x002f2f2f,
    0x00a8a8a8,0x00b6b6b6,0x003c3c3c,0x002b2b2b,
    0x00c1c1c1,0x00ffffff,0x00c8c8c8,0x00a5a5a5,
    0x00202020,0x00898989,0x00000000,0x00909090,
    0x00474747,0x00efefef,0x00eaeaea,0x00b7b7b7,
    0x00151515,0x00060606,0x00cdcdcd,0x00b5b5b5,
    0x00121212,0x007e7e7e,0x00bbbbbb,0x00292929,
    0x000f0f0f,0x00b8b8b8,0x00070707,0x00040404,
    0x009b9b9b,0x00949494,0x00212121,0x00666666,
    0x00e6e6e6,0x00cecece,0x00ededed,0x00e7e7e7,
    0x003b3b3b,0x00fefefe,0x007f7f7f,0x00c5c5c5,
    0x00a4a4a4,0x00373737,0x00b1b1b1,0x004c4c4c,
    0x00919191,0x006e6e6e,0x008d8d8d,0x00767676,
    0x00030303,0x002d2d2d,0x00dedede,0x00969696,
    0x00262626,0x007d7d7d,0x00c6c6c6,0x005c5c5c,
    0x00d3d3d3,0x00f2f2f2,0x004f4f4f,0x00191919,
    0x003f3f3f,0x00dcdcdc,0x00797979,0x001d1d1d,
    0x00525252,0x00ebebeb,0x00f3f3f3,0x006d6d6d,
    0x005e5e5e,0x00fbfbfb,0x00696969,0x00b2b2b2,
    0x00f0f0f0,0x00313131,0x000c0c0c,0x00d4d4d4,
    0x00cfcfcf,0x008c8c8c,0x00e2e2e2,0x00757575,
    0x00a9a9a9,0x004a4a4a,0x00575757,0x00848484,
    0x00111111,0x00454545,0x001b1b1b,0x00f5f5f5,
    0x00e4e4e4,0x000e0e0e,0x00737373,0x00aaaaaa,
    0x00f1f1f1,0x00dddddd,0x00595959,0x00141414,
    0x006c6c6c,0x00929292,0x00545454,0x00d0d0d0,
    0x00787878,0x00707070,0x00e3e3e3,0x00494949,
    0x00808080,0x00505050,0x00a7a7a7,0x00f6f6f6,
    0x00777777,0x00939393,0x00868686,0x00838383,
    0x002a2a2a,0x00c7c7c7,0x005b5b5b,0x00e9e9e9,
    0x00eeeeee,0x008f8f8f,0x00010101,0x003d3d3d,
};

static const uint32_t camellia_sp3033[256] = {
    0x38003838,0x41004141,0x16001616,0x76007676,
    0xd900d9d9,0x93009393,0x60006060,0xf200f2f2,
    0x72007272,0xc200c2c2,0xab00abab,0x9a009a9a,
    0x75007575,0x06000606,0x57005757,0xa000a0a0,
    0x91009191,0xf700f7f7,0xb500b5b5,0xc900c9c9,
    0xa200a2a2,0x8c008c8c,0xd200d2d2,0x90009090,
    0xf600f6f6,0x07000707,0xa700a7a7,0x27002727,
    0x8e008e8e,0xb200b2b2,0x49004949,0xde00dede,
    0x43004343,0x5c005c5c,0xd700d7d7,0xc700c7c7,
    0x3e003e3e,0xf500f5f5,0x8f008f8f,0x67006767,
    0x1f001f1f,0x18001818,0x6e006e6e,0xaf00afaf,
    0x2f002f2f,0xe200e2e2,0x85008585,0x0d000d0d,
    0x53005353,0xf000f0f0,0x9c009c9c,0x65006565,
    0xea00eaea,0xa300a3a3,0xae00aeae,0x9e009e9e,
    0xec00ecec,0x80008080,0x2d002d2d,0x6b006b6b,
    0xa800a8a8,0x2b002b2b,0x36003636,0xa600a6a6,
    0xc500c5c5,0x86008686,0x4d004d4d,0x33003333,
    0xfd00fdfd,0x66006666,0x58005858,0x96009696,
    0x3a003a3a,0x09000909,0x95009595,0x10001010,
    0x78007878,0xd800d8d8,0x42004242,0xcc00cccc,
    0xef00efef,0x26002626,0xe500e5e5,0x61006161,
    0x1a001a1a,0x3f003f3f,0x3b003b3b,0x82008282,
    0xb600b6b6,0xdb00dbdb,0xd400d4d4,0x98009898,
    0xe800e8e8,0x8b008b8b,0x02000202,0xeb00ebeb,
    0x0a000a0a,0x2c002c2c,0x1d001d1d,0xb000b0b0,
    0x6f006f6f,0x8d008d8d,0x88008888,0x0e000e0e,
    0x19001919,0x87008787,0x4e004e4e,0x0b000b0b,
    0xa900a9a9,0x0c000c0c,0x79007979,0x11001111,
    0x7f007f7f,0x22002222,0xe700e7e7,0x59005959,
    0xe100e1e1,0xda00dada,0x3d003d3d,0xc800c8c8,
    0x12001212,0x04000404,0x74007474,0x54005454,
    0x30003030,0x7e007e7e,0xb400b4b4,0x28002828,
    0x55005555,0x68006868,0x50005050,0xbe00bebe,
    0xd000d0d0,0xc400c4c4,0x31003131,0xcb00cbcb,
    0x2a002a2a,0xad00adad,0x0f000f0f,0xca00caca,
    0x70007070,0xff00ffff,0x32003232,0x69006969,
    0x08000808,0x62006262,0x00000000,0x24002424,
    0xd100d1d1,0xfb00fbfb,0xba00baba,0xed00eded,
    0x45004545,0x81008181,0x73007373,0x6d006d6d,
    0x84008484,0x9f009f9f,0xee00eeee,0x4a004a4a,
    0xc300c3c3,0x2e002e2e,0xc100c1c1,0x01000101,
    0xe600e6e6,0x25002525,0x48004848,0x99009999,
    0xb900b9b9,0xb300b3b3,0x7b007b7b,0xf900f9f9,
    0xce00cece,0xbf00bfbf,0xdf00dfdf,0x71007171,
    0x29002929,0xcd00cdcd,0x6c006c6c,0x13001313,
    0x64006464,0x9b009b9b,0x63006363,0x9d009d9d,
    0xc000c0c0,0x4b004b4b,0xb700b7b7,0xa500a5a5,
    0x89008989,0x5f005f5f,0xb100b1b1,0x17001717,
    0xf400f4f4,0xbc00bcbc,0xd300d3d3,0x46004646,
    0xcf00cfcf,0x37003737,0x5e005e5e,0x47004747,
    0x94009494,0xfa00fafa,0xfc00fcfc,0x5b005b5b,
    0x97009797,0xfe00fefe,0x5a005a5a,0xac00acac,
    0x3c003c3c,0x4c004c4c,0x03000303,0x35003535,
    0xf300f3f3,0x23002323,0xb800b8b8,0x5d005d5d,
    0x6a006a6a,0x92009292,0xd500d5d5,0x21002121,
    0x44004444,0x51005151,0xc600c6c6,0x7d007d7d,
    0x39003939,0x83008383,0xdc00dcdc,0xaa00aaaa,
    0x7c007c7c,0x77007777,0x56005656,0x05000505,
    0x1b001b1b,0xa400a4a4,0x15001515,0x34003434,
    0x1e001e1e,0x1c001c1c,0xf800f8f8,0x52005252,
    0x20002020,0x14001414,0xe900e9e9,0xbd00bdbd,
    0xdd00dddd,0xe400e4e4,0xa100a1a1,0xe000e0e0,
    0x8a008a8a,0xf100f1f1,0xd600d6d6,0x7a007a7a,
    0xbb00bbbb,0xe300e3e3,0x40004040,0x4f004f4f,
};

static const uint32_t camellia_sp4404[256] = {
    0x70700070,0x2c2c002c,0xb3b300b3,0xc0c000c0,
    0xe4e400e4,0x57570057,0xeaea00ea,0xaeae00ae,
    0x23230023,0x6b6b006b,0x45450045,0xa5a500a5,
    0xeded00ed,0x4f4f004f,0x1d1d001d,0x92920092,
    0x86860086,0xafaf00af,0x7c7c007c,0x1f1f001f,
    0x3e3e003e,0xdcdc00dc,0x5e5e005e,0x0b0b000b,
    0xa6a600a6,0x39390039,0xd5d500d5,0x5d5d005d,
    0xd9d900d9,0x5a5a005a,0x51510051,0x6c6c006c,
    0x8b8b008b,0x9a9a009a,0xfbfb00fb,0xb0b000b0,
    0x74740074,0x2b2b002b,0xf0f000f0,0x84840084,
    0xdfdf00df,0xcbcb00cb,0x34340034,0x76760076,
    0x6d6d006d,0xa9a900a9,0xd1d100d1,0x04040004,
    0x14140014,0x3a3a003a,0xdede00de,0x11110011,
    0x32320032,0x9c9c009c,0x53530053,0xf2f200f2,
    0xfefe00fe,0xcfcf00cf,0xc3c300c3,0x7a7a007a,
    0x24240024,0xe8e800e8,0x60600060,0x69690069,
    0xaaaa00aa,0xa0a000a0,0xa1a100a1,0x62620062,
    0x54540054,0x1e1e001e,0xe0e000e0,0x64640064,
    0x10100010,0x00000000,0xa3a300a3,0x75750075,
    0x8a8a008a,0xe6e600e6,0x09090009,0xdddd00dd,
    0x87870087,0x83830083,0xcdcd00cd,0x90900090,
    0x73730073,0xf6f600f6,0x9d9d009d,0xbfbf00bf,
    0x52520052,0xd8d800d8,0xc8c800c8,0xc6c600c6,
    0x81810081,0x6f6f006f,0x13130013,0x63630063,
    0xe9e900e9,0xa7a700a7,0x9f9f009f,0xbcbc00bc,
    0x29290029,0xf9f900f9,0x2f2f002f,0xb4b400b4,
    0x78780078,0x06060006,0xe7e700e7,0x71710071,
    0xd4d400d4,0xabab00ab,0x88880088,0x8d8d008d,
    0x72720072,0xb9b900b9,0xf8f800f8,0xacac00ac,
    0x36360036,0x2a2a002a,0x3c3c003c,0xf1f100f1,
    0x40400040,0xd3d300d3,0xbbbb00bb,0x43430043,
    0x15150015,0xadad00ad,0x77770077,0x80800080,
    0x82820082,0xecec00ec,0x27270027,0xe5e500e5,
    0x85850085,0x35350035,0x0c0c000c,0x41410041,
    0xefef00ef,0x93930093,0x19190019,0x21210021,
    0x0e0e000e,0x4e4e004e,0x65650065,0xbdbd00bd,
    0xb8b800b8,0x8f8f008f,0xebeb00eb,0xcece00ce,
    0x30300030,0x5f5f005f,0xc5c500c5,0x1a1a001a,
    0xe1e100e1,0xcaca00ca,0x47470047,0x3d3d003d,
    0x01010001,0xd6d600d6,0x56560056,0x4d4d004d,
    0x0d0d000d,0x66660066,0xcccc00cc,0x2d2d002d,
    0x12120012,0x20200020,0xb1b100b1,0x99990099,
    0x4c4c004c,0xc2c200c2,0x7e7e007e,0x05050005,
    0xb7b700b7,0x31310031,0x17170017,0xd7d700d7,
    0x58580058,0x61610061,0x1b1b001b,0x1c1c001c,
    0x0f0f000f,0x16160016,0x18180018,0x22220022,
    0x44440044,0xb2b200b2,0xb5b500b5,0x91910091,
    0x08080008,0xa8a800a8,0xfcfc00fc,0x50500050,
    0xd0d000d0,0x7d7d007d,0x89890089,0x97970097,
    0x5b5b005b,0x95950095,0xffff00ff,0xd2d200d2,
    0xc4c400c4,0x48480048,0xf7f700f7,0xdbdb00db,
    0x03030003,0xdada00da,0x3f3f003f,0x94940094,
    0x5c5c005c,0x02020002,0x4a4a004a,0x33330033,
    0x67670067,0xf3f300f3,0x7f7f007f,0xe2e200e2,
    0x9b9b009b,0x26260026,0x37370037,0x3b3b003b,
    0x96960096,0x4b4b004b,0xbebe00be,0x2e2e002e,
    0x79790079,0x8c8c008c,0x6e6e006e,0x8e8e008e,
    0xf5f500f5,0xb6b600b6,0xfdfd00fd,0x59590059,
    0x98980098,0x6a6a006a,0x46460046,0xbaba00ba,
    0x25250025,0x42420042,0xa2a200a2,0xfafa00fa,
    0x07070007,0x55550055,0xeeee00ee,0x0a0a000a,
    0x49490049,0x68680068,0x38380038,0xa4a400a4,
    0x28280028,0x7b7b007b,0xc9c900c9,0xc1c100c1,
    0xe3e300e3,0xf4f400f4,0xc7c700c7,0x9e9e009e,
};


/*
 * Stuff related to the Camellia key schedule
 */
#define subl(x) subL[(x)]
#define subr(x) subR[(x)]

void
camellia_setup128(const unsigned char *key, uint32_t *subkey)
{
    uint32_t kll, klr, krl, krr;
    uint32_t il, ir, t0, t1, w0, w1;
    uint32_t kw4l, kw4r, dw, tl, tr;
    uint32_t subL[26];
    uint32_t subR[26];

    /*
     *  k == kll || klr || krl || krr (|| is concatination)
     */
    kll = GETU32(key     );
    klr = GETU32(key +  4);
    krl = GETU32(key +  8);
    krr = GETU32(key + 12);
    /*
     * generate KL dependent subkeys
     */
    subl(0) = kll; subr(0) = klr;
    subl(1) = krl; subr(1) = krr;
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 15);
    subl(4) = kll; subr(4) = klr;
    subl(5) = krl; subr(5) = krr;
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 30);
    subl(10) = kll; subr(10) = klr;
    subl(11) = krl; subr(11) = krr;
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 15);
    subl(13) = krl; subr(13) = krr;
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 17);
    subl(16) = kll; subr(16) = klr;
    subl(17) = krl; subr(17) = krr;
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 17);
    subl(18) = kll; subr(18) = klr;
    subl(19) = krl; subr(19) = krr;
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 17);
    subl(22) = kll; subr(22) = klr;
    subl(23) = krl; subr(23) = krr;

    /* generate KA */
    kll = subl(0); klr = subr(0);
    krl = subl(1); krr = subr(1);
    CAMELLIA_F(kll, klr, CAMELLIA_SIGMA1L, CAMELLIA_SIGMA1R,
	       w0, w1, il, ir, t0, t1);
    krl ^= w0; krr ^= w1;
    CAMELLIA_F(krl, krr, CAMELLIA_SIGMA2L, CAMELLIA_SIGMA2R,
	       kll, klr, il, ir, t0, t1);
    CAMELLIA_F(kll, klr, CAMELLIA_SIGMA3L, CAMELLIA_SIGMA3R,
	       krl, krr, il, ir, t0, t1);
    krl ^= w0; krr ^= w1;
    CAMELLIA_F(krl, krr, CAMELLIA_SIGMA4L, CAMELLIA_SIGMA4R,
	       w0, w1, il, ir, t0, t1);
    kll ^= w0; klr ^= w1;

    /* generate KA dependent subkeys */
    subl(2) = kll; subr(2) = klr;
    subl(3) = krl; subr(3) = krr;
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 15);
    subl(6) = kll; subr(6) = klr;
    subl(7) = krl; subr(7) = krr;
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 15);
    subl(8) = kll; subr(8) = klr;
    subl(9) = krl; subr(9) = krr;
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 15);
    subl(12) = kll; subr(12) = klr;
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 15);
    subl(14) = kll; subr(14) = klr;
    subl(15) = krl; subr(15) = krr;
    CAMELLIA_ROLDQo32(kll, klr, krl, krr, w0, w1, 34);
    subl(20) = kll; subr(20) = klr;
    subl(21) = krl; subr(21) = krr;
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 17);
    subl(24) = kll; subr(24) = klr;
    subl(25) = krl; subr(25) = krr;


    /* absorb kw2 to other subkeys */
    subl(3) ^= subl(1); subr(3) ^= subr(1);
    subl(5) ^= subl(1); subr(5) ^= subr(1);
    subl(7) ^= subl(1); subr(7) ^= subr(1);
    subl(1) ^= subr(1) & ~subr(9);
    dw = subl(1) & subl(9), subr(1) ^= CAMELLIA_RL1(dw);
    subl(11) ^= subl(1); subr(11) ^= subr(1);
    subl(13) ^= subl(1); subr(13) ^= subr(1);
    subl(15) ^= subl(1); subr(15) ^= subr(1);
    subl(1) ^= subr(1) & ~subr(17);
    dw = subl(1) & subl(17), subr(1) ^= CAMELLIA_RL1(dw);
    subl(19) ^= subl(1); subr(19) ^= subr(1);
    subl(21) ^= subl(1); subr(21) ^= subr(1);
    subl(23) ^= subl(1); subr(23) ^= subr(1);
    subl(24) ^= subl(1); subr(24) ^= subr(1);

    /* absorb kw4 to other subkeys */
    kw4l = subl(25); kw4r = subr(25);
    subl(22) ^= kw4l; subr(22) ^= kw4r;
    subl(20) ^= kw4l; subr(20) ^= kw4r;
    subl(18) ^= kw4l; subr(18) ^= kw4r;
    kw4l ^= kw4r & ~subr(16);
    dw = kw4l & subl(16), kw4r ^= CAMELLIA_RL1(dw);
    subl(14) ^= kw4l; subr(14) ^= kw4r;
    subl(12) ^= kw4l; subr(12) ^= kw4r;
    subl(10) ^= kw4l; subr(10) ^= kw4r;
    kw4l ^= kw4r & ~subr(8);
    dw = kw4l & subl(8), kw4r ^= CAMELLIA_RL1(dw);
    subl(6) ^= kw4l; subr(6) ^= kw4r;
    subl(4) ^= kw4l; subr(4) ^= kw4r;
    subl(2) ^= kw4l; subr(2) ^= kw4r;
    subl(0) ^= kw4l; subr(0) ^= kw4r;

    /* key XOR is end of F-function */
    SUBL(0) = subl(0) ^ subl(2);
    SUBR(0) = subr(0) ^ subr(2);
    SUBL(2) = subl(3);
    SUBR(2) = subr(3);
    SUBL(3) = subl(2) ^ subl(4);
    SUBR(3) = subr(2) ^ subr(4);
    SUBL(4) = subl(3) ^ subl(5);
    SUBR(4) = subr(3) ^ subr(5);
    SUBL(5) = subl(4) ^ subl(6);
    SUBR(5) = subr(4) ^ subr(6);
    SUBL(6) = subl(5) ^ subl(7);
    SUBR(6) = subr(5) ^ subr(7);
    tl = subl(10) ^ (subr(10) & ~subr(8));
    dw = tl & subl(8), tr = subr(10) ^ CAMELLIA_RL1(dw);
    SUBL(7) = subl(6) ^ tl;
    SUBR(7) = subr(6) ^ tr;
    SUBL(8) = subl(8);
    SUBR(8) = subr(8);
    SUBL(9) = subl(9);
    SUBR(9) = subr(9);
    tl = subl(7) ^ (subr(7) & ~subr(9));
    dw = tl & subl(9), tr = subr(7) ^ CAMELLIA_RL1(dw);
    SUBL(10) = tl ^ subl(11);
    SUBR(10) = tr ^ subr(11);
    SUBL(11) = subl(10) ^ subl(12);
    SUBR(11) = subr(10) ^ subr(12);
    SUBL(12) = subl(11) ^ subl(13);
    SUBR(12) = subr(11) ^ subr(13);
    SUBL(13) = subl(12) ^ subl(14);
    SUBR(13) = subr(12) ^ subr(14);
    SUBL(14) = subl(13) ^ subl(15);
    SUBR(14) = subr(13) ^ subr(15);
    tl = subl(18) ^ (subr(18) & ~subr(16));
    dw = tl & subl(16), tr = subr(18) ^ CAMELLIA_RL1(dw);
    SUBL(15) = subl(14) ^ tl;
    SUBR(15) = subr(14) ^ tr;
    SUBL(16) = subl(16);
    SUBR(16) = subr(16);
    SUBL(17) = subl(17);
    SUBR(17) = subr(17);
    tl = subl(15) ^ (subr(15) & ~subr(17));
    dw = tl & subl(17), tr = subr(15) ^ CAMELLIA_RL1(dw);
    SUBL(18) = tl ^ subl(19);
    SUBR(18) = tr ^ subr(19);
    SUBL(19) = subl(18) ^ subl(20);
    SUBR(19) = subr(18) ^ subr(20);
    SUBL(20) = subl(19) ^ subl(21);
    SUBR(20) = subr(19) ^ subr(21);
    SUBL(21) = subl(20) ^ subl(22);
    SUBR(21) = subr(20) ^ subr(22);
    SUBL(22) = subl(21) ^ subl(23);
    SUBR(22) = subr(21) ^ subr(23);
    SUBL(23) = subl(22);
    SUBR(23) = subr(22);
    SUBL(24) = subl(24) ^ subl(23);
    SUBR(24) = subr(24) ^ subr(23);

    /* apply the inverse of the last half of P-function */
    dw = SUBL(2) ^ SUBR(2), dw = CAMELLIA_RL8(dw);
    SUBR(2) = SUBL(2) ^ dw, SUBL(2) = dw;
    dw = SUBL(3) ^ SUBR(3), dw = CAMELLIA_RL8(dw);
    SUBR(3) = SUBL(3) ^ dw, SUBL(3) = dw;
    dw = SUBL(4) ^ SUBR(4), dw = CAMELLIA_RL8(dw);
    SUBR(4) = SUBL(4) ^ dw, SUBL(4) = dw;
    dw = SUBL(5) ^ SUBR(5), dw = CAMELLIA_RL8(dw);
    SUBR(5) = SUBL(5) ^ dw, SUBL(5) = dw;
    dw = SUBL(6) ^ SUBR(6), dw = CAMELLIA_RL8(dw);
    SUBR(6) = SUBL(6) ^ dw, SUBL(6) = dw;
    dw = SUBL(7) ^ SUBR(7), dw = CAMELLIA_RL8(dw);
    SUBR(7) = SUBL(7) ^ dw, SUBL(7) = dw;
    dw = SUBL(10) ^ SUBR(10), dw = CAMELLIA_RL8(dw);
    SUBR(10) = SUBL(10) ^ dw, SUBL(10) = dw;
    dw = SUBL(11) ^ SUBR(11), dw = CAMELLIA_RL8(dw);
    SUBR(11) = SUBL(11) ^ dw, SUBL(11) = dw;
    dw = SUBL(12) ^ SUBR(12), dw = CAMELLIA_RL8(dw);
    SUBR(12) = SUBL(12) ^ dw, SUBL(12) = dw;
    dw = SUBL(13) ^ SUBR(13), dw = CAMELLIA_RL8(dw);
    SUBR(13) = SUBL(13) ^ dw, SUBL(13) = dw;
    dw = SUBL(14) ^ SUBR(14), dw = CAMELLIA_RL8(dw);
    SUBR(14) = SUBL(14) ^ dw, SUBL(14) = dw;
    dw = SUBL(15) ^ SUBR(15), dw = CAMELLIA_RL8(dw);
    SUBR(15) = SUBL(15) ^ dw, SUBL(15) = dw;
    dw = SUBL(18) ^ SUBR(18), dw = CAMELLIA_RL8(dw);
    SUBR(18) = SUBL(18) ^ dw, SUBL(18) = dw;
    dw = SUBL(19) ^ SUBR(19), dw = CAMELLIA_RL8(dw);
    SUBR(19) = SUBL(19) ^ dw, SUBL(19) = dw;
    dw = SUBL(20) ^ SUBR(20), dw = CAMELLIA_RL8(dw);
    SUBR(20) = SUBL(20) ^ dw, SUBL(20) = dw;
    dw = SUBL(21) ^ SUBR(21), dw = CAMELLIA_RL8(dw);
    SUBR(21) = SUBL(21) ^ dw, SUBL(21) = dw;
    dw = SUBL(22) ^ SUBR(22), dw = CAMELLIA_RL8(dw);
    SUBR(22) = SUBL(22) ^ dw, SUBL(22) = dw;
    dw = SUBL(23) ^ SUBR(23), dw = CAMELLIA_RL8(dw);
    SUBR(23) = SUBL(23) ^ dw, SUBL(23) = dw;
}

void
camellia_setup256(const unsigned char *key, uint32_t *subkey)
{
    uint32_t kll,klr,krl,krr;           /* left half of key */
    uint32_t krll,krlr,krrl,krrr;       /* right half of key */
    uint32_t il, ir, t0, t1, w0, w1;    /* temporary variables */
    uint32_t kw4l, kw4r, dw, tl, tr;
    uint32_t subL[34];
    uint32_t subR[34];

    /*
     *  key = (kll || klr || krl || krr || krll || krlr || krrl || krrr)
     *  (|| is concatination)
     */

    kll  = GETU32(key     );
    klr  = GETU32(key +  4);
    krl  = GETU32(key +  8);
    krr  = GETU32(key + 12);
    krll = GETU32(key + 16);
    krlr = GETU32(key + 20);
    krrl = GETU32(key + 24);
    krrr = GETU32(key + 28);

    /* generate KL dependent subkeys */
    subl(0) = kll; subr(0) = klr;
    subl(1) = krl; subr(1) = krr;
    CAMELLIA_ROLDQo32(kll, klr, krl, krr, w0, w1, 45);
    subl(12) = kll; subr(12) = klr;
    subl(13) = krl; subr(13) = krr;
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 15);
    subl(16) = kll; subr(16) = klr;
    subl(17) = krl; subr(17) = krr;
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 17);
    subl(22) = kll; subr(22) = klr;
    subl(23) = krl; subr(23) = krr;
    CAMELLIA_ROLDQo32(kll, klr, krl, krr, w0, w1, 34);
    subl(30) = kll; subr(30) = klr;
    subl(31) = krl; subr(31) = krr;

    /* generate KR dependent subkeys */
    CAMELLIA_ROLDQ(krll, krlr, krrl, krrr, w0, w1, 15);
    subl(4) = krll; subr(4) = krlr;
    subl(5) = krrl; subr(5) = krrr;
    CAMELLIA_ROLDQ(krll, krlr, krrl, krrr, w0, w1, 15);
    subl(8) = krll; subr(8) = krlr;
    subl(9) = krrl; subr(9) = krrr;
    CAMELLIA_ROLDQ(krll, krlr, krrl, krrr, w0, w1, 30);
    subl(18) = krll; subr(18) = krlr;
    subl(19) = krrl; subr(19) = krrr;
    CAMELLIA_ROLDQo32(krll, krlr, krrl, krrr, w0, w1, 34);
    subl(26) = krll; subr(26) = krlr;
    subl(27) = krrl; subr(27) = krrr;
    CAMELLIA_ROLDQo32(krll, krlr, krrl, krrr, w0, w1, 34);

    /* generate KA */
    kll = subl(0) ^ krll; klr = subr(0) ^ krlr;
    krl = subl(1) ^ krrl; krr = subr(1) ^ krrr;
    CAMELLIA_F(kll, klr, CAMELLIA_SIGMA1L, CAMELLIA_SIGMA1R,
	       w0, w1, il, ir, t0, t1);
    krl ^= w0; krr ^= w1;
    CAMELLIA_F(krl, krr, CAMELLIA_SIGMA2L, CAMELLIA_SIGMA2R,
	       kll, klr, il, ir, t0, t1);
    kll ^= krll; klr ^= krlr;
    CAMELLIA_F(kll, klr, CAMELLIA_SIGMA3L, CAMELLIA_SIGMA3R,
	       krl, krr, il, ir, t0, t1);
    krl ^= w0 ^ krrl; krr ^= w1 ^ krrr;
    CAMELLIA_F(krl, krr, CAMELLIA_SIGMA4L, CAMELLIA_SIGMA4R,
	       w0, w1, il, ir, t0, t1);
    kll ^= w0; klr ^= w1;

    /* generate KB */
    krll ^= kll; krlr ^= klr;
    krrl ^= krl; krrr ^= krr;
    CAMELLIA_F(krll, krlr, CAMELLIA_SIGMA5L, CAMELLIA_SIGMA5R,
	       w0, w1, il, ir, t0, t1);
    krrl ^= w0; krrr ^= w1;
    CAMELLIA_F(krrl, krrr, CAMELLIA_SIGMA6L, CAMELLIA_SIGMA6R,
	       w0, w1, il, ir, t0, t1);
    krll ^= w0; krlr ^= w1;

    /* generate KA dependent subkeys */
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 15);
    subl(6) = kll; subr(6) = klr;
    subl(7) = krl; subr(7) = krr;
    CAMELLIA_ROLDQ(kll, klr, krl, krr, w0, w1, 30);
    subl(14) = kll; subr(14) = klr;
    subl(15) = krl; subr(15) = krr;
    subl(24) = klr; subr(24) = krl;
    subl(25) = krr; subr(25) = kll;
    CAMELLIA_ROLDQo32(kll, klr, krl, krr, w0, w1, 49);
    subl(28) = kll; subr(28) = klr;
    subl(29) = krl; subr(29) = krr;

    /* generate KB dependent subkeys */
    subl(2) = krll; subr(2) = krlr;
    subl(3) = krrl; subr(3) = krrr;
    CAMELLIA_ROLDQ(krll, krlr, krrl, krrr, w0, w1, 30);
    subl(10) = krll; subr(10) = krlr;
    subl(11) = krrl; subr(11) = krrr;
    CAMELLIA_ROLDQ(krll, krlr, krrl, krrr, w0, w1, 30);
    subl(20) = krll; subr(20) = krlr;
    subl(21) = krrl; subr(21) = krrr;
    CAMELLIA_ROLDQo32(krll, krlr, krrl, krrr, w0, w1, 51);
    subl(32) = krll; subr(32) = krlr;
    subl(33) = krrl; subr(33) = krrr;

    /* absorb kw2 to other subkeys */
    subl(3) ^= subl(1); subr(3) ^= subr(1);
    subl(5) ^= subl(1); subr(5) ^= subr(1);
    subl(7) ^= subl(1); subr(7) ^= subr(1);
    subl(1) ^= subr(1) & ~subr(9);
    dw = subl(1) & subl(9), subr(1) ^= CAMELLIA_RL1(dw);
    subl(11) ^= subl(1); subr(11) ^= subr(1);
    subl(13) ^= subl(1); subr(13) ^= subr(1);
    subl(15) ^= subl(1); subr(15) ^= subr(1);
    subl(1) ^= subr(1) & ~subr(17);
    dw = subl(1) & subl(17), subr(1) ^= CAMELLIA_RL1(dw);
    subl(19) ^= subl(1); subr(19) ^= subr(1);
    subl(21) ^= subl(1); subr(21) ^= subr(1);
    subl(23) ^= subl(1); subr(23) ^= subr(1);
    subl(1) ^= subr(1) & ~subr(25);
    dw = subl(1) & subl(25), subr(1) ^= CAMELLIA_RL1(dw);
    subl(27) ^= subl(1); subr(27) ^= subr(1);
    subl(29) ^= subl(1); subr(29) ^= subr(1);
    subl(31) ^= subl(1); subr(31) ^= subr(1);
    subl(32) ^= subl(1); subr(32) ^= subr(1);


    /* absorb kw4 to other subkeys */
    kw4l = subl(33); kw4r = subr(33);
    subl(30) ^= kw4l; subr(30) ^= kw4r;
    subl(28) ^= kw4l; subr(28) ^= kw4r;
    subl(26) ^= kw4l; subr(26) ^= kw4r;
    kw4l ^= kw4r & ~subr(24);
    dw = kw4l & subl(24), kw4r ^= CAMELLIA_RL1(dw);
    subl(22) ^= kw4l; subr(22) ^= kw4r;
    subl(20) ^= kw4l; subr(20) ^= kw4r;
    subl(18) ^= kw4l; subr(18) ^= kw4r;
    kw4l ^= kw4r & ~subr(16);
    dw = kw4l & subl(16), kw4r ^= CAMELLIA_RL1(dw);
    subl(14) ^= kw4l; subr(14) ^= kw4r;
    subl(12) ^= kw4l; subr(12) ^= kw4r;
    subl(10) ^= kw4l; subr(10) ^= kw4r;
    kw4l ^= kw4r & ~subr(8);
    dw = kw4l & subl(8), kw4r ^= CAMELLIA_RL1(dw);
    subl(6) ^= kw4l; subr(6) ^= kw4r;
    subl(4) ^= kw4l; subr(4) ^= kw4r;
    subl(2) ^= kw4l; subr(2) ^= kw4r;
    subl(0) ^= kw4l; subr(0) ^= kw4r;

    /* key XOR is end of F-function */
    SUBL(0) = subl(0) ^ subl(2);
    SUBR(0) = subr(0) ^ subr(2);
    SUBL(2) = subl(3);
    SUBR(2) = subr(3);
    SUBL(3) = subl(2) ^ subl(4);
    SUBR(3) = subr(2) ^ subr(4);
    SUBL(4) = subl(3) ^ subl(5);
    SUBR(4) = subr(3) ^ subr(5);
    SUBL(5) = subl(4) ^ subl(6);
    SUBR(5) = subr(4) ^ subr(6);
    SUBL(6) = subl(5) ^ subl(7);
    SUBR(6) = subr(5) ^ subr(7);
    tl = subl(10) ^ (subr(10) & ~subr(8));
    dw = tl & subl(8), tr = subr(10) ^ CAMELLIA_RL1(dw);
    SUBL(7) = subl(6) ^ tl;
    SUBR(7) = subr(6) ^ tr;
    SUBL(8) = subl(8);
    SUBR(8) = subr(8);
    SUBL(9) = subl(9);
    SUBR(9) = subr(9);
    tl = subl(7) ^ (subr(7) & ~subr(9));
    dw = tl & subl(9), tr = subr(7) ^ CAMELLIA_RL1(dw);
    SUBL(10) = tl ^ subl(11);
    SUBR(10) = tr ^ subr(11);
    SUBL(11) = subl(10) ^ subl(12);
    SUBR(11) = subr(10) ^ subr(12);
    SUBL(12) = subl(11) ^ subl(13);
    SUBR(12) = subr(11) ^ subr(13);
    SUBL(13) = subl(12) ^ subl(14);
    SUBR(13) = subr(12) ^ subr(14);
    SUBL(14) = subl(13) ^ subl(15);
    SUBR(14) = subr(13) ^ subr(15);
    tl = subl(18) ^ (subr(18) & ~subr(16));
    dw = tl & subl(16), tr = subr(18) ^ CAMELLIA_RL1(dw);
    SUBL(15) = subl(14) ^ tl;
    SUBR(15) = subr(14) ^ tr;
    SUBL(16) = subl(16);
    SUBR(16) = subr(16);
    SUBL(17) = subl(17);
    SUBR(17) = subr(17);
    tl = subl(15) ^ (subr(15) & ~subr(17));
    dw = tl & subl(17), tr = subr(15) ^ CAMELLIA_RL1(dw);
    SUBL(18) = tl ^ subl(19);
    SUBR(18) = tr ^ subr(19);
    SUBL(19) = subl(18) ^ subl(20);
    SUBR(19) = subr(18) ^ subr(20);
    SUBL(20) = subl(19) ^ subl(21);
    SUBR(20) = subr(19) ^ subr(21);
    SUBL(21) = subl(20) ^ subl(22);
    SUBR(21) = subr(20) ^ subr(22);
    SUBL(22) = subl(21) ^ subl(23);
    SUBR(22) = subr(21) ^ subr(23);
    tl = subl(26) ^ (subr(26) & ~subr(24));
    dw = tl & subl(24), tr = subr(26) ^ CAMELLIA_RL1(dw);
    SUBL(23) = subl(22) ^ tl;
    SUBR(23) = subr(22) ^ tr;
    SUBL(24) = subl(24);
    SUBR(24) = subr(24);
    SUBL(25) = subl(25);
    SUBR(25) = subr(25);
    tl = subl(23) ^ (subr(23) & ~subr(25));
    dw = tl & subl(25), tr = subr(23) ^ CAMELLIA_RL1(dw);
    SUBL(26) = tl ^ subl(27);
    SUBR(26) = tr ^ subr(27);
    SUBL(27) = subl(26) ^ subl(28);
    SUBR(27) = subr(26) ^ subr(28);
    SUBL(28) = subl(27) ^ subl(29);
    SUBR(28) = subr(27) ^ subr(29);
    SUBL(29) = subl(28) ^ subl(30);
    SUBR(29) = subr(28) ^ subr(30);
    SUBL(30) = subl(29) ^ subl(31);
    SUBR(30) = subr(29) ^ subr(31);
    SUBL(31) = subl(30);
    SUBR(31) = subr(30);
    SUBL(32) = subl(32) ^ subl(31);
    SUBR(32) = subr(32) ^ subr(31);

    /* apply the inverse of the last half of P-function */
    dw = SUBL(2) ^ SUBR(2), dw = CAMELLIA_RL8(dw);
    SUBR(2) = SUBL(2) ^ dw, SUBL(2) = dw;
    dw = SUBL(3) ^ SUBR(3), dw = CAMELLIA_RL8(dw);
    SUBR(3) = SUBL(3) ^ dw, SUBL(3) = dw;
    dw = SUBL(4) ^ SUBR(4), dw = CAMELLIA_RL8(dw);
    SUBR(4) = SUBL(4) ^ dw, SUBL(4) = dw;
    dw = SUBL(5) ^ SUBR(5), dw = CAMELLIA_RL8(dw);
    SUBR(5) = SUBL(5) ^ dw, SUBL(5) = dw;
    dw = SUBL(6) ^ SUBR(6), dw = CAMELLIA_RL8(dw);
    SUBR(6) = SUBL(6) ^ dw, SUBL(6) = dw;
    dw = SUBL(7) ^ SUBR(7), dw = CAMELLIA_RL8(dw);
    SUBR(7) = SUBL(7) ^ dw, SUBL(7) = dw;
    dw = SUBL(10) ^ SUBR(10), dw = CAMELLIA_RL8(dw);
    SUBR(10) = SUBL(10) ^ dw, SUBL(10) = dw;
    dw = SUBL(11) ^ SUBR(11), dw = CAMELLIA_RL8(dw);
    SUBR(11) = SUBL(11) ^ dw, SUBL(11) = dw;
    dw = SUBL(12) ^ SUBR(12), dw = CAMELLIA_RL8(dw);
    SUBR(12) = SUBL(12) ^ dw, SUBL(12) = dw;
    dw = SUBL(13) ^ SUBR(13), dw = CAMELLIA_RL8(dw);
    SUBR(13) = SUBL(13) ^ dw, SUBL(13) = dw;
    dw = SUBL(14) ^ SUBR(14), dw = CAMELLIA_RL8(dw);
    SUBR(14) = SUBL(14) ^ dw, SUBL(14) = dw;
    dw = SUBL(15) ^ SUBR(15), dw = CAMELLIA_RL8(dw);
    SUBR(15) = SUBL(15) ^ dw, SUBL(15) = dw;
    dw = SUBL(18) ^ SUBR(18), dw = CAMELLIA_RL8(dw);
    SUBR(18) = SUBL(18) ^ dw, SUBL(18) = dw;
    dw = SUBL(19) ^ SUBR(19), dw = CAMELLIA_RL8(dw);
    SUBR(19) = SUBL(19) ^ dw, SUBL(19) = dw;
    dw = SUBL(20) ^ SUBR(20), dw = CAMELLIA_RL8(dw);
    SUBR(20) = SUBL(20) ^ dw, SUBL(20) = dw;
    dw = SUBL(21) ^ SUBR(21), dw = CAMELLIA_RL8(dw);
    SUBR(21) = SUBL(21) ^ dw, SUBL(21) = dw;
    dw = SUBL(22) ^ SUBR(22), dw = CAMELLIA_RL8(dw);
    SUBR(22) = SUBL(22) ^ dw, SUBL(22) = dw;
    dw = SUBL(23) ^ SUBR(23), dw = CAMELLIA_RL8(dw);
    SUBR(23) = SUBL(23) ^ dw, SUBL(23) = dw;
    dw = SUBL(26) ^ SUBR(26), dw = CAMELLIA_RL8(dw);
    SUBR(26) = SUBL(26) ^ dw, SUBL(26) = dw;
    dw = SUBL(27) ^ SUBR(27), dw = CAMELLIA_RL8(dw);
    SUBR(27) = SUBL(27) ^ dw, SUBL(27) = dw;
    dw = SUBL(28) ^ SUBR(28), dw = CAMELLIA_RL8(dw);
    SUBR(28) = SUBL(28) ^ dw, SUBL(28) = dw;
    dw = SUBL(29) ^ SUBR(29), dw = CAMELLIA_RL8(dw);
    SUBR(29) = SUBL(29) ^ dw, SUBL(29) = dw;
    dw = SUBL(30) ^ SUBR(30), dw = CAMELLIA_RL8(dw);
    SUBR(30) = SUBL(30) ^ dw, SUBL(30) = dw;
    dw = SUBL(31) ^ SUBR(31), dw = CAMELLIA_RL8(dw);
    SUBR(31) = SUBL(31) ^ dw, SUBL(31) = dw;
}

void
camellia_setup192(const unsigned char *key, uint32_t *subkey)
{
    unsigned char kk[32];
    uint32_t krll, krlr, krrl,krrr;

    memcpy(kk, key, 24);
    memcpy((unsigned char *)&krll, key+16,4);
    memcpy((unsigned char *)&krlr, key+20,4);
    krrl = ~krll;
    krrr = ~krlr;
    memcpy(kk+24, (unsigned char *)&krrl, 4);
    memcpy(kk+28, (unsigned char *)&krrr, 4);
    camellia_setup256(kk, subkey);
}


/**
 * Stuff related to camellia encryption/decryption
 */
void
camellia_encrypt128(const uint32_t *subkey, uint32_t *io)
{
    uint32_t il, ir, t0, t1;

    /* pre whitening but absorb kw2*/
    io[0] ^= SUBL(0);
    io[1] ^= SUBR(0);
    /* main iteration */

    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(2),SUBR(2),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(3),SUBR(3),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(4),SUBR(4),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(5),SUBR(5),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(6),SUBR(6),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(7),SUBR(7),
		     io[0],io[1],il,ir,t0,t1);

    CAMELLIA_FLS(io[0],io[1],io[2],io[3], SUBL(8),SUBR(8), SUBL(9),SUBR(9),
		 t0,t1,il,ir);

    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(10),SUBR(10),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(11),SUBR(11),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(12),SUBR(12),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(13),SUBR(13),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(14),SUBR(14),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(15),SUBR(15),
		     io[0],io[1],il,ir,t0,t1);

    CAMELLIA_FLS(io[0],io[1],io[2],io[3], SUBL(16), SUBR(16), SUBL(17),SUBR(17),
		 t0,t1,il,ir);

    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(18),SUBR(18),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(19),SUBR(19),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(20),SUBR(20),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(21),SUBR(21),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(22),SUBR(22),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(23),SUBR(23),
		     io[0],io[1],il,ir,t0,t1);

    /* post whitening but kw4 */
    io[2] ^= SUBL(24);
    io[3] ^= SUBR(24);

    t0 = io[0];
    t1 = io[1];
    io[0] = io[2];
    io[1] = io[3];
    io[2] = t0;
    io[3] = t1;
}

void
camellia_decrypt128(const uint32_t *subkey, uint32_t *io)
{
    uint32_t il,ir,t0,t1;               /* temporary valiables */
    
    /* pre whitening but absorb kw2*/
    io[0] ^= SUBL(24);
    io[1] ^= SUBR(24);

    /* main iteration */
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(23),SUBR(23),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(22),SUBR(22),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(21),SUBR(21),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(20),SUBR(20),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(19),SUBR(19),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(18),SUBR(18),
		     io[0],io[1],il,ir,t0,t1);

    CAMELLIA_FLS(io[0],io[1],io[2],io[3],SUBL(17),SUBR(17),SUBL(16),SUBR(16),
		 t0,t1,il,ir);

    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(15),SUBR(15),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(14),SUBR(14),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(13),SUBR(13),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(12),SUBR(12),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(11),SUBR(11),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(10),SUBR(10),
		     io[0],io[1],il,ir,t0,t1);

    CAMELLIA_FLS(io[0],io[1],io[2],io[3], SUBL(9),SUBR(9), SUBL(8),SUBR(8),
		 t0,t1,il,ir);

    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(7),SUBR(7),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(6),SUBR(6),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(5),SUBR(5),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(4),SUBR(4),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(3),SUBR(3),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(2),SUBR(2),
		     io[0],io[1],il,ir,t0,t1);

    /* post whitening but kw4 */
    io[2] ^= SUBL(0);
    io[3] ^= SUBR(0);

    t0 = io[0];
    t1 = io[1];
    io[0] = io[2];
    io[1] = io[3];
    io[2] = t0;
    io[3] = t1;
}

/**
 * stuff for 192 and 256bit encryption/decryption
 */
void
camellia_encrypt256(const uint32_t *subkey, uint32_t *io)
{
    uint32_t il,ir,t0,t1;           /* temporary valiables */

    /* pre whitening but absorb kw2*/
    io[0] ^= SUBL(0);
    io[1] ^= SUBR(0);

    /* main iteration */
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(2),SUBR(2),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(3),SUBR(3),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(4),SUBR(4),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(5),SUBR(5),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(6),SUBR(6),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(7),SUBR(7),
		     io[0],io[1],il,ir,t0,t1);

    CAMELLIA_FLS(io[0],io[1],io[2],io[3], SUBL(8),SUBR(8), SUBL(9),SUBR(9),
		 t0,t1,il,ir);

    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(10),SUBR(10),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(11),SUBR(11),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(12),SUBR(12),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(13),SUBR(13),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(14),SUBR(14),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(15),SUBR(15),
		     io[0],io[1],il,ir,t0,t1);

    CAMELLIA_FLS(io[0],io[1],io[2],io[3], SUBL(16),SUBR(16), SUBL(17),SUBR(17),
		 t0,t1,il,ir);

    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(18),SUBR(18),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(19),SUBR(19),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(20),SUBR(20),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(21),SUBR(21),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(22),SUBR(22),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(23),SUBR(23),
		     io[0],io[1],il,ir,t0,t1);

    CAMELLIA_FLS(io[0],io[1],io[2],io[3], SUBL(24),SUBR(24), SUBL(25),SUBR(25),
		 t0,t1,il,ir);

    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(26),SUBR(26),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(27),SUBR(27),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(28),SUBR(28),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(29),SUBR(29),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(30),SUBR(30),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(31),SUBR(31),
		     io[0],io[1],il,ir,t0,t1);

    /* post whitening but kw4 */
    io[2] ^= SUBL(32);
    io[3] ^= SUBR(32);

    t0 = io[0];
    t1 = io[1];
    io[0] = io[2];
    io[1] = io[3];
    io[2] = t0;
    io[3] = t1;
}

void
camellia_decrypt256(const uint32_t *subkey, uint32_t *io)
{
    uint32_t il,ir,t0,t1;           /* temporary valiables */

    /* pre whitening but absorb kw2*/
    io[0] ^= SUBL(32);
    io[1] ^= SUBR(32);
	
    /* main iteration */
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(31),SUBR(31),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(30),SUBR(30),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(29),SUBR(29),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(28),SUBR(28),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(27),SUBR(27),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(26),SUBR(26),
		     io[0],io[1],il,ir,t0,t1);

    CAMELLIA_FLS(io[0],io[1],io[2],io[3], SUBL(25),SUBR(25), SUBL(24),SUBR(24),
		 t0,t1,il,ir);

    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(23),SUBR(23),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(22),SUBR(22),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(21),SUBR(21),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(20),SUBR(20),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(19),SUBR(19),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(18),SUBR(18),
		     io[0],io[1],il,ir,t0,t1);

    CAMELLIA_FLS(io[0],io[1],io[2],io[3], SUBL(17),SUBR(17), SUBL(16),SUBR(16),
		 t0,t1,il,ir);

    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(15),SUBR(15),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(14),SUBR(14),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(13),SUBR(13),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(12),SUBR(12),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(11),SUBR(11),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(10),SUBR(10),
		     io[0],io[1],il,ir,t0,t1);

    CAMELLIA_FLS(io[0],io[1],io[2],io[3], SUBL(9),SUBR(9), SUBL(8),SUBR(8),
		 t0,t1,il,ir);

    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(7),SUBR(7),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(6),SUBR(6),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(5),SUBR(5),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(4),SUBR(4),
		     io[0],io[1],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[0],io[1], SUBL(3),SUBR(3),
		     io[2],io[3],il,ir,t0,t1);
    CAMELLIA_ROUNDSM(io[2],io[3], SUBL(2),SUBR(2),
		     io[0],io[1],il,ir,t0,t1);

    /* post whitening but kw4 */
    io[2] ^= SUBL(0);
    io[3] ^= SUBR(0);

    t0 = io[0];
    t1 = io[1];
    io[0] = io[2];
    io[1] = io[3];
    io[2] = t0;
    io[3] = t1;
}

void
Camellia_Ekeygen(const int keyBitLength, 
		 const unsigned char *rawKey, 
		 uint32_t *subkey)
{
    KASSERT(keyBitLength == 128 || keyBitLength == 192 || keyBitLength == 256,
	    ("Invalid key size (%d).", keyBitLength));

    switch(keyBitLength) {
    case 128:
	camellia_setup128(rawKey, subkey);
	break;
    case 192:
	camellia_setup192(rawKey, subkey);
	break;
    case 256:
	camellia_setup256(rawKey, subkey);
	break;
    default:
	break;
    }
}
void
Camellia_EncryptBlock(const int keyBitLength, 
		      const unsigned char *plaintext, 
		      const uint32_t *subkey,
		      unsigned char *ciphertext)
{
    uint32_t tmp[4];

    tmp[0] = GETU32(plaintext);
    tmp[1] = GETU32(plaintext + 4);
    tmp[2] = GETU32(plaintext + 8);
    tmp[3] = GETU32(plaintext + 12);

    switch (keyBitLength) {
    case 128:
	camellia_encrypt128(subkey, tmp);
	break;
    case 192:
	/* fall through */
    case 256:
	camellia_encrypt256(subkey, tmp);
	break;
    default:
	break;
    }

    PUTU32(ciphertext,    tmp[0]);
    PUTU32(ciphertext+4,  tmp[1]);
    PUTU32(ciphertext+8,  tmp[2]);
    PUTU32(ciphertext+12, tmp[3]);
}

void
Camellia_DecryptBlock(const int keyBitLength, 
		      const unsigned char *ciphertext, 
		      const uint32_t *subkey,
		      unsigned char *plaintext)
{
    uint32_t tmp[4];
 
    tmp[0] = GETU32(ciphertext);
    tmp[1] = GETU32(ciphertext + 4);
    tmp[2] = GETU32(ciphertext + 8);
    tmp[3] = GETU32(ciphertext + 12);

    switch (keyBitLength) {
    case 128:
	camellia_decrypt128(subkey, tmp);
	break;
    case 192:
	/* fall through */
    case 256:
	camellia_decrypt256(subkey, tmp);
	break;
    default:
	break;
    }

    PUTU32(plaintext,    tmp[0]);
    PUTU32(plaintext+4,  tmp[1]);
    PUTU32(plaintext+8,  tmp[2]);
    PUTU32(plaintext+12, tmp[3]);
}
