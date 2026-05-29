#ifndef __UART_PUPPY_H__
#define __UART_PUPPY_H__

/*** UART Register offsets ***/
#define UART_STATUS 0x20
#define UART_SETUP  0x24
#define UART_ERROR  0x28
#define UART_IRQEN  0x2C
#define UART_VALID  0x30
#define UART_DATA   0x34

// Status bits
#define UART_TX_BUSY BIT(0)
#define UART_RX_BUSY BIT(1)

// Setup bits
#define UART_CLKDIV_SHIFT  16
#define UART_RXEN          BIT(9)
#define UART_TXEN          BIT(8)
#define UART_CLEAN_FIFO    BIT(5)
#define UART_POLL_EN       BIT(4)
#define UART_STOP_BITS     BIT(3)
#define UART_BIT_LEN_SHIFT 1
#define UART_PARITY_EN     BIT(0)

// IRQEN bits
#define UART_RXIRQEN  BIT(0)
#define UART_ERRIRQEN BIT(1)

#define UART_PUPPY_MAX_BUFFER_SIZE 64

#endif // __UART_PUPPY_H__
