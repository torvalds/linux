/*	$OpenBSD: ecc_cdh.c,v 1.4 2024/12/24 18:32:31 tb Exp $ */

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

/*
 * ECC-CDH test vectors extracted from version 14.1 of Component-Testing#ECCCDH
 * https://csrc.nist.gov/Projects/Cryptographic-Algorithm-Validation-Program/
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/err.h>
#include <openssl/objects.h>

static const struct ecc_cdh_test {
	int nid;
	const char *peer_x;
	const char *peer_y;
	const char *priv;
	const char *pub_x;
	const char *pub_y;
	const char *want;
} ecc_cdh_tests[] = {
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "af33cd0629bc7e996320a3f40368f74de8704fa37b8fab69abaae280",
		.peer_y =
		    "882092ccbba7930f419a8a4f9bb16978bbc3838729992559a6f2e2d7",
		.priv =
		    "8346a60fc6f293ca5a0d2af68ba71d1dd389e5e40837942df3e43cbd",
		.pub_x =
		    "8de2e26adf72c582d6568ef638c4fd59b18da171bdf501f1d929e048",
		.pub_y =
		    "4a68a1c2b0fb22930d120555c1ece50ea98dea8407f71be36efac0de",
		.want =
		    "7d96f9a3bd3c05cf5cc37feb8b9d5209d5c2597464dec3e9983743e8",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "13bfcd4f8e9442393cab8fb46b9f0566c226b22b37076976f0617a46",
		.peer_y =
		    "eeb2427529b288c63c2f8963c1e473df2fca6caa90d52e2f8db56dd4",
		.priv =
		    "043cb216f4b72cdf7629d63720a54aee0c99eb32d74477dac0c2f73d",
		.pub_x =
		    "2f90f5c8eac9c7decdbb97b6c2f715ab725e4fe40fe6d746efbf4e1b",
		.pub_y =
		    "66897351454f927a309b269c5a6d31338be4c19a5acfc32cf656f45c",
		.want =
		    "ee93ce06b89ff72009e858c68eb708e7bc79ee0300f73bed69bbca09",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "756dd806b9d9c34d899691ecb45b771af468ec004486a0fdd283411e",
		.peer_y =
		    "4d02c2ca617bb2c5d9613f25dd72413d229fd2901513aa29504eeefb",
		.priv =
		    "5ad0dd6dbabb4f3c2ea5fe32e561b2ca55081486df2c7c15c9622b08",
		.pub_x =
		    "005bca45d793e7fe99a843704ed838315ab14a5f6277507e9bc37531",
		.pub_y =
		    "43e9d421e1486ae5893bfd23c210e5c140d7c6b1ada59d842c9a98de",
		.want =
		    "3fcc01e34d4449da2a974b23fc36f9566754259d39149790cfa1ebd3",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "0f537bf1c1122c55656d25e8aa8417e0b44b1526ae0523144f9921c4",
		.peer_y =
		    "f79b26d30e491a773696cc2c79b4f0596bc5b9eebaf394d162fb8684",
		.priv =
		    "0aa6ff55a5d820efcb4e7d10b845ea3c9f9bc5dff86106db85318e22",
		.pub_x =
		    "2f96754131e0968198aa78fbe8c201dc5f3581c792de487340d32448",
		.pub_y =
		    "61e8a5cd79615203b6d89e9496f9e236fe3b6be8731e743d615519c6",
		.want =
		    "49129628b23afcef48139a3f6f59ff5e9811aa746aa4ff33c24bb940",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "2b3631d2b06179b3174a100f7f57131eeea8947be0786c3dc64b2239",
		.peer_y =
		    "83de29ae3dad31adc0236c6de7f14561ca2ea083c5270c78a2e6cbc0",
		.priv =
		    "efe6e6e25affaf54c98d002abbc6328da159405a1b752e32dc23950a",
		.pub_x =
		    "355e962920bde043695f6bffb4b355c63da6f5de665ed46f2ec817e2",
		.pub_y =
		    "748e095368f62e1d364edd461719793b404adbdaacbcadd88922ff37",
		.want =
		    "fcdc69a40501d308a6839653a8f04309ec00233949522902ffa5eac6",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "4511403de29059f69a475c5a6a5f6cabed5d9f014436a8cb70a02338",
		.peer_y =
		    "7d2d1b62aa046df9340f9c37a087a06b32cf7f08a223f992812a828b",
		.priv =
		    "61cb2932524001e5e9eeed6df7d9c8935ee3322029edd7aa8acbfd51",
		.pub_x =
		    "d50e4adabfd989d7dbc7cf4052546cc7c447a97630436997ad4b9536",
		.pub_y =
		    "5bea503473c5eaef9552d42c40b1f2f7ca292733b255b9bbe1b12337",
		.want =
		    "827e9025cb62e0e837c596063f3b9b5a0f7afd8d8783200086d61ec1",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "314a0b26dd31c248845d7cc17b61cad4608259bed85a58d1f1ffd378",
		.peer_y =
		    "66e4b350352e119eecada382907f3619fd748ea73ae4899dfd496302",
		.priv =
		    "8c7ace347171f92def98d845475fc82e1d1496da81ee58f505b985fa",
		.pub_x =
		    "b1a8dcac89aca2799320b451df1c7ff4d97567abb68141c0d95fc2aa",
		.pub_y =
		    "3524950902b1510bdc987d860afc27ad871ceaea66935abd3c0a99a8",
		.want =
		    "335ba51228d94acbed851ca7821c801d5cb1c7975d7aa90a7159f8fa",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "abe6843beec2fd9e5fb64730d0be4d165438ce922ed75dd80b4603e5",
		.peer_y =
		    "6afe8673a96c4ba9900ad85995e631e436c6cc88a2c2b47b7c4886b8",
		.priv =
		    "382feb9b9ba10f189d99e71a89cdfe44cb554cec13a212840977fb68",
		.pub_x =
		    "abb6f1e3773ff8fc73aea2a0b107809ce70adcefed6e41fc5cb43045",
		.pub_y =
		    "a963897ae906c10a055eeadb97ffdd6f748d3e5621e5fff304e48ba7",
		.want =
		    "8c2e627594206b34f7356d3426eb3d79f518ef843fbe94014cceace3",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "13cf9d6d2c9aae8274c27d446afd0c888ffdd52ae299a35984d4f527",
		.peer_y =
		    "dcbee75b515751f8ee2ae355e8afd5de21c62a939a6507b538cbc4af",
		.priv =
		    "e0d62035101ef487c485c60fb4500eebe6a32ec64dbe97dbe0232c46",
		.pub_x =
		    "88537735e9b23e3e0e076f135a82d33f9bffb465f3abce8322a62a62",
		.pub_y =
		    "b4c8c123673197875c0bd14ed097606d330fba2b9200ef65a44764d3",
		.want =
		    "632abb662728dbc994508873d5c527ca5ef923c0d31fa6c47ef4c825",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "965b637c0dfbc0cf954035686d70f7ec30929e664e521dbaa2280659",
		.peer_y =
		    "82a58ff61bc90019bbcbb5875d3863db0bc2a1fa34b0ad4de1a83f99",
		.priv =
		    "b96ade5b73ba72aa8b6e4d74d7bf9c58e962ff78eb542287c7b44ba2",
		.pub_x =
		    "37682926a54f70a4c1748f54d50d5b00138a055f924f2c65e5b0bbe4",
		.pub_y =
		    "596afefcdd640d29635015b89bdddd1f8c2723686d332e7a06ca8799",
		.want =
		    "34641141aab05ef58bd376d609345901fb8f63477c6be9097f037f1f",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "73cc645372ca2e71637cda943d8148f3382ab6dd0f2e1a49da94e134",
		.peer_y =
		    "df5c355c23e6e232ebc3bee2ab1873ee0d83e3382f8e6fe613f6343c",
		.priv =
		    "a40d7e12049c71e6522c7ff2384224061c3a457058b310557655b854",
		.pub_x =
		    "399801243bfe0c2da9b0a53c8ca57f2eee87aaa94a8e4d5e029f42ca",
		.pub_y =
		    "aa49e6d4b47cee7a5c4ab71d5a67da84e0b9b425ce3e70da68c889e7",
		.want =
		    "4f74ac8507501a32bfc5a78d8271c200e835966e187e8d00011a8c75",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "546578216250354e449e21546dd11cd1c5174236739acad9ce0f4512",
		.peer_y =
		    "d2a22fcd66d1abedc767668327c5cb9c599043276239cf3c8516af24",
		.priv =
		    "ad2519bc724d484e02a69f05149bb047714bf0f5986fac2e222cd946",
		.pub_x =
		    "df9c1e0ef15e53b9f626e2be1cbe893639c06f3e0439ee95d7d4b1e3",
		.pub_y =
		    "7a52a7386adda243efdf8941085c84e31239cab92b8017336748965e",
		.want =
		    "ad09c9ae4d2324ea81bb555b200d3c003e22a6870ee03b52df49e4de",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "1d46b1dc3a28123cb51346e67baec56404868678faf7d0e8b2afa22a",
		.peer_y =
		    "0ec9e65ec97e218373e7fc115c2274d5b829a60d93f71e01d58136c3",
		.priv =
		    "3d312a9b9d8ed09140900bbac1e095527ebc9e3c6493bcf3666e3a29",
		.pub_x =
		    "b4a0198dc8810e884425b750928b0c960c31f7a99663400b01a179df",
		.pub_y =
		    "812b601bfc0738242c6f86f830f27acd632ca618a0b5280c9d5769f7",
		.want =
		    "ef029c28c68064b8abd2965a38c404fb5e944ace57e8638daba9d3cd",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "266d038cc7a4fe21f6c976318e827b82bb5b8f7443a55298136506e0",
		.peer_y =
		    "df123d98a7a20bbdf3943df2e3563422f8c0cf74d53aaabdd7c973ba",
		.priv =
		    "8ce0822dc24c153995755ac350737ef506641c7d752b4f9300c612ed",
		.pub_x =
		    "00dfc7ec137690cd6d12fdb2fd0b8c5314582108769c2b722ffb3958",
		.pub_y =
		    "5eef3da4ba458127346bb64023868bddb7558a2ecfc813645f4ce9fe",
		.want =
		    "f83c16661dfcbad021cc3b5a5af51d9a18db4653866b3ff90787ce3e",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "eb0a09f7a1c236a61f595809ec5670efd92e4598d5e613e092cdfdca",
		.peer_y =
		    "50787ae2f2f15b88bc10f7b5f0aee1418373f16153aebd1fba54288d",
		.priv =
		    "0ff9b485325ab77f29e7bc379fed74bfac859482da0dee7528c19db2",
		.pub_x =
		    "7e603e6976db83c36011508fa695d1b515249e2e54b48fcbcfb90247",
		.pub_y =
		    "0179a600ce86adfca9b1b931fa5173d618da09e841803d19b0264286",
		.want =
		    "f51258c63f232e55a66aa25ebd597b2018d1052c02eeb63866758005",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "6b2f6b18a587f562ffc61bd9b0047322286986a78f1fd139b84f7c24",
		.peer_y =
		    "7096908e4615266be59a53cd655515056ff92370a6271a5d3823d704",
		.priv =
		    "19cf5ff6306467f28b9fe0675a43c0582552c8c12e59ce7c38f292b1",
		.pub_x =
		    "fc20e906e609c112cfc2e0fea6303882c5db94e87e022373ab2c082a",
		.pub_y =
		    "aecdf1daa71782bc5a26bbbd8d7e8a76490e26abc17dffc774bd7341",
		.want =
		    "7fdc969a186ff18429f2a276dac43beea21182d82ce2e5a0876552b1",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "328101ba826acd75ff9f34d5574ce0dbc92f709bad8d7a33c47940c1",
		.peer_y =
		    "df39f1ea88488c55d5538160878b9ced18a887ea261dd712d14024ff",
		.priv =
		    "90a15368e3532c0b1e51e55d139447c2c89bc160719d697291ea7c14",
		.pub_x =
		    "c6837d506e976da7db3ad1267c359dff2ea6fb0b7f7f8e77024c59e9",
		.pub_y =
		    "67eb491d2fc8a530c46525d2a8b2d7c1df5fba1ae740a4649c683ee6",
		.want =
		    "3d60ab6db2b3ffe2d29ccff46d056e54230cf34982e241556ed2920c",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "0081e34270871e2ebbd94183f617b4ae15f0416dd634fe6e934cf3c0",
		.peer_y =
		    "3a1e9f38a7b90b7317d26b9f6311063ab58b268cf489b2e50386d5d6",
		.priv =
		    "8e0838e05e1721491067e1cabc2e8051b290e2616eec427b7121897d",
		.pub_x =
		    "e9150f770075626019e18f95473b71e6828041791d3f08d3faeeaa2b",
		.pub_y =
		    "475f70735eaae52308a3b763dc88efe18ab590ebafa035f6e08b001c",
		.want =
		    "9116d72786f4db5df7a8b43078c6ab9160d423513d35ea5e2559306d",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "2623632fdf0bd856805a69aa186d4133ef5904e1f655a972d66cce07",
		.peer_y =
		    "2cef9728dd06fb8b50150f529b695076d4507983912585c89bd0682e",
		.priv =
		    "38106e93f16a381adb1d72cee3da66ae462ad4bbfea9ecdf35d0814e",
		.pub_x =
		    "7be6c4c917829ab657dd79e8637d7aefd2f81f0de7654d957e97658d",
		.pub_y =
		    "430d22d9e8438310f61e0d43f25fa3e34585f432baad27db3021bf0d",
		.want =
		    "207c53dcefac789aaa0276d9200b3a940ce5f2296f4cb2e81a185d3d",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "8ee4d1dcc31dee4bf6fe21ca8a587721d910acfb122c16c2a77a8152",
		.peer_y =
		    "4ebf323fff04eb477069a0ac68b345f6b1ae134efc31940e513cb99f",
		.priv =
		    "e5d1718431cf50f6cbd1bc8019fa16762dfa12c989e5999977fb4ea2",
		.pub_x =
		    "2ea4966e7f92ed7f5cc61fde792045f63b731d6e7d0de2577f2d8ece",
		.pub_y =
		    "1c4a7b1ede6f839162292df424be78e8176fb6f942a3c02391700f31",
		.want =
		    "10e467da34f48ad7072005bccd6da1b2ba3f71eafa1c393842f91d74",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "97dcbe6d28335882a6d193cc54a1063dd0775dc328565300bb99e691",
		.peer_y =
		    "dad11dd5ece8cfd9f97c9a526e4a1506e6355969ee87826fc38bcd24",
		.priv =
		    "3d635691b62a9a927c633951c9369c8862bd2119d30970c2644727d6",
		.pub_x =
		    "438bbb980517afb20be1d674e3ac2b31cef07a9b23fb8f6e38e0d6c0",
		.pub_y =
		    "0be5f1c47d58d21b6ed28423b32f5a94750da47edcef33ea79942afd",
		.want =
		    "82fd2f9c60c4f999ac00bbe64bfc11da8ff8cda2e499fced65230bb1",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "ce9126dd53972dea1de1d11efef900de34b661859c4648c5c0e534f7",
		.peer_y =
		    "e113b6f2c1659d07f2716e64a83c18bbce344dd2121fe85168eae085",
		.priv =
		    "acf3c85bbdc379f02f5ea36e7f0f53095a9e7046a28685a8659bf798",
		.pub_x =
		    "ff7511215c71d796bd646e8474be4416b91684ce0d269ef6f422013b",
		.pub_y =
		    "b7bf5e79b5a9393bb9ea42c0bdb2d3c2dc806e1a7306aa58e4fdbea5",
		.want =
		    "530f7e7fc932613b29c981f261cb036cba3f1df3864e0e1cba2685a2",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "84419967d6cfad41e75a02b6da605a97949a183a97c306c4b46e66a5",
		.peer_y =
		    "5cc9b259718b1bc8b144fde633a894616ffd59a3a6d5d8e942c7cbb7",
		.priv =
		    "cffd62cb00a0e3163fbf2c397fadc9618210f86b4f54a675287305f0",
		.pub_x =
		    "04bf4d948f4430d18b4ed6c96dbaf981fa11a403ed16887f06754981",
		.pub_y =
		    "7c1326a9cef51f79d4e78303d6064b459f612584ac2fdf593d7d5d84",
		.want =
		    "49f6fd0139248ef4df2db05d1319bd5b1489e249827a45a8a5f12427",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "7c9cac35768063c2827f60a7f51388f2a8f4b7f8cd736bd6bc337477",
		.peer_y =
		    "29ee6b849c6025d577dbcc55fbd17018f4edbc2ef105b004d6257bcd",
		.priv =
		    "85f903e43943d13c68932e710e80de52cbc0b8f1a1418ea4da079299",
		.pub_x =
		    "970a4a7e01d4188497ceb46955eb1b842d9085819a9b925c84529d3d",
		.pub_y =
		    "dfa2526480f833ea0edbd204e4e365fef3472888fe7d9691c3ebc09f",
		.want =
		    "8f7e34e597ae8093b98270a74a8dfcdbed457f42f43df487c5487161",
	},
	{
		.nid = NID_secp224r1,
		.peer_x =
		    "085a7642ad8e59b1a3e8726a7547afbecffdac1dab7e57230c6a9df4",
		.peer_y =
		    "f91c36d881fe9b8047a3530713554a1af4c25c5a8e654dcdcf689f2e",
		.priv =
		    "cce64891a3d0129fee0d4a96cfbe7ac470b85e967529057cfa31a1d9",
		.pub_x =
		    "a6b29632db94da2125dc1cf80e03702687b2acc1122022fa2174765a",
		.pub_y =
		    "61723edd73e10daed73775278f1958ba56f1fc9d085ebc2b64c84fe5",
		.want =
		    "71954e2261e8510be1a060733671d2e9d0a2d012eb4e09556d697d2a",
	},

	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "700c48f77f56584c5cc632ca65640db9"
		    "1b6bacce3a4df6b42ce7cc838833d287",
		.peer_y =
		    "db71e509e3fd9b060ddb20ba5c51dcc5"
		    "948d46fbf640dfe0441782cab85fa4ac",
		.priv =
		    "7d7dc5f71eb29ddaf80d6214632eeae0"
		    "3d9058af1fb6d22ed80badb62bc1a534",
		.pub_x =
		    "ead218590119e8876b29146ff89ca617"
		    "70c4edbbf97d38ce385ed281d8a6b230",
		.pub_y =
		    "28af61281fd35e2fa7002523acc85a42"
		    "9cb06ee6648325389f59edfce1405141",
		.want =
		    "46fc62106420ff012e54a434fbdd2d25"
		    "ccc5852060561e68040dd7778997bd7b",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "809f04289c64348c01515eb03d5ce7ac"
		    "1a8cb9498f5caa50197e58d43a86a7ae",
		.peer_y =
		    "b29d84e811197f25eba8f5194092cb6f"
		    "f440e26d4421011372461f579271cda3",
		.priv =
		    "38f65d6dce47676044d58ce5139582d5"
		    "68f64bb16098d179dbab07741dd5caf5",
		.pub_x =
		    "119f2f047902782ab0c9e27a54aff5eb"
		    "9b964829ca99c06b02ddba95b0a3f6d0",
		.pub_y =
		    "8f52b726664cac366fc98ac7a012b268"
		    "2cbd962e5acb544671d41b9445704d1d",
		.want =
		    "057d636096cb80b67a8c038c890e887d"
		    "1adfa4195e9b3ce241c8a778c59cda67",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "a2339c12d4a03c33546de533268b4ad6"
		    "67debf458b464d77443636440ee7fec3",
		.peer_y =
		    "ef48a3ab26e20220bcda2c1851076839"
		    "dae88eae962869a497bf73cb66faf536",
		.priv =
		    "1accfaf1b97712b85a6f54b148985a1b"
		    "dc4c9bec0bd258cad4b3d603f49f32c8",
		.pub_x =
		    "d9f2b79c172845bfdb560bbb01447ca5"
		    "ecc0470a09513b6126902c6b4f8d1051",
		.pub_y =
		    "f815ef5ec32128d3487834764678702e"
		    "64e164ff7315185e23aff5facd96d7bc",
		.want =
		    "2d457b78b4614132477618a5b077965e"
		    "c90730a8c81a1c75d6d4ec68005d67ec",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "df3989b9fa55495719b3cf46dccd28b5"
		    "153f7808191dd518eff0c3cff2b705ed",
		.peer_y =
		    "422294ff46003429d739a33206c87525"
		    "52c8ba54a270defc06e221e0feaf6ac4",
		.priv =
		    "207c43a79bfee03db6f4b944f53d2fb7"
		    "6cc49ef1c9c4d34d51b6c65c4db6932d",
		.pub_x =
		    "24277c33f450462dcb3d4801d57b9ced"
		    "05188f16c28eda873258048cd1607e0d",
		.pub_y =
		    "c4789753e2b1f63b32ff014ec42cd6a6"
		    "9fac81dfe6d0d6fd4af372ae27c46f88",
		.want =
		    "96441259534b80f6aee3d287a6bb17b5"
		    "094dd4277d9e294f8fe73e48bf2a0024",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "41192d2813e79561e6a1d6f53c8bc1a4"
		    "33a199c835e141b05a74a97b0faeb922",
		.peer_y =
		    "1af98cc45e98a7e041b01cf35f462b75"
		    "62281351c8ebf3ffa02e33a0722a1328",
		.priv =
		    "59137e38152350b195c9718d39673d51"
		    "9838055ad908dd4757152fd8255c09bf",
		.pub_x =
		    "a8c5fdce8b62c5ada598f141adb3b26c"
		    "f254c280b2857a63d2ad783a73115f6b",
		.pub_y =
		    "806e1aafec4af80a0d786b3de45375b5"
		    "17a7e5b51ffb2c356537c9e6ef227d4a",
		.want =
		    "19d44c8d63e8e8dd12c22a87b8cd4ece"
		    "27acdde04dbf47f7f27537a6999a8e62",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "33e82092a0f1fb38f5649d5867fba28b"
		    "503172b7035574bf8e5b7100a3052792",
		.peer_y =
		    "f2cf6b601e0a05945e335550bf648d78"
		    "2f46186c772c0f20d3cd0d6b8ca14b2f",
		.priv =
		    "f5f8e0174610a661277979b58ce5c90f"
		    "ee6c9b3bb346a90a7196255e40b132ef",
		.pub_x =
		    "7b861dcd2844a5a8363f6b8ef8d49364"
		    "0f55879217189d80326aad9480dfc149",
		.pub_y =
		    "c4675b45eeb306405f6c33c38bc69eb2"
		    "bdec9b75ad5af4706aab84543b9cc63a",
		.want =
		    "664e45d5bba4ac931cd65d52017e4be9"
		    "b19a515f669bea4703542a2c525cd3d3",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "6a9e0c3f916e4e315c91147be571686d"
		    "90464e8bf981d34a90b6353bca6eeba7",
		.peer_y =
		    "40f9bead39c2f2bcc2602f75b8a73ec7"
		    "bdffcbcead159d0174c6c4d3c5357f05",
		.priv =
		    "3b589af7db03459c23068b64f63f28d3"
		    "c3c6bc25b5bf76ac05f35482888b5190",
		.pub_x =
		    "9fb38e2d58ea1baf7622e96720101cae"
		    "3cde4ba6c1e9fa26d9b1de0899102863",
		.pub_y =
		    "d5561b900406edf50802dd7d73e89395"
		    "f8aed72fba0e1d1b61fe1d22302260f0",
		.want =
		    "ca342daa50dc09d61be7c196c85e60a8"
		    "0c5cb04931746820be548cdde055679d",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "a9c0acade55c2a73ead1a86fb0a97132"
		    "23c82475791cd0e210b046412ce224bb",
		.peer_y =
		    "f6de0afa20e93e078467c053d241903e"
		    "dad734c6b403ba758c2b5ff04c9d4229",
		.priv =
		    "d8bf929a20ea7436b2461b541a11c80e"
		    "61d826c0a4c9d322b31dd54e7f58b9c8",
		.pub_x =
		    "20f07631e4a6512a89ad487c4e9d6303"
		    "9e579cb0d7a556cb9e661cd59c1e7fa4",
		.pub_y =
		    "6de91846b3eee8a5ec09c2ab1f41e21b"
		    "d83620ccdd1bdce3ab7ea6e02dd274f5",
		.want =
		    "35aa9b52536a461bfde4e85fc756be92"
		    "8c7de97923f0416c7a3ac8f88b3d4489",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "94e94f16a98255fff2b9ac0c9598aac3"
		    "5487b3232d3231bd93b7db7df36f9eb9",
		.peer_y =
		    "d8049a43579cfa90b8093a94416cbefb"
		    "f93386f15b3f6e190b6e3455fedfe69a",
		.priv =
		    "0f9883ba0ef32ee75ded0d8bda39a514"
		    "6a29f1f2507b3bd458dbea0b2bb05b4d",
		.pub_x =
		    "abb61b423be5d6c26e21c605832c9142"
		    "dc1dfe5a5fff28726737936e6fbf516d",
		.pub_y =
		    "733d2513ef58beab202090586fac91bf"
		    "0fee31e80ab33473ab23a2d89e58fad6",
		.want =
		    "605c16178a9bc875dcbff54d63fe00df"
		    "699c03e8a888e9e94dfbab90b25f39b4",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "e099bf2a4d557460b5544430bbf6da11"
		    "004d127cb5d67f64ab07c94fcdf5274f",
		.peer_y =
		    "d9c50dbe70d714edb5e221f4e020610e"
		    "eb6270517e688ca64fb0e98c7ef8c1c5",
		.priv =
		    "2beedb04b05c6988f6a67500bb813faf"
		    "2cae0d580c9253b6339e4a3337bb6c08",
		.pub_x =
		    "3d63e429cb5fa895a9247129bf4e48e8"
		    "9f35d7b11de8158efeb3e106a2a87395",
		.pub_y =
		    "0cae9e477ef41e7c8c1064379bb7b554"
		    "ddcbcae79f9814281f1e50f0403c61f3",
		.want =
		    "f96e40a1b72840854bb62bc13c40cc27"
		    "95e373d4e715980b261476835a092e0b",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "f75a5fe56bda34f3c1396296626ef012"
		    "dc07e4825838778a645c8248cff01658",
		.peer_y =
		    "33bbdf1b1772d8059df568b061f3f112"
		    "2f28a8d819167c97be448e3dc3fb0c3c",
		.priv =
		    "77c15dcf44610e41696bab758943eff1"
		    "409333e4d5a11bbe72c8f6c395e9f848",
		.pub_x =
		    "ad5d13c3db508ddcd38457e5991434a2"
		    "51bed49cf5ddcb59cdee73865f138c9f",
		.pub_y =
		    "62cec1e70588aa4fdfc7b9a09daa6780"
		    "81c04e1208b9d662b8a2214bf8e81a21",
		.want =
		    "8388fa79c4babdca02a8e8a34f9e4355"
		    "4976e420a4ad273c81b26e4228e9d3a3",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "2db4540d50230756158abf61d9835712"
		    "b6486c74312183ccefcaef2797b7674d",
		.peer_y =
		    "62f57f314e3f3495dc4e099012f5e0ba"
		    "71770f9660a1eada54104cdfde77243e",
		.priv =
		    "42a83b985011d12303db1a800f2610f7"
		    "4aa71cdf19c67d54ce6c9ed951e9093e",
		.pub_x =
		    "ab48caa61ea35f13f8ed07ffa6a13e8d"
		    "b224dfecfae1a7df8b1bb6ebaf0cb97d",
		.pub_y =
		    "1274530ca2c385a3218bddfbcbf0b402"
		    "4c9badd5243bff834ebff24a8618dccb",
		.want =
		    "72877cea33ccc4715038d4bcbdfe0e43"
		    "f42a9e2c0c3b017fc2370f4b9acbda4a",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "cd94fc9497e8990750309e9a8534fd11"
		    "4b0a6e54da89c4796101897041d14ecb",
		.peer_y =
		    "c3def4b5fe04faee0a11932229fff563"
		    "637bfdee0e79c6deeaf449f85401c5c4",
		.priv =
		    "ceed35507b5c93ead5989119b9ba342c"
		    "fe38e6e638ba6eea343a55475de2800b",
		.pub_x =
		    "9a8cd9bd72e71752df91440f77c54750"
		    "9a84df98114e7de4f26cdb39234a625d",
		.pub_y =
		    "d07cfc84c8e144fab2839f5189bb1d7c"
		    "88631d579bbc58012ed9a2327da52f62",
		.want =
		    "e4e7408d85ff0e0e9c838003f28cdbd5"
		    "247cdce31f32f62494b70e5f1bc36307",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "15b9e467af4d290c417402e040426fe4"
		    "cf236bae72baa392ed89780dfccdb471",
		.peer_y =
		    "cdf4e9170fb904302b8fd93a820ba8cc"
		    "7ed4efd3a6f2d6b05b80b2ff2aee4e77",
		.priv =
		    "43e0e9d95af4dc36483cdd1968d2b7ee"
		    "b8611fcce77f3a4e7d059ae43e509604",
		.pub_x =
		    "f989cf8ee956a82e7ebd9881cdbfb2fd"
		    "946189b08db53559bc8cfdd48071eb14",
		.pub_y =
		    "5eff28f1a18a616b04b7d337868679f6"
		    "dd84f9a7b3d7b6f8af276c19611a541d",
		.want =
		    "ed56bcf695b734142c24ecb1fc1bb64d"
		    "08f175eb243a31f37b3d9bb4407f3b96",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "49c503ba6c4fa605182e186b5e81113f"
		    "075bc11dcfd51c932fb21e951eee2fa1",
		.peer_y =
		    "8af706ff0922d87b3f0c5e4e31d8b259"
		    "aeb260a9269643ed520a13bb25da5924",
		.priv =
		    "b2f3600df3368ef8a0bb85ab22f41fc0"
		    "e5f4fdd54be8167a5c3cd4b08db04903",
		.pub_x =
		    "69c627625b36a429c398b45c38677cb3"
		    "5d8beb1cf78a571e40e99fe4eac1cd4e",
		.pub_y =
		    "81690112b0a88f20f7136b28d7d47e5f"
		    "bc2ada3c8edd87589bc19ec9590637bd",
		.want =
		    "bc5c7055089fc9d6c89f83c1ea1ada87"
		    "9d9934b2ea28fcf4e4a7e984b28ad2cf",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "19b38de39fdd2f70f7091631a4f75d19"
		    "93740ba9429162c2a45312401636b29c",
		.peer_y =
		    "09aed7232b28e060941741b6828bcdfa"
		    "2bc49cc844f3773611504f82a390a5ae",
		.priv =
		    "4002534307f8b62a9bf67ff641ddc60f"
		    "ef593b17c3341239e95bdb3e579bfdc8",
		.pub_x =
		    "5fe964671315a18aa68a2a6e3dd1fde7"
		    "e23b8ce7181471cfac43c99e1ae80262",
		.pub_y =
		    "d5827be282e62c84de531b963884ba83"
		    "2db5d6b2c3a256f0e604fe7e6b8a7f72",
		.want =
		    "9a4e8e657f6b0e097f47954a63c75d74"
		    "fcba71a30d83651e3e5a91aa7ccd8343",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "2c91c61f33adfe9311c942fdbff6ba47"
		    "020feff416b7bb63cec13faf9b099954",
		.peer_y =
		    "6cab31b06419e5221fca014fb84ec870"
		    "622a1b12bab5ae43682aa7ea73ea08d0",
		.priv =
		    "4dfa12defc60319021b681b3ff84a10a"
		    "511958c850939ed45635934ba4979147",
		.pub_x =
		    "c9b2b8496f1440bd4a2d1e52752fd372"
		    "835b364885e154a7dac49295f281ec7c",
		.pub_y =
		    "fbe6b926a8a4de26ccc83b802b121240"
		    "0754be25d9f3eeaf008b09870ae76321",
		.want =
		    "3ca1fc7ad858fb1a6aba232542f3e2a7"
		    "49ffc7203a2374a3f3d3267f1fc97b78",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "a28a2edf58025668f724aaf83a50956b"
		    "7ac1cfbbff79b08c3bf87dfd2828d767",
		.peer_y =
		    "dfa7bfffd4c766b86abeaf5c99b6e50c"
		    "b9ccc9d9d00b7ffc7804b0491b67bc03",
		.priv =
		    "1331f6d874a4ed3bc4a2c6e9c74331d3"
		    "039796314beee3b7152fcdba5556304e",
		.pub_x =
		    "59e1e101521046ad9cf1d082e9d2ec7d"
		    "d22530cce064991f1e55c5bcf5fcb591",
		.pub_y =
		    "482f4f673176c8fdaa0bb6e59b15a3e4"
		    "7454e3a04297d3863c9338d98add1f37",
		.want =
		    "1aaabe7ee6e4a6fa732291202433a237"
		    "df1b49bc53866bfbe00db96a0f58224f",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "a2ef857a081f9d6eb206a81c4cf78a80"
		    "2bdf598ae380c8886ecd85fdc1ed7644",
		.peer_y =
		    "563c4c20419f07bc17d0539fade1855e"
		    "34839515b892c0f5d26561f97fa04d1a",
		.priv =
		    "dd5e9f70ae740073ca0204df60763fb6"
		    "036c45709bf4a7bb4e671412fad65da3",
		.pub_x =
		    "30b9db2e2e977bcdc98cb87dd736cbd8"
		    "e78552121925cf16e1933657c2fb2314",
		.pub_y =
		    "6a45028800b81291bce5c2e1fed7ded6"
		    "50620ebbe6050c6f3a7f0dfb4673ab5c",
		.want =
		    "430e6a4fba4449d700d2733e557f66a3"
		    "bf3d50517c1271b1ddae1161b7ac798c",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "ccd8a2d86bc92f2e01bce4d6922cf7fe"
		    "1626aed044685e95e2eebd464505f01f",
		.peer_y =
		    "e9ddd583a9635a667777d5b8a8f31b0f"
		    "79eba12c75023410b54b8567dddc0f38",
		.priv =
		    "5ae026cfc060d55600717e55b8a12e11"
		    "6d1d0df34af831979057607c2d9c2f76",
		.pub_x =
		    "46c9ebd1a4a3c8c0b6d572b5dcfba124"
		    "67603208a9cb5d2acfbb733c40cf6391",
		.pub_y =
		    "46c913a27d044185d38b467ace011e04"
		    "d4d9bbbb8cb9ae25fa92aaf15a595e86",
		.want =
		    "1ce9e6740529499f98d1f1d71329147a"
		    "33df1d05e4765b539b11cf615d6974d3",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "c188ffc8947f7301fb7b53e36746097c"
		    "2134bf9cc981ba74b4e9c4361f595e4e",
		.peer_y =
		    "bf7d2f2056e72421ef393f0c0f2b0e00"
		    "130e3cac4abbcc00286168e85ec55051",
		.priv =
		    "b601ac425d5dbf9e1735c5e2d5bdb79c"
		    "a98b3d5be4a2cfd6f2273f150e064d9d",
		.pub_x =
		    "7c9e950841d26c8dde8994398b8f5d47"
		    "5a022bc63de7773fcf8d552e01f1ba0a",
		.pub_y =
		    "cc42b9885c9b3bee0f8d8c57d3a8f635"
		    "5016c019c4062fa22cff2f209b5cc2e1",
		.want =
		    "4690e3743c07d643f1bc183636ab2a9c"
		    "b936a60a802113c49bb1b3f2d0661660",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "317e1020ff53fccef18bf47bb7f2dd77"
		    "07fb7b7a7578e04f35b3beed222a0eb6",
		.peer_y =
		    "09420ce5a19d77c6fe1ee587e6a49fba"
		    "f8f280e8df033d75403302e5a27db2ae",
		.priv =
		    "fefb1dda1845312b5fce6b81b2be205a"
		    "f2f3a274f5a212f66c0d9fc33d7ae535",
		.pub_x =
		    "38b54db85500cb20c61056edd3d88b6a"
		    "9dc26780a047f213a6e1b900f76596eb",
		.pub_y =
		    "6387e4e5781571e4eb8ae62991a33b5d"
		    "c33301c5bc7e125d53794a39160d8fd0",
		.want =
		    "30c2261bd0004e61feda2c16aa5e21ff"
		    "a8d7e7f7dbf6ec379a43b48e4b36aeb0",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "45fb02b2ceb9d7c79d9c2fa93e9c7967"
		    "c2fa4df5789f9640b24264b1e524fcb1",
		.peer_y =
		    "5c6e8ecf1f7d3023893b7b1ca1e4d178"
		    "972ee2a230757ddc564ffe37f5c5a321",
		.priv =
		    "334ae0c4693d23935a7e8e043ebbde21"
		    "e168a7cba3fa507c9be41d7681e049ce",
		.pub_x =
		    "3f2bf1589abf3047bf3e54ac9a95379b"
		    "ff95f8f55405f64eca36a7eebe8ffca7",
		.pub_y =
		    "5212a94e66c5ae9a8991872f66a72723"
		    "d80ec5b2e925745c456f5371943b3a06",
		.want =
		    "2adae4a138a239dcd93c243a3803c3e4"
		    "cf96e37fe14e6a9b717be9599959b11c",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "a19ef7bff98ada781842fbfc51a47aff"
		    "39b5935a1c7d9625c8d323d511c92de6",
		.peer_y =
		    "e9c184df75c955e02e02e400ffe45f78"
		    "f339e1afe6d056fb3245f4700ce606ef",
		.priv =
		    "2c4bde40214fcc3bfc47d4cf434b629a"
		    "cbe9157f8fd0282540331de7942cf09d",
		.pub_x =
		    "29c0807f10cbc42fb45c9989da50681e"
		    "ead716daa7b9e91fd32e062f5eb92ca0",
		.pub_y =
		    "ff1d6d1955d7376b2da24fe1163a2716"
		    "59136341bc2eb1195fc706dc62e7f34d",
		.want =
		    "2e277ec30f5ea07d6ce513149b9479b9"
		    "6e07f4b6913b1b5c11305c1444a1bc0b",
	},
	{
		.nid = NID_X9_62_prime256v1,
		.peer_x =
		    "356c5a444c049a52fee0adeb7e5d82ae"
		    "5aa83030bfff31bbf8ce2096cf161c4b",
		.peer_y =
		    "57d128de8b2a57a094d1a001e572173f"
		    "96e8866ae352bf29cddaf92fc85b2f92",
		.priv =
		    "85a268f9d7772f990c36b42b0a331adc"
		    "92b5941de0b862d5d89a347cbf8faab0",
		.pub_x =
		    "9cf4b98581ca1779453cc816ff28b410"
		    "0af56cf1bf2e5bc312d83b6b1b21d333",
		.pub_y =
		    "7a5504fcac5231a0d12d658218284868"
		    "229c844a04a3450d6c7381abe080bf3b",
		.want =
		    "1e51373bd2c6044c129c436e742a55be"
		    "2a668a85ae08441b6756445df5493857",
	},

	{
		.nid = NID_secp384r1,
		.peer_x =
		    "a7c76b970c3b5fe8b05d2838ae04ab47697b9eaf52e76459"
		    "2efda27fe7513272734466b400091adbf2d68c58e0c50066",
		.peer_y =
		    "ac68f19f2e1cb879aed43a9969b91a0839c4c38a49749b66"
		    "1efedf243451915ed0905a32b060992b468c64766fc8437a",
		.priv =
		    "3cc3122a68f0d95027ad38c067916ba0eb8c38894d22e1b1"
		    "5618b6818a661774ad463b205da88cf699ab4d43c9cf98a1",
		.pub_x =
		    "9803807f2f6d2fd966cdd0290bd410c0190352fbec7ff624"
		    "7de1302df86f25d34fe4a97bef60cff548355c015dbb3e5f",
		.pub_y =
		    "ba26ca69ec2f5b5d9dad20cc9da711383a9dbe34ea3fa5a2"
		    "af75b46502629ad54dd8b7d73a8abb06a3a3be47d650cc99",
		.want =
		    "5f9d29dc5e31a163060356213669c8ce132e22f57c9a04f4"
		    "0ba7fcead493b457e5621e766c40a2e3d4d6a04b25e533f1",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "30f43fcf2b6b00de53f624f1543090681839717d53c7c955"
		    "d1d69efaf0349b7363acb447240101cbb3af6641ce4b88e0",
		.peer_y =
		    "25e46c0c54f0162a77efcc27b6ea792002ae2ba82714299c"
		    "860857a68153ab62e525ec0530d81b5aa15897981e858757",
		.priv =
		    "92860c21bde06165f8e900c687f8ef0a05d14f290b3f07d8"
		    "b3a8cc6404366e5d5119cd6d03fb12dc58e89f13df9cd783",
		.pub_x =
		    "ea4018f5a307c379180bf6a62fd2ceceebeeb7d4df063a66"
		    "fb838aa35243419791f7e2c9d4803c9319aa0eb03c416b66",
		.pub_y =
		    "68835a91484f05ef028284df6436fb88ffebabcdd69ab013"
		    "3e6735a1bcfb37203d10d340a8328a7b68770ca75878a1a6",
		.want =
		    "a23742a2c267d7425fda94b93f93bbcc24791ac51cd8fd50"
		    "1a238d40812f4cbfc59aac9520d758cf789c76300c69d2ff",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "1aefbfa2c6c8c855a1a216774550b79a24cda37607bb1f7c"
		    "c906650ee4b3816d68f6a9c75da6e4242cebfb6652f65180",
		.peer_y =
		    "419d28b723ebadb7658fcebb9ad9b7adea674f1da3dc6b63"
		    "97b55da0f61a3eddacb4acdb14441cb214b04a0844c02fa3",
		.priv =
		    "12cf6a223a72352543830f3f18530d5cb37f26880a0b2944"
		    "82c8a8ef8afad09aa78b7dc2f2789a78c66af5d1cc553853",
		.pub_x =
		    "fcfcea085e8cf74d0dced1620ba8423694f903a219bbf901"
		    "b0b59d6ac81baad316a242ba32bde85cb248119b852fab66",
		.pub_y =
		    "972e3c68c7ab402c5836f2a16ed451a33120a7750a6039f3"
		    "ff15388ee622b7065f7122bf6d51aefbc29b37b03404581b",
		.want =
		    "3d2e640f350805eed1ff43b40a72b2abed0a518bcebe8f2d"
		    "15b111b6773223da3c3489121db173d414b5bd5ad7153435",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "8bc089326ec55b9cf59b34f0eb754d93596ca290fcb3444c"
		    "83d4de3a5607037ec397683f8cef07eab2fe357eae36c449",
		.peer_y =
		    "d9d16ce8ac85b3f1e94568521aae534e67139e310ec72693"
		    "526aa2e927b5b322c95a1a033c229cb6770c957cd3148dd7",
		.priv =
		    "8dd48063a3a058c334b5cc7a4ce07d02e5ee6d8f1f3c51a1"
		    "600962cbab462690ae3cd974fb39e40b0e843daa0fd32de1",
		.pub_x =
		    "e38c9846248123c3421861ea4d32669a7b5c3c08376ad281"
		    "04399494c84ff5efa3894adb2c6cbe8c3c913ef2eec5bd3c",
		.pub_y =
		    "9fa84024a1028796df84021f7b6c9d02f0f4bd1a612a03cb"
		    "f75a0beea43fef8ae84b48c60172aadf09c1ad016d0bf3ce",
		.want =
		    "6a42cfc392aba0bfd3d17b7ccf062b91fc09bbf3417612d0"
		    "2a90bdde62ae40c54bb2e56e167d6b70db670097eb8db854",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "eb952e2d9ac0c20c6cc48fb225c2ad154f53c8750b003fd3"
		    "b4ed8ed1dc0defac61bcdde02a2bcfee7067d75d342ed2b0",
		.peer_y =
		    "f1828205baece82d1b267d0d7ff2f9c9e15b69a72df47058"
		    "a97f3891005d1fb38858f5603de840e591dfa4f6e7d489e1",
		.priv =
		    "84ece6cc3429309bd5b23e959793ed2b111ec5cb43b6c180"
		    "85fcaea9efa0685d98a6262ee0d330ee250bc8a67d0e733f",
		.pub_x =
		    "3222063a2997b302ee60ee1961108ff4c7acf1c0ef1d5fb0"
		    "d164b84bce71c431705cb9aea9a45f5d73806655a058bee3",
		.pub_y =
		    "e61fa9e7fbe7cd43abf99596a3d3a039e99fa9dc93b0bdd9"
		    "cad81966d17eeaf557068afa7c78466bb5b22032d1100fa6",
		.want =
		    "ce7ba454d4412729a32bb833a2d1fd2ae612d4667c3a900e"
		    "069214818613447df8c611de66da200db7c375cf913e4405",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "441d029e244eb7168d647d4df50db5f4e4974ab3fdaf022a"
		    "ff058b3695d0b8c814cc88da6285dc6df1ac55c553885003",
		.peer_y =
		    "e8025ac23a41d4b1ea2aa46c50c6e479946b59b6d76497cd"
		    "9249977e0bfe4a6262622f13d42a3c43d66bdbb30403c345",
		.priv =
		    "68fce2121dc3a1e37b10f1dde309f9e2e18fac47cd177095"
		    "1451c3484cdb77cb136d00e731260597cc2859601c01a25b",
		.pub_x =
		    "868be0e694841830e424d913d8e7d86b84ee1021d82b0ecf"
		    "523f09fe89a76c0c95c49f2dfbcf829c1e39709d55efbb3b",
		.pub_y =
		    "9195eb183675b40fd92f51f37713317e4a9b4f715c8ab22e"
		    "0773b1bc71d3a219f05b8116074658ee86b52e36f3897116",
		.want =
		    "ba69f0acdf3e1ca95caaac4ecaf475bbe51b54777efce01c"
		    "a381f45370e486fe87f9f419b150c61e329a286d1aa265ec",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "3d4e6bf08a73404accc1629873468e4269e82d90d832e58a"
		    "d72142639b5a056ad8d35c66c60e8149fac0c797bceb7c2f",
		.peer_y =
		    "9b0308dc7f0e6d29f8c277acbc65a21e5adb83d11e6873bc"
		    "0a07fda0997f482504602f59e10bc5cb476b83d0a4f75e71",
		.priv =
		    "b1764c54897e7aae6de9e7751f2f37de849291f88f0f9109"
		    "3155b858d1cc32a3a87980f706b86cc83f927bdfdbeae0bd",
		.pub_x =
		    "c371222feaa6770c6f3ea3e0dac9740def4fcf821378b7f9"
		    "1ff937c21e0470f70f3a31d5c6b2912195f10926942b48ae",
		.pub_y =
		    "047d6b4d765123563f81116bc665b7b8cc6207830d805fd8"
		    "4da7cb805a65baa7c12fd592d1b5b5e3e65d9672a9ef7662",
		.want =
		    "1a6688ee1d6e59865d8e3ada37781d36bb0c2717eef92e61"
		    "964d3927cb765c2965ea80f7f63e58c322ba0397faeaf62b",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "f5f6bef1d110da03be0017eac760cc34b24d092f736f237b"
		    "c7054b3865312a813bcb62d297fb10a4f7abf54708fe2d3d",
		.peer_y =
		    "06fdf8d7dc032f4e10010bf19cbf6159321252ff415fb919"
		    "20d438f24e67e60c2eb0463204679fa356af44cea9c9ebf5",
		.priv =
		    "f0f7a96e70d98fd5a30ad6406cf56eb5b72a510e9f192f50"
		    "e1f84524dbf3d2439f7287bb36f5aa912a79deaab4adea82",
		.pub_x =
		    "99c8c41cb1ab5e0854a346e4b08a537c1706a61553387c8d"
		    "94943ab15196d40dbaa55b8210a77a5d00915f2c4ea69eab",
		.pub_y =
		    "5531065bdcf17bfb3cb55a02e41a57c7f694c383ad289f90"
		    "0fbd656c2233a93c92e933e7a26f54cbb56f0ad875c51bb0",
		.want =
		    "d06a568bf2336b90cbac325161be7695eacb2295f599500d"
		    "787f072612aca313ee5d874f807ddef6c1f023fe2b6e7cd0",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "7cdec77e0737ea37c67b89b7137fe38818010f4464438ee4"
		    "d1d35a0c488cad3fde2f37d00885d36d3b795b9f93d23a67",
		.peer_y =
		    "28c42ee8d6027c56cf979ba4c229fdb01d234944f8ac4336"
		    "50112c3cf0f02844e888a3569dfef7828a8a884589aa055e",
		.priv =
		    "9efb87ddc61d43c482ba66e1b143aef678fbd0d1bebc2000"
		    "941fabe677fe5b706bf78fce36d100b17cc787ead74bbca2",
		.pub_x =
		    "4c34efee8f0c95565d2065d1bbac2a2dd25ae964320eb6bc"
		    "cedc5f3a9b42a881a1afca1bb6b880584fa27b01c193cd92",
		.pub_y =
		    "d8fb01dbf7cd0a3868c26b951f393c3c56c2858cee901f77"
		    "93ff5d271925d13a41f8e52409f4eba1990f33acb0bac669",
		.want =
		    "bb3b1eda9c6560d82ff5bee403339f1e80342338a9913448"
		    "53b56b24f109a4d94b92f654f0425edd4c205903d7586104",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "8eeea3a319c8df99fbc29cb55f243a720d95509515ee5cc5"
		    "87a5c5ae22fbbd009e626db3e911def0b99a4f7ae304b1ba",
		.peer_y =
		    "73877dc94db9adddc0d9a4b24e8976c22d73c844370e1ee8"
		    "57f8d1b129a3bd5f63f40caf3bd0533e38a5f5777074ff9e",
		.priv =
		    "d787a57fde22ec656a0a525cf3c738b30d73af61e743ea90"
		    "893ecb2d7b622add2f94ee25c2171467afb093f3f84d0018",
		.pub_x =
		    "171546923b87b2cbbad664f01ce932bf09d6a61181686784"
		    "46bfa9f0938608cb4667a98f4ec8ac1462285c2508f74862",
		.pub_y =
		    "fa41cb4db68ae71f1f8a3e8939dc52c2dec61a83c983beb2"
		    "a02baf29ec49278088882ed0cf56c74b5c173b552ccf63cf",
		.want =
		    "1e97b60add7cb35c7403dd884c0a75795b7683fff8b49f9d"
		    "8672a8206bfdcf0a106b8768f983258c74167422e44e4d14",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "a721f6a2d4527411834b13d4d3a33c29beb83ab7682465c6"
		    "cbaf6624aca6ea58c30eb0f29dd842886695400d7254f20f",
		.peer_y =
		    "14ba6e26355109ad35129366d5e3a640ae798505a7fa55a9"
		    "6a36b5dad33de00474f6670f522214dd7952140ab0a7eb68",
		.priv =
		    "83d70f7b164d9f4c227c767046b20eb34dfc778f5387e32e"
		    "834b1e6daec20edb8ca5bb4192093f543b68e6aeb7ce788b",
		.pub_x =
		    "57cd770f3bbcbe0c78c770eab0b169bc45e139f86378ffae"
		    "1c2b16966727c2f2eb724572b8f3eb228d130db4ff862c63",
		.pub_y =
		    "7ec5c8813b685558d83e924f14bc719f6eb7ae0cbb2c4742"
		    "27c5bda88637a4f26c64817929af999592da6f787490332f",
		.want =
		    "1023478840e54775bfc69293a3cf97f5bc914726455c6653"
		    "8eb5623e218feef7df4befa23e09d77145ad577db32b41f9",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "d882a8505c2d5cb9b8851fc676677bb0087681ad53faceba"
		    "1738286b45827561e7da37b880276c656cfc38b32ade847e",
		.peer_y =
		    "34b314bdc134575654573cffaf40445da2e6aaf987f7e913"
		    "cd4c3091523058984a25d8f21da8326192456c6a0fa5f60c",
		.priv =
		    "8f558e05818b88ed383d5fca962e53413db1a0e4637eda19"
		    "4f761944cbea114ab9d5da175a7d57882550b0e432f395a9",
		.pub_x =
		    "9a2f57f4867ce753d72b0d95195df6f96c1fae934f602efd"
		    "7b6a54582f556cfa539d89005ca2edac08ad9b72dd1f60ba",
		.pub_y =
		    "d9b94ee82da9cc601f346044998ba387aee56404dc6ecc8a"
		    "b2b590443319d0b2b6176f9d0eac2d44678ed561607d09a9",
		.want =
		    "6ad6b9dc8a6cf0d3691c501cbb967867f6e4bbb764b60dbf"
		    "f8fcff3ed42dbba39d63cf325b4b4078858495ddee75f954",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "815c9d773dbf5fb6a1b86799966247f4006a23c92e68c55e"
		    "9eaa998b17d8832dd4d84d927d831d4f68dac67c6488219f",
		.peer_y =
		    "e79269948b2611484560fd490feec887cb55ef99a4b52488"
		    "0fa7499d6a07283aae2afa33feab97deca40bc606c4d8764",
		.priv =
		    "0f5dee0affa7bbf239d5dff32987ebb7cf84fcceed643e1d"
		    "3c62d0b3352aec23b6e5ac7fa4105c8cb26126ad2d1892cb",
		.pub_x =
		    "23346bdfbc9d7c7c736e02bdf607671ff6082fdd27334a8b"
		    "c75f3b23681ebe614d0597dd614fae58677c835a9f0b273b",
		.pub_y =
		    "82ba36290d2f94db41479eb45ab4eaf67928a2315138d59e"
		    "ecc9b5285dfddd6714f77557216ea44cc6fc119d8243efaf",
		.want =
		    "cc9e063566d46b357b3fcae21827377331e5e290a36e60cd"
		    "7c39102b828ae0b918dc5a02216b07fe6f1958d834e42437",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "1c0eeda7a2be000c5bdcda0478aed4db733d2a9e34122437"
		    "9123ad847030f29e3b168fa18e89a3c0fba2a6ce1c28fc3b",
		.peer_y =
		    "ec8c1c83c118c4dbea94271869f2d868eb65e8b44e21e6f1"
		    "4b0f4d9b38c068daefa27114255b9a41d084cc4a1ad85456",
		.priv =
		    "037b633b5b8ba857c0fc85656868232e2febf59578718391"
		    "b81da8541a00bfe53c30ae04151847f27499f8d7abad8cf4",
		.pub_x =
		    "8878ac8a947f7d5cb2b47aad24fbb8210d86126585399a28"
		    "71f84aa9c5fde3074ae540c6bf82275ca822d0feb862bc74",
		.pub_y =
		    "632f5cd2f900c2711c32f8930728eb647d31edd8d650f965"
		    "4e7d33e5ed1b475489d08daa30d8cbcba6bfc3b60d9b5a37",
		.want =
		    "deff7f03bd09865baf945e73edff6d5122c03fb561db87de"
		    "c8662e09bed4340b28a9efe118337bb7d3d4f7f568635ff9",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "c95c185e256bf997f30b311548ae7f768a38dee43eeeef43"
		    "083f3077be70e2bf39ac1d4daf360c514c8c6be623443d1a",
		.peer_y =
		    "3e63a663eaf75d8a765ab2b9a35513d7933fa5e26420a524"
		    "4550ec6c3b6f033b96db2aca3d6ac6aab052ce929595aea5",
		.priv =
		    "e3d07106bedcc096e7d91630ffd3094df2c7859db8d7edbb"
		    "2e37b4ac47f429a637d06a67d2fba33838764ef203464991",
		.pub_x =
		    "e74a1a2b85f1cbf8dbbdf050cf1aff8acb02fda2fb6591f9"
		    "d3cfe4e79d0ae938a9c1483e7b75f8db24505d65065cdb18",
		.pub_y =
		    "1773ee591822f7abaa856a1a60bc0a5203548dbd1cb50254"
		    "66eff8481bd07614eaa04a16c3db76905913e972a5b6b59d",
		.want =
		    "c8b1038f735ad3bb3e4637c3e47eab487637911a6b7950a4"
		    "e461948329d3923b969e5db663675623611a457fcda35a71",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "3497238a7e6ad166df2dac039aa4dac8d17aa925e7c7631e"
		    "b3b56e3aaa1c545fcd54d2e5985807910fb202b1fc191d2a",
		.peer_y =
		    "a49e5c487dcc7aa40a8f234c979446040d9174e3ad357d40"
		    "4d7765183195aed3f913641b90c81a306ebf0d8913861316",
		.priv =
		    "f3f9b0c65a49a506632c8a45b10f66b5316f9eeb06fae218"
		    "f2da62333f99905117b141c760e8974efc4af10570635791",
		.pub_x =
		    "a4ad77aa7d86e5361118a6b921710c820721210712f4c347"
		    "985fdee58aa4effa1e28be80a17b120b139f96300f89b49b",
		.pub_y =
		    "1ddf22e07e03f1560d8f45a480094560dba9fae7f9531130"
		    "c1b57ebb95982496524f31d3797793396fa823f22bdb4328",
		.want =
		    "d337eaa32b9f716b8747b005b97a553c59dab0c51df41a2d"
		    "49039cdae705aa75c7b9e7bc0b6a0e8c578c902bc4fff23e",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "90a34737d45b1aa65f74e0bd0659bc118f8e4b774b761944"
		    "ffa6573c6df4f41dec0d11b697abd934d390871d4b453240",
		.peer_y =
		    "9b590719bb3307c149a7817be355d684893a307764b512ee"
		    "ffe07cb699edb5a6ffbf8d6032e6c79d5e93e94212c2aa4e",
		.priv =
		    "59fce7fad7de28bac0230690c95710c720e528f9a4e54d3a"
		    "6a8cd5fc5c5f21637031ce1c5b4e3d39647d8dcb9b794664",
		.pub_x =
		    "9c43bf971edf09402876ee742095381f78b1bd3aa39b5132"
		    "af75dbfe7e98bd78bde10fe2e903c2b6379e1deee175a1b0",
		.pub_y =
		    "a6c58ecea5a477bb01bd543b339f1cc49f1371a2cda4d46e"
		    "b4e53e250597942351a99665a122ffea9bde0636c375daf2",
		.want =
		    "32d292b695a4488e42a7b7922e1ae537d76a3d21a0b2e368"
		    "75f60e9f6d3e8779c2afb3a413b9dd79ae18e70b47d337c1",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "dda546acfc8f903d11e2e3920669636d44b2068aeb66ff07"
		    "aa266f0030e1535b0ed0203cb8a460ac990f1394faf22f1d",
		.peer_y =
		    "15bbb2597913035faadf413476f4c70f7279769a40c986f4"
		    "70c427b4ee4962abdf8173bbad81874772925fd32f0b159f",
		.priv =
		    "3e49fbf950a424c5d80228dc4bc35e9f6c6c0c1d04440998"
		    "da0a609a877575dbe437d6a5cedaa2ddd2a1a17fd112aded",
		.pub_x =
		    "5a949594228b1a3d6f599eb3db0d06070fbc551c657b5823"
		    "4ba164ce3fe415fa5f3eb823c08dc29b8c341219c77b6b3d",
		.pub_y =
		    "2baad447c8c290cfed25edd9031c41d0b76921457327f42d"
		    "b31122b81f337bbf0b1039ec830ce9061a3761953c75e4a8",
		.want =
		    "1220e7e6cad7b25df98e5bbdcc6c0b65ca6c2a50c5ff6c41"
		    "dca71e475646fd489615979ca92fb4389aeadefde79a24f1",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "788be2336c52f4454d63ee944b1e49bfb619a08371048e6d"
		    "a92e584eae70bde1f171c4df378bd1f3c0ab03048a237802",
		.peer_y =
		    "4673ebd8db604eaf41711748bab2968a23ca4476ce144e72"
		    "8247f08af752929157b5830f1e26067466bdfa8b65145a33",
		.priv =
		    "50ccc1f7076e92f4638e85f2db98e0b483e6e2204c92bdd4"
		    "40a6deea04e37a07c6e72791c190ad4e4e86e01efba84269",
		.pub_x =
		    "756c07df0ce32c839dac9fb4733c9c28b70113a676a7057c"
		    "38d223f22a3a9095a8d564653af528e04c7e1824be4a6512",
		.pub_y =
		    "17c2ce6962cbd2a2e066297b39d57dd9bb4680f0191d390f"
		    "70b4e461419b2972ce68ad46127fdda6c39195774ea86df3",
		.want =
		    "793bb9cd22a93cf468faf804a38d12b78cb12189ec679ddd"
		    "2e9aa21fa9a5a0b049ab16a23574fe04c1c3c02343b91beb",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "d09bb822eb99e38060954747c82bb3278cf96bbf36fece34"
		    "00f4c873838a40c135eb3babb9293bd1001bf3ecdee7bf26",
		.peer_y =
		    "d416db6e1b87bbb7427788a3b6c7a7ab2c165b1e366f9608"
		    "df512037584f213a648d47f16ac326e19aae972f63fd76c9",
		.priv =
		    "06f132b71f74d87bf99857e1e4350a594e5fe35533b88855"
		    "2ceccbc0d8923c902e36141d7691e28631b8bc9bafe5e064",
		.pub_x =
		    "2a3cc6b8ff5cde926e7e3a189a1bd029c9b586351af8838f"
		    "4f201cb8f4b70ef3b0da06d352c80fc26baf8f42b784459e",
		.pub_y =
		    "bf9985960176da6d23c7452a2954ffcbbcb24249b43019a2"
		    "a023e0b3dabd461f19ad3e775c364f3f11ad49f3099400d3",
		.want =
		    "012d191cf7404a523678c6fc075de8285b243720a9030477"
		    "08bb33e501e0dbee5bcc40d7c3ef6c6da39ea24d830da1e8",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "13741262ede5861dad71063dfd204b91ea1d3b7c631df68e"
		    "b949969527d79a1dc59295ef7d2bca6743e8cd77b04d1b58",
		.peer_y =
		    "0baaeadc7e19d74a8a04451a135f1be1b02fe299f9dc00bf"
		    "df201e83d995c6950bcc1cb89d6f7b30bf54656b9a4da586",
		.priv =
		    "12048ebb4331ec19a1e23f1a2c773b664ccfe90a28bfb846"
		    "fc12f81dff44b7443c77647164bf1e9e67fd2c07a6766241",
		.pub_x =
		    "bc18836bc7a9fdf54b5352f37d7528ab8fa8ec544a8c6180"
		    "511cbfdd49cce377c39e34c031b5240dc9980503ed2f262c",
		.pub_y =
		    "8086cbe338191080f0b7a16c7afc4c7b0326f9ac66f58552"
		    "ef4bb9d24de3429ed5d3277ed58fcf48f2b5f61326bec6c6",
		.want =
		    "ad0fd3ddffe8884b9263f3c15fe1f07f2a5a22ffdc7e9670"
		    "85eea45f0cd959f20f18f522763e28bcc925e496a52dda98",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "9e22cbc18657f516a864b37b783348b66f1aa9626cd631f4"
		    "fa1bd32ad88cf11db52057c660860d39d11fbf024fabd444",
		.peer_y =
		    "6b0d53c79681c28116df71e9cee74fd56c8b7f04b39f1198"
		    "cc72284e98be9562e35926fb4f48a9fbecafe729309e8b6f",
		.priv =
		    "34d61a699ca576169fcdc0cc7e44e4e1221db0fe63d16850"
		    "c8104029f7d48449714b9884328cae189978754ab460b486",
		.pub_x =
		    "867f81104ccd6b163a7902b670ef406042cb0cce7dcdc63d"
		    "1dfc91b2c40e3cdf7595834bf9eceb79849f1636fc8462fc",
		.pub_y =
		    "9d4bde8e875ec49697d258d1d59465f8431c6f5531e1c59e"
		    "9f9ebe3cf164a8d9ce10a12f1979283a959bad244dd83863",
		.want =
		    "dc4ca392dc15e20185f2c6a8ea5ec31dfc96f56153a47394"
		    "b3072b13d0015f5d4ae13beb3bed54d65848f9b8383e6c95",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "2db5da5f940eaa884f4db5ec2139b0469f38e4e6fbbcc52d"
		    "f15c0f7cf7fcb1808c749764b6be85d2fdc5b16f58ad5dc0",
		.peer_y =
		    "22e8b02dcf33e1b5a083849545f84ad5e43f77cb71546dbb"
		    "ac0d11bdb2ee202e9d3872e8d028c08990746c5e1dde9989",
		.priv =
		    "dc60fa8736d702135ff16aab992bb88eac397f5972456c72"
		    "ec447374d0d8ce61153831bfc86ad5a6eb5b60bfb96a862c",
		.pub_x =
		    "b69beede85d0f829fec1b893ccb9c3e052ff692e13b97453"
		    "7bc5b0f9feaf7b22e84f03231629b24866bdb4b8cf908914",
		.pub_y =
		    "66f85e2bfcaba2843285b0e14ebc07ef7dafff8b424416fe"
		    "e647b59897b619f20eed95a632e6a4206bf7da429c04c560",
		.want =
		    "d765b208112d2b9ed5ad10c4046e2e3b0dbf57c469329519"
		    "e239ac28b25c7d852bf757d5de0ee271cadd021d86cfd347",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "329647baa354224eb4414829c5368c82d7893b39804e08cb"
		    "b2180f459befc4b347a389a70c91a23bd9d30c83be5295d3",
		.peer_y =
		    "cc8f61923fad2aa8e505d6cfa126b9fabd5af9dce290b756"
		    "60ef06d1caa73681d06089c33bc4246b3aa30dbcd2435b12",
		.priv =
		    "6fa6a1c704730987aa634b0516a826aba8c6d6411d3a4c89"
		    "772d7a62610256a2e2f289f5c3440b0ec1e70fa339e251ce",
		.pub_x =
		    "53de1fc1328e8de14aecab29ad8a40d6b13768f86f7d2984"
		    "33d20fec791f86f8bc73f358098b256a298bb488de257bf4",
		.pub_y =
		    "ac28944fd27f17b82946c04c66c41f0053d3692f275da55c"
		    "d8739a95bd8cd3af2f96e4de959ea8344d8945375905858b",
		.want =
		    "d3778850aeb58804fbe9dfe6f38b9fa8e20c2ca4e0dec335"
		    "aafceca0333e3f2490b53c0c1a14a831ba37c4b9d74be0f2",
	},
	{
		.nid = NID_secp384r1,
		.peer_x =
		    "29d8a36d22200a75b7aea1bb47cdfcb1b7fd66de96704143"
		    "4728ab5d533a060df732130600fe6f75852a871fb2938e39",
		.peer_y =
		    "e19b53db528395de897a45108967715eb8cb55c3fcbf2337"
		    "9372c0873a058d57544b102ecce722b2ccabb1a603774fd5",
		.priv =
		    "74ad8386c1cb2ca0fcdeb31e0869bb3f48c036afe2ef110c"
		    "a302bc8b910f621c9fcc54cec32bb89ec7caa84c7b8e54a8",
		.pub_x =
		    "27a3e83cfb9d5122e73129d801615857da7cc089cccc9c54"
		    "ab3032a19e0a0a9f677346e37f08a0b3ed8da6e5dd691063",
		.pub_y =
		    "8d60e44aa5e0fd30c918456796af37f0e41957901645e5c5"
		    "96c6d989f5859b03a0bd7d1f4e77936fff3c74d204e5388e",
		.want =
		    "81e1e71575bb4505498de097350186430a6242fa6c57b85a"
		    "5f984a23371123d2d1424eefbf804258392bc723e4ef1e35",
	},

	{
		.nid = NID_secp521r1,
		.peer_x =
		    "000000685a48e86c79f0f0875f7bc18d25eb5fc8c0b07e5d"
		    "a4f4370f3a9490340854334b1e1b87fa395464c60626124a"
		    "4e70d0f785601d37c09870ebf176666877a2046d",
		.peer_y =
		    "000001ba52c56fc8776d9e8f5db4f0cc27636d0b741bbe05"
		    "400697942e80b739884a83bde99e0f6716939e632bc8986f"
		    "a18dccd443a348b6c3e522497955a4f3c302f676",
		.priv =
		    "0000017eecc07ab4b329068fba65e56a1f8890aa935e5713"
		    "4ae0ffcce802735151f4eac6564f6ee9974c5e6887a1fefe"
		    "e5743ae2241bfeb95d5ce31ddcb6f9edb4d6fc47",
		.pub_x =
		    "000000602f9d0cf9e526b29e22381c203c48a886c2b06730"
		    "33366314f1ffbcba240ba42f4ef38a76174635f91e6b4ed3"
		    "4275eb01c8467d05ca80315bf1a7bbd945f550a5",
		.pub_y =
		    "000001b7c85f26f5d4b2d7355cf6b02117659943762b6d1d"
		    "b5ab4f1dbc44ce7b2946eb6c7de342962893fd387d1b73d7"
		    "a8672d1f236961170b7eb3579953ee5cdc88cd2d",
		.want =
		    "005fc70477c3e63bc3954bd0df3ea0d1f41ee21746ed95fc"
		    "5e1fdf90930d5e136672d72cc770742d1711c3c3a4c334a0"
		    "ad9759436a4d3c5bf6e74b9578fac148c831",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "000001df277c152108349bc34d539ee0cf06b24f5d350067"
		    "7b4445453ccc21409453aafb8a72a0be9ebe54d12270aa51"
		    "b3ab7f316aa5e74a951c5e53f74cd95fc29aee7a",
		.peer_y =
		    "0000013d52f33a9f3c14384d1587fa8abe7aed74bc33749a"
		    "d9c570b471776422c7d4505d9b0a96b3bfac041e4c6a6990"
		    "ae7f700e5b4a6640229112deafa0cd8bb0d089b0",
		.priv =
		    "000000816f19c1fb10ef94d4a1d81c156ec3d1de08b66761"
		    "f03f06ee4bb9dcebbbfe1eaa1ed49a6a990838d8ed318c14"
		    "d74cc872f95d05d07ad50f621ceb620cd905cfb8",
		.pub_x =
		    "000000d45615ed5d37fde699610a62cd43ba76bedd8f85ed"
		    "31005fe00d6450fbbd101291abd96d4945a8b57bc73b3fe9"
		    "f4671105309ec9b6879d0551d930dac8ba45d255",
		.pub_y =
		    "000001425332844e592b440c0027972ad1526431c06732df"
		    "19cd46a242172d4dd67c2c8c99dfc22e49949a56cf90c647"
		    "3635ce82f25b33682fb19bc33bd910ed8ce3a7fa",
		.want =
		    "000b3920ac830ade812c8f96805da2236e002acbbf13596a"
		    "9ab254d44d0e91b6255ebf1229f366fb5a05c5884ef46032"
		    "c26d42189273ca4efa4c3db6bd12a6853759",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "00000092db3142564d27a5f0006f819908fba1b85038a5bc"
		    "2509906a497daac67fd7aee0fc2daba4e4334eeaef0e0019"
		    "204b471cd88024f82115d8149cc0cf4f7ce1a4d5",
		.peer_y =
		    "0000016bad0623f517b158d9881841d2571efbad63f85cbe"
		    "2e581960c5d670601a6760272675a548996217e4ab2b8ebc"
		    "e31d71fca63fcc3c08e91c1d8edd91cf6fe845f8",
		.priv =
		    "0000012f2e0c6d9e9d117ceb9723bced02eb3d4eebf5feea"
		    "f8ee0113ccd8057b13ddd416e0b74280c2d0ba8ed291c443"
		    "bc1b141caf8afb3a71f97f57c225c03e1e4d42b0",
		.pub_x =
		    "000000717fcb3d4a40d103871ede044dc803db508aaa4ae7"
		    "4b70b9fb8d8dfd84bfecfad17871879698c292d2fd5e17b4"
		    "f9343636c531a4fac68a35a93665546b9a878679",
		.pub_y =
		    "000000f3d96a8637036993ab5d244500fff9d2772112826f"
		    "6436603d3eb234a44d5c4e5c577234679c4f9df725ee5b91"
		    "18f23d8a58d0cc01096daf70e8dfec0128bdc2e8",
		.want =
		    "006b380a6e95679277cfee4e8353bf96ef2a1ebdd060749f"
		    "2f046fe571053740bbcc9a0b55790bc9ab56c3208aa05ddf"
		    "746a10a3ad694daae00d980d944aabc6a08f",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "000000fdd40d9e9d974027cb3bae682162eac1328ad61bc4"
		    "353c45bf5afe76bf607d2894c8cce23695d920f2464fda47"
		    "73d4693be4b3773584691bdb0329b7f4c86cc299",
		.peer_y =
		    "00000034ceac6a3fef1c3e1c494bfe8d872b183832219a7e"
		    "14da414d4e3474573671ec19b033be831b915435905925b4"
		    "4947c592959945b4eb7c951c3b9c8cf52530ba23",
		.priv =
		    "000000e548a79d8b05f923b9825d11b656f222e8cb98b0f8"
		    "9de1d317184dc5a698f7c71161ee7dc11cd31f4f4f8ae3a9"
		    "81e1a3e78bdebb97d7c204b9261b4ef92e0918e0",
		.pub_x =
		    "0000000ce800217ed243dd10a79ad73df578aa8a3f9194af"
		    "528cd1094bbfee27a3b5481ad5862c8876c0c3f91294c0ab"
		    "3aa806d9020cbaa2ed72b7fecdc5a09a6dad6f32",
		.pub_y =
		    "000001543c9ab45b12469232918e21d5a351f9a4b9cbf9ef"
		    "b2afcc402fa9b31650bec2d641a05c440d35331c0893d11f"
		    "b13151335988b303341301a73dc5f61d574e67d9",
		.want =
		    "00fbbcd0b8d05331fef6086f22a6cce4d35724ab7a2f49dd"
		    "8458d0bfd57a0b8b70f246c17c4468c076874b0dff7a0336"
		    "823b19e98bf1cec05e4beffb0591f97713c6",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "00000098d99dee0816550e84dbfced7e88137fddcf581a72"
		    "5a455021115fe49f8dc3cf233cd9ea0e6f039dc7919da973"
		    "cdceaca205da39e0bd98c8062536c47f258f44b5",
		.peer_y =
		    "000000cd225c8797371be0c4297d2b457740100c774141d8"
		    "f214c23b61aa2b6cd4806b9b70722aa4965fb622f42b7391"
		    "e27e5ec21c5679c5b06b59127372997d421adc1e",
		.priv =
		    "000001c8aae94bb10b8ca4f7be577b4fb32bb2381032c494"
		    "2c24fc2d753e7cc5e47b483389d9f3b956d20ee9001b1eef"
		    "9f23545f72c5602140046839e963313c3decc864",
		.pub_x =
		    "00000106a14e2ee8ff970aa8ab0c79b97a33bba2958e070b"
		    "75b94736b77bbe3f777324fa52872771aa88a63a9e8490c3"
		    "378df4dc760cd14d62be700779dd1a4377943656",
		.pub_y =
		    "0000002366ce3941e0b284b1aa81215d0d3b9778fce23c8c"
		    "d1e4ed6fa0abf62156c91d4b3eb55999c3471bed275e9e60"
		    "e5aa9d690d310bfb15c9c5bbd6f5e9eb39682b74",
		.want =
		    "0145cfa38f25943516c96a5fd4bfebb2f645d10520117aa5"
		    "1971eff442808a23b4e23c187e639ff928c3725fbd1c0c2a"
		    "d0d4aeb207bc1a6fb6cb6d467888dc044b3c",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "0000007ae115adaaf041691ab6b7fb8c921f99d8ed32d283"
		    "d67084e80b9ad9c40c56cd98389fb0a849d9ecf7268c297b"
		    "6f93406119f40e32b5773ed25a28a9a85c4a7588",
		.peer_y =
		    "000001a28e004e37eeaefe1f4dbb71f1878696141af3a10a"
		    "9691c4ed93487214643b761fa4b0fbeeb247cf6d3fba7a60"
		    "697536ad03f49b80a9d1cb079673654977c5fa94",
		.priv =
		    "0000009b0af137c9696c75b7e6df7b73156bb2d45f482e5a"
		    "4217324f478b10ceb76af09724cf86afa316e7f89918d31d"
		    "54824a5c33107a483c15c15b96edc661340b1c0e",
		.pub_x =
		    "000000748cdbb875d35f4bccb62abe20e82d32e4c14dc2fe"
		    "b5b87da2d0ccb11c9b6d4b7737b6c46f0dfb4d896e2db92f"
		    "cf53cdbbae2a404c0babd564ad7adeac6273efa3",
		.pub_y =
		    "000001984acab8d8f173323de0bb60274b228871609373bb"
		    "22a17287e9dec7495873abc09a8915b54c8455c8e02f654f"
		    "602e23a2bbd7a9ebb74f3009bd65ecc650814cc0",
		.want =
		    "005c5721e96c273319fd60ecc46b5962f698e974b429f28f"
		    "e6962f4ac656be2eb8674c4aafc037eab48ece612953b1e8"
		    "d861016b6ad0c79805784c67f73ada96f351",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "0000012588115e6f7f7bdcfdf57f03b169b479758baafdaf"
		    "569d04135987b2ce6164c02a57685eb5276b5dae6295d3fe"
		    "90620f38b5535c6d2260c173e61eb888ca920203",
		.peer_y =
		    "000001542c169cf97c2596fe2ddd848a222e367c5f7e6267"
		    "ebc1bcd9ab5dcf49158f1a48e4af29a897b7e6a82091c2db"
		    "874d8e7abf0f58064691344154f396dbaed188b6",
		.priv =
		    "000001e48faacee6dec83ffcde944cf6bdf4ce4bae727478"
		    "88ebafee455b1e91584971efb49127976a52f4142952f7c2"
		    "07ec0265f2b718cf3ead96ea4f62c752e4f7acd3",
		.pub_x =
		    "0000010eb1b4d9172bcc23f4f20cc9560fc54928c3f34ea6"
		    "1c00391dc766c76ed9fa608449377d1e4fadd12360254173"
		    "30b4b91086704ace3e4e6484c606e2a943478c86",
		.pub_y =
		    "00000149413864069825ee1d0828da9f4a97713005e9bd1a"
		    "dbc3b38c5b946900721a960fe96ad2c1b3a44fe3de915613"
		    "6d44cb17cbc2415729bb782e16bfe2deb3069e43",
		.want =
		    "01736d9717429b4f412e903febe2f9e0fffd81355d6ce2c0"
		    "6ff3f66a3be15ceec6e65e308347593f00d7f33591da4043"
		    "c30763d72749f72cdceebe825e4b34ecd570",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "00000169491d55bd09049fdf4c2a53a660480fee4c03a053"
		    "8675d1cd09b5bba78dac48543ef118a1173b3fbf8b20e39c"
		    "e0e6b890a163c50f9645b3d21d1cbb3b60a6fff4",
		.peer_y =
		    "00000083494b2eba76910fed33c761804515011fab50e3b3"
		    "77abd8a8a045d886d2238d2c268ac1b6ec88bd71b7ba78e2"
		    "c33c152e4bf7da5d565e4acbecf5e92c7ad662bb",
		.priv =
		    "000000c29aa223ea8d64b4a1eda27f39d3bc98ea0148dd98"
		    "c1cbe595f8fd2bfbde119c9e017a50f5d1fc121c08c1cef3"
		    "1b758859556eb3e0e042d8dd6aaac57a05ca61e3",
		.pub_x =
		    "0000001511c848ef60d5419a98d10204db0fe58224124370"
		    "061bcfa4e9249d50618c56bf3722471b259f38263bb7b280"
		    "d23caf2a1ee8737f9371cdb2732cdc958369930c",
		.pub_y =
		    "000001d461681ae6d8c49b4c5f4d6016143fb1bd7491573e"
		    "3ed0e6c48b82e821644f87f82f0e5f08fd16f1f98fa17586"
		    "200ab02ed8c627b35c3f27617ec5fd92f456203f",
		.want =
		    "018f2ae9476c771726a77780208dedfefa205488996b18fe"
		    "cc50bfd4c132753f5766b2cd744afa9918606de2e016effc"
		    "63622e9029e76dc6e3f0c69f7aeced565c2c",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "0000008415f5bbd0eee387d6c09d0ef8acaf29c66db45d6b"
		    "a101860ae45d3c60e1e0e3f7247a4626a60fdd404965c356"
		    "6c79f6449e856ce0bf94619f97da8da24bd2cfb6",
		.peer_y =
		    "000000fdd7c59c58c361bc50a7a5d0d36f723b17c4f2ad2b"
		    "03c24d42dc50f74a8c465a0afc4683f10fab84652dfe9e92"
		    "8c2626b5456453e1573ff60be1507467d431fbb2",
		.priv =
		    "00000028692be2bf5c4b48939846fb3d5bce74654bb2646e"
		    "15f8389e23708a1afadf561511ea0d9957d0b53453819d60"
		    "fba8f65a18f7b29df021b1bb01cd163293acc3cc",
		.pub_x =
		    "000001cfdc10c799f5c79cb6930a65fba351748e07567993"
		    "e5e410ef4cacc4cd8a25784991eb4674e41050f930c7190a"
		    "c812b9245f48a7973b658daf408822fe5b85f668",
		.pub_y =
		    "00000180d9ddfc9af77b9c4a6f02a834db15e535e0b3845b"
		    "2cce30388301b51cecbe3276307ef439b5c9e6a72dc2d94d"
		    "879bc395052dbb4a5787d06efb280210fb8be037",
		.want =
		    "0105a346988b92ed8c7a25ce4d79d21bc86cfcc7f99c6cd1"
		    "9dbb4a39f48ab943b79e4f0647348da0b80bd864b85c6b8d"
		    "92536d6aa544dc7537a00c858f8b66319e25",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "000001c721eea805a5cba29f34ba5758775be0cf6160e6c0"
		    "8723f5ab17bf96a1ff2bd9427961a4f34b07fc0b14ca4b2b"
		    "f6845debd5a869f124ebfa7aa72fe565050b7f18",
		.peer_y =
		    "000000b6e89eb0e1dcf181236f7c548fd1a8c16b258b52c1"
		    "a9bfd3fe8f22841b26763265f074c4ccf2d634ae97b70195"
		    "6f67a11006c52d97197d92f585f5748bc2672eeb",
		.priv =
		    "000001194d1ee613f5366cbc44b504d21a0cf6715e209cd3"
		    "58f2dd5f3e71cc0d67d0e964168c42a084ebda746f9863a8"
		    "6bacffc819f1edf1b8c727ccfb3047240a57c435",
		.pub_x =
		    "0000016bd15c8a58d366f7f2b2f298cc87b7485e9ee70d11"
		    "d12448b8377c0a82c7626f67aff7f97be7a3546bf417eeed"
		    "df75a93c130191c84108042ea2fca17fd3f80d14",
		.pub_y =
		    "000001560502d04b74fce1743aab477a9d1eac93e5226981"
		    "fdb97a7478ce4ce566ff7243931284fad850b0c2bcae0ddd"
		    "2d97790160c1a2e77c3ed6c95ecc44b89e2637fc",
		.want =
		    "004531b3d2c6cd12f21604c8610e6723dbf4daf80b5a459d"
		    "6ba5814397d1c1f7a21d7c114be964e27376aaebe3a7bc3d"
		    "6af7a7f8c7befb611afe487ff032921f750f",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "000001c35823e440a9363ab98d9fc7a7bc0c0532dc7977a7"
		    "9165599bf1a9cc64c00fb387b42cca365286e8430360bfad"
		    "3643bc31354eda50dc936c329ecdb60905c40fcb",
		.peer_y =
		    "000000d9e7f433531e44df4f6d514201cbaabb06badd6783"
		    "e01111726d815531d233c5cdb722893ffbb2027259d594de"
		    "77438809738120c6f783934f926c3fb69b40c409",
		.priv =
		    "000001fd90e3e416e98aa3f2b6afa7f3bf368e451ad9ca5b"
		    "d54b5b14aee2ed6723dde5181f5085b68169b09fbec72137"
		    "2ccf6b284713f9a6356b8d560a8ff78ca3737c88",
		.pub_x =
		    "000001ebea1b10d3e3b971b7efb69fc878de11c7f472e4e4"
		    "d384c31b8d6288d8071517acade9b39796c7af5163bcf71a"
		    "eda777533f382c6cf0a4d9bbb938c85f44b78037",
		.pub_y =
		    "0000016b0e3e19c2996b2cbd1ff64730e7ca90edca1984f9"
		    "b2951333535e5748baa34a99f61ff4d5f812079e0f01e877"
		    "89f34efdad8098015ee74a4f846dd190d16dc6e1",
		.want =
		    "0100c8935969077bae0ba89ef0df8161d975ec5870ac811a"
		    "e7e65ca5394efba4f0633d41bf79ea5e5b9496bbd7aae000"
		    "b0594baa82ef8f244e6984ae87ae1ed124b7",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "000000093057fb862f2ad2e82e581baeb3324e7b32946f2b"
		    "a845a9beeed87d6995f54918ec6619b9931955d5a89d4d74"
		    "adf1046bb362192f2ef6bd3e3d2d04dd1f87054a",
		.peer_y =
		    "000000aa3fb2448335f694e3cda4ae0cc71b1b2f2a206fa8"
		    "02d7262f19983c44674fe15327acaac1fa40424c395a6556"
		    "cb8167312527fae5865ecffc14bbdc17da78cdcf",
		.priv =
		    "0000009012ecfdadc85ced630afea534cdc8e9d1ab8be5f3"
		    "753dcf5f2b09b40eda66fc6858549bc36e6f8df55998cfa9"
		    "a0703aecf6c42799c245011064f530c09db98369",
		.pub_x =
		    "000000234e32be0a907131d2d128a6477e0caceb86f02479"
		    "745e0fe245cb332de631c078871160482eeef584e274df7f"
		    "a412cea3e1e91f71ecba8781d9205d48386341ad",
		.pub_y =
		    "000001cf86455b09b1c005cffba8d76289a3759628c874be"
		    "ea462f51f30bd581e3803134307dedbb771b3334ee15be2e"
		    "242cd79c3407d2f58935456c6941dd9b6d155a46",
		.want =
		    "017f36af19303841d13a389d95ec0b801c7f9a679a823146"
		    "c75c17bc44256e9ad422a4f8b31f14647b2c7d317b933f7c"
		    "2946c4b8abd1d56d620fab1b5ff1a3adc71f",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "00000083192ed0b1cb31f75817794937f66ad91cf74552cd"
		    "510cedb9fd641310422af5d09f221cad249ee814d16dd7ac"
		    "84ded9eacdc28340fcfc9c0c06abe30a2fc28cd8",
		.peer_y =
		    "0000002212ed868c9ba0fb2c91e2c39ba93996a3e4ebf45f"
		    "2852d0928c48930e875cc7b428d0e7f3f4d503e5d60c68cb"
		    "49b13c2480cd486bed9200caddaddfe4ff8e3562",
		.priv =
		    "000001b5ff847f8eff20b88cfad42c06e58c3742f2f8f1fd"
		    "fd64b539ba48c25926926bd5e332b45649c0b184f77255e9"
		    "d58fe8afa1a6d968e2cb1d4637777120c765c128",
		.pub_x =
		    "000001de3dc9263bc8c4969dc684be0eec54befd9a9f3dba"
		    "194d8658a789341bf0d78d84da6735227cafaf0935195169"
		    "1197573c8c360a11e5285712b8bbdf5ac91b977c",
		.pub_y =
		    "000000812de58cd095ec2e5a9b247eb3ed41d8bef6aeace1"
		    "94a7a05b65aa5d289fbc9b1770ec84bb6be0c2c64cc37c1d"
		    "54a7f5d71377a9adbe20f26f6f2b544a821ea831",
		.want =
		    "00062f9fc29ae1a68b2ee0dcf956cbd38c88ae5f645eaa54"
		    "6b00ebe87a7260bf724be20d34b9d02076655c933d056b21"
		    "e304c24ddb1dedf1dd76de611fc4a2340336",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "000001a89b636a93e5d2ba6c2292bf23033a84f06a3ac122"
		    "0ea71e806afbe097a804cc67e9baa514cfb6c12c9194be30"
		    "212bf7aae7fdf6d376c212f0554e656463ffab7e",
		.peer_y =
		    "00000182efcaf70fc412d336602e014da47256a0b606f2ad"
		    "dcce8053bf817ac8656bb4e42f14c8cbf2a68f488ab35dcd"
		    "f64056271dee1f606a440ba4bd4e5a11b8b8e54f",
		.priv =
		    "0000011a6347d4e801c91923488354cc533e7e35fddf81ff"
		    "0fb7f56bb0726e0c29ee5dcdc5f394ba54cf57269048aab6"
		    "e055895c8da24b8b0639a742314390cc04190ed6",
		.pub_x =
		    "000000fe30267f33ba5cdefc25cbb3c9320dad9ccb1d7d37"
		    "6644620ca4fadee5626a3cede25ad254624def727a7048f7"
		    "145f76162aa98042f9b123b2076f8e8cf59b3fdf",
		.pub_y =
		    "0000001145dc6631953b6e2945e94301d6cbb098fe4b04f7"
		    "ee9b09411df104dc82d7d79ec46a01ed0f2d3e7db6eb6806"
		    "94bdeb107c1078aec6cabd9ebee3d342fe7e54df",
		.want =
		    "0128ab09bfec5406799e610f772ba17e892249fa8e0e7b18"
		    "a04b9197034b250b48294f1867fb9641518f92766066a07a"
		    "8b917b0e76879e1011e51ccbd9f540c54d4f",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "0000017200b3f16a68cbaed2bf78ba8cddfb6cffac262bba"
		    "00fbc25f9dc72a07ce59372904899f364c44cb264c097b64"
		    "7d4412bee3e519892d534d9129f8a28f7500fee7",
		.peer_y =
		    "000000baba8d672a4f4a3b63de48b96f56e18df5d68f7d70"
		    "d5109833f43770d6732e06b39ad60d93e5b43db8789f1ec0"
		    "aba47286a39ea584235acea757dbf13d53b58364",
		.priv =
		    "00000022b6d2a22d71dfaa811d2d9f9f31fbed27f2e1f3d2"
		    "39538ddf3e4cc8c39a330266db25b7bc0a9704f17bde7f35"
		    "92bf5f1f2d4b56013aacc3d8d1bc02f00d3146cc",
		.pub_x =
		    "000000ba38cfbf9fd2518a3f61d43549e7a6a6d28b2be57f"
		    "fd3e0faceb636b34ed17e044a9f249dae8fc132e937e2d93"
		    "49cd2ed77bb1049ceb692a2ec5b17ad61502a64c",
		.pub_y =
		    "0000001ec91d3058573fa6c0564a02a1a010160c313bc7c7"
		    "3510dc983e5461682b5be00dbce7e2c682ad73f29ca822cd"
		    "c111f68fabe33a7b384a648342c3cdb9f050bcdb",
		.want =
		    "0101e462e9d9159968f6440e956f11dcf2227ae4aea81667"
		    "122b6af9239a291eb5d6cf5a4087f358525fcacfa46bb2db"
		    "01a75af1ba519b2d31da33eda87a9d565748",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "0000004efd5dbd2f979e3831ce98f82355d6ca14a5757842"
		    "875882990ab85ab9b7352dd6b9b2f4ea9a1e95c3880d65d1"
		    "f3602f9ca653dc346fac858658d75626f4d4fb08",
		.peer_y =
		    "00000061cf15dbdaa7f31589c98400373da284506d70c89f"
		    "074ed262a9e28140796b7236c2eef99016085e71552ff488"
		    "c72b7339fefb7915c38459cb20ab85aec4e45052",
		.priv =
		    "0000005bacfff268acf6553c3c583b464ea36a1d35e2b257"
		    "a5d49eb3419d5a095087c2fb4d15cf5bf5af816d0f3ff758"
		    "6490ccd3ddc1a98b39ce63749c6288ce0dbdac7d",
		.pub_x =
		    "00000036e488da7581472a9d8e628c58d6ad727311b7e6a3"
		    "f6ae33a8544f34b09280249020be7196916fafd90e2ec54b"
		    "66b5468d2361b99b56fa00d7ac37abb8c6f16653",
		.pub_y =
		    "0000011edb9fb8adb6a43f4f5f5fdc1421c9fe04fc8ba46c"
		    "9b66334e3af927c8befb4307104f299acec4e30f812d9345"
		    "c9720d19869dbfffd4ca3e7d2713eb5fc3f42615",
		.want =
		    "0141d6a4b719ab67eaf04a92c0a41e2dda78f4354fb90bdc"
		    "35202cc7699b9b04d49616f82255debf7bbec045ae58f982"
		    "a66905fcfae69d689785e38c868eb4a27e7b",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "00000129891de0cf3cf82e8c2cf1bf90bb296fe00ab08ca4"
		    "5bb7892e0e227a504fdd05d2381a4448b68adff9c4153c87"
		    "eacb78330d8bd52515f9f9a0b58e85f446bb4e10",
		.peer_y =
		    "0000009edd679696d3d1d0ef327f200383253f6413683d9e"
		    "4fcc87bb35f112c2f110098d15e5701d7ceee416291ff5fe"
		    "d85e687f727388b9afe26a4f6feed560b218e6bb",
		.priv =
		    "0000008e2c93c5423876223a637cad367c8589da69a2d0fc"
		    "68612f31923ae50219df2452e7cc92615b67f17b57ffd2f5"
		    "2b19154bb40d7715336420fde2e89fee244f59dc",
		.pub_x =
		    "000000fa3b35118d6c422570f724a26f90b2833b19239174"
		    "cea081c53133f64db60d6940ea1261299c04c1f4587cdb0c"
		    "4c39616479c1bb0c146799a118032dcf98f899c0",
		.pub_y =
		    "00000069f040229006151fa32b51f679c8816f7c17506b40"
		    "3809dc77cd58a2aec430d94d13b6c916de99f355aa45fcfb"
		    "c6853d686c71be496a067d24bfaea4818fc51f75",
		.want =
		    "00345e26e0abb1aac12b75f3a9cf41efe1c336396dffa4a0"
		    "67a4c2cfeb878c68b2b045faa4e5b4e6fa4678f5b603c351"
		    "903b14bf9a6a70c439257199a640890b61d1",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "000001a3c20240e59f5b7a3e17c275d2314ba1741210ad58"
		    "b71036f8c83cc1f6b0f409dfdd9113e94b67ec39c3291426"
		    "c23ffcc447054670d2908ff8fe67dc2306034c5c",
		.peer_y =
		    "000001d2825bfd3af8b1e13205780c137fe938f84fde4018"
		    "8e61ea02cead81badfdb425c29f7d7fb0324debadc10bbb9"
		    "3de68f62c35069268283f5265865db57a79f7bf7",
		.priv =
		    "00000004d49d39d40d8111bf16d28c5936554326b197353e"
		    "ebbcf47545393bc8d3aaf98f14f5be7074bfb38e6cc97b98"
		    "9754074daddb3045f4e4ce745669fdb3ec0d5fa8",
		.pub_x =
		    "0000012ec226d050ce07c79b3df4d0f0891f9f7adf462e8c"
		    "98dbc1a2a14f5e53a3f5ad894433587cc429a8be9ea1d84f"
		    "a33b1803690dae04da7218d30026157fc995cf52",
		.pub_y =
		    "0000004837dfbf3426f57b5c793269130abb9a38f6185322"
		    "11931154db4eeb9aede88e57290f842ea0f2ea9a5f74c620"
		    "3a3920fe4e305f6118f676b154e1d75b9cb5eb88",
		.want =
		    "006fe9de6fb8e672e7fd150fdc5e617fabb0d43906354ccf"
		    "d224757c7276f7a1010091b17ed072074f8d10a5ec971eb3"
		    "5a5cb7076603b7bc38d432cbc059f80f9488",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "0000007e2d138f2832e345ae8ff65957e40e5ec7163f016b"
		    "df6d24a2243daa631d878a4a16783990c722382130f9e51f"
		    "0c1bd6ff5ac96780e48b68f5dec95f42e6144bb5",
		.peer_y =
		    "000000b0de5c896791f52886b0f09913e26e78dd0b69798f"
		    "c4df6d95e3ca708ecbcbcce1c1895f5561bbabaae372e9e6"
		    "7e6e1a3be60e19b470cdf673ec1fc393d3426e20",
		.priv =
		    "0000011a5d1cc79cd2bf73ea106f0e60a5ace220813b53e2"
		    "7b739864334a07c03367efda7a4619fa6eef3a9746492283"
		    "b3c445610a023a9cc49bf4591140384fca5c8bb5",
		.pub_x =
		    "000000eb07c7332eedb7d3036059d35f7d2288d4377d5f42"
		    "337ad3964079fb120ccd4c8bd384b585621055217023acd9"
		    "a94fcb3b965bfb394675e788ade41a1de73e620c",
		.pub_y =
		    "000000491a835de2e6e7deb7e090f4a11f2c460c0b1f3d5e"
		    "94ee8d751014dc720784fd3b54500c86ebaef18429f09e8e"
		    "876d5d1538968a030d7715dde99f0d8f06e29d59",
		.want =
		    "01e4e759ecedce1013baf73e6fcc0b92451d03bdd50489b7"
		    "8871c333114990c9ba6a9b2fc7b1a2d9a1794c1b60d9279a"
		    "f6f146f0bbfb0683140403bfa4ccdb524a29",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "000000118c36022209b1af8ebad1a12b566fc48744576e11"
		    "99fe80de1cdf851cdf03e5b9091a8f7e079e83b7f827259b"
		    "691d0c22ee29d6bdf73ec7bbfd746f2cd97a357d",
		.peer_y =
		    "000000da5ff4904548a342e2e7ba6a1f4ee5f840411a96cf"
		    "63e6fe622f22c13e614e0a847c11a1ab3f1d12cc850c32e0"
		    "95614ca8f7e2721477b486e9ff40372977c3f65c",
		.priv =
		    "0000010c908caf1be74c616b625fc8c1f514446a6aec83b5"
		    "937141d6afbb0a8c7666a7746fa1f7a6664a2123e8cdf6cd"
		    "8bf836c56d3c0ebdcc980e43a186f938f3a78ae7",
		.pub_x =
		    "00000031890f4c7abec3f723362285d77d2636f876817db3"
		    "bbc88b01e773597b969ff6f013ea470c854ab4a7739004eb"
		    "8cbea69b82ddf36acadd406871798ecb2ac3aa7f",
		.pub_y =
		    "000000d8b429ae3250266b9643c0c765a60dc10155bc2531"
		    "cf8627296f4978b6640a9e600e19d0037d58503fa8079954"
		    "6a814d7478a550aa90e5ebeb052527faaeae5d08",
		.want =
		    "0163c9191d651039a5fe985a0eea1eba018a40ab1937fcd2"
		    "b61220820ee8f2302e9799f6edfc3f5174f369d672d377ea"
		    "8954a8d0c8b851e81a56fda95212a6578f0e",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "000001780edff1ca1c03cfbe593edc6c049bcb2860294a92"
		    "c355489d9afb2e702075ade1c953895a456230a0cde905de"
		    "4a3f38573dbfcccd67ad6e7e93f0b5581e926a5d",
		.peer_y =
		    "000000a5481962c9162962e7f0ebdec936935d0eaa813e82"
		    "26d40d7f6119bfd940602380c86721e61db1830f51e139f2"
		    "10000bcec0d8edd39e54d73a9a129f95cd5fa979",
		.priv =
		    "000001b37d6b7288de671360425d3e5ac1ccb21815079d8d"
		    "73431e9b74a6f0e7ae004a357575b11ad66642ce8b775593"
		    "eba9d98bf25c75ef0b4d3a2098bbc641f59a2b77",
		.pub_x =
		    "000000189a5ee34de7e35aefeaeef9220c18071b4c29a4c3"
		    "bd9d954458bd3e82a7a34da34cff5579b8101c065b1f2f52"
		    "7cf4581501e28ef5671873e65267733d003520af",
		.pub_y =
		    "000001eb4bc50a7b4d4599d7e3fa773ddb9eb252c9b34228"
		    "72e544bdf75c7bf60f5166ddc11eb08fa7c30822dabaee37"
		    "3ab468eb2d922e484e2a527fff2ebb804b7d9a37",
		.want =
		    "015d613e267a36342e0d125cdad643d80d97ed0600afb9e6"
		    "b9545c9e64a98cc6da7c5aaa3a8da0bdd9dd3b97e9788218"
		    "a80abafc106ef065c8f1c4e1119ef58d298b",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "0000016dacffa183e5303083a334f765de724ec5ec940202"
		    "6d4797884a9828a0d321a8cfac74ab737fe20a7d6befcfc7"
		    "3b6a35c1c7b01d373e31abc192d48a4241a35803",
		.peer_y =
		    "0000011e5327cac22d305e7156e559176e19bee7e4f2f59e"
		    "86f1a9d0b6603b6a7df1069bde6387feb71587b8ffce5b26"
		    "6e1bae86de29378a34e5c74b6724c4d40a719923",
		.priv =
		    "000000f2661ac762f60c5fff23be5d969ccd4ec6f98e4e72"
		    "618d12bdcdb9b4102162333788c0bae59f91cdfc172c7a16"
		    "81ee44d96ab2135a6e5f3415ebbcd55165b1afb0",
		.pub_x =
		    "000000a8e25a6902d687b4787cdc94c364ac7cecc5c49548"
		    "3ed363dc0aa95ee2bd739c4c4d46b17006c728b076350d7d"
		    "7e54c6822f52f47162a25109aaaba690cab696ec",
		.pub_y =
		    "00000168d2f08fe19e4dc9ee7a195b03c9f7fe6676f9f520"
		    "b6270557504e72ca4394a2c6918625e15ac0c51b8f95cd56"
		    "0123653fb8e8ee6db961e2c4c62cc54e92e2a2a9",
		.want =
		    "014d6082a3b5ced1ab8ca265a8106f302146c4acb8c30bb1"
		    "4a4c991e3c82a9731288bdb91e0e85bda313912d06384fc4"
		    "4f2153fb13506fa9cf43c9aab5750988c943",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "000000a091421d3703e3b341e9f1e7d58f8cf7bdbd1798d0"
		    "01967b801d1cec27e605c580b2387c1cb464f55ce7ac8033"
		    "4102ab03cfb86d88af76c9f4129c01bedd3bbfc4",
		.peer_y =
		    "0000008c9c577a8e6fc446815e9d40baa66025f15dae285f"
		    "19eb668ee60ae9c98e7ecdbf2b2a68e22928059f67db1880"
		    "07161d3ecf397e0883f0c4eb7eaf7827a62205cc",
		.priv =
		    "000000f430ca1261f09681a9282e9e970a9234227b1d5e58"
		    "d558c3cc6eff44d1bdf53de16ad5ee2b18b92d62fc795861"
		    "16b0efc15f79340fb7eaf5ce6c44341dcf8dde27",
		.pub_x =
		    "0000006c1d9b5eca87de1fb871a0a32f807c725adccde9b3"
		    "967453a71347d608f0c030cd09e338cdecbf4a02015bc8a6"
		    "e8d3e2595fe773ffc2fc4e4a55d0b1a2cc00323b",
		.pub_y =
		    "000001141b2109e7f4981c952aa818a2b9f6f5c41feccdb7"
		    "a7a45b9b4b672937771b008cae5f934dfe3fed10d383ab1f"
		    "38769c92ce88d9be5414817ecb073a31ab368ccb",
		.want =
		    "0020c00747cb8d492fd497e0fec54644bf027d418ab68638"
		    "1f109712a99cabe328b9743d2225836f9ad66e5d7fed1de2"
		    "47e0da92f60d5b31f9e47672e57f710598f4",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "0000004f38816681771289ce0cb83a5e29a1ab06fc91f786"
		    "994b23708ff08a08a0f675b809ae99e9f9967eb1a49f1960"
		    "57d69e50d6dedb4dd2d9a81c02bdcc8f7f518460",
		.peer_y =
		    "0000009efb244c8b91087de1eed766500f0e81530752d469"
		    "256ef79f6b965d8a2232a0c2dbc4e8e1d09214bab38485be"
		    "6e357c4200d073b52f04e4a16fc6f5247187aecb",
		.priv =
		    "0000005dc33aeda03c2eb233014ee468dff753b72f73b009"
		    "91043ea353828ae69d4cd0fadeda7bb278b535d7c57406ff"
		    "2e6e473a5a4ff98e90f90d6dadd25100e8d85666",
		.pub_x =
		    "000000c825ba307373cec8dd2498eef82e21fd9862168dbf"
		    "eb83593980ca9f82875333899fe94f137daf1c4189eb5029"
		    "37c3a367ea7951ed8b0f3377fcdf2922021d46a5",
		.pub_y =
		    "0000016b8a2540d5e65493888bc337249e67c0a68774f3e8"
		    "d81e3b4574a0125165f0bd58b8af9de74b35832539f95c3c"
		    "d9f1b759408560aa6851ae3ac7555347b0d3b13b",
		.want =
		    "00c2bfafcd7fbd3e2fd1c750fdea61e70bd4787a7e68468c"
		    "574ee99ebc47eedef064e8944a73bcb7913dbab5d93dca66"
		    "0d216c553622362794f7a2acc71022bdb16f",
	},
	{
		.nid = NID_secp521r1,
		.peer_x =
		    "000001a32099b02c0bd85371f60b0dd20890e6c7af048c81"
		    "79890fda308b359dbbc2b7a832bb8c6526c4af99a7ea3f0b"
		    "3cb96ae1eb7684132795c478ad6f962e4a6f446d",
		.peer_y =
		    "0000017627357b39e9d7632a1370b3e93c1afb5c851b910e"
		    "b4ead0c9d387df67cde85003e0e427552f1cd09059aad026"
		    "2e235cce5fba8cedc4fdc1463da76dcd4b6d1a46",
		.priv =
		    "000000df14b1f1432a7b0fb053965fd8643afee26b2451ec"
		    "b6a8a53a655d5fbe16e4c64ce8647225eb11e7fdcb236274"
		    "71dffc5c2523bd2ae89957cba3a57a23933e5a78",
		.pub_x =
		    "0000004e8583bbbb2ecd93f0714c332dff5ab3bc6396e62f"
		    "3c560229664329baa5138c3bb1c36428abd4e23d17fcb7a2"
		    "cfcc224b2e734c8941f6f121722d7b6b94154576",
		.pub_y =
		    "000001cf0874f204b0363f020864672fadbf87c8811eb147"
		    "758b254b74b14fae742159f0f671a018212bbf25b8519e12"
		    "6d4cad778cfff50d288fd39ceb0cac635b175ec0",
		.want =
		    "01aaf24e5d47e4080c18c55ea35581cd8da30f1a07956504"
		    "5d2008d51b12d0abb4411cda7a0785b15d149ed301a36970"
		    "62f42da237aa7f07e0af3fd00eb1800d9c41",
	},
};

#define N_ECC_CDH_TESTS (sizeof(ecc_cdh_tests) / sizeof(ecc_cdh_tests[0]))

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	if (len % 8)
		fprintf(stderr, "\n");
}

static int
run_ecc_cdh_test(const struct ecc_cdh_test *test)
{
	static int last_nid;
	static size_t count;
	EC_KEY *key = NULL;
	const EC_GROUP *group;
	EC_POINT *peer_pub = NULL;
	BN_CTX *ctx = NULL;
	BIGNUM *peer_x, *peer_y, *priv, *pub_x, *pub_y, *shared;
	uint8_t *out, *want;
	int out_len;
	int failed = 1;

	if (test->nid != last_nid) {
		last_nid = test->nid;
		count = 0;
	}

	if ((ctx = BN_CTX_new()) == NULL)
		errx(1, "BN_CTX_new");

	BN_CTX_start(ctx);

	if ((peer_x = BN_CTX_get(ctx)) == NULL)
		errx(1, "peer_x = BN_CTX_get()");
	if ((peer_y = BN_CTX_get(ctx)) == NULL)
		errx(1, "peer_y = BN_CTX_get()");
	if ((priv = BN_CTX_get(ctx)) == NULL)
		errx(1, "priv = BN_CTX_get()");
	if ((pub_x = BN_CTX_get(ctx)) == NULL)
		errx(1, "pub_x = BN_CTX_get()");
	if ((pub_y = BN_CTX_get(ctx)) == NULL)
		errx(1, "pub_y = BN_CTX_get()");
	if ((shared = BN_CTX_get(ctx)) == NULL)
		errx(1, "want = BN_CTX_get()");

	if ((key = EC_KEY_new_by_curve_name(test->nid)) == NULL)
		errx(1, "EC_KEY_new_by_curve_name(%d)", test->nid);

	if (!BN_hex2bn(&peer_x, test->peer_x))
		errx(1, "peer_x = BN_hex2bn()");
	if (!BN_hex2bn(&peer_y, test->peer_y))
		errx(1, "peer_y = BN_hex2bn()");

	if ((group = EC_KEY_get0_group(key)) == NULL)
		errx(1, "EC_KEY_get0_group");

	if ((peer_pub = EC_POINT_new(group)) == NULL)
		errx(1, "EC_POINT_new");

	if (!EC_POINT_set_affine_coordinates(group, peer_pub, peer_x, peer_y, ctx))
		errx(1, "EC_POINT_set_affine_coordinates");

	if (!BN_hex2bn(&priv, test->priv))
		errx(1, "priv = BN_hex2bn()");
	if (!BN_hex2bn(&pub_x, test->pub_x))
		errx(1, "pub_x = BN_hex2bn()");
	if (!BN_hex2bn(&pub_y, test->pub_y))
		errx(1, "pub_y = BN_hex2bn()");

	if (!EC_KEY_set_private_key(key, priv))
		errx(1, "EC_KEY_set_private_key");
	if (!EC_KEY_set_public_key_affine_coordinates(key, pub_x, pub_y))
		errx(1, "EC_KEY_set_public_key_affine_coordinates");

	EC_KEY_set_flags(key, EC_FLAG_COFACTOR_ECDH);

	out_len = ECDH_size(key);
	if ((out = calloc(1, out_len)) == NULL)
		errx(1, NULL);

	if (ECDH_compute_key(out, out_len, peer_pub, key, NULL) != out_len)
		errx(1, "ECDH_compute_key");

	if (!BN_hex2bn(&shared, test->want))
		errx(1, "shared = BN_hex2bn()");

	if ((want = calloc(1, out_len)) == NULL)
		errx(1, NULL);

	if (BN_bn2binpad(shared, want, out_len) != out_len)
		errx(1, "BN_bn2binpad");

	if (memcmp(out, want, out_len) != 0) {
		fprintf(stderr, "%s test %zu failed:\nwant:\n",
		    OBJ_nid2sn(test->nid), count);
		hexdump(want, out_len);
		fprintf(stderr, "got:\n");
		hexdump(out, out_len);
		goto failed;
	}

	failed = 0;

 failed:
	count++;

	EC_KEY_free(key);
	EC_POINT_free(peer_pub);
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);
	freezero(out, out_len);
	freezero(want, out_len);

	return failed;
}

int
main(void)
{
	int failed = 0;
	size_t i;

	for (i = 0; i < N_ECC_CDH_TESTS; i++)
		failed |= run_ecc_cdh_test(&ecc_cdh_tests[i]);

	return failed;
}
