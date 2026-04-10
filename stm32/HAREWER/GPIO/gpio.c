#include "gpio.h"

#define LED_GPIO_PORT GPIOC
#define LED_GPIO_PIN  GPIO_Pin_13

void Gpio_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;            //定义结构体GPIO_InitStructure
	
  RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOB, ENABLE); // 使能PB端口时钟  
  GPIO_InitStructure.GPIO_Pin =   GPIO_Pin_7| GPIO_Pin_8;	  //PB7 PB8
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;     	//推挽，增大电流输出能力  
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;  //IO口速度
	GPIO_Init(GPIOB, &GPIO_InitStructure);          //GBIOB初始化  
	RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOA, ENABLE); // 使能PB端口时钟  
  GPIO_InitStructure.GPIO_Pin =   GPIO_Pin_1| GPIO_Pin_2;	  //PB7 PB8
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;     	//推挽，增大电流输出能力  
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;  //IO口速度
	GPIO_Init(GPIOA, &GPIO_InitStructure);          //GBIOB初始化  

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
  GPIO_InitStructure.GPIO_Pin = LED_GPIO_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
  GPIO_Init(LED_GPIO_PORT, &GPIO_InitStructure);

  Led_Off();
	
}

void Led_On(void)
{
  GPIO_ResetBits(LED_GPIO_PORT, LED_GPIO_PIN);
}

void Led_Off(void)
{
  GPIO_SetBits(LED_GPIO_PORT, LED_GPIO_PIN);
}

void Led_Toggle(void)
{
  if (GPIO_ReadOutputDataBit(LED_GPIO_PORT, LED_GPIO_PIN) == Bit_SET)
  {
    Led_On();
  }
  else
  {
    Led_Off();
  }
}
