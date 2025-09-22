/*	$OpenBSD: bn_mod_sqrt.c,v 1.11 2024/08/23 12:56:26 anton Exp $ */

/*
 * Copyright (c) 2022,2023 Theo Buehler <tb@openbsd.org>
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

#include <openssl/bn.h>

/*
 * Test that .sqrt * .sqrt = .a (mod .p) where .p is a prime.  If .sqrt is
 * omitted, .a does not have a square root and BN_mod_sqrt() fails.
 */

struct mod_sqrt_test {
	const char *a;
	const char *p;
	const char *sqrt;
} mod_sqrt_test_data[] = {
	{
		.a = "0",
		.p = "2",
		.sqrt = "0",
	},
	{
		.a = "1",
		.p = "2",
		.sqrt = "1",
	},
	{
		.a = "23",
		.p = "2",
		.sqrt = "1",
	},
	{
		.a = "24",
		.p = "2",
		.sqrt = "0",
	},
	{
		.a = "1",
		.p = "1",
	},
	{
		.a = "0",
		.p = "17",
		.sqrt = "0",
	},
	{
		.a = "1",
		.p = "17",
		.sqrt = "1",
	},
	{
		.a = "3",
		.p = "17",
		.sqrt = "7",
	},

	/*
	 * Test cases resulting in an infinite loop before bn_sqrt.c r1.10.
	 */

	{
		.a = "20a7ee",
		.p = "460201", /* 460201 == 4D5 * E7D */
	},
	{
		.a = "65bebdb00a96fc814ec44b81f98b59fba3c30203928fa521"
		     "4c51e0a97091645280c947b005847f239758482b9bfc45b0"
		     "66fde340d1fe32fc9c1bf02e1b2d0ed",
		.p = "9df9d6cc20b8540411af4e5357ef2b0353cb1f2ab5ffc3e2"
		     "46b41c32f71e951f",
	},

	/*
	 * p = 3 (mod 4)
	 */

	{
		.a = "c3978f75d6c2908ae3e9a714ad3d09b13031868dfc5873d7"
		     "bd9a9691f3b45",
		.p = "e9f638f327f3ca2a2928f5451bb3b6ff",
		.sqrt = "37f112813516c2563028c63a687d38b",
	},
	{
		.a = "1730fbcd9e78e1e786284f708aaa599ffa0d744ff223e3f7"
		     "ac1faac3d7d2a45e",
		.p = "e9f638f327f3ca2a2928f5451bb3b6ff",
		.sqrt = "4d0d44591c8c80bf1314762cf73c251f",
	},
	{
		.a = "1fd006456db047a16b32a48235749b8b627be66a5f9e05d7"
		     "d1857114baa9ff1",
		.p = "e9f638f327f3ca2a2928f5451bb3b6ff",
		.sqrt = "168fa1c701b579827436c8abc65bae54",
	},
	{
		.a = "216c1526fc9afa21788f84ff1bba10e8bccd39fc60978cdd"
		     "f89087d66dffdd35",
		.p = "e9f638f327f3ca2a2928f5451bb3b6ff",
		.sqrt = "5c7fc4b52edf59c44b0916cb134e852a",
	},
	{
		.a = "1cd486f29a9632ef276d10bb9754aae1b2723163a1a42552"
		     "4e514dd9b40e9b6e",
		.p = "e9f638f327f3ca2a2928f5451bb3b6ff",
		.sqrt = "55e8f564c1dd1455fea889203ae81c48",
	},
	{
		.a = "7272d05925151c067fcd3fb44d4d4908c9104691e3fa82a5"
		     "d2ed4a58479020f",
		.p = "e9f638f327f3ca2a2928f5451bb3b6ff",
		.sqrt = "2acad049c45fe1bf4307bd7b962dbd7d",
	},
	{
		.a = "6d12f19be6960d04f651867737a0e9a0b16e614cc5eb6ffe"
		     "cc6c911d3c9b260",
		.p = "e9f638f327f3ca2a2928f5451bb3b6ff",
		.sqrt = "29c68091856db1e3f6d63eaa341cbecd",
	},
	{
		.a = "23681522f2ab9e0e7cf34243e092dd8bada26d18dc1211fc"
		     "d9d49f65569cd179",
		.p = "e9f638f327f3ca2a2928f5451bb3b6ff",
		.sqrt = "5f349a0ac98fd5c7b575bc9ff05482cf",
	},
	{
		.a = "3218f7cc3f00df33b1279c2f5386f1f1837db81cf3a69052"
		     "d23f6220f1b43b4b",
		.p = "e9f638f327f3ca2a2928f5451bb3b6ff",
		.sqrt = "713f53f8fc8038dc2b8ad3adf2bb6a3a",
	},
	{
		.a = "1d1cd78fc48830494c54113173d636119286e7bd0bd7d627"
		     "21063f88256868fc",
		.p = "e9f638f327f3ca2a2928f5451bb3b6ff",
		.sqrt = "565470af11ec50128e82606f07bd3baf",
	},
	{
		.a = "3a9de6e9da1e02f6e7e9b4f1556a0bcd1072d065",
		.p = "e9f638f327f3ca2a2928f5451bb3b6ff",
	},
	{
		.a = "8bdbc0195d2af794784a8b6f7b63d9fce9fe7a29",
		.p = "e9f638f327f3ca2a2928f5451bb3b6ff",
	},
	{
		.a = "262b386d4027cc114b4bb73ea745650e6e4a22ef314e9e03"
		     "486f3fa7d721b15e",
		.p = "e36058b270896dd9380d4d693a2593eb",
		.sqrt = "62d977c29cfede2669e617851d1db12b",
	},
	{
		.a = "172020616142af15b1d55675aae05601206663c1749e3753"
		     "f1b80696d62ece4",
		.p = "e36058b270896dd9380d4d693a2593eb",
		.sqrt = "133c4f98c82d077789f01c232eca5692",
	},
	{
		.a = "199daf5ed7d4c07f9c22af215c32fc024cc609df92135d9e"
		     "360076ae2baf943b",
		.p = "e36058b270896dd9380d4d693a2593eb",
		.sqrt = "50fac2c950af164b0bd7d9b5cf9600ae",
	},
	{
		.a = "15b01b8d11211643af99034b3e902305228c903653baea22"
		     "4c4896949b037800",
		.p = "e36058b270896dd9380d4d693a2593eb",
		.sqrt = "4a8330fc5c8164f858e5cf5028ba69da",
	},
	{
		.a = "a326d1821af01d2c4251d84807980a9942b3ecab8dfe0fe9"
		     "9b4fd27c47de741",
		.p = "e36058b270896dd9380d4d693a2593eb",
		.sqrt = "3317a2ea8c32838e0c8ac6b0838b2518",
	},
	{
		.a = "1213f38d2c4444e54ec1966acd28c568ad273a1d8337154e"
		     "3e2a137c372624fd",
		.p = "e36058b270896dd9380d4d693a2593eb",
		.sqrt = "44076fb0b477e1a29315aabc95dafe43",
	},
	{
		.a = "399cc2207e49304d48c5ae4e32d3db8676fd16bd79ad98e6"
		     "dc001fb405ef6f0",
		.p = "e36058b270896dd9380d4d693a2593eb",
		.sqrt = "1e5c722148e8939872584ca89e01237d",
	},
	{
		.a = "2faee914d3da21f1a5e82303f945593cbbb8bfc94315cb32"
		     "5faca3f8877b23a",
		.p = "e36058b270896dd9380d4d693a2593eb",
		.sqrt = "1b9f0867a249063caf36bc28b68b5d54",
	},
	{
		.a = "17f6c4ba6517237a5ceaaa186400a0e5a9657db2fd863628"
		     "d6ac524027287880",
		.p = "e36058b270896dd9380d4d693a2593eb",
		.sqrt = "4e5323a7cda4e3037d258f1436e3b964",
	},
	{
		.a = "1973a0acd4be1a2115b396969421d78bcd32f2cc9b31b1a7"
		     "3e876b0926f34142",
		.p = "e36058b270896dd9380d4d693a2593eb",
		.sqrt = "50b82d13351cca9addb25052c4d9bead",
	},
	{
		.a = "2768ea8c5bc132c003b0451a9e356ef1b9821646",
		.p = "e36058b270896dd9380d4d693a2593eb",
	},
	{
		.a = "ae5450500ae2fdd7d07df1c46337e3de89730ec2",
		.p = "e36058b270896dd9380d4d693a2593eb",
	},
	{
		.a = "15a78159fcd81c039d997cb2266513d677a36856756ccebd"
		     "7942fe063a99a42b",
		.p = "c3b321a39659c8c574148821cc2f4c23",
		.sqrt = "4a746881ec00b6cb8e637daf97c7f7d1",
	},
	{
		.a = "17ec38e2fbb20de601b699aab3a420d174a2c541938ea004"
		     "a65f97bc3713a273",
		.p = "c3b321a39659c8c574148821cc2f4c23",
		.sqrt = "4e41e59e7b80b061ae255fc98748d2d7",
	},
	{
		.a = "f8327d6ecd54a96d4cb2d1b5ac4be958a1073fd3c9216f18"
		     "4a2e1b65c80c28a",
		.p = "c3b321a39659c8c574148821cc2f4c23",
		.sqrt = "3f04610bae1450420bc38988dfd57fa4",
	},
	{
		.a = "1c8ad905faae5434f6070960507696b2879d5e7891d3073f"
		     "66a96bdf74c1a56c",
		.p = "c3b321a39659c8c574148821cc2f4c23",
		.sqrt = "557ae7ee14896190564703f963bb6516",
	},
	{
		.a = "21bbfe1ed600301ead629d7b87e11c5403bd16dd9c28aaca"
		     "03fffbf6597c1f47",
		.p = "c3b321a39659c8c574148821cc2f4c23",
		.sqrt = "5cee17372e523911e1a0f914db42230d",
	},
	{
		.a = "250de7ab5e802b1b399e8c3333b20d18dbe433ea28c333a2"
		     "0a9db317c5c96f2f",
		.p = "c3b321a39659c8c574148821cc2f4c23",
		.sqrt = "616546bc036efa2a5a30d35c1cb32ec9",
	},
	{
		.a = "20ae8c29c0aa7030c63efd48fb9f371f5a15fb1918859d74"
		     "d91a160d4533c73",
		.p = "c3b321a39659c8c574148821cc2f4c23",
		.sqrt = "16de015e7590bb75c7f9a0d719368285",
	},
	{
		.a = "a9e4dd7fea7f671c392560cc6abd8241ed88655d4adc4907"
		     "1b7f2151d7931f",
		.p = "c3b321a39659c8c574148821cc2f4c23",
		.sqrt = "d08ca78515bb8670926a12f02ae69fa",
	},
	{
		.a = "1a0126ea28387c8caf8a1b0b8440969407279c759673171b"
		     "80ba9a42281cadbc",
		.p = "c3b321a39659c8c574148821cc2f4c23",
		.sqrt = "5197642a69ddeb7c366b25a634a6c33b",
	},
	{
		.a = "52006bab38c980f87129d9d1b0d7a9559b0a8f304ddefee1"
		     "81b9d2874d12cbb",
		.p = "c3b321a39659c8c574148821cc2f4c23",
		.sqrt = "2438cea95ea42a348190d5d004ca0f6f",
	},
	{
		.a = "43e921a1d7d2706dc74144886815c02719a6e3c9",
		.p = "c3b321a39659c8c574148821cc2f4c23",
	},
	{
		.a = "fe62c1aaa54f6abba02bf1add8118c2cc57ae9c",
		.p = "c3b321a39659c8c574148821cc2f4c23",
	},
	{
		.a = "1c70683aeb6b0025b8509da40e743f4f98f74f8b81dcbd2f"
		     "b83c72059f45d986",
		.p = "c3c9132a60dcb07a29ea04ce12f10af7",
		.sqrt = "555346fcb482ad8a501120ab9176fece",
	},
	{
		.a = "c30e49d25c864756e6432366defb5a7cd8cb3f18eda3cde2"
		     "6c88aec3a0ae004",
		.p = "c3c9132a60dcb07a29ea04ce12f10af7",
		.sqrt = "37dd6d96b275bff258b4fde3321a9217",
	},
	{
		.a = "331e18f1f1ed03ac53c78f2d98bf9d2757127d3c25ec0cdb"
		     "f02882cb70595f",
		.p = "c3c9132a60dcb07a29ea04ce12f10af7",
		.sqrt = "7264fcc091e4523541e8f0ee130571e",
	},
	{
		.a = "3c9c518aec7a7b502e49dca16acb91f9f1d57c7ca69dd199"
		     "d55212b710a1f",
		.p = "c3c9132a60dcb07a29ea04ce12f10af7",
		.sqrt = "1f242105b1371a672211389a0b02ae5",
	},
	{
		.a = "a3c104e9871de2bf7e2f3e1f2e072f1d869a72935928e293"
		     "627f04c0fe9567",
		.p = "c3c9132a60dcb07a29ea04ce12f10af7",
		.sqrt = "ccbf0901efa15d8d0d8758a57e2ab4f",
	},
	{
		.a = "c60bb5e0a48db72bc0cebe9455e9f94a08febd08d634591a"
		     "cdd1a7433eeffd9",
		.p = "c3c9132a60dcb07a29ea04ce12f10af7",
		.sqrt = "384a9f1f39e3a04e3ec7e3b99cdf123b",
	},
	{
		.a = "22d9c628e23c14d45eaac1bb5c563cb4718556d31798e754"
		     "bb7f81a3c6b911d",
		.p = "c3c9132a60dcb07a29ea04ce12f10af7",
		.sqrt = "179d2110991a0f85ad40927cb08a9c15",
	},
	{
		.a = "20354be2a9992a4f192687ab68025fe85b9724faea87f37d"
		     "39a43d26b71c4ee",
		.p = "c3c9132a60dcb07a29ea04ce12f10af7",
		.sqrt = "16b36e6bffa91122c3615aa277cc249c",
	},
	{
		.a = "3ad95db7205d9413ed31b2145c4b368db365e949e54c4dbf"
		     "0acaef2bfb54304",
		.p = "c3c9132a60dcb07a29ea04ce12f10af7",
		.sqrt = "1eaf6d6949ddfe0d9b915b2c9f472f78",
	},
	{
		.a = "1aa4354b3a677970dfccafe223df531fd9c753f91d802ba4"
		     "35362bf5ac66c11e",
		.p = "c3c9132a60dcb07a29ea04ce12f10af7",
		.sqrt = "5295a52e193ac5011d91f2bbeef1691e",
	},
	{
		.a = "f48312e68f2a47df61bff728ac986ec049c283",
		.p = "c3c9132a60dcb07a29ea04ce12f10af7",
	},
	{
		.a = "897e77b7f767ac6a99d4be1ac2a4e52153884672",
		.p = "c3c9132a60dcb07a29ea04ce12f10af7",
	},
	{
		.a = "105505111e51c9a73001339ccff3554c2bdd879baa46210e"
		     "1df8f73b70527a97",
		.p = "e84fd723abb1fc0208ad23dfe5986c97",
		.sqrt = "40a92a8fcac919aa46ee0ea2a6ccdc66",
	},
	{
		.a = "330b1cce03412398a36967102c7cf6e47534b630c60ceb26"
		     "04bf2e3842bb994",
		.p = "e84fd723abb1fc0208ad23dfe5986c97",
		.sqrt = "1c93ef2631f86d57ebc0c843e2082fe7",
	},
	{
		.a = "1cfe5a70784209591d7ced341bba4484e1603226449e52b0"
		     "a16d9afa9aa1fc0e",
		.p = "e84fd723abb1fc0208ad23dfe5986c97",
		.sqrt = "56273048848c7784c2589e3d91dbceaf",
	},
	{
		.a = "7baa8b5f02cdd3451df420b504bd8e16c738a07e7f9cb987"
		     "9944b99e1496e7d",
		.p = "e84fd723abb1fc0208ad23dfe5986c97",
		.sqrt = "2c7b6c3c5e43c0f2acd1ebfee4e234ca",
	},
	{
		.a = "608d55d43908ea7ea3aaf4ca05d5a8321d55cbe09b565e4c"
		     "163412e45c62386",
		.p = "e84fd723abb1fc0208ad23dfe5986c97",
		.sqrt = "274deb2741766cffb60a517e8f3fadbc",
	},
	{
		.a = "526ebff9146c9f50021f56304120d8f9b09ce515d0066346"
		     "8f20115421f024",
		.p = "e84fd723abb1fc0208ad23dfe5986c97",
		.sqrt = "91449230a1a386e9c1093ee0cdbba09",
	},
	{
		.a = "2a20b65b2a28f1b3c9e1d876d86a5f5d87d94421ddf7ef69"
		     "1a71f07955a09a8b",
		.p = "e84fd723abb1fc0208ad23dfe5986c97",
		.sqrt = "67d976d658fd3cbd77c1474a0906404c",
	},
	{
		.a = "1e083613b3a5b9f547972034d37a1f6f42733731dabbe889"
		     "99b5c0f50a7dca82",
		.p = "e84fd723abb1fc0208ad23dfe5986c97",
		.sqrt = "57aeb4be88de6be03a7a453a7ba5b50f",
	},
	{
		.a = "11d00fbce28d46f526bb51246a1cdd3c3b2d8bb83725a83a"
		     "564f6a67d23a0ff",
		.p = "e84fd723abb1fc0208ad23dfe5986c97",
		.sqrt = "10e1ce79ade89e839f3f0f29d72eea8a",
	},
	{
		.a = "3285f0c5eb6ff451af071089be87c1a7fe564e1e8de5911f"
		     "fe390fda34c8799e",
		.p = "e84fd723abb1fc0208ad23dfe5986c97",
		.sqrt = "71ba3c5d0d93cb2da4a8242269f8f349",
	},
	{
		.a = "d0d1f950ec1b85862398f403e8768936ea03cf1d",
		.p = "e84fd723abb1fc0208ad23dfe5986c97",
	},
	{
		.a = "e5a7a57d99efdd8965c84e10b83df9d0871cc1e",
		.p = "e84fd723abb1fc0208ad23dfe5986c97",
	},
	{
		.a = "102ce98ef5044dc2f1afa2e6600008198b5329aa0757d239"
		     "3f6860f7e252f5db",
		.p = "dfcf17789278b6e9cafdb4beaf2cfcfb",
		.sqrt = "4059946cd6922b83dd0425fc4acb6651",
	},
	{
		.a = "132922d347e42176551bcf327fcb764380a996142bcec3a7"
		     "e5c55f822c53e99b",
		.p = "dfcf17789278b6e9cafdb4beaf2cfcfb",
		.sqrt = "4609639f50503b898f97fcbce84bd069",
	},
	{
		.a = "1b1be4c1234332b5ac8494697d1de921f851d9c2b4557931"
		     "125b94f68229dbc1",
		.p = "dfcf17789278b6e9cafdb4beaf2cfcfb",
		.sqrt = "534e5790710771e405fa386a75fddcee",
	},
	{
		.a = "272ac5b81ce6e27b4b5aac25ca34abfe8725d28f7f67c8d1"
		     "9d16d2fdbdadf2d5",
		.p = "dfcf17789278b6e9cafdb4beaf2cfcfb",
		.sqrt = "64223ee58ff69643ec0b4cabda6fa41b",
	},
	{
		.a = "77a6fa94c13869d5cc265d70edc83bfed1d0e1b0ac8b89ae"
		     "e56580f73696b4",
		.p = "dfcf17789278b6e9cafdb4beaf2cfcfb",
		.sqrt = "af045f74c28b50570b7d2ed8a53705c",
	},
	{
		.a = "207b2287539f1489a52c29d82415be4d2dbcc256c8f58f51"
		     "697dacc3ea2c091c",
		.p = "dfcf17789278b6e9cafdb4beaf2cfcfb",
		.sqrt = "5b2ff6ea861f522cbe4aeda70ec954b3",
	},
	{
		.a = "a6322e0055de7fd226828206a28c61091a8221449c39bbd1"
		     "969e9cdb1cbd7b3",
		.p = "dfcf17789278b6e9cafdb4beaf2cfcfb",
		.sqrt = "33911aaca827e264d82da9b63fe25c98",
	},
	{
		.a = "5c0625d646c4183b59ccea7fb137399802389d925c999c8f"
		     "34700cb1edd482f",
		.p = "dfcf17789278b6e9cafdb4beaf2cfcfb",
		.sqrt = "265f251061587b79dc4bb3e2dbbddedc",
	},
	{
		.a = "ab2267257b8fb94428bfa7782d114a8189ad9188af7c35aa"
		     "2f58851104a3c3",
		.p = "dfcf17789278b6e9cafdb4beaf2cfcfb",
		.sqrt = "d14f31646a99511d5df395ee0c6f23b",
	},
	{
		.a = "2ccb942f89fcd0f396e6f60dbf26faba338a9f42a08ae83a"
		     "8349881f4d2c9707",
		.p = "dfcf17789278b6e9cafdb4beaf2cfcfb",
		.sqrt = "6b1637561bc8e2b7c6797ff14898b78a",
	},
	{
		.a = "49e501a514b25ec54e97e0643fef58c2331a9e0",
		.p = "dfcf17789278b6e9cafdb4beaf2cfcfb",
	},
	{
		.a = "bda6072e85a4703a58766eb147beca3a9e407324",
		.p = "dfcf17789278b6e9cafdb4beaf2cfcfb",
	},
	{
		.a = "e505c659e3649ac1974baa7ecf478c9c46d988149506c124"
		     "f528a8a1da13bff",
		.p = "dfe11f28376b5562def9fbecfa68ca47",
		.sqrt = "3c88b1ef1aa5441216ef7c2457edfb52",
	},
	{
		.a = "88db3bfef58b46e08c36ddcc96ff4588a8c10311ddac5620"
		     "682e92351245c4",
		.p = "dfe11f28376b5562def9fbecfa68ca47",
		.sqrt = "bb2d50c0a9fc425d2f093f5e9882845",
	},
	{
		.a = "eccf316a16ed9aa505b82e99a9c089513f3f5465178c0595"
		     "5e1c8e7138da57b",
		.p = "dfe11f28376b5562def9fbecfa68ca47",
		.sqrt = "3d8df00919e543a5f782cec0e2833eba",
	},
	{
		.a = "23a72d78c1d23f89f21c9aff268ada5431568d136b8980cf"
		     "cc72b27c7ab4e93b",
		.p = "dfe11f28376b5562def9fbecfa68ca47",
		.sqrt = "5f89488ed8134af87ee97152df43de42",
	},
	{
		.a = "1863d4f3fbbcc5ad3cf555c22d5a34045e91039acfe30365"
		     "4fce949c31d5aba",
		.p = "dfe11f28376b5562def9fbecfa68ca47",
		.sqrt = "13c1259e5a87acded9d3999bdd6e7cae",
	},
	{
		.a = "be8a91ec3aed7e990a3a01e12572b1bcf131a6dd93d46e4f"
		     "c6719ec22197d0",
		.p = "dfe11f28376b5562def9fbecfa68ca47",
		.sqrt = "dcdbd473cf66c3ffb560ff8fb1411d5",
	},
	{
		.a = "b268924f724222d8df0d48f502c6bd33b406742efe65b153"
		     "3f9c34d30819ff5",
		.p = "dfe11f28376b5562def9fbecfa68ca47",
		.sqrt = "356d87ec862cd46e67997119202dd092",
	},
	{
		.a = "c860b34209538b9b8efbb4bb51a70a22c1003dec12e5474d"
		     "2f061f29b1c1c0d",
		.p = "dfe11f28376b5562def9fbecfa68ca47",
		.sqrt = "389f374589c29d78ffd4fddfcbc9fdad",
	},
	{
		.a = "857bb672df76352d6ed3259cdf20a65d941b7c3f2ea07a19"
		     "c513605e4b502bb",
		.p = "dfe11f28376b5562def9fbecfa68ca47",
		.sqrt = "2e36c7760d3a9ab2d437ba376940b71c",
	},
	{
		.a = "248ebd6e07fabf93019b22ea1d1ab4d0fc9eba3d780d7307"
		     "429a57739066844b",
		.p = "dfe11f28376b5562def9fbecfa68ca47",
		.sqrt = "60bd96b2e42a4ba4e1c5529dbc4b31f9",
	},
	{
		.a = "7f0b66aca27105e059b111992986f46a71b25c9a",
		.p = "dfe11f28376b5562def9fbecfa68ca47",
	},
	{
		.a = "a964ffcec8532b0209457c1eda9988e40fe37482",
		.p = "dfe11f28376b5562def9fbecfa68ca47",
	},
	{
		.a = "12dc5769139571557c0af544a1eca5bec5f9b3cf70cc87d2"
		     "d48107c5b4d20d98",
		.p = "fddaa93d6ef55fe16d65f36467751ff3",
		.sqrt = "457c7c100eaa11609392f6af4fbdc07f",
	},
	{
		.a = "10d05ec3bd88a33465a1c5d9d467b587c2ea46f93a369223"
		     "29d1893adf131b9d",
		.p = "fddaa93d6ef55fe16d65f36467751ff3",
		.sqrt = "419b922a2d4e0fdb18d0e31a130a3b57",
	},
	{
		.a = "34d65ba3637b3b9d46a7d1a3558d7e3e946761ae0b8e4db5"
		     "1c4b9b6bdfde0492",
		.p = "fddaa93d6ef55fe16d65f36467751ff3",
		.sqrt = "744d88ffa822b5d259c4fd138e489067",
	},
	{
		.a = "292612ef1616fff0453f2e66580cffc763dad52f12ec5c37"
		     "6246c4d8f69c0062",
		.p = "fddaa93d6ef55fe16d65f36467751ff3",
		.sqrt = "66a2b919787e57087ca4fd47b6ad37cd",
	},
	{
		.a = "3a7ec6f19e0d9a4cca6f9bd8e2d126519c9370a88fe61879"
		     "a0ccef7a6d5ccb23",
		.p = "fddaa93d6ef55fe16d65f36467751ff3",
		.sqrt = "7a5f18c79265b00cbaedc8bbef8cc26d",
	},
	{
		.a = "b2f41bfdd2c86f5115724830a7dff681ad4f06198f7c989a"
		     "b8ed9912f690fe7",
		.p = "fddaa93d6ef55fe16d65f36467751ff3",
		.sqrt = "3582689e0e3d046dae757b3e395f2a09",
	},
	{
		.a = "12b9a1b6c4f1bd7888f206445c91c722219e1ced30bb2ee8"
		     "5eb1a511347c430b",
		.p = "fddaa93d6ef55fe16d65f36467751ff3",
		.sqrt = "453c6e4824654ae983a123749adf41e1",
	},
	{
		.a = "4a7c46417c3fefe77ee50565fe79aaee7886e1b708eebba8"
		     "84fd59d616e1c8",
		.p = "fddaa93d6ef55fe16d65f36467751ff3",
		.sqrt = "8a16821ca0a63ff8b03b4b1a6afc5b5",
	},
	{
		.a = "70b12a7a215f436eac38a7b2e9bdeae47f4e313c9622bbf0"
		     "ddc4ae3cdd48ef6",
		.p = "fddaa93d6ef55fe16d65f36467751ff3",
		.sqrt = "2a766d514a8eeb2518e55d56d435b5b3",
	},
	{
		.a = "1127295ddd79b3dc0d36759e8475e793c903136b3d779ba4"
		     "a4b40eaaa2510cec",
		.p = "fddaa93d6ef55fe16d65f36467751ff3",
		.sqrt = "42440e1bdce37bc4099b36183270b686",
	},
	{
		.a = "780039d1fb613f1654993c81ebce1000256ec462",
		.p = "fddaa93d6ef55fe16d65f36467751ff3",
	},
	{
		.a = "55f30fcaf020db602e2db2bed4ed2cdd061683d9",
		.p = "fddaa93d6ef55fe16d65f36467751ff3",
	},
	{
		.a = "297aa82c160b09d1551443f560c355085c075871687b98af"
		     "3b2028156e24fa4b",
		.p = "db9874b4d0f7b1cfcb69168102ef111f",
		.sqrt = "670bff83e4425dbc782a92735135b8c7",
	},
	{
		.a = "aa4b8a060fcf9f4adb09ccd99c58a0d71023319539618b60"
		     "8be198e4e19898",
		.p = "db9874b4d0f7b1cfcb69168102ef111f",
		.sqrt = "d0cba27ce684c00b2b22b661305ab22",
	},
	{
		.a = "4307405764f9c05f6b8a433fd442e97b20b6dd725a0c5488"
		     "fc0947daf6daecc",
		.p = "db9874b4d0f7b1cfcb69168102ef111f",
		.sqrt = "20bf92a53eeb49fc15a28fa47156c7e1",
	},
	{
		.a = "25e25f800892a56195c46c027eeab26ec820354e889a8057"
		     "a46da80343dfb960",
		.p = "db9874b4d0f7b1cfcb69168102ef111f",
		.sqrt = "627af6200712b962235b167671d6859f",
	},
	{
		.a = "1ee2add6b546b09bfa3acaa640feff18f0a844638a3e526e"
		     "a9c22ff1657802e0",
		.p = "db9874b4d0f7b1cfcb69168102ef111f",
		.sqrt = "58eb64eee715b3535a68d8d4f4c389c9",
	},
	{
		.a = "22c0b5472fee1bb7876e8612b63676678f0815a5a5984718"
		     "d44000eab1632e6",
		.p = "db9874b4d0f7b1cfcb69168102ef111f",
		.sqrt = "1794a1976ec2bb6f199e9e833c9bc7c9",
	},
	{
		.a = "c31233d4d15b615d95bf209890c2bc3446800af361aa69af"
		     "3d65390eb64b01b",
		.p = "db9874b4d0f7b1cfcb69168102ef111f",
		.sqrt = "37ddfd137bf27b3ce0f21c86c844cdf4",
	},
	{
		.a = "12977980fc461d4cf8f61b364e59783c589b5ff4dbb0f67d"
		     "5ef13413dcf58afd",
		.p = "db9874b4d0f7b1cfcb69168102ef111f",
		.sqrt = "44fd2b8b167f4c5a14cabe9344b2e337",
	},
	{
		.a = "3ae43a729fe988282a2a3f2e821aa58409784a661741bf05"
		     "237563c1025edef",
		.p = "db9874b4d0f7b1cfcb69168102ef111f",
		.sqrt = "1eb2423fde03aed73f280a8500172693",
	},
	{
		.a = "25988dd5cd06919bd5cb184431955d865033c58c65466597"
		     "fcb737ac19f27492",
		.p = "db9874b4d0f7b1cfcb69168102ef111f",
		.sqrt = "621ad4ed8769873118ac6279833549c3",
	},
	{
		.a = "b1216cf6c24d756b71dc6cf2c88becfb95bdaf4e",
		.p = "db9874b4d0f7b1cfcb69168102ef111f",
	},
	{
		.a = "15ad69f58f2c2f4e820e73120f229c70e4facc3",
		.p = "db9874b4d0f7b1cfcb69168102ef111f",
	},
	{
		.a = "aa9714b318b654674644c3daac851bbd4f39d01dfd89e3af"
		     "8e099d3b8903329",
		.p = "cd1f525d3eb5172c591bb299f52ba447",
		.sqrt = "343e7b2f7e303f97e6dc9db20894ac1d",
	},
	{
		.a = "40597523833f21d1fee6c963939dd73397e2a2352db1b049"
		     "1b0f8fd50b1bdd5",
		.p = "cd1f525d3eb5172c591bb299f52ba447",
		.sqrt = "2016557da8396370d7a7c3d9a947e666",
	},
	{
		.a = "10ddb7215a1e681614b6d7324b92b24fc715ed863d5038be"
		     "05d740824a0b3114",
		.p = "cd1f525d3eb5172c591bb299f52ba447",
		.sqrt = "41b5964db90af860d17fbbcdd4afcc55",
	},
	{
		.a = "1c36ce697e95d06ec63af3b82a6c2d8e9622c2c33f3f87ea"
		     "ac3e75f2abef8ce",
		.p = "cd1f525d3eb5172c591bb299f52ba447",
		.sqrt = "153f2c9007b549d8e802a903ffd2171a",
	},
	{
		.a = "35a13697c77f9629ef70d36fae88659d39b4048dc67828bc"
		     "4425d1cb41edbbc",
		.p = "cd1f525d3eb5172c591bb299f52ba447",
		.sqrt = "1d4afd8ce507d6950e0b714c27e5e4c2",
	},
	{
		.a = "1886d861ea3d46d0edb942a01397780a84f5ce199e6352d7"
		     "42ed995a94302701",
		.p = "cd1f525d3eb5172c591bb299f52ba447",
		.sqrt = "4f3d39e8213df646cee264182fef1f47",
	},
	{
		.a = "143c8b6264d12017961d3829d3aa0088ce4e8c6261a39e68"
		     "0ea5b276d427e465",
		.p = "cd1f525d3eb5172c591bb299f52ba447",
		.sqrt = "47f9db1679dd06966c91bb7ee81f22c3",
	},
	{
		.a = "121134f6cb228fafbd45a934db668a78d27aa12fb87ab25b"
		     "e714ec93d05137a5",
		.p = "cd1f525d3eb5172c591bb299f52ba447",
		.sqrt = "4402458a9a80428c77dda8334ff41f74",
	},
	{
		.a = "1ba2eabc24b78ce8ed873c0aba09443e5f02b98858c0a09a"
		     "bfdb6d4da798abc3",
		.p = "cd1f525d3eb5172c591bb299f52ba447",
		.sqrt = "541cce775b911592fb759167755743f3",
	},
	{
		.a = "e58fe89a18f8200b4aa289446b6de2ae4e4b0ad911e59cf0"
		     "1e0a0b32b609141",
		.p = "cd1f525d3eb5172c591bb299f52ba447",
		.sqrt = "3c9af08f9f626f41c7ebe2f32875497a",
	},
	{
		.a = "b15263e714de941bb702cf71e0db12613a383de7",
		.p = "cd1f525d3eb5172c591bb299f52ba447",
	},
	{
		.a = "5deae3520dc28e0e3bdee22fd91a66dad70b41c2",
		.p = "cd1f525d3eb5172c591bb299f52ba447",
	},

	/*
	 * p = 5 (mod 8)
	 */

	{
		.a = "495114892a3986a8b4a6e05bacc0e293421716b0c4557330"
		     "0cf4243f784e33d",
		.p = "c9b4dee4aa04ce668cedd3d2bb8395fd",
		.sqrt = "224004cbf31a7c967d53819764506317",
	},
	{
		.a = "3f7683a65a7b5ab1134a944cb831bb8a0f9d9835a45d230b"
		     "d3c6b8bb3902c1",
		.p = "c9b4dee4aa04ce668cedd3d2bb8395fd",
		.sqrt = "7f7639804be2c91aed7cdb70e1af18e",
	},
	{
		.a = "9b044b4d448169fbb7da81261016b997ff24b3921b03dfb1"
		     "d6dc5b1b28de2f1",
		.p = "c9b4dee4aa04ce668cedd3d2bb8395fd",
		.sqrt = "31cd63124ff7c306503ea3a2437bdbb0",
	},
	{
		.a = "128b830f7eed54c5b7cfeced129aa45ad857d6e3d9f2a70e"
		     "8cbf6097607ba1b0",
		.p = "c9b4dee4aa04ce668cedd3d2bb8395fd",
		.sqrt = "44e6f601015c26b885d7cd32663bc970",
	},
	{
		.a = "1883fe98086c0821ff2858347227720a967a2c468a8ad9ab"
		     "018257dd49ec8a18",
		.p = "c9b4dee4aa04ce668cedd3d2bb8395fd",
		.sqrt = "4f389ee648fb323847e453c77db0a364",
	},
	{
		.a = "23ab14a89775f60cc6d8c397711429e9aae1fa59b637d356"
		     "f7a8fbfe6b5379ef",
		.p = "c9b4dee4aa04ce668cedd3d2bb8395fd",
		.sqrt = "5f8e83217d7de9d61afc3edb95912f2f",
	},
	{
		.a = "7c1252084891eb7e2b7d1c5ff6102949bf2b598a6ee05a0d"
		     "03ec0471f963fbd",
		.p = "c9b4dee4aa04ce668cedd3d2bb8395fd",
		.sqrt = "2c8e124682ee306b16eebb31464c868f",
	},
	{
		.a = "824e2173788853d264c075c287c558407ab196193af02427"
		     "b7f7be09945bfcc",
		.p = "c9b4dee4aa04ce668cedd3d2bb8395fd",
		.sqrt = "2da917e56d3146f4d6e496bcfb50509d",
	},
	{
		.a = "506661349ca4c1c1d32c030ebaec699f584e537fd561c5a5"
		     "40bb2339775ad6",
		.p = "c9b4dee4aa04ce668cedd3d2bb8395fd",
		.sqrt = "8f7731dd48b0165519d0dfaf629cd81",
	},
	{
		.a = "d21846fcb456b5cc76e2e9e95c72cd8d10537f2f91b7817c"
		     "06bb2d946f9e1fd",
		.p = "c9b4dee4aa04ce668cedd3d2bb8395fd",
		.sqrt = "39fa851e5ff14e140a6f98124bcbf2bf",
	},
	{
		.a = "3727e2a9498ef20c8a6f0d2f8de533d664bd8f2a",
		.p = "c9b4dee4aa04ce668cedd3d2bb8395fd",
	},
	{
		.a = "2eef7eeaf13b6173f9c68f71cb3a83621672810b",
		.p = "c9b4dee4aa04ce668cedd3d2bb8395fd",
	},
	{
		.a = "7695ae6d4aa3f51985234f35cded2e378912e848d1e08bb9"
		     "921dce2b937aa8",
		.p = "c7ff289dd9db4bc68a80d6d0837f5195",
		.sqrt = "ae3c0c05a16cb2ad83b0dde2f46d15d",
	},
	{
		.a = "6932c69d54861b47fe83f8531f19d3af58d3b85737e1b540"
		     "e2f434b05eb4ffa",
		.p = "c7ff289dd9db4bc68a80d6d0837f5195",
		.sqrt = "2906c888cd5fe48ccca5c925944559bb",
	},
	{
		.a = "22894e6a44618c1a14c10695cf41b5083f7f6d8a34c58309"
		     "9334b3ec5a53a6de",
		.p = "c7ff289dd9db4bc68a80d6d0837f5195",
		.sqrt = "5e07397aa72c55103caf053fd20540c1",
	},
	{
		.a = "d5189535263311b02c0b50f869524ae54ff3b493ccc15952"
		     "330792e5676feac",
		.p = "c7ff289dd9db4bc68a80d6d0837f5195",
		.sqrt = "3a642808df53bce255b9d4ec52316a36",
	},
	{
		.a = "462df11d760ac7ba6320dce59dd012a5a13e81e93a4a23a3"
		     "2c2bb60314bc564",
		.p = "c7ff289dd9db4bc68a80d6d0837f5195",
		.sqrt = "21825fb45eaf1c3cf86b8b6ec3f3e4a6",
	},
	{
		.a = "1e2fbc91cf281ba1a838e600f8a7348b03daa0d6dc5ceb82"
		     "788539919839184d",
		.p = "c7ff289dd9db4bc68a80d6d0837f5195",
		.sqrt = "57e854eb15ea535a9210c70e2b1d0397",
	},
	{
		.a = "1ac89e6b45c1f3d3888411a374f291e52af2abfbd7dcf23d"
		     "e8024623ace61a1c",
		.p = "c7ff289dd9db4bc68a80d6d0837f5195",
		.sqrt = "52ce01029221f38a79750615d89c83e2",
	},
	{
		.a = "408ada83c389027bb76db1c90279ead9b9855b604403ef4b"
		     "b2821f807a2f5dc",
		.p = "c7ff289dd9db4bc68a80d6d0837f5195",
		.sqrt = "2022a3e12dff46cfd45e4b670e14f5c0",
	},
	{
		.a = "17157376051a1da95618c37fa6a1febd48cbc52dcc3150eb"
		     "c590f93be06991e8",
		.p = "c7ff289dd9db4bc68a80d6d0837f5195",
		.sqrt = "4cdf79e5c4a5e9413612c09652b60588",
	},
	{
		.a = "c43c6a117ba38669cadfebc127dcc99a3292fe772077b322"
		     "d47c1f62bc8ed29",
		.p = "c7ff289dd9db4bc68a80d6d0837f5195",
		.sqrt = "3808a0c60e94e3bf5453c16c14b6e936",
	},
	{
		.a = "7899729705aaa981ccd5b22ca3b9384c957e6d8",
		.p = "c7ff289dd9db4bc68a80d6d0837f5195",
	},
	{
		.a = "aa82a2131cf182e08daa64945131572020110835",
		.p = "c7ff289dd9db4bc68a80d6d0837f5195",
	},
	{
		.a = "3b2c1fa25b77539a5adbc8daaaf3afb1660be07651f3f0e1"
		     "77331853bcd49b21",
		.p = "fc5054f3123deeb6ca2d35c77263c08d",
		.sqrt = "7b13e508e84efddeb4eeae18144046db",
	},
	{
		.a = "3b98fb79556ae299a2d9349f8e9544fe7c3fc9b0a7265ec4"
		     "00dfd31c139c1869",
		.p = "fc5054f3123deeb6ca2d35c77263c08d",
		.sqrt = "7b84e784f47a5586e6c3cb970bc98a03",
	},
	{
		.a = "149c120dc19610055d7f0ba9d007f458c558c68a40acd0a6"
		     "00bb40d2650ee1f4",
		.p = "fc5054f3123deeb6ca2d35c77263c08d",
		.sqrt = "48a2f5e66dc7919bd19d00549d177c3c",
	},
	{
		.a = "13c3763c5d74356dafe968f30e1d9a4f8e6c63c6101a02ea"
		     "ec1cec8db01798bc",
		.p = "fc5054f3123deeb6ca2d35c77263c08d",
		.sqrt = "472140c031a001ef403cb71274a91830",
	},
	{
		.a = "8353728f214d74a60f740e3098dc80c8bf3b3878499b71e7"
		     "06ded8c999a5c68",
		.p = "fc5054f3123deeb6ca2d35c77263c08d",
		.sqrt = "2dd6c9cf6caa11c9051d6823a4775155",
	},
	{
		.a = "5e387c6058eeb03b33b59fce405c6278d7e373083fe3ea9c"
		     "f1d64e8be3f58e7",
		.p = "fc5054f3123deeb6ca2d35c77263c08d",
		.sqrt = "26d3b19397db8053cf072f385f71f81e",
	},
	{
		.a = "22eca556be0a5ee93e1bae520342f32331727c75dfca6082"
		     "3aac96741594cd18",
		.p = "fc5054f3123deeb6ca2d35c77263c08d",
		.sqrt = "5e8e13ba599582dc056f0896c4cbae8a",
	},
	{
		.a = "308e76f45903562ae8c4a56fd9e181cf2b802586383cc133"
		     "7a186fe1d79c8726",
		.p = "fc5054f3123deeb6ca2d35c77263c08d",
		.sqrt = "6f7df34d1556913182384ed31aae29eb",
	},
	{
		.a = "9b572efdd07a8a9cc36f20d040d92f3a90fd87a4e0aa651a"
		     "f71361589d360bf",
		.p = "fc5054f3123deeb6ca2d35c77263c08d",
		.sqrt = "31dab1ec08d2030a50cae92533215502",
	},
	{
		.a = "2f720cb02294a4b8b69f41cf5f7071f7010f10038f1e603b"
		     "1394fdb65d388d5",
		.p = "fc5054f3123deeb6ca2d35c77263c08d",
		.sqrt = "1b8d622e0887aadbf3113f7e9c923373",
	},
	{
		.a = "dd9e74c33015c49e90c1b54e29a123bb759e77f0",
		.p = "fc5054f3123deeb6ca2d35c77263c08d",
	},
	{
		.a = "da290f4e04f8dfc3c07e43dbb7121b91ee2ee29d",
		.p = "fc5054f3123deeb6ca2d35c77263c08d",
	},
	{
		.a = "111aa51f58dcaae1d55a7f50cdb2e15da632407008ca0cc3"
		     "7ce53d6f53ea2a",
		.p = "e7616abd0448ce8b22a99105ef99f00d",
		.sqrt = "422bdc54895f3fbab7fdc8e8bfe7edb",
	},
	{
		.a = "19381515e93479f7e5d4ce92ce1ee883ed9f50e5802d120d"
		     "b390d62df8736b6",
		.p = "e7616abd0448ce8b22a99105ef99f00d",
		.sqrt = "1416624ee1b331632ede4da5a72c679f",
	},
	{
		.a = "605525d619104f91c7df399e6d68b960ff69d810ae481754"
		     "5bfa65a00d67dea",
		.p = "e7616abd0448ce8b22a99105ef99f00d",
		.sqrt = "274279c61f21177b40e140015ac3b690",
	},
	{
		.a = "e59076d17860b58cf92dc7edbba52d72c3cfa5baf141c600"
		     "8bf897afc3a0d0e",
		.p = "e7616abd0448ce8b22a99105ef99f00d",
		.sqrt = "3c9b0355733638764f3529906f702f46",
	},
	{
		.a = "1b99dda206922285a173c2e05547e366061c908b7a23af82"
		     "fe78a3f21c6917",
		.p = "e7616abd0448ce8b22a99105ef99f00d",
		.sqrt = "540f073d38ac532e5382f9afc215523",
	},
	{
		.a = "543e02b3d7c30e8ff3a141aaf6a150f01b9874b033344ce8"
		     "348ddbff79aca9",
		.p = "e7616abd0448ce8b22a99105ef99f00d",
		.sqrt = "92da8c6295ced9d22457eff8df968bb",
	},
	{
		.a = "2e6d5bf0b9d6ba16af020be648c01aa77b2717ce9262f23e"
		     "ebf2e2dfb11fd125",
		.p = "e7616abd0448ce8b22a99105ef99f00d",
		.sqrt = "6d051e57847d8f83e8e003e833c514ca",
	},
	{
		.a = "179d7fc2e81864cbd54dfa14306c06fcecfc65ed159f1f4b"
		     "dbd4ba616ceb064e",
		.p = "e7616abd0448ce8b22a99105ef99f00d",
		.sqrt = "4dc0b7fd597e46fdffe17ed9b8928355",
	},
	{
		.a = "3275ac40f4e32e5343d33881d4b3994820572bb121ece88e"
		     "c7995e668017ef56",
		.p = "e7616abd0448ce8b22a99105ef99f00d",
		.sqrt = "71a7ebbb86f85e267d71b796b233b773",
	},
	{
		.a = "34d9fd618cd45afdbe87718ad19d8377d17619376bdaecc0"
		     "ddb8695e035846a",
		.p = "e7616abd0448ce8b22a99105ef99f00d",
		.sqrt = "1d14620b9c69abad878797bfb2fa3bd4",
	},
	{
		.a = "e34380a3d760ab9415cd794c58569e3687568038",
		.p = "e7616abd0448ce8b22a99105ef99f00d",
	},
	{
		.a = "4e3af908dac0c11da23d73e9b1f2d6d3d5e94906",
		.p = "e7616abd0448ce8b22a99105ef99f00d",
	},
	{
		.a = "6ea9d07bd5e2ce59ce999d82dde6e42cfc90ba60f0b70c17"
		     "e6e4cef1c8d5fa8",
		.p = "c039e7b229cc3b7792b37ed541001005",
		.sqrt = "2a1422e259229156026fe1ecaecb2026",
	},
	{
		.a = "171dc2573d5011c8c9254adeef55f0ef572af94fa89bd329"
		     "d0f02d2f6fa916db",
		.p = "c039e7b229cc3b7792b37ed541001005",
		.sqrt = "4ced4e16d63f14de9e77c320d7e61dba",
	},
	{
		.a = "5da9be4927acc2a00f8eb2bd9007b7ea51c77e6440a1e020"
		     "75fbc67618e9de8",
		.p = "c039e7b229cc3b7792b37ed541001005",
		.sqrt = "26b63d2d91137017b1e5e0d3d15210e1",
	},
	{
		.a = "d32669bc65290f5b1fac5c04bcc3de52f582fb9dc22007f4"
		     "776a2fc05472435",
		.p = "c039e7b229cc3b7792b37ed541001005",
		.sqrt = "3a1fbf4661e72f835fc36be9075f43c9",
	},
	{
		.a = "89cb34dafdb05e01f54169143a88b7a9a4201dd66f12b957"
		     "6703e52cc176861",
		.p = "c039e7b229cc3b7792b37ed541001005",
		.sqrt = "2ef448e5f6e4c92d59db78cde0a066f1",
	},
	{
		.a = "5b8674a77fced1b08dd2376af9a96d4c4824786104ebf9f0"
		     "7fcc2ddd3ab0201",
		.p = "c039e7b229cc3b7792b37ed541001005",
		.sqrt = "26447c86fefaa791bf1d1c15d4607fc8",
	},
	{
		.a = "c54d49d0b63981375c4a9129ed4647a7e8a251045a244324"
		     "fd58d42baa8355a",
		.p = "c039e7b229cc3b7792b37ed541001005",
		.sqrt = "382f88a82f4719b32f2021eb845f7045",
	},
	{
		.a = "2256345a067ee92e441731a5007b08461aa7d2adef64664d"
		     "a92c40b7d79a9dc9",
		.p = "c039e7b229cc3b7792b37ed541001005",
		.sqrt = "5dc18f24fe2dd7bcf37ae865c9b48122",
	},
	{
		.a = "36da4779faa88d91d59824acd9b9bd00181aa5c7cfdf8243"
		     "7eb2544592c91c8",
		.p = "c039e7b229cc3b7792b37ed541001005",
		.sqrt = "1da00204dad7da917cb1719a9c826cd0",
	},
	{
		.a = "98f15c0e715dffc5a54dccee9c598314fdee262cee747268"
		     "2f585b7ff248ff8",
		.p = "c039e7b229cc3b7792b37ed541001005",
		.sqrt = "3177d023e1f4310118c00c362b422bf1",
	},
	{
		.a = "5bf6cf4b386b5de218728434e62fd0d802008bdf",
		.p = "c039e7b229cc3b7792b37ed541001005",
	},
	{
		.a = "13c22cf6c3c54b75f2c07830cf50777a5add43bf",
		.p = "c039e7b229cc3b7792b37ed541001005",
	},
	{
		.a = "8ea35eb078a0033844e5869778d9f581007c9c221338d1f6"
		     "4e09c528ac24747",
		.p = "d9c22e349011878f87754687ee523365",
		.sqrt = "2fc5c1c76763b2843384eaa1609285ce",
	},
	{
		.a = "274714f1060ba85737c3803bb6192cef8f1478448e0f3662"
		     "ad57fa2a6cbd13ff",
		.p = "d9c22e349011878f87754687ee523365",
		.sqrt = "6446686b34424dbb056c499ee47d6af8",
	},
	{
		.a = "1be91ab1e867e7c0ff4b9e039fe3a33515ade6590baba482"
		     "6d6ca8347f89082c",
		.p = "d9c22e349011878f87754687ee523365",
		.sqrt = "54875a1b0c7063fb4597f579a852bbce",
	},
	{
		.a = "b436cb73a7318ec101548cd18d97e350873563b145312a88"
		     "6c7e539ebcaa135",
		.p = "d9c22e349011878f87754687ee523365",
		.sqrt = "35b2914a881429245dfb9adaff55f249",
	},
	{
		.a = "485f18975843b575318dc7086eb45bdfdc54a8aed83b4189"
		     "934d2546fe231d7",
		.p = "d9c22e349011878f87754687ee523365",
		.sqrt = "2207504b2e026ad2c0754aadad5d322c",
	},
	{
		.a = "607d62025340dee0adc2de94c8dab17eb912c5cc8cc1589b"
		     "d227218c5ea6759",
		.p = "d9c22e349011878f87754687ee523365",
		.sqrt = "274aabccb65a4996273ebc7d1941981f",
	},
	{
		.a = "48a5421193d567325bc1d25965ac555d9384efa37843d840"
		     "b66b0c104f7618c",
		.p = "d9c22e349011878f87754687ee523365",
		.sqrt = "2217cafc385b2089617782b01c3cc88e",
	},
	{
		.a = "1e496e33fb89a64d8fcc3591304b05f7fddf4cfdb5a0986a"
		     "188e0e4e49f85e14",
		.p = "d9c22e349011878f87754687ee523365",
		.sqrt = "580db67fe95fbe4f965c678c3e80879d",
	},
	{
		.a = "ec4337ba1339cd0bf3374832526038121727b4bb78b6fd4a"
		     "11300ba5506612",
		.p = "d9c22e349011878f87754687ee523365",
		.sqrt = "f5eef0b27ded737243fe13eace325aa",
	},
	{
		.a = "e826c96625f199c8db32619c61d175f2255aeeefa9183a42"
		     "63553a1f3f0f29d",
		.p = "d9c22e349011878f87754687ee523365",
		.sqrt = "3cf232080315a091c876b8cfea4160d6",
	},
	{
		.a = "c7831203f6b08d222b1b622a7cbbee77237fc689",
		.p = "d9c22e349011878f87754687ee523365",
	},
	{
		.a = "bb13fd167d0ceec6baf65d67537d685c82c65c8b",
		.p = "d9c22e349011878f87754687ee523365",
	},
	{
		.a = "bd8c5e76b6e2746d4de3467b5c65169c3f62317dc4595bc6"
		     "dd1c6a1693e07f8",
		.p = "e3fe457cf7ae0d453157fac07cbaa725",
		.sqrt = "37121413fdc699e9e61ca1893700c75a",
	},
	{
		.a = "e24e97d56b8a5982465b48fd71f816623028b66ac9b2070f"
		     "ce474f541473e13",
		.p = "e3fe457cf7ae0d453157fac07cbaa725",
		.sqrt = "3c2c8c3d819b3b178474394684d256d6",
	},
	{
		.a = "eb5e5424a250f5921315b3b80e611a8bb31fe0ca502b3673"
		     "50dfb435d244a00",
		.p = "e3fe457cf7ae0d453157fac07cbaa725",
		.sqrt = "3d5decb13bdf112d2a7b8f7eb0499b9f",
	},
	{
		.a = "6974a858d34d14491e668420065eb1fb7c46a76464e66799"
		     "af8b6f21b43b7f8",
		.p = "e3fe457cf7ae0d453157fac07cbaa725",
		.sqrt = "29139f465027081bf12280ef60322f91",
	},
	{
		.a = "3534a02b5daf8f7448ba228e933b98fc0a237e3b6fa34a60"
		     "78e1a8788f7669",
		.p = "e3fe457cf7ae0d453157fac07cbaa725",
		.sqrt = "74b51a89bdf7e7f1bb04c8cc07d506b",
	},
	{
		.a = "279ffd9d740d5de93484de00ca0b02f876c2f8a49548fa4e"
		     "a16bb9b5a764f63f",
		.p = "e3fe457cf7ae0d453157fac07cbaa725",
		.sqrt = "64b7a63b5b33224381ee8dc1e0bed0da",
	},
	{
		.a = "19ff5b6b054acc5ca156cb335fbb57a949d3b495609625f0"
		     "b1b1ea0e19c1e669",
		.p = "e3fe457cf7ae0d453157fac07cbaa725",
		.sqrt = "5194934308057d3c8ecdcccb8a1f8801",
	},
	{
		.a = "286c1adb06c8e0d91743543c715d66581e0f1404952f2cf1"
		     "9a92168097e4bce",
		.p = "e3fe457cf7ae0d453157fac07cbaa725",
		.sqrt = "196e70cf9b236e13078af721e59d4642",
	},
	{
		.a = "2fb921c1706f29159470f5080f009b7af73a2b4b59319e89"
		     "44313f674843c790",
		.p = "e3fe457cf7ae0d453157fac07cbaa725",
		.sqrt = "6e87f87aae63457cd4a4a6ea532641c6",
	},
	{
		.a = "1e9be1645bc61e1814adf8ca7809fe2f024723b319556971"
		     "114b2a184e9121f6",
		.p = "e3fe457cf7ae0d453157fac07cbaa725",
		.sqrt = "5885400cb80c63bae4914308b2cdf9ea",
	},
	{
		.a = "9ca8cf6730dbca96d208b79b0fd52535e792b304",
		.p = "e3fe457cf7ae0d453157fac07cbaa725",
	},
	{
		.a = "73f219324f36e0aae1f6e8d95b89d0468ab1922b",
		.p = "e3fe457cf7ae0d453157fac07cbaa725",
	},
	{
		.a = "124b02b0fd0ff677573e733d6bc3bea9a6be60d6e84ef86a"
		     "6e67b740c7112749",
		.p = "d258b10a93a427f65fcdf8cbc655b845",
		.sqrt = "446eb9f9ac09ff7ac2b358dd78ebb4c1",
	},
	{
		.a = "d0d034bfbdcca69049d17eead78572c66b5f5a9e230fa1b7"
		     "d41e7b623a83153",
		.p = "d258b10a93a427f65fcdf8cbc655b845",
		.sqrt = "39cd2ece90ca23d6c2693178c5fcf193",
	},
	{
		.a = "25f55f4ea11fd8adef2d8f9781363006366f6caa0b0337a5"
		     "e0184795f3fad",
		.p = "d258b10a93a427f65fcdf8cbc655b845",
		.sqrt = "18a4e931a05b8416bf5b991bc02cf1a",
	},
	{
		.a = "25337b83d2f3e5853dd52b7052496b7a5e6380e941685db6"
		     "b4693edeaa62e106",
		.p = "d258b10a93a427f65fcdf8cbc655b845",
		.sqrt = "61969ce7afde7fada420c587b0241dbf",
	},
	{
		.a = "50eaeefcd5f5a1b5b0926caed2ec08c92c561f00275694d4"
		     "68c7bba52ae7b",
		.p = "d258b10a93a427f65fcdf8cbc655b845",
		.sqrt = "23fb513f8e77985606921ec8e1f49b5",
	},
	{
		.a = "609baed1c2d4d52f1c91a9ed2f3c68ffe87641b5f00464d2"
		     "128086a4c1e981e",
		.p = "d258b10a93a427f65fcdf8cbc655b845",
		.sqrt = "2750d6a4effd2c9ba5f71f80c2f58cce",
	},
	{
		.a = "2d9514eff20d87bf3afcbd25bc681c134780dec20d01b97d"
		     "3b29cc911a1d90c",
		.p = "d258b10a93a427f65fcdf8cbc655b845",
		.sqrt = "1b01816bc1b20bfaee478233e5f22911",
	},
	{
		.a = "28d32b18c9082ae7ea28f0be3cd7aa8efad600561d4347a8"
		     "53b467758443b1a8",
		.p = "d258b10a93a427f65fcdf8cbc655b845",
		.sqrt = "663b1fed3e5cfed7167cce2dbae810b1",
	},
	{
		.a = "fd38ba6f44adf4ce8d06344d0c3e5e4a44a34aaf93497b25"
		     "3f1e9b1009c6b0",
		.p = "d258b10a93a427f65fcdf8cbc655b845",
		.sqrt = "fe9b64d7af41c5ce81ff18a391165af",
	},
	{
		.a = "590d9ed0431461071609075d93270c323533800822a360e1"
		     "60210e2d5c438be",
		.p = "d258b10a93a427f65fcdf8cbc655b845",
		.sqrt = "25bf48a476372ef10dc669ed3f7d4896",
	},
	{
		.a = "5b0f78a90cee80f72695c3b14d340e7a38567f11",
		.p = "d258b10a93a427f65fcdf8cbc655b845",
	},
	{
		.a = "afad58d4e0c0c06fb40a77131871575bc72369a7",
		.p = "d258b10a93a427f65fcdf8cbc655b845",
	},
	{
		.a = "1e19fd22e601573225586b66084a4df65158ef45ad0fe8f0"
		     "a68faa4ad447cf65",
		.p = "ffd67dcb5d329a4a2bb7dd5714883bc5",
		.sqrt = "57c8a49ae8a34fc47bf236674b817723",
	},
	{
		.a = "5c15b5ef617d5cf85e4c9276478d9278e04a30527a2bcb66"
		     "a6adeb05ccb671e",
		.p = "ffd67dcb5d329a4a2bb7dd5714883bc5",
		.sqrt = "2662638f71038b2b0b9e4a06e46bb361",
	},
	{
		.a = "f1243406b9dbcaa1f252e7dd8c22daa9345c9151f6fa6e54"
		     "df6ac088c81ceb2",
		.p = "ffd67dcb5d329a4a2bb7dd5714883bc5",
		.sqrt = "3e1d6b16acc35ee99a3cf9b58f385d65",
	},
	{
		.a = "a951e30e28201b632da15521f791e8101ae9fdf7a107e6c5"
		     "6c35e4f7ce52d46",
		.p = "ffd67dcb5d329a4a2bb7dd5714883bc5",
		.sqrt = "340c978f9204064fc549747774070f41",
	},
	{
		.a = "2d0cda470ec77db7e748640e1c82131141b262df8cc2cea7"
		     "43915d3ce85533e1",
		.p = "ffd67dcb5d329a4a2bb7dd5714883bc5",
		.sqrt = "6b64207016d908d10e20b8d8098fe79b",
	},
	{
		.a = "47ee4bb1de2238c9ffb9ab96eef909b14b165c8cd9bc2049"
		     "0837e26d76134cf",
		.p = "ffd67dcb5d329a4a2bb7dd5714883bc5",
		.sqrt = "21ecc111c02eac8f8693cd21c980dea7",
	},
	{
		.a = "d6d0e077785db6f2246475088ce5104e6bda7921c1fb12ac"
		     "1149bb3c9b7eea",
		.p = "ffd67dcb5d329a4a2bb7dd5714883bc5",
		.sqrt = "ea816f0e6b06c0d446fa0a00e79103c",
	},
	{
		.a = "3f0fa323e5d12f4aa296f2024f86b51cad3a0339d978bb2f"
		     "c4a5ce4fc83a29a4",
		.p = "ffd67dcb5d329a4a2bb7dd5714883bc5",
		.sqrt = "7f0ebfc9efebd5de70d24955fa75393a",
	},
	{
		.a = "247680753d5599c95aab84d1b1fa516d41e092622574f295"
		     "ebf9b51e3825bb74",
		.p = "ffd67dcb5d329a4a2bb7dd5714883bc5",
		.sqrt = "609d7f6a4769d94e1a3ffc6916c02b50",
	},
	{
		.a = "5f1e3012e6d330b09e72cfbae64d71ecc314161a43045cc1"
		     "39c06f35a9842a5",
		.p = "ffd67dcb5d329a4a2bb7dd5714883bc5",
		.sqrt = "2702e8eea69442b4e6172a2a604727b3",
	},
	{
		.a = "37f3b0d8b439d1154897e5570a0c3848df6bbd0",
		.p = "ffd67dcb5d329a4a2bb7dd5714883bc5",
	},
	{
		.a = "7eaf05eecb0d43ecf2512817192528adf0889694",
		.p = "ffd67dcb5d329a4a2bb7dd5714883bc5",
	},
	{
		.a = "105c1fa13632480e2b74fb6cf6bc328cc3942f9d8e53b98f"
		     "1cdbe90f20cbd421",
		.p = "ce66d97f3c1ed87b6103b64014e37bb5",
		.sqrt = "40b738fd5e8a939cff33399ccf94500f",
	},
	{
		.a = "13a3407966b0d6d73397a45fa9be2b11acaecc6e80df03fa"
		     "ed2e048b044bdbd5",
		.p = "ce66d97f3c1ed87b6103b64014e37bb5",
		.sqrt = "46e7329b7c7e48e6fa2a369589706b6b",
	},
	{
		.a = "22147b448cd5a73ec3034e900e5e35e2c7449ddea0217f7b"
		     "65fc368cb7fc571",
		.p = "ce66d97f3c1ed87b6103b64014e37bb5",
		.sqrt = "1759ea6c3c8c4564b5978c40efdc030a",
	},
	{
		.a = "e10c85d3fcfda9437b148bd660ad5a7ebca4d80c8c68c524"
		     "6ceb0939273b4dc",
		.p = "ce66d97f3c1ed87b6103b64014e37bb5",
		.sqrt = "3c01ab6ba6cbdb7e8516e22542b2b18d",
	},
	{
		.a = "28f0530dbfaae27d8c0d6681d32a5d550cd335c110b789ef"
		     "2988c39b6c4d3294",
		.p = "ce66d97f3c1ed87b6103b64014e37bb5",
		.sqrt = "665f9acaaa3c55e7088bb93dc88fb6a3",
	},
	{
		.a = "4962eae6c1079bb2a5ee60600f9b9c9db0715a9779c20a5a"
		     "8415b20c4b3d85",
		.p = "ce66d97f3c1ed87b6103b64014e37bb5",
		.sqrt = "8910bc8cbf206a16968ee9607017bb3",
	},
	{
		.a = "15d05b2c3428db47461d4b90e8146dd4b4ad5f86465abb8a"
		     "7bb252ca423ff7de",
		.p = "ce66d97f3c1ed87b6103b64014e37bb5",
		.sqrt = "4aba823c04a7ec0d08070999f7ab5965",
	},
	{
		.a = "37c0957199baff074190dae6fde2420a56748d522ba89182"
		     "13bdda91a171085",
		.p = "ce66d97f3c1ed87b6103b64014e37bb5",
		.sqrt = "1dddf264310f08e6d212b7809744b075",
	},
	{
		.a = "1170b6f4f5c7d90fc1127a987baf05d47c1f91b62ba0dd01"
		     "e2ffc3e61e26544",
		.p = "ce66d97f3c1ed87b6103b64014e37bb5",
		.sqrt = "10b462a39a4ea25517c916ca1267d4ee",
	},
	{
		.a = "dc67e90034f4b1c96158d66c61ef94f6322bf30540767194"
		     "289d50a0e64a34",
		.p = "ce66d97f3c1ed87b6103b64014e37bb5",
		.sqrt = "ed8984640b9032ff7d5dc6164ea1f40",
	},
	{
		.a = "6412bbff1fbbca4e25a7f59fc99722dd655b1941",
		.p = "ce66d97f3c1ed87b6103b64014e37bb5",
	},
	{
		.a = "a98c16965781b8b6fb49a1fdfbefbd00ade3dc92",
		.p = "ce66d97f3c1ed87b6103b64014e37bb5",
	},

	/*
	 * p = 1 (mod 8), short initial segment of quadratic residues
	 */

	{
		.a = "b0b43e2686e8bf81e07908ee0315393cde48656e98e2c432"
		     "3bd5133a07a1c76",
		.p = "eec87e1e532b9f1a091122c4cff81d01",
		.sqrt = "352c0a67702fab872833b758680a99fb",
	},
	{
		.a = "c95dc97c4405b42ccc6bec830850684245262d383756c231"
		     "8dd79378a9f8669",
		.p = "eec87e1e532b9f1a091122c4cff81d01",
		.sqrt = "38c2ee16f6e355ea53e346fd9078f655",
	},
	{
		.a = "35379a66ef1ef5a281091c35966a31d312095cb7ac0959a0"
		     "0f99997ecb69f251",
		.p = "eec87e1e532b9f1a091122c4cff81d01",
		.sqrt = "74b85e7a7766ca98047ce8a702c9ebd5",
	},
	{
		.a = "4ccc534c7ac3ac9526407a4aae616151c8e5a38984f59a9d"
		     "b5e75f03c296",
		.p = "eec87e1e532b9f1a091122c4cff81d01",
		.sqrt = "8c371cbd5d69a8d82889263b56773d",
	},
	{
		.a = "e30807499ebe6f6c89322fd3c4b428f11ba4952f6fbae497"
		     "c90edc5297bdf3b",
		.p = "eec87e1e532b9f1a091122c4cff81d01",
		.sqrt = "3c452e6bd40178ab4cf09f121a9a89ef",
	},
	{
		.a = "24dc33460e63ad6ae3d5e659bcfcf7208f083c804501a5e9"
		     "1b12f0478c6f5f2f",
		.p = "eec87e1e532b9f1a091122c4cff81d01",
		.sqrt = "6123de04bebd162ffb002e0500ab8359",
	},
	{
		.a = "26c2a761b5d95bc3847ff7a58825f249738d9c1eb8a003ba"
		     "53be67dd6a4b92d",
		.p = "eec87e1e532b9f1a091122c4cff81d01",
		.sqrt = "18e73380a4810e17bb114f3e1f38bb14",
	},
	{
		.a = "362b43adb6aa4f3f7041ee86d42ee37897495fbcd2a52a47"
		     "9e1a80493f1fbc7e",
		.p = "eec87e1e532b9f1a091122c4cff81d01",
		.sqrt = "75c264b8c82dfb64c9cfee3e0c64c699",
	},
	{
		.a = "7364cfd413f046b4bb886cda3d7dcda21f619ebb715a5bd0"
		     "6cc3833be5d4107",
		.p = "eec87e1e532b9f1a091122c4cff81d01",
		.sqrt = "2af7f6495da83f2f27d947cb982cec03",
	},
	{
		.a = "3e7559de589e876d2fe6efd091a0a91511b7334576eef8f0"
		     "ecacb298b61838a",
		.p = "eec87e1e532b9f1a091122c4cff81d01",
		.sqrt = "1f9cbc8282c070ed616cc1a97fac79a9",
	},
	{
		.a = "25ba186abff6724df832e524fd872f6306adefbd",
		.p = "eec87e1e532b9f1a091122c4cff81d01",
	},
	{
		.a = "59193d6469d246596bc69cc0030b2e0a0ff2296a",
		.p = "eec87e1e532b9f1a091122c4cff81d01",
	},
	{
		.a = "1f14cb634a8e3f4f262998e8e661b91b424ec91b48e245b2"
		     "0c7b79d21bfc9307",
		.p = "c6f187c92de82671e10480e2f38454f1",
		.sqrt = "59336bf333f7dab3f26b6e5b3d580ae4",
	},
	{
		.a = "6b1b8ea97893defaaaa262bbedec178071cd0de9fb5b1bf6"
		     "35148c2880bbcac",
		.p = "c6f187c92de82671e10480e2f38454f1",
		.sqrt = "2965aa4725e333b86683bf030fd5828d",
	},
	{
		.a = "12996d48b6f9cc889c9173399f5f4db773f29a964a192980"
		     "2183f3939b94ab8d",
		.p = "c6f187c92de82671e10480e2f38454f1",
		.sqrt = "4500cab9ab7aec1150b1223ee9cd7049",
	},
	{
		.a = "3dae5dd26b2373500194c86816b27350a53aae4b5dd31531"
		     "01d5f6e5a9d3df4",
		.p = "c6f187c92de82671e10480e2f38454f1",
		.sqrt = "1f6a38ef49cc8bd1eef8883f1491227f",
	},
	{
		.a = "9e2de20f229e667f8e79d43f9f31b890f8b9b58aba382037"
		     "ae889c6fc807bf2",
		.p = "c6f187c92de82671e10480e2f38454f1",
		.sqrt = "324ec7636c3dff4fa3253fbc81af6dab",
	},
	{
		.a = "80d25f2f19978918bca80dab98de01d82ae3b169ba0f6e89"
		     "03d21cb4fc27329",
		.p = "c6f187c92de82671e10480e2f38454f1",
		.sqrt = "2d665de92ba36cd16eafa053f5dc4ef5",
	},
	{
		.a = "70410faf098a2039617c43dd4768c4366f3583a543d74d00"
		     "7781d5fae6308ec",
		.p = "c6f187c92de82671e10480e2f38454f1",
		.sqrt = "2a61492be8dbba6a845ebf0d132e2698",
	},
	{
		.a = "5a35e960ebdb0461f1867c55fd8e4f1e626a7facab744924"
		     "2c92e8fe724c17b",
		.p = "c6f187c92de82671e10480e2f38454f1",
		.sqrt = "25fde03b1a02c096f8383c840443c2d3",
	},
	{
		.a = "a1d2ed930145ab18b174126582c20cc189d64844227bcbd4"
		     "6437cd12564cee5",
		.p = "c6f187c92de82671e10480e2f38454f1",
		.sqrt = "32e24ed1bc281819156635ca43ef4a4c",
	},
	{
		.a = "7054c96a5e8841dcc19561c1324d5ad01c31e853eb73f983"
		     "f4d3b86a199cbb6",
		.p = "c6f187c92de82671e10480e2f38454f1",
		.sqrt = "2a65023c989bb86d8ea28dce73589cd5",
	},
	{
		.a = "be49695b8e19d4480f861204bc929de31e6cc630",
		.p = "c6f187c92de82671e10480e2f38454f1",
	},
	{
		.a = "3a0c60b78ae72dfe52da83947fa263ceb2db906b",
		.p = "c6f187c92de82671e10480e2f38454f1",
	},
	{
		.a = "165cf974fe4ed624e4f37336c62619b3e7e810c12efaa571"
		     "5edddc9fecd73a43",
		.p = "debc88c58dfc9ad8e61d69f61fc52d49",
		.sqrt = "4ba9df1fcfeeb2d1c8ee0ac376216ef2",
	},
	{
		.a = "4678e362dc4e15b9398f7ccc8fa691b6a05ba26089a0ee9b"
		     "b6b538a62e96043",
		.p = "debc88c58dfc9ad8e61d69f61fc52d49",
		.sqrt = "21943f77370cefaa2abbd82ec27bbe25",
	},
	{
		.a = "7eff91bde1563cc1919ae3a81fa35182a6a44454164c9ba4"
		     "9e3f3cf689d23a1",
		.p = "debc88c58dfc9ad8e61d69f61fc52d49",
		.sqrt = "2d13d1477e40f4bcf7df8323b1f2c103",
	},
	{
		.a = "1de4798791513fe1babddcef80e6a15c996d15f5b70f584b"
		     "9ece3ca9ab76d2cf",
		.p = "debc88c58dfc9ad8e61d69f61fc52d49",
		.sqrt = "577a7a0479f552028216368fb7a36ecb",
	},
	{
		.a = "82732b80ab0df57861ffec20a3c75c270bc5e134eebb264f"
		     "99d8f3ebbd53ab4",
		.p = "debc88c58dfc9ad8e61d69f61fc52d49",
		.sqrt = "2daf94bf89a6d4b92d99f6ec2be281b1",
	},
	{
		.a = "27eb19d9f5b57e8b8eb038da5358a95908dcbeaf7910a9d2"
		     "7b063d6f00850fc4",
		.p = "debc88c58dfc9ad8e61d69f61fc52d49",
		.sqrt = "6516ee003d56d1d4eac285a97de1af3a",
	},
	{
		.a = "12f3ebcf10e154bd69406ed2df0fe0ffd2f613ac45e69c4b"
		     "bec2c33247a28df",
		.p = "debc88c58dfc9ad8e61d69f61fc52d49",
		.sqrt = "1169f787334a104e5da71364960bb4b3",
	},
	{
		.a = "81de8f01e74ce3a1b254c9f2a18817c806e50a3c95903392"
		     "943ea1b8ec0d2ac",
		.p = "debc88c58dfc9ad8e61d69f61fc52d49",
		.sqrt = "2d9587643969929cfb837f76ca325d5d",
	},
	{
		.a = "148c6cdf92945f7111d9ad5ad4a206a349cc46be1a89781c"
		     "1b09780f40cebafd",
		.p = "debc88c58dfc9ad8e61d69f61fc52d49",
		.sqrt = "48875ebd5c183cc3ae177c2506c27249",
	},
	{
		.a = "fe6ef7838aa594f982ac528e59641752e0a4873ef72d900d"
		     "703462f5f18c20b",
		.p = "debc88c58dfc9ad8e61d69f61fc52d49",
		.sqrt = "3fcdcb3f24ebb63257de3e9acd379046",
	},
	{
		.a = "704e6918f3a0a47b167296642e1f6c064b5fed6",
		.p = "debc88c58dfc9ad8e61d69f61fc52d49",
	},
	{
		.a = "e10a7f7f045c58adf44a12fcc6cec87bd63b75b",
		.p = "debc88c58dfc9ad8e61d69f61fc52d49",
	},
	{
		.a = "b9656d002c98fa8c120ac1db4a05775df1c3319de5bfb690"
		     "1c0d264f372014",
		.p = "fbde582b5fbe2da75cae71930eb93f21",
		.sqrt = "d9db3f403b9e424045df0a09e6cd899",
	},
	{
		.a = "1974d77a065c76035af3290bb7dd8e1cddfc19c59facb248"
		     "1639bd8eee190b",
		.p = "fbde582b5fbe2da75cae71930eb93f21",
		.sqrt = "50ba19e7001bcb74ee9d6df6933e18b",
	},
	{
		.a = "94751c7af51c0786c679e31948c0489d12fabf16c8814bad"
		     "906b3b41e6b229b",
		.p = "fbde582b5fbe2da75cae71930eb93f21",
		.sqrt = "30bcbc5bf9b4bde195aba7aeefa44bfc",
	},
	{
		.a = "3dc05f7bd4aabe0a68e366abcc0487f1a10c4c179cb665b7"
		     "ccf4f39ce6063bb0",
		.p = "fbde582b5fbe2da75cae71930eb93f21",
		.sqrt = "7dbb39e9a61cdbcf7e83b21483d922f6",
	},
	{
		.a = "208977daf6a72daa5c5d4e14fbe2e28fa533753867d08329"
		     "5af878df4c7e377e",
		.p = "fbde582b5fbe2da75cae71930eb93f21",
		.sqrt = "5b4413582ab1a3ff47f447cc08c0d0ed",
	},
	{
		.a = "3de30458947b12443eb955b8895c7b45db79e53805a82933"
		     "3b93dee77f4528e",
		.p = "fbde582b5fbe2da75cae71930eb93f21",
		.sqrt = "1f779e770a66ab59075023137a0f55fd",
	},
	{
		.a = "18f5f5e27aec608f1fd5520a17983aba52d6a1d0ac3d88ec"
		     "0243fb82a649ec4c",
		.p = "fbde582b5fbe2da75cae71930eb93f21",
		.sqrt = "4fefee3399d14feca488bbafcbd9b10e",
	},
	{
		.a = "155a7cab9f3549ddc09cd71691144e147b6471e3e9ecfef2"
		     "6db13e9fa385108d",
		.p = "fbde582b5fbe2da75cae71930eb93f21",
		.sqrt = "49ef89b4f7a44d54bba946b799fee7b0",
	},
	{
		.a = "567ec8920ee7fc5d5a66fd5fb6714a449dd86220a424d4ad"
		     "86f0a6b31b7a3c4",
		.p = "fbde582b5fbe2da75cae71930eb93f21",
		.sqrt = "25337d299557bb92261fd1fd30cc7236",
	},
	{
		.a = "1d3a6ca959af4a5d1cb68a950f244400259e87f51c6f29bd"
		     "ac8905767b7e6342",
		.p = "fbde582b5fbe2da75cae71930eb93f21",
		.sqrt = "568042169dc73549d6345470ee1eabb2",
	},
	{
		.a = "ecc06a093a4b1438a644026f9daba3dbfee04846",
		.p = "fbde582b5fbe2da75cae71930eb93f21",
	},
	{
		.a = "4b770b922a4704dd5dcfb1b44bccc4bc79d75dd6",
		.p = "fbde582b5fbe2da75cae71930eb93f21",
	},
	{
		.a = "142f20f570f05d4fe64988f56be3ab87aa855d4f8b145dda"
		     "7176d7b51c93e765",
		.p = "c579568a7fda58a3116a02bde4c395b1",
		.sqrt = "47e1fb727135a208cc2c7ea7b0c379fe",
	},
	{
		.a = "979a2fbdbf6fe322c4b7a96d1b8dcf98aa337ee09989d42b"
		     "2846137dd294a18",
		.p = "c579568a7fda58a3116a02bde4c395b1",
		.sqrt = "314031569a866f17741995c94339eff3",
	},
	{
		.a = "1125db5aba454aced382b162c403d256b4b74f15a8da2bd9"
		     "8c7df618069b5a00",
		.p = "c579568a7fda58a3116a02bde4c395b1",
		.sqrt = "424188e0d7180cc52c9cade1410166be",
	},
	{
		.a = "5c15ec51bdca83353cda8dae1258b75eda7320fb2b5f61fd"
		     "9c2dd4ef047184",
		.p = "c579568a7fda58a3116a02bde4c395b1",
		.sqrt = "9989bb94715481c52520a8f18b5d16a",
	},
	{
		.a = "a14e2920e9a3d3c9dcb2b2625570c2799d1ec9bf13eef681"
		     "6a2493e0bb67abf",
		.p = "c579568a7fda58a3116a02bde4c395b1",
		.sqrt = "32cd6adb81261c417dd10a2583a2d1de",
	},
	{
		.a = "4f7e44dd885cfcc183c4c6a81bbc54029b5ec16a57c18d2a"
		     "dcb85ddb1f674a",
		.p = "c579568a7fda58a3116a02bde4c395b1",
		.sqrt = "8ea784baad105cace13c408e30333c4",
	},
	{
		.a = "1ca8323e809c9adda0190816e6083aecf2d7249f111b80af"
		     "e243f3ec95b07909",
		.p = "c579568a7fda58a3116a02bde4c395b1",
		.sqrt = "55a6cf22f6779438a874c1790ee79b01",
	},
	{
		.a = "241ffcdf08f03805c6ba3bd1fe664a031aed20225e841269"
		     "8df05607921bb7ae",
		.p = "c579568a7fda58a3116a02bde4c395b1",
		.sqrt = "602a9d0981fbdf7429190008e771bf43",
	},
	{
		.a = "2005bcee577a5a69d7d38a7c33c0f32a5d4d092cb961369b"
		     "8ce8718596581c5",
		.p = "c579568a7fda58a3116a02bde4c395b1",
		.sqrt = "16a2a5a7aed60909e10cdde80133b3d1",
	},
	{
		.a = "24e5fd362ef16d84a64f25481f99daea591b537726220bcf"
		     "59ecc270559a1e21",
		.p = "c579568a7fda58a3116a02bde4c395b1",
		.sqrt = "6130c332ad0069454b0e327e635ad978",
	},
	{
		.a = "aa65d05d557cf6a73d04668ac5c474cf738d20a0",
		.p = "c579568a7fda58a3116a02bde4c395b1",
	},
	{
		.a = "b0e60e655eef70c827476d6706b8ebc040392f78",
		.p = "c579568a7fda58a3116a02bde4c395b1",
	},
	{
		.a = "2e2f92d2457763400965223456e5fb40f67df2022e1b397a"
		     "f37ca2135582db4",
		.p = "d1bd970108af0ab47a7ccff9c1f7b6d1",
		.sqrt = "1b2f1ed46b49eb6a0529b395c74f44bf",
	},
	{
		.a = "c4fab9bdefce241989dc61671de4c86d7d7644c02c0be973"
		     "54a9f6d865b8128",
		.p = "d1bd970108af0ab47a7ccff9c1f7b6d1",
		.sqrt = "3823c5f72e6d52d7022b4cb12a312f00",
	},
	{
		.a = "13d0f37f258adc9fbfbde054fb0a9560eacdc5cd8ccd7cdc"
		     "928730d1b7e1ff4a",
		.p = "d1bd970108af0ab47a7ccff9c1f7b6d1",
		.sqrt = "473982d7a7015f122afc0a97e94bf72b",
	},
	{
		.a = "e7593a8c8cca00d2996f301e0b2246f12609bcdff086020c"
		     "8e78ad216665e",
		.p = "d1bd970108af0ab47a7ccff9c1f7b6d1",
		.sqrt = "3cd73095a2a43b2dc019896d5f51fab",
	},
	{
		.a = "1fb9fe423f25e1546d266fee359a6b65cbbf4fa49822f597"
		     "76a67f989057af2",
		.p = "d1bd970108af0ab47a7ccff9c1f7b6d1",
		.sqrt = "1687d08343e40b4750c9592bac920dbc",
	},
	{
		.a = "dbb601ad3da8e497e34eb2f541fcd576dafefa41751a18bd"
		     "435ceefbef0c44a",
		.p = "d1bd970108af0ab47a7ccff9c1f7b6d1",
		.sqrt = "3b4a64d9bf6ebdd2bd243432c0d532a6",
	},
	{
		.a = "6d71e7136f775c460d920b0796c8df487e7dc97d22ea5d92"
		     "e8bce3470504a13",
		.p = "d1bd970108af0ab47a7ccff9c1f7b6d1",
		.sqrt = "29d8abe2fb649295530753aaf1327711",
	},
	{
		.a = "5609c11c6911640ff282bd04ce8400a1221c097df2ef63ed"
		     "4ac8b4e999d51a5",
		.p = "d1bd970108af0ab47a7ccff9c1f7b6d1",
		.sqrt = "251a49ed949d8ddcc69aa3d6dda2dbdb",
	},
	{
		.a = "2df199e2eba7bd3163719afb875e1f64bb5327a51c2e9fe6"
		     "c56ef8592a9598a",
		.p = "d1bd970108af0ab47a7ccff9c1f7b6d1",
		.sqrt = "1b1cdbcd9320d9443ac170ff59d6c27c",
	},
	{
		.a = "2c91906b42c1a3c210516de02bc028f1f2916992158e13b6"
		     "03565a950e1b863",
		.p = "d1bd970108af0ab47a7ccff9c1f7b6d1",
		.sqrt = "1ab432280a0b62a6784abdb8ff2d863c",
	},
	{
		.a = "ac93b16f0e514f2d4ea5270fede89adea8bf3205",
		.p = "d1bd970108af0ab47a7ccff9c1f7b6d1",
	},
	{
		.a = "74a3c88c833d3c186c8ddfc0ae0cd9134503c933",
		.p = "d1bd970108af0ab47a7ccff9c1f7b6d1",
	},
	{
		.a = "164e6a030c238052514d8152aed4f20009275ea0555e7f14"
		     "f2789eff29e27be0",
		.p = "cc0bd352a591f8665ea869e19947a909",
		.sqrt = "4b913964a95efc1abff3727531e6d1be",
	},
	{
		.a = "121a1159c2305a9bc5b85cd1ae1cda94cee7dbfa471e99f3"
		     "5d5471c7bffe918d",
		.p = "cc0bd352a591f8665ea869e19947a909",
		.sqrt = "4412f0d87aa6931d475a536af11974aa",
	},
	{
		.a = "205b1ab436bfcb8c64aca2a495d47c16703746de53edfcbe"
		     "71762c73f73cd7f4",
		.p = "cc0bd352a591f8665ea869e19947a909",
		.sqrt = "5b02f5b0766eb62563120a97d3da7016",
	},
	{
		.a = "1a84a5e3bf3784e752c2734c536659b1daf6acd4423c7ad9"
		     "c89c56fc2959b6df",
		.p = "cc0bd352a591f8665ea869e19947a909",
		.sqrt = "5264ac2cedc3afd4ed552aeead50c87e",
	},
	{
		.a = "a008aa6563a2233120e31e75f2bf91c4c32ddfbe4ce80e22"
		     "c10f16efe9ed7f5",
		.p = "cc0bd352a591f8665ea869e19947a909",
		.sqrt = "329a0f3350dba44da9c0fa3fb3001460",
	},
	{
		.a = "3454b1dba0a1eb696492911db8d36c58fd1f283802533765"
		     "984e3f5c3ce0dee",
		.p = "cc0bd352a591f8665ea869e19947a909",
		.sqrt = "1cef9f32af8abe01ee1fb856df4a9619",
	},
	{
		.a = "24cc035dd244bca942b1144f4533548fa36faef24f838deb"
		     "371870df172474a3",
		.p = "cc0bd352a591f8665ea869e19947a909",
		.sqrt = "610e8750278f896c548f5b8f6c08d896",
	},
	{
		.a = "24308e5a0df6a24c4c01726759757ee155ef2f107b4d69d7"
		     "90e1c66b47beb33",
		.p = "cc0bd352a591f8665ea869e19947a909",
		.sqrt = "18102a01dea52cf26ec7bcd9ce7bcf1e",
	},
	{
		.a = "2656e62c6b0cc284f0cb348094467a51d89eec529b8ffcfe"
		     "f34805a2211fdfa1",
		.p = "cc0bd352a591f8665ea869e19947a909",
		.sqrt = "6311f6d63ed3fdd25ea3b0b44444fcb8",
	},
	{
		.a = "227ea04bc695cf6afc9ae8414776f4a4a09b7baa3755eb93"
		     "569e1c6e979a09f",
		.p = "cc0bd352a591f8665ea869e19947a909",
		.sqrt = "177e2b9aa023b58ab846307b1e948d16",
	},
	{
		.a = "2b40088613eb00bcaa1fa590a18924b12876e414",
		.p = "cc0bd352a591f8665ea869e19947a909",
	},
	{
		.a = "2ef7c6d7541f87a19fa4f788bc213b2e4807eedc",
		.p = "cc0bd352a591f8665ea869e19947a909",
	},
	{
		.a = "22edc2d92411cc04ef7ad3119a87a5e12073d9e09c74539b"
		     "be76677cbe145ab2",
		.p = "e2fd3d48b3845be5df89591787fd68e1",
		.sqrt = "5e8f9636712aecc1a01b915baeda4f76",
	},
	{
		.a = "29fea5f62359b09c8dbb54de10637f04295258dbe34e91a8"
		     "3dbb0f81447c0f",
		.p = "e2fd3d48b3845be5df89591787fd68e1",
		.sqrt = "67af71fc655003a5abf936a9ac002b8",
	},
	{
		.a = "8f66c7428f3d42ff8c1d5ee690d8378457d6f2398d7031ef"
		     "74663b895758689",
		.p = "e2fd3d48b3845be5df89591787fd68e1",
		.sqrt = "2fe66fbc713a5304afed6b054570859b",
	},
	{
		.a = "fd9a8bfabaf8bae3776af4c4863dd94751de970d9d27a8ee"
		     "12ff19fa805f721",
		.p = "e2fd3d48b3845be5df89591787fd68e1",
		.sqrt = "3fb32357d34705010c2c57f5f01d142d",
	},
	{
		.a = "2fc61df839a0b92ddeb05d69df8f5830b13faa5325cbe198"
		     "e9731e1892c347c",
		.p = "e2fd3d48b3845be5df89591787fd68e1",
		.sqrt = "1ba5c042e81e591307e09615209ed8a4",
	},
	{
		.a = "5721f5f2c40a81655246041e141a8399cabc43e19c866568"
		     "568d5383337dfba",
		.p = "e2fd3d48b3845be5df89591787fd68e1",
		.sqrt = "255683ec2efe5057dea7391c5912ae9c",
	},
	{
		.a = "2c7a60d81f0a2ee7e1ff17f0271be4af740fe7001e6f9602"
		     "07766cffd9dd583e",
		.p = "e2fd3d48b3845be5df89591787fd68e1",
		.sqrt = "6ab4fc3f4f0c7674d9443ffbddf70d99",
	},
	{
		.a = "3a167990a328fae6c924e193fd37eb420eb5761dffbf6269"
		     "b7f1696b6598ee",
		.p = "e2fd3d48b3845be5df89591787fd68e1",
		.sqrt = "79f1ce74a61de8d2da7f4fff2421e30",
	},
	{
		.a = "13ca314b6be0b28ade1f8d3581eb137f8196a74f8f3f801b"
		     "7410bc0d12d49cd",
		.p = "e2fd3d48b3845be5df89591787fd68e1",
		.sqrt = "11cb571a0d7832a5131c3bd9e2b2175b",
	},
	{
		.a = "2ac0f52b513f3d5dc1aad96129f2d68f3512b1a83a9ed1a9"
		     "8ae07407527c1",
		.p = "e2fd3d48b3845be5df89591787fd68e1",
		.sqrt = "1a278fccb0eafab3c74cdb5a1e0660a",
	},
	{
		.a = "b3b7c12e0c3c394e7b41748f46bb5a71422f44fe",
		.p = "e2fd3d48b3845be5df89591787fd68e1",
	},
	{
		.a = "c339daf5e328d836ca0274464ead34c8d4e8b3df",
		.p = "e2fd3d48b3845be5df89591787fd68e1",
	},
	{
		.a = "1cad67a0472e3d78ce2f4db0079bf3d3efef4c16bb8b1295"
		     "df85c8790aae9586",
		.p = "dd371a07847d0e0ba15fe277859db621",
		.sqrt = "55ae976c8da057f164602c832173e3bd",
	},
	{
		.a = "1f2aa0fa2d7aaf972d75e84280b1c193bbd4de6de474a1b1"
		     "daa929b44805eaa4",
		.p = "dd371a07847d0e0ba15fe277859db621",
		.sqrt = "5952bb50076a5002d4fd98ee1b234bee",
	},
	{
		.a = "18b96cb4952e56620973262602bb7c65b81b7155e0561c0f"
		     "c27e611cdaca4ebe",
		.p = "dd371a07847d0e0ba15fe277859db621",
		.sqrt = "4f8ec4312a94b37af9d2677320b55d88",
	},
	{
		.a = "19c0b132a768d471e98aec9b3d06620894d3909bae11fc3c"
		     "3ecb9f33c7cdac28",
		.p = "dd371a07847d0e0ba15fe277859db621",
		.sqrt = "5132056926467b9ebc503517eae731b4",
	},
	{
		.a = "2e91ee9e3f29a743a7ed896237b1cdc4ff7045391128a3fe"
		     "1fe43b7eb33b88c8",
		.p = "dd371a07847d0e0ba15fe277859db621",
		.sqrt = "6d3006960c9822d8e3e20277297f2b4d",
	},
	{
		.a = "a89e2e2f33c155e357ace3a22721d0acd7b7c754698e6f46"
		     "b75c5b8507315d9",
		.p = "dd371a07847d0e0ba15fe277859db621",
		.sqrt = "33f0f13b9bfbccfc3af5ebcf2932234f",
	},
	{
		.a = "bafc09132b129b127efb13fca3e56fd94d70f5e60664d93d"
		     "6e56bcfd43f138",
		.p = "dd371a07847d0e0ba15fe277859db621",
		.sqrt = "dac9a361004882677076847427fc17a",
	},
	{
		.a = "2ca6dca28ad4a0c1b16ddbb1d9439cdfce6f8220ddd77af8"
		     "192d157f0cc18a",
		.p = "dd371a07847d0e0ba15fe277859db621",
		.sqrt = "6aea4b1e58873c72cf789c7e1b38c67",
	},
	{
		.a = "aacc0a85bd836d8b31aafaa15b5b30198cfe32943796490a"
		     "2f66829fea31ead",
		.p = "dd371a07847d0e0ba15fe277859db621",
		.sqrt = "344696a190e09e2fcc7c162be811ea3a",
	},
	{
		.a = "145df6843833e76a7d4f7b9ee3df29ed2a604663887296de"
		     "844739e5f2c59086",
		.p = "dd371a07847d0e0ba15fe277859db621",
		.sqrt = "483530d37ec99f64af73f91ff39c2071",
	},
	{
		.a = "b73e95675c9ae0eeb18640fb9e9363ec44c83275",
		.p = "dd371a07847d0e0ba15fe277859db621",
	},
	{
		.a = "86545503f6b37446d95dfab5af4536927ef7c949",
		.p = "dd371a07847d0e0ba15fe277859db621",
	},
	{
		.a = "1a0ca94f23fb2ff2f05ee6edf6c5288ee6632c9bb78b1f27"
		     "a1418ea74cefec90",
		.p = "d8708392da9cc40e2cc47ad468621c99",
		.sqrt = "51a970741b5119c5d0f3ef99ee70c620",
	},
	{
		.a = "40941999acd5ecaf4819064d829274799f81c557c872b94f"
		     "9c55f90669bbbce",
		.p = "d8708392da9cc40e2cc47ad468621c99",
		.sqrt = "2024f113a8ff4531638916f5ce8ce437",
	},
	{
		.a = "1d2b395e6e74c7a9c89df6b709b809747ad27b2f39e2c84c"
		     "c6bfe6c88c591a48",
		.p = "d8708392da9cc40e2cc47ad468621c99",
		.sqrt = "5669c103c43a3e71eff5067f1c64689e",
	},
	{
		.a = "256567c23e2f5efcae74a5ee4390c2a53371f70cc6a391e4"
		     "6cb41df3ac32d795",
		.p = "d8708392da9cc40e2cc47ad468621c99",
		.sqrt = "61d801f9493c8dd4df81bdec46b11044",
	},
	{
		.a = "1cf702151efb378046c6e1ff7c4fdea64dab11de545b18cc"
		     "4635faff50bb2023",
		.p = "d8708392da9cc40e2cc47ad468621c99",
		.sqrt = "561c45e4d90fbc3d67e257d464522362",
	},
	{
		.a = "2a76050e09a73aeec0b232bd0c58141f1240c51fd0e1c802"
		     "d2c6cf88090cbeb9",
		.p = "d8708392da9cc40e2cc47ad468621c99",
		.sqrt = "6842672d2370bdef87261e322caefacc",
	},
	{
		.a = "3a947f3f87fdac9374669cbd3f426629b2b4b0813a72679c"
		     "91bf1536b4bdf95",
		.p = "d8708392da9cc40e2cc47ad468621c99",
		.sqrt = "1e9d73ae67b54e8c1b37638ff2c5e17f",
	},
	{
		.a = "a2e97855accfaca7225f22b1956b639f40dff10ee0767b7e"
		     "34e8daf8af129d3",
		.p = "d8708392da9cc40e2cc47ad468621c99",
		.sqrt = "330e06e86764464282b30f49d39e279b",
	},
	{
		.a = "233a0d4f8e8b10457cee27d567331ada6c3bb189737efd83"
		     "6461a7c3b00e0e40",
		.p = "d8708392da9cc40e2cc47ad468621c99",
		.sqrt = "5ef6a2fd26778224d90f8d922c064306",
	},
	{
		.a = "22e5d6d527c72836c1120fb64094f939bf0bf7739cd999f0"
		     "7e04c68e3223c074",
		.p = "d8708392da9cc40e2cc47ad468621c99",
		.sqrt = "5e84dc6e6d114b48d36564b1d9881eac",
	},
	{
		.a = "766cf945129c9768cabe5c8bc206ee011a133fb3",
		.p = "d8708392da9cc40e2cc47ad468621c99",
	},
	{
		.a = "8b8061d0214fb275eb3edf0ac4efc12983c5188a",
		.p = "d8708392da9cc40e2cc47ad468621c99",
	},

	/*
	 * p = 1 (mod 8), long initial segment of quadratic residues
	 */

	{
		.a = "121a50ac860d84f7632ea520155e45108edbf726ef400312"
		     "af265d6d6b542aa",
		.p = "ee690b3287cc4bef7173d1ae8c38af11",
		.sqrt = "1104d9fa5277ffd941b2ca504033f3c1",
	},
	{
		.a = "12bba14492ac49da4e944ac02ac9ded3a2d449cadbba048f"
		     "9d30d2a96f6b9e1",
		.p = "ee690b3287cc4bef7173d1ae8c38af11",
		.sqrt = "115007fab662b063f1c1a85a986d9574",
	},
	{
		.a = "13d8ac7d33c3ea1bcb184abc660fe5ff6ac0b3eace1f6ac0"
		     "76425b4a70b6bf0a",
		.p = "ee690b3287cc4bef7173d1ae8c38af11",
		.sqrt = "474762679be31397e717f008a6d94c44",
	},
	{
		.a = "331f552bbda4b1d992dd79a7dac65fc714d6123502efd1ca"
		     "131eca10e1f832cd",
		.p = "ee690b3287cc4bef7173d1ae8c38af11",
		.sqrt = "72665e9479b1cae103882e27b3896e4e",
	},
	{
		.a = "d39f0377067e6d7814a3b54b74d5fc877e6a150a842dee80"
		     "2a5b35f434e94ef",
		.p = "ee690b3287cc4bef7173d1ae8c38af11",
		.sqrt = "3a3056445c077b79b88dd4feac16ee6f",
	},
	{
		.a = "59ffa4f211e11e9e28bf06313768d9e6f30539ce99a64803"
		     "3b147f97992630b",
		.p = "ee690b3287cc4bef7173d1ae8c38af11",
		.sqrt = "25f271261880530813283b393df6d638",
	},
	{
		.a = "196e733b9783263e5097a258d393314e518d78908dbbd0d9"
		     "8e835ea8811ded62",
		.p = "ee690b3287cc4bef7173d1ae8c38af11",
		.sqrt = "50aff6d9df607c2aab6c09da72abe272",
	},
	{
		.a = "1ad74de96a84df153ed96f4f3305965c688ce85c8e8285b1"
		     "16551b86fbf46c41",
		.p = "ee690b3287cc4bef7173d1ae8c38af11",
		.sqrt = "52e4b15907eafab6fab028f7ff777318",
	},
	{
		.a = "d559fae722d3d02118faf185537d3b9872888085c08f0e33"
		     "2f79667588a85",
		.p = "ee690b3287cc4bef7173d1ae8c38af11",
		.sqrt = "3a6d1d1324934f1c6ab27f4340030b2",
	},
	{
		.a = "1fda911e045e686c13d0ed29ef87858456bfe08a6dcc64c5"
		     "044dfa62c2c48755",
		.p = "ee690b3287cc4bef7173d1ae8c38af11",
		.sqrt = "5a4d79d775b65857a6a3eae7e6c65fd7",
	},
	{
		.a = "c2072a78c83712d1f17d1f4d5146a0b14068e152",
		.p = "ee690b3287cc4bef7173d1ae8c38af11",
	},
	{
		.a = "4a46c4c627d798c4a3d26f415e869f18e2b8cd60",
		.p = "ee690b3287cc4bef7173d1ae8c38af11",
	},
	{
		.a = "33a4da7f8fab68215f235b974676bb5f530284c9c4346bd2"
		     "9c0904947b9893a",
		.p = "c94395af7aa282d79adc16e955ceba59",
		.sqrt = "1cbed89a3a6add32a2caedd7aa666d4a",
	},
	{
		.a = "12454eb2f4fb16b606b45cdd2f1964bcae0c638cd67eebbd"
		     "6e3c12cb58303d0",
		.p = "c94395af7aa282d79adc16e955ceba59",
		.sqrt = "11190392b941d3c89014057d43c2b933",
	},
	{
		.a = "1be7aad902860399a59a3a4ab60355a96233a1095785d78f"
		     "7ef1fad6cbfd499f",
		.p = "c94395af7aa282d79adc16e955ceba59",
		.sqrt = "54852d0df6fc249853261a64a8bfc356",
	},
	{
		.a = "171d95cc535784789996b38108bf0570f7bf822c848bdad4"
		     "017dbd03006159e5",
		.p = "c94395af7aa282d79adc16e955ceba59",
		.sqrt = "4ced03f935a5b8e00177ac488ceda8ed",
	},
	{
		.a = "13bc64edf2aa045dff33a834f301b9ae53f21b0507a52a6b"
		     "9bb9f70ec9fa95e0",
		.p = "c94395af7aa282d79adc16e955ceba59",
		.sqrt = "471487b8b79cd2cc1562319251cfd065",
	},
	{
		.a = "575eaa57283b6ca36106e03ac951f1eb514aef0871563f17"
		     "85b83ae61972cc",
		.p = "c94395af7aa282d79adc16e955ceba59",
		.sqrt = "958e0d5480ae5eb29c7a2031e5e4747",
	},
	{
		.a = "1497fcf0a83b6d52e15c77e5ee46c84843e2d405f8551c16"
		     "961fb7ff05f0df47",
		.p = "c94395af7aa282d79adc16e955ceba59",
		.sqrt = "489bc3d8bc5b72090a498a3e932cbccf",
	},
	{
		.a = "8156269059697cbe658d596ee7316dc56e2da0f3c9c6bc94"
		     "2e318b4f92b3a",
		.p = "c94395af7aa282d79adc16e955ceba59",
		.sqrt = "2d7d908fba94f97f14510009d92f58f",
	},
	{
		.a = "52d9cdd76ed355c0b91964303075ed7ed5b6bf574c78166e"
		     "bb57797cc5988d",
		.p = "c94395af7aa282d79adc16e955ceba59",
		.sqrt = "91a2c7ccc1fa93a8353d40cd3567b0a",
	},
	{
		.a = "241675d474e36edda5c3b52592f3ca2778b80e3cc9d7ccfc"
		     "ff9acea456fad162",
		.p = "c94395af7aa282d79adc16e955ceba59",
		.sqrt = "601dedc64c2739d55eade2b81bf9f8c3",
	},
	{
		.a = "1c1209a5d14e9c5fd860b4095128e11d30727138",
		.p = "c94395af7aa282d79adc16e955ceba59",
	},
	{
		.a = "56962f61002646287cb6213cf007e2b812757b3d",
		.p = "c94395af7aa282d79adc16e955ceba59",
	},
	{
		.a = "53277a6735ee7f2b1e6a1ecbaf7697da5d13467f57c2696a"
		     "7ae29f809cc2bc",
		.p = "e01ad2e26513df5d8863c0407b51c089",
		.sqrt = "91e6fc750eae4d47389d23991722161",
	},
	{
		.a = "fb3df21f43f9d6b0454ae88e536fbe2562b90e242e9d4813"
		     "859f853e37086e",
		.p = "e01ad2e26513df5d8863c0407b51c089",
		.sqrt = "fd9c1dcee2d6f1e72864100bf2250f5",
	},
	{
		.a = "1fe8895b0f43e7c7cf435a9708d90fa6af7e102cd797f1c1"
		     "06d28f270ff6ef0",
		.p = "e01ad2e26513df5d8863c0407b51c089",
		.sqrt = "16985135456ec55e3725b2649e85f1ce",
	},
	{
		.a = "8c18527907be48bfe6bba3f269b16accf36cd895650d6b9d"
		     "e1f131f3fd6b2d",
		.p = "e01ad2e26513df5d8863c0407b51c089",
		.sqrt = "bd60f7b30295f53e4b330e67945b6b7",
	},
	{
		.a = "27ceb721ee3cd6332803e248b636270c224fbad700346d2e"
		     "a5568d415f99ded7",
		.p = "e01ad2e26513df5d8863c0407b51c089",
		.sqrt = "64f2f6748b0c7dd09ad91e9987e57627",
	},
	{
		.a = "22c6e133d12d207d07d9a0884092c502e77d38c260f6b23f"
		     "7d95611ffc98be4",
		.p = "e01ad2e26513df5d8863c0407b51c089",
		.sqrt = "1796b981cb621d743e84f701e0026dd9",
	},
	{
		.a = "25893abcc46a5a2aa1a62ebd893decf99a5d9f5d6d6a094b"
		     "876f3ff330720c4",
		.p = "e01ad2e26513df5d8863c0407b51c089",
		.sqrt = "1881b51449505ca0bdb7eb4f6c7ca0bb",
	},
	{
		.a = "bc4082a238d1575921b5eaab6627422cb203184fb28f0e08"
		     "6060e840dc1899c",
		.p = "e01ad2e26513df5d8863c0407b51c089",
		.sqrt = "36e1c98a1360f6c88e4d7e949a6796c9",
	},
	{
		.a = "11cd69e093fb539abe544337e8a572a6d90f9d5c64535b9c"
		     "c6ea45b5fb873b17",
		.p = "e01ad2e26513df5d8863c0407b51c089",
		.sqrt = "438234d3d5d8a32f5c0fdf0b08364d15",
	},
	{
		.a = "23c0c8b1387989e141f57cf845d006452579c911f9f524e8"
		     "6ca76ce6e15cffae",
		.p = "e01ad2e26513df5d8863c0407b51c089",
		.sqrt = "5fab91205cecc253a5ef159f1279f7ce",
	},
	{
		.a = "16506dcf1e424dff4ff0163ff326011f2db4baf3",
		.p = "e01ad2e26513df5d8863c0407b51c089",
	},
	{
		.a = "7c735c98cb5e13142fb42696c52dea4f86c35f27",
		.p = "e01ad2e26513df5d8863c0407b51c089",
	},
	{
		.a = "1546a26d92c7a1b90e846b8def8a95dcc5fd8d098681485c"
		     "f9f40454c2e56a44",
		.p = "d9adedeb8bc5d4d1a4bbd07dc62a48d1",
		.sqrt = "49cd2327060e71f193255ab50227b50c",
	},
	{
		.a = "40d7e51170443f269b12256b03b7f78916639a565bb91683"
		     "7afae2c07bccc81",
		.p = "d9adedeb8bc5d4d1a4bbd07dc62a48d1",
		.sqrt = "2035cc0bddddef9ab1a6044f713e0e2f",
	},
	{
		.a = "12b4099bd0acc2e80398c544809f6ea261d6cfc6753ff181"
		     "7e19ee7cd5cfebb9",
		.p = "d9adedeb8bc5d4d1a4bbd07dc62a48d1",
		.sqrt = "453215e8591db1b9a53f077b3e388bd2",
	},
	{
		.a = "7e978b624e693fda3c0f159fade85c07690c840dec951b49"
		     "571b7563f4f55",
		.p = "d9adedeb8bc5d4d1a4bbd07dc62a48d1",
		.sqrt = "2d01575650de55dc3553c480bd5d61e",
	},
	{
		.a = "1851ce97073e83d6e3c76ece920c5d91460ab827eeec431b"
		     "94adef9c506c80d0",
		.p = "d9adedeb8bc5d4d1a4bbd07dc62a48d1",
		.sqrt = "4ee75e54c41de8a702b6ce0cb35c7911",
	},
	{
		.a = "1eeeb89f9c697a2a08bff8da8dc6d7fd6e71f43b6922a7cf"
		     "8e89087734e24eb2",
		.p = "d9adedeb8bc5d4d1a4bbd07dc62a48d1",
		.sqrt = "58fcb8ed1d1adcd40427574beb6b6b98",
	},
	{
		.a = "82ea9d2e6ce9f8a7e70d20bb3a00eb0951f0aa9602c98202"
		     "a33cf05cbc3f473",
		.p = "d9adedeb8bc5d4d1a4bbd07dc62a48d1",
		.sqrt = "2dc47a644bfbcd83d67f001ec9f37ec3",
	},
	{
		.a = "7ee5b35c2a7284cce7fc26e681f824c7ef88b92ad6a956e3"
		     "04d32ea49935656",
		.p = "d9adedeb8bc5d4d1a4bbd07dc62a48d1",
		.sqrt = "2d0f39c1415aa6ecdb216eba95e8d130",
	},
	{
		.a = "16691aa76616aa66ba8cb19138b2de30f3e19b4a5f1cc57a"
		     "cc7812c0344a0ca6",
		.p = "d9adedeb8bc5d4d1a4bbd07dc62a48d1",
		.sqrt = "4bbe61678ace7fad9e1f61aac5fc4248",
	},
	{
		.a = "22a64631a16464b2b41f91a716213bc7f5289bd1b8ef5dfe"
		     "5de03fa85e43bf49",
		.p = "d9adedeb8bc5d4d1a4bbd07dc62a48d1",
		.sqrt = "5e2ea04651665b0764861ce7cd63a2c1",
	},
	{
		.a = "7904a8e905770d9c71da1bf00c1246d5365070b2",
		.p = "d9adedeb8bc5d4d1a4bbd07dc62a48d1",
	},
	{
		.a = "7941e180046e485ad69df661a9353c369bffa269",
		.p = "d9adedeb8bc5d4d1a4bbd07dc62a48d1",
	},
	{
		.a = "22c8e6edc37789d7738ec6dcefebbd607d1fdaaa7c584cad"
		     "61d1526800c92018",
		.p = "e26c6cd64976b9391f43981671002d69",
		.sqrt = "5e5da4530248bd7ab1a3fbd08f69f1c5",
	},
	{
		.a = "1ca92a917bec25c99039774b1246aa9f2fb5a2c9ba9f4f27"
		     "fa2b893cb3f559b9",
		.p = "e26c6cd64976b9391f43981671002d69",
		.sqrt = "55a84239f80708583d3f9a76ff73958e",
	},
	{
		.a = "1159fd297bb177aa585fdaa7bd4bd97ba5fec4770aaf40c9"
		     "277496ea0743f4fd",
		.p = "e26c6cd64976b9391f43981671002d69",
		.sqrt = "42a5f38ec5af19489aeb45e7bfcce9de",
	},
	{
		.a = "26f55dd518994145017f5fd0dc182be72ab0dcef72ead33b"
		     "2a69a803f6b19161",
		.p = "e26c6cd64976b9391f43981671002d69",
		.sqrt = "63dde2ed42f80e076e0481dbaf0cbf02",
	},
	{
		.a = "2d1189aaeea16c966889adada17bcb26232c86006015164a"
		     "30742831511eff6",
		.p = "e26c6cd64976b9391f43981671002d69",
		.sqrt = "1ada6d768160903b16fe877dd9aa113f",
	},
	{
		.a = "28acf490426026147635bdc6b062d6d04b635c9b99a198cd"
		     "86b47dc47b96e5f",
		.p = "e26c6cd64976b9391f43981671002d69",
		.sqrt = "1982cf14b64995642cd5dbd245c27afb",
	},
	{
		.a = "2fc7aefd1b206bcfb2644576a4c8cca7fe9a72a7b2e4dcc8"
		     "ed127ffb685fd14",
		.p = "e26c6cd64976b9391f43981671002d69",
		.sqrt = "1ba6344ba57e08173d21a98328b71684",
	},
	{
		.a = "f035046ddb03b1e343cc2e5dc24a91fe807dbd7906b2f16f"
		     "02480db9c8627e5",
		.p = "e26c6cd64976b9391f43981671002d69",
		.sqrt = "3dfe9533519e67d651b4c524b737dd99",
	},
	{
		.a = "3609bf3e9b32879a3add17aa6d1542c2f549030d23bf4cb0"
		     "4d65a353f11230f",
		.p = "e26c6cd64976b9391f43981671002d69",
		.sqrt = "1d677c1e06055bd8d77565b6e97b18f4",
	},
	{
		.a = "103a6d3816aa8b314402c12ffb54a111bfc100f599aa5f78"
		     "ff9574c3523e0121",
		.p = "e26c6cd64976b9391f43981671002d69",
		.sqrt = "40747083db7013669a8050e31542a781",
	},
	{
		.a = "4ce4c0d22add35bbe43784924de6dbc0906e533d",
		.p = "e26c6cd64976b9391f43981671002d69",
	},
	{
		.a = "c5c1e597f6dd8751825916133738c2f42c04114",
		.p = "e26c6cd64976b9391f43981671002d69",
	},
	{
		.a = "beeeb4fc61586d0d2d7149410c8bfaa7129f65cd12accd00"
		     "ae821bac17ba37",
		.p = "e74a67bb1628c7b1d923808f320470a9",
		.sqrt = "dd15d5ca7cdce13ab7e98c88fc88f19",
	},
	{
		.a = "2a0b5b4376ec8b0bbb8f0a283a0a317fd09589bdc8e693f0"
		     "031c2bc1a1ae087b",
		.p = "e74a67bb1628c7b1d923808f320470a9",
		.sqrt = "67bf2103f485c73c075cbb5f8243bad9",
	},
	{
		.a = "26c4d966b33befb307a16c8a87acab27a12f94b7921af11e"
		     "430adeacac82a62d",
		.p = "e74a67bb1628c7b1d923808f320470a9",
		.sqrt = "639fa027439489f0eef60d704e8cef98",
	},
	{
		.a = "29eadb8e533f3a9e325bf9bbbc9db41291f5c93281e02a70"
		     "3e7153c744a817e6",
		.p = "e74a67bb1628c7b1d923808f320470a9",
		.sqrt = "6797009adb6ae5b392734d13664e56b2",
	},
	{
		.a = "e4aa928dd30d75d78f5074e8931d697b45c4a65b6ba31109"
		     "86e9bd08fa3cb8e",
		.p = "e74a67bb1628c7b1d923808f320470a9",
		.sqrt = "3c7ca3274282f6fbf175d681fe84526d",
	},
	{
		.a = "32f1d70f5fadfb94c555b61651de9eea025b7150feefddda"
		     "6d5319eb6bdfbe37",
		.p = "e74a67bb1628c7b1d923808f320470a9",
		.sqrt = "72336c9bfae5183830819108df6061da",
	},
	{
		.a = "1c941c73a20169cb3bf4f4c3c37690d041402787e7d12156"
		     "6269ccbbc4990a46",
		.p = "e74a67bb1628c7b1d923808f320470a9",
		.sqrt = "5588c5d715f5f9e0c90823d91b6c7cc7",
	},
	{
		.a = "1579253f887c3a8185c70f385203a19b867d8bf51657e531"
		     "78ac6d665c7ba070",
		.p = "e74a67bb1628c7b1d923808f320470a9",
		.sqrt = "4a248a6f3b66c3dbcf633d86248b6bbf",
	},
	{
		.a = "1a12783318c06b7477ea1f2c7323bf1bade4969a5c2b2d1c"
		     "d5de12ca124bc7b5",
		.p = "e74a67bb1628c7b1d923808f320470a9",
		.sqrt = "51b28a8e6affa359cb3d437785560a6d",
	},
	{
		.a = "b42d6aedb666a8102825bcb9af19b001b37c1902cc3a5751"
		     "9775caa627d0c69",
		.p = "e74a67bb1628c7b1d923808f320470a9",
		.sqrt = "35b12ba2fc934dbfb1f447ea5063f20e",
	},
	{
		.a = "bf7b664d321a2d4a31c0d798f674c0e25a885ec1",
		.p = "e74a67bb1628c7b1d923808f320470a9",
	},
	{
		.a = "2a324cfa40e3117fc1da8357e67be94c39365a56",
		.p = "e74a67bb1628c7b1d923808f320470a9",
	},
	{
		.a = "82e929f47c509779ae81f2eb12dcac84c1288827cd7909ae"
		     "43dbb8bdef71eef",
		.p = "d64bb8768a638d7f5d3825337ace2de9",
		.sqrt = "2dc43980830b15b0c83573d07afe6999",
	},
	{
		.a = "4589649eb9b85e1be15abcc1f48f2eb4c58b197ebac83abc"
		     "8d809024e40803e",
		.p = "d64bb8768a638d7f5d3825337ace2de9",
		.sqrt = "215affc621edf6c85922903ab48ea658",
	},
	{
		.a = "1c868a42c164513eaf724725ca5994f241c16b2e68f303e1"
		     "b5d57b57c1183304",
		.p = "d64bb8768a638d7f5d3825337ace2de9",
		.sqrt = "55747461ff8137d4e05eeaf2c29aa0a8",
	},
	{
		.a = "518e36afe548286568e99c20afef30074ecac2b1c25ac73d"
		     "9556855fda6e2c",
		.p = "d64bb8768a638d7f5d3825337ace2de9",
		.sqrt = "907e3233f20c0db9a659d896a9c2512",
	},
	{
		.a = "4c41116acdaf952fadd8643ad7db01266cbbd3f3dc86242c"
		     "7b255fa17f7bedf",
		.p = "d64bb8768a638d7f5d3825337ace2de9",
		.sqrt = "22edf0b170fabd22b9cd2919e45ebc77",
	},
	{
		.a = "1deb14e6e04d5c3da1bde36ed62b5c98b30d9637ce70d467"
		     "1553062f135fb743",
		.p = "d64bb8768a638d7f5d3825337ace2de9",
		.sqrt = "578424546feb87643c07224b5ac59802",
	},
	{
		.a = "19b9585337d4bf6c126db6f56929140ceff37c6072503ff1"
		     "cdf661c36471bf",
		.p = "d64bb8768a638d7f5d3825337ace2de9",
		.sqrt = "51266f7e4ea87ca3749d508ed010664",
	},
	{
		.a = "e79ca7697fb17fca33dc2be93bc1408b890da7861b685f4f"
		     "432d87a3f0f152f",
		.p = "d64bb8768a638d7f5d3825337ace2de9",
		.sqrt = "3ce00d970ebd09af262bca93d838fa1f",
	},
	{
		.a = "2ad3e70e0575ddcb8a6b150eb98caba70854861249aefd23"
		     "d517198827f406f6",
		.p = "d64bb8768a638d7f5d3825337ace2de9",
		.sqrt = "68b56a7414fae89868d5bf8352d273fa",
	},
	{
		.a = "13120bbe4f9aa9295e79f6e1d14c6e4ad6857a9cb781542c"
		     "d803927706a35b26",
		.p = "d64bb8768a638d7f5d3825337ace2de9",
		.sqrt = "45df23b41b892c677376741d846897ec",
	},
	{
		.a = "39c6f0ca17b31cdd576136a42afacc1951195d46",
		.p = "d64bb8768a638d7f5d3825337ace2de9",
	},
	{
		.a = "364f883be424ee5552e3e2964066cf720981ef15",
		.p = "d64bb8768a638d7f5d3825337ace2de9",
	},
	{
		.a = "2a482b02a0fa79c96ef9d00287b3b5eb306bd82d29cbcee0"
		     "d4698e0d6cad30fa",
		.p = "d3b5db60b30e3077d3bc605643d0a579",
		.sqrt = "680a0d10bec5fd5a019917ee00f61184",
	},
	{
		.a = "dace96b6e89e3d170e76b81a9f023d9f364a80e9ff40cdd3"
		     "338e2b538177cce",
		.p = "d3b5db60b30e3077d3bc605643d0a579",
		.sqrt = "3b2b230a1222cd9af8aa60978608e5b1",
	},
	{
		.a = "1350c9c08466500847eb0573aa711123b77d86a12d52184e"
		     "584496ebb278e1f8",
		.p = "d3b5db60b30e3077d3bc605643d0a579",
		.sqrt = "4651b6413d019c9b27af8be350b9efbd",
	},
	{
		.a = "309971a3c9a159c454fe42fcbabe744b8ad1b8aa8ba475b5"
		     "8466da45a84de",
		.p = "d3b5db60b30e3077d3bc605643d0a579",
		.sqrt = "1be2a35bf675130b7362bd13d1bd119",
	},
	{
		.a = "e88515de6431e1ac3259fc16bc996b369e621113c8505c34"
		     "ba18d7a77239e80",
		.p = "d3b5db60b30e3077d3bc605643d0a579",
		.sqrt = "3cfe918a2d7deaec83f14014593287dd",
	},
	{
		.a = "dd42d98bed06e1eee7825a3df6ee9a9406b501801e35fd43"
		     "54538ed582c38b1",
		.p = "d3b5db60b30e3077d3bc605643d0a579",
		.sqrt = "3b7fd868d0022fc7f467d06f3a673aef",
	},
	{
		.a = "e47b2f19b59423aeddf737a59b95433d6c335de8380113c4"
		     "cdb781e9851d2fd",
		.p = "d3b5db60b30e3077d3bc605643d0a579",
		.sqrt = "3c765e522c77141a85a7f5be88e6dce9",
	},
	{
		.a = "1317abf2c6630cecee00a0b2c3619269c6abc56b19d52521"
		     "afdeed13e8181498",
		.p = "d3b5db60b30e3077d3bc605643d0a579",
		.sqrt = "45e9714caf9599488f7b5f50663a6f47",
	},
	{
		.a = "fb4a9ec53f4b5b8337db3b52729f92660c540a57ba26f6a3"
		     "1132c3df8e7fbcc",
		.p = "d3b5db60b30e3077d3bc605643d0a579",
		.sqrt = "3f68a0d5d9b6773b6462d520fc15a592",
	},
	{
		.a = "17bde3718d8c79dea7bf8177b429b9fbbe2ec377ee056318"
		     "8b1723601dfe97f4",
		.p = "d3b5db60b30e3077d3bc605643d0a579",
		.sqrt = "4df5f7e0af55aa287adf15f97d58a39f",
	},
	{
		.a = "9d6a2d424236cef8719d331a7352de39ecf44983",
		.p = "d3b5db60b30e3077d3bc605643d0a579",
	},
	{
		.a = "c09b3647eca9c87b3a542b0c1e734bf52ce314df",
		.p = "d3b5db60b30e3077d3bc605643d0a579",
	},
	{
		.a = "1d8903ab585dc5b9992ba2f4295e058111c46c3620554d27"
		     "5e46107e55d7a2c",
		.p = "d221951118210ae00de11d059add5179",
		.sqrt = "15bd0fdd249f3c6a8c2f548736905f23",
	},
	{
		.a = "f85fac41fc1b4425c176f56f6e9d62e1e76e1b2211ef4fb4"
		     "59c33ae66b796c",
		.p = "d221951118210ae00de11d059add5179",
		.sqrt = "fc2874c1abda2571ab8a869d1808175",
	},
	{
		.a = "e42771a0f23fd6acdfb6aa40aca83b16c58759815f4dc6a3"
		     "8fd3eef95cee3ea",
		.p = "d221951118210ae00de11d059add5179",
		.sqrt = "3c6b48d5885c257eca850ffce85e46be",
	},
	{
		.a = "195fc0fe25e90c6173a995e481fee9e7e256409fe143fea6"
		     "1826da3486c3e297",
		.p = "d221951118210ae00de11d059add5179",
		.sqrt = "5098a32cc135bdea68430ed532ea0005",
	},
	{
		.a = "b9ba371e8df3cd27092a0acc00e0fb452607e13a8cd3a003"
		     "515de9e1d7bda62",
		.p = "d221951118210ae00de11d059add5179",
		.sqrt = "368342b57f60ae446e6effdf172b074b",
	},
	{
		.a = "110fccbd58d822da1cf00b2ebf75e8479fba35602ae212f8"
		     "7abe5470aa0a8dc",
		.p = "d221951118210ae00de11d059add5179",
		.sqrt = "1085b79c4bb04aeb47ee70702a56c09e",
	},
	{
		.a = "770192a443c51e1652e7057f01317a4daa394f698c7e8552"
		     "a2708ce9a171da8",
		.p = "d221951118210ae00de11d059add5179",
		.sqrt = "2ba2cf3f71977f2ab422dc333af01aed",
	},
	{
		.a = "d64e10830c62eca1dac4bc5f1fa566f9504bb15ca82c63a3"
		     "efa32cef3b1c5b0",
		.p = "d221951118210ae00de11d059add5179",
		.sqrt = "3a8e7f5e1cb03581dc78638cf4860450",
	},
	{
		.a = "1e44103d73e7744ed150aea64c9781bb286d0c8896086aad"
		     "a0d01ec4018edd9e",
		.p = "d221951118210ae00de11d059add5179",
		.sqrt = "5805e8e0c4c53dfa1e3ea9e4ef6659db",
	},
	{
		.a = "2602d83a0b822bf52b8a02e7a5df83d06bf6f75f52658484"
		     "03a60c25c70d2fb",
		.p = "d221951118210ae00de11d059add5179",
		.sqrt = "18a9486523c9cbdcc1d9d812345978a3",
	},
	{
		.a = "7575749b1df9decda4eeb931f3e077a35ab17cb4",
		.p = "d221951118210ae00de11d059add5179",
	},
	{
		.a = "1ab8eec9aa32cd78e7fccc74bea5bc666224eae5",
		.p = "d221951118210ae00de11d059add5179",
	},
	{
		.a = "1fcb7176f1300935e5f662830115c7330fe0a336e27f1ded"
		     "642f51a97f71f6c",
		.p = "e9103af488d3bc5209fe348dcf043dc1",
		.sqrt = "168e01d97eb477d70cd284571dfff3ec",
	},
	{
		.a = "a856d9e3eb0073dc8acf9736094df283bfaa8ecf94d633d3"
		     "0c21fd34154c3df",
		.p = "e9103af488d3bc5209fe348dcf043dc1",
		.sqrt = "33e5f39cdd6b39931e1295525323eced",
	},
	{
		.a = "23ca9267d09a2a9697887b8d71794f91e7ad6ee2282a6247"
		     "5d37273ea49cfb24",
		.p = "e9103af488d3bc5209fe348dcf043dc1",
		.sqrt = "5fb8a8b3082f117d9c32ba395772f1f9",
	},
	{
		.a = "2e5adfa8b2e4baf9a120ad13afc13d420ea82b7b6552ef49"
		     "b93d827edd782420",
		.p = "e9103af488d3bc5209fe348dcf043dc1",
		.sqrt = "6cef6806482bb27a8f431c2be11da15d",
	},
	{
		.a = "1856d952cfe49f088152e15c8a35a6e02e9e6471ef8ba7c2"
		     "a8085d01f197911c",
		.p = "e9103af488d3bc5209fe348dcf043dc1",
		.sqrt = "4eef8bc5cbe83339d5363dd75fcb906c",
	},
	{
		.a = "809153ea20aab3373a44470d4a92e5415293c42084757301"
		     "4ecef756ebe3b6e",
		.p = "e9103af488d3bc5209fe348dcf043dc1",
		.sqrt = "2d5ae64f52b011c2b47b196d3257815a",
	},
	{
		.a = "d8f2c65347eba4148fed9f5420a50727915e41417b090095"
		     "d011f2dd85d13a1",
		.p = "e9103af488d3bc5209fe348dcf043dc1",
		.sqrt = "3aeaaa8f5a07973ffdc5c96e28c6b6a0",
	},
	{
		.a = "2fc2023299021c3bbd160fdc96f3f235105bec74eedd0bc5"
		     "ba2c6ba3aea5845",
		.p = "e9103af488d3bc5209fe348dcf043dc1",
		.sqrt = "1ba48fe5d1a5c1b26b142965ce0dd426",
	},
	{
		.a = "7c57bc6f695242e8c31ea7ea07ccb856fd80291ab481aab2"
		     "66980264ffa3b64",
		.p = "e9103af488d3bc5209fe348dcf043dc1",
		.sqrt = "2c9a8745a0f72ec9dbb8d2ad4e86e6cf",
	},
	{
		.a = "194e31d9e40a273b975b87cf5b4956b892b0b810e9c56d63"
		     "9731eb011f3b5bba",
		.p = "e9103af488d3bc5209fe348dcf043dc1",
		.sqrt = "507cbb530412e9a988aeecda15a9c9e1",
	},
	{
		.a = "d26fa79e1156dd232b5886efaf8b5b7a08dc5740",
		.p = "e9103af488d3bc5209fe348dcf043dc1",
	},
	{
		.a = "cceefd8c7bc0a4b82b8eacbbcb786f9b21f6ff31",
		.p = "e9103af488d3bc5209fe348dcf043dc1",
	},
};

const size_t N_TESTS = sizeof(mod_sqrt_test_data) / sizeof(*mod_sqrt_test_data);

static int
mod_sqrt_test(struct mod_sqrt_test *test, BN_CTX *ctx)
{
	BIGNUM *a, *p, *want, *got, *diff;
	int failed = 1;

	BN_CTX_start(ctx);

	if ((a = BN_CTX_get(ctx)) == NULL)
		errx(1, "a = BN_CTX_get()");
	if ((p = BN_CTX_get(ctx)) == NULL)
		errx(1, "p = BN_CTX_get()");
	if ((want = BN_CTX_get(ctx)) == NULL)
		errx(1, "want = BN_CTX_get()");
	if ((got = BN_CTX_get(ctx)) == NULL)
		errx(1, "got = BN_CTX_get()");
	if ((diff = BN_CTX_get(ctx)) == NULL)
		errx(1, "diff = BN_CTX_get()");

	if (!BN_hex2bn(&a, test->a))
		errx(1, "BN_hex2bn(%s)", test->a);
	if (!BN_hex2bn(&p, test->p))
		errx(1, "BN_hex2bn(%s)", test->p);

	if (BN_mod_sqrt(got, a, p, ctx) == NULL) {
		failed = test->sqrt != NULL;
		if (failed)
			fprintf(stderr, "BN_mod_sqrt(%s, %s) failed\n",
			    test->a, test->p);
		goto out;
	}

	if (!BN_hex2bn(&want, test->sqrt))
		errx(1, "BN_hex2bn(%s)", test->sqrt);
	if (!BN_mod_sub(diff, want, got, p, ctx))
		errx(1, "BN_mod_sub() failed");

	if (!BN_is_zero(diff)) {
		fprintf(stderr, "a: %s\n", test->a);
		fprintf(stderr, "p: %s\n", test->p);
		fprintf(stderr, "want: %s\n", test->sqrt);
		fprintf(stderr, "got: ");
		BN_print_fp(stderr, got);
		fprintf(stderr, "\n\n");

		goto out;
	}

	failed = 0;

 out:
	BN_CTX_end(ctx);

	return failed;
}

static int
bn_mod_sqrt_test(void)
{
	BN_CTX *ctx;
	size_t i;
	int failed = 0;

	if ((ctx = BN_CTX_new()) == NULL)
		errx(1, "BN_CTX_new()");

	for (i = 0; i < N_TESTS; i++)
		failed |= mod_sqrt_test(&mod_sqrt_test_data[i], ctx);

	BN_CTX_free(ctx);

	return failed;
}

/*
 * A list of primes p = 1 (mod 8) with long initial segments of quadratic
 * residues. These exercise the non-deterministic path of Tonelli-Shanks.
 */

#define N_SMALL_SQUARE_TESTS 100

static const struct p_is_1_mod_8_tests {
	const char *p;
	int first_non_square;
	const char *sqrt[N_SMALL_SQUARE_TESTS];
} p_is_1_mod_8_tests[] = {
	{
		.p = "d7a6133d89b7a840ec0d80d2ee197849",
		.first_non_square = 101,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "127b07d9558aa7382fda337e674b0020",
			[3] = "2b0756607c913be85534cec59846e6f8",
			[4] = "2",
			[5] = "1ac3bfe91dd86f91be44b30936b6bcff",
			[6] = "3a03fcd8bc70d0a08444356250a316c9",
			[7] = "22c3c29b3cf04300495e694db5a1fb11",
			[8] = "24f60fb2ab154e705fb466fcce960040",
			[9] = "3",
			[10] = "46dce6880d735d143a40b99097f87f9e",
			[11] = "3eab85485728056c43cd2681a96976ba",
			[12] = "560eacc0f92277d0aa699d8b308dcdf0",
			[13] = "19bf2319a32e92a6c2d80d364a19229d",
			[14] = "51554e1d7adc5173841a9525b4a2ff7d",
			[15] = "3d9a295185f27dc096b33184e4edd834",
			[16] = "4",
			[17] = "407d75edc1fba51c637aeddcb35abc1e",
			[18] = "3771178c009ff5a88f8e9a7b35e10060",
			[19] = "9e48ccec873561b315a0980c409c291",
			[20] = "35877fd23bb0df237c8966126d6d79fe",
			[21] = "2f4a3220291ac0440f3a2cb35d8e8f4c",
			[22] = "218f85684d6378bf6a90d02b0f94dc11",
			[23] = "432f4d621cc773b7945a024a076048d4",
			[24] = "639e198c10d606ffe385160e4cd34ab7",
			[25] = "5",
			[26] = "8478976383e49d4fbda384883343e1",
			[27] = "5690101c1403f487ec6f14822544c361",
			[28] = "4587853679e0860092bcd29b6b43f622",
			[29] = "47804840abaf4bc53e37c87291f55527",
			[30] = "48925009cc14fbef1dee0b4a2f920890",
			[31] = "228ef1765a5448f6a4600026dfb14830",
			[32] = "49ec1f65562a9ce0bf68cdf99d2c0080",
			[33] = "a41fb079bc22c9d1e5658a86db0e6bb",
			[34] = "675b3a95cddf5b210dde6d3e7dd4cfa3",
			[35] = "5b727e9bccd4da204cac92594babe837",
			[36] = "6",
			[37] = "64ba7d11b9d672e6d31a9b5971e60d2f",
			[38] = "24afa6855c01017537555a58a70da6f9",
			[39] = "3bb009b3541796452327d8e6fd8323ba",
			[40] = "49ec462d6ed0ee18778c0db1be28790d",
			[41] = "545f0385bd365fa7862502739a781f69",
			[42] = "2b0b17173a32035911223d142ab00a4e",
			[43] = "67109cc9a742fc0305d031cc20e7fd1f",
			[44] = "5a4f08acdb679d68647333cf9b468ad5",
			[45] = "504b3fbb59894eb53ace191ba42436fd",
			[46] = "38e6358e14aa1cd9778b09cbf05deb48",
			[47] = "319d132fc72dff9b631b3c2ca0c2f49f",
			[48] = "2b88b9bb9772b89f973a45bc8cfddc69",
			[49] = "7",
			[50] = "5c67273eabb54418ef430178047700a0",
			[51] = "3558bf08253c3d5afd58bd1c3c0498e3",
			[52] = "337e4633465d254d85b01a6c9432453a",
			[53] = "1845657cc9012fafd8a266bf41b2ce66",
			[54] = "299a1cb35465365f5f40e0abfc3033ee",
			[55] = "582fca95baf4c0b18befc9aa5b09aff4",
			[56] = "34fb770293ff0559e3d8568784d3794f",
			[57] = "62e847fb9f593dbe7d2f1a5c74594670",
			[58] = "1c85e173426916ea88c889b950bd5d96",
			[59] = "13e4d1ad2ef6d69c758826e554d3fce7",
			[60] = "5c71c09a7dd2acbfbea71dc9243dc7e1",
			[61] = "5bd4d7bf50d72c7642321ac648a41207",
			[62] = "4d543e0b7cb7a087f6c6820871402203",
			[63] = "684b47d1b6d0c900dc1b3be920e5f133",
			[64] = "8",
			[65] = "4720271c07130a09c11989006d5b65b7",
			[66] = "27c5c5d3ef4be7d25b799ea455ff933",
			[67] = "563c042e90bd60de622c83b96aad46f3",
			[68] = "56ab276205c05e082517a5198764000d",
			[69] = "40aabad2f12469f355cbd0e524021817",
			[70] = "69f41210ae2c64ebbbfb9cc4436b121b",
			[71] = "2495fdd3e3ee43c51d78a037f2af961",
			[72] = "68c3e4258877bcefccf04bdc82577789",
			[73] = "a54afbfe8d22b43dba10c45c3e1945a",
			[74] = "387797174f3bf5d6281fe71ea18676e1",
			[75] = "81635b1ae17cb7420576f6f4b6f571",
			[76] = "13c9199d90e6ac3662b4130188138522",
			[77] = "2095521a5b009be0acd08b5ee47fa519",
			[78] = "2bdce5c6e97146d22fd25f4abb3bfa49",
			[79] = "15d3c2c27cdc4411498596c20ef7d174",
			[80] = "6b0effa47761be46f912cc24dadaf3fc",
			[81] = "9",
			[82] = "12af1ddc568bab9c8609a1069ad98974",
			[83] = "4ba992f528674a24e7d11121eb88af39",
			[84] = "5e946440523580881e745966bb1d1e98",
			[85] = "1a0d8dbdac9222fddfacbae62f73dc50",
			[86] = "4040b1fc21cc440034ac36b0e9eb9c90",
			[87] = "1203e4b669c80a95eb2a5134436217aa",
			[88] = "431f0ad09ac6f17ed521a0561f29b822",
			[89] = "62aa36cb4434c129d65d92ff25a9c3c4",
			[90] = "30f5fa5615d91043d4b5421262ff96f",
			[91] = "2b64e196026445e615b7c019aafc52c4",
			[92] = "514778795028c0d1c3597c3edf58e6a1",
			[93] = "25eb81b5dcc44f0a8bcf7fb909690c50",
			[94] = "48e96bc3845c7d38eb06fdf2d9274207",
			[95] = "18ae0da0ae22f6cdee901b535977cfe",
			[96] = "1069e025680b9a41250354b65472e2db",
			[97] = "6bc184c777e6c859c4ce144eb68d6bef",
			[98] = "5648dc4c32ed15b79d16185e1b0c7769",
			[99] = "1ba38364843f97fc20a60d4df1dd141b",
		},
	},
	{
		.p = "ed2ca93a2b8b7053b5cbfbfc164284d9",
		.first_non_square = 83,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "3a3c6e5ea115faa8be69eac1c177f698",
			[3] = "2748dedd33f6c6a09f84973f2f862922",
			[4] = "2",
			[5] = "d7607decaa9bfe5ec031537ce343ed0",
			[6] = "401d83de7c83456c0bee28fd25829dd5",
			[7] = "21f72780e6f6789259086212c3e9538c",
			[8] = "7478dcbd422bf5517cd3d58382efed30",
			[9] = "3",
			[10] = "28f8f6227299e5883b1a59e07b4cdec",
			[11] = "4b557ee20b98bd00344c14563ca8f839",
			[12] = "4e91bdba67ed8d413f092e7e5f0c5244",
			[13] = "10242586b3f9e2ca74409066950b0dc",
			[14] = "a4bfde50f20b288cf44d40af83abd84",
			[15] = "43fffde1b3477313dd57b9cd7261cdc3",
			[16] = "4",
			[17] = "524907a08de4e5c0c153c544c87bf9ce",
			[18] = "3e775e1e484980597a8e3bb6d1daa111",
			[19] = "427441618f7df01a97f7435904162b4d",
			[20] = "1aec0fbd95537fcbd8062a6f9c687da0",
			[21] = "6cb69f443aa14ed65113febd0cfbdf66",
			[22] = "7311499cc97a3d0f4a9c0597b2c761ec",
			[23] = "2e4c8606309b68cd815085fb51529a9c",
			[24] = "6cf1a17d3284e57b9defaa01cb3d492f",
			[25] = "5",
			[26] = "3565c3eaf0daac1e3a2c028657569676",
			[27] = "75da9c979be453e1de8dc5bd8e927b66",
			[28] = "43ee4f01cdecf124b210c42587d2a718",
			[29] = "35864fac94ecba016620c63c0ccc5f56",
			[30] = "3c85a120dba23570cc87f22b973f1654",
			[31] = "6838d8565fcbbc5dbf597fb37a1f5406",
			[32] = "43aefbfa73385b0bc2450f51062aa79",
			[33] = "45bc0d05b45e92a9ec86fdb68d4d8b6d",
			[34] = "617de3651efcd26b6524a971fc68398c",
			[35] = "4101878d7ace3b9fb358dad9ace0a5c8",
			[36] = "6",
			[37] = "673a3026784b7a41328bac018550b86f",
			[38] = "2b23c1a47cab7f8fcd0105546e230715",
			[39] = "4e915dc6234ac56a095df35c40ea4907",
			[40] = "51f1ec44e533cb107634b3c0f699bd8",
			[41] = "3360c9282c4fdef670b93037989ff143",
			[42] = "35e69c414ce1d44b024bcd9a7225a5c1",
			[43] = "2e48de0b4bd10c05fbfb2f9719b20e69",
			[44] = "5681ab761459f6534d33d34f9cf09467",
			[45] = "2862179c5ffd3fb1c4093fa76a9cbc70",
			[46] = "643d5c5c9b5af336e504f41726dfc0d3",
			[47] = "3e98383357bcacf462c70cc9351a91a0",
			[48] = "50092dc55bb055d137b99eff5829e051",
			[49] = "7",
			[50] = "36017e9ef9e274f8024599ccb1154c1f",
			[51] = "5bf023ca007e263e4b5a030838af810c",
			[52] = "20484b0d67f3c594e88120cd2a161b8",
			[53] = "2b9e8a3b3ed1ed9329bede411f92778c",
			[54] = "2cd41d9eb601a00f92018104a5baab5a",
			[55] = "5bec2e51e71bb1f1a852e3e56d43a184",
			[56] = "1497fbca1e4165119e89a815f0757b08",
			[57] = "63423607948c915df58134dba2f29d07",
			[58] = "18d9afef351887618e7bb9e3cfb0211",
			[59] = "3ea4eee3e48e677c53aa0f392d896490",
			[60] = "652cad76c4fc8a2bfb1c8861317ee953",
			[61] = "66fbaa07b40873c3ac00b38cb100f017",
			[62] = "6a018a4798e4a471a9ff4fb9dac762f",
			[63] = "65e57682b4e369b70b1926384bbbfaa4",
			[64] = "8",
			[65] = "6188263fe8ff27e6249f9fb45efc8a9",
			[66] = "3cff332519838822fd763253838bf932",
			[67] = "4c83bbd2d19d60743a698d36db22c917",
			[68] = "489a99f90fc1a4d233247172854a913d",
			[69] = "38607b0f81008cc1a1dd707a16fc4d28",
			[70] = "4eecbfcfce6b4da4c456d606983c3ef4",
			[71] = "303995f9d14e1722b14e98314f7064ca",
			[72] = "703decfd9af86fa0c0af848e728d42b7",
			[73] = "17b2676a1150040ad999fbcd06f41d3a",
			[74] = "1a7989bf779d64cf9a301debf4fec091",
			[75] = "28c04ee827b98f30983507c028a3b72f",
			[76] = "684426770c8f901e85dd754a0e162e3f",
			[77] = "2b5c61ea91f69b1c7c7ebaeefb3240c5",
			[78] = "568763066059e114d011688b51121883",
			[79] = "a43ac4b02b98857c9d659c953154885",
			[80] = "35d81f7b2aa6ff97b00c54df38d0fb40",
			[81] = "9",
			[82] = "d688cbf38d1530c3d11285f8a829bc3",
			[84] = "13bf6ab1b648d2a713a3fe81fc4ac60d",
			[85] = "4760657d9d6c47c60833c08573e79b80",
			[86] = "3532206ae6f0f9a9721be80adf3b3df4",
			[87] = "755e6970bfe25e6ac8bf805cf22b7343",
			[88] = "70a16009896f6352093f0ccb0b3c101",
			[90] = "7aeae26757cdb098b14f0da171e69c4",
			[91] = "2bbeb474b349124e3ef6c222b62ad7b3",
			[92] = "5c990c0c6136d19b02a10bf6a2a53538",
			[93] = "46e5aa500f60629978168938a0c01682",
			[94] = "228459bf06f9dfb15baf8ac31efe34c1",
			[95] = "69e2c2104da63c9b919341b9013db3b7",
			[96] = "1349663fc681a55c79eca7f87fc7f27b",
			[97] = "39c6a22fb3f9d6c91f40fc217241c45e",
			[98] = "42b24dddef7d060a36b28cabe23d4b8a",
			[99] = "b2c2c9408c1395318e7bef960479c2e",
		},
	},
	{
		.p = "f9df802d991ea5ebaed8d3b9a42cf101",
		.first_non_square = 59,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "9b87b93bb96389be7801b89dc1652d8",
			[3] = "59e13489928842f26ae2886d9310b8f3",
			[4] = "2",
			[5] = "4c5c3af3b9fab5886e65b82402dad66f",
			[6] = "69e583a41fd4c41fad8dfafe1d64ce80",
			[7] = "2226e4abbf4dabfbc798cafda99b0c40",
			[8] = "1370f727772c7137cf003713b82ca5b0",
			[9] = "3",
			[10] = "56069730d91a5cc07e2a98a6a68b7202",
			[11] = "3f7c0d6dd412594787118d6095384c3a",
			[12] = "461d171a740e2006d913c2de7e0b7f1b",
			[13] = "40b3ccbea21d3d72c74263ab6879d65f",
			[14] = "119ec14e3683163583a5553d2a0e9311",
			[15] = "2aa84242c307fd827f2ab90abc5e7d0e",
			[16] = "4",
			[17] = "79b4a0e036f6d7ac8945c382d3135b5d",
			[18] = "1d2972bb32c2a9d3b680529d9442f888",
			[19] = "4fc091a564105b66fd19904959dd1c2b",
			[20] = "61270a4625293adad20d63719e774423",
			[21] = "5a0e8b4c8c5c042b55235a9cb7adaf60",
			[22] = "35a1aa98a855b5d2c77f916145f41758",
			[23] = "72a841aa741cb731c7986c8eee1af3b",
			[24] = "261478e559751dac53bcddbd69635401",
			[25] = "5",
			[26] = "47e09b3873557e6bc1ceadd3969fbe88",
			[27] = "13c41d6f1e7a22eb91cec58f150539d8",
			[28] = "444dc9577e9b57f78f3195fb53361880",
			[29] = "57798532e4e9404df4b67adf52c37099",
			[30] = "3590327cc41d87a9e09fd8d9619c06c6",
			[31] = "e15fec88e2da65f950f0a29275df452",
			[32] = "26e1ee4eee58e26f9e006e2770594b60",
			[33] = "614bdf992ff880c1f9092b13cdc354f2",
			[34] = "16f4ebd0bea0a507679dfed7c67cfa36",
			[35] = "84b3b3b80ffec8c1d8269511ac835ac",
			[36] = "6",
			[37] = "7533be777b68bc56c672a6970f2021d4",
			[38] = "66d2c23bdb3236d9d65c761f5cd0b606",
			[39] = "68bdd47cfdfa7a81e2550461aae74818",
			[40] = "4dd251cbe6e9ec6ab283a26c57160cfd",
			[41] = "38d5408abd422d8fcf19e8cf070d342d",
			[42] = "358408a1b5fa21b4ad7eaeab5970dd7d",
			[43] = "395a8a9786765fcc10641a5932985fac",
			[44] = "7ae76551f0f9f35ca0b5b8f879bc588d",
			[45] = "14cacf526b2e855263a7ab4d9b9c6db4",
			[46] = "fefca61affd696e5b8c9179dc6ea1b5",
			[47] = "70c9d4ca66eb1683ef376ed8e27f78de",
			[48] = "6da551f8b10265ddfcb14dfca815f2cb",
			[49] = "7",
			[50] = "309a69e2a9ef1b0b858089b14c6f9e38",
			[51] = "2f7c7a1a80c51f68ca01ad786d31730a",
			[52] = "7877e6b054e42b0620540c62d3394443",
			[53] = "10cd478dce3a9dc19548ee40ca0c2b0",
			[54] = "43d10abec65fa67359d11d40b4017a7f",
			[55] = "528ec8c6e928f443382d5537ef5e4844",
			[56] = "233d829c6d062c6b074aaa7a541d2622",
			[57] = "72bd2260fffce41b2fceb568f468cd5f",
			[58] = "7159169c936f53e5d9e62a97f2c7fcaa",
			[60] = "55508485860ffb04fe55721578bcfa1c",
			[62] = "549a3f9a716d2b96a14ac143160611cc",
			[63] = "6674ae033de903f356ca60f8fcd124c0",
			[64] = "8",
			[65] = "645b10018a4ad21e827fe92a1e59f098",
			[66] = "bdb26c2d680b416a65bbcdbccc495f0",
			[68] = "6763e6d2b30f6929c4d4cb3fe063a47",
			[69] = "5b40ed33d02f5dc35137a11e753e982b",
			[70] = "30c34c8e71a415dbbb1f090970f44828",
			[71] = "13e6bbfca6921f5a4856c934c5d6e3c5",
			[72] = "3a52e576658553a76d00a53b2885f110",
			[74] = "3c65617b9a3fcb1c3f7296ea850efa7",
			[75] = "3258f9ab5593fd1b4744fd4f69064543",
			[76] = "5a5e5ce2d0fdef1db4a5b326f072b8ab",
			[77] = "3552f699dfaa4c271b0eeda26ed29d78",
			[78] = "58976ff498c84c33a271944f973ae216",
			[80] = "37916ba14ecc30360abe0cd6673e68bb",
			[81] = "9",
			[82] = "5a9d0c87e518c6cb13a811c0ebbca59c",
			[83] = "5ef2cb8d6cb52a4627e5c105c143c922",
			[84] = "45c2699480669d9504921e8034d19241",
			[85] = "4bba44a6872d99a6cd89235b86fd6796",
			[86] = "5632d46186d2d1e32be4c3055231edaf",
			[87] = "531f902b8fb73220ef988a46ca801f16",
			[88] = "6b43553150ab6ba58eff22c28be82eb0",
			[90] = "8344564f2307055cba6f63a4f756505",
			[91] = "76240705743b992bad77b0ecb5569616",
			[92] = "e5508354e8396e638f30d91ddc35e76",
			[93] = "154d0dac17c4a150f8fc0c8c1ae2021e",
			[94] = "6713d7b3635f41a9a0bee026abfdddb0",
			[95] = "64321552a81000b462142ecd1287bc36",
			[96] = "4c28f1cab2ea3b58a779bb7ad2c6a802",
			[98] = "440b610a211b8c435480c0c5049c43e8",
			[99] = "3b6b57e41ce79a1519a42b97e4840c53",
		},
	},
	{
		.p = "e74738af4ed4cdd4b646d73096a2ccc1",
		.first_non_square = 73,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "6ac4647c98cf7af7efe36371c9840eaf",
			[3] = "21f4d6a5034093c1707e59226887fd48",
			[4] = "2",
			[5] = "548a6d10aee79ac83c925efab62b6a1b",
			[6] = "67066dfb891738c460a2e75221d367c5",
			[7] = "1953c6191d849b93d1aab79fa4cda95",
			[8] = "11be6fb61d35d7e4d680104d039aaf63",
			[9] = "3",
			[10] = "180026ae5dbe974e7eb06b324927eaf0",
			[11] = "1f9a4dff72cd9019164b73b04505333c",
			[12] = "43e9ad4a06812782e0fcb244d10ffa90",
			[13] = "643262a816c65c4721ec5f69b3669844",
			[14] = "233636554b325c46cbad926a62d900d6",
			[15] = "2b886afec0a6d18648114e84a91330c4",
			[16] = "4",
			[17] = "25046e4d32a4c22cb9f8afba3f3ceffb",
			[18] = "5905f4c67b99a31319635324c5e95f4c",
			[19] = "369e8c7fda6cf7c0d54f91eff4932e14",
			[20] = "3e325e8df10598443d22193b2a4bf88b",
			[21] = "70a98d624f107a23b9291ed17c65e708",
			[22] = "48e20c07cccf03cc10d026cd8f1ef8d8",
			[23] = "39ac71be3dfc7f2469a7df772ca4a372",
			[24] = "193a5cb83ca65c4bf501088c52fbfd37",
			[25] = "5",
			[26] = "8c353e6ed18c48d8d54754f51d9522",
			[27] = "65de83ef09c1bb44517b0b673997f7d8",
			[28] = "32a78c323b093727a3556f3f499b52a",
			[29] = "4ec6fa6a98995394f71e0b2207f371a9",
			[30] = "13107997fc19873b1902a2a615da9376",
			[31] = "51dc60db382d37d1cd141b86b4154757",
			[32] = "237cdf6c3a6bafc9ad00209a07355ec6",
			[33] = "25640e49e12fb784a6ac33b6d27d6e77",
			[34] = "25f4ab6cd383cb4026bffd362b15d7f",
			[35] = "411f7f296f7fac0ef30960fd99f42d43",
			[36] = "6",
			[37] = "2feabe9655a9641a8fa1f87f22531049",
			[38] = "720bb70b21823e069d150e3e64d26b7b",
			[39] = "177beb203ec9b72d675ca269eab76fd5",
			[40] = "30004d5cbb7d2e9cfd60d664924fd5e0",
			[41] = "382967f0e5f5feb1984360723a0c4f82",
			[42] = "22468c3c9efc7faffb5d3e8dd251fc40",
			[43] = "2de84badd9bff0944b460f3b8d0458e",
			[44] = "3f349bfee59b20322c96e7608a0a6678",
			[45] = "16580e82bde20283ff7045bf8bdf7190",
			[46] = "456c8f05a1da32e653cc5b839fa9b9e3",
			[47] = "1092ae6dfcff9bf2b3292f357aca1c05",
			[48] = "5f73de1b41d27ecef44d72a6f482d7a1",
			[49] = "7",
			[50] = "474785105e63cb2e42e342d7c24eafe9",
			[51] = "4b16b51a17a20de39e3f2a36fc8c9abd",
			[52] = "1ee2735f21481546726e185d2fd59c39",
			[53] = "2be013c5a5285182d8f04af39b1d6fef",
			[54] = "4dcc11434c70dc786ba1dec5ced76a8e",
			[55] = "542fb8ff09b332e95786de3e3ec8a568",
			[56] = "466c6caa9664b88d975b24d4c5b201ac",
			[57] = "5a3d33318760312855953f24bffd9ced",
			[58] = "469159fcdc46912a4e3fcab24a5ef2ac",
			[59] = "25635a436530a1238a7f4b2f0025e32b",
			[60] = "5710d5fd814da30c90229d0952266188",
			[61] = "3deb808b5ad9f2cb409d268e094918af",
			[62] = "2dbc7a4f609cc9466560d9177a45bfbb",
			[63] = "4bfb524b588dd2bb750026deee68fbf",
			[64] = "8",
			[65] = "235c214fd9dc61623c9955f87db2b12",
			[66] = "463c486bc87316cf7424a483bf537d38",
			[67] = "41be254c0da85d290b883c6b08421178",
			[68] = "4a08dc9a6549845973f15f747e79dff6",
			[69] = "1632bc546d2fa40a63b66c9c0b10399",
			[70] = "1de0640a5afe477f5b35616ff7a4cf41",
			[71] = "52b9f4585f7d01646e479ec08cff289e",
			[72] = "353b4f2257a187ae838030e70ad00e29",
			[74] = "233e33da1bb170db78a61f5463dea41c",
			[75] = "3d7f07763e91eb0d83cf19848bfada59",
			[76] = "6d3d18ffb4d9ef81aa9f23dfe9265c28",
			[77] = "a8291ed9a50df270c807f51fd931ec",
			[78] = "10d7025482f44ccf3b15f625e68fa814",
			[80] = "6ae27b936cc99d4c3c02a4ba420adbab",
			[81] = "9",
			[82] = "10a7130308b2012359b349defd48bb7c",
			[84] = "5f41deab0b3d98d43f4998d9dd6feb1",
			[85] = "593575a6c6f3d3defc4139e083a23789",
			[86] = "14bb748ced6ace4ce9ba942b124e9978",
			[87] = "54085d04c4746201ca5df23ce9c207b8",
			[88] = "5583209fb536c63c94a689957864db11",
			[90] = "4800740b193bc5eb7c114196db77c0d0",
			[91] = "3ccce2be9baf4d2f5575efc5d8bcaf5d",
			[92] = "7358e37c7bf8fe48d34fbeee594946e4",
			[93] = "5f7b21557f767e363b43f867baf4921c",
			[94] = "25f9463099ba6747ef039591b8a24142",
			[95] = "618ad1c02988e0f3f4ecc2dff670aac2",
			[96] = "3274b970794cb897ea021118a5f7fa6e",
			[98] = "3589155a412df3496c63328abeb40086",
			[99] = "5ecee9fe5868b04b42e25b10cf0f99b4",
		},
	},
	{
		.p = "d3a1b64e241479381932df4745621009",
		.first_non_square = 53,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "3256df5d0e4054ee1c708a8c2a94ce21",
			[3] = "1aceb5e620540ea3c8e28ff09e65a03e",
			[4] = "2",
			[5] = "4a920722c3d286a0acf498eb88a17407",
			[6] = "59c0e022516ddfc6f750aed84fc5ae70",
			[7] = "123de903d6dc595ff3db324dee5f1a27",
			[8] = "64adbeba1c80a9dc38e1151855299c42",
			[9] = "3",
			[10] = "10fb193db4a2e4fb1183cb7aa4488898",
			[11] = "f4ee6591467dcf7223ee39a1828d8a6",
			[12] = "359d6bcc40a81d4791c51fe13ccb407c",
			[13] = "3259920bf856147b6d524ea42d74cd0d",
			[14] = "5a72ba66b8b0b50d7dfc78997218456f",
			[15] = "41028b2be43eda537b3de57589b190bf",
			[16] = "4",
			[17] = "62de61e0af734f1d89f10bc4ac9ae273",
			[18] = "3c9d1836f9537a6dc3e13fa2c5a3a5a6",
			[19] = "4eacea5adabc97af905b65f1f32a67d6",
			[20] = "3e7da8089c6f6bf6bf49ad70341f27fb",
			[21] = "665de344f793cfcf0d8851eeaa979209",
			[22] = "4ab0fe70c9af2e359407cfb7a9284d2e",
			[23] = "55a3bad41617c6c316d63b172d065de3",
			[24] = "201ff6098138b9aa2a918196a5d6b329",
			[25] = "5",
			[26] = "13519dc6a212b6970dbdfa5c48317dde",
			[27] = "506c21b260fc2beb5aa7afd1db30e0ba",
			[28] = "247bd207adb8b2bfe7b6649bdcbe344e",
			[29] = "a108bd629d894d7d4ae6b94fc533244",
			[30] = "25de096124247dca639e5b4bdb451969",
			[31] = "3d7f485499372a399679f8a3ad2cb393",
			[32] = "a4638d9eb13257fa770b5169b0ed785",
			[33] = "63142ce93321793c61bc51c04ef70535",
			[34] = "ddef86ef8e2539ebeca14b526f44b5",
			[35] = "4ce13194f399e69fa318bb4bb8f7ea7b",
			[36] = "6",
			[37] = "9228193d6c0e38073c07cdf448de5ee",
			[38] = "11c081d52a1f27b9dafd424384c59554",
			[39] = "5006ac3ae7fc06103b81937bc75a2b33",
			[40] = "21f6327b6945c9f6230796f548911130",
			[41] = "3f455b8cd2f901c587b0598a682b583f",
			[42] = "1df60d29d9e20a850f71ff40123437c1",
			[43] = "2382b8ce194e6c36c92a0b3faadaaa52",
			[44] = "1e9dccb228cfb9ee447dc7343051b14c",
			[45] = "c145f1a27631aa9edaaeb7b54824c0c",
			[46] = "671467f745d92daf13ebb67ecf24fca3",
			[47] = "264506f56045c3502ef62170be528551",
			[48] = "6866deb5a2c43ea8f5a89f84cbcb8f11",
			[49] = "7",
			[50] = "2810a683232d2f6e74ffd5758f85f69c",
			[51] = "1ebedbfc86ece8fe5b90062a149969f8",
			[52] = "64b32417f0ac28f6daa49d485ae99a1a",
			[54] = "39a0ea18d035261cccbf2d41a9eefb47",
			[55] = "694639a2f3d08884ecb2affda5ee61e3",
			[56] = "1ebc4180b2b30f1d1d39ee146131852b",
			[57] = "5d2cec4ee0c2ce79bf81a15df81ceb4f",
			[58] = "2ce9fcc748ecf7848e9dcf2211e67206",
			[60] = "519c9ff65b96c49122b7145c31feee8b",
			[62] = "12685eaa555f8cb49a90178a0a6015fc",
			[63] = "36b9bb0b84950c1fdb9196e9cb1d4e75",
			[64] = "8",
			[65] = "4de278e7a145b79c2419db6e07e791f",
			[66] = "379ddde1c4ac6a9e5ca97ef7aba7a6c",
			[67] = "37c6a10abcd0fe2aa60f330f989550ca",
			[68] = "de4f28cc52ddafd0550c7bdec2c4b23",
			[69] = "2769cef201b1aac7821d582d2d2e8780",
			[70] = "639e469a7c754f5cd697dab32f90d07",
			[71] = "2c68191bf87dfe5cef79f5f62b726623",
			[72] = "5a6785e0316d845c91706001ba1ac4bd",
			[73] = "3a6fe5f9d81eb2fe4353d17adde7346f",
			[74] = "1576149deacfc14d26864a3cc3e17600",
			[75] = "4d9828cf827030052cc60f942d65eed3",
			[76] = "3647e1986e9b49d8f87c13635f0d405d",
			[77] = "630c6e7117790a971af1517d57363f28",
			[78] = "6856e68603816f2c27fdc4c7d5016934",
			[80] = "56a6663ceb35a14a9a9f8466dd23c013",
			[81] = "9",
			[82] = "59830bfb04b7677115c088282226682f",
			[84] = "6e5efc434ecd999fe223b69f032ebf7",
			[85] = "59925907902ef6ef57cd520278a260c5",
			[86] = "632a92f87db0fc76ab82a2d352768c19",
			[87] = "16d7959610ea9badc85fb36603a94879",
			[88] = "3e3fb96c90b61cccf1233fd7f31175ad",
			[89] = "13088ca71d4342c41581472612636828",
			[90] = "32f14bb91de8aef1348b626fecd999c8",
			[91] = "5c0a55b7b9d58943b809015a4aa020ff",
			[92] = "285a40a5f7e4ebb1eb866918eb555443",
			[93] = "1592a85e0341d9c9825096e1976e192b",
			[94] = "2fb36c59090a20bbaa092c8aa8992ac2",
			[95] = "2e74a563b5f9c6a6b01d52288f9c0d6b",
			[96] = "403fec13027173545523032d4bad6652",
			[98] = "46e35110e4669fed6b51f4b960b27d2b",
			[99] = "2decb30b3d3796e566bcaace487a89f2",
		},
	},
	{
		.p = "d7da4ea4510511593950179f8f711349",
		.first_non_square = 59,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "959d1dbbc442f0c03fab00d7b74bc39",
			[3] = "54011c029cc999cc59490b7ee97eeab5",
			[4] = "2",
			[5] = "27a5f203b507150a1470c9475067a7a9",
			[6] = "5b106a2f61672bff1fc84537031bb8bf",
			[7] = "c1c26a4d8728d54300851fec2a6c397",
			[8] = "12b3a3b778885e1807f5601af6e97872",
			[9] = "3",
			[10] = "5454e217bab81e821a0b173196678210",
			[11] = "1682f9cf7225b6fd06af37171fe3fde1",
			[12] = "2fd8169f1771ddc086be00a1bc733ddf",
			[13] = "217f7c4618539196631e3abcf5fe454d",
			[14] = "46176a55e55c6e8920b06a348e37c612",
			[15] = "43fc3e63a8bbe3e9e702986f4f1ed133",
			[16] = "4",
			[17] = "19453763a8648666e6d09e4de90b0333",
			[18] = "1c0d759334cc8d240bf01028725e34ab",
			[19] = "119bcbb7bff3e1badada7f4d83e36e67",
			[20] = "4f4be4076a0e2a1428e1928ea0cf4f52",
			[21] = "1d5498842ef5c553b8ef2d356bcc6eab",
			[22] = "15b78a01b3f6097e3cf9b453f141e9ed",
			[23] = "980e6f8411b4639e84cd6c712a9cba1",
			[24] = "21b97a458e36b95af9bf8d318939a1cb",
			[25] = "5",
			[26] = "20dcca93e1306a7f700b99170fbce6c7",
			[27] = "242905638557bc0bd28b0add2d0bacd6",
			[28] = "18384d49b0e51aa86010a3fd854d872e",
			[29] = "5a6e09373884c6bfa72122a486a177b3",
			[30] = "12e5df3ef102359d598724a0811ba203",
			[31] = "5c85b8dbe7a3ff15fcc9eb57067b82bc",
			[32] = "2567476ef110bc300feac035edd2f0e4",
			[33] = "6b8e76260fca9eefcef94f3f2d12200f",
			[34] = "3b36f3d67d8463ece78ac1e3396d148e",
			[35] = "394b72ee77c8734547637275946556da",
			[36] = "6",
			[37] = "4b64318604f4ae445368e8876704e136",
			[38] = "c3a81ce44aaa2db666a3d16b0201d09",
			[39] = "1e45b92f0be82f1042ad3fe274ff0c88",
			[40] = "2f308a74db94d4550539e93c62a20f29",
			[41] = "3e9c285ed90c12fe79cf4ba4128f6185",
			[42] = "2025ee1dd9e72c7313b99e9f1c90d856",
			[43] = "3a3dbbf0e96a1a3ce316b144b7826682",
			[44] = "2d05f39ee44b6dfa0d5e6e2e3fc7fbc2",
			[45] = "60e8789931efd23afbfdbbc99e3a1c4e",
			[46] = "fee50de01be07f792ac6f5b9b5de4b7",
			[47] = "8b0f0ef7048f8d3e4c9ba6d4ef80f6f",
			[48] = "5fb02d3e2ee3bb810d7c014378e67bbe",
			[49] = "7",
			[50] = "2ec1194aad54eb3c13e570436947ad1d",
			[51] = "17e29b58202c9cefddc75b5fafc7e372",
			[52] = "42fef88c30a7232cc63c7579ebfc8a9a",
			[53] = "3a0c58a5ec20f119c262f12ae5183157",
			[54] = "3956efe9d33072a42608b80579e216f4",
			[55] = "2be21c25e83850a25db71abbc4a48100",
			[56] = "4bab79f8864c3446f7ef433673018725",
			[57] = "5386749f14c9429c39b8d9d424f075b5",
			[58] = "360395662a2d98180968961c84e61588",
			[60] = "4fe1d1dcff8d49856b4ae6c0f13370e3",
			[61] = "4238404aca0d8514e461aa1b212a5af6",
			[62] = "629800208ccbf1d82b7b1b43ec19118c",
			[63] = "245473ee8957a7fc9018f5fc47f44ac5",
			[64] = "8",
			[65] = "6b2bef42c7770064af9022b31d103d5c",
			[66] = "20454e81a6a03a241dbc136180e8c664",
			[68] = "328a6ec750c90ccdcda13c9bd2160666",
			[69] = "160e654845d6518f69ca871d43a32123",
			[70] = "1bb310d630d1bc146d36acc51d7abf11",
			[72] = "381aeb2669991a4817e02050e4bc6956",
			[73] = "472e264b5e0edc8ec4a88d77a050dc04",
			[74] = "49a4a35f203f4e4d29034e654752f75",
			[75] = "baf113b921a21b4b432f5c48f679109",
			[76] = "2337976f7fe7c375b5b4fe9b07c6dcce",
			[77] = "28caa6a79c150442aa8ad26a6a293d6b",
			[78] = "1ea04d76bcb64865c84cbabc0eef714b",
			[80] = "394286957ce8bd30e78cf2824dd274a5",
			[81] = "9",
			[82] = "f4596207cde9bd65f928c1c6ad96fe",
			[83] = "56f024a084a7418b7fbb56a3398cdf40",
			[84] = "3aa931085deb8aa771de5a6ad798dd56",
			[85] = "5f4260a2d155f008378db93f90204793",
			[86] = "140253b4201d75301a986809baed51d8",
			[87] = "f5d00583106340970471bc36ca2d0d7",
			[88] = "2b6f140367ec12fc79f368a7e283d3da",
			[89] = "6adb209ecb5bcd002bef5b6bee7f253a",
			[90] = "252457a2df234a2d14d12df533c572e7",
			[91] = "1c21f090707344b4d211c183e4e5bd60",
			[92] = "1301cdf082368c73d099ad8e25539742",
			[93] = "4c8047a783cda4682655dc2f757b35bb",
			[94] = "6eaa3447c9e7a4f0b0afe99836c88ee",
			[95] = "69c7233d4237d40d65ac836ac27e305a",
			[96] = "4372f48b1c6d72b5f37f1a6312734396",
			[98] = "4174bd0225dd49541bdad05e6031258f",
			[99] = "4388ed6e567124f7140da5455fabf9a3",
		},
	},
	{
		.p = "e4ad8bf1a7b946d591595a7347b15f71",
		.first_non_square = 53,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "4c49d878822cfbe76fb4b402caaed4fd",
			[3] = "3c37757e7487b01dfbc37216d844db99",
			[4] = "2",
			[5] = "5aeece5c7ed9ca8c1ed52da45babc777",
			[6] = "40713762535e9a0c57f602473ff52998",
			[7] = "3ecda635990f5dea2acb9b1a9fdefbf5",
			[8] = "4c19db00a35f4f06b1eff26db253b577",
			[9] = "3",
			[10] = "91ce7bde461ea19843dc9847de90e4",
			[11] = "125b241be577156bc1797be7208ee62b",
			[12] = "6c3ea0f4bea9e69999d276459727a83f",
			[13] = "425700a36490fb859641b2b08a7eae0a",
			[14] = "10f1f9d70043a045d55a6d116c2a6f31",
			[15] = "469981b6614fca719735f3d6d9dcb656",
			[16] = "4",
			[17] = "72796d6db8598786ae34c9df885233b",
			[18] = "2ffd77decdace0bdc4c195185b1f86",
			[19] = "1dcf29a92bb5b16be3ff967d956e78c1",
			[20] = "2ecfef38aa05b1bd53aeff2a9059d083",
			[21] = "417e0eec0ee9eab8893e2c6432f1410f",
			[22] = "65551d126459325ddff4e27e0e154850",
			[23] = "3394ef48c3876261e519f07c5cc22838",
			[24] = "63cb1d2d00fc12bce16d55e4c7c70c41",
			[25] = "5",
			[26] = "10772742d4d9fbe450d15ef5ffc3fe71",
			[27] = "30072b764a22367b9e0f042ebee2cca6",
			[28] = "67123f86759a8b013bc2243e07f36787",
			[29] = "5cc3d6327e652a41154770644a38b969",
			[30] = "4ad60c254ddec972fae777b256a7c5d5",
			[31] = "2e00a145ed5a04009a8355499ce70919",
			[32] = "4c79d5f060faa8c82d797597e309f483",
			[33] = "5cf3346d66af0589ba6cc2ca64333c79",
			[34] = "4a3aaa0b4b8c6ede15bc17190bdada64",
			[35] = "71363966ddc55278fda04d8b9c1ae94b",
			[36] = "6",
			[37] = "1b3a4a911ed3a3c94924fa36bb0b2ee9",
			[38] = "28e73aa747bd0f9c9a9bc913df016115",
			[39] = "6f8a1ed7db16005ca5df3eddb25eacbb",
			[40] = "1239cf7bc8c3d433087b9308fbd21c8",
			[41] = "6a7533c0bc1acd73a3b48be478dd13a2",
			[42] = "9e55812741e9524ca166ce7bff9be9e",
			[43] = "6244c591bd3277fca7ec50e93b69221c",
			[44] = "24b64837caee2ad782f2f7ce411dcc56",
			[45] = "2c1edf23d4d418cecb262e79cb51f6f4",
			[46] = "e3c5e20bd259295570a71eec9ecd768",
			[47] = "4cf99eaa49470e4938c90a22bc4ccb88",
			[48] = "c304a082a6579a25db46de819620ef3",
			[49] = "7",
			[50] = "4be9dd88c491a225f42b30d899f895f1",
			[51] = "3467e2afaf03823c26a922d43928cc08",
			[52] = "5fff8aaade974fca64d5f51232b4035d",
			[54] = "2359e5caad9d78b08977539d87d1e2a9",
			[55] = "bd103977e456b841e966a74aad0e057",
			[56] = "21e3f3ae0087408baab4da22d854de62",
			[57] = "2c4fbdbb74128aab91bd39e1913a3f27",
			[58] = "3371407d6491e3ec1977f1b775823de2",
			[60] = "577a8884e519b1f262ed72c593f7f2c5",
			[61] = "3ee45bf8835e5badafeb6df43185f5bc",
			[62] = "56cb5291a6e682ffcaf3ff8b4150c72e",
			[63] = "28449950dc8b2d1710f6892368146b92",
			[64] = "8",
			[65] = "143912b856d247f8c39d24c1c062491",
			[66] = "3c15ad8b379ebaff4763bab01a9bcda9",
			[68] = "e4f2dadb70b30f0d5c6993bf10a4676",
			[69] = "635cd56cb6cb00cf27fa36b7aedfbc53",
			[70] = "43c04f20e4e9ac170210fc23ab9c2dc2",
			[72] = "5ffaefbd9b59c17b89832a30b63f0c",
			[74] = "bfdaba577a3472dd49bfd77fd19918f",
			[75] = "4867bf869eed29c05977dffef1a6ea8c",
			[76] = "3b9e5352576b62d7c7ff2cfb2adcf182",
			[77] = "2006ed422b0a69dc4d57d750f38d34ab",
			[78] = "4b050d62afd87d0f33db026adbb1c115",
			[80] = "5d9fde71540b637aa75dfe5520b3a106",
			[81] = "9",
			[82] = "b0dc8230270044d0edf3ab1ad97b16d",
			[84] = "61b16e1989e571647edd01aae1cedd53",
			[85] = "46e0143233ee83c76c0b13168f19a07a",
			[86] = "52a1b3caadecf3720ef1d80d467e74bc",
			[87] = "14589812286e6d6f123be7173952a95c",
			[88] = "1a0351ccdf06e219d16f95772b86ced1",
			[89] = "539cf0fd123cf0a349f1fa6ffdfc4a68",
			[90] = "1b56b739ad25be4c8cb95c8d79bb2ac",
			[91] = "5662acc7560be428cb14e9cc5a4e2128",
			[92] = "6729de91870ec4c3ca33e0f8b9845070",
			[93] = "5cb5d378c21458c2912dd06deb1a7735",
			[94] = "552aec748d185891ca356d1379aed772",
			[95] = "4fc9e390534febdb6c6ec3fea343b79c",
			[96] = "1d175197a5c1215bce7eaea9b82346ef",
			[98] = "4ca9d3683fc855a8eb3e372cfb651409",
			[99] = "37116c53b0654043446c73b561acb281",
		},
	},
	{
		.p = "cbcabb1f226a0c65460ae623608b1ad9",
		.first_non_square = 59,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "20aa56375597d8cdab6a03b709d4a278",
			[3] = "49d44e3b109e23292d3d13445b9b8eea",
			[4] = "2",
			[5] = "1e0e210eb335cae047c35d767f674637",
			[6] = "625c5c0f38eca85e2ecb80174fa4a95e",
			[7] = "3745673b54e841a6a4fb1a8a3759b8bf",
			[8] = "4154ac6eab2fb19b56d4076e13a944f0",
			[9] = "3",
			[10] = "4f0396e7b9df52bac992b43575c33c35",
			[11] = "4d679981c0acbe4a8e305ca2cc963ef9",
			[12] = "38221ea9012dc612eb90bf9aa953fd05",
			[13] = "6596f0c8f4a91c4a17e751f02c81a82f",
			[14] = "20d6292a2fdef5fadd66420ecd85cbd3",
			[15] = "266647395842faa57f249c38db0f1eb1",
			[16] = "4",
			[17] = "3cba2f5354d5ba2192012e780019d728",
			[18] = "61ff02a600c78a69023e0b251d7de768",
			[19] = "577f304041afec01a5c075ab05175217",
			[20] = "3c1c421d666b95c08f86baecfece8c6e",
			[21] = "28429bbbd4c6513ad4f08b5ea351f8a",
			[22] = "29cf35616a805dd926e7cdcbe6d63680",
			[23] = "55d452f7467ccbaa62afc2985787cf17",
			[24] = "7120300b090bba8e873e5f4c141c81d",
			[25] = "5",
			[26] = "29e4c4e321eced80ec26e6133bf20af6",
			[27] = "11b22f920f705d1641ac53a9b24791e5",
			[28] = "5d3feca878998917fc14b10ef1d7a95b",
			[29] = "5b4b3eed097bd01db9cd0c7a288b56c3",
			[30] = "3575cde15467aaaae6e0187e24d6d2ee",
			[31] = "5840337f0dc69197ea45a3a340700304",
			[32] = "49216241cc0aa92e9862d747393890f9",
			[33] = "54002bd7d536884838b4eeda47283774",
			[34] = "4951e9a1bd020bb682f868df70b4bfbc",
			[35] = "376b66fcc1a223932d2b34a06781ff17",
			[36] = "6",
			[37] = "3f93f4d11e213ad8cdab598763039e7b",
			[38] = "324d3ab2b47ec5393ef6070e5374bb34",
			[39] = "261e82ec98f3c07d5dc28a1484db4146",
			[40] = "2dc38d4faeab66efb2e57db87504a26f",
			[41] = "1976a2ea6012d0647da3523808d36740",
			[42] = "42de54dbb74f15f8e3186afc44a48501",
			[43] = "4f751abc9fe3a2f84f1ccb3fd63bd3f6",
			[44] = "30fb881ba1108fd029aa2cddc75e9ce7",
			[45] = "5a2a632c19a160a0d74a18637e35d2a5",
			[46] = "494db3f0b07d25c8a30faeec07f9b5e8",
			[47] = "182d2c235507872a1e99c776b83ccfbe",
			[48] = "5b867dcd200e803f6ee966ee0de320cf",
			[49] = "7",
			[50] = "28770c0a7672d060ecf8d3902f63ee81",
			[51] = "d05c776625315a66187a12849c70f79",
			[52] = "9cd98d3917d3d1163c42430787ca7b",
			[53] = "286f9d986059b7f9938b43ead8c8d654",
			[54] = "5b4a590e885becb546579a228e62e141",
			[55] = "51aa04d6a5d11b6edc1a6fe2233eee6b",
			[56] = "41ac52545fbdebf5bacc841d9b0b97a6",
			[57] = "5aa5e71f1dfc8b3cc0e026612f7e1fb6",
			[58] = "35533f553747e464072c4440bac52a0e",
			[60] = "4ccc8e72b085f54afe493871b61e3d62",
			[61] = "24551bf46096d057148ef425467a9ba9",
			[62] = "3e52033154f120b235978178d647c143",
			[63] = "25fa856d23b1477157199684ba7df09c",
			[64] = "8",
			[65] = "563a6e8eb33b076dbe065396eb315574",
			[66] = "5e864cca82f747ea276994780ac8a19d",
			[68] = "52565c7878be98222208893360576c89",
			[69] = "5ce922cd0c1b446c1e4ca6c4e0f24df8",
			[70] = "11f934d12f2ffcc38b0a891a84025b50",
			[72] = "7ccb5d320daf793418ecfd9258f4c09",
			[73] = "52a7a870244bba2a55519bf744ebed05",
			[74] = "ad23956ae428025f0a0257bf2dda72f",
			[75] = "266fef16f1bd68fca9e46bf0f70c6b20",
			[76] = "1ccc5a9e9f0a3461fa89facd565c76ab",
			[77] = "14072fe6875f3477f254a09d386e4654",
			[78] = "38f4c4a6b86698a0480425b350bdbebe",
			[80] = "539236e45592e0e426fd704962ee01fd",
			[81] = "9",
			[82] = "3f7b778244278ec4835d885d4839b2b4",
			[84] = "50853777a98ca275a9e116bd46a3f14",
			[85] = "22d770096f8b019cc1dbe271c8d798b8",
			[86] = "50718b1afe946781b22c7ce102fd4243",
			[87] = "50259cceac8c1f8b42544d960fee46fb",
			[88] = "539e6ac2d500bbb24dcf9b97cdac6d00",
			[90] = "214009980b33ebcb16ad367d00be99c6",
			[91] = "2df48de5831087e5fa6d5f4b545beb59",
			[92] = "202215309570751080ab60f2b17b7cab",
			[93] = "384da52f8b0dd624357002ef2753ab23",
			[94] = "2d9c483316aab53053292ffc1364a199",
			[95] = "17d3a3a478758dcb2d715b6a6c291770",
			[96] = "e24060161217751d0e7cbe98283903a",
			[98] = "18dda06434bce13a69db33dde445566f",
			[99] = "1c6c11661f9c2e7a64862fc50537a212",
		},
	},
	{
		.p = "fe4140ab1703f4636f45c48bfb0068a1",
		.first_non_square = 53,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "7ba6780b0fed766515a7f52780bc9293",
			[3] = "6149de094ae83131a239de5a6df50f3d",
			[4] = "2",
			[5] = "8e08f47c68cea90d366ed88b052dc0d",
			[6] = "712eb140f38d2eda5c637e54d10bef43",
			[7] = "647ecb433b6c7bac3b1d2f9d6054d0ba",
			[8] = "6f45094f729079943f5da3cf987437b",
			[9] = "3",
			[10] = "329e80ca3b64fa4f1155f9c270b8f4ce",
			[11] = "24c5b58cbd45b14e27fb39d98513e861",
			[12] = "3bad8498813392002ad207d71f164a27",
			[13] = "30b405638cc9503430b6aa41495bb61",
			[14] = "2f1bc9fc42ecee2906bb9cb2c4b48844",
			[15] = "1060959a729316d3567679f61abc49af",
			[16] = "4",
			[17] = "2743cff56b8c8f8ee97f7446ae99739e",
			[18] = "74b2277618c46ecbd1b21aea87354f18",
			[19] = "3fbbfcdfd2487c9b851129505f179076",
			[20] = "11c11e8f8d19d521a6cddb1160a5b81a",
			[21] = "6fc009527f629afe432b1f4f1e4b1fea",
			[22] = "3f01ff2d6c6d833d28056e6072b25e60",
			[23] = "506f5b0f98fa595bed0cbc10ab7c5ee5",
			[24] = "1be3de292fe996aeb67ec7e258e88a1b",
			[25] = "5",
			[26] = "10be4164d8254ac55662322c7ddcf6fe",
			[27] = "259c5970c9b49f317767d6834edec516",
			[28] = "3543aa24a02afd0af90b65513a56c72d",
			[29] = "5e4f3f1a728ca81e0562ea16d3b3cffd",
			[30] = "5f7adf9503c5bf6b2ed871ee578d7f7f",
			[31] = "5a732eeb6c03d753dce4cbca35427431",
			[32] = "de8a129ee520f3287ebb479f30e86f6",
			[33] = "515718e3cde407eb2f1f134140cb21fe",
			[34] = "58f96aeb670e2022511ae47d9ecba760",
			[35] = "7b8229e2856880ab02cd93d5da144228",
			[36] = "6",
			[37] = "6b81b8e27b6cf32c0457ca6fdadea755",
			[38] = "b8e8b04539168fe235d3def1490dad8",
			[39] = "15bf17346ca8088e3cce95dfb9c418f7",
			[40] = "653d019476c9f49e22abf384e171e99c",
			[41] = "70c50f028034d998186c4fa9b24ec776",
			[42] = "4025367c12a3595db005c840e28fca2f",
			[43] = "10bd851db1dbc8acea1e2f5c9a539027",
			[44] = "498b6b197a8b629c4ff673b30a27d0c2",
			[45] = "1aa1add753a6bfb27a34c89a10f89427",
			[46] = "2f0c4d81e381d9b019bf8094b3eecee6",
			[47] = "15534147c6ebf56b2ecc5c10e0770e14",
			[48] = "775b09310267240055a40fae3e2c944e",
			[49] = "7",
			[50] = "6dbdd6e1219b67328dbc40ad8dae0b9d",
			[51] = "70e70926d41fd03e102a5a44ce1229f9",
			[52] = "61680ac71992a068616d548292b76c2",
			[54] = "554ad317c3a3982ba5e4b67278236528",
			[55] = "1459f9f6ec22944520f4871412088d85",
			[56] = "5e3793f885d9dc520d77396589691088",
			[57] = "341ef10187760d3c36d527145b4ad817",
			[58] = "529922ce1cc858cb8a2b30f4684aa9fd",
			[59] = "37be2dfe9028978862327c01c89586e2",
			[60] = "20c12b34e5262da6acecf3ec3578935e",
			[62] = "6ecd16e975917832f0959b6c9542da42",
			[63] = "2f3b211e9b417ea14211ca4c25fe098d",
			[64] = "8",
			[65] = "7345a2c482c5a9588f299989ac40ecad",
			[66] = "18770759a8b2f26d1a24dba6c2d52a07",
			[68] = "4e879fead7191f1dd2fee88d5d32e73c",
			[69] = "3a0988799c07916f61906d56b5afe5ec",
			[70] = "1d94b3814a59d9cd691f356c42e05881",
			[72] = "14dcf1bee57b16cbcbe18eb6ec95ca71",
			[74] = "1d13c613cdf866cb5b13e4a51dc7ebe3",
			[75] = "16112b27b77ef2ceb36a3153d0378511",
			[76] = "7ec946eb7272fb2c652371eb3cd147b5",
			[77] = "3ea093d7619691c048b296bdc0d37662",
			[78] = "3af49044c061c8aad58fc0604c081c2c",
			[80] = "23823d1f1a33aa434d9bb622c14b7034",
			[81] = "9",
			[82] = "21662760541dd24c3115bfa4f0b2f76d",
			[84] = "1ec12e06183ebe66e8ef85edbe6a28cd",
			[85] = "34b4035b5edce75b16326453cc7b7c59",
			[86] = "67f099b3ac6c1e2285f800c1169f3e74",
			[87] = "28d2792fa43528197df02a2ab64f10b4",
			[88] = "7e03fe5ad8db067a500adcc0e564bcc0",
			[89] = "131cea5295f0f890d408cd55292baa9a",
			[90] = "6665be4c64d505763b43d744a8d58a37",
			[91] = "6166d2bbeaabb0fc8f2a7f54155dba4c",
			[92] = "5d628a8be50f41ab952c4c6aa407aad7",
			[93] = "5d9415660fcce03cc4807ceb85b729c0",
			[94] = "63b43ba1ab89508e123048325aa02be5",
			[95] = "453ec81c6776384522e1ecda1b6ddee5",
			[96] = "37c7bc525fd32d5d6cfd8fc4b1d11436",
			[98] = "66c9864c2a725f9949c666709426c822",
			[99] = "6e5120a637d113ea77f1ad8c8f3bb923",
		},
	},
	{
		.p = "c952099317697fbee819206f51261769",
		.first_non_square = 61,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "1030ca5f555429e4df1a0364d52ed574",
			[3] = "1c008a94d898f5934cc6768dc4aa9e7",
			[4] = "2",
			[5] = "6453a0cada2df423a5cadbfdb1f7a43a",
			[6] = "4ead969961f9970cc0957f4c1997a2eb",
			[7] = "9516aa771b15128eb5879ba5cbbcb6c",
			[8] = "206194beaaa853c9be3406c9aa5daae8",
			[9] = "3",
			[10] = "218b5770aff5df5ee5dea41f84d956f8",
			[11] = "2e5ad00f8babb3bf597470ccdefa6904",
			[12] = "38011529b131eb26998ced1b89553ce",
			[13] = "5b7093eeeaabe2273d89ed1269e95a8d",
			[14] = "150e9ff34aebd4279f3f194d0b008b9a",
			[15] = "52f554395146f4e92284fd0195f3da81",
			[16] = "4",
			[17] = "e87f72fca45eea33de5b19bdc8598a6",
			[18] = "30925f1dfffc7dae9d4e0a2e7f8c805c",
			[19] = "dbbf4eb69d3d3d80113b9c179dbdf14",
			[20] = "aac7fd630d97779c836873ed36cef5",
			[21] = "343a4efea6c533ef26fbc9c0455bba4b",
			[22] = "561d91e30353cd92dad551df29b34330",
			[23] = "1c46770900025779e503a37fbf3652f",
			[24] = "2bf6dc60537651a566ee21d71df6d193",
			[25] = "5",
			[26] = "d8061aede379d718c4477b0c72eeb0b",
			[27] = "54019fbe89cae0b9e65363a94dffdb5",
			[28] = "12a2d54ee362a251d6b0f374b97796d8",
			[29] = "1a00dd8133c4574015541507f34fa2a2",
			[30] = "354823c12c93d938a6150ca543c49001",
			[31] = "2c50d713847df4e0e6c8092bbb7ff748",
			[32] = "40c3297d5550a7937c680d9354bb55d0",
			[33] = "1dadd4e7aed55d1dee86fa9fbec2b5f9",
			[34] = "495c10a814ec41ca882fd0a3db20dae0",
			[35] = "e02ae2c2ae827bcbcb8d5cee71ce245",
			[36] = "6",
			[37] = "48666c8d4ae91cdb3baa3faaf209f863",
			[38] = "1348689ee8394ec3c373f58427071294",
			[39] = "22ebc3a88ac979c4cb232cb96ef56c3d",
			[40] = "4316aee15febbebdcbbd483f09b2adf0",
			[41] = "5d6e17005c737be1d08b4c361e7c38aa",
			[42] = "63bdca4c2bc7704338cb6b9040f977bc",
			[43] = "bbcd28418772487d60fa0e99ee30f4e",
			[44] = "5cb5a01f1757677eb2e8e199bdf4d208",
			[45] = "63a8d8cd77205cac09477389c4c0d545",
			[46] = "27626a82bfa70599fc17246961bdadd0",
			[47] = "e64fa81b1411430a01be20a26516d3",
			[48] = "70022a536263d64d3319da3712aa79c",
			[49] = "7",
			[50] = "50f3f3dcaaa4d1785b8210f829ea2b44",
			[51] = "4573f3634cf8d8277095b591bba6eb32",
			[52] = "1270e1b54211bb706d05464a7d53624f",
			[53] = "4b910f3075773d693fc6320490320417",
			[54] = "22b6ba390e83456759a75d74fba0d158",
			[55] = "43d64c3a87c506ed9f669fb02a34490f",
			[56] = "2a1d3fe695d7a84f3e7e329a16011734",
			[57] = "f9eab4f7d57e3626e924cb0a2ed38d1",
			[58] = "2c268106581c388b69a240d04f41ddb3",
			[59] = "5468c6518f3cc44a8b95e3bebafe1160",
			[60] = "2367612074db95eca30f266c253e6267",
			[62] = "45bc5f2b116bb2147e21226906d83f5e",
			[63] = "1bf43ff65513f37ac2096d2f16336244",
			[64] = "8",
			[65] = "3a05b5275ba2635e914620a14673f977",
			[66] = "1a17a7a83581669f72accc1078c3b313",
			[68] = "1d0fee5f948bdd467bcb6337b90b314c",
			[69] = "212575df2c4a30476197bbdee2f94cc7",
			[70] = "a3f6dea37e9f80dc82f120598fd4bf",
			[71] = "f488a226be605e99163987425d9d390",
			[72] = "6124be3bfff8fb5d3a9c145cff1900b8",
			[74] = "13d134042559929d1edc56f26b66f3b",
			[75] = "8c02b4e83afccbe07fe050c4d755183",
			[76] = "1b77e9d6d3a7a7b002277382f3b7be28",
			[77] = "173a062e52c9d7f72af37a962c86bc64",
			[78] = "564a72f1159c013f5c9bad4a81a1e2e2",
			[79] = "4398a271233b37edbd0ff6c5bc3dba1",
			[80] = "1558ffac61b2eef3906d0e7da6d9dea",
			[81] = "9",
			[82] = "360ac3f246c2a2011620260c69e306ee",
			[84] = "60dd6b95c9df17e09a218ceec66ea2d3",
			[85] = "1042e7ab08c6e4551896de0fc918b9a7",
			[86] = "bf0a7212c4ed469c981a78d7c6c7e86",
			[87] = "4e6d3d71bd2dca40b63270d75cb33802",
			[88] = "1d16e5cd10c1e499326e7cb0fdbf9109",
			[90] = "64a206520fe19e1cb19bec5e8e8c04e8",
			[91] = "53cbd73f05e4d2cdd9ea452367ba48a3",
			[92] = "388cee120004aef3ca0746ff7e6ca5e",
			[93] = "5068af97e0ba758c38264b3fb6aaba9c",
			[94] = "3d7f4045e3fa05122e1616448e2b059f",
			[95] = "2b0448ad5e65cf7b24cc311cd17c2e1e",
			[96] = "57edb8c0a6eca34acddc43ae3beda326",
			[97] = "49893d9e5a5a5c7bb1c62e154d45677a",
			[98] = "57fc80f7c21c5a7cce6308ad7cde413d",
			[99] = "3e41996474666480dbbbce08b436dc5d",
		},
	},
	{
		.p = "c4ed76cc85621982d0467c08b185a479",
		.first_non_square = 53,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "3f8cc442a5387175ec9f6de0eb3fcfe6",
			[3] = "5904ffdcb4ae08d74f449e78de9eab94",
			[4] = "2",
			[5] = "259e201e5d0138c02f3566bfd710b661",
			[6] = "32b732707de2692db32c7ea095415db8",
			[7] = "65daac5f051d65191914b053e23ef41",
			[8] = "45d3ee473af13696f707a046db0604ad",
			[9] = "3",
			[10] = "40b664afaf338d62876013bbf2850e05",
			[11] = "4939a174028adff8511dd38caf5f62fa",
			[12] = "12e377131c0607d431bd3f16f4484d51",
			[13] = "4ed277339c1cddb3c56b926ece26e9b3",
			[14] = "52db8f0f6f42d7552ccd23ca9d43c858",
			[15] = "5da61b91394552f37eea84e6e9b9e447",
			[16] = "4",
			[17] = "3263fc1c526565e2ffed7234ab9ede85",
			[18] = "6472a0495b8c5210a683265efc634c7",
			[19] = "20ffb9b29d785211f5fb2761e31d1051",
			[20] = "4b3c403cba0271805e6acd7fae216cc2",
			[21] = "4e785976b5b051b1b2e78ffcc9067f4e",
			[22] = "2e284efd9784af8211500f8c36f5a70e",
			[23] = "594077cd6306cf9a454adc544da4d3d6",
			[24] = "5f7f11eb899d472769ed7ec78702e909",
			[25] = "5",
			[26] = "2e2ba3ff7ddf95e9b6def6042acaea23",
			[27] = "462188c998a801031d875f61ea565e43",
			[28] = "cbb558be0a3aca32322960a7c47de82",
			[29] = "3fdb558ea0ae7f05f9623ff54ff6c31",
			[30] = "399eecae8ad427718879430013cbaead",
			[31] = "1cc235d349911eb835346be57f23afd4",
			[32] = "39459a3e0f7fac54e2373b7afb799b1f",
			[33] = "3ee92e429c7167e910ad8d9e907ce9e6",
			[34] = "9a58bf423affc176de6871c6946427f",
			[35] = "217de1bb8bb1ab729df5ceb28cc237",
			[36] = "6",
			[37] = "4a7d3127acb1d9d63fbb12d1998acbcd",
			[38] = "37a1b1cc5ce5955e6927b8a0be607672",
			[39] = "67dc5debbe41df6e4f625aa1282f90d",
			[40] = "4380ad6d26fafebdc1865490cc7b886f",
			[41] = "59027e0c78b977e07824ad3f13d8fb83",
			[42] = "37abc1ab524a3fa6426e7699e79ed055",
			[43] = "1f5889733073ba0dbc15699a91e9ce77",
			[44] = "327a33e4804c59922e0ad4ef52c6de85",
			[45] = "541316716e5e6f4242a647c92c538156",
			[46] = "1045c1a86d83c28384cc59075121c424",
			[47] = "4e57a3fbf4a57fb46047b4ab5a9d9e13",
			[48] = "25c6ee26380c0fa8637a7e2de8909aa2",
			[49] = "7",
			[50] = "4c1b184bd0a9fbb8016fd2accacc3974",
			[51] = "5f3dc1f5436670ae7db2a3697e96fc1",
			[52] = "274888654d285e1b456f572b1537d113",
			[54] = "2cc7df7b0bbaddf9b6c10026f1c18b51",
			[55] = "5f18346aeb83dd5f3d12ba08d2626129",
			[56] = "1f3658ada6dc6ad876ac347376fe13c9",
			[57] = "1322a9c3b403434ff5f0844599617ca7",
			[58] = "35387bde818eb63ada2200a8961719d1",
			[59] = "f15241a33b0cfb446f6b396297ffbf3",
			[60] = "9a13faa12d7739bd271723ade11dbeb",
			[62] = "45c11c41de0139f08cc806b248eeea7d",
			[63] = "13190051d0f582f4b4b3e10fba6bcdc3",
			[64] = "8",
			[65] = "2f44ee31ef11f1e041c5dc4734a05708",
			[66] = "4acbc94b0fd58ca158900d5ce645bec",
			[68] = "60257e93e0974dbcd06b979f5a47e76f",
			[69] = "4cdf1a7305f09d090eaeda3aeae4a635",
			[70] = "4ae10ea94a44e9be9451a847e77a3936",
			[72] = "c8e54092b718a4214d064cbdf8c698e",
			[74] = "3444a9a3dc80764eb53f9202edb24a12",
			[75] = "333e11b67ca1f92eebca204af60e10f2",
			[76] = "41ff73653af0a423ebf64ec3c63a20a2",
			[77] = "33f489c7462d8f1ae74f1c038423686",
			[78] = "1770c4abd4023f184ba032ba66c8ade3",
			[79] = "3c4c81760ef85c7b571b4f891649e64f",
			[80] = "2e74f653115d36821370e1095542caf5",
			[81] = "9",
			[82] = "55eb50263e7b6e913814c68ba433a0b3",
			[84] = "27fcc3df1a01761f6a775c0f1f78a5dd",
			[85] = "444699a6ec75af134e8304bf932e79db",
			[86] = "2a80de463669824da6b3253c088d97e5",
			[87] = "51bb41a01003857029b5fb4020e9c1d7",
			[88] = "5c509dfb2f095f0422a01f186deb4e1c",
			[89] = "1923d478504b67b59ec8ae1a154654ff",
			[90] = "2ca48bd77c7715b3a2640d4d9f67a6a",
			[91] = "ca61e8231f3b59f1a97cedd8768fbc6",
			[92] = "126c8731bf547a4e45b0c360163bfccd",
			[93] = "479c97732d6fac4a0c4ad8964b95adf7",
			[94] = "356870d65524375c5802aac1ff4c506a",
			[95] = "63840baecb9ce0a51a270159bc79796",
			[96] = "5ef52f572278b33fc6b7e79a37fd267",
			[97] = "3610283097aedf2a259556d93489ea97",
			[98] = "32fe703979c6e733d7cf09150bb36658",
			[99] = "16bf6d8f823e86662312fe9d5c988475",
		},
	},
	{
		.p = "edf5623aef85b0b731e8f3e4c9e8d089",
		.first_non_square = 53,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "16fcdbe33357a001d4fc47a63b82b779",
			[3] = "1ecf4968a0192536a7749d06021eb42d",
			[4] = "2",
			[5] = "20a67fac9adbaf3e3780a6234ab6822e",
			[6] = "2af9620e1f106e9a9051afc7aad33623",
			[7] = "6ca9e7acb8e5b669c9b88ca94e76fdb",
			[8] = "2df9b7c666af4003a9f88f4c77056ef2",
			[9] = "3",
			[10] = "4b00db7c853c5b4f00c6644aad3be157",
			[11] = "2d736e49f3108641e7452d9978126b30",
			[12] = "3d9e92d140324a6d4ee93a0c043d685a",
			[13] = "43cac0cde7a35ba38264df5cbcbb4cd5",
			[14] = "41a304d2883c6a82d4fb6b2c3079e110",
			[15] = "67b91838119086d8078fd5be1bc8080c",
			[16] = "4",
			[17] = "5bd9ce3ab9d83d23da97a992ad7057d5",
			[18] = "44f693a99a06e0057ef4d6f2b288266b",
			[19] = "6bf0939c5e1638d3f76312968f659bd3",
			[20] = "414cff5935b75e7c6f014c46956d045c",
			[21] = "597b55effbb87b39e16b6d017bf7b30e",
			[22] = "2c06cac16e38fca86d4478f9c87c625d",
			[23] = "3c68a5b1ddb3dde1e92ccd7ec30a0b59",
			[24] = "55f2c41c3e20dd3520a35f8f55a66c46",
			[25] = "5",
			[26] = "15af5f9e777e5614e2b9c15efe617ffa",
			[27] = "5c6ddc39e04b6fa3f65dd712065c1c87",
			[28] = "d953cf5971cb6cd3937119529cedfb6",
			[29] = "2715ddb62536c46c4eea2bc625137fcd",
			[30] = "212c5620fd99f5f09808644e22c79c32",
			[31] = "6b577ba061d38d163ed349282302a120",
			[32] = "5bf36f8ccd5e800753f11e98ee0adde4",
			[33] = "13aa3aa2e45a174e92d9731e834dd329",
			[34] = "5f856c5a327c79f10ea1e2a8d7703c3c",
			[35] = "2dc1e3e916a6334eb3477947df87b8ae",
			[36] = "6",
			[37] = "2ce03f825cbb1e23600f9aa78fd89284",
			[38] = "f0756eb9a9c43ad60272f73d2f20934",
			[39] = "312aa19e77e6fef0afd8ad15ba1059ba",
			[40] = "57f3ab41e50cfa19305c2b4f6f710ddb",
			[41] = "56fb9e4458d3ec5aa118b9631c581c61",
			[42] = "6a4cd4bfebe6ce666facf556c29ab9e",
			[43] = "36c0e5bef8ace24e6a9b6a35dc24b702",
			[44] = "5ae6dc93e6210c83ce8a5b32f024d660",
			[45] = "61f37f05d0930dbaa681f269e023868a",
			[46] = "35dab1813560b2e575205b9c85ba76ce",
			[47] = "5037067f108024661ae1aa2c5c3228cd",
			[48] = "72b83c986f211bdc94167fccc16dffd5",
			[49] = "7",
			[50] = "72f04b7000b6200928ed663f298d955d",
			[51] = "f5a4507c2325355cdb0dadb2b2bce2c",
			[52] = "665fe09f203ef9702d1f352b507236df",
			[54] = "6d093c10925464e780f3e48dc96f2e20",
			[55] = "3eca2d8e6904692c541d73928b4b8451",
			[56] = "6aaf5895df0cdbb187f21d8c68f50e69",
			[57] = "27423a9ff36f3d4ef62ac239f3ac9262",
			[58] = "4d1eab01cec65f80ea32b0606b44490",
			[60] = "1e8331cacc64a30722c948689258c071",
			[61] = "274990b8bb86788b18d8567d7cf13f7f",
			[62] = "2012553a6ccaec6d59c20d078ad45a53",
			[63] = "145fdb7062ab1233d5d29a5fbeb64f91",
			[64] = "8",
			[65] = "2323facf1d23fd1dca99c58dcf9f51d1",
			[66] = "abb680a9971a272a8ea46b2364fc14a",
			[68] = "3641c5c57bd5366f7cb9a0bf6f0820df",
			[69] = "6fe190bed95ca359eb6734e670f50c4a",
			[70] = "15c4010e04617c35f94feed5c2417719",
			[72] = "64083ae7bb77f0ac33ff45ff64d883b3",
			[73] = "6bee761e1205d462392ce8e9c95c7da8",
			[74] = "596eca3cd2371ce3ec9ffca3b68bc077",
			[75] = "53e8f32fcf07f6a5eca1e2c6bf4f4ba8",
			[76] = "16143b0233593f0f4322ceb7ab1d98e3",
			[77] = "71702a65ae7de31ccb0de3444caf69af",
			[78] = "114dfd1ba42d0cdda59bb8d4845e6f6d",
			[80] = "6b5b63888416f3be53e65b579f0ec7d1",
			[81] = "9",
			[82] = "2ca2b887752daa4e112076a9f94ee2a4",
			[84] = "3afeb65af814ba436f1219e1d1f96a6d",
			[85] = "584290bf297fad6e90ca21b3a654a2ea",
			[86] = "579d6f1c2105e88017132be6c7046179",
			[87] = "272af66060f7c2226c9cf2d1b97a9396",
			[88] = "580d9582dc71f950da88f1f390f8c4ba",
			[90] = "cf2cfc55fd09eca2f95c704c2352c84",
			[91] = "305a0b4b9244b27f0fac4d3f66cb1493",
			[92] = "752416d7341df4f35f8f58e743d4b9d7",
			[93] = "14764518d2fa770b40d7b124c1028271",
			[94] = "12413e5231c1fbf54bf1ae472e0be00e",
			[95] = "73033d976e08cf57e754d4502cee064f",
			[96] = "420fda027343f64cf0a234c61e9bf7fd",
			[98] = "4d0b5f04882050aa5f02fe592955cc3a",
			[99] = "659b175d16541df17c196b1861b18ef9",
		},
	},
	{
		.p = "f216567317bd6168b283b35efdeb6e01",
		.first_non_square = 61,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "480d3cc58db85c317fd3ca162a7ef31",
			[3] = "751e72f645812eae49b9e8f43808b53c",
			[4] = "2",
			[5] = "66a75882537b0fd3e89cfaf5a83e5caf",
			[6] = "47664935d814e997186d42dfa98dd547",
			[7] = "4da889482696f681bdbef67c7ac1796e",
			[8] = "901a798b1b70b862ffa7942c54fde62",
			[9] = "3",
			[10] = "57d17116e7de4d7c7380b35d174c50fe",
			[11] = "692649e1555a93b8fa42eafb084cf8a1",
			[12] = "7d970868cbb040c1f0fe1768dda0389",
			[13] = "3209b67f7292b1ffb4371dc03ba01083",
			[14] = "148d5a6790f3e04b3568bf944c02fe61",
			[15] = "5520d9f992eb5c8c8881685999651f93",
			[16] = "4",
			[17] = "57d481f0380db09125d31dc6dfd1d9c9",
			[18] = "d827b650a92914947f7b5e427f7cd93",
			[19] = "69962be7fbbdd6eb3f9293403a845b10",
			[20] = "24c7a56e70c741c0e149bd73ad6eb4a3",
			[21] = "4ce832377d3bc8b2e4d6a23d6f5dd99f",
			[22] = "331c2415f14f2d68991cf9c2e2fc696a",
			[23] = "52c3351f96e116c602e2ca5e4298e8a7",
			[24] = "6349c40767938e3a81a92d9faacfc373",
			[25] = "5",
			[26] = "14d036065850428ccb1ba5e425fa304f",
			[27] = "6d45026fb8c62aa22aaa077daa2eb1b3",
			[28] = "56c543e2ca8f74653705c66608687b25",
			[29] = "29f9490f6684ea66db901d68cce1cdb1",
			[30] = "576da7973e26fea493d4f7b7d53fb53",
			[31] = "192cd4b66179a4082118574bfc148b27",
			[32] = "12034f31636e170c5ff4f2858a9fbcc4",
			[33] = "23224092a600150201ba52b3424456d7",
			[34] = "5f9b7eb636b189b226aac177355abc25",
			[35] = "733b83f9abd4bc87d247385c96040338",
			[36] = "6",
			[37] = "3d5cdd23a2a5a3f9d4d323b3f874ca9b",
			[38] = "657de6d78704b86d6df714f94c9596b7",
			[39] = "25a875a44f870578858989b880e1d053",
			[40] = "427374454800c66fcb824ca4cf52cc05",
			[41] = "5bed9dc9cb362674bda905c0aac4335",
			[42] = "f12748e2759fd3766c92ead058963ce",
			[43] = "9ccd7e40b4b4dbe60c0e14bbe7f12cc",
			[44] = "1fc9c2b06d0839f6bdfddd68ed517cbf",
			[45] = "41dfb313e2b3ce1307533d81facfa80c",
			[46] = "4cc8c3b03683a8d1853c9e55ec8d52e8",
			[47] = "3f765db35fcf6e1a387d4ecc26efd1e5",
			[48] = "fb2e10d197608183e1fc2ed1bb40712",
			[49] = "7",
			[50] = "168422fdbc499ccf77f22f26ed47abf5",
			[51] = "2e3b00d48ee6701ed7f985e0698e7e1e",
			[52] = "64136cfee52563ff686e3b8077402106",
			[53] = "6f4feac060b10c8f42797ff7f442ea12",
			[54] = "1be37ad18f7ea4a3693beac00141ee2c",
			[55] = "769f25532938ca79dd2823b747b11e1e",
			[56] = "291ab4cf21e7c0966ad17f289805fcc2",
			[57] = "3a8c4047c4ddf27d1e5e917d9185a6b",
			[58] = "69a0f3549b38d0265f39e4d632238958",
			[59] = "16b4266e9fcb5601a88ba6a1eee156b1",
			[60] = "47d4a27ff1e6a84fa180e2abcb212edb",
			[62] = "158d6e26917f81791039a0a8358c9866",
			[63] = "91cba9aa3f87de37946cfe98da701b7",
			[64] = "8",
			[65] = "cbe13f6bfadaee035d56a39bf98bfad",
			[66] = "6604d1e998f3793831782f4d99b1bd84",
			[68] = "426d5292a7a2004666dd77d13e47ba6f",
			[69] = "1b280a5d865f3099592b6d469a6f3ac",
			[70] = "167dd1d604c01acd4591c8a2c13e301f",
			[71] = "271e22c50a6918cd603a20c2872a8b59",
			[72] = "1b04f6ca152522928fef6bc84fef9b26",
			[73] = "11a5924dfa463201c0cc2c515c1f0bf1",
			[74] = "60091028fb4a74808bad9094366d9379",
			[75] = "656b91e92c0b26960b9a26071c54ae2a",
			[76] = "1ee9fea32041b392335e8cde88e2b7e1",
			[77] = "2bdf95db0fa53563fe363c057dd8bd33",
			[78] = "3df809dc85028674ec0781daa86cdc8c",
			[79] = "2b5c1a1ba7726ca49bdb8ffb265cebc7",
			[80] = "498f4adce18e8381c2937ae75add6946",
			[81] = "9",
			[82] = "4fbde7f74ad09eca78d83d64e54c9a30",
			[83] = "26e0943fdb9849c21d9f0cccdee7b474",
			[84] = "5845f2041d45d002e8d66ee41f2fbac3",
			[85] = "1742e5ddddcdc922680c7ccb36f25999",
			[86] = "3db8bbcd65fc815230dc101898dd51f9",
			[87] = "77f93b086452981199e093f1112ac36",
			[88] = "6638482be29e5ad13239f385c5f8d2d4",
			[90] = "155dfcd19fdd870ca7fe66b847f984f9",
			[91] = "8bf6d293b0a33d416f2d8765850281a",
			[92] = "4c8fec33e9fb33dcacbe1ea278b99cb3",
			[93] = "16b76f5c478034b02d3a2455278766b0",
			[94] = "54aada5a9d62cf663caa710a4fa9265",
			[95] = "564288cd696118b6fb7a77938d8329f6",
			[96] = "2b82ce64489644f3af31581fa84be71b",
			[98] = "1f85ca966e00a855a7eca869b2978a57",
			[99] = "495c8730e85259c23c450d921afb7be2",
		},
	},
	{
		.p = "d5b3e02a00ec37238b8739f3ab1bed99",
		.first_non_square = 67,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "45029faf1a073c8cb536f5eda7fd1560",
			[3] = "f768c37c2f57a1a9c226ff461c562b",
			[4] = "2",
			[5] = "65b4689cf2d75db16dcd3105953ce3c6",
			[6] = "5684c6ba43a113848a8969d2fb6ca23a",
			[7] = "1ff8e09fcaf051326e5c632e0cd4f823",
			[8] = "4baea0cbccddbe0a21194e185b21c2d9",
			[9] = "3",
			[10] = "4285c184615d84354cf9a87345e9d1bb",
			[11] = "e970057e6c9a32e07738b0ab78d90ba",
			[12] = "1eed186f85eaf4353844dfe8c38ac56",
			[13] = "67cc0965e6dd98513e4d39d0b090df81",
			[14] = "1ab1e5e513662050b68d92f8f531b05d",
			[15] = "281851578e9af5ae568a5b6f1c9147fd",
			[16] = "4",
			[17] = "1889951f21b30e2095ab76c87fcf9ba5",
			[18] = "6ac011cb2d6817d6be2582ab324ad79",
			[19] = "5eb623f1739f6fdc336ee0e74b3511ca",
			[20] = "a4b0ef01b3d7bc0afecd7e880a2260d",
			[21] = "5df17f78bd0ed66c6837513486b31cc5",
			[22] = "606ae7b931bdba98f04c5fe37af4ff6b",
			[23] = "148d44e0e884ce444c99b6cd437c575e",
			[24] = "28aa52b579aa101a7674664db442a925",
			[25] = "5",
			[26] = "325e9911c86ae631a6bc1f5b699c7e31",
			[27] = "2e63a4a748e06e4fd4674fdd2550281",
			[28] = "3ff1c13f95e0a264dcb8c65c19a9f046",
			[29] = "38fcfd8cec4b90a0de1c4c38cedc15b9",
			[30] = "2f6ec8019c1949b8d7f7ce76ff9fa69b",
			[31] = "e6ca869dfb345ffd59ace43b3792685",
			[32] = "3e569e926730bb0f49549dc2f4d867e7",
			[33] = "579bee988ae90b7ccc3e68e0c54d9e2f",
			[34] = "569b25c035de96b42221456fa925c7f5",
			[35] = "1f830a9c274bf0424f04e1dc96fba083",
			[36] = "6",
			[37] = "56a04e969e561d11e819e3f0999b2ec3",
			[38] = "1399392aaee18717002eebf72a4f2714",
			[39] = "184fa0b19872dbc03cc003fb7ef0a647",
			[40] = "50a85d213e312eb8f193e90d1f484a23",
			[41] = "32edb4d05e11465009b98a6710278c1",
			[42] = "1a8d7edc19c5bfc9ffd61781bccc9691",
			[43] = "22c80c569d45bb4a9f05877846f790f3",
			[44] = "1d2e00afcd93465c0ee716156f1b2174",
			[45] = "5b6959acd799e1f0bde0591d149abdb9",
			[46] = "21456a4847a1103ea0c239e574779197",
			[47] = "47bfe557af21dc9c64152430cf6cf25c",
			[48] = "3dda30df0bd5e86a7089bfd187158ac",
			[49] = "7",
			[50] = "525aa1e87fb43f878cfba6430e467052",
			[51] = "68bd2ad79d20ea7312ed693d62c8d008",
			[52] = "61bcd5e333106810eecc65249fa2e97",
			[53] = "81bd0dba2601145e75e1d77f1d04844",
			[54] = "2dda7404c9f7036a141503854729f915",
			[55] = "68e4b71490938a9f48933bde86807125",
			[56] = "3563cbca26cc40a16d1b25f1ea6360ba",
			[57] = "4f2d5569b091a5a79ab7e16a6ba25362",
			[58] = "55c448979b854e6bb106146571f68426",
			[59] = "302a3d80209e0026e0fecb63e2ce75dd",
			[60] = "5030a2af1d35eb5cad14b6de39228ffa",
			[61] = "2b92f6a2d393e7a4259eb1cb017f2b00",
			[62] = "5a261ada71bcb91ccb1a04c49d16c6e9",
			[63] = "5feaa1df60d0f3974b15298a267ee869",
			[64] = "8",
			[65] = "6181cb54bab42602fb979953302dbe36",
			[66] = "610168cbd024ca65f82099abc21d6efd",
			[68] = "31132a3e43661c412b56ed90ff9f374a",
			[69] = "4835741318cf28d4227c2137b4c5613c",
			[70] = "209bda2b3602ef01ac5d0be10bfad042",
			[71] = "40ba51b46a55058308aec51c587fc6fe",
			[72] = "d58023965ad02fad7c4b05566495af2",
			[74] = "d230d76484819d75b87a4216ee67233",
			[75] = "4d50bd16cecb62850cac2fc5e8daed7",
			[76] = "1847984719ad576b24a9782514b1ca05",
			[77] = "3c0c6536905353924fb4f8abc19e281b",
			[78] = "3d0dff19927ac726586098881fb096ea",
			[80] = "14961de0367af7815fd9afd101444c1a",
			[81] = "9",
			[82] = "1bccdc1adcd9e6f574bab3fd655d69df",
			[83] = "18d4bce73268207ac84da046587f3c06",
			[84] = "19d0e13886ce8a4abb18978a9db5b40f",
			[85] = "4b9978cf3d8230ed3cadc49a74d5368",
			[86] = "128b08847198de2b1645ac717643a123",
			[87] = "63caad9c66c7cf88bcaf13f4ec88588a",
			[88] = "14de10b79d70c1f1aaee7a2cb531eec3",
			[90] = "e229b9cdcd3aa83a49a4099d95e7868",
			[91] = "636ffea27582b5e892f407d04f9dfc9",
			[92] = "291a89c1d1099c8899336d9a86f8aebc",
			[93] = "26be0f04da5bbe43fd72738163315128",
			[94] = "2978cccfee4c80269a3463e3bebb5dc8",
			[95] = "36c57b7d41260e361126830cb6b5c75f",
			[96] = "5154a56af3542034ece8cc9b6885524a",
			[98] = "37aa9d75b45a3991dd72459841b3ba6e",
			[99] = "2bc50107b45ce98a165aa12026a8b22e",
		},
	},
	{
		.p = "ebad0be3a536493da57f0fe644b4e021",
		.first_non_square = 61,
		.sqrt = {
			[0] = "0",
			[1] = "1",
			[2] = "123db297cea699fb0344c1a1a1f186f3",
			[3] = "bf6436b702b833d17678ccfe261262f",
			[4] = "2",
			[5] = "70f8ee4ff65e26fb5773b23de20012cf",
			[6] = "5dee45725830feb3edd2b0e1513d9316",
			[7] = "66a976a6da314fb8c0e2017aa49b4aaa",
			[8] = "247b652f9d4d33f60689834343e30de6",
			[9] = "3",
			[10] = "3b0561b1c9001e576bf37ae3e029d215",
			[11] = "2a4ac9391e3061d7182f14f088c62564",
			[12] = "17ec86d6e057067a2ecf199fc4c24c5e",
			[13] = "3648df5a9ba7ba504370d498d59fd9a1",
			[14] = "6481962d0448c2db2aa4e42a481786f3",
			[15] = "1af1cbdc9ef10ecc8382347a181e34cc",
			[16] = "4",
			[17] = "3f172b01dabd178cca0bc966f1fdf6f3",
			[18] = "36b917c76bf3cdf109ce44e4e5d494d9",
			[19] = "65ef4e2e99d2781eb82b88fe04e6f1f5",
			[20] = "9bb2f43b879fb46f697ab6a80b4ba83",
			[21] = "18e1abe04941114658e9e6f21f47dd08",
			[22] = "5f978475f36f4e4ce77c522191208b3",
			[23] = "357fa88c030974a0bc36f851f0795e64",
			[24] = "2fd080fef4d44bd5c9d9ae23a239b9f5",
			[25] = "5",
			[26] = "408935d3281810e0ce90b4f2985fcf5e",
			[27] = "23e2ca42508289b74636a66fa723728d",
			[28] = "1e5a1e95f0d3a9cc23bb0cf0fb7e4acd",
			[29] = "5f2c0d7368f0b6b1d2a16643a705a279",
			[30] = "17549d7da5e5b8bd89b2849403020df0",
			[31] = "7210f7c02ab70af02f0c665461b42f9c",
			[32] = "48f6ca5f3a9a67ec0d13068687c61bcc",
			[33] = "4951d8bebbe35c3f19483f7c871c8ee2",
			[34] = "5a7a0a5f2d5f90c936cee2e1f1448983",
			[35] = "2d5cc3d220d762d0bafed021e3b701f7",
			[36] = "6",
			[37] = "4eb91be8674d073b7cad4f89d9b90fd9",
			[38] = "223270fd99acd27c3e6ca67c60efee95",
			[39] = "2c8220ca14a994690d2737863449596e",
			[40] = "75a2488013360c8ecd981a1e84613bf7",
			[41] = "67b2177c12aabd92026438a46ab4cb61",
			[42] = "5d34f549c9252d13b2e417b1dda2793d",
			[43] = "4710ac452532ca6122daeacc47ada339",
			[44] = "549592723c60c3ae305e29e1118c4ac8",
			[45] = "673dbf0c3de42bb460dc06d3614b584c",
			[46] = "600d79740b8a279419d8716358bdcff8",
			[47] = "6bb1a8ff33f37594bad6958999635634",
			[48] = "2fd90dadc0ae0cf45d9e333f898498bc",
			[49] = "7",
			[50] = "5b347cf7094101e71057c82829b7a2bf",
			[51] = "fb13b5a54b5f9a85079f05cd8dbc210",
			[52] = "6c91beb5374f74a086e1a931ab3fb342",
			[53] = "5c04393e2792c7f52e5ea35e49daf887",
			[54] = "2e1dc473635cb2de23f902bdaf03d921",
			[55] = "59c9e5d019f3c45d7bd67877148f7b0b",
			[56] = "22a9df899ca4c38750354791b485d23b",
			[57] = "1cbf6261b5e6f70d8b62240aca1404c0",
			[58] = "6df943332998c675130fe1ea5965661",
			[59] = "3931526869d8069ffab62e14f7463ebc",
			[60] = "35e397b93de21d99070468f4303c6998",
			[62] = "71287677776721bbb63d7a47e3d3271f",
			[63] = "484f5810e95da5ec9d26f489a91cffdd",
			[64] = "8",
			[65] = "24f66f45952f7b1a8a788adf0b988dd8",
			[66] = "55358e335092c9ad35db80f42dbda41a",
			[67] = "541c13cfadd2c6e4a3e57237c33e2b6e",
			[68] = "6d7eb5dfefbc1a2411677d1860b8f23b",
			[69] = "5bd017f4178d6d06fb91e3dd0daee5de",
			[70] = "bce4858fe7d92a437b916d8525a45b0",
			[71] = "2f318248a0091aa2094717d79179c32f",
			[72] = "6d722f8ed7e79be2139c89c9cba929b2",
			[74] = "5979aa2335b2e6800722423a6dce7cc8",
			[75] = "3bcf511930d990317505c00f6be5beeb",
			[76] = "1fce6f86719159003527fdea3ae6fc37",
			[77] = "3548cedb5a44367e61e396e1b9fbccd7",
			[78] = "3820cfa14dd428ff95f92b750c0af8fa",
			[79] = "1b610761a0faff110434c44da3056ad",
			[80] = "13765e8770f3f68ded2f56d501697506",
			[81] = "9",
			[82] = "4f0934854d6129caf0f3268c2b6b9ba1",
			[84] = "31c357c09282228cb1d3cde43e8fba10",
			[85] = "6528c8fcf5a00c45c1a742a924dc6105",
			[86] = "40948b6c8dacf9307d00643172b10021",
			[87] = "58f0cbf88142db86d306e56d7ca053ab",
			[88] = "bf2f08ebe6de9c99cef8a4432241166",
			[90] = "3a9ce6ce4a35ee3761a49f3aa43769e2",
			[91] = "61e94c9be16d9115fcf3d10de819b796",
			[92] = "6aff51180612e941786df0a3e0f2bcc8",
			[93] = "70e2effd74ee8238a346798baf98f88",
			[94] = "27630a3037b0748d160c9f0d0b6446ae",
			[95] = "3ab1e5b6243a976461a7c25ebc9b6933",
			[96] = "5fa101fde9a897ab93b35c47447373ea",
			[98] = "6bfd29bcfea813608e9dc47ad71a2f7c",
			[99] = "6cccb0384aa523b85cf1d114aa626ff5",
		},
	},
};

#define N_P_IS_1_MOD_8_TESTS \
    (sizeof(p_is_1_mod_8_tests) / sizeof(p_is_1_mod_8_tests[0]))

static int
bn_mod_sqrt_p_is_1_mod_8_test(const struct p_is_1_mod_8_tests *test,
    BN_CTX *ctx)
{
	BIGNUM *a, *p, *want, *got, *diff;
	const char *const *sqrts = test->sqrt;
	int i;
	int failed = 0;

	BN_CTX_start(ctx);

	if ((a = BN_CTX_get(ctx)) == NULL)
		errx(1, "a = BN_CTX_get()");
	if ((p = BN_CTX_get(ctx)) == NULL)
		errx(1, "p = BN_CTX_get()");
	if ((want = BN_CTX_get(ctx)) == NULL)
		errx(1, "want = BN_CTX_get()");
	if ((got = BN_CTX_get(ctx)) == NULL)
		errx(1, "got = BN_CTX_get()");
	if ((diff = BN_CTX_get(ctx)) == NULL)
		errx(1, "diff = BN_CTX_get()");

	if (!BN_hex2bn(&p, test->p))
		errx(1, "BN_hex2bn");

	for (i = 0; i < N_SMALL_SQUARE_TESTS; i++) {
		if (!BN_set_word(a, i))
			errx(1, "BN_set_word");

		if (BN_mod_sqrt(got, a, p, ctx) == NULL) {
			if (i < test->first_non_square || sqrts[i] != NULL) {
				fprintf(stderr, "no sqrt(%d) (mod %s)?\n",
				    i, test->p);
				failed |= 1;
			}
			continue;
		}

		if (sqrts[i] == NULL) {
			fprintf(stderr, "sqrt(%d) (mod %s): ", i, test->p);
			BN_print_fp(stderr, got);
			fprintf(stderr, "?\n");
			failed |= 1;
			continue;
		}

		if (!BN_hex2bn(&want, sqrts[i]))
			errx(1, "BN_hex2bn");

		if (!BN_mod_sub(diff, want, got, p, ctx))
			errx(1, "BN_mod_sub() failed\n");

		if (!BN_is_zero(diff)) {
			fprintf(stderr, "a: %d\n", i);
			fprintf(stderr, "p: %s\n", test->p);
			fprintf(stderr, "want: %s\n", sqrts[i]);
			fprintf(stderr, "got:  ");
			BN_print_fp(stderr, got);
			fprintf(stderr, "\n\n");

			failed |= 1;
			continue;
		}
	}

	BN_CTX_end(ctx);

	return failed;
}

static int
bn_mod_sqrt_p_is_1_mod_8(void)
{
	BN_CTX *ctx;
	size_t i;
	int failed = 0;

	if ((ctx = BN_CTX_new()) == NULL)
		errx(1, "BN_CTX_new");

	for (i = 0; i < N_P_IS_1_MOD_8_TESTS; i++) {
		const struct p_is_1_mod_8_tests *test = &p_is_1_mod_8_tests[i];

		failed |= bn_mod_sqrt_p_is_1_mod_8_test(test, ctx);
	}

	BN_CTX_free(ctx);

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= bn_mod_sqrt_test();
	failed |= bn_mod_sqrt_p_is_1_mod_8();

	return failed;
}
