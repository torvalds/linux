#ifndef FSM_STATUSSTRUCT
#define FSM_STATUSSTRUCT
#define srow_cnt 8
#define scolumn_cnt 12
#define status_cnt srow_cnt* scolumn_cnt
struct fsm_status_element
{
    int row;
    int column;
    char state[32];
    char fsmdevcode[32];
    short devid;
};
struct fsm_device_config
{
    int row;
    int column;
    unsigned short IDDevice;
    unsigned short Len;
    char config[1000];
};
struct FSMSSetconfig
{
    unsigned char cmd;
    unsigned short IDDevice;
    struct fsm_device_config config;
};
enum FSMSSetconfigCmd { SetFSMSetting = 1, GetFSMSetting = 2 };
struct FSMSSetconfigParam
{
    unsigned char cmd;
    unsigned short IDDevice;
    struct fsm_device_config* config;
};

struct fsm_devices_config
{
    struct fsm_device_config setel[srow_cnt][scolumn_cnt];
};
struct fsm_statusstruct
{
    struct fsm_status_element statel[srow_cnt][scolumn_cnt];
};
struct fsm_ClientSetting_Setting
{
    unsigned short id;
    unsigned short size_row;
    unsigned short size_column;
};
struct fsm_ServerSetting_Setting
{
    unsigned short size_row;
    unsigned short size_column;
};
struct fsm_Setting_Setting
{
    struct fsm_ClientSetting_Setting fsmcs;
    struct fsm_ServerSetting_Setting fsmss;
};

enum FSMST_VidDevice {
    FSMDeviceConfig = 1,    ///<  Модуль Конфигурации
    FSMDeviceStatistic = 2, ///<  Модуль Конфигурации
};
/*!
\brief ПодВид устроства
*/
enum FSMST_PodVidDevice {
    ComputerStatistic = 1, ///< ComputerStatistic
    FSM_SettingTree_D = 2,
    FSM_Flash=3
};
/*!
\brief Род устроства
*/
enum FSMST_RodDevice {
    PCx86 = 1, ///< PCx86
    CTL_FSM_SettingTree_D = 2,
    CTL_FSM_Flash = 3
};
enum FSMST_Cmd /*0*****125*/
{ GetStatistic = 1,
  AnsGetStatistic = 2 };
enum FSMS_Cmd /*0*****125*/
{ GetSet = 1,
  AnsGetSet = 2,
  SetSetting = 3,
  AnsSetSetting = 4,
  SetSettingClient = 5,
  AnsSetSettingClient = 6,
  SendSettingFull = 7 };

enum FSMCDPC_VidDevice {
    Computer = 1, ///<  Модуль Конфигурации
    Device = 2,
};
/*!
\brief ПодВид устроства
*/
enum FSMCDPC_PodVidDevice {
    PC = 1, ///< ComputerStatistic
    GPIO = 2
};
/*!
\brief Род устроства
*/
enum FSMCDPC_RodDevice {
    ARM = 1, ///< PCx86
    Bit_8 = 2
};

enum FSMIOCTLStat_Cmd { FSMIOCTLStat_Read, FSMIOCTLStat_Requst };
enum FSM_eventlist_status {

    /**Statistick Event List 0x40 - 0x6F **/
    FSM_ControlDeviceRun = 0x40,
    FSM_StaticServerRun = 0x41,
    FSM_SettingServerRun = 0x42,

};
enum FSM_Property_time {
    FSMP_INT,
    FSMP_STRING,
};

typedef struct FSM_PropertyDevice FSM_PropertyDevice_t;

typedef void (*UpdateDataProperty)(FSM_PropertyDevice_t*);

struct FSM_PropertyDevice
{
    char fsmdevcode[32];
    unsigned short devid;
    char PropertyCode[32];
    void* Property;
    unsigned short pr_size;
    UpdateDataProperty udp;
};
//  struct FSM_PropertyDevice pdl[FSM_PropertyTreeSize];

enum FSM_GPIO_Bit_Enum {
    FSM_GPIO_Bit_0 = 1,
    FSM_GPIO_Bit_1 = 2,
    FSM_GPIO_Bit_2 = 4,
    FSM_GPIO_Bit_3 = 8,
    FSM_GPIO_Bit_4 = 16,
    FSM_GPIO_Bit_5 = 32,
    FSM_GPIO_Bit_6 = 64,
    FSM_GPIO_Bit_7 = 128,
};

enum FSM_GPIO_Bit_Cmd {
    FSM_ON_Bit,
    FSM_Eror_ON_Bit,
    FSM_OFF_Bit,
    FSM_Eror_OFF_Bit,
    FSM_Reset_Bit,
    FSM_Event_Bit,
};

void FSM_GPIO_SetBit(enum FSM_GPIO_Bit_Enum Pin);

void FSM_GPIO_ReSetBit(enum FSM_GPIO_Bit_Enum Pin);

void FSM_GPIO_Set_Input(enum FSM_GPIO_Bit_Enum Pin);

void FSM_GPIO_Set_Output(enum FSM_GPIO_Bit_Enum Pin);

unsigned char FSM_GPIO_Get_Status(enum FSM_GPIO_Bit_Enum Pin);

void FSM_GPIO_Reset_timer_callback(unsigned long data);

void FSM_GPIO_Impulse_timer_callback(unsigned long data);

void FSM_GPIO_Reset(void);

void FSM_GPIO_EventEror(void);

void FSM_GPIO_Ctl_Reset(void);
void FSM_GPIO_Ctl_Eror(void);
void FSM_GPIO_Ctl_SetBit(enum FSM_GPIO_Bit_Enum Pin);
void FSM_GPIO_Ctl_ReSetBit(enum FSM_GPIO_Bit_Enum Pin);
void FSM_GPIO_Ctl_Error_ON(void);
void FSM_GPIO_Ctl_Error_OFF(void);

enum FSM_Set_type {
    FSM_T_CHAR = 0,
    FSM_T_INT = 1,
    FSM_T_STRING = 3,
};
struct FSM_SetTreeElement
{
    char* id;
    void* object;
    char type;
    char len;
};

struct FSM_SetTreeElementFS
{
    short type;
    unsigned char iid;
    unsigned char len;
    char id[20];
};

struct FSM_SetTreeGetList
{
    unsigned short IDDevice;
    unsigned short iid;
};

struct FSM_SetTreeGetListCount
{
    unsigned char IDDevice;
    unsigned char count;
};

struct SettingTreeInfo
{
    unsigned char reg;
    unsigned char type;
    unsigned short id;
    struct FSM_SetTreeElementFS fsmdtl[FSM_SettingTreeSize];
    char fsm_tr_temp[254];
    char fsm_tr_size;
    struct FSM_DeviceTree* dt;
};
struct FSM_SetTreeWriteElement
{
    unsigned short id; 
    unsigned short len;
    unsigned char name[20];
    unsigned char Data[100];
   
};

struct FSM_SetTreeReadElement
{
    unsigned short id; 
    unsigned char name[20];
   
};

void FSM_TreeRecive(char* data, short len, struct FSM_DeviceTree* to_dt);
void FSM_SetTreeAdd(struct FSM_DeviceTree* to_dt);
void FSM_SendReuestDevTree(struct FSM_DeviceTree* to_dt);

void FSM_Get_Setting_List_Count(unsigned short IDDevice,struct FSM_SetTreeGetListCount* SetMass);
void FSM_Get_Setting_List_Item(struct FSM_SetTreeGetList* ReadMass,struct FSM_SetTreeElementFS* SetItem);
void FSM_Set_Item(struct FSM_SetTreeWriteElement* in);
int FSM_Get_Item_Read(unsigned short IDDevice,char* Data);
void FSM_Get_Item_Rq(struct FSM_SetTreeReadElement* in);

enum FSM_DevTreeSetControl_Cmd {
    FSM_DevTreeSetGet,
    FSM_DevTreeSetGetCount,
    FSM_DevTreeSetWrite,
    FSM_DevTreeSetReadReqest,
    FSM_DevTreeSetReadRead
};

struct FSMFlahData_StartVector
{
	unsigned int size;
	unsigned int count;
	unsigned int crc32;
};
struct FSMFlahData_DataVector
{
  unsigned int num;
	unsigned int crc32;
	char Data[1024];
};
struct FSMFlahData_DataVerifeVector
{
	unsigned int num;
	unsigned int crc32;
};
struct FSMFlahData_EndVector
{
	unsigned int size;
	unsigned int crc32;
};

struct FSMFirmware
{
	struct FSMFlahData_StartVector svec;
	struct FSMFlahData_DataVector dvec[128];
	struct FSMFlahData_EndVector evec;
};

enum FSM_Flash_Status
{
    FSM_Flash_S_Start,
    FSM_Flash_S_Data,
    FSM_Flash_S_End
};

enum FSM_Flash_CTL
{
    FSM_Flash_CTL_Flash,
    FSM_Flash_CTL_GetStatus,
};

struct FSMFlash_Control
{
char reg;
char state;
struct FSMFirmware firm;
char size;
char count;
struct FSM_DeviceTree* dt;

};
unsigned int FSM_crc32NT(unsigned int crc, unsigned char *buf,unsigned int len);

void FSM_FlashStart(struct FSM_DeviceTree* to_dt);
void FSM_FlashRecive(char* data, short len, struct FSM_DeviceTree* to_dt);

void FSM_CTL_flash_Start(unsigned short IDDevice);
#endif // FSM_STATUSSTRUCT