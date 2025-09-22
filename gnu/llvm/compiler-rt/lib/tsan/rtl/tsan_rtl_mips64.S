.section .text
.set noreorder

.hidden __tsan_setjmp
.comm _ZN14__interception11real_setjmpE,8,8
.globl setjmp
.type setjmp, @function
setjmp:

  // save env parameters
  daddiu $sp,$sp,-40
  sd $s0,32($sp)
  sd $ra,24($sp)
  sd $fp,16($sp)
  sd $gp,8($sp)

  // calculate and save pointer to GOT
  lui $gp,%hi(%neg(%gp_rel(setjmp)))
  daddu $gp,$gp,$t9
  daddiu $gp,$gp,%lo(%neg(%gp_rel(setjmp)))
  move $s0,$gp

  // save jmp_buf
  sd $a0,0($sp)

  // obtain $sp
  dadd $a0,$zero,$sp

  // call tsan interceptor
  jal __tsan_setjmp
  daddiu $a1,$a0,40

  // restore jmp_buf
  ld $a0,0($sp)

  // restore gp
  move $gp,$s0

  // load pointer of libc setjmp to t9
  dla $t9,(_ZN14__interception11real_setjmpE) 

  // restore env parameters
  ld $gp,8($sp)
  ld $fp,16($sp)
  ld $ra,24($sp)
  ld $s0,32($sp)
  daddiu $sp,$sp,40

  // tail jump to libc setjmp
  ld $t9,0($t9)
  jr $t9
  nop

.size setjmp, .-setjmp

.hidden __tsan_setjmp
.globl _setjmp
.comm _ZN14__interception12real__setjmpE,8,8
.type _setjmp, @function
_setjmp:

  // Save env parameters
  daddiu $sp,$sp,-40
  sd $s0,32($sp)
  sd $ra,24($sp)
  sd $fp,16($sp)
  sd $gp,8($sp)

  // calculate and save pointer to GOT
  lui $gp,%hi(%neg(%gp_rel(_setjmp)))
  daddu $gp,$gp,$t9
  daddiu $gp,$gp,%lo(%neg(%gp_rel(_setjmp)))
  move $s0,$gp

  // save jmp_buf
  sd $a0,0($sp)

  // obtain $sp
  dadd $a0,$zero,$sp

  // call tsan interceptor
  jal __tsan_setjmp
  daddiu $a1,$a0,40

  // restore jmp_buf
  ld $a0,0($sp)

  // restore gp
  move $gp,$s0

  // load pointer of libc _setjmp to t9
  dla $t9,(_ZN14__interception12real__setjmpE)

  // restore env parameters
  ld $gp,8($sp)
  ld $fp,16($sp)
  ld $ra,24($sp)
  ld $s0,32($sp)
  daddiu $sp,$sp,40

  // tail jump to libc _setjmp
  ld $t9,0($t9)
  jr $t9
  nop

.size _setjmp, .-_setjmp

.hidden __tsan_setjmp
.globl sigsetjmp
.comm _ZN14__interception14real_sigsetjmpE,8,8
.type sigsetjmp, @function
sigsetjmp:

  // Save env parameters
  daddiu $sp,$sp,-48
  sd $s0,40($sp)
  sd $ra,32($sp)
  sd $fp,24($sp)
  sd $gp,16($sp)

  // calculate and save pointer to GOT
  lui $gp,%hi(%neg(%gp_rel(sigsetjmp)))
  daddu $gp,$gp,$t9
  daddiu $gp,$gp,%lo(%neg(%gp_rel(sigsetjmp)))
  move $s0,$gp

  // save jmp_buf and savesig
  sd $a0,0($sp)
  sd $a1,8($sp)

  // obtain $sp
  dadd $a0,$zero,$sp

  // call tsan interceptor
  jal __tsan_setjmp
  daddiu $a1,$a0,48

  // restore jmp_buf and savesig
  ld $a0,0($sp)
  ld $a1,8($sp)

  // restore gp
  move $gp,$s0

  // load pointer of libc sigsetjmp to t9
  dla $t9,(_ZN14__interception14real_sigsetjmpE) 

  // restore env parameters
  ld $gp,16($sp)
  ld $fp,24($sp)
  ld $ra,32($sp)
  ld $s0,40($sp)
  daddiu $sp,$sp,48

  // tail jump to libc sigsetjmp
  ld $t9,0($t9)
  jr $t9
  nop

.size sigsetjmp, .-sigsetjmp

.hidden __tsan_setjmp
.comm _ZN14__interception16real___sigsetjmpE,8,8
.globl __sigsetjmp
.type __sigsetjmp, @function
__sigsetjmp:

  // Save env parameters
  daddiu $sp,$sp,-48
  sd $s0,40($sp)
  sd $ra,32($sp)
  sd $fp,24($sp)
  sd $gp,16($sp)

  // calculate and save pointer to GOT
  lui $gp,%hi(%neg(%gp_rel(__sigsetjmp)))
  daddu $gp,$gp,$t9
  daddiu $gp,$gp,%lo(%neg(%gp_rel(__sigsetjmp)))
  move $s0,$gp

  // save jmp_buf and savesig
  sd $a0,0($sp)
  sd $a1,8($sp)

  // obtain $sp
  dadd $a0,$zero,$sp

  // call tsan interceptor
  jal __tsan_setjmp
  daddiu $a1,$a0,48

  // restore jmp_buf and savesig
  ld $a0,0($sp)
  ld $a1,8($sp)

  // restore gp
  move $gp,$s0

  // load pointer to libc __sigsetjmp in t9
  dla $t9,(_ZN14__interception16real___sigsetjmpE)

  // restore env parameters
  ld $gp,16($sp)
  ld $fp,24($sp)
  ld $ra,32($sp)
  ld $s0,40($sp)
  daddiu $sp,$sp,48

  // tail jump to libc __sigsetjmp
  ld $t9,0($t9)
  jr $t9
  nop

.size __sigsetjmp, .-__sigsetjmp
