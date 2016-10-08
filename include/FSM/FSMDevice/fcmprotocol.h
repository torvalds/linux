/*!
\file
\brief Протокол FSM
\authors Гусенков.С.В
\version 0.0.1_rc1
\date 30.12.2015
*/
#ifndef FCMPROTOCOL
#define FCMPROTOCOL
#define FSMCountDATASlot 375
#define FSMCountDATA FSMCountDATASlot * 4

/*!
\brief Коды операци
*/
enum FSM_CodeOperation {
    RegDevice = 1,          ///< Регистрация устройства
    AnsRegDevice = 2,       ///< Подтверждение регистрации
    DelLisr = 3,            ///< Удаление устройства из списка
    AnsDelList = 4,         ///< Подтверждение удаления устройства из списка
    AnsPing = 5,            ///< Пинг
    SendCmdToDevice = 6,    ///< Отправка команды устройству
    AnsSendCmdToDevice = 7, ///< Подтверждение приёма команды устройством
    RqToDevice = 8,         ///< Ответ на команду устройством
    AnsRqToDevice = 9,      ///< Подтверждение приёма команды сервером
    SendCmdToServer = 10,   ///< Отправка команды серверу
    SendTxtMassage = 11,    ///< Отправка текстового сообщения
    AnsSendTxtMassage = 12, ///< Подтверждение приёма текстового сообщения
    SendTxtEncMassage = 13, ///< Отправка зашифрованного текстового сообщения
    AnsSendTxtEncMassage = 14, ///< Подтверждение приёма зашифрованного текстового сообщения
    SendAudio = 15,            ///< Передача аудио данных
    SendVideo = 16,            ///< Передача видео данных
    SendBinData = 17,          ///< Передача бинарных данных
    AnsSendBinData = 18,       ///< Подтверждение приёма бинарных данных
    SendSMS = 19,              ///< Отправить СМС
    SendAnsSMS = 20,           ///< Подтверждение СМС
    SendSMStoDev = 21,         ///< Передача СМС устройству
    SendAnsSMStoDev = 22,      ///< Подтверждение СМС устройством
    SendEncSMS = 23,           ///< Отправить зашифрованного СМС
    SendAnsEncSMS = 24,        ///<Подтверждение зашифрованного СМС
    SendEncSMStoDev = 25,      ///< Отправить зашифрованного СМС устройству
    SendAnsEncSMStoDev = 26,   ///< Подтверждение зашифрованного СМС  устройства
    SendEmail = 27,            ///< Отправка email
    AnsEmail = 28,             ///<Подтверждение email
    SendEmailtoDevice = 29,    ///<Передача email устройству
    AnsSendEmailtoDevice = 30, ///<Подтверждение email устройством
    SendEncEmail = 31,         ///<Отправить зашифрованного email
    AnsEncEmail = 32,          ///<Подтверждение зашифрованного email
    SendEncEmailtoDev = 33,    ///< Отправить зашифрованного email устройству
    AnsEncEmailtoDev = 34,     ///< Подтверждение зашифрованного email   устройства
    SocSend = 35,              ///< Отправка сообщение в социальную сеть
    AnsSocSend = 36,           ///< Подтверждение сообщения в социальную сеть
    SocSendtoDev = 37, ///< Передача сообщения в социальную сеть устройству
    AnsSocSendtoDev = 38, ///< Подтверждение   сообщения в социальную сеть устройством
    SocEncSend = 39, ///< Отправить зашифрованного сообщения в социальную сеть
    AnsSocEncSend = 40, ///< Подтверждение зашифрованного сообщения в социальную сеть
    SocEncSendtoDev = 41,          ///<	Отправить зашифрованного сообщения в социальную сеть устройству
    AnsSocEncSendtoDev = 42,       ///<	Подтверждение зашифрованного сообщения в социальную сеть   устройства
    Alern = 43,                    ///<Тревога
    Warning = 44,                  ///<Предупреждение
    Trouble = 45,                  ///<Сбой
    Beep = 46,                     ///<Звуковой сигнал
    PacketFromUserSpace = 47,      ///<Пакет из пространства пользователя
    PacketToUserSpace = 48,        ///<Пакет в пространство пользователя
    PacketToDevice = 49,           ///<Пакет в пространство пользователя
    SysEvent = 50,                 ///<Системное событие
    SendCmdGlobalcmdToClient = 51, ///<Гллбальная команда клиенту
    SendCmdGlobalcmdToServer = 52, ///<Гллбальная команда серверу
    SendCmdToServerStream = 53,    ///<Команда в поток (Сервер)
    AnsSendCmdToServerStream = 54, ///<Ответ на команду в потоке (Сервер)
    SendCmdToClientStream = 55,    ///<Команда в поток (Клиент)
    AnsSendCmdToClientStream = 56, ///<Ответ на команду в потоке (Клиент)
    FSMPing = 57,                  ///Пинг
    FSM_Setting_Read = 58,         ///<Считать настройки
    Ans_FSM_Setting_Read = 59,     ///<Ответ с настроками
    FSM_Setting_Write = 60,        ///<Записать настроки
    Ans_FSM_Setting_Write = 61,    ///<Отчет о выполнение настроек
    FSM_Setting_GetTree = 62,      ///<Список настроек
    Ans_FSM_Setting_GetTree = 63,  ///<Список настроек
};
/*!
\brief Тип устройства
*/
enum FSM_TypeDevice {
    AvtoElSheet = 1,        ///<Автоматически Электрощиток
    MindTepl = 2,           ///<Умная Теплица
    SmartPhone = 3,         ///< Смартфон
    AudioDevice = 4,        ///< Устройство аудио связи
    Network = 5,            ///< Сеть
    StatisticandConfig = 6, ///< Модуль статистики и конфигурации
    Switch = 7,
    ControlMachine = 8,
    SocialAnalytica = 9
};
/*!
\brief Регистрация устроства
*/
#define FSMH_Header_Size_DeviceRegistr 8
struct FSM_DeviceRegistr
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned char type;         ///< Тип устройства
    unsigned char VidDevice;    ///< Вид устройства
    unsigned char PodVidDevice; ///< Подвид устройства
    unsigned char KodDevice;    ///<Код устройства

} __attribute__((aligned(4)));
/*!
\brief Подтверждение регистрации
*/
#define FSMH_Header_Size_AnsDeviceRegistr 8
struct FSM_AnsDeviceRegistr
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned char type;         ///< Тип устройства
    unsigned char VidDevice;    ///< Вид устройства
    unsigned char PodVidDevice; ///< Подвид устройства
    unsigned char KodDevice;    ///<Код устройства

} __attribute__((aligned(4)));
/*!
\brief Удаление устройства из списка
*/
#define FSMH_Header_Size_FSM_DeviceDelete 4
struct FSM_DeviceDelete
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства
} __attribute__((aligned(4)));
/*!
\brief Подтверждение удаления устройства из списка
*/
#define FSMH_Header_Size_AnsDeviceDelete 4
struct FSM_AnsDeviceDelete
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

} __attribute__((aligned(4)));
/*!
\brief Пинг
*/
#define FSMH_Header_Size_Ping 4
struct FSM_Ping
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства
} __attribute__((aligned(4)));
/*!
\brief Отправка команды устройству
*/
#define FSMH_Header_Size_SendCmd 8
struct FSM_SendCmd_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short cmd;        ///< Команда
    unsigned short countparam; ///< Количество параметров

} __attribute__((aligned(4)));
struct FSM_SendCmd
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short cmd;        ///< Команда
    unsigned short countparam; ///< Количество параметров

    unsigned char Data[FSMCountDATA]; ///< Параметры
} __attribute__((aligned(4)));
/*!
\brief  Подтверждение приёма команды устройством
*/
#define FSMH_Header_Size_AnsSendCmd 8
struct FSM_AnsSendCmd
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short cmd;        ///< Команда
    unsigned short countparam; ///< Количество параметров
} __attribute__((aligned(4)));
/*!
\brief Ответ на команду устройством
*/
#define FSMH_Header_Size_AnsCmd 8
struct FSM_AnsCmd
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short cmd;        ///< Команда
    unsigned short countparam; ///< Количество параметров

    unsigned char Data[FSMCountDATA]; ///< Параметры
} __attribute__((aligned(4)));
/*!
\brief  Подтверждение приёма команды сервером
*/
#define FSMH_Header_Size_AnsAnsCmd 8
struct FSM_AnsAnsCmd
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short cmd;        ///< Команда
    unsigned short countparam; ///< Количество параметров
} __attribute__((aligned(4)));

/*!
\brief Отправка команды серверу
*/
#define FSMH_Header_Size_SendCmdTS 8
struct FSM_SendCmdTS_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short cmd;        ///< Команда
    unsigned short countparam; ///< Количество параметров

} __attribute__((aligned(4)));

struct FSM_SendCmdTS
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short cmd;        ///< Команда
    unsigned short countparam; ///< Количество параметров

    unsigned char Data[FSMCountDATA]; ///< Параметры
} __attribute__((aligned(4)));
/*!
\brief Отправка текстового сообщения
*/
#define FSMH_Header_Size_SendMessage 8
struct FSM_SendMessage_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short len;    ///< Длина
    unsigned char lang[2]; ///< Язык

} __attribute__((aligned(4)));

struct FSM_SendMessage
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short len;    ///< Длина
    unsigned char lang[2]; ///< Язык

    unsigned char Data[FSMCountDATA]; ///< Текст
} __attribute__((aligned(4)));
/*!
\brief Подтверждение приёма текстового сообщения
*/
#define FSMH_Header_Size_AnsSendMessage 8
struct FSM_AnsSendMessage
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short len;    ///< Длина
    unsigned char lang[2]; ///< Язык

} __attribute__((aligned(4)));
/*!
\brief Отправка зашифрованного текстового сообщения
*/
#define FSMH_Header_Size_SendEncMessage 8
struct FSM_SendEncMessage_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short alg; ///< Алгоритм
    unsigned short pin; ///< Алгоритм

    unsigned short len;    ///< Длина
    unsigned char lang[2]; ///< Язык

} __attribute__((aligned(4)));

struct FSM_SendEncMessage
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short alg; ///< Алгоритм
    unsigned short pin; ///< Алгоритм

    unsigned short len;    ///< Длина
    unsigned char lang[2]; ///< Язык

    unsigned char Data[FSMCountDATA]; ///< Текст
} __attribute__((aligned(4)));
/*!
\brief Подтверждение приёма зашифрованного текстового сообщения
*/
#define FSMH_Header_Size_AnsSendEncMessage 8
struct FSM_AnsSendEncMessage
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short alg; ///< Алгоритм
    unsigned short len; ///< Длина

} __attribute__((aligned(4)));
/*!
\brief Передача аудио данных
*/
#define FSMH_Header_Size_SendAudioData 8
struct FSM_SendAudioData_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short codec; ///< Кодек
    unsigned short len;   ///< Длина

} __attribute__((aligned(4)));

struct FSM_SendAudioData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short codec; ///< Кодек
    unsigned short len;   ///< Длина

    unsigned char Data[FSMCountDATA]; ///< Аудио
} __attribute__((aligned(4)));
/*!
\brief Передача видео данных
*/
#define FSMH_Header_Size_SendVideoData 8
struct FSM_SendVideoData_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short codec; ///< Кодек
    unsigned short len;   ///< Длина
} __attribute__((aligned(4)));

struct FSM_SendVideoData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short codec; ///< Кодек
    unsigned short len;   ///< Длина

    unsigned char Data[FSMCountDATA]; ///< Видео
} __attribute__((aligned(4)));
/*!
\brief Передача бинарных данных
*/
#define FSMH_Header_Size_SendBinData 8
struct FSM_SendBinData_Header
{
    unsigned char opcode;    ///< Код операцииu
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short len;      ///< Длина
    unsigned short datatype; ///< Тип данных

} __attribute__((aligned(4)));

struct FSM_SendBinData
{
    unsigned char opcode;    ///< Код операцииu
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short len;      ///< Длина
    unsigned short datatype; ///< Тип данных

    unsigned char Data[FSMCountDATA]; ///< Данные
} __attribute__((aligned(4)));
/*!
\brief Подтверждение приёма бинарных данных
*/
#define FSMH_Header_Size_AnsSendBinData 8
struct FSM_AnsSendBinData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short len;      ///< Длина
    unsigned short datatype; ///< Тип данных

} __attribute__((aligned(4)));
/*!
\brief Отправить СМС
*/
#define FSMH_Header_Size_SendSmsData 24
struct FSM_SendSmsData_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lennumber; ///< Длина номера телефона
    unsigned short len;       ///< Длина

    unsigned char number[16]; ///< Номера телефона

} __attribute__((aligned(4)));

struct FSM_SendSmsData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lennumber; ///< Длина номера телефона
    unsigned short len;       ///< Длина

    unsigned char number[16]; ///< Номера телефона

    unsigned char Data[FSMCountDATA]; ///< Текст
} __attribute__((aligned(4)));
/*!
\brief Подтверждение СМС
*/
#define FSMH_Header_Size_ansSendSmsData 24
struct FSM_ansSendSmsData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short len;
    unsigned short lennumber; ///< Длина номера телефона

    unsigned char number[16]; ///< Номера телефона
} __attribute__((aligned(4)));
/*!
\brief Передача СМС устройству
*/
#define FSMH_Header_Size_SendSmsDatatoDev 24
struct FSM_SendSmsDatatoDev_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lennumber; ///< Длина номера телефона
    unsigned short len;       ///< Длина

    unsigned char number[16]; ///< Номера телефона

} __attribute__((aligned(4)));

struct FSM_SendSmsDatatoDev
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lennumber; ///< Длина номера телефона
    unsigned short len;       ///< Длина

    unsigned char number[16]; ///< Номера телефона

    unsigned char Data[FSMCountDATA]; ///< Текст
} __attribute__((aligned(4)));
/*!
\brief Подтверждение СМС устройством
*/
#define FSMH_Header_Size_ansSendSmsDatatoDev 24
struct FSM_ansSendSmsDatatoDev
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lennumber; ///< Длина номера телефона
    unsigned short len;       ///< Длина

    unsigned char number[16]; ///< Номера телефона

} __attribute__((aligned(4)));
/*!
\brief Отправить зашифрованного СМС
*/
#define FSMH_Header_Size_SendEncSmsData 28
struct FSM_SendEncSmsData_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lennumber; ///< Длина номера телефона
    unsigned short len;       ///< Длина

    unsigned short alg; ///<Алгоритм
    unsigned short pin; ///<Пин код

    unsigned char number[16]; ///< Номера телефона

} __attribute__((aligned(4)));

struct FSM_SendEncSmsData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lennumber; ///< Длина номера телефона
    unsigned short len;       ///< Длина

    unsigned short alg; ///<Алгоритм
    unsigned short pin; ///<Пин код

    unsigned char number[16]; ///< Номера телефона

    unsigned char Data[FSMCountDATA]; ///< Текст
} __attribute__((aligned(4)));
/*!
\brief Подтверждение зашифрованного СМС
*/
#define FSMH_Header_Size_ansSendEncSmsData 24
struct FSM_ansSendEncSmsData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lennumber; ///< Длина номера телефона
    unsigned short alg;       ///<Алгоритм

    unsigned char number[16]; ///< Номера телефона

} __attribute__((aligned(4)));
/*!
\brief Отправить зашифрованного СМС устройству
*/
#define FSMH_Header_Size_SendEncSmsDatatoDev 28
struct FSM_SendEncSmsDatatoDev_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lennumber; ///< Длина номера телефона
    unsigned short len;       ///< Длина

    unsigned short alg; ///<Алгоритм
    unsigned short pin; ///<Пин код

    unsigned char number[16]; ///< Номера телефона

} __attribute__((aligned(4)));

struct FSM_SendEncSmsDatatoDev
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lennumber; ///< Длина номера телефона
    unsigned short len;       ///< Длина

    unsigned short alg; ///<Алгоритм
    unsigned short pin; ///<Пин код

    unsigned char number[16]; ///< Номера телефона

    unsigned char Data[FSMCountDATA]; ///< Текст
} __attribute__((aligned(4)));
/*!
\brief Подтверждение зашифрованного СМС  устройства
*/
#define FSMH_Header_Size_ansSendEncSmsDatatoDev 24
struct FSM_ansSendEncSmsDatatoDev
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lennumber; ///< Длина номера телефона
    unsigned short alg;       ///<Алгоритм

    unsigned char number[16]; ///< Номера телефона

} __attribute__((aligned(4)));
/*!
\brief Отправка email
*/
#define FSMH_Header_Size_SendEmailData 40
struct FSM_SendEmailData_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenemail; ///< Длина email
    unsigned short len;      ///< Длина

    unsigned char email[32]; ///< email

} __attribute__((aligned(4)));

struct FSM_SendEmailData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenemail; ///< Длина email
    unsigned short len;      ///< Длина

    unsigned char email[32]; ///< email

    unsigned char Data[FSMCountDATA]; ///< Текст
} __attribute__((aligned(4)));
/*!
\brief Подтверждение email
*/
#define FSMH_Header_Size_AnsSendEmailData 40
struct FSM_AnsSendEmailData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenemail; ///< Длина email
    unsigned short len;      ///< Длина

    unsigned char email[32]; ///< email

} __attribute__((aligned(4)));
/*!
\brief Передача email устройству
*/
#define FSMH_Header_Size_SendEmailDatatoDev 40
struct FSM_SendEmailDatatoDev_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenemail; ///< Длина email
    unsigned short len;      ///< Длина

    unsigned char email[32]; ///< email

} __attribute__((aligned(4)));

struct FSM_SendEmailDatatoDev
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenemail; ///< Длина email
    unsigned short len;      ///< Длина

    unsigned char email[32]; ///< email

    unsigned char Data[FSMCountDATA]; ///< Текст
} __attribute__((aligned(4)));
/*!
\brief Подтверждение email устройством
*/
#define FSMH_Header_Size_AnsSendEmailDatatoDev 40
struct FSM_AnsSendEmailDatatoDev
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenemail; ///< Длина email
    unsigned short len;      ///< Длина

    unsigned char email[32]; ///< email

} __attribute__((aligned(4)));
/*!
\brief Отправить зашифрованного email
*/
#define FSMH_Header_Size_SendEncEmailData 44

struct FSM_SendEncEmailData_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenemail; ///< Длина email
    unsigned short len;      ///< Длина

    unsigned short alg; ///<Алгоритм
    unsigned short pin; ///<Пин код

    unsigned char email[32]; ///< email

} __attribute__((aligned(4)));

struct FSM_SendEncEmailData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenemail; ///< Длина email
    unsigned short len;      ///< Длина

    unsigned short alg; ///<Алгоритм
    unsigned short pin; ///<Пин код

    unsigned char email[32]; ///< email

    unsigned char Data[FSMCountDATA]; ///< Текст
} __attribute__((aligned(4)));
/*!
\brief Подтверждение зашифрованного email
*/
#define FSMH_Header_Size_AnsSendEncEmailData 40
struct FSM_AnsSendEncEmailData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenemail; ///< Длина email
    unsigned short alg;      ///<Алгоритм

    unsigned char email[32]; ///< email

} __attribute__((aligned(4)));
/*!
\brief Отправить зашифрованного email устройству
*/
#define FSMH_Header_Size_SendEncEmailDatatoDev 44
struct FSM_SendEncEmailDatatoDev_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenemail; ///< Длина email
    unsigned short len;      ///< Длина

    unsigned short alg; ///<Алгоритм
    unsigned short pin; ///<Пин код

    unsigned char email[32]; ///< email

} __attribute__((aligned(4)));

struct FSM_SendEncEmailDatatoDev
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenemail; ///< Длина email
    unsigned short len;      ///< Длина

    unsigned short alg; ///<Алгоритм
    unsigned short pin; ///<Пин код

    unsigned char email[32]; ///< email

    unsigned char Data[FSMCountDATA]; ///< Текст
} __attribute__((aligned(4)));
/*!
\brief Подтверждение зашифрованного email   устройства
*/
#define FSMH_Header_Size_AnsSendEncEmailDatatoDev 40
struct FSM_AnsSendEncEmailDatatoDev
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenemail; ///< Длина email
    unsigned short alg;      ///<Алгоритм

    unsigned char email[32]; ///< email

} __attribute__((aligned(4)));
/*!
\brief Отправка сообщение в социальную сеть
*/
#define FSMH_Header_Size_SendSocData 44
struct FSM_SendSocData_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenlogin; ///< Длина login
    unsigned short len;      ///< Длина

    unsigned short sctype;  ///<Тип социально сети
    unsigned short num_zap; ///<Номер записи

    unsigned char login[32]; ///< login

} __attribute__((aligned(4)));

struct FSM_SendSocData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenlogin; ///< Длина login
    unsigned short len;      ///< Длина

    unsigned short sctype;  ///<Тип социально сети
    unsigned short num_zap; ///<Номер записи

    unsigned char login[32]; ///< login

    unsigned char Data[FSMCountDATA]; ///< Текст
} __attribute__((aligned(4)));
/*!
\brief Подтверждение сообщения в социальную сеть
*/
#define FSMH_Header_Size_AnsSendSocData 40
struct FSM_AnsSendSocData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenlogin; ///< Длина login
    unsigned short sctype;   ///<Тип социально сети

    unsigned char login[32]; ///< login

} __attribute__((aligned(4)));
/*!
\brief Передача   сообщения в социальную сеть устройству
*/
#define FSMH_Header_Size_SendSocDatatoDev 44

struct FSM_SendSocDatatoDev_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenlogin; ///< Длина login
    unsigned short len;      ///< Длина

    unsigned short sctype;  ///<Тип социально сети
    unsigned short num_zap; ///<Номер записи

    unsigned char login[32]; ///< login

} __attribute__((aligned(4)));

struct FSM_SendSocDatatoDev
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenlogin; ///< Длина login
    unsigned short len;      ///< Длина

    unsigned short sctype;  ///<Тип социально сети
    unsigned short num_zap; ///<Номер записи

    unsigned char login[32]; ///< login

    unsigned char Data[FSMCountDATA]; ///< Текст
} __attribute__((aligned(4)));
/*!
\brief Подтверждение  сообщения в социальную сеть устройством
*/
#define FSMH_Header_Size_AnsSendSocDatatoDev 40
struct FSM_AnsSendSocDatatoDev
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenlogin; ///< Длина login
    unsigned short sctype;   ///<Тип социально сети

    unsigned char login[32]; ///< login

} __attribute__((aligned(4)));
/*!
\brief Отправить зашифрованного сообщения в социальную сеть
*/
#define FSMH_Header_Size_SendEncSocData 48
struct FSM_SendEncSocData_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenlogin; ///< Длина login
    unsigned short len;      ///< Длина

    unsigned short alg; ///<Алгоритм
    unsigned short pin; ///<Пин код

    unsigned short sctype;  ///<Тип социально сети
    unsigned short num_zap; ///<Номер записи

    unsigned char login[32]; ///< login

} __attribute__((aligned(4)));

struct FSM_SendEncSocData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenlogin; ///< Длина login
    unsigned short len;      ///< Длина

    unsigned short alg; ///<Алгоритм
    unsigned short pin; ///<Пин код

    unsigned short sctype;  ///<Тип социально сети
    unsigned short num_zap; ///<Номер записи

    unsigned char login[32]; ///< login

    unsigned char Data[FSMCountDATA]; ///< Текст
} __attribute__((aligned(4)));
/*!
\brief Подтверждение зашифрованного сообщения в социальную сеть
*/
#define FSMH_Header_Size_AnsSendEncSocData 40
struct FSM_AnsSendEncSocData
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenlogin; ///< Длина login
    unsigned short sctype;   ///<Тип социально сети

    unsigned char login[32]; ///< login

} __attribute__((aligned(4)));
/*!
\brief Отправить зашифрованного сообщения в социальную сеть устройству
*/
#define FSMH_Header_Size_SendEncSocDatatoDev 48
struct FSM_SendEncSocDatatoDev_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenlogin; ///< Длина login
    unsigned short len;      ///< Длина

    unsigned short alg; ///<Алгоритм
    unsigned short pin; ///<Пин код

    unsigned short sctype;  ///<Тип социально сети
    unsigned short num_zap; ///<Номер записи

    unsigned char login[32]; ///< login

} __attribute__((aligned(4)));

struct FSM_SendEncSocDatatoDev
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenlogin; ///< Длина login
    unsigned short len;      ///< Длина

    unsigned short alg; ///<Алгоритм
    unsigned short pin; ///<Пин код

    unsigned short sctype;  ///<Тип социально сети
    unsigned short num_zap; ///<Номер записи

    unsigned char login[32]; ///< login

    unsigned char Data[FSMCountDATA]; ///< Текст
} __attribute__((aligned(4)));
/*!
\brief Подтверждение зашифрованного сообщения в социальную сеть   устройства
*/
#define FSMH_Header_Size_AnsSendEncSocDatatoDev 40
struct FSM_AnsSendEncSocDatatoDev
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short lenlogin; ///< Длина login
    unsigned short sctype;   ///<Тип социально сети

    unsigned char login[32]; ///< login

} __attribute__((aligned(4)));
/*!
\brief Тревога
*/
#define FSMH_Header_Size_AlernSignal 8
struct FSM_AlernSignal
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned int ID; ///< Ид Тревоги

} __attribute__((aligned(4)));
/*!
\brief Предупреждение
*/
#define FSMH_Header_Size_WarningSignal 8
struct FSM_WarningSignal
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned int ID; ///< Ид Предупреждения

} __attribute__((aligned(4)));
/*!
\brief Сбой
*/
#define FSMH_Header_Size_TroubleSignal 8
struct FSM_TroubleSignal
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned int ID; ///< Ид Предупреждения

} __attribute__((aligned(4)));
/*!
\brief Звуковой сигнал
*/
#define FSMH_Header_Size_BeepSignal 8
struct FSM_BeepSignal
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CR
    unsigned short IDDevice; ///< Ид устройства

    unsigned short ID; ///< Ид Звукового сигнала
} __attribute__((aligned(4)));

struct FSM_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

} __attribute__((aligned(4)));

/*!
\brief Связь с пользовательским процессом
*/

#define FSMH_Header_Size_SendCmdUserspace 8

struct FSM_SendCmdUserspace
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short cmd;        ///< Команда
    unsigned short countparam; ///< Количество параметров

    unsigned char Data[FSMCountDATA]; ///< Параметры
} __attribute__((aligned(4)));

struct FSM_SendCmdUserspace_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned short cmd;        ///< Команда
    unsigned short countparam; ///< Количество параметров
} __attribute__((aligned(4)));

/*!
\brief Событие
*/

#define FSMH_Header_Size_EventSignal 8

struct FSM_EventSignal
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned int ID; ///< Ид Предупреждения
} __attribute__((aligned(4)));

/*!
\brief Получение дерева настроек
*/

#define FSMH_Header_Size_GetTreeList 4

struct FSM_GetTreeList_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства
} __attribute__((aligned(4)));

struct FSM_GetTreeList
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства
} __attribute__((aligned(4)));

#define FSMH_Header_Size_Ans_GetTreeList 8

struct FSM_AnsGetTreeList_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned int size;
} __attribute__((aligned(4)));

struct FSM_AnsGetTreeList
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned int size;

    unsigned char Data[FSMCountDATA];
} __attribute__((aligned(4)));

/*!
\brief Получения настройки
*/

#define FSMH_Header_Size_GetSetting 4

struct FSM_GetSetting_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned char name[20];

} __attribute__((aligned(4)));

struct FSM_GetSetting
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned char name[20];
} __attribute__((aligned(4)));

#define FSMH_Header_Size_Ans_GetSetting 4

struct FSM_AnsGetSetting_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства
    
    unsigned int size;
    
    unsigned char name[20];
} __attribute__((aligned(4)));

struct FSM_AnsGetSetting
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned int size;
    
    unsigned char name[20];
    unsigned char Data[FSMCountDATA];
} __attribute__((aligned(4)));

/*!
\brief Запись настройки
*/

#define FSMH_Header_Size_SetSetting 4

struct FSM_SetSetting_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned char name[20];
} __attribute__((aligned(4)));

struct FSM_SetSetting
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    unsigned char name[20];

    unsigned char Data[FSMCountDATA];
} __attribute__((aligned(4)));

#define FSMH_Header_Size_Ans_SetSetting 4

struct FSM_AnsSetSetting_Header
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    int status;
} __attribute__((aligned(4)));

struct FSM_AnsSetSetting
{
    unsigned char opcode;    ///< Код операции
    unsigned char CRC;       ///< CRC
    unsigned short IDDevice; ///< Ид устройства

    int status;
} __attribute__((aligned(4)));

enum FSMIOCTL_Cmd {
    FSMIOCTL_SendData,
};

enum FSM_eventlist {
    FSM_ServerStarted = 0x00,
    FSM_EthernetStarted = 0x01,
    FSM_ServerConfigChanged = 0x02,
    FSM_ServerStatisticChanged = 0x03
    /**CCK Event List 0x04 - 0x3F **/
    /**Statistick Event List 0x40 - 0x6F **/

};
#endif // FCMPROTOCOL
