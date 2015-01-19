#ifndef  BMP_PARSER_H_
#define BMP_PARSER_H_

//parser
#define  U32   		unsigned int 
#define  U16   		unsigned short 
#define  U8			unsigned char
#define  INT32		int
#define	INT16		short
#define  UINT8 		U8
#define  UINT16		U16
#define  UINT32		U32
#pragma pack(1)
typedef struct tagBITMAPFILEHEADER
{
	U16	bfType;  //BM
	U32 bfSize;   //file size 
	U16	bfReserved1;  //0
	U16	bfReserved2;  //0
	U32 bfOffBits;  	//bitmap data offset to  file header	
} BITMAPFILEHEADER;

typedef struct tagBITMAPINFOHEADER{
	U32 biSize;  //this structure occupy size 
	U32	biWidth; //bitmap width (pixel unit)
	U32 biHeight; //bitmap height (pixel unit)
	U16 biPlanes; // 1
	U16 biBitCount;//bits of erery pixel . must be one of follow values 1(double color)
			// 4(16color) 8(256 color) 24(true color)
	U32 biCompression; // bitmap compresstion type must be one of 0 (uncompress)
					  // 1(BI_RLE8) 2(BI_RLE4)
	U32 biSizeImage; 	// bitmap size 
	U32	biXPelsPerMeter; //bitmap horizontal resolution.
	U32	biYPelsPerMeter; //bitmap vertical resolution.
	U32 biClrUsed;	//bitmap used color number.
	U32 biClrImportant;//bitmap most important color number during display process.
} BITMAPINFOHEADER;
#pragma pack()

typedef  struct {
	BITMAPFILEHEADER *bmp_file_header;
	BITMAPINFOHEADER *bmp_info_header;
}bmp_header_t;

static int bmp_init(logo_object_t *logo) ;
static  int  bmp_decode(logo_object_t *plogo);
static int  bmp_deinit(logo_object_t *plogo);


#endif
