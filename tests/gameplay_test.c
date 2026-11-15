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
    assert(BONUS_GRAVITY == 930 && BONUS_MAX_FALL_SPEED == 3600);
    Ball falling = {.active = true, .gravity = true, .x = 400, .y = 400, .vy = 3599};
    update_ball(&g, &falling, (float)STEP);
    assert(falling.vy == BONUS_MAX_FALL_SPEED);

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
        assert(!g.bricks[1][0] && b.bounce_pending && b.vy > 0);
        assert(fabsf(b.bounce_vy + b.vy * 0.72f) < 0.001f);
        for (int step = 0; step < 20 && b.bounce_pending; ++step) update_ball(&g, &b, (float)STEP);
        assert(!b.bounce_pending && b.vy < 0);
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
        assert(fabsf(g.balls[i].base_launch_angle - bounce_offset_angle((i - 1) * 30, g.paddle_width)) < 0.001f);
    }
    g.left_down = true;
    for (int i = 0; i < 120; ++i) update(&g, (float)STEP);
    assert(fabsf(g.charge - 0.5f) < 0.0001f); /* Not multiplied by the number of balls. */
    for (int i = 0; i < 130; ++i) update(&g, (float)STEP);
    assert(g.charge == 1);
    g.paddle += 200;
    update(&g, (float)STEP);
    float angles[3];
    for (int i = 0; i < 3; ++i) angles[i] = launch_angle(&g.balls[i]);
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
    assert(paddle_trails(&g, trails) == 12 && trails[11] == 460);
    g.paddle = 700;
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

static void test_soft_bounce(void) {
    assert(RADIUS == 16);
    Game g = start(123);
    empty_stage(&g);
    const float velocities[3][2] = {{-500, 120}, {500, 120}, {100, -500}};
    for (int wall = 0; wall < 3; ++wall) {
        Ball b = {.active = true, .x = wall == 0 ? RADIUS : wall == 1 ? WIDTH - RADIUS : 400,
                  .y = wall == 2 ? 60 + RADIUS : 400,
                  .vx = velocities[wall][0], .vy = velocities[wall][1]};
        update_ball(&g, &b, (float)STEP);
        assert(b.bounce_pending && b.deforming);
        float x = b.x, y = b.y, vx = b.vx, vy = b.vy;
        for (int step = 0; step < 3; ++step) update_ball(&g, &b, (float)STEP);
        assert(b.bounce_pending && b.x == x && b.y == y && b.vx == vx && b.vy == vy);
        assert(squash_amount(&b) > 0.5f);
        for (int step = 0; step < 9; ++step) update_ball(&g, &b, (float)STEP);
        assert(!b.bounce_pending && b.deforming);
        assert(wall == 2 ? b.vy > 0 : wall == 0 ? b.vx > 0 : b.vx < 0);
        assert(fabsf(hypotf(b.vx, b.vy) - hypotf(vx, vy)) < 0.001f);
        for (int step = 0; step < 130; ++step) update_ball(&g, &b, (float)STEP);
        assert(!b.deforming && squash_amount(&b) == 0);
    }
    empty_stage(&g);
    g.bricks[0][0] = true; ++g.remaining;
    Ball b = {.active = true, .x = 70, .y = 100 - RADIUS, .vy = 400};
    update_ball(&g, &b, (float)STEP);
    assert(b.bounce_pending && b.vy == 400 && !g.bricks[0][0] && g.score == 100);
    for (int i = 0; i < 10; ++i) update_ball(&g, &b, (float)STEP);
    assert(b.vy == -400 && g.score == 100);
    empty_stage(&g);
    g.bricks[0][0] = true; ++g.remaining;
    b = (Ball){.active = true, .x = 70, .y = 100 - RADIUS, .vy = 400, .pierces = 5};
    update_ball(&g, &b, (float)STEP);
    assert(!b.bounce_pending && b.vy > 0 && b.pierces == 4);
}

static void test_spin(void) {
    Ball slow = {.active = true, .vx = -300, .vy = 100};
    Ball fast = slow; fast.vx *= 2; fast.vy *= 2;
    begin_soft_bounce(&slow, 1, 0, 300, 100);
    begin_soft_bounce(&fast, 1, 0, 600, 200);
    assert(fabsf(fast.spin_speed - slow.spin_speed * 2) < 0.0001f);
    float initial = fast.spin_speed;
    update_spin(&fast, 0.5f);
    assert(fast.rotation != 0 && fast.spin_left == 0.5f);
    assert(fabsf(fast.spin_speed * fast.spin_left) < fabsf(initial));
    update_spin(&fast, 0.5f);
    assert(fast.spin_left == 0 && fast.spin_speed == 0);
    float rotation = fast.rotation;
    update_spin(&fast, 1);
    assert(fast.rotation == rotation);
}

static void test_paddle_spin(void) {
    Game g = start(123);
    for (int i = 0; i < 3; ++i) {
        reset_ball(&g, &g.balls[i]);
        g.balls[i].base_launch_angle = bounce_offset_angle(i * 24, g.paddle_width);
    }
    release_ball(&g);
    assert(g.balls[0].spin_speed == 0 && g.balls[0].spin_left == 0);
    assert(g.balls[2].spin_speed > g.balls[1].spin_speed && g.balls[1].spin_speed > 0);
    Ball left = g.balls[2];
    start_paddle_spin(&left, -g.balls[2].base_launch_angle);
    assert(fabsf(left.spin_speed + g.balls[2].spin_speed) < 0.001f);
    update_spin(&left, 1);
    assert(left.spin_speed == 0);
    assert(SOFT_COMPRESS_SECONDS == 0.0325f);
}

static void test_speed_width(void) {
    Game g = start(123);
    assert(g.paddle_width == PADDLE_W);
    g.paddle += 200;
    update(&g, (float)STEP);
    assert(g.paddle_width == PADDLE_W * 2);
    assert(g.width_history[0] == g.paddle_width && g.width_history[1] == PADDLE_W);
    float wide = g.paddle_width;
    update(&g, (float)STEP);
    assert(g.paddle_width <= wide);
    for (int i = 0; i < 240; ++i) update(&g, (float)STEP);
    assert(fabsf(g.paddle_width - PADDLE_W) < 0.01f);
    g.paddle = 0; update(&g, (float)STEP);
    assert(g.paddle == 0 && g.paddle - g.paddle_width / 2 < 0);
    g.paddle = WIDTH; update(&g, (float)STEP);
    assert(g.paddle == WIDTH && g.paddle + g.paddle_width / 2 > WIDTH);
    empty_stage(&g);
    g.paddle = 480; g.paddle_width = 240; reset_history(&g);
    g.paddle_width = 240;
    Ball b = {.active = true, .x = 580, .y = PADDLE_Y - RADIUS - 1, .vy = 600};
    update_ball(&g, &b, (float)STEP);
    assert(b.attached && b.attach_offset > PADDLE_W / 2);
    assert(fabsf(b.base_launch_angle - bounce_offset_angle(100, 240)) < 0.001f);
    g.balls[0] = b;
    g.paddle_width = PADDLE_W;
    update_ball(&g, &g.balls[0], (float)STEP);
    assert(fabsf(g.balls[0].attach_offset) <= PADDLE_W / 2 - RADIUS);
    assert(launch_angle(&g.balls[0]) == b.base_launch_angle);
    g.paddle = 600;
    for (int i = 0; i < PADDLE_HISTORY; ++i) {
        g.paddle_history[i] = 200;
        g.width_history[i] = 240;
    }
    b = (Ball){.active = true, .x = 300, .y = PADDLE_Y - RADIUS - 1, .vy = 600};
    update_ball(&g, &b, (float)STEP);
    assert(!b.attached && b.vy < 0); /* Only the old, extended trail reaches this point. */
}

static void test_render(void) {
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_Surface *surface = SDL_CreateSurface(WIDTH, HEIGHT, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(surface);
    assert(r);
    Game g = start(123);
    g.ball_texture = load_ball_texture(r);
    assert(g.ball_texture);
    float width, height;
    assert(SDL_GetTextureSize(g.ball_texture, &width, &height));
    assert(width == 32 && height == 32);
    for (int i = 0; i < 3; ++i) {
        reset_ball(&g, &g.balls[i]);
        g.balls[i].attach_offset = (i - 1) * 30;
        g.balls[i].base_launch_angle = bounce_offset_angle(g.balls[i].attach_offset, g.paddle_width);
    }
    g.paddle += 150;
    update(&g, (float)STEP);
    render(r, &g);
    /* The three guides must reach three separate points on the rendered surface. */
    for (int i = 0; i < 3; ++i) {
        const Ball *b = &g.balls[i];
        int x = (int)SDL_roundf(b->x + sinf(launch_angle(b)) * 122);
        int y = (int)SDL_roundf(b->y - cosf(launch_angle(b)) * 122);
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
    g.balls[0] = (Ball){.active = true, .x = RADIUS, .y = 400, .vx = -500};
    begin_soft_bounce(&g.balls[0], 1, 0, 500, 0);
    g.balls[0].squash_time = SOFT_COMPRESS_SECONDS;
    render(r, &g);
    Uint8 red, green, blue, alpha;
    assert(SDL_ReadSurfacePixel(surface, 7, 420, &red, &green, &blue, &alpha));
    assert(blue > 100); /* Wider along the wall, with the PNG sprite. */
    assert(SDL_ReadSurfacePixel(surface, 23, 400, &red, &green, &blue, &alpha));
    assert(red < 100); /* Narrower perpendicular to the wall. */
    if (SDL_getenv("BREAKBOUT_SOFT_CAPTURE")) SDL_SaveBMP(surface, SDL_getenv("BREAKBOUT_SOFT_CAPTURE"));
    g.balls[0].x = 400; g.balls[0].y = 400;
    g.balls[0].bounce_pending = false;
    g.balls[0].rotation = 0;
    render(r, &g);
    assert(SDL_ReadSurfacePixel(surface, 420, 400, &red, &green, &blue, &alpha));
    assert(blue > 100); /* Stretched along the outgoing horizontal direction. */
    assert(SDL_ReadSurfacePixel(surface, 400, 420, &red, &green, &blue, &alpha));
    assert(blue < 100); /* Thin across the outgoing direction. */
    g.balls[0].squash_time = SOFT_COMPRESS_SECONDS + 0.25f;
    assert(squash_amount(&g.balls[0]) > 0 && squash_amount(&g.balls[0]) < 1);
    assert(!update_soft_bounce(&g.balls[0], 0.25f));
    assert(!g.balls[0].deforming && squash_amount(&g.balls[0]) == 0);
    break_paddle(&g); render(r, &g);
    SDL_DestroyTexture(g.ball_texture);
    SDL_DestroyRenderer(r); SDL_DestroySurface(surface); SDL_Quit();
}

int main(void) {
    test_placement();
    test_release_and_gravity();
    test_multicatch_and_charge();
    test_trails_and_prediction();
    test_soft_bounce();
    test_spin();
    test_paddle_spin();
    test_speed_width();
    test_render();
    puts("PASS: random bonus placement, three-ball release, impulse/gravity/recoil, multicatch, shared charge, per-ball launch/guide, last-ball loss, trails and prediction");
    return 0;
}
