#include "includes.h"
#include "ecc.h"
#include "hmac.h"

#define IWN_PUBKEY_INTLEN  24
#define IWN_PRIKEY_INTLEN  48

typedef unsigned int DWord;
typedef unsigned char BYTE;

#define MINTLENGTH    13
typedef struct {
	int length ;
	DWord value[MINTLENGTH];
	int Field_type;
} MInt;


static void sha256_digest_int(const void *msg, unsigned len, MInt *d)
{
	unsigned char tmp[64] = {0};
	unsigned i;
	mhash_sha256((unsigned char*)msg, len, tmp);

	for (i=0; i<8; i++)
	{
		unsigned char* p = tmp + 4*i;
		d->value[7-i] = (p[0]<<24) + (p[1]<<16) + (p[2]<<8) + p[3];
	}
	d->length = 8;
}


/*****************************************
*ECC  
******************************************/

#ifndef max
	#define max(a,b) a>b?a:b
#endif

#ifndef min
	#define min(a,b) a<b?a:b
#endif



#define MASK 0xf
#define READ_FILE       52  

#define MAX_CMP_WORD         (DWord)0xffffffff
#define HalfWord unsigned short
#define HALF_WORD_BITS   16
#define BITS_PER_BYTE    8
#define CMP_WORD_SIZE        (sizeof (DWord)*BITS_PER_BYTE)

#define MAX_CMP_WORD         (DWord)0xffffffff
#define MAX_CMP_HALF_WORD    0xffff

#define LOW_HALF(x) ((x) & MAX_CMP_HALF_WORD)
#define HIGH_HALF(x) (((x) >> HALF_WORD_BITS) & MAX_CMP_HALF_WORD)
#define TO_HIGH_HALF(x) (((DWord)(x)) << HALF_WORD_BITS)



#define ONE        1               
#define MINUS_ONE  3               

/* #define MINTLENGTH    13 */
#define POINTTABLELEN 128
#define PARABYTELEN   24
#define PARABUFFER    6
#define MI_NEGATIVE     2
#define FP_SQUARE_ROOT    101 
#define OUTPUT_SIZE  5
#define OUTPUT_LEN   6
#define FP_IsZero(ptr) (((((ptr)->length == 1) && ((ptr)->value[0] == 0)))|| ((ptr)->length==0))
#define FP_Equal(operand1, operand2) (! MI_Compare (operand1, operand2))
#define FP_Move(source,destination) MI_Move(source,destination)
#define MI_BITS_PER_BYTE    8
#define MI_BYTES_PER_WORD   (sizeof (DWord))
#define MI_WORD_SIZE        (sizeof (DWord) * MI_BITS_PER_BYTE)
#define MI_WordToMInt(srcWord,destInt) {if(srcWord==0)	(destInt)->length=0;else	(destInt)->length=1;	(destInt)->value[0]=srcWord;}
#define MI_WORD_2MSB(word) ((word) >> (MI_WORD_SIZE - 2))



typedef struct {
	int  isInfinite; 
	MInt x;          
	MInt y;          
} EcFpPoint;

typedef struct {
	MInt x;          
	MInt y;          
	MInt z;          
}EcFpPointProject;
#define FPSqr_Mul(a,p,product) FP_Mul(a,a,p,product);


typedef	struct {
	MInt P;
	MInt A;
	MInt B;
	MInt seed;
	EcFpPoint BasePoint;
	MInt Order;
	MInt cofactor;
} EllipticCurve;
    
static int randseed;
static EcFpPoint pTable1[POINTTABLELEN];
static EllipticCurve TheCurve;

/* 64位乘法优化*/
#ifdef IWN_ECC_GCCINT64
#define DWordMult(product_64, v1, v2)		\
	do									\
	{									\
		unsigned long long val = 0;			\
		val = ((unsigned long long)(v1)) * ((unsigned long long)(v2));	\
		product_64[1] = (DWord)(val >> 32);	\
		product_64[0] = (DWord)(val);			\
	}while(0)
#else
static  void DWordMult (DWord a[2],const DWord b,const DWord c)
{
  register DWord t, u;
  HalfWord bHigh, bLow, cHigh, cLow;

  bHigh = (HalfWord)HIGH_HALF (b);
  bLow = (HalfWord)LOW_HALF (b);
  cHigh = (HalfWord)HIGH_HALF (c);
  cLow = (HalfWord)LOW_HALF (c);

  a[0] = (DWord)bLow * (DWord)cLow;
  t = (DWord)bLow * (DWord)cHigh;
  u = (DWord)bHigh * (DWord)cLow;
  a[1] = (DWord)bHigh * (DWord)cHigh;
  
  if ((t += u) < u)
    a[1] += TO_HIGH_HALF (1);
  u = TO_HIGH_HALF (t);

  if ((a[0] += u) < u)
    ++ a[1];
  a[1] += HIGH_HALF (t);
 
}
#endif


/* 64位除法优化*/
#ifdef IWN_ECC_CPU_DIVISION64
#define CMP_WordDiv(val_p, dividend_64_p, divisor)		\
   *(val_p) = (unsigned int)(((((unsigned long long)(dividend_64_p)[1])<<32) |(dividend_64_p)[0]) / (unsigned long long)(divisor))
#else
static void CMP_WordDiv (DWord *a,DWord b[2],DWord c)
{

  DWord t[2], u, v;
  HalfWord aHigh, aLow, cHigh, cLow;

  cHigh = (HalfWord)( (c >>16) & 0xffff);
  cLow = (HalfWord)( (c&0xffff));

  t[0] = b[0];
  t[1] = b[1];

  if (cHigh == MAX_CMP_HALF_WORD)
    aHigh = (HalfWord)HIGH_HALF (t[1]);
  else
    aHigh = (HalfWord)(t[1] / (cHigh + 1));
  u = (DWord)aHigh * (DWord)cLow;
  v = (DWord)aHigh * (DWord)cHigh;
  if ((t[0] -= TO_HIGH_HALF (u)) > (MAX_CMP_WORD - TO_HIGH_HALF (u)))
    -- t[1];
  t[1] -= HIGH_HALF (u);
  t[1] -= v;

  while ((t[1] > cHigh) ||
         ((t[1] == cHigh) && (t[0] >= TO_HIGH_HALF (cLow)))) {
    if ((t[0] -= TO_HIGH_HALF (cLow)) > MAX_CMP_WORD - TO_HIGH_HALF (cLow))
      -- t[1];
    t[1] -= cHigh;
    ++ aHigh;
  }

  if (cHigh == MAX_CMP_HALF_WORD)
    aLow = (HalfWord)LOW_HALF (t[1]);
  else
    aLow = 
      (HalfWord)((TO_HIGH_HALF (t[1]) + HIGH_HALF (t[0])) / (cHigh + 1));
  u = (DWord)aLow * (DWord)cLow;
  v = (DWord)aLow * (DWord)cHigh;
  if ((t[0] -= u) > (MAX_CMP_WORD - u))
    -- t[1];
  if ((t[0] -= TO_HIGH_HALF (v)) > (MAX_CMP_WORD - TO_HIGH_HALF (v)))
    -- t[1];
  t[1] -= HIGH_HALF (v);

  while ((t[1] > 0) || ((t[1] == 0) && t[0] >= c)) {
    if ((t[0] -= c) > (MAX_CMP_WORD - c))
      -- t[1];
    ++ aLow;
  }
  
  *a = TO_HIGH_HALF (aHigh) + aLow;

}
#endif



static int MI_RecomputeLength (int, MInt *);
static int MI_Compare (MInt *firstMInt,MInt *secondMInt);
static  int MI_Add (MInt *addend1,MInt *addend2,MInt *sum);
/* static  void DWordMult (DWord a[2],const DWord b,const DWord c); */
static  int MI_Subtract (MInt *minuend,MInt *subtrahend,MInt *difference);
static int DW_AddProduct (DWord *a, DWord *b, DWord c, DWord *d, unsigned int length);
static int MI_Multiply(MInt *multiplicand,MInt *multiplier,MInt *product);
static DWord CMP_ArraySub (DWord *a,DWord *b,DWord *c,  unsigned int length);
static DWord CMP_ArrayLeftShift (DWord *a,unsigned int bits, unsigned int length);
static DWord CMP_ArrayRightShift (DWord *a,unsigned int bits,
									unsigned int length);

/* static void CMP_WordDiv (DWord *a,DWord b[2],DWord c); */
static DWord CMP_SubProduct (DWord *a,DWord *b,DWord c,
							 DWord *d,unsigned int length);
static int CMP_ArrayCmp (DWord *a,DWord *b,unsigned int length);
static int MI_RecomputeLength (int targetLength,MInt *theInt);
int FpMinus (MInt *operand,MInt *prime,MInt *result);
static int FP_Add (MInt *addend1,MInt *addend2, 
				MInt *modulus,MInt *sum);
static int FP_Substract (MInt *minuend,MInt *subtrahend,
					 MInt *modulus,MInt *difference);
static int GenRandomNumberForFixLen(int wordLen,MInt *theInt);
static int MI_Divide (MInt *dividend,MInt *divisor,
			   MInt *quotient,MInt *remainder);
static int FpDivByTwo(MInt *a,MInt *p);
static int FP_Invert (MInt *operand,MInt *modulus,MInt *inverse);
static int FP_Div(MInt *op1,MInt *op2,MInt *prime,MInt *result);
static int GenRandomNumber (MInt *theInt, MInt *maxInt);
static int FP_MulNormal(MInt *multiplicand,MInt *multiplier,
		   MInt *p,MInt *product);
static int OctetStringToMInt (const unsigned char *OString, unsigned int OSLen,
					   MInt *destInt);
static int JointSFKL_Encode(MInt *k, MInt *l,unsigned char *JSF);
static int MIntToOctetString (MInt *srcInt,unsigned int OSBufferSize,
					   unsigned int *OSLen, unsigned char *DString);
static int FP_Mul(MInt *multiplicand,MInt *multiplier,MInt *p,MInt *prod);

static int ECFpAddProj (EcFpPointProject *addend1,
						EcFpPoint *addend2,
						MInt *a,MInt * b,MInt *prime,
						EcFpPointProject *result);

static int ECFpAdd (EcFpPoint *addend1, EcFpPoint *addend2,
			 MInt *a, MInt *b,MInt *prime,EcFpPoint *sum);
static int MIntToFixedLenOS(MInt *srcInt,unsigned int fixedLength,
				unsigned int OSBufferSize, unsigned int *OSLen,
				unsigned char *DString);
static int ECFpKTimes (EcFpPoint *operand,MInt *k,MInt *a,MInt *b,
				MInt *prime, EcFpPoint *result);
static int CanonicalEncode (MInt *source, MInt *destination);

static int ECFpKTimes_FixP (EcFpPoint *operand,EcFpPoint *Table1,
				 MInt *k,MInt *a,MInt *b,
				MInt *prime, EcFpPoint *result);
static int ECFpDoubleProj (EcFpPointProject *operand,MInt *a,MInt *b,
					MInt *prime,EcFpPointProject *result);
static int ECFpKPAddLQs(EcFpPoint *P,EcFpPoint *Q,MInt *u1,MInt *u2,
				 MInt *a, MInt *b,MInt *prime,EcFpPoint *result);
static int EcFpPointToPoint(EcFpPoint *pt1 ,Point *dest);
static int PointToEcFpPoint(const Point *sour,EcFpPoint *dest );
static int EcFpPointToFixLenOS(EcFpPoint *Q,unsigned int fixedLength,
				unsigned int OSBufferSize, unsigned int *OSLen,
				unsigned char *DString);
static int ECES_Format(int len,const unsigned char *OString, unsigned int OSLen,
			unsigned char *DestOString, unsigned int *DestLen);
static int ECES_DeFormat(unsigned char *OString, unsigned int OSLen,
			unsigned char *DestOString, unsigned int *DestLen);

static int  gettalbe(void);

static int MI_Move (MInt *source,MInt *destination)
{
	int i;
	destination->length = source->length;
	for(i=0;i<source->length;i++)
		destination->value[i]=source->value[i];
	return 0;
}
static int MI_Compare (MInt *firstMInt,MInt *secondMInt)
{
  int i;
  
  if (firstMInt->length > secondMInt->length)
  {
	  if((firstMInt->length==1)&&(firstMInt->value[0]==0))
		  return 0;
	  else
		  return (1);
  }
  else if (firstMInt->length < secondMInt->length)
  {
	  if((secondMInt->length==1)&&(secondMInt->value[0]==0))
		  return 0;
	  else
		  return (-1);
  }
  else {
    for (i = firstMInt->length - 1; i >= 0; -- i)
      if (firstMInt->value[i] > secondMInt->value[i])
        return (1);
      else if (firstMInt->value[i] < secondMInt->value[i])
        return (-1);
  }
  return (0);
}
static  int MI_Add (MInt *addend1,MInt *addend2,MInt *sum)
{
  DWord *a, carry, *longValue, *shortValue, word;
  int i, max, min;
  
  if (addend1->length > addend2->length) 
  {
    max = addend1->length; min = addend2->length;
    longValue = addend1->value; shortValue = addend2->value;
  }
  else {
    min = addend1->length;  max = addend2->length;
    longValue = addend2->value;  shortValue = addend1->value;
  }
  do {
    carry = 0;
    a = sum->value;
    for (i = 0; i < min; ++ i) 
	{
      if ((word = longValue[i] + carry) < carry) {
        carry = 1;
        word = shortValue[i];
      }
      else if ((word += shortValue[i]) < shortValue[i])
        carry = 1;
      else 
        carry = 0;
      a[i] = word;
    }
    for (i = min; i < max; ++ i)
      if ((a[i] = carry + longValue[i]) < carry)
        carry = 1;
      else
	  {
        carry = 0;
        ++ i;
        memcpy (&a[i], &longValue[i], 
          sizeof (DWord) * (max - i));
        break;
      }
    
    if (carry == 1) {
      a[max] = 1;
      sum->length = max + 1;
    }
    else
      sum->length = max;
  } while (0);  
  
  return 0;
}


#define BN_UMULT_LOHI(low,high,a,b)	\
	__asm__ __volatile__ ("mul	%3"		\
		: "=a"(low),"=d"(high)	\
		: "a"(a),"g"(b)		\
		: "cc");


static unsigned int pTablexy[POINTTABLELEN*2*6]={
0xe4106640,0x2c836dc6,0x5e4d4b48,0x51236de6,0x8de709ad,0x4ad5f704,0x32db27d2,0x14b52704,0x4ca3a1b0,0xae24817a,0xd4aaadac,0x2bb3a02,
0xe4106640,0x2c836dc6,0x5e4d4b48,0x51236de6,0x8de709ad,0x4ad5f704,0x32db27d2,0x14b52704,0x4ca3a1b0,0xae24817a,0xd4aaadac,0x2bb3a02,
0x3f7bb205,0xfc3a867,0xff49e4d1,0xff0fb5db,0xa6327114,0x3648676c,0x7e722f1d,0xc0b859e2,0x228dee21,0x7a835669,0x9aeac15,0x69ea11c4,
0xc083755b,0x9575039e,0xd19021fb,0x86fc1ee7,0x4f7f2d79,0x5c66f2e4,0x1913fe5d,0xadada2c,0xa62ce5c4,0xd0cbc73f,0xf9bf90b3,0xff51d7c,
0x58440b0f,0x89b77829,0x60e472b6,0x71c71d5c,0x1dd4fcee,0x7bc754ea,0xd9c6cd0c,0xefd71dbc,0x3c91e639,0x3072f469,0xa49f0a0c,0x2e6d2edb,
0x61ab09e1,0xefcbf980,0x3b587515,0xdb7ef142,0xe913707b,0x4e301f80,0x4819a485,0xf61d06b8,0xd3c9e0a7,0x4d46a380,0xfd67e447,0x1edc45a6,
0xf8455ed9,0x57858a9d,0x847c305d,0x86ac7fe9,0x7221ab8d,0x3a7d3ee0,0x934ded95,0x9380c140,0xc90e65ad,0x46791837,0x809fcf99,0x71fc4e1a,
0x99f80457,0x97921090,0xf6ce5c4e,0xc7dbeb44,0x51eb86f8,0x54e97ba5,0x87002d42,0xfc5e03a5,0x358d1bcc,0x62cf9432,0x141119d0,0x2bd3db07,
0x70ae2e0a,0x13253d5c,0xe0790007,0xef0ff820,0x5f24d6b0,0x7c60cb10,0xbd63554e,0xd80f43c9,0x1e011140,0x4dbb36bc,0x4f6c8585,0x9a96a584,
0x84ab6942,0xdc87c83e,0x962dea92,0x30fc3781,0x9bff1b12,0xa93d9be5,0x70d3abcd,0x22174968,0x3a04b5a,0xb05742af,0x7344f456,0x9d46415a,
0x4907881b,0x9dd6bb,0x6bdeef9d,0x5170c88a,0x45144b88,0x4497f4d8,0xd548045f,0x6ea60853,0xc5f79d7a,0x3491287e,0xa322874f,0x4d25fb15,
0x5b9ad258,0xd196827f,0xe6283c6a,0xf8a8d9fc,0x6c5a8c83,0x1cc52aa0,0xd2018f09,0x77aa892b,0x6d1a329c,0x2567bf3e,0x7bfab35d,0x330c1f30,
0x155ddcbd,0x7df358cd,0x63b6ac29,0x52612204,0x460bdb5,0x9138aeaf,0xe322ec64,0x299e10aa,0x69dc1df3,0x10ca527,0xc26c80e1,0xa14cffad,
0xc5a354a6,0x2548112a,0xb56fb78f,0x3146ddaa,0x71ce732f,0x79f88c6d,0xca26652d,0x2a5bae07,0xdfe19cb9,0x271bcf86,0xa90917b3,0xa0ba40fd,
0x374698df,0xf540f20b,0xbd5575dc,0xa537b968,0xe3c007f,0xb48fe3bb,0xefb948b0,0xf40c3aee,0x3f93e2f1,0x64912f82,0x3f931bd3,0x1b29a420,
0xcec4c996,0x15920986,0x2ba1d8bb,0xda4ae229,0xe58e2c9f,0x942074a0,0xa0fdd8f6,0xc2c68157,0x483983c7,0x8f3b0ea5,0xbc98cf5d,0x6f46c447,
0xbc4bb116,0x8e0c7c44,0x46a012b0,0x5c8b6741,0x7dbcd7c9,0x81dea576,0x1bc89029,0x2857f450,0x9d42bddc,0x223c9d23,0x17a2eb51,0x311dd4b9,
0x1de50b35,0x2fca1a1,0xe2dc926a,0xd0ac730a,0xc6e150fe,0x64494fda,0xf3aa9832,0x81b5c5b9,0xc4851acd,0x9763b0dd,0x79bb9ce8,0x2d15c19c,
0xb47c2d54,0x732b64b6,0x55a1bcc2,0x4e96f6f9,0x6188ef8f,0x842fd54e,0x2c6d279a,0x1061de32,0xeb6dece5,0xac4b95b6,0xd26e1d56,0x80e4210a,
0x9c10e25b,0x8c771c60,0xb48e7a3f,0x161b27d4,0x10336b72,0x7eaaaae,0xb19bf364,0x3bace00d,0x2c3fc3de,0x65655b3e,0x5e2fe3cb,0x90248d14,
0xe16090e7,0xbaed1a64,0x2f9b2c98,0x543a9da8,0x6d819d0f,0x28733f3f,0x8d483b44,0x15e76301,0x4c85d02f,0x1d5a2261,0xc12c25aa,0x9f37c31,
0x9ebddd39,0xa928da31,0x2ccf3a73,0x3ff34ba,0xad8388b5,0xf74bb35,0xa4c24b50,0x4b212dfa,0xce76ea5d,0xe38b2cdb,0xe2b1b27,0x734f3401,
0x6920df0e,0x9fd8d302,0xc5094278,0x9a55d7e6,0x1ff90153,0x10869974,0xbb6ba084,0x170f9616,0x6c936176,0xba544c51,0xe6254c00,0x191d9b0c,
0x19db3543,0xc1560786,0x4ce20157,0x30f09787,0xafca1f07,0xcaa2555,0xe2eb8366,0x82a8b368,0x1fa53c8c,0x4343b2f9,0xb32f9448,0x8aa96fbc,
0x3fa051ca,0x983f48b,0x4099aa14,0x792e2fbc,0xdc732c18,0x89a112f2,0x10f6a9e6,0xae5c64a9,0x8bf4533f,0xdf0a9af4,0x975b076f,0x53db7775,
0x7050b9d9,0xb13496bb,0xfd0dfab1,0x6d8bfd3e,0xc753b034,0x7bfa93a3,0x417a7cfb,0xd9ad2ae0,0x1e540f10,0xeee3e661,0x59081002,0x86dc346c,
0x850c5673,0x9a7c08f,0xda4d5007,0xebad1a8d,0x8e3d05f8,0x4ba47df3,0x197a79fa,0xebc568a1,0xe6d88a36,0x17efd62d,0x93071ffe,0x47a031a1,
0xda4750f,0xa157256,0x34399a9d,0x5f236879,0x9ec49d,0x618687ea,0xd3adc70d,0x6bd13d80,0xafe6b65a,0x788ed11,0x9142a8e2,0x9ef52f98,
0xc2e3b88f,0xdc03fbe5,0x80a92059,0x3f916b3f,0x22db6bda,0xb72f1afb,0x67880a13,0xc1081ef1,0x330ae439,0x40cf3e2e,0x9d8c2523,0x7e76afe0,
0x451f34a9,0x745bf978,0xd910a086,0xcf5722fb,0x5463e48d,0x9308da4f,0x49e3fb1e,0xae4c1ebf,0x42874193,0x9078276b,0x8e1b129e,0xe453701,
0x9f8bf79f,0x41c4a096,0xa7577208,0xf7fd86a6,0x375897e1,0x68156d7,0xf064fee4,0x2be5465,0x59a084a2,0x991d9b3d,0xe65305a6,0x1792c167,
0x1dd2f3b2,0x8b39e173,0x146d7cab,0xeb6d946c,0xe86a79b1,0x58987c60,0xb6e97f16,0x3b5eeb53,0x7c86306c,0x6ebb080d,0x1c66ff5,0x920fc81a,
0x86adb8c2,0x2015bcc2,0xfa587163,0xe3ac93ba,0x84de53f5,0x5a6cc771,0xe7e3a56a,0xe174fd4a,0xa20e88b9,0x44cc08d6,0xce0cc74b,0x3b97c45a,
0xab55c57,0x211400fe,0x12dddf80,0x2491393f,0xd4eb6fb7,0x59567387,0xa50f30ca,0x2b58a2af,0x9495bf72,0x48c4e70d,0x9fc333d2,0x24b6c6b7,
0x675f2d0a,0x911710c3,0xc9681627,0x31e8423,0x1eebd29f,0xba8f3111,0xc6356513,0xb21a25b6,0xe8efee1d,0xbb4c46ac,0x76e0183a,0xb0c2ef5f,
0xa5e95d97,0xff9c25ed,0x9041c655,0x52aa736,0x89c4901f,0xbb7c0ec3,0x6ce396ca,0x52969b69,0xa0db46f5,0xb22cbbd,0x7e188ee7,0x77eec0a4,
0xa12767a5,0x2fc79e1b,0x70dfc0d0,0x4e1e0280,0xf63046da,0x9c4fbe6,0x35439674,0xb136c956,0x224f1068,0x93ce2fdf,0xd5672dc5,0x3661bb4a,
0x4450e49b,0xff061e48,0xe2c45575,0x488c3d78,0xd70c4adc,0x1a6a5d55,0x62888cc0,0x50f973e9,0x4f7331b6,0xed7745de,0xdcb31369,0x2a455c60,
0x25565d7f,0x4e14c1f,0xb9b662c3,0x2707974e,0x6628b528,0x2c43917c,0xe8ce6315,0xc7e13b63,0x867ec662,0x5006c7eb,0xf8578be4,0x46fea495,
0x39ee1f95,0x10cca5c4,0x5dbbc7f1,0x127c2d58,0x147ce09f,0x8f670404,0x4f93ad25,0xee900c3f,0xd70af0ca,0xf33dc1b,0xdb30bd38,0xa5e87919,
0x15d6da63,0x3ac0ebc0,0x863f59a8,0x27d51aa6,0xd4744809,0x9280841b,0xbba46994,0x9d913e7f,0x51e4840a,0xbecc3d03,0x7d557b6e,0x1f942f68,
0x3fb0c0c,0x4a2731e1,0x62b46d15,0xe161dfcf,0xd0022c55,0x77621f00,0x15cc14a3,0x27e06c61,0xaca5f841,0x638553b2,0x6b93feca,0xb1fa0082,
0x5fc13949,0xc3d5d9fa,0x701ef953,0xbb5d0c4c,0xccc203cb,0xafea5073,0x93a99af6,0xf449f47,0xbdefb0c3,0xd721b2b8,0x7e307c59,0x1886ee5,
0xa20b0695,0x37990e7b,0x35461baa,0xacb1aa66,0x2b577831,0x5ca3854f,0x8760aca6,0xe647debc,0x9e97167,0xc4f4ea68,0xb0588c11,0x21442260,
0x2fc5d72b,0x9713abfd,0x1a2e593,0x63dc8546,0xf4c7c089,0x6ae40728,0x50d314fe,0x247f6c01,0x88e168e6,0x2916af10,0x3fb85cde,0x3d23a028,
0x79df16f5,0x2e5acf18,0xcbc69a03,0x2eda7756,0xa9939da8,0x73939950,0xe91a7e10,0x10985c6f,0x68a339e4,0xcc1c068c,0x693dc1b0,0x4c62bc6a,
0x5bfa5651,0xe34a970e,0x3d656559,0x87835aef,0xb0e54460,0xb0d7b56b,0x89629205,0x15ca6e5c,0x5e88bc2e,0xe0267a95,0x6b99800b,0x83fbb27d,
0xd26c70be,0x201699e6,0xf9ac6da5,0x48938eea,0xfa1f19b7,0x2676ec6b,0x8278991f,0x939a037d,0x38988363,0xc4855d42,0x7ab2b88b,0x2a6d8efa,
0x4c7cc7b0,0x6da4f875,0x395b0b73,0x3dbab139,0x5be53723,0x63329a57,0xd29e2ef,0xec6de2dc,0xc6779920,0x5dab32ae,0x957382eb,0x2f5fe04,
0x3fb27699,0x2496ede6,0xed8d8be9,0x6a6f2bc8,0x903382c8,0x44cb61d,0x9c688d97,0x819c4e92,0xaca441c3,0x8a63532,0xa4230992,0x747ee636,
0xe26415ff,0xf161d738,0x436e2f8c,0xc26c14,0xd3abeb61,0x169a72b6,0x9d47a942,0x871df996,0x4bfc543,0xa2133626,0x3d6a2522,0x42e35f31,
0xa86f50d0,0x9eeda927,0xf101647f,0xe7bcd2,0xc530fc7a,0xb6ceffb5,0x4b813550,0x2e7729af,0x4c7355,0xfca82c77,0xb005afa9,0x220ca78d,
0xa1bfa21a,0x93a15d89,0x8874f896,0x889be1ed,0x2ccd9213,0x64f13391,0xec8c22b1,0x59552741,0x97d3416e,0xbfa5c77c,0x7f124166,0x6da061f5,
0xe27dac62,0xef5b5ba7,0xc9c64bf2,0x428e1fb3,0x27478616,0xa464a590,0xd206c823,0x4ca43463,0x7519ca7c,0xd0de2f14,0x7c97743e,0x4e72b22e,
0xd706c08c,0xbe6e50ba,0xc6a5a0d8,0x4b95e42b,0x60e168fc,0x7f4bac34,0x4495b0e,0x8b7108f8,0x40f9cbf2,0x79bb166e,0xf1aff93e,0x842703fc,
0xb2164b65,0xb6ac89f9,0x7d0d10,0x9b457fdb,0xb4cef337,0x443bc470,0xe1a5b288,0x9d58a0c2,0x9c9b7f19,0x8db8191c,0x727cc627,0x26b21c69,
0xb6a1ec1a,0x7f7f4241,0x6c4d7d65,0xde9fedeb,0xb0354e10,0x31bc1e64,0xcd1da919,0xc6f76f2e,0x69b46e95,0x751aa9db,0x82b045ad,0x31c07561,
0x474125bd,0x20f56c0b,0xc2d337de,0xe344753,0x852b9743,0xa119efd0,0x9b9384e4,0x419d56f8,0x5060d699,0x5c228ba7,0x8fc276de,0x87abf05e,
0x467d7871,0x7218c898,0x152aa402,0xb315b75,0xc4eb1b3,0xb7e318a5,0x95a34767,0x68b17b95,0xa2591f87,0xbd1ec49b,0xfd0a22a1,0x42c13d34,
0x8cec4d0d,0x7d324483,0xcafa20f1,0xcf63de0c,0xcb25d45d,0x42a720da,0xe5395c70,0xbcfd42f5,0x76ab7165,0x30340b9c,0x3f1667b1,0x5b18e651,
0x8b13d25b,0x53aedec0,0x33a1579f,0x56747172,0x508db1ae,0x47eb52b2,0x7dccd4fe,0x1b8dc748,0x3166f502,0xec67da31,0x1a89d98f,0x3f4774b0,
0xfbd3ed1a,0x92b95865,0xdd496bd3,0x25c786a4,0xe9cd61e7,0xa634b6fd,0x9617c63a,0x4b13ed87,0x5cef903c,0xf2b446c,0xbbd97273,0x249bd597,
0xa26cbca2,0xa35813d2,0x2782c9c0,0x95a7f77c,0x21e13659,0x48a2296c,0xa430d90b,0x1e33a345,0x668d6286,0x3f8ea639,0x88749d39,0x48679550,
0x25121b58,0xd642126a,0xfee7b153,0x7aded183,0x82a34ffd,0xac95c0b0,0xefc09969,0xc1ff70f6,0x16754a55,0xa0d15b0d,0x129062ae,0x436eb49e,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0xeb8e087e,0xac820c4b,0xf3623cc0,0x1df005ed,0x15a97b8d,0xa704ca4b,0x547ba7f8,0xbb5b9b2,0xf6cb3009,0x31c20c65,0x7ca0634,0x19ccb36b,
0xc1a129e1,0xce4e96f6,0x2db660c9,0x3c144ec7,0xb11a8d4c,0xb0d11b23,0xa229ef37,0x4ff63369,0x8eb215e0,0xf144c6ca,0x7fbe6ec9,0x5d1ffe9a,
0x5e8bacfb,0xc5e3845b,0x32a692b6,0x744c5807,0x351af0f7,0x5a4c754d,0x9244e529,0xb7716fcb,0xa08b75db,0x82a3ed20,0xae99b834,0x53da4ba1,
0x7005a6fe,0x42bb79aa,0xe03fec15,0xd6862848,0xda9d963c,0xa86f79a7,0x2ce31b8b,0xa20a2ad1,0xc63849c,0x74563262,0x6ba4e1c2,0xbc3c2dee,
0xf08c8a44,0x80148350,0xaf353c33,0x606e6108,0x87a836ca,0x87d58bdc,0x57d4115f,0xed7c9dcc,0xa09fa815,0xcaa27435,0xb76b1a42,0x372e9755,
0x969e434a,0xcda75748,0x4c5c05aa,0x2760718,0x8ff21152,0x309580e2,0xd1150c60,0xf72882c4,0x54d5a499,0xbebb3e29,0xfbb7d10d,0x334aa103,
0xeea86f99,0xa8d8e3e9,0xedb107b9,0x53d975c2,0x9d2d7132,0x50f2c1fc,0x45aa35ee,0xb7c85d5f,0x87a99f18,0xd136e91,0x7d5de720,0x7a03b038,
0xcee60fb5,0x1a8db28,0x360a135a,0x83ac025d,0x8d85f912,0xea6d0cf,0x7613a523,0x7c935be4,0xd1c7846,0xc13a792b,0xa58dc824,0x2715d425,
0xc34fce69,0x6103b978,0x520875e0,0x7cc5c850,0x3a4d29a0,0x54384f4d,0x54b6f2cf,0xc32265b2,0xf34ed161,0x63dc4fe3,0xae2aa9a0,0x28e8b4cf,
0xec610dc3,0xa385c787,0xb3692417,0x68590271,0xb72a2c15,0x88f25840,0xa35f5d24,0x980ae747,0x790affad,0x8251da4e,0xfafc2083,0x7881f2c9,
0xc518809b,0x78f4ced4,0xde5b6ef6,0xcf7590b2,0x3e96ac85,0x88850f05,0x4b83d3ac,0xc27d9358,0x5d845038,0xc33c07de,0x7a52dd82,0x14da0c01,
0xd5849ef6,0xf7ffc68c,0x6d9e4394,0x5b1be41c,0xb2913135,0x7998a704,0xf8bc3a81,0x67ba625b,0x81bfc872,0xf89c625d,0xb18712ef,0xa43d0d58,
0xf791b123,0xcda8a439,0xfa005e44,0x9a8bc7f1,0xc365778d,0x86737335,0x868d3621,0xa3198b96,0x163f3050,0x20efaf8b,0x75d356c1,0x5eafe71f,
0xaad6f3da,0xdf1b7125,0xbf9d0cbf,0xacfd449a,0x637d35d8,0xbc79f3f9,0x1d090189,0xd5492894,0x63f4a80e,0x58919fbd,0x1af847e8,0x1bc49dbf,
0xd67fb1ec,0xe7ee9a1f,0x857ae056,0xee56963b,0x23806b37,0x34a9f92b,0x2fea0ff4,0x624aec8,0x1eee8f64,0xc2c05ef4,0x5845e762,0x84825231,
0xa9020e3d,0xd48c955,0x7b21a0c7,0xfdb52a7c,0x40a7f540,0xac09c5c5,0x342989a0,0x1d66f6c2,0xc040ae82,0xc0170470,0x6c70683a,0x887be7ba,
0xc9dd0061,0xe336dd01,0x159f65d5,0xba6d0185,0xbed226e0,0x3fe2bf60,0xdb69e3e4,0xe1c23c73,0x2f2b23cf,0x71b3abac,0x97f31c5f,0x971c7c47,
0x584c0279,0x63daca3a,0x33bcdb3f,0x4c47f5bc,0x7e496fc6,0x9fd52b98,0xd56bf07d,0x45434f1e,0xf8aa77de,0xc69f6d2a,0x21701980,0x4c39098f,
0x3073dd99,0xac6faf9,0xae019711,0x9f1e6ac8,0xaca0ea38,0x3d6dc746,0x92ed6267,0x3ece1ca4,0x6282297e,0x95f0d778,0x1ebe20cf,0x479299a9,
0xd0873121,0x88295615,0x2b84d9aa,0xdc6e9f2b,0xd83c9fc5,0x6555a700,0x620307b0,0x5372a7a6,0xe8009a69,0xf3f47b9c,0x65f55310,0x61075fa6,
0x96c8c672,0x76ffc0d0,0xc5a944fe,0xfeafa47e,0xc6c35e62,0x4aa4c8e7,0x152eac7f,0xcc4338,0xf13d8428,0xc7d0ebc4,0xfaf8898b,0x47587b89,
0x476035f4,0x3fd15567,0x1d22f64b,0xc0bb2d6d,0x1426a32c,0xa975ed71,0xe4ce3826,0xe5ca1aef,0x55e4b7e8,0xaceb2646,0xde28d8f,0x954cd542,
0x7b043620,0xd942357c,0xafb0b5ac,0x1b935073,0xb4e6ad6d,0x233397c7,0x3b689196,0x4dd67c79,0x30113d9b,0xa8426352,0xc696c40e,0x65293664,
0xbe18f1f7,0x4afd7707,0x3fbf2161,0x6b433fb9,0xb02bb087,0x89a5702f,0x34ba6bae,0xcd41a1dc,0xec495f55,0xf0e4e1dc,0xfc1136e,0x96b704c,
0xcd0747c,0xbb1a918b,0xe0c83ce0,0x98fa38ef,0xa8fbe9e8,0x4f3bb89b,0xe2d2493b,0x1ec217bf,0x44a3e123,0x49adf5e0,0x229ca7c8,0x67ff9811,
0x8d7ef8d2,0x7fe62efd,0x8460ec41,0x4a948160,0x6e78c0fe,0xbc7f9945,0x4d630727,0xd5c0261a,0x2ca30681,0x87cbff44,0x729e6efe,0x9b472126,
0x722d1679,0xb1a9bb11,0x784c8125,0x9e2746a,0xd94d928e,0x5964daa5,0xb14ff61d,0x96f5587c,0x9e6d4f53,0xf0fb8330,0x9e9faff2,0x78045ebb,
0xbb8a63c5,0x5b3a852f,0x606e31e3,0xe175d270,0x9d720ca9,0x6aff8405,0x61f831,0xe92c5241,0xf9cd8ae8,0xac377efc,0x42c8422e,0xdb3648c,
0x24e8f21e,0xeb61aff8,0x6235f6bc,0xfb36f48a,0xfa586736,0x3edb178e,0xb5ee3e68,0x889d1b0a,0xe7dcc769,0x90dd687d,0x4dcc9d7b,0x9d6835b2,
0xb1e0d620,0xfb52d43c,0xeaa5d177,0x39ba22ad,0xe8922adc,0x4ce38572,0x75f1f91b,0xb16bf7d9,0x68d9c1ac,0x464be557,0x45f943bd,0xb51130e0,
0xd84c2f17,0xa1838b3d,0xf9e55a,0xb23056e7,0x73f97935,0x10d22351,0x789cb3d4,0xa316bc8d,0xa7d2ee8b,0x946671d4,0x8202210b,0x900aef8b,
0x9bc8704a,0x3a91b25b,0x163b513f,0xd654930e,0xd892b80f,0x6f397647,0x3e605e17,0x15f0313f,0x5c2f8c29,0x9bd886db,0x53f3b1fa,0x21764048,
0xbff1f9d2,0xa561c4ff,0xf1aaef41,0x6e212acf,0xe25e1488,0x184bed6f,0xb0c27e35,0xcb5723cd,0x7fdc10f2,0x82228fb0,0xf82d4e90,0xb06ed400,
0x6116f19d,0xe5166925,0xd5d30a01,0x7e1d1f2a,0x38d8a320,0x111246f1,0xaec5fa9,0x5f1f2ac9,0x8b3890da,0x623ec084,0xfc915656,0x5a222f1f,
0x494d9b91,0xf4b6d923,0x2e2c1010,0x70bd3bc6,0xf7edbaa,0x7a240022,0x73829b6d,0xa378db8b,0x500561e6,0xb1974c38,0x3e2b5b34,0x8a044dcf,
0x78a1f40b,0x81d9fe2,0x27a0d408,0x9b2bb073,0x81a1a198,0x5e11ca9,0xeed5be77,0x5a546733,0x65dfceda,0xf769696f,0x4998a4fa,0x82ba181d,
0x2c4daca5,0xedbd2023,0x6f5af809,0xbe8e0121,0xc9bd68fd,0x6ce5f57a,0xcb23ac02,0x1332b31e,0x4ce34a73,0xa8ce4128,0x5963ccb6,0x4490d702,
0xe3a65cf6,0x9e99e5e2,0x334d289f,0x40bf19fc,0xadf3ce7b,0xa225ed19,0xe46f7ab,0x33969302,0x31e35589,0xc342d5c5,0xc6e4f7e3,0x3cdec2cb,
0x363ee92f,0x6812909c,0x2ca17b10,0xa75d6266,0xaded6e8a,0xba43054d,0xf709aa2e,0x1a2c435a,0xb662ef67,0xbe78e8c5,0x9d949b7f,0x636bb84d,
0xaf7d4433,0x65bd886c,0x7b825643,0xd631f0fd,0x63dd37e6,0x3911a847,0x8802a4ef,0x1c838601,0x48a938bc,0x9bee001b,0x14041e83,0x9e8f1b71,
0xa070ef35,0xf7a38443,0x42a26f7d,0x2ac70302,0x6ec6ef96,0x838b2bdf,0xe673f28e,0xf1fd2ca,0x7c789bc8,0xbb05c1ef,0x635e75f9,0x1f8f90c,
0xd82c8ff7,0xd0ee86e9,0x2d407a32,0x8941f1,0x8f0bef75,0xb131b823,0x615c5c06,0xde00a210,0xa15950f7,0x8b67280d,0x39f0b6b8,0x9c1db329,
0xc7f46ab5,0xbc82e328,0x1db3caba,0xc1db9654,0x78bb3c89,0x68e6baa8,0xed70b7e6,0x243c270,0x5c19389d,0x29636aa0,0xfab5702f,0x4607690d,
0x6fcc56db,0x1f90caf0,0xe97294d6,0x59666755,0xcb136bb2,0xa142346a,0xc5a7a8ca,0xc45b1a35,0x600cc397,0xbded5390,0xd4912feb,0x53377909,
0xb73afc2a,0x68acb7a3,0x924a117d,0x35f53883,0x3899121f,0x92e73e46,0xf88a6931,0x82783c9c,0x8a488b3e,0xeb5812b3,0xa88839b5,0xae1ed8,
0xb6e64c8d,0xbfe72309,0xa88dc7ed,0xdeb72494,0xfd4bc090,0xb819d4e5,0xc5ac019c,0x81f84939,0xf2f02c6f,0x2a579c7c,0xd9994be5,0x97ef8222,
0x413d1f42,0x126bbabc,0xd26060b2,0xb21495c3,0xb99d11d4,0x6d654af,0xe67f682e,0x57489f0f,0xfaf50402,0xe1c89fed,0xeeffeb7f,0x65399dc9,
0xdead8335,0xb714804,0xebe67104,0xc4b654ad,0xb4eca2ae,0x61a33f44,0x99682882,0x5f2e14f8,0xe257d343,0x70ad8942,0xb2ea3272,0x89908fbf,
0x23dcf5f1,0x3a64d120,0x323513ec,0x3050fa0e,0xde8832e3,0xadc80b52,0xc3d2d499,0x748cfeb2,0xe6e359a7,0x5b5aa186,0x26b57c36,0x421ffd5e,
0x77ef8557,0xdd10c01e,0xd0ff8cab,0x223e260f,0xd9a52a11,0x1f1ba455,0x9a260fb6,0x439f0ab4,0xf6b5f8a2,0xd01b68f6,0x23951d54,0x282833af,
0xcdec6f19,0x1a756c71,0xbda40734,0xa976c196,0x75ab1f26,0xa2f683ac,0x6fdd9d55,0x1c06eb45,0x19ecd85f,0xc53e0d1,0x3e8b9aa3,0x92f8ddca,
0xdd4ad4eb,0x21ea417f,0x91baefda,0x7ab389df,0x13fc7af7,0x9ad13efc,0xe004c22f,0xabe347dd,0x21055827,0xfc9794f3,0x3bdaf0af,0x6d654f76,
0xe9ed924b,0x65f203f7,0x78fc7f1b,0xe706caa3,0xa2692225,0x2a9d2977,0xbc30c2b8,0xde65d8b5,0x77c0d6d6,0x2cbd769d,0xaf37a523,0xa09364e9,
0x1908d8ba,0x115e96bc,0xfb1bacb,0xac76512a,0x35fac5d,0x7252e9c0,0x4b313dec,0x59b5da2d,0xe65fc9cd,0xd6269964,0xecb5686e,0x554d36cb,
0x71eae5fc,0xfab2992a,0x401c10f2,0xe842b221,0xe4cf6de0,0x86733a2,0x67f50550,0x9cafee4e,0x83a2e655,0x11917060,0x62a4b12b,0x66b7bea3,
0x5225a1d6,0xc5984050,0xcaa944dc,0xe205ef85,0xf1ee6cbd,0x615543e5,0x636cddb5,0x15fcdc62,0xbd08f031,0xff96f174,0xd6ed9b92,0x1bd0f760,
0x3a1cf40b,0x9f069617,0xb1abaf17,0x86665458,0xe725096b,0x220c4b55,0xb25fb2df,0x321df603,0x70e1fdac,0x5da78c36,0x10bc24b1,0x5fc89104,
0xd9cb4c2b,0x58e2acc9,0x85da6606,0x7d09d144,0x95ff2a46,0x8258f470,0x5264ac59,0xbd8e8adc,0x18ebeb3f,0x7e9cc107,0xaefd325d,0x8e979e87,
0xc47d1196,0xa775e3ec,0xa858058a,0x833b50e5,0x2a1c74ad,0x980e1d9f,0x256ef7ef,0x7ddf419e,0xd0d2330f,0xd8344af8,0xb0017a0f,0x89df9ac9,
0xa620abc0,0x99c0d8b,0x68a94f7c,0xedcd5d46,0x357a4f21,0x22ad2317,0x39e560c4,0xa1763de2,0xd8196250,0x3db67c1c,0x60689884,0xa78ea174,
0x6074d9e2,0xcb4064d1,0x9bc017ba,0x3941f432,0x3ad42b83,0x30534dc8,0x7a5b86a2,0x4e5471fb,0x6527f8b9,0x371b3a19,0x459da2ee,0x12b9682e,
0x5683a6dd,0x69f6612a,0x7dec934a,0x4740402d,0x96eac3ad,0x594d49a2,0xaac47af,0x20eebed8,0x5c445adc,0x4c19d013,0x8ce6aecd,0x8872cfae,
0x27c9c9fb,0xdfd5c02f,0xa20bbb5e,0x8fb29c8a,0xfc3209,0x5c2e254f,0xf35ad356,0xdf73ca6b,0xcbc25bc8,0xf194c462,0xf8788d29,0x59e6451c,
};


static  int MI_Subtract (MInt *minuend,MInt *subtrahend,MInt *difference)
{
  MInt  *b, *c;
  DWord ai, borrow;
  int i, status=0;

  do
  {
    if ((i = MI_Compare (minuend, subtrahend)) > 0) 
	{
      b = minuend;
      c = subtrahend;
    }
    else if (i == 0) {
      difference->length = 1;
      difference->value[0] = 0;
      break;
    }
    else {
      b = subtrahend;
      c = minuend;
      status = MI_NEGATIVE;
    }
    borrow = 0;
    for (i = 0; i < c->length ; ++ i) 
	{
      if ((borrow += c->value[i]) < c->value[i]) 
	  {
        difference->value[i] = b->value[i];
        borrow = 1;
      }
      else 
	  {
        if ((ai = b->value[i] - borrow) > (MAX_CMP_WORD - borrow))
          borrow = 1;
        else 
          borrow = 0;
        difference->value[i] = ai;
      }
    }

    for (i = c->length;i < b->length; ++ i) {
      if ((ai = b->value[i] - borrow) > MAX_CMP_WORD - borrow)
        borrow = 1;
      else
        borrow = 0;
      difference->value[i] = ai;
    }
    MI_RecomputeLength (b->length , difference);
  } while (0);

  return (status);  
}

static int DW_AddProduct (DWord *a, DWord *b, DWord c, DWord *d, unsigned int length)
{
  DWord carry, t[2];
  unsigned int i;

  carry = 0;
  for (i = 0; i < length; ++ i) 
  {
    DWordMult (t, c, d[i]);
    if ((a[i] = b[i] + carry) < carry)	carry = 1;
	else	carry = 0;
    if ((a[i] += t[0]) < t[0])
      ++ carry;
    carry += t[1];
  }
  return (carry);
}

static int MI_Multiply(MInt *multiplicand,MInt *multiplier,MInt *product)
{
	DWord a[MINTLENGTH], *b, *c;
	int cLen, i, productLen;
  
    productLen = multiplicand->length + multiplier->length;
    if (MINTLENGTH<productLen)
      return (-1);

    b = multiplicand->value;
    c = multiplier->value;
    cLen = multiplier->length;
    for(i=0;i<productLen;i++)	a[i]=0;;
    
    for (i = 0; i < multiplicand->length; ++ i)
      a[cLen + i] += DW_AddProduct (&a[i], &a[i], b[i], c, cLen);
	for(i=0;i<productLen;i++)
		product->value[i]=a[i];
	MI_RecomputeLength (productLen, product);
  
  return (0);
}
static DWord CMP_ArraySub (DWord *a,DWord *b,DWord *c,
						   unsigned int length)
{
  DWord ai, borrow;
  unsigned int i;

  borrow = 0;
  for (i = 0; i < length; ++ i) {
    if ((borrow += c[i]) < c[i]) {
      a[i] = b[i];
      borrow = 1;
    }
    else {
      if ((ai = b[i] - borrow) > (MAX_CMP_WORD - borrow))
        borrow = 1;
      else 
        borrow = 0;
      a[i] = ai;
    }
  }
  return (borrow);
}

static DWord CMP_ArrayLeftShift (DWord *a,unsigned int bits,
								   unsigned int length)
{
  DWord r, shiftOut;
  unsigned int i, bitsLeft;
  
  if (bits == 0) {
    return (0);
  }
  
  bitsLeft = CMP_WORD_SIZE - bits;
  shiftOut = 0;
  for (i = 0; i < length; ++i) {
    r = a[i];
    a[i] = (r << bits) | shiftOut;
    shiftOut = r >> bitsLeft;
  }
  return (shiftOut);
}
static DWord CMP_ArrayRightShift (DWord *a,unsigned int bits,
									unsigned int length)
{
  DWord r, shiftOut;
  int i, bitsLeft;

  if (bits == 0) {
    return (0);
  }

  bitsLeft = CMP_WORD_SIZE - bits;
  shiftOut = 0;
  for (i = length - 1; i >= 0; --i) {
    r = a[i];
    a[i] = (r >> bits) | shiftOut;
    shiftOut = r << bitsLeft;
  }
  return (shiftOut);
}


static DWord CMP_SubProduct (DWord *a,DWord *b,DWord c,
							 DWord *d,unsigned int length)
{
  DWord borrow, t[2];
  unsigned int i;

  borrow = 0;
  for (i = 0; i < length; ++ i) 
  {
    DWordMult (t, c, d[i]);

    if ((borrow += t[0]) < t[0]) 
      ++ t[1];
    if ((a[i] = b[i] - borrow) > (MAX_CMP_WORD - borrow)) 
      borrow = t[1] + 1;
    else
      borrow = t[1];
  }
  return (borrow);
}
static int CMP_ArrayCmp (DWord *a,DWord *b,unsigned int length)
{
  int i;

  for (i = (int)length - 1; i >= 0; -- i) {
    if (a[i] > b[i])
      return (1);
    else if (a[i] < b[i])
      return (-1);
  }
  return (0);
} 
static int MI_ModularReduce (MInt *operand,MInt *modulus,MInt *reducedValue)
{
  DWord ai, t, *cc, *dd;
  DWord a;
  int i;
  unsigned int ccWords, ddWords, shift;

  MI_RecomputeLength (operand->length,operand);
  MI_RecomputeLength (modulus->length,modulus);

  do {

    if (MI_Compare (operand, modulus) < 0) {
      MI_Move (operand, reducedValue);
      break;
    }

    if (MI_Move (operand, reducedValue) != 0)
      break;

    a=modulus->value[modulus->length-1];
    for(i=0;(i<(int)CMP_WORD_SIZE)&&(a!=0);++i,a>>=1);
    
    shift = CMP_WORD_SIZE - i;

    ccWords = reducedValue->length;
    cc = reducedValue->value;
    if ((cc[ccWords] = CMP_ArrayLeftShift (cc, shift, ccWords)) != 0) 
      cc[++ ccWords] = 0;

    ddWords = modulus->length;
    dd = modulus->value;
    CMP_ArrayLeftShift (dd, shift, ddWords);
    t = dd[ddWords - 1];

    for (i = ccWords - ddWords; i >= 0; -- i) {
     if (t == MAX_CMP_WORD)
        ai = cc[i + ddWords];
      else
        CMP_WordDiv(&ai, &cc[i + ddWords - 1], t + 1);
      cc[i + ddWords] -= CMP_SubProduct (&cc[i], &cc[i], ai, dd, ddWords);

      while (cc[i + ddWords] || (CMP_ArrayCmp (&cc[i], dd, ddWords) >= 0)) {
        cc[i + ddWords] -= CMP_ArraySub (&cc[i], &cc[i], dd, ddWords);
      }
    }

    CMP_ArrayRightShift (dd, shift, ddWords);
    CMP_ArrayRightShift (cc, shift, ddWords);
    MI_RecomputeLength (ddWords, reducedValue);
  } while (0); 
  
  
  return 0;
}

static int MI_RecomputeLength (int targetLength,MInt *theInt)
{
	int i;

	for (i = targetLength - 1; i >= 0; -- i)
		if (theInt->value[i] != 0)
			break;
	theInt->length = i + 1;
	return (0);
}
int FpMinus (MInt *operand,MInt *prime,MInt *result)
{
	if (operand->length == 1 && operand->value[0] == 0) 
		FP_Move (operand, result);
    else
	    MI_Subtract (prime, operand, result);
    return 0;
}

static int FP_Add (MInt *addend1,MInt *addend2, 
				MInt *modulus,MInt *sum)
{
	MInt t;
    MI_Add (addend1, addend2, &t);
    if (MI_Compare (&t, modulus) >= 0)
		MI_Subtract (&t, modulus, sum);
    else 
		MI_Move (&t, sum);
	return 0;
}

static int FP_Substract (MInt *minuend,MInt *subtrahend,
					 MInt *modulus,MInt *difference)
{
	MInt  t;
	int status;

    status = MI_Subtract (minuend, subtrahend, &t);
    if(status==0)
	    MI_Move (&t, difference);
	else
        MI_Subtract (modulus, &t, difference);
    return 0;
  
}

/* #define MB_ECC */
#ifdef MB_ECC
extern void GetRandPri(BYTE *buffer, int sizes);
#endif


static int GenRandomNumberForFixLen(int wordLen,MInt *theInt)
{
	int j,k,aa;
	unsigned int ss=1;

	theInt->length=wordLen;
	
#ifdef MB_ECC
	GetRandPri((BYTE *)theInt->value, wordLen*sizeof(int));
#else
	srand((unsigned)time(NULL));
	
	for(j=0;j<wordLen;j++)
	{
		for(k=0;k<3;k++)
		{
			aa=rand()+randseed+(randseed<<1)+(randseed>>1);
			randseed++;
			ss=ss|(aa<<k*11);
		}
		theInt->value[j]=ss;
		ss=1;
	}
#endif	
	return 0;
}


static int GenRandomNumber (MInt *theInt, MInt *maxInt)
{
	int j,k,aa;
	MInt t;
	unsigned int ss=1;
	
	t.length=maxInt->length ;
	
#ifdef MB_ECC
	GetRandPri((BYTE *)theInt->value, t.length*sizeof(int));
#else
	srand((unsigned)time(NULL));
	
	for(j=0;j<maxInt->length ;j++)
	{
		for(k=0;k<3;k++)
		{
			aa=rand()+randseed+(randseed<<1)+(randseed>>1);
			randseed++;
			ss=ss|(aa<<k*11);
		}
		t.value[j]=ss;
		ss=1;

	}
#endif	
	MI_ModularReduce (&t, maxInt,theInt);

	return 0;
}


static int MI_Divide (MInt *dividend,MInt *divisor,
			   MInt *quotient,MInt *remainder)
{
  DWord ai, t, *aa, *cc, *dd;
  DWord a;
  int i;
  unsigned int ccWords, ddWords, shift;
  
  MI_RecomputeLength (dividend->length,dividend);
  MI_RecomputeLength (divisor->length,divisor);

  do 
  {
    if (MI_Compare (dividend, divisor) < 0) 
    {
      MI_WordToMInt (0, quotient);
      MI_Move (dividend, remainder);
      break;
    }

    MI_Move(dividend, remainder);

    a=divisor->value[divisor->length-1];
    for(i=0;(i<(int)CMP_WORD_SIZE)&&(a!=0);++i,a>>=1);

    shift = CMP_WORD_SIZE - i;
    ccWords = remainder->length;
    cc = remainder->value;
    if ((cc[ccWords] = 
    	CMP_ArrayLeftShift (cc, shift, ccWords)) != 0) 
      cc[++ ccWords] = 0;

    ddWords = divisor->length;
    dd = divisor->value;
    CMP_ArrayLeftShift (dd, shift, ddWords);
    t = dd[ddWords - 1];
    aa = quotient->value;
  
    for (i = ccWords - ddWords; i >= 0; -- i) 
    {
      if (t == MAX_CMP_WORD)
        ai = cc[i + ddWords];
      else
        CMP_WordDiv(&ai, &cc[i + ddWords - 1], t + 1);
      cc[i + ddWords] -= CMP_SubProduct (&cc[i], &cc[i], ai, dd, ddWords);

      while (cc[i + ddWords] || (CMP_ArrayCmp (&cc[i], dd, ddWords) >= 0)) {
        ++ ai;
        cc[i + ddWords] -= CMP_ArraySub (&cc[i], &cc[i], dd, ddWords);
      }
      aa[i] = ai;
    }

    CMP_ArrayRightShift (dd, shift, ddWords);
    CMP_ArrayRightShift (cc, shift, ddWords);
    MI_RecomputeLength (ddWords, remainder);
    MI_RecomputeLength (ccWords - ddWords + 1, quotient);
  } while (0);
              
  return 0;
}
static int FpDivByTwo(MInt *a,MInt *p)
{
	int len,i;
	DWord *av,shifth,shiftl;
	av=a->value;	len=a->length;
	shifth=0;
	if(av[0]==((av[0]>>1)<<1))
	{
		if(av[len-1]==1)
			a->length=len-1;
		for(i=len-1;i>=0;i--)
		{
			shiftl=av[i]-((av[i]>>1)<<1);
			av[i]=(shifth<<31)+(av[i]>>1);
			shifth=shiftl;
		}
	}
	else
	{
		MI_Add(a,p,a);
		len=a->length;
		if(av[len-1]==1)
			a->length=len-1;
		for(i=len-1;i>=0;i--)
		{
			shiftl=av[i]-((av[i]>>1)<<1);
			av[i]=(shifth<<31)+(av[i]>>1);
			shifth=shiftl;
		}
	}
	return 0;
}

static int FP_Invert (MInt *operand,MInt *modulus,MInt *inverse)
{
	MInt q, t1, t3, u1, u3, v1, v3, w;
	int u1Sign;
    MI_WordToMInt (1, &u1);
    MI_WordToMInt (0, &v1);
    MI_Move (operand, &u3);
    MI_Move (modulus, &v3);

    u1Sign = 1;
	while (v3.length != 0 ) 
	{
		MI_Divide (&u3, &v3, &q, &t3);
		MI_Multiply (&q, &v1, &w);
		MI_Add (&u1, &w, &t1);
		MI_Move (&v1, &u1);
		MI_Move (&t1, &v1);
		MI_Move (&v3, &u3);
		MI_Move (&t3, &v3);
		u1Sign = -u1Sign;
    }

    if (u1Sign < 0) 
		MI_Subtract (modulus, &u1, inverse);
    else 
		MI_Move (&u1, inverse);
    
	return 0;
}

static int FP_Div(MInt *op1,MInt *op2,MInt *prime,MInt *result)
{
	MInt temp;
	FP_Invert(op2,prime,&temp);
	FP_Mul(&temp,op1,prime,result);
	return 0;
}

static int FP_MulNormal(MInt *multiplicand,MInt *multiplier,
		   MInt *p,MInt *product) 
{
	DWord a[MINTLENGTH], *bb, *c;
	int cLen, i, productLen;

	if(FP_IsZero(multiplicand)||FP_IsZero(multiplier))
	{	
		product->length =0;	
		product->value [0]=0;	
		return 0;
	}

	productLen = multiplicand->length + multiplier->length;
	bb = multiplicand->value;    
	c = multiplier->value;
	cLen = multiplier->length;
	for(i=0;i<MINTLENGTH;i++)	a[i]=0;
    
	for (i = 0; i < multiplicand->length; ++ i)
		a[cLen + i] += DW_AddProduct (&a[i], &a[i], bb[i], c, cLen);

	for(i=0;i<productLen;i++)
		product->value[i]=a[i];
	MI_RecomputeLength (productLen , product);
	MI_ModularReduce (product, p, product);
	
	return 0;
}

static int (* FpMul)(MInt *,MInt *,MInt *,MInt *);

static int FP_Mul(MInt *multiplicand,MInt *multiplier,MInt *p,MInt *prod)
{
	return (* FpMul)(multiplicand,multiplier,p,prod);
}

static int OctetStringToMInt (const unsigned char *OString, unsigned int OSLen,
					   MInt *destInt)
{
	DWord word;
	int i, j, k, t, words;
	words = (OSLen+MI_BYTES_PER_WORD-1)/MI_BYTES_PER_WORD;
	for (i = OSLen, j = 0; i > 0; i -= 4, ++ j) 
	{
		word = 0;
		t = (int)min (i, (int)MI_BYTES_PER_WORD);
		for (k = 0; k < t; ++ k) 
		{
			word = (word << MI_BITS_PER_BYTE) | OString[i - t + k];
		}
		destInt->value[j] = word;
	}
	MI_RecomputeLength (words, destInt);

	return 0;
}
static int JointSFKL_Encode(MInt *k, MInt *l,unsigned char *JSF)
{
	 unsigned char d0,d1,temp0,temp1,jsfk,jsfl;
	 int  i, index;
	 MInt k1,l1;
	 MI_Move(k,&k1);
	 MI_Move(l,&l1);

	 d0=0;d1=0;
	 index=0;
	 while(k->length||l->length||d0||d1){
		temp0=(k->value[0]+d0)&7;
		temp1=(l->value[0]+d1)&7;
		
		if(!(temp0&01))  jsfk=0;
		else{ 
			 jsfk=temp0&03;
			 if(((temp1&3)==2)&&(((temp0&7)==3)||((temp0&7)==5)))
			     jsfk=(jsfk+2)&3;
		}

		if(!(temp1&01))  jsfl=0;
		else{
			jsfl=temp1&03;
			if(((temp0&3)==2)&&(((temp1&7)==3)||((temp1&7)==5)))
			     jsfl=(jsfl+2)&3;
		}						
		JSF[index]=(((jsfk+1)>>1)*3+((jsfl+1)>>1)); 

		if(((1+jsfk)&3)==(2*d0)) d0=1-d0;
		if(((1+jsfl)&3)==(2*d1)) d1=1-d1;	

		i=k->length-1;
		if(k->value[i]==1)  k->length--;
		temp0=0;
		for(;i>=0;i--){
		    temp1=k->value[i]&01;
			k->value[i]=(k->value[i]>>1)|(temp0<<31);
			temp0=temp1;
		}
		i=l->length-1;
		if(l->value[i]==1) l->length--;
		temp0=0;
		for(;i>=0;i--){
		    temp1=l->value[i]&01;
		    l->value[i]=(l->value[i]>>1)|(temp0<<31);
			temp0=temp1;
		}   
		index++;
	 }
	 MI_Move(&k1,k);
	 MI_Move(&l1,l);

     return index;
}

static int MIntToOctetString (MInt *srcInt,unsigned int OSBufferSize,
					   unsigned int *OSLen, unsigned char *DString)
{
	DWord word;
	int i, j, k, status, t;

	status = 0;
  do
  {
	for (i = srcInt->length - 1, j = 0; i >= 0; -- i) 
	{
		t = MI_BYTES_PER_WORD;
		word = srcInt->value[i];
		if (i == srcInt->length - 1) 
		{
			while ((word>>((t-1)*MI_BITS_PER_BYTE)==0)&&(t>1))
			-- t;
			if(t+i*MI_BYTES_PER_WORD>OSBufferSize) 
			{
				status = OUTPUT_SIZE;
				break;
			}
		}
		for (k = t - 1; k >= 0; -- k) 
			DString[j ++] = 
          (unsigned char)(srcInt->value[i]>>(k*MI_BITS_PER_BYTE));
    }
    if (status == 0)
      *OSLen = j;
  } while (0);

	return (status);
}

static int ECFpAdd (EcFpPoint *addend1, EcFpPoint *addend2,
			 MInt *a, MInt *b,MInt *prime,EcFpPoint *sum)
{
	MInt r, s, t;
	b = b;
    if (addend1->isInfinite == 1) 
	{
		sum->isInfinite = addend2->isInfinite;
		FP_Move (&addend2->x, &sum->x);
		FP_Move (&addend2->y, &sum->y);
		return 0;
    }
    else if (addend2->isInfinite == 1) 
	{
      sum->isInfinite = addend1->isInfinite;
      FP_Move (&addend1->x, &sum->x);
      FP_Move (&addend1->y, &sum->y);
      return 0;
    }
    else if (FP_Equal (&addend1->x, &addend2->x)) 
	{
		FP_Substract (prime, &addend2->y, prime, &r);
		if (FP_Equal (&addend1->y, &r)) 
		{
			sum->isInfinite = 1;
			return 0;
		}
		else if (FP_Equal (&addend1->y, &addend2->y)) 
		{
			FPSqr_Mul (&addend1->x,prime, &r);
			FP_Add (&r, &r, prime, &t);
			FP_Add (&r, &t, prime, &s);
			FP_Add (&s, a, prime, &r);
			FP_Add (&addend1->y, &addend1->y, prime, &t);
			FP_Invert (&t, prime, &s);
			FP_Mul (&s, &r, prime, &t);
			FPSqr_Mul(&t,prime, &r);
			FP_Substract (&r, &addend1->x, prime, &s);
			FP_Substract (&s, &addend2->x, prime, &r);
			FP_Substract (&addend1->x, &r, prime, &s);
			FP_Move (&r, &sum->x);
			FP_Mul (&t, &s, prime, &r);
			FP_Substract (&r, &addend1->y, prime, &sum->y);

			return 0;
        }
    }  
    FP_Substract (&addend2->x, &addend1->x, prime, &t);
    FP_Substract (&addend2->y, &addend1->y, prime, &s);
    FP_Invert (&t, prime, &r);
    FP_Mul (&s, &r, prime, &t);
    FPSqr_Mul (&t, prime, &r);
    FP_Substract (&r, &addend1->x, prime, &s);
    FP_Substract (&s, &addend2->x, prime, &r);
    FP_Substract (&addend1->x, &r, prime, &s);
    FP_Move (&r, &sum->x);
    FP_Mul (&t, &s, prime, &r);
    FP_Substract (&r, &addend1->y, prime, &sum->y);
	sum->isInfinite=0;

	return 0;
}
static int MIntToFixedLenOS(MInt *srcInt,unsigned int fixedLength,
				unsigned int OSBufferSize, unsigned int *OSLen,
				unsigned char *DString)
{
  int d, i, status;
  unsigned int len;

  do
  {
    if ((status = MIntToOctetString (srcInt,OSBufferSize,
      &len, DString)) != 0)
      break;
    if ((d = fixedLength - len) > 0) 
	{
      for (i = fixedLength - 1; i >= d; -- i) 
        DString[i] = DString[i - d];
      for (i = d - 1; i >= 0; -- i)
        DString[i] = 0;
    }
    else if (d < 0) 
	{
      status = OUTPUT_LEN;
      break;
    }
    *OSLen = fixedLength;
  } while (0);

  return (status);
}
static int ECFpKTimes (EcFpPoint *operand,MInt *k,MInt *a,MInt *b,
				MInt *prime, EcFpPoint *result)
{
	EcFpPointProject rr;         
	EcFpPoint pp, qq;
	MInt inverse, kk,temp;
	DWord s, t;
	int i, j, kkWords;
	unsigned int bits;
   	if(FP_IsZero(k))
	{
		result->isInfinite=1;
		return 0;
	}
    MI_WordToMInt (0, &rr.x);
    MI_WordToMInt (1, &rr.y);
    MI_WordToMInt (0, &rr.z);

    CanonicalEncode (k, &kk);

    kkWords = kk.length;
    FP_Move (&operand->x, &pp.x);
    FP_Move (&operand->y, &pp.y);
    
    FP_Move (&operand->x, &qq.x);
    FpMinus (&operand->y, prime, &qq.y);
    
    for (i = kkWords - 1; i >= 0; -- i)
	{
		t = kk.value[i];
		bits = MI_WORD_SIZE;
		if (i == kkWords - 1) 
		{
			while (! (t>>(MI_WORD_SIZE-2)))
			{
				t <<= 2;
				bits -= 2;
			}
		}
		for (j = bits; j > 0; j -= 2, t <<= 2) 
		{
			ECFpDoubleProj (&rr, a, b, prime, &rr);
			if ((s = (t>>(MI_WORD_SIZE-2)))==ONE)
				ECFpAddProj (&rr, &pp, a, b, prime, &rr);
			else if (s == MINUS_ONE) 
				ECFpAddProj (&rr, &qq, a, b, prime, &rr);
		}
	}

    if (FP_IsZero (&rr.z)) 
		result->isInfinite = 1;
    else
	{
		result->isInfinite = 0;
		FP_Invert (&rr.z, prime, &inverse);
		FPSqr_Mul (&inverse, prime, &temp);
		FP_Mul (&rr.x, &temp, prime, &result->x); 
		FP_Mul(&temp,&inverse,prime,&inverse);
		FP_Mul (&rr.y, &inverse, prime, &result->y);
    }
  
	return 0;
}



static DWord C_TABLE1[8] = { 0, 0, 0, 4, 0, 4, 4, 4 },
C_TABLE2[8] = {0, 1, 0, 3, 1, 0, 3, 0 };
 
static int CanonicalEncode (MInt *source, MInt *destination)
{
	DWord word, srcWord;
	int ci, entry, i, j, wordBits, words;
  
	words = source->length;
	ci = 0;
	wordBits = sizeof (DWord) * 8;
	for (i = 0; i < words; ++ i) 
	{
		word = 0;
		srcWord = source->value[i];
		for (j = 0; j < wordBits / 2; ++ j) 
		{
			entry = ci | (unsigned int)((srcWord >> j) & 3);
			ci = (int)C_TABLE1[entry];
			word |= (C_TABLE2[entry] << (2 * j));
		}
		destination->value[2 * i] = word;
		word = 0;
		for (j = MI_WORD_SIZE / 2; j < wordBits - 1; ++ j) 
		{
			entry = ci | (unsigned int)((srcWord >> j) & 3);
			ci = (int)C_TABLE1[entry];
			word |= (C_TABLE2[entry] << (2 * j - MI_WORD_SIZE));
		}
		entry = ci | (unsigned int)(srcWord >> (wordBits - 1)); 
		entry = (i == words - 1) ? (unsigned int)entry :(unsigned int)entry | (unsigned int)(((source->value[i + 1]) << 1) & 2);
		ci = (int)C_TABLE1[entry];
		word |= (C_TABLE2[entry] << (MI_WORD_SIZE - 2));
		destination->value[2 * i + 1] = word;
	}
	if (ci != 0) 
		destination->value[2 * i] = 1;
	else 
		destination->value[2 * i] = 0;
	MI_RecomputeLength (2 * words + 1, destination);
	
	return 0;
}
int PubKeyToOctetString(Point *poPublicKey,unsigned int OSBuffSize,
				unsigned int *OSLen,unsigned char *DString)
{
	MInt x,y;
	int i;
	unsigned int len;
	for(i=PARABUFFER-1;i>=0;i--)
		if(poPublicKey->x[i]!=0)
			break;
	x.length=i+1;
	for(;i>=0;i--)
		x.value[i]=poPublicKey->x[i];

	for(i=PARABUFFER-1;i>=0;i--)
		if(poPublicKey->y[i]!=0)
			break;
	y.length=i+1;
	for(;i>=0;i--)
		y.value[i]=poPublicKey->y[i];

	MIntToFixedLenOS(&x,PARABUFFER*4,OSBuffSize,&len,DString);
	MIntToFixedLenOS(&y,PARABUFFER*4,OSBuffSize,OSLen,DString+len);
	*OSLen+=len;
		
	return 0;
}
/*
extern int randseed;
*/

static int ECES_Format(int len,const unsigned char *OString, unsigned int OSLen,
				unsigned char *DestOString, unsigned int *DestLen)
{
	unsigned int i;
	unsigned char aa;

	if(OSLen>(unsigned int)len-2)
		return 1;

	DestOString[0]=0;

#ifdef MB_ECC
	GetRandPri(DestOString+1, len-OSLen-1);
	DestOString[len-OSLen-1]=0;
	memcpy (&DestOString[len-OSLen], OString, sizeof(unsigned char)*OSLen);
#else
	srand((unsigned)time(NULL));
	for(i=1;i<(unsigned int)len-OSLen-1;i++)
	{
		while((aa=rand()+randseed)==0)
			randseed++;
		randseed++;
		DestOString[i]=aa;
	}
	DestOString[i]=0;
	memcpy (&DestOString[i+1], OString, sizeof(unsigned char)*OSLen);
#endif	
	*DestLen=len;
	return 0;
}
static int ECFpKTimes_FixP (EcFpPoint *operand,EcFpPoint *Table1,
				 MInt *k,MInt *a,MInt *b,
				MInt *prime, EcFpPoint *result)
{
	EcFpPointProject rr;       
	MInt inverse,temp;

	int i,j,m,e,length;
	unsigned int t,t2,ki,ki2;

	operand = operand;
	if(FP_IsZero(k))
	{
		result->isInfinite=1;
		return 0;
	}
	length=1<<(prime->length);

	MI_WordToMInt (0, &rr.x);
    MI_WordToMInt (1, &rr.y);
    MI_WordToMInt (0, &rr.z);

	e=16;
	for(i=e;i>0;i--)
	{

		ECFpDoubleProj (&rr, a, b, prime, &rr);
		t=0; t2=0;
		for(j=0;j<k->length;j++)
		{
			ki=k->value[j]<<(32-i);
			ki=ki>>31;
			ki2=k->value[j]<<(32-(e+i));
			ki2=ki2>>31;
			for(m=0;m<j;m++)
			{ ki=ki*2;	ki2=ki2*2;}
			t+=ki;
			t2+=ki2;
		}
		if(t!=0)
			ECFpAddProj (&rr, Table1+t, a, b, prime, &rr);
		if(t2!=0)
			ECFpAddProj (&rr, Table1+length+t2, a, b, prime, &rr);
	}
	if (FP_IsZero(&rr.z)) 
		result->isInfinite = 1;
    else
	{
		result->isInfinite = 0;
		FP_Invert (&rr.z, prime, &inverse);
		
		FPSqr_Mul (&inverse, prime, &temp);
		(* FpMul) (&rr.x, &temp, prime, &result->x);
		(* FpMul)(&temp,&inverse,prime,&inverse);
		(* FpMul) (&rr.y, &inverse, prime, &result->y);
    }
  
	return 0;
}
static int ECFpDoubleProj (EcFpPointProject *operand,MInt *a,MInt *b,
					MInt *prime,EcFpPointProject *result)
{
	MInt t1,t2,t3,t4,t5,temp;
	b=b;
	if (FP_IsZero (&operand->z))
	{
      MI_WordToMInt (0, &result->x);
      MI_WordToMInt (1, &result->y);
      MI_WordToMInt (0, &result->z);
      return 0;
    }
    MI_Move(&operand->x,&t1);
	MI_Move(&operand->y,&t2);
	MI_Move(&operand->z,&t3);
	
	MI_Move(prime,&temp);
	temp.value[0]=temp.value[0]-3; 
	if(MI_Compare(a,&temp)==0)
	{
		FPSqr_Mul(&t3,prime,&t4);
		FP_Substract(&t1,&t4,prime,&t5);
		FP_Add(&t1,&t4,prime,&t4);
		FP_Mul(&t4,&t5,prime,&t5);
		FP_Add(&t5,&t5,prime,&temp);
		FP_Add(&temp,&t5,prime,&t4);
	}
	else
	{
		MI_Move(a,&t4);
		FPSqr_Mul(&t3,prime,&t5);
		FPSqr_Mul(&t5,prime,&t5);
		FP_Mul(&t4,&t5,prime,&t5);
		FPSqr_Mul(&t1,prime,&t4);
		FP_Add(&t4,&t4,prime,&temp);
		FP_Add(&temp,&t4,prime,&t4);
		FP_Add(&t5,&t4,prime,&t4);
	}
	FP_Mul(&t2,&t3,prime,&t3);
	FP_Add(&t3,&t3,prime,&t3);
	FPSqr_Mul(&t2,prime,&t2);
	FP_Mul(&t1,&t2,prime,&t5);
	FP_Add(&t5,&t5,prime,&temp);
	FP_Add(&temp,&temp,prime,&t5);
	FPSqr_Mul(&t4,prime,&t1);
	FP_Add(&t5,&t5,prime,&temp);
	FP_Substract(&t1,&temp,prime,&t1);
	FPSqr_Mul(&t2,prime,&t2);
	FP_Add(&t2,&t2,prime,&temp);
	FP_Add(&temp,&temp,prime,&t2);
	FP_Move(&t2,&temp);
	FP_Add(&temp,&temp,prime,&t2); 
	FP_Substract(&t5,&t1,prime,&t5);
	FP_Mul(&t4,&t5,prime,&t5);
	FP_Substract(&t5,&t2,prime,&t2);

	MI_Move(&t1,&result->x);
	MI_Move(&t2,&result->y);
	MI_Move(&t3,&result->z);

	return 0;
}

static int ECFpKPAddLQs(EcFpPoint *P,EcFpPoint *Q,MInt *u1,MInt *u2,
				 MInt *a, MInt *b,MInt *prime,EcFpPoint *result)			     
{
	 int  i,JSFlong;
	 unsigned char JSFKL[258];	 
	 	
	 MInt inverse, temp;
	 static EcFpPoint   Point_PQ[8]; 
	 EcFpPointProject rr;	 
	
	 JSFlong=JointSFKL_Encode(u1,u2,JSFKL);	 

	 memset(Point_PQ, 0, sizeof(Point_PQ));
	 
	 FP_Move (&P->x, &Point_PQ[2].x);
     FP_Move (&P->y, &Point_PQ[2].y);       
	  
	 FP_Move (&Q->x, &Point_PQ[0].x);
     FP_Move (&Q->y, &Point_PQ[0].y);       
     FP_Move (&P->x, &Point_PQ[5].x);
     FpMinus (&P->y, prime, &Point_PQ[5].y); 
	 FP_Move (&Q->x, &Point_PQ[1].x);
     FpMinus (&Q->y, prime, &Point_PQ[1].y); 
	
	 ECFpAdd (P,Q,a,b,prime,&Point_PQ[3]);  
      
	 FP_Move (&Point_PQ[3].x, &Point_PQ[7].x);
     FpMinus (&Point_PQ[3].y, prime, &Point_PQ[7].y);   
	 
	 ECFpAdd (P,&Point_PQ[1],a,b,prime,&Point_PQ[4]);

	 FP_Move (&Point_PQ[4].x, &Point_PQ[6].x);
     FpMinus (&Point_PQ[4].y, prime, &Point_PQ[6].y);

	 MI_WordToMInt (0, &rr.x);
     MI_WordToMInt (1, &rr.y);
     MI_WordToMInt (0, &rr.z);
	 
	 for(i=JSFlong-1;i>=0;i--){		 
		ECFpDoubleProj(&rr,a,b,prime, &rr);		
		if(JSFKL[i])
			ECFpAddProj(&rr,&Point_PQ[JSFKL[i]-1],a,b,prime,&rr);
	 }				 
	 
	 FP_Invert (&rr.z, prime, &inverse);
	 FPSqr_Mul (&inverse, prime, &temp);
	 FP_Mul (&rr.x, &temp, prime, &result->x);
	 FP_Mul(&temp,&inverse,prime,&inverse);
	 FP_Mul (&rr.y, &inverse, prime, &result->y);
  
	return 0;

}

static int EcFpPointToPoint(EcFpPoint *pt1 ,Point *dest)
{
	int i,len;
	if(pt1->isInfinite==1)
	{
		pt1->x.length=0;
		pt1->y.length=0;
	}
	len=pt1->x.length;
	for(i=0;i<len;i++)
		dest->x[i]=pt1->x.value[i];
	for(i=len;i<PARABUFFER;i++)
		dest->x[i]=0;
	len=pt1->y.length;
	for(i=0;i<len;i++)
		dest->y[i]=pt1->y.value[i];
	for(i=len;i<PARABUFFER;i++)
		dest->y[i]=0;
	return 0;
}
static int PointToEcFpPoint(const Point *sour,EcFpPoint *dest )
{
	int i;
	for(i=PARABUFFER-1;i>=0;i--)
		if(sour->x[i]!=0)
			break;
	dest->x.length=i+1;
	for(;i>=0;i--)
		dest->x.value[i]=sour->x[i];

	for(i=PARABUFFER-1;i>=0;i--)
		if(sour->y[i]!=0)
			break;
	dest->y.length=i+1;
	for(;i>=0;i--)
		dest->y.value[i]=sour->y[i];

	if((dest->x.length==0)&&(dest->y.length==0))
		dest->isInfinite=1;
	else
		dest->isInfinite=0;
		
	return 0;
}





 int PriKeyToOctetString(unsigned int *piPrivateKey,int piLenOfPriKey,
				unsigned int OSBuffSize,
				unsigned int *OSLen,unsigned char *DString)
{
	MInt s;
	int i;
	for(i=0;i<piLenOfPriKey;i++)
		s.value[i]=piPrivateKey[i];
	s.length=piLenOfPriKey;
	MIntToOctetString(&s,OSBuffSize,OSLen,DString);
	return 0;
}

 int OctetStringToPriKey(const unsigned char *OString, unsigned int OSLen,
				unsigned int *piPrivateKey,int *piLenOfPriKey)
{
	MInt s;
	int i;
	OctetStringToMInt(OString,OSLen,&s);
	
	for(i=0;i<s.length;i++)
		piPrivateKey[i]=s.value[i];
	*piLenOfPriKey=s.length;
	
	return 0;
}

	
 int OctetStringToPubKey(const unsigned char *OString, unsigned int OSLen,
				Point *poPublicKey)
{
	MInt x,y;
	int i;
	unsigned int len;
	len=OSLen/2;
	OctetStringToMInt(OString,len,&x);
	OctetStringToMInt(OString+len,len,&y);
	
	for(i=0;i<x.length ;i++)
		poPublicKey->x[i]=x.value[i];
	for(i=x.length ;i<PARABUFFER;i++)
		poPublicKey->x[i]=0;
	for(i=0;i<y.length ;i++)
		poPublicKey->y[i]=y.value[i];
	for(i=y.length ;i<PARABUFFER;i++)
		poPublicKey->y[i]=0;
	return 0;
}

#if 0
static void Construct_Point(Point *dest)
{
	dest->x=(unsigned int*)malloc(PARABUFFER*sizeof(unsigned int));
	dest->y=(unsigned int*)malloc(PARABUFFER*sizeof(unsigned int));
	return ;
}
static void Destroy_Point(Point *dest)
{
	free(dest->x);
	free(dest->y);
	return ;
}

static void Destroy_Point_Table(PointTable pTable,const int iLength)
{
	int i;
	for(i=0;i<iLength;i++)
		Destroy_Point(pTable+i);
	return ;
}
#endif

#if 0
void DPrint_string(char *name, void *_str,int len)
{
	int i;
	unsigned char *str = (unsigned char *)_str;

	if(name != NULL)
		printf("%s(%d) :\n", name, len);
	for(i=0;i<len;i++)
	{
		printf("%02X ",*str++);
		if((i+1)%16==0) printf("\n");
	}
	printf("\n");
}
#endif

static int  gettalbe(void)
{
	MInt t1,t2;
	int i,j;
	for(j=0;j<128;j++)
	{
		for(i=0;i<6;i++)
		{
			t1.value[i]=pTablexy[j*12+i];
			t1.length=6;
			t2.value[i]=pTablexy[j*12+6+i];
			t2.length=6;
			MI_RecomputeLength(6,&t1);
			MI_RecomputeLength(6,&t2);
			MI_Move(&t1,&(pTable1+j)->x);
			MI_Move(&t2,&(pTable1+j)->y);
		}
	}

	return(0);
}


static int ECFpAddProj (EcFpPointProject *addend1,EcFpPoint *addend2,
			MInt *a,MInt * b,MInt *prime,EcFpPointProject *result)
{
	MInt t1,t2,t3,t4,t5,t7,temp;

	if (FP_IsZero (&addend1->z))
	{
		FP_Move (&addend2->x, &result->x);
		FP_Move (&addend2->y, &result->y);
		MI_WordToMInt(1,&result->z);
		return 0;
    }

	MI_Move(&addend1->x,&t1); 
	MI_Move(&addend1->y,&t2);
	MI_Move(&addend1->z,&t3);
	FPSqr_Mul(&t3,prime,&t7); 
	FP_Mul(&addend2->x,&t7,prime,&t4);
	FP_Mul(&t3,&t7,prime,&t7);
	FP_Mul(&addend2->y,&t7,prime,&t5);
	FP_Substract(&t1,&t4,prime,&t4);
	FP_Substract(&t2,&t5,prime,&t5); //12
	if(FP_IsZero(&t4))
	{
		if(FP_IsZero(&t5)){
			ECFpDoubleProj (addend1, a, b, prime, result);	return 0;
		}
		else{
			MI_WordToMInt (0, &result->x);MI_WordToMInt (1, &result->y);
			MI_WordToMInt (0, &result->z);		return 0;
		}
	}
	FP_Add(&t1,&t1,prime,&temp);
	FP_Substract(&temp,&t4,prime,&t1);
	FP_Add(&t2,&t2,prime,&temp);
	FP_Substract(&temp,&t5,prime,&t2);
	FP_Mul(&t3,&t4,prime,&t3);
	FPSqr_Mul(&t4,prime,&t7);
	FP_Mul(&t4,&t7,prime,&t4);
	FP_Mul(&t1,&t7,prime,&t7);
	FPSqr_Mul(&t5,prime,&t1);
	FP_Substract(&t1,&t7,prime,&t1);
	FP_Add(&t1,&t1,prime,&temp);
	FP_Substract(&t7,&temp,prime,&t7);
	FP_Mul(&t5,&t7,prime,&t5);
	FP_Mul(&t2,&t4,prime,&t4);
	FP_Substract(&t5,&t4,prime,&result->y);
	FpDivByTwo(&result->y,prime); 
	MI_Move(&t1,&result->x);
	MI_Move(&t3,&result->z);

	return 0;
}
static int EcFpPointToFixLenOS(EcFpPoint *Q,unsigned int fixedLength,
				unsigned int OSBufferSize, unsigned int *OSLen,
				unsigned char *DString)
{
	DString[0]=0x04;
	MIntToFixedLenOS(&Q->x,fixedLength,OSBufferSize,OSLen,DString+1);
	MIntToFixedLenOS(&Q->y,fixedLength,OSBufferSize,OSLen,
					DString+1+fixedLength);
	*OSLen=1+2*fixedLength;
	return 0;
}

static int ECES_DeFormat(unsigned char *OString, unsigned int OSLen,
				unsigned char *DestOString, unsigned int *DestLen)
{
	int i,j;

	if(OSLen==(PARABYTELEN-1))
	{
		i=0;
		while(OString[i]!=0)
			i++;
		*DestLen=OSLen-i-1;
		memcpy (DestOString, OString+i+1,
				sizeof(unsigned char)*(*DestLen));

	}
	else
	{
		i=PARABYTELEN-2-OSLen;
		for(j=0;j<i;j++)
			DestOString[j]=0;
		*DestLen=PARABYTELEN-2;
		memcpy (DestOString+i, OString,
		sizeof(unsigned char)*(OSLen));
	}
	
	return 0;
}

int KTimesPoint(unsigned int *piPrivateKey,int *piLenOfPriKey,
				 Point *poTempPublicKey,const int iKeyBitLen1,Point *poAddPoint,const int iKeyBitLen2)
{
	
	int i;
	EcFpPoint point0;
	EcFpPoint point1;
	MInt key;
	
	PointToEcFpPoint(poTempPublicKey,&point1);
	for(i=0;i<*piLenOfPriKey;i++)
		key.value[i]=piPrivateKey[i];
	key.length=*piLenOfPriKey;

	if(MI_Compare(&key,&TheCurve.Order)>=0)
		return 0;
    
	ECFpKTimes(&point1,&key,&TheCurve.A,&TheCurve.B,
				&TheCurve.P,&point0);

	EcFpPointToPoint(&point0 ,poAddPoint);

	return 1;


}

#if 1
int Generate_PubKey(unsigned int *piPrivateKey,int piLenOfPriKey,
				 Point *poPublicKey)
{
	int i;
	EcFpPoint point0;
	MInt key;
		
	for(i=0;i<piLenOfPriKey;i++)
		key.value[i]=piPrivateKey[i];
	key.length=piLenOfPriKey;

	if(MI_Compare(&key,&TheCurve.Order)>=0)
		return 0;

	ECFpKTimes_FixP(&TheCurve.BasePoint,pTable1,&key,
		&TheCurve.A, &TheCurve.B,&TheCurve.P, &point0);
	EcFpPointToPoint(&point0 ,poPublicKey);

	return 1;
}
#endif
#if 0
static int Generate_Key(unsigned int *piPrivateKey,int *piLenOfPriKey,
				 Point *poPublicKey,const int iKeyBitLen)
{
	int i;
	EcFpPoint point0;
	MInt key;

	*piLenOfPriKey=(iKeyBitLen+31)/32;
	if(*piLenOfPriKey>TheCurve.Order.length)
		return 0;

	do{
		GenRandomNumberForFixLen(*piLenOfPriKey,&key);
	}while(MI_Compare(&key,&TheCurve.Order)>=0);

	for(i=0;i<*piLenOfPriKey;i++)
		piPrivateKey[i]=key.value[i];

	ECFpKTimes_FixP(&TheCurve.BasePoint,pTable1,&key,
		&TheCurve.A, &TheCurve.B,&TheCurve.P, &point0);
	EcFpPointToPoint(&point0 ,poPublicKey);

	return 1;
}
#endif


#if 1
static int Encrypt_With_Public_Key(BYTE *pbCipherOut,const BYTE *pbPlainIn,
				int  iPlainLenIn,const Point oPubPoint)
{
	unsigned int CipherLen,len,len1;
	MInt k;
	MInt m,c;
	EcFpPoint Q,E1,E2;

	unsigned char buff[80];
	unsigned int bufflen;
	unsigned int fixlen;
	fixlen=TheCurve.P.length*4;

	if(ECES_Format(fixlen,pbPlainIn,iPlainLenIn,buff,&bufflen)!=0)
	{
		// printf("illegal input");
		return 0;
	}
	OctetStringToMInt(buff,bufflen,&m);
	PointToEcFpPoint(&oPubPoint,&Q);

	do
	{
			GenRandomNumber(&k,&TheCurve.Order);
			ECFpKTimes_FixP(&TheCurve.BasePoint,pTable1,&k,
				&TheCurve.A,&TheCurve.B,&TheCurve.P,&E1) ;
			ECFpKTimes(&Q,&k,&TheCurve.A,&TheCurve.B,
				&TheCurve.P,&E2);  
	}while(FP_IsZero(&E2.x));
		
	FP_Mul(&m,&E2.x,&TheCurve.P,&c);
	CipherLen=TheCurve.P.length *4;

	EcFpPointToFixLenOS(&E1,CipherLen,100,&len,pbCipherOut);
	MIntToFixedLenOS(&c,CipherLen,100,&len1,pbCipherOut+len);
	CipherLen=len+len1;
	
	return CipherLen;
}
#endif
#if 1
static int Decrypt_With_Private_Key(BYTE *pbPlainOut,const BYTE *pbCipherIn,
				int iCipherLenIn,const unsigned int *piPrivateKey,
				int iLenOfPriKey)
{
	int CipherLen,i;
	MInt priKey,c,dem;
	EcFpPoint E1,E2;
	unsigned int DeLen;
	unsigned char buff[80];
	unsigned int len;

	priKey.length=iLenOfPriKey;
	for(i=0;i<iLenOfPriKey;i++)
		priKey.value[i]=piPrivateKey[i];

	CipherLen=iCipherLenIn/3;
	if(pbCipherIn[0]!=0x04)
		return 0;

	OctetStringToMInt(pbCipherIn+1,CipherLen,&E1.x );
	OctetStringToMInt(pbCipherIn+CipherLen+1,CipherLen,&E1.y);
	E1.isInfinite=0;
	OctetStringToMInt(pbCipherIn+2*CipherLen+1,CipherLen,&c);

	ECFpKTimes(&E1,&priKey,&TheCurve.A,&TheCurve.B,
		&TheCurve.P,&E2);  
	FP_Div(&c,&E2.x,&TheCurve.P,&dem);

	MIntToOctetString(&dem,100,&len,buff);
	ECES_DeFormat(buff,len,pbPlainOut,&DeLen);
	pbPlainOut[DeLen]=0;
	return DeLen;

}
#endif

#if 1
int Sign_With_Private_Key(unsigned char *pbSignOut,const unsigned char *pbData,int iLenIn,
				const unsigned int *piPrivateKey,int iLenOfPriKey)
{
	int i;
	MInt  k, t, r;
	MInt  t1, s0;
	EcFpPoint point0;
	MInt priKey,hashValue,temp;
	unsigned int signLen,len;

	priKey.length=iLenOfPriKey;
	for(i=0;i<iLenOfPriKey;i++)
		priKey.value[i]=piPrivateKey[i];

	#ifdef MW_SHA256
		SHA2_ComputeHashForMem(pbData,iLenIn,&hashValue);
	#else
		sha256_digest_int(pbData,iLenIn,&hashValue);
	#endif
	MI_ModularReduce (&hashValue,&TheCurve.Order, &temp);
	MI_Move(&temp,&hashValue);

	do{
		do {
			GenRandomNumber(&k,&TheCurve.Order);
			ECFpKTimes_FixP(&TheCurve.BasePoint,pTable1,
				&k,&TheCurve.A,&TheCurve.B,&TheCurve.P,&point0) ;
			MI_ModularReduce (&point0.x,&TheCurve.Order, &r);
		}while (FP_IsZero(&r));

		FP_MulNormal(&priKey, &r,&TheCurve.Order, &t);
		FP_Add (&t, &hashValue, &TheCurve.Order, &t1);
		FP_Invert (&k,&TheCurve.Order, &t);
		FP_MulNormal (&t, &t1, &TheCurve.Order, &s0);
	}while (FP_IsZero(&s0));

	signLen=TheCurve.P.length*4;

	MIntToFixedLenOS(&r,signLen,100,&len,pbSignOut);
	MIntToFixedLenOS(&s0,signLen,100,&len,pbSignOut+signLen); 
	signLen=signLen*2;

	return signLen;
}
#endif

#if 1
int Verify_With_Public_Key(const unsigned char *pbData,int iDataLen,
			const unsigned char *pbSignIn,int iSignInLen,const Point oPubPoint)
{
	int len, status;
	MInt  c,u1,u2,s,r,hashValue,temp;
	EcFpPoint  point2,Q;
	
	len=iSignInLen/2;
	PointToEcFpPoint(&oPubPoint,&Q);
	sha256_digest_int(pbData,iDataLen,&hashValue);
	MI_ModularReduce (&hashValue,&TheCurve.Order, &temp);
	MI_Move(&temp,&hashValue);

	OctetStringToMInt(pbSignIn,len,&r);
	OctetStringToMInt(pbSignIn+len,len,&s);

	FP_Invert(&s,&TheCurve.Order, &c);
	FP_MulNormal (&hashValue, &c,&TheCurve.Order, &u1);
	FP_MulNormal (&r, &c,&TheCurve.Order, &u2);

	ECFpKPAddLQs(&TheCurve.BasePoint,&Q,&u1,&u2,&TheCurve.A,
			     &TheCurve.B,&TheCurve.P,&point2);

	MI_ModularReduce(&point2.x,&TheCurve.Order,&u1);

	if (MI_Compare (&u1, &r) != 0) 	status=0;
	else status=1;

	return (status);
}
#endif

/**********************************
* ECC library function 
***********************************/

int ECC_Init(void)
{
	unsigned char p192[30]= {0xBD,0xB6,0xF4,0xFE,0x3E,0x8B,0x1D,0x9E,
		                     0x0D,0xA8,0xC0,0xD4,0x6F,0x4C,0x31,0x8C,
							 0xEF,0xE4,0xAF,0xE3,0xB6,0xB8,0x55,0x1F};

	unsigned char a192[30]= {0xBB,0x8E,0x5E,0x8F,0xBC,0x11,0x5E,0x13,
		                     0x9F,0xE6,0xA8,0x14,0xFE,0x48,0xAA,0xA6,
							 0xF0,0xAD,0xA1,0xAA,0x5D,0xF9,0x19,0x85};

	unsigned char b192[30]= {0x18,0x54,0xBE,0xBD,0xC3,0x1B,0x21,0xB7,
		                     0xAE,0xFC,0x80,0xAB,0x0E,0xCD,0x10,0xD5,
							 0xB1,0xB3,0x30,0x8E,0x6D,0xBF,0x11,0xC1};

	unsigned char x192[30]= {0x4A,0xD5,0xF7,0x04,0x8D,0xE7,0x09,0xAD,
							 0x51,0x23,0x6D,0xE6,0x5E,0x4D,0x4B,0x48,
							 0x2C,0x83,0x6D,0xC6,0xE4,0x10,0x66,0x40};

	unsigned char y192[30]= {0x02,0xBB,0x3A,0x02,0xD4,0xAA,0xAD,0xAC,
							 0xAE,0x24,0x81,0x7A,0x4C,0xA3,0xA1,0xB0,
							 0x14,0xB5,0x27,0x04,0x32,0xDB,0x27,0xD2};

	unsigned char n192[30]= {0xBD,0xB6,0xF4,0xFE,0x3E,0x8B,0x1D,0x9E,
							 0x0D,0xA8,0xC0,0xD4,0x0F,0xC9,0x62,0x19,
							 0x5D,0xFA,0xE7,0x6F,0x56,0x56,0x46,0x77};
	
	
	OctetStringToMInt(p192,PARABYTELEN,&TheCurve.P); /*p*/
	OctetStringToMInt(a192,PARABYTELEN,&TheCurve.A); /*a*/
	OctetStringToMInt(b192,PARABYTELEN,&TheCurve.B); /*p*/
	
	OctetStringToMInt(x192,PARABYTELEN,&TheCurve.BasePoint.x); /*x*/
	OctetStringToMInt(y192,PARABYTELEN,&TheCurve.BasePoint.y); /*y*/
	OctetStringToMInt(n192,PARABYTELEN,&TheCurve.Order);/*N*/
	FpMul=FP_MulNormal;

	gettalbe();
		
	return 1;
}
#if 1
int ecc192_genkey_pc(unsigned char *priv_key, unsigned char *pub_key)
{
	unsigned int pubkey_x[24]={0,};
	unsigned int pubkey_y[24]={0,};
	unsigned int temp_pubkey_len =0;
	unsigned int temp_prikey_len =0;
	Point oPubPoint;
	MInt key;
	EcFpPoint point0;
	/*产生临时私钥*/
	oPubPoint.x = pubkey_x;
	oPubPoint.y = pubkey_y;
	
	do{
		GenRandomNumberForFixLen(6,&key);
	}while(MI_Compare(&key,&TheCurve.Order)>=0);

	
	ECFpKTimes_FixP(&TheCurve.BasePoint,pTable1,&key,
		&TheCurve.A, &TheCurve.B,&TheCurve.P, &point0);
	EcFpPointToPoint(&point0 ,&oPubPoint);

	PubKeyToOctetString(	&oPubPoint,
							48,
							&temp_pubkey_len,
							pub_key+1);
	pub_key[0] = 0x04;
#if 0	
	for(i=0;i<6;i++)
		PrivateKey[i]=key.value[i];
#endif
	PriKeyToOctetString(key.value, 6,  24, &temp_prikey_len, priv_key);
	return 0;
}
#endif
#if 1
int ecc192_genkey(unsigned char *priv_key, unsigned char *pub_key)
{
	unsigned int pubkey_x[IWN_PUBKEY_INTLEN]={0,};
	unsigned int pubkey_y[IWN_PUBKEY_INTLEN]={0,};
	
	int  i =0;
	unsigned int temp_pubkey_len =0;
	unsigned int temp_prikey_len =0;
	Point oPubPoint;
	MInt key;
	EcFpPoint point0;
	/*产生临时私钥*/
	oPubPoint.x = pubkey_x;
	oPubPoint.y = pubkey_y;
	
	GenRandomNumberForFixLen(6,&key);
	if(MI_Compare(&key,&TheCurve.Order)>=0)
	{
		for(i = TheCurve.Order.length -1; i>=0; i--)
			key.value[i] &= TheCurve.Order.value[i];
	}
	
	ECFpKTimes_FixP(&TheCurve.BasePoint,pTable1,&key,
		&TheCurve.A, &TheCurve.B,&TheCurve.P, &point0);
	EcFpPointToPoint(&point0 ,&oPubPoint);

	PubKeyToOctetString(	&oPubPoint,
							48,
							&temp_pubkey_len,
							pub_key+1);
	pub_key[0] = 0x04;
#if 0	
	for(i=0;i<6;i++)
		PrivateKey[i]=key.value[i];
#endif
	PriKeyToOctetString(key.value, 6,  24, &temp_prikey_len, priv_key);
	return 0;
}
#endif

#if 1
int ecc192_genkey_MB(unsigned char *priv_key, unsigned char *pub_key)
{
	unsigned int pub_x[6]={0,};
	unsigned int pub_y[6]={0,};
	unsigned int t_priv[24] = {0,};
	
	
	int publen =0;
	int privlen =0;
	Point oPubPoint;
	oPubPoint.x = pub_x;
	oPubPoint.y = pub_y;

#ifdef MB_ECC
	GetRandPri(priv_key, 24);
#endif
	priv_key[0] &= 0xBC;

	OctetStringToPriKey( priv_key, 24, t_priv, &privlen);

	Generate_PubKey(t_priv,6, &oPubPoint);
	PubKeyToOctetString( &oPubPoint, 48, (unsigned int *)&publen, pub_key+1);
	pub_key[0]=0x04;
	return 0;
}
#endif
int ecc192_genpubkey(unsigned char *priv_key, unsigned char *pub_key)
{
	unsigned int pub_x[6]={0,};
	unsigned int pub_y[6]={0,};
	unsigned int t_priv[24] = {0,};
	
	
	int publen =0;
	int privlen =0;
	Point oPubPoint;
	oPubPoint.x = pub_x;
	oPubPoint.y = pub_y;

	OctetStringToPriKey( priv_key, 24, t_priv, &privlen);

	Generate_PubKey(t_priv,6, &oPubPoint);
	PubKeyToOctetString( &oPubPoint, 48, (unsigned int *)&publen, pub_key+1);
	pub_key[0]=0x04;
	return 0;
}

#if 0
int ecc192_generate_pubKey(unsigned char *priv_key, unsigned char *pub_key)
{
	unsigned int pubkey_x[24]={0,};
	unsigned int pubkey_y[24]={0,};
	
	int  i =0;
	unsigned int temp_pubkey_len =0;
	unsigned int temp_prikey_len =0;
	Point oPubPoint;
	MInt key;
	EcFpPoint point0;
	/*产生临时私钥*/
	oPubPoint.x = pubkey_x;
	oPubPoint.y = pubkey_y;
#if 0	
	do{
		GenRandomNumberForFixLen(6,&key);
	}while(MI_Compare(&key,&TheCurve.Order)>=0);
#endif

	OctetStringToPriKey(priv_key, 24, key.value, (int *)&temp_prikey_len);
	
	ECFpKTimes_FixP(&TheCurve.BasePoint,pTable1,&key,
		&TheCurve.A, &TheCurve.B,&TheCurve.P, &point0);
	EcFpPointToPoint(&point0 ,&oPubPoint);

	PubKeyToOctetString(	&oPubPoint,
							48,
							&temp_pubkey_len,
							pub_key+1);
	pub_key[0] = 0x04;
#if 0	
	for(i=0;i<6;i++)
		PrivateKey[i]=key.value[i];
	PriKeyToOctetString(key.value, 6,  24, &temp_prikey_len, priv_key);
#endif
	return 0;
}
#endif

#if 1
int  ecc192_sign(const unsigned char *priv_key, const unsigned char *in, int in_len, unsigned char *out)
{
	int out_len = 0;
	unsigned int PrivateKey[IWN_PRIKEY_INTLEN+1]={0,};
	int LenOfPrivKey = 0;
#if 0	
	DPrint_string("priv_key",priv_key,24);
	DPrint_string("data", in,  in_len);
#endif	
	OctetStringToPriKey(priv_key, 24, PrivateKey, &LenOfPrivKey);
	out_len = Sign_With_Private_Key(out, in, in_len,	PrivateKey, LenOfPrivKey);
	return out_len;
}
#endif

#if 1
int   ecc192_verify(const unsigned char *pub_key, const unsigned char *in ,  int in_len, const unsigned char *sign,int sign_len)
{
	int ret = 0;

	unsigned int pubkey_x[IWN_PUBKEY_INTLEN]={0,};
	unsigned int pubkey_y[IWN_PUBKEY_INTLEN]={0,};

	Point oPubPoint;

	oPubPoint.x = pubkey_x;
	oPubPoint.y = pubkey_y;
#if 0
	DPrint_string("pub_key", pub_key, 48);
#endif	
	OctetStringToPubKey((const unsigned char *)pub_key+1, 48, &oPubPoint);
#if 0
	DPrint_string("x", oPubPoint.x, 24);
	DPrint_string("y", oPubPoint.y, 24);
#endif
	ret = Verify_With_Public_Key(in, in_len, sign, sign_len, oPubPoint);

	return ret;

}
#endif
#if 1
int ecc192_encrypt(unsigned char *pout, const unsigned char *pin, int  len,unsigned char *pub_key)
{
	unsigned int pubkey_x[IWN_PUBKEY_INTLEN]={0,};
	unsigned int pubkey_y[IWN_PUBKEY_INTLEN]={0,};

	Point oPubPoint;

	oPubPoint.x = pubkey_x;
	oPubPoint.y = pubkey_y;
	OctetStringToPubKey((const unsigned char *)pub_key+1, 48, &oPubPoint);
	return Encrypt_With_Public_Key(pout, pin, len, oPubPoint);
}
#endif
#if 1
int ecc192_decrypt(unsigned char *pout, const unsigned char *pin, int  len,unsigned char *priv_key)
{
	unsigned int PrivateKey[IWN_PUBKEY_INTLEN]={0,};
	int LenOfPrivKey = 0;
#if 0	
	DPrint_string("priv_key", priv_key,24);
	DPrint_string("in", in,  in_len);
#endif	
	OctetStringToPriKey(priv_key, 24, PrivateKey, &LenOfPrivKey);
	
	return Decrypt_With_Private_Key(pout,pin, len, (const unsigned int *)PrivateKey,LenOfPrivKey);
}
#endif
int ecc192_ecdh(const unsigned char * priv_key, const unsigned char *pub_key, unsigned char * ecdhkey)
{
	unsigned int pubkey_x[IWN_PUBKEY_INTLEN];
	unsigned int pubkey_y[IWN_PUBKEY_INTLEN];
	Point oPubPoint;
	unsigned int x[IWN_PUBKEY_INTLEN];
	unsigned int y[IWN_PUBKEY_INTLEN];

	unsigned int PrivateKey[IWN_PRIKEY_INTLEN+1];
	int LenOfPrivKey = 0;
	int private_len_int = 6;

	int ret = 0;
	Point poutPublicKey;

	poutPublicKey.x = x;
	poutPublicKey.y = y;


	oPubPoint.x = pubkey_x;
	oPubPoint.y = pubkey_y;
	OctetStringToPubKey((const unsigned char *)pub_key+1, 48, &oPubPoint);

#if 0	
	DPrint_string("priv_key", priv_key,24);
	DPrint_string("in", in,  in_len);
#endif	
	OctetStringToPriKey(priv_key, 24, PrivateKey, &LenOfPrivKey);

	ret = KTimesPoint(PrivateKey,//y
                &private_len_int,     // int个数
                &oPubPoint,//x*P
                192,  // 192
                &poutPublicKey,    //输出的x*y*P
                192   //192
                );
	if(ret != 0)
		PriKeyToOctetString(poutPublicKey.x, 6,  24, (unsigned int *)&ret, ecdhkey);
	return ret;
}

