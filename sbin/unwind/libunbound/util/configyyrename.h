/*
 * configyyrename.h -- renames for config file yy values to avoid conflicts.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef UTIL_CONFIGYYRENAME_H
#define UTIL_CONFIGYYRENAME_H

/* defines to change symbols so that no yacc/lex symbols clash */
#define yymaxdepth ub_c_maxdepth
#define yyparse ub_c_parse
#define yylex   ub_c_lex
#define yyerror ub_c_error
#define yylval  ub_c_lval
#define yychar  ub_c_char
#define yydebug ub_c_debug
#define yypact  ub_c_pact
#define yyr1    ub_c_r1
#define yyr2    ub_c_r2
#define yydef   ub_c_def
#define yychk   ub_c_chk
#define yypgo   ub_c_pgo
#define yyact   ub_c_act
#define yyexca  ub_c_exca
#define yyerrflag ub_c_errflag
#define yynerrs ub_c_nerrs
#define yyps    ub_c_ps
#define yypv    ub_c_pv
#define yys     ub_c_s
#define yy_yys  ub_c_yys
#define yystate ub_c_state
#define yytmp   ub_c_tmp
#define yyv     ub_c_v
#define yy_yyv  ub_c_yyv
#define yyval   ub_c_val
#define yylloc  ub_c_lloc
#define yyreds  ub_c_reds
#define yytoks  ub_c_toks
#define yylhs   ub_c_yylhs
#define yylen   ub_c_yylen
#define yydefred ub_c_yydefred
#define yydgoto ub_c_yydgoto
#define yysindex ub_c_yysindex
#define yyrindex ub_c_yyrindex
#define yygindex ub_c_yygindex
#define yytable  ub_c_yytable
#define yycheck  ub_c_yycheck
#define yyname   ub_c_yyname
#define yyrule   ub_c_yyrule
#define yyin    ub_c_in
#define yyout   ub_c_out
#define yywrap  ub_c_wrap
#define yy_load_buffer_state ub_c_load_buffer_state
#define yy_switch_to_buffer ub_c_switch_to_buffer
#define yy_flush_buffer ub_c_flush_buffer
#define yy_init_buffer ub_c_init_buffer
#define yy_scan_buffer ub_c_scan_buffer
#define yy_scan_bytes ub_c_scan_bytes
#define yy_scan_string ub_c_scan_string
#define yy_create_buffer ub_c_create_buffer
#define yyrestart ub_c_restart
#define yy_delete_buffer ub_c_delete_buffer
#define yypop_buffer_state ub_c_pop_buffer_state
#define yypush_buffer_state ub_c_push_buffer_state
#define yyunput ub_c_unput
#define yyset_in ub_c_set_in
#define yyget_in ub_c_get_in
#define yyset_out ub_c_set_out
#define yyget_out ub_c_get_out
#define yyget_lineno ub_c_get_lineno
#define yyset_lineno ub_c_set_lineno
#define yyset_debug ub_c_set_debug
#define yyget_debug ub_c_get_debug
#define yy_flex_debug ub_c_flex_debug
#define yylex_destroy ub_c_lex_destroy
#define yyfree ub_c_free
#define yyrealloc ub_c_realloc
#define yyalloc ub_c_alloc
#define yymalloc ub_c_malloc
#define yyget_leng ub_c_get_leng
#define yylineno ub_c_lineno
#define yyget_text ub_c_get_text
#define yyss    ub_c_ss
#define yysslim ub_c_sslim
#define yyssp   ub_c_ssp
#define yystacksize ub_c_stacksize
#define yyvs    ub_c_vs
#define yyvsp   ub_c_vsp

#endif /* UTIL_CONFIGYYRENAME_H */
