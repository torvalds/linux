#ifndef  LOGO_PARSER_H
#define  LOGO_PARSER_H

//parser
typedef  struct {
	int  (*init)(logo_object_t *logo);
	int  (*decode)(logo_object_t *logo);
	int  (*deinit)(logo_object_t *logo);
	
}logo_parser_op_t;

typedef  struct{
	int  color_depth;
}bmp_decoder_t;

typedef  struct{
	u32  component;
	u32  out_canvas_index;
}jpg_decoder_t;

typedef  union{
	bmp_decoder_t bmp;
	jpg_decoder_t	jpg;
}decoder_t;

typedef  struct{
	unsigned int	width;
	unsigned int	height;
	unsigned int	color_info;
	int	size; //file size
}pic_info_t;

struct logo_parser{
	char  name[10];
	logo_parser_op_t  op;
	char *output_addr;
	decoder_t  decoder;
	output_dev_t out_dev; //?maybe not useful
	pic_info_t	 logo_pic_info;
	void *priv;
};
typedef  struct logo_parser  logo_parser_t;
typedef  struct {
	logo_parser_t *parser;
	struct list_head  list;
}parser_list_t;

/**************************************************************
************************ function define part ************************
***************************************************************/
 extern int start_logo(void) ;
 extern int exit_logo(logo_object_t *logo);
 //all kind of parser setup .
 extern int bmp_setup(void);
 extern int jpeg_setup(void);
#endif
