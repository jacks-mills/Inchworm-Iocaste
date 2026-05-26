#ifndef __UART_H__
#define __UART_H__

#define MSG_SIZE 128
extern struct k_msgq uart_msgq;

int uart_init(void);

#endif /* __UART_H__ */
