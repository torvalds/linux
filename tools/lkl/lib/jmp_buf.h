#ifndef _LKL_LIB_JMP_BUF_H
#define _LKL_LIB_JMP_BUF_H

int jmp_buf_set(struct lkl_jmp_buf *jmpb);
void jmp_buf_longjmp(struct lkl_jmp_buf *jmpb, int val);

#endif
