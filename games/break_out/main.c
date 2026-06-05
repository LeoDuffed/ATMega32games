/*
    cuando rompes casilla:
        - cae poder especial (se multiplica por 2 la pelota)
        - no todas las casillas valen un punto
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
#define BTN_PORT PORTA
#define BTN_PIN PINA
#define BTN_DDR DDRA

#define BTN_UP PA0
#define BTN_DOWN PA1
#define BTN_LEFT PA2
#define BTN_RIGHT PA3
#define BTN_STOP PA4

#define BTN_MASK ((1 << BTN_UP) | (1 << BTN_DOWN) | (1 << BTN_LEFT) | (1 << BTN_RIGHT) | (1 << BTN_STOP))

#define INPUT_POLL_MS 10
#define GAME_TICK_MS 100
#define BTN_DEBOUNCE_TICKS 2
#define PADDLE_REPEAT_TICKS 2

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

static uint8_t paddle_x;
static int8_t ball_x;
static int8_t ball_y;
static int8_t ball_dx;
static int8_t ball_dy;

static uint8_t blocks[BLOCK_ROWS][BLOCK_COLS];
static uint8_t game_over = 0;
static uint8_t game_pause = 0;
static uint8_t win;
static uint16_t score = 0;

#define SCORE_X 2
#define SCORE_Y (LCD_HEIGHT - 8)

typedef enum {
    BTN_IDX_LEFT,
    BTN_IDX_RIGHT,
    BTN_IDX_STOP,
    BTN_COUNT
} ButtonIndex;

static uint8_t btn_state;
static uint8_t btn_press_events;
static uint8_t btn_release_events;
static uint8_t btn_debounce_cnt[BTN_COUNT];

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
static const uint8_t font5x7[][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
};

static void draw_digit(uint8_t x, uint8_t y, char c){
    if(c < '0' || c > '9') return;
    uint8_t idx = c - '0';

    for(uint8_t col = 0; col < 5; col++){
        uint8_t bits = font5x7[idx][col];
        for(uint8_t row = 0; row < 7; row++){
            if(bits & (1 << row)){
                lcd_set_pixel(x + col, y + row, 1);
            }
        }
    }
}

static void draw_score(uint8_t x, uint8_t y, uint16_t n){
    char buf[6];
    itoa(n, buf, 10);

    uint8_t pos = 0;
    while(buf[pos]){
        draw_digit(x + pos * 6, y, buf[pos]);
        pos++;
    }
}

// Letras para "GAME OVER"
static const uint8_t G_[5] = {0x3E,0x41,0x49,0x49,0x7A};
static const uint8_t A_[5] = {0x7E,0x11,0x11,0x11,0x7E};
static const uint8_t M_[5] = {0x7F,0x02,0x04,0x02,0x7F};
static const uint8_t E_[5] = {0x7F,0x49,0x49,0x49,0x41};
static const uint8_t O_[5] = {0x3E,0x41,0x41,0x41,0x3E};
static const uint8_t V_[5] = {0x1F,0x20,0x40,0x20,0x1F};
static const uint8_t R_[5] = {0x7F,0x09,0x19,0x29,0x46};
static const uint8_t space_[5] = {0x00,0x00,0x00,0x00,0x00};

// Letras para "PAUSE"
static const uint8_t P_[5] = {0x7F,0x09,0x09,0x09,0x06};
static const uint8_t U_[5] = {0x3F,0x40,0x40,0x40,0x3F};
static const uint8_t S_[5] = {0x26,0x49,0x49,0x49,0x32};

static void lcd_draw_pattern_char(uint8_t x, uint8_t y, const uint8_t p[5]){
    for(uint8_t col = 0; col < 5; col++){
        uint8_t bits = p[col];
        for(uint8_t row = 0; row < 7; row++){
            if(bits & (1 << row)){
                lcd_set_pixel(x + col, y + row, 1);
            }
        }
    }
}

static void lcd_draw_game_over_text(void){
    const uint8_t *msg[] = {G_, A_, M_, E_, space_, O_, V_, E_, R_};
    uint8_t x = 12;
    uint8_t y = 25;

    for(uint8_t i = 0; i < 9; i++){
        lcd_draw_pattern_char(x + i * 6, y, msg[i]);
    }
}

static void lcd_draw_pause_text(void){
    const uint8_t *msg[] = {P_, A_, U_, S_, E_};
    uint8_t x = 27;
    uint8_t y = 25;

    for(uint8_t i = 0; i < 5; i++){
        lcd_draw_pattern_char(x + i * 6, y, msg[i]);
    }
}

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

// ---------- BOTONES ----------

static void buttons_init(void) {
    BTN_DDR &= ~BTN_MASK;   // botones como entrada
    BTN_PORT |= BTN_MASK;   // pull-up interno activado
}

static uint8_t buttons_raw_mask(void){
    return (uint8_t) (~BTN_PIN) & (uint8_t) BTN_MASK;
}

static void buttons_reset(void){
    btn_state = buttons_raw_mask();
    btn_press_events = 0;
    btn_release_events = 0;
    for(uint8_t i = 0; i < BTN_COUNT; i++){
        btn_debounce_cnt[i] = 0;
    }
}

static void buttons_poll(void){
    static const uint8_t bits[BTN_COUNT] = {
        (1 << BTN_LEFT),
        (1 << BTN_RIGHT),
        (1 << BTN_STOP),
    };

    uint8_t sample = buttons_raw_mask();

    for(uint8_t i = 0; i < BTN_COUNT; i++){
        uint8_t mask = bits[i];
        uint8_t raw_down = (sample & mask) ? 1 : 0;
        uint8_t stable_down = (btn_state & mask) ? 1 : 0;

        if(raw_down == stable_down){
            btn_debounce_cnt[i] = 0;
            continue;
        }

        if(btn_debounce_cnt[i] < BTN_DEBOUNCE_TICKS){
            btn_debounce_cnt[i]++;
        }

        if(btn_debounce_cnt[i] >= BTN_DEBOUNCE_TICKS){
            btn_debounce_cnt[i] = 0;
            if(raw_down){
                btn_state |= mask;
                btn_press_events |= mask;
            } else {
                btn_state &= (uint8_t)~mask;
                btn_release_events |= mask;
            }
        }
    }
}

static uint8_t button_down(uint8_t pin){
    return(btn_state & (1 << pin)) ? 1 : 0;
}

static uint8_t button_pressed_event(uint8_t pin){
    uint8_t mask = (1 << pin);
    uint8_t v = (btn_press_events & mask) ? 1 : 0;
    btn_press_events &= (uint8_t)~mask;
    return v;
}

static uint8_t any_button_pressed_event(void){
    uint8_t v = btn_press_events;
    btn_press_events = 0;
    return v ? 1 : 0;
}

static void rng_init(void){
    TCCR0 = (1 << CS01) | (1 << CS00);
}

static uint16_t rng_entropy(void){
    uint8_t t = TCNT0;
    uint8_t p = BTN_PIN;
    return ((uint16_t)t << 8) | (uint16_t)(t^p);
}

// ---------- JUEGO ----------

static void game_draw(void) {
    lcd_clear_buffer();

    draw_border();

    draw_blocks();

    draw_rect(paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H);
    draw_rect(ball_x, ball_y, BALL_SIZE, BALL_SIZE);

    // Marcador abajo izquierda
    draw_score(SCORE_X, SCORE_Y, score);

    if(game_over || win){
        lcd_draw_game_over_text();
    } else if(game_pause){
        lcd_draw_pause_text();
    }

    lcd_update();
}

static void game_init(void) {
    paddle_x = 34;

    ball_x = 41;
    ball_y = 35;
    ball_dx = 1;
    ball_dy = -1;

    game_over = 0;
    game_pause = 0;
    win = 0;
    score = 0;

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
                    score++;
                    ball_dy = -ball_dy;
                    return;
                }
            }
        }
    }
}

static void read_input(void){
    static uint8_t paddle_move_cooldown = 0;

    // Pausa: toggle con evento (antirrebote)
    if(button_pressed_event(BTN_STOP)){
        game_pause ^= 1;
    }

    if(game_pause || game_over || win){
        return;
    }

    if(paddle_move_cooldown){
        paddle_move_cooldown--;
    }

    uint8_t want_left = button_down(BTN_LEFT);
    uint8_t want_right = button_down(BTN_RIGHT);

    // Si no se presiona nada, permitir movimiento inmediato al volver a presionar
    if(!want_left && !want_right){
        paddle_move_cooldown = 0;
        return;
    }

    // Suavizar: limitar la tasa de movimiento (evita que avance "de más")
    if(paddle_move_cooldown){
        return;
    }

    if(want_left && !want_right){
        if(paddle_x > 2){
            paddle_x--;
        }
    } else if(want_right && !want_left){
        if(paddle_x < LCD_WIDTH - PADDLE_W - 2){
            paddle_x++;
        }
    }

    paddle_move_cooldown = PADDLE_REPEAT_TICKS;
}

static void game_update(void) {
    if(game_over || win) return;
    if(game_pause) return;

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
    buttons_reset();
    game_init();
    uint8_t prev_end = 0;
    uint8_t restart_armed = 0;

    while (1) {
        uint8_t end = (game_over || win) ? 1 : 0;
        if(end && !prev_end){
            buttons_reset();
            restart_armed = 0;
        }
        prev_end = end;

        for(uint8_t i = 0; i < (GAME_TICK_MS / INPUT_POLL_MS); i++){
            buttons_poll();

            if(game_over || win){
                if(!restart_armed){
                    if(btn_state == 0) restart_armed = 1;
                } else if(any_button_pressed_event()){
                    game_init();
                    buttons_reset();
                    prev_end = 0;
                    restart_armed = 0;
                }
            } else {
                read_input();
            }

            _delay_ms(INPUT_POLL_MS);
        }

        game_update();
        game_draw();
    }

    return 0;
}
