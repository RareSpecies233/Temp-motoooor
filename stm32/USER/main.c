#include "stm32f10x.h"

#include <string.h>
#include <stdio.h>

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


#define MOTOR_PWM_MAX 3000

typedef struct
{
    int has_led_idle;
    int led_idle;

    int has_m1_dir;
    int m1_dir;
    int has_m1_speed;
    int m1_speed;
    int has_m1_duration;
    int m1_duration_ms;

    int has_m2_dir;
    int m2_dir;
    int has_m2_speed;
    int m2_speed;
    int has_m2_duration;
    int m2_duration_ms;
} ControlCommand;

static int g_led_idle_on = 0;
static int g_motor1_dir = 0;
static int g_motor2_dir = 0;
static int g_motor1_speed = 0;
static int g_motor2_speed = 0;
static long g_motor1_remain_ms = -1;
static long g_motor2_remain_ms = -1;

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

static int char_to_lower(int c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c - 'A' + 'a';
    }
    return c;
}

static int str_ieq(const char *a, const char *b)
{
    while (*a != 0 && *b != 0)
    {
        if (char_to_lower((int)*a) != char_to_lower((int)*b))
        {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == 0 && *b == 0);
}

static int is_json_space(char c)
{
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static int json_find_value(const char *json, const char *key, const char **value_pos)
{
    char key_token[40];
    unsigned int i;
    const char *p;

    if (json == 0 || key == 0 || value_pos == 0)
    {
        return 0;
    }

    key_token[0] = '"';
    for (i = 0; key[i] != 0 && i < (sizeof(key_token) - 3); i++)
    {
        key_token[i + 1] = key[i];
    }
    key_token[i + 1] = '"';
    key_token[i + 2] = 0;

    p = strstr(json, key_token);
    if (p == 0)
    {
        return 0;
    }

    p += (i + 2);
    while (*p != 0 && *p != ':')
    {
        p++;
    }
    if (*p != ':')
    {
        return 0;
    }

    p++;
    while (*p != 0 && is_json_space(*p))
    {
        p++;
    }

    if (*p == 0)
    {
        return 0;
    }

    *value_pos = p;
    return 1;
}

static int json_get_int(const char *json, const char *key, int *out_value)
{
    const char *p;
    int sign = 1;
    int value = 0;
    int has_digit = 0;

    if (!json_find_value(json, key, &p))
    {
        return 0;
    }

    if (*p == '-')
    {
        sign = -1;
        p++;
    }

    while (*p >= '0' && *p <= '9')
    {
        value = value * 10 + (*p - '0');
        has_digit = 1;
        p++;
    }

    if (!has_digit)
    {
        return 0;
    }

    *out_value = value * sign;
    return 1;
}

static int json_get_bool(const char *json, const char *key, int *out_value)
{
    const char *p;

    if (!json_find_value(json, key, &p))
    {
        return 0;
    }

    if ((strncmp(p, "true", 4) == 0) || (strncmp(p, "TRUE", 4) == 0) || (*p == '1'))
    {
        *out_value = 1;
        return 1;
    }

    if ((strncmp(p, "false", 5) == 0) || (strncmp(p, "FALSE", 5) == 0) || (*p == '0'))
    {
        *out_value = 0;
        return 1;
    }

    return 0;
}

static int json_get_string(const char *json, const char *key, char *out_value, unsigned int out_size)
{
    const char *p;
    unsigned int idx = 0;

    if (!json_find_value(json, key, &p))
    {
        return 0;
    }

    if (*p != '"' || out_size == 0)
    {
        return 0;
    }

    p++;
    while (*p != 0 && *p != '"' && idx < (out_size - 1))
    {
        out_value[idx++] = *p;
        p++;
    }

    out_value[idx] = 0;
    return (idx > 0);
}

static int parse_direction(const char *value, int *dir_out)
{
    if (str_ieq(value, "forward") || str_ieq(value, "fwd") || str_ieq(value, "cw") || str_ieq(value, "0"))
    {
        *dir_out = 0;
        return 1;
    }

    if (str_ieq(value, "reverse") || str_ieq(value, "rev") || str_ieq(value, "ccw") || str_ieq(value, "1"))
    {
        *dir_out = 1;
        return 1;
    }

    return 0;
}

static int json_get_direction(const char *json, const char *key, int *dir_out)
{
    int int_dir;
    char text_dir[20];

    if (json_get_int(json, key, &int_dir))
    {
        *dir_out = (int_dir != 0) ? 1 : 0;
        return 1;
    }

    if (json_get_string(json, key, text_dir, sizeof(text_dir)))
    {
        return parse_direction(text_dir, dir_out);
    }

    return 0;
}

static void command_reset(ControlCommand *cmd)
{
    memset(cmd, 0, sizeof(ControlCommand));
}

static int parse_control_json(const char *json, ControlCommand *cmd)
{
    int value;
    int any_field = 0;

    if (json_get_bool(json, "led_idle", &value) || json_get_int(json, "led_idle", &value))
    {
        cmd->has_led_idle = 1;
        cmd->led_idle = (value != 0) ? 1 : 0;
        any_field = 1;
    }

    if (json_get_direction(json, "m1_dir", &value) || json_get_direction(json, "m1_direction", &value))
    {
        cmd->has_m1_dir = 1;
        cmd->m1_dir = value;
        any_field = 1;
    }

    if (json_get_int(json, "m1_speed", &value))
    {
        cmd->has_m1_speed = 1;
        cmd->m1_speed = value;
        any_field = 1;
    }

    if (json_get_int(json, "m1_duration_ms", &value) || json_get_int(json, "m1_time_ms", &value))
    {
        cmd->has_m1_duration = 1;
        cmd->m1_duration_ms = value;
        any_field = 1;
    }

    if (json_get_direction(json, "m2_dir", &value) || json_get_direction(json, "m2_direction", &value))
    {
        cmd->has_m2_dir = 1;
        cmd->m2_dir = value;
        any_field = 1;
    }

    if (json_get_int(json, "m2_speed", &value))
    {
        cmd->has_m2_speed = 1;
        cmd->m2_speed = value;
        any_field = 1;
    }

    if (json_get_int(json, "m2_duration_ms", &value) || json_get_int(json, "m2_time_ms", &value))
    {
        cmd->has_m2_duration = 1;
        cmd->m2_duration_ms = value;
        any_field = 1;
    }

    return any_field;
}

static void apply_command(const ControlCommand *cmd)
{
    if (cmd->has_led_idle)
    {
        g_led_idle_on = cmd->led_idle;
    }

    if (cmd->has_m1_dir)
    {
        g_motor1_dir = cmd->m1_dir;
    }

    if (cmd->has_m2_dir)
    {
        g_motor2_dir = cmd->m2_dir;
    }

    if (cmd->has_m1_speed)
    {
        g_motor1_speed = clamp_int(cmd->m1_speed, 0, MOTOR_PWM_MAX);
        if (g_motor1_speed == 0)
        {
            g_motor1_remain_ms = 0;
        }
    }

    if (cmd->has_m2_speed)
    {
        g_motor2_speed = clamp_int(cmd->m2_speed, 0, MOTOR_PWM_MAX);
        if (g_motor2_speed == 0)
        {
            g_motor2_remain_ms = 0;
        }
    }

    if (cmd->has_m1_duration)
    {
        if (cmd->m1_duration_ms > 0)
        {
            g_motor1_remain_ms = cmd->m1_duration_ms;
        }
        else
        {
            g_motor1_remain_ms = -1;
        }
    }

    if (cmd->has_m2_duration)
    {
        if (cmd->m2_duration_ms > 0)
        {
            g_motor2_remain_ms = cmd->m2_duration_ms;
        }
        else
        {
            g_motor2_remain_ms = -1;
        }
    }
}

static void apply_outputs(void)
{
    if (g_motor1_speed > 0)
    {
        moto1(g_motor1_dir);
    }
    TIM_SetCompare1(TIM4, g_motor1_speed);

    if (g_motor2_speed > 0)
    {
        moto2(g_motor2_dir);
    }
    TIM_SetCompare1(TIM3, g_motor2_speed);

    if (g_led_idle_on)
    {
        Led_On();
    }
    else
    {
        Led_Off();
    }
}

static void update_motor_duration_1ms(void)
{
    if (g_motor1_remain_ms > 0)
    {
        g_motor1_remain_ms--;
        if (g_motor1_remain_ms == 0)
        {
            g_motor1_speed = 0;
        }
    }

    if (g_motor2_remain_ms > 0)
    {
        g_motor2_remain_ms--;
        if (g_motor2_remain_ms == 0)
        {
            g_motor2_speed = 0;
        }
    }
}

static void blink_led_for_uart(void)
{
    Led_On();
    delay_ms(70);
    Led_Off();
    delay_ms(70);
    if (g_led_idle_on)
    {
        Led_On();
    }
    else
    {
        Led_Off();
    }
}

static void send_json_reply(int ok, const char *code, const char *cn_msg, const char *en_msg)
{
    printf("{\"ok\":%s,\"code\":\"%s\",\"message_cn\":\"%s\",\"message_en\":\"%s\"}\r\n",
        ok ? "true" : "false",
        code,
        cn_msg,
        en_msg);
}

static void handle_uart_command(void)
{
    u16 rx_len;
    char json_cmd[USART_REC_LEN];
    ControlCommand cmd;

    if ((USART_RX_STA & 0x8000) == 0)
    {
        return;
    }

    rx_len = USART_RX_STA & 0x3FFF;
    if (rx_len >= USART_REC_LEN)
    {
        rx_len = USART_REC_LEN - 1;
    }

    memcpy(json_cmd, USART_RX_BUF, rx_len);
    json_cmd[rx_len] = 0;
    USART_RX_STA = 0;

    blink_led_for_uart();

    command_reset(&cmd);
    if (parse_control_json(json_cmd, &cmd))
    {
        apply_command(&cmd);
        send_json_reply(1, "ok", "执行成功", "success");
    }
    else
    {
        send_json_reply(0, "parse_error", "JSON解析失败", "json parse failed");
    }
}

static void led_boot_sequence(void)
{
    u8 i;

    Led_On();
    delay_ms(5000);

    for (i = 0; i < 3; i++)
    {
        Led_Off();
        delay_ms(200);
        Led_On();
        delay_ms(200);
    }

    Led_Off();
}


 int main(void)
 {	
      // 初始化串口 115200 波特率
      uart_init(115200);
   SystemInit(); //配置系统时钟为72M
     delay_init();    //延时函数初始化
   Gpio_Init();    //初始化GPIO（电机方向与LED）

   pwm_int1(7199,0);      //初始化pwm输出 72000 000 /7199+1=10000
   pwm_int2(7199,0);

     TIM_SetCompare1(TIM4, 0);
     TIM_SetCompare1(TIM3, 0);

     led_boot_sequence();

  while(1)
    {
        handle_uart_command();
        update_motor_duration_1ms();
        apply_outputs();
        delay_ms(1);
    }
 }

