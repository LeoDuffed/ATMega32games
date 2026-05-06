/* TODO

    - Que desaparezcan los cuadros hasta el final de la pantalla
    - mostrar texto de "GAME OVER" y "PAUSE"
    - arreglar el contador de SCORE (hay que hacerlo como el de snake)
    - 

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
#define BTN_PIN PIND
#define BTN_DDR DDRD
#define BTN_LEFT PD2
#define BTN_RIGHT PD3
#define BTN_STOP PD4
# define BTN_MASK ((1 << BTN_LEFT) | (1 << BTN_RIGHT) | (1 << BTN_STOP))

#define INPUT_POLL_MS 10
#define GAME_TICK_MS 180
#define BTN_DEBOUNCE_TICKS 2

#define LCD_WIDTH   84
#define LCD_HEIGHT  48
static uint8_t lcd_buffer[504];

#define CELL_SIZE   4
#define GRID_W (LCD_WIDTH / CELL_SIZE)   // 21
#define GRID_H (LCD_HEIGHT / CELL_SIZE)  // 12

#define PLAYER_Y 10
#define NUM_OBS 4

typedef struct {
    uint8_t x;
    uint8_t y;
} Point;

static uint8_t player_x = 10;
static Point obs[NUM_OBS];
static uint16_t score = 0;
static uint8_t game_over = 0;
static uint8_t game_paused = 0;

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

static void draw_cell(uint8_t gx, uint8_t gy){
    uint8_t px = gx * CELL_SIZE;
    uint8_t py = gy * CELL_SIZE;

    for(uint8_t y = 0; y < CELL_SIZE; y++){
        for(uint8_t x = 0; x < CELL_SIZE; x++){
            lcd_set_pixel(px + x, py + y, 1);
        }
    }
}

static const uint8_t digits[10][5] = {
    {7,5,5,5,7}, // 0
    {2,6,2,2,7}, // 1
    {7,1,7,4,7}, // 2
    {7,1,7,1,7}, // 3
    {5,5,7,1,1}, // 4
    {7,4,7,1,7}, // 5
    {7,4,7,5,7}, // 6
    {7,1,1,1,1}, // 7
    {7,5,7,5,7}, // 8
    {7,5,7,1,7}  // 9
};

static void draw_digit(uint8_t x, uint8_t y, uint8_t n){
    for(uint8_t row = 0; row < 5; row++){
        for(uint8_t col = 0; col < 3; col++){
            if(digits[n][row] & (1 << (2 - col))){
                lcd_set_pixel(x + col, y + row, 1);
            }
        }
    }
}

static void draw_score(uint8_t value){
    uint8_t centenas = (value / 100) % 10;
    uint8_t decenas = (value / 10) % 10;
    uint8_t unidades = value % 10;

    draw_digit(3, 2, centenas);
    draw_digit(8, 2, decenas);
    draw_digit(13, 2, unidades);
}

static void game_draw(void) {
    lcd_clear_buffer(); 
    draw_border();

    draw_score(score);
    draw_cell(player_x, PLAYER_Y);

    for(uint8_t i = 0; i < NUM_OBS; i++){
        draw_cell(obs[i].x, obs[i].y);
    }

    lcd_update();
}

static void buttons_init(void){
    BTN_DDR &= ~BTN_MASK;
    BTN_PORT |= BTN_MASK;
}

static uint8_t buttons_read_raw(void){
    uint8_t value = 0;

    if (!(BTN_PIN & (1 << BTN_LEFT))) value |= (1 << BTN_LEFT);
    if (!(BTN_PIN & (1 << BTN_RIGHT))) value |= (1 << BTN_RIGHT);
    if (!(BTN_PIN & (1 << BTN_STOP))) value |= (1 << BTN_STOP);

    return value;
}

static uint8_t buttons_read_debounce(void){
    static uint8_t last_raw = 0;
    static uint8_t stable = 0;
    static uint8_t count = 0;

    uint8_t raw = buttons_read_raw();

    if(raw == last_raw){
        if(count < BTN_DEBOUNCE_TICKS){
            count++;
        } else {
            stable = raw;
        }
    } else {
        count = 0;
        last_raw = raw;
    }

    return stable;
}

static void spawn_obstacle(uint8_t i){
    obs[i].x = 1 + (rand() % (GRID_W - 2));
    obs[i].y = 1;  
}

static void game_reset(void){
    player_x = GRID_W / 2;
    score = 0;
    game_over = 0;
    game_paused = 0;

    srand(7);

    for(uint8_t i = 0; i < NUM_OBS; i++){
        obs[i].x = 1 + (rand() % (GRID_W - 2));
        obs[i].y = 1 + i * 2;
    }

    game_draw();
}

static void game_update(void){
    if(game_paused || game_over) return;

    for(uint8_t i = 0; i < NUM_OBS; i++){
        obs[i].y++;

        if(obs[i].x == player_x && obs[i].y == PLAYER_Y){
            game_over = 1;
            return;
        }

        if(obs[i].y >= GRID_H - 1){
            score++;
            spawn_obstacle(i);
        }
    }
}

static void game_input(uint8_t pressed_edges){
    if(pressed_edges & (1 << BTN_STOP)){
        if(game_over){
            game_reset();
        } else {
            game_paused = !game_paused;
        }
    }

    if(game_paused || game_over) return;

    if(pressed_edges & (1 << BTN_LEFT)){
        if(player_x > 1){
            player_x--;
        }
    }

    if(pressed_edges & (1 << BTN_RIGHT)){
        if(player_x < GRID_W - 2){
            player_x++;
        }
    }
}

int main(void){
    lcd_init();
    buttons_init();

    game_reset();

    uint16_t elapsed_ms = 0;
    uint8_t prev_buttons = 0;

    while(1){
        uint8_t buttons = buttons_read_debounce();
        uint8_t edges = buttons & ~prev_buttons;
        prev_buttons = buttons;

        game_input(edges);

        elapsed_ms += INPUT_POLL_MS;

        if(elapsed_ms >= GAME_TICK_MS){
            elapsed_ms = 0;
            game_update();
            game_draw();
        }

        _delay_ms(INPUT_POLL_MS);
    }

    return 0;
}