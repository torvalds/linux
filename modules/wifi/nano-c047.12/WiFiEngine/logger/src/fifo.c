
#include "driverenv.h"
#include "fifo.h"

void fifo_init(struct fifo_t *lg, char *buffer, unsigned int size) {
   lg->size = size;
   lg->in = lg->out = 0;
   lg->buffer = buffer;
}

unsigned int fifo_len(struct fifo_t *lg) {
   if(lg->in < lg->out)
      return lg->size + lg->in - lg->out;
   else
      return lg->in - lg->out;
}

unsigned int fifo_buff_size(struct fifo_t *fifo) { return fifo->size; }

unsigned int fifo_inc_read(struct fifo_t *lg, unsigned int len) {
   lg->out = (lg->out + len) % lg->size;
   return len;
}

unsigned int fifo_get(struct fifo_t *lg, char *data, unsigned int len) {
   unsigned int l;

   l = DE_MIN( len, (lg->size - lg->out) );

   DE_MEMCPY(data, lg->buffer+lg->out, l);

   if(l<len)
      DE_MEMCPY(data+l, lg->buffer , len - l);

   fifo_inc_read(lg,len);

   return len;
}

unsigned int fifo_put(struct fifo_t *lg, const char *data, unsigned int len) {
   unsigned int l;

   /* sanity check */
   if(len > lg->size)
      return 0;

   l = DE_MIN( len , lg->size - lg->in );

   DE_MEMCPY(lg->buffer + lg->in, data, l);

   if( l < len )
      DE_MEMCPY( lg->buffer, data+l, len-l);

   lg->in = (lg->in + len) % lg->size;

   return len;
}
