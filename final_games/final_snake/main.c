#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define LDR_PIN PA6
#define LDR_ADC_CHANNEL 6
#define LDR_DARK_THRESHOLD 512
#define LED_PIN PB6

#define LCD_PORT PORTB
#define LCD_DDR DDRB

#define LCD_RST PB0
#define LCD_CE PB4
#define LCD_DC PB1
#define LCD_DIN PB5
#define LCD_CLK PB7

#define BTN_PORT PORTA
#define BTN_PIN PINA
#define BTN_DDR DDRA

#define BTN_UP PA0
#define BTN_DOWN PA1
#define BTN_LEFT PA2
#define BTN_RIGHT PA3
#define BTN_A PA4
#define BTN_B PA5

#define BTN_MASK ((1 << BTN_UP) | (1 << BTN_DOWN) | (1 << BTN_LEFT) | (1 << BTN_RIGHT) | (1 << BTN_A) | (1 << BTN_B))

#define INPUT_POLL_MS 10
#define GAME_TICK_MS 180
#define BTN_DEBOUNCE_TICKS 2

#define LCD_WIDTH 84
#define LCD_HEIGHT 48
static uint8_t lcd_buffer[504];

#define CELL_SIZE 4
#define GRID_W (LCD_WIDTH / CELL_SIZE) // 21
#define GRID_H (LCD_HEIGHT / CELL_SIZE) // 12
#define MAX_SNAKE 64

#define EEPROM_ADDR 0x50

#define EE_MAGIC_0 0x0000
#define EE_MAGIC_1 0x0001
#define EE_VERSION 0x0002

#define MAGIC_0 'R'
#define MAGIC_1 'P'
#define EEPROM_VERSION 1

#define EE_SCORE_BASE 0x0100 // SNAKE
#define TOP_COUNT 4

static uint8_t highscore_saved;

typedef struct {
    uint8_t x;
    uint8_t y;
} Point;

static Point snake[MAX_SNAKE];
static uint8_t snake_length;
static Point food;

typedef enum {
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} Direction;

static Direction dir;
static Direction next_dir;
static uint8_t game_over;
static uint8_t game_stop;
static uint16_t score;
static uint16_t rng_state __attribute__((section(".noinit")));

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

static void lcd_ce_low(void){ 
    LCD_PORT &= ~(1 << LCD_CE); 
}
static void lcd_ce_high(void){
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
    if (is_data) lcd_dc_data();
    else lcd_dc_cmd();

    lcd_ce_low();

    for (uint8_t i = 0; i < 8; i++) {
        if (data & 0x80) lcd_din_high();
        else lcd_din_low();

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

    if (color)
        lcd_buffer[index] |= (1 << (y % 8));
    else
        lcd_buffer[index] &= ~(1 << (y % 8));
}

static void lcd_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color) {
    for (uint8_t i = 0; i < w; i++) {
        for (uint8_t j = 0; j < h; j++) {
            lcd_set_pixel(x + i, y + j, color);
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

static void lcd_draw_snake_text(void){
    const uint8_t *msg[] = {S_,N_,A_,K_,E_};
    lcd_draw_pattern_text(26, 14, msg, 5);
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

// numeros que se muestran
static const uint8_t font5x7[][5] PROGMEM = {
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

// Botones
static void buttons_init(void) {
    BTN_DDR &= ~(BTN_MASK);
    // Pull-ups internos (botones a GND)
    BTN_PORT |= BTN_MASK;
}

static uint8_t buttons_raw_mask(void) {
    // 1 = presionado (activo en LOW por pull-up)
    return (uint8_t)(~BTN_PIN) & (uint8_t)BTN_MASK;
}

static void buttons_reset(void) {
    btn_state = buttons_raw_mask();
    btn_press_events = 0;
    btn_release_events = 0;
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
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

static uint8_t button_down(uint8_t pin) {
    return (btn_state & (1 << pin)) ? 1 : 0;
}

static uint8_t button_pressed_event(uint8_t pin) {
    uint8_t mask = (1 << pin);
    uint8_t v = (btn_press_events & mask) ? 1 : 0;
    btn_press_events &= (uint8_t)~mask;
    return v;
}

static uint8_t any_button_pressed_event(void) {
    uint8_t v = btn_press_events;
    btn_press_events = 0;
    return v ? 1 : 0;
}

// Entropía simple usando Timer0 + estado de botones
static void rng_init(void) {
    // Timer0 en modo normal, prescaler /64
    TCCR0 = (1 << CS01) | (1 << CS00);
}

static uint16_t rng_entropy(void) {
    uint8_t t = TCNT0;
    uint8_t p = BTN_PIN;
    return ((uint16_t)t << 8) | (uint16_t)(t ^ p);
}

static void read_input(void) {
    // Pausa: toggle al presionar
    if (button_pressed_event(BTN_B)) {
        game_stop ^= 1;
    }

    // Evitar reversa directa
    if (button_down(BTN_UP) && dir != DIR_DOWN) {
        next_dir = DIR_UP;
    } else if (button_down(BTN_DOWN) && dir != DIR_UP) {
        next_dir = DIR_DOWN;
    } else if (button_down(BTN_LEFT) && dir != DIR_RIGHT) {
        next_dir = DIR_LEFT;
    } else if (button_down(BTN_RIGHT) && dir != DIR_LEFT) {
        next_dir = DIR_RIGHT;
    }
}

// menu
static const uint8_t CURSOR_R_[5] PROGMEM = {0x00, 0x3E, 0x1C, 0x08, 0x00};

static void lcd_draw_cursor(uint8_t x, uint8_t y){
    lcd_draw_pattern_char(x, y, CURSOR_R_);
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

// Juego
static uint8_t snake_hits_itself(uint8_t x, uint8_t y) {
    for (uint8_t i = 0; i < snake_length; i++) {
        if (snake[i].x == x && snake[i].y == y) return 1;
    }
    return 0;
}

static void place_food(void) {
    Point prev = food;
    uint16_t attempts = (uint16_t)GRID_W * (uint16_t)GRID_H * 4;

    while (attempts--) {
        uint8_t fx = rand() % GRID_W;
        uint8_t fy = rand() % GRID_H;

        if (fx == prev.x && fy == prev.y) continue;
        if (snake_hits_itself(fx, fy)) continue;

        food.x = fx;
        food.y = fy;
        return;
    }

    // Fallback (si ya casi no hay espacio): permitir repetir posición previa
    attempts = (uint16_t)GRID_W * (uint16_t)GRID_H * 4;
    while (attempts--) {
        uint8_t fx = rand() % GRID_W;
        uint8_t fy = rand() % GRID_H;

        if (!snake_hits_itself(fx, fy)) {
            food.x = fx;
            food.y = fy;
            return;
        }
    }
}

static void game_init(void) {
    snake_length = 3;
    score = 0;
    game_over = 0;
    game_stop = 0;

    snake[0].x = 10; snake[0].y = 6;
    snake[1].x = 9;  snake[1].y = 6;
    snake[2].x = 8;  snake[2].y = 6;

    dir = DIR_RIGHT;
    next_dir = DIR_RIGHT;

    food.x = 0xFF;
    food.y = 0xFF;
    {
        uint16_t seed = rng_state ^ rng_entropy();
        seed ^= (uint16_t)((seed << 7) | (seed >> 9));
        if (seed == 0) seed = 0xA5A5;
        rng_state = seed;
        srand(seed);
    }
    place_food();
}

static void game_update(void) {
    if (game_over) return;
    if (game_stop) return;

    dir = next_dir;

    Point new_head = snake[0];

    switch (dir) {
        case DIR_UP:
            if (new_head.y == 0) {
                game_over = 1;
                return;
            }
            new_head.y--;
            break;

        case DIR_DOWN:
            if (new_head.y >= GRID_H - 1) {
                game_over = 1;
                return;
            }
            new_head.y++;
            break;

        case DIR_LEFT:
            if (new_head.x == 0) {
                game_over = 1;
                return;
            }
            new_head.x--;
            break;

        case DIR_RIGHT:
            if (new_head.x >= GRID_W - 1) {
                game_over = 1;
                return;
            }
            new_head.x++;
            break;
    }

    // Colisión consigo misma
    for (uint8_t i = 0; i < snake_length; i++) {
        if (snake[i].x == new_head.x && snake[i].y == new_head.y) {
            game_over = 1;
            return;
        }
    }

    // Desplazar cuerpo
    for (int8_t i = snake_length; i > 0; i--) {
        if (i < MAX_SNAKE) {
            snake[i] = snake[i - 1];
        }
    }

    snake[0] = new_head;

    // Comer
    if (new_head.x == food.x && new_head.y == food.y) {
        if (snake_length < MAX_SNAKE - 1) {
            snake_length++;
        }
        score++;
        place_food();
    } else {
        // Si no comió, eliminar cola lógica
        // ya se desplazó todo, así que la longitud se mantiene
    }
}

static void game_draw(void) {
    lcd_clear_buffer();

    // Opcional: borde
    draw_border();

    // Dibujar comida
    lcd_fill_rect(food.x * CELL_SIZE, food.y * CELL_SIZE, CELL_SIZE, CELL_SIZE, 1);

    // Dibujar snake
    for (uint8_t i = 0; i < snake_length; i++) {
        // Cabeza ligeramente distinta
        if (i == 0) {
            lcd_fill_rect(snake[i].x * CELL_SIZE, snake[i].y * CELL_SIZE, CELL_SIZE, CELL_SIZE, 1);
            lcd_set_pixel(snake[i].x * CELL_SIZE + 1, snake[i].y * CELL_SIZE + 1, 0);
        } else {
            lcd_fill_rect(snake[i].x * CELL_SIZE, snake[i].y * CELL_SIZE, CELL_SIZE, CELL_SIZE, 1);
        }
    }

    // Score arriba izquierda
    lcd_draw_number(2, 2, score);

    if (game_over) {
        lcd_draw_game_over_text();
    } else if (game_stop) {
        lcd_draw_pause_text();
    }

    lcd_update();
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

static uint16_t adc_read(uint8_t channel) {
    ADMUX = (ADMUX & 0xE0) | (channel & 0x1F);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC)) {
    }
    return ADC;
}

static void ldr_led_init(void) {
    // PA6/ADC6 como entrada analogica
    DDRA &= ~(1 << LDR_PIN);
    // Pull-up interno desactivado
    PORTA &= ~(1 << LDR_PIN);

    // ADC habilitado, referencia AVCC, prescaler /128
    ADMUX = (1 << REFS0) | (LDR_ADC_CHANNEL & 0x1F);
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
    (void)adc_read(LDR_ADC_CHANNEL);

    // PB6 como salida
    DDRB |= (1 << LED_PIN);
    // LED apagado al inicio
    PORTB &= ~(1 << LED_PIN);
}

static void ldr_led_update(void) {
    // Lectura alta = poca luz, segun el divisor usado
    uint16_t ldr_value = adc_read(LDR_ADC_CHANNEL);
    if (ldr_value >= LDR_DARK_THRESHOLD) {
        PORTB |= (1 << LED_PIN);   // prende LED
    } else {
        PORTB &= ~(1 << LED_PIN);  // apaga LED
    }
}

// Main
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

            // Al entrar a GAME OVER, limpiar eventos y requerir soltar botones antes de reiniciar
            if (game_over && !prev_game_over) {
                if(!highscore_saved){
                    scores_try_insert(score);
                    highscore_saved = 1;
                }
                buttons_reset();
                restart_armed = 0;
            }
            prev_game_over = game_over;

            // Polling rápido para mejor respuesta + antirrebote
            for (uint8_t i = 0; i < (GAME_TICK_MS / INPUT_POLL_MS); i++) {
                ldr_led_update();
                buttons_poll();

                if (game_over) {
                    if (!restart_armed) {
                        if (btn_state == 0) restart_armed = 1; // esperar a que suelten todo
                    } else if (any_button_pressed_event()) {
                        app_state = APP_END;
                        buttons_reset();
                    }
                } else {
                    read_input();
                }

                _delay_ms(INPUT_POLL_MS);
            }

            if (!game_over) {
                game_update();
            }

            game_draw();

        } else if(app_state == APP_END){
            end_game_update();

            if(button_pressed_event(BTN_A)){
                if(end_selection == (uint8_t)END_ITEM_PLAY){
                    game_init();
                    prev_game_over = 0;
                    restart_armed = 0;
                    highscore_saved = 0;
                    app_state = APP_GAME;
                    buttons_reset();
                
                } else if(end_selection == (uint8_t)END_ITEM_SCAPE){
                    app_state = APP_MENU;
                    buttons_reset();
                }
            }

            end_game_draw();
        }
        
    }
    return 0;
}
