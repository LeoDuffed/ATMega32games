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
#define BTN_A PA4
#define BTN_B PA5

#define BTN_MASK ((1 << BTN_UP) | (1 << BTN_DOWN) | (1 << BTN_A) | (1 << BTN_B))

#define INPUT_POLL_MS 10
#define BTN_DEBOUNCE_TICKS 2

#define LCD_WIDTH   84
#define LCD_HEIGHT  48
static uint8_t lcd_buffer[504];

typedef struct {
    uint8_t x;
    uint8_t y;
} Point;

typedef enum {
    BTN_IDX_UP,
    BTN_IDX_DOWN,
    BTN_IDX_A,
    BTN_IDX_B,
    BTN_COUNT
} ButtonIndex;

static uint8_t btn_state;
static uint8_t btn_press_events;
static uint8_t btn_debounce_cnt[BTN_COUNT];

static uint16_t score1 = 129;
static uint16_t score2 = 70; 
static uint16_t score3 = 32;
static uint16_t score4 = 17;

typedef enum {
    MENU_ITEM_PLAY,
    MENU_ITEM_LOAD,
    MENU_ITEM_COUNT
} MenuItem;
static uint8_t menu_selection = (uint8_t)MENU_ITEM_PLAY;

typedef enum {
    APP_MENU,
    APP_INSTRUCCION,
    APP_SCORE,
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
    else         lcd_dc_cmd();

    lcd_ce_low();

    for (uint8_t i = 0; i < 8; i++) {
        if (data & 0x80) lcd_din_high();
        else             lcd_din_low();

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
    LCD_DDR |= (1 << LCD_RST) | (1 << LCD_CE) | (1 << LCD_DC) |
               (1 << LCD_DIN) | (1 << LCD_CLK);

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

static const uint8_t J_[5] = {0x20,0x40,0x41,0x3F,0x01};
static const uint8_t U_[5] = {0x3F,0x40,0x40,0x40,0x3F};
static const uint8_t G_[5] = {0x3E,0x41,0x49,0x49,0x7A};
static const uint8_t A_[5] = {0x7E,0x11,0x11,0x11,0x7E};
static const uint8_t R_[5] = {0x7F,0x09,0x19,0x29,0x46};
static const uint8_t C_[5] = {0x3E,0x41,0x41,0x41,0x22};
static const uint8_t B_[5] = {0x7F,0x49,0x49,0x49,0x36};
static const uint8_t S_[5] = {0x26,0x49,0x49,0x49,0x32};
static const uint8_t O_[5] = {0x3E,0x41,0x41,0x41,0x3E};
static const uint8_t E_[5] = {0x7F,0x49,0x49,0x49,0x41};

static const uint8_t a_[5] = {0x20,0x54,0x54,0x54,0x78};
static const uint8_t b_[5] = {0x7F,0x44,0x44,0x44,0x38};
static const uint8_t c_[5] = {0x38,0x44,0x44,0x44,0x28};
static const uint8_t d_[5] = {0x38,0x44,0x44,0x44,0x7F};
static const uint8_t e_[5] = {0x38,0x54,0x54,0x54,0x18};
static const uint8_t i_[5] = {0x00,0x44,0x7D,0x40,0x00};
static const uint8_t l_[5] = {0x00,0x41,0x7F,0x40,0x00};
static const uint8_t n_[5] = {0x7C,0x04,0x04,0x04,0x78};
static const uint8_t o_[5] = {0x38,0x44,0x44,0x44,0x38};
static const uint8_t p_[5] = {0x7C,0x14,0x14,0x14,0x08};
static const uint8_t r_[5] = {0x7C,0x08,0x04,0x04,0x08};
static const uint8_t s_[5] = {0x48,0x54,0x54,0x54,0x24};
static const uint8_t t_[5] = {0x04,0x3F,0x44,0x40,0x20};
static const uint8_t u_[5] = {0x3C,0x40,0x40,0x20,0x7C};
static const uint8_t m_[5] = {0x7C,0x04,0x18,0x04,0x78};

static const uint8_t espacio[5]={0,0,0,0,0};
static const uint8_t punto[5]={0,0,0x60,0x60,0};

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
        uint8_t bits = p[col];
        for (uint8_t row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                lcd_set_pixel(x + col, y + row, 1);
            }
        }
    }
}

static void lcd_draw_jugar_text(void){
    const uint8_t *msg[] = {J_, U_, G_, A_, R_};
    uint8_t x = 26;
    uint8_t y = 14;

    for(uint8_t i = 0; i < 5; i++){
        lcd_draw_pattern_char(x + i * 6, y, msg[i]);
    }
}

static void lcd_draw_cargar_text(void){
    const uint8_t *msg[] = {C_ ,A_ ,R_ ,G_, A_, R_};
    uint8_t x = 24;
    uint8_t y = 27;

    for(uint8_t i = 0; i < 6; i++){
        lcd_draw_pattern_char(x + i * 6, y, msg[i]);
    }
}

static void lcd_draw_B_text(void){
    const uint8_t *msg[] = {B_};
    uint8_t x = 75;
    uint8_t y = 39;

    for(uint8_t i = 0; i < 1; i++){
        lcd_draw_pattern_char(x + i * 6, y, msg[i]);
    }
}

static void lcd_draw_text_one(void){
    const uint8_t *msg[] = {C_,o_,n_,e_,c_,t_,a_,espacio,l_,a_};
    uint8_t x = 6;
    uint8_t y = 4;

    for(uint8_t i = 0; i < 10; i++){
        lcd_draw_pattern_char(x + i * 6, y, msg[i]);
    }
}

static void lcd_draw_text_two(void){
    const uint8_t *msg[] = {c_,o_,n_,s_,o_,l_,a_,espacio,a_,espacio,l_,a_};
    uint8_t x = 6;
    uint8_t y = 12;

    for(uint8_t i = 0; i < 12; i++){
        lcd_draw_pattern_char(x + i * 6, y, msg[i]);
    }
}

static void lcd_draw_text_tree(void){
    const uint8_t *msg[] = {c_,o_,m_,p_,u_,t_,a_,d_,o_,r_,a_};
    uint8_t x = 6;
    uint8_t y = 20;

    for(uint8_t i = 0; i < 11; i++){
        lcd_draw_pattern_char(x + i * 6, y, msg[i]);
    }
}

static void lcd_draw_text_four(void){
    const uint8_t *msg[] = {c_,o_,n_,espacio,e_,l_,espacio,c_,a_,b_,l_,e_};
    uint8_t x = 6;
    uint8_t y = 28;

    for(uint8_t i = 0; i < 12; i++){
        lcd_draw_pattern_char(x + i * 6, y, msg[i]);
    }
}

static void lcd_draw_text_five(void){
    const uint8_t *msg[] = {e_,s_,p_,e_,c_,i_,a_,l_};
    uint8_t x = 6;
    uint8_t y = 36;

    for(uint8_t i = 0; i < 8; i++){
        lcd_draw_pattern_char(x + i * 6, y, msg[i]);
    }
}

static void lcd_draw_score_text(void){
    const uint8_t *msg[] = {S_, C_, O_, R_, E_};
    uint8_t x = 29;
    uint8_t y = 2;

    for(uint8_t i = 0; i < 5; i++){
        lcd_draw_pattern_char(x + i * 6, y, msg[i]);
    }
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
        uint8_t bits = font5x7[idx][col];
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
        lcd_draw_char_digit(x + pos * 6, y, buf[pos]);
        pos++;
    }
}

static void lcd_draw_rank(uint8_t x, uint8_t y, uint8_t rank){
    lcd_draw_number(x, y, rank);
    lcd_draw_pattern_char(x + 6, y, punto);
}

static void lcd_draw_score_row(uint8_t y, uint8_t rank, uint16_t score){
    lcd_draw_rank(3, y, rank);
    lcd_draw_number(20, y, score);
}

// botones
static void buttons_init(void) {
    BTN_DDR &= (uint8_t)~BTN_MASK;   
    BTN_PORT |= BTN_MASK;            
}

static uint8_t buttons_raw_mask(void){
    return (uint8_t)(~BTN_PIN) & (uint8_t)BTN_MASK;
}

static void buttons_reset(void){
    btn_state = buttons_raw_mask();
    btn_press_events = 0;
    for(uint8_t i = 0; i < BTN_COUNT; i++){
        btn_debounce_cnt[i] = 0;
    }
}

static void buttons_poll(void){
    static const uint8_t bits[BTN_COUNT] = {
        (1 << BTN_UP),
        (1 << BTN_DOWN),
        (1 << BTN_A),
        (1 << BTN_B),
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
            }
        }
    }
}

static uint8_t button_pressed_event(uint8_t pin){
    uint8_t mask = (1 << pin);
    uint8_t v = (btn_press_events & mask) ? 1 : 0;
    btn_press_events &= (uint8_t)~mask;
    return v;
}

// Menu

static const uint8_t CURSOR_R_[5] = {0x00, 0x3E, 0x1C, 0x08, 0x00};

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
    lcd_draw_jugar_text();
    lcd_draw_cargar_text();

    if(menu_selection == (uint8_t)MENU_ITEM_PLAY){
        lcd_draw_cursor(16, 14);
    } else {
        lcd_draw_cursor(16, 27);
    }

    lcd_update();
}

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

static void score_draw(void){
    lcd_clear_buffer();

    draw_border();

    lcd_draw_score_text();
    lcd_draw_score_row(12, 1, score1);
    lcd_draw_score_row(20, 2, score2);
    lcd_draw_score_row(28, 3, score3);
    lcd_draw_score_row(36, 4, score4);

    lcd_update();
}

int main(void){
    lcd_init();
    buttons_init();
    buttons_reset();

    while (1) {
        buttons_poll();

        if(app_state == APP_MENU){
            menu_update();

            if(button_pressed_event(BTN_A)){
                if(menu_selection == (uint8_t)MENU_ITEM_LOAD){
                    app_state = APP_INSTRUCCION;
                } else if(menu_selection == (uint8_t)MENU_ITEM_PLAY){
                    app_state = APP_SCORE;
                }
            }

            menu_draw();
        
        } else if(app_state == APP_INSTRUCCION){
            if(button_pressed_event(BTN_B)){
                app_state = APP_MENU;
            }

            instruction_draw();
        } else if(app_state == APP_SCORE){
            score_draw();
        }

        _delay_ms(INPUT_POLL_MS);
    } 

    return 0;
    
}