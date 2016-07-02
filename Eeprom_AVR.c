/***************************************************************************

         EERPOM存取位置标准化接口-在AVR内部EEPROM中的实现

此接口为具体项目: 所有存取位置的,提供统一接口。实现了调用层与移动应用的分离。
***************************************************************************/

#include "Eeprom.h"

//#include <avr_macros.h>
#include <intrinsics.h>//interrupt_en
#include <comp_a90.h>//sei()
#include <ioavr.h>

//------------------------寄存器说明及相关操作宏----------------------------
//EEAR  地址寄存器
//EEDR  数据寄存器

//控制寄存器EECR位定义
#define EECR_EERIE   0x08    //E2就绪中断使能
#define EECR_EEMWE   0x04    //写使能
#define EECR_EEWE    0x02    //写使能
#define EECR_EERE    0x01    //读使能

#define _BusyWait()  do{ }while(EECR & EECR_EEWE)
#define _WriteIn()   do{\
  _CLI();\
  EECR = 0x04;\
  EECR = 0x06;\
  _SEI();\
}while(0)


//-----------------------------初始化函数---------------------------------
void Eeprom_Init(void)
{

}

//---------------------------读取Eeprom数据---------------------------------
void Eeprom_Rd(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len)
{
  unsigned char *pData;
  pData = (unsigned char*)pVoid;
  for(; Len > 0; Len--, Adr++, pData++){
    _BusyWait();
    _CLI();                  //临界区操作
    EEAR = Adr;
    EECR = EECR_EERE;
    *pData = EEDR;
    _SEI();
  }    
}

//---------------------------写入Eeprom数据---------------------------------
void Eeprom_Wr(EepromAdr_t Adr,
               const void *pVoid,
               EepromLen_t Len)
{
  const unsigned char *pData;
  pData = (const unsigned char*)(pVoid);
  for(; Len > 0; Len--, Adr++, pData++){
    _BusyWait();
    EEAR = Adr;
    EEDR = *pData;
    _WriteIn();
  }    
}


