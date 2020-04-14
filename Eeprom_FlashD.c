/***************************************************************************

         Eeprom读写模块-使用内部的Flash双页大空量空间模拟-跨多个页面时实现
此模块独立于硬件,使用标准Flash接口实现，以适用于不同MCU的FLASH(或外部FLASH)
使用说明:
1. 需全局宏定义起始地址EEPROM_BASE1/2,作为EEPROM的页大小EEPROM_SIZE,以及
   缓存大小EEPROM_BUF_SIZE
2. 发现初始化标志为首次使用FLASH等情况时，可通过Eeprom_Format()将EEPROM清零
3. 此模块系统需求:
   (1. Eeprom_init()放入调用EEPROM前的初始化中
   (2. Eeprom_Task()放入系统256ms进程中
   (3. 实现WDT_Week()
●内部机制说明:
采用双Flash回写机制+缓冲区域交叉写入,具体为:
//  ■每个扇区起始，有一个>=4Byte的信息头存储累加器（没有做数据校验）
//  ■采用一个缓冲数据机制，以减少写入概率,读数时，通过查询命中可确保数据的正确性
//  ■有数据需要写入时,操作步骤为:
//    ◆1. 将累加器小的Flash区擦除
//    ◆2. 将累加器大的Flash区数据+结合被修改的数据一起，分组写入上一步的Flash区中
//    ◆3. 写入完成，校验无误后，写入新的信息头(累加器+1)表示成功。
****************************************************************************/

#include "Eeprom.h"
#include "Flash.h"              //Flash标准函数接口
#include <string.h>
//#include "IoCtrl.h"             //仅调用WDT_Week();

//--------------------------宏默认值-------------------------------------
//默认按STM32F4x 1M容量，128K扇区配置
#ifndef EEPROM_WR_BACK_OV   //回写时间默认单位256ms
  #define EEPROM_WR_BACK_OV  (10 * 4)
#endif

#ifndef EEPROM_BASE1                 //第一个EEPROM的页起始地址
  #define EEPROM_BASE1  0x08020000  
#endif

#ifndef EEPROM_BASE2                 //第二个EEPROM的页起始地址
  #define EEPROM_BASE2  0x08040000  
#endif

#ifndef EEPROM_SIZE
  #define EEPROM_SIZE  0x00020000  //实际EEPROM的页大小,需为8的倍数
#endif

#ifndef EEPROM_BUF_SIZE
  #define EEPROM_BUF_SIZE  10240    //缓存的数据大小,<=EEPROM_SIZE
#endif

#define _HEADER_SIZE        8      //定义数据头大小,固定为8以保证8字节对齐和校验

struct _Eeprom{ //管理器
  //页缓冲
  EepromAdr_t BufBase;   //缓存数据的EEPROM基址(不含_HEADER_SIZE,-1表示无缓冲)
  unsigned long Counter[2];   //页计数器(在缓冲区前以保证8节字对齐,首个有效，第二个取反)
  unsigned char Buf[EEPROM_BUF_SIZE];   //用户内容缓冲

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
  
  Flash_Write(OffFlashAdr, pVoid, Len);
}

//-----------------------格式化对应页----------------------------
static void _Format(unsigned char IsPage2) 
{
  unsigned long FlashBase;
  if(IsPage2) FlashBase = EEPROM_BASE2;
  else FlashBase = EEPROM_BASE1;
  Flash_ErasePage(FlashBase);
}

//---------------------------初始化函数---------------------------------
void Eeprom_Init(void)
{
  _Eeprom.BufBase = (unsigned long)-1;//没有缓冲
  _Eeprom.WrBackTimer = 0;                    //不需要回写
  _Eeprom.BufWrCount = 0;             //计数器清零
  _UpdateCounterAndNew();               //更新计数器与新页标志
}

//----------------------格式化Eeprom数据---------------------------------
void Eeprom_Format(void)
{
  /*_Format(0);
  _Format(1); 
  _Eeprom.BufBase = (unsigned long)-1;//让缓冲区失效
  _Eeprom.WrBackTimer = 0;//不需要回写
  _Eeprom.BufWrCount = 0; //重新开始  
  
  //将当前计数值写入对应页以统计Flash的写入次数
  _WrFlash(_Eeprom.Page2New, 0, _Eeprom.Counter, 8);*/
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
  
  if(_Eeprom.BufBase == (unsigned long)-1) return; //异常，缓冲区里没有数据
  
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
  if(!memcmp((void*)(OldFlashBase + _HEADER_SIZE + _Eeprom.BufBase), 
             _Eeprom.Buf, EEPROM_BUF_SIZE))
    return; //没有更新
  
  //准备3：先格式化待写入页面  
  _Format(CurNew);  
  
  unsigned long OffAdr = _HEADER_SIZE; //当前地址偏移
  //1. 将缓冲区地址前面的Flash原有数据写入新页面
  if(_Eeprom.BufBase){
    _WrFlash(CurNew,
             OffAdr, 
             (unsigned char*)(OldFlashBase + OffAdr),
             _Eeprom.BufBase);
    OffAdr += _Eeprom.BufBase;  //偏移更新至下次
  }
  //2. 将缓冲的新数据写入(_FlashToBuf()时已确保不超限)
  _WrFlash(CurNew, OffAdr, _Eeprom.Buf, EEPROM_BUF_SIZE);
  OffAdr += EEPROM_BUF_SIZE;  //偏移更新至下次
  
  //3. 将缓冲区地址后面的Flash原有数据写入新页面
  if(OffAdr < EEPROM_SIZE){
    _WrFlash(CurNew,
             OffAdr, 
             (unsigned char*)(OldFlashBase + OffAdr),
             EEPROM_SIZE - OffAdr);
  }
  //4.最新定时器累加并写入(未考虑计数到底回环到0情况)
  _Eeprom.Counter[0]++;
  _Eeprom.Counter[1] = 0 - _Eeprom.Counter[0];//取反作为校验码写入
  _WrFlash(CurNew, 0, _Eeprom.Counter, 8);//写数据头
  //注：忽略了对数据头写入的校验！
  
  //5.最后切换到新页以完成(注: 此时缓冲区里的数据仍然是新的并有效)
  _Eeprom.Page2New = CurNew;
}

//-----------------------填充指定位置新数据到缓冲区-------------------------
static void _FlashToBuf(EepromAdr_t Adr)
{
  Adr &= ~0x07; //保证8字对齐方便写Flash时提高效率
  if((Adr + EEPROM_BUF_SIZE) > (EEPROM_SIZE - _HEADER_SIZE)){//最后超限了，直接缓冲最后数据
    Adr = (EEPROM_SIZE - _HEADER_SIZE) - EEPROM_BUF_SIZE;
  }
  //找到基址
  unsigned long FlashBase; 
  if(_Eeprom.Page2New) FlashBase = EEPROM_BASE2 + _HEADER_SIZE;
  else FlashBase = EEPROM_BASE1 + _HEADER_SIZE;
  //copy数据
  memcpy(_Eeprom.Buf, 
         (const unsigned char*)(FlashBase + Adr), 
         EEPROM_BUF_SIZE);
  //缓冲完成，最后才能更新地址
  _Eeprom.BufBase = Adr; 
}

//---------------------------写用户数据到缓冲区---------------------------------
//只有命中时才能写入或比较内容，返回1表示成功(含内容相同)，否则失败
static signed char  _EerpomToBuf(EepromAdr_t Adr,
                                 const void *pVoid,
                                 EepromLen_t Len)

{
  if(Adr < _Eeprom.BufBase) return 0;//起始位置在缓冲区后面,可能后半部分命中也认为不命中
  
  EepromAdr_t OffBufBase = Adr - _Eeprom.BufBase;//得到缓冲区内偏移
  
  if(OffBufBase >= EEPROM_BUF_SIZE) return 0; //起始位置在缓冲区后面
  
  if((OffBufBase + Len) >  EEPROM_BUF_SIZE) return 0; //部分命中，也认为没命中
  
  //检查内容是否一致，若没更改则不处理->RAM对RAM忽略相同检查
  //if(!memcmp(&_Eeprom.Buf[OffBufBase], pVoid, Len))
  //  return 1; //一致也相当于写了，也成功！  
  
  //在缓冲区里了，写入缓冲区 
  memcpy(&_Eeprom.Buf[OffBufBase], pVoid, Len);
  _Eeprom.WrBackTimer = EEPROM_WR_BACK_OV; //启动自动回写
  return 1; //成功！！！
}

//---------------------------写Eeprom数据---------------------------------
void Eeprom_Wr(EepromAdr_t Adr,
               const void *pVoid,
               EepromLen_t Len)
{
  //1. 缓冲区命中时写到缓冲区
  if(_EerpomToBuf(Adr, pVoid, Len)) return; 
  //2. 没有命中，检查写入的数据是否没更改过
  unsigned long FlashBase; 
  if(_Eeprom.Page2New) FlashBase = EEPROM_BASE2 + _HEADER_SIZE;
  else FlashBase = EEPROM_BASE1 + _HEADER_SIZE;
  if(!memcmp((void*)(FlashBase + Adr), pVoid, Len)) return; //数据没有被更改过!  
  //3. 回写原有缓冲区数据
  _WrBack();
  //4. 回写原有数据后，读取现在区域的内容到缓冲区
  _FlashToBuf(Adr);
  //5. 再次缓冲区命中写到缓冲区(一定对,否则代码有问题)
  _EerpomToBuf(Adr, pVoid, Len);
}

/*******************************************************************************
                                读数据相关
********************************************************************************/
//---------------------------得到用户数据从缓冲区-------------------------------
//考虑到存在一个结构中，写整个数据，但读一个数据情况，故采取部分读方式
//返回0全部读完，Len: 未读; 0x80...前半部分未读完，0x40...后半部分未读完，
static unsigned long _EerpomFromBuf(EepromAdr_t Adr,
                                    void *pVoid,
                                    EepromLen_t Len)

{
  if(Adr < _Eeprom.BufBase){//起始不对
    EepromAdr_t EndPos = Adr + Len;
    if(EndPos <= _Eeprom.BufBase) //结束位置也不对
      return Len; //起始位置在缓冲区前面了
    //数据的前面部分在缓冲区里了
    EepromLen_t RdCount = EndPos - _Eeprom.BufBase;
    memcpy(pVoid, &_Eeprom.Buf, RdCount);
    return 0x80000000 | (Len - RdCount); //前半部分未完
  }
  EepromAdr_t OffBufBase = Adr - _Eeprom.BufBase;//得到缓冲区内偏移
  
  if(OffBufBase >= EEPROM_BUF_SIZE) return Len; //起始位置在缓冲区后面了
  
  EepromLen_t RdCount;
  //全部或数据的后面部分在缓冲区里了
  if((OffBufBase + Len) >  EEPROM_BUF_SIZE){
    RdCount = EEPROM_BUF_SIZE - OffBufBase; //截断
  }
  else RdCount = Len;
  //从缓冲区全部或后半部分读数据 
  memcpy(pVoid, &_Eeprom.Buf[OffBufBase], RdCount);
  if(RdCount == Len) return 0;//读完了
  
  return 0x40000000 | (Len - RdCount); //后半部分未完
}

//---------------------------读取Eeprom数据---------------------------------
void Eeprom_Rd(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len)
{
  //1. 先从缓冲区获得数据
  unsigned long  NextLen = _EerpomFromBuf(Adr, pVoid, Len);
  if(NextLen == 0) return; //全部读回了

  //2.从Flash里读取
  unsigned long FlashBase; 
  if(_Eeprom.Page2New) FlashBase = EEPROM_BASE2 + _HEADER_SIZE;
  else FlashBase = EEPROM_BASE1 + _HEADER_SIZE;
  FlashBase += Adr;
  
  if(NextLen != Len){//部分缓冲了
    NextLen &= 0x3fffffff;
    if(NextLen & 0x80000000)//数据在前半部分
      FlashBase -= (Len - NextLen);
    else FlashBase += (Len - NextLen);  //在后半部分
    Len = NextLen;
  }
  //因写入FLASH时，外部已完成了对自身RAM的更新，故不检查缓冲区是否写入
  #ifdef Flash_Read
    Flash_Read(FlashBase, pVoid, Len);
  #else //普通读内存的方法
    memcpy(pVoid, (unsigned char*)(FlashBase), Len);
  #endif
}

//---------------------------得到用户指针从缓冲区-------------------------------
//考虑到存在一个结构中，写整个数据，但读一个数据情况，故采取部分读方式
//返回NULL: 不在缓冲区,-1: 部分命中，否则返回数据指针
static const unsigned char *_pEerpomFromBuf(EepromAdr_t Adr,
                                            EepromLen_t Len)

{
  if(Adr < _Eeprom.BufBase){//起始不对
    EepromAdr_t EndPos = Adr + Len;
    if(EndPos <= _Eeprom.BufBase) //结束位置也不对
      return NULL;
    //数据的前面部分在缓冲区里了
    return (unsigned char *)-1;
  }
  EepromAdr_t OffBufBase = Adr - _Eeprom.BufBase;//得到缓冲区内偏移
  
  if(OffBufBase >= EEPROM_BUF_SIZE) return NULL; //起始位置在缓冲区后面了
  
  //全部或数据的后面部分在缓冲区里了
  if((OffBufBase + Len) >  EEPROM_BUF_SIZE) return (unsigned char *)-1; //部分在
  //全部在了，返回缓冲区指针
  return &_Eeprom.Buf[OffBufBase];
}

//-------------------------由Eeprom基址转换为只读指针------------------
//可用于只读数据，建议以结构方式进行以提高效率
const unsigned char *Eeprom_pGetRd(EepromAdr_t Adr, EepromLen_t Len)
{
  //1. 先从缓冲区获得数据
  const unsigned char *pData = _pEerpomFromBuf(Adr, Len);
  if(pData == (unsigned char *)-1){ //部分命中时
    if(_Eeprom.WrBackTimer){//有新数据时，需回写原有缓冲区数据防止数据为一半
      _WrBack();
    }
    pData = NULL;//从内存读了
  }
  if(pData != NULL) return pData; //命中了
  //直接指向有效内存区域
  unsigned long FlashBase; 
  if(_Eeprom.Page2New) FlashBase = EEPROM_BASE2 + _HEADER_SIZE;
  else FlashBase = EEPROM_BASE1 + _HEADER_SIZE;
  return (unsigned char *)(FlashBase + Adr);
}

//-------------------------由Eeprom基址转换为可写指针------------------
//可用于数据的读写，若只读不更改，应使用Eeprom_pGetRd()以节省写入次数和效率
//建议以结构方式进行以提高效率
const unsigned char *Eeprom_pGetWr(EepromAdr_t Adr, EepromLen_t Len)
{
  //1. 先从缓冲区获得数据
  const unsigned char *pData = _pEerpomFromBuf(Adr, Len);
  if((pData != NULL) && (pData != (unsigned char *)-1)){ //命中了
    if(_Eeprom.BufWrCount != 255) _Eeprom.BufWrCount++; //缓冲区内写累加
    _Eeprom.WrBackTimer = EEPROM_WR_BACK_OV; //自动回写
    return pData; 
  }
  //2. 部分命中或没命中时
  if(_Eeprom.WrBackTimer) _WrBack();//有新数据时，需回写原有缓冲区数据

  //3.缓冲数据
  _FlashToBuf(Adr);

  //回写用户数据用
  _Eeprom.BufWrCount = 0; //缓冲区为新数据了
  _Eeprom.WrBackTimer = EEPROM_WR_BACK_OV; //自动回写
  //4.返回缓冲的数据  
  return _pEerpomFromBuf(Adr, Len); //此次能中了
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

  


