#ifndef  JPEG_PARSER_H_
#define JPEG_PARSER_H_



#define JPEG_INFO               	AV_SCRATCH_0
#define JPEG_PIC_WIDTH          AV_SCRATCH_1
#define JPEG_PIC_HEIGHT         AV_SCRATCH_2
#define PADDINGSIZE		1024
#define JPEG_TAG		0xff
#define JPEG_TAG_SOI	0xd8
#define JPEG_TAG_SOF0	0xc0
#define JPEG_TAG_EOI		0xd9
#define JPEG_TAG_SOS		0xda
#define MREG_DECODE_PARAM   AV_SCRATCH_2 /* bit 0-3: pico_addr_mode */
                                         /* bit 15-4: reference height */
#define MREG_TO_AMRISC      AV_SCRATCH_8
#define MREG_FROM_AMRISC    AV_SCRATCH_9

#define PICINFO_BUF_IDX_MASK        0x0007
#define PICINFO_INTERLACE           0x0020
#define PICINFO_INTERLACE_TOP       0x0010

#define  JPEG_INVALID_FILE_SIZE	 0x2000000 
enum {
	PIC_NA = 0,
	PIC_DECODED = 1,
	PIC_FETCHED = 2
};
typedef  struct {
	vframe_t  	vf;
	volatile u32 	state;
	u32 __iomem*	vaddr;
	u32  canvas_index;
	
}jpeg_private_t;
static int jpeg_init(logo_object_t *logo) ;
static  int  jpeg_decode(logo_object_t *plogo);
static int jpeg_deinit(logo_object_t *plogo);
#endif 
