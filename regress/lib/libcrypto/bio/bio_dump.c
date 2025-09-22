/*	$OpenBSD: bio_dump.c,v 1.5 2025/05/18 06:41:51 tb Exp $ */
/*
 * Copyright (c) 2024 Theo Buehler <tb@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <string.h>

#include <openssl/bio.h>

const uint8_t dump[] = {
	0x74, 0x45, 0xc6, 0x20, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x36, 0xd8, 0x61, 0x48, 0x68, 0x3c, 0xc0, 0x68,
	0xaa, 0x15, 0x57, 0x77, 0xe3, 0xec, 0xb4, 0x98,
	0xc6, 0x08, 0xfc, 0x59, 0xb3, 0x4f, 0x45, 0xcf,
	0x4b, 0xc2, 0xae, 0x98, 0xb5, 0xeb, 0xe0, 0xb5,
	0xc1, 0x68, 0xba, 0xcf, 0x7c, 0xf7, 0x7b, 0x38,
	0x43, 0x2f, 0xb9, 0x0e, 0x23, 0x02, 0xb9, 0x4f,
	0x8c, 0x26, 0xeb, 0xef, 0x70, 0x98, 0x82, 0xa7,
	0xb9, 0x78, 0xc5, 0x08, 0x96, 0x99, 0xb3, 0x84,
	0xa3, 0x4f, 0xfb, 0xd7, 0x38, 0xa9, 0xd9, 0xd4,
	0x53, 0x0f, 0x4f, 0x64, 0x97, 0xdf, 0xcf, 0xf3,
	0x4f, 0xc8, 0xd2, 0x56, 0x3f, 0x0d, 0x72, 0xd4,
	0x55, 0x98, 0x89, 0xb0, 0x45, 0x26, 0x3f, 0x7a,
	0xbd, 0x9d, 0x96, 0x15, 0xa2, 0x10, 0x14, 0x85,
	0xaa, 0xa1, 0x7c, 0x84, 0xfb, 0xc4, 0xa5, 0x7b,
	0xc6, 0xe3, 0xad, 0x85, 0x57, 0x96, 0xbb, 0x81,
	0x18, 0x0c, 0xed, 0x2f, 0xf7, 0x6a, 0x4c, 0x4d,
	0x59, 0xe1, 0xcc, 0xc5, 0x3a, 0x9f, 0x48, 0xfc,
	0x1d, 0x7c, 0x0d, 0xa4, 0x79, 0x96, 0xe7, 0x2b,
	0x39, 0x15, 0xf9, 0x3a, 0x6a, 0x5e, 0x7c, 0x4e,
	0xc9, 0x3b, 0xaf, 0xeb, 0x3b, 0xcf, 0x8d, 0x6a,
	0x57, 0xe6, 0xc5, 0xba, 0xbd, 0xa6, 0xa0, 0x6b,
	0x03, 0xd5, 0xa3, 0x9f, 0x99, 0x2a, 0xea, 0x88,
	0x72, 0x1b, 0x66, 0x6c, 0x5e, 0x1d, 0x49, 0xd5,
	0x1e, 0x1e, 0xcc, 0x1a, 0xb1, 0xd8, 0xf7, 0x91,
	0x1e, 0x1e, 0xcc, 0x1a, 0x20, 0x00, 0x20, 0x00,
	0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00,
	0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00,
	0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00,
	0x20, 0x00, 0x20, 0x00, 0x20, 0x00,
};
#define DUMP_LEN (sizeof(dump) / sizeof(dump[0]))

const uint8_t bytes[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
	0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
	0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
	0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
	0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
	0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
	0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
	0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
	0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
	0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
	0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
	0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
	0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};
#define BYTES_LEN (sizeof(bytes) / sizeof(bytes[0]))

static const struct bio_dump_testcase {
	int indent;
	const char *input;
	int inlen;
	const char *output;
} bio_dump_testcases[] = {
	{
		.indent = 0,
		.input = "",
		.inlen = 0,
		.output = "",
	},
	{
		.indent = 0,
		.input = "",
		.inlen = 1,
		.output = "0001 - <SPACES/NULS>\n",
	},
	{
		.indent = 6,
		.input = " ",
		.inlen = 1,
		.output = "      0001 - <SPACES/NULS>\n",
	},
	{
		.indent = -1,
		.input = "!",
		.inlen = 1,
		.output =
"0000 - 21                                                !\n",
	},
	{
		.indent = -1,
		.input = "~",
		.inlen = 1,
		.output =
"0000 - 7e                                                ~\n",
	},
	{
		.indent = 4,
		.input = dump,
		.inlen = DUMP_LEN,
		.output =
"    0000 - 74 45 c6 20 00 00 00 00-00 00 00 00 00 00 00 00   tE. ............\n"
"    0010 - 36 d8 61 48 68 3c c0 68-aa 15 57 77 e3 ec b4 98   6.aHh<.h..Ww....\n"
"    0020 - c6 08 fc 59 b3 4f 45 cf-4b c2 ae 98 b5 eb e0 b5   ...Y.OE.K.......\n"
"    0030 - c1 68 ba cf 7c f7 7b 38-43 2f b9 0e 23 02 b9 4f   .h..|.{8C/..#..O\n"
"    0040 - 8c 26 eb ef 70 98 82 a7-b9 78 c5 08 96 99 b3 84   .&..p....x......\n"
"    0050 - a3 4f fb d7 38 a9 d9 d4-53 0f 4f 64 97 df cf f3   .O..8...S.Od....\n"
"    0060 - 4f c8 d2 56 3f 0d 72 d4-55 98 89 b0 45 26 3f 7a   O..V?.r.U...E&?z\n"
"    0070 - bd 9d 96 15 a2 10 14 85-aa a1 7c 84 fb c4 a5 7b   ..........|....{\n"
"    0080 - c6 e3 ad 85 57 96 bb 81-18 0c ed 2f f7 6a 4c 4d   ....W....../.jLM\n"
"    0090 - 59 e1 cc c5 3a 9f 48 fc-1d 7c 0d a4 79 96 e7 2b   Y...:.H..|..y..+\n"
"    00a0 - 39 15 f9 3a 6a 5e 7c 4e-c9 3b af eb 3b cf 8d 6a   9..:j^|N.;..;..j\n"
"    00b0 - 57 e6 c5 ba bd a6 a0 6b-03 d5 a3 9f 99 2a ea 88   W......k.....*..\n"
"    00c0 - 72 1b 66 6c 5e 1d 49 d5-1e 1e cc 1a b1 d8 f7 91   r.fl^.I.........\n"
"    00d0 - 1e 1e cc 1a                                       ....\n"
"    00f6 - <SPACES/NULS>\n",
	},
	{
		.indent = 11,
		.input = dump,
		.inlen = DUMP_LEN,
		.output =
"           0000 - 74 45 c6 20 00 00 00 00-00 00 00 00 00 00   tE. ..........\n"
"           000e - 00 00 36 d8 61 48 68 3c-c0 68 aa 15 57 77   ..6.aHh<.h..Ww\n"
"           001c - e3 ec b4 98 c6 08 fc 59-b3 4f 45 cf 4b c2   .......Y.OE.K.\n"
"           002a - ae 98 b5 eb e0 b5 c1 68-ba cf 7c f7 7b 38   .......h..|.{8\n"
"           0038 - 43 2f b9 0e 23 02 b9 4f-8c 26 eb ef 70 98   C/..#..O.&..p.\n"
"           0046 - 82 a7 b9 78 c5 08 96 99-b3 84 a3 4f fb d7   ...x.......O..\n"
"           0054 - 38 a9 d9 d4 53 0f 4f 64-97 df cf f3 4f c8   8...S.Od....O.\n"
"           0062 - d2 56 3f 0d 72 d4 55 98-89 b0 45 26 3f 7a   .V?.r.U...E&?z\n"
"           0070 - bd 9d 96 15 a2 10 14 85-aa a1 7c 84 fb c4   ..........|...\n"
"           007e - a5 7b c6 e3 ad 85 57 96-bb 81 18 0c ed 2f   .{....W....../\n"
"           008c - f7 6a 4c 4d 59 e1 cc c5-3a 9f 48 fc 1d 7c   .jLMY...:.H..|\n"
"           009a - 0d a4 79 96 e7 2b 39 15-f9 3a 6a 5e 7c 4e   ..y..+9..:j^|N\n"
"           00a8 - c9 3b af eb 3b cf 8d 6a-57 e6 c5 ba bd a6   .;..;..jW.....\n"
"           00b6 - a0 6b 03 d5 a3 9f 99 2a-ea 88 72 1b 66 6c   .k.....*..r.fl\n"
"           00c4 - 5e 1d 49 d5 1e 1e cc 1a-b1 d8 f7 91 1e 1e   ^.I...........\n"
"           00d2 - cc 1a                                       ..\n"
"           00f6 - <SPACES/NULS>\n",
	},
	{
		.indent = 18,
		.input = dump,
		.inlen = DUMP_LEN,
		.output =
"                  0000 - 74 45 c6 20 00 00 00 00-00 00 00 00 00   tE. .........\n"
"                  000d - 00 00 00 36 d8 61 48 68-3c c0 68 aa 15   ...6.aHh<.h..\n"
"                  001a - 57 77 e3 ec b4 98 c6 08-fc 59 b3 4f 45   Ww.......Y.OE\n"
"                  0027 - cf 4b c2 ae 98 b5 eb e0-b5 c1 68 ba cf   .K........h..\n"
"                  0034 - 7c f7 7b 38 43 2f b9 0e-23 02 b9 4f 8c   |.{8C/..#..O.\n"
"                  0041 - 26 eb ef 70 98 82 a7 b9-78 c5 08 96 99   &..p....x....\n"
"                  004e - b3 84 a3 4f fb d7 38 a9-d9 d4 53 0f 4f   ...O..8...S.O\n"
"                  005b - 64 97 df cf f3 4f c8 d2-56 3f 0d 72 d4   d....O..V?.r.\n"
"                  0068 - 55 98 89 b0 45 26 3f 7a-bd 9d 96 15 a2   U...E&?z.....\n"
"                  0075 - 10 14 85 aa a1 7c 84 fb-c4 a5 7b c6 e3   .....|....{..\n"
"                  0082 - ad 85 57 96 bb 81 18 0c-ed 2f f7 6a 4c   ..W....../.jL\n"
"                  008f - 4d 59 e1 cc c5 3a 9f 48-fc 1d 7c 0d a4   MY...:.H..|..\n"
"                  009c - 79 96 e7 2b 39 15 f9 3a-6a 5e 7c 4e c9   y..+9..:j^|N.\n"
"                  00a9 - 3b af eb 3b cf 8d 6a 57-e6 c5 ba bd a6   ;..;..jW.....\n"
"                  00b6 - a0 6b 03 d5 a3 9f 99 2a-ea 88 72 1b 66   .k.....*..r.f\n"
"                  00c3 - 6c 5e 1d 49 d5 1e 1e cc-1a b1 d8 f7 91   l^.I.........\n"
"                  00d0 - 1e 1e cc 1a                              ....\n"
"                  00f6 - <SPACES/NULS>\n",
	},
	{
		.indent = 25,
		.input = dump,
		.inlen = DUMP_LEN,
		.output =
"                         0000 - 74 45 c6 20 00 00 00 00-00 00 00   tE. .......\n"
"                         000b - 00 00 00 00 00 36 d8 61-48 68 3c   .....6.aHh<\n"
"                         0016 - c0 68 aa 15 57 77 e3 ec-b4 98 c6   .h..Ww.....\n"
"                         0021 - 08 fc 59 b3 4f 45 cf 4b-c2 ae 98   ..Y.OE.K...\n"
"                         002c - b5 eb e0 b5 c1 68 ba cf-7c f7 7b   .....h..|.{\n"
"                         0037 - 38 43 2f b9 0e 23 02 b9-4f 8c 26   8C/..#..O.&\n"
"                         0042 - eb ef 70 98 82 a7 b9 78-c5 08 96   ..p....x...\n"
"                         004d - 99 b3 84 a3 4f fb d7 38-a9 d9 d4   ....O..8...\n"
"                         0058 - 53 0f 4f 64 97 df cf f3-4f c8 d2   S.Od....O..\n"
"                         0063 - 56 3f 0d 72 d4 55 98 89-b0 45 26   V?.r.U...E&\n"
"                         006e - 3f 7a bd 9d 96 15 a2 10-14 85 aa   ?z.........\n"
"                         0079 - a1 7c 84 fb c4 a5 7b c6-e3 ad 85   .|....{....\n"
"                         0084 - 57 96 bb 81 18 0c ed 2f-f7 6a 4c   W....../.jL\n"
"                         008f - 4d 59 e1 cc c5 3a 9f 48-fc 1d 7c   MY...:.H..|\n"
"                         009a - 0d a4 79 96 e7 2b 39 15-f9 3a 6a   ..y..+9..:j\n"
"                         00a5 - 5e 7c 4e c9 3b af eb 3b-cf 8d 6a   ^|N.;..;..j\n"
"                         00b0 - 57 e6 c5 ba bd a6 a0 6b-03 d5 a3   W......k...\n"
"                         00bb - 9f 99 2a ea 88 72 1b 66-6c 5e 1d   ..*..r.fl^.\n"
"                         00c6 - 49 d5 1e 1e cc 1a b1 d8-f7 91 1e   I..........\n"
"                         00d1 - 1e cc 1a                           ...\n"
"                         00f6 - <SPACES/NULS>\n",
	},
	{
		.indent = 32,
		.input = dump,
		.inlen = DUMP_LEN,
		.output =
"                                0000 - 74 45 c6 20 00 00 00 00-00   tE. .....\n"
"                                0009 - 00 00 00 00 00 00 00 36-d8   .......6.\n"
"                                0012 - 61 48 68 3c c0 68 aa 15-57   aHh<.h..W\n"
"                                001b - 77 e3 ec b4 98 c6 08 fc-59   w.......Y\n"
"                                0024 - b3 4f 45 cf 4b c2 ae 98-b5   .OE.K....\n"
"                                002d - eb e0 b5 c1 68 ba cf 7c-f7   ....h..|.\n"
"                                0036 - 7b 38 43 2f b9 0e 23 02-b9   {8C/..#..\n"
"                                003f - 4f 8c 26 eb ef 70 98 82-a7   O.&..p...\n"
"                                0048 - b9 78 c5 08 96 99 b3 84-a3   .x.......\n"
"                                0051 - 4f fb d7 38 a9 d9 d4 53-0f   O..8...S.\n"
"                                005a - 4f 64 97 df cf f3 4f c8-d2   Od....O..\n"
"                                0063 - 56 3f 0d 72 d4 55 98 89-b0   V?.r.U...\n"
"                                006c - 45 26 3f 7a bd 9d 96 15-a2   E&?z.....\n"
"                                0075 - 10 14 85 aa a1 7c 84 fb-c4   .....|...\n"
"                                007e - a5 7b c6 e3 ad 85 57 96-bb   .{....W..\n"
"                                0087 - 81 18 0c ed 2f f7 6a 4c-4d   ..../.jLM\n"
"                                0090 - 59 e1 cc c5 3a 9f 48 fc-1d   Y...:.H..\n"
"                                0099 - 7c 0d a4 79 96 e7 2b 39-15   |..y..+9.\n"
"                                00a2 - f9 3a 6a 5e 7c 4e c9 3b-af   .:j^|N.;.\n"
"                                00ab - eb 3b cf 8d 6a 57 e6 c5-ba   .;..jW...\n"
"                                00b4 - bd a6 a0 6b 03 d5 a3 9f-99   ...k.....\n"
"                                00bd - 2a ea 88 72 1b 66 6c 5e-1d   *..r.fl^.\n"
"                                00c6 - 49 d5 1e 1e cc 1a b1 d8-f7   I........\n"
"                                00cf - 91 1e 1e cc 1a               .....\n"
"                                00f6 - <SPACES/NULS>\n",
	},
	{
		.indent = 35,
		.input = dump,
		.inlen = DUMP_LEN,
		.output =
"                                   0000 - 74 45 c6 20 00 00 00 00-  tE. ....\n"
"                                   0008 - 00 00 00 00 00 00 00 00-  ........\n"
"                                   0010 - 36 d8 61 48 68 3c c0 68-  6.aHh<.h\n"
"                                   0018 - aa 15 57 77 e3 ec b4 98-  ..Ww....\n"
"                                   0020 - c6 08 fc 59 b3 4f 45 cf-  ...Y.OE.\n"
"                                   0028 - 4b c2 ae 98 b5 eb e0 b5-  K.......\n"
"                                   0030 - c1 68 ba cf 7c f7 7b 38-  .h..|.{8\n"
"                                   0038 - 43 2f b9 0e 23 02 b9 4f-  C/..#..O\n"
"                                   0040 - 8c 26 eb ef 70 98 82 a7-  .&..p...\n"
"                                   0048 - b9 78 c5 08 96 99 b3 84-  .x......\n"
"                                   0050 - a3 4f fb d7 38 a9 d9 d4-  .O..8...\n"
"                                   0058 - 53 0f 4f 64 97 df cf f3-  S.Od....\n"
"                                   0060 - 4f c8 d2 56 3f 0d 72 d4-  O..V?.r.\n"
"                                   0068 - 55 98 89 b0 45 26 3f 7a-  U...E&?z\n"
"                                   0070 - bd 9d 96 15 a2 10 14 85-  ........\n"
"                                   0078 - aa a1 7c 84 fb c4 a5 7b-  ..|....{\n"
"                                   0080 - c6 e3 ad 85 57 96 bb 81-  ....W...\n"
"                                   0088 - 18 0c ed 2f f7 6a 4c 4d-  .../.jLM\n"
"                                   0090 - 59 e1 cc c5 3a 9f 48 fc-  Y...:.H.\n"
"                                   0098 - 1d 7c 0d a4 79 96 e7 2b-  .|..y..+\n"
"                                   00a0 - 39 15 f9 3a 6a 5e 7c 4e-  9..:j^|N\n"
"                                   00a8 - c9 3b af eb 3b cf 8d 6a-  .;..;..j\n"
"                                   00b0 - 57 e6 c5 ba bd a6 a0 6b-  W......k\n"
"                                   00b8 - 03 d5 a3 9f 99 2a ea 88-  .....*..\n"
"                                   00c0 - 72 1b 66 6c 5e 1d 49 d5-  r.fl^.I.\n"
"                                   00c8 - 1e 1e cc 1a b1 d8 f7 91-  ........\n"
"                                   00d0 - 1e 1e cc 1a               ....\n"
"                                   00f6 - <SPACES/NULS>\n",
	},
	{
		.indent = 39,
		.input = dump,
		.inlen = DUMP_LEN,
		.output =
"                                       0000 - 74 45 c6 20 00 00 00   tE. ...\n"
"                                       0007 - 00 00 00 00 00 00 00   .......\n"
"                                       000e - 00 00 36 d8 61 48 68   ..6.aHh\n"
"                                       0015 - 3c c0 68 aa 15 57 77   <.h..Ww\n"
"                                       001c - e3 ec b4 98 c6 08 fc   .......\n"
"                                       0023 - 59 b3 4f 45 cf 4b c2   Y.OE.K.\n"
"                                       002a - ae 98 b5 eb e0 b5 c1   .......\n"
"                                       0031 - 68 ba cf 7c f7 7b 38   h..|.{8\n"
"                                       0038 - 43 2f b9 0e 23 02 b9   C/..#..\n"
"                                       003f - 4f 8c 26 eb ef 70 98   O.&..p.\n"
"                                       0046 - 82 a7 b9 78 c5 08 96   ...x...\n"
"                                       004d - 99 b3 84 a3 4f fb d7   ....O..\n"
"                                       0054 - 38 a9 d9 d4 53 0f 4f   8...S.O\n"
"                                       005b - 64 97 df cf f3 4f c8   d....O.\n"
"                                       0062 - d2 56 3f 0d 72 d4 55   .V?.r.U\n"
"                                       0069 - 98 89 b0 45 26 3f 7a   ...E&?z\n"
"                                       0070 - bd 9d 96 15 a2 10 14   .......\n"
"                                       0077 - 85 aa a1 7c 84 fb c4   ...|...\n"
"                                       007e - a5 7b c6 e3 ad 85 57   .{....W\n"
"                                       0085 - 96 bb 81 18 0c ed 2f   ....../\n"
"                                       008c - f7 6a 4c 4d 59 e1 cc   .jLMY..\n"
"                                       0093 - c5 3a 9f 48 fc 1d 7c   .:.H..|\n"
"                                       009a - 0d a4 79 96 e7 2b 39   ..y..+9\n"
"                                       00a1 - 15 f9 3a 6a 5e 7c 4e   ..:j^|N\n"
"                                       00a8 - c9 3b af eb 3b cf 8d   .;..;..\n"
"                                       00af - 6a 57 e6 c5 ba bd a6   jW.....\n"
"                                       00b6 - a0 6b 03 d5 a3 9f 99   .k.....\n"
"                                       00bd - 2a ea 88 72 1b 66 6c   *..r.fl\n"
"                                       00c4 - 5e 1d 49 d5 1e 1e cc   ^.I....\n"
"                                       00cb - 1a b1 d8 f7 91 1e 1e   .......\n"
"                                       00d2 - cc 1a                  ..\n"
"                                       00f6 - <SPACES/NULS>\n",
	},
	{
		.indent = 46,
		.input = dump,
		.inlen = DUMP_LEN,
		.output =
"                                              0000 - 74 45 c6 20 00 00   tE. ..\n"
"                                              0006 - 00 00 00 00 00 00   ......\n"
"                                              000c - 00 00 00 00 36 d8   ....6.\n"
"                                              0012 - 61 48 68 3c c0 68   aHh<.h\n"
"                                              0018 - aa 15 57 77 e3 ec   ..Ww..\n"
"                                              001e - b4 98 c6 08 fc 59   .....Y\n"
"                                              0024 - b3 4f 45 cf 4b c2   .OE.K.\n"
"                                              002a - ae 98 b5 eb e0 b5   ......\n"
"                                              0030 - c1 68 ba cf 7c f7   .h..|.\n"
"                                              0036 - 7b 38 43 2f b9 0e   {8C/..\n"
"                                              003c - 23 02 b9 4f 8c 26   #..O.&\n"
"                                              0042 - eb ef 70 98 82 a7   ..p...\n"
"                                              0048 - b9 78 c5 08 96 99   .x....\n"
"                                              004e - b3 84 a3 4f fb d7   ...O..\n"
"                                              0054 - 38 a9 d9 d4 53 0f   8...S.\n"
"                                              005a - 4f 64 97 df cf f3   Od....\n"
"                                              0060 - 4f c8 d2 56 3f 0d   O..V?.\n"
"                                              0066 - 72 d4 55 98 89 b0   r.U...\n"
"                                              006c - 45 26 3f 7a bd 9d   E&?z..\n"
"                                              0072 - 96 15 a2 10 14 85   ......\n"
"                                              0078 - aa a1 7c 84 fb c4   ..|...\n"
"                                              007e - a5 7b c6 e3 ad 85   .{....\n"
"                                              0084 - 57 96 bb 81 18 0c   W.....\n"
"                                              008a - ed 2f f7 6a 4c 4d   ./.jLM\n"
"                                              0090 - 59 e1 cc c5 3a 9f   Y...:.\n"
"                                              0096 - 48 fc 1d 7c 0d a4   H..|..\n"
"                                              009c - 79 96 e7 2b 39 15   y..+9.\n"
"                                              00a2 - f9 3a 6a 5e 7c 4e   .:j^|N\n"
"                                              00a8 - c9 3b af eb 3b cf   .;..;.\n"
"                                              00ae - 8d 6a 57 e6 c5 ba   .jW...\n"
"                                              00b4 - bd a6 a0 6b 03 d5   ...k..\n"
"                                              00ba - a3 9f 99 2a ea 88   ...*..\n"
"                                              00c0 - 72 1b 66 6c 5e 1d   r.fl^.\n"
"                                              00c6 - 49 d5 1e 1e cc 1a   I.....\n"
"                                              00cc - b1 d8 f7 91 1e 1e   ......\n"
"                                              00d2 - cc 1a               ..\n"
"                                              00f6 - <SPACES/NULS>\n",
	},
	{
		.indent = 53,
		.input = dump,
		.inlen = DUMP_LEN,
		.output =
"                                                     0000 - 74 45 c6 20   tE. \n"
"                                                     0004 - 00 00 00 00   ....\n"
"                                                     0008 - 00 00 00 00   ....\n"
"                                                     000c - 00 00 00 00   ....\n"
"                                                     0010 - 36 d8 61 48   6.aH\n"
"                                                     0014 - 68 3c c0 68   h<.h\n"
"                                                     0018 - aa 15 57 77   ..Ww\n"
"                                                     001c - e3 ec b4 98   ....\n"
"                                                     0020 - c6 08 fc 59   ...Y\n"
"                                                     0024 - b3 4f 45 cf   .OE.\n"
"                                                     0028 - 4b c2 ae 98   K...\n"
"                                                     002c - b5 eb e0 b5   ....\n"
"                                                     0030 - c1 68 ba cf   .h..\n"
"                                                     0034 - 7c f7 7b 38   |.{8\n"
"                                                     0038 - 43 2f b9 0e   C/..\n"
"                                                     003c - 23 02 b9 4f   #..O\n"
"                                                     0040 - 8c 26 eb ef   .&..\n"
"                                                     0044 - 70 98 82 a7   p...\n"
"                                                     0048 - b9 78 c5 08   .x..\n"
"                                                     004c - 96 99 b3 84   ....\n"
"                                                     0050 - a3 4f fb d7   .O..\n"
"                                                     0054 - 38 a9 d9 d4   8...\n"
"                                                     0058 - 53 0f 4f 64   S.Od\n"
"                                                     005c - 97 df cf f3   ....\n"
"                                                     0060 - 4f c8 d2 56   O..V\n"
"                                                     0064 - 3f 0d 72 d4   ?.r.\n"
"                                                     0068 - 55 98 89 b0   U...\n"
"                                                     006c - 45 26 3f 7a   E&?z\n"
"                                                     0070 - bd 9d 96 15   ....\n"
"                                                     0074 - a2 10 14 85   ....\n"
"                                                     0078 - aa a1 7c 84   ..|.\n"
"                                                     007c - fb c4 a5 7b   ...{\n"
"                                                     0080 - c6 e3 ad 85   ....\n"
"                                                     0084 - 57 96 bb 81   W...\n"
"                                                     0088 - 18 0c ed 2f   .../\n"
"                                                     008c - f7 6a 4c 4d   .jLM\n"
"                                                     0090 - 59 e1 cc c5   Y...\n"
"                                                     0094 - 3a 9f 48 fc   :.H.\n"
"                                                     0098 - 1d 7c 0d a4   .|..\n"
"                                                     009c - 79 96 e7 2b   y..+\n"
"                                                     00a0 - 39 15 f9 3a   9..:\n"
"                                                     00a4 - 6a 5e 7c 4e   j^|N\n"
"                                                     00a8 - c9 3b af eb   .;..\n"
"                                                     00ac - 3b cf 8d 6a   ;..j\n"
"                                                     00b0 - 57 e6 c5 ba   W...\n"
"                                                     00b4 - bd a6 a0 6b   ...k\n"
"                                                     00b8 - 03 d5 a3 9f   ....\n"
"                                                     00bc - 99 2a ea 88   .*..\n"
"                                                     00c0 - 72 1b 66 6c   r.fl\n"
"                                                     00c4 - 5e 1d 49 d5   ^.I.\n"
"                                                     00c8 - 1e 1e cc 1a   ....\n"
"                                                     00cc - b1 d8 f7 91   ....\n"
"                                                     00d0 - 1e 1e cc 1a   ....\n"
"                                                     00f6 - <SPACES/NULS>\n",
	},
	{
		.indent = 60,
		.input = dump,
		.inlen = DUMP_LEN,
		.output =
"                                                            0000 - 74 45   tE\n"
"                                                            0002 - c6 20   . \n"
"                                                            0004 - 00 00   ..\n"
"                                                            0006 - 00 00   ..\n"
"                                                            0008 - 00 00   ..\n"
"                                                            000a - 00 00   ..\n"
"                                                            000c - 00 00   ..\n"
"                                                            000e - 00 00   ..\n"
"                                                            0010 - 36 d8   6.\n"
"                                                            0012 - 61 48   aH\n"
"                                                            0014 - 68 3c   h<\n"
"                                                            0016 - c0 68   .h\n"
"                                                            0018 - aa 15   ..\n"
"                                                            001a - 57 77   Ww\n"
"                                                            001c - e3 ec   ..\n"
"                                                            001e - b4 98   ..\n"
"                                                            0020 - c6 08   ..\n"
"                                                            0022 - fc 59   .Y\n"
"                                                            0024 - b3 4f   .O\n"
"                                                            0026 - 45 cf   E.\n"
"                                                            0028 - 4b c2   K.\n"
"                                                            002a - ae 98   ..\n"
"                                                            002c - b5 eb   ..\n"
"                                                            002e - e0 b5   ..\n"
"                                                            0030 - c1 68   .h\n"
"                                                            0032 - ba cf   ..\n"
"                                                            0034 - 7c f7   |.\n"
"                                                            0036 - 7b 38   {8\n"
"                                                            0038 - 43 2f   C/\n"
"                                                            003a - b9 0e   ..\n"
"                                                            003c - 23 02   #.\n"
"                                                            003e - b9 4f   .O\n"
"                                                            0040 - 8c 26   .&\n"
"                                                            0042 - eb ef   ..\n"
"                                                            0044 - 70 98   p.\n"
"                                                            0046 - 82 a7   ..\n"
"                                                            0048 - b9 78   .x\n"
"                                                            004a - c5 08   ..\n"
"                                                            004c - 96 99   ..\n"
"                                                            004e - b3 84   ..\n"
"                                                            0050 - a3 4f   .O\n"
"                                                            0052 - fb d7   ..\n"
"                                                            0054 - 38 a9   8.\n"
"                                                            0056 - d9 d4   ..\n"
"                                                            0058 - 53 0f   S.\n"
"                                                            005a - 4f 64   Od\n"
"                                                            005c - 97 df   ..\n"
"                                                            005e - cf f3   ..\n"
"                                                            0060 - 4f c8   O.\n"
"                                                            0062 - d2 56   .V\n"
"                                                            0064 - 3f 0d   ?.\n"
"                                                            0066 - 72 d4   r.\n"
"                                                            0068 - 55 98   U.\n"
"                                                            006a - 89 b0   ..\n"
"                                                            006c - 45 26   E&\n"
"                                                            006e - 3f 7a   ?z\n"
"                                                            0070 - bd 9d   ..\n"
"                                                            0072 - 96 15   ..\n"
"                                                            0074 - a2 10   ..\n"
"                                                            0076 - 14 85   ..\n"
"                                                            0078 - aa a1   ..\n"
"                                                            007a - 7c 84   |.\n"
"                                                            007c - fb c4   ..\n"
"                                                            007e - a5 7b   .{\n"
"                                                            0080 - c6 e3   ..\n"
"                                                            0082 - ad 85   ..\n"
"                                                            0084 - 57 96   W.\n"
"                                                            0086 - bb 81   ..\n"
"                                                            0088 - 18 0c   ..\n"
"                                                            008a - ed 2f   ./\n"
"                                                            008c - f7 6a   .j\n"
"                                                            008e - 4c 4d   LM\n"
"                                                            0090 - 59 e1   Y.\n"
"                                                            0092 - cc c5   ..\n"
"                                                            0094 - 3a 9f   :.\n"
"                                                            0096 - 48 fc   H.\n"
"                                                            0098 - 1d 7c   .|\n"
"                                                            009a - 0d a4   ..\n"
"                                                            009c - 79 96   y.\n"
"                                                            009e - e7 2b   .+\n"
"                                                            00a0 - 39 15   9.\n"
"                                                            00a2 - f9 3a   .:\n"
"                                                            00a4 - 6a 5e   j^\n"
"                                                            00a6 - 7c 4e   |N\n"
"                                                            00a8 - c9 3b   .;\n"
"                                                            00aa - af eb   ..\n"
"                                                            00ac - 3b cf   ;.\n"
"                                                            00ae - 8d 6a   .j\n"
"                                                            00b0 - 57 e6   W.\n"
"                                                            00b2 - c5 ba   ..\n"
"                                                            00b4 - bd a6   ..\n"
"                                                            00b6 - a0 6b   .k\n"
"                                                            00b8 - 03 d5   ..\n"
"                                                            00ba - a3 9f   ..\n"
"                                                            00bc - 99 2a   .*\n"
"                                                            00be - ea 88   ..\n"
"                                                            00c0 - 72 1b   r.\n"
"                                                            00c2 - 66 6c   fl\n"
"                                                            00c4 - 5e 1d   ^.\n"
"                                                            00c6 - 49 d5   I.\n"
"                                                            00c8 - 1e 1e   ..\n"
"                                                            00ca - cc 1a   ..\n"
"                                                            00cc - b1 d8   ..\n"
"                                                            00ce - f7 91   ..\n"
"                                                            00d0 - 1e 1e   ..\n"
"                                                            00d2 - cc 1a   ..\n"
"                                                            00f6 - <SPACES/NULS>\n",
	},
	{
		.indent = 67,
		.input = dump,
		.inlen = DUMP_LEN,
		.output =
"                                                                0000 - 74   t\n"
"                                                                0001 - 45   E\n"
"                                                                0002 - c6   .\n"
"                                                                0003 - 20    \n"
"                                                                0004 - 00   .\n"
"                                                                0005 - 00   .\n"
"                                                                0006 - 00   .\n"
"                                                                0007 - 00   .\n"
"                                                                0008 - 00   .\n"
"                                                                0009 - 00   .\n"
"                                                                000a - 00   .\n"
"                                                                000b - 00   .\n"
"                                                                000c - 00   .\n"
"                                                                000d - 00   .\n"
"                                                                000e - 00   .\n"
"                                                                000f - 00   .\n"
"                                                                0010 - 36   6\n"
"                                                                0011 - d8   .\n"
"                                                                0012 - 61   a\n"
"                                                                0013 - 48   H\n"
"                                                                0014 - 68   h\n"
"                                                                0015 - 3c   <\n"
"                                                                0016 - c0   .\n"
"                                                                0017 - 68   h\n"
"                                                                0018 - aa   .\n"
"                                                                0019 - 15   .\n"
"                                                                001a - 57   W\n"
"                                                                001b - 77   w\n"
"                                                                001c - e3   .\n"
"                                                                001d - ec   .\n"
"                                                                001e - b4   .\n"
"                                                                001f - 98   .\n"
"                                                                0020 - c6   .\n"
"                                                                0021 - 08   .\n"
"                                                                0022 - fc   .\n"
"                                                                0023 - 59   Y\n"
"                                                                0024 - b3   .\n"
"                                                                0025 - 4f   O\n"
"                                                                0026 - 45   E\n"
"                                                                0027 - cf   .\n"
"                                                                0028 - 4b   K\n"
"                                                                0029 - c2   .\n"
"                                                                002a - ae   .\n"
"                                                                002b - 98   .\n"
"                                                                002c - b5   .\n"
"                                                                002d - eb   .\n"
"                                                                002e - e0   .\n"
"                                                                002f - b5   .\n"
"                                                                0030 - c1   .\n"
"                                                                0031 - 68   h\n"
"                                                                0032 - ba   .\n"
"                                                                0033 - cf   .\n"
"                                                                0034 - 7c   |\n"
"                                                                0035 - f7   .\n"
"                                                                0036 - 7b   {\n"
"                                                                0037 - 38   8\n"
"                                                                0038 - 43   C\n"
"                                                                0039 - 2f   /\n"
"                                                                003a - b9   .\n"
"                                                                003b - 0e   .\n"
"                                                                003c - 23   #\n"
"                                                                003d - 02   .\n"
"                                                                003e - b9   .\n"
"                                                                003f - 4f   O\n"
"                                                                0040 - 8c   .\n"
"                                                                0041 - 26   &\n"
"                                                                0042 - eb   .\n"
"                                                                0043 - ef   .\n"
"                                                                0044 - 70   p\n"
"                                                                0045 - 98   .\n"
"                                                                0046 - 82   .\n"
"                                                                0047 - a7   .\n"
"                                                                0048 - b9   .\n"
"                                                                0049 - 78   x\n"
"                                                                004a - c5   .\n"
"                                                                004b - 08   .\n"
"                                                                004c - 96   .\n"
"                                                                004d - 99   .\n"
"                                                                004e - b3   .\n"
"                                                                004f - 84   .\n"
"                                                                0050 - a3   .\n"
"                                                                0051 - 4f   O\n"
"                                                                0052 - fb   .\n"
"                                                                0053 - d7   .\n"
"                                                                0054 - 38   8\n"
"                                                                0055 - a9   .\n"
"                                                                0056 - d9   .\n"
"                                                                0057 - d4   .\n"
"                                                                0058 - 53   S\n"
"                                                                0059 - 0f   .\n"
"                                                                005a - 4f   O\n"
"                                                                005b - 64   d\n"
"                                                                005c - 97   .\n"
"                                                                005d - df   .\n"
"                                                                005e - cf   .\n"
"                                                                005f - f3   .\n"
"                                                                0060 - 4f   O\n"
"                                                                0061 - c8   .\n"
"                                                                0062 - d2   .\n"
"                                                                0063 - 56   V\n"
"                                                                0064 - 3f   ?\n"
"                                                                0065 - 0d   .\n"
"                                                                0066 - 72   r\n"
"                                                                0067 - d4   .\n"
"                                                                0068 - 55   U\n"
"                                                                0069 - 98   .\n"
"                                                                006a - 89   .\n"
"                                                                006b - b0   .\n"
"                                                                006c - 45   E\n"
"                                                                006d - 26   &\n"
"                                                                006e - 3f   ?\n"
"                                                                006f - 7a   z\n"
"                                                                0070 - bd   .\n"
"                                                                0071 - 9d   .\n"
"                                                                0072 - 96   .\n"
"                                                                0073 - 15   .\n"
"                                                                0074 - a2   .\n"
"                                                                0075 - 10   .\n"
"                                                                0076 - 14   .\n"
"                                                                0077 - 85   .\n"
"                                                                0078 - aa   .\n"
"                                                                0079 - a1   .\n"
"                                                                007a - 7c   |\n"
"                                                                007b - 84   .\n"
"                                                                007c - fb   .\n"
"                                                                007d - c4   .\n"
"                                                                007e - a5   .\n"
"                                                                007f - 7b   {\n"
"                                                                0080 - c6   .\n"
"                                                                0081 - e3   .\n"
"                                                                0082 - ad   .\n"
"                                                                0083 - 85   .\n"
"                                                                0084 - 57   W\n"
"                                                                0085 - 96   .\n"
"                                                                0086 - bb   .\n"
"                                                                0087 - 81   .\n"
"                                                                0088 - 18   .\n"
"                                                                0089 - 0c   .\n"
"                                                                008a - ed   .\n"
"                                                                008b - 2f   /\n"
"                                                                008c - f7   .\n"
"                                                                008d - 6a   j\n"
"                                                                008e - 4c   L\n"
"                                                                008f - 4d   M\n"
"                                                                0090 - 59   Y\n"
"                                                                0091 - e1   .\n"
"                                                                0092 - cc   .\n"
"                                                                0093 - c5   .\n"
"                                                                0094 - 3a   :\n"
"                                                                0095 - 9f   .\n"
"                                                                0096 - 48   H\n"
"                                                                0097 - fc   .\n"
"                                                                0098 - 1d   .\n"
"                                                                0099 - 7c   |\n"
"                                                                009a - 0d   .\n"
"                                                                009b - a4   .\n"
"                                                                009c - 79   y\n"
"                                                                009d - 96   .\n"
"                                                                009e - e7   .\n"
"                                                                009f - 2b   +\n"
"                                                                00a0 - 39   9\n"
"                                                                00a1 - 15   .\n"
"                                                                00a2 - f9   .\n"
"                                                                00a3 - 3a   :\n"
"                                                                00a4 - 6a   j\n"
"                                                                00a5 - 5e   ^\n"
"                                                                00a6 - 7c   |\n"
"                                                                00a7 - 4e   N\n"
"                                                                00a8 - c9   .\n"
"                                                                00a9 - 3b   ;\n"
"                                                                00aa - af   .\n"
"                                                                00ab - eb   .\n"
"                                                                00ac - 3b   ;\n"
"                                                                00ad - cf   .\n"
"                                                                00ae - 8d   .\n"
"                                                                00af - 6a   j\n"
"                                                                00b0 - 57   W\n"
"                                                                00b1 - e6   .\n"
"                                                                00b2 - c5   .\n"
"                                                                00b3 - ba   .\n"
"                                                                00b4 - bd   .\n"
"                                                                00b5 - a6   .\n"
"                                                                00b6 - a0   .\n"
"                                                                00b7 - 6b   k\n"
"                                                                00b8 - 03   .\n"
"                                                                00b9 - d5   .\n"
"                                                                00ba - a3   .\n"
"                                                                00bb - 9f   .\n"
"                                                                00bc - 99   .\n"
"                                                                00bd - 2a   *\n"
"                                                                00be - ea   .\n"
"                                                                00bf - 88   .\n"
"                                                                00c0 - 72   r\n"
"                                                                00c1 - 1b   .\n"
"                                                                00c2 - 66   f\n"
"                                                                00c3 - 6c   l\n"
"                                                                00c4 - 5e   ^\n"
"                                                                00c5 - 1d   .\n"
"                                                                00c6 - 49   I\n"
"                                                                00c7 - d5   .\n"
"                                                                00c8 - 1e   .\n"
"                                                                00c9 - 1e   .\n"
"                                                                00ca - cc   .\n"
"                                                                00cb - 1a   .\n"
"                                                                00cc - b1   .\n"
"                                                                00cd - d8   .\n"
"                                                                00ce - f7   .\n"
"                                                                00cf - 91   .\n"
"                                                                00d0 - 1e   .\n"
"                                                                00d1 - 1e   .\n"
"                                                                00d2 - cc   .\n"
"                                                                00d3 - 1a   .\n"
"                                                                00f6 - <SPACES/NULS>\n",
	},
	{
		.indent = 4,
		.input = bytes,
		.inlen = BYTES_LEN,
		.output =
"    0000 - 00 01 02 03 04 05 06 07-08 09 0a 0b 0c 0d 0e 0f   ................\n"
"    0010 - 10 11 12 13 14 15 16 17-18 19 1a 1b 1c 1d 1e 1f   ................\n"
"    0020 - 20 21 22 23 24 25 26 27-28 29 2a 2b 2c 2d 2e 2f    !\"#$%&'()*+,-./\n"
"    0030 - 30 31 32 33 34 35 36 37-38 39 3a 3b 3c 3d 3e 3f   0123456789:;<=>?\n"
"    0040 - 40 41 42 43 44 45 46 47-48 49 4a 4b 4c 4d 4e 4f   @ABCDEFGHIJKLMNO\n"
"    0050 - 50 51 52 53 54 55 56 57-58 59 5a 5b 5c 5d 5e 5f   PQRSTUVWXYZ[\\]^_\n"
"    0060 - 60 61 62 63 64 65 66 67-68 69 6a 6b 6c 6d 6e 6f   `abcdefghijklmno\n"
"    0070 - 70 71 72 73 74 75 76 77-78 79 7a 7b 7c 7d 7e 7f   pqrstuvwxyz{|}~.\n"
"    0080 - 80 81 82 83 84 85 86 87-88 89 8a 8b 8c 8d 8e 8f   ................\n"
"    0090 - 90 91 92 93 94 95 96 97-98 99 9a 9b 9c 9d 9e 9f   ................\n"
"    00a0 - a0 a1 a2 a3 a4 a5 a6 a7-a8 a9 aa ab ac ad ae af   ................\n"
"    00b0 - b0 b1 b2 b3 b4 b5 b6 b7-b8 b9 ba bb bc bd be bf   ................\n"
"    00c0 - c0 c1 c2 c3 c4 c5 c6 c7-c8 c9 ca cb cc cd ce cf   ................\n"
"    00d0 - d0 d1 d2 d3 d4 d5 d6 d7-d8 d9 da db dc dd de df   ................\n"
"    00e0 - e0 e1 e2 e3 e4 e5 e6 e7-e8 e9 ea eb ec ed ee ef   ................\n"
"    00f0 - f0 f1 f2 f3 f4 f5 f6 f7-f8 f9 fa fb fc fd fe ff   ................\n",
	},
};

#define N_TESTS (sizeof(bio_dump_testcases) / sizeof(bio_dump_testcases[0]))

static int
bio_dump_test(const struct bio_dump_testcase *tc)
{
	BIO *bio;
	char *got;
	long got_len;
	int ret;
	int failed = 1;

	if ((bio = BIO_new(BIO_s_mem())) == NULL)
		errx(1, "BIO_new");

	if ((ret = BIO_dump_indent(bio, tc->input, tc->inlen, tc->indent)) == -1)
		errx(1, "BIO_dump_indent");
	if ((got_len = BIO_get_mem_data(bio, &got)) < 0)
		errx(1, "BIO_get_mem_data");
	if (ret != got_len || strlen(tc->output) != (size_t)ret) {
		fprintf(stderr, "indent %d: ret %d, got_len %ld, strlen %zu\n",
		    tc->indent, ret, got_len, strlen(tc->output));
		goto err;
	}
	if (got_len > 0 && strncmp(tc->output, got, got_len) != 0) {
		fprintf(stderr, "%d: mismatch\n", tc->indent);
		goto err;
	}

	failed = 0;

 err:
	BIO_free(bio);

	return failed;
}

int
main(void)
{
	size_t i;
	int failed = 0;

	for (i = 0; i < N_TESTS; i++)
		failed |= bio_dump_test(&bio_dump_testcases[i]);

	return failed;
}
