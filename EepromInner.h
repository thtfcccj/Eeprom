/***************************************************************************

     Eeprom读写模块-当在MCU内部直接寻址时(EEPROM或FLASH)的附加函数接口
此接口独立于硬件与应用, 满足及需要此功能时，底层需实现下述函数
注：当使用外部大容量Flash或EEPROM，也可以模拟实现，为此功能以提高效率。
****************************************************************************/

#ifndef _EEPROM_INNER_H
#define _EEPROM_INNER_H
#ifdef SUPPORT_EX_PREINCLUDE//不支持Preinlude時
  #include "Preinclude.h"
#endif

/***************************************************************************
                            EEPROM辅助函数
****************************************************************************/

//---------------------------初始化函数---------------------------------
void Eeprom_Init(void);

//----------------------格式化Eeprom数据---------------------------------
void Eeprom_Format(void);

//-----------------------------缓存复位---------------------------------
//全部恢复为0xff
void Eeprom_BufReset(void);

//-----------------------------任务函数---------------------------------
//放入250ms进程中
void Eeprom_Task(void);

//-----------------------得到写入次数函数---------------------------------
//有此功能时将返回，否则返回为0
unsigned long Eeprom_GetWrCount(void);

//----------------------------强制回写缓冲区数据------------------------
//重要数据保存时(如系统实始化标志等)，可调用此函数强制回写
void Eeprom_ForceWrBuf(void);

//-----------------------------强制回写并重启---------------------------
//在检测到关机，或需要复位时需调用此函数防止数据未保存而丢失
void Eeprom_ForceWrBufAndRestart(void);

//-------------------------由Eeprom基址转换为只读指针------------------
//可用于只读数据，建议以结构方式进行以提高效率
const unsigned char *Eeprom_pGetRd(EepromAdr_t Adr, EepromLen_t Len);

//-------------------------由Eeprom基址转换为可写指针------------------
//可用于数据的读写，若只读不更改，应使用Eeprom_pGetRd()以节省写入次数和效率
//建议以结构方式进行以提高效率
unsigned char *Eeprom_pGetWr(EepromAdr_t Adr, EepromLen_t Len);

/***************************************************************************
                    末尾预留短周期长时间空间功能相关
//此空间用于解决存储需短周期但长时间时FALSH模拟擦写次数问题如：小时计数策略为：
//准备一段空间如a[256], EEPROM擦除一页时为可写入0xff,要写入时依次写入1个非0xff
//通过统计非0xff个数可获得小时数，计数到256时，再擦除
//内部调用策略为:
//外部需写入时，调用flash模块写入，若写满了，则写入真正的保存值以回写
****************************************************************************/
//需在全局里定义预留大小，应为8的倍数，并注意在InfoBase中去除此空间。
//#define EEPROM_LAST_RCV_SIZE   1024
#ifdef EEPROM_LAST_RCV_SIZE //末尾预留空间时
//------------------------------得到末次写入位置--------------------------------
//返回保留空间的硬件(如flash)基址
unsigned long Eeprom_GetLastRcvHwBase(void);
#endif //EEPROM_LAST_RCV_SIZE

//--------------------得到允许写入保留区域数据的个数----------------------------
//在EEPROM写入完成但未锁定时调用,返回将复制原有数据回写的个数
unsigned short Eeprom_cbGetWrBackLastRcvCount(void);

#endif 
  


