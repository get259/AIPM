#ifndef CAR_UART_H
#define CAR_UART_H
#include <stdbool.h>
#include <stdint.h>
void CarUart_Init(void);
/* 仅供主循环调用；发生数据丢失时告知调用方重置解析器。 */
bool CarUart_Read(uint8_t *byte, bool *data_lost);
#endif
