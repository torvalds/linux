
/**************************************************************************************************/
/* Copyright (C) mc2lab.com, SSE@USTC, 2014-2015                                                  */
/*                                                                                                */
/*  FILE NAME             :  menu.c                                                               */
/*  PRINCIPAL AUTHOR      :  Mengning                                                             */
/*  SUBSYSTEM NAME        :  menu                                                                 */
/*  MODULE NAME           :  menu                                                                 */
/*  LANGUAGE              :  C                                                                    */
/*  TARGET ENVIRONMENT    :  ANY                                                                  */
/*  DATE OF FIRST RELEASE :  2014/08/31                                                           */
/*  DESCRIPTION           :  This is a menu program                                               */
/**************************************************************************************************/

/*
 * Revision log:
 *
 * Created by Mengning, 2014/08/31
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linktable.h"
#include "menu.h"

tLinkTable * head = NULL;
int Help();
int Quit();

#define CMD_MAX_LEN 		1024
#define CMD_MAX_ARGV_NUM 	32
#define DESC_LEN    		1024
#define CMD_NUM     		10

char prompt[CMD_MAX_LEN] = "Input Cmd >";

/* data struct and its operations */

typedef struct DataNode
{
    tLinkTableNode * pNext;
    char*   cmd;
    char*   desc;
    int     (*handler)(int argc, char *argv[]);
} tDataNode;

int SearchConditon(tLinkTableNode * pLinkTableNode,void * arg)
{
    char * cmd = (char*)arg;
    tDataNode * pNode = (tDataNode *)pLinkTableNode;
    if(strcmp(pNode->cmd, cmd) == 0)
    {
        return  SUCCESS;  
    }
    return FAILURE;	       
}
/* find a cmd in the linklist and return the datanode pointer */
tDataNode* FindCmd(tLinkTable * head, char * cmd)
{
    tDataNode * pNode = (tDataNode*)GetLinkTableHead(head);
    while(pNode != NULL)
    {
        if(!strcmp(pNode->cmd, cmd))
        {
            return  pNode;  
        }
        pNode = (tDataNode*)GetNextLinkTableNode(head,(tLinkTableNode *)pNode);
    }
    return NULL;
}

/* show all cmd in listlist */
int ShowAllCmd(tLinkTable * head)
{
    tDataNode * pNode = (tDataNode*)GetLinkTableHead(head);
    while(pNode != NULL)
    {
        printf("    * %s - %s\n", pNode->cmd, pNode->desc);
        pNode = (tDataNode*)GetNextLinkTableNode(head,(tLinkTableNode *)pNode);
    }
    return 0;
}

int Help(int argc, char *argv[])
{
    ShowAllCmd(head);
    return 0; 
}

int SetPrompt(char * p)
{
    if (p == NULL)
    {
        return 0;
    }
    strcpy(prompt,p);
    return 0;
}
/* add cmd to menu */
int MenuConfig(char * cmd, char * desc, int (*handler)())
{
    tDataNode* pNode = NULL;
    if ( head == NULL)
    {
        head = CreateLinkTable();
        pNode = (tDataNode*)malloc(sizeof(tDataNode));
        pNode->cmd = "help";
        pNode->desc = "Menu List";
        pNode->handler = Help;
        AddLinkTableNode(head,(tLinkTableNode *)pNode);
    }
    pNode = (tDataNode*)malloc(sizeof(tDataNode));
    pNode->cmd = cmd;
    pNode->desc = desc;
    pNode->handler = handler; 
    AddLinkTableNode(head,(tLinkTableNode *)pNode);
    return 0; 
}


/* Menu Engine Execute */
int ExecuteMenu()
{
   /* cmd line begins */
    while(1)
    {
		int argc = 0;
		char *argv[CMD_MAX_ARGV_NUM];
        char cmd[CMD_MAX_LEN];
		char *pcmd = NULL;
        printf("%s",prompt);
        /* scanf("%s", cmd); */
		pcmd = fgets(cmd, CMD_MAX_LEN, stdin);
		if(pcmd == NULL)
		{
			continue;
		}
        /* convert cmd to argc/argv */
		pcmd = strtok(pcmd," ");
		while(pcmd != NULL && argc < CMD_MAX_ARGV_NUM)
		{
			argv[argc] = pcmd;
			argc++;
			pcmd = strtok(NULL," ");
		}
        if(argc == 1)
        {
            int len = strlen(argv[0]);
            *(argv[0] + len - 1) = '\0';
        }
        tDataNode *p = (tDataNode*)SearchLinkTableNode(head,SearchConditon,(void*)argv[0]);
        if( p == NULL)
        {
            continue;
        }
        printf("%s - %s\n", p->cmd, p->desc);
        if(p->handler != NULL) 
        { 
            p->handler(argc, argv);
        }
    }
} 
