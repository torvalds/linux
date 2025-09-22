/*	$OpenBSD: bn_gcd.c,v 1.3 2023/04/07 17:09:54 tb Exp $ */

/*
 * Copyright (c) 2023 Theo Buehler <tb@openbsd.org>
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
#include <stdio.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/err.h>

int BN_gcd(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, BN_CTX *ctx);
int BN_gcd_ct(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, BN_CTX *ctx);

static const struct gcd_test_fn {
	const char *name;
	int (*fn)(BIGNUM *, const BIGNUM *, const BIGNUM *, BN_CTX *);
	int fails_on_zero;
} gcd_fn[] = {
	{
		.name = "BN_gcd",
		.fn = BN_gcd,
	},
	{
		.name = "BN_gcd_ct",
		.fn = BN_gcd_ct,
		.fails_on_zero = 1,
	},
};

#define N_GCD_FN (sizeof(gcd_fn) / sizeof(gcd_fn[0]))

static const struct gcd_test {
	const char *a;
	const char *b;
	const char *r;
} bn_gcd_tests[] = {
	{
		.a = "0",
		.b = "0",
		.r = "0",
	},
	{
		.a = "1",
		.b = "1",
		.r = "1",
	},
	{
		.a = "1",
		.b = "0",
		.r = "1",
	},
	{
		.a = "0",
		.b = "1",
		.r = "1",
	},
	{
		.a = "57",
		.b = "0",
		.r = "57",
	},
	{
		.a = "0",
		.b = "57",
		.r = "57",
	},

	/*
	 * The following test cases were randomly generated.
	 */

	{
		.a = "255",
		.b = "278d3",
		.r = "3",
	},
	{
		.a = "6a54d",
		.b = "619",
		.r = "7",
	},
	{
		.a = "e9",
		.b = "e695",
		.r = "1",
	},
	{
		.a = "3f3a9",
		.b = "41f",
		.r = "5",
	},
	{
		.a = "643",
		.b = "5bff1",
		.r = "7",
	},
	{
		.a = "2bb",
		.b = "29be3",
		.r = "3",
	},
	{
		.a = "49e",
		.b = "5770e",
		.r = "6",
	},
	{
		.a = "f1d5",
		.b = "fb",
		.r = "1",
	},
	{
		.a = "250eb50",
		.b = "206b0",
		.r = "2b0",
	},
	{
		.a = "57ad2",
		.b = "64927d6",
		.r = "6a6",
	},
	{
		.a = "430bc",
		.b = "48757a4",
		.r = "564",
	},
	{
		.a = "7593a8c",
		.b = "7161c",
		.r = "7ec",
	},
	{
		.a = "5771161",
		.b = "53d9b",
		.r = "5e9",
	},
	{
		.a = "4132d82",
		.b = "37b1e",
		.r = "49e",
	},
	{
		.a = "72492b0",
		.b = "65f90",
		.r = "730",
	},
	{
		.a = "14bab",
		.b = "163605b",
		.r = "1af",
	},
	{
		.a = "602d952bd",
		.b = "5a08833",
		.r = "62ebb",
	},
	{
		.a = "698bf2b",
		.b = "65849195f",
		.r = "701db",
	},
	{
		.a = "37d4a5f",
		.b = "402dcce6f",
		.r = "488d3",
	},
	{
		.a = "614e922",
		.b = "6644a8e72",
		.r = "6cc7a",
	},
	{
		.a = "1eea0a75c",
		.b = "1ea5c64",
		.r = "21ac4",
	},
	{
		.a = "6136d54",
		.b = "6acfd5d8c",
		.r = "7e544",
	},
	{
		.a = "2bba7aaf2",
		.b = "33894f6",
		.r = "34902",
	},
	{
		.a = "467c1e94",
		.b = "3c036c",
		.r = "4d34",
	},
	{
		.a = "19c3fcfc5",
		.b = "15f969b26a7",
		.r = "1b9928b",
	},
	{
		.a = "145666d9a",
		.b = "13bbb7f3bb6",
		.r = "159a73a",
	},
	{
		.a = "1b0c38f9e",
		.b = "18765759b3e",
		.r = "1f0ce22",
	},
	{
		.a = "784064e87",
		.b = "667b4b1bc85",
		.r = "7fbc6f7",
	},
	{
		.a = "498054afdd3",
		.b = "44d269073",
		.r = "596efd7",
	},
	{
		.a = "198ba337af6",
		.b = "1f5755eca",
		.r = "214ab6a",
	},
	{
		.a = "5effbed2b",
		.b = "6a7cdb539bd",
		.r = "7b7352f",
	},
	{
		.a = "14388cf1265",
		.b = "153338971",
		.r = "174af49",
	},
	{
		.a = "2d4aff46122f30851a52b",
		.b = "2edb8fdd14ab1b645",
		.r = "332805d24fd69",
	},
	{
		.a = "43733ea897495e14339f9",
		.b = "3e843370070058d8d",
		.r = "49977f36263b5",
	},
	{
		.a = "58aafd7662b8de019",
		.b = "5538bdda74849754e4949",
		.r = "5daa7b4439b0f",
	},
	{
		.a = "474100872e962de8b0596",
		.b = "45223dbbeaaba9e92",
		.r = "580218df81b62",
	},
	{
		.a = "307c55bb52be32b4d",
		.b = "2cc525ce8e2d2bc67aa6d",
		.r = "30c17810923d9",
	},
	{
		.a = "4a2a6504adf43e733",
		.b = "497c44b84fb6f8774f787",
		.r = "5ea902856aae1",
	},
	{
		.a = "5b8e523f7c7b60972",
		.b = "4aebe8c99000a8bd30652",
		.r = "63b8fd8af9062",
	},
	{
		.a = "53a047f3c3f81986811d7",
		.b = "53b004a9fa740fcbb",
		.r = "64d12a952833b",
	},
	{
		.a = "6f177c61fa6afe2d209",
		.b = "670a0a35553c35d52fb18f5",
		.r = "74c624a8a32333b",
	},
	{
		.a = "5343fd2374414df3034",
		.b = "507c3e327a45f92c78eeb84",
		.r = "5974af2eca1fe94",
	},
	{
		.a = "4a07f6267329d80c7d4bdfa",
		.b = "56a012dd8379b7a0e16",
		.r = "59052131189608e",
	},
	{
		.a = "63bba164fd337e80d0c",
		.b = "615a76fb66b1094efbcba2c",
		.r = "735585f95d70274",
	},
	{
		.a = "2453ea8b7fca0284b883078",
		.b = "234099d522bcad7e248",
		.r = "2d69bd4f4b8d088",
	},
	{
		.a = "61b5dc7806b84d2b08c",
		.b = "58b8dde9e5f8ecfde43cd74",
		.r = "75ff164a01eaa7c",
	},
	{
		.a = "3816984948c1e79f882",
		.b = "3b5b01106809f6bb2e2e016",
		.r = "48170e737316bca",
	},
	{
		.a = "41b4deb320e45f566b",
		.b = "4874dbcc44f53775a6a1d7",
		.r = "4977a5459131df",
	},
	{
		.a = "364580a30652080c07c9dc48",
		.b = "2e1c73c5131cbbf50628",
		.r = "36825feeccb49b58",
	},
	{
		.a = "695d74ad760a5795570a3",
		.b = "62bc190742c20aa29644c84eb",
		.r = "6abcf0da95d5be6f7",
	},
	{
		.a = "74db3db1ab6c202baff804bcd",
		.b = "6380a7b03658ba56b34a3",
		.r = "756f230199f1529ab",
	},
	{
		.a = "4b26c75874a09700b9d512ed8",
		.b = "4ca3361909906b0cf7618",
		.r = "53055ca37fd154698",
	},
	{
		.a = "1f81faea97f44cd5c6adb457d",
		.b = "22c452983db5a6ce9736f",
		.r = "241a04dc3e4719c37",
	},
	{
		.a = "95e5584090df556539aa9fd9",
		.b = "b02f19eec1f1f2d44c59",
		.r = "c5c889564b7e8bab",
	},
	{
		.a = "4a908939e089498a7feab",
		.b = "60045b573dccb74a9e2e379d5",
		.r = "6116326ef394c59e7",
	},
	{
		.a = "39443c6a7d6bd95529a12",
		.b = "409e977e693f5bd1b17b0433a",
		.r = "49802f618e94d218a",
	},
	{
		.a = "3e6fc6cf9eaba3e94f24f03",
		.b = "345aa9ded17c65a6c85dd7e6d61",
		.r = "3ea3e52d4d5d024c389",
	},
	{
		.a = "64f62375ce39484ec7e85f4e3d",
		.b = "69459ea484d6480a376e91",
		.r = "748b7824736c5c39e5",
	},
	{
		.a = "4186cead307cad31530dd205a39",
		.b = "4f5ee69f4afbb46507ccc8b",
		.r = "533aef3d540410a6b2b",
	},
	{
		.a = "536b816e2daa705e44bae82",
		.b = "4e8a14632ccec82f41941b1fd7e",
		.r = "5bf87a677ef2ec1bd76",
	},
	{
		.a = "6518022e9b5920c69de925",
		.b = "6d90bcb3174b93f44a2854f601",
		.r = "7cc47ec91e90d61599",
	},
	{
		.a = "2f443ac559d746b4b8f87c802af",
		.b = "2ef35b22f59380207cd3a09",
		.r = "33f82dc6aa168711c2f",
	},
	{
		.a = "17418a08c177e039b1e5000",
		.b = "183bf7b0342fe483457340ab000",
		.r = "1da412c88f3a0c3f000",
	},
	{
		.a = "77685efaa0d4e6a25268eaa07",
		.b = "65b89e487f7a272f6187b",
		.r = "80861458bce7097f7",
	},
	{
		.a = "1488a0aab5415ee1ce826ecb38f7b33",
		.b = "131c43cca73843a27b9feac46cd03d5de7a2f",
		.r = "17b3a3001f3660377791cd8cf",
	},
	{
		.a = "5349d29af82e6b8fae6c25859fadec6207032",
		.b = "64d277bfb253b9a22f3c0187445253a",
		.r = "6573b739e39b0650a9c2c2f02",
	},
	{
		.a = "21df6046ab805b9f71a304d34236fe8",
		.b = "1fe085864e8e75ea073efebfe6fbab133e4e8",
		.r = "23e3d1525042d237154d245f8",
	},
	{
		.a = "56454c66089738f7074ce55236dce53b22a11",
		.b = "5d5308342f5a0c2277762ba37273f07",
		.r = "71fb0beacaae6adab73b8cfbb",
	},
	{
		.a = "25e20fcd81385c099f1db654feba9b1",
		.b = "2134ea8527e2ad47f460d70b918ba69fd4c6b",
		.r = "268d03475633df8adccd7eda9",
	},
	{
		.a = "6713eb612dab2940e2741150dfa203f632d5f",
		.b = "611184b794056ec03b0aeb7c1646be9",
		.r = "752472cea4fe17a0cc5881d23",
	},
	{
		.a = "39a3fe02e4f8cb7cfc972275862512c6e863c",
		.b = "356731db8ae0f405b01423d24a00df4",
		.r = "4230cc0ff4cb762463e8f45a4",
	},
	{
		.a = "48c66d2a5a9f354fbe6a24379b3c868",
		.b = "3d52dfc550d9288335bc4ca727d0008e225d8",
		.r = "4e38e92332937fe6d87b9e888",
	},
	{
		.a = "30de4f96ec186d1f64298c5ae4510f448",
		.b = "30cbcdb08d24818e2845286f4b70760116f96e8",
		.r = "32f5f8996bc47879755ea626348",
	},
	{
		.a = "4e3cbdc0d866680fd0cf79732b914033c",
		.b = "4c0fc242dbe2f4ba5592ffc621eca68499e807c",
		.r = "556fcff7e03458aba9b3229def4",
	},
	{
		.a = "4f902e3c221d879daae7536c66d0d1e0a",
		.b = "4e2451f07d9559f3c36746cef62cb2ce95525de",
		.r = "5be6e06842ff90445f2d002fdce",
	},
	{
		.a = "1ec50fe3ba8edcdd47cecf6b1e67d2263bee334",
		.b = "20eb4fa3aabacd28aa5126fa29425be14",
		.r = "244d6242059f13924023015b8bc",
	},
	{
		.a = "f85a29486c90172f2fcb0e8f2b9cc357235f8",
		.b = "d988a3b4b5b168ed7ebe15381da5818",
		.r = "fdfee673f1bde9585970c65b8",
	},
	{
		.a = "60ff4616bccb386ab1c8c4fd663afd554f8d8cc",
		.b = "5e4e6ea5e8217e17f69c63ea07d8e93bc",
		.r = "780275e6eaa0fe4bf87a78c9974",
	},
	{
		.a = "1ba4b71fa06394748ad36489d8a1232de12d6f0",
		.b = "1da078788fa13e18814edbf15907ef930",
		.r = "235f9d5de3c2a7b93122d930ad0",
	},
	{
		.a = "3b09f70af16cc8c40573bfffb63750b2c",
		.b = "3d4c9928047325884f90888dee2d64f64dca29c",
		.r = "3fc1e8665f4df4243c2062b6d0c",
	},
	{
		.a = "4d3079fb7639d1e7fdab98015c21c782c4c2b7878",
		.b = "3dfed750b07e2be9e32d2627c31a0a1e3c8",
		.r = "500cd222944614585265eb9f9e958",
	},
	{
		.a = "536e6614ee85be4cba997bceeacd5d87db",
		.b = "48a13d2d2df5788977b9e8ecd434cba45fe3ee1b",
		.r = "549a3e0100b99d244e7f85384439",
	},
	{
		.a = "1d83beadd7fe8d557ee9985f3d4c47b9144",
		.b = "22be84a3090cce29b0b666a7cb049f9ba808cb494",
		.r = "25d4b9fb71a6f79ff05eeabfbd454",
	},
	{
		.a = "5b5bc6dc131ab9f1f765c976c96852f5cb024d121",
		.b = "4c8375dd9a13a1c7d31f2c3fc87dd070075",
		.r = "5ec3c5e03869f4dd4372d274fb553",
	},
	{
		.a = "220220e66a4abb06681151d2a9cdf0d30a0",
		.b = "26002cab182d2500064e1c19184707b9e8ba673e0",
		.r = "2633ad7265ae0db8b891f4459eee0",
	},
	{
		.a = "1e2ff1ff6bd8bee4b18ff6fc09e2b4a82e6",
		.b = "207e14815ccc0a2f5cf3903d7e2bf754356c65676",
		.r = "239e193c1bc65766c6b580dcddcc2",
	},
	{
		.a = "26b7793830e334ac155cc8d480f2f3a6de4",
		.b = "27a0ce62b6c85cd1566cdc532b5b1e322e52996cc",
		.r = "297defac305c3a2f2154541ef1dc4",
	},
	{
		.a = "50fda952177aa819c4042847414f2d2d14e",
		.b = "45255a9ae2fd394ed8b760ce3133fb71f94362a86",
		.r = "56aae2037820936b47d6a9cf771fa",
	},
	{
		.a = "5acedf307d7016ffd7380015df422691ebbf7d97e91",
		.b = "53c6266c3e6520cc40abc70370ad94e8a629d",
		.r = "686ec3910fdac81085a738d1d4799d3",
	},
	{
		.a = "584f15ec6c7e7bfa4ba24e1780fde1511fe12",
		.b = "575a78c8e8bb4f02e57efb0010d86b4a408f150fe7e",
		.r = "6317a26e7ae3218b60d8ef6cfa64186",
	},
	{
		.a = "f617d44caf7dc67791e20feac2c1c27e655f",
		.b = "11542c315141d0c46bae8863c1a945963b91b644603",
		.r = "12f20af110dee02b9bbde9c9431ef47",
	},
	{
		.a = "54538d3df7d85581e923e44291cae3aa3a3fcd20cc5",
		.b = "67eef9400e79570ce246df916892707014bbb",
		.r = "6a00b7b006c767a710fdd8b7652cacd",
	},
	{
		.a = "1d567e0e5af1b4994bd82baf9c601d188107d",
		.b = "1dae559a89e7e21212345a8148689c33da8d28f3fc9",
		.r = "1f41fa99ba8e144971e741732e76cc1",
	},
	{
		.a = "2c3172df78361b326b2c1f83749d1b9e42672",
		.b = "2ff4be223d91f31e03e07fee0c18e082f970191a41a",
		.r = "3693dd9f37613c4a7de1d831f44dd26",
	},
	{
		.a = "32e11a127828647a88f8f7b6d48f475e8e71eda585b",
		.b = "2ddf9f7e0f870347401ce5d93ff70a5a3a9cb",
		.r = "3a4156da31ca81617974669400e3629",
	},
	{
		.a = "47f54df57b2d124be53c0c6603700e8f69829238b49",
		.b = "489167e7ed18db6d541c8a94cf6a58f10836d",
		.r = "5ca417bb8cb42fefd91fd832a89bdb7",
	},
	{
		.a = "8963a8cf862bfeda36412dea185c6984c3acf3b22204",
		.b = "8035daf441d2a728b3dbded074c57e103e58ad65efaf5fe3"
		     "b23c",
		.r = "a7cbf319f110484e28a631c8425609ed72ec",
	},
	{
		.a = "40f583721493abb5ead342942d9e142ce51f7251ecb6a333"
		     "c0888",
		.b = "4cce4f3aff6019192f7e32c372c9e32385b2352d32518",
		.r = "52d94814b2149c815d82619dc02750648e558",
	},
	{
		.a = "34a08b5c152b6bda536ab231199c5de7d8e96ad8814f3",
		.b = "328d23516a0bacc130bcbb9b95965ffdd99827c49f306a6c"
		     "06983",
		.r = "431ef899d79eb5aff9b6aa3759bca625f1317",
	},
	{
		.a = "4765f462b4167b25df009c3d0d710d707dc4e8a990e81",
		.b = "5495c974f06f6f2cf98d285af77a76be3ef32eab77ca815b"
		     "a2031",
		.r = "58ab78d483cc486014fb6ea972c22f7ab7b5d",
	},
	{
		.a = "44ad54c2afc1aeac7ec3511d847cbbee8902ec2ede784",
		.b = "3cf4ddd41867fea4681efd043e079ba7788bcf66dd2694ba"
		     "dce1c",
		.r = "495c740575ba1a56128a3a6721dc6b26fa664",
	},
	{
		.a = "1fa1582b89ffb94b0b97acdb1de271dbfdbc305af4513fdf"
		     "5b307",
		.b = "27f5a44e5e13c9b5261f50a2f19f00e6a5023c0c755e5",
		.r = "27f8d558c92c865c814efc3aa6a1f4d70bf73",
	},
	{
		.a = "591a92aedb904d763b651b2db15c965368d909b219ee607a"
		     "5c68c",
		.b = "5b4be0a485cd99ee2d1126e62f657581da85e941e725c",
		.r = "68117134ecfe0984ea313bfe818d2072186dc",
	},
	{
		.a = "23d0f4377e7c81b3531b876a2c277c5c60ec5db222e02",
		.b = "28b7dd0cdd3dbf3d6f92099865fdda7b659264979e55a77a"
		     "7d9a2",
		.r = "2e942c88caa87225c5a8498d87fa59dc870fa",
	},
	{
		.a = "44c61f3dbac478676dcbec9e9b85a1fce1822640a45c7830"
		     "7cca326",
		.b = "4f6d3f6085a1937280761490ea12e7bfa776274e9b22dda",
		.r = "58ca1394a549af73f5c95c86891f65fe0f02ce6",
	},
	{
		.a = "4db4b7f75bc45038ca28ac52e2f39fd546890e7b6566753f"
		     "65750f8",
		.b = "5340542372ef34ac534d473cb2e0a5ffde3dae86c609c68",
		.r = "65ac57454f6de8aa076e8754b9766f088c01598",
	},
	{
		.a = "5d975374b2babbc978bd336e1c3f219f1e288f2e1db9945",
		.b = "4fc6275ab900f3b867af5ac9107a6605e4b7092613ae76ca"
		     "0caff79",
		.r = "607a26e662f6a1c9bd222c7f34dab5814576bcf",
	},
	{
		.a = "7809cd380713c9d12f3f5700bbc69abc9a4508c400d3b6a",
		.b = "7a6d99dcad253fafd6b85854588a608eb8983a6ef7f1f2cd"
		     "8f181a2",
		.r = "7f5c9339cbbeffe9bae9e1fbcaac2b9ca73c89e",
	},
	{
		.a = "3d9e9943572b9a1f79ad2957b7c244a596d0ac6f1de51efc"
		     "9137e16",
		.b = "426bbbff4bdbff38522f35096168f0edd6eb8827bdfcd92",
		.r = "4aded736b377b0e75db171dbbfefdfae8d76c4a",
	},
	{
		.a = "37f44546e8b6837ded1179af6539491608d495d2b95d940",
		.b = "39acb184673736b04fdc295ec18d4873c9e87be5291a7de0"
		     "66146c0",
		.r = "48cd7455d2c1cfa8df085c3bbcda3db9aae91c0",
	},
	{
		.a = "44e3fb32ef2f76b49a2f237268b130a67a4d33fcf6b5526",
		.b = "45346b4482d0967d50c829d9cd3c004ae308b257ba3e424f"
		     "281c142",
		.r = "47ab194984b1e24a53f37d7458b0ab935bf72a6",
	},
	{
		.a = "583ccfde7a58547a5d4865a587f53d5bb14357ae1884ace",
		.b = "672af3971a2220742816400394cf5e51d30c7e84f12c5f99"
		     "fc5ff6a",
		.r = "73691aa5d3db4eaf50cb025db292dbf56c4655e",
	},
	{
		.a = "5313de0a5a6130944f256c41f1f0eb656fd0db717a910d71"
		     "667328c93",
		.b = "523fa7f470e6662d511de7c89b98332246311c00411602c9"
		     "d",
		.r = "63c5b2d400135796ba239bc1897c105a035cf4af1",
	},
	{
		.a = "6261142ad8b400e852a18dcc22ad59e799b49a441877590a"
		     "2b3a9c848",
		.b = "540da3c401662ab7a46a45112216372f40b265bf5f29a41b"
		     "8",
		.r = "6d6843c2bfad5aa2576615cae1dae5d89fa8dd748",
	},
	{
		.a = "608ca9bd26508d03ca0bd1b7298277f8f663f211a8d8f9ef"
		     "3",
		.b = "66a32e807cd5ba8aaa27a26a6557b612410405c533e30db0"
		     "cd181388b",
		.r = "7c082059c2fe5ec79de0cd0fdc6589bf628c34dbf",
	},
	{
		.a = "3c3f129b151cebc5ad541b2f055935f4b6b6d999916278d5"
		     "4f39de38e",
		.b = "399a8dbd7959ceb3c21d040c70edc1b0b6a78979649cc87e"
		     "6",
		.r = "3df890c79455b17a785efc18df7c4036b8d2036ce",
	},
	{
		.a = "a4c9bbe3961f010618e049eee287111d6ac85ed9eec95d91",
		.b = "9b400dddd76cd1e856dc42e0250fa7abd9e1ca6d40406de9"
		     "ef6a4fdb",
		.r = "ba72df9f2f039bf06d4db604f939971305056d95",
	},
	{
		.a = "4c877496518b9c0d55af99d7d06a06257c5a28e10f113b39"
		     "6bc15c3e4",
		.b = "3e6fe477af388c8ac70124319f42bd92ce5544bca3ae90b2"
		     "c",
		.r = "4edc3785759dca43fffb4143a7da85079d538bf5c",
	},
	{
		.a = "1a9606caa24d27f4e8886379ace27a42809d16e826628e5b"
		     "50b5e3672",
		.b = "186dfdc633d6b265fa84e201e361465007ddc0b5ce2cdf6d"
		     "a",
		.r = "1b58867321f49d16b1354c1788a813cc155f90c1e",
	},
	{
		.a = "3280c3a090a37682a1f0726730833cc826c31dc489026926"
		     "5",
		.b = "35ae94985e20a405161cdc4901436dd36397c38fdd4a7e33"
		     "bae99cdef",
		.r = "4192391ee1a855ec8961147e8939fe45cac80d73b",
	},
	{
		.a = "55cd7d3fcb430b18fafd57849074593658619b78c342a80f"
		     "35f",
		.b = "6c002b00fbc036c3fc8e61ed14bf9dc00557be4fcd953c30"
		     "6220f314535",
		.r = "6d6cdb2824bf090412b402523161702b44aca463751",
	},
	{
		.a = "5b7c45727d9663df29ccd97638ea1e5365e69737e1d2999c"
		     "ffe0a67e34a",
		.b = "5b77539fe191d0829ebd7db19748881f4ed579662f8b3788"
		     "02a",
		.r = "6c1d167c73f79f0a90667983c795100c81a582d7aba",
	},
	{
		.a = "6147dc08368c5a0de97a1935be04149805f74804c0717685"
		     "335",
		.b = "7536275bc0c3267b7fdc41743e6419955861fc5fa9bfd466"
		     "0634a4fbf57",
		.r = "7737a65bb5496810a9f619da5a791c8393baf33c5db",
	},
	{
		.a = "27828740d648ee93a505acedb32f024ac4480381a4f5b908"
		     "14ed67a54fe",
		.b = "24e2003c3aa30c231bea1c97822dd073c29c4c530aeb2d5e"
		     "c66",
		.r = "2a4fd1d87bc6758693cb48907b0daaab4ae3730ef9a",
	},
	{
		.a = "3238e873f75c5e83aab4011008be5feca52b86af433a6e72"
		     "68d",
		.b = "2c155346f370ea6bedf6914edfd6f661c6e715fda6407b56"
		     "7548e0e4cb7",
		.r = "3657adc3dc062f48f73ed00287b9d829a0c38eaed3b",
	},
	{
		.a = "639383fe0a40f99614d8e5546e51c541e303f4db310d9c88"
		     "657",
		.b = "530a8a0bf423b328dbffa4d4576e7450ed609c096421f2a9"
		     "32699a2f73b",
		.r = "6c8164cbaebfbfa60301953d5aa2643ba693be30469",
	},
	{
		.a = "6104c4f4458890e2328e437f28d4098b2a095bc350756587"
		     "b2a",
		.b = "624a78cea710d55ed9002ee5ea2b04bf0c20e07cf07af576"
		     "05b0515784a",
		.r = "7496a2ec992703084ce1c0612530bb24fa4fce869ca",
	},
	{
		.a = "21bcfd56d055ce91bf9dfeb28dbd856b8948dc6bcdf4ddad"
		     "c85",
		.b = "21613395d1b095dbc2692e515625af73e204ca223ac29153"
		     "885bd953ee5",
		.r = "24089f6dfee4127877db2b33b3db071fc92031cb779",
	},
	{
		.a = "2da7975f2ad52bd7d58b2d498890293ea0cc7e6e7194733c"
		     "f52ed491620782a1df1ca",
		.b = "378ab3607cfa0980ee987cabdc98b365a2cafedd48860d16"
		     "12357d10b42",
		.r = "37e537c681b6044fd268d6e53cefce7c5de4f6cf7975ab81"
		     "6",
	},
	{
		.a = "2021c9e46190f90e3c10576444aa894f248347eaaeac5b96"
		     "d9796824473db64d9a8f5",
		.b = "1d17ba28d355234b36eb29bd3eb53caddd4e12f6144ca07e"
		     "54524e2a22b",
		.r = "265dd940ee35b5eb0d4ab86f6cd19a4ddef731cdb8fae8b1"
		     "f",
	},
	{
		.a = "260f058e995913d6e981e740f57555e3ee86158ad4d8339f"
		     "06afedcad195ebda92732",
		.b = "2249d97ddb467f6f63a6f76ae328cd8d34c2f3115ec4dd7c"
		     "01accbbe856",
		.r = "2c5aca0890e94d789e13c3137a5c614284461a0081dcbe2c"
		     "e",
	},
	{
		.a = "582298f46a8c2bf26123bcb6754e71100d5c32ab6186078e"
		     "fd5c2490ca829be4bcd53",
		.b = "6b0f201145e607d237aecf363c5758cd75261ddb034c3167"
		     "e3a31b38583",
		.r = "6f31d80d7dcaae28f5160f13bfd6070db69a67bcbf7cf0e3"
		     "f",
	},
	{
		.a = "68d6e63cd61d4ed41693bc9e9bf5c1dfaf8a3dfeb44c8112"
		     "c5005c69ea94e4d5b3f0f",
		.b = "69d2bc47efe097dad533f6b6700e078de4bb56bee98f3535"
		     "c4ba41b3f7d",
		.r = "6e96f508e056eedf13eefe495a5074d9cfd51c970ac0f324"
		     "b",
	},
	{
		.a = "38f65d469da22947dfccb3fcff57132c50903f7d5f421851"
		     "4b4a906fa27",
		.b = "35f6780af8e1935b2f26094ab97c4231cb103a7f0c1d91b5"
		     "2d458ac5e3ff932cf8ead",
		.r = "3dc4f73d3af023d0f5cf52a1db4e0bb77fbf7d197b33c82a"
		     "9",
	},
	{
		.a = "d37bb7a5630dd5ac27a8bbe67a9e572d9adc849cba18d49a"
		     "46229844da5ae4915e68",
		.b = "e1af2e7b007853438ec70464709355c401b6a153d3588c88"
		     "ad34b945b8",
		.r = "10e3d1db198dc19ba1e4fe56f88772f0dd033831e6054c0a"
		     "8",
	},
	{
		.a = "cd763ea0a64c145f02c704855a79c9c3560db84138e5e3a4"
		     "1518be36e2acaff923e",
		.b = "b866a8b952e2560bab904405b5460e7746d86f656994db40"
		     "072946252",
		.r = "d865003bbcff5c843cb5fdeb4f5d0b20393c372fa5f4a4a",
	},
	{
		.a = "192d53a727cd60ffd45f517998642027212ceb1c22cf5e04"
		     "c86a374578e0a7ca0b025c8",
		.b = "160b4e86db9e83d0b52a5419fa1507a5691ea7f53b752778"
		     "b98132567afd8",
		.r = "1d0af6c5acad8c58a45571ee6ecdf76cf93146f4b833fa1d"
		     "958",
	},
	{
		.a = "6e72fcf18123c5fd106660b972858a9f60153e0da651e506"
		     "4d2dedbeb1acb",
		.b = "5afe95c191b714b28933c2c4209d87c8322114863bc7633c"
		     "c54bb3eba70c449ab38d535",
		.r = "779639703fc67cfb5b44cc447cf421864ed9297c67d3253c"
		     "321",
	},
	{
		.a = "2244b5c5cfcb3ed924dd5400e93f3ba9f3b91af3c9951350"
		     "947b78e2e63329e79cea9d4",
		.b = "29351beb4d4d1427e1864b0c0f50a83b7ce96a99e1354fb7"
		     "1b1bd5a5f56fc",
		.r = "293f736bb759cf96ffe993d1318f386e9825a16fef8a0ceb"
		     "3ec",
	},
	{
		.a = "5cbd24cde8f4ecc8e05edaacbfe038f7587ed23304980129"
		     "693ee7ba9ccb2e4c37cfe3c",
		.b = "511f8aa67af7b2fbc64ba4411ce6d5cb9b5d6fc079139d29"
		     "2aa4b3235dff4",
		.r = "5ddb2337c954a1d3f7bf64f47d6c31f31bc4aae902b91165"
		     "9dc",
	},
	{
		.a = "568e21d4ea4742f62550e06340933309c92235f91233624a"
		     "323ad33837e8f4df0cedeb9",
		.b = "478f9166ae59494b600cb56998a6085b28c851229aeb88c3"
		     "43e64b4b5b78b",
		.r = "5c0f58b79e8f8e5cd804cfbabb6e69d19195283d224de10d"
		     "aad",
	},
	{
		.a = "4c3188411f8916bf3bc0ae56202f7fa377f68094ff57e216"
		     "bf90fb61a4cd0",
		.b = "3c1c9e1ab85fe9e59760317a49dd166d5bdabf5b7a17789c"
		     "e81ca4fea487de64c85e630",
		.r = "4e3d0022f0e8936a6c364df09cfdf6114355cc757c5a1cbf"
		     "a50",
	},
	{
		.a = "11666f96a79abce41bdd2cfe3d5645340039176f66bf8f17"
		     "c32f76d4f57a8",
		.b = "1391e654616857ab4bbf5e9bc59e6e95e1bb0466b128cfe4"
		     "fc56f302e9de322382266a8",
		.r = "14e192315ba47c1ec563966fb4203cdce3c2198777757cfe"
		     "858",
	},
	{
		.a = "5dd52c6c3f0a8e3c69a4741c4e9e1ebe42b3491ef4748155"
		     "bf29001790c70aa59d114e7",
		.b = "63e0213b6a9ec52cc3b17dd7bed6e0c5c65d6e309871d223"
		     "c692ca6adbc77",
		.r = "786a2b6037ac9e417750820a5408fd854a1dd7822dfa8281"
		     "5c3",
	},
	{
		.a = "f339707d7232d30823ec944eb57f0c40994db22a6151bfdc"
		     "100838e0e2f15d",
		.b = "e8a967992d66b599802b25c4173b71c092b9b297e4fe6f5e"
		     "b2be799de65ca4eda7b71b97",
		.r = "f76e8c28e7c097b1151a3340e9a6f81da2736868f20106df"
		     "429f",
	},
	{
		.a = "9e768f5cbb3b3d6f7690ee66fc18b71e6e42de3e7ba7ae7d"
		     "1230c7ab75ae1f7035998746",
		.b = "8a64e46ac9bff23d58e79f10772853bb7aadb2258642282b"
		     "a3faeeca2856b2",
		.r = "a10ae3ba4bd787e552a47efb031d917dfe78b8a362d9b3c0"
		     "4236",
	},
	{
		.a = "6676693864c9be0b2d54f2fddb64734d49ea8608eb6c77a4"
		     "c95ce3d87e98a30a793bf731b",
		.b = "70a9d0d8930a216d33cdd447a8f386e76b67133b657389e4"
		     "b94f95dfbf89d17",
		.r = "742a457f57c070b0d76dc3817106351863acb689d3d8ca49"
		     "9e4d5",
	},
	{
		.a = "3d3f2333e7053685777a0a35774020f35f37c5dc10dc2f74"
		     "770fcf181ee58977917bfba79",
		.b = "3c659ed6efa9270b17532a4da41e80e33629597b82666b9c"
		     "5d5f669d551378b",
		.r = "433d6d9daaa8273a36fb2ec6d4c7403e2609b0ade522e44f"
		     "06d5b",
	},
	{
		.a = "26df635cda763dcb206cea154dea57bdbb0c61c207772042"
		     "9a0621f12a037ab1f3fc65104",
		.b = "2301db7a7731c7ac7fe37451ff70e3ae589d7fd1b8e4e6ff"
		     "fe2300f6e8a1574",
		.r = "299457348e1ae419c4d856b7208d9d3002c2179d4d36d1e9"
		     "52f3c",
	},
	{
		.a = "32e720d3ff483e0970b16a2386ae0f6853984e8c800148cf"
		     "e9e00454619c6b75230c1ca31",
		.b = "3abaf52e682307727bcd803f699f5a74df082b8ed85cdf27"
		     "1498b6c144d54c3",
		.r = "3d5c919f138c486f1d41adea3eecedf5e7c638a97dda3a57"
		     "d71d7",
	},
	{
		.a = "581da07e6c7da7915b98e34f3ce2df3d5038df161cbfa9a2"
		     "71d3c58f86dc091",
		.b = "4c9ec9a853b470c8c0dd6ea8eef17135530e5e3b8802efb0"
		     "f8c34488f04253775bf79a29b",
		.r = "588b9cebe85c4a617f1db1e85de3f7bad59ad52b99e9abca"
		     "25227",
	},
	{
		.a = "62970e919336999817be9521a615285a8db12e33160d2132"
		     "9eccb37bc43409d68f93799bb",
		.b = "698bb6088d35165e3606663ad8bbfbc96095874aac7f1a45"
		     "8746b6fcd138091",
		.r = "7e43151d6f44023d51e5fcb458d21118dfdb5493b4add441"
		     "8794f",
	},
	{
		.a = "4a7ecde2877df55b4a71db70a1b48d4b6b9f00148b450bd9"
		     "9bdcd1425dc9ac19e",
		.b = "4e88b372e168f3b35abec63e3c34c6ac51c22878c6747aea"
		     "1605538e96415156e799576fce2",
		.r = "5b47cb37e95f7ae2ee32c61c0368e5bb5902becd43fa1069"
		     "4b98976",
	},
	{
		.a = "1886913566ee6de450c1bb2df7118ae2a7a116327b6e4ead"
		     "bd6f9f7bb244eb0d2",
		.b = "19e0bbc5cb73a10da7ec1249f1d1cecbfc0a003e55c09334"
		     "29293a2c93751c3ea69d8679b56",
		.r = "1cc44e36e74585dcda4a6f53e39cc8b01b47928968e0d2f3"
		     "cf8b342",
	},
	{
		.a = "5f3a2c0b3c745311105cc6f6b2b460c5cc223b6111e0df12"
		     "9fd84b2387218b30b",
		.b = "6ba00655a3417cccc7d0b64daad96efc0a5b5ea1910d280a"
		     "603e6af3337295a0e2315509b09",
		.r = "7a836b272ddac421d45b0a25722dcee984d7cb192ddffa50"
		     "8b85ffd",
	},
	{
		.a = "63aa301030ef2eb49a2566470bccb90e1b0b7e4a7e45b149"
		     "4556eb47044fd13f0",
		.b = "7076c5b843c90cceae07e2e5e8f08167c0f27c6226d88ede"
		     "0ef4a74aac509ffb109c97b76d0",
		.r = "7f1726ce9ca18dde940d0da2bba31fca807fcf187d12d354"
		     "4780970",
	},
	{
		.a = "5151e338d3760bb7716028c045b7022d254d329cf2375b9e"
		     "c2233c6c8e6faf526",
		.b = "56d9d823ecb6a7612e0a8cee927d88080b29dc95435ed411"
		     "0bbffa49f957e63a3586abad5aa",
		.r = "67855cbdd911b1b2c5aa7aaca9a667fdf2fc2a53e71e1fc3"
		     "a50fa46",
	},
	{
		.a = "56f5e7190ab7ae84beba7746a9b27782ca7832904b330ecc"
		     "3954342d1be8acaf1",
		.b = "5d4d9d53bbfd5a82c49c7e7c33dc0bad53dde899642bfe83"
		     "9056fce5260c16b1bfdeb73ba1b",
		.r = "5dce8a6cc7f1100c375137bcd796e2d34f385c7aaa15267a"
		     "f5239c7",
	},
	{
		.a = "4d16c85b71f478b9cab1432b07b80db869d279923ac19638"
		     "b866d137fb5381fa18b6407155f",
		.b = "4ef20f6226388decfc59dcaa0662c74554f170027b344c10"
		     "45af07d25320cca7b",
		.r = "58bec748aea77048b9dfc219223c73ccaa1f32d903ac9acd"
		     "15e984f",
	},
	{
		.a = "60328dc320fdf6262a28a2cb49b8faa6fd025f60e9af3709"
		     "7f6d33e8d63740c9c",
		.b = "5ad7495b98a54265ceb0e36c7bd1b1fbafed5d0d32790166"
		     "1e2b0351f88d563d1380daa3134",
		.r = "6a07a2a77696240c1a4bc6626677a90fbf1210ac15b8523c"
		     "e058f8c",
	},
	{
		.a = "2ada6242bb6cc488ae781765e7946f6c7afa742fa22a10fe"
		     "5096a5e25d4662ca5c80f834f",
		.b = "334c4a3485923bd40e7f476260a668bd2743c79c819f2328"
		     "18edf7e4f50da5769ecbda346329a28223341",
		.r = "33f17f3a6bbcf17c181d8f7c13e74d514c98e3e628f87006"
		     "58cd1696e6207",
	},
	{
		.a = "116bcd8479b8977f3718bde35bd6fdf81812871aa2c888c7"
		     "9c058f423ab7d6385cd96598a",
		.b = "ff4b7acaffca1ecdb159befd40177452837d1596ca45f2ad"
		     "fd94a4d7d24a7d8c10d52331c786fa7010e6",
		.r = "150e9b0d68cf9546371739fcdfbdcfb662cd0910b65e8be9"
		     "743333126c866",
	},
	{
		.a = "31b1fff563621964f922465bbc2e6a273c65eef5aaa70692"
		     "fe9402ea9e34c3de978cde805",
		.b = "3ce8f8d96e0dc345e2ed0268bbd0b9149eaf79be4bcdc2de"
		     "0ee9d4e0fbf58fb928342a69bf772fba17159",
		.r = "3d74328b3406e9339af633fdb1f840d3f73ac53a333bbf4e"
		     "bcad4f564c1f1",
	},
	{
		.a = "35a452dd614fe7fee6f6cc1101b69980faac01b347a0f98e"
		     "d92579b0df8d9547e0dcf7a27",
		.b = "3a36d5da926d5c775e4588939d740d7ffd01a37fa1157fae"
		     "bb43cb2e1dcda302cb0d97022865c5c43c903",
		.r = "45c72930805ce0bc2f501404c897b828dd3311542d444cc9"
		     "aa8e4bf3f2b1f",
	},
	{
		.a = "54709f5da657b3cd9c53fdc481eab8a4ff55960b81032208"
		     "816c8ac63e83a001d84316bd0",
		.b = "5547ee69224b4569a774ddff29c386e050d79f68105ce717"
		     "ae5554f8ae5813ef92fe4e412f746b067c830",
		.r = "56ce87708173c3f347240d9188e39df8c1cbe6bad771cc84"
		     "abe538d73d670",
	},
	{
		.a = "582436c89ce1b69993d372fbb3efe56fd98ed01797075071"
		     "11b367bca3ed422c47731c3c754422822458d",
		.b = "505f3e85baf27ffc59822432dc6be370282d2a50345d7e93"
		     "8295e75b01c0b8f865c74b04f",
		.r = "65645b3a51e014830040c63c1bbbe4421536e910b48a3001"
		     "bd5f2dfb657c1",
	},
	{
		.a = "c21f93ac416ac68e131736c009187b9fe1066fb41c55dd0e"
		     "15235211ceb6124fa831099e",
		.b = "c5502a7884c7dc04608aa4b3a990050517fea2c56775a128"
		     "be5baab5a58d05f313bbb09278918566495a",
		.r = "c7c1e75e42448a1b7bfa46f18696e834e5d973e62efbd790"
		     "fe6d1db99dae",
	},
	{
		.a = "51e1ec852753ea16507229075a7e291728975425e1dad21c"
		     "a3ff80221fe48726d8ece58f6",
		.b = "51257f27b935687b014f8140c4b59542e8fefc1bf201d11b"
		     "53581594c8a06f1dadd2dbf195adf798678de",
		.r = "68875cab619af70abd75a89a40eecde3a1502b5430cd7938"
		     "aadb39c1380aa",
	},
	{
		.a = "5536f79100df9ae80b3fe4afdaf70b025af96430452af4da"
		     "547b8a620b7e1f6ddab222526d4",
		.b = "6a47460921edc3de377569b670ea0a919c375314a366c652"
		     "bcd9abb642ae53ef3123b0db0f7b77aad66a41c",
		.r = "6fcc63ea83f37c6435ecf4a205502dd4b0cf979ad62b3e9f"
		     "16ebaf389521f24",
	},
	{
		.a = "6f7df67694667235a7db91a84463615e4825e6a750704598"
		     "1bf8475db70d237f548326867cf",
		.b = "66889dcc9881c53db1c02f741e462203dc40edb1931ca92e"
		     "18c29f91a36c4e50f050ba5fe4a68591feacbcd",
		.r = "76fadabc9ebdaaa534887b73763f788de0bc5c2d74009bf7"
		     "8a59e12dc65f767",
	},
	{
		.a = "522c9f3d8affecc1d1a6b1f0d8dd59837d786b424811464b"
		     "f705accb9e371e80ff80b53efb132abcd5f7fc7",
		.b = "4f2a601d7f0f802ead4383cb18fc55ddad88347cc569e73c"
		     "d84a7d302128e9665e903175797",
		.r = "5ec7929a08be3a9c3307950b3da7a0fc91f60c574958364d"
		     "82718e1cbf3eb39",
	},
	{
		.a = "6a92d27f6ade6e00696bce4a84600e851ce06bbc5bfc51b2"
		     "c97b78d91d4ff7457bd4f5a002930e168ba5721",
		.b = "764c31550c2fbbc8df2ff51a11116286af8613aa215fff71"
		     "8e30bc2f0de5daae67f61b43e19",
		.r = "7810f9e35452f529dc0414cf230b4cfd9c5ef86e176cc901"
		     "c0f270ea79b3e4d",
	},
	{
		.a = "776572ff9922ee187d4e012eb4e51df04ca377b4e2362601"
		     "d050787b069f03053bf7fd9acc228fa1d9b1a8",
		.b = "722162ef71899f03ea9c3a79f42f113c042ef1a98dd832e9"
		     "a09a98e7bb8a2816ca80ec2498",
		.r = "7d18769900c4ee401f7e3302eb6adec095b086fbf7c14022"
		     "842824c8a35228",
	},
	{
		.a = "704a3e08400a2547bf55534a388a030ac9d578abd16d6e91"
		     "4afd0c206169865a938b9113fea",
		.b = "705df82ca14906c33ca0274f8e1b1f298aa423c611baa0e3"
		     "70f3b1362803d3dc4c09546cf67b0748bf2b72a",
		.r = "72f7735b6a15ba7f5be4d23a12e7408d068c5fcf2c5ce787"
		     "760fd3138682dee",
	},
	{
		.a = "3fc7a378c8cf05031a6d046377b972a1750bed9ba593570f"
		     "cfb8bf7f5c9d50ecf31b5eb28e4",
		.b = "4db0ffa81b690182e0f3e06ef9e69026b5ce6406a896dc83"
		     "1889f00b84906aade207119d7652af684fa20fc",
		.r = "502a09e0f389139ddf160ed6a19ac4bb710b02519a62cfa1"
		     "cfa55e0efb1d1f4",
	},
	{
		.a = "16cbb51798798f0c16a94a1fcb5e2586dd9c21af834bcbdb"
		     "b023b57d07834a775e20d8469f77d5f94d82bff",
		.b = "1bfd911d099dab2ada4a8802cb0458c6b0595a165007f8f1"
		     "8e6e846358eee4b91d4ac1fbc43",
		.r = "1e216f67c3a6d2621e21b8e0ca0534dbec20a1a31e2cb732"
		     "efca7397f9d2c6f",
	},
	{
		.a = "44cdb070b28fce182cb43b25333e1ccae887f4d147fa77f6"
		     "79c162b4b77071965b4bdccd1d74f0d33e2cbaa50",
		.b = "4d9f3309128c553d5f57ec02bbba290e57c26267cdd1a5cc"
		     "f13fd5f03664f077ebec3bb57f810",
		.r = "5b77b2a8f63255f7315f5966de10651d1715b74bb9654892"
		     "e5083bf49dcc611d0",
	},
	{
		.a = "2647edd2e11a223bca5724d6091946e25216db7931c96f06"
		     "177558bf7985288633e95f900b54f3fbf5ae9123f",
		.b = "1fc2b218e131cad304876c1d09bf163f9612bf5afdd2bab9"
		     "c71be88a05be2b38f32671b5e0db7",
		.r = "2759c93c8dfc827d7c2644e71ff0139650f80510807fd6ec"
		     "a2b1bd19e5f36ea31",
	},
	{
		.a = "36581909e6c39c3bf952ae81696c1419da97c9f38a3f40be"
		     "a497fc72a991cdc7c10343d658fe78db64340369b",
		.b = "39366d8a79b046202bd9ae5472ed7e49dbfc090063844b5b"
		     "574d6a9b6212fd4c693dc9a59287b",
		.r = "47baf308398f5c586faa74220ba8bceb406a6b29f8827fdc"
		     "227336b36f22e2d49",
	},
	{
		.a = "3417000970f156f7981b412cdac3efced457f2d0614042d0"
		     "c97be7cf6aad37ec2d8ce2292a467699c17b1062c",
		.b = "3ff5db132200d8c379e548a6e3c934bae79fd509fc36efc9"
		     "3c984f71b4f2af6afe9fd94454ba4",
		.r = "41f97cf95567cf25245694d8508618aca1476da358bbe1c0"
		     "6c543137cd864dd0c",
	},
	{
		.a = "6cd488c8a691a37180363f607df41cad6ff38cddbbe45cc7"
		     "c2f29d0267e8f05e054d01e8cb0b350679f7335d",
		.b = "749e450b884827115eb77c622300f5b002c48f2257baffd5"
		     "c5a1060db36b7a0bf369cec39715",
		.r = "89fbd91b2ced71c432be0ec4c04c9b8fe61ef9b5870e5d78"
		     "3a84a39d3235d481",
	},
	{
		.a = "2c3d1e27c27d09773ac154c62cfe94878e3218bc42d12ba3"
		     "572e39b33fa456442680c9ad4b1e99d189b90af20",
		.b = "25d3d1bd9b2d0149e5d1811b4de4814577f0dcbdbfbffbff"
		     "cbd3111ac9c49995d72388f7a29e0",
		.r = "2c60932e5574bef4c948b60c8b81def818ddc3efa45e4d29"
		     "cafc3b709aaaffae0",
	},
	{
		.a = "5a568e8f4c8e2ebbdf73537193b4580da661794afb37efd7"
		     "b25f2405956e5ce00736fbd7650f7",
		.b = "4557e77035caf8429267990f443b9a7302c44e4c942721c1"
		     "63c90c672a42b8c1af7c0029d03f8e83a4862e843",
		.r = "5a9dcffb4fcb3eb6bdb247af9f311b118eb1474aa66e4c78"
		     "640484b52d9a9a1fb",
	},
	{
		.a = "5f90593a7a231b83793a3490d425e1a1de1db8cd1fbb3a73"
		     "348a332138b52c2d5613efdea9dac",
		.b = "5ab5021b9909a11b4e9febefbb3edfe69d4a2e0fbb4fea85"
		     "b57f92b2e5706d76e6b4dc1180090f6f1ce4c873c",
		.r = "76b6af6e823b4ba077d790f1d01e6730d04803f4e4c62618"
		     "24687081619f2136c",
	},
	{
		.a = "f5cbf9c6e50adae62ef916b5241dff18f2dfe4704aa60d79"
		     "b035d2f2611d87c3e192ea93926fcf",
		.b = "12cf1ce2298ba5f7c4e11873a42485a6db8a876ea27b5c3e"
		     "d92d8f423e67dc52158926928a5e28b7e13fc526d25",
		.r = "13be9d4ad29c676ff96078bd4093ac49d512f532844d6493"
		     "126ad6dbba8e690b649",
	},
	{
		.a = "1fa5d509f556a4c587c709fb49adf1b24817fc3301652167"
		     "89510894bb26993f99fe800882c20dc",
		.b = "1c97ce542d64bb2b0c0666783b50ad7b0ec83dd585326a2e"
		     "edb287ed08900414ad5a35303da5548ef7874e1b784",
		.r = "219df42a796f67ba3fb54c4a95aeee52cf50e912cc90fcd5"
		     "7727d9bb278771ceee4",
	},
	{
		.a = "3f79506e9a18ecfc96ecf82ae0e88b3c61b6afc8919df51c"
		     "e958d3e8956f3be1d9f44cbcf53f92c",
		.b = "3ffa55470d667fa61719bb07cab29e9e78748401bb248825"
		     "50d32d71a37def452f2ee24a5878decbbba036f5b74",
		.r = "433ee870e398a7045a26803592d99e9f3f7d0bc931ee7a5f"
		     "7e5bd81d671899d74ec",
	},
	{
		.a = "1e88e5e1b5ac07c2e1868026dc8b58fd14c17338e19f0b7c"
		     "b42ec3af35eb01ed9a69d588beead9ee8bfad6fd702",
		.b = "222adfc3c1ba3ab465a187e4861a963813657e04bdc8fcb1"
		     "d72419645621975d43ea7d508b0416e",
		.r = "26718380c83c5b4c29dacacaea851056054770896fc8373d"
		     "c088e14c64224d6876a",
	},
	{
		.a = "6f53e0a1787a708e47bc9fe83d695a32b97d60927970a6d3"
		     "e0c7c7940ea7104fdad2c2b2a345fc2",
		.b = "6998951fc97fa4ce0d4e038f51031bb412560291e5391204"
		     "3eb3cb05971620915f8e896b4b82a7209506248e60a",
		.r = "759797a7335f9f0be50246d2906c3fd300c921b3ae21d133"
		     "c81caabdd16b41a0c56",
	},
	{
		.a = "404df924bab9c9eb6c43df3712bf251d44ab49dd0da0d809"
		     "7225ca0afde0e63ae8e3c5c9c38d168",
		.b = "42d9ad894a69a138d23ad400e70876d3189f993c06c3b722"
		     "f0fc175cfaf2a53d11da39162822f3d0bd2deb096e8",
		.r = "489d13795e291c9dd3aad13c3cbb2137c6d1cdc696128f62"
		     "58e41563b61c4b636a8",
	},
	{
		.a = "11fdc2eaf5a0227b9a6bae285f8d057036b972c5b46dc13c"
		     "9982982d35c608e37e6d4e6b23572beb1e0c6ca86f8",
		.b = "112f78a73b732aa0e65be6a8075f531fc176986eaf4f4aaa"
		     "f51a1532490cdbbde1f6ff7ca1c68d8",
		.r = "15f0d4c054ad88f5ee39c62f53de3efd922bf821cd0fd469"
		     "08d421218b6d4760758",
	},
	{
		.a = "48f25eb7d347227114488ff751f9fd907b80fb8370ff95a2"
		     "f4e20ed1f8587f0bfc6e433d1368e17",
		.b = "45268786ce924277c48ccf87bcbcccaf6a9abbac4055e612"
		     "827d30499ba9acfd5f41f3c3e9b1831d0bf65074e83",
		.r = "4d224af8f3dd16f8c916c1c871e6063cb619f0accc7aa03c"
		     "d6f4874857794f87ec7",
	},
	{
		.a = "230dde83b7195646b37ec13da3f9be4befc86492d2d65a1d"
		     "134245c2740f4dba0d9b22d79b0300ba00a8953",
		.b = "1e00b351da3fc2d7a92a3863d606d963873e0b42e9323f53"
		     "76768b00d1f73c5c432e60f08a1188fe2063bb5094bc8ef3"
		     "4a731",
		.r = "25d3f691c07a8bf2fe4ca889e6398e569210af5908aa6e09"
		     "3c67163703ad224522114e56d",
	},
	{
		.a = "589f46b0dbff1eba912f1936684b26de8014c18685e9d74f"
		     "30bd31eedce3538bf414ee7bce19d883e84d5a1",
		.b = "5e3bf15c8c82e6e92d77c169868e8b89ecf812291c25ce7c"
		     "8615f1c035a1a0a6787f8c777147b6561df511da82a8e8fd"
		     "441a3",
		.r = "68c76daa2e2a401874ebf9b82566b969132ca2fcba086df4"
		     "87a555e3e26d05d9cd1841a03",
	},
	{
		.a = "3649851b4ba0c637119aa5c53309bf937829973e54da5dad"
		     "11258fd1ef9deedfac65911552e50e6aa0edce2f1389f491"
		     "d9e29",
		.b = "34723bb44b3b993ceab03d15e6c1c1fe2be151ac95d66459"
		     "5758768ffd10186b984604fe8f182b4bebce953",
		.r = "38593265e78e78b00ab40b01c619c3a0701202e697de1097"
		     "c0ef93170e07e53aba19ca851",
	},
	{
		.a = "2940099c4bc4ff3479aa4b3545359c165edf212e215c2310"
		     "9b7f031ce69760df577d931600ed8737c143ba1",
		.b = "307560f1adcf4265d9b3e924ac729fbe695e27162e965d8e"
		     "0902655a051941f9809c2087437e5ad813a828fe103d5d60"
		     "ba77d",
		.r = "33ccad14f748edceb7c1cbdec84dbe3ae3353edcd0d61c7b"
		     "bd535e599d9600bfc530843f3",
	},
	{
		.a = "3cde94be664c6c5ca7d8739597cc53ee554247e8f5e73b3a"
		     "27b3b4b33ba65983e725e59924e32ed4f5f1bb4",
		.b = "3d1e0b1f767fbcbd39110747bfe209deed805708b2225dd1"
		     "edec2db9376bea0943fe924a3ff70399c7cbac034f35befd"
		     "5f404",
		.r = "3ef6117751b78060f043af98185626b9aebbc0f8b1d035ed"
		     "acffc6020ad6a172ad8f85dcc",
	},
	{
		.a = "2eb2d4011022e0d082870fda04ab4fd39007f0dd808a8d48"
		     "46a9ee2903922cfd8c35b8fcf764d4491fa40e4",
		.b = "27418e94127081dbebcbb9e8a374b1421f0b9440a8977344"
		     "7ef4544b15d5466157800e66a9564f91a27a60c3e10163dd"
		     "dae3c",
		.r = "30e1241abff04618884278256ae3f007bce514a250c9f2ec"
		     "a8276f8519afeca70d2d8e8dc",
	},
	{
		.a = "42c5f83c6dfcd0292baefe5061f9c4350c00bafa02959afe"
		     "b830f1528202b87f38a308ef95c9d2ec1a7126d",
		.b = "45993bc7165341ad65abf6600542ed3a592798b31e0631ba"
		     "cf8fd3ee0bda613f218bb8a917fea9608b703a01b766724f"
		     "f697f",
		.r = "58608cfcecaec3e088210fdf6c1e6f1bf7b746dcdbe47002"
		     "8e3fb73f7baeb2b031865ff39",
	},
	{
		.a = "3d900881a88aae0e937ec4ab473b40878b12ea9b3075e864"
		     "87ff592e4188fab6e34878c98f38c08a0891647",
		.b = "3b6d97698c50aa9a1293065aaa2ede7a25cdf961a79117ce"
		     "177cac2aca1aaff004283dde652cd5fd84eff17dfa965e35"
		     "30f7b",
		.r = "4ed4240327bdfef5fc636fa2bdb399e82e1d1023c501b37d"
		     "f5ca6e2eaaaa81232b01cf399",
	},
	{
		.a = "2de3dd6f988b7ad5331d43660f258a899beedb5637501149"
		     "6bfca53ea72a73b4751ce4dc1cca0dd7e27da7c8e3575842"
		     "8b08c98",
		.b = "309535207f6f72e75fef6cc5701d9279aa2f23a9d9146dbe"
		     "871164f3dbeeffaec516366685259cf2843938ba8",
		.r = "340aa5245babbe936447907ed363da1760c2ed50ec15e860"
		     "afd003f2207461ca4d345445948",
	},
	{
		.a = "378d58d4e50373da3dc5cfa9406655ddce1f30c04c8fbce2"
		     "4a22e827268bc301b5aff83f47bad460ee99595ef",
		.b = "3b84d43deacfdf9a69a139b549a72dd330ab25b640637d89"
		     "02e8d41c8912c0c1a2cf8f443374daab97f2798beec8a54b"
		     "756b569",
		.r = "436a5604f9425acffa4c56c50ac01124560a03e4431015ac"
		     "985ec89391c5a766a3bdbf7396b",
	},
	{
		.a = "2e2be92a674b64271ba77a8ceef636947c06c8e043dbbba6"
		     "419aa89d72b5b0c3c8fcf0e297cb1d89b48a28afbe81249a"
		     "ee5e45c",
		.b = "324369fc279d52cf53b932597f686ce898d05e486757d6cd"
		     "c56db2400656450c3710a909cb6c9a244b5ccde3c",
		.r = "3818db349fa3908706c1d601b7b8af9d0bcc0cb2e59bc2ae"
		     "160bb678c151d0d0169db45d6ec",
	},
	{
		.a = "3cf8d75fa4dff25e8d3ae92b8fe1268ae29d7dc6796e8edf"
		     "4168d084e172eb066c0672678de975dc8f266efbee1fe284"
		     "2eda24",
		.b = "363be447b5dcd69f18a23cddb887413c538e2ca983b47dad"
		     "40968ac80fca6befc4659464d14a34321da9078c",
		.r = "46e81c9549394650f06cb651ea66d9e343711fa23e51142d"
		     "e1c84b376c75a6d08bddffa234",
	},
	{
		.a = "5dcb41c0c32ce8b01ee1c09a48ec6c03139a109f1de3a99d"
		     "d5cd9c72cedef58f9d83c2a1efe2853fc991282637dece62"
		     "970cf26",
		.b = "74f3a69b820d910ed7c0e47d0fdfb74b27990df6502e27eb"
		     "f7dd95368748b11012e92aca10c5b927df0e489f2",
		.r = "7bdcd8ca6c2ebfb0312b400d328e77b99a5423a7447c420e"
		     "5b359640ed3d3a31251ea7f4b0a",
	},
	{
		.a = "19cd8b4aa083fbdd52c5e36278cf791308d40acacf5ad1bb"
		     "e362f125129d6471ce98fab1a73b8c155f76067c0",
		.b = "1600d7a5562574c7b3992703e469b4d2c57f3b49dbddea56"
		     "c4087949baf82f3016423ff4deacdda9ddabd0b7ae65071c"
		     "990a540",
		.r = "1d4cab55e0d93ef827953dacfbe3135ac9500f5b41d559ed"
		     "0cd10af09d955255e9a0ee6cdc0",
	},
	{
		.a = "1521d207a6110cf889bd3bf2288204e532edd789c0087e28"
		     "d11f1f0c2cf4281a1604c2e5c48cf0b3878047f7b",
		.b = "11e2602fff3cd4b12e1df2edd9c13aacec71fa1c1258b58b"
		     "8f21ee736ce29d639ba63c7efe76c0043f8f8759ee9b9d40"
		     "c6d8293",
		.r = "1696a702010efbcbe0f8428337ab4d8392cee906db9ef465"
		     "f860ffa079afd87a009118cf98f",
	},
	{
		.a = "1ba61782c3046b89565645b83fa16aafa8dfadffc1c39f99"
		     "bafe55ebd529b6e46cfe0bb1120b46b58e89f015a",
		.b = "1bea4aa3c29b7c7266d543da0337040df47b0e97555cd08b"
		     "d79ec07c346b71814cfa3af0c8ab6e5d2877353657961e78"
		     "20c2a5e",
		.r = "1e83c86d8156cd04087fcc693ea652114a035261f4298204"
		     "d7c6764ae538ae99edd7586fe56",
	},
	{
		.a = "3ecf496e4292b75f9a9670880538645345bf502e1a5bc2c8"
		     "9328b16944216d0a0e66c65b5192f4432c630af3bfe",
		.b = "46951e3e9c58c1a91b9b75d56c33bb511da5e4872c995ef9"
		     "949455cca8e7af6e3f58d5319218249718aaf871de538f67"
		     "bbe50dcd2",
		.r = "4d04a92c6bb39ee24669385a7264209b2f471f2b6b81bc4e"
		     "1cbf1b4cfd7f633c8e51846a4ef26",
	},
	{
		.a = "b37e192b94f62fbbd90ea281a2fd8fc59402b6a15b22395c"
		     "cf26b94089e19257750d6f0aad5ce89311dc88c6676989ef"
		     "c51e6b5d",
		.b = "aca73bf07977f19b5ab808cdab73702ade57811031783532"
		     "1b4e3a5e114374004aa903360b9b8811ed1c384be5",
		.r = "c561207d0c56ad8c6a62a5718d66fbeb819fd3f53b69bad7"
		     "6d2ce4f793a04ef167c5bf7fd743",
	},
	{
		.a = "5209152a97476e27655f662b192bced6a6378a399de7838f"
		     "8e2619ae5b5e32b627042058245ecc2968cd55a26916d9b3"
		     "801481ab9",
		.b = "5d72f984a9382edf6aec318ce694dfb517cf9fb3594fedc9"
		     "8b0f817c607285a5dae2a102d2dcc1eee0bedc586fb",
		.r = "62b0bad8348883fda93d1d871656e88a150078fdb6855e78"
		     "08cec104a068d5d478e62af058e77",
	},
	{
		.a = "54d02589d4c0f2d1f19e5dbe19426ce8f10c3718f20d084b"
		     "b2b218d30dffc03cf774d5dcb1ba3cec4c7969dc5c6",
		.b = "61a0bf4ac88d4cb5f1171a705d4d7998d128e203c9c6debe"
		     "9e0195f0a76b095378bbef1aaa61227df204299f9218a20a"
		     "4478f45ea",
		.r = "6ca36fa2cad2b8b0fe8f96af99809b33ccec45e9d52a1374"
		     "6ce61eeac0cee3b92420862f9054e",
	},
	{
		.a = "59fbb5e77ac4c6939f506a3d2b88561c7d857072cf0254ae"
		     "03f3f3b7580d79f44563f1107d32fdb316b649ae19c",
		.b = "5308f555030dfe2a2d3acc43b8a0f76fcc3adf883f41e73c"
		     "b0d22384bf1da72d0b610f2f26ad369d3528047a6118210c"
		     "7564bd2fc",
		.r = "5a9be472cc242465acd0fd46542ff8a8ca1a0d57215d97b9"
		     "d347095bb0d6b307e98c59195bcac",
	},
	{
		.a = "13c6df0db0cad2e785a62a999c9207722733730694a6c277"
		     "04f97a23dda58d54afda7e039254c1ad64c3d4e8d18b2326"
		     "83441fc85",
		.b = "1154aaa622140d190c33b5d6c543a78e532665581bb3a3d2"
		     "c4629bdf86acb532ada4c5f70979dba6471dd6d4ab3",
		.r = "16712b107ba923b96e99ec357f4a4b21bb9ac59b10e7af22"
		     "81fb523eb258972737b8a3ddda541",
	},
	{
		.a = "ad2ec769ec95442a5822b2ea7fb629f33e3599d319428362"
		     "2016ac11cacc2757b9c4f4c598fb5c460bddcef5f0f6cb0c"
		     "a891dbf4",
		.b = "a7c23207484b51c7fd1f29115dbac4e645d5fa19dbf57d70"
		     "65d42e073cb2027cb63c3ffaa68c4c6e7c648a0144",
		.r = "d3da58210ecd163b976b24fde2c9f082797d0d48942d35c6"
		     "ed6a1e964cd01e923833830678c4",
	},
	{
		.a = "3389e7430497fbfd7cac8430d84669cb672f888da04a0f68"
		     "81d74b5e15a29533670b077f2853edabe588b7fa63cb8d14"
		     "1416691c5",
		.b = "3ba8383dfa55b8f94a8a1672dfedb3d353b4f4d589b1134e"
		     "bd9ae57b8eb780c5bfbcf911a04949dc498cb2082a1",
		.r = "3bd105eac1dc90b428c864fc6ee986e7cd881a9461f11bd2"
		     "d1e89b831f9029c97d185c660e565",
	},
	{
		.a = "68937566f61f83d4be7315ff349e2c3db46247c7534b82ef"
		     "edb3ff280f01be3240a21bd043bacadc83a6e9fb34ca3941"
		     "bb2b643bb81",
		.b = "657f9bc80e5d7a8749637a9ffae10b1378b7531779fb245c"
		     "92dad2253102a61408d3bb952c87774157e155b6fd9b3",
		.r = "698a96710f17107988c87ba2ebb9f968dad998ff44545043"
		     "0e1ae0db1f35f1652ca51b174f9afad",
	},
	{
		.a = "806e21a6018f6902c32b5ac321abd7a9cf3871165926037b"
		     "b70bb13ecdb601fec01431e17341ab539f2b074468f10e4d"
		     "f19688267c",
		.b = "973640b2656ab402878f1d7d910d1c65a4bbf0243deb0a22"
		     "c5e3b5c49ceebda794c2e2efbbd43f73bb264301e44c",
		.r = "a191a837fb16e2706612fc2329b522d1d708e95e3e2ba43c"
		     "8b17a6c83987029e496582e5dc024c",
	},
	{
		.a = "4bfe6d14d02adbdece3c22192db04c62a7e6928590d4f4c0"
		     "a214c35b5c51c094fcea85ad161c0cf6ce9b641645fd4",
		.b = "4f4e3b6003407e08520742559cafb7589e94fa8f2e5d22ad"
		     "def829a1a4313fda088baa948df6adcbbd1d9458e5fbe4ee"
		     "8c7f4d974f4",
		.r = "5b668aa84bc05a492e9fe10afd275844a5197f828dcb50fe"
		     "9eadf68ec180b2ee10319fc58d4447c",
	},
	{
		.a = "16282008f340c333d130e2f7784183fb78546a5c0371c704"
		     "25387c03849b26f351843868865be89ee6820fa936285",
		.b = "1606d544d1376ac311019aa10888a8c67bb518f0008e4fea"
		     "56fa823fcdd9489475cf1c013adb41fa5bb3e913b24cd780"
		     "b8f22c0b2e1",
		.r = "177f836eb1cbf73e066e460bad118c201af32b042be78aa3"
		     "98ecbb87e0710976726bf2767a1d5c3",
	},
	{
		.a = "715a6da986222027f63d756ccef34905d458b4ca7ab5fc15"
		     "049a1aeb234874f104529c6f9a06969a27cd2f7210fff67e"
		     "a1ccd75f7ee",
		.b = "7282278d0877ee30b1f0a0bb2654f57cd0e699fdd9cb3ff5"
		     "9dd5f1e5e006989c179d55b5f05d6b36729eae3b1e18e",
		.r = "7c392f13a9f95cf9e2f70153c9dadf3d48394670ba5ae281"
		     "73bbf89c16de3234e99d33d08ffc70e",
	},
	{
		.a = "f1bc46ce609cdf7a7c97bd840b9b224869cec121e65a74ed"
		     "818a8954da8b40c694bae85a1c16111c47e7d82b0106",
		.b = "fb75c11e8dee621c2ae2452251bf8d517c05d54d36e87f1f"
		     "5230a1c4d22a87836c1f048d5f4a334e7ddd03e4df206bf7"
		     "64649bdc96",
		.r = "128da5ef0333c0d1efee31eb0a0c6816df7ef64565fa795e"
		     "c1a355d6cc4ba707787b5faa67674ce",
	},
	{
		.a = "5be40b81712d5af8f166e965433656616dc88505dae1c99c"
		     "f6251c40ffaa726cab994f3f2a6f8c9d885a48a48b45241e"
		     "4ec158130f6",
		.b = "5017c43fe300730ec66d150aae46ac1ed96136a79fdc493b"
		     "9333c73a0292cac1454966ed522dd4ad175b7af495fea",
		.r = "65ec987fb27319bc814c28c2f38e8d84ea0d8caa7e7bddee"
		     "887664442c04a6c68cd72777d68761a",
	},
	{
		.a = "24e090e514260628e7006fe8ef6982dcde7b6641ed26650d"
		     "67fe928111bb02f7e7d7894dafd79d23dcc761e1cdaf1e55"
		     "04c6bff620b",
		.b = "222d8a56ba7bf5a83b835705d81ed5583f7066a36155b3d7"
		     "0cbe9e71aff38b31ed74a3cea2ea3e26129f5c7ec4d39",
		.r = "27c9ee26d3cb8aa69095d665e1d14beadffd7902ffa7870e"
		     "025c24fe8e1a5a8d5cc68d27a3f2a8d",
	},
	{
		.a = "61405295402dd8b00e25700dfacf55b743e601814262c84d"
		     "e0798206b1802e8d491d5a02537aeb1def33efd9106918d1"
		     "eced5",
		.b = "6626c83bcea1940abf7aece1d543b9172da120438bd315b3"
		     "5fce702d4cf40ceaad6c5a37b6d5831f0041f03cba63b1e9"
		     "aec522acec80dab7ab735",
		.r = "6c40ce2b24791b71a8514e9ff5d974186a4050b08456d03a"
		     "58d71c1976920487b813752b193641dcac0c7",
	},
	{
		.a = "5e46d5de23e7c2a600290201d033e698d1d55eea4e67e465"
		     "f6ef83091c696554b9871fbaaf93a166c96224be5d0e79ce"
		     "75601d43cbe6e19f6684d",
		.b = "663d6b9033445b119e8a40f2589ae93c5642726461cab0a7"
		     "72fea02072bdb4139a73a2b48bb1a72bb390248e6aad95ce"
		     "a8ad3",
		.r = "78b05f955b7d0909b24486705188e732a6b3261bd0d90b45"
		     "968e3fa1e3316dbbed97e86e2cceb34d8742f",
	},
	{
		.a = "7c27f13e17b58dbdd5eaf0def441960a365cc6a92b3c2169"
		     "c59c15f3028f232587112c89c0464b31fc0bf61a497450f1"
		     "fd977",
		.b = "69d9a1bbfb7804f4ecc322b26e77e14466e3d474c09022b8"
		     "a28c934319344858ebd6ad300737ba51feaa0f100bdb7f24"
		     "a06023471aaa3f095daf1",
		.r = "7cc662a8353de4828ea513b11e28b093ae2dead59e1cef76"
		     "c99fc112980d723bac561364940efd64d4105",
	},
	{
		.a = "3c43ed3c6f93afdf6ebaf31befb5399d686eb0964e77494d"
		     "2039c68582f3cc1f8fb401217f18b60fc5119594fb0f82a7"
		     "c1cb38c2c35bd092737d0",
		.b = "3787da5390f2c638eb5b4ac61d2885d79d087d377fb6b249"
		     "28d8b4f1e1b4e46f0665627422091dc0fc8205e6a445ee9a"
		     "c7cf0",
		.r = "4611f3c04c5a9a21bcf07704efd6e217cb1d030109a7caab"
		     "7e51cd12927b59b0f0c00b603b0f5e30e42d0",
	},
	{
		.a = "26c430b726deba75855f8756fbdc3f66253d7c434a90d788"
		     "b73101aee9adced0fa265d906eb776337d9357eded1e31c5"
		     "afb61c8dbaf1235d917dd",
		.b = "227295b37eaca84adb6fe12af22af018c5a70887707fa808"
		     "54344cd39cb0ae8d8f1b70ef8c29e6e38f2cfa637233109a"
		     "bfc65",
		.r = "28f99f06a87e410dda5fb6f4c63d02a0009df2c8e0086fcf"
		     "5f0b63d24e1722964fa7ed778b93e3599c44f",
	},
	{
		.a = "28b7a4eeb8f949472ccbd81afbf5967c68b4404d78744a66"
		     "41eeb49656f0c5658fed789f3e911c5bfe40a398a8776ed6"
		     "a788cfdcd50e12deb8113",
		.b = "2879837a5b56ba4e56812fd7b586cb43b28f279fefc3a0dd"
		     "d6968d9272500e44ca3abff4d2a73a843c786264b510c2e5"
		     "ce6d7",
		.r = "2a37b7efe69d1fa03018413a39fc11da6212d5a2b312f88c"
		     "702a6c0fb913944ad63e2aec38fb97cab11fd",
	},
	{
		.a = "6ab9c198e86120643261db28c3b4bafec69938f7b4be4be1"
		     "dd7a4558b7b891ce8b6cdbb8efbe2574516ba730c510548d"
		     "8c99628efeb8a2031abeb",
		.b = "6cf468976da9cf19b0ed4b90fc5648f676c62b1c29ac2095"
		     "0575ed8c6ebaeb2cdb3c66acf1b50a4c62097eb861defbbb"
		     "60cad",
		.r = "75a486b9ff6c5e6d027ae67a70c3f25c01fc7e0a826aeeb8"
		     "d77013ac33b43e9991a0a07db6df09396eac7",
	},
	{
		.a = "66773cc79a73512979d0c797d5450eca79039239c0048e29"
		     "97f127c1a9140401eb790fa93c2459dab8c8741efe0f0b05"
		     "3116dbc4f19b3ea394513",
		.b = "667303ba154d16bd6d221e5f2af88584c8ec971cc5d3cfe4"
		     "a815ebeea56d00efd0c793c21b2cf9a9b7f37ed2fa1e3926"
		     "7593d",
		.r = "68b8c5e600e1d87cf63cc326855ed9b4263b2be793d00b31"
		     "2b1a0b224a04b4d354d0e27ad7a6322f267c5",
	},
	{
		.a = "b69c2f59854f1b7d9f48a5de97bcdd5ffb2d567345afcfc9"
		     "64c81abfdfccaa46d0332643487aa5bca28f4afa703886af"
		     "0cc3c4",
		.b = "e82f0af1d64112f342bbf28a9fa9a7a8746e98d01d777b88"
		     "90857d23f3d68c6762478b77105ffe142be97d8a6d7975b9"
		     "3e95b2960916b27eebc63c",
		.r = "ee9ed664275dfb3490e1c5b4212865380c1529098e615a18"
		     "592970a533ca75b58a5f7e99450d568cf6d56c",
	},
	{
		.a = "2f031d9878d58ef04fa42f3db553f38c6d13ad4f2acb5666"
		     "52e938cde96707fe12dab205fe2f0e7fd90eb8dc0db7ca34"
		     "8e1ecac",
		.b = "2562b63f3618a28f06036e276a3a0b51f740c6c4ccc62358"
		     "0b332529e3c886a56d74dc88213afc34f2631e5a13b2fddd"
		     "38318ab4f5033df22d7a9dc",
		.r = "3165477e4299482810038cda9e697de73399dcd3b266c5d0"
		     "4781c4b56bf7d6b21ca6b3c4549b47716945fbc",
	},
	{
		.a = "4099c54116dc69711a515cea21910dc0ade5f71bc3cbbcb8"
		     "722596882216ca77b1305babc60c0506e39ecabc92b4da82"
		     "b1c7739",
		.b = "46b308f71f4e280ea802ee3235cefab5abdd7c4539943290"
		     "4868f866958ddefe7e7935d3a67480e7c112da217a23f827"
		     "c9b46d923f47e52c51d2071",
		.r = "4930122b2e74cfeeaf98f311b02cb6b7d74d2c2cbb957cf7"
		     "194be2c3f5dd616464afd4278b18210fa862117",
	},
	{
		.a = "2eba58133e1905b0c3e3d1f38a050722bed713fee446e1f7"
		     "8a5d5b34ed3eb1dfd5890a8e9cfbe6f3431ba3ef82d593b9"
		     "da631d52c1f58c41f2a02f",
		.b = "30d19a2230add9ef8636858843cac35ad81346e14c737468"
		     "de5c9235eacc9c73ae7cb19cac6c3c5e0f873b3776451623"
		     "dcc83b",
		.r = "3a4b375620193c4b9f9ffedf3936a038b68bee38723a9591"
		     "671e6dc252921ab32ef4eabcbe1d5e98952609",
	},
	{
		.a = "62d3eeed739586b307d520aa66fdf31627b2b06dd2707a62"
		     "7400bd1f2db8b6e780c3ea9f26007fc632e0b2eb084f0b87"
		     "e5c1c0077a854d7f409ad7e",
		.b = "669360d5bdd658d566543fe18895b0d7f454c8a02c394b03"
		     "a1d9b2b003f6ac2d2d4528297ac68608ff2f4a63cb349281"
		     "946e9f6",
		.r = "6fd213d3646cf9a5129db36182a84778ffacfab2d66c71b3"
		     "f4b005ea09340d7598046deb3706b4d8d528966",
	},
	{
		.a = "347888d60a33f8b61f9bb2007532aa1eb744e2c2a84ecf97"
		     "c295c4137ea532e6e672e14287055c936325a1e7542b81f0"
		     "57c700f",
		.b = "38cb5516f2b444d95a339f89acb2cf464868279b43e8bbee"
		     "bf91ae3c3c87b745756f2ad7fd8b77c20225fe52b67bd952"
		     "f7e6c3902d3b95673114dbb",
		.r = "4510be2431127d541a3eff0dae5dcde6f1e63013ba108f9c"
		     "5a0692c24e34edf4dcd9620fe7a65c4a26e2ef7",
	},
	{
		.a = "45e85d18e8d7c1aa45704b4c2058bb1614dc1d3fb8340f69"
		     "806522d93702813241e6da3294f7ae7409983383382a7bea"
		     "65fabe0",
		.b = "538d2e8074c2edf23f519974b2864cafb3eee403d6836f2c"
		     "b867cb5aaafd0c1347ad0eee407b681dd442d554e018d4df"
		     "1202bf831be7b887f1961a0",
		.r = "56bcbfa2a82c68699f85d0b9045e6309274412af1bf9e93f"
		     "f3b911dc96038c7628bf877b6a77dee23c0a1a0",
	},
	{
		.a = "1288316274aacc19f984426f79563260201a93817228c304"
		     "7b1af465bd63bd2d28e0b0cfabbd593d18263e57c1a1d5a8"
		     "7fb9c7d97a8f42d7c3648",
		.b = "10941ef14a7e6a5a46ed981831c20dee4d585eacf824aaab"
		     "98992437669dc86627a78dd1d820cc473d510525844948f1"
		     "c9098",
		.r = "14f78f752730b6f05aee516ee5a9e8c8066752b68f1e2359"
		     "e188bb1f5fa30b818654385cfde2b6cdeb678",
	},
	{
		.a = "280357b80157b056a9f93236ea4a96b0a30180678dab7e29"
		     "042855b83c6900334df17b9d756c37b0862661fc311c9386"
		     "731c9be4be8c7c83582ff6bba",
		.b = "240ea2a02538bb8d07c1c7dda2b441666ff44de744a61795"
		     "59c66aa4795ffbd4160d90c8bd5856f112ba55ce1b619e08"
		     "ee26b5f26",
		.r = "2fba59e8f3c862b295846bbd2ee0faf2d8d18f0223b7cd06"
		     "71ffbeab03b49418e4ea97b55e4164e51ec5f8ada",
	},
	{
		.a = "93e0f8a84c728310c6b64841da981c57dac26291708cd338"
		     "bdf55a8188474b6fd791b5f26088c9ad56286523f4ba86cb"
		     "81bdbc2",
		.b = "834da9c3dcd0642656ae7661b7c56de481e5b7ba1e78c276"
		     "907e2681a7c35fc4d381b429903c7315e850573f1aa03529"
		     "7492b0d7da203b3dd5b6076",
		.r = "ac42f47f1b241a5a237960708e9ca24df3c74c7d792031c2"
		     "d7f0c557ad3bb8228319397c7f90457bac284da",
	},
	{
		.a = "9dcb088cffb10531ddf0211f9127b767bed3d7cd56db782d"
		     "9d99f775b0aa5a12bcdb34f9de21acd8dfdeab663ce3b113"
		     "834508f3529c0e4c33d94c19",
		.b = "b044e928a4fbf8635c92585f0b97d2dc37b2958d1802a94e"
		     "1018785a5c10df530e039792f82b8c05c96552914fb0e74e"
		     "58f9f525",
		.r = "b7c22f0b01fcf7e8b6e6231b3d9470dbbabc2d531f6ded3a"
		     "4d20deb5821a54a7fabdb5b16a0f793037aad2e3",
	},
	{
		.a = "2c73e3861c90dce2a7d933f2060f84850429d00c01c514c5"
		     "63505886a2b11a4c4ac2b63825f29d814ca1e6f4afa41e4c"
		     "cae9aa407",
		.b = "288531d5b6595048d0e7b770ea6cf9c26786a3aeb7a1b5f0"
		     "307030acf9317edd73120816386c46a9f9b91f7c6aec1d68"
		     "5f72c962570187b34c40f8a53",
		.r = "2ccbbbad78953dafe4777123a005bb92757bf250e00abd52"
		     "3724fdbd51fe3b5d14c596e281b8a0745648f2857",
	},
	{
		.a = "4999e088dcea64cbf4cba439295ed028c001ee0b3be03232"
		     "61d01e1d59570a9ec5c5611f5778a8f47c9930bfc6bd2ec1"
		     "5a05746d0",
		.b = "487ff683437e448efae77e8a5bc621807e0a832b854bd7a6"
		     "68517b8d217bfa8b0996d6769a94578c09d3e27263512f27"
		     "5ce5ec27cce7653407e95d230",
		.r = "4ab76065639776a1253bdba9ec37341444641ed9a1336a71"
		     "b4292d257b8436e6ae3f44967966b3bb41a4b8a50",
	},
	{
		.a = "53177f79018ed47528ec38153be5e8c6e90039b7cde42f7f"
		     "0126ce89feb93e11fe09861687dd73b537639034ca5561fa"
		     "454faf0cb8e80c8eca7c4ded7",
		.b = "58be4ef81a47d47f6642037ee4e405f01d7c9ae87a05c493"
		     "64797d99c1ebfca8454e219ec7b388c9b969d14db722bf94"
		     "7e125132f",
		.r = "5b294290e7c727163911d40aeae5d9f1fb482e4b8df31b55"
		     "19d77d6db35f1b79458f219c5fc87d7267d192077",
	},
	{
		.a = "1add9825d173a9f85180570eb176be0cc1854e34ac4396fb"
		     "8342b4f6da67830e9a636ba89534dbb50150bab457492bd5"
		     "40ba103e",
		.b = "18d51b9dccc09ed1a938915e0662b23c78ba1f9dbe4f2585"
		     "8c7d395cd4147c0a4d229a05655b7d81f325371f10aa1b93"
		     "f48adbe09dbe0598fa7da896",
		.r = "203fd7241aa0e48302d55a51df04c84d84330e3652e6b39e"
		     "9ffa7bdab9cd999e7ad1e0e454d191c8595848f6",
	},
	{
		.a = "547d4291bcbeb5f4a506357ffc3a962d5d0326eb39ca3e7c"
		     "6e3bd08384a83b53cda4ceb000f4d8001713e6111265a38a"
		     "3164575cb",
		.b = "46b880b9775b5eac3b4af101d290d229a9495ef51f08d47d"
		     "643e5258bebd9400d02ae7686395066a9130e1d2b98b4a1f"
		     "383e8e863a108bd90f6083a07",
		.r = "55ac2d896546247e799293383cbe526c07ec1132c5007de6"
		     "8ef8def59325b7d6aaed8b05c806bffc128ae9fa5",
	},
	{
		.a = "478d00c0c26851533fa5fb00e1d663f4db0d9a96c20957d6"
		     "1d412edff90f713f37347b4692f280c01c2b8862505d0660"
		     "1abc1600ef4",
		.b = "4545034b956ee4aae5f482a37749aca3c85f81c4069f8e16"
		     "f268de62136bda95f1a3aefc7fa6919a237eba53fd86a084"
		     "d4b2eaf747a9c3d619a0071b25c",
		.r = "50397d24f801f90f08afbc36b8a736928acb83a89fdac71b"
		     "5f12c6318792c2e5c9578033e1c7134f2cf2c166d24",
	},
	{
		.a = "31e346bb41fbc4a426d1df66e980dc34bcdece632cd64f8d"
		     "fe8b5e834cc7c2b786426c8a420610f2c22739601ae91dd4"
		     "5f138a7cee8",
		.b = "3004245c6132c6a83102b859144076b36d99dc67fd58175e"
		     "1af03088a47c2fb72f87a4f0d70085bac3c1e6635915ddd8"
		     "30fa74a57448739813212522538",
		.r = "32d0eb3acf9f2656ec77db54badd51ea28ca120078e69f85"
		     "ae2896b410a2fc0fad95077ee3cdf1efbf891c894e8",
	},
	{
		.a = "1ccdd16940b590eb23ff82c5f9f6d57a621f34eb3236f803"
		     "c095124b7da573c6e289dbbb662e93649a5dfc8d6dc98fae"
		     "c46e02856ebdef3b31eadca55a0",
		.b = "1ae5fd16b2b322ca4b7f7b1f9a4796fb8970aad3d0fa91e0"
		     "8ad507ce487e3c0b8f627bff1ee11678fb3d96f9337a632d"
		     "7b55b2d7420",
		.r = "1f287b4a3c158c597491a8cda09bf5b7b598508d14fe62ca"
		     "39c4bd4dcbfe60d8ab9ab1256b7dfac21a10a48a7a0",
	},
	{
		.a = "5fa7b4163add817d4e75cf4f577e120b240beb9a009b3930"
		     "e079bae2413f6676918426159eaa93318ebe2b5793abefaf"
		     "b5e38c02f2aed3ae66b6fdda9b8",
		.b = "4a264e3d87007b75657b6280a5255bb3a516faa770d62884"
		     "87c26e23122f8e148544c2706cee6c87dd63146b3d760eb1"
		     "b09db607f48",
		.r = "6164889cc4d58877517991567e772f06ee3557047736bf2b"
		     "3dd56a320968c9cbfcee92db6f94d4af72741363a28",
	},
	{
		.a = "4b280b247c4464acd4c5834dd98fa9457899ca12873fecd9"
		     "b87c180d0173d69ca2d152854b040b1899e2cadf29b3333e"
		     "87574957177",
		.b = "4aa64863f4890c3f9a5c1f7e2c7064e80ffdef93f1f11b04"
		     "dbd64b7e0ead8f4121a915ea44101954e8e58e2bd6af14b8"
		     "98ed0b0c64ccda0ce2d8a5784d7",
		.r = "5978246f6a19c365ea7dd01721a2378a9fee53707d4df7b0"
		     "d8e83f44b5fdaac20d39b79f8f39829bf93fad96765",
	},
	{
		.a = "2ba16f75674eb3a0dd7d19eaa93d49e7f21d32a8be164ddc"
		     "6e08681e4aebbc08adf959daea7928d7ddf79d1c793e219d"
		     "535e84fb234",
		.b = "23b197ea7236d30bbdab39f6f90758cd4b69663e9e0b5444"
		     "7126db3ad6ef8149e4038213b761a19552dec45892d5f9a4"
		     "cb2d9a7ce7d900571046dd40cf4",
		.r = "2e87d4fd2fd116d078cbcb0616a04cf5aa91ace4e89621b0"
		     "4158afa45e44b71ccf66c3046fb3738ec30663b0c2c",
	},
	{
		.a = "3b61d77906519c5de1be3277abae9007bf94091e4a62daee"
		     "7a901070b2088a96995511dfe37c42c5c2de572408b9abbe"
		     "29de5522aaa2ccc0739cdde8fbf",
		.b = "3c25ab9e8afbf26dd1fa537b1a647c571c3f0d261b4539c9"
		     "cc919ac09e02e1df3b26dc3419dd70337c1480dcbd2acc19"
		     "d613a6cd9fb",
		.r = "3cfe3abb8fd56c28ca4e2e5ba63e005f47086b208db7c28c"
		     "5be741c0dc4d8e47fb78051e6a33a4ec7fef86aa2ab",
	},
	{
		.a = "2969653d2fd900fc2e1a9522e08d762cdc7da4f86d014efc"
		     "dca49284133d92979943445f066fba74a829f8d81f1baab2"
		     "6a5732cf89a88845eb310f7ba22",
		.b = "259a8f8c0e0785b581046d1b87c4ef8c4869f16aa5598c9e"
		     "7c8e5354c80b752e4271825b83f99666d815b229ac6e86ea"
		     "0bf165cb0ca",
		.r = "31d8a7405d8e6721f4d4cea11bcb866886a157d3d2f7b019"
		     "e7b394bfef7f7cd9f708bef0d4297f4cd9585be1876",
	},
	{
		.a = "506f8d1906b029c816b5cd4ab1d9b59d9fc89b74336141e4"
		     "5eb79f17388a29246eb55331331de78dd60515a3adbf9b1c"
		     "323976491668d9c8556",
		.b = "494be98359239745ee26f3d7aaa521e2cb276a37d47c166e"
		     "0369c44fbb77d7c3be7b0ca2ce85d69706e652557ff30906"
		     "15ca9f4e970ee9c4b4075b6ff4ebefca8a7f2",
		.r = "5909113404994148bf757ae73c913969c4cdd10aefce2a7c"
		     "cc0deadecbaeb2eb0d9f42fa135b3aa1f57e91619b6e9c69"
		     "e",
	},
	{
		.a = "21343f9bae0a088229513be0b7b3980c8c4254084f1af6b8"
		     "dad25196f2c46bd19ab09a5e070cf6383147b27e2de3f59d"
		     "ab9e98705ed82ea5976e5594e7e54e17f9988",
		.b = "21488f27cf5d5c2f8e68984c29367add5d261a07ea636743"
		     "6f3710787c7a684f2cd6f31aa67c4cfc7031cad9d1480085"
		     "305d3a55e7d2e2eb408",
		.r = "23315f7125c5e9557a20a2ee85c4d2ebe6bd52e73e6a8831"
		     "43f032dcb8997966ec5b4e09777e4577108496e284da4de3"
		     "8",
	},
	{
		.a = "58a5d5787fc9ed20149a99828579cf06aa7fb92956fa8783"
		     "0c63f9d88a98d1ac89ceb3da69a1c723a97413aa4022d857"
		     "3c89ca9488ba1cf1c79",
		.b = "5b07e7dafbfdd8ad725a7c4095fdc4a52df69702253f374f"
		     "e32ffa965a5ce4549f266a98590ce8de7c6e01f7a62bf429"
		     "9c4a4defbd38bc845104ddcf8912c1cbfacdd",
		.r = "64f6bfe98925bc757c53aa7f9feeb67757f12c01992bb2e3"
		     "bccdd4c940303dbbcba127d73cf193d78d3c0efa78bbf442"
		     "1",
	},
	{
		.a = "d91b8fe0860e8843877df1899e65373722d4faec5b6d3acd"
		     "217f33733f46659a7afb4857fb368ed583fd1ceb33ae2faf"
		     "7ebcb71e48df0ca9ba",
		.b = "10620c79f1b5119d772b87c9585dd059a198281b8563dabb"
		     "e62965f1798a6021024604bdad93f5908141f99ce060aa1f"
		     "2f691d15ff1ab0460f2d22620bb410797204a",
		.r = "11d9f3439f0ee0fd907ed8a473e377bdebb29200acc7fe09"
		     "4637215325c5e63ebd9fdcc111c7579ac3c3c60f9688f5e5"
		     "6",
	},
	{
		.a = "8df384d9fc8aef45ef7bc545004b42473b5167c0af668748"
		     "e73747ff944665b1cd8af417cdb4f2cccbdb5fa260eb3e54"
		     "e94988fabf95776b42",
		.b = "9134ead1a618378f80138ccdccc39d364151c221a8d3c6b2"
		     "893ed89a2199b3104dc5993ea2171e03dd3333e6f51e8628"
		     "9b698fa4ba0a12bafaf6d33bfdf1aa790872",
		.r = "a8644df3da83254713532df1104ff7c63d56471252a1c0b2"
		     "ef32fa3e071c3dd61c35a0b722d7f81c70cb0881492446c2",
	},
	{
		.a = "4503fb24d650b931b8b9488e30c934775bb84af01d25048a"
		     "b4da3a829ea01fa9f2f72c251130c8f138bde24974ffa17a"
		     "abeea363c98282cd2ea",
		.b = "437bf0cb65aabfab5125a12841d24b03191b3f5d2954af40"
		     "14394340b98b7e21278c0afa76e458144bafce54b3edebb7"
		     "9ebab9670fdcaa965181841680c093d775df2",
		.r = "4b9b8468e03c458b40fc9e211b392d842f31becc6ac91e19"
		     "4e9937d2e1e0ca319e761529b4f17e9f241ca19267754ad4"
		     "2",
	},
	{
		.a = "51e178c447a0d68b7f63b31f380b6f11031eb470dc79eb1d"
		     "63286dc855d138aa61fb13c14e049f899546934f58e83df6"
		     "c6d1040f05f217934eaaca5066b5108e700de",
		.b = "4e966d28e5a384975950696b18aa0718b53f1a0a641fa8a2"
		     "e9fcb89a8bac638e9bbba048d7a4e4c9d9e00255bcd27e15"
		     "1ed5a22faaee4ed40b2",
		.r = "53828448c58e59b5686c5476e392ff03d9dae93db7b59815"
		     "1f96ef490d88000fd6d10c13a825b8703818397738963c8d"
		     "a",
	},
	{
		.a = "73bf0bd98904a9426b98276ef8c4db6c0d88f798af42c6cb"
		     "7331923b42cbe1e8b837ddc5fd64b8dac401ef638f912193"
		     "5650d75095d4caa642a47f90bcd149baa1495",
		.b = "641c99db7076110f5f409a5e8a5cd5d95343fa00fec709b3"
		     "49cddd9099ce3df4d3b5f3b5c07492509704dbfdc6160bdc"
		     "5b4c095f434a3b4bf13",
		.r = "75940862ea8c907c0a285e99a363887db2396c2af94f7bc0"
		     "5ce010d4f05d7e06c915dad223de4bb487c40823b425bbcf"
		     "d",
	},
	{
		.a = "64051802505f6a22a4dbe92b6941a998f2769451f226b229"
		     "0f9f49527ddf48e7033748fbf6a5b8aacd8e04c60bd91cd4"
		     "3dc3ad4b071d3de011b96829c228e8092c1561d",
		.b = "5f966b3742e94b0026201eba0ad5c5df694ddc5ecca6c3ae"
		     "9ba7a607cb6d7cf251422130b756550124e026247fbae63b"
		     "4defeef4c767c3f5ecc83",
		.r = "7dcb81527d60b9836352d5ec3fb149d3e84ec3ee942f6c08"
		     "39916f157865b53b0baa40e767a6dc8003f184966d6aeb1d"
		     "a6f",
	},
	{
		.a = "1d61c7a7e3573a91d180cac4739adf48b5b74c72f7063038"
		     "e9c4d1765d7e4763124ab4031b640e689e25ccc6afddb4a4"
		     "fecbd6fef8527ed069a84ca9d2c9667274d2da7",
		.b = "212cf854e16a4cb27c31647c40475f2eb107a6088a3421c1"
		     "7dc6505e0174aa96a8c6b0927600088a2a528db26569563b"
		     "7076cb90679d4e744ad17",
		.r = "22263a57f0b9e118ee0dcbfce244d886f9703057ded0c813"
		     "639b667a40bbcc6c025fd10f4825d775591bb1db67c9ebb4"
		     "373",
	},
	{
		.a = "446a314d814702308b15e746cc333e10b1b7993052aa8b7f"
		     "d2ad435b4a8ea39cfa9d15e3b6b1cb6a16f0f809a4dd0b75"
		     "b9009b854f2d4bf42bc23",
		.b = "3fe2ef96281cc785d5f4b5e9682c08a7abc223fe9cf74dbf"
		     "97e7fea943970e45bee301db05528c0b82137f9b411041ca"
		     "90f39805b607cfa52f7b41f5e8602fc47c6bafb",
		.r = "45c015f28190be09b97094a8c59bc04f34239eda90ae2ce9"
		     "085dd1bfd5205b26d22f1b362290ae010f19cae6a6a02462"
		     "723",
	},
	{
		.a = "ed71c4d73211323dd48ee38cad0864c8179c3b7198701fd9"
		     "cdcd05ae0298a47985802c387324ad277c915d050d463173"
		     "d702e5a873ed31527a142fdcd086cc759dcde9",
		.b = "f99de4bea4fa9b029bb50dd02b77fb53d2d61403b1bf80f4"
		     "ed44cda0f665679f8601c385729f7d90c3216fc025c3a9df"
		     "9af035f5ab423c668fe5",
		.r = "134afb2bc229180a005cca6cd96115a480c26218b7512cb2"
		     "99ae6629e77e74344ba74968a67e25cce60bd0bb7f3d1aac"
		     "017",
	},
	{
		.a = "158f69f9bf4d1cae17a2b6a85e72a62272837ac452fe147d"
		     "d69b096bc68a8f8e51cefe6e7f7c8d38290c918ed82073a2"
		     "909cfbb48669d7f21a982",
		.b = "14d3d9149b52047e1ab18766533415bb6a4bc5f2f5e80285"
		     "c45dd1f73542089039a15d35162e37e7e258374cf9fed223"
		     "c1ce9c4fdc1d49985892edf4b6197bc96613ab6",
		.r = "170caa7f4eb0bb1c72138de7ab78d8ec7ca40bc8c5ad2962"
		     "af6edf075a6af208f5a05631ed2f9531a56e2141c3e5883d"
		     "cca",
	},
	{
		.a = "1d5716fc95b5ba0fa4a0998f836ae1dab1c194480d2064d6"
		     "aed4349bc6d65f591d9dd4a42411bb249965a64ab979e9fc"
		     "f227b29095c8a2ead7f6d",
		.b = "1c44c0622846ffdedea025144cc4e84aaaea8ce1dc5d1205"
		     "58164aa8b63aae1975fa5bbc42895bfa8f2e13dd8d1cfc5d"
		     "8ae30db81f9bf2d1d3ca75c9ebc6b9368dc085f",
		.r = "21e1d58ab5abf752819ed5cd86894f812415170bc16fd03f"
		     "5360e713602f25215e2e01000a690f52ddd48e802b790625"
		     "ee9",
	},
	{
		.a = "5f95d61d5ac8dfb6237f983a975254959e16a64099f629b1"
		     "73d59728ba69a78b82e1d0d6ac33bcd2ce1925da02d7c34f"
		     "dd9ca4fb2fbf2f91a1ffc",
		.b = "5250417c833f88eeb2891b033f077fd58a3d3d56afe8749f"
		     "688006fd5e3db3924ead2f9fcc610b86f41d570f66d0f9be"
		     "131d49c41f851a60a3a7cb0ab6117b469f46944",
		.r = "6b8d9606cd8926ebc739c6162125b4ce0dab87354c13b6a7"
		     "7075cd343d8732a5af16fc330fb1c600b6b36d47f66b6607"
		     "43c",
	},
	{
		.a = "59039282401a65c164713172f1ccf9c4d97afaada58d9566"
		     "7e7a7533e064bd6390f59a6b7a34bc2c8f19f36fcc5fb272"
		     "ca6b46fa91f64c9f20af505ecf0004def24cea6",
		.b = "58352c71242e461377106b457b8d778004c825641a53ccf7"
		     "d4697f821a074e21ec65cec7d9f8112177d76746d955e48f"
		     "008f4f513cf75ba3c0dba",
		.r = "65da4653de3b0fdcd2e5ba6ecbfa05efbff01288526b7fcc"
		     "15e5332828b3e4f19945d535e4599129c4ba06c4818d673a"
		     "5ea",
	},
	{
		.a = "3684d6032ff15dd42be955e32b0fdbb85f2a1c6c8bb00384"
		     "1dae0e00bf12df6984067c54bc5838bd4e8fac98ff0cd678"
		     "0ef1cae4037d800c62ba4d1",
		.b = "3411cd025265dd2da51495775758bff7813259693b9ae734"
		     "67b5b87febb41c27e9fd871150eb93e09fc8283c335432c6"
		     "3c9ad3a46cc81fed9be9e1c49d4ced031d200fe69",
		.r = "42513f708f738678c4fa764fa601af6e93c152b165ffc39d"
		     "f97146b14f57d39fec1033b7ba6040e142716f66659ca2fa"
		     "d972f",
	},
	{
		.a = "35f5d92e10e5b20213267b3e394a9a9c389095800cee76a0"
		     "0ee0dcb7fbfded16bff6b4d011ab39a680421248423f6825"
		     "76f202c5358d69e73c253d2a507e53fda2453f075",
		.b = "2d337d460643e294732a70f1970c4bab4ea720422a238027"
		     "1ee50e5bd897b18ce52a8820757e139dd515e187c4a24a9e"
		     "82aa1fc2e5a72f0f7a6eddf",
		.r = "37805ddf1106510d462969dd0901ed9ac685ceb2039519a2"
		     "2d3e18de0d5009876dcd66d24f1303facfba475c8e14af6b"
		     "19fb9",
	},
	{
		.a = "176df0a85bd4230ba16ce860963ad6cb1b05e1feea2ea3c1"
		     "a3ab916f039e3e1a27db3565ac5648b82717eef5c357cc51"
		     "694ca181b64ea28ded069df",
		.b = "15f221fbd53d4eb74ddc06ad6d768ee04e40dcf58bc1c084"
		     "235786fcbbdadd235181084d85938955813459912103d37e"
		     "964d685b4627e682d4491eb2c03bdb39e32039353",
		.r = "1a62b3f8b2d3cc0657433fa37f6521c051eb5eae3ecaec7c"
		     "763d5095a346a60cf3470357626a1e074ec0358e382c2899"
		     "21dc7",
	},
	{
		.a = "4e6a76eba33ff0050eae889e896f01022b3e97d8d3d79bf3"
		     "ef4aa0a7e3c47e1d260082fdf5cc48e7a4fba332f2a1bf6b"
		     "2f865d5b1d22d4252ad556e",
		.b = "4251261d1cfcbd60e29b0159cdbde3ef593f14855e260e1d"
		     "810dfa3b7965644c8294f03b39c69587808b0d5d67bf359d"
		     "059caf99067305486e0d9399eff72f285b5da1a02",
		.r = "55daf1aecc86dbd8dc1bc1e39cfd601e24895032894b1b4e"
		     "aad0154972a597cb8b0d836d57c634962767c053c27576c0"
		     "a04ae",
	},
	{
		.a = "a7e3063f253529bf049b25d65650adb1585fa551103b71ad"
		     "bc022b3a897b7549d658795d8e6561b2222d76da2243fc2b"
		     "42d10dad8bab28e5265804",
		.b = "ac5f34ac9bf20f064fa36b457d77769035a4bfe6702e6b42"
		     "edabe0a2bef03516d463a04bad0e57d6f2ae8f43050292b7"
		     "4d54e24c40a6fcb02c20bf699cbb4c213f4e60d4",
		.r = "bab2f490dac73cb3fc7839d65ab4eeb93f5a333dae6f326b"
		     "c13de7cf18fb94acaccb915da4d45c0c51f7b902360eab63"
		     "4b94",
	},
	{
		.a = "4046e1893473d68e42c580d2533b7e34db79b16f9d859ac2"
		     "3dbab1998300ce578e0b0f0ce4e907d53d7f682e23d37dc2"
		     "147c451ae0b00cd68fbded75ab158fec892149847",
		.b = "319d4ac19ff7a710f7e30b17f732dd5908ea93167aaf1734"
		     "edd540c438fbf91d41f1c65d1a46c6dc5b13eeb7dbc968d4"
		     "12c249f762691a511f07b15",
		.r = "412458da91a0a1521fd0d1c22b7c053b814fab4df3e87ba6"
		     "ae21dc22d25b3bee7e475a3f48ecba135dc83868c6ddd25e"
		     "d58e9",
	},
	{
		.a = "1acc10bd504085b8432777bb6c271c9055c233cc21b8ce2d"
		     "6b470cdb6d1d0c74af0e1695442da2081d9492d35770cccc"
		     "2d1731e002cdcb45ee45921",
		.b = "1b4f37bd77e0a7b314f592443f59ea10de1dbf0445bc6bed"
		     "128dae391f8516186ee45b516ca3475fb3ea11116a34f2ee"
		     "850a74e0ba20fdef9ee45588c3ca0ef127d0489af",
		.r = "1d9141432a729a9b3aaa82d0503baa91568654af70cd4363"
		     "0355718ca82f62ee6f14770f10c6437bc42d1854c6eeccb5"
		     "e36ed",
	},
	{
		.a = "60b82f6346f5e1ff60a0176b471ce808fbf43358a16d736b"
		     "db3219f3f5f9a46a996122b2f2784c50121dd93596279b2d"
		     "6a2a07713e677ea4c9419005a6d986bcf3a9ce0bf",
		.b = "74939f572534c6a8436abfddbf00ae0c33a465675989946a"
		     "474da3c695fba4c6490478f37b3d6e5c76e590662d87dcf0"
		     "14ac17b13cbe07199cd3ba5",
		.r = "7e8091f648a47ce00f184882ab10932efe8967447807c941"
		     "0b82727bb25620adb9fb91c58bd701cb50a444af3deda2ac"
		     "cc4f1",
	},
	{
		.a = "5041a12256efd3559135a980a600dd8c71d33bc55b01475c"
		     "2d8516d2d606962efd8b8b4536a96f9b9a0e092f275b8652"
		     "c5ac60f2c8956d6fdc821e9fe480061b3b9d5e2dce4",
		.b = "3f8ba54304ec5f724fe2f8161a4a15eeb203ef9b96c9c3dc"
		     "ad5769b15ff60240643b6b3e6dffb1f57e210e794a46d510"
		     "aaa4243762085a3e6d2e8ddf4",
		.r = "53f22b42f1cfb21f48afc31b3ef5e63f16117906d9d5619f"
		     "3b9e4425d35fc00588d23493defc3b3c741b57f0e3925105"
		     "55a1934",
	},
	{
		.a = "482592525884dcfa5a3c7d1ef4ee019ee36b74efa8760e03"
		     "b8c9c25f73d11c2b0eefb6a4c1eb054d6cab50608dc46180"
		     "cffe442c5028a444591cbbe4fbd7a3a8a7e9af7aec6",
		.b = "48068b5e29280df0aa9946c85ac651eb99129e56460fcb80"
		     "3404cf2aba590a80f27862bac4f5dfc3b53f705efc47d5b4"
		     "301b11b24aff938ef1b2a6dce",
		.r = "54d661714aa606419e3c4a493fac4758d55a4de6d0ae8669"
		     "d539a1867447073a821c4f8ead9730cf1eca56bd3913143a"
		     "b8cacce",
	},
	{
		.a = "5baf6c4efe7c773afef12ce9e45f367cf462bc8677b90154"
		     "9df1f106ad7a59e79d3407532fd4e40792ca94ea8614a51a"
		     "da0f04042588aaa21f71a15ac747a583753ecee0193",
		.b = "4fec301df38963b0df53ae7456fc3241d338fb0ebc719c4c"
		     "b6590ed2930885854f03c0bbe83ccc313ce7a40819957c71"
		     "1dca992576432c20528369c33",
		.r = "5d49d2348ebd7e89402f54b05af16b6b25834d1d39c96f89"
		     "cb908d3dbd18f80edd0b2d1b11f45e3eec077694101e855d"
		     "49cbd11",
	},
	{
		.a = "2a2b63ed2a72e6759bc1cd7f47e1431dd871bfb226ed3aa8"
		     "8f7b1b17abcb08dbb3098d12b5e3e58d06963a6a9519bbce"
		     "c6528ef1a45476623662a2e11",
		.b = "2c46b323b913734721878e9a9898953631956bb6c9a2b4ef"
		     "cedf8362065409753428aaea818f13649321f92611843a31"
		     "8a8a38a4840460119a08d217641560a1976b1d679c3",
		.r = "2c9003a62b4e3a5e43abc538038004d390a656d238b411e5"
		     "db21f7bcd7f7d79f52749cfeec14ece4baaa35aa51746e41"
		     "2024acf",
	},
	{
		.a = "1cbd299d709faa872571432fbc27fe7bd54d73f083d4f64c"
		     "c0bb273983fc6ca835d6d520d2e469609c785aa1d5052fb5"
		     "1dbe4d591c5493638b2e19eb6",
		.b = "1d41ded2310a8082543d3a458ab9d3a3b7673562b365d209"
		     "9a00f32b66a9ed87d95dcb3c9ba5a87a969d6f5b74a030c6"
		     "fa6faa5d03e7676a5855b91c46dee64e00f8658e75a",
		.r = "24425b1ed3cd100af596e80c6fd95404e71c5b4fe9aa7add"
		     "518c3577da1e13a1074e60eb08c7d8e13f02475430a2e63a"
		     "be88cb2",
	},
	{
		.a = "1ca04f2c09b9596c42931ba8ee02db210ff47fb418a30b57"
		     "9f7efd43eef13532cbd81ab417785c1683a69980735dad72"
		     "c50c14231b7a97b7b8bf95e67",
		.b = "1c808fde57e0c48e8ce127b7f2f1fbf45c48f1e750d05780"
		     "94af9ab929452340a01ff8bef56aaf980518b11b3657574c"
		     "9094992f00e9f865d7d20e2458bf72c7d301f84c3db",
		.r = "22a0e51cd1b018a42796cbef433a38feabbb38205518b6e3"
		     "1e354f8d88b3b5b6c639b774804a44a0fefdd1b95a8a0e55"
		     "39ec419",
	},
	{
		.a = "52c2829183621220af8de2c0ed3dc76a8a6dec5267236f0b"
		     "2a22a805d4bcb70a18c8ec62e535954c6aaab0d88425c7a9"
		     "0a8071419efef709b1fb521d0",
		.b = "44f44bcf15967aa6840213cae893971a501acda24258d714"
		     "8725ee3e032a422e5be1d69121a9f9ec7678b2de016ed626"
		     "f697ea4620534572e975ef10d3e91ce1bc9c8cb8890",
		.r = "5a8dc5f968e491cf69c4addbb173d8b7ab541ea69290fbf7"
		     "9d4c58c8dae65a97350ae0cf2888c8bf8d2ea686f99dcf74"
		     "c85e790",
	},
	{
		.a = "3d5c521fa9d5e9b5ce987fbd317fbe82fa1a22ba4195c3b7"
		     "d240fb96f7f64afa63609f0938cb071c148c762b02b78eca"
		     "0f102b0a00d8f340bfed713df",
		.b = "4486f157d285fc7fc4b3baf3d13557a20652a98199c15282"
		     "65c7b45f4368f968402871bf12e674f29521f75deda405f9"
		     "cb8f5ba0548a7fb9815c90b398fb11abf1979cab9fd",
		.r = "4ded51500c9fbfa781c67ad1f645744af2c37a43d3dbd701"
		     "c2226db2484f0d386e5c7a4bb721eb6e1d978862914bfff5"
		     "cd625bb",
	},
	{
		.a = "9b8e2ba3744f0e2ccc6c64489f5e636e128fe0f78e5c4eb0"
		     "ef7edfa0e873f642478f131b5d6cc48cd6bb6c8ef878f5df"
		     "edeaa3fccfed4206ea3cb46f49ae74a9",
		.b = "9cfb2f846b2252a87acdb0cb8f1edbc4c7f411d20dde77f0"
		     "a4e88be62b53b2fa3426f6a42eb8a937ab7280b1035ee3bd"
		     "7945fd0c7a17f8e5ae45947971d22425af6081491de906d5"
		     "003f",
		.r = "bfa5e6375e267d9bb687e7ac7213831fb8cfc3229e4d9d56"
		     "99ff6827e2d0aed224e70dfcbac063d07abb5666e0c0af9b"
		     "41bd0111f5a7",
	},
	{
		.a = "2ebadfb0f01dcb25f167580ed3b4b9d59419cf269d32072d"
		     "1147e8fcdfe4b8cd10039a6c8465c4bd9ac22009fc6b8454"
		     "9adb4b1ab384a9dacfcb558dd3363b32b",
		.b = "25e6acf6f10c62342188f297d83778472942056aa75d7e9f"
		     "6f807df6a48128a021d8da02ab76be79cb49cc238428e2a0"
		     "e588d8949ce6a9c02165ede046b0ca2888a4b370e656f936"
		     "e93ff",
		.r = "309ff1985b7e18d34be1be59b8c973ee2bd8a9fad57e956b"
		     "88a202f470119f705cd4fd39066a4398a482a9865141a336"
		     "7a267a3f8b069",
	},
	{
		.a = "6cceed6943720a5a324197e2cdaedad687e7adda7c0d243c"
		     "fcab73a56f2f12dadb559e79ef3aff46c19c0d56c6c61d1d"
		     "22fd91fe2e624a3d41aec8a461a3f6ed6",
		.b = "79a4c305491a52aa8f683102d1453741ccddcfad823e04ad"
		     "0420452aca76e439c065e7f4ed424e49d479148a6b3dd2b1"
		     "afd5d16bdedc4926156574651bfa621cd194054c92d2cee6"
		     "6929a",
		.r = "7fc3b98762a487c12434b4913191da9e4990f0451817ac25"
		     "3297905f6217caeaf7861560edbb94aabf59f26cda12c830"
		     "7a236dced98da",
	},
	{
		.a = "63294e7f411ed0934592fd1ce56aa40af1f598c5eb3313c5"
		     "a7d72d0ca7fbd9fe0a58d0c47af5afa04a239cf98d6a0bfa"
		     "6ef0d145f8b3724a8e454e6cc620e431d82aaf229ba34760"
		     "8980a",
		.b = "6fd3d8cdbb3230478da0959b4b373d71087dbe5424f610df"
		     "cd24f85d66a3d22f37da887118c1679676b9259ed5a8cd79"
		     "99bdc6342d98ffeedba5d89e6db6530d6",
		.r = "720e83b9e76c2f544657365583ec7cb2f9dabb4bc64d088a"
		     "212405a4589a86264a22d4b81e77cceb3ae25a24a29e45dc"
		     "b6b0015062c22",
	},
	{
		.a = "dbd562f8706bd4ff1e3a3e3634de519231e6088576e60e55"
		     "1f10ec0abe40f95eac51dabbc571f9878e35b17d89cbca5a"
		     "327d0cca3600389249a460e444a72233",
		.b = "ea1adae8f7712d0808c291c3d001201135640d4b182caf77"
		     "b9b7090ba51a4e990b1a7be13bf6a145f94082f0b80a77e9"
		     "85af920fddcad73de46572e5b50633bedd0f15dbfcd5977b"
		     "6ab9",
		.r = "f728f23f3351350ede514d09827e93cef6e262d00f191535"
		     "0847175ba8e3e5ba34037e8ff5229194ec74a96101efff4b"
		     "059c0f1c08c1",
	},
	{
		.a = "4e62aa8b1c23927db9db64c95eebfef251ae385904f5798c"
		     "ce13acb401fe2e49ba9ee86342c080b63639a5deaedd9ccd"
		     "dd327677a0bba4da0cdba26fc68a6e95636cd47b691f4771"
		     "d656d",
		.b = "4612daa4e7a3d3775dda8c03495ef240de334b3854747e51"
		     "982e4b4cc98559ad40e1b29b63586d71228a4e2f2df1f5bc"
		     "0ddc87c48f504f1d1fbb175017e817637",
		.r = "5067e4b9a1c71f5d2ac2b6efa944791b160e131bf05a479d"
		     "d33823e918b27df3a775377d670d882016dfc62ccfcfa8ef"
		     "a186fb8cbabfd",
	},
	{
		.a = "5782ea018a2d729011580674ecd0fd6f2fc08775d370e387"
		     "fbb229df3b46c3159b02abf84f71c2acd4dccd7672700800"
		     "63c0d9508588d8ea2ad006c9d17fa8005b12c08f05be86f6"
		     "ca475",
		.b = "6f1193e160ac77e6201a927a6ab19f46ebe565a1d972eeeb"
		     "d69352900a9ce5eeed923a15fe44ae410de225d3265bda50"
		     "f26956167cf7a5af19988bd90a2c905f1",
		.r = "7435426ff00b17e724ef65e2a274a0a3cd920fba3719c06b"
		     "cad61d485da7236efad8b31592dfdaf04bc5f75765134d03"
		     "c793017085499",
	},
	{
		.a = "6359ee8249c069ec298cbeb0fd9840e579ad9c8d35064207"
		     "90b5b93a9c9b08a54e6dbe18168fed3cd8a17718f9a2ad38"
		     "cd45d617d86f21479b6164faafa80d2e4e0fbf77f3e07977"
		     "7468",
		.b = "596ab00e4d9697fa70ec94c7e21ba6afde01e261e891ef58"
		     "081385ceb26ba02020cb58159670bd8f0cee21be73a53e6b"
		     "0a77b4e3ff40a92d7653653098009798",
		.r = "64126ee141f2ea8ac8a19530379ca283fa4b0f6133e78086"
		     "6374a49b319d99755321c42dba02a1483bfb0b0b82eb83fa"
		     "33cf2fbcf518",
	},
	{
		.a = "af2ebc45afff617620644e4f1b5234fdb933ab59534411cd"
		     "f4f80081e5048874684a94cf4197ea6ab18137cff1b88954"
		     "365da4b4ba63a9509800040528fc93ff3e",
		.b = "b4698b65c0b51cb00948d5d894fb2113333888a964d930f6"
		     "e477fb65c9f5880003ad205d69eea463e14a201cc31de4d9"
		     "c22e96730dfe9ef3bfeae662507787237ba7e0fca09e5a20"
		     "f7382e",
		.r = "c52c16502901190a09a84fadf933fa99be94b142196fcc3e"
		     "f685f9b81a7596acb73f1a6ad014dd5afc1156181f6cda66"
		     "64db4cf33994ca",
	},
	{
		.a = "32bd7040cc6866c2a5de1a578d8585a0fbcd75545158acb7"
		     "cc07c092537c777f6470d2c81f2b653a856ac731a0bc8e64"
		     "3eec2c37dcc2ef374724d50ac0f6e19d273b1430d2f7abaf"
		     "ae89c5c",
		.b = "36c361594992c8b0ad0b8ed3cbecf7aa5dba9f55b2064398"
		     "31081eecf83dd8e39e50020a919323994c2cf367e497787c"
		     "6d40ee718105088aa308955002745cc2dec",
		.r = "379d7395a1f2680686876adc77c0f44ca50d271b80740af9"
		     "6d697e5c7686a07490ae10bb0af3baab942873862e7cb801"
		     "4b57100bdf3d3cc",
	},
	{
		.a = "6ab2c1f8e181aadae9772ea977fcbd6c0e769e9b03cfa1ae"
		     "f1836a1a96114172a589429f6fb2c73c7b395be503caf3ca"
		     "9f808ada6a10ef799a654de7c18a42c5b37",
		.b = "5ed086dce77d1e41ab41f8c7236e1146a102aa2b9cf543d7"
		     "ce344b94fdf62a09796dffb761dec0a19c94881a26bd7c88"
		     "d7e9f2a41926aadd938cf8046646eb83889830bcfc588d41"
		     "f60a031",
		.r = "6fad8f4134b6057fa6c577eb469d2a1f0493a4123156fbbf"
		     "18b4b87e9611a511cf285e68f5c8da5f1167f9ad927756a3"
		     "74fb81caa603bff",
	},
	{
		.a = "4222b1f9292feabe85a201d7546c7fde474b882c72c68aa0"
		     "831767fb7502f6710d4dda3682c594f76c3a9e95798f3e29"
		     "c4f208b84a43bffd66a9bfe6bef7cbf3f0f",
		.b = "421306ddb3844b33b1316dc79bb75fd6e1fa0413fa3e97ae"
		     "e2416ee2be0677509caf92904e3f55ac4b59ecf2987a4963"
		     "ccdcce2aaf0934a574ee1214771a756b01109d1eb730506b"
		     "cb9f0a7",
		.r = "4274b685f1621bdc8e6b09a293c06555ba1af6aa8aa28ba7"
		     "2d830fbb7325fcfec6d4554ab1be06a06fd85ecc7462c70e"
		     "e80ad341b770047",
	},
	{
		.a = "16b17d8beb25b38e18784fcf2126b1cf548ac5adb1716a52"
		     "58374dff36496e1bf518d6cfb071430669646f0a0c5b59cb"
		     "8156dffa4f6df5248a7451113f6dad2ed8dafdce8e2f6c3b"
		     "b5ad9df",
		.b = "1b2b4e114eee4170d7ced5e894d921ac2550c00e62bda9eb"
		     "eedc48b5a5215f122a5aa75f04e5d4d83def46ac52476c48"
		     "a26963b7347aa71819f76516bb296340d51",
		.r = "1dd468531f7021964214cecf79e9e73e868b887291607e4e"
		     "9af36c03ef8c472054f4250ba8951fb8b4d53c88bc5edf81"
		     "ff47ae2170cbf57",
	},
	{
		.a = "323c6f3e02cb806d581580e1388a4e20013e0501fe70c982"
		     "c3afc8d5ff04856103bdd8a57e47e3293e6ad69e19bfa94d"
		     "4ba469e65a2424960e2341ec60090e2d1e542f8d19fc3301"
		     "a0bade",
		.b = "2f8677752eac018756090ab75170b4e5246c28cd6efcea6e"
		     "51ecf22a00530f5ce2015758419b23b0188785cffb278165"
		     "e92028c492e77f1d2a712f2df446bc99da",
		.r = "386d1d78f1223150f0b22e01de5dc7a14a606d00cf504478"
		     "694b0fd9df55e8d944dd9ff94f23670788f1944bf5376066"
		     "4f324cdf52ddb6",
	},
	{
		.a = "24e663278b6b76e6e472090021b0c3fcf10851a94e225d97"
		     "0e93344f952e0d0142fd106d1f12056e821993bc7a66f773"
		     "f04584139442affd723b798f52b63d4a21c",
		.b = "284b2b26790a09071efac26cfa788d9f3549ecf642064a6a"
		     "b1a1ed2c705c1d43c7c7690bb50a1538e0869915007676f9"
		     "43d4b39b24270275aa134de2b6be1012a2f7df067a794472"
		     "de278cc",
		.r = "3013a01d87cae7273ae913ce8317e62f6183fd9d1b0733d7"
		     "fa3eb629bb945ef0eb31187ddcfbee739467271edefd898d"
		     "4fdbc26ef1bfb0c",
	},
	{
		.a = "545d432d036aff1691fefe5327b30f2178a8960b86aee56a"
		     "522dcd14e7fc04a277b9502373e8df7457b8edc63b1dbfa6"
		     "050abfeef758e84b8e7d21bd38e9693a459e30c8b80af40c"
		     "50f1743",
		.b = "4c50a7e6879619d3cdc8283524d9677ae8b15654ed9b5d9e"
		     "adce653180782fca9d28f56717d9e27837b5309f8aec61cf"
		     "3ab8dd2ba67ef7143538a18b9b169a36e51",
		.r = "57692da8c61de8e02aa9230ced68c3566014563552b9995c"
		     "8db15082638a254b2dd6056b71faa4547cac5768006975c4"
		     "ea77311f7cc96b3",
	},
	{
		.a = "1896655e0b2717301b8e7fc93b174d5092a46ae2b8e574d4"
		     "6a48ffed75df363b1f3079969aa13087eafe8d910212f48f"
		     "77e294ab538eff2d40708984966be46937847",
		.b = "15f198b7de0ef20eb58877b470ce8b1595b082be9a8554cc"
		     "9936abae6774cec058b24270d77c6279342ce2d0584680de"
		     "c38e2b54fa2bb3b3c4fb840bd193dd3fafadc9447f1834d3"
		     "4d782c32d",
		.r = "1b60531fa4b7912ffc8396d21dafb03a6ce484d925d5c281"
		     "62d3e5ed871175e9e83299164e9682a34ad55d940bc83d32"
		     "a49b40068e0c93fdd",
	},
	{
		.a = "43d8780b46c862cdb9baf5bde283ddaddde8ccb24aafd634"
		     "855de1fbdc6f0a2f1b4d8a83f711120e703c3aa46cf8147c"
		     "d6f37da644737bdc28e8fb3d31968b7a7c3af",
		.b = "3f7167c05f5a6a976060da8bc4d8ea655cceae378978cf50"
		     "4cfaf93dacdd1efa1d17c1354a268f982d7eb8c52997b258"
		     "da5d91abd74b3d027830f99861707902df4e3dd60f7bd98f"
		     "5bd9526bb",
		.r = "4a07551954c425e696fafffa8f2aefcce1e29ead1ec506c6"
		     "198d085d1dd31c2af5cef4b29ad65d8c5fd22fb5062182f8"
		     "96b3bd77f8497a6d5",
	},
	{
		.a = "67ddb1941fdd37163a493e3da23050851964f45d8d753e5c"
		     "60c84ffb32e8d6dac19418385f90b43dbf1af8b71421329c"
		     "ed35e1476952d18ed2094c1ba974c976bbfa5",
		.b = "52c3fd899ea97862a0427c3b0030faeba715f30babd0c1e1"
		     "b0461ecfd4e4bb35622c51b507addd4c59e76467add7fabf"
		     "afb87c0f2f52128ec6a239195d67883cfd350e24fb4b7c13"
		     "7e1503815",
		.r = "6b2b63676ff005d7250de5ff66d64c29c5a7a7e72736602d"
		     "c4a9b8a097c986899e1177df736e0aa7cc75845a460886ce"
		     "a568d0c4d0d101d5b",
	},
	{
		.a = "57e1552f1cae68e8412770607b7c6d67d5f6ef1f17c4000a"
		     "045667d35bf1d040c99b590f2b669af84f2d642679a95ba2"
		     "7a46bcd290ab6e1044fedc02b04eeedd3b75",
		.b = "5cf62e9235bc95309dcbe49ba173500e7187bfc9be14044c"
		     "532e1aeacccfef483ac66e7e26564f8e2ce45bd7bdd391e7"
		     "52f17d3f5830b70edaea49ca72b98f2c37d2d508b644149f"
		     "a8fbebab",
		.r = "5cfdcbe753eb651090213d2a783f08190d48c662bde9940e"
		     "239fec164c9a8a81c9dd1051c9b1f75c9aafd1b698ce0729"
		     "e7e37e76a06c2e5d",
	},
	{
		.a = "20ea9c5dcb3087f695b27c4f815f189fe882ea70a8ebcef1"
		     "ae3b97577f24ae2656f0c01bf9fd33aa22d4c112aff8092d"
		     "6794d3b0fdda4108aadc1a9fe9bb61fcf28d9404446ca5a0"
		     "09d88df49",
		.b = "266088bdb989801f5b345876164751f85a8a564061dc2827"
		     "d9514c0b53776553b041ecd57151d9c55da2a89d3c7ebd36"
		     "b43d56723336d9beee5af50fd2230428e1213",
		.r = "2b1cf5b2dd1d1cd04cabdf12f70f16028b22a281d653955b"
		     "4ea0436f92b4c913317bcb862d7380b774e385191f881383"
		     "a7d956a8afa4239e3",
	},
	{
		.a = "541e1f20337202fd80059713e6cc475dd33792c9d92ad34c"
		     "1773f1585df2da12dafd3f8d24816e2ad0093aa31cfcc8da"
		     "fb249a5e5930ab4b9f6cb9c5547a86bc953bf165f1abe376"
		     "14006361",
		.b = "622f62ec0a012dec5507e76c7707206f4fb26f5020a0a2b6"
		     "371fe7aa171a09a4e60fb3174c8072910e826da3525dc6bf"
		     "af73356544b7623090d911d4b8991e29fe83",
		.r = "62c57e0dc260c7ee0602c25df9828b47e89be38550145da4"
		     "5432dd9c9a7b96278d71a606c9f3645fbb3517626b8f32ab"
		     "d0afe435f4ecb433",
	},
	{
		.a = "2cd4051f179794ef9ef8862d26ef9dab6d182c0cb8f7ebf5"
		     "639c9d3496fefeb0aba58a39c9cd23ac10be381a767249d1"
		     "237d2ec1ee8c8ee11f8eb1bdc98369eb72997",
		.b = "353fc536676f933bcc683cd1749f43c301f70c78c14061e1"
		     "96937152e2525308c58c0aeb1870c673d29dc587c10d85e5"
		     "a43f58ca37cd96c237bf47334f480d92d1548b001a773aef"
		     "7ce7bae6d",
		.r = "36d086207bdb311c6c7f40ff336994320618686e0a8ce908"
		     "d26977e9366d3fbe9d0c7531b6fce290b1054c3ac6b94ef8"
		     "a20c196ef6fe052a3",
	},
	{
		.a = "44255a3fed2be652f9a34a4f4abe0eb311c2e57a0f85343d"
		     "2d097d970782ef3dd61260410f5607b495103b0989ba4109"
		     "6da388c5cbb64a497e249adf4d23c71489c7b",
		.b = "4344b250abb87d531d7c6ec2665fefeb44623de4394fb82f"
		     "20b4af81c9b0f1f95a5e8024cbb26ec876dfeb740137d8e1"
		     "e22975a708e57a0bf009b09bb0855d871f9dc94dc4f2f05d"
		     "0a38cce7d",
		.r = "593263ec0796447aac52c4687c2c795e17288b03a9e90eba"
		     "0d7264d0759409064c33461d6126c433f2928ebab6635f0b"
		     "b7622d036e16ec2b1",
	},
	{
		.a = "441a895ec6bb6311365e27a6bbeefe3cccd5319e2477ab4a"
		     "114f31b7075dbbbd2fcbfc52bf596b95566179f8e3ba1b3d"
		     "d5d9fe612a117174b171e6d7d8a9dfcbecae0e0cfb2c1290"
		     "a45b38e1dd1",
		.b = "3508180db77ae987b951ad99f94eee1371709f6a9e5e1f54"
		     "e15af30d3305d1c03a859252fdbcc6616a362f44d2f9a107"
		     "54f38da41a8e01a65703de5bcb8ad1d99b34863",
		.r = "45665350710b2ce8936b54b6de415eeed16612f11bf2613c"
		     "a7e8c8a4ee4e6103206e45bef2d696cdfaca8c25dc15f7ed"
		     "3baaed29ae81af3be71",
	},
	{
		.a = "54da6ed1f70897bc3cd66600ddda1608fb2455d09321da8b"
		     "c87ccaaf82291a66101c2e6e0169057bbad4531983194bb9"
		     "fe1a67eaaf08583ff718bb86a8aa3e5db3d23d540bc9a2a4"
		     "3fa939e0dcf",
		.b = "595bbda1f517ee904529a9300cf6bb6baf27445355b55349"
		     "7c3e337b693b2ae8df63188698f84f1bb8e99b231f1541fe"
		     "7f1e55f471342543896860c3b4a6425cbedca2b",
		.r = "5e3d812cc1c469dc551de91f369368b93c94b947c69afc31"
		     "67db9e82486c42774559e218e3ded0b730a52a42b0fd768c"
		     "86b1791b5aebd5f5d31",
	},
	{
		.a = "440f0ad9199fbb9071aafb23f0bffd90cfa77cf928940edb"
		     "cb952194ce19bc173eb4de9dff4b962399851fbde873aa8a"
		     "6e4286ca648c2a42c264b74d58c931c319af9e2",
		.b = "5032c0da861e24e1004fff70b44b084acb03854d88f4e70e"
		     "f66639dd7d3a5cefdb421e4d660f903b2dd99f5ab4bb25ff"
		     "b7ecd3973c46d32557773f4c82095c7ca94edcfab23c3af2"
		     "afb486eba12",
		.r = "50ccbbd6ba5993da82e631824c52f869753f03590b296be3"
		     "c5f0ddf8a2390e7646e7066418551aef8e5185cbf70dd335"
		     "75a05f94cd88f56b282",
	},
	{
		.a = "3c875709bfe10d50babf283029ebd6254860eb2ea5fa96a0"
		     "68fc3466e32d7a242fccf602fd6dff901041973b7b57d046"
		     "52cf6a8ffcf6d46316609a82fa58b1fbeabd4bc",
		.b = "3d179ade5d386f2d58590c71984454bd7669fc25b059fdaa"
		     "42bf7e64075fc7f0204b7e90c0f9f70b727264a56a7bf1c3"
		     "27cbd33afbbba9ed1bd124d2fff205eb31a61b0487d28c9b"
		     "cab2df5390c",
		.r = "40bbe8a212039b7f67dd515ffbb394c114d743aae1137f74"
		     "28f14e2056daa74ed90088b1fd0a4349faf168d3c01a3cbb"
		     "c793d521ecd159d183c",
	},
	{
		.a = "15e3afb34c242493b52d1dc6dbb65f7678297c34bf9f17ba"
		     "e3d4496f08a44ea971aeb7584ac66255d153584fd7a07e4f"
		     "3b5b0597d2ae476b02012299f71c46825911f10f2daf7319"
		     "d74e2df221b",
		.b = "1468791ae811bf12455592f932b46f69a98e89ca3b333fcb"
		     "f287c8eb173c4971554753915de9f73719527343530eb6e5"
		     "f14409d8717b7088274efbbbf8891295a399beb",
		.r = "1ad99426793db7942559aa005fea7d65827a4ee96d8754b4"
		     "bb94b0220b7bb008c7b2bb5511b5022cc5e23e99be347164"
		     "5205cfcc19360830a47",
	},
	{
		.a = "41cf8df652d0fedc98d1a4a6ad6a8549028ca5359eac99a0"
		     "aaa8465e92c4006b23c3bfbe55da176cdd1e1c23e8b442cd"
		     "e4e0b04a796836ddb3fe7105f7863c8de9888efdf198c764"
		     "f100f554db1",
		.b = "4b441c2350d6c803ed1585f765774356132b84acc4d6e063"
		     "3767dd9f272080bee38af40fc0ccf1491caebd0d397056c0"
		     "1fa0a94cfa2c3ff15c8f26a8e2f88c2f5ce3547",
		.r = "52f009be59e82fbab92297bbd7abe8a43b252958092adda9"
		     "c4ef32c24e08c752bee3e6ba574d2607dc1359382f29ae40"
		     "96f0171290295eb388b",
	},
	{
		.a = "6956040bbdab4b9bc0655df3a2cae92548495d3a31d53580"
		     "f7d24be5f7abe168f127e4d3833e814b381a6dfbdca18f8d"
		     "941afa3bfcdd6e61495f05cbfe67140edf361e97527d7630"
		     "c8ac3f07d56",
		.b = "71c2d447d125d367b7c91f4cf019b6a0d7f9ae9637d4d75b"
		     "1eee393667904609a9e43bf618c25a1c6dd82fde20afec33"
		     "2507b73233987aa27bb7756ee0b6fc84d277d02",
		.r = "7338a44552a4ecad8029aeef2a4c222b111dd5f256c623fd"
		     "11c39599d070eb64fdd1ef4db2078002ccbdffc8ab2a555c"
		     "8fa570796b45effd6f2",
	},
	{
		.a = "66007f6a13f6e7086df1d1eb53423db4310683351648aea0"
		     "bca894901744467996594837aeb9d1b0652948e5ec7fb5f5"
		     "6bef90232ab1450f41c3929c311d8ce595b5df86df8584ff"
		     "9dbf988846d",
		.b = "602242d9a346d1ad7b3474efea6151eb28d899c695b05acf"
		     "626d984cc2646a8292b62037393907223c005b6035d5e80d"
		     "74a4a5ebbc1690e6c77c9b568281aa1b30a714d",
		.r = "685bb934a3edaafe9df57649a9678283591c89e169aa2f96"
		     "86bc78711d252015f17bf6865f697b09be0dda12bda0fd3a"
		     "2c505c94aced0b7a277",
	},
	{
		.a = "a1c8515f72e0513de3e7a560a4f25406bd7df2e39666ddf9"
		     "0cd339d9ad4620e1fcb1063cc965e3b5f74ca5cb47905e0f"
		     "1d02c1f339e8a11b41d9f7ee44202a6974b9675b731810",
		.b = "aa6a210674f66079f971d693322fb89c87df9c3746cc5b52"
		     "760a3d7d5f6591c96a29e22142cbcd4a3d4d2f56a44a3397"
		     "10436032b9b286d2506b217bbaf00bfb026c6bba10b0b847"
		     "f3903d68b7e082f949d0",
		.r = "ae558be6e081e845c1b7332505e65eefd673020d06271289"
		     "98a2a3cf0d3d6457e58d0d504dba4b956d18059a9073530a"
		     "84f9aefc01b78ee7c5f2b930",
	},
	{
		.a = "63a2fc46309a37f6dee99d2027c20815e0ae0c73845585b2"
		     "32cb4ecdf4670fb5f00df6165eb48aa2c589a8439e4531c0"
		     "43521257245446c94e826722a04d60a711fd262271f9213",
		.b = "5998b0bb3a3c937764cba78a0df0e9edb2b15f4233e2f594"
		     "ad195f1d76fe799d1ba7b98c4e983b78c1df913632096723"
		     "b18b796e296c1edbdc87fea7f543a1285e2cefbc4411c420"
		     "e512835e32f232de8767d",
		.r = "75fb0f8b649d3b211427a2b659490fe7c9e45aab6a032310"
		     "2f36ddb69bf6431160297ef61824517550deaf40751a65bc"
		     "db3edf29d723bb44e83090247",
	},
	{
		.a = "5f37cd5f95c9d3a33b10ea4fa514894e4ffc15dfe73766e8"
		     "f3f7789fdbe9c998ff8020eb56bf47027a7dfdd28ba2d289"
		     "c8d3879d782342a9a6321d4a182657831dac8569986585a",
		.b = "609a77e881ed16209a21be8c97ee2fb56a169827b53cbc86"
		     "c4902e4f97c7bcf903a1934aea4c8979120d1e9480f36890"
		     "a439f4686aa390654e074f821155b96623585c939030f1b7"
		     "185632ab71febf33a50fa",
		.r = "69e8c95b10f176e970088059cefa975139f63098fd1572b7"
		     "ee198569f40de0c0f165e60b5f2041e6cc3f6561fc26325f"
		     "0c73759241cfcae05f5dfc986",
	},
	{
		.a = "286933c1c00f2b660e3fe32ef6e83860802f604833db3b71"
		     "d3dfb4159832ddb6b8c523b4dd97a638153601a00ef0277d"
		     "afcd4672ee18270248d330370e2b3680fa6d45e2589f9737"
		     "a1598116771404cc0f6fa",
		.b = "236d98a863512a94fa2596fcd9f8208b3443fb57c21bdfe2"
		     "97057ce9d71060cc6d3d5cc8803ed478f08b96e1975e4cc7"
		     "8dcb4e51a0f815b758ac30f505da9d3bcec9bd95e58cc7e",
		.r = "28da8edc8852c39e8ecd24e02e049bf949018e6cbf56da50"
		     "106cd851f68aa25eb010e5e07f3bc0890d5437bfae851246"
		     "69d2272c6c86d470c54b558f2",
	},
	{
		.a = "a1f7a323d50b847d125780718fba3e18dc0d27dedd4bd564"
		     "1a1e77d4f135ba301d9e35e0a64636e02beccd74dcfe7dbb"
		     "aa887e81d70cfb20cb29b7f01388cf03574bee3b662c06e1"
		     "729b24b3661baf9897c5",
		.b = "b69a7319b587f4ddfbae74df4f9150d294378dcf72daad7a"
		     "1457f22a0d3d06f440b336e5235d01ad0d5701fb89bcd32d"
		     "91dc6619453ff97efec1fad9327b75d611d2f5d6e92b21",
		.r = "bfbc64ece94c37370bfe82d8e6a0251d01aa25ca8a24eb7a"
		     "c2a46436bfb1bed25bb7461e33dbca024b5630b642566abb"
		     "9e9885e376ac5e2918370fbf",
	},
	{
		.a = "580c74733e7398fc4f93a09607019a42520d56a192197917"
		     "461fb30901f0d1cc51e8ad0fc811087193d99df741275886"
		     "5f46e78373f104531a3847bf474b28f56a5676c958768394"
		     "54634afe2973cc9ffbc3b",
		.b = "4f616036bea3ee473e1d48729f0d8d0a1f6eff21ad44737a"
		     "9050b6e974fe2d83593025c34f69e95285631e3e18b50962"
		     "f85f2966a77975cfc0850bc2859fa595f563def7e253495",
		.r = "5c5adfa84bfefd45bd3de3e20e061559bae05d2abd5fe44f"
		     "cc33068bf024f905f4898bef8fe95cb454d42a0bae6d7113"
		     "ef5cee3dcdc6918271af159ef",
	},
	{
		.a = "698290f68e55de40eebbeffdb5e85ee1c90b9b26bfde16b7"
		     "e57f6d49372d28b452b740cc0151ad03f4a4cdc620d4ecc6"
		     "ca4801532302aa6ad15e95f03ee47b364c51025560b3e4e",
		.b = "7c154bd23d9802aaacb972cec50ee01e0cd238d99d52acdf"
		     "6e423fb2d25a8d43d48c190a98722bd1dc9ed362572552be"
		     "849525a75808ed1858301fde5ede16966aaee47b96840725"
		     "f0959af32aa06e9456b82",
		.r = "7f9b685607df0eb284c13f737512dc08fc285924763cca08"
		     "010271ac23a1e3beaa6bbe99d4b0123172315084fcf38bb8"
		     "05d095248901f3b0aac02d4c2",
	},
	{
		.a = "2d0508ff787ed77aae3eec5f3b653c16a61e5c2131484a6a"
		     "76c8f5c55a3fb8892cc944746d88d6df8a173934a3b08e33"
		     "ab661013ea4cac984e7702c480121cc0a61e9a84b8cb790",
		.b = "2d0d2615faa6a3270be7838e2113f003257db1301c1733b1"
		     "c0e7b6ecbfecd7df5025aca3ff20424029a0be85400b17a0"
		     "10e3d3bb2d68b869b8e3ca35e2ca578707f41ef94d1cd5f5"
		     "6576cc6585ed9e98f3d30",
		.r = "31f493348ec0de8a0b395765ec9e0a2c53fcfeb860b7f491"
		     "5b97d5501a7abf7a231d8b51b0a14d403a0c33614c2bf64e"
		     "871540ffbed0a5c71228c9af0",
	},
	{
		.a = "63358d19c542aeb26102faa596947f11a4b855390eca13a0"
		     "5de52db574a62508f6f6ceccc465d354129670206e28ed00"
		     "233fbcdfcc39d33d381dd2aa085d1213b543264757e01f43"
		     "5",
		.b = "5240f1b49d6adb2bd9e56f356ff3be942989b83a35fa88cb"
		     "4b1807725401d82353e90e44dcefb7d485e7c0e0e8761ddc"
		     "7fd082754b0c8ecb157f26efb878bf39db5afeee008bac7d"
		     "c04512e3d5cb1aa85edaa87",
		.r = "6d728eb98af7b75867fe8171099636251252e3aebdde0ab3"
		     "8e61da18162d7d3f9424cd4cf5952b6fca88ea01b52118ed"
		     "415123848a78f861a9c5ee89675",
	},
	{
		.a = "4b1a96b626c403e74398c96f775cdf543313d7e5c30f3200"
		     "da045ddef99606fb3e06a0cf102c315bbe544684bc97e0af"
		     "25c63e95e96cf79d488ff0c09d3bf476871bd2e16193193b"
		     "cea695c9f7b5506ca32c59b",
		.b = "53b13941fcaa37b2c6488181b60a22f77ff393613bd75ebf"
		     "9fd887e7f3a45c40b081d164f9cd694c96ef585623a75b94"
		     "4f9852d74b707d074d16ad35913035ecd50dbdce84def4c1"
		     "9",
		.r = "5ac217cda9a3e134d89a18453518a3e305b32161ff87275a"
		     "c038066f3bdfc755cd27b5096fd0b467ad050f2fc0b621cc"
		     "e0a5ba13cc43066bdfcebcafee3",
	},
	{
		.a = "2e9c3add5fbc37e68c6ecb0d69893a6230c96097d703c6f6"
		     "44ac81f78853e7fe1c153e31c332515363e1fc3d116103b2"
		     "1edabf3f9267a2a8b859256acbdb5001bc81bec6e75817d6"
		     "a",
		.b = "28ab95367724b0d6b25899eddf97bff39af736f56ec36b58"
		     "8b2c79d15d0f7b83e6c002c8ab93319a004752b9ae65edbb"
		     "14ec62070a24b1d7c982da5e8f278e4ab4f5ee794777a0ba"
		     "d3251551c42e9dec1613d96",
		.r = "35bae327800c8ab846e8a3ff9e2065d49843562e1053b305"
		     "419c722fdfc60facd8b96b361497ea6208c1e113733f94e3"
		     "dfade4850b027c4e29361acf1fe",
	},
	{
		.a = "28d49ba94cec96bcd7c4c4b648fa02fc964253c91b3f8055"
		     "9bd301eb085239e0bb81d27a0930f67d31da8e0800fe00c3"
		     "110206ae5510c198c7ca8455e81cbbfe2da668a94275b84a"
		     "9a7622cae01b9218fd38665",
		.b = "25fa6fb38d020d435dab152bc5f17d9ce691fe72d6c02047"
		     "464d2812a51728a5b021a1dccb8699c18b2a96ae7ba7905f"
		     "c03926debba4c1832cec97b05ecd3d1810c7249d15a82dbc"
		     "d",
		.r = "2b4f6fe0b050b6b25d1c45cc405c1b826fae754bdf2ebdfa"
		     "67796176e627df6ba640e8a1b423af7bb4b398b0a90a6dbc"
		     "7188f88814fcab190921a455087",
	},
	{
		.a = "211a642e3def54b03888b99bbd5ba647232038dc9a570283"
		     "1018dfe3754088d6698ef224d63545c0605abf884e979f6d"
		     "45135895794fccae82d2cd3701059f621d9f7dad15e1f226"
		     "d6730f64b83b88214ae4b79",
		.b = "2224220d00b674d4c20d71b7c3f3eb4c0477cb760add9e1d"
		     "9a142f7fbbfa6f15103365902c5b9f71cddef2f7e73a7ba3"
		     "6c4cf6febca1e8164c4244af2def72c61c73b7ee84d2668e"
		     "3",
		.r = "280c8c13bfe857fa3522cbb70d5c04f0d998a1e8847dda48"
		     "f7de2a9387a2a2c25b731e5f4750e8337841b897c73618ac"
		     "e8869a75d1394fdfea8e773f2f9",
	},
	{
		.a = "4994b58f2a35736e189646223fe59b96081e41138a136f81"
		     "802d7260c251e9ef469f328dd791df126f4181a848ad08f5"
		     "9b0b6f6714b77387039be2e24b0a45f8b870db8a32e572e5"
		     "6",
		.b = "53dbd859b1980b52a856a182bcad446ba31e9e38c8f38239"
		     "9787ade90ce668230fc79d8c9ee37ac58b4ec70d09b2d96a"
		     "7dd434fe13950da2d5d89260fcde8437da73896f4e3edc91"
		     "9e7eaf24e68fb83cb3f13a6",
		.r = "5caf1894f182dbc7729b357e28a8485603bc4d4d73ca64e6"
		     "9dcdbb5ea37ab9534c5caed7896669fe36624011b29bc26c"
		     "e7fa14520e4fe6b9bf49c581b8e",
	},
	{
		.a = "4e9d6310d7f9107cd13932e8fd8d123529e7f1c1be039c77"
		     "7cebbc80fa688158c55fff8cdb7bdaaa605c234aff554b0a"
		     "76a93eb7aa6133a24e462260d6f407280f4e6b42978bc2b7"
		     "f99ee90a835fc4abaabe12a",
		.b = "566140ad10242b4c8661e010188b30f6bb119f13d91ba8d7"
		     "80b3d7027551d50d4dad1bd8a2a941f016f9949e67f63675"
		     "33ccf7badf855598b792c69c18f6e78dee8d310d42716dcb"
		     "a",
		.r = "635ebf35796abc2d75157ca5244cef21dd30f3de6d6a0b09"
		     "f540e25d28a4c24e8cbe0e8041015f818da03ab383ab48a3"
		     "f8bdd4cc724bbf01c05a076300a",
	},
	{
		.a = "3d5bbcf58d4674d72427fd547b4c3a04363549248d086c00"
		     "a6d7bbd4bcbde6d709db692514346c347096000f3819937c"
		     "29a251dd0f4709ea1823566ae73218ee7134bfb2dc4f4d71"
		     "6",
		.b = "384b1ee7d2f374c44125d327983f5616a1d3a980b0451f1b"
		     "53bc5297b5a9110b104b7785b3ae44efc4bc1f188dec1081"
		     "3ae93366f10e204e9aefa65923fc08baea8c8d8c9ec6dd16"
		     "c25ffe541b0666ad1983832",
		.r = "49b1f9b8a8be2063c6937252ad20c082a2baeffcf4495d51"
		     "875f49203a3c4e87df4e8f386d6ffea8f247a261be9d453d"
		     "be646ec243f44f3a86074a80b92",
	},
	{
		.a = "2f4f1d590e2344a8304c0da12ed20d320027447352a6c91f"
		     "be0910aa7b22176a52de2a743ee00c970af26d4d6b17ee65"
		     "b5733a6762c4a910a90e34b223852f1570217677e5391f3e"
		     "450",
		.b = "3a9055f97eb776d220bda9012bb25b2fb333d692b4a64df8"
		     "465e2b4c4e92937e0c2da20df98aef923f2ddf775e89392d"
		     "626759563dba5c9b9c0bd4e72c43840873322884687d4f82"
		     "c17e78e8f23047c7e486436b0",
		.r = "3eb0196e0434c7973e4184ed035a3dfb7b22537dffb54e7f"
		     "318d399e34318c098240e2b12490cc36587630abe98683d0"
		     "b525f6000cd29faf2311d047d7ff0",
	},
	{
		.a = "362e74a668ad43cc6a7b5205fcd03142a012aef0145b20a0"
		     "13e57019ecfc0d8dd82320991fb55114ea5a355c69220410"
		     "3a5ea3c17763bd095098e16c16a778b37e312bcd0542dad9"
		     "f944c97cabea1e4c4b8253a67",
		.b = "3349ab6445ba4fbf835b4acb058afcef4c4cec07fea01935"
		     "dd99708e29f6929c63c7403cdd665b055f066e7a32d27cb9"
		     "45af3db650525bbe6cce328e48c7df688fc671f9dec68e8b"
		     "633",
		.r = "4142e2681e3d2a70ba3651a484d2526af26d8e9945ed1ae2"
		     "2a6a6edb8ef6efd3623b3fb972e992ff29c8b0ed34c69259"
		     "c8a3973f06b667306ed2fadd6825f",
	},
	{
		.a = "6259d0352e249d1288eb6321e88fa4c2ff6372e6bdcd744e"
		     "75175953185e3d04b315c6cdea3e443bc0d07236b2b545d6"
		     "c02740fb1055d6cb8dcedd7fd603ef0a58204de4f717c737"
		     "8d71ffd215ae50c226585ef9d",
		.b = "6451bf96dfda1c3624bc3face722a88fe80cdd859c9fa6bf"
		     "5d7aaf8017ddf75c53e1542c4c18b436abd6585bcdc20c91"
		     "34a227cd3d758c2d715be9762d2b310d8e302ba7802ac6bb"
		     "2d9",
		.r = "6edc27a1e26484474f1b8989e30fb334c40fef25b5b0388b"
		     "8984d36d7376f0ca903572b6bbaa658c4fcb06bdef18b8bf"
		     "231344e15d656b79cb9af46a2637d",
	},
	{
		.a = "29c1dd0b414e89c92df099bcc0368148bd9da74cba13ab75"
		     "69926f43c155bb73c224eff597c168da1a3482c1eb5fdacb"
		     "3525efe8cd760e75ae4f82a9927744711ad4aedd9678207a"
		     "5c3",
		.b = "2725f9ce0e76a58de38b514540a785b5f81b6b477cadc764"
		     "e451d4cfc672ffe8b45708674d8677355519b4fac1b2e205"
		     "2da08bb71e0302fb9b0539e5a089ec820934a7fab8a67f1a"
		     "cdfd284cb4d1a47a0fb6d323d",
		.r = "2a112329b5a954fabcd561681a5c706baff6c5bc63409d5e"
		     "75f92905b8556dd9a413c066d06de375e0673881e03d2de5"
		     "ba0ef77162eb00b6ff38b3a3d93b3",
	},
	{
		.a = "6cbbcc9a66966508df9163db9047397c268dca3bb912c7d0"
		     "0d6f648c1d7d1e17bb401428f1f35e228ddf31a1eeafa6a9"
		     "ad423d8a568cf53b8295f18445d5659bdca6546b3bf486d0"
		     "742",
		.b = "6d1caee6688280ba580c2be1f6a05e46e8adbcde74f0eb20"
		     "415b7e7a1d78392adb45a9568bd2d75154fed048aa63223c"
		     "f07dbe02b17e80ef1d849e2deb22355f564e950640dc123b"
		     "eca95514b77db9569a1fb8742",
		.r = "7b73d174ad87c7f6929c0ff8cfdd46216ca8b52d9af14965"
		     "372989b353d9ece7225447637beebf9a3b4dc5c55fedb01c"
		     "462c9869cffda467450d5bfaa6e02",
	},
	{
		.a = "2ffb47c34f03a353a71fcd0407c23a0907fd8f9699e6a79d"
		     "4c498c7b7c21635da7dd9c6bb1b6c2ca7a1a9a9f8cccad9f"
		     "df7e9692298db418bdce2d0a0b2eef85b0a8bf2e669443e1"
		     "977",
		.b = "3167566cc4a6b4394bbfa428b59bfbd481c89f0297510a00"
		     "06f8525bb254618d0f9fb5a7d7b6896118f084ee67e904eb"
		     "7e058353ccc8284802dc4e4be387aa9e51fc4f037b800de9"
		     "a134c5d3718d8b357cecee8f7",
		.r = "34a0e93f4d59795393a404dfc5be097ef284c02b3f431e03"
		     "2850144f7b9f182a1968f0419b62897dfd68bc556da82d7d"
		     "4bee27efa7441a57f10ad3a969b7d",
	},
	{
		.a = "2b5b70d4b8acaa1efd052d536b2011fd6f9e4537cf137ead"
		     "5ea8e5a9290e1eeb798184d6a03ded2c300b1a2888fa0953"
		     "31c19b46ab58c131831664120d5ecdb508617499b43d2e84"
		     "48b239e907af8310e551fc9ca",
		.b = "2e28a7ae1bb29b3c58a9958f4861eaff3ed846ac0fa1a97e"
		     "c413e437c8da9968de78b2c3d7e8ebedf359de341562f243"
		     "c4c4b2307af6f8b8783d1ea71ae86aab49b2da6c95e58aee"
		     "80e",
		.r = "2f731e7066df73cdfafff2f0c05435593fd49aa6c57ad125"
		     "1927ceabf03a4a19f97102ac81607b8448d74ea340017b4f"
		     "e1b2b8a3428066cc7a9163fc774e2",
	},
	{
		.a = "1f502837bbe73b0fea089bba9cc9951db97ce193168acb55"
		     "c9d10465524feb504e1a07b0b084e1b43e8a638cf3072b70"
		     "4d134b6f3922918979651ce87aadb83d84de661569ffb63d"
		     "44ce00e1b0b0d2e95a917fde4",
		.b = "2023c2547a2c3c4344046ef7fca206ddaf19f348747d41ff"
		     "7631592d81059726324e90a8bff168f5ee920f6a39dbcfe0"
		     "52efe5507a45ab92134124da25f3e13c7addd6ff6c18c6d7"
		     "a54",
		.r = "286972b401a798a475640cdf1f12a08ef72faa75eb2b9560"
		     "db47d7c9c6ef3a7a509bf1abde076f599cee0fefc031c043"
		     "115427e8c030205f469879ec18ffc",
	},
	{
		.a = "2601371a98307e3e57ff5dce30264ff73dc3597a1c10d06e"
		     "7024e3a553455c77470eef23ea143a5baf1980439603d854"
		     "c854e0c6ab98dc517da6e52cbbde9821ab4f2669ca5cdda9"
		     "7c64",
		.b = "293f63a8d23b4a57972134d1fd5808e7cae2e69756ad8fcc"
		     "c5582950e50d39f860f1c68805c07bd56be1bb3bf42c3323"
		     "c78547a51f5e69bd52676c0d92d7e595d1ef6fd9bf0895ac"
		     "588f18ef54b6befc24a7ab4014",
		.r = "29986146c05bb389e166d704f15fe12df8dc9797512c5863"
		     "6c2a88c32dc781a1d8b2e494bf69a8fbfce90f29a7159cc6"
		     "fc5bddc6e867d456c8d9fdfcf3ad54",
	},
	{
		.a = "60d0b1fee7def86c82f63bd34fef4daf7342dc1b7a957874"
		     "bb74a7c2b468418a85a05bb320b87d1608ba5462d1655cde"
		     "b21131889a22f6f5a8bc80780af967f366c94fbb19fa2db1"
		     "020ee",
		.b = "6d006a52efe8d29184eae98fb77a073be1410cbd17149372"
		     "a8a58298cf94c39062914d7d9696b20b52d256639f0c1d5c"
		     "bfdcd3fb61c21c2dcdac746b927ddc8cf8ff4f4c40bfeeb3"
		     "b797e5440aa1040781859b9cf22",
		.r = "7cab7c05cbbd2434ada90d92153bcb0c2307d9cf9e97b183"
		     "925d0993cf15cd10c4a559f22e7a37569fdf2cbaf06a664e"
		     "295cb75ddceeb76aba71f5c6186c36e",
	},
	{
		.a = "6b65db8f217601560b67966fff62a85326a355b04362373c"
		     "b75b61bf72df96ccf564e776a9e260df527619eea83e2134"
		     "2a4aa260e21267633a335d5f1e423d57382715ed19757c55"
		     "6b8a6",
		.b = "6de2dc6f6d0ee1e9e51bc8f32191af47b123bc6b91fdaa82"
		     "4b541bfbadf38d5f0f24498378aa1e1a8c8c2f58acda2ae7"
		     "29d85b2760326806b9075293908cee6e7a6584fb9a48cf61"
		     "30645512e1ee3782fdcd5b44402",
		.r = "7686fcf9c12c8d09c554b47747cb85068829caa4cadf13f2"
		     "a6e51b1beebded15f29c3b5811c7a520d2789c8e38557d96"
		     "a942f083aed301fed8d55380993f01a",
	},
	{
		.a = "47eceb53ea1c6f9bd9710e5b441d7b8aae96074ee598100b"
		     "5fe1a6d1da97a34175009b0b70403a1bad9ced6a669c8989"
		     "43ecb37b7f1fa9b2f6961f8ba948e26b9b6348f135aad8df"
		     "49e24613b1d2564d0955c6d9b57",
		.b = "36a85b25a3519226e79420fa52ce09ab7bc251334b47a1b2"
		     "b5ba8e6c57e95440f4c29ea43944b0f7021c2997a8d9c0bd"
		     "ee01976d43e3b17c255adb3b2ce39cbe070190e995dda8ae"
		     "33d17",
		.r = "482139d04fb1265200bb6dac1053e701e2f380deadf3d879"
		     "fdb290ca71dc1be05acadc1cd92c859f5f102b2497e32821"
		     "4a8ade526ad3bd6f75b01dbb5221cbf",
	},
	{
		.a = "4e776b04968be7d5b36f76f639c69655c3ae7e6dc631dee2"
		     "2953a6b0ba71641ea0737f0d20d231d897efb5d6c6611607"
		     "d8ac4c2603c414ce2c050d7c5af793645114c239ae3e82b1"
		     "703314f365abfd17ebe304e3e02",
		.b = "40dcfedd9e92687615ca5ebae237e2c6518e81c2bcfe5a16"
		     "1dc4040e8ca08da86e8b13a2c7e81830ba4194bf1da4ade2"
		     "6574c799c59334978b1ea0b4fcd24835b78436b20a288f7e"
		     "3a662",
		.r = "539efa234b6ec2b3522f56fc3458eddc1f824c7ed5e69b61"
		     "ffef8dd721b9047b8e84dbba68c61496026b7b3225b52a37"
		     "7649853bf31996fe08cc1d5cc60df3e",
	},
	{
		.a = "6248530af91ff8a2512105605308371dd35ad4cbab53268d"
		     "f60a98f71b069570cea5f0d1627814906d78bcb2f0311d83"
		     "2da3747d865670c808d6e5561f9c57629a2a2fd0f97f9fe3"
		     "ef7b8",
		.b = "68d82bcea9af58edf598014eb89d362ab60796fb6cebcdb7"
		     "c1f362791026b855d2d53ff21e200c5a6f09f8f5988b81f5"
		     "e5ac6eeaff8d40645c2610357cb17d7aa92b841bc0f8a62a"
		     "88796f971a90df96c9fd8ed9568",
		.r = "71df33a8cd2b40bcc1cd88ab619f9be7ba598ee307e882d7"
		     "e3c3fe21006207d4027413375acde14895e4d6f531864398"
		     "df0700e19199e152c44a32312e1ada8",
	},
	{
		.a = "48a027241005fad8ad81dd613ec10404f19355fa0d70f838"
		     "df119556204fe6e37d2ca5691d0f6d1a46d2f60448960619"
		     "c542269e1cb159a8154976b01b7156ddb34644f94abfedd1"
		     "ecc90ce5baee6208587a79ef864",
		.b = "5e06838a5e561438cecc8b5050e7b76f2c0a709ad75bbef4"
		     "db8a6766e7d08f4147acd0eb24f838274731070dcbdb3fae"
		     "f6806e06a765d32cb0ad4b9bcf938dd420e99a0c1850959e"
		     "068ec",
		.r = "5ef23537d261c6701373d1b59b311b8d74aaab868c28c12f"
		     "c40c742d32f510516383527ba995526ca4ae14b1f0af8ec2"
		     "2f03db896320319fb5fd6f9aaad223c",
	},
	{
		.a = "24f02c53b3ceba7449b66b5b3fec9b0301c1ec94241f1a49"
		     "d6054987e8b15323f3e191b7a3bc2f58d3e5918fbfa2d6b0"
		     "a91a8b91f71070e42594fc4e9cb1ccbccadd0d39dd66ff6d"
		     "b67d7a5a0a4df91b5472bc00c2c",
		.b = "2203162a0ac7281aa4801c56bd345ee940a3c56e75786cc6"
		     "94495214fb92e6ed32ebecb645c832f2f9414425e14f875e"
		     "d25d9b90512d391bda492fba97acd268fc81eb68f6b3288f"
		     "b3cec",
		.r = "269cefdbbc38de2dfe75b1556db7819facd5afff70fe141b"
		     "0320907a0542e85582bcf724cd74a31dcca1f1e45158c048"
		     "7d15ff63cfaa19735b3fe2b794cb314",
	},
	{
		.a = "311daa474d338d72eba858a27a869ac50e27c00a98336ed0"
		     "79dc55a141ba10d4d2df9cce57c37fb623ab1acc3a908a3d"
		     "63bea88f2c88e9ea6291a03a983638d061ac4083990a489a"
		     "e8358314f98aa",
		.b = "2f2dc2784c526989ce90674a073b56e1c698b72f34e86014"
		     "f6a5bf3ff45373192ba3ecd94b643072bbc581795d5ebfd7"
		     "73f685dc72cf17b744c3f4b1c183a4c9b15ad83cc835e27c"
		     "0253e12637e50519f4d936ae525eb84aaee1e",
		.r = "359c38503bbc71801a6d09d4659fa3aee3eb9ba08aeae383"
		     "f283a91a563b27c075cd0b75a9eefe69ac1858f744cb0bdd"
		     "1275222460356f837598f3fc6db51b1b6bd56",
	},
	{
		.a = "4fc542ebb5dd54c2f75c4767b4693b59e46a7cddd3b1c034"
		     "92bd7c1ce9b99903189112669a122888ddfeba2abc2c5d84"
		     "0f6e6b3e652a3863265b618034cff081c55f0848082873fe"
		     "0860d6c458f32a5d090a56ceefc26d2f68c51",
		.b = "4927608f0f371e47def5bc8602aeee09b98d8ffcc22332cc"
		     "755714695efff915eef1edc8f852cf1db7ad046e235d32d3"
		     "d5e0905e7a56d1cca7fe5996b10b65e77dcdd9c10a9a9f92"
		     "9ef6a40e1f899",
		.r = "5c65d455c60362ee4771fa967eb6756a2c332262a0fb6edf"
		     "d5853b4d0422139aebbf60a87abcb1800b711dd6d3552a15"
		     "f657ef0f3d052a29a86c91feb98fc84c63e9f",
	},
	{
		.a = "420f9b7bc5385ff84114032b2fb8b6312a216fd86ea9426b"
		     "6b5ba622405b6bca3d38d41cc9892744d687e61bc7aa96ed"
		     "44cdd5f62e8f2ce900bb7bff4f3e3a3c40d19e47d7430c4f"
		     "b259d41806a8c",
		.b = "45e49fd63db64eff1699c8c4cb518a4ddb640a4687589635"
		     "f9f03bae7af06e1ab608a230be3e536e7ecc236af9faa31a"
		     "b1bd91103b4c382144bd0c8956b32a57bc7d9cdbe996d305"
		     "646dae29d1c1358eff8b509f8b9d80108e2fc",
		.r = "50ce5c547d5bead574e0e816d5165d55d07b9a30f9ff64e4"
		     "2e42333e3afc89deaef1b2f493b63877fdb17a37a6f5fec6"
		     "ae797d43d5ea9090b09f5bfd1ec2fe4fea8ec",
	},
	{
		.a = "44b8197d0642e796cd53c6bede2289a4b812c4a1b10b645e"
		     "819e8d079c41f6c14404f7565e2cf057a097395ecd789317"
		     "fdce049d6e19a4e4897df8c9af394bfadd61e2fd56b8cb34"
		     "04e1cbe933a63",
		.b = "40e078cf89ac3fb2c4810cae1b443b669f7c5166f7537fb2"
		     "223ff70700df746f8d618cb9a455d5875a56e73b600ecb97"
		     "212611f42b255753048c6a6ef47ba91702b7709d5e72cfb8"
		     "b045ed808916937f1b8dc634cecc95b94812b",
		.r = "4eed24653e77a9eca3fe2c1b63c3df278c0f4c0280985957"
		     "d6b004f66dcf6e39a9d74947d2af6ccffba2d7e3899b0076"
		     "ac2452b8e0270cbdfcf0649cf43a7d40b1975",
	},
	{
		.a = "76080bd59c509c12905beaadf568f83e5406fabaf03de198"
		     "0da2dbc4f7313d7d1fce50bab61e959611213fa50a348842"
		     "e98866853219b1aec55fecb4437418d218a9e159faa081da"
		     "d55340f6f4007",
		.b = "653a2d3e6cd9fe2a6682652fe58df01102ea6afd799ef20b"
		     "5e59eb9b258fc48397f33f796a2e8d5708cd1f2f1bb78e80"
		     "91343bdcd8df1cd53b04b451d131933031b8670f57ef7e6c"
		     "79cd965edd09286e9114030f23cecb2802b03",
		.r = "7625e3b21f93cac17b64759bbd6374c5b8f1794d42eb2140"
		     "169bcbff6f30c26727704a63f36c0af124f7c072337bd3eb"
		     "c1efe0deff13f498f7d52bdfd49e7d429c479",
	},
	{
		.a = "2165efdf206b9f3d284b1649b883fc4ebe19dd2eded07226"
		     "1ff33f63c76f0462fdf33151defc54c504fcc045ba6f7d8e"
		     "9c78e2ca866588e3260d831cb0a76f9574bb9c38b68f9f5a"
		     "7a8635ab69816cd357b8e9fe2a5e44aa0af72",
		.b = "26990c66e29b1fc2fa5a27bf7027255c2a9269dc8564dc31"
		     "0e0b1897ec76be18a36c8ff535107a71d401048286d881b1"
		     "457e952f083ba5b6eaf21ba928523d39e7b5add8b92460cf"
		     "588fba4b641b2",
		.r = "26f01c12ab3c84a7fd95926bbba5947df3f3a793c7bcb4d3"
		     "561546eb89e5204de6cf1cffd37a791bd0bc257beb0d4aad"
		     "2dc95704d0bbb53db71767911605798c464fa",
	},
	{
		.a = "4cd1da71ab1b7d0f0d2878d3d9afd4a65503f44ef1a5bb96"
		     "c672eee44baf3ba6dafb576338cdc58ce53dcef44adb96a6"
		     "916db259bbd6898e570b0cde7c104eb75bafec51d3afffb3"
		     "028b761e2ea3bbffc27446702564d43cac3e",
		.b = "59ee8b7e46d672cba41db23f9a553be294b9576ae9eed5f7"
		     "ef2219814e85d92374def56b34a26840f79e475227e68e52"
		     "e0cb9c1d4028a5c4aba40560bc60cedc9f74f0bd9733077a"
		     "c51c2d9b5c26",
		.r = "6051ff998a975b8d4afd44d3ff5991ea396ec2404262d25d"
		     "0bcbf143d4abf8e2e43a758dca2f8bf63e689429cedab262"
		     "1c82d299eb665aae5cffcf49887cb8ccc47a",
	},
	{
		.a = "3278327513e6660bf43bdd9c2f0503d1db14d1d011b20d15"
		     "035249dc68f402f2439f86e9f088299d6b4d72127f6710a3"
		     "5867445a93421a509087dbaea6cd8cdefb800116be932f54"
		     "838bddf1aef01",
		.b = "3261ecbf9169630196e0b0edcc37c13503bbd96689be7a8c"
		     "fad898fdb62ba588291aeaf08c010c81ed36873d0b489037"
		     "1005bf531376dbdf91b8fa0a00519326103a2d231c33fe26"
		     "561bbbdb2d5e8cc8319fd772cea67d866fc3b",
		.r = "421d0cd7cf2e36f148cab008dbe8dd131425d2e57c0c8dab"
		     "49446b570494e8f73508dcba47e2fd04ca795a1be21c756e"
		     "2eb72fcaef768ff7677b017e3bcce7af48a5f",
	},
	{
		.a = "3112ae572ed5c2bf9efb02d11203769de237edd54a7daeef"
		     "99a79f002eaf11c5f83ae6915b9b3903e7a1207d481900b1"
		     "cf7ef3e8bb0455a99d9ff56e2d44fe34a1b740f128aeaec5"
		     "7cda32fe1949c2459b0d0d8ab17886332129dfd",
		.b = "2a07dae423f1f9dd41d69fc5aff306e5bd3553f08d2d088d"
		     "4bac224caa1c0f00525b4b7661d611cbb2a121139177a2f2"
		     "97ea52e7bcb4bd1670ec031da4e641b2d73dc25b80e79f4d"
		     "7ddd94ba401dbb5",
		.r = "31da054252aff10a4cc955cacafd6a64a8a03e575c850b92"
		     "e7fb4eb591a887c424d59d77cfb9e7669ad27407f0d5ae53"
		     "5c097e281aa88744bcb5495f7614dee4f008169",
	},
	{
		.a = "5a99840534ed75cf3c48250eb21477027251fee776c4513f"
		     "162ab48f0fba91297c86e9181c55410dda0d2b9a9a88eb05"
		     "b1de88d5c0cebd9de8c30534c45e7a52c31b784d697b26b4"
		     "3d9ce287597559b",
		.b = "5d00668b79b842c749fc97f756bfd23c4d93e32b1e45f33c"
		     "03a75ba6856e557ea44b07e6d5614833d7432304bd8a2e5a"
		     "d0669a9653550ce3d8fe1720c41aa5c8ef1ba841c3dec653"
		     "d7e627eb6f73e00d753540d8a3a716e437896ab",
		.r = "76408d57c8d9ed78d6719c18ac1e5ceadbf9e440482b03a2"
		     "84ddcdbc2409f8c358fd1a68710632141ad8a53b6a56df02"
		     "ec6f344d7a329acb7183d42aa5f4908a15b03b9",
	},
	{
		.a = "1fff5661c3409cc5bb806df271119041576bf30437f579d3"
		     "febe68040124b4a8a80470c99831b91936544c540f59d1b0"
		     "f185d1ceb609219c05a922f30be429c30719c0bd8c332020"
		     "3bdb54f0da90f5cab3c2a90a341097369cd3fe3",
		.b = "209b21fda82c9e7b5a9fce721e69e740a6aaed5ab49b6404"
		     "0ef4ef69e3b8604cc01d73c5274d5400890666a90e30a969"
		     "0600793f66e12ce64a63b5f330289469ba1787ddf90601ca"
		     "5783beb15663d4f",
		.r = "20adb144f68ca15bf1b2c53e08928d98fafc2bc50a78ebd2"
		     "527b5311e76fc872b36b9e2b67fb3432e98b8dd02a1fab5a"
		     "36cf09a6c20e343ceadd377b1bc466c63439671",
	},
	{
		.a = "342ba5b58ce8c7bbd2e8e886bd9ded881c8974fa8ff330d5"
		     "370cedb0a15643f92441c822fd4ec520de49506b4a8fe3ee"
		     "a95e405acbf5e09421fe79e0b6808d98ea42433cc7835dbb"
		     "15d4a4b5813e851696d895cc37321edc016f1c4",
		.b = "3db942a5261eb2554d8c07f49c9b4f63d7836374c70e1728"
		     "3e698093c3b35f7b8551a1d8398937e121d9072049dcf8e7"
		     "0ba8b689350bbff993da177cce0d98eb7561da786618aa01"
		     "f92d5399af1af3c",
		.r = "439f3e885a5a4174c79b5f267480d8975e689bdce44724b4"
		     "fc035b5988e881119529a00aef3a25a46a7a8d3089b91651"
		     "7aded47b31ceec49fc12421e03b713436055f64",
	},
	{
		.a = "371e2bf2000f60628f208640c701ea4171d01828172d0c30"
		     "cde6c73e7751285ce32d3c7f4e3ec6898412ebb726dcc80f"
		     "687ca47b2e9d7a1767a6bd51c924e5a1ea5ad29b2cddec59"
		     "c832ab32ddae4b70a7811dd1c7800d8878ebc3d",
		.b = "428146e53ba9e513544ec840520e6a3bcea1a34f598b5890"
		     "32302576e99cd54a8467b43a5cadde2562021b8717a730f5"
		     "cc7c450d64d1bb070020a65b2b9bcc9ef4dc3358cd6c6397"
		     "3b3662b9ad977cd",
		.r = "45e7a46b06c582473292b42270e5d11cf5a4816cda22381e"
		     "d5adf05d8f071109f3d0aafd89da4fb012df63fb0bddf518"
		     "0e33d0d2d4b0f1a3ac2750c2219088e80e6fb95",
	},
	{
		.a = "174624f63c7f56165bd918816c6f9928e4a559e8ba011a9c"
		     "a3c2b3e54939622e8b54b92db303745a6e89f014f216ad7e"
		     "dcc6eab8c1178d1532831fb5c004a37064916b91cfa39b9e"
		     "5bc232b62bf369",
		.b = "1902db596f7070d5dc3df09f805f3008fb680fd8eb572775"
		     "d2f562b4a37f6cf500e67d5d680bff68cf0aa456269c99e5"
		     "92a10b713eb3a2d160f2fc3ad1b66a99e60c441671ef7afc"
		     "fd05380181525eb53b9a86043d4698b2a65f6d",
		.r = "1983b5ae26a2caf8c8e5f67ad020fbb72a97bc8c9a66c070"
		     "b76c857b84fa7679ac539b8478a937d0564d408f643659ca"
		     "5b6e3041eca6c166cd4dfe59acabd1304f2fc9",
	},
	{
		.a = "166ac1610f2feb02a26390da221b50c826931c98e94aa393"
		     "99415c85237f730e65c9c8d0b50043e96e62249e1fb39885"
		     "f4ab8cf2a9b77c1c8cfb95f501052da8e80450f0988967fb"
		     "4df550b692127ea82e10d168d3cb3715595a014",
		.b = "19488565e24ac7c11eeaf77144b3d95508203e931eb9b7e8"
		     "0639b92f0ff337fff07ed97b4722b7f8814d071c5fa09260"
		     "7453069aa9708c431cc4bc2209ae30db76db8f248c5a4592"
		     "76f2438d9a2de0c",
		.r = "1ac1ea1783caf51bf4ab0f40db6810354a0fa44e69770677"
		     "bf04602b02b519f64fbb150019cde0254bacfa365553afa2"
		     "1e1012fe4b5b4596fff33c84ff84cb9d4c047cc",
	},
	{
		.a = "5b75b6143a1c6cbcac489957ac2e43ceb1eaf389d6f40285"
		     "b167f0d6f01ef6dc680c42aa7f4b15c2b970b519cc2a748c"
		     "7d495d7dca200144f7b35490710d90f589c95bae6e2450b9"
		     "96d13d9cc1d1dac",
		.b = "5e9a81924c38ae2f348d6b8d72fe1efdd831a0244801108b"
		     "1f9c37218a167bbcd699ad7709a3a4a6fadd10a2977161f0"
		     "2f4097c4614c904ab0a9cbf6b212505995cd2c369b4494b0"
		     "4e5ba0367b9881ccfa59645b6abeca953699734",
		.r = "69d18f3862bc74145c45a988b81cf48e2cc03bfe96c34f9b"
		     "981012b15fd615c51d585a913340688db34ed8d52f4c52ca"
		     "945d58910fb63e927ca8058d88725ef986e0ec4",
	},
	{
		.a = "674701bcb8b7d1afcdee9861b2523a119593959acd7d16aa"
		     "ef2ad3509ec7e9cc60b91b833ade557d0276185244155ffc"
		     "9065a9341b281f6435b7d26d6caa6b39d475f919b5d8167d"
		     "048d1a5bb433da5bd0d4c5901b71d585c32047047",
		.b = "6215d41d790981f440660c4097750c75c0b6ef765b2fc08f"
		     "d7caed2fc6305e84d751d806a3b469ec3ea6825e8bfc18f9"
		     "acb12ad29d099ad6009a0baa13a2bbb1776a567ab705788c"
		     "555ff3c59cd6848f7",
		.r = "76a0f17a23014c2b2b4d06cd952086837c69c3da36bc8cee"
		     "d0d88a74e50dfefe4a8d294cc6ab52a5cee2859ee8ffb141"
		     "0dd1e4fe363035825401679aeab07ab247b67d431",
	},
	{
		.a = "2ad3d974fa2b64854fba3e82434a1df17390ffe49f984a77"
		     "ce7384a02e7fe7b4c77097187ba049699cfedd4d1372cd1a"
		     "a64f837235ca2f9bec822c822e9981ee9afc4ec4f666b37f"
		     "dba827c818306a34a5ec916857dfaf2b8b128d81a",
		.b = "3353e8614d8b3c5c86c76006ab8fb3f1f0973a57ebe08d81"
		     "93a6712a7d180c6ddb91ba324239e399bb910105bf011f48"
		     "f13a9c6c08a11f963cde4adf41ce4bb88ce2f395b11fe2ec"
		     "b19ceaf98d08b19fa",
		.r = "3848694f56c96a0867df3953f63d8b90a140c15b0922dc17"
		     "173002079a986e669a0f032ccae911a8aef52bea6abc0427"
		     "76d99aeb5cfa27d105bca7e2487d051f113e69782",
	},
	{
		.a = "5f6264c220d6716aaf0c345b02791a38ede3a0ad8a44bc08"
		     "6e2fcf001bfabae0028b2ffea988bf1c2328ff9b01524db1"
		     "659dcf2934c26be9c32205933f5334ff2220de1d7a8333b0"
		     "307ad05cd08b8bde423036e898e400c5e0cc887a0",
		.b = "551ff6ad295ae1bce11f72540ef81f4233d1fb688708c3f8"
		     "2c92695e9f57be321dfb98cb7ece06cee2a191470209cb39"
		     "737f9465747378a2b6a2c01ae0e92b9e5e6d954576f25abe"
		     "957f4ad86a8d73ea0",
		.r = "6b4c3708b48166f86d1701bfd9152d7455ac0b56024fe4d6"
		     "e947813c8c87d343a54a14459804fc951190a17134cee19a"
		     "1d6bcca81480ab030ff2c58f677976bbf96d2e4e0",
	},
	{
		.a = "29dc9a1cc77dad36cb857326121100f546dc63a624fd950a"
		     "5edda4361acc17af684faaf932a8a467233321a6966c3e61"
		     "29f490b98e2d463b9abcd4c34263042b7966953411e6ab95"
		     "4f3dabd1d94baf3",
		.b = "32e7ae042092a898c8de2d016f3c9d9d88d534a11fb0ff37"
		     "dac04057e7093ee68fc6a1a9f2639fe69959d82f0109bd6d"
		     "f470869db5feece762683943c506ee79c7544a56a0da307b"
		     "9a02ffec03ae0b4b2797a5699fda4f17853a12f",
		.r = "3376875790192ffc6c7981d34ce9a79e445ab96df93be276"
		     "a290745feef1f204cf826ab0029074ef42b7a7b207f78d40"
		     "fcf5c639a1fe2492cf0bdbcf69b6d0ce3bb05b3",
	},
	{
		.a = "1e0b1c983532babc965cdedc4aea20127d2110d1c6bf636e"
		     "b5384cb9a67059e3bab8ef56a28dc34630ca37fc126ed7b6"
		     "25fc1164f8dd06e79ac03cd6668dee32834e32abbe29e4b9"
		     "415c34f6b12407369",
		.b = "208f4ba8619b531371a3359236423c217565ad6261992978"
		     "42e96a79b0ad9bc89312da9aba13acc17c7cbd12ee3fc769"
		     "e2473be10228f38e666afaefe868a9a960cff9e20fdc047c"
		     "e44b85d26e523a5acef4507da9c145f335ab3521b",
		.r = "270744a30ad54fcc784ebb3ea9f8bfc50372c09ec5578eb5"
		     "8ff70118681efada3d75394a5ed973df0333a14573418ae2"
		     "42c5585b923bcfc6d10f1ee3fd1ca497553c7de6f",
	},
	{
		.a = "4a83938144f1565d1407f16a811c8fd217b594b210dbc259"
		     "459ec5fdf9769ee85878ebe617c386284285e7354898f5ed"
		     "2236d1f832c606e2e7598680c4d3a103730e32a700895f86"
		     "fa2ab0cf34af11b48",
		.b = "424ee13ea5c95ff7e99889e3912b217b97975227c303f1d1"
		     "57c7f8147e239cbcad6a3dd28ffe8315a60b566b326b2827"
		     "cb21f0da1acb02a8a094415fec55f6a76ca47d7443608703"
		     "21e4db7cd1449892b57ecc2c26d8efd6a6bfab978",
		.r = "4aaf2a59ebfbb0d69ab6e0b867762c3fe76c54757caecc33"
		     "3f2c4fc9d39edda28e1beb4bd7fdf35d27f8189bc8c76a7d"
		     "c1613c932b1b921fea1b1eaf11a8390cacc472b28",
	},
	{
		.a = "6c95db45eae9c3890c716013ac0e8c8093000c0030d6b7eb"
		     "d556e749fd6f253d31622c5b8db0ba41d1a0709e0ed5d2d3"
		     "1406d0cec72e7b78bb4a21c5480bdca95a648e8331a26ca8"
		     "5f67c442d9471860",
		.b = "7de0f00b2f156e38ab5549ff5ba31e16a6cb843823a97044"
		     "7abd7542db53e78e812b4eb58d16df6a614a07bb590d673a"
		     "7208ebb98a0fc80b070c55ecfb0a6590f530daddc5ded569"
		     "b80aae255b9cda0b6058c2971474106319df77e0",
		.r = "7e7ff05971d259d4c99403f85afb3236188d72bf350e2e2d"
		     "4c44be9b3a6c569ebf47e8d0dd55c6157a8846d49a1556fa"
		     "d3bb1e00b077bc2a01eab5a0790e0ffadffe39e0",
	},
	{
		.a = "4394fd57db60ab172f4c909cdb8f3ea434e2bc6adc1ba173"
		     "a087dda50d52ed530384197589a0a66dc3fa5c7e47fbbc77"
		     "bd1bf2064d70f6ae0ab33b8fab6beeb56c985baba93b6ee5"
		     "fe086c99c919e3986",
		.b = "4940242edd9807e4ec93d363187f29ec73623961e851a5f1"
		     "176e62cf1de9d47e95d43fefa3e6e05a7de8ef3eef1fbe0a"
		     "e9ac7bf200f8d5c9dd7eb3acca2f1e5245790065e319f456"
		     "419bb208665aa060009300b9197ffd6000d810876",
		.r = "5360c87629e74983d59adea5ec47b9a198b3c5613afc3926"
		     "b9ccdad58725f4e232b7b5f8963838fb1772338620ba9240"
		     "967ebfdb2e29aef289cfd0d50f7a7036081e42e92",
	},
	{
		.a = "61be17703b62e5adf51e2c388ee77758a80632adab7c3a0d"
		     "d56b86ce827242baeb8a2324490c3c1e1615dcf8518aa7c2"
		     "65983441c7542c896e71dd213fdd04be682f654308c05cec"
		     "3c14c8defff396874763f57687e54e32f4ce9d1434c",
		.b = "522d15c2681543b7bb50d64526e6032bebf8cd452cb06f5f"
		     "0105616cc681eee2d522a86f683cd675082de34844ad7836"
		     "4555c7d2b1e960ae02826cdb4d80937bbfcdfa59d0cdbf2b"
		     "4c9a96dc35b5ecbfbac",
		.r = "6534089f006b049c13da2253ee4057e2aac083ce0610de29"
		     "00b01eddd57f4ed13adda2146fbd37bbb443420a0e7a081a"
		     "015e82b9dc85dd6c2389ce55a139e96f6448a424e6c",
	},
	{
		.a = "b287e8ac4abad5500ebe7739682f6bcf324d1afb32d68b0f"
		     "fd0b689aecdcf61473947ab8cc84a0a8873fce752d7027d3"
		     "403ccc28e522aab65b993883cd781b68b7a7d20bbabbdf3e"
		     "93c02aab4ccc335e4fbff8135d7d55fb5fee349bc8",
		.b = "abf36a5b8c6d09a3db1ed4cc037cb32072fb0812652bebb4"
		     "e7f0e194494017958cce507f64f1270083cf4855c9ea85ed"
		     "cd2f99bcb43dcfdff5e65ff2f6b18878999a646e67b10871"
		     "d767a014cb39c06268",
		.r = "bcfa04e04d226734bb8794716a3a3424893b94724dd5e802"
		     "54adad60ee8cea7cf7e8d798aa1287462a43b9d337b89d2b"
		     "8e2dbda0efe829e8fd784d61f0e09df7cee9d9a108",
	},
	{
		.a = "5805113ca06a22d67bf70a022cb538560f1e48331209ec43"
		     "682117ee841f4b7c425f23ce05b9febfe1ceb76784f6ff8c"
		     "4022df8cf61625b95f02f5f8cbf9a24f885776ee0b10acfb"
		     "e9b6535ceaf6c592243",
		.b = "5e164dba2f2ef870b086ee79a8b37b80aaf4d26c1ab53c1f"
		     "3de828ddc22e1820f5a7857de133684e95d9c0487270ac90"
		     "66bea466dc67a70a6f6f22687f35c159314eef3d5c4ee0e2"
		     "f18f1b8a98f270a71bd45d44de71025b1dc966618d3",
		.r = "679f032203be069cd9d44729aecad54a5fd518f9582cf4be"
		     "a3ef0bd7e62dec0aa124369db4f68ff416700a6f052e83a1"
		     "aaf3dee6d31c8f34192a60eba7eb57c759686b53d3f",
	},
	{
		.a = "6133ca653e55b423bb47988e353d8500c668540f10efeafa"
		     "a08aa8b04894517b1a4ce8b0431c2a219dd6c738b878f3a3"
		     "ec8f55f975b24e3d56b42ac4b087200411afc3c6716402ac"
		     "9d05a3b5c099be9d073cea6b51264fab6b602c76efe",
		.b = "52bca44f91ee3f41b18fd6286b91463c00a50f7ac5131d76"
		     "ac1dcc7fd60340a3f55357a364be360744b8699a83a4ac31"
		     "a5b7a6f9d9bd35da094e2d20a3cfb9d21aa3b573013d8301"
		     "cb8cbad78c811e60f5e",
		.r = "61b7a3d5d104cc66dca872b821dfc45afc88f7e46010b538"
		     "83aff819e90d1406991b3e104c4842db52fe1bc97c8daf37"
		     "a0ae1aaf3faa02ab92645764cf9173160c80cde79ea",
	},
	{
		.a = "5ba6fdd7fbeccfe482b2f57cdac17d53d09a60bb76422fb4"
		     "22b4c7785ce7186c88bf33cba40afa8f74a2d66b195c7d3a"
		     "f49ffc436fffb2e32c4cdfb67253c1afa4898bcb5dcbe39e"
		     "e6288129c59cf16dc177d63d0bf02fe216c1fbeba9b",
		.b = "5a1e35a1fa9ca5d66244af81b6a239d1d7b94841e38413ad"
		     "35b47fafb2d7509a30011e6d7a64360d4ccdb9f86d00a939"
		     "4721575f8fe9e23306ffe3059ef99447d09fd00f5c795d38"
		     "36c1dc663834da34991",
		.r = "70d68f80fbeac749d6f0f718ba5a6bca7ca23dee2b7ee687"
		     "ba0e3be0f45941d0ee637b3e8406ea2aed4f1638bdb47f17"
		     "7beec21cab2e49d832d9bda5a9d3b62003d6429bcbf",
	},
	{
		.a = "58f54f7ea83715900bc82bd5fc183f926f0fef989159bab7"
		     "c3aa18fd186efd7fc79d2a58c0bbd8556c1fabb784975801"
		     "396089ffa82f59eff04a2557b532f99bd39e08f597e00d0c"
		     "c96eccf3618cc02ec57b592d4d019152b686bd84bac",
		.b = "4866326c48b8226bec5aae93bc3b3b3fdac0a60319730efa"
		     "7d9aa373da03f59159d3ccbeb478dbb5393fd91c6daaad8c"
		     "28dc0187a59448847ad960648b8fdfc14f785f09e423c8cb"
		     "1c6cc5862b4ec95a25c",
		.r = "5f4a880841ce22ffd67705b03d965c39517a79f8e8ab6669"
		     "482eef3966895a050c67cce16965f98cd53477aeac38a521"
		     "02a6274dba0f5d2f02f1d5f1dff5653db7ca109a9cc",
	},
	{
		.a = "6a0e2e61d2859ce130d11aff8a8a9cc0672d3cea86da2f31"
		     "4a6a656e13f4830826cd8a6044d1233f7a6d70cae35060ea"
		     "fb66ae062c7f4af5eff978da2c5c29421322ed65293dbe9e"
		     "1855cb2977bec2fbf20",
		.b = "594a63062ee8b0b61f0143ed9237906010c17e6d03cfb2fe"
		     "675f13edc7f8c54770f25dbb275f3236a8fa3bddc10c4fa8"
		     "fe0b704e6c4c0ee29929d2d49500a4254903350cdc72b02e"
		     "dddf83964b5c382b4f12571fc4741fd5b3ac9c24a20",
		.r = "6c4c28b2358329ee65678c92b6e9b31187d4ddd793f9af45"
		     "e780b723f63b82c287e2ab0fb06ccd173a9bec960667a47f"
		     "0ebd5a2ce8d1d0059500be32d67c8ab308e3e08fda0",
	},
	{
		.a = "b37bbec16572713e3cadbcbd2703c48673edde83d6ab5cc0"
		     "02c32dd693950f4c3965506c04cddcbd754f23245ce63f92"
		     "f5dcb926f40bbf08583de1a11047e0c2508b47d0896cc0cb"
		     "fbe031a5e9a148923e",
		.b = "e8cceb9061392e35fd652346312aff143d88fb156695e0a7"
		     "f7b84419ca35214374733457d32517d0cb7c16c31a10f002"
		     "d4f2bd2fd4ee4ded122f1e221dfc989d0c8dd66deb1ac49a"
		     "9199290905db12c064d3376805451b969ee424a59a",
		.r = "e9eb6a282f306af29a28df6d720469568183f136e58813e3"
		     "d61929b91f8538cf0f777fd5fb1672f0bdc4e59ccb9a4f79"
		     "16aabeba3101842a700fa4c08e758ec7169726d682",
	},
};

#define N_GCD_TESTS (sizeof(bn_gcd_tests) / sizeof(bn_gcd_tests[0]))

static int
bn_gcd_test(const struct gcd_test *testcase)
{
	BN_CTX *ctx;
	BIGNUM *a, *b, *want, *got;
	size_t i;
	int signs;
	int failed = 0;

	if ((ctx = BN_CTX_new()) == NULL)
		errx(1, "BN_CTX_new");

	BN_CTX_start(ctx);

	if ((a = BN_CTX_get(ctx)) == NULL)
		errx(1, "a = BN_CTX_get(ctx)");
	if ((b = BN_CTX_get(ctx)) == NULL)
		errx(1, "b = BN_CTX_get(ctx)");
	if ((want = BN_CTX_get(ctx)) == NULL)
		errx(1, "want = BN_CTX_get(ctx)");
	if ((got = BN_CTX_get(ctx)) == NULL)
		errx(1, "got = BN_CTX_get(ctx)");

	if (!BN_hex2bn(&a, testcase->a))
		errx(1, "a = hex2bn(%s)", testcase->a);
	if (!BN_hex2bn(&b, testcase->b))
		errx(1, "b = hex2bn(%s)", testcase->b);
	if (!BN_hex2bn(&want, testcase->r))
		errx(1, "want = hex2bn(%s)", testcase->r);

	for (i = 0; i < N_GCD_FN; i++) {
		for (signs = 0; signs < 4; signs++) {
			const struct gcd_test_fn *test = &gcd_fn[i];

			/* XXX - BN_gcd_ct(a, 0) divides by zero */
			if (test->fails_on_zero && BN_is_zero(b))
				continue;

			BN_set_negative(a, (signs >> 0) & 1);
			BN_set_negative(b, (signs >> 1) & 1);

			if (!test->fn(got, a, b, ctx)) {
				fprintf(stderr, "%s(%s, %s) failed\n",
				    test->name, testcase->a, testcase->b);
				goto err;
			}

			if (BN_cmp(got, want) != 0) {
				fprintf(stderr, "a: %s\n", testcase->a);
				fprintf(stderr, "b: %s\n", testcase->b);
				fprintf(stderr, "%s, i: %zu, signs %d, want %s, got ",
				    test->name, i, signs, testcase->r);
				BN_print_fp(stderr, got);
				fprintf(stderr, "\n");

				failed |= 1;
			}
		}
	}

 err:
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);

	return failed;
}

static int
run_bn_gcd_tests(void)
{
	size_t i;
	int failed = 0;

	for (i = 0; i < N_GCD_TESTS; i++)
		failed |= bn_gcd_test(&bn_gcd_tests[i]);

	return failed;
}

/*
 * This test uses the coprime factorization 2^(2k) - 1 = (2^k - 1) * (2^k + 1).
 */

static int
bn_binomial_gcd_test(const struct gcd_test_fn *test, int k, BIGNUM *a,
    BIGNUM *b, BN_CTX *ctx)
{
	BIGNUM *gcd;
	int shift, signs;
	int failed = 0;

	BN_CTX_start(ctx);

	if ((gcd = BN_CTX_get(ctx)) == NULL)
		errx(1, "%s: gcd = BN_CTX_get(ctx)", test->name);

	for (shift = 0; shift < 16; shift++) {
		for (signs = 0; signs < 4; signs++) {
			/* XXX - BN_gcd_ct(a, 0) divides by zero */
			if (test->fails_on_zero && BN_is_zero(b))
				continue;

			BN_set_negative(a, (signs >> 0) & 1);
			BN_set_negative(b, (signs >> 1) & 1);

			if (!test->fn(gcd, a, b, ctx)) {
				errx(1, "%s(), k: %d, shift: %d, signs %d\n",
				    test->name, k, shift, signs);
			}

			if (BN_is_negative(gcd)) {
				fprintf(stderr, "%s: negative gcd, "
				    "k: %d, shift: %d, signs %d\n",
				    test->name, k, shift, signs);
				failed |= 1;
			}

			if (BN_ucmp(gcd, b) != 0) {
				fprintf(stderr, "%s: BN_ucmp(gcd, b) failed, "
				    "k: %d, shift: %d, signs %d\n",
				    test->name, k, shift, signs);
				BN_print_fp(stderr, gcd);
				fprintf(stderr, "\n");
				BN_print_fp(stderr, b);
				fprintf(stderr, "\n");
				failed |= 1;
			}
		}

		if (!BN_lshift1(a, a))
			errx(1, "%s: BN_lshift1(a, a)", test->name);
		if (!BN_lshift1(b, b))
			errx(1, "%s: BN_lshift1(b, b)", test->name);
	}

	BN_CTX_end(ctx);

	return failed;
}

static int
run_bn_binomial_gcd_tests(void)
{
	BN_CTX *ctx;
	BIGNUM *a, *b;
	size_t i;
	int k;
	int failed = 0;

	if ((ctx = BN_CTX_new()) == NULL)
		errx(1, "%s: BN_CTX_new()", __func__);

	BN_CTX_start(ctx);

	if ((a = BN_CTX_get(ctx)) == NULL)
		errx(1, "%s: a = BN_CTX_get(ctx)", __func__);
	if ((b = BN_CTX_get(ctx)) == NULL)
		errx(1, "%s: b = BN_CTX_get(ctx)", __func__);

	for (i = 0; i < N_GCD_FN; i++) {
		const struct gcd_test_fn *test = &gcd_fn[i];

		for (k = 0; k < 400; k++) {
			BN_zero(a);
			BN_zero(b);

			/* a = 2^(2k) - 1 */
			if (!BN_set_bit(a, 2 * k))
				errx(1, "%s: BN_set_bit(a, 2 * k)", test->name);
			if (!BN_sub_word(a, 1))
				errx(1, "%s: BN_sub_word(a, 1)", test->name);

			/* b = 2^k - 1 */
			if (!BN_set_bit(b, k))
				errx(1, "%s: BN_set_bit(a, k)", test->name);
			if (!BN_sub_word(b, 1))
				errx(1, "%s: BN_sub_word(a, 1)", test->name);

			failed |= bn_binomial_gcd_test(test, k, a, b, ctx);

			BN_zero(a);
			BN_zero(b);

			/* a = 2^(2k) - 1 */
			if (!BN_set_bit(a, 2 * k))
				errx(1, "%s: BN_set_bit(a, 2 * k)", test->name);
			if (!BN_sub_word(a, 1))
				errx(1, "%s: BN_sub_word(a, 1)", test->name);

			/* b = 2^k + 1 */
			if (!BN_set_bit(b, k))
				errx(1, "%s: BN_set_bit(a, k)", test->name);
			if (!BN_add_word(b, 1))
				errx(1, "%s: BN_add_word(a, 1)", test->name);

			failed |= bn_binomial_gcd_test(test, k, a, b, ctx);
		}
	}

	BN_CTX_end(ctx);
	BN_CTX_free(ctx);

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= run_bn_gcd_tests();
	failed |= run_bn_binomial_gcd_tests();

	return failed;
}
