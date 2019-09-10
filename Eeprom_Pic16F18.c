/*************************************************************
             EEPROM在PIC17F18xxx 内部EEPROM中的实现
 * 因调用的库文件，此文件同时适用于XC8编译器的大部分PIC MCU
*************************************************************/

#include <pic.h>
#include "PicBit.h"

#include "Eeprom.h"

//PIC堆栈深度测试,实际堆栈 = IDE显示堆栈 - 5
//#define SUPPORT_STACK_TEST

#ifdef SUPPORT_STACK_TEST
#include <string.h>

//---------------------------读取Eeprom数据---------------------------------
void _Wr_Test3(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len)
{
   Eeprom_Rd(Adr,pVoid,Len);//多4级
}

//---------------------------读取Eeprom数据---------------------------------
void _Wr_Test2(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len)
{
   _Wr_Test3(Adr,pVoid,Len);//多3级
}

//---------------------------读取Eeprom数据---------------------------------
void _Wr_Test(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len)
{
   _Wr_Test2(Adr,pVoid,Len);//多2级
}
#endif

//---------------------------读取Eeprom数据---------------------------------
void Eeprom_Rd(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len)
{
    #ifdef SUPPORT_STACK_TEST
     memset(pVoid, 0, Len); //PIC堆栈深度测试,多5级
    #endif

    unsigned char *pData;
    pData = (unsigned char*)pVoid;
    for(;Len > 0;Len--,Adr++,pData++)
    {
      *pData = eeprom_read(Adr);
    }    
}

//---------------------------写Eeprom数据---------------------------------
void Eeprom_Wr(EepromAdr_t Adr,
               const void *pVoid,
               EepromLen_t Len)
{
    #ifdef SUPPORT_STACK_TEST
      _Wr_Test(Adr,pVoid,Len); //PIC堆栈深度测试:多1级
    #endif
    unsigned char *pData;
    pData = (unsigned char*)(pVoid);
    for(;Len > 0;Len--,Adr++,pData++)
    {
      eeprom_write(Adr, *pData);
    }
}




