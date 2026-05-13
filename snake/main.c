#define F_CPU 8000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define LCD_PORT PORTB
#define LCD_DDR  DDRB

#define LCD_RST  PB0
#define LCD_CE   PB4
#define LCD_DC   PB1
#define LCD_DIN  PB5
#define LCD_CLK  PB7

#define BTN_PORT PORTA
#define BTN_PIN  PINA
#define BTN_DDR  DDRA

#define BTN_UP     PA0
#define BTN_DOWN   PA1
#define BTN_LEFT   PA2
#define BTN_RIGHT  PA3
#define BTN_STOP   PA4

#define BTN_MASK ((1 << BTN_UP) | (1 << BTN_DOWN) | (1 << BTN_LEFT) | (1 << BTN_RIGHT) | (1 << BTN_STOP))

#define INPUT_POLL_MS 10
#define GAME_TICK_MS 180
#define BTN_DEBOUNCE_TICKS 2

#define LCD_WIDTH   84
#define LCD_HEIGHT  48
static uint8_t lcd_buffer[504];

#define CELL_SIZE   4
#define GRID_W      (LCD_WIDTH / CELL_SIZE)   // 21
#define GRID_H      (LCD_HEIGHT / CELL_SIZE)  // 12
#define MAX_SNAKE   64

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
    BTN_IDX_STOP,
    BTN_COUNT
} ButtonIndex;

static uint8_t btn_state;
static uint8_t btn_press_events;
static uint8_t btn_release_events;
static uint8_t btn_debounce_cnt[BTN_COUNT];

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

static void lcd_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color) {
    for (uint8_t i = 0; i < w; i++) {
        for (uint8_t j = 0; j < h; j++) {
            lcd_set_pixel(x + i, y + j, color);
        }
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

static void lcd_draw_char_digit(uint8_t x, uint8_t y, char c) {
    if (c < '0' || c > '9') return;
    uint8_t idx = c - '0';

    for (uint8_t col = 0; col < 5; col++) {
        uint8_t bits = font5x7[idx][col];
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

static void lcd_draw_game_over_text(void) {
    const uint8_t *msg[] = {G_, A_, M_, E_, space_, O_, V_, E_, R_};
    uint8_t x = 12;
    uint8_t y = 18;

    for (uint8_t i = 0; i < 9; i++) {
        lcd_draw_pattern_char(x + i * 6, y, msg[i]);
    }
}

static void lcd_draw_pause_text(void) {
    const uint8_t *msg[] = {P_, A_, U_, S_, E_};
    uint8_t x = 27;
    uint8_t y = 18;

    for (uint8_t i = 0; i < 5; i++) {
        lcd_draw_pattern_char(x + i * 6, y, msg[i]);
    }
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
        (1 << BTN_STOP),
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
    if (button_pressed_event(BTN_STOP)) {
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

// Main
int main(void) {
    lcd_init();
    buttons_init();
    rng_init();
    buttons_reset();
    game_init();
    uint8_t prev_game_over = 0;
    uint8_t restart_armed = 0;

    while (1) {
        // Al entrar a GAME OVER, limpiar eventos y requerir soltar botones antes de reiniciar
        if (game_over && !prev_game_over) {
            buttons_reset();
            restart_armed = 0;
        }
        prev_game_over = game_over;

        // Polling rápido para mejor respuesta + antirrebote
        for (uint8_t i = 0; i < (GAME_TICK_MS / INPUT_POLL_MS); i++) {
            buttons_poll();

            if (game_over) {
                if (!restart_armed) {
                    if (btn_state == 0) restart_armed = 1; // esperar a que suelten todo
                } else if (any_button_pressed_event()) {
                    game_init();
                    buttons_reset();
                    prev_game_over = 0;
                    restart_armed = 0;
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
    }

    return 0;
}
