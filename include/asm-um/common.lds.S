#include <asm-generic/vmlinux.lds.h>

  .fini      : { *(.fini)    } =0x9090
  _etext = .;
  PROVIDE (etext = .);

  . = ALIGN(4096);
  _sdata = .;
  PROVIDE (sdata = .);

  . = ALIGN(16);		/* Exception table */
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  RODATA

  .unprotected : { *(.unprotected) }
  . = ALIGN(4096);
  PROVIDE (_unprotected_end = .);

  . = ALIGN(4096);
  __uml_setup_start = .;
  .uml.setup.init : { *(.uml.setup.init) }
  __uml_setup_end = .;
	
  __uml_help_start = .;
  .uml.help.init : { *(.uml.help.init) }
  __uml_help_end = .;
	
  __uml_postsetup_start = .;
  .uml.postsetup.init : { *(.uml.postsetup.init) }
  __uml_postsetup_end = .;
	
  __setup_start = .;
  .init.setup : { *(.init.setup) }
  __setup_end = .;

  . = ALIGN(32);
  __per_cpu_start = . ; 
  .data.percpu : { *(.data.percpu) }
  __per_cpu_end = . ;
	
  __initcall_start = .;
  .initcall.init : {
	*(.initcall1.init) 
	*(.initcall2.init) 
	*(.initcall3.init) 
	*(.initcall4.init) 
	*(.initcall5.init) 
	*(.initcall6.init) 
	*(.initcall7.init)
  }
  __initcall_end = .;

  __con_initcall_start = .;
  .con_initcall.init : { *(.con_initcall.init) }
  __con_initcall_end = .;

  __uml_initcall_start = .;
  .uml.initcall.init : { *(.uml.initcall.init) }
  __uml_initcall_end = .;
  __init_end = .;

  SECURITY_INIT

  __exitcall_begin = .;
  .exitcall : { *(.exitcall.exit) }
  __exitcall_end = .;

  __uml_exitcall_begin = .;
  .uml.exitcall : { *(.uml.exitcall.exit) }
  __uml_exitcall_end = .;

  . = ALIGN(4);
  __alt_instructions = .;
  .altinstructions : { *(.altinstructions) }
  __alt_instructions_end = .;
  .altinstr_replacement : { *(.altinstr_replacement) }
  /* .exit.text is discard at runtime, not link time, to deal with references
     from .altinstructions and .eh_frame */
  .exit.text : { *(.exit.text) }
  .exit.data : { *(.exit.data) }

  __preinit_array_start = .;
  .preinit_array : { *(.preinit_array) }
  __preinit_array_end = .;
  __init_array_start = .;
  .init_array : { *(.init_array) }
  __init_array_end = .;
  __fini_array_start = .;
  .fini_array : { *(.fini_array) }
  __fini_array_end = .;

   . = ALIGN(4096);
  __initramfs_start = .;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;

  /* Sections to be discarded */
  /DISCARD/ : {
 	*(.exitcall.exit)
  }

