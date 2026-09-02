#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdint.h>
#include <string.h>

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
#define BTN_A      PA4
#define BTN_B      PA5

#define BTN_MASK ((1 << BTN_UP) | (1 << BTN_DOWN) | (1 << BTN_LEFT) | (1 << BTN_RIGHT) | (1 << BTN_A) | (1 << BTN_B))

#define LCD_WIDTH  84
#define LCD_HEIGHT 48

#define TILE_SIZE 4
#define SCREEN_W  21
#define SCREEN_H  12
#define WORLD_W   2
#define WORLD_H   2
#define ROOM_COUNT 4

#define INPUT_POLL_MS 10
#define BTN_DEBOUNCE_TICKS 2

#define TILE_FLOOR 0
#define TILE_TREE  1
#define TILE_GRASS 2
#define TILE_WATER 3
#define TILE_HOUSE 4
#define TILE_ROCK  5
#define TILE_MON   6

#define BATTLE_PLAYER_MAX_HP 10
#define MON_COUNT 3

static uint8_t lcd_buffer[504];

typedef enum {
    DIR_UP,
    DIR_RIGHT,
    DIR_DOWN,
    DIR_LEFT
} Direction;

typedef enum {
    BTN_IDX_UP,
    BTN_IDX_DOWN,
    BTN_IDX_LEFT,
    BTN_IDX_RIGHT,
    BTN_IDX_A,
    BTN_IDX_B,
    BTN_COUNT
} ButtonIndex;

typedef enum {
    STATE_MAP,
    STATE_BATTLE,
    STATE_BATTLE_DONE
} GameState;

typedef struct {
    uint8_t max_hp;
    uint8_t attack;
    uint8_t capture_rate;
} MonsterStats;

static const MonsterStats monsters[MON_COUNT] PROGMEM = {
    {5, 1, 80},
    {7, 2, 60},
    {9, 2, 45}
};

static const uint8_t rooms[ROOM_COUNT][SCREEN_H][SCREEN_W] PROGMEM = {
    {
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,0,0,0,0},
        {1,0,0,4,4,4,0,0,0,0,0,0,2,2,2,2,2,0,5,0,0},
        {1,0,0,4,0,4,0,0,1,1,0,0,2,2,2,6,2,0,0,0,0},
        {1,0,0,4,4,4,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,0,0,0,0,0,0,0,0,0,5,0,0,0,1,1,1,1,0,0,0},
        {1,0,2,2,2,2,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0},
        {1,0,2,2,2,2,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0},
        {1,0,2,2,6,2,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,1,1,1,1}
    },
    {
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {0,0,3,3,3,3,3,3,0,0,0,2,2,2,2,2,2,0,0,0,1},
        {0,0,3,3,3,3,3,3,0,0,0,2,2,2,2,2,2,0,5,0,1},
        {0,0,3,3,3,3,3,3,0,0,0,2,2,6,2,2,2,0,0,0,1},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {0,0,0,0,0,0,1,1,1,1,1,0,0,0,4,4,4,0,0,0,1},
        {0,0,2,2,2,0,1,0,0,0,1,0,0,0,4,0,4,0,0,0,1},
        {0,0,2,6,2,0,1,0,5,0,1,0,0,0,4,4,4,0,0,0,1},
        {0,0,2,2,2,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
        {0,0,0,0,0,0,1,1,0,0,1,0,0,0,0,0,0,0,0,0,1},
        {1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,1,1,1,1}
    },
    {
        {1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,1,1,1,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,0,2,2,2,2,2,0,0,0,0,0,1,1,1,1,0,0,0,0,0},
        {1,0,2,6,2,2,2,0,0,0,5,0,1,0,0,1,0,0,0,0,0},
        {1,0,2,2,2,2,2,0,0,0,0,0,1,0,0,1,0,0,0,0,0},
        {1,0,0,0,0,0,0,0,0,0,0,0,1,1,0,1,0,0,0,0,0},
        {1,0,0,0,3,3,3,3,3,0,0,0,0,0,0,1,0,0,0,0,0},
        {1,0,0,0,3,3,3,3,3,0,0,0,0,0,0,1,0,0,0,0,0},
        {1,0,0,0,3,3,3,3,3,0,0,0,0,2,2,2,2,2,0,0,0},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,2,2,6,2,2,0,0,0},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
    },
    {
        {1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,1,1,1,1},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {0,0,0,0,1,1,1,1,1,1,0,0,0,2,2,2,2,2,0,0,1},
        {0,0,0,0,1,0,0,0,0,1,0,0,0,2,2,6,2,2,0,0,1},
        {0,0,0,0,1,0,5,0,0,1,0,0,0,2,2,2,2,2,0,0,1},
        {0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1},
        {0,0,0,0,1,1,0,0,1,1,0,0,3,3,3,3,3,3,0,0,1},
        {0,0,0,0,0,0,0,0,0,0,0,0,3,3,3,3,3,3,0,0,1},
        {0,0,4,4,4,0,0,0,0,0,0,0,3,3,3,3,3,3,0,0,1},
        {0,0,4,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {0,0,4,4,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
    }
};

static uint8_t room_x;
static uint8_t room_y;
static uint8_t player_x;
static uint8_t player_y;
static Direction player_dir;
static GameState game_state;
static uint8_t player_hp;
static uint8_t enemy_id;
static uint8_t enemy_hp;
static uint8_t enemy_max_hp;
static uint8_t enemy_attack;
static uint8_t enemy_capture_rate;
static uint8_t battle_flash;
static uint8_t battle_done_ticks;
static uint8_t battle_done_kind;
static uint16_t rng_state;
static uint8_t btn_state;
static uint8_t btn_press_events;
static uint8_t btn_debounce_cnt[BTN_COUNT];
static uint8_t walk_tick;

static void lcd_ce_low(void) { LCD_PORT &= ~(1 << LCD_CE); }
static void lcd_ce_high(void) { LCD_PORT |= (1 << LCD_CE); }
static void lcd_dc_cmd(void) { LCD_PORT &= ~(1 << LCD_DC); }
static void lcd_dc_data(void) { LCD_PORT |= (1 << LCD_DC); }
static void lcd_rst_low(void) { LCD_PORT &= ~(1 << LCD_RST); }
static void lcd_rst_high(void) { LCD_PORT |= (1 << LCD_RST); }
static void lcd_clk_low(void) { LCD_PORT &= ~(1 << LCD_CLK); }
static void lcd_clk_high(void) { LCD_PORT |= (1 << LCD_CLK); }
static void lcd_din_low(void) { LCD_PORT &= ~(1 << LCD_DIN); }
static void lcd_din_high(void) { LCD_PORT |= (1 << LCD_DIN); }

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
    memset(lcd_buffer, 0, sizeof(lcd_buffer));
}

static void lcd_clear_buffer(void) {
    memset(lcd_buffer, 0, sizeof(lcd_buffer));
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
    uint8_t mask = (uint8_t)(1 << (y & 7));

    if (color) lcd_buffer[index] |= mask;
    else lcd_buffer[index] &= (uint8_t)~mask;
}

static void lcd_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color) {
    for (uint8_t yy = 0; yy < h; yy++) {
        for (uint8_t xx = 0; xx < w; xx++) {
            lcd_set_pixel(x + xx, y + yy, color);
        }
    }
}

static void lcd_hline(uint8_t x0, uint8_t x1, uint8_t y, uint8_t color) {
    if (y >= LCD_HEIGHT) return;
    if (x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
    for (uint8_t x = x0; x <= x1; x++) {
        lcd_set_pixel(x, y, color);
    }
}

static void buttons_init(void) {
    BTN_DDR &= (uint8_t)~BTN_MASK;
    BTN_PORT |= BTN_MASK;
}

static uint8_t buttons_raw_mask(void) {
    return (uint8_t)(~BTN_PIN) & (uint8_t)BTN_MASK;
}

static void buttons_reset(void) {
    btn_state = buttons_raw_mask();
    btn_press_events = 0;
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        btn_debounce_cnt[i] = 0;
    }
}

static void buttons_poll(void) {
    static const uint8_t bits[BTN_COUNT] = {
        (1 << BTN_UP), (1 << BTN_DOWN), (1 << BTN_LEFT),
        (1 << BTN_RIGHT), (1 << BTN_A), (1 << BTN_B)
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
            }
        }
    }
}

static uint8_t button_down(uint8_t pin) {
    return (btn_state & (1 << pin)) ? 1 : 0;
}

static uint8_t button_pressed_event(uint8_t pin) {
    uint8_t mask = (uint8_t)(1 << pin);
    uint8_t value = (btn_press_events & mask) ? 1 : 0;
    btn_press_events &= (uint8_t)~mask;
    return value;
}

static uint8_t rng_next(void) {
    uint16_t x = rng_state;
    x ^= (uint16_t)(x << 7);
    x ^= (uint16_t)(x >> 9);
    x ^= (uint16_t)(x << 8);
    rng_state = x ? x : 0xACE1;
    return (uint8_t)x;
}

static uint8_t current_room(void) {
    return room_y * WORLD_W + room_x;
}

static uint8_t tile_at(uint8_t room, uint8_t x, uint8_t y) {
    if (x >= SCREEN_W || y >= SCREEN_H || room >= ROOM_COUNT) return TILE_TREE;
    return pgm_read_byte(&rooms[room][y][x]);
}

static uint8_t tile_blocked(uint8_t tile) {
    return tile == TILE_TREE || tile == TILE_WATER || tile == TILE_HOUSE || tile == TILE_ROCK || tile == TILE_MON;
}

static void start_battle(uint8_t id) {
    const MonsterStats *stats = &monsters[id % MON_COUNT];

    enemy_id = id % MON_COUNT;
    enemy_max_hp = pgm_read_byte(&stats->max_hp);
    enemy_attack = pgm_read_byte(&stats->attack);
    enemy_capture_rate = pgm_read_byte(&stats->capture_rate);
    enemy_hp = enemy_max_hp;
    if (player_hp == 0) player_hp = BATTLE_PLAYER_MAX_HP;

    battle_flash = 8;
    battle_done_ticks = 0;
    battle_done_kind = 0;
    game_state = STATE_BATTLE;
    buttons_reset();
}

static void finish_battle(uint8_t kind) {
    battle_done_kind = kind;
    battle_done_ticks = 18;
    game_state = STATE_BATTLE_DONE;
    buttons_reset();
}

static void enemy_turn(void) {
    if (enemy_attack >= player_hp) {
        player_hp = 0;
        finish_battle(3);
    } else {
        player_hp -= enemy_attack;
    }
}

static void attack_enemy(void) {
    uint8_t damage = 2 + (rng_next() & 1);

    if (damage >= enemy_hp) {
        enemy_hp = 0;
        finish_battle(1);
    } else {
        enemy_hp -= damage;
        enemy_turn();
    }
}

static void capture_enemy(void) {
    uint8_t missing_hp = enemy_max_hp - enemy_hp;
    uint8_t chance = enemy_capture_rate + (missing_hp * 14);

    if (chance > 230) chance = 230;

    if (rng_next() < chance) {
        finish_battle(2);
    } else {
        enemy_turn();
    }
}

static void maybe_start_random_encounter(uint8_t stepped_tile) {
    if (stepped_tile != TILE_GRASS || game_state != STATE_MAP) return;
    if ((rng_next() & 3) != 0) return;

    uint8_t zone = current_room();
    uint8_t id = (uint8_t)((zone + (rng_next() % MON_COUNT)) % MON_COUNT);
    start_battle(id);
}

static void game_init(void) {
    TCCR0 = (1 << CS01) | (1 << CS00);
    room_x = 0;
    room_y = 0;
    player_x = 8;
    player_y = 10;
    player_dir = DIR_UP;
    game_state = STATE_MAP;
    player_hp = BATTLE_PLAYER_MAX_HP;
    enemy_id = 0;
    enemy_hp = 0;
    enemy_max_hp = 0;
    enemy_attack = 0;
    enemy_capture_rate = 0;
    battle_flash = 0;
    battle_done_ticks = 0;
    battle_done_kind = 0;
    rng_state = 0xACE1;
    walk_tick = 0;
}

static uint8_t try_move(int8_t dx, int8_t dy, Direction dir) {
    int8_t nx = (int8_t)player_x + dx;
    int8_t ny = (int8_t)player_y + dy;
    uint8_t next_room_x = room_x;
    uint8_t next_room_y = room_y;

    player_dir = dir;

    if (nx < 0) {
        if (room_x == 0) return 0;
        next_room_x--;
        nx = SCREEN_W - 1;
    } else if (nx >= SCREEN_W) {
        if (room_x + 1 >= WORLD_W) return 0;
        next_room_x++;
        nx = 0;
    }

    if (ny < 0) {
        if (room_y == 0) return 0;
        next_room_y--;
        ny = SCREEN_H - 1;
    } else if (ny >= SCREEN_H) {
        if (room_y + 1 >= WORLD_H) return 0;
        next_room_y++;
        ny = 0;
    }

    uint8_t room = next_room_y * WORLD_W + next_room_x;
    uint8_t tile = tile_at(room, (uint8_t)nx, (uint8_t)ny);
    if (tile == TILE_MON) {
        start_battle((uint8_t)((room + nx + ny) % MON_COUNT));
        return 0;
    }
    if (tile_blocked(tile)) return 0;

    room_x = next_room_x;
    room_y = next_room_y;
    player_x = (uint8_t)nx;
    player_y = (uint8_t)ny;
    return tile;
}

static void update_game(void) {
    rng_state ^= (uint16_t)(btn_state + TCNT0 + player_x + (player_y << 4));

    if (game_state == STATE_BATTLE) {
        if (battle_flash) battle_flash--;
        if (button_pressed_event(BTN_A)) attack_enemy();
        else if (button_pressed_event(BTN_B)) capture_enemy();
        return;
    }

    if (game_state == STATE_BATTLE_DONE) {
        if (battle_done_ticks) {
            battle_done_ticks--;
        } else {
            if (player_hp == 0) {
                player_hp = BATTLE_PLAYER_MAX_HP;
                room_x = 0;
                room_y = 0;
                player_x = 8;
                player_y = 10;
            }
            game_state = STATE_MAP;
            buttons_reset();
        }
        return;
    }

    if (walk_tick) {
        walk_tick--;
        return;
    }

    uint8_t stepped_tile = 0;
    if (button_down(BTN_UP)) {
        stepped_tile = try_move(0, -1, DIR_UP);
        walk_tick = 4;
    } else if (button_down(BTN_DOWN)) {
        stepped_tile = try_move(0, 1, DIR_DOWN);
        walk_tick = 4;
    } else if (button_down(BTN_LEFT)) {
        stepped_tile = try_move(-1, 0, DIR_LEFT);
        walk_tick = 4;
    } else if (button_down(BTN_RIGHT)) {
        stepped_tile = try_move(1, 0, DIR_RIGHT);
        walk_tick = 4;
    }

    maybe_start_random_encounter(stepped_tile);
}

static void draw_floor(uint8_t px, uint8_t py) {
    lcd_set_pixel(px + 1, py + 3, 1);
}

static void draw_tree(uint8_t px, uint8_t py) {
    lcd_rect(px, py, 4, 4, 1);
    lcd_set_pixel(px + 1, py + 1, 0);
    lcd_set_pixel(px + 2, py + 2, 0);
}

static void draw_grass(uint8_t px, uint8_t py) {
    lcd_set_pixel(px, py + 3, 1);
    lcd_set_pixel(px + 1, py + 1, 1);
    lcd_set_pixel(px + 2, py + 2, 1);
    lcd_set_pixel(px + 3, py, 1);
}

static void draw_water(uint8_t px, uint8_t py) {
    lcd_rect(px, py, 4, 4, 0);
    lcd_set_pixel(px, py + 1, 1);
    lcd_set_pixel(px + 1, py + 1, 1);
    lcd_set_pixel(px + 2, py + 2, 1);
    lcd_set_pixel(px + 3, py + 2, 1);
}

static void draw_house(uint8_t px, uint8_t py) {
    lcd_rect(px, py, 4, 4, 1);
    lcd_set_pixel(px + 1, py + 3, 0);
    lcd_set_pixel(px + 2, py + 3, 0);
    lcd_set_pixel(px + 1, py + 1, 0);
}

static void draw_rock(uint8_t px, uint8_t py) {
    lcd_set_pixel(px + 1, py, 1);
    lcd_set_pixel(px + 2, py, 1);
    lcd_set_pixel(px, py + 1, 1);
    lcd_set_pixel(px + 3, py + 1, 1);
    lcd_set_pixel(px + 1, py + 2, 1);
    lcd_set_pixel(px + 2, py + 2, 1);
    lcd_set_pixel(px + 1, py + 3, 1);
}

static void draw_mon(uint8_t px, uint8_t py) {
    lcd_set_pixel(px + 1, py, 1);
    lcd_set_pixel(px + 2, py, 1);
    lcd_set_pixel(px, py + 1, 1);
    lcd_set_pixel(px + 3, py + 1, 1);
    lcd_set_pixel(px, py + 2, 1);
    lcd_set_pixel(px + 3, py + 2, 1);
    lcd_set_pixel(px + 1, py + 3, 1);
    lcd_set_pixel(px + 2, py + 3, 1);
    lcd_set_pixel(px + 1, py + 1, 1);
    lcd_set_pixel(px + 2, py + 2, 1);
}

static void draw_player(uint8_t px, uint8_t py) {
    lcd_rect(px, py, 4, 4, 0);

    lcd_set_pixel(px + 1, py, 1);
    lcd_set_pixel(px + 2, py, 1);
    lcd_set_pixel(px + 1, py + 1, 1);
    lcd_set_pixel(px + 2, py + 1, 1);
    lcd_set_pixel(px, py + 2, 1);
    lcd_set_pixel(px + 3, py + 2, 1);
    lcd_set_pixel(px + 1, py + 3, 1);
    lcd_set_pixel(px + 2, py + 3, 1);

    if (player_dir == DIR_UP) {
        lcd_set_pixel(px + 1, py + 1, 0);
        lcd_set_pixel(px + 2, py + 1, 0);
    } else if (player_dir == DIR_DOWN) {
        lcd_set_pixel(px + 1, py, 0);
        lcd_set_pixel(px + 2, py, 0);
    } else if (player_dir == DIR_LEFT) {
        lcd_set_pixel(px, py + 1, 1);
        lcd_set_pixel(px + 3, py + 1, 0);
    } else {
        lcd_set_pixel(px + 3, py + 1, 1);
        lcd_set_pixel(px, py + 1, 0);
    }
}

static void draw_big_mon(uint8_t x, uint8_t y, uint8_t id) {
    if (id == 0) {
        lcd_rect(x + 4, y + 3, 8, 8, 1);
        lcd_set_pixel(x + 6, y + 5, 0);
        lcd_set_pixel(x + 10, y + 5, 0);
        lcd_hline(x + 5, x + 11, y + 9, 0);
        lcd_set_pixel(x + 2, y + 7, 1);
        lcd_set_pixel(x + 13, y + 7, 1);
    } else if (id == 1) {
        lcd_rect(x + 5, y + 4, 6, 8, 1);
        lcd_set_pixel(x + 6, y + 6, 0);
        lcd_set_pixel(x + 9, y + 6, 0);
        lcd_set_pixel(x + 1, y + 3, 1);
        lcd_set_pixel(x + 2, y + 4, 1);
        lcd_set_pixel(x + 3, y + 5, 1);
        lcd_set_pixel(x + 14, y + 3, 1);
        lcd_set_pixel(x + 13, y + 4, 1);
        lcd_set_pixel(x + 12, y + 5, 1);
    } else {
        lcd_rect(x + 4, y + 5, 8, 7, 1);
        lcd_set_pixel(x + 5, y + 4, 1);
        lcd_set_pixel(x + 10, y + 4, 1);
        lcd_set_pixel(x + 6, y + 7, 0);
        lcd_set_pixel(x + 9, y + 7, 0);
        lcd_hline(x + 3, x + 12, y + 12, 1);
    }
}

static void draw_big_player(uint8_t x, uint8_t y) {
    lcd_rect(x + 5, y, 6, 5, 1);
    lcd_rect(x + 4, y + 5, 8, 7, 1);
    lcd_set_pixel(x + 6, y + 2, 0);
    lcd_set_pixel(x + 9, y + 2, 0);
    lcd_set_pixel(x + 3, y + 7, 1);
    lcd_set_pixel(x + 12, y + 7, 1);
    lcd_set_pixel(x + 5, y + 12, 1);
    lcd_set_pixel(x + 10, y + 12, 1);
}

static void draw_hp_bar(uint8_t x, uint8_t y, uint8_t hp, uint8_t max_hp) {
    uint8_t fill = 0;
    if (max_hp) fill = (uint8_t)(((uint16_t)hp * 22) / max_hp);

    lcd_hline(x, x + 23, y, 1);
    lcd_hline(x, x + 23, y + 4, 1);
    lcd_set_pixel(x, y + 1, 1);
    lcd_set_pixel(x, y + 2, 1);
    lcd_set_pixel(x, y + 3, 1);
    lcd_set_pixel(x + 23, y + 1, 1);
    lcd_set_pixel(x + 23, y + 2, 1);
    lcd_set_pixel(x + 23, y + 3, 1);

    for (uint8_t i = 0; i < fill; i++) {
        lcd_set_pixel(x + 1 + i, y + 2, 1);
    }
}

static void draw_battle_actions(void) {
    lcd_rect(28, 39, 7, 7, 0);
    lcd_hline(29, 34, 42, 1);
    lcd_set_pixel(33, 40, 1);
    lcd_set_pixel(32, 41, 1);
    lcd_set_pixel(31, 43, 1);

    lcd_rect(49, 39, 7, 7, 0);
    lcd_set_pixel(52, 39, 1);
    lcd_set_pixel(51, 40, 1);
    lcd_set_pixel(53, 40, 1);
    lcd_hline(50, 54, 42, 1);
    lcd_hline(50, 54, 43, 1);
}

static void draw_battle(void) {
    lcd_clear_buffer();

    draw_big_mon(54, 5, enemy_id);
    draw_hp_bar(6, 6, enemy_hp, enemy_max_hp);
    draw_big_player(8, 27);
    draw_hp_bar(54, 31, player_hp, BATTLE_PLAYER_MAX_HP);
    draw_battle_actions();

    if (battle_flash & 1) {
        lcd_rect(0, 0, LCD_WIDTH, 2, 1);
        lcd_rect(0, LCD_HEIGHT - 2, LCD_WIDTH, 2, 1);
    }

    lcd_update();
}

static void draw_battle_done(void) {
    lcd_clear_buffer();

    if (battle_done_kind == 1) {
        lcd_rect(34, 16, 16, 16, 1);
        lcd_rect(38, 20, 8, 8, 0);
    } else if (battle_done_kind == 2) {
        lcd_rect(35, 16, 14, 14, 0);
        lcd_hline(36, 48, 23, 1);
        lcd_set_pixel(35, 20, 1);
        lcd_set_pixel(36, 18, 1);
        lcd_set_pixel(38, 17, 1);
        lcd_set_pixel(45, 17, 1);
        lcd_set_pixel(47, 18, 1);
        lcd_set_pixel(49, 20, 1);
        lcd_set_pixel(35, 26, 1);
        lcd_set_pixel(36, 28, 1);
        lcd_set_pixel(38, 29, 1);
        lcd_set_pixel(45, 29, 1);
        lcd_set_pixel(47, 28, 1);
        lcd_set_pixel(49, 26, 1);
    } else {
        lcd_rect(31, 14, 22, 20, 1);
        lcd_hline(34, 50, 20, 0);
        lcd_hline(34, 50, 28, 0);
    }

    lcd_update();
}

static void draw_tile(uint8_t tile, uint8_t px, uint8_t py) {
    if (tile == TILE_FLOOR) draw_floor(px, py);
    else if (tile == TILE_TREE) draw_tree(px, py);
    else if (tile == TILE_GRASS) draw_grass(px, py);
    else if (tile == TILE_WATER) draw_water(px, py);
    else if (tile == TILE_HOUSE) draw_house(px, py);
    else if (tile == TILE_ROCK) draw_rock(px, py);
    else if (tile == TILE_MON) draw_mon(px, py);
}

static void draw_game(void) {
    if (game_state == STATE_BATTLE) {
        draw_battle();
        return;
    }

    if (game_state == STATE_BATTLE_DONE) {
        draw_battle_done();
        return;
    }

    uint8_t room = current_room();
    lcd_clear_buffer();

    for (uint8_t y = 0; y < SCREEN_H; y++) {
        for (uint8_t x = 0; x < SCREEN_W; x++) {
            uint8_t tile = tile_at(room, x, y);
            draw_tile(tile, x * TILE_SIZE, y * TILE_SIZE);
        }
    }

    draw_player(player_x * TILE_SIZE, player_y * TILE_SIZE);
    lcd_update();
}

int main(void) {
    lcd_init();
    buttons_init();
    buttons_reset();
    game_init();

    while (1) {
        buttons_poll();
        update_game();
        draw_game();
        _delay_ms(INPUT_POLL_MS);
    }
}
