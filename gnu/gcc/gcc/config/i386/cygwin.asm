/* stuff needed for libgcc on win32.
 *
 *   Copyright (C) 1996, 1998, 2001, 2003 Free Software Foundation, Inc.
 *   Written By Steve Chamberlain
 * 
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 * 
 * In addition to the permissions in the GNU General Public License, the
 * Free Software Foundation gives you unlimited permission to link the
 * compiled version of this file with other programs, and to distribute
 * those programs without any restriction coming from the use of this
 * file.  (The General Public License restrictions do apply in other
 * respects; for example, they cover modification of the file, and
 * distribution when not linked into another program.)
 * 
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 * 
 *    As a special exception, if you link this library with files
 *    compiled with GCC to produce an executable, this does not cause
 *    the resulting executable to be covered by the GNU General Public License.
 *    This exception does not however invalidate any other reasons why
 *    the executable file might be covered by the GNU General Public License.
 */

#ifdef L_chkstk

/* Function prologue calls _alloca to probe the stack when allocating more
   than CHECK_STACK_LIMIT bytes in one go.  Touching the stack at 4K
   increments is necessary to ensure that the guard pages used
   by the OS virtual memory manger are allocated in correct sequence.  */

	.global ___chkstk
	.global	__alloca
___chkstk:
__alloca:
	pushl  %ecx		/* save temp */
	movl   %esp,%ecx	/* get sp */
	addl   $0x8,%ecx	/* and point to return addr */

probe: 	cmpl   $0x1000,%eax	/* > 4k ?*/
	jb    done		

	subl   $0x1000,%ecx  		/* yes, move pointer down 4k*/
	orl    $0x0,(%ecx)   		/* probe there */
	subl   $0x1000,%eax  	 	/* decrement count */
	jmp    probe           	 	/* and do it again */

done: 	subl   %eax,%ecx	   
	orl    $0x0,(%ecx)	/* less that 4k, just peek here */

	movl   %esp,%eax
	movl   %ecx,%esp	/* decrement stack */

	movl   (%eax),%ecx	/* recover saved temp */
	movl   4(%eax),%eax	/* get return address */
	jmp    *%eax	
#endif
