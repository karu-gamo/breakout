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
#define PREVIEW_SAMPLES (PREVIEW_SEGMENTS + 1)
#define PREVIEW_END_Y (PADDLE_Y - RADIUS)

typedef enum { TITLE, PLAYING, CLEAR, GAME_OVER } Screen;
typedef struct {
    float x, y, vx, vy, life, lifetime, size;
    Uint8 red, green, blue;
} Particle;

typedef struct {
    Screen screen;
    float paddle, x, y, vx, vy;
    bool attached;
    bool left_down;
    float charge, shot_power, attach_offset, launch_ratio, shake_time;
    int pierces;
    Uint32 random_state;
    Particle particles[MAX_PARTICLES];
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

static void reset_ball(Game *g) {
    g->attached = true;
    g->preview_active = false;
    g->charge = g->shot_power = 0;
    g->pierces = 0;
    g->attach_offset = 0;
    g->launch_ratio = 0.35f;
    g->x = g->paddle;
    g->y = PADDLE_Y - RADIUS - 1;
    g->vx = speed(g) * 0.35f;
    g->vy = -sqrtf(speed(g) * speed(g) - g->vx * g->vx);
}

static SDL_FRect brick_rect(int row, int col) {
    return (SDL_FRect){42.0f + col * 88.0f, 100.0f + row * 32.0f, 82, 25};
}

static void load_stage(Game *g) {
    g->remaining = 0;
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
    case PLAYING: g->left_down = true; break;
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
    if (g->screen != PLAYING || !g->attached) return;
    g->shot_power = g->charge;
    g->pierces = (int)floorf(g->charge * 5.0f + 0.0001f);
    float launch_speed = speed(g) * (1.0f + 4.0f * g->charge);
    g->vx = launch_speed * g->launch_ratio;
    g->vy = -sqrtf(launch_speed * launch_speed - g->vx * g->vx);
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

static void update(Game *g, float dt) {
    update_particles(g, dt);
    if (g->screen != PLAYING) return;
    if (g->attached) {
        g->x = g->paddle + g->attach_offset;
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
    if (g->vy > 0 && old_y + RADIUS <= PADDLE_Y &&
        g->y + RADIUS >= PADDLE_Y) {
        float t = (PADDLE_Y - RADIUS - old_y) / (g->y - old_y);
        float hit_x = old_x + (g->x - old_x) * t;
        if (fabsf(hit_x - g->paddle) <= PADDLE_W / 2 + RADIUS) {
            reset_ball(g);
            g->attach_offset = clamp(hit_x - g->paddle, -PADDLE_W / 2 + RADIUS,
                                     PADDLE_W / 2 - RADIUS);
            g->launch_ratio = clamp(g->attach_offset / (PADDLE_W / 2), -1, 1) * 0.85f;
            if (fabsf(g->launch_ratio) < 0.15f) g->launch_ratio = 0.35f;
            g->x = g->paddle + g->attach_offset;
            return;
        }
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
    if (g->y - RADIUS > HEIGHT) {
        if (--g->lives == 0) {
            g->screen = GAME_OVER;
            g->left_down = false;
        } else reset_ball(g);
    }
}

typedef struct {
    float x, y;
    Uint8 alpha;
} BallPreview;

static float preview_strength(const Game *g) {
    if (g->screen != PLAYING || g->attached || !g->preview_active ||
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
        float opacity = (float)(PREVIEW_SEGMENTS - i) * 0.05f;
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
    /* Draw far to near, with opacity increasing by 5% per distance segment. */
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

static void render(SDL_Renderer *r, const Game *g) {
    color(r, 12, 18, 32);
    SDL_RenderClear(r);
    color(r, 226, 236, 250);
    if (g->screen == TITLE) {
        centered(r, 230, "BREAKOUT", 5);
        centered(r, 330, "CLICK TO START", 2);
        centered(r, 395, "MOVE MOUSE TO CONTROL THE PADDLE", 2);
        centered(r, 430, "HOLD LEFT BUTTON TO CHARGE / RELEASE TO FIRE", 2);
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
        float shake_x = 0, shake_y = 0;
        if (g->attached && g->left_down && g->charge > 0) {
            shake_x = sinf(g->shake_time * 105) * 4 * g->charge;
            shake_y = sinf(g->shake_time * 137) * 2 * g->charge;
        }
        color(r, (Uint8)(90 + 165 * g->charge),
                 (Uint8)(220 - 170 * g->charge), (Uint8)(235 - 185 * g->charge));
        SDL_FRect paddle = {g->paddle - PADDLE_W / 2 + shake_x,
                            PADDLE_Y + shake_y, PADDLE_W, 15};
        SDL_RenderFillRect(r, &paddle);
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
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Breakout", SDL_GetError(), NULL);
        return 1;
    }
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    if (!SDL_CreateWindowAndRenderer("Breakout", WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE,
                                     &window, &renderer)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Breakout", SDL_GetError(), NULL);
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
