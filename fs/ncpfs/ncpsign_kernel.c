/*
 *  ncpsign_kernel.c
 *
 *  Arne de Bruijn (arne@knoware.nl), 1997
 *
 */

#include <linux/config.h>

#ifdef CONFIG_NCPFS_PACKET_SIGNING

#include <linux/string.h>
#include <linux/ncp.h>
#include <linux/bitops.h>
#include "ncpsign_kernel.h"

/* i386: 32-bit, little endian, handles mis-alignment */
#ifdef __i386__
#define GET_LE32(p) (*(int *)(p))
#define PUT_LE32(p,v) { *(int *)(p)=v; }
#else
/* from include/ncplib.h */
#define BVAL(buf,pos) (((__u8 *)(buf))[pos])
#define PVAL(buf,pos) ((unsigned)BVAL(buf,pos))
#define BSET(buf,pos,val) (BVAL(buf,pos) = (val))

static inline __u16
WVAL_LH(__u8 * buf, int pos)
{
	return PVAL(buf, pos) | PVAL(buf, pos + 1) << 8;
}
static inline __u32
DVAL_LH(__u8 * buf, int pos)
{
	return WVAL_LH(buf, pos) | WVAL_LH(buf, pos + 2) << 16;
}
static inline void
WSET_LH(__u8 * buf, int pos, __u16 val)
{
	BSET(buf, pos, val & 0xff);
	BSET(buf, pos + 1, val >> 8);
}
static inline void
DSET_LH(__u8 * buf, int pos, __u32 val)
{
	WSET_LH(buf, pos, val & 0xffff);
	WSET_LH(buf, pos + 2, val >> 16);
}

#define GET_LE32(p) DVAL_LH(p,0)
#define PUT_LE32(p,v) DSET_LH(p,0,v)
#endif

static void nwsign(char *r_data1, char *r_data2, char *outdata) {
 int i;
 unsigned int w0,w1,w2,w3;
 static int rbit[4]={0, 2, 1, 3};
#ifdef __i386__
 unsigned int *data2=(int *)r_data2;
#else
 unsigned int data2[16];
 for (i=0;i<16;i++)
  data2[i]=GET_LE32(r_data2+(i<<2));
#endif 
 w0=GET_LE32(r_data1);
 w1=GET_LE32(r_data1+4);
 w2=GET_LE32(r_data1+8);
 w3=GET_LE32(r_data1+12);
 for (i=0;i<16;i+=4) {
  w0=rol32(w0 + ((w1 & w2) | ((~w1) & w3)) + data2[i+0],3);
  w3=rol32(w3 + ((w0 & w1) | ((~w0) & w2)) + data2[i+1],7);
  w2=rol32(w2 + ((w3 & w0) | ((~w3) & w1)) + data2[i+2],11);
  w1=rol32(w1 + ((w2 & w3) | ((~w2) & w0)) + data2[i+3],19);
 }
 for (i=0;i<4;i++) {
  w0=rol32(w0 + (((w2 | w3) & w1) | (w2 & w3)) + 0x5a827999 + data2[i+0],3);
  w3=rol32(w3 + (((w1 | w2) & w0) | (w1 & w2)) + 0x5a827999 + data2[i+4],5);
  w2=rol32(w2 + (((w0 | w1) & w3) | (w0 & w1)) + 0x5a827999 + data2[i+8],9);
  w1=rol32(w1 + (((w3 | w0) & w2) | (w3 & w0)) + 0x5a827999 + data2[i+12],13);
 }
 for (i=0;i<4;i++) {
  w0=rol32(w0 + ((w1 ^ w2) ^ w3) + 0x6ed9eba1 + data2[rbit[i]+0],3);
  w3=rol32(w3 + ((w0 ^ w1) ^ w2) + 0x6ed9eba1 + data2[rbit[i]+8],9);
  w2=rol32(w2 + ((w3 ^ w0) ^ w1) + 0x6ed9eba1 + data2[rbit[i]+4],11);
  w1=rol32(w1 + ((w2 ^ w3) ^ w0) + 0x6ed9eba1 + data2[rbit[i]+12],15);
 }
 PUT_LE32(outdata,(w0+GET_LE32(r_data1)) & 0xffffffff);
 PUT_LE32(outdata+4,(w1+GET_LE32(r_data1+4)) & 0xffffffff);
 PUT_LE32(outdata+8,(w2+GET_LE32(r_data1+8)) & 0xffffffff);
 PUT_LE32(outdata+12,(w3+GET_LE32(r_data1+12)) & 0xffffffff);
}

/* Make a signature for the current packet and add it at the end of the */
/* packet. */
void __sign_packet(struct ncp_server *server, const char *packet, size_t size, __u32 totalsize, void *sign_buff) {
	unsigned char data[64];

	memcpy(data, server->sign_root, 8);
	*(__u32*)(data + 8) = totalsize;
	if (size < 52) {
		memcpy(data + 12, packet, size);
		memset(data + 12 + size, 0, 52 - size);
	} else {
		memcpy(data + 12, packet, 52);
	}
	nwsign(server->sign_last, data, server->sign_last);
	memcpy(sign_buff, server->sign_last, 8);
}

int sign_verify_reply(struct ncp_server *server, const char *packet, size_t size, __u32 totalsize, const void *sign_buff) {
	unsigned char data[64];
	unsigned char hash[16];

	memcpy(data, server->sign_root, 8);
	*(__u32*)(data + 8) = totalsize;
	if (size < 52) {
		memcpy(data + 12, packet, size);
		memset(data + 12 + size, 0, 52 - size);
	} else {
		memcpy(data + 12, packet, 52);
	}
	nwsign(server->sign_last, data, hash);
	return memcmp(sign_buff, hash, 8);
}

#endif	/* CONFIG_NCPFS_PACKET_SIGNING */

