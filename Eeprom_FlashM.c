/***************************************************************************

         Eeprom读写模块-使用内部的Flash空间来模拟-跨多个页面时实现
此模块独立于硬件,使用标准Flash接口实现，以适用于不同MCU的FLASH(或外部FLASH)
使用说明:
1. 需全局宏定义起始地址EEPROM_BASE,容量FLASH_CAPACITY,页大小FLASH_PAGE_SIZE
2. 发现初始化标志为首次使用FLASH等情况时，可通过Eeprom_Format()将EEPROM清零
3. 此模块系统需求:
   (1. Eeprom_init()放入调用EEPROM前的初始化中
   (2. Eeprom_Task()放入系统256ms进程中
   (3. 实现WDT_Week()
内部机制说明:
   为减小擦除次数此模块将最后需要写入的页缓冲入RAM中，在用户切换页，或用户
没有写入需求导至内部判断超时时(2s)，将会将缓冲区值自动写入FLASH中

注：此模块禁止存储>1页的数据
****************************************************************************/

#include "Eeprom.h"
#include "EepromInner.h"
#include "Flash.h"              //Flash标准函数接口
#include <string.h>
//#include "IoCtrl.h"             //仅调用WDT_Week();


/******************************************************************************
                               常量与结构
******************************************************************************/

//--------------------------全局定义-------------------------------------
//支持用EEPROM写缓冲时， 同时定义一次最大写入数据大小
//#define SUPPORT_EEPROM_WR_BUF  512   

//--------------------------宏默认值-------------------------------------
#ifndef EEPROM_WR_BACK_OV   //回写时间默认256ms*8
  #define EEPROM_WR_BACK_OV  8
#endif

#ifndef EEPROM_BASE
  #define EEPROM_BASE  0x08008000  //EEPROM的页起始地址（页32）
#endif

#ifndef FLASH_CAPACITY    //Flash的空间总容量,必须为FLASH_PAGE_SIZE整数倍
  #define FLASH_CAPACITY  0x08000  //默认32768
#endif

#ifndef FLASH_PAGE_SIZE 	//此值为实际芯片资料上的页大小
  #define FLASH_PAGE_SIZE  512   //没有定义FLASH页时使用默认值
#endif

struct _Eeprom{ //页缓冲
  unsigned long Buf[FLASH_PAGE_SIZE / 4];  //页缓冲，保证4节字对齐
  unsigned char WrBackTimer; //定时器,有值时表示脏（表示缓冲与对应Flash区值对不上） 
  unsigned char BufWrCount;//调用计数器，用于防止用户频繁调用获得可写指针时，复位回写定时器从而导致不自动回写
  unsigned char CurPage;  //当前命中的页
};
struct _Eeprom  _Eeprom;

#ifdef SUPPORT_EEPROM_WR_BUF
struct _WrBuf{ //页缓冲
  unsigned long Buf[SUPPORT_EEPROM_WR_BUF / 4];  //写数
  unsigned long EepromAdr;          //写入数据基址
  unsigned short Len;                //写入数据长度，0表示没有需写入的数据
};
struct _WrBuf _WrBuf;

#endif


/******************************************************************************
                               相关函数实现
******************************************************************************/

//---------------------------初始化函数---------------------------------
void Eeprom_Init(void)
{
  _Eeprom.CurPage = 255; //定位没有在命中区的页，缓冲区值无效
  _Eeprom.WrBackTimer = 0;     //没有脏
  _Eeprom.BufWrCount = 0;             //计数器清零
  #ifdef SUPPORT_EEPROM_WR_BUF
    _WrBuf.Len = 0;      //没有需要写入的数据
  #endif
}

//----------------------格式化Eeprom数据---------------------------------
void Eeprom_Format(void)
{
  unsigned long Base = EEPROM_BASE;
  for(; Base < (EEPROM_BASE + FLASH_CAPACITY); Base+= FLASH_PAGE_SIZE){
    Flash_Unlock();//解锁
    Flash_ErasePage(Base);//擦除页
    Flash_Lock();//上锁
    //WDT_Week();//喂狗一次
  }
  _Eeprom.BufWrCount = 0; //重新开始 
}

//--------------------------回写缓冲区(一页)数据---------------------------------
static void _WrPageBack(void)
{
  unsigned long Base = EEPROM_BASE + _Eeprom.CurPage * FLASH_PAGE_SIZE;
  if((Base >= (EEPROM_BASE + FLASH_CAPACITY)) || //异常或没有缓冲
     (Base < EEPROM_BASE)) return; 
  //复位相关
   _Eeprom.WrBackTimer = 0; 
   _Eeprom.BufWrCount = 0;
   
  //确保缓冲区里的值是新的
  if(!memcmp((void*)Base, _Eeprom.Buf, FLASH_PAGE_SIZE)) return; //没有更新
  
  Flash_Unlock();//解锁
  Flash_ErasePage(Base);//写数据前擦除这页
  Flash_Write(Base, _Eeprom.Buf, FLASH_PAGE_SIZE);
  Flash_Lock();//上锁
}

//--------------------------回写数据---------------------------------
static void _WrBack(void)
{
  #ifdef SUPPORT_EEPROM_WR_BUF
    if(_WrBuf.Len){//有需要写入时
      if(!_Eeprom.WrBackTimer) _Eeprom.WrBackTimer = 1;//任务调用时用于回写
      Eeprom_Wr(_WrBuf.EepromAdr, _WrBuf.Buf, _WrBuf.Len);//可能在缓冲中
      _WrBuf.Len = 0;
      //继续可强制写入
    }
  #endif
  _WrPageBack();
}

//---------------------------写Eeprom页内数据---------------------------------
static void _WrInPage(EepromAdr_t Adr,
                      const void *pVoid,
                      EepromLen_t Len)
{
  unsigned char NextPage = Adr / FLASH_PAGE_SIZE;
  //先检查命中与否
  if(NextPage != _Eeprom.CurPage){
    if(_Eeprom.WrBackTimer) _WrPageBack();//脏了但没有自动更新至FLASH时，先回写     
      
    //缓冲下一页数据
    unsigned long Base = EEPROM_BASE + NextPage * FLASH_PAGE_SIZE;
    memcpy(_Eeprom.Buf,(unsigned char*)Base, FLASH_PAGE_SIZE);
    _Eeprom.CurPage = NextPage;
  }
  //内存比较中否真改了(暂时认为只要调用即更新)
  //if(相同时) reutrn 返回不改
  //在缓冲区里,覆盖要写入的新数据
  memcpy(((unsigned char*)_Eeprom.Buf) + (Adr % FLASH_PAGE_SIZE), pVoid,Len);//覆盖
  _Eeprom.WrBackTimer = EEPROM_WR_BACK_OV; //启动自动回写
  if(_Eeprom.BufWrCount != 255) _Eeprom.BufWrCount++; //缓冲区内写累加
}

//---------------------------写Eeprom数据---------------------------------
void Eeprom_Wr(EepromAdr_t Adr,
               const void *pVoid,
               EepromLen_t Len)
{
  //先检查是否跨页,跨页里需分为两部分处理
  unsigned short InPageBase = Adr % FLASH_PAGE_SIZE;
  if((InPageBase + Len) > FLASH_PAGE_SIZE){//跨页了
    //分成两页操作 (暂禁止存储<2页的数据)
    EepromLen_t CurLen = FLASH_PAGE_SIZE - InPageBase;
    _WrInPage(Adr, pVoid, CurLen);
    _WrInPage(Adr + CurLen, (const char *)pVoid + CurLen, Len - CurLen);    
  }
  else _WrInPage(Adr, pVoid, Len);
}

//---------------------------读取Eeprom数据---------------------------------
void Eeprom_Rd(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len)
{
  //因写入FLASH时，外部已完成了对自身RAM的更新，故不检查缓冲区是否写入
  #ifdef Flash_Read
    Flash_Read(EEPROM_BASE + Adr, pVoid, Len);
  #else //普通读内存的方法
    memcpy(pVoid, (unsigned char*)(EEPROM_BASE + Adr), Len);
  #endif
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
  if(!_Eeprom.WrBackTimer) return; //没有回写需求
  _Eeprom.WrBackTimer--;
  if(_Eeprom.WrBackTimer) return;  //等待过程中
  //时间到执行回写
  _WrBack();
}

//--------------------------强制回写缓冲区数据---------------------------------
void Eeprom_ForceWrBuf(void)
{
  _WrBack();
  _Eeprom.WrBackTimer = 0;//不再等待
}

//--------------------------强制回写并重启---------------------------
void Eeprom_ForceWrBufAndRestart(void)
{
  _WrBack();  
  while(1);
}

/******************************************************************************
                               读写指针函数实现
******************************************************************************/

//-------------------------由Eeprom基址转换为只读指针------------------
//可用于只读数据，建议以结构方式进行以提高效率
const unsigned char *Eeprom_pGetRd(EepromAdr_t Adr, EepromLen_t Len)
{
  //先从当前EEPROM缓冲区里找
  #ifdef SUPPORT_EEPROM_WR_BUF
    if(_WrBuf.Len){//有数据缓冲时
      EepromAdr_t EndAdr = _WrBuf.EepromAdr + SUPPORT_EEPROM_WR_BUF;
      if((Adr >= _WrBuf.EepromAdr) && (Adr < EndAdr)){//起始在范围
        if((Adr + Len) <= EndAdr){//整组在范围了，命中了直接返回缓冲区
         return ((unsigned char *)_WrBuf.Buf) + (Adr - _WrBuf.EepromAdr); 
        }
        else{//部分命中时，先将现有数据入EEPROM缓冲区中
          Eeprom_Wr(_WrBuf.EepromAdr, _WrBuf.Buf, _WrBuf.Len);//可能在缓冲中
          _WrBuf.Len = 0;          
        }
      }
    }
  #endif
  //检查EEPOM缓冲区：命中时返回缓冲区，部分命中时需回写再读
  if(_Eeprom.WrBackTimer && (_Eeprom.CurPage != 0xFF)){//
    EepromAdr_t BufBase = _Eeprom.CurPage * FLASH_PAGE_SIZE;
    EepromAdr_t BufEnd = BufBase + FLASH_PAGE_SIZE;
    if((Adr >= BufBase) && (Adr < BufEnd)){//起始对了
      if((Adr + Len) <= BufEnd){//整组在范围了
        return ((unsigned char *)_Eeprom.Buf) + (Adr % FLASH_PAGE_SIZE); 
      }
      else _WrBack();//部分命中时,先回写,然后读只读区域
    }
  }
  
  return (const unsigned char *)(EEPROM_BASE +Adr);
}

//-------------------------由Eeprom基址转换为可写指针------------------
//可用于数据的读写，若只读不更改，应使用Eeprom_pGetRd()以节省写入次数和效率
//建议以结构方式进行以提高效率
unsigned char *Eeprom_pGetWr(EepromAdr_t Adr, EepromLen_t Len)
{
  #ifdef SUPPORT_EEPROM_WR_BUF
    if(_WrBuf.Len){//有数据缓冲时
      EepromAdr_t EndAdr = _WrBuf.EepromAdr + SUPPORT_EEPROM_WR_BUF;
      if((Adr >= _WrBuf.EepromAdr) && ((Adr + Len) <= EndAdr)){//全在范围，命中了
         if(_Eeprom.BufWrCount != 255) _Eeprom.BufWrCount++; //缓冲区内写累加  
         //直接返回缓冲区
         _Eeprom.WrBackTimer = EEPROM_WR_BACK_OV; //启动自动强制回写
         return ((unsigned char *)_WrBuf.Buf) + (Adr - _WrBuf.EepromAdr);   
      }
      //未命中或部分命中，先写入以释放EEPROM数据
     _WrBack();
    }
    //从当前位置缓冲新的数据
    const unsigned char *pRd = Eeprom_pGetRd(Adr, SUPPORT_EEPROM_WR_BUF);//确保数据是新的
    memcpy(_WrBuf.Buf, pRd, SUPPORT_EEPROM_WR_BUF);
    _WrBuf.EepromAdr = Adr;
    _WrBuf.Len = SUPPORT_EEPROM_WR_BUF;
    _Eeprom.WrBackTimer = EEPROM_WR_BACK_OV; //启动自动强制回写
    return (unsigned char *)_WrBuf.Buf; 
  #else
    return NULL;  //不支持
  #endif
}


