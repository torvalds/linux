#ifndef VM_API_INCLUDE_
#define VM_API_INCLUDE_

typedef struct vm_output_para{
	int width;
	int height;
	int bytesperline;
	int v4l2_format;
	int index;
	int v4l2_memory;
	int zoom;     // set -1 as invalid
	int mirror;   // set -1 as invalid
	int angle;
	unsigned vaddr;
	unsigned int ext_canvas;
}vm_output_para_t;
struct videobuf_buffer;
int vm_fill_buffer(struct videobuf_buffer* vb , vm_output_para_t* para);

#ifdef CONFIG_CMA

int vm_init_buf(size_t size);
void vm_deinit_buf(void);
void vm_reserve_cma(void);
#endif

#endif /* VM_API_INCLUDE_ */
