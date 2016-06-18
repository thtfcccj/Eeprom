/***************************************************************************

             EERPOM存取位置标准化-具体应用实现模板

***************************************************************************/
#ifndef __INFO_项目名称_硬件版本号(可选)_H 
#define __INFO_项目名称_硬件版本号(可选)_H 

/**********************************************************************
                       各模块存储分配
**********************************************************************/

//UvSensor模块
struct _UvSensorInfoIni{
  unsigned char Reserved[8];//预留空间
};

//DA_Adj模块
struct _DA_AdjInfoIni{
  unsigned char Reserved[4];//预留空间
};

//ModbusRtuMng模块
struct _ModbusRtuMngInfoIni{
  unsigned char Reserved[8];//预留空间
};

/**********************************************************************
                        信息区结构定义
**********************************************************************/
typedef struct _InfoBase{
   //0址固定为初始化标志,为0x5a时表示已初始化,其它时为未使用
  unsigned char InitedFlag;
  struct _UvSensorInfoIni UvSensorInfoIni;
  struct _DA_AdjInfoIni DA_AdjInfoIni;
  struct _ModbusRtuMngInfoIni ModbusRtuMngInfoIni;
  
}InfoBase_t;

/**********************************************************************
                        相关基址操作宏
**********************************************************************/
//InfoBase初始化标志
#define InfoBase_GetInitedFlagBase() struct_offset(InfoBase_t,InitedFlag)

//UvSensor模块
#define UvSensor_GetInfoBase()  struct_offset(InfoBase_t, UvSensorInfoIni)

//DA_Adj模块
#define DA_Adj_GetInfoBase()  struct_offset(InfoBase_t, DA_AdjInfoIni)

//ModbusRtuMng模块
#define ModbusRtuMng_GetInfoBase()  struct_offset(InfoBase_t, ModbusRtuMngInfoIni)


#endif //#define __INFO_项目名称_硬件版本号(可选)_H 


