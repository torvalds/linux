#!/usr/bin/env perl

use strict;

#----------------------------------------------------------------------
# Globals
#----------------------------------------------------------------------
our $unimplemented_str = "UNIMPLEMENTED";
our $success_str = "OK";
our $swap = 1;
our $addr_size = 4;
our $thread_suffix_supported = 0;
our $max_bytes_per_line = 32;
our $addr_format = sprintf("0x%%%u.%ux", $addr_size*2, $addr_size*2);
our $pid_format = "%04.4x";
our $tid_format = "%04.4x";
our $reg8_href = { extract => \&get8, format => "0x%2.2x" };
our $reg16_href = { extract => \&get16, format => "0x%4.4x" };
our $reg32_href = { extract => \&get32, format => "0x%8.8x" };
our $reg64_href = { extract => \&get64, format => "0x%s" };
our $reg80_href = { extract => \&get80, format => "0x%s" };
our $reg128_href = { extract => \&get128, format => "0x%s" };
our $reg256_href = { extract => \&get256, format => "0x%s" };
our $float32_href = { extract => \&get32, format => "0x%8.8x" };
our $float64_href = { extract => \&get64, format => "0x%s" };
our $float96_href = { extract => \&get96, format => "0x%s" };
our $curr_cmd = undef;
our $curr_full_cmd = undef;
our %packet_times;
our $curr_time = 0.0;
our $last_time = 0.0;
our $base_time = 0.0;
our $packet_start_time = 0.0;
our $reg_cmd_reg;
our %reg_map = (
	'i386-gdb' => [ 
	    { name => 'eax',    info => $reg32_href     },
        { name => 'ecx',    info => $reg32_href     },
        { name => 'edx',    info => $reg32_href     },
        { name => 'ebx',    info => $reg32_href     },
        { name => 'esp',    info => $reg32_href     },
        { name => 'ebp',    info => $reg32_href     },
        { name => 'esi',    info => $reg32_href     },
        { name => 'edi',    info => $reg32_href     },
        { name => 'eip',    info => $reg32_href     },
        { name => 'eflags', info => $reg32_href     },
        { name => 'cs',     info => $reg32_href     },
        { name => 'ss',     info => $reg32_href     },
        { name => 'ds',     info => $reg32_href     },
        { name => 'es',     info => $reg32_href     },
        { name => 'fs',     info => $reg32_href     },
        { name => 'gs',     info => $reg32_href     },
        { name => 'st0',    info => $reg80_href     },
        { name => 'st1',    info => $reg80_href     },
        { name => 'st2',    info => $reg80_href     },
        { name => 'st3',    info => $reg80_href     },
        { name => 'st4',    info => $reg80_href     },
        { name => 'st5',    info => $reg80_href     },
        { name => 'st6',    info => $reg80_href     },
        { name => 'st7',    info => $reg80_href     },
        { name => 'fctrl',  info => $reg32_href     },
        { name => 'fstat',  info => $reg32_href     },
        { name => 'ftag',   info => $reg32_href     },
        { name => 'fiseg',  info => $reg32_href     },
        { name => 'fioff',  info => $reg32_href     },
        { name => 'foseg',  info => $reg32_href     },
        { name => 'fooff',  info => $reg32_href     },
        { name => 'fop',    info => $reg32_href     },
    	{ name => 'xmm0',   info => $reg128_href    },
    	{ name => 'xmm1',   info => $reg128_href    },
    	{ name => 'xmm2',   info => $reg128_href    },
    	{ name => 'xmm3',   info => $reg128_href    },
    	{ name => 'xmm4',   info => $reg128_href    },
    	{ name => 'xmm5',   info => $reg128_href    },
    	{ name => 'xmm6',   info => $reg128_href    },
    	{ name => 'xmm7',   info => $reg128_href    },
    	{ name => 'mxcsr',  info => $reg32_href     },
        { name => 'mm0',    info => $reg64_href     },
        { name => 'mm1',    info => $reg64_href     },
        { name => 'mm2',    info => $reg64_href     },
        { name => 'mm3',    info => $reg64_href     },
        { name => 'mm4',    info => $reg64_href     },
        { name => 'mm5',    info => $reg64_href     },
        { name => 'mm6',    info => $reg64_href     },
        { name => 'mm7',    info => $reg64_href     },
    ],
    
    'i386-lldb' => [
        { name => 'eax',          info => $reg32_href   },
        { name => 'ebx',          info => $reg32_href   },
        { name => 'ecx',          info => $reg32_href   },
        { name => 'edx',          info => $reg32_href   },
        { name => 'edi',          info => $reg32_href   },
        { name => 'esi',          info => $reg32_href   },
        { name => 'ebp',          info => $reg32_href   },
        { name => 'esp',          info => $reg32_href   },
        { name => 'ss',           info => $reg32_href   },
        { name => 'eflags',       info => $reg32_href   },
        { name => 'eip',          info => $reg32_href   },
        { name => 'cs',           info => $reg32_href   },
        { name => 'ds',           info => $reg32_href   },
        { name => 'es',           info => $reg32_href   },
        { name => 'fs',           info => $reg32_href   },
        { name => 'gs',           info => $reg32_href   },
    	{ name => 'fctrl',        info => $reg16_href   },
    	{ name => 'fstat',        info => $reg16_href   },
    	{ name => 'ftag',         info => $reg8_href    },
    	{ name => 'fop',          info => $reg16_href   },
    	{ name => 'fioff',        info => $reg32_href   },
    	{ name => 'fiseg',        info => $reg16_href   },
    	{ name => 'fooff',        info => $reg32_href   },
    	{ name => 'foseg',        info => $reg16_href   },
    	{ name => 'mxcsr',        info => $reg32_href   },
    	{ name => 'mxcsrmask',    info => $reg32_href   },
    	{ name => 'stmm0',        info => $reg80_href   },
    	{ name => 'stmm1',        info => $reg80_href   },
    	{ name => 'stmm2',        info => $reg80_href   },
    	{ name => 'stmm3',        info => $reg80_href   },
    	{ name => 'stmm4',        info => $reg80_href   },
    	{ name => 'stmm5',        info => $reg80_href   },
    	{ name => 'stmm6',        info => $reg80_href   },
    	{ name => 'stmm7',        info => $reg80_href   },
    	{ name => 'xmm0',         info => $reg128_href  },
    	{ name => 'xmm1',         info => $reg128_href  },
    	{ name => 'xmm2',         info => $reg128_href  },
    	{ name => 'xmm3',         info => $reg128_href  },
    	{ name => 'xmm4',         info => $reg128_href  },
    	{ name => 'xmm5',         info => $reg128_href  },
    	{ name => 'xmm6',         info => $reg128_href  },
    	{ name => 'xmm7',         info => $reg128_href  },
    	{ name => 'trapno',       info => $reg32_href   },
    	{ name => 'err',          info => $reg32_href   },
    	{ name => 'faultvaddr',   info => $reg32_href   },
    ],
    
    'arm-gdb' => [
        { name => 'r0'      , info => $reg32_href   },
        { name => 'r1'      , info => $reg32_href   },
        { name => 'r2'      , info => $reg32_href   },
        { name => 'r3'      , info => $reg32_href   },
        { name => 'r4'      , info => $reg32_href   },
        { name => 'r5'      , info => $reg32_href   },
        { name => 'r6'      , info => $reg32_href   },
        { name => 'r7'      , info => $reg32_href   },
        { name => 'r8'      , info => $reg32_href   },
        { name => 'r9'      , info => $reg32_href   },
        { name => 'r10'     , info => $reg32_href   },
        { name => 'r11'     , info => $reg32_href   },
        { name => 'r12'     , info => $reg32_href   },
        { name => 'sp'      , info => $reg32_href   },
        { name => 'lr'      , info => $reg32_href   },
        { name => 'pc'      , info => $reg32_href   },
        { name => 'f0'      , info => $float96_href },
        { name => 'f1'      , info => $float96_href },
        { name => 'f2'      , info => $float96_href },
        { name => 'f3'      , info => $float96_href },
        { name => 'f4'      , info => $float96_href },
        { name => 'f5'      , info => $float96_href },
        { name => 'f6'      , info => $float96_href },
        { name => 'f7'      , info => $float96_href },
        { name => 'fps'     , info => $reg32_href   },
        { name => 'cpsr'    , info => $reg32_href   },
        { name => 's0'      , info => $float32_href },
        { name => 's1'      , info => $float32_href },
        { name => 's2'      , info => $float32_href },
        { name => 's3'      , info => $float32_href },
        { name => 's4'      , info => $float32_href },
        { name => 's5'      , info => $float32_href },
        { name => 's6'      , info => $float32_href },
        { name => 's7'      , info => $float32_href },
        { name => 's8'      , info => $float32_href },
        { name => 's9'      , info => $float32_href },
        { name => 's10'     , info => $float32_href },
        { name => 's11'     , info => $float32_href },
        { name => 's12'     , info => $float32_href },
        { name => 's13'     , info => $float32_href },
        { name => 's14'     , info => $float32_href },
        { name => 's15'     , info => $float32_href },
        { name => 's16'     , info => $float32_href },
        { name => 's17'     , info => $float32_href },
        { name => 's18'     , info => $float32_href },
        { name => 's19'     , info => $float32_href },
        { name => 's20'     , info => $float32_href },
        { name => 's21'     , info => $float32_href },
        { name => 's22'     , info => $float32_href },
        { name => 's23'     , info => $float32_href }, 
        { name => 's24'     , info => $float32_href },
        { name => 's25'     , info => $float32_href },
        { name => 's26'     , info => $float32_href },
        { name => 's27'     , info => $float32_href },
        { name => 's28'     , info => $float32_href },
        { name => 's29'     , info => $float32_href },
        { name => 's30'     , info => $float32_href },
        { name => 's31'     , info => $float32_href },
        { name => 'fpscr'   , info => $reg32_href   },
        { name => 'd16'     , info => $float64_href },
        { name => 'd17'     , info => $float64_href },
        { name => 'd18'     , info => $float64_href },
        { name => 'd19'     , info => $float64_href },
        { name => 'd20'     , info => $float64_href },
        { name => 'd21'     , info => $float64_href },
        { name => 'd22'     , info => $float64_href },
        { name => 'd23'     , info => $float64_href }, 
        { name => 'd24'     , info => $float64_href },
        { name => 'd25'     , info => $float64_href },
        { name => 'd26'     , info => $float64_href },
        { name => 'd27'     , info => $float64_href },
        { name => 'd28'     , info => $float64_href },
        { name => 'd29'     , info => $float64_href },
        { name => 'd30'     , info => $float64_href },
        { name => 'd31'     , info => $float64_href },
    ],
    
    
    'arm-lldb' => [
        { name => 'r0'      , info => $reg32_href   },
        { name => 'r1'      , info => $reg32_href   },
        { name => 'r2'      , info => $reg32_href   },
        { name => 'r3'      , info => $reg32_href   },
        { name => 'r4'      , info => $reg32_href   },
        { name => 'r5'      , info => $reg32_href   },
        { name => 'r6'      , info => $reg32_href   },
        { name => 'r7'      , info => $reg32_href   },
        { name => 'r8'      , info => $reg32_href   },
        { name => 'r9'      , info => $reg32_href   },
        { name => 'r10'     , info => $reg32_href   },
        { name => 'r11'     , info => $reg32_href   },
        { name => 'r12'     , info => $reg32_href   },
        { name => 'sp'      , info => $reg32_href   },
        { name => 'lr'      , info => $reg32_href   },
        { name => 'pc'      , info => $reg32_href   },
        { name => 'cpsr'    , info => $reg32_href   },
        { name => 's0'      , info => $float32_href },
        { name => 's1'      , info => $float32_href },
        { name => 's2'      , info => $float32_href },
        { name => 's3'      , info => $float32_href },
        { name => 's4'      , info => $float32_href },
        { name => 's5'      , info => $float32_href },
        { name => 's6'      , info => $float32_href },
        { name => 's7'      , info => $float32_href },
        { name => 's8'      , info => $float32_href },
        { name => 's9'      , info => $float32_href },
        { name => 's10'     , info => $float32_href },
        { name => 's11'     , info => $float32_href },
        { name => 's12'     , info => $float32_href },
        { name => 's13'     , info => $float32_href },
        { name => 's14'     , info => $float32_href },
        { name => 's15'     , info => $float32_href },
        { name => 's16'     , info => $float32_href },
        { name => 's17'     , info => $float32_href },
        { name => 's18'     , info => $float32_href },
        { name => 's19'     , info => $float32_href },
        { name => 's20'     , info => $float32_href },
        { name => 's21'     , info => $float32_href },
        { name => 's22'     , info => $float32_href },
        { name => 's23'     , info => $float32_href }, 
        { name => 's24'     , info => $float32_href },
        { name => 's25'     , info => $float32_href },
        { name => 's26'     , info => $float32_href },
        { name => 's27'     , info => $float32_href },
        { name => 's28'     , info => $float32_href },
        { name => 's29'     , info => $float32_href },
        { name => 's30'     , info => $float32_href },
        { name => 's31'     , info => $float32_href },
        { name => 'd0'      , info => $float64_href },
        { name => 'd1'      , info => $float64_href },
        { name => 'd2'      , info => $float64_href },
        { name => 'd3'      , info => $float64_href },
        { name => 'd4'      , info => $float64_href },
        { name => 'd5'      , info => $float64_href },
        { name => 'd6'      , info => $float64_href },
        { name => 'd7'      , info => $float64_href },
        { name => 'd8'      , info => $float64_href },
        { name => 'd9'      , info => $float64_href },
        { name => 'd10'     , info => $float64_href },
        { name => 'd11'     , info => $float64_href },
        { name => 'd12'     , info => $float64_href },
        { name => 'd13'     , info => $float64_href },
        { name => 'd14'     , info => $float64_href },
        { name => 'd15'     , info => $float64_href },
        { name => 'd16'     , info => $float64_href },
        { name => 'd17'     , info => $float64_href },
        { name => 'd18'     , info => $float64_href },
        { name => 'd19'     , info => $float64_href },
        { name => 'd20'     , info => $float64_href },
        { name => 'd21'     , info => $float64_href },
        { name => 'd22'     , info => $float64_href },
        { name => 'd23'     , info => $float64_href }, 
        { name => 'd24'     , info => $float64_href },
        { name => 'd25'     , info => $float64_href },
        { name => 'd26'     , info => $float64_href },
        { name => 'd27'     , info => $float64_href },
        { name => 'd28'     , info => $float64_href },
        { name => 'd29'     , info => $float64_href },
        { name => 'd30'     , info => $float64_href },
        { name => 'd31'     , info => $float64_href },
        { name => 'fpscr'   , info => $reg32_href   },
        { name => 'exc'     , info => $reg32_href   },
        { name => 'fsr'     , info => $reg32_href   },
        { name => 'far'     , info => $reg32_href   },
    ],    
    
    'x86_64-gdb' => [
    	{ name => 'rax'		, info => $reg64_href   },
    	{ name => 'rbx'     , info => $reg64_href   },
    	{ name => 'rcx'     , info => $reg64_href   },
    	{ name => 'rdx'     , info => $reg64_href   },
    	{ name => 'rsi'     , info => $reg64_href   },
    	{ name => 'rdi'     , info => $reg64_href   },
    	{ name => 'rbp'     , info => $reg64_href   },
    	{ name => 'rsp'     , info => $reg64_href   },
    	{ name => 'r8'      , info => $reg64_href   },
    	{ name => 'r9'      , info => $reg64_href   },
    	{ name => 'r10'     , info => $reg64_href   },
    	{ name => 'r11'     , info => $reg64_href   },
    	{ name => 'r12'     , info => $reg64_href   },
    	{ name => 'r13'     , info => $reg64_href   },
    	{ name => 'r14'     , info => $reg64_href   },
    	{ name => 'r15'     , info => $reg64_href   },
    	{ name => 'rip'     , info => $reg64_href   },
    	{ name => 'eflags'  , info => $reg32_href   },
    	{ name => 'cs'      , info => $reg32_href   },
    	{ name => 'ss'      , info => $reg32_href   },
    	{ name => 'ds'      , info => $reg32_href   },
    	{ name => 'es'      , info => $reg32_href   },
    	{ name => 'fs'      , info => $reg32_href   },
    	{ name => 'gs'      , info => $reg32_href   },
    	{ name => 'stmm0'   , info => $reg80_href   },
    	{ name => 'stmm1'   , info => $reg80_href   },
    	{ name => 'stmm2'   , info => $reg80_href   },
    	{ name => 'stmm3'   , info => $reg80_href   },
    	{ name => 'stmm4'   , info => $reg80_href   },
    	{ name => 'stmm5'   , info => $reg80_href   },
    	{ name => 'stmm6'   , info => $reg80_href   },
    	{ name => 'stmm7'   , info => $reg80_href   },
    	{ name => 'fctrl'   , info => $reg32_href   },
    	{ name => 'fstat'   , info => $reg32_href   },
    	{ name => 'ftag'    , info => $reg32_href   },
    	{ name => 'fiseg'   , info => $reg32_href   },
    	{ name => 'fioff'   , info => $reg32_href   },
    	{ name => 'foseg'   , info => $reg32_href   },
    	{ name => 'fooff'   , info => $reg32_href   },      
    	{ name => 'fop'     , info => $reg32_href   },
    	{ name => 'xmm0'	, info => $reg128_href  },
    	{ name => 'xmm1'    , info => $reg128_href  },
    	{ name => 'xmm2'    , info => $reg128_href  },
    	{ name => 'xmm3'    , info => $reg128_href  },
    	{ name => 'xmm4'    , info => $reg128_href  },
    	{ name => 'xmm5'    , info => $reg128_href  },
    	{ name => 'xmm6'    , info => $reg128_href  },
    	{ name => 'xmm7'    , info => $reg128_href  },
    	{ name => 'xmm8'    , info => $reg128_href  },
    	{ name => 'xmm9'    , info => $reg128_href  },
    	{ name => 'xmm10'   , info => $reg128_href  },
    	{ name => 'xmm11'   , info => $reg128_href  },
    	{ name => 'xmm12'   , info => $reg128_href  },
    	{ name => 'xmm13'   , info => $reg128_href  },
    	{ name => 'xmm14'   , info => $reg128_href  },
    	{ name => 'xmm15'   , info => $reg128_href	},
    	{ name => 'mxcsr'   , info => $reg32_href	},
    ],

    'x86_64-lldb' => [
        { name => 'rax'		    , info => $reg64_href   },
        { name => 'rbx'		    , info => $reg64_href   },
        { name => 'rcx'		    , info => $reg64_href   },
        { name => 'rdx'		    , info => $reg64_href   },
        { name => 'rdi'		    , info => $reg64_href   },
        { name => 'rsi'		    , info => $reg64_href   },
        { name => 'rbp'		    , info => $reg64_href   },
        { name => 'rsp'		    , info => $reg64_href   },
        { name => 'r8 '		    , info => $reg64_href   },
        { name => 'r9 '		    , info => $reg64_href   },
        { name => 'r10'		    , info => $reg64_href   },
        { name => 'r11'		    , info => $reg64_href   },
        { name => 'r12'		    , info => $reg64_href   },
        { name => 'r13'		    , info => $reg64_href   },
        { name => 'r14'		    , info => $reg64_href   },
        { name => 'r15'		    , info => $reg64_href   },
        { name => 'rip'		    , info => $reg64_href   },
        { name => 'rflags'	    , info => $reg64_href   },
        { name => 'cs'		    , info => $reg64_href   },
        { name => 'fs'		    , info => $reg64_href   },
        { name => 'gs'		    , info => $reg64_href   },
        { name => 'fctrl'       , info => $reg16_href   },
        { name => 'fstat'       , info => $reg16_href   },
        { name => 'ftag'        , info => $reg8_href    },
        { name => 'fop'         , info => $reg16_href   },
        { name => 'fioff'       , info => $reg32_href   },
        { name => 'fiseg'       , info => $reg16_href   },
        { name => 'fooff'       , info => $reg32_href   },
        { name => 'foseg'       , info => $reg16_href   },
        { name => 'mxcsr'       , info => $reg32_href   },
        { name => 'mxcsrmask'   , info => $reg32_href   },
        { name => 'stmm0'       , info => $reg80_href   },
        { name => 'stmm1'       , info => $reg80_href   },
        { name => 'stmm2'       , info => $reg80_href   },
        { name => 'stmm3'       , info => $reg80_href   },
        { name => 'stmm4'       , info => $reg80_href   },
        { name => 'stmm5'       , info => $reg80_href   },
        { name => 'stmm6'       , info => $reg80_href   },
        { name => 'stmm7'       , info => $reg80_href   },
        { name => 'xmm0'	    , info => $reg128_href  },
        { name => 'xmm1'	    , info => $reg128_href  },
        { name => 'xmm2'	    , info => $reg128_href  },
        { name => 'xmm3'	    , info => $reg128_href  },
        { name => 'xmm4'	    , info => $reg128_href  },
        { name => 'xmm5'	    , info => $reg128_href  },
        { name => 'xmm6'	    , info => $reg128_href  },
        { name => 'xmm7'	    , info => $reg128_href  },
        { name => 'xmm8'	    , info => $reg128_href  },
        { name => 'xmm9'	    , info => $reg128_href  },
        { name => 'xmm10'	    , info => $reg128_href  },
        { name => 'xmm11'	    , info => $reg128_href  },
        { name => 'xmm12'	    , info => $reg128_href  },
        { name => 'xmm13'	    , info => $reg128_href  },
        { name => 'xmm14'	    , info => $reg128_href  },
        { name => 'xmm15'	    , info => $reg128_href  },
        { name => 'trapno'      , info => $reg32_href   },
        { name => 'err'         , info => $reg32_href   },
        { name => 'faultvaddr'	, info => $reg64_href   },
    ]
);

our $max_register_name_len = 0;
calculate_max_register_name_length();
our @point_types = ( "software_bp", "hardware_bp", "write_wp", "read_wp", "access_wp" );
our $opt_v = 0;	# verbose
our $opt_g = 0;	# debug
our $opt_q = 0;	# quiet
our $opt_r = undef;
use Getopt::Std;
getopts('gvqr:'); 

our $registers_aref = undef;

if (length($opt_r))
{
	if (exists $reg_map{$opt_r})
	{
	    $registers_aref = $reg_map{$opt_r};		
	}
	else
	{
		die "Can't get registers group for '$opt_r'\n";
	}
}

sub extract_key_value_pairs 
{
    my $kv_href = {};
    my $arrayref = shift;
    my $str = join('',@$arrayref);
    my @kv_strs = split(/;/, $str);
    foreach my $kv_str (@kv_strs)
    {
        my ($key, $value) = split(/:/, $kv_str);
        $kv_href->{$key} = $value;
    }
    return $kv_href;
}

sub get_thread_from_thread_suffix
{
    if ($thread_suffix_supported)
    {
        my $arrayref = shift;
        # Skip leading semi-colon if needed
        $$arrayref[0] == ';' and shift @$arrayref;
        my $thread_href = extract_key_value_pairs ($arrayref);
        if (exists $thread_href->{thread})
        {
            return $thread_href->{thread};
        }
    }
    return undef;
}

sub calculate_max_register_name_length
{
	$max_register_name_len = 7;
	foreach my $reg_href (@$registers_aref)
	{
		my $name_len = length($reg_href->{name});
		if ($max_register_name_len < $name_len)
		{
			$max_register_name_len = $name_len;			
		}
	}
}
#----------------------------------------------------------------------
# Hash that maps command characters to the appropriate functions using
# the command character as the key and the value being a reference to
# the dump function for dumping the command itself.
#----------------------------------------------------------------------
our %cmd_callbacks = 
(
	'?' => \&dump_last_signal_cmd,
	'H' => \&dump_set_thread_cmd,
	'T' => \&dump_thread_is_alive_cmd,
	'q' => \&dump_general_query_cmd,
	'Q' => \&dump_general_set_cmd,
	'g' => \&dump_read_regs_cmd,
	'G' => \&dump_write_regs_cmd,
	'p' => \&dump_read_single_register_cmd,
	'P' => \&dump_write_single_register_cmd,	
	'm' => \&dump_read_mem_cmd,
	'M' => \&dump_write_mem_cmd,
	'X' => \&dump_write_mem_binary_cmd,
	'Z' => \&dump_bp_wp_command,
	'z' => \&dump_bp_wp_command,
	'k' => \&dump_kill_cmd,
	'A' => \&dump_A_command,
	'c' => \&dump_continue_cmd,
	's' => \&dump_continue_cmd,
	'C' => \&dump_continue_with_signal_cmd,
	'S' => \&dump_continue_with_signal_cmd,
	'_M' => \&dump_allocate_memory_cmd,
	'_m' => \&dump_deallocate_memory_cmd,
	# extended commands
	'v' => \&dump_extended_cmd
);

#----------------------------------------------------------------------
# Hash that maps command characters to the appropriate functions using
# the command character as the key and the value being a reference to
# the dump function for the response to the command.
#----------------------------------------------------------------------
our %rsp_callbacks = 
(
	'c' => \&dump_stop_reply_packet,
	's' => \&dump_stop_reply_packet,
	'C' => \&dump_stop_reply_packet,
	'?' => \&dump_stop_reply_packet,
	'T' => \&dump_thread_is_alive_rsp,
	'H' => \&dump_set_thread_rsp,
	'q' => \&dump_general_query_rsp,
	'g' => \&dump_read_regs_rsp,
	'p' => \&dump_read_single_register_rsp,
	'm' => \&dump_read_mem_rsp,
	'_M' => \&dump_allocate_memory_rsp,

	# extended commands
	'v' => \&dump_extended_rsp,
);


sub dump_register_value
{
    my $indent = shift;
	my $arrayref = shift;
	my $reg_num = shift;

    if ($reg_num >= @$registers_aref)
    {
        printf("\tinvalid register index %d\n", $reg_num);
        return;
    }
    
    my $reg_href = $$registers_aref[$reg_num];
    my $reg_name = $reg_href->{name};
	if ($$arrayref[0] eq '#')
	{
        printf("\t%*s: error: EOS reached when trying to read register %d\n", $max_register_name_len, $reg_name, $reg_num);
	}
	
    my $reg_info = $reg_href->{info};
    my $reg_extract = $reg_info->{extract};
    my $reg_format = $reg_info->{format};
    my $reg_val = &$reg_extract($arrayref);
    if ($indent) {
    	printf("\t%*s = $reg_format", $max_register_name_len, $reg_name, $reg_val);        
    } else {
    	printf("%s = $reg_format", $reg_name, $reg_val);        
    }
}

#----------------------------------------------------------------------
# Extract the command into an array of ASCII char strings for easy
# processing
#----------------------------------------------------------------------
sub extract_command
{
	my $cmd_str = shift;
	my @cmd_chars = split(/ */, $cmd_str);
	if ($cmd_chars[0] ne '$')
	{
		# only set the current command if it isn't a reply
		$curr_cmd = $cmd_chars[0]; 
	}
	return @cmd_chars;
}

#----------------------------------------------------------------------
# Strip the 3 checksum array entries after we don't need them anymore
#----------------------------------------------------------------------
sub strip_checksum
{
	my $arrayref = shift;
	splice(@$arrayref, -3);
}

#----------------------------------------------------------------------
# Dump all strings in array by joining them together with no space 
# between them
#----------------------------------------------------------------------
sub dump_chars
{
	print join('',@_);
}

#----------------------------------------------------------------------
# Check if the response is an error 'EXX'
#----------------------------------------------------------------------
sub is_error_response
{
	if ($_[0] eq 'E')
	{
		shift;
		print "ERROR = " . join('',@_) . "\n";
		return 1;
	}
	return 0;
}

#----------------------------------------------------------------------
# 'H' command
#----------------------------------------------------------------------
sub dump_set_thread_cmd
{
	my $cmd = shift;
	my $mod = shift;
	print "set_thread ( $mod, " . join('',@_) . " )\n";
}

#----------------------------------------------------------------------
# 'T' command
#----------------------------------------------------------------------
our $T_cmd_tid = -1;
sub dump_thread_is_alive_cmd
{
	my $cmd = shift;
	$T_cmd_tid = get_hex(\@_);
	printf("thread_is_alive ( $tid_format )\n", $T_cmd_tid);
}

sub dump_thread_is_alive_rsp
{
	my $rsp = join('',@_);
	
	printf("thread_is_alive ( $tid_format ) =>", $T_cmd_tid);
	if ($rsp eq 'OK')
	{
		print " alive.\n";
	}
	else
	{
		print " dead.\n";
	}
}

#----------------------------------------------------------------------
# 'H' response
#----------------------------------------------------------------------
sub dump_set_thread_rsp
{
	if (!is_error_response(@_))
	{
		print join('',@_) . "\n";
	}
}

#----------------------------------------------------------------------
# 'q' command
#----------------------------------------------------------------------
our $gen_query_cmd;
our $qRegisterInfo_reg_num = -1;
sub dump_general_query_cmd
{
	$gen_query_cmd = join('',@_);
	if ($gen_query_cmd eq 'qC')
	{
		print 'get_current_pid ()';
	}
	elsif ($gen_query_cmd eq 'qfThreadInfo')
	{
		print 'get_first_active_threads ()';
	}
	elsif ($gen_query_cmd eq 'qsThreadInfo')
	{
		print 'get_subsequent_active_threads ()';
	}
	elsif (index($gen_query_cmd, 'qThreadExtraInfo') == 0)
	{
		# qThreadExtraInfo,id
		print 'get_thread_extra_info ()';
	}
	elsif (index($gen_query_cmd, 'qThreadStopInfo') == 0)
	{
		# qThreadStopInfoXXXX
		@_ = splice(@_, length('qThreadStopInfo'));
		my $tid = get_addr(\@_);
		printf('get_thread_stop_info ( thread = 0x%4.4x )', $tid);
	}
	elsif (index($gen_query_cmd, 'qSymbol:') == 0)
	{
		# qCRC:addr,length
		print 'gdb_ready_to_serve_symbol_lookups ()';
	}
	elsif (index($gen_query_cmd, 'qCRC:') == 0)
	{
		# qCRC:addr,length
		@_ = splice(@_, length('qCRC:'));
		my $address = get_addr(\@_);
		shift @_;
		my $length = join('', @_);
		printf("compute_crc (addr = $addr_format, length = $length)", $address);
	}
	elsif (index($gen_query_cmd, 'qGetTLSAddr:') == 0)
	{
		# qGetTLSAddr:thread-id,offset,lm
		@_ = splice(@_, length('qGetTLSAddr:'));
		my ($tid, $offset, $lm) = split (/,/, join('', @_));
		print "get_thread_local_storage_addr (thread-id = $tid, offset = $offset, lm = $lm)";
	}
	elsif ($gen_query_cmd eq 'qOffsets')
	{
		print 'get_section_offsets ()';
	}
	elsif (index($gen_query_cmd, 'qRegisterInfo') == 0)
	{
		@_ = splice(@_, length('qRegisterInfo'));
		$qRegisterInfo_reg_num = get_hex(\@_);
		
		printf "get_dynamic_register_info ($qRegisterInfo_reg_num)";
	}
	else
	{
		print $gen_query_cmd;
	}
	print "\n";
}

#----------------------------------------------------------------------
# 'q' response
#----------------------------------------------------------------------
sub dump_general_query_rsp
{
	my $gen_query_rsp = join('',@_);
	my $gen_query_rsp_len = length ($gen_query_rsp);
	if ($gen_query_cmd eq 'qC' and index($gen_query_rsp, 'QC') == 0)
	{
		shift @_; shift @_;
		my $pid = get_hex(\@_);
		printf("pid = $pid_format\n", $pid);
		return;
	}
	elsif (index($gen_query_cmd, 'qRegisterInfo') == 0)
	{
		if ($gen_query_rsp_len == 0)
		{
			print "$unimplemented_str\n";			
		}
		else
		{
			if (index($gen_query_rsp, 'name') == 0)
			{
				$qRegisterInfo_reg_num == 0 and $registers_aref = [];

				my @name_and_values = split (/;/, $gen_query_rsp);
			
				my $reg_name = undef;
				my $byte_size = 0;
				my $pseudo = 0;
				foreach (@name_and_values)
				{
					my ($name, $value) = split /:/;				
					if    ($name eq "name") { $reg_name = $value; }
					elsif ($name eq "bitsize") { $byte_size = $value / 8; }
					elsif ($name eq "container-regs") { $pseudo = 1; }
				}
				if (defined $reg_name and $byte_size > 0)
				{
					if    ($byte_size == 4)  {push @$registers_aref, { name => $reg_name, info => $reg32_href   , pseudo => $pseudo };}
					elsif ($byte_size == 8)  {push @$registers_aref, { name => $reg_name, info => $reg64_href   , pseudo => $pseudo };}
					elsif ($byte_size == 1)  {push @$registers_aref, { name => $reg_name, info => $reg8_href    , pseudo => $pseudo };}
					elsif ($byte_size == 2)  {push @$registers_aref, { name => $reg_name, info => $reg16_href   , pseudo => $pseudo };}
					elsif ($byte_size == 10) {push @$registers_aref, { name => $reg_name, info => $reg80_href   , pseudo => $pseudo };}
					elsif ($byte_size == 12) {push @$registers_aref, { name => $reg_name, info => $float96_href , pseudo => $pseudo };}
					elsif ($byte_size == 16) {push @$registers_aref, { name => $reg_name, info => $reg128_href  , pseudo => $pseudo };}
					elsif ($byte_size == 32) {push @$registers_aref, { name => $reg_name, info => $reg256_href  , pseudo => $pseudo };}
				}
			}
			elsif ($gen_query_rsp_len == 3 and index($gen_query_rsp, 'E') == 0)
			{
				calculate_max_register_name_length();
			}
		}
	}
	elsif ($gen_query_cmd =~ 'qThreadStopInfo')
	{
		dump_stop_reply_packet (@_);
	}
	if (dump_standard_response(\@_))
	{
		# Do nothing...
	}
	else
	{
		print join('',@_) . "\n";
	}
}

#----------------------------------------------------------------------
# 'Q' command
#----------------------------------------------------------------------
our $gen_set_cmd;
sub dump_general_set_cmd
{
	$gen_query_cmd = join('',@_);
	if ($gen_query_cmd eq 'QStartNoAckMode')
	{
		print "StartNoAckMode ()"
	}
	elsif ($gen_query_cmd eq 'QThreadSuffixSupported')
	{
	    $thread_suffix_supported = 1;
		print "ThreadSuffixSupported ()"
	}
	elsif (index($gen_query_cmd, 'QSetMaxPayloadSize:') == 0)
	{
		@_ = splice(@_, length('QSetMaxPayloadSize:'));
		my $max_payload_size = get_hex(\@_);
		# QSetMaxPayloadSize:XXXX  where XXXX is a hex length of the max
		# packet payload size supported by gdb
		printf("SetMaxPayloadSize ( 0x%x (%u))", $max_payload_size, $max_payload_size);
	}
	elsif (index ($gen_query_cmd, 'QSetSTDIN:') == 0)
	{
		@_ = splice(@_, length('QSetSTDIN:'));
		printf ("SetSTDIN (path ='%s')\n", get_hex_string (\@_));
	}
	elsif (index ($gen_query_cmd, 'QSetSTDOUT:') == 0)
	{
		@_ = splice(@_, length('QSetSTDOUT:'));
		printf ("SetSTDOUT (path ='%s')\n", get_hex_string (\@_));
	}
	elsif (index ($gen_query_cmd, 'QSetSTDERR:') == 0)
	{
		@_ = splice(@_, length('QSetSTDERR:'));
		printf ("SetSTDERR (path ='%s')\n", get_hex_string (\@_));
	}
	else
	{
		print $gen_query_cmd;
	}
	print "\n";
}

#----------------------------------------------------------------------
# 'k' command
#----------------------------------------------------------------------
sub dump_kill_cmd
{
	my $cmd = shift;
	print "kill (" . join('',@_) . ")\n";
}

#----------------------------------------------------------------------
# 'g' command
#----------------------------------------------------------------------
sub dump_read_regs_cmd
{
	my $cmd = shift;
	print "read_registers ()\n";
}

#----------------------------------------------------------------------
# 'G' command
#----------------------------------------------------------------------
sub dump_write_regs_cmd
{
	print "write_registers:\n";
	my $cmd = shift;
    foreach my $reg_href (@$registers_aref)
    {
		last if ($_[0] eq '#');
		if ($reg_href->{pseudo} == 0)
		{
            my $reg_info_href = $reg_href->{info};
            my $reg_name = $reg_href->{name};
            my $reg_extract = $reg_info_href->{extract};
            my $reg_format = $reg_info_href->{format};
            my $reg_val = &$reg_extract(\@_);
    		printf("\t%*s = $reg_format\n", $max_register_name_len, $reg_name, $reg_val);		    
		}
	}			
}

sub dump_read_regs_rsp
{
	print "read_registers () =>\n";
	if (!is_error_response(@_))
	{
	#	print join('',@_) . "\n";
	    foreach my $reg_href (@$registers_aref)
	    {
			last if ($_[0] eq '#');
    		if ($reg_href->{pseudo} == 0)
    		{
    	        my $reg_info_href = $reg_href->{info};
    	        my $reg_name = $reg_href->{name};
    	        my $reg_extract = $reg_info_href->{extract};
                my $reg_format = $reg_info_href->{format};
                my $reg_val = &$reg_extract(\@_);
    			printf("\t%*s = $reg_format\n", $max_register_name_len, $reg_name, $reg_val);
			}
		}			
	}
}

sub dump_read_single_register_rsp
{
    dump_register_value(0, \@_, $reg_cmd_reg);
    print "\n";
}

#----------------------------------------------------------------------
# '_M' - allocate memory command (LLDB extension)
#
#   Command: '_M'
#      Arg1: Hex byte size as big endian hex string
# Separator: ','
#      Arg2: permissions as string that must be a string that contains any
#            combination of 'r' (readable) 'w' (writable) or 'x' (executable)
#
#   Returns: The address that was allocated as a big endian hex string
#            on success, else an error "EXX" where XX are hex bytes
#            that indicate an error code.
#
# Examples:
#   _M10,rw     # allocate 16 bytes with read + write permissions
#   _M100,rx    # allocate 256 bytes with read + execute permissions
#----------------------------------------------------------------------
sub dump_allocate_memory_cmd
{
	shift; shift; # shift off the '_' and the 'M'
	my $byte_size = get_addr(\@_);
	shift;	# Skip ','
	printf("allocate_memory ( byte_size = %u (0x%x), permissions = %s)\n", $byte_size, $byte_size, join('',@_));
}

sub dump_allocate_memory_rsp
{
    if (@_ == 3 and $_[0] == 'E')
    {
	    printf("allocated memory addr = ERROR (%s))\n", join('',@_));        
    }
    else
    {
	    printf("allocated memory addr = 0x%s\n", join('',@_));        
    }
}

#----------------------------------------------------------------------
# '_m' - deallocate memory command (LLDB extension)
#
#   Command: '_m'
#      Arg1: Hex address as big endian hex string
#
#   Returns: "OK" on success "EXX" on error
#
# Examples:
#   _m201000    # Free previously allocated memory at address 0x201000
#----------------------------------------------------------------------
sub dump_deallocate_memory_cmd
{
	shift; shift; # shift off the '_' and the 'm'
	printf("deallocate_memory ( addr =  0x%s)\n", join('',@_));
}


#----------------------------------------------------------------------
# 'p' command
#----------------------------------------------------------------------
sub dump_read_single_register_cmd
{
	my $cmd = shift;
	$reg_cmd_reg = get_hex(\@_);
	my $thread = get_thread_from_thread_suffix (\@_);
	my $reg_href = $$registers_aref[$reg_cmd_reg];
  
	if (defined $thread)
	{
    	print "read_register ( reg = \"$reg_href->{name}\", thread = $thread )\n";
	}
	else
	{
    	print "read_register ( reg = \"$reg_href->{name}\" )\n";
	}
}


#----------------------------------------------------------------------
# 'P' command
#----------------------------------------------------------------------
sub dump_write_single_register_cmd
{
	my $cmd = shift;
	my $reg_num = get_hex(\@_);
	shift (@_);	# Discard the '='
	
	print "write_register ( ";
	dump_register_value(0, \@_, $reg_num);
	my $thread = get_thread_from_thread_suffix (\@_);
	if (defined $thread)
	{
	    print ", thread = $thread";
	}
	print " )\n";
}

#----------------------------------------------------------------------
# 'm' command
#----------------------------------------------------------------------
our $read_mem_address = 0;
sub dump_read_mem_cmd
{
	my $cmd = shift;
	$read_mem_address = get_addr(\@_);
	shift;	# Skip ','
	printf("read_mem ( $addr_format, %s )\n", $read_mem_address, join('',@_));
}

#----------------------------------------------------------------------
# 'm' response
#----------------------------------------------------------------------
sub dump_read_mem_rsp
{
	# If the memory read was 2 or 4 bytes, print it out in native format
	# instead of just as bytes.
	my $num_nibbles = @_;
	if ($num_nibbles == 2)
	{
		printf(" 0x%2.2x", get8(\@_));
	}
	elsif ($num_nibbles == 4)
	{
		printf(" 0x%4.4x", get16(\@_));
	}
	elsif ($num_nibbles == 8)
	{
		printf(" 0x%8.8x", get32(\@_));
	}
	elsif ($num_nibbles == 16)
	{
		printf(" 0x%s", get64(\@_));
	}
	else
	{
		my $curr_address = $read_mem_address;
		my $nibble;
		my $nibble_offset = 0;
		my $max_nibbles_per_line = 2 * $max_bytes_per_line;
		foreach $nibble (@_)
		{
			if (($nibble_offset % $max_nibbles_per_line) == 0)
			{
				($nibble_offset > 0) and print "\n    ";
				printf("$addr_format: ", $curr_address + $nibble_offset/2);
			}
			(($nibble_offset % 2) == 0) and print ' ';			
			print $nibble;
			$nibble_offset++;
		}
	}
	print "\n";
}

#----------------------------------------------------------------------
# 'c' or 's' command
#----------------------------------------------------------------------
sub dump_continue_cmd
{	
	my $cmd = shift;
	my $cmd_str;
	$cmd eq 'c' and $cmd_str = 'continue';
	$cmd eq 's' and $cmd_str = 'step';
	my $address = -1;
	if (@_)
	{
		my $address = get_addr(\@_);
		printf("%s ($addr_format)\n", $cmd_str, $address);
	}
	else
	{
		printf("%s ()\n", $cmd_str);
	}
}

#----------------------------------------------------------------------
# 'Css' continue (C) with signal (ss where 'ss' is two hex digits)
# 'Sss' step (S) with signal (ss where 'ss' is two hex digits)
#----------------------------------------------------------------------
sub dump_continue_with_signal_cmd
{	
	my $cmd = shift;
	my $address = -1;
	my $cmd_str;
	$cmd eq 'c' and $cmd_str = 'continue';
	$cmd eq 's' and $cmd_str = 'step';
	my $signal = get_hex(\@_);
	if (@_)
	{
		my $address = 0;
		if (@_ && $_[0] == ';')
		{
			shift;
		 	$address = get_addr(\@_);
		}
	}

	if ($address != -1)
	{
		printf("%s_with_signal (signal = 0x%2.2x, address = $addr_format)\n", $cmd_str, $signal, $address);
	}
	else
	{
		printf("%s_with_signal (signal = 0x%2.2x)\n", $cmd_str, $signal);
	}
}

#----------------------------------------------------------------------
# 'A' command
#----------------------------------------------------------------------
sub dump_A_command
{	
	my $cmd = get_expected_char(\@_, 'A') or print "error: incorrect command letter for argument packet, expected 'A'\n";
	printf("set_program_arguments (\n");
	do
	{
		my $arg_len = get_uint(\@_);
		get_expected_char(\@_, ',') or die "error: missing comma after argument length...?\n";
		my $arg_idx = get_uint(\@_);
		get_expected_char(\@_, ',') or die "error: missing comma after argument number...?\n";
	
		my $arg = '';
		my $num_hex8_bytes = $arg_len/2;
		for (1 .. $num_hex8_bytes)
		{
			$arg .= sprintf("%c", get8(\@_))
		}
		printf("        <%3u> argv[%u] = '%s'\n", $arg_len, $arg_idx, $arg);
		if (@_ > 0)
		{
			get_expected_char(\@_, ',') or die "error: missing comma after argument argument ASCII hex bytes...?\n";
		}		
	} while (@_ > 0);	
	printf("    )\n");
}


#----------------------------------------------------------------------
# 'z' and 'Z' command
#----------------------------------------------------------------------
sub dump_bp_wp_command
{	
	my $cmd = shift;
	my $type = shift;
	shift;	# Skip ','
	my $address = get_addr(\@_);
	shift;	# Skip ','
	my $length = join('',@_);
	if ($cmd eq 'z')
	{
		printf("remove $point_types[$type]($addr_format, %d)\n", $address, $length);
	}
	else
	{
		printf("insert $point_types[$type]($addr_format, %d)\n", $address, $length);		
	}
}


#----------------------------------------------------------------------
# 'X' command
#----------------------------------------------------------------------
sub dump_write_mem_binary_cmd
{	
	my $cmd = shift;
	my $address = get_addr(\@_);
	shift;	# Skip ','
	
	my ($length, $binary) = split(/:/, join('',@_));
	printf("write_mem_binary ( $addr_format, %d, %s)\n", $address, $length, $binary);

}

#----------------------------------------------------------------------
# 'M' command
#----------------------------------------------------------------------
sub dump_write_mem_cmd
{	
	my $cmd = shift;
	my $address = get_addr(\@_);
	shift;	# Skip ','
	my ($length, $hex_bytes) = split(/:/, join('',@_));
#	printf("write_mem ( $addr_format, %d, %s)\n", $address, $length, $hex_bytes);
	printf("write_mem ( addr = $addr_format, len = %d (0x%x), bytes = ", $address, $length, $length);
	splice(@_, 0, length($length)+1);

	my $curr_address = $address;
	my $nibble;
	my $nibble_count = 0;
	my $max_nibbles_per_line = 2 * $max_bytes_per_line;
	foreach $nibble (@_)
	{
		(($nibble_count % 2) == 0) and print ' ';
		print $nibble;
		$nibble_count++;
	}

	# If the memory to write is 2 or 4 bytes, print it out in native format
	# instead of just as bytes.
	if (@_ == 4)
	{
		printf(" ( 0x%4.4x )", get16(\@_));
	}
	elsif (@_ == 8)
	{
		printf(" ( 0x%8.8x )", get32(\@_));
	}
	print " )\n";

}

#----------------------------------------------------------------------
# 'v' command
#----------------------------------------------------------------------
our $extended_rsp_callback = 0;
sub dump_extended_cmd
{
	$extended_rsp_callback = 0;
	if (join('', @_[0..4]) eq "vCont")
	{
		dump_extended_continue_cmd(splice(@_,5));
	}
	elsif (join('', @_[0..7]) eq 'vAttach;')
	{
		dump_attach_command (splice(@_,8));
	}
	elsif (join('', @_[0..11]) eq 'vAttachWait;')
	{
		dump_attach_wait_command (splice(@_,12));
	}
}

#----------------------------------------------------------------------
# 'v' response
#----------------------------------------------------------------------
sub dump_extended_rsp
{
	if ($extended_rsp_callback)
	{
		&$extended_rsp_callback(@_);
	}
	$extended_rsp_callback = 0;
}

#----------------------------------------------------------------------
# 'vAttachWait' command
#----------------------------------------------------------------------
sub dump_attach_wait_command
{
	print "attach_wait ( ";
	while (@_)
	{
		printf("%c", get8(\@_))
	}
	printf " )\n";
	
}

#----------------------------------------------------------------------
# 'vAttach' command
#----------------------------------------------------------------------
sub dump_attach_command
{
	printf("attach ( pid = %i )", get_hex(\@_));
	$extended_rsp_callback = \&dump_stop_reply_packet;
}

#----------------------------------------------------------------------
# 'vCont' command
#----------------------------------------------------------------------
sub dump_extended_continue_cmd
{
	print "extended_continue ( ";
	my $cmd = shift;
	if ($cmd eq '?')
	{
		print "list supported modes )\n";
		$extended_rsp_callback = \&dump_extended_continue_rsp;
	}
	elsif  ($cmd eq ';')
	{
		$extended_rsp_callback = \&dump_stop_reply_packet;
		my $i = 0;
		while ($#_ >= 0)
		{
			if ($i > 0)
			{
				print ", ";
			}
			my $continue_cmd = shift;
			my $tmp;
			if ($continue_cmd eq 'c')
			{ 
				print "continue";
			}
			elsif ($continue_cmd eq 'C')			
			{
				print "continue with signal ";
				print shift;
				print shift;
			}
			elsif ($continue_cmd eq 's')			
			{ 
				print "step";
			}
			elsif ($continue_cmd eq 'S')			
			{
				print "step with signal ";
				print shift;
				print shift;
			}

			if ($_[0] eq ':')
			{
				shift; # Skip ':'
				print " for thread ";
				while ($#_ >= 0)
				{
					$tmp = shift;
					if (length($tmp) > 0 && $tmp ne ';') {
						print $tmp; 
					} else { 
						last;
					}
				}
			}
			$i++;
		}
		
		printf " )\n";
	}
}

#----------------------------------------------------------------------
# 'vCont' response
#----------------------------------------------------------------------
sub dump_extended_continue_rsp
{
	if (scalar(@_) == 0)
	{
		print "$unimplemented_str\n";
	}
	else
	{
		print "extended_continue supports " . join('',@_) . "\n";
	}
}

#----------------------------------------------------------------------
# Dump the command ascii for any unknown commands
#----------------------------------------------------------------------
sub dump_other_cmd
{
	print "other = " . join('',@_) . "\n";
}

#----------------------------------------------------------------------
# Check to see if the response was unsupported with appropriate checksum
#----------------------------------------------------------------------
sub rsp_is_unsupported
{
	return join('',@_) eq "#00";
}

#----------------------------------------------------------------------
# Check to see if the response was "OK" with appropriate checksum
#----------------------------------------------------------------------
sub rsp_is_OK
{
	return join('',@_) eq "OK#9a";
}

#----------------------------------------------------------------------
# Dump a response for an unknown command
#----------------------------------------------------------------------
sub dump_other_rsp
{
	print "other = " . join('',@_) . "\n";
}

#----------------------------------------------------------------------
# Get a byte from the ascii string assuming that the 2 nibble ascii
# characters are in hex. 
#
# The argument for this function needs to be a reference to an array 
# that contains single character strings and the array will get 
# updated by shifting characters off the front of it (no leading # "0x")
#----------------------------------------------------------------------
sub get8
{
	my $arrayref = shift;
	my $val = hex(shift(@$arrayref) . shift(@$arrayref));
	return $val;
}

#----------------------------------------------------------------------
# Get a 16 bit integer and swap if $swap global is set to a non-zero 
# value.
#
# The argument for this function needs to be a reference to an array 
# that contains single character strings and the array will get 
# updated by shifting characters off the front of it (no leading # "0x")
#----------------------------------------------------------------------
sub get16
{
	my $arrayref = shift;
	my $val = 0;
	if ($swap)
	{
		$val =	get8($arrayref) 	|
				get8($arrayref) << 8;
	}
	else
	{
		$val =	get8($arrayref) << 8 |
				get8($arrayref)		 ;
	}
	return $val;
}

#----------------------------------------------------------------------
# Get a 32 bit integer and swap if $swap global is set to a non-zero 
# value.
#
# The argument for this function needs to be a reference to an array 
# that contains single character strings and the array will get 
# updated by shifting characters off the front of it (no leading # "0x")
#----------------------------------------------------------------------
sub get32
{
	my $arrayref = shift;
	my $val = 0;
	if ($swap)
	{
		$val =	get8($arrayref)       |
				get8($arrayref) << 8  |
				get8($arrayref) << 16 |
				get8($arrayref) << 24 ;
	}
	else
	{
		$val =	get8($arrayref) << 24 |
				get8($arrayref) << 16 |
				get8($arrayref) <<  8 |
				get8($arrayref)       ;
	}
	return $val;
}

#----------------------------------------------------------------------
# Get a 64 bit hex value as a string
#
# The argument for this function needs to be a reference to an array 
# that contains single character strings and the array will get 
# updated by shifting characters off the front of it (no leading # "0x")
#----------------------------------------------------------------------
sub get64
{
	my $arrayref = shift;
	my $val = '';
	my @nibbles;
	if ($swap)
	{
        push @nibbles, splice(@$arrayref, 14, 2);
        push @nibbles, splice(@$arrayref, 12, 2);
        push @nibbles, splice(@$arrayref, 10, 2);
        push @nibbles, splice(@$arrayref, 8, 2);
        push @nibbles, splice(@$arrayref, 6, 2);
        push @nibbles, splice(@$arrayref, 4, 2);
        push @nibbles, splice(@$arrayref, 2, 2);
        push @nibbles, splice(@$arrayref, 0, 2);
	}
	else
	{
	    (@nibbles) = splice(@$arrayref, 0, ((64/8) * 2));
	}
    $val = join('', @nibbles);        
	return $val;
}

#----------------------------------------------------------------------
# Get a 80 bit hex value as a string
#
# The argument for this function needs to be a reference to an array 
# that contains single character strings and the array will get 
# updated by shifting characters off the front of it (no leading # "0x")
#----------------------------------------------------------------------
sub get80
{
	my $arrayref = shift;
	my $val = '';
	my @nibbles;
	if ($swap)
	{
        push @nibbles, splice(@$arrayref, 18, 2);
        push @nibbles, splice(@$arrayref, 16, 2);
        push @nibbles, splice(@$arrayref, 14, 2);
        push @nibbles, splice(@$arrayref, 12, 2);
        push @nibbles, splice(@$arrayref, 10, 2);
        push @nibbles, splice(@$arrayref, 8, 2);
        push @nibbles, splice(@$arrayref, 6, 2);
        push @nibbles, splice(@$arrayref, 4, 2);
        push @nibbles, splice(@$arrayref, 2, 2);
        push @nibbles, splice(@$arrayref, 0, 2);
	}
	else
	{
	    (@nibbles) = splice(@$arrayref, 0, ((80/8) * 2));
	}
    $val = join('', @nibbles);        
	return $val;
}

#----------------------------------------------------------------------
# Get a 96 bit hex value as a string
#
# The argument for this function needs to be a reference to an array 
# that contains single character strings and the array will get 
# updated by shifting characters off the front of it (no leading # "0x")
#----------------------------------------------------------------------
sub get96
{
	my $arrayref = shift;
	my $val = '';
	my @nibbles;
	if ($swap)
	{
        push @nibbles, splice(@$arrayref, 22, 2);
        push @nibbles, splice(@$arrayref, 20, 2);
        push @nibbles, splice(@$arrayref, 18, 2);
        push @nibbles, splice(@$arrayref, 16, 2);
        push @nibbles, splice(@$arrayref, 14, 2);
        push @nibbles, splice(@$arrayref, 12, 2);
        push @nibbles, splice(@$arrayref, 10, 2);
        push @nibbles, splice(@$arrayref, 8, 2);
        push @nibbles, splice(@$arrayref, 6, 2);
        push @nibbles, splice(@$arrayref, 4, 2);
        push @nibbles, splice(@$arrayref, 2, 2);
        push @nibbles, splice(@$arrayref, 0, 2);
	}
	else
	{
	    (@nibbles) = splice(@$arrayref, 0, ((96/8) * 2));
	}
    $val = join('', @nibbles);        
	return $val;
}

#----------------------------------------------------------------------
# Get a 128 bit hex value as a string
#
# The argument for this function needs to be a reference to an array 
# that contains single character strings and the array will get 
# updated by shifting characters off the front of it (no leading # "0x")
#----------------------------------------------------------------------
sub get128
{
	my $arrayref = shift;
	my $val = '';
	my @nibbles;
	if ($swap)
	{
        push @nibbles, splice(@$arrayref, 30, 2);
        push @nibbles, splice(@$arrayref, 28, 2);
        push @nibbles, splice(@$arrayref, 26, 2);
        push @nibbles, splice(@$arrayref, 24, 2);
        push @nibbles, splice(@$arrayref, 22, 2);
        push @nibbles, splice(@$arrayref, 20, 2);
        push @nibbles, splice(@$arrayref, 18, 2);
        push @nibbles, splice(@$arrayref, 16, 2);
        push @nibbles, splice(@$arrayref, 14, 2);
        push @nibbles, splice(@$arrayref, 12, 2);
        push @nibbles, splice(@$arrayref, 10, 2);
        push @nibbles, splice(@$arrayref, 8, 2);
        push @nibbles, splice(@$arrayref, 6, 2);
        push @nibbles, splice(@$arrayref, 4, 2);
        push @nibbles, splice(@$arrayref, 2, 2);
        push @nibbles, splice(@$arrayref, 0, 2);
	}
	else
	{
	    (@nibbles) = splice(@$arrayref, 0, ((128/8) * 2));
	}
    $val = join('', @nibbles);        
	return $val;
}

#----------------------------------------------------------------------
# Get a 256 bit hex value as a string
#
# The argument for this function needs to be a reference to an array 
# that contains single character strings and the array will get 
# updated by shifting characters off the front of it (no leading # "0x")
#----------------------------------------------------------------------
sub get256
{
	my $arrayref = shift;
	my $val = '';
	my @nibbles;
	if ($swap)
	{
        push @nibbles, splice(@$arrayref, 62, 2);
        push @nibbles, splice(@$arrayref, 60, 2);
        push @nibbles, splice(@$arrayref, 58, 2);
        push @nibbles, splice(@$arrayref, 56, 2);
        push @nibbles, splice(@$arrayref, 54, 2);
        push @nibbles, splice(@$arrayref, 52, 2);
        push @nibbles, splice(@$arrayref, 50, 2);
        push @nibbles, splice(@$arrayref, 48, 2);
        push @nibbles, splice(@$arrayref, 46, 2);
        push @nibbles, splice(@$arrayref, 44, 2);
        push @nibbles, splice(@$arrayref, 42, 2);
        push @nibbles, splice(@$arrayref, 40, 2);
        push @nibbles, splice(@$arrayref, 38, 2);
        push @nibbles, splice(@$arrayref, 36, 2);
        push @nibbles, splice(@$arrayref, 34, 2);
        push @nibbles, splice(@$arrayref, 32, 2);
        push @nibbles, splice(@$arrayref, 30, 2);
        push @nibbles, splice(@$arrayref, 28, 2);
        push @nibbles, splice(@$arrayref, 26, 2);
        push @nibbles, splice(@$arrayref, 24, 2);
        push @nibbles, splice(@$arrayref, 22, 2);
        push @nibbles, splice(@$arrayref, 20, 2);
        push @nibbles, splice(@$arrayref, 18, 2);
        push @nibbles, splice(@$arrayref, 16, 2);
        push @nibbles, splice(@$arrayref, 14, 2);
        push @nibbles, splice(@$arrayref, 12, 2);
        push @nibbles, splice(@$arrayref, 10, 2);
        push @nibbles, splice(@$arrayref, 8, 2);
        push @nibbles, splice(@$arrayref, 6, 2);
        push @nibbles, splice(@$arrayref, 4, 2);
        push @nibbles, splice(@$arrayref, 2, 2);
        push @nibbles, splice(@$arrayref, 0, 2);
	}
	else
	{
	    (@nibbles) = splice(@$arrayref, 0, ((256/8) * 2));
	}
    $val = join('', @nibbles);        
	return $val;
}

#----------------------------------------------------------------------
# Get an unsigned integer value by grabbing items off the front of 
# the array stopping when a non-digit char string is encountered.
#
# The argument for this function needs to be a reference to an array 
# that contains single character strings and the array will get 
# updated by shifting characters off the front of it
#----------------------------------------------------------------------
sub get_uint
{
	my $arrayref = shift;
	@$arrayref == 0 and return 0;
	my $val = 0;
	while ($$arrayref[0] =~ /[0-9]/)
	{
		$val = $val * 10 + int(shift(@$arrayref));
	}
	return $val;
}

#----------------------------------------------------------------------
# Check the first character in the array and if it matches the expected
# character, return that character, else return undef;
#
# The argument for this function needs to be a reference to an array 
# that contains single character strings and the array will get 
# updated by shifting characters off the front of it. If the expected
# character doesn't match, it won't touch the array. If the first
# character does match, it will shift it off and return it.
#----------------------------------------------------------------------
sub get_expected_char
{
	my $arrayref = shift;
	my $expected_char = shift;
	if ($expected_char eq $$arrayref[0])
	{
		return shift(@$arrayref);
	}
	return undef;
}
#----------------------------------------------------------------------
# Get a hex value by grabbing items off the front of the array and 
# stopping when a non-hex char string is encountered.
#
# The argument for this function needs to be a reference to an array 
# that contains single character strings and the array will get 
# updated by shifting characters off the front of it (no leading # "0x")
#----------------------------------------------------------------------
sub get_hex
{
	my $arrayref = shift;
	my $my_swap = @_ ? shift : 0;
	my $shift = 0;
	my $val = 0;
	while ($$arrayref[0] =~ /[0-9a-fA-F]/)
	{
		if ($my_swap)
		{
			my $byte = hex(shift(@$arrayref)) << 4 | hex(shift(@$arrayref));
			$val |= $byte << $shift;
			$shift += 8;
		}
		else
		{
			$val <<= 4;
			$val |= hex(shift(@$arrayref));
		}
	}
	return $val;
}

#----------------------------------------------------------------------
# Get an address value by grabbing items off the front of the array.
#
# The argument for this function needs to be a reference to an array 
# that contains single character strings and the array will get 
# updated by shifting characters off the front of it (no leading # "0x")
#----------------------------------------------------------------------
sub get_addr
{
	get_hex(shift);
}

sub get_hex_string
{
	my $arrayref = shift;
	my $str = '';
	while ($$arrayref[0] =~ /[0-9a-fA-F]/ and $$arrayref[1] =~ /[0-9a-fA-F]/)
	{
		my $hi_nibble = hex(shift(@$arrayref));
		my $lo_nibble = hex(shift(@$arrayref));
		my $byte = ($hi_nibble << 4) | $lo_nibble;
		$str .= chr($byte);
	}
	return $str;
}

sub dump_stop_reply_data
{
    while ($#_ >= 0)
	{
		last unless ($_[0] ne '#');
		
	
		my $key = '';
		my $value = '';
		my $comment = '';
        if ($_[0] =~ /[0-9a-fA-F]/ && $_[1] =~ /[0-9a-fA-F]/)
    	{
    		my $reg_num = get8(\@_);
    		shift(@_);	# Skip ':'
    		if (defined ($registers_aref) && $reg_num < @$registers_aref)
    		{
                dump_register_value(1, \@_, $reg_num);
                print "\n";
        		shift(@_);	# Skip ';'
        		next;
    		}
    		$key = sprintf("reg %u", $reg_num);
    	}
    	my $char;
    	
    	if (length($key) == 0)
    	{
    		while (1)
    		{
    			$char = shift(@_);
    			if (length($char) == 0 or $char eq ':' or $char eq '#') { last; }
    			$key .= $char;
    		}
    	}
    	
		while (1)
		{
			$char = shift(@_);
			if (length($char) == 0 or $char eq ';' or $char eq '#') { last; }
			$value .= $char;
		}
		if ($key eq 'metype')
		{
		    our %metype_to_name = (
		        '1' => ' (EXC_BAD_ACCESS)',
                '2' => ' (EXC_BAD_INSTRUCTION)',
                '3' => ' (EXC_ARITHMETIC)',
                '4' => ' (EXC_EMULATION)',
                '5' => ' (EXC_SOFTWARE)',
                '6' => ' (EXC_BREAKPOINT)',
                '7' => ' (EXC_SYSCALL)',
                '8' => ' (EXC_MACH_SYSCALL)',
                '9' => ' (EXC_RPC_ALERT)',
                '10' => ' (EXC_CRASH)'
            );
            if (exists $metype_to_name{$value})
            {
                $comment = $metype_to_name{$value};
            }
		}
		printf("\t%*s = %s$comment\n", $max_register_name_len, $key, $value);
	}
}

#----------------------------------------------------------------------
# Dumps a Stop Reply Packet which happens in response to a step, 
# continue, last signal, and probably a few other commands.
#----------------------------------------------------------------------
sub dump_stop_reply_packet
{
	my $what = shift(@_);
	if ($what eq 'S' or $what eq 'T')
	{
	    my $signo = get8(\@_);
	    
	    our %signo_to_name = (
                '1'  => ' SIGHUP',
                '2'  => ' SIGINT',
                '3'  => ' SIGQUIT',
                '4'  => ' SIGILL',
                '5'  => ' SIGTRAP',
                '6'  => ' SIGABRT',
                '7'  => ' SIGPOLL/SIGEMT',
                '8'  => ' SIGFPE',
                '9'  => ' SIGKILL',
                '10' => ' SIGBUS',
                '11' => ' SIGSEGV',
                '12' => ' SIGSYS',
                '13' => ' SIGPIPE',
                '14' => ' SIGALRM',
                '15' => ' SIGTERM',
                '16' => ' SIGURG',
                '17' => ' SIGSTOP',
                '18' => ' SIGTSTP',
                '19' => ' SIGCONT',
                '20' => ' SIGCHLD',
                '21' => ' SIGTTIN',
                '22' => ' SIGTTOU',
                '23' => ' SIGIO',
                '24' => ' SIGXCPU',
                '25' => ' SIGXFSZ',
                '26' => ' SIGVTALRM',
                '27' => ' SIGPROF',
                '28' => ' SIGWINCH',
                '29' => ' SIGINFO',
                '30' => ' SIGUSR1',
                '31' => ' SIGUSR2',
                '145' => ' TARGET_EXC_BAD_ACCESS',        # 0x91
                '146' => ' TARGET_EXC_BAD_INSTRUCTION',   # 0x92
                '147' => ' TARGET_EXC_ARITHMETIC',        # 0x93
                '148' => ' TARGET_EXC_EMULATION',         # 0x94
                '149' => ' TARGET_EXC_SOFTWARE',          # 0x95
                '150' => ' TARGET_EXC_BREAKPOINT'         # 0x96
        );
        my $signo_str = sprintf("%i", $signo);
        my $signo_name = '';
	    if (exists $signo_to_name{$signo_str})
        {
            $signo_name = $signo_to_name{$signo_str};
        }
		printf ("signal (signo=%u$signo_name)\n", $signo);
		dump_stop_reply_data (@_);
	}
	elsif ($what eq 'W')
	{
		print 'process_exited( ' . shift(@_) . shift(@_) . " )\n";
	}
	elsif ($what eq 'X')
	{
		print 'process_terminated( ' . shift(@_) . shift(@_) . " )\n";
	}
	elsif ($what eq 'O')
	{
		my $console_output = '';
		my $num_hex8_bytes = @_/2;
		for (1 .. $num_hex8_bytes)
		{
			$console_output .= sprintf("%c", get8(\@_))
		}
		
		print "program_console_output('$console_output')\n";
	}
}

#----------------------------------------------------------------------
# '?' command
#----------------------------------------------------------------------
sub dump_last_signal_cmd
{
	my $cmd = shift;
	print 'last_signal (' . join('',@_) . ")\n";
}

sub dump_raw_command
{
	my $cmd_aref = shift;
	my $callback_ref;
	$curr_cmd = $$cmd_aref[0];

    if ($curr_cmd eq 'q' or $curr_cmd eq 'Q' or $curr_cmd eq '_')
    {
        $curr_full_cmd = '';
        foreach my $ch (@$cmd_aref)
        {
            $ch !~ /[A-Za-z_]/ and last;
            $curr_full_cmd .= $ch;
        }
    }
    else
    {
        $curr_full_cmd = $curr_cmd;
    }
	
	$curr_cmd eq '_' and $curr_cmd .= $$cmd_aref[1];	
	$callback_ref = $cmd_callbacks{$curr_cmd};
	if ($callback_ref)
	{
		&$callback_ref(@$cmd_aref);
	}
	else
	{
		# Strip the command byte for responses since we injected that above
		dump_other_cmd(@$cmd_aref); 
	} 		
}

sub dump_standard_response
{
	my $cmd_aref = shift;
	
	my $cmd_len = scalar(@$cmd_aref);
	if ($cmd_len == 0)
	{
		print "$unimplemented_str\n";
		return 1;
	}	

	my $response = join('', @$cmd_aref);
	if ($response eq 'OK')
	{
		print "$success_str\n";
		return 1;
	}
	
	if ($cmd_len == 3 and index($response, 'E') == 0)
	{
		print "ERROR: " . substr($response, 1) . "\n";
		return 1;		
	}
	
	return 0;
}
sub dump_raw_response
{
	my $cmd_aref = shift;
	my $callback_ref;
	
	if ($packet_start_time != 0.0)
	{
	    if (length($curr_full_cmd) > 0)
	    {
            $packet_times{$curr_full_cmd} += $curr_time - $packet_start_time;
	    }
	    else
	    {
            $packet_times{$curr_cmd} += $curr_time - $packet_start_time;
	    }
        $packet_start_time = 0.0;
	}
	
	$callback_ref = $rsp_callbacks{$curr_cmd};

	if ($callback_ref)
	{
		&$callback_ref(@$cmd_aref);
	}
	else
	{
		dump_standard_response($cmd_aref) or dump_other_rsp(@$cmd_aref);
	} 	
	
}
#----------------------------------------------------------------------
# Dumps any command and handles simple error checking on the responses
# for commands that are unsupported or OK.
#----------------------------------------------------------------------
sub dump_command
{
	my $cmd_str = shift;

	# Dump the original command string if verbose is on
	if ($opt_v)
	{
		print "dump_command($cmd_str)\n    ";
	}

	my @cmd_chars = extract_command($cmd_str);
	my $is_cmd = 1;
	
	my $cmd = $cmd_chars[0];
	if ($cmd eq '$')
	{
		$is_cmd = 0;		# Note that this is a reply
		$cmd = $curr_cmd;	# set the command byte appropriately
		shift @cmd_chars;	# remove the '$' from the cmd bytes
	}
	
	# Check for common responses across all commands and handle them
	# if we can
	if ( $is_cmd == 0 )
	{
		if (rsp_is_unsupported(@cmd_chars))
		{
			print "$unimplemented_str\n";
			return;
		}
		elsif (rsp_is_OK(@cmd_chars))
		{
			print "$success_str\n";
			return;
		}
		# Strip the checksum information for responses
		strip_checksum(\@cmd_chars);
	}

	my $callback_ref;
	if ($is_cmd) {
		$callback_ref = $cmd_callbacks{$cmd};
	} else {
		$callback_ref = $rsp_callbacks{$cmd};
	}

	if ($callback_ref)
	{
		&$callback_ref(@cmd_chars);
	}
	else
	{
		# Strip the command byte for responses since we injected that above
		if ($is_cmd) {
			dump_other_cmd(@cmd_chars); 
		} else {
			dump_other_rsp(@cmd_chars);
		}
		
	} 	
}


#----------------------------------------------------------------------
# Process a gdbserver log line by looking for getpkt and putkpt and
# tossing any other lines.

#----------------------------------------------------------------------
sub process_log_line
{
	my $line = shift;
	#($opt_v and $opt_g) and print "# $line";

	my $extract_cmd = 0;
	my $delta_time = 0.0;
	if ($line =~ /^(\s*)([1-9][0-9]+\.[0-9]+)([^0-9].*)$/)
	{
	    my $leading_space = $1;
	    $curr_time = $2;
	    $line = $3;
	    if ($base_time == 0.0)
	    {
	        $base_time = $curr_time;
	    }
	    else
	    {
	        $delta_time = $curr_time - $last_time;
	    }
	    printf ("(%.6f, %+.6f): ",  $curr_time - $base_time, $delta_time);
	    $last_time = $curr_time;
	}
	else
	{
	    $curr_time = 0.0
	}

	if ($line =~ /getpkt /)
	{
		$extract_cmd = 1;
		print "\n--> ";
		$packet_start_time = $curr_time;
	}
	elsif ($line =~ /putpkt /)
	{
		$extract_cmd = 1;
		print "<-- ";
	}
	elsif ($line =~ /.*Sent:  \[[0-9]+\.[0-9]+[:0-9]*\] (.*)/)
	{
		$opt_g and print "maintenance dump-packets command: $1\n";
		my @raw_cmd_bytes = split(/ */, $1);
		$packet_start_time = $curr_time;
		print "\n--> ";
		dump_raw_command(\@raw_cmd_bytes);
		process_log_line($2);
	}
	elsif ($line =~ /.*Recvd: \[[0-9]+\.[0-9]+[:0-9]*\] (.*)/)
	{
		$opt_g and print "maintenance dump-packets reply: $1\n";
		my @raw_rsp_bytes = split(/ */, $1);
		print "<-- ";
		dump_raw_response(\@raw_rsp_bytes);		
		print "\n";
	}
	elsif ($line =~ /getpkt: (.*)/)
	{
		if ($1 =~ /\$([^#]+)#[0-9a-fA-F]{2}/)
		{
			$opt_g and print "command: $1\n";
			my @raw_cmd_bytes = split(/ */, $1);
			print "--> ";
    		$packet_start_time = $curr_time;
			dump_raw_command(\@raw_cmd_bytes);			
		}
		elsif ($1 =~ /\+/)
		{
			#print "--> ACK\n";
		}
		elsif ($1 =~ /-/)
		{
			#print "--> NACK\n";
		}
	}
	elsif ($line =~ /putpkt: (.*)/)
	{
		if ($1 =~ /\$([^#]+)#[0-9a-fA-F]{2}/)
		{
			$opt_g and print "response: $1\n";
			my @raw_rsp_bytes = split(/ */, $1);
			print "<-- ";
			dump_raw_response(\@raw_rsp_bytes);		
			print "\n";
		}
		elsif ($1 =~ /\+/)
		{
			#print "<-- ACK\n";
		}
		elsif ($1 =~ /-/)
		{
			#print "<-- NACK\n";
		}
	}
	elsif ($line =~ /send packet: (.*)/)
	{
		if ($1 =~ /\$([^#]+)#[0-9a-fA-F]{2}/)
		{
			$opt_g and print "command: $1\n";
			my @raw_cmd_bytes = split(/ */, $1);
			print "--> ";
    		$packet_start_time = $curr_time;
			dump_raw_command(\@raw_cmd_bytes);			
		}
		elsif ($1 =~ /\+/)
		{
			#print "--> ACK\n";
		}
		elsif ($1 =~ /-/)
		{
			#print "--> NACK\n";
		}
	}
	elsif ($line =~ /read packet: (.*)/)
	{
		if ($1 =~ /\$([^#]*)#[0-9a-fA-F]{2}/)
		{
			$opt_g and print "response: $1\n";
			my @raw_rsp_bytes = split(/ */, $1);
			print "<-- ";
			dump_raw_response(\@raw_rsp_bytes);		
			print "\n";
		}
		elsif ($1 =~ /\+/)
		{
			#print "<-- ACK\n";
		}
		elsif ($1 =~ /-/)
		{
			#print "<-- NACK\n";
		}
	}
	elsif ($line =~ /Sending packet: \$([^#]+)#[0-9a-fA-F]{2}\.\.\.(.*)/)
	{
		$opt_g and print "command: $1\n";
		my @raw_cmd_bytes = split(/ */, $1);
		print "\n--> ";
		$packet_start_time = $curr_time;
		dump_raw_command(\@raw_cmd_bytes);
		process_log_line($2);
	}
	elsif ($line =~ /Packet received: (.*)/)
	{
		$opt_g and print "response: $1\n";
		my @raw_rsp_bytes = split(/ */, $1);
		print "<-- ";
		dump_raw_response(\@raw_rsp_bytes);		
		print "\n";
	}
	
	if ($extract_cmd)
	{
		my $beg = index($line, '("') + 2;
		my $end = rindex($line, '");');
		$packet_start_time = $curr_time;
		dump_command(substr($line, $beg, $end - $beg));
	}
}


our $line_num = 0;
while(<>)
{
	$line_num++;
	$opt_q or printf("# %5d: $_", $line_num);
	process_log_line($_);
}

if (%packet_times)
{
    print "----------------------------------------------------------------------\n";
    print "Packet timing summary:\n";
    print "----------------------------------------------------------------------\n";
    print "Packet                 Time       %\n";
    print "---------------------- -------- ------\n";
    my @packet_names = keys %packet_times;
    my $total_packet_times = 0.0;
    foreach my $key (@packet_names)
    {
        $total_packet_times += $packet_times{$key};
    }

    foreach my $value (sort {$packet_times{$b} cmp $packet_times{$a}} @packet_names)
    {
        my $percent = ($packet_times{$value} / $total_packet_times) * 100.0;
        if ($percent < 10.0)
        {
            printf("%22s %1.6f   %2.2f\n", $value, $packet_times{$value}, $percent);
            
        }
        else
        {
            printf("%22s %1.6f  %2.2f\n", $value, $packet_times{$value}, $percent);            
        }
    }   
    print "---------------------- -------- ------\n";
    printf ("                 Total %1.6f 100.00\n", $total_packet_times);
}







