#define F_CPU 8000000UL
#define BAUD 9600
#define UBRR_VALUE ((F_CPU / (16UL * BAUD)) - 1)

#include <avr/io.h>
#include <util/delay.h>

void uart_init(void)
{
    UBRRH = (unsigned char)(UBRR_VALUE >> 8);
    UBRRL = (unsigned char)UBRR_VALUE;

    UCSRB = (1 << TXEN);

    UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0);
}

void uart_send_char(char c)
{
    while (!(UCSRA & (1 << UDRE)));

    UDR = c;
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
    uart_init();

    while (1)
    {
        uart_send_string("UART OK\r\n");

        _delay_ms(1000);
    }
}