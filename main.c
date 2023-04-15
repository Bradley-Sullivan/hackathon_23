#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <time.h>

#define GAME_HEIGHT     8
#define GAME_WIDTH      24

#define SPRITE_HEIGHT   4
#define SPRITE_WIDTH    4

#define MAX_NUM_ENEMIES 4
#define NUM_ETYPES      6

#define E_PROJ_INTV     750.f    // (ms)
#define P_PROJ_INTV     500.f    // (ms)

#define UD              0
#define LR              1

#define PATH0LR         "LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL"
#define PATH0UD         "UUDDUUDDUUDDUUDDUUDDUUDDUUDDUUD"

#define PATH1LR         "LLLLLLRRRRRRLLLLLLRRRRRRLLLLLL"
#define PATH1UD         "DDDUUUUUUDDDDDDUUUUUUDDDDDDUUU"

#define U32LROT(X)      (0x80000000 & X ? ((X << 1) | 1) : (X << 1))
#define U32RROT(X)      (0x00000001 & X ? ((X >> 1) | 0x80000000) : (X >> 1))

typedef struct game_timer_t {
    float interval;
    float i_time;
    float c_time;
} game_timer_t;

typedef struct player_t {
    uint16_t    sprite;
    uint8_t     emitter;        // shift value
    uint8_t     c_pos;
    uint8_t     r_pos;
    uint8_t     super_cd;
    uint8_t     atk_cd;
} player_t;

typedef struct enemy_t {
    uint16_t    sprite;
    uint16_t    emitter;
    uint8_t     c_pos;
    uint8_t     r_pos;

    uint32_t    mvmt_mask[2];      // get's rotated
    uint32_t    ud_mvmt;           // 1's -> UP : 0's -> DOWN
    uint32_t    lr_mvmt;           // 1's -> LEFT : 0's -> RIGHT

    game_timer_t     atk_timer;
    game_timer_t     super_timer;
    game_timer_t     mvmt_timer[2];
} enemy_t;

struct game_data {
    float       i_time;         // (ms)
    float       g_time;         // (ms)

    /* time since last proj. position update */
    game_timer_t     p_proj_timer;
    game_timer_t     e_proj_timer;

    uint8_t     num_enemies;

    uint32_t    e_proj[GAME_HEIGHT];
    uint32_t    p_proj[GAME_HEIGHT];

    uint8_t     g_state;
    uint8_t     g_flags;

    uint32_t    frame[GAME_HEIGHT];
} game_data;

// ENEMY SPRITES
static const uint16_t e_sprites[NUM_ETYPES] = { 0x96F9, 0x966F, 0x136D, 0x6317, 0x3EE3, 0x06F0 };

static player_t player;
static enemy_t enemies[MAX_NUM_ENEMIES];

void init_game();

void update_game();

void update_input();
void update_player();
void update_enemies();
void update_projectiles();

void update_frame();

void process_movement();
uint32_t set_movement(const char *mv_str);

uint32_t get_sprite_slice(uint8_t depth, uint16_t sprite);
uint16_t clear_sprite_slice(uint8_t depth, uint16_t slice_mask, uint16_t sprite);

void p_bits(uint32_t x, int width);
void print_frame();

int main(void) {
    init_game();

    game_data.i_time = (clock() * 1000 / CLOCKS_PER_SEC);
    while (1) { /*  game loop   */
        usleep(50000);
        game_data.g_time = clock() - game_data.i_time;


        update_game();
        print_frame();
    }

    return 0;
}

void init_game() {
    player.sprite = 0x0660;
    player.r_pos = 0;
    player.c_pos = 0;

    enemies[0].sprite = e_sprites[2];

    enemies[0].r_pos = 0;
    enemies[0].c_pos = GAME_WIDTH;

    enemies[0].r_pos = GAME_HEIGHT - SPRITE_HEIGHT;
    enemies[0].c_pos = GAME_WIDTH;
    enemies[0].emitter = 0x8008;
    enemies[0].atk_timer.interval = 50000;

    enemies[0].mvmt_mask[LR] = 1;
    enemies[0].mvmt_mask[UD] = 1;
    enemies[0].ud_mvmt = set_movement(PATH1UD);
    enemies[0].lr_mvmt = set_movement(PATH1LR);

    enemies[0].mvmt_timer[UD].interval = 1000;
    enemies[0].mvmt_timer[LR].interval = 1000;

    enemies[0].mvmt_timer[UD].i_time = clock();
    enemies[0].mvmt_timer[LR].i_time = clock();

    game_data.p_proj[2] = (1 << 20);
    game_data.p_proj_timer.i_time = clock();

    game_data.e_proj[2] = (1 << 7);
    game_data.e_proj_timer.i_time = clock();

    game_data.num_enemies = 2;
    game_data.p_proj_timer.interval = P_PROJ_INTV;
    game_data.e_proj_timer.interval = E_PROJ_INTV;
}

void update_game() {
    // update_input();

    update_player();
    update_enemies();
    update_projectiles();

    update_frame();
}

void update_player() {
    uint32_t p_slice = 0;

    for (int i = 0; i < GAME_HEIGHT; i += 1) {
        uint8_t p_depth = i - player.r_pos;
        if (p_depth >= 0 && p_depth < SPRITE_HEIGHT) { /* if "scanline" hits player */
            p_slice = get_sprite_slice(p_depth, player.sprite);
            p_slice <<= GAME_WIDTH - player.c_pos;
            p_slice &= game_data.e_proj[i];

            if (p_slice) {
                // consume projectile
                game_data.e_proj[i] &= ~p_slice;

                p_slice >>= GAME_WIDTH - player.c_pos;

                // deform player sprite
                player.sprite = clear_sprite_slice(p_depth, p_slice, player.sprite);
            }
        }
    }

    // for (int i = 0; i < game_data.num_enemies; i += 1) {
    //     // want bit-level resolution on enemy-player collisions!!
    //     if (enemies[i].c_pos <= player.c_pos + (SPRITE_WIDTH - 1)) {
    //         if (enemies[i].r_pos <= player.r_pos + (SPRITE_HEIGHT - 1) &&
    //             enemies[i].r_pos >= player.r_pos) {
    //             // game over
    //         }
    //     }
    // }
}

void update_enemies() {
    uint16_t e_occ = 0;
    uint32_t e_slice = 0, e_proj_mask = 0;

    for (int k = 0; k < game_data.num_enemies; k += 1) {
        // process position updates
            // do testing before this
        process_movement();

        enemies[k].atk_timer.c_time = clock() - enemies[k].atk_timer.i_time;
        for (int i = 0; i < GAME_HEIGHT; i += 1) {
            uint8_t e_depth = i - enemies[k].r_pos;
            if (e_depth >= 0 && e_depth < SPRITE_HEIGHT) {  /* if "scanline" hits enemy */
                if (enemies[k].atk_timer.c_time >= enemies[k].atk_timer.interval) { /* fire projectile */
                    uint32_t em_slice = get_sprite_slice(e_depth, enemies[k].emitter);
                    em_slice <<= GAME_WIDTH - enemies[k].c_pos;

                    // fire projectile
                    game_data.e_proj[i] |= em_slice;

                    // reset atk timer
                    enemies[k].atk_timer.i_time = clock();
                }

                e_slice = get_sprite_slice(e_depth, enemies[k].sprite);
                e_slice <<= GAME_WIDTH - enemies[k].c_pos;
                e_slice &= game_data.p_proj[i];

                if (e_slice) {  /* p_proj and enemy collision */
                    // consume projectile
                    game_data.p_proj[i] &= ~e_slice;

                    e_slice >>= GAME_WIDTH - enemies[k].c_pos;

                    // deform enemy sprite
                    enemies[k].sprite = clear_sprite_slice(e_depth, e_slice, enemies[k].sprite);
                }
            }
        }        
    }
}

void update_projectiles() {
    game_data.p_proj_timer.c_time = clock() - game_data.p_proj_timer.i_time;
    game_data.e_proj_timer.c_time = clock() - game_data.e_proj_timer.i_time;

    for (int i = 0; i < GAME_HEIGHT; i += 1) {
        if (game_data.p_proj[i]) {
            if (game_data.p_proj_timer.c_time >= game_data.p_proj_timer.interval) { /* advance projectiles */
                game_data.p_proj[i] >>= 1;

                game_data.p_proj_timer.i_time = clock();
            }
        }
        
        if (game_data.e_proj[i]) {
            if (game_data.e_proj_timer.c_time >= game_data.e_proj_timer.interval) { /* advance projectiles */
                game_data.e_proj[i] <<= 1;

                game_data.e_proj_timer.i_time = clock();
            }
        }
    }
}

void update_frame() {
    uint8_t p_depth, e_depth;
    for (int i = 0; i < GAME_HEIGHT; i += 1) {
        game_data.frame[i] = 0;

        // store projectiles
        game_data.frame[i] = (game_data.p_proj[i] | game_data.e_proj[i]);

        // store player sprite
        p_depth = i - player.r_pos;
        if (p_depth >= 0 && p_depth < SPRITE_HEIGHT) {
            uint32_t p_slice = get_sprite_slice(p_depth, player.sprite);
            // printf("\tp_slice       : "); p_bits(p_slice, 32);
            p_slice <<= GAME_WIDTH - player.c_pos;
            game_data.frame[i] |= p_slice;
        }

        // store enemy sprites
        for (int k = 0; k < game_data.num_enemies; k += 1) {
            e_depth = i - enemies[k].r_pos;
            if (e_depth >= 0 && e_depth < SPRITE_HEIGHT) {
                uint32_t e_slice = get_sprite_slice(e_depth, enemies[k].sprite);
                // printf("\te_slice[%d]       : ", k); p_bits(e_slice, 32);
                e_slice <<= GAME_WIDTH - enemies[k].c_pos;
                game_data.frame[i] |= e_slice;
            }
        }
    }
}

void process_movement() {
    for (int i = 0; i < game_data.num_enemies; i += 1) {
        enemies[i].mvmt_timer[UD].c_time = clock() - enemies[i].mvmt_timer[UD].i_time;
        enemies[i].mvmt_timer[LR].c_time = clock() - enemies[i].mvmt_timer[LR].i_time;

        if (enemies[i].mvmt_timer[UD].c_time >= enemies[i].mvmt_timer[UD].interval) {
            if (enemies[i].mvmt_mask[UD]) { /* if mvmt enabled */
                if (enemies[i].mvmt_mask[UD] & enemies[i].ud_mvmt) { /* move up */
                    enemies[i].r_pos = (enemies[i].r_pos > 0 ? enemies[i].r_pos - 1 : 0);
                } else { /* move down */
                    enemies[i].r_pos = (enemies[i].r_pos + 2 < GAME_HEIGHT - 1 ? enemies[i].r_pos + 1 : GAME_HEIGHT - 2);
                }

                // rotate mask
                enemies[i].mvmt_mask[UD] = U32LROT(enemies[i].mvmt_mask[UD]);
            }

            enemies[i].mvmt_timer[UD].i_time = clock();
        }

        if (enemies[i].mvmt_timer[LR].c_time >= enemies[i].mvmt_timer[LR].interval) {
            if (enemies[i].mvmt_mask[LR]) { /* if mvmt enabled */
                if (enemies[i].mvmt_mask[LR] & enemies[i].lr_mvmt) { /* move left */
                    enemies[i].c_pos = (enemies[i].c_pos > 0 ? enemies[i].c_pos - 1 : 0);
                } else { /* move right */
                    enemies[i].c_pos = (enemies[i].c_pos < GAME_WIDTH ? enemies[i].c_pos + 1 : GAME_WIDTH);
                }

                // rotate mask
                enemies[i].mvmt_mask[LR] = U32LROT(enemies[i].mvmt_mask[LR]);
            }           

            enemies[i].mvmt_timer[LR].i_time = clock();
        }
    }

    printf("\tmvmt_mask     : "); p_bits(enemies[0].mvmt_mask[UD], 32);
}

uint32_t set_movement(const char *mv_str) {
    uint32_t m = 0;
    uint8_t len = strlen(mv_str);
    if (mv_str && len < 32) {
        for (int i = 0; i < len; i += 1) {
            switch (mv_str[i]) {
                case 'l':
                case 'L':
                case 'u':
                case 'U':
                    m |= 1 << i;
                    break;
                    break;
                default:
                    break;
            }
        }
    }

    return m;
}

uint32_t get_sprite_slice(uint8_t depth, uint16_t sprite) {
    uint32_t slice = 0;
    uint8_t slice_shift = (SPRITE_HEIGHT - depth - 1) * SPRITE_WIDTH;

    if (depth >= 0 && depth < SPRITE_HEIGHT) {
        slice = sprite & (0x000F << (slice_shift));
        slice >>= slice_shift;
    } else {
        return 0;
    }
    
    return slice;
}

uint16_t clear_sprite_slice(uint8_t depth, uint16_t slice_mask, uint16_t sprite) {
    if (depth >= 0 && depth < SPRITE_HEIGHT) {
        slice_mask <<= ((SPRITE_HEIGHT - depth - 1) * SPRITE_WIDTH);
        sprite &= ~slice_mask;
    }

    return sprite;
}

void p_bits(uint32_t x, int width) {
    for (int i = width - 1; i >= 0; i -= 1) {
        printf(" %c ", ((1<<i) & x) ? '1' : '0');
    }
    printf("\n");
}

void print_frame() {
    printf("========= BEGIN FRAME =========\n");
    for (int i = 0; i < GAME_HEIGHT; i += 1) {
        p_bits(game_data.frame[i], 32);
    }
    printf("=========  END  FRAME =========\n");
}
