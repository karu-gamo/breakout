#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../third_party/stb_image.h"
#include "ball_png.h"

#define WIDTH 960
#define HEIGHT 720
#define BASE_COLS 10
#define BASE_ROWS 5
#define COLS (BASE_COLS * 2)
#define INITIAL_ROWS (BASE_ROWS * 2)
#define ROWS 40
#define BRICK_TOP 100.0f
#define BRICK_PITCH 16.0f
#define BRICK_H 12.5f
#define DESCENT_LINE_Y 260.0f
#define BASE_DESCENT_SPEED 8.0f
#define BONUS_BRICK_INTERVAL 40
#define SILVER_BRICK_HITS 4
#define GOLD_BRICK_HITS 8
#define BRICK_FADE_SECONDS 0.5f
#define RADIUS 16.0f
#define SOFT_COMPRESS_SECONDS 0.0325f
#define SOFT_RECOVER_SECONDS 0.5f
#define PADDLE_Y 650.0f
#define PADDLE_W 120.0f
#define STEP (1.0 / 240.0)
/* Heights from the reference image, in the 960 x 720 logical canvas. */
#define PREVIEW_START_Y 290.0f
#define PREVIEW_FULL_Y 425.0f
#define PREVIEW_SEGMENTS 10
#define MAX_PARTICLES 768
#define MAX_BALLS 64
#define BONUS_GRAVITY 930.0f
#define BONUS_MAX_FALL_SPEED 3600.0f
#define BONUS_RESTITUTION 0.72f
#define CHARGE_SECONDS 1.0f
#define TRAIL_SECONDS 0.2f
#define MAX_TRAILS 12
#define PADDLE_HISTORY 49
#define TRAIL_FAST_SPEED 1200.0f
#define MISS_DURATION 0.85f
#define MAX_PADDLE_SCALE 2.0f
#define PREVIEW_SAMPLES (PREVIEW_SEGMENTS + 1)
#define PREVIEW_END_Y (PADDLE_Y - RADIUS)

typedef enum { TITLE, PLAYING, GAME_OVER } Screen;
typedef enum { NO_METAL, SILVER, GOLD } StrongType;
typedef struct {
    float x, y, vx, vy, life, lifetime, size;
    Uint8 red, green, blue;
} Particle;

typedef struct {
    float x, y, vx, vy, angle, spin, width;
} PaddlePiece;

typedef struct {
    bool active, attached, gravity;
    float x, y, vx, vy;
    float shot_power, attach_offset, base_launch_angle;
    int pierces;
    bool preview_active;
    float preview_x, preview_vx, preview_vy;
    float rotation, spin_speed, spin_left;
    bool deforming, bounce_pending;
    float squash_time, normal_x, normal_y, bounce_vx, bounce_vy;
} Ball;

typedef struct {
    Screen screen;
    SDL_Texture *ball_texture;
    float paddle, paddle_width, paddle_speed;
    bool left_down;
    float charge, shake_time, animation_time;
    Ball balls[MAX_BALLS];
    Uint32 random_state;
    Particle particles[MAX_PARTICLES];
    float paddle_history[PADDLE_HISTORY], width_history[PADDLE_HISTORY];
    float miss_time;
    PaddlePiece paddle_pieces[3];
    bool bricks[ROWS][COLS], bonus_bricks[ROWS][COLS];
    Uint8 strong_hits[ROWS][COLS], strong_type[ROWS][COLS];
    float brick_hit_time[ROWS][COLS];
    float scroll_offset, row_age[ROWS];
    SDL_Color row_color[ROWS];
    int generated_rows, bonus_credit, strong_credit, silvers_since_gold;
    bool reset_field_after_miss;
    int level, lives, remaining;
    Uint64 score, destroyed;
} Game;

static SDL_Texture *load_ball_texture(SDL_Renderer *renderer) {
    int width, height, channels;
    unsigned char *pixels = stbi_load_from_memory(ball_png, sizeof(ball_png),
                                                  &width, &height, &channels, 4);
    if (!pixels) {
        SDL_SetError("Cannot decode embedded ball.png: %s", stbi_failure_reason());
        return NULL;
    }
    SDL_Surface *surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32,
                                                pixels, width * 4);
    SDL_Texture *texture = surface ? SDL_CreateTextureFromSurface(renderer, surface) : NULL;
    SDL_DestroySurface(surface);
    stbi_image_free(pixels);
    if (texture) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }
    return texture;
}

static float clamp(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static float speed(const Game *g) {
    (void)g;
    return 340.0f;
}

static void reset_history(Game *g) {
    g->paddle_width = PADDLE_W;
    g->paddle_speed = 0;
    for (int i = 0; i < PADDLE_HISTORY; ++i) {
        g->paddle_history[i] = g->paddle;
        g->width_history[i] = g->paddle_width;
    }
}

static void record_paddle(Game *g) {
    float movement = g->paddle - g->paddle_history[0];
    float smoothing = 1 - expf(-(float)STEP / 0.08f);
    g->paddle_speed += (fabsf(movement) / (float)STEP - g->paddle_speed) * smoothing;
    g->paddle_width = PADDLE_W * (1 + (MAX_PADDLE_SCALE - 1) *
        clamp(g->paddle_speed / TRAIL_FAST_SPEED, 0, 1));
    for (int i = PADDLE_HISTORY - 1; i > 0; --i) {
        g->paddle_history[i] = g->paddle_history[i - 1];
        g->width_history[i] = g->width_history[i - 1];
    }
    g->paddle_history[0] = g->paddle;
    g->width_history[0] = g->paddle_width;
}

static int paddle_trails(const Game *g, float positions[MAX_TRAILS]) {
    if (g->miss_time > 0 || g->screen != PLAYING) return 0;
    float travel = 0;
    for (int i = 1; i < PADDLE_HISTORY; ++i)
        travel += fabsf(g->paddle_history[i] - g->paddle_history[i - 1]);
    float velocity = travel / TRAIL_SECONDS;
    int count = 1 + (int)(clamp(velocity / TRAIL_FAST_SPEED, 0, 1) * (MAX_TRAILS - 1));
    for (int i = 0; i < count; ++i)
        positions[i] = g->paddle_history[(i + 1) * (PADDLE_HISTORY - 1) / count];
    return count;
}

static float bounce_offset_angle(float offset, float width) {
    return clamp(offset / (width / 2), -1, 1) * 0.8f;
}

static float launch_angle(const Ball *b) {
    return b->base_launch_angle;
}

static void place_attached_ball(const Game *g, Ball *b) {
    b->attach_offset = clamp(b->attach_offset, -g->paddle_width / 2 + RADIUS,
                             g->paddle_width / 2 - RADIUS);
    b->x = g->paddle + b->attach_offset;
    b->y = PADDLE_Y - RADIUS - 1;
}

static bool hit_paddle(float center, float width, float old_x, float old_y,
                       float x, float y, float *offset) {
    float contact = PADDLE_Y - RADIUS;
    if (old_y > contact || y < contact || y <= old_y) return false;
    float t = (contact - old_y) / (y - old_y);
    *offset = old_x + (x - old_x) * t - center;
    return fabsf(*offset) <= width / 2 + RADIUS;
}

static void reset_ball(const Game *g, Ball *b) {
    *b = (Ball){.active = true, .attached = true};
    b->pierces = 0;
    b->attach_offset = 0;
    b->base_launch_angle = 0;
    place_attached_ball(g, b);
    b->vx = speed(g) * 0.35f;
    b->vy = -sqrtf(speed(g) * speed(g) - b->vx * b->vx);
}

static SDL_FRect brick_rect(const Game *g, int row, int col) {
    return (SDL_FRect){42.0f + col * 44.0f, BRICK_TOP + row * BRICK_PITCH + g->scroll_offset, 41, BRICK_H};
}

static float random_unit(Game *g);

/* A shuffled pastel sequence, fixed across runs so level bands are recognizable. */
static const SDL_Color level_palette[] = {
    {232, 139, 159, 255}, {157, 165, 224, 255}, {132, 201, 182, 255},
    {236, 177, 135, 255}, {192, 155, 218, 255}, {132, 190, 218, 255},
    {225, 208, 142, 255}, {166, 207, 151, 255}, {214, 153, 182, 255},
    {151, 179, 217, 255}
};

static SDL_Color generated_row_color(int level, int row_number) {
    int count = (int)SDL_arraysize(level_palette);
    int band = (level / 10) % count;
    SDL_Color start = level_palette[band], end = level_palette[(band + 1) % count];
    float t = (row_number % 10) / 9.0f;
    return (SDL_Color){(Uint8)SDL_roundf(start.r + (end.r - start.r) * t),
                       (Uint8)SDL_roundf(start.g + (end.g - start.g) * t),
                       (Uint8)SDL_roundf(start.b + (end.b - start.b) * t), 255};
}

static int split_interval(int level) {
    int increase = level > 1 ? (level - 1) / 4 : 0;
    int maximum_increase = 50 - BONUS_BRICK_INTERVAL;
    return BONUS_BRICK_INTERVAL + (increase < maximum_increase ? increase : maximum_increase);
}

static int strong_interval(int level) {
    if (level < 10) return 0;
    int interval = 60 - (level - 10) / 5;
    return interval > 20 ? interval : 20;
}

static int silvers_per_gold(int level) {
    int silvers = level <= 20 ? 4 : 4 - (level - 20) / 20;
    return silvers > 0 ? silvers : 0;
}

static void assign_strong_brick(Game *g, int row, int col) {
    bool gold = g->silvers_since_gold >= silvers_per_gold(g->level);
    if (gold) g->silvers_since_gold = 0;
    else ++g->silvers_since_gold;
    g->strong_type[row][col] = gold ? GOLD : SILVER;
    g->strong_hits[row][col] = gold ? GOLD_BRICK_HITS : SILVER_BRICK_HITS;
}

static void place_specials(Game *g, int *cells, int count, int bonuses, int strong) {
    for (int i = 0; i < bonuses + strong; ++i) {
        int chosen = i + (int)(random_unit(g) * (count - i));
        int cell = cells[chosen]; cells[chosen] = cells[i]; cells[i] = cell;
        int row = cell / COLS, col = cell % COLS;
        if (i < bonuses) g->bonus_bricks[row][col] = true;
        else assign_strong_brick(g, row, col);
    }
}

static SDL_Color strong_brick_color(const Game *g, int row, int col) {
    float periodic_age = fmodf(g->animation_time, 1.0f);
    float hit_age = g->animation_time - g->brick_hit_time[row][col];
    float age = fminf(periodic_age, hit_age);
    float flash = 1 - clamp(age / 0.5f, 0, 1);
    SDL_Color base = g->strong_type[row][col] == GOLD ?
        (SDL_Color){255, 216, 64, 255} : (SDL_Color){128, 128, 128, 255};
    return (SDL_Color){(Uint8)SDL_roundf(base.r + (255 - base.r) * flash),
                      (Uint8)SDL_roundf(base.g + (255 - base.g) * flash),
                      (Uint8)SDL_roundf(base.b + (255 - base.b) * flash), 255};
}

static void init_field(Game *g) {
    g->remaining = INITIAL_ROWS * COLS;
    g->scroll_offset = 0;
    g->generated_rows = INITIAL_ROWS;
    g->bonus_credit = g->remaining % split_interval(g->level);
    int hard_interval = strong_interval(g->level);
    g->strong_credit = hard_interval ? g->remaining % hard_interval : 0;
    SDL_memset(g->bricks, 0, sizeof(g->bricks));
    SDL_memset(g->bonus_bricks, 0, sizeof(g->bonus_bricks));
    SDL_memset(g->strong_hits, 0, sizeof(g->strong_hits));
    SDL_memset(g->strong_type, 0, sizeof(g->strong_type));
    for (int r = 0; r < ROWS; ++r) {
        g->row_age[r] = BRICK_FADE_SECONDS;
        g->row_color[r] = generated_row_color(g->level, r);
        for (int c = 0; c < COLS; ++c) {
            g->bricks[r][c] = r < INITIAL_ROWS;
            g->brick_hit_time[r][c] = -100;
        }
    }
    int cells[INITIAL_ROWS * COLS];
    for (int i = 0; i < INITIAL_ROWS * COLS; ++i) cells[i] = i;
    place_specials(g, cells, INITIAL_ROWS * COLS,
        g->remaining / split_interval(g->level), hard_interval ? g->remaining / hard_interval : 0);
}

static void start_run(Game *g) {
    g->level = 1;
    g->silvers_since_gold = 0;
    g->destroyed = g->score = 0;
    g->lives = 3;
    g->miss_time = 0;
    g->reset_field_after_miss = false;
    g->left_down = false;
    g->charge = 0;
    reset_history(g);
    SDL_memset(g->particles, 0, sizeof(g->particles));
    SDL_memset(g->balls, 0, sizeof(g->balls));
    init_field(g);
    reset_ball(g, &g->balls[0]);
}

static void click(Game *g) {
    switch (g->screen) {
    case TITLE:
        start_run(g);
        g->screen = PLAYING;
        break;
    case PLAYING: if (g->miss_time <= 0) g->left_down = true; break;
    case GAME_OVER: g->screen = TITLE; break;
    }
}

static bool brick_solid(const Game *g, int row) {
    /* At exactly 50% opacity, it is still non-solid. */
    return g->row_age[row] > BRICK_FADE_SECONDS * 0.5f;
}

static float descent_speed(const Game *g) {
    float multiplier = 4;
    for (int r = 0; r < ROWS && multiplier > 1; ++r) {
        if (brick_rect(g, r, 0).y + BRICK_H <= DESCENT_LINE_Y) continue;
        for (int c = 0; c < COLS; ++c) {
            if (g->bricks[r][c]) { multiplier = 1; break; }
        }
    }
    return BASE_DESCENT_SPEED * (1 + 0.25f * (g->level / 10)) * multiplier;
}

static bool field_threatens_paddle(const Game *g) {
    for (int r = 0; r < ROWS; ++r) {
        if (!brick_solid(g, r)) continue;
        for (int c = 0; c < COLS; ++c) {
            if (!g->bricks[r][c]) continue;
            SDL_FRect block = brick_rect(g, r, c);
            if (block.y + block.h > PADDLE_Y + 15) return true;
            if (block.y + block.h >= PADDLE_Y && block.y <= PADDLE_Y + 15 &&
                block.x + block.w >= g->paddle - g->paddle_width / 2 &&
                block.x <= g->paddle + g->paddle_width / 2) return true;
        }
    }
    return false;
}

static void add_top_row(Game *g) {
    for (int r = ROWS - 1; r > 0; --r) {
        SDL_memcpy(g->bricks[r], g->bricks[r - 1], sizeof(g->bricks[r]));
        SDL_memcpy(g->bonus_bricks[r], g->bonus_bricks[r - 1], sizeof(g->bonus_bricks[r]));
        SDL_memcpy(g->strong_hits[r], g->strong_hits[r - 1], sizeof(g->strong_hits[r]));
        SDL_memcpy(g->strong_type[r], g->strong_type[r - 1], sizeof(g->strong_type[r]));
        SDL_memcpy(g->brick_hit_time[r], g->brick_hit_time[r - 1], sizeof(g->brick_hit_time[r]));
        g->row_age[r] = g->row_age[r - 1];
        g->row_color[r] = g->row_color[r - 1];
    }
    for (int c = 0; c < COLS; ++c) {
        g->bricks[0][c] = true;
        g->bonus_bricks[0][c] = false;
        g->strong_hits[0][c] = 0;
        g->strong_type[0][c] = NO_METAL;
        g->brick_hit_time[0][c] = -100;
    }
    g->row_age[0] = 0;
    g->row_color[0] = generated_row_color(g->level, g->generated_rows++);
    int bonus_count = (g->bonus_credit + COLS) / split_interval(g->level);
    g->bonus_credit = (g->bonus_credit + COLS) % split_interval(g->level);
    int hard_interval = strong_interval(g->level);
    int strong_count = hard_interval ? (g->strong_credit + COLS) / hard_interval : 0;
    g->strong_credit = hard_interval ? (g->strong_credit + COLS) % hard_interval : 0;
    int cells[COLS];
    for (int c = 0; c < COLS; ++c) cells[c] = c;
    place_specials(g, cells, COLS, bonus_count, strong_count);
    g->remaining += COLS;
}

static bool update_field(Game *g, float dt) {
    for (int r = 0; r < ROWS; ++r)
        g->row_age[r] = fminf(BRICK_FADE_SECONDS, g->row_age[r] + dt);
    g->scroll_offset += descent_speed(g) * dt;
    /* Lower rows cannot reach storage limits before crossing the death line. */
    if (field_threatens_paddle(g)) return true;
    while (g->scroll_offset >= BRICK_PITCH) {
        g->scroll_offset -= BRICK_PITCH;
        add_top_row(g);
    }
    return field_threatens_paddle(g);
}

static bool any_attached(const Game *g) {
    for (int i = 0; i < MAX_BALLS; ++i)
        if (g->balls[i].active && g->balls[i].attached) return true;
    return false;
}

static int ball_count(const Game *g) {
    int count = 0;
    for (int i = 0; i < MAX_BALLS; ++i) count += g->balls[i].active;
    return count;
}

static void start_paddle_spin(Ball *b, float offset_angle) {
    /* The signed contact angle grows toward either edge of the paddle. */
    b->spin_speed = hypotf(b->vx, b->vy) * 0.024f * (offset_angle / 0.8f);
    b->spin_left = fabsf(b->spin_speed) > 0.0001f ? 1.0f : 0;
}

static void release_ball(Game *g) {
    g->left_down = false;
    if (g->screen != PLAYING || g->miss_time > 0) return;
    for (int i = 0; i < MAX_BALLS; ++i) {
        Ball *b = &g->balls[i];
        if (!b->active || !b->attached) continue;
        b->shot_power = g->charge;
        b->pierces = (int)floorf(g->charge * 5.0f + 0.0001f);
        float launch_speed = speed(g) * (1.0f + 1.5f * g->charge);
        place_attached_ball(g, b);
        b->vx = launch_speed * sinf(launch_angle(b));
        b->vy = -launch_speed * cosf(launch_angle(b));
        start_paddle_spin(b, b->base_launch_angle);
        b->attached = false;
        b->gravity = false;
    }
    g->charge = 0;
}

static float random_unit(Game *g) {
    if (g->random_state == 0) g->random_state = 1234567;
    g->random_state = g->random_state * 1664525u + 1013904223u;
    return (float)(g->random_state >> 8) / 16777216.0f;
}

static void free_bonus_balls(Game *g, const Ball *impact, int row, int col) {
    SDL_FRect block = brick_rect(g, row, col);
    float impulse = clamp(hypotf(impact->vx, impact->vy) * 0.65f, 160, 1100);
    int emitted = 0;
    for (int slot = 0; slot < MAX_BALLS && emitted < 3; ++slot) {
        if (g->balls[slot].active) continue;
        Ball *ball = &g->balls[slot];
        *ball = (Ball){.active = true, .gravity = true};
        ball->x = block.x + block.w * (emitted + 1) / 4;
        ball->y = block.y + block.h / 2;
        ball->vx = (emitted - 1) * impulse * 0.85f + impact->vx * 0.25f
                   + (random_unit(g) - 0.5f) * impulse * 0.25f;
        ball->vy = -impulse * (0.55f + random_unit(g) * 0.5f) + impact->vy * 0.15f;
        ++emitted;
    }
}

static void shatter(Game *g, const Ball *b, int row, int col) {
    SDL_FRect rect = brick_rect(g, row, col);
    int slot = 0;
    for (int i = 0; i < 24; ++i) {
        while (slot < MAX_PARTICLES && g->particles[slot].life > 0) ++slot;
        if (slot == MAX_PARTICLES) return;
        float x = rect.x + ((i % 8) + 0.5f) * rect.w / 8;
        float y = rect.y + ((i / 8) + 0.5f) * rect.h / 3;
        float lifetime = 0.55f + random_unit(g) * 0.5f;
        Particle p = {0};
        p.x = x; p.y = y;
        p.vx = (x - rect.x - rect.w / 2) * 7 + (random_unit(g) - 0.5f) * 180 + b->vx * 0.15f;
        p.vy = -100 - random_unit(g) * 230 + b->vy * 0.12f;
        p.life = p.lifetime = lifetime;
        p.size = 3 + random_unit(g) * 5;
        p.red = g->row_color[row].r;
        p.green = g->row_color[row].g;
        p.blue = g->row_color[row].b;
        g->particles[slot++] = p;
    }
}

static void update_particles(Game *g, float dt) {
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        Particle *p = &g->particles[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vy += 600 * dt;
    }
}

static void break_paddle(Game *g) {
    --g->lives;
    g->miss_time = MISS_DURATION;
    g->left_down = false;
    SDL_memset(g->balls, 0, sizeof(g->balls));
    g->charge = 0;
    for (int i = 0; i < 3; ++i) {
        g->paddle_pieces[i] = (PaddlePiece){
            g->paddle + (i - 1) * g->paddle_width / 3,
            PADDLE_Y + 7.5f,
            (i - 1) * 240.0f + (i == 1 ? 35 : 0), -330.0f - i * 45,
            0, (i == 1 ? -1 : 1) * (7.0f + i * 3), g->paddle_width / 3};
    }
}

static void start_spin(Ball *b, float nx, float ny) {
    float torque = b->vx * ny - b->vy * nx;
    float direction = torque != 0 ? (torque > 0 ? 1 : -1) : (nx + ny >= 0 ? 1 : -1);
    b->spin_speed = direction * hypotf(b->vx, b->vy) * 0.018f;
    b->spin_left = 1.0f;
}

static void update_spin(Ball *b, float dt) {
    if (b->spin_left <= 0) return;
    float step = fminf(dt, b->spin_left);
    float remaining = b->spin_left - step;
    b->rotation = fmodf(b->rotation + b->spin_speed * (b->spin_left + remaining) * step / 2,
                         6.28318530718f);
    b->spin_left = remaining;
    if (remaining <= 0) b->spin_speed = 0;
}

static void begin_soft_bounce(Ball *b, float nx, float ny, float vx, float vy) {
    float length = hypotf(nx, ny);
    b->normal_x = nx / length;
    b->normal_y = ny / length;
    b->bounce_vx = vx;
    b->bounce_vy = vy;
    start_spin(b, nx, ny);
    b->squash_time = 0;
    b->deforming = b->bounce_pending = true;
}

static float squash_amount(const Ball *b) {
    if (!b->deforming) return 0;
    if (b->squash_time < SOFT_COMPRESS_SECONDS)
        return sinf(b->squash_time / SOFT_COMPRESS_SECONDS * 1.57079632679f);
    float recovery = (b->squash_time - SOFT_COMPRESS_SECONDS) / SOFT_RECOVER_SECONDS;
    return cosf(clamp(recovery, 0, 1) * 1.57079632679f);
}

static bool update_soft_bounce(Ball *b, float dt) {
    if (!b->deforming) return false;
    b->squash_time += dt;
    if (b->bounce_pending) {
        if (b->squash_time < SOFT_COMPRESS_SECONDS) return true;
        /* Only now, at maximum compression, reverse the physical velocity. */
        b->vx = b->bounce_vx;
        b->vy = b->bounce_vy;
        b->bounce_pending = false;
    }
    if (b->squash_time >= SOFT_COMPRESS_SECONDS + SOFT_RECOVER_SECONDS)
        b->deforming = false;
    return false;
}

static void update_ball(Game *g, Ball *b, float dt) {
    if (!b->active) return;
    update_spin(b, dt);
    if (b->attached) {
        place_attached_ball(g, b);
        return;
    }
    if (update_soft_bounce(b, dt)) return;
    if (b->gravity) b->vy = fminf(b->vy + BONUS_GRAVITY * dt, BONUS_MAX_FALL_SPEED);
    float old_x = b->x, old_y = b->y;
    if (b->vy <= 0) b->preview_active = false;
    if (!b->preview_active && b->vy > 0 && old_y <= PREVIEW_START_Y &&
        old_y + b->vy * dt >= PREVIEW_START_Y) {
        float time_to_line = (PREVIEW_START_Y - old_y) / b->vy;
        b->preview_x = old_x + b->vx * time_to_line;
        b->preview_vx = b->vx;
        b->preview_vy = b->vy;
        b->preview_active = true;
    }
    b->x += b->vx * dt;
    b->y += b->vy * dt;
    float wall_nx = 0, wall_ny = 0;
    float out_vx = b->vx, out_vy = b->vy;
    if (b->x < RADIUS) { b->x = RADIUS; wall_nx = 1; out_vx = fabsf(b->vx); }
    if (b->x > WIDTH - RADIUS) { b->x = WIDTH - RADIUS; wall_nx = -1; out_vx = -fabsf(b->vx); }
    if (b->y < 60 + RADIUS) { b->y = 60 + RADIUS; wall_ny = 1; out_vy = fabsf(b->vy); }
    if (wall_nx != 0 || wall_ny != 0) {
        begin_soft_bounce(b, wall_nx, wall_ny, out_vx, out_vy);
        return;
    }
    float hit_offset;
    if (hit_paddle(g->paddle, g->paddle_width, old_x, old_y, b->x, b->y, &hit_offset)) {
        reset_ball(g, b);
        b->base_launch_angle = bounce_offset_angle(hit_offset, g->paddle_width);
        b->attach_offset = clamp(hit_offset, -g->paddle_width / 2 + RADIUS,
                                 g->paddle_width / 2 - RADIUS);
        place_attached_ball(g, b);
        return;
    }
    /* All body contacts are resolved before any afterimage contact. */
    float trails[MAX_TRAILS];
    int count = paddle_trails(g, trails);
    for (int i = 0; i < count; ++i) {
        float width = g->width_history[(i + 1) * (PADDLE_HISTORY - 1) / count];
        if (!hit_paddle(trails[i], width, old_x, old_y, b->x, b->y, &hit_offset)) continue;
        float magnitude = hypotf(b->vx, b->vy);
        float bounce_angle = bounce_offset_angle(hit_offset, width);
        b->vx = magnitude * sinf(bounce_angle);
        b->vy = -magnitude * cosf(bounce_angle);
        start_paddle_spin(b, bounce_offset_angle(hit_offset, width));
        b->x = trails[i] + hit_offset;
        b->y = PADDLE_Y - RADIUS - 1;
        b->preview_active = false;
        return;
    }
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            if (!g->bricks[r][c] || !brick_solid(g, r)) continue;
            SDL_FRect rect = brick_rect(g, r, c);
            float nx = clamp(b->x, rect.x, rect.x + rect.w);
            float ny = clamp(b->y, rect.y, rect.y + rect.h);
            float dx = b->x - nx, dy = b->y - ny;
            if (dx * dx + dy * dy > RADIUS * RADIUS) continue;
            bool was_strong = g->strong_hits[r][c] > 0;
            bool destroyed = true;
            if (was_strong) {
                g->brick_hit_time[r][c] = g->animation_time;
                int damage = b->shot_power >= 0.9999f ? 2 : 1;
                int remaining_hits = g->strong_hits[r][c] - damage;
                g->strong_hits[r][c] = (Uint8)(remaining_hits > 0 ? remaining_hits : 0);
                destroyed = remaining_hits <= 0;
            }
            if (destroyed) {
                g->bricks[r][c] = false;
                if (g->bonus_bricks[r][c]) {
                    g->bonus_bricks[r][c] = false;
                    free_bonus_balls(g, b, r, c);
                }
                g->score += 100;
                ++g->destroyed;
                g->level = 1 + (int)(g->destroyed / 100);
                --g->remaining;
                if (b->shot_power > 0) shatter(g, b, r, c);
            }
            /* Strong bricks always reflect, even on a full-charge hit. */
            if (!was_strong && b->pierces > 0) {
                start_spin(b, -b->vx, -b->vy);
                --b->pierces;
                continue;
            }
            /* Resolve the contact first, hold and squash, then reflect. */
            float normal_x = 0, normal_y = 0;
            if (old_y <= rect.y && b->vy > 0) {
                normal_y = -1; b->y = rect.y - RADIUS;
            } else if (old_y >= rect.y + rect.h && b->vy < 0) {
                normal_y = 1; b->y = rect.y + rect.h + RADIUS;
            } else if (old_x <= rect.x && b->vx > 0) {
                normal_x = -1; b->x = rect.x - RADIUS;
            } else if (old_x >= rect.x + rect.w && b->vx < 0) {
                normal_x = 1; b->x = rect.x + rect.w + RADIUS;
            } else if (fabsf(b->vy) >= fabsf(b->vx)) {
                normal_y = b->vy >= 0 ? -1 : 1;
                b->y = normal_y < 0 ? rect.y - RADIUS : rect.y + rect.h + RADIUS;
            } else {
                normal_x = b->vx >= 0 ? -1 : 1;
                b->x = normal_x < 0 ? rect.x - RADIUS : rect.x + rect.w + RADIUS;
            }
            float dot = b->vx * normal_x + b->vy * normal_y;
            float restitution = b->gravity ? BONUS_RESTITUTION : 1.0f;
            begin_soft_bounce(b, normal_x, normal_y,
                (b->vx - 2 * dot * normal_x) * restitution,
                (b->vy - 2 * dot * normal_y) * restitution);
            return;
        }
    }
    if (b->y - RADIUS > HEIGHT) b->active = false;
}

static void update(Game *g, float dt) {
    g->animation_time += dt;
    update_particles(g, dt);
    if (g->screen != PLAYING) return;
    if (g->miss_time > 0) {
        for (int i = 0; i < 3; ++i) {
            PaddlePiece *p = &g->paddle_pieces[i];
            p->x += p->vx * dt;
            p->y += p->vy * dt;
            p->vy += 850 * dt;
            p->angle += p->spin * dt;
        }
        g->miss_time = fmaxf(0, g->miss_time - dt);
        if (g->miss_time == 0) {
            reset_history(g);
            if (g->lives == 0) g->screen = GAME_OVER;
            else {
                if (g->reset_field_after_miss) init_field(g);
                reset_ball(g, &g->balls[0]);
            }
            g->reset_field_after_miss = false;
        }
        return;
    }
    record_paddle(g);
    if (update_field(g, dt)) {
        g->reset_field_after_miss = true;
        break_paddle(g);
        return;
    }
    if (any_attached(g) && g->left_down) {
        g->charge = clamp(g->charge + dt / CHARGE_SECONDS, 0, 1);
        g->shake_time += dt;
    }
    for (int i = 0; i < MAX_BALLS && g->screen == PLAYING; ++i)
        update_ball(g, &g->balls[i], dt);
    if (g->screen == PLAYING && ball_count(g) == 0) break_paddle(g);
}

typedef struct {
    float x, y;
    Uint8 alpha;
} BallPreview;

static float preview_strength(const Game *g, const Ball *b) {
    if (g->screen != PLAYING || g->miss_time > 0 || !b->active || b->gravity || b->attached || !b->preview_active ||
        b->vy <= 0 || b->y > PREVIEW_END_Y) return 0;
    return clamp((b->y - PREVIEW_START_Y) /
                 (PREVIEW_FULL_Y - PREVIEW_START_Y), 0, 1);
}

static int predict_ball(const Game *g, const Ball *b, BallPreview samples[PREVIEW_SAMPLES]) {
    float strength = preview_strength(g, b);
    if (strength <= 0) return 0;
    /* Freeze the trajectory at the yellow-line crossing. Fold horizontal
       travel into the arena to include any number of side-wall reflections. */
    const float span = WIDTH - 2 * RADIUS;
    const float distance = PREVIEW_END_Y - PREVIEW_START_Y;
    int count = 0;
    for (int i = 0; i <= PREVIEW_SEGMENTS; ++i) {
        float fraction = (float)i / PREVIEW_SEGMENTS;
        float y = PREVIEW_START_Y + distance * fraction;
        if (y <= b->y) continue; /* Never leave prediction dots behind the ball. */
        float time = (y - PREVIEW_START_Y) / b->preview_vy;
        float phase = fmodf(b->preview_x - RADIUS + b->preview_vx * time,
                            2 * span);
        if (phase < 0) phase += 2 * span;
        float x = RADIUS + (phase <= span ? phase : 2 * span - phase);
        float opacity = fmaxf(0.05f, (float)(PREVIEW_SEGMENTS - i) * 0.05f);
        samples[count++] = (BallPreview){x, y,
            (Uint8)SDL_roundf(255.0f * opacity * strength)};
    }
    return count;
}

static void draw_disc(SDL_Renderer *r, float x, float y, float radius) {
    for (int row = -(int)radius; row <= (int)radius; ++row) {
        float half = sqrtf(radius * radius - (float)(row * row));
        SDL_RenderLine(r, x - half, y + row, x + half, y + row);
    }
}

static void draw_ball(SDL_Renderer *r, SDL_Texture *texture, float x, float y, float radius) {
    if (!texture) { draw_disc(r, x, y, radius); return; }
    Uint8 red, green, blue, alpha;
    SDL_GetRenderDrawColor(r, &red, &green, &blue, &alpha);
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, alpha);
    SDL_FRect dest = {x - radius, y - radius, radius * 2, radius * 2};
    SDL_RenderTexture(r, texture, NULL, &dest);
}

static void draw_soft_ball(SDL_Renderer *r, SDL_Texture *texture, const Ball *b, float x, float y) {
    if (!texture) { draw_disc(r, x, y, RADIUS); return; }
    float squash = squash_amount(b);
    float normal_scale = 1 - 0.55f * squash;
    float tangent_scale = 1 + 0.45f * squash;
    float nx = b->deforming ? b->normal_x : 1;
    float ny = b->deforming ? b->normal_y : 0;
    if (b->bounce_pending) {
        x -= nx * RADIUS * (1 - normal_scale);
        y -= ny * RADIUS * (1 - normal_scale);
    } else if (b->deforming) {
        /* After release, stretch along the outgoing path and narrow across it. */
        float magnitude = hypotf(b->bounce_vx, b->bounce_vy);
        if (magnitude > 0) {
            nx = b->bounce_vx / magnitude;
            ny = b->bounce_vy / magnitude;
        }
        normal_scale = 1 + 0.55f * squash;
        tangent_scale = 1 - 0.45f * squash;
    }
    Uint8 red, green, blue, alpha;
    SDL_GetRenderDrawColor(r, &red, &green, &blue, &alpha);
    SDL_FColor tint = {1, 1, 1, alpha / 255.0f};
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 255);
    SDL_Vertex vertices[4];
    const int indices[] = {0, 1, 2, 0, 2, 3};
    float cosine = cosf(b->rotation), sine = sinf(b->rotation);
    for (int i = 0; i < 4; ++i) {
        float u = i == 0 || i == 3 ? 0 : 1, v = i < 2 ? 0 : 1;
        float dx = (u * 2 - 1) * RADIUS, dy = (v * 2 - 1) * RADIUS;
        float rx = dx * cosine - dy * sine, ry = dx * sine + dy * cosine;
        float along_normal = (rx * nx + ry * ny) * normal_scale;
        float along_tangent = (-rx * ny + ry * nx) * tangent_scale;
        vertices[i] = (SDL_Vertex){
            {x + nx * along_normal - ny * along_tangent,
             y + ny * along_normal + nx * along_tangent}, tint, {u, v}};
    }
    SDL_RenderGeometry(r, texture, vertices, 4, indices, 6);
}

static void render_prediction(SDL_Renderer *r, const Game *g, const Ball *b) {
    BallPreview samples[PREVIEW_SAMPLES];
    int count = predict_ball(g, b, samples);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    /* Draw far to near. Keep the landing marker at least 5% opaque before fade. */
    for (int i = count - 1; i >= 0; --i) {
        if (samples[i].alpha == 0) continue;
        SDL_SetRenderDrawColor(r, 250, 250, 245, samples[i].alpha);
        draw_ball(r, g->ball_texture, samples[i].x, samples[i].y, RADIUS);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void color(SDL_Renderer *r, Uint8 red, Uint8 green, Uint8 blue) {
    SDL_SetRenderDrawColor(r, red, green, blue, 255);
}

static void centered(SDL_Renderer *r, float y, const char *text, float scale) {
    SDL_SetRenderScale(r, scale, scale);
    SDL_RenderDebugText(r, (WIDTH / scale - (float)SDL_strlen(text) * 8) / 2,
                        y / scale, text);
    SDL_SetRenderScale(r, 1, 1);
}

static void draw_paddle(SDL_Renderer *r, float x, float y, float width,
                        float red, float green, float blue, float alpha) {
    SDL_Vertex vertices[4];
    const int indices[] = {0, 1, 2, 0, 2, 3};
    for (int j = 0; j < 4; ++j) {
        float dx = (j == 0 || j == 3 ? -1 : 1) * width / 2;
        float dy = (j < 2 ? -1 : 1) * 7.5f;
        vertices[j].position = (SDL_FPoint){x + dx, y + dy};
        vertices[j].color = (SDL_FColor){red, green, blue, alpha};
        vertices[j].tex_coord = (SDL_FPoint){0, 0};
    }
    SDL_RenderGeometry(r, NULL, vertices, 4, indices, 6);
}

static void render_launch_guide(SDL_Renderer *r, const Game *g, const Ball *b) {
    if (g->screen != PLAYING || !b->active || !b->attached) return;
    float dx = sinf(launch_angle(b)), dy = -cosf(launch_angle(b));
    color(r, 255, 225, 130);
    for (int i = 0; i < 10; ++i) {
        float start = RADIUS + 7 + i * 13.0f, end = start + 7;
        SDL_RenderLine(r, b->x + dx * start, b->y + dy * start,
                          b->x + dx * end, b->y + dy * end);
    }
    float x = b->x + dx * 145, y = b->y + dy * 145;
    SDL_RenderLine(r, x, y, x - dx * 12 + dy * 6, y - dy * 12 - dx * 6);
    SDL_RenderLine(r, x, y, x - dx * 12 - dy * 6, y - dy * 12 + dx * 6);
}

static void render_paddle_pieces(SDL_Renderer *r, const Game *g) {
    const int indices[] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 3; ++i) {
        const PaddlePiece *p = &g->paddle_pieces[i];
        SDL_Vertex vertices[4];
        float cosine = cosf(p->angle), sine = sinf(p->angle);
        for (int j = 0; j < 4; ++j) {
            float x = (j == 0 || j == 3 ? -1 : 1) * p->width / 2;
            float y = (j < 2 ? -1 : 1) * 7.5f;
            vertices[j].position = (SDL_FPoint){p->x + x * cosine - y * sine,
                                               p->y + x * sine + y * cosine};
            vertices[j].color = (SDL_FColor){90 / 255.0f, 220 / 255.0f, 235 / 255.0f, 1};
            vertices[j].tex_coord = (SDL_FPoint){0, 0};
        }
        SDL_RenderGeometry(r, NULL, vertices, 4, indices, 6);
    }
}

static void render(SDL_Renderer *r, const Game *g) {
    color(r, 12, 18, 32);
    SDL_RenderClear(r);
    color(r, 226, 236, 250);
    if (g->screen == TITLE) {
        centered(r, 230, "BreakBout", 5);
        centered(r, 330, "CLICK TO START", 2);
        centered(r, 395, "MOVE MOUSE TO CONTROL THE PADDLE", 2);
        centered(r, 430, "HOLD LEFT BUTTON TO CHARGE / RELEASE TO FIRE", 2);
        centered(r, 485, "MOVE FASTER TO WIDEN THE PADDLE (UP TO 2X)", 2);
        centered(r, 535, "ENDLESS MODE - KEEP THE BLOCKS AWAY", 1.5f);
        centered(r, 580, "ESC: QUIT", 1.5f);
    } else {
        char hud[128];
        SDL_snprintf(hud, sizeof(hud), "LEVEL %d   LIVES %d   BALLS %d   SCORE %llu",
                     g->level, g->lives, ball_count(g), (unsigned long long)g->score);
        centered(r, 24, hud, 2);
        color(r, 40, 55, 78);
        SDL_RenderLine(r, 0, 59, WIDTH, 59);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        for (int row = 0; row < ROWS; ++row) {
            Uint8 alpha = (Uint8)SDL_roundf(255 * clamp(g->row_age[row] / BRICK_FADE_SECONDS, 0, 1));
            Uint8 red = g->row_color[row].r;
            Uint8 green = g->row_color[row].g;
            Uint8 blue = g->row_color[row].b;
            for (int col = 0; col < COLS; ++col) {
                if (!g->bricks[row][col]) continue;
                SDL_FRect rect = brick_rect(g, row, col);
                SDL_Color tint = g->strong_hits[row][col] > 0 ? strong_brick_color(g, row, col) :
                    (SDL_Color){red, green, blue, 255};
                SDL_SetRenderDrawColor(r, tint.r, tint.g, tint.b, alpha);
                SDL_RenderFillRect(r, &rect);
                if (g->bonus_bricks[row][col]) {
                    SDL_SetRenderDrawColor(r, 255, 235, 130, alpha);
                    SDL_RenderRect(r, &rect);
                    for (int ball = 0; ball < 3; ++ball) {
                        float phase = g->animation_time * (13 + ball * 3) + row * 7 + col * 3 + ball * 2;
                        float radius = 2.5f;
                        float x = rect.x + rect.w / 2 + sinf(phase) * (rect.w / 2 - radius - 1);
                        float y = rect.y + rect.h / 2 + sinf(phase * 1.7f + ball) * (rect.h / 2 - radius - 1);
                        draw_ball(r, g->ball_texture, x, y, radius);
                    }
                }
            }
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        for (int i = 0; i < MAX_BALLS; ++i)
            render_prediction(r, g, &g->balls[i]);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        for (int i = 0; i < MAX_PARTICLES; ++i) {
            const Particle *p = &g->particles[i];
            if (p->life <= 0) continue;
            SDL_SetRenderDrawColor(r, p->red, p->green, p->blue,
                (Uint8)(255 * clamp(p->life / p->lifetime, 0, 1)));
            SDL_FRect shard = {p->x, p->y, p->size, p->size};
            SDL_RenderFillRect(r, &shard);
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        if (g->miss_time > 0) {
            render_paddle_pieces(r, g);
        } else if (g->screen != GAME_OVER) {
            float trails[MAX_TRAILS];
            int trail_count = paddle_trails(g, trails);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            for (int i = trail_count - 1; i >= 0; --i) {
                float width = g->width_history[(i + 1) * (PADDLE_HISTORY - 1) / trail_count];
                draw_paddle(r, trails[i], PADDLE_Y + 7.5f, width,
                            90 / 255.0f, 220 / 255.0f, 235 / 255.0f, (85 - (trail_count > 1 ? 40.0f * i / (trail_count - 1) : 0)) / 255.0f);
            }
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            float shake_x = 0, shake_y = 0;
            if (any_attached(g) && g->left_down && g->charge > 0) {
                shake_x = sinf(g->shake_time * 105) * 4 * g->charge;
                shake_y = sinf(g->shake_time * 137) * 2 * g->charge;
            }
            draw_paddle(r, g->paddle + shake_x, PADDLE_Y + 7.5f + shake_y,
                        g->paddle_width, (90 + 165 * g->charge) / 255.0f,
                        (220 - 170 * g->charge) / 255.0f, (235 - 185 * g->charge) / 255.0f, 1);
            for (int i = 0; i < MAX_BALLS; ++i) {
                const Ball *b = &g->balls[i];
                if (!b->active) continue;
                render_launch_guide(r, g, b);
                color(r, 255, 255, 255);
                draw_soft_ball(r, g->ball_texture, b, b->x + (b->attached ? shake_x : 0),
                          b->y + (b->attached ? shake_y : 0));
            }
            if (g->screen == PLAYING && any_attached(g)) {
                char charge_text[96];
                SDL_snprintf(charge_text, sizeof(charge_text), "POWER %d%% / HOLD LEFT - RELEASE TO FIRE",
                             (int)SDL_roundf(g->charge * 100));
                centered(r, 540, charge_text, 2);
            }
        }
        if (g->screen == GAME_OVER) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 8, 12, 24, 230);
            SDL_FRect panel = {100, 285, 760, 155};
            SDL_RenderFillRect(r, &panel);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            color(r, 250, 250, 245);
            centered(r, 315, "GAME OVER", 3);
            centered(r, 385, "CLICK TO RETURN TO TITLE", 2);
        }
    }
    SDL_RenderPresent(r);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "BreakBout", SDL_GetError(), NULL);
        return 1;
    }
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    if (!SDL_CreateWindowAndRenderer("BreakBout", WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE,
                                     &window, &renderer)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "BreakBout", SDL_GetError(), NULL);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderLogicalPresentation(renderer, WIDTH, HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(renderer, 1);
    Game game = {.screen = TITLE, .paddle = WIDTH / 2,
                 .random_state = (Uint32)SDL_GetPerformanceCounter()};
    game.ball_texture = load_ball_texture(renderer);
    if (!game.ball_texture) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "BreakBout", SDL_GetError(), window);
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
        return 1;
    }
    bool running = true, focused = true;
    Uint64 last = SDL_GetPerformanceCounter();
    double accumulator = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            SDL_ConvertEventToRenderCoordinates(renderer, &event);
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) running = false;
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                focused = false;
                game.left_down = false;
                game.charge = 0;
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) focused = true;
            if (event.type == SDL_EVENT_MOUSE_MOTION)
                game.paddle = clamp(event.motion.x, PADDLE_W / 2, WIDTH - PADDLE_W / 2);
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                game.paddle = clamp(event.button.x, PADDLE_W / 2, WIDTH - PADDLE_W / 2);
                click(&game);
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT &&
                game.left_down) release_ball(&game);
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double elapsed = (double)(now - last) / (double)SDL_GetPerformanceFrequency();
        last = now;
        if (focused) {
            accumulator += elapsed < 0.05 ? elapsed : 0.05;
            while (accumulator >= STEP) {
                update(&game, (float)STEP);
                accumulator -= STEP;
            }
        } else accumulator = 0;
        render(renderer, &game);
        SDL_Delay(1);
    }
    SDL_DestroyTexture(game.ball_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
