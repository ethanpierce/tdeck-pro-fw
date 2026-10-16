#pragma once
#include <cstddef>
#include <cstdint>
using uart_port_t=int;
constexpr int UART_NUM_2=2,ESP_OK=0,UART_PIN_NO_CHANGE=-1,UART_DATA_8_BITS=8,UART_PARITY_DISABLE=0,UART_STOP_BITS_1=1,UART_HW_FLOWCTRL_DISABLE=0,UART_SCLK_DEFAULT=0;
struct uart_config_t {int baud_rate=0,data_bits=0,parity=0,stop_bits=0,flow_ctrl=0,source_clk=0;};
int uart_driver_install(uart_port_t,int,int,int,void*,int);
int uart_driver_delete(uart_port_t);
int uart_param_config(uart_port_t,const uart_config_t*);
int uart_set_pin(uart_port_t,int,int,int,int);
int uart_read_bytes(uart_port_t,uint8_t*,size_t,unsigned);
int uart_set_baudrate(uart_port_t,uint32_t);
int uart_flush_input(uart_port_t);
