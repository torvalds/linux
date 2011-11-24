#include <stdio.h>
#include <ctype.h>

/*! 
 * @internal
 * @brief Formats a buffer on stdout.
 *
 * @param data pointer to buffer to format
 * @param len size of data
 * @param prefix string prepended to each output line
 */
void
nrx_printbuf(const void *data, size_t len, const char *prefix)
{
   int i, j;
   const unsigned char *p = (const unsigned char *)data;
   for(i = 0; i < len; i += 16) {
      printf("%s %04x: ", prefix, i);
      for(j = 0; j < 16; j++) {
         if(i + j < len)
            printf("%02x ", p[i+j]);
         else
            printf("   ");
      }
      printf(" : ");
      for(j = 0; j < 16; j++) {
         if(i + j < len) {
            if(isprint(p[i+j]))
               printf("%c", p[i+j]);
            else
               printf(".");
         } else
            printf(" ");
      }
      printf("\n");
   }
}

