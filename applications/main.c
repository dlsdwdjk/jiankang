#include "ds18b20.h"
#include "drv_common.h"
#include "rtdef.h"
#include "rtdbg.h"
#include "stm32f1xx.h"

/*
 * 函数名：DS18B20_Mode_Out_PP
 * 描述  ：使DS18B20-DATA引脚变为输出模式
 * 输入  ：无
 * 输出  ：无
 */
static void DS18B20_Mode_Out_PP(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

     /*选择要控制的macDS18B20_DQ_GPIO_PORT引脚*/
    GPIO_InitStructure.Pin = GPIO_PIN_13;
    /*设置引脚模式为通用推挽输出*/
    GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
    /*设置引脚速率为50MHz */
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
    /*调用库函数，初始化macDS18B20_DQ_GPIO_PORT*/
    HAL_GPIO_Init(GPIOC, &GPIO_InitStructure);
}

/*
 * 使DS18B20的数据引脚变为输入模式
 * */
static void DS18B20_Mode_IPU(void)
{
    GPIO_InitTypeDef GPIO_Initure;

    GPIO_Initure.Pin = GPIO_PIN_13;
    GPIO_Initure.Mode = GPIO_MODE_INPUT; //浮空模式

    HAL_GPIO_Init(GPIOC,&GPIO_Initure);
}

/*
 * 主机给DS18B20发送复位脉冲
 * */
static void DS18B20_Reset(void)
{
    /*主机设置为推挽输出*/
    DS18B20_Mode_Out_PP();
    /*主机控制产生至少480us的低电平复位信号*/
    DS18B20_DATA_0;
    rt_hw_us_delay(750);
    /*释放总线*/
    DS18B20_DATA_1;

    /*设备接收到复位信号后，会等待15-60us，然后开始给主机发送存在脉冲*/
    rt_hw_us_delay(15);
}

/*
 * 检测DS18B20存在脉冲
 * return: 0 成功检测设备存在
 *        -1 检测设备存在失败
 * */
static uint8_t DS18B20_Check(void)
{
    uint8_t pulse_time = 0;

    /*设置为上拉输入*/
    DS18B20_Mode_IPU();
    /*存在脉冲为一个60-240us的低电平信号*/
    /*如果没有接收到低电平存在信号就一直循环，直到pluse_time达到100*/
    while(HAL_GPIO_ReadPin(GPIOC,GPIO_PIN_13) && pulse_time<100)
    {
        pulse_time++;
        rt_hw_us_delay(1);
    }

    /*经过100us还没接收到存在信号*/
    if(pulse_time >= 100)
    {
        rt_kprintf("Receive presence pulse failed!\n");
        return -1;
    }
    else
    {
        pulse_time = 0;
    }

    /*检测到存在信号*/
    while(!DS18B20_DATA_READ() && pulse_time<240)
    {
        pulse_time++;
        rt_hw_us_delay(1);
    }
    if(pulse_time > 240)
    {
        rt_kprintf("presense pulse incorrect!\n");
        return -1;
    }
    else
    {
        rt_kprintf("Receive presence pulse successful!\n");
        return 0;
    }
}

/*
 * 写一个字节到DS18B20，低位先行
 * */
void DS18B10_WriteByte(uint8_t data)
{
    uint8_t i;
    uint8_t current_bit;

    /*循环发送8位数据*/
    for(i = 0; i < 8; i++)
    {
        current_bit = data & 0x01;    //取当前最低位
        data = data >> 1;

        /*写1时段，拉低后15us内必须释放*/
        if(current_bit)
        {
            DS18B20_DATA_0;
            rt_hw_us_delay(10);
            DS18B20_DATA_1;
            rt_hw_us_delay(58);
         }
        /*写0时段，拉低后整个时段期间主设备需一直拉低60-120us*/
        else
        {
            DS18B20_DATA_0;
            rt_hw_us_delay(70);
            DS18B20_DATA_1;

            /* 1us < 恢复时间 < 无穷大 */
            rt_hw_us_delay(2);
        }

    }

}

/*
 * 从DS18B20读取一个bit
 * */
uint8_t DS18B20_ReadBit(void)
{
    uint8_t data;

    DS18B20_Mode_Out_PP();

    /*读，需由主机产生1-15us的低电平*/
    DS18B20_DATA_0;
    rt_hw_us_delay(10);

    /*开始读bit*/
    /*设置为输入，释放总线，由外部上拉电阻将总线拉高*/
    DS18B20_Mode_IPU();

    if(DS18B20_DATA_READ() == 1)
    {
        data = 1;
    }
    else
    {
        data = 0;
    }

    rt_hw_us_delay(45);

    return data;
}

/*
 * 从DS18B20读取一个字节
 * */
uint8_t DS18B20_ReadByte(void)
{
    uint8_t i, j;
    uint8_t data = 0x00;

    for (i = 0; i<8; i++)
    {
        j = DS18B20_DATA_READ();
        data = data |(j << i);
    }
    return data;
}

/*
 * 跳过匹配ROM(总线仅一个设备时)
 * */
static void DS18B20_SkipRom(void)
{
    DS18B20_Reset();
    DS18B20_Check();
    DS18B10_WriteByte(0xCC);
}

/*
 *开始温度转换
 * */
void DS18B20_Start(void)
{
    DS18B20_Reset();
    DS18B20_Check();
    DS18B10_WriteByte(0xcc);
    DS18B10_WriteByte(0x44);
}


/*
 * 读取DS18B20温度值
 * */
float DS18B20_GetTemp_SkipRom(void)
{
    uint8_t TH,TL;
    short s_tem;
    float f_tem;

    DS18B20_SkipRom();
    DS18B10_WriteByte(0x44);  //温度转换

    DS18B20_SkipRom();
    DS18B10_WriteByte(0xBE);  //读取温度值

    TL = DS18B20_ReadByte();
    TH = DS18B20_ReadByte();

    s_tem = TH<<8;
    s_tem = s_tem|TL;

    if( s_tem < 0 )     /* 负温度 */
        f_tem = (~s_tem+1) * 0.0625;
    else
        f_tem = s_tem * 0.0625;

    return f_tem;
}

/*
 * 初始化DS18B20 IO口，同时检测DS18B20的存在
 * return: 1 初始化失败
 *         0 初始化成功
 * */
uint8_t DS18B20_Init(void)
{

    GPIO_InitTypeDef GPIO_Initure;

    rt_kprintf("Start to initialize ds18B20\n");
    /*GPIO初始化*/
    __HAL_RCC_GPIOC_CLK_ENABLE();         //开启GPIOC时钟

    GPIO_Initure.Pin=GPIO_PIN_13;
    GPIO_Initure.Mode=GPIO_MODE_OUTPUT_PP;
    GPIO_Initure.Pull=GPIO_PULLUP;
    GPIO_Initure.Speed=GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &GPIO_Initure);

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    rt_kprintf("Reset the ds18b20!\n");
    DS18B20_Reset();

    //rt_kprintf("Start to detect the ds18b20!\n"); //DS18B20初始化过程对时间敏感，在初始化过程谨慎使用耗时函数
    return DS18B20_Check();
}

static int Ds18b20Init(void)
{
    DS18B20_Init();
    return RT_EOK;
}\
MSH_CMD_EXPORT(Ds18b20Init,RT_NULL);
INIT_APP_EXPORT(Ds18b20Init);




