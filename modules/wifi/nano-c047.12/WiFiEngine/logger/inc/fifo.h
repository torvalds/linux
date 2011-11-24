#ifndef _FIFO_H_
#define _FIFO_H_

struct fifo_t {
   unsigned int size;
   unsigned int in;
   unsigned int out;
   char *buffer;
};

void fifo_init(struct fifo_t*, char* buffer, unsigned int size);
unsigned int fifo_inc_read( struct fifo_t *lg, unsigned int len);
unsigned int fifo_len(struct fifo_t*);
unsigned int fifo_get(struct fifo_t*, char *data, unsigned int len);
unsigned int fifo_put(struct fifo_t*, const char *data, unsigned int len);
unsigned int fifo_buff_size(struct fifo_t *fifo);

#endif /* _FIFO_H_ */
