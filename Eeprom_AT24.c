/***************************************************************************

         EERPOM存取位置标准化接口-在24系列中的实现
//通过配置,支持从24C01~24C512中的所有驱动(部分芯片未测试!!!!)
//使用I2c标准接口的驱动), 针对不同的24系列,只需少量配置即可实现
***************************************************************************/

/***************************************************************************
                        用户配置说明
//请在预编译中根据下述说明做定义,(24x中的x指不同系列)
***************************************************************************/
//针对当前项目支持的芯片,必须定义下述中的一个
//#define SUPPORT_AT24x01
//#define SUPPORT_AT24x02
//#define SUPPORT_AT24x04
//#define SUPPORT_AT24x08
//#define SUPPORT_AT24x16
//#define SUPPORT_AT24x32
//#define SUPPORT_AT24x64
//#define SUPPORT_AT24x128
//#define SUPPORT_AT24x256
//#define SUPPORT_AT24x512

//指定使用其它I2C硬件设备,默认放在I2c.h里的 (&I2c.I2cDev0)
//定义后,请在Eeprom_AT24_Pri.h里实现:Eeprom_At24_pGetI2cDev()
//#define AT24_OTHER_I2C_DEV

//接的硬件地址A0,A1,A2,不定义时表示全部接地,否则需定义为1~7
//#define AT24_HW_ADR

//I2C通讯时的等待时间,超时通读不上,I2c_Task()调用周期为单位,不定义是为10
//#define AT24_WAIT_OV

//I2C写Eeprom区块时,等待时间,直接调用系统硬件延时
//默认使用Delay.h中的DelayMs(), 时间为100mS
//定义后,请在Eeprom_AT24_Pri.h里实现:Eeprom_At24_pGetWrDelay()
//#define AT24_OTHER_WR_DELAY

signed char Eeprom_Err = 0;  //标识最后一次数读写错误
//外部需要访问时,在请在Eeprom_AT24_Pri.h里实现声明

/***************************************************************************
                             内部宏转义
***************************************************************************/

#ifndef AT24_HW_ADR
  #define AT24_HW_ADR  0 //连接的硬件地址A0,A1,A2,默认全部接地,否则需定义
#endif

#ifndef AT24_WAIT_OV
  #define AT24_WAIT_OV  10
#endif

#ifndef AT24_OTHER_I2C_DEV
  #include "I2c.h"
  #define       Eeprom_pGetI2cDev()      (&I2c.I2cDev[0])
#else
  #include "Eeprom_AT24_Pri.h.h" //用户自已实现
#endif 

#ifndef AT24_OTHER_WR_DELAY
  #include "Delay.h"
  #define Eeprom_cbDelayWr() do{DelayMs(100);}while(0)
#else
  #include "Eeprom_AT24_Pri.h.h" //用户自已实现
#endif 

/***************************************************************************
                 内部宏转义-针对不同芯片部分
***************************************************************************/
#ifdef SUPPORT_AT24x01
  #define _CMD_BUF_SIZE    1   //命令字长
  #define _GetI2cAdr(adr)     ((0xA0 | AT24_HW_ADR) >> 1)   //硬件地址A0,A1,A2
  #define _GetBlockSize()  8   //一次性可读取的区块长度
#endif

#ifdef SUPPORT_AT24x02
  #define _CMD_BUF_SIZE    1   //命令字长
  #define _GetI2cAdr(adr)     ((0xA0 | AT24_HW_ADR) >> 1)   //硬件地址A0,A1,A2
  #define _GetBlockSize()  16  //一次性可读取的区块长度
#endif

#ifdef SUPPORT_AT24x04
  #define _CMD_BUF_SIZE    1   //命令字长
  #define _GetI2cAdr(adr)  ((0xA0 | AT24_HW_ADR | (((adr) >> 8) & 0x01))\
                            >> 1)   //硬件地址A1,A2
  #define _GetBlockSize()  16  //一次性可读取的区块长度
#endif

#ifdef SUPPORT_AT24x08
  #define _CMD_BUF_SIZE    1   //命令字长
  #define _GetI2cAdr(adr)  ((0xA0 | AT24_HW_ADR | (((adr) >> 8) & 0x03))\
                            >> 1)   //硬件地址A2
  #define _GetBlockSize()  16  //一次性可读取的区块长度
#endif

#ifdef SUPPORT_AT24x16
  #define _CMD_BUF_SIZE    1   //命令字长
  #define _GetI2cAdr(adr)  ((0xA0 | (((adr) >> 8) & 0x07)) >> 1)   //无硬件地址
  #define _GetBlockSize()  16  //一次性可读取的区块长度
#endif

#ifdef SUPPORT_AT24x32
  #define _CMD_BUF_SIZE    2   //命令字长
  #define _GetI2cAdr(adr)     ((0xA0 | AT24_HW_ADR) >> 1)   //硬件地址A0,A1,A2
  #define _GetBlockSize()  32   //一次性可读取的区块长度
#endif

#ifdef SUPPORT_AT24x64
  #define _CMD_BUF_SIZE    2   //命令字长
  #define _GetI2cAdr(adr)     ((0xA0 | AT24_HW_ADR) >> 1)   //硬件地址A0,A1,A2
  #define _GetBlockSize()  32   //一次性可读取的区块长度
#endif

#ifdef SUPPORT_AT24x128
  #define _CMD_BUF_SIZE    2   //命令字长
  #define _GetI2cAdr(adr)     ((0xA0 | AT24_HW_ADR) >> 1)   //硬件地址A0,A1,A2
  #define _GetBlockSize()  64   //一次性可读取的区块长度
#endif

#ifdef SUPPORT_AT24x256
  #define _CMD_BUF_SIZE    2   //命令字长
  #define _GetI2cAdr(adr)     ((0xA0 | AT24_HW_ADR) >> 1)   //硬件地址A0,A1
  #define _GetBlockSize()  64   //一次性可读取的区块长度
#endif

#ifdef SUPPORT_AT24x512
  #define _CMD_BUF_SIZE    2   //命令字长
  #define _GetI2cAdr(adr)     ((0xA0 | AT24_HW_ADR) >> 1)   //硬件地址A0,A1
  #define _GetBlockSize()  128  //一次性可读取的区块长度
#endif

//#ifdef SUPPORT_AT24x1024 ->因存储位置对齐没单独处理,故暂不提供支持
//  #define _CMD_BUF_SIZE    2   //命令字长
//  #define _GetI2cAdr(adr)     ((0xA0 | AT24_HW_ADR) >> 1)   //硬件地址A1
//  #define _GetBlockSize()  128  //一次性可读取的区块长度
//#endif

/***************************************************************************
                           相关函数实现
***************************************************************************/

#include "Eeprom.h"
#include "I2cDev.h"   //建立在该驱动之上
#include <string.h>

static I2cData_t Eeprom_I2cData;       //定义一个实例化命令
static unsigned char Eeprom_CmdBuf[_CMD_BUF_SIZE];//固定用命令缓冲区

//-----------------------------初始化函数---------------------------------
void Eeprom_Init(void)
{
  //memset(&Eeprom_I2cData,0,sizeof(I2cData_t));
  //命令单独预初始化一直保持不变的部分:
  Eeprom_I2cData.CmdSize = _CMD_BUF_SIZE;    //通讯过程中保持固定
  Eeprom_I2cData.pCmd = Eeprom_CmdBuf;
}

//---------------------------读取Eeprom数据---------------------------------
void Eeprom_Rd(EepromAdr_t Adr,   //Eerpom中的存取位置
               void *pVoid,       //读回数据的起始位置
               EepromLen_t Len)  //读取长度
{
  Eeprom_I2cData.Flag = I2C_CMD_RD | AT24_WAIT_OV;
  unsigned char *pByteData = (unsigned char *)pVoid;
  unsigned char CurSize;

  Eeprom_Err = 0; //预置正确
  
  //因I2C驱动程序限制最大数据为255,故需分区块读
  while(Len){
    //判断这次要读字节数
    if(Len < 255)CurSize = Len;//判断地址是否越过页写整数地址
    else CurSize = 255;

    Eeprom_I2cData.DataSize = CurSize;
    Eeprom_I2cData.pData = pByteData;
    Eeprom_I2cData.SlvAdr = _GetI2cAdr(Adr);  
    #if _CMD_BUF_SIZE > 1
      Eeprom_CmdBuf[1] = (unsigned char)Adr;//低8位地址
      Eeprom_CmdBuf[0] = (unsigned char)(Adr >> 8);//高8位地址
    #else
      Eeprom_CmdBuf[0] = (unsigned char)Adr;//低8位地址
    #endif
    //Eeprom_I2cData.pCmd = &Eeprom_CmdBuf[0]; //挂接命令

    I2cDev_ReStart(Eeprom_pGetI2cDev(),&Eeprom_I2cData);
    while(!I2cDev_IsEnd(Eeprom_pGetI2cDev()));
    //故障处理
    if(I2cDev_eGetSatate(Eeprom_pGetI2cDev()) != eI2cDone)
      Eeprom_Err = -1;
    //下一区块
    Len -= CurSize;
    Adr += CurSize;
    pByteData += CurSize;
    //这里看门狗处理
  }
}

//---------------------------写入Eeprom数据---------------------------------
void Eeprom_Wr(EepromAdr_t Adr,   //Eerpom中的存取位置
               const void *pVoid, //被写数据的起始位置
               EepromLen_t Len)   //写入长度
{
  Eeprom_I2cData.Flag = I2C_CMD_WR | AT24_WAIT_OV;
  unsigned char *pByteData = (unsigned char *)pVoid;
  unsigned short i;
  unsigned char CurSize;
  
  Eeprom_Err = 0; //预置正确
  
  //分区块写
  while(Len){
    //判断这次要写字节数
    i = _GetBlockSize() - (Adr % _GetBlockSize());
    if(Len < i)CurSize = Len;//判断地址是否越过页写整数地址
    else
      CurSize = i;

    Eeprom_I2cData.DataSize = CurSize;
    Eeprom_I2cData.pData = pByteData;
    Eeprom_I2cData.SlvAdr = _GetI2cAdr(Adr);  
    #if _CMD_BUF_SIZE > 1
      Eeprom_CmdBuf[1] = (unsigned char)Adr;//低8位地址
      Eeprom_CmdBuf[0] = (unsigned char)(Adr >> 8);//高8位地址
    #else
      Eeprom_CmdBuf[0] = (unsigned char)Adr;//低8位地址
    #endif
    //Eeprom_I2cData.pCmd = &Eeprom_CmdBuf[0]; //挂接命令

    I2cDev_ReStart(Eeprom_pGetI2cDev(),&Eeprom_I2cData);
    while(!I2cDev_IsEnd(Eeprom_pGetI2cDev()));
    Eeprom_cbDelayWr();//写延时
    //故障处理
    if(I2cDev_eGetSatate(Eeprom_pGetI2cDev()) != eI2cDone)
      Eeprom_Err = -1;
    //下一区块
    Len -= CurSize;
    Adr += CurSize;
    pByteData += CurSize;
    //这里看门狗处理
  }
  Eeprom_Err = 0;
}

//--------------------------Eeprom写常量数据--------------------------
//此函数为附加功能, 部分驱动实现
void Eeprom_WrConst(EepromAdr_t Adr,   //Eerpom中的存取位置
                  unsigned char Data,         //常量数据
                  EepromLen_t Len)            //写入长度
{
  unsigned short CurSize;
  static unsigned char DataBuf[_GetBlockSize()];
  memset(DataBuf, Data, _GetBlockSize());
  do{
    if(Len > _GetBlockSize()) CurSize = _GetBlockSize();
    else CurSize = Len;
    Eeprom_Wr(Adr,DataBuf,CurSize);
    Len -= CurSize;
    Adr += CurSize;
  }while(Len);
}
