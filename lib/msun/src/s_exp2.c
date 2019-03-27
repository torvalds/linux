/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 David Schultz <das@FreeBSD.ORG>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <float.h>

#include "math.h"
#include "math_private.h"

#define	TBLBITS	8
#define	TBLSIZE	(1 << TBLBITS)

static const double
    redux    = 0x1.8p52 / TBLSIZE,
    P1	     = 0x1.62e42fefa39efp-1,
    P2	     = 0x1.ebfbdff82c575p-3,
    P3	     = 0x1.c6b08d704a0a6p-5,
    P4	     = 0x1.3b2ab88f70400p-7,
    P5	     = 0x1.5d88003875c74p-10;

static volatile double
    huge     = 0x1p1000,
    twom1000 = 0x1p-1000;

static const double tbl[TBLSIZE * 2] = {
/*	exp2(z + eps)		eps	*/
	0x1.6a09e667f3d5dp-1,	 0x1.9880p-44,
	0x1.6b052fa751744p-1,	 0x1.8000p-50,
	0x1.6c012750bd9fep-1,	-0x1.8780p-45,
	0x1.6cfdcddd476bfp-1,	 0x1.ec00p-46,
	0x1.6dfb23c651a29p-1,	-0x1.8000p-50,
	0x1.6ef9298593ae3p-1,	-0x1.c000p-52,
	0x1.6ff7df9519386p-1,	-0x1.fd80p-45,
	0x1.70f7466f42da3p-1,	-0x1.c880p-45,
	0x1.71f75e8ec5fc3p-1,	 0x1.3c00p-46,
	0x1.72f8286eacf05p-1,	-0x1.8300p-44,
	0x1.73f9a48a58152p-1,	-0x1.0c00p-47,
	0x1.74fbd35d7ccfcp-1,	 0x1.f880p-45,
	0x1.75feb564267f1p-1,	 0x1.3e00p-47,
	0x1.77024b1ab6d48p-1,	-0x1.7d00p-45,
	0x1.780694fde5d38p-1,	-0x1.d000p-50,
	0x1.790b938ac1d00p-1,	 0x1.3000p-49,
	0x1.7a11473eb0178p-1,	-0x1.d000p-49,
	0x1.7b17b0976d060p-1,	 0x1.0400p-45,
	0x1.7c1ed0130c133p-1,	 0x1.0000p-53,
	0x1.7d26a62ff8636p-1,	-0x1.6900p-45,
	0x1.7e2f336cf4e3bp-1,	-0x1.2e00p-47,
	0x1.7f3878491c3e8p-1,	-0x1.4580p-45,
	0x1.80427543e1b4ep-1,	 0x1.3000p-44,
	0x1.814d2add1071ap-1,	 0x1.f000p-47,
	0x1.82589994ccd7ep-1,	-0x1.1c00p-45,
	0x1.8364c1eb942d0p-1,	 0x1.9d00p-45,
	0x1.8471a4623cab5p-1,	 0x1.7100p-43,
	0x1.857f4179f5bbcp-1,	 0x1.2600p-45,
	0x1.868d99b4491afp-1,	-0x1.2c40p-44,
	0x1.879cad931a395p-1,	-0x1.3000p-45,
	0x1.88ac7d98a65b8p-1,	-0x1.a800p-45,
	0x1.89bd0a4785800p-1,	-0x1.d000p-49,
	0x1.8ace5422aa223p-1,	 0x1.3280p-44,
	0x1.8be05bad619fap-1,	 0x1.2b40p-43,
	0x1.8cf3216b54383p-1,	-0x1.ed00p-45,
	0x1.8e06a5e08664cp-1,	-0x1.0500p-45,
	0x1.8f1ae99157807p-1,	 0x1.8280p-45,
	0x1.902fed0282c0ep-1,	-0x1.cb00p-46,
	0x1.9145b0b91ff96p-1,	-0x1.5e00p-47,
	0x1.925c353aa2ff9p-1,	 0x1.5400p-48,
	0x1.93737b0cdc64ap-1,	 0x1.7200p-46,
	0x1.948b82b5f98aep-1,	-0x1.9000p-47,
	0x1.95a44cbc852cbp-1,	 0x1.5680p-45,
	0x1.96bdd9a766f21p-1,	-0x1.6d00p-44,
	0x1.97d829fde4e2ap-1,	-0x1.1000p-47,
	0x1.98f33e47a23a3p-1,	 0x1.d000p-45,
	0x1.9a0f170ca0604p-1,	-0x1.8a40p-44,
	0x1.9b2bb4d53ff89p-1,	 0x1.55c0p-44,
	0x1.9c49182a3f15bp-1,	 0x1.6b80p-45,
	0x1.9d674194bb8c5p-1,	-0x1.c000p-49,
	0x1.9e86319e3238ep-1,	 0x1.7d00p-46,
	0x1.9fa5e8d07f302p-1,	 0x1.6400p-46,
	0x1.a0c667b5de54dp-1,	-0x1.5000p-48,
	0x1.a1e7aed8eb8f6p-1,	 0x1.9e00p-47,
	0x1.a309bec4a2e27p-1,	 0x1.ad80p-45,
	0x1.a42c980460a5dp-1,	-0x1.af00p-46,
	0x1.a5503b23e259bp-1,	 0x1.b600p-47,
	0x1.a674a8af46213p-1,	 0x1.8880p-44,
	0x1.a799e1330b3a7p-1,	 0x1.1200p-46,
	0x1.a8bfe53c12e8dp-1,	 0x1.6c00p-47,
	0x1.a9e6b5579fcd2p-1,	-0x1.9b80p-45,
	0x1.ab0e521356fb8p-1,	 0x1.b700p-45,
	0x1.ac36bbfd3f381p-1,	 0x1.9000p-50,
	0x1.ad5ff3a3c2780p-1,	 0x1.4000p-49,
	0x1.ae89f995ad2a3p-1,	-0x1.c900p-45,
	0x1.afb4ce622f367p-1,	 0x1.6500p-46,
	0x1.b0e07298db790p-1,	 0x1.fd40p-45,
	0x1.b20ce6c9a89a9p-1,	 0x1.2700p-46,
	0x1.b33a2b84f1a4bp-1,	 0x1.d470p-43,
	0x1.b468415b747e7p-1,	-0x1.8380p-44,
	0x1.b59728de5593ap-1,	 0x1.8000p-54,
	0x1.b6c6e29f1c56ap-1,	 0x1.ad00p-47,
	0x1.b7f76f2fb5e50p-1,	 0x1.e800p-50,
	0x1.b928cf22749b2p-1,	-0x1.4c00p-47,
	0x1.ba5b030a10603p-1,	-0x1.d700p-47,
	0x1.bb8e0b79a6f66p-1,	 0x1.d900p-47,
	0x1.bcc1e904bc1ffp-1,	 0x1.2a00p-47,
	0x1.bdf69c3f3a16fp-1,	-0x1.f780p-46,
	0x1.bf2c25bd71db8p-1,	-0x1.0a00p-46,
	0x1.c06286141b2e9p-1,	-0x1.1400p-46,
	0x1.c199bdd8552e0p-1,	 0x1.be00p-47,
	0x1.c2d1cd9fa64eep-1,	-0x1.9400p-47,
	0x1.c40ab5fffd02fp-1,	-0x1.ed00p-47,
	0x1.c544778fafd15p-1,	 0x1.9660p-44,
	0x1.c67f12e57d0cbp-1,	-0x1.a100p-46,
	0x1.c7ba88988c1b6p-1,	-0x1.8458p-42,
	0x1.c8f6d9406e733p-1,	-0x1.a480p-46,
	0x1.ca3405751c4dfp-1,	 0x1.b000p-51,
	0x1.cb720dcef9094p-1,	 0x1.1400p-47,
	0x1.ccb0f2e6d1689p-1,	 0x1.0200p-48,
	0x1.cdf0b555dc412p-1,	 0x1.3600p-48,
	0x1.cf3155b5bab3bp-1,	-0x1.6900p-47,
	0x1.d072d4a0789bcp-1,	 0x1.9a00p-47,
	0x1.d1b532b08c8fap-1,	-0x1.5e00p-46,
	0x1.d2f87080d8a85p-1,	 0x1.d280p-46,
	0x1.d43c8eacaa203p-1,	 0x1.1a00p-47,
	0x1.d5818dcfba491p-1,	 0x1.f000p-50,
	0x1.d6c76e862e6a1p-1,	-0x1.3a00p-47,
	0x1.d80e316c9834ep-1,	-0x1.cd80p-47,
	0x1.d955d71ff6090p-1,	 0x1.4c00p-48,
	0x1.da9e603db32aep-1,	 0x1.f900p-48,
	0x1.dbe7cd63a8325p-1,	 0x1.9800p-49,
	0x1.dd321f301b445p-1,	-0x1.5200p-48,
	0x1.de7d5641c05bfp-1,	-0x1.d700p-46,
	0x1.dfc97337b9aecp-1,	-0x1.6140p-46,
	0x1.e11676b197d5ep-1,	 0x1.b480p-47,
	0x1.e264614f5a3e7p-1,	 0x1.0ce0p-43,
	0x1.e3b333b16ee5cp-1,	 0x1.c680p-47,
	0x1.e502ee78b3fb4p-1,	-0x1.9300p-47,
	0x1.e653924676d68p-1,	-0x1.5000p-49,
	0x1.e7a51fbc74c44p-1,	-0x1.7f80p-47,
	0x1.e8f7977cdb726p-1,	-0x1.3700p-48,
	0x1.ea4afa2a490e8p-1,	 0x1.5d00p-49,
	0x1.eb9f4867ccae4p-1,	 0x1.61a0p-46,
	0x1.ecf482d8e680dp-1,	 0x1.5500p-48,
	0x1.ee4aaa2188514p-1,	 0x1.6400p-51,
	0x1.efa1bee615a13p-1,	-0x1.e800p-49,
	0x1.f0f9c1cb64106p-1,	-0x1.a880p-48,
	0x1.f252b376bb963p-1,	-0x1.c900p-45,
	0x1.f3ac948dd7275p-1,	 0x1.a000p-53,
	0x1.f50765b6e4524p-1,	-0x1.4f00p-48,
	0x1.f6632798844fdp-1,	 0x1.a800p-51,
	0x1.f7bfdad9cbe38p-1,	 0x1.abc0p-48,
	0x1.f91d802243c82p-1,	-0x1.4600p-50,
	0x1.fa7c1819e908ep-1,	-0x1.b0c0p-47,
	0x1.fbdba3692d511p-1,	-0x1.0e00p-51,
	0x1.fd3c22b8f7194p-1,	-0x1.0de8p-46,
	0x1.fe9d96b2a23eep-1,	 0x1.e430p-49,
	0x1.0000000000000p+0,	 0x0.0000p+0,
	0x1.00b1afa5abcbep+0,	-0x1.3400p-52,
	0x1.0163da9fb3303p+0,	-0x1.2170p-46,
	0x1.02168143b0282p+0,	 0x1.a400p-52,
	0x1.02c9a3e77806cp+0,	 0x1.f980p-49,
	0x1.037d42e11bbcap+0,	-0x1.7400p-51,
	0x1.04315e86e7f89p+0,	 0x1.8300p-50,
	0x1.04e5f72f65467p+0,	-0x1.a3f0p-46,
	0x1.059b0d315855ap+0,	-0x1.2840p-47,
	0x1.0650a0e3c1f95p+0,	 0x1.1600p-48,
	0x1.0706b29ddf71ap+0,	 0x1.5240p-46,
	0x1.07bd42b72a82dp+0,	-0x1.9a00p-49,
	0x1.0874518759bd0p+0,	 0x1.6400p-49,
	0x1.092bdf66607c8p+0,	-0x1.0780p-47,
	0x1.09e3ecac6f383p+0,	-0x1.8000p-54,
	0x1.0a9c79b1f3930p+0,	 0x1.fa00p-48,
	0x1.0b5586cf988fcp+0,	-0x1.ac80p-48,
	0x1.0c0f145e46c8ap+0,	 0x1.9c00p-50,
	0x1.0cc922b724816p+0,	 0x1.5200p-47,
	0x1.0d83b23395dd8p+0,	-0x1.ad00p-48,
	0x1.0e3ec32d3d1f3p+0,	 0x1.bac0p-46,
	0x1.0efa55fdfa9a6p+0,	-0x1.4e80p-47,
	0x1.0fb66affed2f0p+0,	-0x1.d300p-47,
	0x1.1073028d7234bp+0,	 0x1.1500p-48,
	0x1.11301d0125b5bp+0,	 0x1.c000p-49,
	0x1.11edbab5e2af9p+0,	 0x1.6bc0p-46,
	0x1.12abdc06c31d5p+0,	 0x1.8400p-49,
	0x1.136a814f2047dp+0,	-0x1.ed00p-47,
	0x1.1429aaea92de9p+0,	 0x1.8e00p-49,
	0x1.14e95934f3138p+0,	 0x1.b400p-49,
	0x1.15a98c8a58e71p+0,	 0x1.5300p-47,
	0x1.166a45471c3dfp+0,	 0x1.3380p-47,
	0x1.172b83c7d5211p+0,	 0x1.8d40p-45,
	0x1.17ed48695bb9fp+0,	-0x1.5d00p-47,
	0x1.18af9388c8d93p+0,	-0x1.c880p-46,
	0x1.1972658375d66p+0,	 0x1.1f00p-46,
	0x1.1a35beb6fcba7p+0,	 0x1.0480p-46,
	0x1.1af99f81387e3p+0,	-0x1.7390p-43,
	0x1.1bbe084045d54p+0,	 0x1.4e40p-45,
	0x1.1c82f95281c43p+0,	-0x1.a200p-47,
	0x1.1d4873168b9b2p+0,	 0x1.3800p-49,
	0x1.1e0e75eb44031p+0,	 0x1.ac00p-49,
	0x1.1ed5022fcd938p+0,	 0x1.1900p-47,
	0x1.1f9c18438cdf7p+0,	-0x1.b780p-46,
	0x1.2063b88628d8fp+0,	 0x1.d940p-45,
	0x1.212be3578a81ep+0,	 0x1.8000p-50,
	0x1.21f49917ddd41p+0,	 0x1.b340p-45,
	0x1.22bdda2791323p+0,	 0x1.9f80p-46,
	0x1.2387a6e7561e7p+0,	-0x1.9c80p-46,
	0x1.2451ffb821427p+0,	 0x1.2300p-47,
	0x1.251ce4fb2a602p+0,	-0x1.3480p-46,
	0x1.25e85711eceb0p+0,	 0x1.2700p-46,
	0x1.26b4565e27d16p+0,	 0x1.1d00p-46,
	0x1.2780e341de00fp+0,	 0x1.1ee0p-44,
	0x1.284dfe1f5633ep+0,	-0x1.4c00p-46,
	0x1.291ba7591bb30p+0,	-0x1.3d80p-46,
	0x1.29e9df51fdf09p+0,	 0x1.8b00p-47,
	0x1.2ab8a66d10e9bp+0,	-0x1.27c0p-45,
	0x1.2b87fd0dada3ap+0,	 0x1.a340p-45,
	0x1.2c57e39771af9p+0,	-0x1.0800p-46,
	0x1.2d285a6e402d9p+0,	-0x1.ed00p-47,
	0x1.2df961f641579p+0,	-0x1.4200p-48,
	0x1.2ecafa93e2ecfp+0,	-0x1.4980p-45,
	0x1.2f9d24abd8822p+0,	-0x1.6300p-46,
	0x1.306fe0a31b625p+0,	-0x1.2360p-44,
	0x1.31432edeea50bp+0,	-0x1.0df8p-40,
	0x1.32170fc4cd7b8p+0,	-0x1.2480p-45,
	0x1.32eb83ba8e9a2p+0,	-0x1.5980p-45,
	0x1.33c08b2641766p+0,	 0x1.ed00p-46,
	0x1.3496266e3fa27p+0,	-0x1.c000p-50,
	0x1.356c55f929f0fp+0,	-0x1.0d80p-44,
	0x1.36431a2de88b9p+0,	 0x1.2c80p-45,
	0x1.371a7373aaa39p+0,	 0x1.0600p-45,
	0x1.37f26231e74fep+0,	-0x1.6600p-46,
	0x1.38cae6d05d838p+0,	-0x1.ae00p-47,
	0x1.39a401b713ec3p+0,	-0x1.4720p-43,
	0x1.3a7db34e5a020p+0,	 0x1.8200p-47,
	0x1.3b57fbfec6e95p+0,	 0x1.e800p-44,
	0x1.3c32dc313a8f2p+0,	 0x1.f800p-49,
	0x1.3d0e544ede122p+0,	-0x1.7a00p-46,
	0x1.3dea64c1234bbp+0,	 0x1.6300p-45,
	0x1.3ec70df1c4eccp+0,	-0x1.8a60p-43,
	0x1.3fa4504ac7e8cp+0,	-0x1.cdc0p-44,
	0x1.40822c367a0bbp+0,	 0x1.5b80p-45,
	0x1.4160a21f72e95p+0,	 0x1.ec00p-46,
	0x1.423fb27094646p+0,	-0x1.3600p-46,
	0x1.431f5d950a920p+0,	 0x1.3980p-45,
	0x1.43ffa3f84b9ebp+0,	 0x1.a000p-48,
	0x1.44e0860618919p+0,	-0x1.6c00p-48,
	0x1.45c2042a7d201p+0,	-0x1.bc00p-47,
	0x1.46a41ed1d0016p+0,	-0x1.2800p-46,
	0x1.4786d668b3326p+0,	 0x1.0e00p-44,
	0x1.486a2b5c13c00p+0,	-0x1.d400p-45,
	0x1.494e1e192af04p+0,	 0x1.c200p-47,
	0x1.4a32af0d7d372p+0,	-0x1.e500p-46,
	0x1.4b17dea6db801p+0,	 0x1.7800p-47,
	0x1.4bfdad53629e1p+0,	-0x1.3800p-46,
	0x1.4ce41b817c132p+0,	 0x1.0800p-47,
	0x1.4dcb299fddddbp+0,	 0x1.c700p-45,
	0x1.4eb2d81d8ab96p+0,	-0x1.ce00p-46,
	0x1.4f9b2769d2d02p+0,	 0x1.9200p-46,
	0x1.508417f4531c1p+0,	-0x1.8c00p-47,
	0x1.516daa2cf662ap+0,	-0x1.a000p-48,
	0x1.5257de83f51eap+0,	 0x1.a080p-43,
	0x1.5342b569d4edap+0,	-0x1.6d80p-45,
	0x1.542e2f4f6ac1ap+0,	-0x1.2440p-44,
	0x1.551a4ca5d94dbp+0,	 0x1.83c0p-43,
	0x1.56070dde9116bp+0,	 0x1.4b00p-45,
	0x1.56f4736b529dep+0,	 0x1.15a0p-43,
	0x1.57e27dbe2c40ep+0,	-0x1.9e00p-45,
	0x1.58d12d497c76fp+0,	-0x1.3080p-45,
	0x1.59c0827ff0b4cp+0,	 0x1.dec0p-43,
	0x1.5ab07dd485427p+0,	-0x1.4000p-51,
	0x1.5ba11fba87af4p+0,	 0x1.0080p-44,
	0x1.5c9268a59460bp+0,	-0x1.6c80p-45,
	0x1.5d84590998e3fp+0,	 0x1.69a0p-43,
	0x1.5e76f15ad20e1p+0,	-0x1.b400p-46,
	0x1.5f6a320dcebcap+0,	 0x1.7700p-46,
	0x1.605e1b976dcb8p+0,	 0x1.6f80p-45,
	0x1.6152ae6cdf715p+0,	 0x1.1000p-47,
	0x1.6247eb03a5531p+0,	-0x1.5d00p-46,
	0x1.633dd1d1929b5p+0,	-0x1.2d00p-46,
	0x1.6434634ccc313p+0,	-0x1.a800p-49,
	0x1.652b9febc8efap+0,	-0x1.8600p-45,
	0x1.6623882553397p+0,	 0x1.1fe0p-40,
	0x1.671c1c708328ep+0,	-0x1.7200p-44,
	0x1.68155d44ca97ep+0,	 0x1.6800p-49,
	0x1.690f4b19e9471p+0,	-0x1.9780p-45,
};

/*
 * exp2(x): compute the base 2 exponential of x
 *
 * Accuracy: Peak error < 0.503 ulp for normalized results.
 *
 * Method: (accurate tables)
 *
 *   Reduce x:
 *     x = 2**k + y, for integer k and |y| <= 1/2.
 *     Thus we have exp2(x) = 2**k * exp2(y).
 *
 *   Reduce y:
 *     y = i/TBLSIZE + z - eps[i] for integer i near y * TBLSIZE.
 *     Thus we have exp2(y) = exp2(i/TBLSIZE) * exp2(z - eps[i]),
 *     with |z - eps[i]| <= 2**-9 + 2**-39 for the table used.
 *
 *   We compute exp2(i/TBLSIZE) via table lookup and exp2(z - eps[i]) via
 *   a degree-5 minimax polynomial with maximum error under 1.3 * 2**-61.
 *   The values in exp2t[] and eps[] are chosen such that
 *   exp2t[i] = exp2(i/TBLSIZE + eps[i]), and eps[i] is a small offset such
 *   that exp2t[i] is accurate to 2**-64.
 *
 *   Note that the range of i is +-TBLSIZE/2, so we actually index the tables
 *   by i0 = i + TBLSIZE/2.  For cache efficiency, exp2t[] and eps[] are
 *   virtual tables, interleaved in the real table tbl[].
 *
 *   This method is due to Gal, with many details due to Gal and Bachelis:
 *
 *	Gal, S. and Bachelis, B.  An Accurate Elementary Mathematical Library
 *	for the IEEE Floating Point Standard.  TOMS 17(1), 26-46 (1991).
 */
double
exp2(double x)
{
	double r, t, twopk, twopkp1000, z;
	uint32_t hx, ix, lx, i0;
	int k;

	/* Filter out exceptional cases. */
	GET_HIGH_WORD(hx,x);
	ix = hx & 0x7fffffff;		/* high word of |x| */
	if(ix >= 0x40900000) {			/* |x| >= 1024 */
		if(ix >= 0x7ff00000) {
			GET_LOW_WORD(lx,x);
			if(((ix & 0xfffff) | lx) != 0 || (hx & 0x80000000) == 0)
				return (x + x);	/* x is NaN or +Inf */
			else 
				return (0.0);	/* x is -Inf */
		}
		if(x >= 0x1.0p10)
			return (huge * huge); /* overflow */
		if(x <= -0x1.0ccp10)
			return (twom1000 * twom1000); /* underflow */
	} else if (ix < 0x3c900000) {		/* |x| < 0x1p-54 */
		return (1.0 + x);
	}

	/* Reduce x, computing z, i0, and k. */
	STRICT_ASSIGN(double, t, x + redux);
	GET_LOW_WORD(i0, t);
	i0 += TBLSIZE / 2;
	k = (i0 >> TBLBITS) << 20;
	i0 = (i0 & (TBLSIZE - 1)) << 1;
	t -= redux;
	z = x - t;

	/* Compute r = exp2(y) = exp2t[i0] * p(z - eps[i]). */
	t = tbl[i0];		/* exp2t[i0] */
	z -= tbl[i0 + 1];	/* eps[i0]   */
	if (k >= -(1021 << 20))
		INSERT_WORDS(twopk, 0x3ff00000 + k, 0);
	else
		INSERT_WORDS(twopkp1000, 0x3ff00000 + k + (1000 << 20), 0);
	r = t + t * z * (P1 + z * (P2 + z * (P3 + z * (P4 + z * P5))));

	/* Scale by 2**(k>>20). */
	if(k >= -(1021 << 20)) {
		if (k == 1024 << 20)
			return (r * 2.0 * 0x1p1023);
		return (r * twopk);
	} else {
		return (r * twopkp1000 * twom1000);
	}
}

#if (LDBL_MANT_DIG == 53)
__weak_reference(exp2, exp2l);
#endif
