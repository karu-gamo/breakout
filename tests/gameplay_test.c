/* Run the game logic without opening an interactive window. */
#define SDL_MAIN_HANDLED
#define main breakout_app_main
#include "../src/main.c"
#undef main
#undef NDEBUG
#include <assert.h>

static Game start(Uint32 seed) {
    Game g = {.screen = TITLE, .paddle = 480, .random_state = seed};
    click(&g);
    return g;
}

static void empty_stage(Game *g) {
    SDL_memset(g->bricks, 0, sizeof(g->bricks));
    SDL_memset(g->bonus_bricks, 0, sizeof(g->bonus_bricks));
    g->bricks[4][9] = true;
    g->remaining = 1;
}

static void test_placement(void) {
    Game g = start(123), other = start(321);
    assert(SDL_memcmp(g.bonus_bricks, other.bonus_bricks, sizeof(g.bonus_bricks)) != 0);
    for (int stage = 1; stage <= 6; ++stage) {
        g.stage = stage;
        load_stage(&g);
        int total = 0, bonus = 0;
        for (int r = 0; r < ROWS; ++r) {
            for (int c = 0; c < COLS; ++c) {
                assert(!g.bonus_bricks[r][c] || g.bricks[r][c]);
                total += g.bricks[r][c];
                bonus += g.bonus_bricks[r][c];
            }
        }
        assert(bonus == total / 11 && g.remaining == total);
        assert(ball_count(&g) == 1 && g.balls[0].attached);
    }
}

static void test_release_and_gravity(void) {
    Game g = start(123);
    empty_stage(&g);
    g.bricks[0][0] = g.bonus_bricks[0][0] = true;
    ++g.remaining;
    g.balls[0] = (Ball){.active = true, .x = 70, .y = 92, .vy = 340};
    update_ball(&g, &g.balls[0], (float)STEP);
    assert(ball_count(&g) == 4 && !g.bonus_bricks[0][0] && !g.bricks[0][0]);
    assert(g.score == 100 && g.remaining == 1);
    for (int i = 1; i <= 3; ++i) assert(g.balls[i].gravity && !g.balls[i].attached);
    float before = g.balls[1].vy;
    g.balls[1].x = 400; g.balls[1].y = 400;
    update_ball(&g, &g.balls[1], (float)STEP);
    assert(fabsf(g.balls[1].vy - before - BONUS_GRAVITY * (float)STEP) < 0.001f);

    Game slow = start(123), fast = start(123);
    Ball impact = {.active = true, .vx = 100, .vy = -300};
    free_bonus_balls(&slow, &impact, 0, 0);
    impact.vx *= 3; impact.vy *= 3;
    free_bonus_balls(&fast, &impact, 0, 0);
    for (int i = 1; i <= 3; ++i)
        assert(hypotf(fast.balls[i].vx, fast.balls[i].vy) > hypotf(slow.balls[i].vx, slow.balls[i].vy));

    float recoil[2];
    for (int i = 0; i < 2; ++i) {
        empty_stage(&g);
        g.bricks[1][0] = true; ++g.remaining;
        Ball b = {.active = true, .gravity = true, .x = 70, .y = 124, .vy = i ? 1000 : 500};
        update_ball(&g, &b, (float)STEP);
        assert(!g.bricks[1][0] && b.vy < 0);
        recoil[i] = fabsf(b.vy);
    }
    assert(recoil[1] > recoil[0]);
}

static void test_multicatch_and_charge(void) {
    Game g = start(123);
    empty_stage(&g);
    for (int i = 0; i < 3; ++i) {
        g.balls[i] = (Ball){.active = true, .gravity = true,
            .x = g.paddle + (i - 1) * 30, .y = PADDLE_Y - RADIUS - 1, .vy = 600};
        update_ball(&g, &g.balls[i], (float)STEP);
        assert(g.balls[i].attached && !g.balls[i].gravity);
        assert(fabsf(g.balls[i].base_launch_angle - bounce_offset_angle((i - 1) * 30)) < 0.001f);
    }
    g.left_down = true;
    for (int i = 0; i < 120; ++i) update(&g, (float)STEP);
    assert(fabsf(g.charge - 0.5f) < 0.0001f); /* Not multiplied by the number of balls. */
    for (int i = 0; i < 130; ++i) update(&g, (float)STEP);
    assert(g.charge == 1);
    g.paddle += 200;
    update(&g, (float)STEP);
    float angles[3];
    for (int i = 0; i < 3; ++i) angles[i] = launch_angle(&g, &g.balls[i]);
    release_ball(&g);
    for (int i = 0; i < 3; ++i) {
        Ball *b = &g.balls[i];
        assert(!b->attached && !b->gravity && b->pierces == 5);
        assert(fabsf(hypotf(b->vx, b->vy) - speed(&g) * 2.5f) < 0.001f);
        assert(fabsf(atan2f(b->vx, -b->vy) - angles[i]) < 0.0001f);
    }
    int lives = g.lives;
    g.balls[0].y = HEIGHT + 20; g.balls[0].vy = 100;
    update(&g, (float)STEP);
    assert(g.lives == lives && ball_count(&g) == 2);
    for (int i = 1; i < 3; ++i) { g.balls[i].y = HEIGHT + 20; g.balls[i].vy = 100; }
    update(&g, (float)STEP);
    assert(g.lives == lives - 1 && g.miss_time > 0);
    for (int i = 0; i < 220; ++i) update(&g, (float)STEP);
    assert(ball_count(&g) == 1 && g.balls[0].attached);
}

static void test_trails_and_prediction(void) {
    Game g = start(123);
    for (int i = 0; i < PADDLE_HISTORY; ++i) g.paddle_history[i] = 700 - i * 5.0f;
    float trails[MAX_TRAILS];
    assert(paddle_trails(&g, trails) == 6 && trails[5] == 460);
    g.paddle = 700; g.paddle_angle = 0;
    Ball b = {.active = true, .x = 465, .y = PADDLE_Y - RADIUS - 1, .vy = 600};
    update_ball(&g, &b, (float)STEP);
    assert(!b.attached && b.vy < 0);
    reset_history(&g);
    b = (Ball){.active = true, .x = 700, .y = PADDLE_Y - RADIUS - 1, .vy = 600};
    update_ball(&g, &b, (float)STEP);
    assert(b.attached);
    b = (Ball){.active = true, .preview_active = true, .preview_x = 480,
        .preview_vy = 340, .x = 480, .y = PREVIEW_START_Y + 1, .vy = 340};
    BallPreview points[PREVIEW_SAMPLES];
    int count = predict_ball(&g, &b, points);
    assert(count == 10 && points[count - 1].y == PREVIEW_END_Y);
    b.y = PREVIEW_FULL_Y;
    count = predict_ball(&g, &b, points);
    assert(points[count - 1].alpha == 13);
}

static void test_render(void) {
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_Surface *surface = SDL_CreateSurface(WIDTH, HEIGHT, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(surface);
    assert(r);
    Game g = start(123);
    for (int i = 0; i < 3; ++i) {
        reset_ball(&g, &g.balls[i]);
        g.balls[i].attach_offset = (i - 1) * 30;
        g.balls[i].base_launch_angle = bounce_offset_angle(g.balls[i].attach_offset);
    }
    g.paddle += 150;
    update(&g, (float)STEP);
    render(r, &g);
    /* The three guides must reach three separate points on the rendered surface. */
    for (int i = 0; i < 3; ++i) {
        const Ball *b = &g.balls[i];
        int x = (int)SDL_roundf(b->x + sinf(launch_angle(&g, b)) * 122);
        int y = (int)SDL_roundf(b->y - cosf(launch_angle(&g, b)) * 122);
        bool visible = false;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                Uint8 red, green, blue, alpha;
                assert(SDL_ReadSurfacePixel(surface, x + dx, y + dy, &red, &green, &blue, &alpha));
                if (red > 200 && green > 150) visible = true;
            }
        }
        assert(visible);
    }
    /* Optional local capture for visual inspection. */
    if (SDL_getenv("BREAKBOUT_TEST_CAPTURE")) SDL_SaveBMP(surface, SDL_getenv("BREAKBOUT_TEST_CAPTURE"));
    g.animation_time += 0.25f; render(r, &g);
    break_paddle(&g); render(r, &g);
    SDL_DestroyRenderer(r); SDL_DestroySurface(surface); SDL_Quit();
}

int main(void) {
    test_placement();
    test_release_and_gravity();
    test_multicatch_and_charge();
    test_trails_and_prediction();
    test_render();
    puts("PASS: random bonus placement, three-ball release, impulse/gravity/recoil, multicatch, shared charge, per-ball launch/guide, last-ball loss, trails and prediction");
    return 0;
}
