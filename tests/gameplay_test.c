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

static void empty_field(Game *g) {
    SDL_memset(g->bricks, 0, sizeof(g->bricks));
    SDL_memset(g->bonus_bricks, 0, sizeof(g->bonus_bricks));
    SDL_memset(g->strong_hits, 0, sizeof(g->strong_hits));
    g->bricks[4][9] = true;
    g->remaining = 1;
}

static void test_placement(void) {
    Game g = start(123), other = start(321);
    assert(SDL_memcmp(g.bonus_bricks, other.bonus_bricks, sizeof(g.bonus_bricks)) != 0);
    for (int run = 0; run < 6; ++run) {
        init_field(&g);
        int total = 0, bonus = 0;
        for (int r = 0; r < ROWS; ++r) {
            for (int c = 0; c < COLS; ++c) {
                assert(!g.bonus_bricks[r][c] || g.bricks[r][c]);
                total += g.bricks[r][c];
                bonus += g.bonus_bricks[r][c];
            }
        }
        assert(bonus == total / 40 && g.remaining == total);
        assert(total == 200);
        assert(1 + bonus * 3 <= MAX_BALLS);
        SDL_FRect first = brick_rect(&g, 0, 0), last = brick_rect(&g, INITIAL_ROWS - 1, COLS - 1);
        assert(first.w == 41 && first.h == 12.5f);
        assert(first.w * first.h == 82 * 25 / 4.0f);
        assert(last.x + last.w < WIDTH && last.y + last.h < PREVIEW_START_Y);
        assert(ball_count(&g) == 1 && g.balls[0].attached);
    }
}

static void test_release_and_gravity(void) {
    Game g = start(123);
    empty_field(&g);
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
        empty_field(&g);
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
    empty_field(&g);
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
    g.balls[0].y = HEIGHT + 20; g.balls[0].vy = 100;
    update(&g, (float)STEP);
    assert(g.screen == PLAYING && g.miss_time == 0 && ball_count(&g) == 2);
    for (int i = 1; i < 3; ++i) { g.balls[i].y = HEIGHT + 20; g.balls[i].vy = 100; }
    update(&g, (float)STEP);
    assert(g.miss_time > 0 && ball_count(&g) == 0);
    for (int i = 0; i < 220; ++i) update(&g, (float)STEP);
    assert(g.screen == PLAYING && ball_count(&g) == 1 && g.lives == 2);
    assert(g.balls[0].attached);
    for (int miss = 0; miss < 2; ++miss) {
        release_ball(&g);
        g.balls[0].x = 20; g.balls[0].y = HEIGHT + RADIUS + 1; g.balls[0].vy = 100;
        update(&g, (float)STEP);
        for (int frame = 0; frame < 220; ++frame) update(&g, (float)STEP);
    }
    assert(g.screen == GAME_OVER && g.lives == 0);
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
    empty_field(&g);
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
    empty_field(&g);
    g.bricks[0][0] = true; ++g.remaining;
    Ball b = {.active = true, .x = 70, .y = 100 - RADIUS, .vy = 400};
    update_ball(&g, &b, (float)STEP);
    assert(b.bounce_pending && b.vy == 400 && !g.bricks[0][0] && g.score == 100);
    for (int i = 0; i < 10; ++i) update_ball(&g, &b, (float)STEP);
    assert(b.vy == -400 && g.score == 100);
    empty_field(&g);
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
    empty_field(&g);
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

static void clear_board(Game *g) {
    SDL_memset(g->bricks, 0, sizeof(g->bricks));
    SDL_memset(g->bonus_bricks, 0, sizeof(g->bonus_bricks));
    SDL_memset(g->strong_hits, 0, sizeof(g->strong_hits));
    g->remaining = 0;
    g->scroll_offset = 0;
    for (int row = 0; row < ROWS; ++row) g->row_age[row] = BRICK_FADE_SECONDS;
}

static void test_endless_descent(void) {
    Game g = start(123);
    assert(g.level == 1 && descent_speed(&g) == BASE_DESCENT_SPEED * 4);
    g.scroll_offset = 4;
    assert(descent_speed(&g) == BASE_DESCENT_SPEED);
    for (int col = 0; col < COLS; ++col) { g.bricks[9][col] = false; --g.remaining; }
    assert(descent_speed(&g) == BASE_DESCENT_SPEED * 4);
    init_field(&g);
    g.scroll_offset = BRICK_PITCH - 0.01f;
    float old_y = brick_rect(&g, 0, 0).y;
    bool old_bonus = g.bonus_bricks[0][0];
    SDL_Color old_color = g.row_color[0];
    assert(!update_field(&g, (float)STEP));
    assert(g.generated_rows == INITIAL_ROWS + 1 && g.remaining == 220);
    int bonuses = 0;
    for (int row = 0; row < ROWS; ++row)
        for (int col = 0; col < COLS; ++col) bonuses += g.bonus_bricks[row][col];
    assert(bonuses == 220 / 40);
    assert(BASE_DESCENT_SPEED == 8);
    assert(g.row_age[0] == 0 && !brick_solid(&g, 0));
    assert(g.bonus_bricks[1][0] == old_bonus && SDL_memcmp(&g.row_color[1], &old_color, sizeof(old_color)) == 0);
    assert(fabsf(brick_rect(&g, 1, 0).y - old_y - BASE_DESCENT_SPEED * (float)STEP) < 0.001f);
    g.row_age[0] = 0.25f;
    g.bonus_bricks[0][0] = false;
    Ball b = {.active = true, .x = 70, .y = 100 - RADIUS, .vy = 200};
    update_ball(&g, &b, (float)STEP);
    assert(g.bricks[0][0] && !b.bounce_pending);
    g.row_age[0] = 0.2501f;
    b = (Ball){.active = true, .x = 70, .y = 100 - RADIUS, .vy = 200};
    update_ball(&g, &b, (float)STEP);
    assert(!g.bricks[0][0] && b.bounce_pending);
    clear_board(&g);
    for (int i = 0; i < 900; ++i) {
        g.bricks[0][0] = true; g.remaining = 1;
        b = (Ball){.active = true, .x = 70, .y = 100 - RADIUS, .vy = 200};
        update_ball(&g, &b, (float)STEP);
        assert(g.screen == PLAYING && g.remaining == 0);
        assert(g.level == 1 + (int)(g.destroyed / 100));
    }
    assert(g.level == 10 && g.destroyed == 901);
    float level_ten = descent_speed(&g);
    g.level = 9; assert(descent_speed(&g) < level_ten);
    g.level = 19; assert(descent_speed(&g) == level_ten);
    g.level = 20; assert(descent_speed(&g) > level_ten);

    /* Endless replenishment: repeatedly clear the lower field and insert rows. */
    init_field(&g);
    for (int i = 0; i < 200; ++i) {
        for (int row = 2; row < ROWS; ++row) {
            for (int col = 0; col < COLS; ++col) {
                if (g.bricks[row][col]) --g.remaining;
                g.bricks[row][col] = g.bonus_bricks[row][col] = false;
            }
        }
        g.scroll_offset = BRICK_PITCH - 0.001f;
        assert(!update_field(&g, (float)STEP));
        int count = 0;
        for (int row = 0; row < ROWS; ++row)
            for (int col = 0; col < COLS; ++col) count += g.bricks[row][col];
        assert(count == g.remaining);
    }
}

static void test_block_death(void) {
    Game g = start(123);
    clear_board(&g);
    int row = 34, col = 9;
    g.bricks[row][col] = true; g.remaining = 1;
    g.scroll_offset = PADDLE_Y - (BRICK_TOP + row * BRICK_PITCH + BRICK_H);
    assert(field_threatens_paddle(&g));
    g.paddle = 800;
    assert(!field_threatens_paddle(&g));
    g.scroll_offset += 16;
    assert(field_threatens_paddle(&g)); /* Below the paddle, at a different x. */
    g.row_age[row] = 0.25f;
    assert(!field_threatens_paddle(&g));
    g.row_age[row] = BRICK_FADE_SECONDS;
    g.level = 5; g.destroyed = 450; g.score = 45000;
    update(&g, (float)STEP);
    assert(g.miss_time > 0 && ball_count(&g) == 0 && g.fatal_death && g.lives == 3);
    for (int i = 0; i < 220; ++i) update(&g, (float)STEP);
    assert(g.screen == GAME_OVER && g.lives == 3 && g.level == 5 && g.score == 45000 && g.destroyed == 450);
    assert(ball_count(&g) == 0);
    click(&g); assert(g.screen == TITLE);
    click(&g); assert(g.screen == PLAYING && g.level == 1 && g.score == 0);
    assert(ball_count(&g) == 1 && g.balls[0].attached);
}

static void test_level_colors(void) {
    for (int row = 0; row < 10; ++row) {
        SDL_Color a = generated_row_color(1, row), b = generated_row_color(9, row);
        assert(SDL_memcmp(&a, &b, sizeof(a)) == 0);
        SDL_Color c = generated_row_color(10, row);
        assert(SDL_memcmp(&a, &c, sizeof(a)) != 0);
        SDL_Color repeat = generated_row_color(1, row + 10);
        assert(SDL_memcmp(&a, &repeat, sizeof(a)) == 0);
        assert(a.r > 100 && a.g > 100 && a.b > 100);
    }
    SDL_Color first = generated_row_color(1, 0), last = generated_row_color(1, 9);
    assert(SDL_memcmp(&first, &level_palette[0], sizeof(first)) == 0);
    assert(SDL_memcmp(&last, &level_palette[1], sizeof(last)) == 0);
    Game g = start(123);
    SDL_Color existing = g.row_color[0];
    g.level = 10;
    add_top_row(&g);
    assert(SDL_memcmp(&g.row_color[1], &existing, sizeof(existing)) == 0);
    SDL_Color expected = generated_row_color(10, INITIAL_ROWS);
    assert(SDL_memcmp(&g.row_color[0], &expected, sizeof(expected)) == 0);
}

static void test_strong_bricks(void) {
    assert(strong_interval(9) == 0 && strong_interval(10) == 60);
    assert(strong_interval(14) == 60 && strong_interval(15) == 59);
    assert(strong_interval(205) == 21 && strong_interval(210) == 20 && strong_interval(500) == 20);
    assert(split_interval(1) == 40 && split_interval(4) == 40 && split_interval(5) == 41);
    assert(split_interval(9) == 42 && split_interval(40) == 49 && split_interval(41) == 50 && split_interval(500) == 50);
    const int levels[] = {1, 9, 10, 15, 60, 160};
    for (int index = 0; index < (int)SDL_arraysize(levels); ++index) {
        Game g = start(123); g.level = levels[index]; init_field(&g);
        int strong = 0, split = 0;
        for (int row = 0; row < ROWS; ++row) {
            for (int col = 0; col < COLS; ++col) {
                strong += g.strong_hits[row][col] > 0;
                split += g.bonus_bricks[row][col];
                assert(!g.strong_hits[row][col] || !g.bonus_bricks[row][col]);
            }
        }
        int interval = strong_interval(g.level);
        assert(strong == (interval ? 200 / interval : 0));
        assert(split == 200 / split_interval(g.level));
        add_top_row(&g);
        strong = split = 0;
        for (int row = 0; row < ROWS; ++row)
            for (int col = 0; col < COLS; ++col) {
                strong += g.strong_hits[row][col] > 0;
                split += g.bonus_bricks[row][col];
                assert(!g.strong_hits[row][col] || !g.bonus_bricks[row][col]);
            }
        assert(strong == (interval ? 220 / interval : 0));
        assert(split == 220 / split_interval(g.level));
    }
    for (int metal = SILVER; metal <= GOLD; ++metal) {
      for (int charge = 0; charge < 3; ++charge) {
        Game g = start(123); clear_board(&g);
        g.level = 10; g.destroyed = 900; g.score = 90000;
        int durability = metal == GOLD ? 8 : 4;
        int damage = charge == 2 ? 2 : 1;
        int hits = durability / damage;
        g.bricks[0][0] = true; g.strong_hits[0][0] = durability;
        g.strong_type[0][0] = metal; g.remaining = 1;
        for (int hit = 0; hit < hits; ++hit) {
            g.animation_time = 0.7f + hit;
            Ball b = {.active = true, .x = 70, .y = BRICK_TOP - RADIUS, .vy = 400,
                      .shot_power = charge == 2 ? 1 : charge == 1 ? 0.5f : 0,
                      .pierces = charge ? 5 : 0};
            update_ball(&g, &b, (float)STEP);
            assert(b.bounce_pending && b.pierces == (charge ? 5 : 0));
            int remaining = durability - (hit + 1) * damage;
            assert(g.strong_hits[0][0] == remaining);
            if (remaining > 0) {
                assert(g.bricks[0][0] && g.destroyed == 900 && g.score == 90000 && g.remaining == 1);
                assert(g.strong_type[0][0] == metal);
                SDL_Color white = strong_brick_color(&g, 0, 0);
                assert(white.r == 255 && white.g == 255 && white.b == 255);
                update_ball(&g, &b, (float)STEP);
                assert(g.strong_hits[0][0] == remaining);
            } else assert(!g.bricks[0][0] && g.destroyed == 901 && g.remaining == 0);
        }
      }
    }
    Game g = start(123); g.level = 10; init_field(&g);
    g.brick_hit_time[0][0] = -100;
    g.strong_type[0][0] = GOLD;
    g.animation_time = 0;
    SDL_Color color = strong_brick_color(&g, 0, 0);
    assert(color.r == 255 && color.g == 255 && color.b == 255);
    g.animation_time = 0.5f;
    color = strong_brick_color(&g, 0, 0);
    assert(color.r == 255 && color.g == 216 && color.b == 64);
    g.animation_time = 1;
    color = strong_brick_color(&g, 0, 0);
    assert(color.g == 255 && color.b == 255);
    g.strong_hits[0][0] = 2; g.brick_hit_time[0][0] = 0.7f;
    add_top_row(&g);
    assert(g.strong_hits[1][0] == 2 && g.brick_hit_time[1][0] == 0.7f);
}

static void test_metal_distribution(void) {
    const int levels[] = {10, 20, 39, 40, 60, 80, 100, 200};
    const int ratios[] = {4, 4, 4, 3, 2, 1, 0, 0};
    for (int i = 0; i < (int)SDL_arraysize(levels); ++i) {
        Game g = start(123); g.level = levels[i];
        assert(silvers_per_gold(g.level) == ratios[i]);
        int gold = 0, silver = 0;
        for (int block = 0; block < (ratios[i] + 1) * 3; ++block) {
            assign_strong_brick(&g, 0, 0);
            if (g.strong_type[0][0] == GOLD) {
                ++gold; assert(g.strong_hits[0][0] == 8);
            } else { ++silver; assert(g.strong_hits[0][0] == 4); }
        }
        assert(gold == 3 && silver == ratios[i] * 3);
    }
    Game g = start(123); g.animation_time = 0.75f;
    g.brick_hit_time[0][0] = -100; g.strong_type[0][0] = SILVER;
    SDL_Color gray = strong_brick_color(&g, 0, 0);
    assert(gray.r == 128 && gray.g == 128 && gray.b == 128);
    g.brick_hit_time[0][0] = g.animation_time;
    SDL_Color white = strong_brick_color(&g, 0, 0);
    assert(white.r == 255 && white.g == 255 && white.b == 255);
    g.strong_type[0][0] = GOLD; g.strong_hits[0][0] = 2;
    add_top_row(&g);
    assert(g.strong_type[1][0] == GOLD && g.strong_hits[1][0] == 2);
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
    break_paddle(&g, false); render(r, &g);
    g.screen = PLAYING; g.miss_time = 0;
    init_field(&g); add_top_row(&g);
    for (int col = 0; col < COLS; ++col) g.bonus_bricks[0][col] = false;
    render(r, &g);
    assert(SDL_ReadSurfacePixel(surface, 50, 105, &red, &green, &blue, &alpha));
    assert(red == 12 && green == 18 && blue == 32); /* New row starts invisible. */
    g.row_age[0] = 0.25f; render(r, &g);
    assert(SDL_ReadSurfacePixel(surface, 50, 105, &red, &green, &blue, &alpha));
    assert(red > 100 && red < g.row_color[0].r);
    g.row_age[0] = 0.5f; render(r, &g);
    assert(SDL_ReadSurfacePixel(surface, 50, 105, &red, &green, &blue, &alpha));
    assert(red == g.row_color[0].r && green == g.row_color[0].g && blue == g.row_color[0].b);
    if (SDL_getenv("BREAKBOUT_FIELD_CAPTURE")) SDL_SaveBMP(surface, SDL_getenv("BREAKBOUT_FIELD_CAPTURE"));
    g.level = 10; init_field(&g); g.animation_time = 0.75f;
    int strong_row = -1, strong_col = -1;
    for (int row = 0; row < ROWS && strong_row < 0; ++row)
        for (int col = 0; col < COLS; ++col)
            if (g.strong_hits[row][col]) { strong_row = row; strong_col = col; break; }
    assert(strong_row >= 0);
    SDL_FRect strong_rect = brick_rect(&g, strong_row, strong_col);
    int px = (int)(strong_rect.x + strong_rect.w / 2), py = (int)(strong_rect.y + strong_rect.h / 2);
    render(r, &g);
    assert(SDL_ReadSurfacePixel(surface, px, py, &red, &green, &blue, &alpha));
    SDL_Color expected_metal = strong_brick_color(&g, strong_row, strong_col);
    assert(red == expected_metal.r && green == expected_metal.g && blue == expected_metal.b);
    if (SDL_getenv("BREAKBOUT_STRONG_CAPTURE")) SDL_SaveBMP(surface, SDL_getenv("BREAKBOUT_STRONG_CAPTURE"));
    g.animation_time = 1; render(r, &g);
    assert(SDL_ReadSurfacePixel(surface, px, py, &red, &green, &blue, &alpha));
    assert(red == 255 && green == 255 && blue == 255);
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
    test_endless_descent();
    test_block_death();
    test_level_colors();
    test_strong_bricks();
    test_metal_distribution();
    test_render();
    puts("PASS: random bonus placement, three-ball release, impulse/gravity/recoil, multicatch, shared charge, per-ball launch/guide, last-ball loss, trails and prediction");
    return 0;
}
