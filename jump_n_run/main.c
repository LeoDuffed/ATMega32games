/* 
    FALTA:
        - Score
        - Pausa

    CORREGIR:
        - Otro boton para saltar (UP)
        - Arreglar el boton para saltar
*/


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
#define BTN_PIN PINA
#define BTN_DDR DDRA

#define BTN_UP PA0
#define BTN_DOWN PA1
#define BTN_LEFT PA2
#define BTN_RIGHT PA3
#define BTN_STOP PA4

#define BTN_MASK ((1 << BTN_UP) | (1 << BTN_DOWN) | (1 << BTN_LEFT) | (1 << BTN_RIGHT) | (1 << BTN_STOP))

#define LCD_WIDTH   84
#define LCD_HEIGHT  48

static uint8_t lcd_buffer[504];

// Dinosaurio
#define DINO_X 10
#define DINO_W 6
#define DINO_H 8
#define GROUND_Y 40

// Obstáculo
#define OBS_W 5
#define OBS_H 9

uint8_t dino_y;
int8_t velocity_y;
uint8_t jumping;

int8_t obstacle_x;
uint8_t obstacle_h;

uint8_t game_over;
uint16_t score;

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

// ---------- DIBUJO ----------

static void draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    for (uint8_t i = x; i < x + w; i++) {
        for (uint8_t j = y; j < y + h; j++) {
            lcd_set_pixel(i, j, 1);
        }
    }
}

static void draw_ground(void) {
    for (uint8_t x = 0; x < LCD_WIDTH; x++) {
        lcd_set_pixel(x, GROUND_Y, 1);
    }
}

static void draw_dino(void) {
    draw_rect(DINO_X, dino_y, DINO_W, DINO_H);

    // Ojito simple
    lcd_set_pixel(DINO_X + 4, dino_y + 2, 0);
}

static void draw_obstacle(void) {
    draw_rect(obstacle_x, GROUND_Y - obstacle_h, OBS_W, obstacle_h);
}

static void draw_score_bar(void) {
    uint8_t small_score = score % 80;

    for (uint8_t x = 2; x < 2 + small_score; x++) {
        lcd_set_pixel(x, 2, 1);
    }
}

static void game_draw(void) {
    lcd_clear_buffer();

    draw_ground();
    draw_dino();
    draw_obstacle();
    draw_score_bar();

    lcd_update();
}

// ---------- BOTONES ----------

static void buttons_init(void) {
    BTN_DDR &= ~BTN_MASK;
    BTN_PORT |= BTN_MASK;
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
    dino_y = GROUND_Y - DINO_H;
    velocity_y = 0;
    jumping = 0;

    obstacle_x = LCD_WIDTH - 8;
    obstacle_h = OBS_H;

    game_over = 0;
    score = 0;
}

static uint8_t check_collision(void) {
    uint8_t dino_left = DINO_X;
    uint8_t dino_right = DINO_X + DINO_W;
    uint8_t dino_top = dino_y;
    uint8_t dino_bottom = dino_y + DINO_H;

    uint8_t obs_left = obstacle_x;
    uint8_t obs_right = obstacle_x + OBS_W;
    uint8_t obs_top = GROUND_Y - obstacle_h;
    uint8_t obs_bottom = GROUND_Y;

    if (dino_right >= obs_left &&
        dino_left <= obs_right &&
        dino_bottom >= obs_top &&
        dino_top <= obs_bottom) {
        return 1;
    }

    return 0;
}

static void game_update(void) {
    if (game_over) {
        if (button_pressed(BTN_STOP)) {
            game_init();
        }

        return;
    }

    // Salto
    if (button_pressed(BTN_UP) && jumping == 0) {
        velocity_y = -5;
        jumping = 1;
    }

    // Gravedad
    dino_y += velocity_y;
    velocity_y++;

    if (dino_y >= GROUND_Y - DINO_H) {
        dino_y = GROUND_Y - DINO_H;
        velocity_y = 0;
        jumping = 0;
    }

    // Movimiento del obstáculo
    obstacle_x -= 2;

    if (obstacle_x < -OBS_W) {
        obstacle_x = LCD_WIDTH - 1;
        score++;

        if (score % 3 == 0) {
            obstacle_h = 12;
        } else {
            obstacle_h = OBS_H;
        }
    }

    if (check_collision()) {
        game_over = 1;
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
        _delay_ms(80);
    }

    return 0;
}