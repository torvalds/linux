/*-
 * Copyright (c) 2004 Sam Leffler, Errno Consulting
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
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
 * CCMP test module.
 *
 * Test vectors come from section I.7.4 of P802.11i/D7.0, October 2003.
 *
 * To use this tester load the net80211 layer (either as a module or
 * by statically configuring it into your kernel), then kldload this
 * module.  It should automatically run all test cases and print
 * information for each.  To run one or more tests you can specify a
 * tests parameter to the module that is a bit mask of the set of tests
 * you want; e.g. insmod ccmp_test tests=7 will run only test mpdu's
 * 1, 2, and 3.
 */
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/module.h>

#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>

/*
==== CCMP test mpdu   1 ====

-- MPDU Fields

7  Version  = 0
8  Type     = 2   SubType  = 0  Data
9  ToDS     = 0   FromDS   = 0
10  MoreFrag = 0   Retry    = 1
11  PwrMgt   = 0   moreData = 0
12  Encrypt  = 1
13  Order    = 0
14  Duration = 11459
15  A1 = 0f-d2-e1-28-a5-7c    DA
16  A2 = 50-30-f1-84-44-08    SA
17  A3 = ab-ae-a5-b8-fc-ba    BSSID
18  SC = 0x3380
19  seqNum = 824 (0x0338)  fraqNum = 0 (0x00)
20  Algorithm = AES_CCM
21  Key ID = 0
22  TK = c9 7c 1f 67 ce 37 11 85  51 4a 8a 19 f2 bd d5 2f
23  PN = 199027030681356  (0xB5039776E70C)
24  802.11 Header =  08 48 c3 2c 0f d2 e1 28 a5 7c 50 30 f1 84 44 08
25  	ab ae a5 b8 fc ba 80 33
26  Muted 802.11 Header =  08 40 0f d2 e1 28 a5 7c 50 30 f1 84 44 08
27  	ab ae a5 b8 fc ba 00 00
28  CCMP Header =  0c e7 00 20 76 97 03 b5
29  CCM Nonce = 00 50 30 f1 84 44 08 b5  03 97 76 e7 0c
30 Plaintext Data = f8 ba 1a 55 d0 2f 85 ae 96 7b b6 2f b6 cd a8 eb
1	7e 78 a0 50
2  CCM MIC =  78 45 ce 0b 16 f9 76 23
3  -- Encrypted MPDU with FCS
4  08 48 c3 2c 0f d2 e1 28 a5 7c 50 30 f1 84 44 08 ab ae a5 b8 fc ba
5  80 33 0c e7 00 20 76 97 03 b5 f3 d0 a2 fe 9a 3d bf 23 42 a6 43 e4
6  32 46 e8 0c 3c 04 d0 19 78 45 ce 0b 16 f9 76 23 1d 99 f0 66
*/
static const u_int8_t test1_key[] = {		/* TK */
	0xc9, 0x7c, 0x1f, 0x67, 0xce, 0x37, 0x11, 0x85,  0x51, 0x4a, 0x8a,
	0x19, 0xf2, 0xbd, 0xd5, 0x2f
};
static const u_int8_t test1_plaintext[] = {	/* Plaintext MPDU w/o MIC */
	0x08, 0x48, 0xc3, 0x2c, 0x0f, 0xd2, 0xe1, 0x28,	/* 802.11 Header */
	0xa5, 0x7c, 0x50, 0x30, 0xf1, 0x84, 0x44, 0x08,
	0xab, 0xae, 0xa5, 0xb8, 0xfc, 0xba, 0x80, 0x33, 
	0xf8, 0xba, 0x1a, 0x55, 0xd0, 0x2f, 0x85, 0xae,	/* Plaintext Data */
	0x96, 0x7b, 0xb6, 0x2f, 0xb6, 0xcd, 0xa8, 0xeb,
	0x7e, 0x78, 0xa0, 0x50, 
};
static const u_int8_t test1_encrypted[] = {	/* Encrypted MPDU with MIC */
	0x08, 0x48, 0xc3, 0x2c, 0x0f, 0xd2, 0xe1, 0x28,
	0xa5, 0x7c, 0x50, 0x30, 0xf1, 0x84, 0x44, 0x08,
	0xab, 0xae, 0xa5, 0xb8, 0xfc, 0xba, 0x80, 0x33,
	0x0c, 0xe7, 0x00, 0x20, 0x76, 0x97, 0x03, 0xb5,
	0xf3, 0xd0, 0xa2, 0xfe, 0x9a, 0x3d, 0xbf, 0x23,
	0x42, 0xa6, 0x43, 0xe4, 0x32, 0x46, 0xe8, 0x0c,
	0x3c, 0x04, 0xd0, 0x19, 0x78, 0x45, 0xce, 0x0b,
	0x16, 0xf9, 0x76, 0x23,
};

/*
==== CCMP test mpdu   2 ====

-- MPDU Fields

 9  Version  = 0
10  Type     = 2   SubType  = 3  Data+CF-Ack+CF-Poll
11  ToDS     = 0   FromDS   = 0
12  MoreFrag = 0   Retry    = 0
13  PwrMgt   = 0   moreData = 0
14  Encrypt  = 1
15  Order    = 1
16  Duration = 20842
17  A1 = ea-10-0c-84-68-50    DA
18  A2 = ee-c1-76-2c-88-de    SA
19  A3 = af-2e-e9-f4-6a-07    BSSID
20  SC = 0xCCE0
21  seqNum = 3278 (0x0CCE)  fraqNum = 0 (0x00)
22  Algorithm = AES_CCM
23  Key ID = 2
24  TK = 8f 7a 05 3f a5 77 a5 59  75 29 27 20 97 a6 03 d5
25  PN = 54923164817386  (0x31F3CBBA97EA)
26  802.11 Header =  38 c0 6a 51 ea 10 0c 84 68 50 ee c1 76 2c 88 de
27  	af 2e e9 f4 6a 07 e0 cc
28  Muted 802.11 Header =  08 c0 ea 10 0c 84 68 50 ee c1 76 2c 88 de
29  	af 2e e9 f4 6a 07 00 00
30  CCMP Header =  ea 97 00 a0 ba cb f3 31
31  CCM Nonce = 00 ee c1 76 2c 88 de 31  f3 cb ba 97 ea
32  Plaintext Data = 83 a0 63 4b 5e d7 62 7e b9 df 22 5e 05 74 03 42
33  	de 19 41 17
34  CCM MIC =  54 2f bf 8d a0 6a a4 ae
35  -- Encrypted MPDU with FCS
36  38 c0 6a 51 ea 10 0c 84 68 50 ee c1 76 2c 88 de af 2e e9 f4 6a 07
37  e0 cc ea 97 00 a0 ba cb f3 31 81 4b 69 65 d0 5b f2 b2 ed 38 d4 be
38  b0 69 fe 82 71 4a 61 0b 54 2f bf 8d a0 6a a4 ae 25 3c 47 38
*/
static const u_int8_t test2_key[] = {		/* TK */
	0x8f, 0x7a, 0x05, 0x3f, 0xa5, 0x77, 0xa5, 0x59,  0x75, 0x29, 0x27,
	0x20, 0x97, 0xa6, 0x03, 0xd5
};
static const u_int8_t test2_plaintext[] = {	/* Plaintext MPDU w/o MIC */
	0x38, 0xc0, 0x6a, 0x51, 0xea, 0x10, 0x0c, 0x84, 0x68, 0x50, 0xee,
	0xc1, 0x76, 0x2c, 0x88, 0xde, 0xaf, 0x2e, 0xe9, 0xf4, 0x6a, 0x07,
	0xe0, 0xcc,
	0x83, 0xa0, 0x63, 0x4b, 0x5e, 0xd7, 0x62, 0x7e, 0xb9, 0xdf, 0x22,
	0x5e, 0x05, 0x74, 0x03, 0x42, 0xde, 0x19, 0x41, 0x17
};
static const u_int8_t test2_encrypted[] = {	/* Encrypted MPDU with MIC */
	0x38, 0xc0, 0x6a, 0x51, 0xea, 0x10, 0x0c, 0x84, 0x68, 0x50, 0xee,
	0xc1, 0x76, 0x2c, 0x88, 0xde, 0xaf, 0x2e, 0xe9, 0xf4, 0x6a, 0x07,
	0xe0, 0xcc, 0xea, 0x97, 0x00, 0xa0, 0xba, 0xcb, 0xf3, 0x31, 0x81,
	0x4b, 0x69, 0x65, 0xd0, 0x5b, 0xf2, 0xb2, 0xed, 0x38, 0xd4, 0xbe,
	0xb0, 0x69, 0xfe, 0x82, 0x71, 0x4a, 0x61, 0x0b, 0x54, 0x2f, 0xbf,
	0x8d, 0xa0, 0x6a, 0xa4, 0xae,
};

/*
==== CCMP test mpdu   3 ====

-- MPDU Fields

41  Version  = 0
42  Type     = 2   SubType  = 11
43  ToDS     = 0   FromDS   = 0
44  MoreFrag = 0   Retry    = 1
45  PwrMgt   = 0   moreData = 0
46  Encrypt  = 1
47  Order    = 1
48  Duration = 25052
49  A1 = d9-57-7d-f7-63-c8    DA
50 A2 = b6-a8-8a-df-36-91    SA
1  A3 = dc-4a-8b-ca-94-dd    BSSID
2  SC = 0x8260
3  seqNum = 2086 (0x0826)  fraqNum = 0 (0x00)
4  QC = 0x0000
5  MSDU Priority = 0 (0x0)
6  Algorithm = AES_CCM
7  Key ID = 2
8  TK = 40 cf b7 a6 2e 88 01 3b  d6 d3 af fc c1 91 04 1e
9  PN = 52624639632814  (0x2FDCA0F3A5AE)
10  802.11 Header =  b8 c8 dc 61 d9 57 7d f7 63 c8 b6 a8 8a df 36 91
11  	dc 4a 8b ca 94 dd 60 82 20 85
12  Muted 802.11 Header =  88 c0 d9 57 7d f7 63 c8 b6 a8 8a df 36 91
13  	dc 4a 8b ca 94 dd 00 00 00 00
14  CCMP Header =  ae a5 00 a0 f3 a0 dc 2f
15  CCM Nonce = 00 b6 a8 8a df 36 91 2f dc a0 f3 a5 ae
16  Plaintext Data  = 2c 1b d0 36 83 1c 95 49 6c 5f 4d bf 3d 55 9e 72
17  	de 80 2a 18
18  CCM MIC =  fd 1f 1f 61 a9 fb 4b b3
19  -- Encrypted MPDU with FCS
20  b8 c8 dc 61 d9 57 7d f7 63 c8 b6 a8 8a df 36 91 dc 4a 8b ca 94 dd
21  60 82 20 85 ae a5 00 a0 f3 a0 dc 2f 89 d8 58 03 40 b6 26 a0 b6 d4
22  d0 13 bf 18 f2 91 b8 96 46 c8 fd 1f 1f 61 a9 fb 4b b3 60 3f 5a ad
*/
static const u_int8_t test3_key[] = {		/* TK */
	0x40, 0xcf, 0xb7, 0xa6, 0x2e, 0x88, 0x01, 0x3b,  0xd6, 0xd3,
	0xaf, 0xfc, 0xc1, 0x91, 0x04, 0x1e
};
static const u_int8_t test3_plaintext[] = {	/* Plaintext MPDU w/o MIC */
	0xb8, 0xc8, 0xdc, 0x61, 0xd9, 0x57, 0x7d, 0xf7, 0x63, 0xc8,
	0xb6, 0xa8, 0x8a, 0xdf, 0x36, 0x91, 0xdc, 0x4a, 0x8b, 0xca,
	0x94, 0xdd, 0x60, 0x82, 0x20, 0x85,
	0x2c, 0x1b, 0xd0, 0x36, 0x83, 0x1c, 0x95, 0x49, 0x6c, 0x5f,
	0x4d, 0xbf, 0x3d, 0x55, 0x9e, 0x72, 0xde, 0x80, 0x2a, 0x18
};
static const u_int8_t test3_encrypted[] = {	/* Encrypted MPDU with MIC */
	0xb8, 0xc8, 0xdc, 0x61, 0xd9, 0x57, 0x7d, 0xf7, 0x63, 0xc8,
	0xb6, 0xa8, 0x8a, 0xdf, 0x36, 0x91, 0xdc, 0x4a, 0x8b, 0xca,
	0x94, 0xdd, 0x60, 0x82, 0x20, 0x85, 0xae, 0xa5, 0x00, 0xa0,
	0xf3, 0xa0, 0xdc, 0x2f, 0x89, 0xd8, 0x58, 0x03, 0x40, 0xb6,
	0x26, 0xa0, 0xb6, 0xd4, 0xd0, 0x13, 0xbf, 0x18, 0xf2, 0x91,
	0xb8, 0x96, 0x46, 0xc8, 0xfd, 0x1f, 0x1f, 0x61, 0xa9, 0xfb,
	0x4b, 0xb3,
};

/*
==== CCMP test mpdu  4 ==== 

-- MPDU Fields
25  Version  = 0
26  Type     = 2   SubType  = 10
27  ToDS     = 0   FromDS   = 1
28  MoreFrag = 0   Retry    = 1
29  PwrMgt   = 0   moreData = 0
30  Encrypt  = 1
31  Order    = 1
32  Duration = 4410
33  A1 = 71-2a-9d-df-11-db    DA
34  A2 = 8e-f8-22-73-47-01    BSSID
35  A3 = 59-14-0d-d6-46-a2    SA
36  SC = 0x2FC0
37  seqNum = 764 (0x02FC)  fraqNum = 0 (0x00)
38  QC = 0x0007
39  MSDU Priority = 7 (0x0)
40  Algorithm = AES_CCM
41  Key ID = 0
42  TK = 8c 89 a2 eb c9 6c 76 02  70 7f cf 24 b3 2d 38 33
43  PN = 270963670912995  (0xF670A55A0FE3)
44  802.11 Header =  a8 ca 3a 11 71 2a 9d df 11 db 8e f8 22 73 47 01
45  	59 14 0d d6 46 a2 c0 2f 67 a5
46  Muted 802.11 Header =  88 c2 71 2a 9d df 11 db 8e f8 22 73 47 01
47  	59 14 0d d6 46 a2 00 00 07 00
48  CCMP Header =  e3 0f 00 20 5a a5 70 f6
49  CCM Nonce = 07 8e f8 22 73 47 01 f6  70 a5 5a 0f e3
50  Plaintext Data = 4f ad 2b 1c 29 0f a5 eb d8 72 fb c3 f3 a0 74 89
51  	8f 8b 2f bb
52  CCM MIC =  31 fc 88 00 4f 35 ee 3d
-- Encrypted MPDU with FCS
2  a8 ca 3a 11 71 2a 9d df 11 db 8e f8 22 73 47 01 59 14 0d d6 46 a2
3  c0 2f 67 a5 e3 0f 00 20 5a a5 70 f6 9d 59 b1 5f 37 14 48 c2 30 f4
4  d7 39 05 2e 13 ab 3b 1a 7b 10 31 fc 88 00 4f 35 ee 3d 45 a7 4a 30
*/
static const u_int8_t test4_key[] = {		/* TK */
	0x8c, 0x89, 0xa2, 0xeb, 0xc9, 0x6c, 0x76, 0x02,
	0x70, 0x7f, 0xcf, 0x24, 0xb3, 0x2d, 0x38, 0x33,
};
static const u_int8_t test4_plaintext[] = {	/* Plaintext MPDU w/o MIC */
	0xa8, 0xca, 0x3a, 0x11, 0x71, 0x2a, 0x9d, 0xdf, 0x11, 0xdb,
	0x8e, 0xf8, 0x22, 0x73, 0x47, 0x01, 0x59, 0x14, 0x0d, 0xd6,
	0x46, 0xa2, 0xc0, 0x2f, 0x67, 0xa5,
	0x4f, 0xad, 0x2b, 0x1c, 0x29, 0x0f, 0xa5, 0xeb, 0xd8, 0x72,
	0xfb, 0xc3, 0xf3, 0xa0, 0x74, 0x89, 0x8f, 0x8b, 0x2f, 0xbb,
};
static const u_int8_t test4_encrypted[] = {	/* Encrypted MPDU with MIC */
	0xa8, 0xca, 0x3a, 0x11, 0x71, 0x2a, 0x9d, 0xdf, 0x11, 0xdb,
	0x8e, 0xf8, 0x22, 0x73, 0x47, 0x01, 0x59, 0x14, 0x0d, 0xd6,
	0x46, 0xa2, 0xc0, 0x2f, 0x67, 0xa5, 0xe3, 0x0f, 0x00, 0x20,
	0x5a, 0xa5, 0x70, 0xf6, 0x9d, 0x59, 0xb1, 0x5f, 0x37, 0x14,
	0x48, 0xc2, 0x30, 0xf4, 0xd7, 0x39, 0x05, 0x2e, 0x13, 0xab,
	0x3b, 0x1a, 0x7b, 0x10, 0x31, 0xfc, 0x88, 0x00, 0x4f, 0x35,
	0xee, 0x3d,
};

/*
==== CCMP test mpdu   5 ====

-- MPDU Fields

7  Version  = 0
8  Type     = 2   SubType  = 8
9  ToDS     = 0   FromDS   = 1
10  MoreFrag = 0   Retry    = 1
11  PwrMgt   = 1   moreData = 0
12  Encrypt  = 1
13  Order    = 1
14  Duration = 16664
15  A1 = 45-de-c6-9a-74-80    DA
16  A2 = f3-51-94-6b-c9-6b    BSSID
17  A3 = e2-76-fb-e6-c1-27    SA
18  SC = 0xF280
19  seqNum = 3880 (0x0F28)  fraqNum = 0 (0x00)
20  QC = 0x000b
21  MSDU Priority = 0 (0x0)
22  Algorithm = AES_CCM
23  Key ID = 2
24  TK = a5 74 d5 14 3b b2 5e fd  de ff 30 12 2f df d0 66
25  PN = 184717420531255  (0xA7FFE03C0E37)
26  802.11 Header =  88 da 18 41 45 de c6 9a 74 80 f3 51 94 6b c9 6b
27  	e2 76 fb e6 c1 27 80 f2 4b 19
28  Muted 802.11 Header =  88 c2 45 de c6 9a 74 80 f3 51 94 6b c9 6b
29  	e2 76 fb e6 c1 27 00 00 0b 00
30  CCMP Header =  37 0e 00 a0 3c e0 ff a7
31  CCM Nonce = 0b f3 51 94 6b c9 6b a7 ff e0 3c 0e 37
32  Plaintext Data = 28 96 9b 95 4f 26 3a 80 18 a9 ef 70 a8 b0 51 46
33  	24 81 92 2e
34  CCM MIC =  ce 0c 3b e1 97 d3 05 eb
35  -- Encrypted MPDU with FCS
36  88 da 18 41 45 de c6 9a 74 80 f3 51 94 6b c9 6b e2 76 fb e6 c1 27
37  80 f2 4b 19 37 0e 00 a0 3c e0 ff a7 eb 4a e4 95 6a 80 1d a9 62 4b
38  7e 0c 18 b2 3e 61 5e c0 3a f6 ce 0c 3b e1 97 d3 05 eb c8 9e a1 b5
*/
static const u_int8_t test5_key[] = {		/* TK */
	0xa5, 0x74, 0xd5, 0x14, 0x3b, 0xb2, 0x5e, 0xfd,
	0xde, 0xff, 0x30, 0x12, 0x2f, 0xdf, 0xd0, 0x66,
};
static const u_int8_t test5_plaintext[] = {	/* Plaintext MPDU w/o MIC */
	0x88, 0xda, 0x18, 0x41, 0x45, 0xde, 0xc6, 0x9a, 0x74, 0x80,
	0xf3, 0x51, 0x94, 0x6b, 0xc9, 0x6b, 0xe2, 0x76, 0xfb, 0xe6,
	0xc1, 0x27, 0x80, 0xf2, 0x4b, 0x19,
	0x28, 0x96, 0x9b, 0x95, 0x4f, 0x26, 0x3a, 0x80, 0x18, 0xa9,
	0xef, 0x70, 0xa8, 0xb0, 0x51, 0x46, 0x24, 0x81, 0x92, 0x2e,
};
static const u_int8_t test5_encrypted[] = {	/* Encrypted MPDU with MIC */
	0x88, 0xda, 0x18, 0x41, 0x45, 0xde, 0xc6, 0x9a, 0x74, 0x80,
	0xf3, 0x51, 0x94, 0x6b, 0xc9, 0x6b, 0xe2, 0x76, 0xfb, 0xe6,
	0xc1, 0x27, 0x80, 0xf2, 0x4b, 0x19, 0x37, 0x0e, 0x00, 0xa0,
	0x3c, 0xe0, 0xff, 0xa7, 0xeb, 0x4a, 0xe4, 0x95, 0x6a, 0x80,
	0x1d, 0xa9, 0x62, 0x4b, 0x7e, 0x0c, 0x18, 0xb2, 0x3e, 0x61,
	0x5e, 0xc0, 0x3a, 0xf6, 0xce, 0x0c, 0x3b, 0xe1, 0x97, 0xd3,
	0x05, 0xeb,
};

/*
==== CCMP test mpdu   6 ====

-- MPDU Fields

41  Version  = 0
42  Type     = 2   SubType  = 8
43  ToDS     = 0   FromDS   = 1
44  MoreFrag = 0   Retry    = 0
45  PwrMgt   = 1   moreData = 0
46  Encrypt  = 1
47  Order    = 0
48  Duration = 8161
49  A1 = 5a-f2-84-30-fd-ab    DA
50  A2 = bf-f9-43-b9-f9-a6    BSSID
1   A3 = ab-1d-98-c7-fe-73    SA
2  SC = 0x7150
3  seqNum = 1813 (0x0715)  fraqNum = 0 (0x00)
4  QC = 0x000d
5  PSDU Priority = 13 (0xd)
6  Algorithm = AES_CCM
7  Key ID = 1
8  TK = f7 1e ea 4e 1f 58 80 4b 97 17 23 0a d0 61 46 41
9  PN    = 118205765159305  (0x6B81ECA48989)
10  802.11 Header =  88 52 e1 1f 5a f2 84 30 fd ab bf f9 43 b9 f9 a6
11  	ab 1d 98 c7 fe 73 50 71  3d 6a
12  Muted 802.11 Header =  88 42 5a f2 84 30 fd ab bf f9 43 b9 f9 a6
13  	ab 1d 98 c7 fe 73 00 00 0d 00
14  CCMP Header =  89 89 00 60 a4 ec 81 6b
15  CCM Nonce = 0d bf f9 43 b9 f9 a6 6b  81 ec a4 89 89
16  Plaintext Data = ab fd a2 2d 3a 0b fc 9c c1 fc 07 93 63 c2 fc a1
17  	43 e6 eb 1d
18  CCM MIC =  30 9a 8d 5c 46 6b bb 71
19  -- Encrypted MPDU with FCS
20  88 52 e1 1f 5a f2 84 30 fd ab bf f9 43 b9 f9 a6 ab 1d 98 c7 fe 73
21  50 71 3d 6a 89 89 00 60 a4 ec 81 6b 9a 70 9b 60 a3 9d 40 b1 df b6
22  12 e1 8b 5f 11 4b ad b6 cc 86 30 9a 8d 5c 46 6b bb 71 86 c0 4e 97
*/
static const u_int8_t test6_key[] = {		/* TK */
	0xf7, 0x1e, 0xea, 0x4e, 0x1f, 0x58, 0x80, 0x4b,
	0x97, 0x17, 0x23, 0x0a, 0xd0, 0x61, 0x46, 0x41,
};
static const u_int8_t test6_plaintext[] = {	/* Plaintext MPDU w/o MIC */
	0x88, 0x52, 0xe1, 0x1f, 0x5a, 0xf2, 0x84, 0x30, 0xfd, 0xab,
	0xbf, 0xf9, 0x43, 0xb9, 0xf9, 0xa6, 0xab, 0x1d, 0x98, 0xc7,
	0xfe, 0x73, 0x50, 0x71, 0x3d, 0x6a,
	0xab, 0xfd, 0xa2, 0x2d, 0x3a, 0x0b, 0xfc, 0x9c, 0xc1, 0xfc,
	0x07, 0x93, 0x63, 0xc2, 0xfc, 0xa1, 0x43, 0xe6, 0xeb, 0x1d,
};
static const u_int8_t test6_encrypted[] = {	/* Encrypted MPDU with MIC */
	0x88, 0x52, 0xe1, 0x1f, 0x5a, 0xf2, 0x84, 0x30, 0xfd, 0xab,
	0xbf, 0xf9, 0x43, 0xb9, 0xf9, 0xa6, 0xab, 0x1d, 0x98, 0xc7,
	0xfe, 0x73, 0x50, 0x71, 0x3d, 0x6a, 0x89, 0x89, 0x00, 0x60,
	0xa4, 0xec, 0x81, 0x6b, 0x9a, 0x70, 0x9b, 0x60, 0xa3, 0x9d,
	0x40, 0xb1, 0xdf, 0xb6, 0x12, 0xe1, 0x8b, 0x5f, 0x11, 0x4b,
	0xad, 0xb6, 0xcc, 0x86, 0x30, 0x9a, 0x8d, 0x5c, 0x46, 0x6b,
	0xbb, 0x71,
};

/*
==== CCMP test mpdu   7 ====

-- MPDU Fields

25  Version  = 0
26  Type     = 2   SubType  = 1  Data+CF-Ack
27  ToDS     = 1   FromDS   = 0
28  MoreFrag = 0   Retry    = 1
29  PwrMgt   = 1   moreData = 1
30  Encrypt  = 1
31  Order    = 0
32  Duration = 18049
33  A1 = 9b-50-f4-fd-56-f6    BSSID
34  A2 = ef-ec-95-20-16-91    SA
35  A3 = 83-57-0c-4c-cd-ee    DA
36  SC = 0xA020
37  seqNum = 2562 (0x0A02)  fraqNum = 0 (0x00)
38  Algorithm = AES_CCM
39  Key ID = 3
40  TK = 1b db 34 98 0e 03 81 24 a1 db 1a 89 2b ec 36 6a
41  PN = 104368786630435  (0x5EEC4073E723)
42  Header =  18 79 81 46 9b 50 f4 fd 56 f6 ef ec 95 20 16 91 83 57
43  	0c 4c cd ee 20 a0
44  Muted MAC Header =  08 41 9b 50 f4 fd 56 f6 ef ec 95 20 16 91
45  	83 57 0c 4c cd ee 00 00
46  CCMP Header =  23 e7 00 e0 73 40 ec 5e
47  CCM Nonce = 00 ef ec 95 20 16 91 5e ec 40 73 e7 23
48  Plaintext Data = 98 be ca 86 f4 b3 8d a2 0c fd f2 47 24 c5 8e b8
49  	35 66 53 39
50  CCM MIC =  2d 09 57 ec fa be 95 b9
-- Encrypted MPDU with FCS
1  18 79 81 46 9b 50 f4 fd 56 f6 ef ec 95 20 16 91 83 57 0c 4c cd ee
2  20 a0 23 e7 00 e0 73 40 ec 5e 12 c5 37 eb f3 ab 58 4e f1 fe f9 a1
3  f3 54 7a 8c 13 b3 22 5a 2d 09 57 ec fa be 95 b9 aa fa 0c c8
*/
static const u_int8_t test7_key[] = {		/* TK */
	0x1b, 0xdb, 0x34, 0x98, 0x0e, 0x03, 0x81, 0x24,
	0xa1, 0xdb, 0x1a, 0x89, 0x2b, 0xec, 0x36, 0x6a,
};
static const u_int8_t test7_plaintext[] = {	/* Plaintext MPDU w/o MIC */
	0x18, 0x79, 0x81, 0x46, 0x9b, 0x50, 0xf4, 0xfd, 0x56, 0xf6,
	0xef, 0xec, 0x95, 0x20, 0x16, 0x91, 0x83, 0x57, 0x0c, 0x4c,
	0xcd, 0xee, 0x20, 0xa0,
	0x98, 0xbe, 0xca, 0x86, 0xf4, 0xb3, 0x8d, 0xa2, 0x0c, 0xfd,
	0xf2, 0x47, 0x24, 0xc5, 0x8e, 0xb8, 0x35, 0x66, 0x53, 0x39,
};
static const u_int8_t test7_encrypted[] = {	/* Encrypted MPDU with MIC */
	0x18, 0x79, 0x81, 0x46, 0x9b, 0x50, 0xf4, 0xfd, 0x56, 0xf6,
	0xef, 0xec, 0x95, 0x20, 0x16, 0x91, 0x83, 0x57, 0x0c, 0x4c,
	0xcd, 0xee, 0x20, 0xa0, 0x23, 0xe7, 0x00, 0xe0, 0x73, 0x40,
	0xec, 0x5e, 0x12, 0xc5, 0x37, 0xeb, 0xf3, 0xab, 0x58, 0x4e,
	0xf1, 0xfe, 0xf9, 0xa1, 0xf3, 0x54, 0x7a, 0x8c, 0x13, 0xb3,
	0x22, 0x5a, 0x2d, 0x09, 0x57, 0xec, 0xfa, 0xbe, 0x95, 0xb9,
};

/*
==== CCMP test mpdu   8 ====

-- MPDU Fields

6  Version  = 0
7  Type     = 2   SubType  = 11
8  ToDS     = 1   FromDS   = 0
9  MoreFrag = 0   Retry    = 1
10  PwrMgt   = 1   moreData = 0
11  Encrypt  = 1
12  Order    = 1
13  Duration = 29260
14  A1 = 55-2d-5f-72-bb-70    BSSID
15  A2 = ca-3f-3a-ae-60-c4    SA
16  A3 = 8b-a9-b5-f8-2c-2f    DA
17  SC = 0xEB50
18  seqNum = 3765 (0x0EB5)  fraqNum = 0 (0x00)
19  QC = 0x000a
20  MSDU Priority = 10 (0xa)
21  Algorithm = AES_CCM
22  Key ID = 2
23  TK = 6e ac 1b f5 4b d5 4e db 23 21 75 43 03 02 4c 71
24  PN    = 227588596223197  (0xCEFD996ECCDD)
25  802.11 Header =  b8 d9 4c 72 55 2d 5f 72 bb 70 ca 3f 3a ae 60 c4
26  	8b a9 b5 f8 2c 2f 50 eb 2a 55
27  Muted 802.11 Header =  88 c1 55 2d 5f 72 bb 70 ca 3f 3a ae 60 c4
28  	8b a9 b5 f8 2c 2f 00 00 0a 00
29  CCMP Header =  dd cc 00 a0 6e 99 fd ce
30  CCM Nonce = 0a ca 3f 3a ae 60 c4 ce fd 99 6e cc dd
31  Plaintext Data = 57 cb 5c 0e 5f cd 88 5e 9a 42 39 e9 b9 ca d6 0d
32  	64 37 59 79
33  CCM MIC =  6d ba 8e f7 f0 80 87 dd
-- Encrypted MPDU with FCS
35  b8 d9 4c 72 55 2d 5f 72 bb 70 ca 3f 3a ae 60 c4 8b a9 b5 f8 2c 2f
36  50 eb 2a 55 dd cc 00 a0 6e 99 fd ce 4b f2 81 ef 8e c7 73 9f 91 59
37  1b 97 a8 7d c1 4b 3f a1 74 62 6d ba 8e f7 f0 80 87 dd 0c 65 74 3f
*/
static const u_int8_t test8_key[] = {		/* TK */
	0x6e, 0xac, 0x1b, 0xf5, 0x4b, 0xd5, 0x4e, 0xdb,
	0x23, 0x21, 0x75, 0x43, 0x03, 0x02, 0x4c, 0x71,
};
static const u_int8_t test8_plaintext[] = {	/* Plaintext MPDU w/o MIC */
	0xb8, 0xd9, 0x4c, 0x72, 0x55, 0x2d, 0x5f, 0x72, 0xbb, 0x70,
	0xca, 0x3f, 0x3a, 0xae, 0x60, 0xc4, 0x8b, 0xa9, 0xb5, 0xf8,
	0x2c, 0x2f, 0x50, 0xeb, 0x2a, 0x55,
	0x57, 0xcb, 0x5c, 0x0e, 0x5f, 0xcd, 0x88, 0x5e, 0x9a, 0x42,
	0x39, 0xe9, 0xb9, 0xca, 0xd6, 0x0d, 0x64, 0x37, 0x59, 0x79,
};
static const u_int8_t test8_encrypted[] = {	/* Encrypted MPDU with MIC */
	0xb8, 0xd9, 0x4c, 0x72, 0x55, 0x2d, 0x5f, 0x72, 0xbb, 0x70,
	0xca, 0x3f, 0x3a, 0xae, 0x60, 0xc4, 0x8b, 0xa9, 0xb5, 0xf8,
	0x2c, 0x2f, 0x50, 0xeb, 0x2a, 0x55, 0xdd, 0xcc, 0x00, 0xa0,
	0x6e, 0x99, 0xfd, 0xce, 0x4b, 0xf2, 0x81, 0xef, 0x8e, 0xc7,
	0x73, 0x9f, 0x91, 0x59, 0x1b, 0x97, 0xa8, 0x7d, 0xc1, 0x4b,
	0x3f, 0xa1, 0x74, 0x62, 0x6d, 0xba, 0x8e, 0xf7, 0xf0, 0x80,
	0x87, 0xdd,
};

#define	TEST(n,name,cipher,keyix,pn) { \
	name, IEEE80211_CIPHER_##cipher,keyix, pn##LL, \
	test##n##_key,   sizeof(test##n##_key), \
	test##n##_plaintext, sizeof(test##n##_plaintext), \
	test##n##_encrypted, sizeof(test##n##_encrypted) \
}

struct ciphertest {
	const char	*name;
	int		cipher;
	int		keyix;
	u_int64_t	pn;
	const u_int8_t	*key;
	size_t		key_len;
	const u_int8_t	*plaintext;
	size_t		plaintext_len;
	const u_int8_t	*encrypted;
	size_t		encrypted_len;
} ccmptests[] = {
	TEST(1, "CCMP test mpdu 1", AES_CCM, 0, 199027030681356),
	TEST(2, "CCMP test mpdu 2", AES_CCM, 2, 54923164817386),
	TEST(3, "CCMP test mpdu 3", AES_CCM, 2, 52624639632814),
	TEST(4, "CCMP test mpdu 4", AES_CCM, 0, 270963670912995),
	TEST(5, "CCMP test mpdu 5", AES_CCM, 2, 184717420531255),
	TEST(6, "CCMP test mpdu 6", AES_CCM, 1, 118205765159305),
	TEST(7, "CCMP test mpdu 7", AES_CCM, 3, 104368786630435),
	TEST(8, "CCMP test mpdu 8", AES_CCM, 2, 227588596223197),
};

static void
dumpdata(const char *tag, const void *p, size_t len)
{
	int i;

	printf("%s: 0x%p len %u", tag, p, len);
	for (i = 0; i < len; i++) {
		if ((i % 16) == 0)
			printf("\n%03d:", i);
		printf(" %02x", ((const u_int8_t *)p)[i]);
	}
	printf("\n");
}

static void
cmpfail(const void *gen, size_t genlen, const void *ref, size_t reflen)
{
	int i;

	for (i = 0; i < genlen; i++)
		if (((const u_int8_t *)gen)[i] != ((const u_int8_t *)ref)[i]) {
			printf("first difference at byte %u\n", i);
			break;
		}
	dumpdata("Generated", gen, genlen);
	dumpdata("Reference", ref, reflen);
}

static void
printtest(const struct ciphertest *t)
{
	printf("keyix %u pn %llu key_len %u plaintext_len %u\n"
		, t->keyix
		, t->pn
		, t->key_len
		, t->plaintext_len
	);
}

static int
runtest(struct ieee80211vap *vap, struct ciphertest *t)
{
	struct ieee80211_key *key = &vap->iv_nw_keys[t->keyix];
	struct mbuf *m = NULL;
	const struct ieee80211_cipher *cip;
	int hdrlen;

	printf("%s: ", t->name);

	/*
	 * Setup key.
	 */
	memset(key, 0, sizeof(*key));
	key->wk_flags = IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV;
	key->wk_cipher = &ieee80211_cipher_none;
	if (!ieee80211_crypto_newkey(vap, t->cipher,
	    IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV, key)) {
		printf("FAIL: ieee80211_crypto_newkey failed\n");
		goto bad;
	}

	memcpy(key->wk_key, t->key, t->key_len);
	key->wk_keylen = t->key_len;
	memset(key->wk_keyrsc, 0, sizeof(key->wk_keyrsc));
	key->wk_keytsc = t->pn-1;	/* PN-1 since we do encap */
	if (!ieee80211_crypto_setkey(vap, key)) {
		printf("FAIL: ieee80211_crypto_setkey failed\n");
		goto bad;
	}

	/*
	 * Craft frame from plaintext data.
	 */
	cip = key->wk_cipher;
	m = m_getcl(M_NOWAIT, MT_HEADER, M_PKTHDR);
	m->m_data += cip->ic_header;
	memcpy(mtod(m, void *), t->plaintext, t->plaintext_len);
	m->m_len = t->plaintext_len;
	m->m_pkthdr.len = m->m_len;
	hdrlen = ieee80211_anyhdrsize(mtod(m, void *));

	/*
	 * Encrypt frame w/ MIC.
	 */
	if (!cip->ic_encap(key, m)) {
		printtest(t);
		printf("FAIL: ccmp encap failed\n");
		goto bad;
	}
	/*
	 * Verify: frame length, frame contents.
	 */
	if (m->m_pkthdr.len != t->encrypted_len) {
		printf("FAIL: encap data length mismatch\n");
		printtest(t);
		cmpfail(mtod(m, const void *), m->m_pkthdr.len,
			t->encrypted, t->encrypted_len);
		goto bad;
	} else if (memcmp(mtod(m, const void *), t->encrypted, t->encrypted_len)) {
		printf("FAIL: encrypt data does not compare\n");
		printtest(t);
		cmpfail(mtod(m, const void *), m->m_pkthdr.len,
			t->encrypted, t->encrypted_len);
		dumpdata("Plaintext", t->plaintext, t->plaintext_len);
		goto bad;
	}

	/*
	 * Decrypt frame; strip MIC.
	 */
	if (!cip->ic_decap(key, m, hdrlen)) {
		printf("FAIL: ccmp decap failed\n");
		printtest(t);
		cmpfail(mtod(m, const void *), m->m_len,
			t->plaintext, t->plaintext_len);
		goto bad;
	}
	/*
	 * Verify: frame length, frame contents.
	 */
	if (m->m_pkthdr.len != t->plaintext_len) {
		printf("FAIL: decap botch; length mismatch\n");
		printtest(t);
		cmpfail(mtod(m, const void *), m->m_pkthdr.len,
			t->plaintext, t->plaintext_len);
		goto bad;
	} else if (memcmp(mtod(m, const void *), t->plaintext, t->plaintext_len)) {
		printf("FAIL: decap botch; data does not compare\n");
		printtest(t);
		cmpfail(mtod(m, const void *), m->m_pkthdr.len,
			t->plaintext, t->plaintext_len);
		goto bad;
	}
	m_freem(m);
	ieee80211_crypto_delkey(vap, key);
	printf("PASS\n");
	return 1;
bad:
	if (m != NULL)
		m_freem(m);
	ieee80211_crypto_delkey(vap, key);
	return 0;
}

/*
 * Module glue.
 */

static	int tests = -1;
static	int debug = 0;

static int
init_crypto_ccmp_test(void)
{
	struct ieee80211com ic;
	struct ieee80211vap vap;
	struct ifnet ifp;
	int i, pass, total;

	memset(&ic, 0, sizeof(ic));
	memset(&vap, 0, sizeof(vap));
	memset(&ifp, 0, sizeof(ifp));

	ieee80211_crypto_attach(&ic);

	/* some minimal initialization */
	strncpy(ifp.if_xname, "test_ccmp", sizeof(ifp.if_xname));
	vap.iv_ic = &ic;
	vap.iv_ifp = &ifp;
	if (debug)
		vap.iv_debug = IEEE80211_MSG_CRYPTO;
	ieee80211_crypto_vattach(&vap);

	pass = 0;
	total = 0;
	for (i = 0; i < nitems(ccmptests); i++)
		if (tests & (1<<i)) {
			total++;
			pass += runtest(&vap, &ccmptests[i]);
		}
	printf("%u of %u 802.11i AES-CCMP test vectors passed\n", pass, total);

	ieee80211_crypto_vdetach(&vap);
	ieee80211_crypto_detach(&ic);

	return (pass == total ? 0 : -1);
}

static int
test_ccmp_modevent(module_t mod, int type, void *unused)
{
	switch (type) {
	case MOD_LOAD:
		(void) init_crypto_ccmp_test();
		return 0;
	case MOD_UNLOAD:
		return 0;
	}
	return EINVAL;
}

static moduledata_t test_ccmp_mod = {
	"test_ccmp",
	test_ccmp_modevent,
	0
};
DECLARE_MODULE(test_ccmp, test_ccmp_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(test_ccmp, 1);
MODULE_DEPEND(test_ccmp, wlan, 1, 1, 1);
