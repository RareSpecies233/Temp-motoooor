#include "stm32f10x.h"

#include "delay.h"
#include "gpio.h"
#include "moto.h"
#include "pwm.h"
#include "sys.h"
#include "usart.h"


 /**************************************************************************
作者：平衡小车之家
我的淘宝小店：http://shop114407458.taobao.com/
**************************************************************************/



//------------接线说明---------------
//TB6612丝印标识--------STM32F1主板引脚

//    PWMA    -----------    B6
//    AIN1    -----------    B8
//    AIN2    -----------    B7
//    STBY    -----------    5V
//    VM      -----------    5-12V（外接电源）
//    GND     -----------    GND  （外接电源）
//    VCC     -----------    5V   （逻辑电源）
//    GND     -----------    GND   （逻辑共地）
// 

//------------接线说明---------------

//TB6612丝印标识--------电机
//    AO1   ------------  电机线电源+
//    AO2   ------------  电机线电源-
//------------接线说明---------------

int val1 = 0;
int val2 = 0;

// 手写字符串转数字（不使用atoi，不报错）
// 支持负数的字符串转数字
int my_atoi(u8 *str)
{
    int num = 0;
    int sign = 1;  // 1=正数，-1=负数
    
    // 如果第一位是负号，标记负数
    if(*str == '-')
    {
        sign = -1;
        str++;
    }
    
    // 正常读取数字
    while(*str >= '0' && *str <= '9')
    {
        num = num * 10 + (*str - '0');
        str++;
    }
    
    return num * sign;  // 带上正负号返回
}


 int main(void)
 {	
	  u8 i;
    u8 j;
    u8 buf1[10];
    u8 buf2[10];
	  // 初始化串口 115200 波特率
    uart_init(115200);
   SystemInit(); //配置系统时钟为72M   
	 delay_init();    //延时函数初始化
   Gpio_Init();    //初始化gpio口B pin_7/pin_8
 
   pwm_int1(7199,0);      //初始化pwm输出 72000 000 /7199+1=10000 
   pwm_int2(7199,0);
	
  while(1)
	{
		if(USART_RX_STA & 0x8000)
        {
			val1=0;
			val2=0;
            for(i=0;i<10;i++){buf1[i]=0;buf2[i]=0;}
            i=0;j=0;
            
            while(USART_RX_BUF[i]!=',' && USART_RX_BUF[i]!=0)
            {
                buf1[i]=USART_RX_BUF[i];
                i++;
            }
            
            i++;
            
            while(USART_RX_BUF[i]!=0 && USART_RX_BUF[i]!=0x0d && USART_RX_BUF[i]!=0x0a)
            {
                buf2[j++]=USART_RX_BUF[i++];
            }
            
            val1=my_atoi(buf1);
            //val2=my_atoi(buf2);
			buf2[j] = 0;      // 手动加结束符，彻底清除残留！ 24                                          
            val2=my_atoi(buf2);
			buf2[j] = 0;
			//val2=0;
			if(val1 < 0)  
			{
				moto1(1),val1 = -val1;
			}
			else
			{
				moto1(0);
			}
            if(val1 > 2999)val1 = 3000;
            if(val2 < 0)  
			{
				moto2(1),val2 = -val2;
			}
			else
			{
				moto2(0);
			}
            if(val2 > 2999)val2 = 3000;
            
            USART_RX_STA=0;
        }

	 //moto(0); 		//moto=0时正转
	 TIM_SetCompare1(TIM4,val1);   //设置TIM4通道1的占空比  3000/7200
	 TIM_SetCompare1(TIM3,val2);
	
	
//	moto(1);                //moto=0时反转
//	TIM_SetCompare1(TIM4,4000);   //设置TIM4通道1的占空比  4000/7200
	}
 }

