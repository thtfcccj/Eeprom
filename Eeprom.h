/***************************************************************************

                          Eeprom读写模块标准化接口

****************************************************************************/
#ifndef _EEPROM_H
#define _EEPROM_H
#ifdef SUPPORT_EX_PREINCLUDE//不支持Preinlude時
  #include "Preinclude.h"
#endif

//读写块实现(块地址,E2地址,读写长度),uiaddr_e2的类型可根据情况确定
#ifndef EepromAdr_t
  #define EepromAdr_t  unsigned char
#endif

#ifndef EepromLen_t
  #define EepromLen_t  unsigned char
#endif

/***************************************************************************
                          对外接口部分
***************************************************************************/

//-----------------------------初始化函数---------------------------------
void Eeprom_Init(void);

//---------------------------读取Eeprom数据---------------------------------
void Eeprom_Rd(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len);

//---------------------------写Eeprom数据---------------------------------
void Eeprom_Wr(EepromAdr_t Adr,
               const void *pVoid,
               EepromLen_t Len);

//--------------------------Eeprom写常量数据--------------------------
//此函数为附加功能, 部分驱动实现
void Eeprom_WrConst(EepromAdr_t Adr,   //Eerpom中的存取位置
                  unsigned char Data,  //常量数据
                  EepromLen_t Len);    //写入长度

#endif
