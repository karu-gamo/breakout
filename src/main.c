#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define WIDTH 960
#define HEIGHT 720
#define COLS 10
#define ROWS 5
#define RADIUS 8.0f
#define PADDLE_Y 650.0f
#define PADDLE_W 120.0f
#define STEP (1.0 / 240.0)
/* Heights from the reference image, in the 960 x 720 logical canvas. */
#define PREVIEW_START_Y 290.0f
#define PREVIEW_FULL_Y 425.0f
#define PREVIEW_SEGMENTS 10
#define MAX_PARTICLES 768
#define CHARGE_SECONDS 1.0f
#define TRAIL_SECONDS 0.2f
#define MAX_TRAILS 6
#define PADDLE_HISTORY 49
#define TRAIL_FAST_SPEED 1200.0f
#define MISS_DURATION 0.85f
#define DEG_TO_RAD (0.017453292519943295f)
#define MAX_TILT (40.0f * DEG_TO_RAD)
#define PREVIEW_SAMPLES (PREVIEW_SEGMENTS + 1)
#define PREVIEW_END_Y (PADDLE_Y - RADIUS)

typedef enum { TITLE, PLAYING, CLEAR, GAME_OVER } Screen;
typedef struct {
    float x, y, vx, vy, life, lifetime, size;
    Uint8 red, green, blue;
} Particle;

typedef struct {
    float x, y, vx, vy, angle, spin;
} PaddlePiece;

typedef struct {
    Screen screen;
    float paddle, x, y, vx, vy;
    bool attached;
    bool left_down;
    float paddle_angle;
    float angle_history[PADDLE_HISTORY];
    float charge, shot_power, attach_offset, base_launch_angle, shake_time;
    int pierces;
    Uint32 random_state;
    Particle particles[MAX_PARTICLES];
    float paddle_history[PADDLE_HISTORY];
    float miss_time;
    PaddlePiece paddle_pieces[3];
    bool preview_active;
    float preview_x, preview_vx, preview_vy;
    bool bricks[ROWS][COLS];
    int stage, lives, remaining, score;
} Game;

static float clamp(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static float speed(const Game *g) {
    return 340.0f + 25.0f * (float)(g->stage < 12 ? g->stage - 1 : 11);
}

static void reset_history(Game *g) {
    for (int i = 0; i < PADDLE_HISTORY; ++i) {
        g->paddle_history[i] = g->paddle;
        g->angle_history[i] = g->paddle_angle;
    }
}

static void record_paddle(Game *g) {
    float movement = g->paddle - g->paddle_history[0];
    if (movement != 0)
        g->paddle_angle = clamp(g->paddle_angle + movement * 0.15f * DEG_TO_RAD,
                                -MAX_TILT, MAX_TILT);
    else {
        float amount = 90 * DEG_TO_RAD * (float)STEP;
        g->paddle_angle -= clamp(g->paddle_angle, -amount, amount);
    }
    for (int i = PADDLE_HISTORY - 1; i > 0; --i) {
        g->paddle_history[i] = g->paddle_history[i - 1];
        g->angle_history[i] = g->angle_history[i - 1];
    }
    g->paddle_history[0] = g->paddle;
    g->angle_history[0] = g->paddle_angle;
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

static float bounce_offset_angle(float offset) {
    return clamp(offset / (PADDLE_W / 2), -1, 1) * 0.8f;
}

static float launch_angle(const Game *g) {
    return g->base_launch_angle + g->paddle_angle;
}

static void place_attached_ball(Game *g) {
    float c = cosf(g->paddle_angle), n = sinf(g->paddle_angle);
    g->x = g->paddle + g->attach_offset * c + (RADIUS + 8.5f) * n;
    g->y = PADDLE_Y + 7.5f + g->attach_offset * n - (RADIUS + 8.5f) * c;
}

static bool hit_paddle(float center, float angle, float old_x, float old_y,
                       float x, float y, float *offset) {
    float c = cosf(angle), n = sinf(angle);
    float before = (old_x - center) * n - (old_y - PADDLE_Y - 7.5f) * c;
    float after = (x - center) * n - (y - PADDLE_Y - 7.5f) * c;
    float contact = 7.5f + RADIUS;
    if (before < contact || after > contact || after >= before) return false;
    float t = (before - contact) / (before - after);
    float hx = old_x + (x - old_x) * t - center;
    float hy = old_y + (y - old_y) * t - PADDLE_Y - 7.5f;
    *offset = hx * c + hy * n;
    return fabsf(*offset) <= PADDLE_W / 2 + RADIUS;
}

static void reset_ball(Game *g) {
    g->attached = true;
    g->preview_active = false;
    g->charge = g->shot_power = 0;
    g->pierces = 0;
    g->attach_offset = 0;
    g->base_launch_angle = 0;
    place_attached_ball(g);
    g->vx = speed(g) * 0.35f;
    g->vy = -sqrtf(speed(g) * speed(g) - g->vx * g->vx);
}

static SDL_FRect brick_rect(int row, int col) {
    return (SDL_FRect){42.0f + col * 88.0f, 100.0f + row * 32.0f, 82, 25};
}

static void load_stage(Game *g) {
    g->remaining = 0;
    g->miss_time = 0;
    g->left_down = false;
    g->paddle_angle = 0;
    reset_history(g);
    SDL_memset(g->particles, 0, sizeof(g->particles));
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            /* Three simple layouts repeat as the ball gradually gets faster. */
            bool present = (g->stage % 3 == 1) ||
                (g->stage % 3 == 2 ? (r + c) % 2 == 0 || r == 0 :
                 c >= r && c < COLS - r);
            g->bricks[r][c] = present;
            g->remaining += present;
        }
    }
    reset_ball(g);
}

static void click(Game *g) {
    switch (g->screen) {
    case TITLE:
        g->stage = 1;
        g->lives = 3;
        g->score = 0;
        load_stage(g);
        g->screen = PLAYING;
        break;
    case PLAYING: if (g->miss_time <= 0) g->left_down = true; break;
    case CLEAR:
        ++g->stage;
        load_stage(g);
        g->screen = PLAYING;
        break;
    case GAME_OVER: g->screen = TITLE; break;
    }
}

static void release_ball(Game *g) {
    g->left_down = false;
    if (g->screen != PLAYING || !g->attached || g->miss_time > 0) return;
    g->shot_power = g->charge;
    g->pierces = (int)floorf(g->charge * 5.0f + 0.0001f);
    float launch_speed = speed(g) * (1.0f + 1.5f * g->charge);
    place_attached_ball(g);
    g->vx = launch_speed * sinf(launch_angle(g));
    g->vy = -launch_speed * cosf(launch_angle(g));
    g->attached = false;
    g->charge = 0;
}

static float random_unit(Game *g) {
    if (g->random_state == 0) g->random_state = 1234567;
    g->random_state = g->random_state * 1664525u + 1013904223u;
    return (float)(g->random_state >> 8) / 16777216.0f;
}

static void shatter(Game *g, int row, int col) {
    SDL_FRect b = brick_rect(row, col);
    int slot = 0;
    for (int i = 0; i < 24; ++i) {
        while (slot < MAX_PARTICLES && g->particles[slot].life > 0) ++slot;
        if (slot == MAX_PARTICLES) return;
        float x = b.x + ((i % 8) + 0.5f) * b.w / 8;
        float y = b.y + ((i / 8) + 0.5f) * b.h / 3;
        float lifetime = 0.55f + random_unit(g) * 0.5f;
        Particle p = {0};
        p.x = x; p.y = y;
        p.vx = (x - b.x - b.w / 2) * 7 + (random_unit(g) - 0.5f) * 180 + g->vx * 0.15f;
        p.vy = -100 - random_unit(g) * 230 + g->vy * 0.12f;
        p.life = p.lifetime = lifetime;
        p.size = 3 + random_unit(g) * 5;
        p.red = (Uint8)(245 - row * 25);
        p.green = (Uint8)(100 + row * 25);
        p.blue = (Uint8)(100 + row * 30);
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
    g->attached = false;
    g->preview_active = false;
    g->charge = 0;
    for (int i = 0; i < 3; ++i) {
        g->paddle_pieces[i] = (PaddlePiece){
            g->paddle + (i - 1) * PADDLE_W / 3 * cosf(g->paddle_angle),
            PADDLE_Y + 7.5f + (i - 1) * PADDLE_W / 3 * sinf(g->paddle_angle),
            (i - 1) * 240.0f + (i == 1 ? 35 : 0), -330.0f - i * 45,
            g->paddle_angle, (i == 1 ? -1 : 1) * (7.0f + i * 3)};
    }
}

static void update(Game *g, float dt) {
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
            g->paddle_angle = 0;
            reset_history(g);
            if (g->lives == 0) g->screen = GAME_OVER;
            else reset_ball(g);
        }
        return;
    }
    record_paddle(g);
    if (g->attached) {
        place_attached_ball(g);
        if (g->left_down) {
            g->charge = clamp(g->charge + dt / CHARGE_SECONDS, 0, 1);
            g->shake_time += dt;
        }
        return;
    }
    float old_x = g->x, old_y = g->y;
    if (g->vy <= 0) g->preview_active = false;
    if (!g->preview_active && g->vy > 0 && old_y <= PREVIEW_START_Y &&
        old_y + g->vy * dt >= PREVIEW_START_Y) {
        float time_to_line = (PREVIEW_START_Y - old_y) / g->vy;
        g->preview_x = old_x + g->vx * time_to_line;
        g->preview_vx = g->vx;
        g->preview_vy = g->vy;
        g->preview_active = true;
    }
    g->x += g->vx * dt;
    g->y += g->vy * dt;
    if (g->x < RADIUS) { g->x = RADIUS; g->vx = fabsf(g->vx); }
    if (g->x > WIDTH - RADIUS) { g->x = WIDTH - RADIUS; g->vx = -fabsf(g->vx); }
    if (g->y < 60 + RADIUS) { g->y = 60 + RADIUS; g->vy = fabsf(g->vy); }
    float hit_offset;
    if (hit_paddle(g->paddle, g->paddle_angle, old_x, old_y, g->x, g->y, &hit_offset)) {
        reset_ball(g);
        g->base_launch_angle = bounce_offset_angle(hit_offset);
        g->attach_offset = clamp(hit_offset, -PADDLE_W / 2 + RADIUS,
                                 PADDLE_W / 2 - RADIUS);
        place_attached_ball(g);
        return;
    }
    /* All body contacts are resolved before any afterimage contact. */
    float trails[MAX_TRAILS];
    int count = paddle_trails(g, trails);
    for (int i = 0; i < count; ++i) {
        float angle = g->angle_history[(i + 1) * (PADDLE_HISTORY - 1) / count];
        if (!hit_paddle(trails[i], angle, old_x, old_y, g->x, g->y, &hit_offset)) continue;
        float magnitude = hypotf(g->vx, g->vy);
        float bounce_angle = angle + bounce_offset_angle(hit_offset);
        g->vx = magnitude * sinf(bounce_angle);
        g->vy = -magnitude * cosf(bounce_angle);
        float c = cosf(angle), n = sinf(angle);
        g->x = trails[i] + hit_offset * c + (RADIUS + 8.5f) * n;
        g->y = PADDLE_Y + 7.5f + hit_offset * n - (RADIUS + 8.5f) * c;
        g->preview_active = false;
        return;
    }
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            if (!g->bricks[r][c]) continue;
            SDL_FRect b = brick_rect(r, c);
            float nx = clamp(g->x, b.x, b.x + b.w);
            float ny = clamp(g->y, b.y, b.y + b.h);
            float dx = g->x - nx, dy = g->y - ny;
            if (dx * dx + dy * dy > RADIUS * RADIUS) continue;
            g->bricks[r][c] = false;
            g->score += 100;
            --g->remaining;
            if (g->shot_power > 0) shatter(g, r, c);
            if (g->remaining == 0) {
                g->screen = CLEAR;
                g->left_down = false;
                return;
            }
            if (g->pierces > 0) {
                --g->pierces;
                continue;
            }
            if (old_y + RADIUS <= b.y) {
                g->y = b.y - RADIUS; g->vy = -fabsf(g->vy);
            } else if (old_y - RADIUS >= b.y + b.h) {
                g->y = b.y + b.h + RADIUS; g->vy = fabsf(g->vy);
            } else if (old_x < b.x) {
                g->x = b.x - RADIUS; g->vx = -fabsf(g->vx);
            } else {
                g->x = b.x + b.w + RADIUS; g->vx = fabsf(g->vx);
            }
            return;
        }
    }
    if (g->y - RADIUS > HEIGHT) break_paddle(g);
}

typedef struct {
    float x, y;
    Uint8 alpha;
} BallPreview;

static float preview_strength(const Game *g) {
    if (g->screen != PLAYING || g->miss_time > 0 || g->attached || !g->preview_active ||
        g->vy <= 0 || g->y > PREVIEW_END_Y) return 0;
    return clamp((g->y - PREVIEW_START_Y) /
                 (PREVIEW_FULL_Y - PREVIEW_START_Y), 0, 1);
}

static int predict_ball(const Game *g, BallPreview samples[PREVIEW_SAMPLES]) {
    float strength = preview_strength(g);
    if (strength <= 0) return 0;
    /* Freeze the trajectory at the yellow-line crossing. Fold horizontal
       travel into the arena to include any number of side-wall reflections. */
    const float span = WIDTH - 2 * RADIUS;
    const float distance = PREVIEW_END_Y - PREVIEW_START_Y;
    int count = 0;
    for (int i = 0; i <= PREVIEW_SEGMENTS; ++i) {
        float fraction = (float)i / PREVIEW_SEGMENTS;
        float y = PREVIEW_START_Y + distance * fraction;
        if (y <= g->y) continue; /* Never leave prediction dots behind the ball. */
        float time = (y - PREVIEW_START_Y) / g->preview_vy;
        float phase = fmodf(g->preview_x - RADIUS + g->preview_vx * time,
                            2 * span);
        if (phase < 0) phase += 2 * span;
        float x = RADIUS + (phase <= span ? phase : 2 * span - phase);
        float opacity = fmaxf(0.05f, (float)(PREVIEW_SEGMENTS - i) * 0.05f);
        samples[count++] = (BallPreview){x, y,
            (Uint8)SDL_roundf(255.0f * opacity * strength)};
    }
    return count;
}

static void draw_ball(SDL_Renderer *r, float x, float y) {
    for (int row = -(int)RADIUS; row <= (int)RADIUS; ++row) {
        float half = sqrtf(RADIUS * RADIUS - (float)(row * row));
        SDL_RenderLine(r, x - half, y + row, x + half, y + row);
    }
}

static void render_prediction(SDL_Renderer *r, const Game *g) {
    BallPreview samples[PREVIEW_SAMPLES];
    int count = predict_ball(g, samples);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    /* Draw far to near. Keep the landing marker at least 5% opaque before fade. */
    for (int i = count - 1; i >= 0; --i) {
        if (samples[i].alpha == 0) continue;
        SDL_SetRenderDrawColor(r, 250, 250, 245, samples[i].alpha);
        draw_ball(r, samples[i].x, samples[i].y);
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

static void draw_paddle(SDL_Renderer *r, float x, float y, float angle,
                        float red, float green, float blue, float alpha) {
    SDL_Vertex vertices[4];
    const int indices[] = {0, 1, 2, 0, 2, 3};
    float c = cosf(angle), n = sinf(angle);
    for (int j = 0; j < 4; ++j) {
        float dx = (j == 0 || j == 3 ? -1 : 1) * PADDLE_W / 2;
        float dy = (j < 2 ? -1 : 1) * 7.5f;
        vertices[j].position = (SDL_FPoint){x + dx * c - dy * n, y + dx * n + dy * c};
        vertices[j].color = (SDL_FColor){red, green, blue, alpha};
        vertices[j].tex_coord = (SDL_FPoint){0, 0};
    }
    SDL_RenderGeometry(r, NULL, vertices, 4, indices, 6);
}

static void render_launch_guide(SDL_Renderer *r, const Game *g) {
    if (g->screen != PLAYING || !g->attached) return;
    float dx = sinf(launch_angle(g)), dy = -cosf(launch_angle(g));
    color(r, 255, 225, 130);
    for (int i = 0; i < 10; ++i) {
        float start = 15 + i * 13.0f, end = start + 7;
        SDL_RenderLine(r, g->x + dx * start, g->y + dy * start,
                          g->x + dx * end, g->y + dy * end);
    }
    float x = g->x + dx * 145, y = g->y + dy * 145;
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
            float x = (j == 0 || j == 3 ? -1 : 1) * PADDLE_W / 6;
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
        centered(r, 485, "MOVE MOUSE TO TILT AND AIM (+/-40 DEG)", 2);
        centered(r, 560, "ESC: QUIT", 1.5f);
    } else {
        char hud[128];
        SDL_snprintf(hud, sizeof(hud), "STAGE %d     LIVES %d     SCORE %d",
                     g->stage, g->lives, g->score);
        centered(r, 24, hud, 2);
        color(r, 40, 55, 78);
        SDL_RenderLine(r, 0, 59, WIDTH, 59);
        for (int row = 0; row < ROWS; ++row) {
            color(r, (Uint8)(245 - row * 25), (Uint8)(100 + row * 25), (Uint8)(100 + row * 30));
            for (int col = 0; col < COLS; ++col) {
                if (g->bricks[row][col]) {
                    SDL_FRect b = brick_rect(row, col);
                    SDL_RenderFillRect(r, &b);
                }
            }
        }
        render_prediction(r, g);
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
                float angle = g->angle_history[(i + 1) * (PADDLE_HISTORY - 1) / trail_count];
                draw_paddle(r, trails[i], PADDLE_Y + 7.5f, angle,
                            90 / 255.0f, 220 / 255.0f, 235 / 255.0f, (85 - (trail_count > 1 ? 40.0f * i / (trail_count - 1) : 0)) / 255.0f);
            }
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            float shake_x = 0, shake_y = 0;
            if (g->attached && g->left_down && g->charge > 0) {
                shake_x = sinf(g->shake_time * 105) * 4 * g->charge;
                shake_y = sinf(g->shake_time * 137) * 2 * g->charge;
            }
            draw_paddle(r, g->paddle + shake_x, PADDLE_Y + 7.5f + shake_y,
                        g->paddle_angle, (90 + 165 * g->charge) / 255.0f,
                        (220 - 170 * g->charge) / 255.0f, (235 - 185 * g->charge) / 255.0f, 1);
            render_launch_guide(r, g);
            if (g->shot_power > 0) color(r, 255, 200, 100);
            else color(r, 250, 250, 245);
            draw_ball(r, g->x + (g->attached ? shake_x : 0),
                      g->y + (g->attached ? shake_y : 0));
            if (g->screen == PLAYING && g->attached) {
                char charge_text[96];
                SDL_snprintf(charge_text, sizeof(charge_text), "POWER %d%% / HOLD LEFT - RELEASE TO FIRE",
                             (int)SDL_roundf(g->charge * 100));
                centered(r, 540, charge_text, 2);
            }
        }
        if (g->screen == CLEAR || g->screen == GAME_OVER) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 8, 12, 24, 230);
            SDL_FRect panel = {100, 285, 760, 155};
            SDL_RenderFillRect(r, &panel);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            color(r, 250, 250, 245);
            centered(r, 315, g->screen == CLEAR ? "STAGE CLEAR!" : "GAME OVER", 3);
            centered(r, 385, g->screen == CLEAR ? "CLICK FOR NEXT STAGE" : "CLICK TO RETURN TO TITLE", 2);
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
    Game game = {.screen = TITLE, .paddle = WIDTH / 2};
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
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
