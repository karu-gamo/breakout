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

typedef enum { TITLE, PLAYING, CLEAR, GAME_OVER } Screen;
typedef struct {
    Screen screen;
    float paddle, x, y, vx, vy;
    bool attached;
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
    case PLAYING: g->attached = false; break;
    case CLEAR:
        ++g->stage;
        load_stage(g);
        g->screen = PLAYING;
        break;
    case GAME_OVER: g->screen = TITLE; break;
    }
}

static void update(Game *g, float dt) {
    if (g->screen != PLAYING) return;
    if (g->attached) {
        g->x = g->paddle;
        return;
    }
    float old_x = g->x, old_y = g->y;
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
            float offset = clamp((hit_x - g->paddle) / (PADDLE_W / 2), -1, 1);
            g->vx = speed(g) * offset * 0.85f;
            g->vy = -sqrtf(speed(g) * speed(g) - g->vx * g->vx);
            g->y = PADDLE_Y - RADIUS;
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
            if (old_y + RADIUS <= b.y) {
                g->y = b.y - RADIUS; g->vy = -fabsf(g->vy);
            } else if (old_y - RADIUS >= b.y + b.h) {
                g->y = b.y + b.h + RADIUS; g->vy = fabsf(g->vy);
            } else if (old_x < b.x) {
                g->x = b.x - RADIUS; g->vx = -fabsf(g->vx);
            } else {
                g->x = b.x + b.w + RADIUS; g->vx = fabsf(g->vx);
            }
            if (g->remaining == 0) g->screen = CLEAR;
            return;
        }
    }
    if (g->y - RADIUS > HEIGHT) {
        if (--g->lives == 0) g->screen = GAME_OVER;
        else reset_ball(g);
    }
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
        centered(r, 430, "CLICK TO LAUNCH / 3 LIVES", 2);
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
        color(r, 90, 220, 235);
        SDL_FRect paddle = {g->paddle - PADDLE_W / 2, PADDLE_Y, PADDLE_W, 15};
        SDL_RenderFillRect(r, &paddle);
        color(r, 250, 250, 245);
        for (int y = -(int)RADIUS; y <= (int)RADIUS; ++y) {
            float half = sqrtf(RADIUS * RADIUS - (float)(y * y));
            SDL_RenderLine(r, g->x - half, g->y + y, g->x + half, g->y + y);
        }
        if (g->screen == PLAYING && g->attached) centered(r, 540, "CLICK TO LAUNCH", 2);
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
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) focused = false;
            if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) focused = true;
            if (event.type == SDL_EVENT_MOUSE_MOTION)
                game.paddle = clamp(event.motion.x, PADDLE_W / 2, WIDTH - PADDLE_W / 2);
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                game.paddle = clamp(event.button.x, PADDLE_W / 2, WIDTH - PADDLE_W / 2);
                click(&game);
            }
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
