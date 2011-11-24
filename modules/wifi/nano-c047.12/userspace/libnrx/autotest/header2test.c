/* Copyright (C) 2007 Nanoradio AB */
/* $Id: header2test.c 9954 2008-09-15 09:41:38Z joda $ */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define DEBUG(level, args...)   do {if(level<=settings.debuglevel) fprintf(stderr, args);} while(0)

#define DEBUG_HIGH(args...)   DEBUG(3, args)
#define DEBUG_MED(args...)    DEBUG(4, args)
#define DEBUG_LOW(args...)    DEBUG(5, args)

#define ERROR(args...) do { \
   DEBUG(1, "ERROR[%d]: ", __LINE__); \
   DEBUG(1, args); \
}while(0)

#define FATAL(args...) do { \
   DEBUG(0, "FATAL[%d]: ", __LINE__); \
   DEBUG(0, args); \
   exit(1); \
}while(0)

#define ASSERT(x) do { \
   if (!(x)) \
      FATAL("Assertion failed on \"%s\".\n", #x); \
}while(0)

#define ELEMENTS_IN_VECTOR(x) (sizeof(x)/sizeof(*x))


/*******************************************/
/*                                         */
/*               CONFIGURATION             */
/*                                         */
/*******************************************/

typedef struct {
      int help;
      int debuglevel;
      int dump_func;
      int dump_name;
      int dump_vars;
      int call_switch;
      int test_func;
      int stubs;
}settings_t;

settings_t settings = {
   .help        = 0,
   .debuglevel  = 1,
   .dump_func   = 0,
   .dump_name  = 0,
   .dump_vars   = 0,
   .call_switch = 0,
   .test_func   = 0,
   .stubs       = 0,
};

enum opt_type_t {FLAG, INT};
typedef struct {
      char *arg_name;
      char *arg_short;
      char *help_text;
      enum opt_type_t opt_type;
      int  *variable;
}option_t;

option_t options[] = {
   {"--help",      "-h", "Show this help.",               FLAG, &(settings.help)},
   {"--debug",     "-d", "Set debug level, range 0-5.",   INT,  &settings.debuglevel},
   {"--functions", "-f", "Writes function declarations.", FLAG, &settings.dump_func},
   {"--func_name", "-n", "Writes function names only.",   FLAG, &settings.dump_name},
   {"--variables", "-v", "Writes variable types.",        FLAG, &settings.dump_vars},
   {"--call",      "-c", "Writes call switch.",           FLAG, &settings.call_switch},
   {"--testfunc",  "-t", "Writes test functions.",        FLAG, &settings.test_func},
   {"--stubs",     "-s", "Writes stubs for functions.",   FLAG, &settings.stubs},
};

int configure(int argc, char *argv[])
{
   int i, j;
   for (i = 1; i < argc; i++) {
      DEBUG_LOW("Search %s\n", argv[i]);
      for (j = 0; j < ELEMENTS_IN_VECTOR(options); j++) {
         DEBUG_LOW("Try %s\n", options[j].arg_name);
         if (!strcmp(argv[i], options[j].arg_name) || !strcmp(argv[i], options[j].arg_short)) {
            DEBUG_LOW("Found %s\n", options[j].arg_name);
            if (options[j].opt_type == FLAG) {
               *(options[j].variable) = 1;
               DEBUG_LOW("Flag set for %s\n", options[j].arg_name);
            }
            else if (options[j].opt_type == INT) {
               if (argv[i+1] != NULL && *argv[i+1] >= '0' && *argv[i+1]<='9') {
                  *options[j].variable = atoi(argv[i+1]);
                  i++;
               }
               else {
                  ERROR("Option \"%s\" requires a numeric value.\n", argv[i]);
                  return 1;
               }
            }
            else {
               FATAL("Internal error. Unknown type.\n");
            }
            break;
         }
      }
      if (j == ELEMENTS_IN_VECTOR(options)) { // Not found
         ERROR("Unknown option: %s\n", argv[i]);
         return 1;
      }
   }
   return 0;
}

int show_help()
{
   int j;
   printf("This progam's options:\n");
   for (j = 0; j < ELEMENTS_IN_VECTOR(options); j++)
      printf("    %s, %-11s %-5s %s\n", 
             options[j].arg_short, 
             options[j].arg_name, 
             options[j].opt_type == INT ? "<n>" : "",
             options[j].help_text);
}


/*******************************************/
/*                                         */
/*              DOXYGEN STUFF              */
/*                                         */
/*******************************************/

int doxy_beautification(char *outbuf, int len, const char *doxygen)
{
   const char *ch = doxygen;
   const char *p;
   int bracket = 0;
   int indentation = 0;

   if (!doxygen[0]) {
      *outbuf = '\0';
      return 0;
   }

   /* Find out indentation + Skip blanks*/
   while (*ch && (*ch=='\r' || *ch=='\n' || *ch==' ' || *ch=='*' || *ch=='!')) {
      indentation++;
      if (*ch=='\r' || *ch=='\n')
         indentation = 0;
      ch++;
   }
   
   while (*ch) {
      int ind = 0;
      int newlines = 0;
      while (*ch && *ch!='\r' && *ch!='\n') {
         if (*ch == '<' && *(ch-1) != '\\') /* remove html tags */
            bracket = 1;
         if (!bracket) 
               *outbuf++ = *ch;
         if (*ch == '>' && *(ch-1) != '\\')
            bracket = 0;
         ch++;
      }

      p = ch;
      while (*p && (*p=='\r' || *p=='\n' || *p==' ' || *p=='*')) {
         if (*ch=='\r' || *ch=='\n') {
            ind = 0;
            if (*p=='\n')
               newlines++;
         }
         p++;
         if (ind++ <= indentation)
            ch = p;
      }
      if (*p == '\0')
         break;
      for (;newlines > 0; newlines--)
         *outbuf++ = '\n';
   }
   *outbuf = '\0';
   return 0;
}


const char *doxy_find_tag(const char *doxytext, const char *tag, const char *key)
{
   const char *p;
   int i;

   for (p = strstr(doxytext, tag); p != NULL; p = strstr(p, tag)) { /* loop over tag word */
      const char *p2;
      /* Check it's a tag */
      if (*(p-1) != '@' && *(p-1) != '\\')
         continue;
      p = p + strlen(tag);
      if (*p != ' ' && *p != '\n')
         continue;

      for ( ; *p == ' ' || *p == '\n' ;p++) ; /* skip spaces */

      if (key == NULL)
         return p;

      /* Check key */
      p2 = key;
      while (*p && &p2 && *p == *p2) {
         p++;
         p2++;
      }
      if (*p2 == '\0' && (*p == ' ' || *p == '\n')) { /* agreed */
         for ( ; *p == ' ' || *p == '\n' ;p++) ;
         return p;
      }
   }

   return NULL;
}

#define IS_ALPHA(x) ((x >= 'A' && x <= 'Z') || (x >= 'a' && x <= 'z'))

int doxy_get_keyword_data(char *outbuf, int len, const char *text, const char *tag, const char *key)
{
   const char *p;
   int i = 0;

   *outbuf = '\0';
   for (p = doxy_find_tag(text, tag, key);
        p != NULL;
        p = doxy_find_tag(p, tag, key)) /* loop over tags */
   {
      /* Add to string */
      while (*p 
             && i+2 < len       /* check size */
             && !(*(p-1) == '\n' && *p == '\n') /* two linefeeds in a row => end of section */
             && !((*p == '@' || *p =='\\') && IS_ALPHA(*(p+1))) ) /* new @tag */
      {
            char ch = *p;
            if (ch == '\n')
               ch = ' ';
            if (i == 0 || !(ch == ' ' && outbuf[i-1] == ' '))
               outbuf[i++] = ch;
         p++;
      }

      outbuf[i++] = ' ';
      outbuf[i] = '\0';
      
      for (i = i - 1 ; i >= 0  && (outbuf[i] == ' ' || outbuf[i] == '\n'); i--) /* Remove trailing spaces */
         outbuf[i] = '\0';
   }

   return (*outbuf == '\0');
}


/*******************************************/
/*                                         */
/*           HEADER FILE PARSER            */
/*                                         */
/*******************************************/

typedef struct variable_s {
      char type[128];
      char name[128];
} variable_t;

typedef struct {
      char type[128];
      char name[128];
      char help[16384];
      char brief[256];
      int var_count;
      variable_t variables[25];
} function_t;

int line = 0;

int variable_type_and_name_separaor(const char *buf, char *type, char *name)
{
   const char *p;
   for (p = &buf[strlen(buf)-1]; p > buf && *p != ' ' && *p != '*'; p--); // Find last space
   ASSERT(p > buf);
   ASSERT(strlen(p+1));
   if (*p == ' ') 
      strncpy(type, buf, p-buf);
   else
      strncpy(type, buf, (p-buf) + 1);
   strcpy(name, p + 1);

   return 0;
}


#define COMPRESSABLE "(){}[].,;*!=&| "
enum mode_t { SEARCHING, FUNC_NAME, FUNC_VAR, FUNC_END};
char *modes[] = { "SEARCHING", "FUNC_NAME", "FUNC_VAR", "FUNC_END"};

int read_to_separator(FILE *fin, function_t *function) 
{
   enum mode_t mode = SEARCHING;

   int parenthesis = 0;
   int bracket = 0;
   int curl_bracket = 0;
   int comment = 0;
   int comment_line = 0;
   int define = 0;
   char ch = '\0';
   char prev = '\0';
   char buf[1000];
   char *pek = buf;
   int doxy = 0;
   char doxygen[sizeof(function->help)]; /* hopefully sufficient */

   memset(function, 0, sizeof(function_t));

   *buf = '\0';
   
   while (1) {
      prev = ch;

      ch = (char)fgetc(fin);
      DEBUG_MED("%c", ch);
      
      if (feof(fin))
         if (mode == SEARCHING) {
            DEBUG_HIGH("End of file.\n");
            break;
         }
         else {
            FATAL("Unexpected end of file (mode = %s (%s))\n", modes[mode], buf);
         }
      if (ferror(fin)) {
         FATAL("Error reading file (%s)\n", strerror(errno));
      }

      if (ch == '\n') {
         DEBUG_LOW("<BR>");
         line++;
      }

      // Skip includes & defines
      if (!comment && !comment_line && ch == '#') {
         define++;
         ASSERT(define == 1);
         DEBUG_LOW("<DEFINE %d>", define);
         continue;
      }
      if (define && ch == '\n') {
         // Line ending with \ will extend the #define line.
         if (prev != '\\' || comment || comment_line) {
            DEBUG_LOW("</DEFINE %d>", define);
            define--;
            ASSERT(define >= 0);
         }
         else
            DEBUG_LOW("<DEFINE CONTINUED>");
      }
      if (define)
         continue;

      // Clean up of spaces
      if (pek > buf && *(pek-1) == ' ' && strchr(COMPRESSABLE, ch)) {
         pek--;
         *pek = '\0';
         DEBUG_LOW("<BACK>");
      }

      // Remove comments
      if (!comment && prev == '/' && ch == '/') {
         comment_line++;
         pek--;
         *pek = '\0';
         DEBUG_LOW("<REM_LINE>");
         continue;
      }
      if (comment_line && ch == '\n') {
         comment_line = 0;
         ASSERT(!comment);
         ch = ' ';
         DEBUG_LOW("</REM_LINE>");
      }
      if (prev == '/' && ch == '*') {
         comment++;
         pek--;
         *pek = '\0';
         if (mode == SEARCHING) {
            memset(doxygen, 0, sizeof(doxygen));
            doxy = 0;
         }
         DEBUG_LOW("<REM>");
         continue;
      }
      if (comment && prev == '*' && ch == '/') {
         comment = 0;
         ch = ' ';
         DEBUG_LOW("</REM>");
      }
      if (comment || comment_line) {
         if (mode == SEARCHING) 
            doxygen[doxy++] = ch;
         continue;
      }

      // Brackets and parenthesis
      if (ch == '(') {
         //printf("mode = %s", modes[mode]);
         ASSERT(mode == FUNC_NAME);
         ASSERT(strlen(buf));
         parenthesis++;
         ASSERT(parenthesis == 1); // Can there ever be more?
         mode = FUNC_VAR;
         variable_type_and_name_separaor(buf, function->type, function->name);
         pek = buf;
         *pek = '\0';
         DEBUG_LOW("<PARENTHESIS %d>", parenthesis);
         continue;
      }
      if (ch == ')') {
         parenthesis--;
         ASSERT(parenthesis >= 0);
         ASSERT(mode == FUNC_VAR);
         ASSERT(function->var_count < ELEMENTS_IN_VECTOR(function->variables));
         DEBUG_LOW("</PARENTHESIS %d>", parenthesis);
         if (parenthesis == 0) {
            mode = FUNC_END;
            variable_type_and_name_separaor(buf, 
                                            function->variables[function->var_count].type,
                                            function->variables[function->var_count].name);
            function->var_count++;
         }
         continue;
      }
      if (ch == '[') {
         bracket++;
         ASSERT(bracket == 1);    // Can there ever be more?
         DEBUG_LOW("<BRACKET %d>", bracket);
         continue;
      }
      if (ch == ']') {
         bracket--;
         ASSERT(bracket >= 0);
         DEBUG_LOW("</BRACKET %d>", bracket);
         continue;
      }
      if (ch == '{') {
         curl_bracket++;
         //ASSERT(mode == STRUCT);
         ASSERT(curl_bracket == 1); // Can there ever be more?
         DEBUG_LOW("<CURL_BRACKET %d>", curl_bracket);
         continue;
      }
      if (ch == '}') {
         curl_bracket--;
         ASSERT(curl_bracket >= 0);
         //ASSERT(mode == STRUCT);
         DEBUG_LOW("</CURL_BRACKET %d>", curl_bracket);
         continue;
      }

      // Clean up of spaces
      if (strchr(" \n\r\t", ch)) {
         ch = ' ';
         if (strlen(buf)==0 || strchr(COMPRESSABLE, *(pek-1))) { // Don't need spaces inbetween.
            DEBUG_LOW("<SKIP>");
            continue;
         }
      }

      // Separators
      if (ch == ',') {
         ASSERT(mode == FUNC_VAR);
         ASSERT(function->var_count < ELEMENTS_IN_VECTOR(function->variables));
         variable_type_and_name_separaor(buf, 
                                         function->variables[function->var_count].type,
                                         function->variables[function->var_count].name);
         function->var_count++;
         pek = buf;
         *pek = '\0';
         continue;
      }
      if (ch == ';') {
         if (parenthesis == 0 && bracket == 0 && curl_bracket == 0) {
            DEBUG_LOW("<SEPARATOR>");
            ASSERT(mode == FUNC_END);
            doxy_beautification(function->help, sizeof(function->help), doxygen);
            doxy_get_keyword_data(function->brief, sizeof(function->brief), function->help, "brief", NULL);
            return 0;
         }
         else
            DEBUG_LOW("<!SEPARATOR p=%d, b=%d, cb=%d>", parenthesis, bracket, curl_bracket);
      }

      if (mode == SEARCHING)
         if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || strchr("_", ch)) {
            mode = FUNC_NAME;
            DEBUG_LOW("<MODE FUNC_NAME>");
         }

      // Adding to buffer
      *pek=ch;
      pek++;
      *pek='\0';
      if (pek - buf >= sizeof(buf) - 1) {
         FATAL("Buffer full\n");
      }

      
   } // while 1

   return 1;
}


/*******************************************/
/*                                         */
/*              FUNCTION LIST              */
/*                                         */
/*******************************************/

typedef struct function_list_s {
      struct function_list_s *next;
      function_t function;
} function_list_t;


function_list_t *list = NULL;
function_list_t *last = NULL;

int add_to_function_list(FILE *fin)
{
   line = 1;

   while (1) {
      function_list_t *element;
      function_t *function;
      int i, done;
      element = malloc(sizeof(function_list_t));
      ASSERT(function != NULL);
      function = &element->function;
      done = read_to_separator(fin, function);
      if (done) {
         free(element);
         break;
      }
      else {
         element->next = NULL;
         if (list == NULL)
            list = element;
         if (last != NULL)
            last->next = element;
         last = element;
      }
   }
   
   return 0;
}


int free_function_list()
{
   while (list != NULL) {
      function_list_t *element;
      element = list;
      list = element->next;
      free(element);
   }
   last = NULL;
}


/*******************************************/
/*                                         */
/*             GENERATE OUTPUT             */
/*                                         */
/*******************************************/

int dump_function(const function_t *function)
{
   int i;
   printf("%s %s(", function->type, function->name);
   for (i = 0; i < function->var_count; i++)
      printf(
         "%s %s%s", 
         function->variables[i].type,
         function->variables[i].name,
         i < function->var_count - 1 ? ", " : ""
         );
   printf(")");
}

int dump_all_functions()
{
   function_list_t *element;
   
   for (element = list; element != NULL; element = element->next) {
      dump_function(&element->function);
      printf(";\n");
   }

   return 0;
}

int dump_all_func_names()
{
   function_list_t *element;
   
   for (element = list; element != NULL; element = element->next) 
      printf("%s\n", element->function.name);

   return 0;
}


int dump_vars()
{
   function_list_t *element;
   int i;
   
   for (element = list; element != NULL; element = element->next) {
      for (i = 0; i < element->function.var_count; i++)
         printf(
            "%s\n", 
            element->function.variables[i].type
            );
   }
   return 0;
}


int create_call_switch()
{
   function_list_t *element = list;
   printf(
      "/****************************************************************/\n"
      "/* Variable that must exist:                                    */\n"
      "/* char *testobj  - A string with name of function to be tested.*/\n"
      "/* char *argv[]   - A string vector, where argv[0] to argv[N-1] */\n"
      "/*                  above are arguments to the function. Last   */\n"
      "/*                  element, argv[N] must be NULL. This is      */\n"
      "/*                  similar to the main() function in C.        */\n"
      "/* int ret        - Will contain test outcome, 0 means success. */\n"
      "/****************************************************************/\n"
      );
   
   printf("   ");
   for (element = list; element != NULL; element = element->next) {
      printf(
         "if (!strcmp(testobj, \"%s\")) {\n"
         "      ret = test_%s(argc, argv);\n"
         "   }\n", 
         element->function.name, element->function.name);
      if (element->next != NULL)
         printf("   else ");
   }
   printf(
      "/***************************************************************/\n"
      "/* END OF AUTO-GENERATED SECTION.                              */\n"
      "/***************************************************************/\n"
      );

   return 0;
}

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
#ifndef __uint32_t_defined
typedef unsigned int   uint32_t;
#endif


enum direction_t {INPUT, OUTPUT, INTERNAL};

typedef struct {
      char *type;
      char *name;
      enum direction_t direction;
      char *declaration;
      char *initialization;
      char *len_and_size;
      char *call_str;
      char *printout;
      char *help;
} var_entry_t;

#define ENTRY(a1, a2, a3, a4, a5, a6, a7, a8, a9) {a1, a2, a3, a4, a5, a6, a7, a8, a9}

var_entry_t variables[] = {
   {"nrx_context" ,NULL, INTERNAL, NULL, NULL, NULL, "%s", NULL, NULL},
   {"size_t", "len",  INTERNAL,  "size_t %s=4096;", NULL, "if (%s<0)\n      FATAL(\"Length needed.\\n\");", "%s", "printf(\"%%d\", %s);", "Length of vector"},
   {"void*", "cb_ctx", INTERNAL, "void *%s;", "%s=(void*)&event;", NULL, "%s", "printf(\"%p\",cb_ctx);", "Data for callback function"},
   {"nrx_callback_t", NULL, INTERNAL, "nrx_callback_t %s;", "%s = callback;", NULL, "%s", "printf(\"%%p\",%s);", "Callback function"},

   {"size_t*", "len", OUTPUT,    "size_t %s=0;", NULL, "if (%s<0)\n      FATAL(\"Length needed.\\n\");", "&%s", "printf(\"%%d\", %s);", "Length of vector"},
   {"char*",       NULL, OUTPUT, "char %s[4096];", "init_char_p(%s);", "len=sizeof(%s);", "%s", "printf(\"%%s\", %s);", "String"},
   {"const char*", NULL, INPUT,  "char %s[4096];",  "init_const_char_p(%s,argv);", "len=strlen(%s);", "%s", "printf(\"%%s\", %s);", "String"},
   {"const void*", NULL, INPUT,  "uint8_t %s[4096];", "init_const_uint8_t_p(%s,&len,argv);", NULL, "(void*)%s", "hexdump(%s, len);", "Binary buffer given in hex, e.g. 0x123456789abcdef"},
   {"int",         NULL, INPUT,  "int %s;", "init_int(&%s, argv);", "len=-1;", "%s", "printf(\"%%d\", %s);", "Integer value"},
   {"int32_t",     NULL, INPUT,  "int32_t %s;", "init_int(&%s, argv);", "len=-1;", "%s", "printf(\"%%d\", %s);", "Integer value"},
   {"int8_t",     NULL, INPUT,  "int8_t %s;", "init_int8_t(&%s, argv);", "len=-1;", "%s", "printf(\"%%d\", %s);", "Integer value"},
   {"uint32_t",    NULL, INPUT,  "uint32_t %s;", "init_uint32_t(&%s, argv);", "len=-1;", "%s", "printf(\"%%u\", %s);", "Positive integer value"},
   {"nrx_callback_handle",    NULL, INPUT,  "nrx_callback_handle %s;", "init_uint32_t(&%s, argv);", "len=-1;", "%s", "printf(\"%%u\", %s);", "Positive integer value"},
   {"uint16_t",    NULL, INPUT,  "uint16_t %s;", "init_uint16_t(&%s, argv);", "len=-1;", "%s", "printf(\"%%u\", %s);", "Positive integer value"},
   {"uint8_t",     NULL, INPUT,  "uint8_t %s;", "init_uint8_t(&%s, argv);", "len=-1;", "%s", "printf(\"%%u\", %s);", "8-bit unsigned int"},
   {"int32_t*",    NULL, OUTPUT, "int32_t %s=0;",  NULL, "len=-1;", "&%s", "printf(\"%%d\", %s);", "Integer"},
   {"int*",        NULL, OUTPUT, "int32_t %s=0;",  NULL, "len=-1;", "&%s", "printf(\"%%d\", %s);", "Integer"},
   {"uint32_t*",   NULL, OUTPUT, "uint32_t %s=0;", NULL, "len=-1;", "&%s", "printf(\"%%u\", %s);", "Unsigned int"},
   {"unsigned int",NULL, INPUT, "uint32_t %s=0;", "init_uint32_t(&%s, argv);", "len=-1;", "%s", "printf(\"%%u\", %s);", "Positive integer value"},
   {"uint8_t*",    NULL, OUTPUT, "uint8_t %s=0;",  NULL, "len=-1;", "&%s", "printf(\"%%u\", %s);", "8-bit unsigned int"},
   {"const uint8_t*", NULL, INPUT, "uint8_t %s[4096];", "init_const_uint8_t_p(%s,(size_t *)&len,argv);", NULL, "%s", "hexdump(%s,len);", "Hex vector givens as e.g. 0x1234567890abcdef..."},
   {"void*",       "scan_nets", OUTPUT, "uint8_t %s[32768];", "memset(%s,0,32768);", "len=sizeof(%s);", "(void*)%s", "nrx_show_scan(ctx,(char *)%s, len);", "Scan nets"},
   {"void*",       NULL, OUTPUT, "uint8_t %s[4096];", "memset(%s,0,4096);", "len=sizeof(%s);", "(void*)%s", "hexdump(%s, len);", "Binary vector given in hex"},
   {"nrx_adaptive_tx_rate_mode_t", NULL, INPUT, "nrx_adaptive_tx_rate_mode_t %s;", "defines_str_8((int8_t*)&%s, argv);", "len=-1;", "%s", "printf(\"0x%%X\", %s);", "Integer/defines"},
   {"nrx_bss_type_t",       NULL, INPUT, "nrx_bss_type_t %s;", "defines_str_32(&%s, argv);", "len=-1;", "%s", "printf(\"0x%%X\", %s);",  "Integer/defines"},
   {"nrx_ch_list_t",        NULL, INPUT, "nrx_ch_list_t %s;", "init_nrx_ch_list_t(&%s, argv);", "len=-1;", "%s", "print_nrx_ch_list_t(&%s);", "Channel list format: length channel channel channel..."},
   {"const nrx_ch_list_t*", NULL, INPUT, "nrx_ch_list_t %s[1];", "init_nrx_ch_list_t(%s, argv);", "len=-1;", "NULL_ON_ZERO_LEN(%s)", "print_nrx_ch_list_t(%s);", "Channel list format: length channel channel channel... (NULL or 0 equals a null pointer)"},
   {"const nrx_rate_list_t*",NULL, INPUT, "nrx_rate_list_t %s[1];", "init_nrx_rate_list_t(%s, argv);", "len=-1;", "NULL_ON_ZERO_LEN(%s)", "print_nrx_rate_list_t(%s);", "List of rates. Format: length rate1 rate2... (NULL or 0 equals a null pointer)"},
   {"const nrx_retry_list_t*", NULL, INPUT, "nrx_retry_list_t %s[1];", "init_nrx_retry_list_t(%s,argv);", "len=-1;", "NULL_ON_ZERO_LEN(%s)", "print_nrx_retry_list_t(%s);", "List of retries. Format: length retries1 retries2... (NULL or 0 equals a null pointer)"},
   {"const nrx_mac_addr_list_t*", NULL, INPUT, "nrx_mac_addr_list_t %s[1];", "init_const_nrx_mac_addr_list_t(%s,argv);","len=-1;", "NULL_ON_ZERO_LEN(%s)", "print_nrx_mac_addr_list_t(%s);", "List of mac addresses (6 hex characters each): length mac_addr1 mac_addr2... (NULL or 0 equals a null pointer)"},
   {"const nrx_gpio_list_t*", NULL, INPUT, "nrx_gpio_list_t %s[1];", "init_nrx_gpio_list_t(%s, argv);", "len=-1", "NULL_ON_ZERO_LEN(%s)", "print_nrx_gpio_list_t(%s);", "List of gpio pins. Two entries each, pin number (0-4) and active level (0=low, 1=high). Format: length pin_no1 level1 pin_no2 level2..."},

   {"nrx_conn_lost_type_t", NULL, INPUT, "nrx_conn_lost_type_t %s;", "defines_str_32(&%s, argv);", "len=-1;", "%s", "printf(\"0x%%X\", %s);",  "Integer/defines"},
   {"nrx_detection_target_t", NULL, INPUT, "nrx_detection_target_t %s;", "defines_str_32(&%s, argv);", "len=-1;", "%s", "printf(\"0x%%X\", %s);",  "Integer/defines"},
   {"nrx_wmm_ac_t",         NULL, INPUT, "nrx_wmm_ac_t %s;", "defines_str_32(&%s, argv);", "len=-1;", "%s", "printf(\"0x%%X\", %s);",  "Integer/defines"},
   {"nrx_mac_addr_t",       NULL, INPUT, "nrx_mac_addr_t %s;", "init_nrx_mac_addr_t(&%s, argv);", "len=-1;", "%s", "printf_nrx_mac_addr_t(&%s);", "String with 6 colon separated mac octets. Format: 12:34:56:78:90:AB"},
   {"in_addr_t",            NULL, INPUT, "in_addr_t %s;", "init_in_addr_t(&%s, argv);", "len=-1;", "%s", "printf_in_addr_t(&%s);", "Ip-address string, e.g. \\\"192.168.0.1\\\""},
   {"nrx_preamble_type_t",  NULL, INPUT, "nrx_preamble_type_t %s;", "init_nrx_preamble_type_t(&%s, argv);", "len=-1;", "%s", "print_nrx_preamble_type_t(&%s);", "Preamble: NRX_SHORT_PREAMBLE or NRX_LONG_PREAMBLE"},
   {"nrx_rate_t",             NULL, INPUT, "nrx_rate_t %s;", "init_uint8_t(&%s, argv);", "len=-1;", "%s", "printf(\"%%u\", %s);", "Wireless rate in IEEE units (500kbit/s)"},
   {"nrx_rate_t*",          NULL, OUTPUT, "nrx_rate_t %s;", NULL, "len=-1;", "&%s", "printf(\"%%u\", %s);", "Wireless rate in IEEE units (500kbit/s)"},
   {"nrx_region_code_t",    NULL, INPUT, "nrx_region_code_t %s;", "init_nrx_region_code_t(&%s, argv);", "len=-1;", "%s", "print_nrx_region_code_t(&%s);", "Region code: NRX_REGION_JAPAN, NRX_REGION_AMERICA or NRX_REGION_EMEA"},
   {"nrx_scan_type_t",      NULL, INPUT, "nrx_scan_type_t %s;", "init_nrx_scan_type_t(&%s, argv);", "len=-1;", "%s", "print_nrx_scan_type_t(&%s);", "Scan type. Format: NRX_SCAN_ACTIVE or NRX_SCAN_PASSIVE"},
   {"nrx_sn_pol_t",         NULL, INPUT, "nrx_sn_pol_t %s;", "defines_str_32(&%s, argv);", "len=-1;", "%s", "printf(\"0x%%X\", %s);",  "Integer/defines"},
   {"nrx_ssid_t",           NULL, INPUT, "nrx_ssid_t %s;", "init_nrx_ssid_t(&%s, argv);", "len=-1;", "%s", "printf(\"%%s\", %s.octet);", "String containing ssid octets, the string \\\"any\\\" will be passed as a NULL ssid."},
   {"nrx_thr_dir_t",        NULL, INPUT, "nrx_thr_dir_t %s;", "defines_str_32(&%s, argv);", "len=-1;", "%s", "printf(\"0x%%X\", %s);", "Integer/defines"},
   {"nrx_traffic_type_t",   NULL, INPUT, "nrx_traffic_type_t %s;", "defines_str_32(&%s, argv);", "len=-1;", "%s", "printf(\"0x%%X\", %s);", "Integer/defines"},
   {"nrx_job_flags_t",      NULL, INPUT, "nrx_job_flags_t %s;", "defines_str_32(&%s, argv);", "len=-1;", "%s", "printf(\"0x%%X\", %s);", "Integer/defines"},
   {"nrx_antenna_t",        NULL, INPUT, "nrx_antenna_t %s;", "init_nrx_antenna_t(&%s, argv);", "len=-1;", "%s", "print_nrx_antenna_t(&%s);", "Antenna. Format: NRX_ANTENNA_1 or NRX_ANTENNA_2"},   
   {"nrx_sp_len_t",         NULL, INPUT, "nrx_sp_len_t %s;", "init_nrx_sp_len_t(&%s, argv);", "len=-1;", "%s", "print_nrx_sp_len_t(&%s);", "Scan period length. Format: NRX_SP_LEN_ALL, NRX_SP_LEN_2, NRX_SP_LEN_4, NRX_SP_LEN_6"},
   {"nrx_scan_dlv_pol_t",   NULL, INPUT, "nrx_scan_dlv_pol_t %s;", "init_nrx_scan_dlv_pol_t(&%s, argv);", "len=-1;", "%s", "print_nrx_scan_dlv_pol_t(&%s);", "Scan dlv pol. Format: NRX_SCAN_DLV_POL_FIRST or NRX_SCAN_DLV_POL_BEST"},
   {"nrx_scan_job_state_t", NULL, INPUT, "nrx_scan_job_state_t %s;", "init_nrx_scan_job_state_t(&%s, argv);", "len=-1;", "%s", "print_nrx_scan_job_state_t(&%s);", "Scan state. Format: NRX_SCAN_JOB_STATE_SUSPENDED or NRX_SCAN_JOB_STATE_RUNNING"},
   {"nrx_arp_policy_t", NULL, INPUT, "nrx_arp_policy_t %s;", "init_nrx_arp_policy_t(&%s, argv);", "len=-1;", "%s", "print_nrx_arp_policy_t(&%s);", "ARP policy. Format: NRX_ARP_HANDLE_MYIP_FORWARD_REST, NRX_ARP_HANDLE_MYIP_FORWARD_NONE, NRX_ARP_HANDLE_NONE_FORWARD_MYIP or NRX_ARP_HANDLE_NONE_FORWARD_ALL"},
   {"nrx_encryption_t", NULL, INPUT, "nrx_encryption_t %s;", "init_nrx_encryption_t(&%s, argv);", "len=-1;", "%s", "print_nrx_encryption_t(&%s);", "Encryption Mode. Format: NRX_ENCR_DISABLED, NRX_ENCR_WEP, NRX_ENCR_TKIP, NRX_ENCR_CCMP" },
   {"nrx_authentication_t", NULL, INPUT, "nrx_authentication_t %s;", "init_nrx_authentication_t(&%s, argv);", "len=-1;", "%s", "print_nrx_authentication_t(&%s);", "Authentication Mode. Format:  NRX_AUTH_OPEN, NRX_AUTH_SHARED, NRX_AUTH_8021X, NRX_AUTH_AUTOSWITCH, NRX_AUTH_WPA, NRX_AUTH_WPA_PSK, NRX_AUTH_WPA_NONE, NRX_AUTH_WPA_2, NRX_AUTH_WPA_2_PSK" },
   {"nrx_ssid_action_t", NULL, INPUT, "nrx_ssid_action_t %s;", "init_nrx_ssid_action_t(&%s, argv);", "len=-1;", "%s", "print_nrx_ssid_action_t(&%s);", "Adding removing SSID. Format: NRX_SSID_ADD, NRX_SSID_REMOVE" },
   {"nrx_bool",             NULL, INPUT, "nrx_bool %s;", "init_nrx_bool(&%s, argv);", "len=-1;", "%s", "print_nrx_bool(&%s);", "Bool: 0 or 1"},
   {"nrx_bool*",            NULL, OUTPUT, "nrx_bool %s;", NULL, "len=-1;", "&%s", "print_nrx_bool(&%s);", "Bool: 0 or 1"},
   {"nrx_channel_t*",       NULL, OUTPUT, "nrx_channel_t %s;", NULL, "len=-1;", "&%s", "printf(\"%%d\", %s);", "Channel number"},
};


var_entry_t *find_variable(const variable_t *var)
{
   int v;
   for (v = 0; v < ELEMENTS_IN_VECTOR(variables); v++)
      if (!strcmp(var->type, variables[v].type) && 
          (variables[v].name == NULL || !strcmp(var->name, variables[v].name)) )
         break;
   if (v == ELEMENTS_IN_VECTOR(variables)) 
      FATAL("Could not find variable type %s\n", var->type);

   return &variables[v];
}

int create_brief_function(const function_t *function) 
{
   printf("const char brief_%s[] = \"%s\";\n\n", function->name, function->brief);

   return 0;
}

int create_help_function(const function_t *function)
{
   int i;
   int has_input = 0, has_output = 0;
   var_entry_t *var;
   
   printf("int help_%s()\n", function->name);
   printf("{\n");
   
   // Overview of API
   if (function->help[0]) {
      const char *ch = function->help;
      int indentation = 0;
      printf("   printf(\"Overview:\\n\");\n");
      printf("   printf(\"     ");
      while (*ch) {
         if (*ch == '"' || *ch == '\\')  // special cases
            printf("\\");
         if (*ch == '%')
            printf("%%");
         
         if (*ch == '\n')
            printf("\\n\");\n   printf(\"     ");
         else
            printf("%c", *ch);
         
         ch++;
         if (*ch == '\0')
            printf("\\n\");\n");
      }
   }
      
   // Usage stuff
   for (i = 0; i < function->var_count; i++) {
      if (find_variable(&function->variables[i])->direction == INPUT)
         has_input = 1;
      if (find_variable(&function->variables[i])->direction == OUTPUT)
         has_output = 1;
   }
   printf("   printf(\"\\nUsage: %s [OPTIONS]%s\\n\");\n", function->name, has_input?" <INPUT DATA>":"");
   printf("   printf(\"\\nOptions:\\n\");\n");
   printf("   printf(\"     %-30s  %s\\n\");\n", "--help,",   "show this text and exit.");
   printf("   printf(\"     %-30s  %s\\n\");\n", "--ifname,", "name of interface, default: use first wireless found");
   for (i = 0; i < function->var_count; i++)
      if (!strcmp("nrx_callback_t", function->variables[i].type))
         printf("   printf(\"     %-30s  %s\\n\");\n", "--timeout,", "time to wait for trigger.");
   if (has_input) {
      printf("   printf(\"\\nInput data:\\n\");\n");
      for (i = 0; i < function->var_count; i++) {
         var = find_variable(&function->variables[i]);
         if (var->direction == INPUT) { // Only inpust on command line.
            char typename[1000];
            sprintf(typename, "%s %s,", function->variables[i].type, function->variables[i].name);
            printf("   printf(\"     %-30s  %s\\n\");\n", typename, var->help);
         }
      }
   }
   if (has_output) {
      printf("   printf(\"\\nOutput data:\\n\");\n");
      for (i = 0; i < function->var_count; i++) {
         var = find_variable(&function->variables[i]);
         if (var->direction == OUTPUT) { // Only inpust on command line.
            char typename[1000];
            sprintf(typename, "%s %s,", function->variables[i].type, function->variables[i].name);
            printf("   printf(\"     %-30s  %s\\n\");\n", typename, var->help);
         }
      }
   }
   printf("\n   return 0;\n"
          "}\n\n");
   
   return 0;
}


int create_test_function(const function_t *function)
{
   int i, count;
   var_entry_t *var;
   int has_callback = 0;
   int has_len = 0;
   int has_input = 0;
   int ret_void = 0; /* doesn't return */
   int ret_handler = 0; /* return type is callback handler */

   for (i = 0; i < function->var_count; i++) {
      if (find_variable(&function->variables[i])->direction == INPUT)
         has_input = 1;
      if (!strcmp("size_t", function->variables[i].type) || !strcmp("size_t*", function->variables[i].type))
         has_len = 1;
      if (!strcmp("nrx_callback_t", function->variables[i].type))
         has_callback = 1;
   }

   printf("int test_%s(int argc, const char *argv[])\n", function->name);
   printf("{\n");

   printf("   // Do all declarations\n"
          "   const char *ifname = NULL;\n"
          "   nrx_context ctx;\n");
   if (has_callback)
      printf("   int timeout = 0;\n"
             "   int event = 0;\n");
   for (i = 0; i < function->var_count; i++) {
      var = find_variable(&function->variables[i]);
      if (var->declaration != NULL) {
         printf("   ");
         printf(var->declaration, function->variables[i].name);
         printf("\n");
      }
   }
   if (has_input)
      printf("   int fwd;\n");
   if(strcmp(function->type, "void") == 0) {
      ret_void = 1;
   } else {
      printf("   %s ret;\n\n", function->type);
      if(strcmp(function->type, "nrx_callback_handle") == 0)
         ret_handler = 1;
   }

   printf("   // Options\n"
          "   while (1) {\n"
          "      if (argv[0] != NULL &&!strcmp(argv[0], \"--help\")) {\n");
   printf("         help_%s();\n", function->name);
   printf("         argv++;\n"
          "         exit(1);\n"
          "         continue;\n"
          "      }\n"
          "      if (argv[0] != NULL && !strcmp(argv[0], \"--ifname\")) {\n"
          "         ifname = argv[1];\n"
          "         ASSERT(ifname != NULL);\n"
          "         argv += 2;\n"
          "         continue;\n"
          "      }\n");
   if (has_callback)
      printf(
         "      if (argv[0] != NULL && !strcmp(argv[0], \"--timeout\")) {\n"
         "         ASSERT(argv[1] != NULL);\n"
         "         timeout = atoi(argv[1]);\n"
         "         argv += 2;\n"
         "         continue;\n"
         "      }\n");
   printf("      break;\n"
          "   }\n"
          "\n");

   printf("   // Do all initializations\n"
          "   if(nrx_init_context(&ctx, ifname) != 0)\n"
          "      FATAL(\"nrx_init_context failed.\\n\");\n");
   for (i = 0; i < function->var_count; i++) {
      var = find_variable(&function->variables[i]);
      if (var->initialization != NULL) {
         if (var->direction == INPUT) {
            printf("   if (*argv == NULL)\n"
                   "      FATAL(\"Too few inputs\\n\");\n");
            printf("   fwd = ");
         }
         else 
            printf("   ");
         printf(var->initialization, function->variables[i].name);
         printf("\n");
         if (var->direction == INPUT) {
            printf("   if (fwd < 0)\n");
            printf("      FATAL(\"Init of variable '%s %s' failed\\n\");\n", function->variables[i].type, function->variables[i].name);
            printf("   while(fwd-- > 0)\n"
                   "      argv++;\n");
         }
         if (has_len && var->len_and_size != NULL) {
            printf("   ");
            printf(var->len_and_size, function->variables[i].name);
            printf("\n");
         }
      }
   }
   printf("   if (*argv != NULL)\n"
          "      FATAL(\"Too many arguments\\n\");\n"
          "\n");

   printf("   // Function call.\n");
   if(!ret_void)
      printf("   ret=");
   printf("%s(", function->name);
   for (i = 0; i < function->var_count; i++) {
      if (i > 0)
         printf(", ");
      var = find_variable(&function->variables[i]);
      printf(var->call_str, function->variables[i].name);
   }
   printf(");\n");
   if(ret_void) {
   } else { 
      if(ret_handler) {
         printf("   if(ret == 0)\n");
      } else {
         printf("   if(ret != 0)\n");
      }
      printf("      printf(\"%s FAILED, exit code %%d, %%s.\\n\", ret, strerror(ret));\n", function->name);
      printf("   else {\n");

      // Print out results
      count=0;
      for (i = 0; i < function->var_count; i++) {
         var = find_variable(&function->variables[i]);
         if (var->direction == OUTPUT && var->printout != NULL) {
            char typename[1000];
            if (count++ == 0) {
               printf("      // Print out results.\n");
               printf("      printf(\"Output data:\");\n");
            }
            sprintf(typename, "%s %s = ", function->variables[i].type, function->variables[i].name);
            printf("      printf(\"\\n    %-25s\");", typename);
            printf(var->printout, function->variables[i].name);
            printf("\n");
         }
      }
      if (count != 0)
         printf("      printf(\"\\n\");\n");
      if (has_callback) {
         printf("\n"
                "      // Wait for event\n"
                "      while(timeout) {\n"                          // <-- if timeout == 0, we'll skip this part
                "         int err = nrx_wait_event(ctx, 1000);\n"
                "         if (!err) {\n"
                "            nrx_next_event(ctx);\n"
                "            if (event)\n {\n"
                "               printf(\"EVENT: OK\\n\");\n"
                "               break;\n"
                "         }  }\n"
                "         else if (err == EWOULDBLOCK) {\n"
                "            if (--timeout == 0) {\n"               // <-- allows timeout == -1 to be virtually forever
                "               printf(\"EVENT: Timeout\\n\");\n"
                "               ret = ETIMEDOUT;\n"
                "            }\n"
                "         }\n"
                "         else {\n"
                "            printf(\"EVENT: Failure\\n\");\n"
                "            ret = err;\n"
                "            break;\n"
                "         }\n"
                "      }\n");
      }

      printf("   } // on success\n");
   }
   printf("\n"
          "   // Clean up\n"
          "   nrx_free_context(ctx);\n"
          "\n"
          "   return ret;\n"
          "}\n\n");

   return 0;
}

int create_all_test_functions()
{
   function_list_t *element;
   int i;
   
   for (element = list; element != NULL; element = element->next) {
      printf("\n"
             "/****************************************************************\n"
             " *\n"
             " *   ");
      dump_function(&element->function);printf(";\n");
      printf(" *\n"
             " *****************************************************************/\n");
      create_brief_function(&element->function);
      create_help_function(&element->function);
      create_test_function(&element->function);
   }
   return 0;
}

int create_stub(const function_t *function)
{
   int i, count = 0;
   dump_function(function);
   printf("\n{\n");
   printf("   printf(\"Function %s NOT IMPLEMENTED\\n\");\n", function->name);
   printf("   printf(\"Input data:\");\n");
   for (i = 0; i < function->var_count; i++) {         // Print out all input variables.
      var_entry_t *var;
      var = find_variable(&function->variables[i]);
      if (var->direction == INPUT) {
         char typename[1000];
         count++;
         sprintf(typename, "%s %s = ", function->variables[i].type, function->variables[i].name);
         printf("   printf(\"\\n    %-25s\");", typename);
         printf(var->printout, function->variables[i].name);
         printf("\n");
      }
   }
   if (count == 0)
      printf("   printf(\"\\n    N/A\\n\");");
   else
      printf("   printf(\"\\n\");\n");
   printf("   exit(1);\n");
   printf("   return 1;\n");
   printf("}\n\n");

   return 0;
}

int create_all_stubs()
{
   function_list_t *element;
   int i;
   
   for (element = list; element != NULL; element = element->next) {
      create_stub(&element->function);
   }
   return 0;
}

/*******************************************/
/*                                         */
/*                   MAIN                  */
/*                                         */
/*******************************************/

int main(int argc, char *argv[])
{
   int err;

   err = configure(argc, argv);
   if (err) {
      printf("For info, use --help.\n");
      return 1;
   }

   if (settings.help) {
      show_help();
      return 1;
   }
   
   add_to_function_list(stdin);

   if (settings.dump_func)
      dump_all_functions();

   if (settings.dump_name)
      dump_all_func_names();

   if (settings.dump_vars)
      dump_vars();

   if (settings.call_switch)
      create_call_switch();

   if (settings.test_func)
      create_all_test_functions();

   if (settings.stubs)
      create_all_stubs();

   free_function_list();
   
   return 0;

}
