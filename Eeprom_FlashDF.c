/***************************************************************************

      Eeprom读写模块-使用内部的Flash双页小空量空间模拟-全RAM缓冲时的实现

此模块为Eeprom_FlashD.c的简化版，使用了全RAM缓冲以规避命中问题，使用条件为：
   1. 用户的需要的EEPROM空间在一个扇区(多页时一组连续扇区)内能搞定。
   2. 管理的两个Flash扇区(多页时一组连续扇区)均为小容量用RAM够多，以保证全缓冲区占用的RAM较小

注：默认按64k Flash容量，512byte扇区配置, EEPROM放最后，单页模式，即EEPROM大小为512byte
****************************************************************************/

#include "Eeprom.h"
#include "Flash.h"              //Flash标准函数接口
#include <string.h>
//#include "IoCtrl.h"             //仅调用WDT_Week();

//--------------------------宏默认值-------------------------------------
#define _HEADER_SIZE        8      //定义数据头大小,固定为8以保证8字节对齐和校验

#ifndef EEPROM_WR_BACK_OV   //回写时间默认单位256ms
  #define EEPROM_WR_BACK_OV  (5 * 4)
#endif

#ifndef EEPROM_BASE1                 //第一个EEPROM的页起始地址
  #define EEPROM_BASE1  (0x10000 - 0x400)  
#endif

#ifndef EEPROM_BASE2                 //第二个EEPROM的页起始地址
  //此值应满足(EEPROM_BASE2 - EEPROM_BASE1) >= (EEPROM_SIZE + _HEADER_SIZE)
  #define EEPROM_BASE2  (0x10000 - 0x200)  
#endif

//FLASH分页个数,可用多个连续页组成一个EEPROM_SIZE,此值满足：
//((EEPROM_SIZE + _HEADER_SIZE) / EEPROM_PAGE_COUNT) 应正好对应FLASH的一页
#ifndef EEPROM_PAGE_COUNT  
  #define EEPROM_PAGE_COUNT     1     //默认只有一页
#endif

//实际EEPROM的页大小,需为扇区大小-_HEADER_SIZE
#ifndef EEPROM_SIZE
  #define EEPROM_SIZE  (512 - _HEADER_SIZE)       
#endif

struct _Eeprom{ //管理器
  //页缓冲
  unsigned long Counter[2];   //页计数器(在缓冲区前以保证8节字对齐,首个有效，第二个取反)
  unsigned char Buf[EEPROM_SIZE];   //用户内容全缓冲,不含页计数器

  //第二页为新数据，否则为第一页
  unsigned char Page2New;
  //回写数据定时器,有值时表示有数据需要更新写入
  unsigned char WrBackTimer; 
  //Eeprom_pGEtWr()调用计数器，用于防止用户频繁调用获得可写指针时，复位回写定时器从而导致不自动回写
  unsigned char BufWrCount;
  //数据头错误标志
  signed char HeaderErr;  
};

struct _Eeprom  _Eeprom;

#define  _PAGE_SIZE   ((EEPROM_SIZE + _HEADER_SIZE) / EEPROM_PAGE_COUNT) //应正好为FLASH的一页
/*******************************************************************************
                           初始化与格式化及相关公共函数
********************************************************************************/
//----------------------从Flash中更新计数器与新页标志----------------------------
static void _UpdateCounterAndNew(void)
{
  unsigned char Page2New = 0;
  unsigned long Counter = *(unsigned long*)EEPROM_BASE1;
  unsigned long Counter2 = *(unsigned long*)EEPROM_BASE2;
  if(Counter == 0xffffffff){
    if(Counter2 == 0xffffffff)//芯片头一次被使用
      Counter = 0;   
    else{//第二页有数据
      Counter = Counter2;
      Page2New = 1;//第二页了
    }
  }
  else{//两页均有数据,比较大小(暂没校验是否累加器有问题)
    if(Counter < Counter2){ //在第二页
      Counter = Counter2;
      Page2New = 1;
    }
  }
  //校验数据头
  if(Page2New) Counter2 = *(unsigned long*)(EEPROM_BASE2 + 4);
  else Counter2 = *(unsigned long*)(EEPROM_BASE1 + 4);
  if(Counter2 != (0 - Counter)) //校验错误
    _Eeprom.HeaderErr = -1;
  else _Eeprom.HeaderErr = 0; //正确了
  
  _Eeprom.Counter[0] = Counter;
  _Eeprom.Counter[1] = 0 - _Eeprom.Counter[0];//取反以写入
  _Eeprom.Page2New = Page2New;
  
  //最后缓冲全数据至缓冲区
  if(Page2New) 
    memcpy(_Eeprom.Buf, (const char*)(EEPROM_BASE2 + _HEADER_SIZE), EEPROM_SIZE);
  else
    memcpy(_Eeprom.Buf, (const char*)(EEPROM_BASE1 + _HEADER_SIZE), EEPROM_SIZE);
}

//--------------------将数据写入对应Flash中----------------------------
static void _WrFlash(unsigned char IsPage2,
                     unsigned long OffFlashAdr, //此为页内的flash基址
                     const void *pVoid,
                     EepromLen_t Len)
{
  //获得绝对位置
  if(IsPage2) OffFlashAdr += EEPROM_BASE2;
  else OffFlashAdr += EEPROM_BASE1;  
  #if EEPROM_PAGE_COUNT <= 1 //一页内完成
    Flash_Write(OffFlashAdr, pVoid, Len);
  #else //多个FLASH页组成时，分别处理
    unsigned short CurLen = OffFlashAdr % _PAGE_SIZE; //页内偏移
    if((CurLen + Len) <= _PAGE_SIZE){//在一页内写完了
      Flash_Write(OffFlashAdr, pVoid, Len);
      return;
    }
    CurLen = _PAGE_SIZE - CurLen; //第一页可写入数量
    //第一页单独写
    const unsigned char *pPos = (const unsigned char *)pVoid;
    Flash_Write(OffFlashAdr, pPos, CurLen);
    Len -= CurLen;
    pPos += CurLen;
    OffFlashAdr += CurLen;
    //第二页起整页写入
    for(; Len > _PAGE_SIZE; Len -= _PAGE_SIZE, 
                             pPos += _PAGE_SIZE, OffFlashAdr += _PAGE_SIZE){   
      Flash_Write(OffFlashAdr, pPos, _PAGE_SIZE);
    }
    //最后一页单独写
    Flash_Write(OffFlashAdr, pPos, Len);
  #endif
}

//-----------------------格式化对应页----------------------------
static void _Format(unsigned char IsPage2) 
{
  unsigned long FlashBase;
  if(IsPage2) FlashBase = EEPROM_BASE2;
  else FlashBase = EEPROM_BASE1;
  
  #if EEPROM_PAGE_COUNT <= 1 //一页内完成
    Flash_ErasePage(FlashBase);
  #else //多个FLASH页组成时，分别格式化
    for(unsigned char Count = EEPROM_PAGE_COUNT; Count > 0; Count--){
      Flash_ErasePage(FlashBase);
      FlashBase += _PAGE_SIZE;
    }
  #endif
}

//---------------------------初始化函数---------------------------------
void Eeprom_Init(void)
{
  _Eeprom.WrBackTimer = 0;                    //不需要回写
  _Eeprom.BufWrCount = 0;             //计数器清零
  _UpdateCounterAndNew();               //更新计数器与新页标志
}

//----------------------格式化Eeprom数据---------------------------------
void Eeprom_Format(void)
{

}

/*******************************************************************************
                                写数据相关
********************************************************************************/
//------------------------------回写缓冲区数据----------------------------------
static void _WrBack(void)
{
  //复位回写相关
  _Eeprom.WrBackTimer = 0; 
  _Eeprom.BufWrCount = 0;  
  
  //准备1：先找到待写入的新页面与老基址
  unsigned char CurNew;
  unsigned long OldFlashBase; 
  if(_Eeprom.Page2New){
    OldFlashBase = EEPROM_BASE2;    
    CurNew = 0;//正好相反
  }
  else{
    OldFlashBase = EEPROM_BASE1;      
    CurNew = 1;
  }
  //准备2：无条件确保缓冲区里的值是新的
  if(!memcmp((void*)(OldFlashBase + _HEADER_SIZE), _Eeprom.Buf, EEPROM_SIZE))
    return; //没有更新
  
  //准备3：先格式化待写入页面  
  _Format(CurNew);  
  
  //1写数据区
  unsigned long OffAdr = _HEADER_SIZE; //当前地址偏移
  //将缓冲的新数据写入(_FlashToBuf()时已确保不超限)
  _WrFlash(CurNew, OffAdr, _Eeprom.Buf, EEPROM_SIZE);
  OffAdr += EEPROM_SIZE;  //偏移更新至下次
  
  //2.最新定时器累加并写入(未考虑计数到底回环到0情况)
  _Eeprom.Counter[0]++;
  _Eeprom.Counter[1] = 0 - _Eeprom.Counter[0];//取反作为校验码写入
  _WrFlash(CurNew, 0, _Eeprom.Counter, _HEADER_SIZE);//写数据头
  //注：忽略了对数据头写入的校验！
  
  //3.最后切换到新页以完成(注: 此时缓冲区里的数据仍然是新的并有效)
  _Eeprom.Page2New = CurNew;
}

//---------------------------写Eeprom数据---------------------------------
void Eeprom_Wr(EepromAdr_t Adr,
               const void *pVoid,
               EepromLen_t Len)
{
  //检查数据区安全性
  // (略)
  
  //先写到缓冲区
  memcpy(&_Eeprom.Buf[Adr], pVoid, Len);
  
  //启动自动回写
  _Eeprom.WrBackTimer = EEPROM_WR_BACK_OV; 
  
}
/*******************************************************************************
                                读数据相关
********************************************************************************/

//---------------------------读取Eeprom数据---------------------------------
void Eeprom_Rd(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len)
{
  //检查数据区安全性
  // (略)
  
  memcpy(pVoid, &_Eeprom.Buf[Adr], Len); //直接在缓冲区里了
}

//-------------------------由Eeprom基址转换为只读指针------------------
//可用于只读数据，建议以结构方式进行以提高效率
const unsigned char *Eeprom_pGetRd(EepromAdr_t Adr, EepromLen_t Len)
{
  return  &_Eeprom.Buf[Adr];   //直接在缓冲区里了
}

//-------------------------由Eeprom基址转换为可写指针------------------
//可用于数据的读写，若只读不更改，应使用Eeprom_pGetRd()以节省写入次数和效率
//建议以结构方式进行以提高效率
const unsigned char *Eeprom_pGetWr(EepromAdr_t Adr, EepromLen_t Len)
{
  return  &_Eeprom.Buf[Adr];   //直接在缓冲区里了
}

//-------------------------任务函数---------------------------------
//放入250ms进程中
void Eeprom_Task(void)
{
  //防止用户频繁调用时复位回写，此机制用于强制回写
  if(_Eeprom.BufWrCount == 255){
    _WrBack();
    return;
  }
  //自动回写计数
  if(!_Eeprom.WrBackTimer) return; //没有回写需求
  _Eeprom.WrBackTimer--;
  if(_Eeprom.WrBackTimer) return;  //等待过程中
  //时间到执行回写
  _WrBack();
}

//-----------------------得到写入次数函数---------------------------------
//有此功能时将返回，否则返回为0
unsigned long Eeprom_GetWrCount(void)
{
  return _Eeprom.Counter[0];
}

//--------------------------强制回写缓冲区数据---------------------------------
//重要数据保存时(如系统实始化标志等)，可调用此函数强制回写
void Eeprom_ForceWrBuf(void)
{
  _WrBack();
}

//--------------------------------强制回写并重启-------------------------------
//在检测到关机，或需要复位时需调用此函数防止数据未保存而丢失
void Eeprom_ForceWrBufAndRestart(void)
{
  _WrBack();  
  while(1);
}

  


