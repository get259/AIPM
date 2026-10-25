#include "car_uart.h"
#include "car_config.h"
#include "stm32f10x.h"

static volatile uint8_t rx_buffer[CAR_RX_CAPACITY];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static volatile bool rx_lost;

void CarUart_Init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef uart;
    NVIC_InitTypeDef irq;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &gpio);

    USART_StructInit(&uart);
    uart.USART_BaudRate = CAR_UART_BAUD;
    uart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    uart.USART_WordLength = USART_WordLength_8b;
    uart.USART_StopBits = USART_StopBits_1;
    uart.USART_Parity = USART_Parity_No;
    uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART1, &uart);
    rx_head = rx_tail = 0;
    rx_lost = false;
    irq.NVIC_IRQChannel = USART1_IRQn;
    irq.NVIC_IRQChannelPreemptionPriority = 1;
    irq.NVIC_IRQChannelSubPriority = 0;
    irq.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&irq);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART1, ENABLE);
    /* PA9 已配置为发送引脚；回传格式未约定，应用不主动发送。 */
}

void USART1_IRQHandler(void)
{
    const uint32_t status = USART1->SR;
    if ((status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE |
                   USART_SR_FE | USART_SR_PE)) != 0) {
        const uint8_t byte = (uint8_t)USART1->DR;
        if ((status & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) != 0) {
            rx_lost = true;
            return;
        }
        const uint16_t next = (uint16_t)((rx_head + 1U) % CAR_RX_CAPACITY);
        if (next == rx_tail || rx_lost) {
            rx_lost = true;
            return;
        }
        rx_buffer[rx_head] = byte;
        rx_head = next;
    }
}

bool CarUart_Read(uint8_t *byte, bool *data_lost)
{
    uint32_t previous_mask;
    bool available = false;
    /* 极短临界区，防止清理队列时与接收中断交错；恢复原中断状态。 */
    __asm volatile ("mrs %0, primask" : "=r" (previous_mask) :: "memory");
    __disable_irq();
    *data_lost = rx_lost;
    if (rx_lost) {
        rx_tail = rx_head;
        rx_lost = false;
    } else if (rx_tail != rx_head) {
        *byte = rx_buffer[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1U) % CAR_RX_CAPACITY);
        available = true;
    }
    __asm volatile ("msr primask, %0" :: "r" (previous_mask) : "memory");
    return available;
}
