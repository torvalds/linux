/* Copyright (C) 2007 Nanoradio AB */
/* $Id: testprogram.in.c 9954 2008-09-15 09:41:38Z joda $ */

/********************************************************************************************
 *
 * The scripts included in this file are dependent of the following environmental variables
 *
 * NRX_API_PROTOCOL_HEADERS : Files with official APIs.
 *                            All functions in this file will be exported unless marked with NRX_API_EXCLUDE.
 * NRX_API_PRIVATE_HEADERS  : File with internal functions. 
 *                            No functions will be exported unless marked with NRX_API_FOR_TESTING.
 * NRX_API_TYPE_HEADERS     : Files with definition of types
 *                            Currently only #defines are extracted from this file, but eventually enums, typedefs, etc.
 * NRX_API_LIBRARY_FILE     : Compiled library/executable with functions (only one file supported)
 * NRX_API_OUTPUT_FILE      : Name of the output file (e.g. "nrxpriv")
 *
 *
 * Keywords that can be used in header files in association to functions.
 *
 * NRX_API_EXCLUDE          : Function will be excluded.
 *                            Applies to NRX_API_PROTOCOL_HEADERS.
 * NRX_API_FOR_TESTING      : Function will be included, but hidden from listing.
 *                            Applies to NRX_API_PROTOCOL_HEADERS and NRX_API_PRIVATE_HEADERS.
 * NRX_API_NOT_IMPLEMENTED  : Function will be listed in the "Functions not yet completed" section.
 * 
 * Should a function exist in the header file but not in the lib, a stub will be created and the function
 * will be listed in "Functions replaced with stub" section.
 *
 ********************************************************************************************/

// Autogenerate compiler errors when files do not exist
// NRX_API_INSERT_COMMAND=FILES="$NRX_API_PROTOCOL_HEADERS $NRX_API_TYPE_HEADERS $NRX_API_PRIVATE_HEADERS $NRX_API_LIBRARY_FILE";for f in $FILES; do if [ ! -f $f ];then echo "#error Could not find file $f"; fi ; done

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Autogenerate includes
// NRX_API_INSERT_COMMAND=FILES="$NRX_API_TYPE_HEADERS $NRX_API_PRIVATE_HEADERS $NRX_API_PROTOCOL_HEADERS";for f in $FILES; do echo "#include \"$f\"";  done


int nrx_show_scan(nrx_context ctx, char *nets, size_t len); /* no header file for this function */

/* Debugging stuff */
#define FATAL(args...) do { \
   fprintf(stderr, "FATAL[%s:%d]: ", __FILE__, __LINE__); \
   fprintf(stderr,  args); \
   return -1; \
}while(0)

#define ASSERT(x) do { \
   if (!(x)) \
      FATAL("Assertion failed on \"%s\".\n", #x); \
}while(0)

#define DEBUG(args...) printf(args)


/* Defines */
#define NULL_ON_ZERO_LEN(x) ((x->len)?(x):NULL) /* If len is 0, use NULL pointer instead */
#define __UNUSED__ __attribute__((__unused__)) 

static int __UNUSED__ init_char_p(char buf[])
{
   buf[0]='\0';
   return 0;
}

static int __UNUSED__ init_const_char_p(char *buf, const char *argv[])
{
   strcpy(buf, argv[0]);
   return 1;
}

static int __UNUSED__ init_int(int *num, const char *argv[])
{
   char *end;
   
   *num = strtol(argv[0], &end, 0);
   if(*end != '\0')
      FATAL("Not a numeric value (%s)\n", argv[0]);
   return 1;
}

static int __UNUSED__ init_int8_t(int8_t *i8, const char *argv[])
{
   int i32;
   
   init_int(&i32, argv);
   if (i32 > 0x7F)
      FATAL("Overflow: Number larger than 0x7F (%s)\n", *argv);
   if (i32 < -0x80)
      FATAL("Overflow: Number smaller than -0x80 (%s)\n", *argv);
      
   *i8 = i32;
   return 1;
}

static int __UNUSED__ init_uint32_t(uint32_t *u32, const char *argv[])
{
   char *end;
   
   *u32 = strtoul(argv[0], &end, 0);
   if(*end != '\0')
      FATAL("Not a numeric value (%s)\n", argv[0]);
   return 1;
}

static int __UNUSED__ init_uint16_t(uint16_t *u16, const char *argv[])
{
   uint32_t u32;
   init_uint32_t(&u32, argv);
   if (u32 > 0xFFFF)
      FATAL("Overflow: Number larger than 0xFFFF (%s)\n", *argv);
   *u16 = u32;
   return 1;
}

static int __UNUSED__ init_uint8_t(uint8_t *u8, const char *argv[])
{
   uint32_t u32;
   init_uint32_t(&u32, argv);
   if (u32 > 0xFF)
      FATAL("Overflow: Number larger than 0xFF (%s)\n", *argv);
   *u8 = u32;
   return 1;
}

void nrx_printbuf (const void *data, size_t len, const char *prefix); /* XXX */
int hexdump(const uint8_t *buf, size_t len)
{
   printf("\n");
   nrx_printbuf(buf, len, "  ");
   return 0;
}

//#define CHAR2NUM(x) ({if (x>='0' && x<='9') (int)(x-'0'); else FATAL("Non-numeric char.\n");})
#define CHAR2HEX(c)     ({                      \
   char x = c;                                  \
   int ret = -1;                                \
   if (x>='0' && x<='9')                        \
      ret = x-'0';                              \
   else if (x>='A' && x<='F')                   \
      ret = x-'A'+10;                           \
   else if (x>='a' && x<='f')                   \
      ret = x-'a'+10;                           \
   else                                         \
      FATAL("Not a hex char: '%c'.\n", x);      \
   ret;                                         \
})

static int __UNUSED__ init_const_uint8_t_p(uint8_t vect[], size_t *len, const char *argv[])
{
   *len = 0;
   if (argv[0][0] == '0' && argv[0][1] == 'x') {
      int i=0;
      const char *str = &argv[0][2];
      if (strlen(str) % 2) { // Odd n.o. chars
         vect[i] = CHAR2HEX(*str++);
         i++;
      }
      while (*str != '\0') {
         vect[i]  = CHAR2HEX(*str++) << 4;
         vect[i] += CHAR2HEX(*str++);
         i++;
      }
      *len = i;
   }
   else {
      FATAL("Only hex supported. Use C format: 0x... (%s)\n", *argv);
   }
   return 1;
}




/* Handle defines */
typedef struct {
   const char *name;
   int value;
}def_pair_t;


/* Parse out all defines from nrx_lib.h */
def_pair_t def_pair[] = {
// NRX_API_INSERT_COMMAND=for x in `cat $NRX_API_TYPE_HEADERS | grep \#define | grep NRX_ | sed s/\ \ \*/\\\\t/g | cut -f 2` ; do printf "\t{\"$x\", $x},\n" ; done
};


int defines_str_32(int *num, const char *argv[])
{
   int i;
   const char *or;
   const char *str = argv[0];

   *num = 0;
   or = strchr(str, '|');
   while (1) {
      if (or == NULL)
         or = str + strlen(str);
      if (str[0] >= '0' && str[0] <= '9') {  // Numeric
         int numtmp;
         const char *argvtmp[2] = {str, NULL};
         init_int(&numtmp, argvtmp);
         *num |= numtmp;
      }
      else {  // Some define str
         for (i = 0; i < sizeof(def_pair)/sizeof(*def_pair); i++)
            if (strlen(def_pair[i].name)==or-str && !strncmp(def_pair[i].name, str, or-str)) {
               *num |= def_pair[i].value;
               break;
            }
         if (i == sizeof(def_pair)/sizeof(*def_pair)) // Nothing found.
            FATAL("Unknown identifier: %s\n", str);
      }
      if (*or == '\0')
         break;
      str = or+1;
      or = strchr(str, '|');
   }

   return 1;
}

static int __UNUSED__ defines_str_16(int16_t *num, const char *argv[])
{
   int i, ret;
   ret = defines_str_32( &i, argv);
   *num = i;
   return ret;
}

static int __UNUSED__ defines_str_8(int8_t *num, const char *argv[])
{
   int i, ret;
   ret = defines_str_32( &i, argv);
   *num = i;
   return ret;
}

/*
static int __UNUSED__ init_nrx_adaptive_tx_rate_mode_t(int *num, const char *argv[])
{
   defines_str(num, argv[0]);
   return 1;
}

static int __UNUSED__ init_nrx_bss_type_t(int *num, const char *argv[])
{
   defines_str(num, argv[0]);
   return 1;
}

static int __UNUSED__ init_nrx_bss_type_t(int *num, const char *argv[])
{
   defines_str(num, argv[0]);
   return 1;
}

static int __UNUSED__ init_nrx_bt_tr_type_t(int *num, const char *argv[])
{
   defines_str(num, argv[0]);
   return 1;
}
*/

static int __UNUSED__ init_nrx_ch_list_t(nrx_ch_list_t *p, const char *argv[])
{
   int i, ret, count;
   if (!strcmp(*argv, "NULL")) {
      p->len = 0;
      return 1;
   }
   ret = init_int(&p->len, argv);
   count = ret;
   argv += ret;
   for (i = 0; i < p->len; i++) {
      if (*argv == NULL)
         FATAL("Need %d more inputs (for this variable alone)\n", p->len - i);
      ret = init_uint16_t(&p->channel[i], argv);
      count += ret;
      argv += ret;
   }
   return count;
}

static int __UNUSED__ print_nrx_ch_list_t(const nrx_ch_list_t *p)
{
   int i;

   printf("%d ", p->len);
   for (i = 0; i < p->len; i++) {
      printf("%d ", p->channel[i]);
   }
   return 0;
}

static int __UNUSED__ init_nrx_mac_addr_t(nrx_mac_addr_t *mac_addr, const char *argv[])
{
   int i;
   const char *str = *argv;

   for (i = 0; i < 6; i++) {
      mac_addr->octet[i] = (CHAR2HEX(*str++) << 4) + CHAR2HEX(*str++);
      if (*str == ':')
         str++;
   }

   return 1;
}

static int __UNUSED__ print_nrx_mac_addr_t(const nrx_mac_addr_t *mac_addr)
{
   printf("%02X:%02X:%02X:%02X:%02X:%02X",
          (uint8_t)mac_addr->octet[0],
          (uint8_t)mac_addr->octet[1],
          (uint8_t)mac_addr->octet[2],
          (uint8_t)mac_addr->octet[3],
          (uint8_t)mac_addr->octet[4],
          (uint8_t)mac_addr->octet[5]);
   return 0;
}

/*
static int __UNUSED__ init_const_nrx_mac_addr_list_t(nrx_mac_addr_list_t *mac_addr_list, const char *argv[])
{
   int i, ret;
   if (!strcmp(*argv, "NULL")) {
      mac_addr_list->len = 0;
      return 1;
   }
   mac_addr_list->len = atoi(*argv++);
   for (i = 0; i < mac_addr_list->len; i++) {
      if (*argv == NULL)
         FATAL("Need %d more inputs (for this variable alone)\n", mac_addr_list->len - i);
      if ((ret = init_nrx_mac_addr_t(&mac_addr_list->mac_addr[i], argv++)) < 0)
         return ret;
   }
   return 1 + i;
}
*/

static int __UNUSED__ print_const_nrx_mac_addr_list_t(const nrx_mac_addr_list_t *mac_addr_list)
{
   int i;
   printf("%d ", mac_addr_list->len);
   for (i = 0; i < mac_addr_list->len; i++)
      print_nrx_mac_addr_t(&mac_addr_list->mac_addr[i]);
   return 0;
}

static int __UNUSED__ init_in_addr_t(in_addr_t *ip, const char *argv[])
{
   *ip = inet_addr(argv[0]);
   return 1;
}

static int __UNUSED__ print_in_addr_t(const in_addr_t *ip)
{
   printf("%s", inet_ntoa(*(struct in_addr *)ip));
   return 0;
}

static int __UNUSED__ init_nrx_gpio_pin_t(nrx_gpio_pin_t *p, const char *argv[])
{
   p->gpio_pin = atoi(*argv++);
   if (*argv == NULL)
      FATAL("Only one entry, expect tuple.\n");
   p->active_high = atoi(*argv++);
   return 2;
}

static int __UNUSED__ print_nrx_gpio_pin_t(const nrx_gpio_pin_t *p)
{
   printf("%u %s", p->gpio_pin, p->active_high?"HIGH":"LOW");
   return 0;
}

static int __UNUSED__ init_nrx_gpio_list_t(nrx_gpio_list_t *p, const char *argv[])
{
   int i, ret;
   if (!strcmp(*argv, "NULL")) {
      p->len = 0;
      return 1;
   }
   p->len = atoi(*argv++);
   if (p->len < 0 || p->len > 5)
      FATAL("Length must be between 0-5\n");
   for (i = 0; i < p->len; i++, argv += 2) {
      if (*argv == NULL)
         FATAL("Need %d more inputs (for this variable alone)\n", 2*(p->len - i));
      if ((ret = init_nrx_gpio_pin_t(&p->pin[i], argv)) < 0)
          return ret;
   }
   return 1 + 2*i;
}

static int __UNUSED__ print_nrx_gpio_list_t(const nrx_gpio_list_t *p)
{
   int i;
   printf("%d ", p->len);
   for (i = 0; i < p->len; i++)
      print_nrx_gpio_pin_t(&p->pin[i]);
   return 0;
}

static int __UNUSED__ init_nrx_preamble_type_t(nrx_preamble_type_t *p, const char *argv[])
{
   if (!strcmp("NRX_SHORT_PREAMBLE", argv[0]))
      *p = NRX_SHORT_PREAMBLE;
   else if (!strcmp("NRX_LONG_PREAMBLE", argv[0]))
      *p = NRX_LONG_PREAMBLE;
   else
      FATAL("Unknown value: %s\n", argv[0]);   
   return 1;
}

static int __UNUSED__ print_nrx_preamble_type_t(const nrx_preamble_type_t *p)
{
   if (*p == NRX_SHORT_PREAMBLE)
      printf("NRX_SHORT_PREAMBLE");
   else if (*p == NRX_LONG_PREAMBLE)
      printf("NRX_LONG_PREAMBLE");
   else
      FATAL("Unknown value: %d\n", *p);   
   return 0;
}

static int __UNUSED__ init_nrx_rate_list_t(nrx_rate_list_t *p, const char *argv[])
{
   int i, ret, count;
   if (!strcmp(*argv, "NULL")) {
      p->len = 0;
      return 1;
   }
   ret = init_int(&p->len, argv);
   count = ret;
   argv += ret;
   for (i = 0; i < p->len; i++) {
      if (*argv == NULL)
         FATAL("Need %d more inputs (for this variable alone)\n", p->len - i);
      ret = init_uint8_t(&p->rates[i], argv);
      count += ret;
      argv += ret;
   }
   return count;
}

static int __UNUSED__ print_nrx_rate_list_t(const nrx_rate_list_t *p)
{
   int i;

   printf("%d ", p->len);
   for (i = 0; i < p->len; i++) {
      printf("%u ", p->rates[i]);
   }
   return 0;
}

static int __UNUSED__ init_nrx_region_code_t(nrx_region_code_t *p, const char *argv[])
{
   if (!strcmp("NRX_REGION_JAPAN", argv[0]))
      *p = NRX_REGION_JAPAN;
   else if (!strcmp("NRX_REGION_AMERICA", argv[0]))
      *p = NRX_REGION_AMERICA;
   else if (!strcmp("NRX_REGION_EMEA", argv[0]))
      *p = NRX_REGION_EMEA;
   else
      FATAL("Unknown value: %s\n", argv[0]);   
   return 1;
}

static int __UNUSED__ print_nrx_region_code_t(const nrx_region_code_t *p)
{
   if (*p == NRX_REGION_JAPAN)
      printf("NRX_REGION_JAPAN");
   else if (*p == NRX_REGION_AMERICA)
      printf("NRX_REGION_AMERICA");
   else if (*p == NRX_REGION_EMEA)
      printf("NRX_REGION_EMEA");
   else
      FATAL("Unknown value: %d\n", *p);   
   return 0;
}

static int __UNUSED__ init_nrx_scan_type_t(nrx_scan_type_t *p, const char *argv[])
{
   if (!strcmp("NRX_SCAN_ACTIVE", argv[0]))
      *p = NRX_SCAN_ACTIVE;
   else if (!strcmp("NRX_SCAN_PASSIVE", argv[0]))
      *p = NRX_SCAN_PASSIVE;
   else
      FATAL("Unknown value: %s\n", argv[0]);   
   return 1;
}

static int __UNUSED__ print_nrx_scan_type_t(const nrx_scan_type_t *p)
{
   if (*p == NRX_SCAN_ACTIVE)
      printf("NRX_SCAN_ACTIVE");
   else if (*p == NRX_SCAN_PASSIVE)
      printf("NRX_SCAN_PASSIVE");
   else
      FATAL("Unknown value: %d\n", *p);   
   return 0;
}

static int __UNUSED__ init_nrx_ssid_t(nrx_ssid_t *ssid, const char *argv[])
{
   if(strlen(argv[0]) > NRX_MAX_SSID_LEN - 1)
      return -1;

   if(strcmp(argv[0], "any") == 0) {
       ssid->ssid_len = 0;
   }
   else {
       init_const_char_p(ssid->ssid, argv);
       ssid->ssid_len = strlen(ssid->ssid);
   }
   return 1;
}

/*
static int __UNUSED__ init_nrx_antenna_t(nrx_antenna_t *p, const char *argv[])
{
   if (!strcmp("NRX_ANTENNA_1", argv[0]))
      *p = NRX_ANTENNA_1;
   else if (!strcmp("NRX_ANTENNA_2", argv[0]))
      *p = NRX_ANTENNA_2;
   else
      FATAL("Unknown value: %s\n", argv[0]);   
   return 1;
}

static int __UNUSED__ print_nrx_antenna_t(const nrx_antenna_t *p)
{
   if (*p == NRX_ANTENNA_1)
      printf("NRX_ANTENNA_1");
   else if (*p == NRX_ANTENNA_2)
      printf("NRX_ANTENNA_2");
   else
      FATAL("Unknown value: %d\n", *p);   
   return 0;
}
*/
/*
#define ELEMENTS(x)        (sizeof(x)/sizeof(*x))
#define SUBSTRLEN(x)  ({const char *p=x; while(*p != '\0' && *p != ',') p++; p-x;})
#define ENUMVECT(enum_type, members...)  \
   enum_type enumvect[] = {args}; \
   char *enumstr = {#args}; \
   ...
*/

static int __UNUSED__ init_nrx_sp_len_t(nrx_sp_len_t *p, const char *argv[])
{
   if (!strcmp("NRX_SP_LEN_ALL", argv[0]))
      *p = NRX_SP_LEN_ALL;
   else if (!strcmp("NRX_SP_LEN_2", argv[0]))
      *p = NRX_SP_LEN_2;
   else if (!strcmp("NRX_SP_LEN_4", argv[0]))
      *p = NRX_SP_LEN_4;
   else if (!strcmp("NRX_SP_LEN_6", argv[0]))
      *p = NRX_SP_LEN_6;
   else
      FATAL("Unknown value: %s\n", argv[0]);   
   return 1;
}

typedef struct {
   const char *str;
   int num;
} enum_list;
#define ENUM_LIST enum_list elist[]

#define CASEPRINT(x) case x: printf(#x); break
#define DEFAULT      default: FATAL("Unknown value: %d\n", *p);break

static int __UNUSED__ print_nrx_sp_len_t(const nrx_sp_len_t *p)
{
   switch(*p) {
      CASEPRINT(NRX_SP_LEN_ALL);
      CASEPRINT(NRX_SP_LEN_2);
      CASEPRINT(NRX_SP_LEN_4);
      CASEPRINT(NRX_SP_LEN_6);
      DEFAULT;
   }
   return 0;
}
/*
static int __UNUSED__ init_nrx_scan_dlv_pol_t(nrx_scan_dlv_pol_t *p, const char *argv[])
{
   if (!strcmp("NRX_SCAN_DLV_POL_FIRST", argv[0]))
      *p = NRX_SCAN_DLV_POL_FIRST;
   else if (!strcmp("NRX_SCAN_DLV_POL_BEST", argv[0]))
      *p = NRX_SCAN_DLV_POL_BEST;
   else
      FATAL("Unknown value: %s\n", argv[0]);
   return 1;
}

static int __UNUSED__ print_nrx_scan_dlv_pol_t(const nrx_scan_dlv_pol_t *p)
{
   switch(*p) {
      CASEPRINT(NRX_SCAN_DLV_POL_FIRST);
      CASEPRINT(NRX_SCAN_DLV_POL_BEST);
      default:
         FATAL("Unknown value: %d\n", *p);
   }
   return 0;
}
*/
static int __UNUSED__ init_nrx_scan_job_state_t(nrx_scan_job_state_t *p, const char *argv[])
{
   if (!strcmp("NRX_SCAN_JOB_STATE_SUSPENDED", argv[0]))
      *p = NRX_SCAN_JOB_STATE_SUSPENDED;
   else if (!strcmp("NRX_SCAN_JOB_STATE_RUNNING", argv[0]))
      *p = NRX_SCAN_JOB_STATE_RUNNING;
   else
      FATAL("Unknown value: %s\n", argv[0]);
   return 1;
}

static int __UNUSED__ print_nrx_scan_job_state_t(const nrx_scan_job_state_t *p)
{
   switch(*p) {
      CASEPRINT(NRX_SCAN_JOB_STATE_SUSPENDED);
      CASEPRINT(NRX_SCAN_JOB_STATE_RUNNING);
      DEFAULT;
   }
   return 0;
}

static int __UNUSED__ init_nrx_arp_policy_t(nrx_arp_policy_t *p, const char *argv[])
{
   if (!strcmp("NRX_ARP_HANDLE_MYIP_FORWARD_REST", argv[0]))
      *p = NRX_ARP_HANDLE_MYIP_FORWARD_REST;
   else if (!strcmp("NRX_ARP_HANDLE_MYIP_FORWARD_NONE", argv[0]))
      *p = NRX_ARP_HANDLE_MYIP_FORWARD_NONE;
   else if (!strcmp("NRX_ARP_HANDLE_NONE_FORWARD_MYIP", argv[0]))
      *p = NRX_ARP_HANDLE_NONE_FORWARD_MYIP;
   else if (!strcmp("NRX_ARP_HANDLE_NONE_FORWARD_ALL", argv[0]))
      *p = NRX_ARP_HANDLE_NONE_FORWARD_ALL;
   else
      FATAL("Unknown value: %s\n", argv[0]);
   return 1;
}

static int __UNUSED__ print_nrx_arp_policy_t(const nrx_arp_policy_t *p)
{
   switch(*p) {
      CASEPRINT(NRX_ARP_HANDLE_MYIP_FORWARD_REST);
      CASEPRINT(NRX_ARP_HANDLE_MYIP_FORWARD_NONE);
      CASEPRINT(NRX_ARP_HANDLE_NONE_FORWARD_MYIP);
      CASEPRINT(NRX_ARP_HANDLE_NONE_FORWARD_ALL);
      DEFAULT;
   }
   return 0;
}

static int __UNUSED__ init_nrx_retry_list_t(nrx_retry_list_t *p, const char *argv[])
{
   return init_nrx_rate_list_t((nrx_rate_list_t *)p, argv);
}

static int __UNUSED__ print_nrx_retry_list_t(const nrx_retry_list_t *p)
{
   return print_nrx_rate_list_t((const nrx_rate_list_t *)p);
}

static int __UNUSED__ init_nrx_bool(nrx_bool *p, const char *argv[])
{
   switch (**argv) {
      case '0': *p = 0; break;
      case '1': *p = 1; break;
      default: FATAL("Unknown bool, %c\n", **argv); break;
   }
   return 1;
}
static int __UNUSED__ print_nrx_bool(const nrx_bool *p)
{
   printf("%d", *p);
   return 0;
}


static int __UNUSED__ init_nrx_encryption_t(nrx_encryption_t *p,
                                            const char *argv[])
{
   if (!strcmp("NRX_ENCR_DISABLED", argv[0]))
      *p = NRX_ENCR_DISABLED;
   else if (!strcmp("NRX_ENCR_WEP", argv[0]))
      *p = NRX_ENCR_WEP;
   else if (!strcmp("NRX_ENCR_TKIP", argv[0]))
      *p = NRX_ENCR_TKIP;
   else if (!strcmp("NRX_ENCR_CCMP", argv[0]))
      *p = NRX_ENCR_CCMP;
   else
      FATAL("Unknown value: %s\n", argv[0]);
   return 1;
}

static int __UNUSED__ print_nrx_encryption_t(const nrx_encryption_t *p)
{
   switch(*p) {
      CASEPRINT(NRX_ENCR_DISABLED);
      CASEPRINT(NRX_ENCR_WEP);
      CASEPRINT(NRX_ENCR_TKIP);
      CASEPRINT(NRX_ENCR_CCMP);
      DEFAULT;
   }
   return 0;
}


static int __UNUSED__ init_nrx_authentication_t(nrx_authentication_t *p,
                                                const char *argv[])
{
   if (!strcmp("NRX_AUTH_OPEN", argv[0]))
      *p = NRX_AUTH_OPEN;
   else if (!strcmp("NRX_AUTH_SHARED", argv[0]))
      *p = NRX_AUTH_SHARED;
   else if (!strcmp("NRX_AUTH_8021X", argv[0]))
      *p = NRX_AUTH_8021X;
   else if (!strcmp("NRX_AUTH_AUTOSWITCH", argv[0]))
      *p = NRX_AUTH_AUTOSWITCH;
   else if (!strcmp("NRX_AUTH_WPA", argv[0]))
      *p = NRX_AUTH_WPA;
   else if (!strcmp("NRX_AUTH_WPA_PSK", argv[0]))
      *p = NRX_AUTH_WPA_PSK;
   else if (!strcmp("NRX_AUTH_WPA_NONE", argv[0]))
      *p = NRX_AUTH_WPA_NONE;
   else if (!strcmp("NRX_AUTH_WPA_2", argv[0]))
      *p = NRX_AUTH_WPA_2;
   else if (!strcmp("NRX_AUTH_WPA_2_PSK", argv[0]))
      *p = NRX_AUTH_WPA_2_PSK;
   else
      FATAL("Unknown value: %s\n", argv[0]);
   return 1;
}

static int __UNUSED__ print_nrx_authentication_t(const nrx_authentication_t *p)
{
   switch(*p) {
      CASEPRINT(NRX_ENCR_DISABLED);
      CASEPRINT(NRX_ENCR_WEP);
      CASEPRINT(NRX_ENCR_TKIP);
      CASEPRINT(NRX_ENCR_CCMP);
      DEFAULT;
   }
   return 0;
}

static int __UNUSED__ init_nrx_ssid_action_t(nrx_ssid_action_t *p,
                                             const char *argv[])
{
   if (!strcmp("NRX_SSID_ADD", argv[0]))
      *p = NRX_SSID_ADD;
   else if (!strcmp("NRX_SSID_REMOVE", argv[0]))
      *p = NRX_SSID_REMOVE;
   else 
      FATAL("Unknown value: %s\n", argv[0]);
   return 1;
}

static int __UNUSED__ print_nrx_ssid_action_t(const nrx_ssid_action_t *p)
{
   switch(*p) {
      CASEPRINT(NRX_SSID_ADD);
      CASEPRINT(NRX_SSID_REMOVE);
      DEFAULT;
   }
   return 0;
}


#undef FATAL
#define FATAL(args...) do { \
   fprintf(stderr, "FATAL[%s:%d]: ", __FILE__, __LINE__); \
   fprintf(stderr,  args); \
   exit(1); \
}while(0)


// Callback function
static int callback(nrx_context ctx, int operation, void *event_data, size_t event_data_size, void *user_data)
{
   printf("Callback operation: ");
   switch (operation) {
      case NRX_CB_TRIGGER: 
         printf("TRIGGERED\n"); 
         *(int *)user_data += 1;
         break;
      case NRX_CB_CANCEL:  
         printf("CANCELED\n");    
         break;
      default: 
         FATAL("Unknown option\n"); 
         break;
   }
   return 0;
}

// Handle debug info from NRXAPI
static int debug_callback(int prio, const char *file, int line, const char *message)
{
   char *prefix;
   switch (prio) {
      case NRX_PRIO_ERR: prefix = "ERROR"; break;
      case NRX_PRIO_WRN: prefix = "WARNING"; break;
      case NRX_PRIO_LOG: prefix = "DEBUG"; break;
      default: prefix = "???"; break;
   }
   fprintf(stderr, "%s[%s:%d]: %s\n", prefix, file, line, message);
   return 0;
}


// Autogenerate function stubs, parse for not implemented functions
// NRX_API_INSERT_COMMAND=for SHORT in `cat $NRX_API_PROTOCOL_HEADERS | ./header2test --func_name` ; do FULLNAME=`cat $NRX_API_PROTOCOL_HEADERS | ./header2test --functions | grep "int $SHORT("` ; nm $NRX_API_LIBRARY_FILE | cut -b 12- | grep -x "$SHORT" > /dev/null ; if [ $? -ne 0 ] ; then echo "$FULLNAME" ; fi ; done | ./header2test --stubs

// Autogenerate test of functions, parse for official functions (including not implemented ones) and hidden test functions
// NRX_API_INSERT_COMMAND=cat $NRX_API_PROTOCOL_HEADERS | sed -e '/./!d;:loop;$!{;N;/\n$/!b loop;};/NRX_API_EXCLUDE/d' | ./header2test --testfunc
// NRX_API_INSERT_COMMAND=cat $NRX_API_PRIVATE_HEADERS | sed -e '/./!d;:loop;$!{;N;/\n$/!b loop;};/NRX_API_FOR_TESTING/!d' | ./header2test --testfunc


typedef struct {
   const char *name;
   int (*func)(int argc, const char *argv[]);
   const char *brief;
} str_func_pair_t;

// Autogenerate name/func of all functions that are implemented (parse out test api and not implemented ones)
const str_func_pair_t implemented[] = {
   // NRX_API_INSERT_COMMAND=for SHORT in `cat $NRX_API_PROTOCOL_HEADERS | sed -e '/./!d;:loop;$!{;N;/\n$/!b loop;};/NRX_API_EXCLUDE/d' | sed -e '/^$/b;:loop;$!{;N;/\n$/!b loop;};/NRX_API_NOT_IMPLEMENTED/d' | ./header2test --func_name | grep -v nrx_init_context | grep -v nrx_nrxioctl | grep -v nrx_free_context` ; do nm $NRX_API_LIBRARY_FILE | cut -b 12- | grep -x "$SHORT" > /dev/null ; if [ $? -eq 0 ] ; then printf "\t{\"$SHORT\", test_$SHORT, brief_$SHORT},\n" ; fi ; done
};

// Autogenerate name/func for functions that are not fully implemented (parse for not implemented api)
const str_func_pair_t incomplete[] = {
   // NRX_API_INSERT_COMMAND=for SHORT in `cat $NRX_API_PROTOCOL_HEADERS | sed -e '/./!d;:loop;$!{;N;/\n$/!b loop;};/NRX_API_NOT_IMPLEMENTED/!d' | ./header2test --func_name` ; do nm $NRX_API_LIBRARY_FILE | cut -b 12- | grep -x "$SHORT" > /dev/null ; if [ $? -eq 0 ] ; then printf "\t{\"$SHORT\", test_$SHORT, brief_$SHORT},\n" ; fi ; done
};

// Autogenerate name/func for functions which have a stub
const str_func_pair_t stubs[] = {
   // NRX_API_INSERT_COMMAND=for SHORT in `cat $NRX_API_PROTOCOL_HEADERS | ./header2test --func_name` ; do nm $NRX_API_LIBRARY_FILE | cut -b 12- | grep -x "$SHORT" > /dev/null ; if [ $? -ne 0 ] ; then printf "\t{\"$SHORT\", test_$SHORT, brief_$SHORT},\n" ; fi ; done
};

// Autogenerate name/func for all functions used for testing only
const str_func_pair_t hidden[] = {
   // NRX_API_INSERT_COMMAND=for SHORT in `cat $NRX_API_PRIVATE_HEADERS | sed -e '/./!d;:loop;$!{;N;/\n$/!b loop;};/NRX_API_FOR_TESTING/!d' | ./header2test --func_name` ; do printf "\t{\"$SHORT\", test_$SHORT, brief_$SHORT},\n" ; done
};


extern const char *md5sum_this_file; // Init at end of this file.


int main(int argc, const char *argv[])
{
   int i, ret;
   const char *testobj = argv[0];

   // Generate output file name
   static char *output_fn =
   // NRX_API_INSERT_COMMAND=printf "\t\"$NRX_API_OUTPUT_FILE\";\n"
   
   for (i = strlen(testobj) - 1; i >= 0; i--)
      if (testobj[i] == '/') 
         break;
   testobj += i + 1;

   if (!strcmp(testobj, output_fn)) {
      testobj = argv[1];
      argv++;
      argc--;
   }

   argc--;
   argv++;

#define FIND_AND_EXECUTE(group)                                                 \
   for (i = 0; i < sizeof(group)/sizeof(*group); i++)                           \
      if (!strcmp(testobj, group[i].name)) {                                    \
         ret = group[i].func(argc, argv);                                       \
         if (ret != 0)                                                          \
            printf("Test failed.\n");                                           \
         return ret;                                                            \
      }

#define FIND_AND_PRINT(group, title)                                            \
   for (i = 0; i < sizeof(group)/sizeof(*group); i++) {                         \
      if (i == 0)                                                               \
         printf("\n" title ":\n");                                              \
      printf("      %-43s %s\n", group[i].name, group[i].brief);                \
   }
   
   if (testobj == NULL || !strcmp(testobj, "--help")) {
      if (argc == 1) {          /* want help with particular function */
         const char *dummy[2] = {"--help", NULL};
         testobj = argv[0];
         argv = dummy;
         FIND_AND_EXECUTE(implemented)
         FIND_AND_EXECUTE(incomplete)
         FIND_AND_EXECUTE(stubs)
         FIND_AND_EXECUTE(hidden)
         printf("Unknown function: %s\n", testobj);
         printf("Use --help only for list of functions.\n");
         return 1;
      }
      printf("Usage: %s [OPTIONS]\n", output_fn);
      printf("    --help ,    show this help text and exit.\n");
      printf("    --version , show version info and exit.\n\n");
      printf("Usage: %s <FUNCTION> [OPTIONS] <INPUT DATA>\n", output_fn);
      printf("To get help about a certain function, write: %s FUNCTION --help\n", output_fn);
      printf("This program supports soft links. A link is assumed to be named after the functions to be tested. E.g. the command line ./this_program some_function and ./some_function are equivalent as long as some_function is a link to this_program.\n");

      FIND_AND_PRINT(implemented, "Implemented functions")
      FIND_AND_PRINT(incomplete, "Functions not yet completed")
      FIND_AND_PRINT(stubs, "Functions replaced with stub")

      return 1;
   }

   else if (!strcmp(testobj, "--version")) { 
      char *version = "$Id: testprogram.in.c 9954 2008-09-15 09:41:38Z joda $";
      char *md5sums[] = {
         // Autogenerate md5sum for this file. 
         // NRX_API_INSERT_COMMAND=for f in $NRX_API_PROTOCOL_HEADERS  $NRX_API_PRIVATE_HEADERS $NRX_API_TYPE_HEADERS $NRX_API_LIBRARY_FILE; do if [ -f $f ]; then C="md5sum $f";X=`$C | awk '{ printf $1 }'`; printf "\t\"$X : $C\",\n" ; fi ; done
         // NRX_API_INSERT_COMMAND=for f in header2test.c replace_nanokey testprogram.in.c; do if [ -f $f ]; then C="md5sum $f";X=`$C | awk '{ printf $1 }'`; printf "\t\"$X : $C\",\n"; fi ; done
      } ;
      printf("Version:\n      %s\n\n", version);
      printf("Compile-time md5sums:\n");
      for (i = 0; i < sizeof(md5sums)/sizeof(*md5sums); i++)
         printf("      %s\n", md5sums[i]);
      if (md5sum_this_file)
         printf("      %s\n", md5sum_this_file); // Initiated last in this file
      return 1;
   }

   else {
      nrx_set_log_cb(debug_callback);

      FIND_AND_EXECUTE(implemented)
      FIND_AND_EXECUTE(incomplete)
      FIND_AND_EXECUTE(stubs)
      FIND_AND_EXECUTE(hidden)
   }
      
   printf("Unsupported function: %s\n", testobj);
   printf("Use --help for more information.\n");
   return 1;
   
}


// These lines should be as late as possible, as everything after it will be omitted!!!
const char *md5sum_this_file =
// NRX_API_INSERT_COMMAND=f=$NRX_API_OUTPUT_FILE.c; if [ -f $f ]; then L=`cat $f | wc -l`;X=`md5sum $f | awk '{ printf $1 }'`; printf "\t\"$X : head -n $L $f | md5sum\";\n"; else printf "\tNULL;"; fi
