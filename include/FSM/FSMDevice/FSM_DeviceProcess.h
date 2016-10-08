/*!
\file
\brief Процессс работы с устроствами
\authors Гусенков.С.В
\version 0.0.1_rc1
\date 30.12.2015
*/

#ifndef FSM_DEVICEPROCESS_H
#define FSM_DEVICEPROCESS_H

#include "FSM/FSMSetting/FSM_settings.h"
#include "FSM/FSMAudio/FSM_AudioStream.h"
#include <FSM/FSMEthernet/FSMEthernetHeader.h>
#include "FSM/FSMDevice/fcmprotocol.h"
#include "FSM/FSMDevice/fsm_statusstruct.h"
#include "FSM/FSMDevice/fcm_audiodeviceclass.h"
#include "FSM/FSM_Switch/fsm_switch.h"
#include "FSM/FSM_Commutator/FSM_Commutator.h"

typedef struct FSM_DeviceTree FSM_DeviceTree_t;
/*!
\brief Прототип функции обратной связи
*/
typedef void (*ApplayProcess)(FSM_DeviceTree_t*, FSM_DeviceTree_t*);
// void FS_AFun(struct FSM_DeviceTree* to,struct FSM_DeviceTree* from);
typedef void (*DeviceProcess)(char*, short, FSM_DeviceTree_t*, FSM_DeviceTree_t*);
// void FS_Fun(char* Data,short len, struct FSM_DeviceTree* to,struct FSM_DeviceTree* from);

/*!
\brief Информации о  устройстве
*/
struct FSM_DeviceTree
{
    unsigned char registr;             ///< Состояние регистрации
    unsigned short IDDevice;           ///< Ид устройства
    struct FSM_DeviceFunctionTree* dt; ///< Информации о виде устройства
    char state[32];
    void* config;
    void* data;
    int id;
    struct FSM_DeviceTree* TrDev;
    unsigned char debug;
    struct FSM_PropertyDevice pdl[FSM_PropertyTreeSize];
    int pdl_count;
};
/*!
\brief Информации о виде устройства
*/
struct FSM_DeviceFunctionTree
{
    unsigned char registr;      ///< Состояние регистрации
    unsigned char type;         ///< Тип устройства
    unsigned char VidDevice;    ///< Вид устройства
    unsigned char PodVidDevice; ///< Подвид устройства
    unsigned char KodDevice;    ///<Код устройства
    DeviceProcess Proc;         ///< Обратная связь
    ApplayProcess aplayp;
    unsigned char debug;
    unsigned short config_len;
};

/*!
\brief Регистрация устройства
\param[in] dt Пакет регистрации
\return Код ошибки
*/
unsigned char FSM_DeviceRegister(struct FSM_DeviceRegistr dt);
/*!
\brief Регистрация класса устройств
\param[in] dft Пакет класса устроства
\return Код ошибки
*/
unsigned char FSM_DeviceClassRegister(struct FSM_DeviceFunctionTree dft);
/*!
\brief Поиск класса устроства
\param[in] dt Пакет регистрации
\return Код ошибки
*/
struct FSM_DeviceFunctionTree* FSM_FindDeviceClass(struct FSM_DeviceRegistr dt);
/*!
\brief Поиск класса устроства
\param[in] dft Пакет класса устроства
\return Ссылку на класс устроства
*/
struct FSM_DeviceFunctionTree* FSM_FindDeviceClass2(struct FSM_DeviceFunctionTree dft);
/*!
\brief Поиск устроства
\param[in] id ID
\return Ссылку на класс устроства
*/
struct FSM_DeviceTree* FSM_FindDevice(unsigned short id);
/*!
\brief Удаление из списка устроства
\param[in] fdd Пакет удаления устройства
\return Ссылку на устроство
*/
void FSM_DeRegister(struct FSM_DeviceDelete fdd);
/*!
\brief Удаление из списка классов устроства
\param[in] dft Пакет класса устроства
*/
void FSM_ClassDeRegister(struct FSM_DeviceFunctionTree dft);

/*!
\brief Получение статистики
\return Статистику
*/
struct fsm_statusstruct* FSM_GetStatistic(void);

/*!
\brief Установка статуса
 \param[in] fdt Устроство
 \param[in] status Состояние
*/
void FSM_Statstic_SetStatus(struct FSM_DeviceTree* fdt, char* status);

/*!
\brief Получение настроек
\return Настройки
*/
struct fsm_devices_config* FSM_GetSetting(void);

/*!
\brief Установка структуры настроек
 \param[in] fdt Устроство
 \param[in] set Настроки
*/
void FSM_Setting_Set(struct FSM_DeviceTree* fdt, void* set);

/*!
\brief Применить настройки
 \param[in] fdt Устроство
 \param[in] set Настроки
*/
void FSM_Setting_Applay(struct FSM_DeviceTree* fdt, void* set);

/*!
\brief Регистрация устройства
\param[in] dt Пакет регистрации
\return Код ошибки
*/
typedef unsigned char (*FSM_FDeviceRegister)(struct FSM_DeviceRegistr);
/*!
\brief Регистрация класса устройств
\param[in] dft Пакет класса устроства
\return Код ошибки
*/
typedef unsigned char (*FSM_FDeviceClassRegister)(struct FSM_DeviceFunctionTree);
/*!
\brief Поиск класса устроства
\param[in] dt Пакет регистрации
\return Код ошибки
*/
typedef struct FSM_DeviceFunctionTree* (*FSM_FFindDeviceClass)(struct FSM_DeviceRegistr);
/*!
\brief Поиск класса устроства
\param[in] dft Пакет класса устроства
\return Ссылку на класс устроства
*/
typedef struct FSM_DeviceFunctionTree* (*FSM_FFindDeviceClass2)(struct FSM_DeviceFunctionTree dft);
/*!
\brief Поиск устроства
\param[in] id ID
\return Ссылку на класс устроства
*/
typedef struct FSM_DeviceTree* (*FSM_FFindDevice)(unsigned short id);
/*!
\brief Удаление из списка устроства
\param[in] fdd Пакет удаления устройства
\return Ссылку на устроство
*/
typedef void (*FSM_FDeRegister)(struct FSM_DeviceDelete fdd);
/*!
\brief Удаление из списка классов устроства
\param[in] dft Пакет класса устроства
*/
typedef void (*FSM_FClassDeRegister)(struct FSM_DeviceFunctionTree dft);

void FSM_SendEventToDev(enum FSM_eventlist idevent, struct FSM_DeviceTree* TransportDevice);
void FSM_SendEventToAllDev(enum FSM_eventlist idevent);

void FSM_ToProcess(int id, char* Data, short len, struct FSM_DeviceTree* from_dt);

int FSM_ToCmdStream(struct FSM_DeviceTree* pdt);

void FSM_Beep(int value, int msec);

int FSM_AddProperty(char* PropertyCode,
                    void* Property,
                    unsigned short pr_size,
                    UpdateDataProperty udp,
                    struct FSM_DeviceTree* dt);

enum FSM_UK /*125 *** 254*/
{ 
    FSMNotRegistred = 0,
    FSMGetCmdStream = 1,
    AnsFSMGetCmdStream = 2,
    FSMFlash_Start=3,
    FSMFlash_Execute=4,
    FSMFlash_Confirm=5,
    FSMFlash_Data=6
};

#endif /* FSM_DEVICEPROCESS_H */
