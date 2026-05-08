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

#define BTN_MASK ((1 << BTN_UP) | (1 << BTN_DOWN) | (1 << BTN_LEFT) | (1 << BTN_RIGHT) | (1 << BTN_STOP))

#define LCD_WIDTH   84
#define LCD_HEIGHT  48
static uint8_t lcd_buffer[504];

// Juego
#define PADDLE_Y 43
#define PADDLE_W 16
#define PADDLE_H 2

#define BALL_SIZE 2

#define BLOCK_ROWS 4
#define BLOCK_COLS 8
#define BLOCK_W 9
#define BLOCK_H 4
#define BLOCK_START_X 5
#define BLOCK_START_Y 6

uint8_t paddle_x;
int8_t ball_x;
int8_t ball_y;
int8_t ball_dx;
int8_t ball_dy;

uint8_t blocks[BLOCK_ROWS][BLOCK_COLS];
uint8_t game_over;
uint8_t win;

// ---------- LCD ----------

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
    LCD_DDR |= (1 << LCD_RST) | (1 << LCD_CE) | (1 << LCD_DC) | (1 << LCD_DIN) | (1 << LCD_CLK);

    lcd_ce_high();
    lcd_clk_low();
    lcd_rst_high();

    lcd_rst_low();
    _delay_ms(10);
    lcd_rst_high();

    lcd_command(0x21);
    lcd_command(0xBF);
    lcd_command(0x04);
    lcd_command(0x14);
    lcd_command(0x20);
    lcd_command(0x0C);

    memset(lcd_buffer, 0x00, sizeof(lcd_buffer));
}

static void lcd_clear_buffer(void) {
    memset(lcd_buffer, 0x00, sizeof(lcd_buffer));
}

static void lcd_update(void) {
    lcd_command(0x40);
    lcd_command(0x80);

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

// ---------- DIBUJOS ----------

static void draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    for (uint8_t i = x; i < x + w; i++) {
        for (uint8_t j = y; j < y + h; j++) {
            lcd_set_pixel(i, j, 1);
        }
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

static void draw_blocks(void) {
    for (uint8_t r = 0; r < BLOCK_ROWS; r++) {
        for (uint8_t c = 0; c < BLOCK_COLS; c++) {
            if (blocks[r][c] == 1) {
                uint8_t x = BLOCK_START_X + c * BLOCK_W;
                uint8_t y = BLOCK_START_Y + r * BLOCK_H;
                draw_rect(x, y, BLOCK_W - 1, BLOCK_H - 1);
            }
        }
    }
}

static void game_draw(void) {
    lcd_clear_buffer();

    draw_border();
    draw_blocks();

    draw_rect(paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H);
    draw_rect(ball_x, ball_y, BALL_SIZE, BALL_SIZE);

    lcd_update();
}

// ---------- BOTONES ----------

static void buttons_init(void) {
    BTN_DDR &= ~BTN_MASK;   // botones como entrada
    BTN_PORT |= BTN_MASK;   // pull-up interno activado
}

static uint8_t button_pressed(uint8_t button) {
    if (!(BTN_PIN & (1 << button))) {
        return 1;
    } else {
        return 0;
    }
}

// ---------- JUEGO ----------

static void game_init(void) {
    paddle_x = 34;

    ball_x = 41;
    ball_y = 35;
    ball_dx = 1;
    ball_dy = -1;

    game_over = 0;
    win = 0;

    for (uint8_t r = 0; r < BLOCK_ROWS; r++) {
        for (uint8_t c = 0; c < BLOCK_COLS; c++) {
            blocks[r][c] = 1;
        }
    }
}

static uint8_t blocks_remaining(void) {
    for (uint8_t r = 0; r < BLOCK_ROWS; r++) {
        for (uint8_t c = 0; c < BLOCK_COLS; c++) {
            if (blocks[r][c] == 1) {
                return 1;
            }
        }
    }

    return 0;
}

static void check_block_collision(void) {
    for (uint8_t r = 0; r < BLOCK_ROWS; r++) {
        for (uint8_t c = 0; c < BLOCK_COLS; c++) {
            if (blocks[r][c] == 1) {
                uint8_t bx = BLOCK_START_X + c * BLOCK_W;
                uint8_t by = BLOCK_START_Y + r * BLOCK_H;

                if (ball_x + BALL_SIZE >= bx &&
                    ball_x <= bx + BLOCK_W - 1 &&
                    ball_y + BALL_SIZE >= by &&
                    ball_y <= by + BLOCK_H - 1) {
                    
                    blocks[r][c] = 0;
                    ball_dy = -ball_dy;
                    return;
                }
            }
        }
    }
}

static void game_update(void) {
    if (game_over || win) {
        if (button_pressed(BTN_STOP)) {
            game_init();
        }
        return;
    }

    if (button_pressed(BTN_LEFT)) {
        if (paddle_x > 2) {
            paddle_x -= 2;
        }
    }

    if (button_pressed(BTN_RIGHT)) {
        if (paddle_x < LCD_WIDTH - PADDLE_W - 2) {
            paddle_x += 2;
        }
    }

    ball_x += ball_dx;
    ball_y += ball_dy;

    if (ball_x <= 1) {
        ball_x = 1;
        ball_dx = 1;
    }

    if (ball_x >= LCD_WIDTH - BALL_SIZE - 1) {
        ball_x = LCD_WIDTH - BALL_SIZE - 1;
        ball_dx = -1;
    }

    if (ball_y <= 1) {
        ball_y = 1;
        ball_dy = 1;
    }

    if (ball_y + BALL_SIZE >= PADDLE_Y &&
        ball_y + BALL_SIZE <= PADDLE_Y + PADDLE_H &&
        ball_x + BALL_SIZE >= paddle_x &&
        ball_x <= paddle_x + PADDLE_W) {
        
        ball_dy = -1;
    }

    check_block_collision();

    if (ball_y >= LCD_HEIGHT - BALL_SIZE - 1) {
        game_over = 1;
    }

    if (!blocks_remaining()) {
        win = 1;
    }
}

// ---------- MAIN ----------

int main(void) {
    lcd_init();
    buttons_init();
    game_init();

    while (1) {
        game_update();
        game_draw();
        _delay_ms(60);
    }

    return 0;
}