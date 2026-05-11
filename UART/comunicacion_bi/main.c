#define F_CPU 8000000UL
#define BAUD 9600
#define UBRR_VALUE ((F_CPU / (16UL * BAUD)) - 1)

#include <avr/io.h>

void uart_init(void)
{
    UBRRH = (unsigned char)(UBRR_VALUE >> 8);
    UBRRL = (unsigned char)UBRR_VALUE;

    UCSRB = (1 << RXEN) | (1 << TXEN);

    UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0);
}

void uart_send_char(char c)
{
    while (!(UCSRA & (1 << UDRE)));
    UDR = c;
}

char uart_receive_char(void)
{
    while (!(UCSRA & (1 << RXC)));
    return UDR;
}

void uart_send_string(const char *s)
{
    while (*s)
    {
        uart_send_char(*s++);
    }
}

int main(void)
{
    char dato;

    uart_init();

    uart_send_string("UART listo. Escribe una letra:\r\n");

    while (1)
    {
        dato = uart_receive_char();

        uart_send_string("Recibi: ");
        uart_send_char(dato);
        uart_send_string("\r\n");
    }
}