/*
    cuando rompes casilla:
        - cae poder especial (se multiplica por 2 la pelota)
        - no todas las casillas valen un punto
*/


#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// foto res
#define LDR_PIN   PA6
#define LED_PIN   PB6

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
#define BTN_A PA5
#define BTN_B PA6

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

#define EEPROM_ADDR 0x50

#define EE_MAGIC_0 0x0000
#define EE_MAGIC_1 0x0001
#define EE_VERSION 0x0002

#define MAGIC_0 'R'
#define MAGIC_1 'P'
#define EEPROM_VERSION 1

#define EE_SCORE_BASE 0x0110   // BREAKOUT
#define TOP_COUNT 4

static uint8_t highscore_saved;

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

typedef enum {
    MENU_ITEM_PLAY,
    MENU_ITEM_LOAD,
    MENU_ITEM_COUNT
} MenuItem;
static uint8_t menu_selection = (uint8_t)MENU_ITEM_PLAY;

typedef enum {
    END_ITEM_PLAY,
    END_ITEM_SCAPE,
    END_ITEM_COUNT
} EndItem;
static uint8_t end_selection = (uint8_t)END_ITEM_PLAY;

typedef enum {
    APP_MENU,
    APP_INSTRUCCION,
    APP_SCORE,
    APP_GAME,
    APP_END,
} AppState;

static AppState app_state = APP_MENU;

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

static void draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    for (uint8_t i = x; i < x + w; i++) {
        for (uint8_t j = y; j < y + h; j++) {
            lcd_set_pixel(i, j, 1);
        }
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

// Textos que se muestran
static const uint8_t G_[5] PROGMEM = {0x3E,0x41,0x49,0x49,0x7A};
static const uint8_t A_[5] PROGMEM = {0x7E,0x11,0x11,0x11,0x7E};
static const uint8_t M_[5] PROGMEM = {0x7F,0x02,0x04,0x02,0x7F};
static const uint8_t E_[5] PROGMEM = {0x7F,0x49,0x49,0x49,0x41};
static const uint8_t O_[5] PROGMEM = {0x3E,0x41,0x41,0x41,0x3E};
static const uint8_t V_[5] PROGMEM = {0x1F,0x20,0x40,0x20,0x1F};
static const uint8_t R_[5] PROGMEM = {0x7F,0x09,0x19,0x29,0x46};
static const uint8_t J_[5] PROGMEM = {0x20,0x40,0x41,0x3F,0x01};
static const uint8_t U_[5] PROGMEM = {0x3F,0x40,0x40,0x40,0x3F};
static const uint8_t C_[5] PROGMEM = {0x3E,0x41,0x41,0x41,0x22};
static const uint8_t B_[5] PROGMEM = {0x7F,0x49,0x49,0x49,0x36};
static const uint8_t S_[5] PROGMEM = {0x26,0x49,0x49,0x49,0x32};
static const uint8_t P_[5] PROGMEM = {0x7F,0x09,0x09,0x09,0x06};
static const uint8_t I_[5] PROGMEM = {0x41,0x41,0x7F,0x41,0x41};
static const uint8_t N_[5] PROGMEM = {0x7F,0x02,0x04,0x08,0x7F};
static const uint8_t D_[5] PROGMEM = {0x7F,0x41,0x41,0x22,0x1C};
static const uint8_t L_[5] PROGMEM = {0x7F,0x40,0x40,0x40,0x40};
static const uint8_t T_[5] PROGMEM = {0x01,0x01,0x7F,0x01,0x01};
static const uint8_t K_[5] PROGMEM = {0x7F,0x08,0x14,0x22,0x41};

static const uint8_t a_[5] PROGMEM = {0x20,0x54,0x54,0x54,0x78};
static const uint8_t b_[5] PROGMEM = {0x7F,0x44,0x44,0x44,0x38};
static const uint8_t c_[5] PROGMEM = {0x38,0x44,0x44,0x44,0x28};
static const uint8_t d_[5] PROGMEM = {0x38,0x44,0x44,0x44,0x7F};
static const uint8_t e_[5] PROGMEM = {0x38,0x54,0x54,0x54,0x18};
static const uint8_t i_[5] PROGMEM = {0x00,0x44,0x7D,0x40,0x00};
static const uint8_t l_[5] PROGMEM = {0x00,0x41,0x7F,0x40,0x00};
static const uint8_t n_[5] PROGMEM = {0x7C,0x04,0x04,0x04,0x78};
static const uint8_t o_[5] PROGMEM = {0x38,0x44,0x44,0x44,0x38};
static const uint8_t p_[5] PROGMEM = {0x7C,0x14,0x14,0x14,0x08};
static const uint8_t r_[5] PROGMEM = {0x7C,0x08,0x04,0x04,0x08};
static const uint8_t s_[5] PROGMEM = {0x48,0x54,0x54,0x54,0x24};
static const uint8_t t_[5] PROGMEM = {0x04,0x3F,0x44,0x40,0x20};
static const uint8_t u_[5] PROGMEM = {0x3C,0x40,0x40,0x20,0x7C};
static const uint8_t m_[5] PROGMEM = {0x7C,0x04,0x18,0x04,0x78};

static const uint8_t space_[5] PROGMEM = {0x00,0x00,0x00,0x00,0x00};
static const uint8_t punto_[5] PROGMEM = {0,0,0x60,0x60,0};

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

static void lcd_draw_pattern_char(uint8_t x, uint8_t y, const uint8_t p[5]) {
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t bits = pgm_read_byte(&p[col]);
        for (uint8_t row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                lcd_set_pixel(x + col, y + row, 1);
            }
        }
    }
}

static void lcd_draw_pattern_text(uint8_t x, uint8_t y, const uint8_t *msg[], uint8_t len){
    for(uint8_t i = 0; i < len; i++){
        lcd_draw_pattern_char(x + i * 6, y, msg[i]);
    }
}

lcd_draw_break_out_text(void){ // falta ver si si queda bien
    const uint8_t *msg[] = {B_,R_,A_,K_,E_,space_,O_,U_,T_};
    lcd_draw_pattern_text(26, 14, msg, 8);
}

static void lcd_draw_cargar_text(void){
    const uint8_t *msg[] = {C_ ,A_ ,R_ ,G_, A_, R_};
    lcd_draw_pattern_text(24, 27, msg, 6);
}

static void lcd_draw_text_one(void){
    const uint8_t *msg[] = {C_,o_,n_,e_,c_,t_,a_,space_,l_,a_};
    lcd_draw_pattern_text(6, 4, msg, 10);
}

static void lcd_draw_text_two(void){
    const uint8_t *msg[] = {c_,o_,n_,s_,o_,l_,a_,space_,a_,space_,l_,a_};
    lcd_draw_pattern_text(6, 12, msg, 12);
}

static void lcd_draw_text_tree(void){
    const uint8_t *msg[] = {c_,o_,m_,p_,u_,t_,a_,d_,o_,r_,a_};
    lcd_draw_pattern_text(6, 20, msg, 11);
}

static void lcd_draw_text_four(void){
    const uint8_t *msg[] = {c_,o_,n_,space_,e_,l_,space_,c_,a_,b_,l_,e_};
    lcd_draw_pattern_text(6, 28, msg, 12);
}

static void lcd_draw_text_five(void){
    const uint8_t *msg[] = {e_,s_,p_,e_,c_,i_,a_,l_};
    lcd_draw_pattern_text(6, 36, msg, 8);
}

static void lcd_draw_B_text(void){
    const uint8_t *msg[] = {B_};
    lcd_draw_pattern_text(75, 39, msg, 1);
}

static void lcd_draw_score_text(void){
    const uint8_t *msg[] = {S_, C_, O_, R_, E_};
    lcd_draw_pattern_text(29, 1, msg, 5);
}

static void lcd_draw_game_over_text(void) {
    const uint8_t *msg[] = {G_, A_, M_, E_, space_, O_, V_, E_, R_};
    lcd_draw_pattern_text(12, 18, msg, 9);
}

static void lcd_draw_pause_text(void) {
    const uint8_t *msg[] = {P_, A_, U_, S_, E_};
    lcd_draw_pattern_text(27, 18, msg, 5);
}

static void lcd_draw_seguir_text(void){
    const uint8_t *msg[] = {S_,E_,G_,U_,I_,R_};
    lcd_draw_pattern_text(26, 9, msg, 6);
}

static void lcd_draw_jugando_text(void){
    const uint8_t *msg[] = {J_,U_,G_,A_,N_,D_,O_};
    lcd_draw_pattern_text(22, 17, msg, 7);
}

static void lcd_draw_salir_text(void){
    const uint8_t *msg[] = {S_,A_,L_,I_,R_};
    lcd_draw_pattern_text(26, 31, msg, 5);
}

// Numeros que se muestran
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

static void lcd_draw_char_digit(uint8_t x, uint8_t y, char c) {
    if (c < '0' || c > '9') return;
    uint8_t idx = c - '0';

    for (uint8_t col = 0; col < 5; col++) {
        uint8_t bits = pgm_read_byte(&font5x7[idx][col]);
        for (uint8_t row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                lcd_set_pixel(x + col, y + row, 1);
            }
        }
    }
}

static void lcd_draw_number(uint8_t x, uint8_t y, uint16_t n) {
    char buf[6];
    itoa(n, buf, 10);

    uint8_t pos = 0;
    while (buf[pos]) {
        lcd_draw_char_digit(x + pos * 6, y, buf[pos]);
        pos++;
    }
}

static void lcd_draw_rank(uint8_t x, uint8_t y, uint8_t rank){
    lcd_draw_number(x, y, rank);
    lcd_draw_pattern_char(x + 6, y, punto_);
}

static void lcd_draw_score_row(uint8_t y, uint8_t rank, uint16_t score){
    lcd_draw_rank(3, y, rank);
    lcd_draw_number(20, y, score);
}

// botones
static void buttons_init(void) {
    BTN_DDR &= ~(BTN_MASK);
    // Pull-ups internos (botones a GND)
    BTN_PORT |= BTN_MASK;
}

static uint8_t buttons_raw_mask(void){
    return (uint8_t)(~BTN_PIN) & (uint8_t) BTN_MASK;
}

static void buttons_reset(void){
    btn_state = buttons_raw_mask();
    btn_press_events = 0;
    btn_release_events = 0;
    for(uint8_t i = 0; i < BTN_COUNT; i++){
        btn_debounce_cnt[i] = 0;
    }
}

static void buttons_poll(void) {
    static const uint8_t bits[BTN_COUNT] = {
        (1 << BTN_UP),
        (1 << BTN_DOWN),
        (1 << BTN_LEFT),
        (1 << BTN_RIGHT),
        (1 << BTN_A),
        (1 << BTN_B),
    };

    uint8_t sample = buttons_raw_mask();

    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        uint8_t mask = bits[i];
        uint8_t raw_down = (sample & mask) ? 1 : 0;
        uint8_t stable_down = (btn_state & mask) ? 1 : 0;

        if (raw_down == stable_down) {
            btn_debounce_cnt[i] = 0;
            continue;
        }

        if (btn_debounce_cnt[i] < BTN_DEBOUNCE_TICKS) {
            btn_debounce_cnt[i]++;
        }

        if (btn_debounce_cnt[i] >= BTN_DEBOUNCE_TICKS) {
            btn_debounce_cnt[i] = 0;
            if (raw_down) {
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

// menu
static const uint8_t CURSOR_R[5] PROGMEM = {0x00, 0x3E, 0x1C, 0x08, 0x00};

static void lcd_draw_cursor(uint8_t x, uint8_t y){
    lcd_draw_pattern_char(x, y, CURSOR_R);
}

static void menu_update(void){
    if(button_pressed_event(BTN_UP)){
        if(menu_selection > 0){
            menu_selection--;
        }
    }

    if(button_pressed_event(BTN_DOWN)){
        if(menu_selection + 1u < (uint8_t)MENU_ITEM_COUNT){
            menu_selection++;
        }
    }
}

static void menu_draw(void){
    lcd_clear_buffer();

    draw_border();
    lcd_draw_snake_text();
    lcd_draw_cargar_text();

    if(menu_selection == (uint8_t)MENU_ITEM_PLAY){
        lcd_draw_cursor(16, 14);
    } else {
        lcd_draw_cursor(16, 27);
    }

    lcd_update();
}

// pantalla de instrucciones
static void instruction_draw(void){
    lcd_clear_buffer();

    draw_border();
    lcd_draw_B_text();

    lcd_draw_text_one();
    lcd_draw_text_two();
    lcd_draw_text_tree();
    lcd_draw_text_four();
    lcd_draw_text_five();

    lcd_update();
}

// codigo memoria externa
static void twi_init(void){
    TWSR = 0x00;
    TWBR = 72;
    TWCR = (1 << TWEN);
}

static void twi_start(void){
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    while(!(TWCR & ( 1 << TWINT)));
}

static void twi_stop(void){
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
    _delay_us(10);
}

static void twi_write(uint8_t data){
    TWDR = data;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while(!(TWCR & (1 << TWINT)));
}

static uint8_t twi_read_nack(void){
    TWCR = (1 << TWINT) | (1 << TWEN);
    while(!(TWCR & (1 << TWINT)));
    return TWDR;
}

static void eeprom_write_byte(uint16_t mem_addr, uint8_t data){
    twi_start();
    twi_write((EEPROM_ADDR << 1) | 0);
    twi_write((uint8_t)(mem_addr >> 8));
    twi_write((uint8_t)mem_addr);
    twi_write(data);
    twi_stop();
    _delay_ms(10);
}

static uint8_t eeprom_read_byte(uint16_t mem_addr){
    uint8_t data;

    twi_start();
    twi_write((EEPROM_ADDR << 1) | 0);
    twi_write((uint8_t)(mem_addr >> 8));
    twi_write((uint8_t)mem_addr);

    twi_start();
    twi_write((EEPROM_ADDR << 1) | 1);

    data = twi_read_nack();
    twi_stop();

    return data;
}

static void eeprom_write_u16(uint16_t addr, uint16_t value){
    eeprom_write_byte(addr, (uint8_t)(value & 0xFF));
    eeprom_write_byte(addr + 1, (uint8_t)(value >> 8));
}

static uint16_t eeprom_read_u16(uint16_t addr){
    uint8_t low = eeprom_read_byte(addr);
    uint8_t high = eeprom_read_byte(addr + 1);
    return ((uint16_t)high << 8) | low;
}

static void scores_read(uint16_t scores[TOP_COUNT]){
    for(uint8_t i = 0; i < TOP_COUNT; i++){
        scores[i] = eeprom_read_u16(EE_SCORE_BASE + (i*2));
    }
}

static void scores_write(uint16_t scores[TOP_COUNT]){
    for(uint8_t i = 0; i < TOP_COUNT; i++){
        eeprom_write_u16(EE_SCORE_BASE + (i * 2),scores[i]);
    }
}

static void scores_try_insert(uint16_t new_score){
    uint16_t scores[TOP_COUNT];

    scores_read(scores);

    for(uint8_t i = 0; i < TOP_COUNT; i++){
        if(new_score > scores[i]){
            for(uint8_t j = TOP_COUNT - 1; j > i; j--){
                scores[j] = scores[j - 1];
            }

            scores[i] = new_score;
            scores_write(scores);
            return;
        }
    }
}

static void eeprom_init_if_needed(void){
    uint8_t m0 = eeprom_read_byte(EE_MAGIC_0);
    uint8_t m1 = eeprom_read_byte(EE_MAGIC_1);

    if(m0 != MAGIC_0 || m1 != MAGIC_1){
        eeprom_write_byte(EE_MAGIC_0, MAGIC_0);
        eeprom_write_byte(EE_MAGIC_1, MAGIC_1);
        eeprom_write_byte(EE_VERSION, EEPROM_VERSION);

        uint16_t zeros[TOP_COUNT] = {0,0,0,0};
        scores_write(zeros);
    }
}

static void scores_clear(void){
    uint16_t zeros[TOP_COUNT] = {0, 0, 0, 0};
    scores_write(zeros);
}

// pantalla de scores
static void score_draw(void){

    uint16_t scores[TOP_COUNT];

    scores_read(scores);

    lcd_clear_buffer();
    draw_border();

    lcd_draw_score_text();
    lcd_draw_score_row(12, 1, scores[0]);
    lcd_draw_score_row(20, 2, scores[1]);
    lcd_draw_score_row(28, 3, scores[2]);
    lcd_draw_score_row(36, 4, scores[3]);

    lcd_update();
}

// juego
static void game_draw(void) {
    lcd_clear_buffer();

    draw_border();

    draw_blocks();

    draw_rect(paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H);
    draw_rect(ball_x, ball_y, BALL_SIZE, BALL_SIZE);

    // Marcador abajo izquierda
    lcd_draw_number(SCORE_X, SCORE_Y, score);

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

// pantalla end game
static void end_game_update(void){
    if(button_pressed_event(BTN_UP)){
        if(end_selection > 0) end_selection--;
    }

    if(button_pressed_event(BTN_DOWN)){
        if(end_selection + 1u < (uint8_t)END_ITEM_COUNT) end_selection++;
    }
}

static void end_game_draw(void){
    lcd_clear_buffer();

    draw_border();
    lcd_draw_seguir_text();
    lcd_draw_jugando_text();
    lcd_draw_salir_text();

    if(end_selection == (uint8_t)END_ITEM_PLAY){
        lcd_draw_cursor(16, 13);
    } else {
        lcd_draw_cursor(16, 31);
    }

    lcd_update();
}

static void ldr_led_init(void) {
    // PD6 como entrada
    DDRA &= ~(1 << LDR_PIN);
    // Pull-up interno desactivado
    PORTA &= ~(1 << LDR_PIN);

    // PB6 como salida
    DDRB |= (1 << LED_PIN);
    // LED apagado al inicio
    PORTB &= ~(1 << LED_PIN);
}

static void ldr_led_update(void) {
    // Si PD6 lee 1, asumimos poca luz
    if (PINA & (1 << LDR_PIN)) {
        PORTB |= (1 << LED_PIN);   // prende LED
    } else {
        PORTB &= ~(1 << LED_PIN);  // apaga LED
    }
}

// main
int main(void) {
    lcd_init();
    buttons_init();
    rng_init();
    twi_init();
    eeprom_init_if_needed();
    buttons_reset();
    ldr_led_init();

    uint8_t prev_game_over = 0;
    uint8_t restart_armed = 0;
    highscore_saved = 0;

    while (1) {

        ldr_led_update();
        buttons_poll();

        if(app_state == APP_MENU){
            menu_update();

            if(button_pressed_event(BTN_A)){
                if(menu_selection == (uint8_t)MENU_ITEM_LOAD){
                    app_state = APP_INSTRUCCION;
                    buttons_reset();
                } else if(menu_selection == (uint8_t)MENU_ITEM_PLAY){
                    app_state = APP_SCORE;
                    buttons_reset();
                }
            }

            menu_draw();
            _delay_ms(INPUT_POLL_MS);
        
        } else if(app_state == APP_INSTRUCCION){
            if(button_pressed_event(BTN_B)){
                app_state = APP_MENU;
                buttons_reset();
            }
            
            instruction_draw();
        } else if(app_state == APP_SCORE){
            if(button_pressed_event(BTN_A) || button_pressed_event(BTN_B)){
                game_init();
                highscore_saved = 0;
                app_state = APP_GAME;
                buttons_reset();
            }

            score_draw();
            _delay_ms(INPUT_POLL_MS);
        
        } else if(app_state == APP_GAME){
            if(game_over && !prev_game_over){
                if(!highscore_saved){
                    scores_try_insert(score);
                    highscore_saved = 1;
                }
                buttons_reset();
                restart_armed = 0;
            }
            prev_game_over = game_over;
        }

        
        return 0;
    }
}
    
    /*
    uint8_t prev_end = 0;
    uint8_t restart_armed = 0;

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
        */