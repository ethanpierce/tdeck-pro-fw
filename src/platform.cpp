#include "platform.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>
#include <driver/gpio.h>
#include <driver/i2c.h>
#include <driver/uart.h>
#include <driver/usb_serial_jtag.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace platform {
Console console;
Uart modemUart;
namespace {
constexpr uart_port_t ModemPort=UART_NUM_1;
constexpr i2c_port_t I2cPort=I2C_NUM_0;
constexpr spi_host_device_t SpiHost=SPI2_HOST;
SemaphoreHandle_t i2cMutex=nullptr;
bool i2cReady=false,spiReady=false;
struct I2cLock {
    bool locked=i2cMutex && xSemaphoreTake(i2cMutex,pdMS_TO_TICKS(100))==pdTRUE;
    ~I2cLock(){if(locked)xSemaphoreGive(i2cMutex);}
};
void configureGpio(int pin,gpio_mode_t mode,bool up=false,bool down=false){
    if(!GPIO_IS_VALID_GPIO(pin)){return;}
    gpio_config_t config{};
    config.pin_bit_mask=uint64_t(1)<<pin;
    config.mode=mode;
    config.pull_up_en=up?GPIO_PULLUP_ENABLE:GPIO_PULLUP_DISABLE;
    config.pull_down_en=down?GPIO_PULLDOWN_ENABLE:GPIO_PULLDOWN_DISABLE;
    config.intr_type=GPIO_INTR_DISABLE;
    const esp_err_t result=gpio_config(&config);
    if(result!=ESP_OK)ESP_LOGE("platform","GPIO%d configuration failed: %s",pin,esp_err_to_name(result));
}
}
uint32_t millis(){return uint32_t(esp_timer_get_time()/1000);}
bool begin(){console.begin();return bool(console);}
void delay(uint32_t ms){if(ms)vTaskDelay(std::max(TickType_t(1),pdMS_TO_TICKS(ms)));else taskYIELD();}
size_t freeHeap(){return heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);}
size_t minimumFreeHeap(){return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);}
size_t psramSize(){return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);}
size_t freePsram(){return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);}
[[noreturn]] void restart(){esp_restart();for(;;)vTaskDelay(portMAX_DELAY);}
void Console::begin(unsigned){
    if(ready_)return;
    usb_serial_jtag_driver_config_t config{};config.tx_buffer_size=4096;config.rx_buffer_size=2048;
    esp_err_t err=usb_serial_jtag_driver_install(&config);
    ready_=err==ESP_OK || err==ESP_ERR_INVALID_STATE;
}
int Console::available(){
    if(!ready_)return 0;
    if(pending_<0){uint8_t byte=0;if(usb_serial_jtag_read_bytes(&byte,1,0)==1)pending_=byte;}
    return pending_<0?0:1;
}
int Console::read(){if(!available())return -1;int value=pending_;pending_=-1;return value;}
size_t Console::write(const uint8_t* bytes,size_t size){
    if(!ready_||!bytes||!size)return 0;
    int sent=usb_serial_jtag_write_bytes(bytes,size,pdMS_TO_TICKS(20));return sent>0?size_t(sent):0;
}
size_t Console::write(const char* text){return text?write(reinterpret_cast<const uint8_t*>(text),std::strlen(text)):0;}
int Console::printf(const char* format,...){
    va_list args;va_start(args,format);va_list copy;va_copy(copy,args);
    int size=std::vsnprintf(nullptr,0,format,copy);va_end(copy);
    if(size<0||size>8192){va_end(args);return -1;}
    std::vector<char> bytes(size_t(size)+1);std::vsnprintf(bytes.data(),bytes.size(),format,args);va_end(args);
    return int(write(reinterpret_cast<const uint8_t*>(bytes.data()),size_t(size)));
}
bool Uart::begin(unsigned baud,int rx,int tx){
    if(ready_)return true;
    uart_config_t config{};config.baud_rate=int(baud);config.data_bits=UART_DATA_8_BITS;
    config.parity=UART_PARITY_DISABLE;config.stop_bits=UART_STOP_BITS_1;config.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;
    config.source_clk=UART_SCLK_DEFAULT;
    esp_err_t result=uart_param_config(ModemPort,&config);
    const char* step="parameters";
    if(result==ESP_OK){step="pin routing";result=uart_set_pin(ModemPort,tx,rx,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);}
    if(result==ESP_OK){step="driver installation";result=uart_driver_install(ModemPort,int(rxSize_),0,0,nullptr,0);}
    ready_=result==ESP_OK;
    if(!ready_)ESP_LOGE("platform","Modem UART %s failed (TX%d RX%d %u baud): %s",step,tx,rx,baud,esp_err_to_name(result));
    return ready_;
}
int Uart::available(){size_t n=0;if(ready_)uart_get_buffered_data_len(ModemPort,&n);return int(n);}
int Uart::read(){uint8_t byte=0;return ready_ && uart_read_bytes(ModemPort,&byte,1,0)==1?int(byte):-1;}
size_t Uart::write(const uint8_t* bytes,size_t size){if(!ready_||!bytes)return 0;int n=uart_write_bytes(ModemPort,bytes,size);return n>0?size_t(n):0;}
void gpioOutput(int pin,bool level){
    if(!GPIO_IS_VALID_OUTPUT_GPIO(pin)){return;}
    // Preload the latch before enabling output. gpio_config selects the GPIO
    // IOMUX function (unlike gpio_set_direction), including former JTAG pins.
    // Do not reset the pin, which would introduce a pull-up/disabled interval.
    gpio_set_level(gpio_num_t(pin),level);
    configureGpio(pin,GPIO_MODE_OUTPUT);
}
void gpioInput(int pin,bool up,bool down){configureGpio(pin,GPIO_MODE_INPUT,up,down);}
void gpioWrite(int pin,bool level){if(pin>=0)gpio_set_level(gpio_num_t(pin),level);}
int gpioRead(int pin){return pin<0?0:gpio_get_level(gpio_num_t(pin));}
bool i2cBegin(int sda,int scl,uint32_t hz){
    if(i2cReady)return true;
    if(!i2cMutex)i2cMutex=xSemaphoreCreateMutex();
    if(!i2cMutex)return false;
    i2c_config_t config{};config.mode=I2C_MODE_MASTER;config.sda_io_num=sda;config.scl_io_num=scl;
    config.sda_pullup_en=GPIO_PULLUP_ENABLE;config.scl_pullup_en=GPIO_PULLUP_ENABLE;config.master.clk_speed=hz;
    i2cReady=i2c_param_config(I2cPort,&config)==ESP_OK && i2c_driver_install(I2cPort,I2C_MODE_MASTER,0,0,0)==ESP_OK;
    return i2cReady;
}
bool i2cProbe(uint8_t address){
    I2cLock lock;if(!i2cReady||!lock.locked)return false;
    i2c_cmd_handle_t cmd=i2c_cmd_link_create();if(!cmd)return false;
    i2c_master_start(cmd);i2c_master_write_byte(cmd,uint8_t(address<<1),true);i2c_master_stop(cmd);
    esp_err_t result=i2c_master_cmd_begin(I2cPort,cmd,pdMS_TO_TICKS(30));i2c_cmd_link_delete(cmd);return result==ESP_OK;
}
bool i2cWrite(uint8_t a,const uint8_t* p,size_t n){I2cLock lock;return i2cReady&&lock.locked&&p&&n&&i2c_master_write_to_device(I2cPort,a,p,n,pdMS_TO_TICKS(30))==ESP_OK;}
bool i2cRead(uint8_t a,uint8_t* p,size_t n){I2cLock lock;return i2cReady&&lock.locked&&p&&n&&i2c_master_read_from_device(I2cPort,a,p,n,pdMS_TO_TICKS(30))==ESP_OK;}
bool i2cWriteRead(uint8_t a,const uint8_t* tx,size_t tn,uint8_t* rx,size_t rn){I2cLock lock;return i2cReady&&lock.locked&&tx&&tn&&rx&&rn&&i2c_master_write_read_device(I2cPort,a,tx,tn,rx,rn,pdMS_TO_TICKS(30))==ESP_OK;}
bool spiBegin(int sck,int miso,int mosi){
    if(spiReady)return true;
    spi_bus_config_t bus{};bus.mosi_io_num=mosi;bus.miso_io_num=miso;bus.sclk_io_num=sck;
    bus.quadwp_io_num=-1;bus.quadhd_io_num=-1;bus.max_transfer_sz=4096;
    spiReady=spi_bus_initialize(SpiHost,&bus,SPI_DMA_CH_AUTO)==ESP_OK;return spiReady;
}
spi_device_handle_t spiDevice(int cs,uint32_t hz,uint8_t mode){
    if(!spiReady){return nullptr;}
    spi_device_handle_t handle=nullptr;spi_device_interface_config_t config{};
    config.clock_speed_hz=int(hz);config.mode=mode;config.spics_io_num=cs;config.queue_size=1;
    return spi_bus_add_device(SpiHost,&config,&handle)==ESP_OK?handle:nullptr;
}
bool spiTransfer(spi_device_handle_t device,const uint8_t* tx,uint8_t* rx,size_t size){
    if(!device||!size||size>4096)return false;
    spi_transaction_t transaction{};transaction.length=size*8;transaction.tx_buffer=tx;transaction.rx_buffer=rx;
    return spi_device_polling_transmit(device,&transaction)==ESP_OK;
}
bool spiAcquire(spi_device_handle_t device){return device&&spi_device_acquire_bus(device,portMAX_DELAY)==ESP_OK;}
void spiRelease(spi_device_handle_t device){if(device)spi_device_release_bus(device);}
}
