/***************************************************************************

                Eeprom读写模块-使用单页内部的Flash空间来模拟
此模块独立于硬件,使用标准Flash接口实现，以适用于不同MCU的FLASH
使用说明:
1. 需全局宏定义起始地址为EEPROM_BASE,及EEPROM大小(小于FLASH一页的大小)
2. 发现初始化标志为首次使用FLASH等情况时，可通过Eeprom_Format()将EEPROM清零
****************************************************************************/

#include "Eeprom.h"
#include "Flash.h"              //Flash标准函数接口
#include <string.h>

//----------------------宏默认值及检查---------------------------------
#ifndef EEPROM_BASE
  #define EEPROM_BASE  0x0800FC00  //EEPROM的页起始地址（页63）
#endif

#ifdef FLASH_PAGE_SIZE 	//定义有FLASH页时,校验
  #ifndef EEPROM_SIZE
    #define EEPROM_SIZE  FLASH_PAGE_SIZE      //EEPROM区大小
  #elif EEPROM_SIZE > FLASH_PAGE_SIZE    //EEPROM区大小不允许超过FLASH一页大小,允许比其小
    #undef EEPROM_SIZE
    #define EEPROM_SIZE  FLASH_PER_PAGE
  #endif
#else //不校验
  #ifndef EEPROM_SIZE
    #define EEPROM_SIZE  512 //EEPROM区大小
  #endif
#endif //#ifdef FLASH_PAGE_SIZE

struct _Eeprom{ //页缓冲
  unsigned long a; //为Buf unsigned short对齐站位
  unsigned char Buf[EEPROM_SIZE];
};

struct _Eeprom  __Eeprom;
 
//----------------------格式化Eeprom数据---------------------------------
void Eeprom_Format(void)
{
  Flash_Unlock();//解锁
  Flash_ErasePage(EEPROM_BASE);//擦除页
  Flash_Lock();//上锁
}

//---------------------------写Eeprom数据---------------------------------
void Eeprom_Wr(EepromAdr_t Adr,
               const void *pVoid,
               EepromLen_t Len)
{
  //将Flash中的内容拷贝到缓冲区
  memcpy(__Eeprom.Buf,(unsigned char*)EEPROM_BASE, EEPROM_SIZE);
  //在缓冲区里,覆盖要写入的数据
  memcpy(&(__Eeprom.Buf[Adr]),pVoid,Len);
  //写入缓冲区里的全部数据
  Flash_Unlock();//解锁
  Flash_ErasePage(EEPROM_BASE);//写数据前擦除这页
  Flash_Write(EEPROM_BASE, __Eeprom.Buf, EEPROM_SIZE);
  Flash_Lock();//上锁
}
 
//---------------------------读取Eeprom数据---------------------------------
void Eeprom_Rd(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len)
{
  #ifdef Flash_Read
    Flash_Read(EEPROM_BASE + Adr, pVoid, Len);
  #else //普通读内存的方法
    memcpy(pVoid, (unsigned char*)(EEPROM_BASE + Adr), Len);
  #endif
}

 
  


