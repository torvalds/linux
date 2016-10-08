/*!
\file
\brief Набор данных FSM Аудиоустроство
\authors Гусенков.С.В
\version 0.0.1_rc1
\date 30.12.2015
*/
#ifndef FCM_AUDIODEVICECLASS_H
#define FCM_AUDIODEVICECLASS_H
/*!
\brief Вид устроства
*/
enum FSMAD_VidDevice {
    CommunicationDevice = 1 ///< Устроства связи
};
/*!
\brief ПодВид устроства
*/
enum FSMAD_PodVidDevice {
    CCK = 1, ///< СЦК
    ControlCCK = 2
};
/*!
\brief Род устроства
*/
enum FSMAD_RodDevice {
    MN921 = 1, ///< MN921
    MN825 = 2, ///< MN825
    PO07 = 3,  ///< PO07
    PO08 = 4,  ///< PO08
    PO06 = 5,  ///< PO06
    MN524 = 6, ///< MN524
    MN111 = 7, ///< MN111
    ControlCCKServer = 8
};
struct FSME1Buff
{
    unsigned short count;
    char Data[31][320];
};
struct FSM_E1Device
{
    char reg;
    unsigned short iddev;
    int idstream;
    struct fsm_ethernet_dev* ethdev;
    unsigned short streams_id[32];
    unsigned char bit_ch;
    unsigned long long e1_eror_ch;
    unsigned long long pkg_count;
    unsigned char cht;
    // struct FSME1Buff E1buffs;
};
struct fsm_po06_serversetting
{
    int dd;
};
struct fsm_po06_abonent
{
    unsigned short id;
    unsigned short idchannel;
};

struct fsm_po06_subscriber
{
    unsigned short idch1;
    unsigned short idch2;
    struct fsm_po06_abonent fsm_ab[2][12];
};
struct fsm_po07_subscriber
{
    unsigned short idch1;
    struct fsm_po06_abonent fsm_ab[2][12];
};
struct fsm_po08_subscriber
{
    unsigned short idch1;
    struct fsm_po06_abonent fsm_ab[2][12];
};
struct fsm_mn825_subscriber
{
    unsigned short idch1;
    unsigned short idch2;
};
struct fsm_mn921_subscriber
{
    unsigned short idch1;
    unsigned short idch2;
};

struct fsm_po06_setting
{
    struct fsm_po06_subscriber fsm_p006_su_s;
    // struct fsm_po06_serversetting fsm_p006_se_s;
};
struct fsm_po07_setting
{
    struct fsm_po07_subscriber fsm_p007_su_s;
    // struct fsm_po06_serversetting fsm_p006_se_s;
};
struct fsm_po08_setting
{
    struct fsm_po08_subscriber fsm_p008_su_s;
    // struct fsm_po06_serversetting fsm_p006_se_s;
};
struct fsm_mn825_setting
{
    struct fsm_mn825_subscriber fsm_mn825_su_s;
    // struct fsm_po06_serversetting fsm_p006_se_s;
};
struct fsm_mn921_setting
{
    struct fsm_mn921_subscriber fsm_mn921_su_s;
    // struct fsm_po06_serversetting fsm_p006_se_s;
};

struct FSM_PO06Device
{
    char reg;
    unsigned short iddev;
    int idstream;
    int idcon;
    struct fsm_ethernet_dev* ethdev;
    struct fsm_po06_setting po06set;
};
struct FSM_MN825Device
{
    char reg;
    unsigned short iddev;
    int idstream;
    int idcon;
    struct fsm_ethernet_dev* ethdev;
    struct fsm_mn825_setting mn825set;
};
struct FSM_MN921Device
{
    char reg;
    unsigned short iddev;
    int idstream;
    int idcon;
    struct fsm_ethernet_dev* ethdev;
    struct fsm_mn921_setting mn921set;
};
struct FSM_PO08Device
{
    char reg;
    unsigned short iddev;
    int idstream;
    int idcon;
    struct fsm_ethernet_dev* ethdev;
    struct fsm_po08_setting po08set;
};
struct FSM_PO07Device
{
    char reg;
    unsigned short iddev;
    int idstream;
    int idcon;
    struct fsm_ethernet_dev* ethdev;
    struct fsm_po07_setting po07set;
};

enum FSME1Command /*0*****125*/
{ FSME1SendStream = 1 };
enum FSMPO06Command /*0*****125*/
{ FSMPO06SendStream = 1,
  FSMPO06ConnectToDevE1 = 2,
  FSMPO06DisConnectToDevE1 = 3,
  SetSettingClientPo06 = 4,
  AnsSetSettingClientPo06 = 5,
  GetSettingClientPo06 = 6,
  AnsGetSettingClientPo06 = 7,
  FSMPo06AudioRun = 8,
  FSMPo06Reset = 10,
  FSMPo06Reregister = 11,
  FSMPo06GetCRC = 13,
  FSMPo06SendIP = 14,
};
enum FSMMN825Command /*0*****125*/
{ FSMMN825SendStream = 1,
  FSMMN825ConnectToDevE1 = 2,
  FSMMN825DisConnectToDevE1 = 3,
  SetSettingClientMN825 = 4,
  AnsSetSettingClientMN825 = 5,
  GetSettingClientMN825 = 6,
  AnsGetSettingClientMN825 = 7,
  FSMMN825AudioRun = 8,
  FSMMN825Reset = 10,
  FSMMN825Reregister = 11,
  FSMMN825SetTangenta = 12,
  FSMMN825GetCRC = 13,
  FSMMN825SendIP = 14,

};
enum FSMMN921Command /*0*****125*/
{ FSMMN921SendStream = 1,
  FSMMN921ConnectToDevE1 = 2,
  FSMMN921DisConnectToDevE1 = 3,
  SetSettingClientMN921 = 4,
  AnsSetSettingClientMN921 = 5,
  GetSettingClientMN921 = 6,
  AnsGetSettingClientMN921 = 7,
  FSMMN921AudioRun = 8,
  FSMMN921Reset = 10,
  FSMMN921Reregister = 11,
  FSMMN921GetCRC = 13,
  FSMMN921SendIP = 14,
};
enum FSMPO07Command /*0*****125*/
{ FSMPO07SendStream = 1,
  FSMPO07ConnectToDevE1 = 2,
  FSMPO07DisConnectToDevE1 = 3,
  SetSettingClientPO07 = 4,
  AnsSetSettingClientPO07 = 5,
  GetSettingClientPO07 = 6,
  AnsGetSettingClientPO07 = 7,
  FSMPo07AudioRun = 8,
  FSMPo07Reset = 10,
  FSMPo07Reregister = 11,
  FSMPo07GetCRC = 13,
  FSMPo07SendIP = 14,
};
enum FSMPO08Command /*0*****125*/
{ FSMPO08SendStream = 1,
  FSMPO08ConnectToDevE1 = 2,
  FSMPO08DisConnectToDevE1 = 3,
  SetSettingClientPO08 = 4,
  AnsSetSettingClientPO08 = 5,
  GetSettingClientPO08 = 6,
  AnsGetSettingClientPO08 = 7,
  FSMPo08AudioRun = 8,
  FSMPo08Reset = 10,
  FSMPo08Reregister = 11,
  FSMPo08GetCRC = 13,
  FSMPo08SendIP = 14,

};

struct FSME1Pkt
{
    char channels;
    char count;
    char Data[1024];
} __attribute__((__packed__));
void FSM_E1SendPacket(char* Data1, unsigned char len);

struct FSMPO06CommCons
{
    unsigned short id;
    unsigned short channel;
};

enum FSM_CCK_Alert {
    FSM_CCK_Server_Connect_Error = 0,
    FSM_CCK_Memory_Test_Filed = 1,
    FSM_MN111_Power_5V_Error = 2,
    FSM_MN111_Power_n5V_Error = 3,
    FSM_MN111_Power_n60V_Error = 4,
    FSM_MN111_Power_90V_Error = 5,
    FSM_MN111_Power_220V_Error = 6,
};

enum FSM_eventlist_CCK {
    FSM_CCK_MN845_Started = 0x04,
    FSM_CCK_MN921_Started = 0x05,
    FSM_CCK_MN111_Started = 0x06,
    FSM_CCK_PO08_Started = 0x07,
    FSM_CCK_PO07_Started = 0x08,
    FSM_CCK_PO06_Started = 0x09,
};

enum FSMMN111Command /*0*****125*/
{ SetSettingClientMN111 = 0,
  FSM_Get_CRC = 1,
  FSM_Get_MN111_Power_5V = 2,
  FSM_Get_MN111_Power_n5V = 3,
  FSM_Get_MN111_Power_n60V = 4,
  FSM_Get_MN111_Power_90V = 5,
  FSM_Get_MN111_Power_220V = 6,
  FSM_Ans_Get_MN111_Power_5V = 7,
  FSM_Ans_Get_MN111_Power_n5V = 8,
  FSM_Ans_Get_MN111_Power_n60V = 9,
  FSM_Ans_Get_MN111_Power_90V = 10,
  FSM_Ans_Get_MN111_Power_220V = 11,
  FSM_Read_MN111_Power_5V = 12,
  FSM_Read_MN111_Power_n5V = 13,
  FSM_Read_MN111_Power_n60V = 14,
  FSM_Read_MN111_Power_90V = 15,
  FSM_Read_MN111_Power_220V = 16,
  FSM_Read_MN111_AutoReqest = 17,
};

struct fsm_mn111_subscriber
{
    unsigned short idch1;
};
struct fsm_mn111_setting
{
    struct fsm_mn111_subscriber fsm_mn111_su_s;
    // struct fsm_po06_serversetting fsm_p006_se_s;
};

struct MN111OneVoltageState
{
    unsigned short value;
    char newdata;
};
struct MN111VoltageState
{
    struct MN111OneVoltageState MN111_Power_5V;
    struct MN111OneVoltageState MN111_Power_n5V;
    struct MN111OneVoltageState MN111_Power_n60V;
    struct MN111OneVoltageState MN111_Power_90V;
    struct MN111OneVoltageState MN111_Power_220V;
    unsigned short sel;
};
struct FSM_MN111Device
{
    char reg;
    unsigned short iddev;
    int idstream;
    int idcon;
    struct fsm_ethernet_dev* ethdev;
    struct fsm_mn111_setting mn111set;
    struct MN111VoltageState vst;
    struct FSM_DeviceTree* fsms;
};

struct CCKDeviceInfo
{
    unsigned char reg;
    unsigned char type;
    unsigned short id;
    unsigned char Position;
    unsigned char n;
    unsigned char ip[4]; 
    unsigned char dstlen;
    unsigned int crc32;
    unsigned short ramstate;
  
};

enum FSM_CCKControl_Cmd {
    FSM_CCKGetInfo,
};

void FSMCCK_AddDeviceInfo(struct CCKDeviceInfo* CCK);
void FSM_CCK_Get_Data(struct CCKDeviceInfo* CCKMass);
void FSM_CCK_MN111_Reqest_Voltage(enum FSMMN111Command fsmcmd, unsigned short IDDevice);
float FSM_CCK_MN111_Read_Voltage(enum FSMMN111Command fsmcmd, unsigned short IDDevice);
char FSM_CCK_MN111_Read_Voltage_State(enum FSMMN111Command fsmcmd,unsigned short IDDevice);
void FSM_CCK_MN825_SendCMD(enum FSMMN825Command fsmcmd, unsigned short IDDevice);
void FSM_CCK_MN825_SendCMD_Set(enum FSMMN825Command fsmcmd, unsigned short IDDevice);
void FSM_CCK_MN825_SendCMD_ReSet(enum FSMMN825Command fsmcmd, unsigned short IDDevice);
void FSM_CCK_MN921_SendCMD(enum FSMMN921Command fsmcmd, unsigned short IDDevice);
void FSM_CCK_MN921_SendCMD_Set(enum FSMMN921Command fsmcmd, unsigned short IDDevice);
void FSM_CCK_MN921_SendCMD_ReSet(enum FSMMN921Command fsmcmd, unsigned short IDDevice);
void FSM_CCK_PO07_SendCMD(enum FSMPO07Command fsmcmd, unsigned short IDDevice);
void FSM_CCK_PO07_SendCMD_Set(enum FSMPO07Command fsmcmd, unsigned short IDDevice);
void FSM_CCK_PO07_SendCMD_ReSet(enum FSMPO07Command fsmcmd, unsigned short IDDevice);
void FSM_CCK_PO08_SendCMD(enum FSMPO08Command fsmcmd, unsigned short IDDevice);
void FSM_CCK_PO08_SendCMD_Set(enum FSMPO08Command fsmcmd, unsigned short IDDevice);
void FSM_CCK_PO08_SendCMD_ReSet(enum FSMPO08Command fsmcmd, unsigned short IDDevice);
#endif /* FCM_AUDIODEVICECLASS_H */

