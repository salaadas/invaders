//
// Things to do tonight:
//
// - Waiting screen before game start.
// - CRT/Scanline/Vignette shader for offscreen buffer.
//

//
// Things to do after the hackathon:
//

// @Bug What is being drawn on the lower left corner of the screen?

//
// @Incomplete: Different music for waiting lounge and then cross-fade to the in-game music...
//  In fact, we would want different music for different modes, our sound mixer does not suppport
//  this yet.
//

// @Incomplete: Show something that says 'Press R to restart!'
// @Fixme: Mipmapping doesn't work for high res 2d texture at small scale...

// @Cleanup: Make the transition stuff less hacky.
// @Cleanup: Clean up the visual counting up.
// @Cleanup: We probably want an entity manager for better performance.

// @Todo: Add auto complete for high score names.
// @Todo Add checksum for save game.

// @Bug: Why is OpenGL crashing on us?

//
// Todo:
// - FORMATION
// - Spline movements for the bugs formation.
//
// - Scaling formation.
//
// - Shooting bullets according to direction while diving down.
//
// - Freezing pickup.
// - Flashbang pickup.
// - Mind control pickup.
//
// - Perturbation with the shots.
//
// - Splash screen.
// - Bonus rounds.
//
// - Add a z offset for the invaders, so that we can be independent of the array ordering when drawing.
//

#include "invaders.h"
#include "main.h"
#include "opengl.h"
#include "hud.h"
#include "time_info.h"
#include "events.h"
#include "mixer.h"
#include "text_input.h"
#include "file_utils.h"
#include "string_builder.h"
#include <sys/stat.h> // For mkdir.

constexpr auto CHEAT_MODE = true;

constexpr auto SHIP_RADIUS    = .053f;
constexpr auto BULLET_RADIUS  = .023f;
constexpr auto PICKUP_RADIUS  = .018f;
constexpr auto INVADER_RADIUS_AT_SCALE_1 = .041f;

constexpr auto LIVE_MIN_Y = -.2f;
constexpr auto LIVE_MAX_Y = 1.2f;

constexpr auto SHOT_COOLDOWN_BASE        = .08f;
constexpr auto SINGLE_SHOT_COOLDOWN_TIME = .15f;

constexpr auto SHIP_SHIELD_TIME    = 8.0f;
constexpr auto RESPAWN_SHIELD_TIME = 1.2f;
constexpr auto SHIP_PIERCING_TIME  = 8.0f;

// Each pickup of the V-shot expands the angle range, and that angle
// stays for some amount of time before shrinking back to normal straight angle.
constexpr auto SHIP_V_SHOT_STABILITY_TIME            = 10.0f;
constexpr auto SHIP_V_SHOT_DEGREES_PER_PICKUP        = 12.0f;
constexpr auto SHIP_V_SHOT_DEGREES_MAX               = 120.0f;
constexpr auto SHIP_V_SHOT_SHRINK_DEGREES_PER_SECOND = 2.0f;
constexpr auto SHIP_V_SHOT_EXPAND_DEGREES_PER_SECOND = 20.0f;

f32 ship_v_shot_stability_cooldown;
f32 ship_v_shot_target_angle;
f32 ship_v_shot_current_angle;

constexpr auto NUM_LIVES = 3;
i32 lives;

Texture_Map *ship_map;
Texture_Map *ship_bullet_map;
Texture_Map *contrail_map;
Texture_Map *invader_bullet_map;
Texture_Map *pickup_extra_bullets_map;
Texture_Map *pickup_v_shot_map;
Texture_Map *pickup_shield_map;
Texture_Map *pickup_piercing_map;

Texture_Map *dead_cross_map;
Texture_Map *notebook_background;

Music game_music;
Music general_music;

Sound sound_ship_shot;
Sound sound_pickup;
Sound sound_invader_die;
Sound sound_ship_die;
Sound sound_ship_respawn;
Sound sound_new_stage;
Sound sound_transition;

bool ship_destroyed = false;
f32 ship_theta;

f32 ship_shot_cooldown;
f32 ship_shield_cooldown;
f32 ship_piercing_cooldown;

f32 respawn_cooldown = -1.0f;

Shot_Type ship_shot_type = Shot_Type::STRAIGHT_SINGLE;

Vector2 bullet_size;
Vector4 bullet_color;

Vector2 ship_position;
Vector2 ship_size;
Vector4 ship_color;

Vector2 invader_size;
Vector4 invader_color;

Vector2 formation_offset;

Shader *shader_argb_no_texture;
Shader *shader_argb_and_texture;
Shader *shader_text;
Shader *shader_meltdown;

Dynamic_Font *small_font;
Dynamic_Font *big_font;

Vector2 pickup_size;
Vector4 pickup_color;

RArr<Bullet*>           live_bullets;
RArr<Invader*>          live_invaders;
RArr<Particle_Emitter*> live_emitters;
RArr<Pickup*>           live_pickups;

RArr<Texture_Map*> invader_maps;

i64 score;
i32 level_index;
i64 shot_index;
i64 total_pickups_gotten;

RArr<Layout_Line> layout_lines;

constexpr auto MODE_TRANSITION_DURATION_BASE = 1.35f;
Transition_Mode transition_mode = Transition_Mode::NOTHING;
f32 transition_duration;
f32 transition_elapsed = -1;
Game_Mode transition_into_mode; // Only used if  transition_mode == ::CHANGE_MODE
RArr<Particle> transition_dots;

f32 current_invader_radius;

f32 time_of_level_start;
i32 dudes_spawned_this_level;
f32 current_aggro;
f32 strafe_cooldown;

Texture_Map previous_stage_texture;
Bitmap previous_stage_bitmap;

constexpr auto MAX_LEADER_BOARD_ENTRIES = 10;
RArr<High_Score_Record> score_leader_board;

Game_Mode game_mode;

#define cat_find(catalog, name) catalog_find(&catalog, String(name));
void init_shaders()
{
    shader_argb_no_texture   = cat_find(shader_catalog, "argb_no_texture");  assert(shader_argb_no_texture);
    shader_argb_and_texture  = cat_find(shader_catalog, "argb_and_texture"); assert(shader_argb_and_texture);
    shader_text              = cat_find(shader_catalog, "text");             assert(shader_text);
    shader_meltdown          = cat_find(shader_catalog, "meltdown");         assert(shader_meltdown);

    shader_argb_no_texture->backface_cull  = false;
    shader_argb_and_texture->backface_cull = false;
    shader_meltdown->backface_cull = false;
}
#undef cat_find

void rendering_2d_right_handed()
{
    f32 w = render_target_width;
    f32 h = render_target_height;
    if (h < 1) h = 1;

    // This is a GL-style projection matrix mapping to [-1, 1] for x and y
    Matrix4 tm = Matrix4(1.0);
    tm[0][0] = 2 / w;
    tm[1][1] = 2 / h;
    tm[3][0] = -1;
    tm[3][1] = -1;

    view_to_proj_matrix    = tm;
    world_to_view_matrix   = Matrix4(1.0);
    object_to_world_matrix = Matrix4(1.0);

    refresh_transform();
}

void rendering_2d_right_handed_unit_scale()
{
    // @Note: cutnpaste from rendering_2d_right_handed
    f32 h = render_target_height / (f32)render_target_width;

    // This is a GL-style projection matrix mapping to [-1, 1] for x and y
    auto tm = Matrix4(1.0);
    tm[0][0] = 2;
    tm[1][1] = 2 / h;
    tm[3][0] = -1;
    tm[3][1] = -1;

    view_to_proj_matrix    = tm;
    world_to_view_matrix   = Matrix4(1.0);
    object_to_world_matrix = Matrix4(1.0);

    refresh_transform();
}

void draw_generated_quads(Dynamic_Font *font, Vector4 color)
{
    rendering_2d_right_handed();
    set_shader(shader_text);

    // glBlendFunc(GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 1.0); // @Investigate: What is anisotropy have to do with font rendering?
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    GLuint last_texture_id = 0xffffffff;

    immediate_begin();

    for (auto quad : font->current_quads)
    {
        auto page = quad.glyph->page;
        auto map  = &page->texture;

        if (page->dirty)
        {
            page->dirty = false;
            auto bitmap = page->bitmap_data;

            // Regenerating the texture. Or should we not?
            {
                // if (map->id == 0xffffffff || !map->id)
                {
                    // printf("Generating a texture for font page\n");
                    glGenTextures(1, &map->id);
                    glBindTexture(GL_TEXTURE_2D, map->id);
                }

                map->format = bitmap->XXX_format;
                update_texture_from_bitmap(map, bitmap);
            }
        }

        if (map->id != last_texture_id)
        {
            // @Speed
            // This will cause a flush for every call to draw_text.
            // But if we don't do this then we won't set the texture.
            // Need to refactor the text rendering code so that we don't have to deal with this
            immediate_flush();
            last_texture_id = map->id;
            set_texture(String("diffuse_texture"), map);
        }

        immediate_letter_quad(quad, color);
        // immediate_flush();
    }

    immediate_flush();
}

void draw_prepared_text(Dynamic_Font *font, i64 x, i64 y, Vector4 color)
{
    generate_quads_for_prepared_text(font, x, y);
    draw_generated_quads(font, color);
}

i64 draw_text(Dynamic_Font *font, i64 x, i64 y, String text, Vector4 color)
{
    auto width = prepare_text(font, text);
    draw_prepared_text(font, x, y, color);

    return width;
}

void round(Vector2 *p)
{
    p->x = floorf(p->x + .5f);
    p->y = floorf(p->y + .5f);
}

void immediate_image(Vector2 position, Vector2 size, Vector4 color, f32 theta)
{
    assert(current_shader == shader_argb_and_texture);

    auto h = Vector2(size.x*.5f * render_target_height, 0);
    auto v = Vector2(0, size.y*.5f * render_target_height);

    auto radians = theta * (TAU / 360.0f);

    h = rotate(h, radians);
    v = rotate(v, radians);

    auto pos = position * Vector2(render_target_width, render_target_height);

    auto p0 = pos - h - v;
    auto p1 = pos + h - v;
    auto p2 = pos + h + v;
    auto p3 = pos - h + v;

    round(&p0);
    round(&p1);
    round(&p2);
    round(&p3);

    immediate_quad(p0, p1, p2, p3, argb_color(color));
}

inline
void draw_image_centered_at(Texture_Map *map, Vector2 position, Vector2 size, Vector4 color, f32 theta = 0)
{
    rendering_2d_right_handed();

    set_shader(shader_argb_and_texture);
    set_texture(String("diffuse_texture"), map);

    immediate_image(position, size, color, theta);
}

bool ship_has_shield()
{
    return ship_shield_cooldown > 0;
}

void draw_ship_at(Vector2 position, Vector2 size, f32 theta, Texture_Map *map)
{
    auto color = ship_color;

    if (ship_has_shield())
    {
        auto rate = 1.2f;
        auto theta = timez.current_time * TAU * rate;
        auto y = cosf(static_cast<f32>(theta)) * cosf(static_cast<f32>(theta));

        auto k = (y + 1.0f) * 0.5f;
        Clamp(&k, 0.0f, 1.0f);

        k *= .8f;

        color.x = k;
        color.y = k;
        color.z = 1;
    }

    draw_image_centered_at(map, position, size, color, theta);
}
void draw_emitter(Particle_Emitter *emitter)
{
    rendering_2d_right_handed();
    set_shader(shader_argb_and_texture);
    set_texture(String("diffuse_texture"), contrail_map);

    for (auto &it : emitter->particles)
    {
        auto color_t = get_random_within_range(0.0f, 1.0f);
        auto color = lerp(emitter->color0, emitter->color1, color_t);

        auto theta = 0.0f;
        auto size  = Vector2(it.size);

        immediate_image(it.position, size, color, theta);
    }
}

void set_shield(f32 duration)
{
    // @Incomplete: Not playing sound here...
    ship_shield_cooldown = duration;
}

void cooldown(f32 *value_pointer, f32 dt)
{
    auto value = *value_pointer;

    value -= dt;
    if (value < 0) value = 0;

    *value_pointer = value;
}

f32 get_t_factor(f32 elapsed, f32 duration)
{
    auto denom = static_cast<f32>(duration);
    if (denom < .00001f) denom = 1.0f;

    auto t = elapsed / denom;
    Clamp(&t, 0.0f, 1.0f);

    return t;
}

void reset_transition_mode()
{
    transition_elapsed = -1;
    transition_mode = Transition_Mode::NOTHING;
}

bool accumulate_transition()
{
    auto done = false;

    auto dt = timez.current_dt;
    transition_elapsed += dt;
    if (transition_elapsed > transition_duration)
    {
        reset_transition_mode();
        done = true;
    }

    return done;
}

void do_stage_transition()
{
    copy_current_framebuffer_into_bitmap(&previous_stage_bitmap);

    previous_stage_texture.num_mipmap_levels = 1;
    previous_stage_texture.format = previous_stage_bitmap.XXX_format;

    update_texture_from_bitmap(&previous_stage_texture, &previous_stage_bitmap);

    transition_mode = Transition_Mode::NEXT_STAGE;
    constexpr auto STAGE_TRANSITION_DURATION = .7f;
    transition_duration = STAGE_TRANSITION_DURATION;
    transition_elapsed = 0;
}

void do_change_mode_transition(Game_Mode mode, f32 duration)
{
    assert(transition_dots.count == 0);

    transition_mode = Transition_Mode::CHANGE_GAME_MODE;
    transition_into_mode = mode;

    transition_duration = duration;
    transition_elapsed = 0;

    play_sound(&sound_transition);
}

// These are the visual numbers that are used to do the counting up.
// They are not meant to be used outside of draw_score_overview_screen().
i64 visual_score = 0;
i32 visual_level_index = -1;
i64 visual_total_pickups_gotten = 0;

f32 visual_count_up_elapsed = 0;
f32 visual_duration_per_count_up = -1;
i64 visual_steps = 0;
i32 visual_step_multiplier = 1;
constexpr auto VISUAL_DURATION_COUNT_UP_BASE = 1.0f;

bool counting_up_completed = false;

constexpr auto PROMPT_POP_UP_DURATION = .7f; // @Fixme: This is more like a DESIRED duration, because for very large range, the step is so small that a 'dt' can step through multiple steps...
f32 prompt_pop_up_elapsed = 0;

// @Fixme: This isn't doing what I wanted right now, which is to give a duration of the entire count up
// and it would start counting up from there.
// Maybe we should think about specify the duration for each step instead....
// But now it looks kind of fine, so Hah!
template <typename T>
void visual_counting_up(T *current, T target, f32 dt, T step = 1)
{
    static_assert((std::is_arithmetic<T>::value || std::is_enum<T>::value), "T must be arithmetic type or enum");
    assert(step > 0);

    if (*current >= target) return;

    auto duration = VISUAL_DURATION_COUNT_UP_BASE;

    if (visual_duration_per_count_up < 0)
    {
        f32 delta = target - *current;

        assert(delta > 0);
        auto pow_multiplier = static_cast<i64>(log10f(delta)) - 2;
        if (pow_multiplier < 0) pow_multiplier = 0;

        visual_step_multiplier = powf(10, pow_multiplier);

        auto num_steps = static_cast<i64>(delta / (step*visual_step_multiplier));
        visual_duration_per_count_up = duration / num_steps;
    }

    auto elapsed_this_step = visual_count_up_elapsed - visual_steps * visual_duration_per_count_up;
    if (elapsed_this_step >= visual_duration_per_count_up)
    {
        *current += step * visual_step_multiplier;
        visual_steps += 1;

        if (*current >= target)
        {
            *current = target;

            visual_count_up_elapsed = 0;
            visual_duration_per_count_up = -1;
            visual_steps = 0;
            return;
        }
    }

    visual_count_up_elapsed += dt;
}

f32 /*new y*/ do_one_readout(Dynamic_Font *font, String text, f32 start_y, f32 start_x = 0, f32 *alpha_scale = NULL)
{
    f32 x;

    auto width = prepare_text(font, text);
    if (start_x == 0)
    {
        // Align center if not supplied a starting x.
        x = render_target_width*.5f - width*.5f;
    }
    else if (start_x > 0)
    {
        // Align left if start_x is positive.
        x = start_x;
    }
    else
    {
        // Align right if start_x is negative.
        x = render_target_width - width + start_x;
    }

    auto y = start_y;

    auto ox = font->character_height * .05f;
    auto oy = -ox;

    auto backing_color    = Vector4(.9f, .23f, .8f, .6f);
    auto foreground_color = Vector4(.1, .1, .1, 1);

    if (alpha_scale)
    {
        backing_color.w    *= *alpha_scale;
        foreground_color.w *= *alpha_scale;
    }

    draw_prepared_text(font, x + ox, y + oy, backing_color);
    draw_prepared_text(font, x,      y,      foreground_color);

    start_y -= font->default_line_spacing;
    return start_y;
}

bool should_ask_for_score_name()
{
    for (i64 i = 0; i < score_leader_board.count; ++i)
    {
        auto other = &score_leader_board[i];

        if (score <= other->score) continue;

        return true;
    }

    return false;
}

void draw_name_prompt()
{
    auto dt = timez.current_dt;
    auto font = small_font;

    auto margin = render_target_height * .01f;

    auto font_h = font->character_height * 1.1f;
    auto text_h = font_h * .5f;

    auto desired_y = render_target_height*.55f - text_h;
    auto factor = get_t_factor(prompt_pop_up_elapsed, PROMPT_POP_UP_DURATION);

    if (prompt_pop_up_elapsed != PROMPT_POP_UP_DURATION) prompt_pop_up_elapsed += dt;

    auto yy = lerp(LIVE_MIN_Y, desired_y, factor);

    yy = do_one_readout(font, String("Your name, new champ?"), yy, margin);
    yy -= margin * 1.8f;

    auto w = static_cast<f32>(render_target_width) - 2*margin;

    Rect r = {.x = margin, .y = yy, .w = w, .h = font_h};

    draw_high_score_name_input(r, font);
    immediate_flush();
}

void draw_press_any_key()
{
    auto dt = timez.current_dt;

    if (prompt_pop_up_elapsed < PROMPT_POP_UP_DURATION)
    {
        // Wait a while before showing that you can press any key.
        prompt_pop_up_elapsed += .8f * dt;
        return;
    }

    auto font = small_font;
    auto h = font->character_height * 1.1f * .5f;
    auto yy = render_target_height*.55f - h;

    auto alpha_scale = cosf(timez.current_time);
    alpha_scale *= alpha_scale;

    do_one_readout(font, String("Press any key to view leaderboard"), yy, 0, &alpha_scale);
    immediate_flush();
}

void draw_overall_background()
{
    rendering_2d_right_handed();

    auto w = render_target_width;
    auto h = render_target_height;

    auto p0 = Vector2(0, 0);
    auto p1 = Vector2(w, 0);
    auto p2 = Vector2(w, h);
    auto p3 = Vector2(0, h);

    set_shader(shader_argb_and_texture);
    set_texture(String("diffuse_texture"), notebook_background);

    immediate_quad(p0, p1, p2, p3, 0xffffffff);
    immediate_flush();
}

void draw_score_overview_screen()
{
    draw_overall_background();

    {
        auto dt = timez.current_dt;

        auto yy = .8f * render_target_height;
        yy = do_one_readout(small_font, String("Overview:"), yy);

        auto xp = render_target_height * .031f;

        if (visual_total_pickups_gotten != total_pickups_gotten)
        {
            visual_counting_up(&visual_total_pickups_gotten, total_pickups_gotten, dt);
        }

        do_one_readout(small_font, String("Pickups gotten:"), yy, xp);
        yy = do_one_readout(small_font, tprint(String("%ld"), visual_total_pickups_gotten), yy, -xp);

        if (visual_total_pickups_gotten == total_pickups_gotten)
        {
            if (visual_level_index != level_index)
            {
                visual_counting_up(&visual_level_index, level_index, dt);
            }
        }

        do_one_readout(small_font, String("Stages:"), yy, xp);
        yy = do_one_readout(small_font, tprint(String("%ld"), visual_level_index+1), yy, -xp);

        if (visual_level_index == level_index)
        {
            visual_counting_up(&visual_score, score, dt);
        }

        if (visual_score == score) counting_up_completed = true;

        do_one_readout(small_font, String("Total score:"), yy, xp);
        yy = do_one_readout(small_font, tprint(String("%ld"), visual_score), yy, -xp);
    }

    //
    // Prompting for name input.
    //
    if (counting_up_completed)
    {
        if (should_ask_for_score_name()) draw_name_prompt();
        else draw_press_any_key();                              
    }
}

void draw_leader_board_screen()
{
    draw_overall_background();

    auto yy = .8f * render_target_height;
    yy = do_one_readout(big_font, String("Hall of fame:"), yy);

    auto xp = render_target_height * .041f;
    i64 index = 1;
    for (auto &it : score_leader_board)
    {
        auto name = it.name;

        // If your name is too long, we do a '...' substitute for the later part.
        constexpr auto NAME_DISPLAY_MAX_LENGTH = 12;
        if (name.count > NAME_DISPLAY_MAX_LENGTH)
        {
            name.count = NAME_DISPLAY_MAX_LENGTH;
            name = join(2, name, String("..."));
        }

        do_one_readout(small_font, join(2, tprint(String("%2ld. "), index), name), yy, xp);
        yy = do_one_readout(small_font, tprint(String("%ld"), it.score), yy, -xp);

        index += 1;
    }
}

void draw_in_game_screen()
{
    draw_overall_background();
    draw_ship_at(ship_position, ship_size, ship_theta, ship_map);

    for (auto it : live_emitters) draw_emitter(it);

    invader_size = Vector2(current_invader_radius*2, current_invader_radius*2);
    for (auto it : live_invaders) draw_image_centered_at(it->map, it->position, invader_size, invader_color, it->theta);

    for (auto it : live_pickups) draw_image_centered_at(it->map, it->position, pickup_size, pickup_color);
    for (auto it : live_bullets) draw_image_centered_at(it->map, it->position, bullet_size, bullet_color);

    // Draw the lives.
    {
        auto size   = ship_size * .7f;
        auto margin = size.x * .2f;

        auto position = Vector2(SHIP_RADIUS);
        position.x += margin;
        position.y *= .75f;

        for (auto i = 0; i < NUM_LIVES; ++i)
        {
            auto color = Vector4(1, 1, 1, 1);

            if (i >= lives)
            {
                auto k = .4f;
                color.x = color.y = color.z = k;

                draw_image_centered_at(ship_map, position, size, color);
                draw_image_centered_at(dead_cross_map, position, size, Vector4(1, 1, 1, 1));
            }
            else
            {
                draw_image_centered_at(ship_map, position, size, color);
            }


            position.x += margin + size.x;
        }
    }

    immediate_flush();

    {
        rendering_2d_right_handed();

        auto fh = small_font->character_height;

        auto ox = fh * .05f;
        auto oy = -ox;

        auto mx = fh * .8f;

        auto text = tprint(String("Score: %d"), score);

        auto backing_color = Vector4(.9f, .23f, .8f, .6f);

        auto strobe_t = cosf(timez.current_time);
        strobe_t *= strobe_t;

        backing_color = lerp(backing_color, Vector4(1, 0, 0, .6f), strobe_t);

        draw_text(small_font, mx + ox, render_target_height - fh + oy, text, backing_color);

        auto k = 0.18f;
        auto foreground_color = Vector4(k*k, k*k, k, 1);
        draw_text(small_font, mx, render_target_height - fh, text, foreground_color);
    }

    if (transition_mode == Transition_Mode::NEXT_STAGE)
    {
        // Draw the transition meltdown.
        rendering_2d_right_handed();
        set_shader(shader_meltdown);

        set_texture(String("diffuse_texture"), &previous_stage_texture);

        auto meltdown_t = get_t_factor(transition_elapsed, transition_duration);

        auto loc = glGetUniformLocation(current_shader->program, "meltdown_t");
        glUniform1f(loc, meltdown_t);

        loc = glGetUniformLocation(current_shader->program, "texture_width");
        glUniform1f(loc, previous_stage_texture.width);

        loc = glGetUniformLocation(current_shader->program, "game_time");
        glUniform1f(loc, timez.current_time);

        accumulate_transition();

        auto w = render_target_width;
        auto h = render_target_height;

        immediate_begin();
        immediate_quad(Vector2(0, 0), Vector2(w, 0), Vector2(w, h), Vector2(0, h), 0xffeeffbb);
        immediate_flush();
    }
}

void spawn_change_mode_transition_dots()
{
    auto transition_t = get_t_factor(transition_elapsed, transition_duration);

    auto size_each = .2f;
    auto num_circles_per_line = static_cast<i64>(1.0f / size_each) * 1;

    //
    // Spawn the dot particles for this line.
    //
    for (i64 i = 0; i < num_circles_per_line; ++i)
    {
        auto x = get_random_within_range(0, 1);
        auto y_margin = .001f;
        auto y = get_random_within_range(transition_t - y_margin, transition_t + y_margin);

        auto particle = array_add(&transition_dots);
        particle->position = Vector2(x, y);
        particle->velocity = Vector2(0, 0);
        particle->size     = size_each;
        particle->drag     = 1.0f;

        auto lifetime = transition_t; // The later the dot is spawned, the later it decays.
        assert(lifetime >= 0);
        particle->lifetime = lifetime;
    }

    auto done = accumulate_transition();        
    if (done)
    {
        game_mode = transition_into_mode;

        // @Hack: We are sort of using the elapsed thing here both for the fade in and fade out so we need to
        // reset it before the fade out/decay part happens.
        //
        // This is because I don't really like the idea of spawning the particle twice since the randomization
        // will cause the positions to be different, but really do we care?
        for (auto &dot : transition_dots) dot.elapsed = 0;
    }
}

void draw_waiting_lounge_screen()
{
    //
    // @Incomplete: Probably want to do some intro of the sample program
    // being played like in the arcade...
    //

    draw_overall_background();

    auto yy = render_target_height * .6f;
    yy = do_one_readout(big_font, String("Press any key"), yy);
    yy = do_one_readout(big_font, String("to start game!"), yy);
}

void draw_game()
{
    if (!small_font || !big_font || was_window_resized_this_frame)
    {
        small_font = get_font_at_size(FONT_FOLDER, String("KarminaBold.otf"), BIG_FONT_SIZE * .5f);
        big_font   = get_font_at_size(FONT_FOLDER, String("KarminaBold.otf"), BIG_FONT_SIZE * .9f);
    }

    //
    // Render to the offscreen buffer.
    //
    set_render_target(0, the_offscreen_buffer, NULL);

    glClearColor(.13, .20, .23, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    // Draw the background.
    switch (game_mode)
    {
        case Game_Mode::IN_GAME:        draw_in_game_screen(); break;
        case Game_Mode::SCORE_OVERVIEW: draw_score_overview_screen(); break;
        case Game_Mode::LEADER_BOARD:   draw_leader_board_screen(); break;
        case Game_Mode::WAITING_LOUNGE: draw_waiting_lounge_screen(); break;
        default: assert(0);
    }

    if (transition_mode == Transition_Mode::CHANGE_GAME_MODE)
    {
        spawn_change_mode_transition_dots();
    }

    if (transition_dots.count)
    {
        //
        // Draw the dots (depending on the transition mode and game mode, this
        // will either fade in or fade out the particles.
        //

        auto color = Vector4(0, 0, 0, 1);
        rendering_2d_right_handed();

        immediate_begin();
        set_shader(shader_argb_and_texture);
        set_texture(String("diffuse_texture"), contrail_map);

        for (auto &dot : transition_dots)
        {
            auto pos  = dot.position;
            auto size = Vector2(dot.size);

            // @Hack: I don't think it is great to use the same spawn both for fading in and out particles....
            if (transition_mode == Transition_Mode::NOTHING)
            {
                auto alpha_t = dot.elapsed / dot.lifetime;
                Clamp(&alpha_t, 0.0f, 1.0f);

                color.w = lerp(1.0f, 0.3f, alpha_t);
            }
            else
            {
                auto alpha_t = dot.elapsed / dot.lifetime;
                Clamp(&alpha_t, 0.0f, 1.0f);

                color.w = lerp(.3f, 1.f, alpha_t);

                constexpr auto TRANSITION_FADEIN_PERIOD = .1f;
                dot.elapsed += TRANSITION_FADEIN_PERIOD * timez.current_dt;
            }

            immediate_image(pos, size, color, 0);
        }

        immediate_flush();
    }

    //
    // Render to the back buffer.
    // 
    set_render_target(0, the_back_buffer);
    rendering_2d_right_handed();

    set_shader(shader_argb_and_texture);

    f32 back_buffer_width  = the_back_buffer->width;
    f32 back_buffer_height = the_back_buffer->height;

    f32 w  = (f32)(the_offscreen_buffer->width);
    f32 h  = (f32)(the_offscreen_buffer->height);
    f32 bx = floorf(.5f * (back_buffer_width - the_offscreen_buffer->width));
    f32 by = floorf(.5f * (back_buffer_height - the_offscreen_buffer->height));

    auto p0 = Vector2(bx,     by);
    auto p1 = Vector2(bx + w, by);
    auto p2 = Vector2(bx + w, by + h);
    auto p3 = Vector2(bx,     by + h);

    set_texture(String("diffuse_texture"), the_offscreen_buffer);
    immediate_begin();
    immediate_quad(p0, p1, p2, p3, 0xffffffff);
    immediate_flush();

    draw_hud();
}

u32 key_left;
u32 key_right;
u32 key_up;
u32 key_down;
u32 key_action;

void do_fire_bullets()
{
    play_sound(&sound_ship_shot);

    auto fire_bullet = [](f32 speed, f32 angle) -> Bullet* {
        auto bullet = New<Bullet>();

        bullet->position = ship_position;
        bullet->position.y += .6f * SHIP_RADIUS; // Make it look like it shoots at the tip.

        bullet->map = ship_bullet_map;

        bullet->velocity = speed * Vector2(sinf(angle), cosf(angle));

        bullet->can_pierce = ship_piercing_cooldown > 0;

        {
            auto emitter = New<Particle_Emitter>();
            bullet->emitter = emitter;

            emitter->theta0 = TAU * .8f;
            emitter->theta1 = TAU * .9f;
            emitter->drag0  = .9f;
            emitter->drag1  = .97f;

            if (bullet->can_pierce)
            {
                emitter->color0 = Vector4(.2f, .7f, .92f, 1);
                emitter->color1 = Vector4(.1f, .3f, .69f, 1);
            }
            else
            {
                auto k0 = 1.0f;
                auto k1 = .50f;

                emitter->color0 = Vector4(k0, k0*.3f, k0*.3f, 1);
                emitter->color1 = Vector4(k1, k1*.3f, k1*.3f, 1);
            }

            array_add(&live_emitters, emitter);
        }

        array_add(&live_bullets, bullet);

        return bullet;
    };

    auto num_shots_fired = 0;
    shot_index += 1;

    constexpr auto SPEED = 1.2f;

    auto radians = ship_v_shot_current_angle * (TAU / 360.0f);
    auto v_shot_angle = radians * .5f;

    if ((ship_shot_type == Shot_Type::STRAIGHT_SINGLE) || (ship_shot_type == Shot_Type::STRAIGHT_TRIPLE))
    {
        f32 angle = 0;

        // Shooting zigzagoon if we are doing single shots.
        if (ship_shot_type == Shot_Type::STRAIGHT_SINGLE)
        {
            if (shot_index % 2) angle =  v_shot_angle;
            else                angle = -v_shot_angle;
        }

        fire_bullet(SPEED, angle);

        num_shots_fired += 1;
    }

    if ((ship_shot_type == Shot_Type::STRAIGHT_DOUBLE) || (ship_shot_type == Shot_Type::STRAIGHT_TRIPLE))
    {
        auto l_angle = -v_shot_angle;
        auto left    = fire_bullet(SPEED, l_angle);

        auto r_angle = v_shot_angle;
        auto right   = fire_bullet(SPEED, r_angle);

        constexpr auto OX = .03f;

        left->position.x  -= OX;
        right->position.x += OX;

        num_shots_fired += 2;
    }

    ship_shot_cooldown = SHOT_COOLDOWN_BASE + num_shots_fired * SINGLE_SHOT_COOLDOWN_TIME;
}

void maybe_fire_bullets()
{
    if (ship_shot_cooldown > 0) return;
    if (ship_destroyed)         return;

    do_fire_bullets();
}

void destroy_ship()
{
    if constexpr (CHEAT_MODE) return;

    play_sound(&sound_ship_die);

    ship_destroyed = true;

    respawn_cooldown = 1.5f;

    // Particles
    {
        auto em = New<Particle_Emitter>();
        array_add(&live_emitters, em);

        em->size0 = .12f;
        em->size1 = .19f;

        em->speed0 = 2.f;
        em->speed1 = 5.f;

        em->color0 = Vector4(1, .3, 1, 1);
        em->color1 = Vector4(1, 1, 1, 1);

        em->fadeout_period   = .3f;
        em->emitter_lifetime = .6f;

        em->position = ship_position;
        em->velocity = rotate(Vector2(1, 0), get_random_within_range(.15f, TAU * .9f));
    }
}

auto shoot_button_down = false; // @Cleanup: This might be problematic if we switch game mode and it is not reset to false.
void handle_event_for_game(Event event)
{
    if (event.type != EVENT_KEYBOARD) return;

    auto key     = event.key_code;
    auto pressed = event.key_pressed;

    switch (key)
    {
        case Key_Code::CODE_ARROW_LEFT:  key_left  = pressed; break;
        case Key_Code::CODE_ARROW_RIGHT: key_right = pressed; break;
        case Key_Code::CODE_ARROW_DOWN:  key_down  = pressed; break;
        case Key_Code::CODE_ARROW_UP:    key_up    = pressed; break;
        case Key_Code::CODE_SPACEBAR:    shoot_button_down = static_cast<bool>(pressed); break;

        case Key_Code::CODE_K: {
            if constexpr (CHEAT_MODE)
            {
                // Force kill.
                ship_destroyed   = true;
                respawn_cooldown = 0;
                lives = -1;
            }
        } break;
    }
}

void add_high_score_record(String _name, i64 score)
{
    auto name = copy_string(_name);

    High_Score_Record record = {.name = name, .score = score};

    // Go through the leader board and add the new record to the right index
    // if we have a score greater than at least one of the record inside the
    // leader board.
    for (i64 i = 0; i < score_leader_board.count; ++i)
    {
        auto other = &score_leader_board[i];

        if (score <= other->score) continue;

        array_add(&score_leader_board);

        // Move everything from index 'i' to the right by 1 index.
        {
            auto dest   = &score_leader_board.data[i + 1];
            auto src    = other;
            // Subtract 1 from count because we just added a record above.
            auto nbytes = (score_leader_board.count-1 - i) * sizeof(High_Score_Record);

            memmove(dest, src, nbytes);
        }

        score_leader_board.data[i] = record;

        // Free the excess (at most one at a time).
        if (score_leader_board.count > MAX_LEADER_BOARD_ENTRIES)
        {
            auto excess_count = score_leader_board.count - MAX_LEADER_BOARD_ENTRIES;
            assert(excess_count == 1);

            auto excess = pop(&score_leader_board);
            free_string(&excess.name);
        }

        return;
    }

    // We get here if our score is lower than all of the records.
    if (score_leader_board.count < MAX_LEADER_BOARD_ENTRIES)
    {
        array_add(&score_leader_board, record);
    }
}

template <typename T>
void put(String_Builder *builder, T x)
{
    static_assert((std::is_arithmetic<T>::value || std::is_enum<T>::value), "T must be arithmetic type or enum");

    auto size = sizeof(T);
    ensure_contiguous_space(builder, size);

    // if (TARGET_IS_LITTLE_ENDIAN)

    auto current_buffer = builder->current_buffer;
    memcpy(current_buffer->data.data + current_buffer->occupied, &x, size);
    current_buffer->occupied += size;
}

void put(String_Builder *builder, String s)
{
    if (s.data == NULL)
    {
        assert((s.count == 0));
    }

    put(builder, s.count);
    append(builder, s.data, s.count);
}

template <typename T>
void get(String *s, T *x)
{
    static_assert((std::is_arithmetic<T>::value || std::is_enum<T>::value), "T must be arithmetic type or enum");

    auto size = sizeof(T);
    assert((s->count >= size));

    memcpy(x, s->data, size);

    s->data  += size;
    s->count -= size;
}

bool consume_u8_and_length(String *src, u8 *dest, i64 count)
{
    if (count < 0) return false;
    if (count > src->count) return false;
    if (count == 0) return true;

    memcpy(dest, src->data, count);

    src->data  += count;
    src->count -= count;

    return true;
}

void get(String *src, String *dest)
{
    i64 count;
    get(src, &count);

    assert((count >= 0));

    dest->count = count;
    dest->data  = reinterpret_cast<u8*>(my_alloc(count));

    if (!dest->data) return; // @Incomplete: Log error
    auto success = consume_u8_and_length(src, dest->data, count);

    if (!success)
    {
        printf("NOT ENOUGH ROOM LEFT IN STRING!\n");
        assert(0);
    }
}

String save_path;
String get_save_path()
{
    if (save_path) return save_path;

    String path;

    auto root_dir = getenv("XDG_DATA_HOME");
    if (!root_dir)
    {
        root_dir = getenv("HOME");

        if (!root_dir) path = copy_string(String("data/save.dat"));
        else           path = sprint(String("%s/.local/share/Invaders/save.dat"), root_dir);
    }
    else
    {
        path = sprint(String("%s/Invaders/save.dat"), root_dir);
    }

    // Make all directories that lead up to this.
    for (i64 i = 0; i < path.count; ++i)
    {
        if (path[i] != '/') continue;

        path.data[i] = '\0'; // @Hack: This is bad, we are stomping things just to pass to mkdir.
        defer {path.data[i] = '/';};

        mkdir(reinterpret_cast<char*>(path.data), S_IRWXU);
    }

    save_path = path;
    return save_path;
}

bool write_entire_file(String path, String_Builder builder)
{
    auto c_path = reinterpret_cast<char*>(temp_c_string(path));

    auto file_descriptor = fopen(c_path, "wb");
    if (!file_descriptor) return false;

    auto s = builder_to_string(&builder);
    defer {free_string(&s);};
    
    auto written = fwrite(s.data, s.count, sizeof(u8), file_descriptor);
    assert((written == 1));

    fclose(file_descriptor);

    return true;
}

void save_high_score_leader_board()
{
    auto path = get_save_path();

    String_Builder builder;

    put(&builder, score_leader_board.count);
    for (auto &it : score_leader_board)
    {
        put(&builder, it.name);
        put(&builder, it.score);
    }

    auto success = write_entire_file(path, builder);
    if (!success) logprint("save_high_score_leader_board", "Failed to save high score at path '%s'!\n", temp_c_string(path));
}

void release_leader_board(RArr<High_Score_Record> *leader_board_ptr)
{
    auto leader_board = *leader_board_ptr;
    for (auto &it : leader_board)
    {
        free_string(&it.name);
    }
        
    array_reset(leader_board_ptr);
}

void load_high_score_leader_board()
{
    if (score_leader_board.count) release_leader_board(&score_leader_board);

    auto path = get_save_path();

    auto [data, success] = read_entire_file(path);
    if (!success) return;

    auto orig_data = data;
    defer {free_string(&orig_data);};

    i64 num_records;
    get(&data, &num_records);
    assert(num_records <= MAX_LEADER_BOARD_ENTRIES);

    array_resize(&score_leader_board, num_records);
    for (i64 i = 0; i < num_records; ++i)
    {
        auto record = &score_leader_board[i];

        get(&data, &record->name);
        get(&data, &record->score);
    }
}

void init_ship_position()
{
    ship_theta = 0;

    ship_position = Vector2(.5f, .15f);
    set_shield(RESPAWN_SHIELD_TIME);
}

void release(Layout_Line *line)
{
    array_free(&line->invader_slots);
}

void restart_game(bool skip_wait_lounge = true)
{
    if (skip_wait_lounge) game_mode = Game_Mode::IN_GAME;
    else                  game_mode = Game_Mode::WAITING_LOUNGE;

    init_ship_position();
    set_high_score_text(String(""));

    lives = NUM_LIVES;
    ship_destroyed = false;

    ship_shield_cooldown = ship_piercing_cooldown = 0;

    ship_v_shot_stability_cooldown = 0;
    ship_v_shot_target_angle = ship_v_shot_current_angle = 0;

    respawn_cooldown = -1.0f;

    ship_shot_type = Shot_Type::STRAIGHT_SINGLE;

    score = 0;
    level_index = -1;
    shot_index = 0;
    total_pickups_gotten = 0;

    reset_transition_mode();
    assert(transition_dots.count == 0);

    for (auto &it : layout_lines) release(&it);
    array_reset(&layout_lines);

    // Reset the visual count ups. @Cleanup: Remove the visual_*
    {
        visual_score = 0;
        visual_level_index = -1;
        visual_total_pickups_gotten = 0;

        visual_count_up_elapsed = 0;
        visual_duration_per_count_up = -1;
        visual_steps = 0;
        visual_step_multiplier = 1;
        counting_up_completed = false;
    }
}

void read_input()
{
    // @Cleanup: Should we really not read any input in transitions? Think about this...
    if (transition_mode == Transition_Mode::CHANGE_GAME_MODE) return;

    for (auto event : events_this_frame)
    {
        switch (game_mode)
        {
            case Game_Mode::IN_GAME: handle_event_for_game(event); break;
            case Game_Mode::SCORE_OVERVIEW: {
                if (should_ask_for_score_name())
                {
                    if (!counting_up_completed) break;

                    handle_high_score_input_event(event);

                    if (high_score_name_input.entered)
                    {
                        high_score_name_input.entered = false;

                        auto text = high_score_name_input.text;
                        eat_spaces(&text);
                        eat_trailing_spaces(&text);

                        set_high_score_text(String(""));
                        if (!text.count) break;

                        add_high_score_record(text, score);
                        do_change_mode_transition(Game_Mode::LEADER_BOARD, MODE_TRANSITION_DURATION_BASE);
                    }
                }
                else
                {
                    // Press any key to continue.
                    if (event.key_pressed) do_change_mode_transition(Game_Mode::LEADER_BOARD, MODE_TRANSITION_DURATION_BASE);
                }
            } break;
            case Game_Mode::LEADER_BOARD: {
                // @Incomplete: Show something that says 'Press R to restart!'

                if (event.type != EVENT_KEYBOARD) break;

                if (event.key_pressed)
                {
                    if (event.key_code == Key_Code::CODE_R)
                    {
                        restart_game();
                    }
                }
            } break;
            case Game_Mode::WAITING_LOUNGE: {
                // Press any key to start game.
                auto transition_duration = MODE_TRANSITION_DURATION_BASE * .7f;
                if (event.key_pressed)
                {
                    do_change_mode_transition(Game_Mode::IN_GAME, transition_duration);
                }
            } break;
        }
    }

    if (shoot_button_down)
    {
        maybe_fire_bullets();
    }

    per_frame_update_mouse_position();
}

constexpr auto MIDFIELD = 5.0f / 8.0f;

/*
void invader_random_target(Invader *invader)
{
    auto target = &invader->target_position;
    target->x = get_random_within_range(0, 1);
    target->y = get_random_within_range(MIDFIELD * .5f, MIDFIELD * .8f);
}

void update_proc_side(Invader *i)
{
    constexpr auto RATE = 1.5f;
    auto theta = timez.current_time * TAU * RATE;

    auto dx = cosf(theta);

    i->position.x += dx * .5f * timez.current_dt;
}

void update_proc_circle(Invader *i)
{
    constexpr auto RATE = 1.5f;

    auto theta = timez.current_time * TAU * RATE;

    auto dx = cosf(theta);
    auto dy = sinf(theta);

    auto r = .5f * timez.current_dt;
    i->position.x += dx * r;
    i->position.y += dy * r;
}
*/

Vector2 get_target_position(Invader *invader)
{
    auto line = &layout_lines[invader->line_index];
    auto slot = &line->invader_slots[invader->slot_index];

    return slot->position + formation_offset;
}

constexpr auto NUM_INVADER_PROCS = 3; // 3 because an invader can also have no update proc, which count as a proc too.

void add_invader(i32 line_index, i32 slot_index)
{
    auto invader = New<Invader>();

    auto roll = rand() % invader_maps.count;

    invader->map = invader_maps[roll];
    invader->line_index = line_index;
    invader->slot_index = slot_index;

    invader->position = get_target_position(invader);
    invader->position.y = LIVE_MAX_Y;

    invader->spawn_position = invader->position;

    invader->action = Invader_Action::FALLING_IN;

/*
    // Update proc.
    auto proc_roll = rand() % NUM_INVADER_PROCS;

    if      (proc_roll == 1) invader->update_proc = update_proc_side;
    else if (proc_roll == 2) invader->update_proc = update_proc_circle;
*/

    array_add(&live_invaders, invader);
}

Level_Config LEVELS_LAYOUT[] = {
    {.num_lines = 2, .invaders_per_line = 5, .enemy_scale = 1.12f}, // Level one.
    {.num_lines = 3, .invaders_per_line = 5, .enemy_scale = 1.10f},
    {.num_lines = 4, .invaders_per_line = 5, .enemy_scale = 1.05f},
    {.num_lines = 4, .invaders_per_line = 6, .enemy_scale = 1.00f},
    {.num_lines = 5, .invaders_per_line = 6, .enemy_scale = 0.92f},
    {.num_lines = 5, .invaders_per_line = 7, .enemy_scale = 0.85f},
};

void spawn_invaders(Layout_Line *line)
{
    dudes_spawned_this_level += line->invader_slots.count;

    for (i32 invader_index = 0; invader_index < line->invader_slots.count; ++invader_index)
    {
        add_invader(line->line_index, invader_index);
    }
}

void init_new_level()
{
    time_of_level_start = timez.current_time;
    dudes_spawned_this_level = 0;
    strafe_cooldown = 1.3f;

    constexpr auto LEVELS_LAYOUT_COUNT = sizeof(LEVELS_LAYOUT) / sizeof(Level_Config);

    assert(level_index >= 0);

    auto config_index = level_index;
    Clamp(&config_index, 0, static_cast<i32>(LEVELS_LAYOUT_COUNT - 1));
    Level_Config config = LEVELS_LAYOUT[config_index];

    current_invader_radius = INVADER_RADIUS_AT_SCALE_1 * config.enemy_scale;

    for (auto &it : layout_lines) release(&it);
    array_resize(&layout_lines, config.num_lines);

    memset(layout_lines.data, 0, sizeof(layout_lines[0]) * layout_lines.count); // @Hack @Fixme: Fix the array reserve not setting the default value for stuff...

    for (i32 line_index = 0; line_index < config.num_lines; ++line_index)
    {
        auto line = &layout_lines[line_index];
        line->line_index = line_index;

        auto y = .53f + .1f * (line_index);

        auto num_invaders = config.invaders_per_line;

        auto margin = current_invader_radius * .7f;
        auto line_width = num_invaders * current_invader_radius*2 + (num_invaders - 1) * margin;

        auto x0 = .5 - line_width * .5f;
        auto x1 = .5 + line_width * .5f;

        constexpr auto SPAWN_COOLDOWN_PER_LINE = .7f;
        line->spawn_cooldown = .1f + line_index * SPAWN_COOLDOWN_PER_LINE;

        array_resize(&line->invader_slots, num_invaders);
        for (i32 invader_index = 0; invader_index < num_invaders; ++invader_index)
        {
            auto t = invader_index / static_cast<f32>(num_invaders - 1);

            auto slot = &line->invader_slots[invader_index];
            slot->position = Vector2(lerp(x0, x1, t), y);
        }
    }

    auto fader_text = tprint(String("Stage %d"), level_index + 1);
    auto fader = game_report(fader_text);

    constexpr auto STAGE_ANNOUNCEMENT_DURATION = 2.5f;
    fader->fade_out_t = STAGE_ANNOUNCEMENT_DURATION;

    fader->color = Vector4(.23f, .87f, .43f, 1);

    ship_shot_cooldown = 0;
}

void init_game()
{
    // srand(time(NULL));

    auto white_color = Vector4(1, 1, 1, 1);
    ship_color = bullet_color = invader_color = pickup_color = white_color;

    // Textures.
    ship_map           = catalog_find(&texture_catalog, String("ship"));
    contrail_map       = catalog_find(&texture_catalog, String("contrail"));
    ship_bullet_map    = catalog_find(&texture_catalog, String("ship_bullet"));
    invader_bullet_map = catalog_find(&texture_catalog, String("invader_bullet"));
    pickup_extra_bullets_map = catalog_find(&texture_catalog, String("extra_bullet"));
    pickup_v_shot_map        = catalog_find(&texture_catalog, String("v_shot"));
    pickup_shield_map        = catalog_find(&texture_catalog, String("shield"));
    pickup_piercing_map      = catalog_find(&texture_catalog, String("piercing"));

    dead_cross_map = catalog_find(&texture_catalog, String("dead_cross"));
    notebook_background = catalog_find(&texture_catalog, String("notebook"));

    // Sizes.
    ship_size    = Vector2(SHIP_RADIUS*2, SHIP_RADIUS*2);
    bullet_size  = Vector2(BULLET_RADIUS*2, BULLET_RADIUS*2);
    pickup_size  = Vector2(PICKUP_RADIUS*2, PICKUP_RADIUS*2);

    // Invaders' textures.
    auto bug1 = catalog_find(&texture_catalog, String("bug1"));
    auto bug2 = catalog_find(&texture_catalog, String("bug2"));
    auto bug3 = catalog_find(&texture_catalog, String("bug3"));
    auto bug4 = catalog_find(&texture_catalog, String("bug4"));

    array_add(&invader_maps, bug1);
    array_add(&invader_maps, bug2);
    array_add(&invader_maps, bug3);
    array_add(&invader_maps, bug4);

    // Sounds.
    // @Cleanup: Make a sound catalog for this.
    {
        auto game_music_success = load_music(&game_music, String("data/sounds/theme.mp3"));
        assert(game_music_success);

        auto general_music_success = load_music(&general_music, String("data/sounds/annoying_music.mp3"));
        assert(general_music_success);

        auto success = load_sound(&sound_ship_shot, String("data/sounds/ship_shot.mp3")); assert(success);
        success = load_sound(&sound_pickup,       String("data/sounds/pickup.mp3")); assert(success);
        success = load_sound(&sound_invader_die,  String("data/sounds/invader_die.mp3")); assert(success);
        success = load_sound(&sound_ship_die,     String("data/sounds/ship_die.mp3")); assert(success);
        success = load_sound(&sound_ship_respawn, String("data/sounds/ship_respawn.mp3")); assert(success);
        success = load_sound(&sound_new_stage,    String("data/sounds/new_stage.mp3")); assert(success);
        success = load_sound(&sound_transition,   String("data/sounds/transition.mp3")); assert(success);

        // Start playing the music right away.
        // @Incomplete: We want to think about the music mixing part and when do we play certain musics.
        play_music(&game_music);
        play_music(&general_music);
    }

    restart_game(false);

    // @Cleanup: Should we be loading everytime we transition into the leader board view?
    load_high_score_leader_board();
}

void linear_move(Vector2 *position, Vector2 velocity, f32 dt)
{
    position->x += velocity.x * dt;
    position->y += velocity.y * dt;
}

f32 get_random_sign()
{
    auto x = rand();

    return (x & 1) ? -1.0f : 1.0f;
}

void init_strafe_path(Path *path, Vector2 start)
{
    path->path_flags = 0;    
    array_reset(&path->waypoints);

    auto strafe_type = (Strafe_Type)(rand() % (i32)Strafe_Type::NUM_STRAFE_TYPE);
    path->strafe_type = strafe_type;

    array_add(&path->waypoints, start);

    auto pos = start;

    switch (strafe_type)
    {
        case Strafe_Type::DOWNWARD: {
            f32 left_or_right;

            if      (start.x < .2f)       left_or_right = 1;
            else if (start.x < (1 - .2f)) left_or_right = -1;
            else                          left_or_right = get_random_sign();

            pos += Vector2(left_or_right * .2f, -.25f);
            array_add(&path->waypoints, pos);

            pos += Vector2(-left_or_right * .4f, -.25f);
            array_add(&path->waypoints, pos);

            pos += Vector2(left_or_right * .5f, -.25f);
            array_add(&path->waypoints, pos);

            pos.y = LIVE_MIN_Y;
            array_add(&path->waypoints, pos);

            path->path_flags |= ENDPOINT_IS_OFF_THE_BOTTOM;
        } break;
        case Strafe_Type::SIDEWAYS: {
            pos += Vector2(-.1f, -.25f);
            array_add(&path->waypoints, pos);

            pos += Vector2(.5f, 0.f);
            array_add(&path->waypoints, pos);

            pos += Vector2(.0f, .3f);
            array_add(&path->waypoints, pos);

            array_add(&path->waypoints, start);

            path->path_flags |= ENDPOINT_IS_IN_FORMATION;
        } break;
        case Strafe_Type::KAMIKAZE_STRAIGHT: {
            auto left_or_right = get_random_sign();

            pos += Vector2(left_or_right * .08f, .08f);
            array_add(&path->waypoints, pos);

            pos = ship_position;
            array_add(&path->waypoints, pos);
            
            pos.y = LIVE_MIN_Y;
            array_add(&path->waypoints, pos);

            path->path_flags |= ENDPOINT_IS_OFF_THE_BOTTOM;
        } break;
        case Strafe_Type::KAMIKAZE_DRUNKEN: {
            auto left_or_right = get_random_sign();

            pos += Vector2(left_or_right * .08f, .08f);
            array_add(&path->waypoints, pos);

            auto k = .1f;

            pos.x = ship_position.x;
            pos.y -= k;
            array_add(&path->waypoints, pos);

            auto side = .18f;

            pos.x += -left_or_right * side;
            pos.y -= k;
            array_add(&path->waypoints, pos);
            
            pos.x += left_or_right * side;
            pos.y -= k;
            array_add(&path->waypoints, pos);
            
            pos.x += -left_or_right * side;
            pos.y -= k;
            array_add(&path->waypoints, pos);
            
            pos.x += left_or_right * side;
            pos.y -= k;
            array_add(&path->waypoints, pos);

            pos.y = LIVE_MIN_Y;
            array_add(&path->waypoints, pos);

            path->path_flags |= ENDPOINT_IS_OFF_THE_BOTTOM;
        } break;
    }

    path->cursor.waypoint_index = 1; // Ignore the first waypoint...
}

my_pair<Vector2, bool> follow_path(Path *path, Vector2 pos, f32 distance_to_travel, f32 *desired_theta)
{
    // This modifies the path cursor.

    // @Incomplete: If the path is too far off the screen, we might think
    // of wrapping the movement.
    while (distance_to_travel > 0)
    {
        auto index  = path->cursor.waypoint_index;
        auto target = path->waypoints[index];

        auto dir = unit_vector(target - pos);
        auto len = glm::distance(target, pos);

        auto theta = atan2(dir.y, dir.x) * (360.0f / TAU);
        *desired_theta = theta;

        if (len <= distance_to_travel)
        {
            distance_to_travel -= len;
            path->cursor.waypoint_index += 1;

            if (path->cursor.waypoint_index >= path->waypoints.count)
            {
                return {target, true};
            }
        }
        else
        {
            auto result_pos = pos + dir * distance_to_travel;
            return {result_pos, false};
        }
    }

    assert(0);
    return {pos, false};
}

void release(Path *path)
{
    array_free(&path->waypoints);
}

void destroy_invader(Invader *invader)
{
    array_unordered_remove_by_value(&live_invaders, invader); // @Speed:

    play_sound(&sound_invader_die);

    // Particles
    {
        auto em = New<Particle_Emitter>();
        array_add(&live_emitters, em);

        em->size0 = .12f;
        em->size1 = .19f;

        em->speed0 = 2.f;
        em->speed1 = 5.f;

        em->color0 = Vector4(1, 1, .3f, 1);
        em->color1 = Vector4(1, 1, 1, 1);

        em->fadeout_period   = .7f;
        em->emitter_lifetime = .2f;

        em->position = invader->position;
        em->velocity = rotate(Vector2(1, 0), get_random_within_range(.15f, TAU * .9f));
    }

    {
        auto em = New<Particle_Emitter>();
        array_add(&live_emitters, em);

        em->size0 = .03f;
        em->size1 = .10f;

        em->speed0 = 6.f;
        em->speed1 = 9.f;

        em->color0 = Vector4(.8f, .6f, .6f, 1);
        em->color1 = Vector4(.6f, .4f, .4f, 1);

        em->fadeout_period   = .7f;
        em->emitter_lifetime = .2f;

        em->position = invader->position;
        em->velocity = rotate(Vector2(1, 0), get_random_within_range(.15f, TAU * .9f));
    }

    // Pickups drop.
    auto drop_roll = rand() % 100;
    if (drop_roll < 20)
    {
        auto pickup = New<Pickup>();
        array_add(&live_pickups, pickup);

        auto roll = rand() % 100;

        pickup->position = invader->position;

        constexpr auto PICKUP_SPEED = -.2f; // Negative because the drop falls down.
        pickup->velocity = Vector2(0, PICKUP_SPEED * get_random_within_range(.7f, 1.7f));

        if (roll < 15)
        {
            pickup->type = Pickup_Type::V_SHOT;
            pickup->map  = pickup_v_shot_map;
        }
        else if (roll < 37)
        {
            pickup->type = Pickup_Type::PIERCING;
            pickup->map  = pickup_piercing_map;
        }
        else if (roll < 50)
        {
            pickup->type = Pickup_Type::EXTRA_BULLETS;
            pickup->map  = pickup_extra_bullets_map;
        }
        else
        {
            pickup->type = Pickup_Type::SHIELD;
            pickup->map  = pickup_shield_map;
        }
    }

    my_free(invader);

    release(&invader->strafe_path);
}

bool test_against_invaders(Bullet *bullet)
{
    for (auto it : live_invaders)
    {
        if (glm::distance(it->position, bullet->position) < (current_invader_radius + BULLET_RADIUS))
        {
            destroy_invader(it);
            return true;
        }
    }

    return false;
}

bool test_against_ship(Vector2 test_position, f32 radius)
{
    if (ship_destroyed) return false;

    return glm::distance(ship_position, test_position) < (radius + SHIP_RADIUS);
}

void simulate_bullets()
{
    auto simulate_one_bullet = [](Bullet *bullet) -> bool {
        linear_move(&bullet->position, bullet->velocity, timez.current_dt);

        if ((bullet->position.y > LIVE_MAX_Y) || (bullet->position.y < LIVE_MIN_Y)) return true;

        if (bullet->emitter)
        {
            bullet->emitter->position = bullet->position;
            bullet->emitter->velocity = bullet->velocity;
        }

        if (bullet->player_friendly)
        {
            if (test_against_invaders(bullet))
            {
                constexpr auto SCORE_MULTIPLIER = 10;
                score += 1 * SCORE_MULTIPLIER;

                return !bullet->can_pierce; // Keep going if we can pierce.
            }
        }
        else
        {
            if (test_against_ship(bullet->position, BULLET_RADIUS))
            {
                if (!ship_has_shield()) destroy_ship();

                return true;
            }
        }

        return false;
    };

    for (i64 it_index = 0; it_index < live_bullets.count; )
    {
        auto it = live_bullets[it_index];

        auto done = simulate_one_bullet(it);

        if (done)
        {
            if (it->emitter) it->emitter->producing = false;

            live_bullets[it_index] = live_bullets[live_bullets.count - 1];
            live_bullets.count -= 1;

            my_free(it);

            continue;
        }

        it_index += 1;
    }
}

void ship_got_pickup(Pickup *pickup)
{
    total_pickups_gotten += 1;

    play_sound(&sound_pickup);

    switch (pickup->type)
    {
        case Pickup_Type::EXTRA_BULLETS: {
            if (ship_shot_type == Shot_Type::STRAIGHT_SINGLE)
            {
                ship_shot_type = Shot_Type::STRAIGHT_DOUBLE;
            }
            else if (ship_shot_type == Shot_Type::STRAIGHT_DOUBLE)
            {
                ship_shot_type = Shot_Type::STRAIGHT_TRIPLE;
            }
            else if (ship_shot_type == Shot_Type::STRAIGHT_TRIPLE)
            {
                // @Incomplete: We should not just switch back to the single shot?
                ship_shot_type = Shot_Type::STRAIGHT_SINGLE;
            }
        } break;
        case Pickup_Type::V_SHOT: {
            ship_v_shot_stability_cooldown = SHIP_V_SHOT_STABILITY_TIME;
            ship_v_shot_target_angle += SHIP_V_SHOT_DEGREES_PER_PICKUP;

            Clamp(&ship_v_shot_target_angle, 0.0f, SHIP_V_SHOT_DEGREES_MAX);
        } break;
        case Pickup_Type::SHIELD: {
            set_shield(SHIP_SHIELD_TIME);
        } break;
        case Pickup_Type::PIERCING: {
            ship_piercing_cooldown += SHIP_PIERCING_TIME;
        } break;
        default: assert(0);
    }
}

f32 ilength(f32 x, f32 y)
{
    auto length = x*x + y*y;
    auto denom = 1.0f / sqrtf(length);

    return denom;
}

void invader_fire_bullet(Invader *invader)
{
    auto bullet = New<Bullet>();

    bullet->position = invader->position;
    bullet->position.y -= .015f;

    bullet->velocity.x = 0;
    if (rand() % 2) bullet->velocity.y = -.5f;
    else            bullet->velocity.y = -.35f;

    // auto strafe_type = invader->strafe_path.strafe_type;
    // if (strafe_type == Strafe_Type::DOWNWARD)
    // {
    //     auto radians = invader->theta * (TAU / 360.0f);
    //     bullet->velocity = rotate(bullet->velocity, radians);
    // }

    bullet->map = invader_bullet_map;
    bullet->player_friendly = false;

    array_add(&live_bullets, bullet);

    auto emitter = New<Particle_Emitter>();
    bullet->emitter = emitter;

    emitter->theta0 = TAU * .6f;
    emitter->theta1 = TAU * .9f;
    emitter->drag0  = .9f;
    emitter->drag1  = .97f;

    auto k0 = 1.0f;
    auto k1 = .30f;

    emitter->color0 = Vector4(k0*.3f, k0, k0*.5f, 1);
    emitter->color1 = Vector4(k1*.3f, k1, k1*.5f, 1);

    emitter->fadeout_period = 0.04f;

    array_add(&live_emitters, emitter);
}

//
// Invader actions:
//

void start_shot_cooldown(Invader *i)
{
    switch (i->action)
    {
        case Invader_Action::FALLING_IN: i->shot_cooldown = -1; break;
        case Invader_Action::SLEEPING:   i->shot_cooldown = get_random_within_range(2.2f, 9.5f); break;
        case Invader_Action::STRAFING:   i->shot_cooldown = get_random_within_range(.20f, 1.2f); break;
        default: i->shot_cooldown = -1; break;
    }
}

constexpr auto FALLING_IN_SPEED = .45f;

void falling_in(Invader *i)
{
    auto target = get_target_position(i);

    i->position.x = target.x;

    auto ty = target.y;

    if (i->position.y <= ty)
    {
        // We are done.
        i->action = Invader_Action::SLEEPING;
        // start_sleep_cooldown(i);
        start_shot_cooldown(i);
        return;
    }
    
    i->position.y -= FALLING_IN_SPEED * timez.current_dt;
    if (i->position.y < ty) i->position.y = ty;

    // Compute new theta.
    auto denom = ty - i->spawn_position.y;
    if (denom)
    {
        auto t = (i->position.y - i->spawn_position.y) / denom; // [0..1].

        constexpr auto NUM_CIRCLE_ROLLS = 2;
        auto theta = 360.0f * NUM_CIRCLE_ROLLS * t;

        i->theta = theta;
    }
}

f32 wrap_degrees(f32 dtheta)
{
    if (dtheta < -180) dtheta += 360;
    if (dtheta >  180) dtheta -= 360;

    return dtheta;
}

f32 turn_toward(f32 current, f32 target, f32 amount)
{
    current = wrap_degrees(current);
    target  = wrap_degrees(target);

    auto delta_theta = target - current;
    if (delta_theta >  180) target -= 360;
    if (delta_theta < -180) target += 360;

    auto result = move_toward(current, target, amount);
    return result;
}

void sleeping(Invader *i)
{
    constexpr auto DTHETA_DT = 370.0f;
    i->theta = turn_toward(i->theta, 0, DTHETA_DT * timez.current_dt);
    i->position = get_target_position(i);
}

void strafing(Invader *i)
{
    auto path = &i->strafe_path;
    if (path->path_flags & ENDPOINT_IS_IN_FORMATION)
    {
        auto wp = &path->waypoints[path->waypoints.count - 1];
        *wp = get_target_position(i);
    }

    auto speed_slow = .51f + .060f * level_index;
    auto speed_fast = .91f + .075f * level_index;

    auto speed = lerp(speed_slow, speed_fast, current_aggro);

    auto distance_to_travel = speed * timez.current_dt;

    f32 target_theta;
    auto [target_pos, done] = follow_path(path, i->position, distance_to_travel, &target_theta);

    {
        //
        // :Downward
        // Make the invaders facing downward.
        //
        target_theta -= 90;

        // Update theta to be like target_theta.
        constexpr auto DTHETA_DT = 90.0f;
        i->theta = turn_toward(i->theta, target_theta, DTHETA_DT * timez.current_dt);

        // i->theta = target_theta;
    }

    i->position = target_pos;

    if (done)
    {
        if (path->path_flags & ENDPOINT_IS_IN_FORMATION)
        {
            i->action = Invader_Action::SLEEPING;
            // start_sleep_cooldown(i);
        }
        else if (path->path_flags & ENDPOINT_IS_OFF_THE_BOTTOM)
        {
            // Set the guy to the top and start falling in.
            i->position.y = LIVE_MAX_Y;
            i->action = Invader_Action::FALLING_IN;
        }
        else
        {
            // Default strafe....
            i->action = Invader_Action::SLEEPING;
            // start_sleep_cooldown(i);
        }
    }
}

template <typename T>
void array_unordered_remove_by_index(RArr<T> *array, i64 index)
{
    assert(index >= 0);
    assert(index < array->count);

    auto last_index = array->count - 1;

    if (index != last_index) array->data[index] = array->data[last_index];

    array->count -= 1;
}

void start_strafing(Invader *i)
{
    // :Downward @Incomplete: Do some turn_toward here rather than just setting the value.
    i->theta = 180;

    i->action = Invader_Action::STRAFING;
    init_strafe_path(&i->strafe_path, i->position);
}

void decide_about_strafing()
{
    auto now     = timez.current_time;
    auto elapsed = now - time_of_level_start;

    auto minutes = elapsed / 60;
    auto dude_modifer = powf(.5f, minutes);

    assert(live_invaders.count <= dudes_spawned_this_level);
    auto denom = std::max(dudes_spawned_this_level, 1);

    // How many dudes are alive, as a ratio to dude spawned?
    auto dudes_alive_ratio = dude_modifer * (live_invaders.count / static_cast<f32>(denom));

    current_aggro = 1 - dudes_alive_ratio;
    Clamp(&current_aggro, 0.0f, 1.0f);

    //
    // Spawn strafing dudes according to our 'progress' within in the current level stage.
    //
    i32 num_strafing = 0;

    RArr<Invader*> sleeping;
    sleeping.allocator = {global_context.temporary_storage, __temporary_allocator};

    for (auto it : live_invaders)
    {
        if (it->action == Invader_Action::STRAFING) num_strafing += 1;
        if (it->action == Invader_Action::SLEEPING) array_add(&sleeping, it);
    }

    auto desired_strafer = 1 + level_index * 1.2f + current_aggro * level_index * 1.3f;
    auto desired = static_cast<i32>(floorf(desired_strafer + .5f));

    // Start strafing one guy at a time (if there are multiple).
    if (sleeping.count && (num_strafing < desired))
    {
        cooldown(&strafe_cooldown, timez.current_dt);

        if (strafe_cooldown == 0)
        {
            auto index = rand() % sleeping.count;
            auto invader = sleeping[index];
            array_unordered_remove_by_index(&sleeping, index);

            start_strafing(invader);

            // Reset the strafe cooldown (the pace should go up later in the level).
            auto delay0 = .1f;
            auto delay1 = 5.f * powf(.5f, level_index / 2.0f);

            strafe_cooldown = get_random_within_range(delay0, delay1);

            start_shot_cooldown(invader);
        }
    }
}

void simulate_invaders(f32 dt)
{
    decide_about_strafing();

    RArr<Invader*> to_destroy;
    to_destroy.allocator = {global_context.temporary_storage, __temporary_allocator};

    for (auto it : live_invaders)
    {
        if (test_against_ship(it->position, current_invader_radius))
        {
            array_add(&to_destroy, it);
            if (!ship_has_shield()) destroy_ship();
        }

        if (it->shot_cooldown > 0) cooldown(&it->shot_cooldown, dt);
        if (it->shot_cooldown == 0)
        {
            invader_fire_bullet(it);
            start_shot_cooldown(it);
        }

        switch (it->action)
        {
            case Invader_Action::FALLING_IN: falling_in(it); break;
            case Invader_Action::SLEEPING:   sleeping(it); break;
            case Invader_Action::STRAFING:   strafing(it); break;
        }
    }

    for (auto it : to_destroy)
    {
        destroy_invader(it);
    }
}

Particle spawn_particle(Particle_Emitter *emitter)
{
    Particle result;

    result.position = emitter->position;

    // Randomize velocity direciton and speed.
    auto velocity = emitter->velocity;
    velocity = rotate(velocity, get_random_within_range(emitter->theta0, emitter->theta1));
    velocity *= get_random_within_range(emitter->speed0, emitter->speed1);
    result.velocity = velocity;

    // Randomize size.
    result.size = get_random_within_range(emitter->size0, emitter->size1);

    // Randomzie lifetime.
    result.lifetime = get_random_within_range(emitter->lifetime0, emitter->lifetime1);

    // Randomize drag.
    result.drag = get_random_within_range(emitter->drag0, emitter->drag1);
    Clamp(&result.drag, .0f, 1.f);

    return result;
}

void simulate_one_particle(Particle *p, f32 dt, f32 fadeout_period)
{
    p->elapsed += dt;
    p->position += p->velocity * dt * (1 - p->drag);

    p->size = move_toward(p->size, 0, fadeout_period * dt);
}

constexpr auto NUM_PARTICLES_PER_SECOND = 150;
bool update_emitter(Particle_Emitter *emitter, f32 dt)
{
    for (i64 it_index = 0; it_index < emitter->particles.count; )
    {
        auto it = &emitter->particles[it_index];

        simulate_one_particle(it, dt, emitter->fadeout_period);

        if (it->elapsed > it->lifetime)
        {
            array_unordered_remove_by_index(&emitter->particles, it_index);
            continue;
        }

        it_index += 1;
    }

    auto dt_per_particle = 1.0f / NUM_PARTICLES_PER_SECOND;

    emitter->elapsed   += dt;
    emitter->remainder += dt;

    if (emitter->emitter_lifetime >= 0)
    {
        emitter->emitter_lifetime -= dt;
        if (emitter->emitter_lifetime < 0) emitter->producing = false;
    }

    bool done = false;

    if (emitter->producing)
    {
        while (emitter->remainder > dt_per_particle)
        {
            emitter->remainder -= dt_per_particle;
            auto p = spawn_particle(emitter);
            simulate_one_particle(&p, dt, emitter->fadeout_period);

            array_add(&emitter->particles, p);
        }
    }
    else
    {
        if (!emitter->particles.count)
        {
            done = true;
        }
    }

    return done;
}

void release(Particle_Emitter *emitter)
{
    array_free(&emitter->particles);
    my_free(emitter);
}

void simulate_pickups(f32 dt)
{
    auto simulate_one_pickup = [](Pickup *pickup, f32 dt) -> bool {
        linear_move(&pickup->position, pickup->velocity, dt);

        if (pickup->position.y > LIVE_MAX_Y) return true;
        if (pickup->position.y < LIVE_MIN_Y) return true;

        if (test_against_ship(pickup->position, PICKUP_RADIUS))
        {
            // if (ship_has_shield())
            // {
            //     // @Incomplete: Failed to pickup, play some sound.
            // }
            // else
            {
                ship_got_pickup(pickup);
                // @Incomplete: Play some sound.
            }

            return true;
        }

        return false;
    };

    for (i64 it_index = 0; it_index < live_pickups.count; )
    {
        auto it = live_pickups[it_index];
        auto done = simulate_one_pickup(it, dt);

        if (done)
        {
            live_pickups[it_index] = live_pickups[live_pickups.count - 1];
            live_pickups.count -= 1;

            my_free(it);

            continue;
        }

        it_index += 1;
    }
}

bool simulate_spawns(f32 dt)
{
    bool is_spawning = false;

    for (auto &it : layout_lines)
    {
        if (it.spawn_cooldown < 0) continue; // Already spawned.

        is_spawning = true;

        cooldown(&it.spawn_cooldown, dt);
        if (it.spawn_cooldown == 0)
        {
            it.spawn_cooldown = -1; // Mark as spawned.
            spawn_invaders(&it);
        }
    }

    //
    // Update formation stuff.
    //
    auto theta   = fmodf(100.0f * timez.current_time, 360.0f);
    auto radians = theta * (TAU / 360.0f);

    constexpr auto FORMATION_OFFSET_SCALE = .01f;
    formation_offset = FORMATION_OFFSET_SCALE * Vector2(cosf(radians), sinf(2*radians));

    return is_spawning;
}

void do_entities_cleanup()
{
    for (auto it : live_bullets)
    {
        my_free(it);
    }
    array_reset(&live_bullets);

    for (auto it : live_emitters)
    {
        release(it);
    }
    array_reset(&live_emitters);

    for (auto it : live_invaders)
    {
        my_free(it);
    }
    array_reset(&live_invaders);

    for (auto it : live_pickups)
    {
        my_free(it);
    }
    array_reset(&live_pickups);
}

void simulate()
{
    auto dt = timez.current_dt;

    if (transition_mode == Transition_Mode::CHANGE_GAME_MODE) return;

    //
    // Once we complete the transition, we start to decay the dot particles.
    //
    {
        for (i64 it_index = 0; it_index < transition_dots.count; )
        {
            auto it = &transition_dots[it_index];

            constexpr auto TRANSITION_FADEOUT_PERIOD = .1f;
            simulate_one_particle(it, dt, TRANSITION_FADEOUT_PERIOD);

            if (it->elapsed > it->lifetime)
            {
                array_unordered_remove_by_index(&transition_dots, it_index);
                continue;
            }

            it_index += 1;
        }
    }

    if (game_mode != Game_Mode::IN_GAME)
    {
        // Play the general music for anything else but IN_GAME...
        // @Incomplete: We want music fade in and fade out.
        update_music(&general_music);
        return;
    }

    update_music(&game_music);

    cooldown(&ship_shot_cooldown, dt);
    cooldown(&ship_shield_cooldown, dt);
    cooldown(&ship_v_shot_stability_cooldown, dt);
    cooldown(&ship_piercing_cooldown, dt);

    if (ship_v_shot_stability_cooldown > 0)
    {
        // We are growing.
        if (ship_v_shot_current_angle < ship_v_shot_target_angle)
        {
            auto d_angle = dt * SHIP_V_SHOT_EXPAND_DEGREES_PER_SECOND;
            ship_v_shot_current_angle += d_angle;

            if (ship_v_shot_current_angle > ship_v_shot_target_angle) ship_v_shot_current_angle = ship_v_shot_target_angle;
        }
    }
    else
    {
        ship_v_shot_target_angle = 0;

        // We are shrinking.
        if (ship_v_shot_current_angle > ship_v_shot_target_angle)
        {
            auto d_angle = dt * SHIP_V_SHOT_SHRINK_DEGREES_PER_SECOND;
            ship_v_shot_current_angle -= d_angle;

            if (ship_v_shot_current_angle < 0) ship_v_shot_current_angle = 0;
        }
    }

    if (respawn_cooldown >= 0)
    {
        // Make the ship turn and fall like an apple when it is destroyed.
        constexpr auto SHIP_ANGLE_DOWN     = 180.0f;
        constexpr auto SHIP_DESTROY_DTHETA = 470.0f;

        ship_theta = turn_toward(ship_theta, SHIP_ANGLE_DOWN, dt * SHIP_DESTROY_DTHETA);

        constexpr auto SHIP_DESTROY_FALL_RATE = .9f;
        ship_position.y = move_toward(ship_position.y, LIVE_MIN_Y, dt * SHIP_DESTROY_FALL_RATE);

        respawn_cooldown -= dt;
        if (respawn_cooldown < 0)
        {
            lives -= 1;

            if (lives < 0)
            {
//                should_quit = true;
                do_entities_cleanup();
                do_change_mode_transition(Game_Mode::SCORE_OVERVIEW, MODE_TRANSITION_DURATION_BASE);
            }
            else
            {
                ship_destroyed = false;

                play_sound(&sound_ship_respawn, false);

                // Add shield to the ship after it respawned.
                init_ship_position();
            }
        }
    }

    auto ship_velocity = Vector2(0);

    if (key_left)  ship_velocity.x -= 1;
    if (key_right) ship_velocity.x += 1;
    if (key_up)    ship_velocity.y += 1;
    if (key_down)  ship_velocity.y -= 1;

    if (glm::length(ship_velocity) > 1.0f)
    {
        ship_velocity = unit_vector(ship_velocity);
    }

    if (!ship_destroyed && (ship_velocity.x || ship_velocity.y))
    {
        constexpr auto SHIP_DV = .6f;
        ship_velocity *= SHIP_DV;

        linear_move(&ship_position, ship_velocity, dt);

        auto x0 = 0.03f;
        auto x1 = 1 - x0;
        auto y0 = 0.1f; // @Hardcoded: Ship cannot move below the lives status line.
        auto y1 = 0.35f;

        Clamp(&ship_position.x, x0, x1);
        Clamp(&ship_position.y, y0, y1);
    }

    if (transition_mode == Transition_Mode::NOTHING)
    {
        // Only spawn if we are transitioning complete.
        auto spawning = simulate_spawns(dt);

        if (!spawning && !live_invaders.count)
        {
            if (level_index >= 0) // Meaning we are going to a new stage.
            {
                do_stage_transition();
                play_sound(&sound_new_stage, false);
            }

            level_index += 1;
            init_new_level();
        }
    }

    simulate_bullets();
    simulate_invaders(dt);
    simulate_pickups(dt);

    for (i64 it_index = 0; it_index < live_emitters.count; )
    {
        auto it = live_emitters[it_index];

        auto done = update_emitter(it, dt);
        if (done)
        {
            release(it);
            array_unordered_remove_by_index(&live_emitters, it_index);
            continue;
        }

        it_index += 1;
    }
}
