struct rde_peer peer = {
	.conf.local_as = 42,
	.conf.remote_as = 22512,
};
struct rde_peer peer_l4 = {
	.conf.local_as = 196618,
	.conf.remote_as = 22512,
};
struct rde_peer peer_r4 = {
	.conf.local_as = 22512,
	.conf.remote_as = 196618,
};
struct rde_peer peer_b4 = {
	.conf.local_as = 196618,
	.conf.remote_as = 424242,
};

struct vector {
	uint8_t	*data;
	size_t	size;
	uint8_t	*expected;
	size_t	expsize;
} vectors[] = {

	{
		.data = "\xc0\x08\x04\x00\x00\x48\xce",
		.size = 7
	},
	{
		.data = "\xc0\x08\x04\x0f\x10\xc8\x02",
		.size = 7
	},
	{
		.data = "\xc0\x08\x04\x3b\xc0\x00\xc9",
		.size = 7
	},
	{
		.data = "\xe0\x08\x04\x4e\x97\x00\x03",
		.size = 7
	},
	{
		.data = "\xe0\x08\x08\x3b\xc0\x00\xc9\x3b\xc0\x00\xcb",
		.size = 11
	},
	{
		.data = "\xc0\x08\x08\x9e\x19\x7a\x44\x9e\x19\x7a\x45",
		.size = 11
	},
	{
		.data = "\xc0\x08\x14\x00\x00\x0b\x5a\x00\x00\x3f\x89\x00\x00"
		    "\x3f\x94\x00\x00\x48\xce\x00\x00\xa2\xda",
		.size = 23
	},
	{
		.data = "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x00\x40",
		.size = 11
	},
	{
		.data = "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x00\x42",
		.size = 11
	},
	{
		.data = "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x00\xb2",
		.size = 11
	},
	{
		.data = "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x00\xd3",
		.size = 11
	},
	{
		.data = "\xe0\x10\x08\x00\x02\xfc\x00\x00\x00\x01\x10",
		.size = 11
	},
	{
		.data = "\xe0\x10\x08\x00\x02\xfc\x00\x00\x00\x01\x1e",
		.size = 11
	},
	{
		.data = "\xe0\x10\x08\x00\x02\xfc\x00\x00\x00\x01\x29",
		.size = 11
	},
	{
		.data = "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x01\xb6",
		.size = 11
	},
	{
		.data = "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x01\xe0",
		.size = 11
	},
	{
		.data = "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x02\x8b",
		.size = 11
	},
	{
		.data = "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x02\xc5",
		.size = 11
	},
	{
		.data = "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x03\xc2",
		.size = 11
	},
	{
		.data = "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x04\x09",
		.size = 11
	},
	{
		.data = "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x04\xe4",
		.size = 11
	},
	{
		.data = "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x05\x57",
		.size = 11
	},
	{
		.data = "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x05\x92",
		.size = 11
	},
	{
		.data = "\xc0\x10\x08\x00\x02\xfd\xe8\x00\x00\x2a\xf8",
		.size = 11
	},
	{
		.data = "\xc0\x20\x0c\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03",
		.size = 15
	},
	{
		.data = "\xc0\x20\x18\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03",
		.size = 27
	},
	{
		.data = "\xe0\x20\x24\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x00\xed\x5f\x00\x00\xed\x5f\x00\x00"
		    "\xed\x5f",
		.size = 39
	},
	{
		.data = "\xe0\x20\x24\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x16\x49\x00\x03\x16\x49\x00\x00"
		    "\x00\x64",
		.size = 39
	},
	{
		.data = "\xe0\x20\x24\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x16\xa9\x00\x03\x16\xa9\x00\x00"
		    "\x00\x00",
		.size = 39
	},
	{
		.data = "\xe0\x20\x24\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x24\x92\x00\x00\x00\x03\x00\x00"
		    "\x00\x01",
		.size = 39
	},
	{
		.data = "\xe0\x20\x24\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x25\x4b\x00\x00\x00\xc8\x00\x00"
		    "\x00\x00",
		.size = 39
	},
	{
		.data = "\xe0\x20\x24\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x26\x8f\x00\x00\x01\x2c\x00\x00"
		    "\x00\x00",
		.size = 39
	},
	{
		.data = "\xe0\x20\x24\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x26\xa3\x00\x00\x02\x58\x00\x03"
		    "\x26\xa3",
		.size = 39
	},
	{
		.data = "\xe0\x20\x24\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x33\xc8\x00\x00\x07\xd0\x00\x00"
		    "\x00\x1e",
		.size = 39
	},
	{
		.data = "\xe0\x20\x30\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x00\xdf\x1e\x00\x00\x00\x1e\x00\x00"
		    "\x00\x00\x00\x00\xdf\x1e\x00\x00\x00\x1e\x00\x00"
		    "\x00\x0a",
		.size = 51
	},
	{
		.data = "\xe0\x20\x30\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x1c\xe3\x00\x00\x00\x01\x00\x00"
		    "\x00\x00\x00\x03\x1c\xe3\x00\x00\x00\x01\x00\x00"
		    "\x00\x02",
		.size = 51
	},
	{
		.data = "\xe0\x20\x30\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x26\xa3\x00\x00\x02\x58\x00\x03"
		    "\x26\xa3\x00\x03\x26\xa3\x00\x03\x26\xa3\x00\x00"
		    "\x00\xc8",
		.size = 51
	},
	{
		.data = "\xe0\x20\x30\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x27\xdd\x00\x00\x00\x00\x00\x00"
		    "\x00\x00\x00\x03\x27\xdd\x00\x00\x00\x00\x00\x00"
		    "\x23\x49",
		.size = 51
	},
	{
		.data = "\xe0\x20\x30\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x27\xdd\x00\x00\x00\x00\x00\x00"
		    "\x00\x00\x00\x03\x27\xdd\x00\x00\x00\x00\x00\x00"
		    "\xc2\x51",
		.size = 51
	},
	{
		.data = "\xe0\x20\x30\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x28\xe8\x00\x00\x00\x00\x00\x00"
		    "\x00\x00\x00\x03\x28\xe8\x00\x00\x00\x01\x00\x00"
		    "\x00\x00",
		.size = 51
	},
	{
		.data = "\xe0\x20\x3c\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x02\x1a\xcd\x00\x00\xfc\x00\x00\x00"
		    "\x00\x0b\x00\x02\x1a\xcd\x00\x00\xfc\x00\x00\x00"
		    "\x00\x15\x00\x02\x1a\xcd\x00\x00\xff\xed\x00\x00"
		    "\x00\x01",
		.size = 63
	},
	{
		.data = "\xe0\x20\x3c\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x16\xa9\x00\x00\x00\x00\x00\x03"
		    "\x35\x3a\x00\x03\x16\xa9\x00\x03\x16\xa9\x00\x00"
		    "\x00\x00\x00\x03\x26\xa3\x00\x03\x26\xa3\x00\x00"
		    "\x03\x84",
		.size = 63
	},
	{
		.data = "\xe0\x20\x3c\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x16\xa9\x00\x03\x16\xa9\x00\x03"
		    "\x16\xa9\x00\x03\x26\xa3\x00\x00\x01\x2c\x00\x00"
		    "\x03\xe7\x00\x03\x26\xa3\x00\x00\x02\x58\x00\x03"
		    "\x26\xa3",
		.size = 63
	},
	{
		.data = "\xe0\x20\xb4\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x01\x00\x00\xd0\x5b\x00\x00\x00\x0b\x00\x00"
		    "\x00\x03\x00\x03\x22\xd3\x00\x00\x00\x0a\x00\x00"
		    "\x00\x0a\x00\x03\x22\xd3\x00\x00\x00\x0a\x00\x00"
		    "\x00\xc9\x00\x03\x22\xd3\x00\x00\x00\x0a\x00\x00"
		    "\x00\xca\x00\x03\x22\xd3\x00\x00\x00\x14\x00\x00"
		    "\x00\x0b\x00\x03\x22\xd3\x00\x00\x00\x14\x00\x00"
		    "\x00\x64\x00\x03\x22\xd3\x00\x00\x00\x14\x00\x00"
		    "\x00\x65\x00\x03\x22\xd3\x00\x00\x00\x14\x00\x00"
		    "\x00\x66\x00\x03\x22\xd3\x00\x00\x00\x14\x00\x00"
		    "\x00\x67\x00\x03\x22\xd3\x00\x00\x00\x14\x00\x00"
		    "\x00\x68\x00\x03\x22\xd3\x00\x00\x00\x14\x00\x00"
		    "\x00\xc8\x00\x03\x22\xd3\x00\x00\x00\x14\x00\x00"
		    "\x00\xcf\x00\x03\x22\xd3\x00\x00\x00\x14\x00\x00"
		    "\x00\xd0\x00\x03\x22\xd3\x00\x00\x00\x79\x00\x00"
		    "\x00\x00",
		.size = 183
	},
	{
		.data = "\xc0\x10\x08\x43\x00\x00\x00\x00\x00\x00\x02",
		.size = 11,
		.expected = "",
		.expsize = 0,
	},
	{
		.data = "\xc0\x10\x10\x00\x02\xfc\x00\x00\x00\x00\x40"
		    "\x43\x00\x00\x00\x00\x00\x00\x02",
		.size = 19,
		.expected = "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x00\x40",
		.expsize = 11,
	},
	{
		.data = "\xc0\x10\x18\x00\x02\xfc\x00\x00\x00\x00\x40"
		    "\x43\x00\x00\x00\x00\x00\x00\x02"
		    "\x06\x00\x00\x00\x00\x00\x00\x01",
		.size = 27,
		.expected = "\xc0\x10\x10\x06\x00\x00\x00\x00\x00\x00\x01"
		    "\x00\x02\xfc\x00\x00\x00\x00\x40",
		.expsize = 19,
	},
	{
		.data = "\xe0\x08\x04\x4e\x97\x00\x03"
		    "\xc0\x10\x08\x43\x00\x00\x00\x00\x00\x00\x02",
		.size = 7 + 11,
		.expected = "\xe0\x08\x04\x4e\x97\x00\x03",
		.expsize = 7,
	},
	{
		.data = "\xe0\x08\x04\x4e\x97\x00\x03"
		    "\xc0\x10\x10\x00\x02\xfc\x00\x00\x00\x00\x40"
		    "\x43\x00\x00\x00\x00\x00\x00\x02",
		.size = 7 + 19,
		.expected = "\xe0\x08\x04\x4e\x97\x00\x03"
		    "\xc0\x10\x08\x00\x02\xfc\x00\x00\x00\x00\x40",
		.expsize = 7 + 11,
	},
	{
		.data = "\xc0\x10\x08\x43\x00\x00\x00\x00\x00\x00\x02"
		    "\xc0\x20\x0c\x00\x00\xd0\x5b\x00\x00\x00\x0b"
		    "\x00\x00\x00\x03",
		.size = 11 + 15,
		.expected = "\xc0\x20\x0c\x00\x00\xd0\x5b\x00\x00\x00\x0b"
		    "\x00\x00\x00\x03",
		.expsize = 15,
	},
	{
		.data = "\xe0\x08\x04\x4e\x97\x00\x03"
		    "\xc0\x10\x08\x43\x00\x00\x00\x00\x00\x00\x02"
		    "\xc0\x20\x0c\x00\x00\xd0\x5b\x00\x00\x00\x0b"
		    "\x00\x00\x00\x03",
		.size = 7 + 11 + 15,
		.expected = "\xe0\x08\x04\x4e\x97\x00\x03"
		    "\xc0\x20\x0c\x00\x00\xd0\x5b\x00\x00\x00\x0b"
		    "\x00\x00\x00\x03",
		.expsize = 7 + 15,
	},
};

struct community filters[] = {
	{ /* 0 */
		.flags = COMMUNITY_TYPE_BASIC,
		.data1 = COMMUNITY_WELLKNOWN,
		.data2 = COMMUNITY_NO_ADVERTISE
	},
	{ /* 1 */
		.flags = COMMUNITY_TYPE_BASIC,
		.data1 = COMMUNITY_WELLKNOWN,
		.data2 = COMMUNITY_NO_EXPORT
	},
	{ /* 2 */
		.flags = COMMUNITY_TYPE_BASIC,
		.data1 = COMMUNITY_WELLKNOWN,
		.data2 = COMMUNITY_NO_EXPSUBCONFED
	},
	{ /* 3 */
		.flags = COMMUNITY_TYPE_BASIC,
		.data1 = 22512,
		.data2 = 1
	},
	{ /* 4 */
		.flags = COMMUNITY_TYPE_BASIC,
		.data1 = 22512,
		.data2 = 42
	},
	{ /* 5 */
		.flags = COMMUNITY_TYPE_BASIC,
		.data1 = 22512,
		.data2 = 65366
	},
	{ /* 6 */
		.flags = COMMUNITY_TYPE_BASIC,
		.data1 = 1,
		.data2 = 22512
	},
	{ /* 7 */
		.flags = COMMUNITY_TYPE_BASIC,
		.data1 = 42,
		.data2 = 22512
	},
	{ /* 8 */
		.flags = COMMUNITY_TYPE_BASIC,
		.data1 = 65366,
		.data2 = 22512
	},
	{ /* 9 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 22512,
		.data2 = 22512,
		.data3 = 22512
	},
	{ /* 10 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 22512,
		.data2 = 42,
		.data3 = 22512
	},
	{ /* 11 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 22512,
		.data2 = 42,
		.data3 = 42
	},
	{ /* 12 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 42,
		.data2 = 22512,
		.data3 = 22512
	},
	{ /* 13 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 42,
		.data2 = 42,
		.data3 = 22512
	},
	{ /* 14 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 42,
		.data2 = 22512,
		.data3 = 42
	},
	{ /* 15 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 65366,
		.data2 = 22512,
		.data3 = 22512
	},
	{ /* 16 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 22512,
		.data2 = 65366,
		.data3 = 22512
	},
	{ /* 17 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 22512,
		.data2 = 65366,
		.data3 = 65366
	},
	{ /* 18 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 42,
		.data2 = 42,
		.data3 = 42
	},
	{ /* 19 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 65366,
		.data2 = 65366,
		.data3 = 65366
	},
	{ /* 20 */
		.flags = COMMUNITY_TYPE_BASIC | (COMMUNITY_ANY << 16),
		.data1 = COMMUNITY_WELLKNOWN,
	},
	{ /* 21 */
		.flags = COMMUNITY_TYPE_BASIC | (COMMUNITY_ANY << 16),
		.data1 = 22512,
	},
	{ /* 22 */
		.flags = COMMUNITY_TYPE_BASIC | (COMMUNITY_ANY << 8),
		.data2 = 22512
	},
	{ /* 23 */
		.flags = COMMUNITY_TYPE_LARGE | (COMMUNITY_ANY << 8) |
		    (COMMUNITY_ANY << 16) | (COMMUNITY_ANY << 24),
	},
	{ /* 24 */
		.flags = COMMUNITY_TYPE_LARGE | (COMMUNITY_ANY << 16) |
		    (COMMUNITY_ANY << 24),
		.data1 = 22512,
	},
	{ /* 25 */
		.flags = COMMUNITY_TYPE_EXT,
		.data3 = EXT_COMMUNITY_TRANS_TWO_AS << 8 | 0x02,
		.data1 = 22512,
		.data2 = 42,
	},
	{ /* 26 */
		.flags = COMMUNITY_TYPE_EXT,
		.data3 = EXT_COMMUNITY_TRANS_FOUR_AS << 8 | 0x02,
		.data1 = 22512,
		.data2 = 42,
	},
	{ /* 27 */
		.flags = COMMUNITY_TYPE_EXT,
		.data3 = EXT_COMMUNITY_TRANS_FOUR_AS << 8 | 0x02,
		.data1 = 22512,
		.data2 = 15,
	},
	{ /* 28 */
		.flags = COMMUNITY_TYPE_BASIC | (COMMUNITY_LOCAL_AS << 8) |
		    (COMMUNITY_NEIGHBOR_AS << 16),
	},
	{ /* 29 */
		.flags = COMMUNITY_TYPE_LARGE | (COMMUNITY_LOCAL_AS << 8) |
		    (COMMUNITY_NEIGHBOR_AS << 24),
	},
	{ /* 30 */
		.flags = COMMUNITY_TYPE_EXT | (COMMUNITY_LOCAL_AS << 8) |
		    (COMMUNITY_NEIGHBOR_AS << 16),
		.data3 = EXT_COMMUNITY_TRANS_TWO_AS << 8 | 0x02,
	},
	{ /* 31 */
		.flags = COMMUNITY_TYPE_BASIC,
		.data1 = 42,
		.data2 = 22512,
	},
	{ /* 32 */
		.flags = COMMUNITY_TYPE_BASIC,
		.data1 = 65366,
		.data2 = 22512,
	},
	{ /* 33 */
		.flags = COMMUNITY_TYPE_BASIC,
		.data1 = 42,
		.data2 = 65366,
	},
	{ /* 34 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 42,
		.data2 = 0,
		.data3 = 22512,
	},
	{ /* 35 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 196618,
		.data2 = 0,
		.data3 = 22512,
	},
	{ /* 36 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 22512,
		.data2 = 0,
		.data3 = 196618,
	},
	{ /* 37 */
		.flags = COMMUNITY_TYPE_LARGE,
		.data1 = 196618,
		.data2 = 0,
		.data3 = 424242,
	},
	{ /* 38 */
		.flags = COMMUNITY_TYPE_EXT,
		.data1 = 42,
		.data2 = 22512,
		.data3 = EXT_COMMUNITY_TRANS_TWO_AS << 8 | 0x02,
	},
	{ /* 39 */
		.flags = COMMUNITY_TYPE_EXT,
		.data1 = 42,
		.data2 = 22512,
		.data3 = EXT_COMMUNITY_TRANS_FOUR_AS << 8 | 0x02,
	},
	{ /* 40 */
		.flags = COMMUNITY_TYPE_EXT,
		.data1 = 196618,
		.data2 = 22512,
		.data3 = EXT_COMMUNITY_TRANS_FOUR_AS << 8 | 0x02,
	},
	{ /* 41 */
		.flags = COMMUNITY_TYPE_EXT,
		.data1 = 22512,
		.data2 = 196618,
		.data3 = EXT_COMMUNITY_TRANS_TWO_AS << 8 | 0x02,
	},
	{ /* 42 */
		.flags = COMMUNITY_TYPE_EXT | (COMMUNITY_ANY << 24),
	},
	{ /* 43 */
		.flags = COMMUNITY_TYPE_EXT | (COMMUNITY_ANY << 8),
		.data3 = (-1 << 8) | 0x02,
	},
	{ /* 44 */
		.flags = COMMUNITY_TYPE_EXT | (COMMUNITY_ANY << 8),
		.data3 = EXT_COMMUNITY_TRANS_FOUR_AS << 8 | 0x02,
	},
	{ /* 45 */
		.flags = COMMUNITY_TYPE_EXT | (COMMUNITY_ANY << 16),
		.data3 = EXT_COMMUNITY_TRANS_FOUR_AS << 8 | 0x02,
		.data1 = 196618,
	},
};

struct testfilter {
	int		 in[8];
	int		 delete;
	int		 match;
	int		 mout;
	int		 ncomm;
	int		 next;
	int		 nlarge;
	struct rde_peer *peer;
} testfilters[] = {
	{
		.in = { 1, 2, -1 },
		.match = 1,
		.mout = 1,
		.delete = 1,
	},
	{
		.in = { 1, 2, -1 },
		.match = 2,
		.mout = 1,
		.delete = 2,
	},
	{
		.in = { 1, 2, -1 },
		.match = 20,
		.mout = 1,
		.delete = 20,
	},
	{
		.in = { 1, 2, 1, 2, -1 },
		.match = 5,
		.mout = 0,
		.delete = 5,
	},
	{
		.in = { 3, 4, 5, 6, 7, 8, -1 },
		.match = 5,
		.mout = 1,
		.delete = 5,
	},
	{ /* 5 */
		.in = { 3, 4, 5, 6, 7, 8, -1 },
		.match = 20,
		.mout = 0,
		.delete = -1,
	},
	{
		.in = { 3, 4, 5, 6, 7, 8, -1 },
		.match = 21,
		.mout = 1,
		.delete = 21,
	},
	{
		.in = { 3, 4, 5, 6, 7, 8, -1 },
		.match = 22,
		.mout = 1,
		.delete = 22,
	},
	{
		.in = { 17, 18, 19, -1 },
		.match = 22,
		.mout = 0,
		.delete = 17,
	},
	{
		.in = { 17, 18, 19, 7, 8, -1 },
		.match = 18,
		.mout = 1,
		.delete = 19,
	},
	{ /* 10 */
		.in = { 1, 3, 5, -1 },
		.match = 23,
		.mout = 0,
		.delete = -1,
	},
	{
		.in = { 1, 3, 5, 17, -1 },
		.match = 23,
		.mout = 1,
		.delete = 23,
	},
	{
		.in = { 1, 3, 5, 19, -1 },
		.match = 24,
		.mout = 0,
		.delete = -1,
	},
	{
		.in = { 19, 18, 17, -1 },
		.match = 24,
		.mout = 1,
		.delete = 24,
	},
	{
		.in = { 25, 26, -1 },
		.match = 25,
		.mout = 1,
		.delete = 25,
	},
	{ /* 15 */
		.in = { 25, 26, -1 },
		.match = 26,
		.mout = 1,
		.delete = 26,
	},
	{
		.in = { 17, 0, -1 },
		.match = 0,
		.mout = 1,
		.delete = 0,
	},
	{
		.in = { -1 },
		.match = 21,
		.mout = 0,
		.delete = -1,
		.ncomm = 0 + 1,
		.next = 0 + 1,
		.nlarge = 0 + 1,
	},
	{
		.in = { 0, 3, 6, -1 },
		.match = -1,
		.delete = -1,
		.ncomm = 3 + 1,
		.next = 0 + 1,
		.nlarge = 0 + 1,
	},
	{
		.in = { 0, 25, 26, 19, -1 },
		.match = -1,
		.delete = -1,
		.ncomm = 1 + 1,
		.next = 2 + 1,
		.nlarge = 1 + 1,
	},
	{ /* 20 */
		.in = { 0, 10, 26, -1 },
		.match = -1,
		.delete = -1,
		.ncomm = 1 + 1,
		.next = 1 + 1,
		.nlarge = 1 + 1,
	},
	{
		.in = { 28, -1 },
		.match = 28,
		.mout = 1,
		.delete = 28,
		.peer = &peer,
	},
	{
		.in = { 31, -1 },
		.match = 28,
		.mout = 1,
		.delete = 28,
		.peer = &peer,
	},
	{
		.in = { 31, -1 },
		.match = 28,
		.mout = 0,
		.peer = &peer_l4,
	},
	{
		.in = { 31, -1 },
		.match = 28,
		.mout = 0,
		.peer = &peer_r4,
	},
	{ /* 25 */
		.in = { 31, -1 },
		.match = 28,
		.mout = 0,
		.peer = &peer_b4,
	},
	{
		.in = { 32, -1 },
		.match = 28,
		.mout = 0,
		.peer = &peer,
	},
	{
		.in = { 32, -1 },
		.match = 28,
		.mout = 0,
		.peer = &peer_l4,
	},
	{
		.in = { 32, -1 },
		.match = 28,
		.mout = 0,
		.peer = &peer_r4,
	},
	{
		.in = { 32, -1 },
		.match = 28,
		.mout = 0,
		.peer = &peer_b4,
	},
	{ /* 30 */
		.in = { 33, -1 },
		.match = 28,
		.mout = 0,
		.peer = &peer,
	},
	{
		.in = { 33, -1 },
		.match = 28,
		.mout = 0,
		.peer = &peer_l4,
	},
	{
		.in = { 33, -1 },
		.match = 28,
		.mout = 0,
		.peer = &peer_r4,
	},
	{
		.in = { 33, -1 },
		.match = 28,
		.mout = 0,
		.peer = &peer_b4,
	},
	{
		.in = { 29, -1 },
		.match = 29,
		.mout = 1,
		.delete = 29,
		.peer = &peer,
	},
	{ /* 35 */
		.in = { 29, -1 },
		.match = 29,
		.mout = 1,
		.delete = 29,
		.peer = &peer_l4,
	},
	{
		.in = { 29, -1 },
		.match = 29,
		.mout = 1,
		.delete = 29,
		.peer = &peer_r4,
	},
	{
		.in = { 29, -1 },
		.match = 29,
		.mout = 1,
		.delete = 29,
		.peer = &peer_b4,
	},
	{
		.in = { 34, -1 },
		.match = 29,
		.mout = 1,
		.peer = &peer,
	},
	{
		.in = { 34, -1 },
		.match = 29,
		.mout = 0,
		.peer = &peer_l4,
	},
	{ /* 40 */
		.in = { 34, -1 },
		.match = 29,
		.mout = 0,
		.peer = &peer_r4,
	},
	{
		.in = { 34, -1 },
		.match = 29,
		.mout = 0,
		.peer = &peer_b4,
	},
	{
		.in = { 35, -1 },
		.match = 29,
		.mout = 0,
		.peer = &peer,
	},
	{
		.in = { 35, -1 },
		.match = 29,
		.mout = 1,
		.peer = &peer_l4,
	},
	{
		.in = { 35, -1 },
		.match = 29,
		.mout = 0,
		.peer = &peer_r4,
	},
	{ /* 45 */
		.in = { 35, -1 },
		.match = 29,
		.mout = 0,
		.peer = &peer_b4,
	},
	{
		.in = { 36, -1 },
		.match = 29,
		.mout = 0,
		.peer = &peer,
	},
	{
		.in = { 36, -1 },
		.match = 29,
		.mout = 0,
		.peer = &peer_l4,
	},
	{
		.in = { 36, -1 },
		.match = 29,
		.mout = 1,
		.peer = &peer_r4,
	},
	{
		.in = { 36, -1 },
		.match = 29,
		.mout = 0,
		.peer = &peer_b4,
	},
	{ /* 50 */
		.in = { 37, -1 },
		.match = 29,
		.mout = 0,
		.peer = &peer,
	},
	{
		.in = { 37, -1 },
		.match = 29,
		.mout = 0,
		.peer = &peer_l4,
	},
	{
		.in = { 37, -1 },
		.match = 29,
		.mout = 0,
		.peer = &peer_r4,
	},
	{
		.in = { 37, -1 },
		.match = 29,
		.mout = 1,
		.peer = &peer_b4,
	},
	{
		.in = { 30, -1 },
		.match = 30,
		.mout = 1,
		.delete = 30,
		.peer = &peer,
	},
	{ /* 55 */
		.in = { 30, -1 },
		.match = 30,
		.mout = 1,
		.delete = 30,
		.peer = &peer_l4,
	},
	{
		.in = { 30, -1 },
		.match = 30,
		.mout = 1,
		.delete = 30,
		.peer = &peer_r4,
	},
	{
		.in = { 38, -1 },
		.match = 30,
		.mout = 1,
		.peer = &peer,
	},
	{
		.in = { 38, -1 },
		.match = 30,
		.mout = 0,
		.peer = &peer_l4,
	},
	{
		.in = { 38, -1 },
		.match = 30,
		.mout = 0,
		.peer = &peer_r4,
	},
	{ /* 60 */
		.in = { 39, -1 },
		.match = 30,
		.mout = 1,
		.peer = &peer,
	},
	{
		.in = { 39, -1 },
		.match = 30,
		.mout = 0,
		.peer = &peer_l4,
	},
	{
		.in = { 39, -1 },
		.match = 30,
		.mout = 0,
		.peer = &peer_r4,
	},
	{
		.in = { 40, -1 },
		.match = 30,
		.mout = 0,
		.peer = &peer,
	},
	{
		.in = { 40, -1 },
		.match = 30,
		.mout = 1,
		.peer = &peer_l4,
	},
	{ /* 65 */
		.in = { 40, -1 },
		.match = 30,
		.mout = 0,
		.peer = &peer_r4,
	},
	{
		.in = { 41, -1 },
		.match = 30,
		.mout = 0,
		.peer = &peer,
	},
	{
		.in = { 41, -1 },
		.match = 30,
		.mout = 0,
		.peer = &peer_l4,
	},
	{
		.in = { 41, -1 },
		.match = 30,
		.mout = 1,
		.peer = &peer_r4,
	},
	{
		.in = { 38, -1 },
		.match = 30,
		.mout = 1,
		.peer = &peer,
	},
	{ /* 70 */
		.in = { 38, 39, 40, -1 },
		.match = 42,
		.mout = 1,
		.delete = 42,
		.next = 0 + 1,
		.peer = &peer,
	},
	{
		.in = { 38, 39, 40, -1 },
		.match = 43,
		.mout = 1,
		.delete = 43,
		.next = 0 + 1,
		.peer = &peer,
	},
	{
		.in = { 39, 40, -1 },
		.match = 44,
		.mout = 1,
		.delete = 44,
		.next = 0 + 1,
		.peer = &peer,
	},
	{
		.in = { 38, -1 },
		.match = 44,
		.mout = 0,
		.peer = &peer,
	},
	{
		.in = { 40, -1 },
		.match = 45,
		.mout = 1,
		.delete = 45,
		.next = 0 + 1,
		.peer = &peer,
	},
	{ /* 75 */
		.in = { 38, 39, 41, -1 },
		.match = 45,
		.mout = 0,
		.peer = &peer,
	},
};
