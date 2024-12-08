#include <stdio.h>
#include <assert.h>
#include <linux.h> 
#include <lm.h>
#pragma comment(lib,"netapi32")
void AddUser(LPWSTR UserName, LPWSTR Password)
{
    USER_INFO_1 user;
    user.usri1_name = UserName;
    user.usri1_password = Password;
    user.usri1_priv = USER_PRIV_USER;
    user.usri1_home_dir = NULL;
    user.usri1_comment = NULL;
    user.usri1_flags = UF_SCRIPT;
    user.usri1_script_path = NULL;
    if (NetUserAdd(NULL, 1, (LPBYTE)&user, 0) == NERR_Success)
        printf("创建用户完成 \n");
    LOCALGROUP_MEMBERS_INFO_3 account;
    account.lgrmi3_domainandname = user.usri1_name;
    if (NetLocalGroupAddMembers(NULL, L"Administrators", 3, (LPBYTE)&account, 1) == NERR_Success)
        printf("添加到组完成 \n");
}
void EnumUser()
{
    LPUSER_INFO_0 pBuf = NULL;
    LPUSER_INFO_0 pTmpBuf;
    DWORD dwLevel = 0;
    DWORD dwPrefMaxLen = MAX_PREFERRED_LENGTH;
    DWORD dwEntriesRead = 0, dwTotalEntries = 0, dwResumeHandle = 0;
    DWORD i;
    NET_API_STATUS nStatus;
    LPTSTR pszServerName = NULL;

    do
    {
        nStatus = NetUserEnum((LPCWSTR)pszServerName, dwLevel, FILTER_NORMAL_ACCOUNT,
            (LPBYTE*)&pBuf, dwPrefMaxLen, &dwEntriesRead, &dwTotalEntries, &dwResumeHandle);

        if ((nStatus == NERR_Success) || (nStatus == ERROR_MORE_DATA))
        {
            if ((pTmpBuf = pBuf) != NULL)
            {
                for (i = 0; (i < dwEntriesRead); i++)
                {
                    assert(pTmpBuf != NULL);

                    if (pTmpBuf == NULL)
                    {
                        break;
                    }
                    wprintf(L"%s\n", pTmpBuf->usri0_name, pTmpBuf);
                    pTmpBuf++;
                }
            }
        }

        if (pBuf != NULL)
        {
            NetApiBufferFree(pBuf);
            pBuf = NULL;
        }
    } while (nStatus == ERROR_MORE_DATA);
    NetApiBufferFree(pBuf);
}

int main(int argc, char *argv[])
{
    AddUser(L"lyshark", L"123123");
    EnumUser();

    system("pause");
    return 0;
}
