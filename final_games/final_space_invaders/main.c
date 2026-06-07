#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// foto res
#define LDR_PIN PA6
#define LED_PIN PB6

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

#define BTN_MASK ((1 << BTN_UP) | (1 << BTN_DOWN) | (1 << BTN_LEFT) | (1 << BTN_RIGHT) | (1 << BTN_STOP) | (1 << BTN_A))

#define INPUT_POLL_MS 10
#define GAME_TICK_MS 180
#define BTN_DEBOUNCE_TICKS 2
#define SHIP_REPEAT_TICKS 2

#define LCD_WIDTH   84
#define LCD_HEIGHT  48
static uint8_t lcd_buffer[504];

typedef struct {
    uint8_t x;
    uint8_t y;
} Point;

// Juego
#define PLAY_X_MIN 1
#define PLAY_X_MAX (LCD_WIDTH - 2)
#define PLAY_Y_MIN 1
#define PLAY_Y_MAX (LCD_HEIGHT - 2)

#define SHIP_W 9
#define SHIP_H 4
#define SHIP_Y (PLAY_Y_MAX - SHIP_H + 1)

#define PLAYER_SHOT_W 1
#define PLAYER_SHOT_H 3
#define PLAYER_SHOT_SPEED 2

#define ALIEN_ROWS 4
#define ALIEN_COLS 8
#define ALIEN_W 6
#define ALIEN_H 4
#define ALIEN_GAP_X 2
#define ALIEN_GAP_Y 2
#define ALIEN_DROP 2

#define ALIEN_FORMATION_W (ALIEN_COLS * ALIEN_W + (ALIEN_COLS - 1) * ALIEN_GAP_X)
#define ALIEN_START_X ((LCD_WIDTH - ALIEN_FORMATION_W) / 2)
#define ALIEN_START_Y 9

#define ALIEN_SHOT_W 1
#define ALIEN_SHOT_H 3
#define ALIEN_SHOT_SPEED 2

#define SCORE_X 2
#define SCORE_Y 2
#define LIVES_X 76
#define LIVES_Y 2

static uint16_t score = 0;
static uint8_t game_over = 0;
static uint8_t game_paused = 0;
static uint8_t game_win = 0;
static uint16_t rng_state __attribute__((section(".noinit")));
static uint8_t lives = 3;

static uint8_t player_x;
static int8_t player_shot_x;
static int8_t player_shot_y;
static uint8_t player_shot_active;

static int8_t alien_shot_x;
static int8_t alien_shot_y;
static uint8_t alien_shot_active;

static uint8_t player_invuln_ticks;

static uint8_t aliens[ALIEN_ROWS][ALIEN_COLS];
static uint8_t alien_offset_x;
static uint8_t alien_offset_y;
static int8_t alien_dx;
static uint8_t alien_move_counter;
static uint8_t alien_anim;
static uint8_t alien_fire_cooldown;

typedef enum {
    BTN_IDX_UP,
    BTN_IDX_DOWN,
    BTN_IDX_LEFT,
    BTN_IDX_RIGHT,
    BTN_IDX_A,
    BTN_IDX_B,
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
static const uint8_t Y_[5] PROGMEM = {0x03,0x04,0x78,0x04,0x03};
static const uint8_t W_[5] PROGMEM = {0x7F,0x20,0x18,0x20,0x7F};

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

static void lcd_draw_pattern_char(uint8_t x, uint8_t y, const uint8_t p[5]){
    for(uint8_t col = 0; col < 5; col++){
        uint8_t bits = pgm_read_byte(&p[col]);
        for(uint8_t row = 0; row < 7; row++){
            if(bits & (1 << row)){
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

static void lcd_draw_space_invaders_text(void){
    const uint8_t *space_msg[] = {S_,P_,A_,C_,E_};
    const uint8_t *invaders_msg[] = {I_,N_,V_,A_,D_,E_,R_,S_};

    lcd_draw_pattern_text(27, 9, space_msg, 5);
    lcd_draw_pattern_text(17, 17, invaders_msg, 8);
}

static void lcd_draw_cargar_text(void){
    const uint8_t *msg[] = {C_ ,A_ ,R_ ,G_, A_, R_};
    lcd_draw_pattern_text(24, 31, msg, 6);
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

static void lcd_draw_win_text(void){
    const uint8_t *msg[] = {Y_, O_, U_, space_, W_, I_, N_};
    lcd_draw_pattern_text(22, 18, msg, 7);
}

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

static void lcd_draw_char_digit(uint8_t x, uint8_t y, char c){
    if(c < '0' || c > '9') return;
    uint8_t idx = c - '0';
    
    for(uint8_t col = 0; col < 5; col++){
        uint8_t bits = pgm_read_byte(&font5x7[idx][col]);
        for(uint8_t row = 0; row < 7; row++){
            if(bits & (1 << row)){
                lcd_set_pixel(x + col, y + row, 1);
            }
        }
    }
}

static void lcd_draw_number(uint8_t x, uint8_t y, uint16_t n){
    char buf[6];
    itoa(n, buf, 10);

    uint8_t pos = 0;
    while(buf[pos]){
        draw_digit(x + pos * 6, y, buf[pos]);
        pos++;
    }
}

static void buttons_init(void){
    BTN_DDR &= ~BTN_MASK;
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

static void buttons_poll(void){
    static const uint8_t bits[BTN_COUNT] = {
        (1 << BTN_UP),
        (1 << BTN_DOWN),
        (1 << BTN_LEFT),
        (1 << BTN_RIGHT),
        (1 << BTN_STOP),
        (1 << BTN_A),
    };

    uint8_t sample = buttons_raw_mask();

    for(uint8_t i = 0; i < BTN_COUNT; i++){
        uint8_t mask = bits[i];
        uint8_t raw_down = (sample & mask) ? 1 : 0;
        uint8_t stable_down = (btn_state & mask) ? 1 : 0;

        if (raw_down == stable_down){
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
    return (btn_state & (1 << pin)) ? 1 : 0;
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
    return ((uint16_t)t << 8) | (uint16_t)(t ^ p);
}

static void draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h){
    for(uint8_t i = 0; i < w; i++){
        for(uint8_t j = 0; j < h; j++){
            lcd_set_pixel(x + i, y + j, 1);
        }
    }
}

static void draw_sprite_9x4(uint8_t x, uint8_t y, const uint16_t rows[4]){
    for(uint8_t r = 0; r < 4; r++){
        uint16_t bits = rows[r];
        for(uint8_t c = 0; c < 9; c++){
            if(bits & (1u << (8 - c))){
                lcd_set_pixel(x + c, y + r, 1);
            }
        }
    }
}

static void draw_sprite_6x4(uint8_t x, uint8_t y, const uint8_t rows[4]){
    for(uint8_t r = 0; r < 4; r++){
        uint8_t bits = rows[r];
        for(uint8_t c = 0; c < 6; c++){
            if(bits & (1u << (5 - c))){
                lcd_set_pixel(x + c, y + r, 1);
            }
        }
    }
}

static void draw_ship(uint8_t x, uint8_t y){
    static const uint16_t ship_rows[4] = {
        0x010, // 000010000
        0x038, // 000111000
        0x07C, // 001111100
        0x1FF, // 111111111
    };
    draw_sprite_9x4(x, y, ship_rows);
}

static void draw_alien(uint8_t x, uint8_t y, uint8_t frame){
    static const uint8_t alien0[4] = {
        0x1E, // 011110
        0x2D, // 101101
        0x3F, // 111111
        0x12, // 010010
    };
    static const uint8_t alien1[4] = {
        0x1E, // 011110
        0x2D, // 101101
        0x3F, // 111111
        0x21, // 100001
    };
    draw_sprite_6x4(x, y, frame ? alien1 : alien0);
}

static uint8_t aabb_overlap_i16(int16_t ax, int16_t ay, int16_t aw, int16_t ah,
                                int16_t bx, int16_t by, int16_t bw, int16_t bh){
    if(ax + aw <= bx) return 0;
    if(bx + bw <= ax) return 0;
    if(ay + ah <= by) return 0;
    if(by + bh <= ay) return 0;
    return 1;
}

static uint8_t aliens_get_bounds(uint8_t *min_col, uint8_t *max_col, uint8_t *max_row){
    uint8_t any = 0;
    uint8_t mn = 0xFF;
    uint8_t mx = 0;
    uint8_t mr = 0;

    for(uint8_t r = 0; r < ALIEN_ROWS; r++){
        for(uint8_t c = 0; c < ALIEN_COLS; c++){
            if(!aliens[r][c]) continue;
            any = 1;
            if(c < mn) mn = c;
            if(c > mx) mx = c;
            if(r > mr) mr = r;
        }
    }

    if(!any) return 0;
    *min_col = mn;
    *max_col = mx;
    *max_row = mr;
    return 1;
}

static uint8_t aliens_count_alive(void){
    uint8_t count = 0;
    for(uint8_t r = 0; r < ALIEN_ROWS; r++){
        for(uint8_t c = 0; c < ALIEN_COLS; c++){
            if(aliens[r][c]) count++;
        }
    }
    return count;
}

static uint8_t alien_move_interval_ticks(uint8_t alive){
    if(alive > 24) return 3;
    if(alive > 16) return 2;
    if(alive > 8)  return 2;
    return 1;
}

static void spawn_player_shot(void){
    player_shot_active = 1;
    player_shot_x = (int8_t)(player_x + (SHIP_W / 2));
    player_shot_y = (int8_t)(SHIP_Y - PLAYER_SHOT_H);
}

static void spawn_alien_shot(void){
    uint8_t tries = 12;

    while(tries--){
        uint8_t col = (uint8_t)(rand() % ALIEN_COLS);
        for(int8_t r = (int8_t)(ALIEN_ROWS - 1); r >= 0; r--){
            if(!aliens[(uint8_t)r][col]) continue;
            int16_t ax = (int16_t)alien_offset_x + (int16_t)col * (ALIEN_W + ALIEN_GAP_X);
            int16_t ay = (int16_t)alien_offset_y + (int16_t)r * (ALIEN_H + ALIEN_GAP_Y);
            alien_shot_active = 1;
            alien_shot_x = (int8_t)(ax + (ALIEN_W / 2));
            alien_shot_y = (int8_t)(ay + ALIEN_H);
            return;
        }
    }
}

static void game_init(void){
    game_over = 0;
    game_paused = 0;
    game_win = 0;
    score = 0;
    lives = 1;

    {
        uint16_t seed = rng_state ^ rng_entropy();
        seed ^= (uint16_t)((seed << 7) | (seed >> 9));
        if(seed == 0) seed = 0xA5A5;
        rng_state = seed;
        srand(seed);
    }

    player_x = (LCD_WIDTH - SHIP_W) / 2;
    player_shot_active = 0;
    alien_shot_active = 0;
    player_invuln_ticks = 0;

    for(uint8_t r = 0; r < ALIEN_ROWS; r++){
        for(uint8_t c = 0; c < ALIEN_COLS; c++){
            aliens[r][c] = 1;
        }
    }

    alien_offset_x = (uint8_t)ALIEN_START_X;
    alien_offset_y = (uint8_t)ALIEN_START_Y;
    alien_dx = 1;
    alien_move_counter = 0;
    alien_anim = 0;
    alien_fire_cooldown = 10;
}

static void read_input(void){
    static uint8_t move_cooldown = 0;

    if(button_pressed_event(BTN_STOP)){
        game_paused ^= 1;
    }

    if(game_paused || game_over || game_win){
        return;
    }

    if(move_cooldown){
        move_cooldown--;
    }

    uint8_t want_left = button_down(BTN_LEFT);
    uint8_t want_right = button_down(BTN_RIGHT);

    // Igual que en Breakout: si no se presiona nada, permitir movimiento inmediato al volver a presionar
    if((!want_left && !want_right) || (want_left && want_right)){
        move_cooldown = 0;
    } else {
        // Suavizar: limitar la tasa de movimiento
        if(!move_cooldown){
            if(want_left && !want_right){
                if(player_x > PLAY_X_MIN){
                    player_x--;
                }
            } else if(want_right && !want_left){
                if(player_x < (uint8_t)(PLAY_X_MAX - SHIP_W + 1)){
                    player_x++;
                }
            }
            move_cooldown = SHIP_REPEAT_TICKS;
        }
    }

    uint8_t fire = 0;
    fire |= button_pressed_event(BTN_A);
    fire |= button_pressed_event(BTN_UP);

    if(fire && !player_shot_active){
        spawn_player_shot();
    }
}

static void game_update(void){
    if(game_over || game_win) return;
    if(game_paused) return;

    if(player_invuln_ticks){
        player_invuln_ticks--;
    }

    // Movimiento del disparo del jugador + colisiones con aliens
    if(player_shot_active){
        player_shot_y -= PLAYER_SHOT_SPEED;

        if(player_shot_y < PLAY_Y_MIN){
            player_shot_active = 0;
        } else {
            for(uint8_t r = 0; r < ALIEN_ROWS; r++){
                for(uint8_t c = 0; c < ALIEN_COLS; c++){
                    if(!aliens[r][c]) continue;

                    int16_t ax = (int16_t)alien_offset_x + (int16_t)c * (ALIEN_W + ALIEN_GAP_X);
                    int16_t ay = (int16_t)alien_offset_y + (int16_t)r * (ALIEN_H + ALIEN_GAP_Y);
                    if(aabb_overlap_i16(ax, ay, ALIEN_W, ALIEN_H,
                                        player_shot_x, player_shot_y, PLAYER_SHOT_W, PLAYER_SHOT_H)){
                        aliens[r][c] = 0;
                        player_shot_active = 0;
                        score += (uint16_t)((ALIEN_ROWS - r) * 10);
                        goto shot_done;
                    }
                }
            }
        }
    }
shot_done:

    // Disparo alien
    if(alien_shot_active){
        alien_shot_y += ALIEN_SHOT_SPEED;

        if(alien_shot_y > PLAY_Y_MAX){
            alien_shot_active = 0;
        } else if(!player_invuln_ticks){
            if(aabb_overlap_i16(player_x, SHIP_Y, SHIP_W, SHIP_H,
                                alien_shot_x, alien_shot_y, ALIEN_SHOT_W, ALIEN_SHOT_H)){
                alien_shot_active = 0;
                if(lives){
                    lives--;
                }
                if(lives == 0){
                    game_over = 1;
                    return;
                }
                player_invuln_ticks = 8;
                player_shot_active = 0;
                player_x = (LCD_WIDTH - SHIP_W) / 2;
                alien_fire_cooldown = 10;
            }
        }
    }

    // Cooldown de disparo alien
    if(!alien_shot_active){
        if(alien_fire_cooldown){
            alien_fire_cooldown--;
        } else {
            spawn_alien_shot();
            alien_fire_cooldown = (uint8_t)(6 + (rand() % 12));
        }
    }

    // Movimiento de la formación
    uint8_t alive = aliens_count_alive();
    if(alive == 0){
        game_win = 1;
        return;
    }

    uint8_t interval = alien_move_interval_ticks(alive);
    alien_move_counter++;
    if(alien_move_counter >= interval){
        alien_move_counter = 0;

        uint8_t min_col = 0;
        uint8_t max_col = 0;
        uint8_t max_row = 0;
        if(!aliens_get_bounds(&min_col, &max_col, &max_row)){
            game_win = 1;
            return;
        }

        int16_t left = (int16_t)alien_offset_x + (int16_t)min_col * (ALIEN_W + ALIEN_GAP_X);
        int16_t right = (int16_t)alien_offset_x + (int16_t)max_col * (ALIEN_W + ALIEN_GAP_X) + (ALIEN_W - 1);

        int16_t next_left = left + alien_dx;
        int16_t next_right = right + alien_dx;

        if(next_left < PLAY_X_MIN || next_right > PLAY_X_MAX){
            alien_dx = (int8_t)-alien_dx;
            alien_offset_y = (uint8_t)(alien_offset_y + ALIEN_DROP);
        } else {
            alien_offset_x = (uint8_t)(alien_offset_x + alien_dx);
        }

        alien_anim ^= 1;

        int16_t bottom = (int16_t)alien_offset_y + (int16_t)max_row * (ALIEN_H + ALIEN_GAP_Y) + (ALIEN_H - 1);
        if(bottom >= SHIP_Y){
            game_over = 1;
            return;
        }
    }
}

static void game_draw(void){
    lcd_clear_buffer();
    draw_border();

    lcd_draw_number(SCORE_X, SCORE_Y, score);

    for(uint8_t r = 0; r < ALIEN_ROWS; r++){
        for(uint8_t c = 0; c < ALIEN_COLS; c++){
            if(!aliens[r][c]) continue;
            uint8_t ax = (uint8_t)(alien_offset_x + c * (ALIEN_W + ALIEN_GAP_X));
            uint8_t ay = (uint8_t)(alien_offset_y + r * (ALIEN_H + ALIEN_GAP_Y));
            draw_alien(ax, ay, alien_anim);
        }
    }

    if(!player_invuln_ticks || (player_invuln_ticks & 1u) == 0u){
        draw_ship(player_x, SHIP_Y);
    }

    if(player_shot_active){
        draw_rect((uint8_t)player_shot_x, (uint8_t)player_shot_y, PLAYER_SHOT_W, PLAYER_SHOT_H);
    }
    if(alien_shot_active){
        draw_rect((uint8_t)alien_shot_x, (uint8_t)alien_shot_y, ALIEN_SHOT_W, ALIEN_SHOT_H);
    }

    if(game_over){
        lcd_draw_game_over_text();
    } else if(game_win){
        lcd_draw_win_text();
    } else if(game_paused){
        lcd_draw_pause_text();
    }

    lcd_update();
}

int main(void){
    lcd_init();
    buttons_init();
    rng_init();
    buttons_reset();
    game_init();

    uint8_t prev_end = 0;
    uint8_t restart_armed = 0;

    while(1){
        uint8_t end = (game_over || game_win) ? 1 : 0;
        if(end && !prev_end){
            buttons_reset();
            restart_armed = 0;
        }
        prev_end = end;

        for(uint8_t i = 0; i < (GAME_TICK_MS / INPUT_POLL_MS); i++){
            buttons_poll();

            if(end){
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
