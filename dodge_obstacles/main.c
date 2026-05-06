#define F_CPU 8000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// pantalla LCD
#define LCD_PORT PORTB
#define LCD_DDR DDRB
#define LCD_RST PB0
#define LCD_CE PB4
#define LCD_DC PB1
#define LCD_DIN PB5
#define LCD_CLK PB7

// botones
#define BTN_PORT PORTD
#define BTN_PIN PIND
#define BTN_DDR DDRD
#define BTN_UP PD0
#define BTN_DOWN PD1
#define BTN_LEFT PD2
#define BTN_RIGHT PD3
#define BTN_STOP PD4
# define BTN_MASK ((1 << BTN_UP) | (1 << BTN_DOWN) | (1 << BTN_LEFT) | (1 << BTN_RIGHT) | (1 << BTN_STOP))

#define INPUT_POLL_MS 10
#define GAME_TICK_MS 180
#define BTN_DEBOUNCE_TICKS 2

#define LCD_WIDTH   84
#define LCD_HEIGHT  48
static uint8_t lcd_buffer[504];

#define CELL_SIZE   4
#define GRID_W (LCD_WIDTH / CELL_SIZE)   // 21
#define GRID_H (LCD_HEIGHT / CELL_SIZE)  // 12

typedef struct {
    uint8_t x;
    uint8_t y;
} Point;

static void lcd_ce_low(void) { 
    LCD_PORT &= ~(1 << LCD_CE); 
}
static void lcd_ce_high(void) { 
    LCD_PORT |=  (1 << LCD_CE); 
}
static void lcd_dc_cmd(void){ 
    LCD_PORT &= ~(1 << LCD_DC); 
}
static void lcd_dc_data(void){
    LCD_PORT |=  (1 << LCD_DC); 
}
static void lcd_rst_low(void){ 
    LCD_PORT &= ~(1 << LCD_RST); 
}
static void lcd_rst_high(void){ 
    LCD_PORT |=  (1 << LCD_RST); 
}

static void lcd_clk_low(void){ 
    LCD_PORT &= ~(1 << LCD_CLK); 
}
static void lcd_clk_high(void){ 
    LCD_PORT |=  (1 << LCD_CLK); 
}

static void lcd_din_low(void){ 
    LCD_PORT &= ~(1 << LCD_DIN); 
}
static void lcd_din_high(void){
    LCD_PORT |=  (1 << LCD_DIN); 
}

static void lcd_send_byte(uint8_t data, uint8_t is_data) {
    if (is_data) {
        lcd_dc_data();
    } else {
        lcd_dc_cmd();
    }

    lcd_ce_low();

    for (uint8_t i = 0; i < 8; i++) {
        if (data & 0x80){
            lcd_din_high();
        } else {             
            lcd_din_low();
        }

        lcd_clk_high();
        _delay_us(1);
        lcd_clk_low();
        _delay_us(1);

        data <<= 1;
    }

    lcd_ce_high();
}

static void lcd_command(uint8_t cmd) {
    lcd_send_byte(cmd, 0);
}

static void lcd_data(uint8_t data) {
    lcd_send_byte(data, 1);
}

static void lcd_init(void) {
    // Salidas
    LCD_DDR |= (1 << LCD_RST) | (1 << LCD_CE) | (1 << LCD_DC) | (1 << LCD_DIN) | (1 << LCD_CLK);

    lcd_ce_high();
    lcd_clk_low();
    lcd_rst_high();

    // Reset hardware
    lcd_rst_low();
    _delay_ms(10);
    lcd_rst_high();

    // Inicialización PCD8544
    lcd_command(0x21); // extended instruction set
    lcd_command(0xBF); // contraste
    lcd_command(0x04); // temp coefficient
    lcd_command(0x14); // bias mode
    lcd_command(0x20); // basic instruction set
    lcd_command(0x0C); // normal display mode

    memset(lcd_buffer, 0x00, sizeof(lcd_buffer));
}

static void lcd_clear_buffer(void) {
    memset(lcd_buffer, 0x00, sizeof(lcd_buffer));
}

static void lcd_update(void) {
    lcd_command(0x40); // Y = 0
    lcd_command(0x80); // X = 0

    for (uint16_t i = 0; i < sizeof(lcd_buffer); i++) {
        lcd_data(lcd_buffer[i]);
    }
}

static void lcd_set_pixel(uint8_t x, uint8_t y, uint8_t color) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;

    uint16_t index = x + (y / 8) * LCD_WIDTH;

    if (color) {
        lcd_buffer[index] |= (1 << (y % 8));
    } else {
        lcd_buffer[index] &= ~(1 << (y % 8));
    }
}

static void draw_border(void) {
    for (uint8_t x = 0; x < LCD_WIDTH; x++) {
        lcd_set_pixel(x, 0, 1);
        lcd_set_pixel(x, LCD_HEIGHT - 1, 1);
    }
    for (uint8_t y = 0; y < LCD_HEIGHT; y++) {
        lcd_set_pixel(0, y, 1);
        lcd_set_pixel(LCD_WIDTH - 1, y, 1);
    }
}

static void game_draw(void) {
    lcd_clear_buffer();

    // Opcional: borde
    draw_border();

    lcd_update();
}

int main(void){
    lcd_init();
    game_draw();

    return 0;
}