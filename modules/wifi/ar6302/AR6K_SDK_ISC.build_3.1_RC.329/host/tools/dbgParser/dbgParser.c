/*
 * Copyright (c) 2006 Atheros Communications Inc.
 * All rights reserved.
 * 
 *
 * 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
 * 
 */

/* This tool parses the recevent logs stored in the binary format 
   by the wince athsrc */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <asm/types.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/types.h>
#include <linux/if.h>
#include <linux/wireless.h>
#include <a_config.h>
#include <a_osapi.h>
#include <a_types.h>
#include <athdefs.h>
#include <ieee80211.h>
#include <wmi.h>
#include <athdrv_linux.h>
#include <dbglog_api.h>

#undef DEBUG
#undef DBGLOG_DEBUG

#define ID_LEN                         2
#define FILENAME_LENGTH_MAX            128
#define DBGLOG_FILE                    "dbglog.h"
#define DBGLOGID_FILE                  "dbglog_id.h"
#define DBGLOG_OUTPUT_FILE             "dbglog.out"

const A_CHAR *progname;
A_CHAR dbglogfile[FILENAME_LENGTH_MAX];
A_CHAR dbglogidfile[FILENAME_LENGTH_MAX];
A_CHAR dbglogoutfile[FILENAME_LENGTH_MAX];
A_CHAR dbgloginfile[FILENAME_LENGTH_MAX];
FILE *fpout;
FILE *fpin;

A_CHAR dbglog_id_tag[DBGLOG_MODULEID_NUM_MAX][DBGLOG_DBGID_NUM_MAX][DBGLOG_DBGID_DEFINITION_LEN_MAX];

#define AR6K_MAX_DBG_BUFFER_SIZE 1500

struct dbg_binary_record {
    A_UINT32 ts;
    A_UINT32 length;
    A_UINT8  log[AR6K_MAX_DBG_BUFFER_SIZE];
};

struct dbg_binary_header {
    A_UINT8     sig;
    A_UINT8     ver;
    A_UINT16    len;
    A_UINT32    reserved;
};


#ifdef DEBUG
A_INT32 debugRecEvent = 0;
#define RECEVENT_DEBUG_PRINTF(args...)        if (debugRecEvent) printf(args);
#else
#define RECEVENT_DEBUG_PRINTF(args...)
#endif

A_INT32
string_search(FILE *fp, A_CHAR *string)
{
    A_CHAR str[DBGLOG_DBGID_DEFINITION_LEN_MAX];

    rewind(fp);
    memset(str, 0, DBGLOG_DBGID_DEFINITION_LEN_MAX);
    while (!feof(fp)) {
        if (fgets(str, sizeof(str), fp)) {
            if (strstr(str, string)) return 1;
        }
    }

    return 0;
}

void
get_module_name(A_CHAR *string, A_CHAR *dest)
{
    A_CHAR *str1, *str2;
    A_CHAR str[DBGLOG_DBGID_DEFINITION_LEN_MAX];

    memset(str, 0, DBGLOG_DBGID_DEFINITION_LEN_MAX);
    strcpy(str, string);
    str1 = strtok(str, "_");
    while ((str2 = strtok(NULL, "_"))) {
        str1 = str2;
    }

    strcpy(dest, str1);
}

#ifdef DBGLOG_DEBUG
void
dbglog_print_id_tags(void)
{
    A_INT32 i, j;

    for (i = 0; i < DBGLOG_MODULEID_NUM_MAX; i++) {
        for (j = 0; j < DBGLOG_DBGID_NUM_MAX; j++) {
            printf("[%d][%d]: %s\n", i, j, dbglog_id_tag[i][j]);
        }
    }
}
#endif /* DBGLOG_DEBUG */

A_INT32
dbglog_generate_id_tags(void)
{
    A_INT32 id1, id2;
    FILE *fp1, *fp2;
    A_CHAR str1[DBGLOG_DBGID_DEFINITION_LEN_MAX];
    A_CHAR str2[DBGLOG_DBGID_DEFINITION_LEN_MAX];
    A_CHAR str3[DBGLOG_DBGID_DEFINITION_LEN_MAX];

    if (!(fp1 = fopen(dbglogfile, "r"))) {
        perror(dbglogfile);
        return -1;
    }

    if (!(fp2 = fopen(dbglogidfile, "r"))) {
        fclose(fp1);
        perror(dbglogidfile);
        return -1;
    }

    memset(dbglog_id_tag, 0, sizeof(dbglog_id_tag));
    if (string_search(fp1, "DBGLOG_MODULEID_START")) {
        int ret = fscanf(fp1, "%s %s %d", str1, str2, &id1);
        do {
            memset(str3, 0, DBGLOG_DBGID_DEFINITION_LEN_MAX);
            get_module_name(str2, str3);
            strcat(str3, "_DBGID_DEFINITION_START");
            if (string_search(fp2, str3)) {
                memset(str3, 0, DBGLOG_DBGID_DEFINITION_LEN_MAX);
                get_module_name(str2, str3);
                strcat(str3, "_DBGID_DEFINITION_END");
                ret = fscanf(fp2, "%s %s %d", str1, str2, &id2);
                while (!(strstr(str2, str3))) {
                    strcpy((A_CHAR *)&dbglog_id_tag[id1][id2], str2);
                    ret= fscanf(fp2, "%s %s %d", str1, str2, &id2);
                }
            }
            ret = fscanf(fp1, "%s %s %d", str1, str2, &id1);
        } while (!(strstr(str2, "DBGLOG_MODULEID_END")));
    }

    fclose(fp2);
    fclose(fp1);

    return 0;
}

static void
usage(void)
{
    fprintf(stderr, "usage:\n%s <input log file> <output file> \n", progname);
    exit(-1);
}

static A_INT32 
decode_debug_rec(struct dbg_binary_record *dbg_rec)
{
#define BUF_SIZE    120
    A_UINT32 count;
    A_UINT32 timestamp;
    A_UINT32 debugid;
    A_UINT32 numargs;
    A_UINT32 moduleid;
    A_INT32 *buffer;
    A_UINT32 length;
    A_CHAR buf[BUF_SIZE];
    A_UINT32 curpos;
    static A_INT32 numOfRec = 0;
    A_INT32 len;

#ifdef DBGLOG_DEBUG
    RECEVENT_DEBUG_PRINTF("Application received target debug event: %d\n", len);
#endif /* DBGLOG_DEBUG */
    count = 0;
    len = dbg_rec->length;
    length = (len >> 2);
    buffer = (A_INT32 *)dbg_rec->log;

    while (count < length) {
        debugid = DBGLOG_GET_DBGID(buffer[count]);
        moduleid = DBGLOG_GET_MODULEID(buffer[count]);
        numargs = DBGLOG_GET_NUMARGS(buffer[count]);
        timestamp = DBGLOG_GET_TIMESTAMP(buffer[count]);
        switch (numargs) {
            case 0:
            fprintf(fpout, "%8d: %s (%d)\n", dbg_rec->ts,
                    dbglog_id_tag[moduleid][debugid],
                    timestamp);
#ifdef DBGLOG_DEBUG
            RECEVENT_DEBUG_PRINTF("%s (%d)\n",
                                  dbglog_id_tag[moduleid][debugid], 
                                  timestamp);
#endif /* DBGLOG_DEBUG */
            break;

            case 1:
            fprintf(fpout, "%8d: %s (%d): 0x%x\n", dbg_rec->ts,
                    dbglog_id_tag[moduleid][debugid], 
                    timestamp, buffer[count+1]);
#ifdef DBGLOG_DEBUG
            RECEVENT_DEBUG_PRINTF("%s (%d): 0x%x\n", 
                                  dbglog_id_tag[moduleid][debugid], 
                                  timestamp, buffer[count+1]);
#endif /* DBGLOG_DEBUG */
            break;

            case 2:
            fprintf(fpout, "%8d: %s (%d): 0x%x, 0x%x\n", dbg_rec->ts,
                    dbglog_id_tag[moduleid][debugid], 
                    timestamp, buffer[count+1],
                    buffer[count+2]);
#ifdef DBGLOG_DEBUG
            RECEVENT_DEBUG_PRINTF("%s (%d): 0x%x, 0x%x\n",
                                  dbglog_id_tag[moduleid][debugid], 
                                  timestamp, buffer[count+1],
                                  buffer[count+2]);
#endif /* DBGLOG_DEBUG */
            break;

            default:
            RECEVENT_DEBUG_PRINTF("Invalid args: %d\n", numargs);
        }
        count += (numargs + 1);

        numOfRec++;
    }

    /* Update the last rec at the top of file */
    curpos = ftell(fpout);
    if( fgets(buf, BUF_SIZE, fpout) ) {
        buf[BUF_SIZE - 1] = 0;  /* In case string is longer from logs */
        length = strlen(buf);
        memset(buf, ' ', length-1);
        buf[length] = 0;
        fseek(fpout, curpos, SEEK_SET);
        fprintf(fpout, "%s", buf);
    }

    rewind(fpout);
    /* Update last record */
    fprintf(fpout, "%08d\n", numOfRec);
    fseek(fpout, curpos, SEEK_SET);
    fflush(fpout);

#undef BUF_SIZE
    return 0;
}

A_INT32 main(A_INT32 argc, A_CHAR** argv)
{
    A_CHAR *workarea;
    A_CHAR *platform;
    struct dbg_binary_record dbg_rec;
    A_UINT32 min_ts;
    A_INT32 min_rec_num;
    A_UINT32 rec_num;
    A_INT32 i;
    struct dbg_binary_header dbg_header;
    A_UINT16 dbg_rec_len;
    A_UINT16 dbg_header_len;

    progname = argv[0];
    if (argc != 3) {
        usage();
    }

    if ((workarea = getenv("WORKAREA")) == NULL) {
        printf("export WORKAREA\n");
        return -1;
    }

    if ((platform = getenv("ATH_PLATFORM")) == NULL) {
        printf("export ATH_PLATFORM\n");
        return -1;
    }

    /* Get the file name for dbglog header file */
    memset(dbglogfile, 0, FILENAME_LENGTH_MAX);
    strcpy(dbglogfile, workarea);
    strcat(dbglogfile, "/include/");
    strcat(dbglogfile, DBGLOG_FILE);

    /* Get the file name for dbglog id header file */
    memset(dbglogidfile, 0, FILENAME_LENGTH_MAX);
    strcpy(dbglogidfile, workarea);
    strcat(dbglogidfile, "/include/");
    strcat(dbglogidfile, DBGLOGID_FILE);

    /* Get the file name for dbglog input file */
    memset(dbgloginfile, 0, FILENAME_LENGTH_MAX);
    strcpy(dbgloginfile, argv[1]);
    if (!(fpin = fopen(dbgloginfile, "rb"))) {
        perror(dbgloginfile);
        return -1;
    }

    /* Get the file name for dbglog output file */
    memset(dbglogoutfile, 0, FILENAME_LENGTH_MAX);
    strcpy(dbglogoutfile, argv[2]);
    if (!(fpout = fopen(dbglogoutfile, "w+"))) {
        perror(dbglogoutfile);
        return -1;
    }


    /* first 8 bytes are to indicate the last record */
    fseek(fpout, 8, SEEK_SET);
    fprintf(fpout, "\n");

    dbglog_generate_id_tags();
#ifdef DBGLOG_DEBUG
    dbglog_print_id_tags();
#endif /* DBGLOG_DEBUG */

    /* first 8 bytes are header */  
    if (fread(&dbg_header, sizeof(struct dbg_binary_header), 1, fpin)!=1) {
        perror("dbg_header mismatch\n");
        return -1;
    }

    /* check header signature */
    dbg_rec_len = sizeof(A_UINT32) * 2;
    if (dbg_header.sig == 0xDB) {
        dbg_rec_len += dbg_header.len;
        dbg_header_len = 8;
    } else {
        /* header not present; assume max size */ 
        dbg_rec_len += AR6K_MAX_DBG_BUFFER_SIZE;
        dbg_header_len = 0;
    }

    /* go past header */
    fseek(fpin, dbg_header_len , SEEK_SET);

    min_ts = 0xFFFFFFFF;
    rec_num = 0;
    min_rec_num = 0;
    while (!feof(fpin)) {
        if (fread (&dbg_rec, dbg_rec_len, 1, fpin)) {
            if (dbg_rec.ts < min_ts) {
                min_ts = dbg_rec.ts;
                min_rec_num = rec_num;
            }
            rec_num++;
        }
    }

    /* go past header */
    fseek(fpin, dbg_header_len , SEEK_SET);
        
    // Goto the first min record
    fseek(fpin, min_rec_num * dbg_rec_len , SEEK_CUR);
    while (!feof(fpin)) {
        if (fread (&dbg_rec, dbg_rec_len, 1, fpin)) {
            decode_debug_rec(&dbg_rec);
        }
    }

    /* go past header */
    fseek(fpin, dbg_header_len , SEEK_SET);

    for (i=0;i<min_rec_num;i++) {
        if (fread (&dbg_rec, dbg_rec_len, 1, fpin)) {
            decode_debug_rec(&dbg_rec);
        } else {
            break;
        }
    }

    fclose(fpin);
    fclose(fpout);
    return 0;
}

