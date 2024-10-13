// @Cleanup: If we do need more UI, maybe we should use the entire ui.h from sokoban.
// @Incomplete: Maybe we want autocomplete hints for the names input? We do, and we should do that later.

#include "text_input.h"
#include "invaders.h"
#include "font.h"
#include "events.h"
#include "time_info.h"
#include "file_utils.h"
#include "clipboard.h"

Text_Input_Theme default_text_input_theme;
Text_Input_State text_input_state;
Text_Input high_score_name_input;

RArr<Rect> scissor_stack; // @Cleanup: Remove this.

enum Status_Flags
{
    OVER     = 0x01,
    FOCUS    = 0x02,
    DOWN     = 0x04,
    PRESSED  = 0x08,
};

// Check if point is inside rect.
bool is_inside(f32 x, f32 y, Rect r)
{
    return (x >= r.x) && (x <= r.x + r.w) && (y >= r.y) && (y <= r.y + r.h);
}

Key_Current_State ui_get_button_state(Key_Code key_code, void *handle = NULL)
{
    auto state = input_button_states[key_code];
    return state;
}

u32 get_status_flags(Rect r)
{
    u32 status = 0;

    auto [x, y] = render_target_mouse_pointer_position(glfw_window, true);

    // if (!is_visible_in_scissor(x, y)) return status;

    if (is_inside(x, y, r))
    {
        status |= Status_Flags::OVER;

        auto mouse_button_left_state  = ui_get_button_state(CODE_MOUSE_LEFT);
        if (mouse_button_left_state & KSTATE_START) status |= Status_Flags::PRESSED;
        // if (mouse_button_left_state & KSTATE_DOWN)  status |= Status_Flags::DOWN;
    }

    return status;
}

Rect get_rect(f32 x, f32 y, f32 w, f32 h)
{
    Rect r;
    r.x = x;
    r.y = y;
    if (w < 0) r.x += w;
    if (h < 0) r.y += h;

    r.w = fabsf(w);
    r.h = fabsf(h);

    return r;
}

void get_quad(Rect r, Vector2 *p0, Vector2 *p1, Vector2 *p2, Vector2 *p3)
{
    p0->x = r.x;
    p1->x = r.x + r.w;
    p2->x = r.x + r.w;
    p3->x = r.x;

    p0->y = r.y;
    p1->y = r.y;
    p2->y = r.y + r.h;
    p3->y = r.y + r.h;
}

my_pair<f32 /*y0*/, f32 /*y1*/> get_y_range(Dynamic_Font *font, f32 text_y)
{
    constexpr auto FONT_CURSOR_EXTRA_HEIGHT = 0.1f; // @Hardcode @Cleanup
    auto y0 = text_y + font->typical_descender;
    auto y1 = y0 + font->typical_ascender + FONT_CURSOR_EXTRA_HEIGHT * font->character_height;

    y1 += 1; // To inlcude the top pixel, rather than limit just before the top pixel.

    auto padding = (y1 - y0) / 8.0f;

    // We tend to need more extra padding on the top and bottom,
    // since most letters don't have descenders.
    y0 -= padding;
    y1 += padding * 2;

    return {y0, y1};
}

inline
void set_scissor(Rect r)
{
    //
    // We are adding 1 to the width and height to the scissoring region
    // because if we don't it eats into the frame thickness.
    // *However*, this is wrong for cases when up pass in a zero-width or zero-height
    // rect.
    // So for now, I'm keeping things this way.
    //

    auto x = static_cast<i32>(r.x);
    auto y = static_cast<i32>(r.y);
    auto w = static_cast<i32>(r.w) + 1;
    auto h = static_cast<i32>(r.h) + 1;

    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
}

inline
void clear_scissor()
{
    glDisable(GL_SCISSOR_TEST);
}

void push_scissor(Rect r)
{
    if (scissor_stack)
    {
        auto top = &scissor_stack[scissor_stack.count - 1];

        // Intersect rectangles.
        auto x = std::max(top->x, r.x);
        auto y = std::max(top->y, r.y);
        auto w = std::max(std::min(top->x + top->w, r.x + r.w) - x, 0.0f);
        auto h = std::max(std::min(top->y + top->h, r.y + r.h) - y, 0.0f);

        r = get_rect(x, y, w, h);
    }

    set_scissor(r);
    array_add(&scissor_stack, r);
}

void pop_scissor()
{
    pop(&scissor_stack);

    if (scissor_stack)
    {
        auto r = scissor_stack[scissor_stack.count - 1];
        set_scissor(r);
    }
    else
    {
        clear_scissor();
    }
}

f32 em(Dynamic_Font *font, f32 x)
{
    return font->em_width * x;
}

void init_high_score_input()
{
    auto input = &high_score_name_input;

    input->text.data    = input->input_buffer.data;
    input->text.count   = 0;
    input->insert_point = 0;

    input->text_input_state = &text_input_state;
    text_input_state.input  = &high_score_name_input;

    input->active  = true;
    input->initted = true;
}

void append_text(Text_Input *input, String to_add);
void set_high_score_text(String text)
{
    auto input = &high_score_name_input;
    if (!input->initted) init_high_score_input();

    input->text.count = 0;
    input->insert_point = 0;

    if (text.count) append_text(input, text);
}

void append_text(Text_Input *input, String text)
{
    auto bytes_available = input->input_buffer.count - input->text.count;
    auto nbytes = std::min(bytes_available, text.count);

    u8 *dest = input->input_buffer.data;
    u8 *src  = text.data;
    memcpy(dest + input->text.count, src, nbytes);
    input->text.count   += nbytes;
    input->insert_point += nbytes; // @Bug @Hack: Assume insert_point is at the end. If we want this to be more robust, we could just insert at the point here as we do when pasting.

    // update_auto_complete(input); @Incomplete:
}

void clamp_insert_point(Text_Input *input)
{
    Clamp(&input->insert_point, 0, static_cast<i32>(input->text.count));
}

f32 update_action_durations(Text_Input_State *state, Text_Input_Theme *theme, bool begin, f32 dt)
{
    if (begin)
    {
        state->action_duration   = 0;
        state->action_duration_2 = 0;
    }

    auto pressed_factor = 0.0f;
    if (state->action_duration >= 0) // Some juice when you push the button.
    {
        state->action_duration += dt;
        auto denom = theme->press_duration;
        if (!denom) denom = 1;

        auto factor = state->action_duration / denom;
        pressed_factor = 1 - factor;
        if (pressed_factor < 0) state->action_duration = -1;

        Clamp(&pressed_factor, 0.f, 1.f);
        pressed_factor *= pressed_factor;
    }

    if (state->action_duration_2 >= 0) // Some juice when you push the button.
    {
        state->action_duration_2 += dt;
    }

    return pressed_factor;
}

void update_over_state(Text_Input_State *state, Text_Input_Theme *theme, u32 status_flags, f32 dt)
{
    if (status_flags & Status_Flags::OVER)
    {
        state->over_effect_t = move_toward(state->over_effect_t, 1, dt * theme->over_fade_in_speed);
        state->over_duration += dt;
    }
    else
    {
        state->over_effect_t = move_toward(state->over_effect_t, 0, dt * theme->over_fade_out_speed);
        if (!state->over_effect_t) state->over_duration = 0;
    }

    if (status_flags & Status_Flags::DOWN)
    {
        state->down_effect_t = move_toward(state->down_effect_t, 1, dt * theme->down_fade_in_speed);
        state->down_duration += dt;
    }
    else
    {
        state->down_effect_t = move_toward(state->down_effect_t, 0, dt * theme->down_fade_out_speed);
        if (!state->down_effect_t) state->down_duration = 0;
    }
}

my_pair<f32 /*over_factor*/, f32 /*pressed_flash_factor*/> update_production_value_region(Rect r, bool changed, Text_Input_State *state, u32 status_flags, Text_Input_Theme *theme)
{
    assert(theme);

    auto dt = timez.ui_dt;
    update_over_state(state, theme, status_flags, dt);

    auto pressed_factor = update_action_durations(state, theme, changed, dt);

    auto blend_factor = sinf(TAU * state->over_duration * .5f);
    blend_factor += 1;
    blend_factor *= .5f;
    Clamp(&blend_factor, 0.0f, 1.0f);

    constexpr auto STROBE_BASE   = .6f;
    constexpr auto STROBE_HEIGHT = .4f;

    auto base = STROBE_BASE * state->over_effect_t;
    auto over_factor = base + state->over_effect_t * blend_factor * .4f;

    return {over_factor, pressed_factor};
}

i32 get_insert_point_for_cursor(Dynamic_Font *font, String text, f32 text_x, i32 mouse_x, bool glyphs_prepped = false)
{
    // @Temporary: Assume the number of glyphs is equal
    // to the number of characters. Not necessarily true.
    // This ALSO assumes that 'text' is only single-byte characters,
    // which won't last long. But let's just get this working.
    // Eventually, we should expand 'text' to an array of utf32
    // before rendering.

    if (!glyphs_prepped)
    {
        prepare_text(font, text);
    }

    f32 best_dist = FLT_MAX;

    auto x = text_x;
    u32 best_utf32;

    // Doing first pass to get the best distance.
    i64 glyph_index = 0;
    i64 best_index;
    for (auto glyph : font->temporary_glyphs)
    {
        auto dist = fabsf(mouse_x - x);
        if (dist < best_dist)
        {
            best_dist  = dist;
            best_utf32 = glyph->utf32;
            best_index = glyph_index;
        }

        // Once dist goes past the minimum and starts increasing,
        // we could early out, but we are not doing that yet.
        x += glyph->advance;
        glyph_index += 1;
    }

    // Doing second pass to get the byte offset.
    auto best_byte_offset = -1;

    // Early out if we didn't find a thing:
    if (best_dist == FLT_MAX) return best_byte_offset;

    {
        auto t = text.data;
        i64 it_index = 0;
        while (t < (text.data + text.count))
        {
            auto utf32 = character_utf8_to_utf32(t, text.data + text.count - t);

            if (utf32 && (utf32 == best_utf32) && (it_index == best_index))
            {
                best_byte_offset = t - text.data;
                break;
            }

            t = unicode_next_character(t);
            it_index += 1;
        }
        assert((best_byte_offset != -1)); // Byte offset must be found if we are here.
    }

    // Allow ourselves to click beyond the last character.
    auto dist = fabsf(mouse_x - x);
    if (dist < best_dist) best_byte_offset = text.count;

    return best_byte_offset;
}

my_pair<f32 /*x0*/, f32 /*x1*/> get_selection_endpoints(Dynamic_Font *font, String text, f32 text_x, i32 start_character, i32 end_character)
{
    auto s_up_to_start = text;
    s_up_to_start.count = start_character;

    auto s_up_to_end = text;
    s_up_to_end.count = end_character;

    auto start_width = get_text_width(font, s_up_to_start);
    auto end_width   = get_text_width(font, s_up_to_end);

    f32 sx0 = text_x + start_width;
    f32 sx1 = text_x + end_width;

    return {sx0, sx1};
}

inline
bool non_empty(Selection selection)
{
    return selection.active && (selection.start_character != selection.end_character);
}

Vector4 get_cursor_color(Text_Input *input, Text_Input_Theme *theme, Vector4 non_white)
{
    auto white = theme->cursor_color;

    auto t = cosf((timez.ui_time - input->last_keypress_time) * 3.0f);
    t *= t;

    auto color = lerp(non_white, white, t);
    return color;
}

void draw_high_score_name_input(Rect r, Dynamic_Font *font)
{
    assert(font);

    auto mark = get_temporary_storage_mark();
    defer { set_temporary_storage_mark(mark); };

    auto input = &high_score_name_input;
    auto state = &text_input_state;
    auto theme = &default_text_input_theme;

    state->rect  = r;
    state->input = input;

    // @Incomplete: Since we assume that there is only one input in the game, we don't
    // care about active widgets.
    // defer { stop_using_state(&state->widget); };

    // if (input->do_active_widget_add)
    // {
    //     if (input->active)
    //     {
    //         active_widget_add(&state->widget);
    //     }
    //     input->do_active_widget_add = false;
    // }
    // else if (input->do_active_widget_deactivate_all)
    // {
    //     active_widget_deactivate_all(&state->widget);
    //     input->do_active_widget_deactivate_all = false;
    // }

    clamp_insert_point(input);

    auto x0 = static_cast<i32>(r.x);
    auto y0 = static_cast<i32>(r.y);
    auto x1 = static_cast<i32>(r.x + r.w);
    auto y1 = static_cast<i32>(r.y + r.h);

    auto xpad = em(font, theme->alignment_pad_ems);
    push_scissor(r);
    defer { pop_scissor(); };

    auto status_flags = get_status_flags(r);

    // @Note: We are using update_production_value button for some factors, but,
    // we interpolate 'active_t' on our own.
    bool changed = status_flags & Status_Flags::PRESSED;

    if (input->active) state->active_t = move_toward(state->active_t, 1, theme->active_fade_in_speed);
    else               state->active_t = move_toward(state->active_t, 0, theme->active_fade_out_speed);

    auto [over_factor, flash_factor] = update_production_value_region(r, changed, state, status_flags, theme);
    auto active_factor = state->active_t;

    auto text_color = lerp(theme->text_color, theme->text_color_over,   over_factor);
    text_color      = lerp(text_color,        theme->text_color_active, active_factor);

    auto background_color = lerp(theme->background_color, theme->background_color_over,   over_factor);
    background_color      = lerp(background_color,        theme->background_color_active, active_factor);
    background_color      = lerp(background_color,        theme->background_color_flash,  flash_factor);

    auto text_width = get_text_width(font, input->text);

    auto text_x = x0 + xpad;
    auto text_y = y0 + theme->text_baseline_vertical_position * r.h;

    // Left alignment.
    text_x = x0 + xpad;

    if (!input->active) state->camera_initted = false;
    if (!state->camera_initted)
    {
        state->camera_initted = true;
        state->camera_x = -text_x + x0;
    }

    if (input->active)
    {
        if (input->cursor_tracking_mouse)
        {
            // Update cursor position based on mouse drag;
            // do this before any other logic involving the insert point,
            // and before drawing the cursor!

            auto now = timez.ui_time;
            input->last_keypress_time = now;

            //
            // We need to figure out where in the text the user clicked.
            // We used to do this after rendering the text, so we knew for sure wher it was.
            // But this was a little more laggy with regard to where the cursor appears (one
            // frame of lag), and it was not helpful with regard to the new deadzone code :Deadzone.
            // Nowe we do this before drawing; we need to know where the text was last frame..
            // Despite this, it is not right @Robustness @Temporary because if the user-level code
            // is drawing this text input at different coordinates and the user start selecting
            // at the same time than it was drawn last frame, it will be wrong (quite hard to do honestly).
            //
            // This is the reason why we store last_displayed_text_x, which is slightly scary but not so much.
            auto last_text_x = state->last_displayed_text_x;
            if (last_text_x == FLT_MAX) last_text_x = text_x;

            // This call to get_insert_point_for_cursor will generate all the glyphs to the temporary glyphs array,
            // but this array will be re-generated again later when we do draw_text() below so it is not efficient
            // right now. @Speed:
            auto [mouse_x_float, mouse_y_float] = render_target_mouse_pointer_position(glfw_window, true);
            input->insert_point = get_insert_point_for_cursor(font, input->text, last_text_x, mouse_x_float, false);

            if (input->selection.active)
            {
                input->selection.end_character = input->insert_point;
            }
            else
            {
                input->selection.start_character = input->insert_point;
                input->selection.end_character   = input->insert_point;
                input->selection.active          = true;
            }
        }        

        // Find the x coordinate of the insert point.
        auto s_up_to_insert     = input->text;
        s_up_to_insert.count    = input->insert_point;
        auto width_up_to_insert = get_text_width(font, s_up_to_insert);

        auto margin = em(font, theme->text_insert_margin);
        Clamp(&margin, 0.0f, .49f * r.w);

        auto alignment_pad = xpad;
        Clamp(&alignment_pad, 0.0f, .49f * r.w);

        auto insert_x = (x0 - state->camera_x) + width_up_to_insert;
        auto deadzone_x0 = x0 + margin;
        auto deadzone_x1 = x1 - margin;

        //
        //
        // :Deadzone
        //
        // In general when the user is moving the cursor around, inside text that is wider than the rect 
        // provided so that only a portion of the text can be seen at a time, we want to have some margin
        // around the cursor, so that you can see past it on the right or left.
        // ^ This is what the 'text_insert_margin' is for.
        //
        // The deadzone area consists of:
        // - The area starting from the beginning of the text input rect and
        //   ends at the left 'text_insert_margin'.
        // - The area starting from the right 'text_insert_margin' and
        //   ends at the right edge of the text input rect.
        // Note that the margin for the deadzones could change when you click inside them.
        //
        // The behavior of the text input is as follows:
        // - When you click on the text input, the cursor will jump to the position clicked. And the margin
        //   will be set to the point at which you clicked.
        // - When you drag the selection, the cursor will go along with the mouse position, and this will
        //   obey the margin previously set.
        //
        // In the case of clicking inside the deadzone area. The margin of the deadzone will change to match
        // where you clicked as mentioned above. This will be resetted to 'text_insert_margin' when you either:
        // - Click back inside           the non-deadzone area (the middle main area).
        // - Move the cursor back inside the non-deadzone area (the middle main area).
        //
        if (input->did_initial_mouse_click_so_check_for_deadzone_change)
        {
            input->did_initial_mouse_click_so_check_for_deadzone_change = false;

            // @Robustness: We are using the mouse position from this frame,
            // not the actual position of the input event, which is going
            // to be a differenet value, maybe..
            if ((insert_x < deadzone_x0) || (insert_x > deadzone_x1))
            {
                state->shrunken_deadzone_margin = 0; // Dummy value >= 0 to kick us into shrink mode.
            }
        }

        // Not moving the camera pane when click, on when drag or moving by the keyboard
        if (state->shrunken_deadzone_margin >= 0)
        {
            // If the shrunken margin got bigger, so that it approchaes the real margin again,
            // let it get bigger. If it meets or exceeds the size of the preferred margin, disable shrinking.
            if (insert_x < deadzone_x0)
            {
                auto new_margin = insert_x - x0;
                state->shrunken_deadzone_margin = std::max(state->shrunken_deadzone_margin, new_margin);
            }
            else if (insert_x > deadzone_x1)
            {
                auto new_margin = x1 - insert_x;
                state->shrunken_deadzone_margin = std::max(state->shrunken_deadzone_margin, new_margin);
            }

            if (state->shrunken_deadzone_margin >= margin)
            {
                // No need to shrink the margin anymore. Turn off shrink mode.
                state->shrunken_deadzone_margin = -1; // -1 means disable shrinking mode.
            }
            else
            {
                // Recompute the margin and deadzone.
                deadzone_x0 = x0 + state->shrunken_deadzone_margin;
                deadzone_x1 = x1 - state->shrunken_deadzone_margin;
            }
        }

        auto dx = 0.0f;
        if (insert_x < deadzone_x0)
        {
            dx = deadzone_x0 - insert_x;
        }
        else if (insert_x > deadzone_x1)
        {
            dx = deadzone_x1 - insert_x;
        }

        state->camera_x -= dx;

        auto left_limit  = -alignment_pad;
        auto right_limit = text_width + alignment_pad - (x1 - x0);

        if (right_limit < left_limit) right_limit = left_limit;

        Clamp(&state->camera_x, left_limit, right_limit);
        text_x = x0 - state->camera_x;
        state->last_displayed_text_x = text_x;
    }

    rendering_2d_right_handed();
    set_shader(shader_argb_no_texture);

    if (background_color.w)
    {
        Vector2 p0, p1, p2, p3;
        get_quad(r, &p0, &p1, &p2, &p3);

        immediate_quad(p0, p1, p2, p3, argb_color(background_color));
    }

    // Draw the selection if it is active.
    if (input->active && non_empty(input->selection))
    {
        auto [sx0, sx1] = get_selection_endpoints(font, input->text, text_x, input->selection.start_character, input->selection.end_character);

        auto sy0 = static_cast<f32>(y0);
        auto sy1 = static_cast<f32>(y1);

        auto p0 = Vector2(sx0, sy0);
        auto p1 = Vector2(sx1, sy0);
        auto p2 = Vector2(sx1, sy1);
        auto p3 = Vector2(sx0, sy1);

        immediate_quad(p0, p1, p2, p3, argb_color(theme->selection_color));
    }

/* @Incomplete: Maybe we want autocomplete hints for the names input?
    if (input->active)
    {
        // Draw auto complete hints.
        if (input->match_array)
        {
            auto auto_complete_text  = input->match_array[input->match_selection];
            auto auto_complete_color = theme->text_color_auto_complete;

            if (input->tab_pressed)
            {
                auto center = 0.5f;
                auto stride = 0.24f;

                auto now   = timez.ui_time;
                auto theta = 9 * (now - input->completion_change_time);

                // If we are in the first quadrant TAU/4, fake up our stride so that
                // we are brighter for longer.
                if (theta < (TAU * .25f))
                {
                    stride *= 6;
                }

                auto w = cosf(theta) * stride + center;
                Clamp(&w, 0.0f, 1.0f);

                auto_complete_color.w = w;
            }

            // Skip the part of the auto_complete_text that overlaps with text?
            // @Incomplete: Need to customize auto complete colors.
            draw_text(font, static_cast<i32>(text_x), static_cast<i32>(text_y), auto_complete_text, auto_complete_color);
        }
    }

    if ((input->longest_match >= 0) && (input->longest_match < input->text.count))
    {
        auto failed_color = theme->text_color_auto_complete_failed;

        draw_text(font, static_cast<i32>(text_x), static_cast<i32>(text_y), input->text, failed_color);

        auto s = input->text;
        s.count -= s.count - input->longest_match;
        draw_text(font, static_cast<i32>(text_x), static_cast<i32>(text_y), s, text_color);
    }
    else
    {
        draw_text(font, static_cast<i32>(text_x), static_cast<i32>(text_y), input->text, text_color);
    }
*/

    draw_text(font, static_cast<i32>(text_x), static_cast<i32>(text_y), input->text, text_color);

    // Don't draw cursor if not active.
    if (!input->active) return;

    auto cursor_color = get_cursor_color(input, theme, text_color);

    // Draw the cursor
    {
        auto s_up_to_insert  = input->text;
        s_up_to_insert.count = input->insert_point;

        auto width = get_text_width(font, s_up_to_insert);
        auto cursor_x = text_x + width;

        // Do not draw wide cursor over auto complete hint.
        auto max_text_count = input->text.count;
        // if (input->match_array) max_text_count = input->match_array[input->match_selection].count; @Incomplete:

        auto cw = em(font, theme->cursor_width_outside_text);
        if (input->insert_point < max_text_count)
        {
            cw = em(font, theme->cursor_width_inside_text);
            cursor_x -= cw * .5f;
        }

        rendering_2d_right_handed();
        set_shader(shader_argb_no_texture);
        {
            auto x0 = cursor_x;
            auto x1 = x0 + cw;
            auto [y0, y1] = get_y_range(font, text_y);

            y0 = y1 - font->character_height;

            auto p0 = Vector2(x0, y0);
            auto p1 = Vector2(x1, y0);
            auto p2 = Vector2(x1, y1);
            auto p3 = Vector2(x0, y1);

            immediate_begin();
            immediate_quad(p0, p1, p2, p3, argb_color(cursor_color));
            immediate_flush();
        }
    }
}

// @Cleanup: Move this along with the unicode functions inside font.cpp so another file:
String character_utf32_to_utf8(u32 utf32) // Temporary storage.
{
    // @Note: We swap the order around because memcpy is little-endian on our machine so it swaps around the bytes.
    u32 utf8;
    i64 nbytes = 0; // In order to allocate the string with the same size.

    if (utf32 < 0x80)
    {
        utf8 = utf32;
        nbytes = 1;
    }
    else if (utf32 < 0x800)
    {
        utf8 = ((0x80 | (utf32 & 0x3f)) << 8)
            | (0xc0 | (utf32 >> 6));

        nbytes = 2;
    }
    else if (utf32 < 0x10000)
    {
        utf8 = ((0x80 | (utf32 & 0x3f)) << 16)
            | ((0x80 | ((utf32 >> 6) & 0x3f)) << 8)
            | (0xe0 | (utf32 >> 12));

        nbytes = 3;
    }
    else if (utf32 < 0x200000)
    {
        utf8 = ((0x80 | (utf32 & 0x3f)) << 24)
            | ((0x80 | ((utf32 >>  6) & 0x3f)) << 16)
            | ((0x80 | ((utf32 >> 12) & 0x3f)) <<  8)
            | (0xf0 | (utf32 >> 18));

        nbytes = 4;
    }

    auto result = talloc_string(nbytes);

    auto dest = result.data;
    auto src  = reinterpret_cast<u8*>(&utf8);
    memcpy(dest, src, nbytes);

    return result;
}

void set_input(Text_Input *input, String s)
{
    assert(input->initted);

    if (s.count >= input->input_buffer.count) s.count = input->input_buffer.count - 1; // Should we even do the -1 for the cursor

    u8 *dest = input->input_buffer.data;
    u8 *src  = s.data;
    memcpy(dest, src, s.count);

    input->text.count   = s.count;
    input->insert_point = s.count;

//    update_auto_complete(input);
}

my_pair<i32 /* start */, i32 /* end */> get_selection_indices(Text_Input *input)
{
    // Maybe we should just have a canonical version of these stored on 'selection'.
    auto start = input->selection.start_character;
    Clamp(&start, 0, static_cast<i32>(input->text.count));

    auto end = input->selection.end_character;
    Clamp(&end, 0, static_cast<i32>(input->text.count));

    if (end < start)
    {
        swap_elements(&start, &end);
    }

    return {start, end};
}

void replace_selection(Text_Input *input, String s)
{
    auto [start, end] = get_selection_indices(input);

    auto pre = input->text;
    pre.count = start;

    auto post = input->text;
    advance(&post, end);

    auto new_text = join(3, pre, s, post);
    set_input(input, new_text);

    if (!s.count)
    {
        input->insert_point     = start;
        input->selection.active = false;
    }
    else
    {
        input->selection.start_character = start;
        input->selection.end_character   = start + s.count;
    }
}

bool insert_text(Text_Input *input, String utf8_text)
{
    // If not enough room, none of the text is inserted. Usually called on character
    // at a time by the text input's handler.

    if ((input->text.count + utf8_text.count) > input->input_buffer.count) return false; // Not enough room!

    // Delete the current selected block if we are typing new stuff.
    if (non_empty(input->selection))
    {
        replace_selection(input, String(""));
    }

    clamp_insert_point(input);

    if (input->insert_point < input->text.count)
    {
        // Copy right-to-left so that we don't wipe out or own text
        // for small inserts.
        auto i = input->text.count;
        while (i > input->insert_point)
        {
            input->input_buffer[i] = input->input_buffer[i - utf8_text.count];
            i -= 1;
        }
    }

    memcpy(input->input_buffer.data + input->insert_point, utf8_text.data, utf8_text.count);

    input->text.count   += utf8_text.count;
    input->insert_point += utf8_text.count;
//    update_auto_complete(input); @Incomplete: No autocomplete.

    return true;
}

i32 seek_left_one_utf8_character(String s, i32 _point)
{
    auto point = _point - 1;
    while (point > 0)
    {
        auto c = s[point];
        if ((192 & c) != 128) return point;
        point -= 1;
    }

    return 0;
}

//
// Below are the text editing routines mapped to different hotkeys.
// This is so that we can call them from our own code without having
// to go through the UI to do so.
//

inline
void input_proc_backspace_from_cursor(Text_Input *input)
{
    if (non_empty(input->selection))
    {
        replace_selection(input, String(""));
    }
    else if ((input->insert_point <= input->text.count) &&
             (input->insert_point >= 1))
    {
        auto prev_point = seek_left_one_utf8_character(input->text, input->insert_point);
        auto nbytes = input->insert_point - prev_point;

        for (i64 i = prev_point; i < (input->text.count - nbytes); ++i)
        {
            input->text.data[i] = input->text[i + nbytes];
        }

        input->insert_point -= nbytes;
        input->text.count   -= nbytes;
//        update_auto_complete(input); @Incomplete:
    }
}

inline
void input_proc_select_all(Text_Input *input)
{
    auto selection = &input->selection;
    selection->active          = true;
    selection->start_character = 0;
    selection->end_character   = input->text.count;
}

String get_selection(Text_Input *input) // Temporary Storage
{
    if (!input->selection.active) return String("");

    auto [start, end] = get_selection_indices(input);

    String result = talloc_string(end - start);
    memcpy(result.data, input->text.data + start, result.count);

    return result;
}

inline
void input_proc_copy_selection_to_clipboard(Text_Input *input, bool cut_selection)
{
    if (non_empty(input->selection))
    {
        auto sel = get_selection(input); // This does not make new memory.
        os_clipboard_set_text(sel);

        if (cut_selection) replace_selection(input, String(""));
    }
}

inline
void input_proc_insert_clipboard_at_cursor(Text_Input *input)
{
    auto pasted_text = os_clipboard_get_text();

    // Sanitize the pasted text.
    for (i64 i = 0; i < pasted_text.count; ++i)
    {
        if (pasted_text[i] == '\n') pasted_text.data[i] = ' ';
    }

    if (non_empty(input->selection))
    {
        replace_selection(input, String(""));
    }

    if ((input->text.count + pasted_text.count) <= input->input_buffer.count)
    {
        auto pre  = input->text;
        pre.count = input->insert_point;

        String post;
        post.data  = input->text.data  + input->insert_point;
        post.count = input->text.count - input->insert_point;

        auto combined = join(3, pre, pasted_text, post);

        set_input(input, combined);

        input->insert_point = pre.count + pasted_text.count;
    }
}

inline
void input_proc_delete_from_cursor(Text_Input *input)
{
    if (non_empty(input->selection))
    {
        replace_selection(input, String(""));
    }
    else if (input->insert_point < input->text.count)
    {
        auto nbytes = utf8_char_length(input->text[input->insert_point]);
        for (i64 i = input->insert_point; i < (input->text.count - nbytes); ++i)
        {
            input->text.data[i] = input->text[i + nbytes];
        }

        input->text.count -= nbytes;
//        update_auto_complete(input); @Incomplete: No autocomplete.
    }
}

inline
void reset_or_begin_selection(Text_Input *input, bool do_begin_select)
{
    auto selection = &input->selection;
    if (do_begin_select)
    {
        if (!selection->active)
        {
            auto insert_point = input->insert_point;

            selection->active = true;
            selection->start_character = insert_point;
            selection->end_character   = insert_point;
        }
    }
    else
    {
        selection->active = false;
    }
}

inline
void update_selection_after_cursor_movement(Text_Input *input)
{
    auto selection = &input->selection;
    if (selection->active) selection->end_character = input->insert_point;
}

inline
void input_proc_move_cursor_left(Text_Input *input, bool do_selection)
{
    reset_or_begin_selection(input, do_selection);

    auto prev_point = seek_left_one_utf8_character(input->text, input->insert_point);
    input->insert_point = prev_point;
    clamp_insert_point(input);

    update_selection_after_cursor_movement(input);
}

inline
void input_proc_move_cursor_right(Text_Input *input, bool do_selection)
{
    reset_or_begin_selection(input, do_selection);

    if (input->insert_point < input->text.count)
    {
        auto nbytes = utf8_char_length(input->text[input->insert_point]);
        input->insert_point += nbytes;
        clamp_insert_point(input);
    }

    update_selection_after_cursor_movement(input);
}

inline
void input_proc_move_cursor_to_point(Text_Input *input, bool do_selection, i32 to_point)
{
    reset_or_begin_selection(input, do_selection);
    input->insert_point = to_point;
    update_selection_after_cursor_movement(input);
}

inline
void input_proc_press_enter(Text_Input *input, bool shift_pressed = false)
{
    auto handled = false;
/*
    if (input->tab_pressed)
    {
        handled = accept_auto_complete(input);
    }
*/

    input->entered = true;
    input->shift_plus_enter_was_pressed = shift_pressed;

/*
    if (input->text)
    {
        auto copy = copy_string(input->text); // @Investigate because this is a potential @Leak.
        array_add(&input->command_history, copy); // Don't add blank lines.
    }
*/

//    input->command_history_index += 1;
}

inline
void deselect(Selection *selection)
{
    selection->active = false;
}

inline
void start_selecting(Text_Input *input)
{
    deselect(&input->selection);
    input->cursor_tracking_mouse = true;

    // @Robustness: We do not have access to the hash for the widget here,
    // because that is only available in draw (because it depends on the source
    // code location). We could save the last hash or something... but, without that
    // we cannot do any computations hereabout the deadzones. So we set a boolean flag
    // that indicates that we should do the computation in the draw procedure (it would
    // be preferred if we could set this flag on the state struct).

    // In general, this shows that the UI has both immediate mode and retained mode aspect.
    // This is apt to change.
    input->did_initial_mouse_click_so_check_for_deadzone_change = true;
}

void handle_high_score_input_event(Event event)
{
    auto input = &high_score_name_input;
    if (!input->initted) init_high_score_input();

    assert(input->active);
/* @Incomplete: Do we want this input to always be active? I guess so...
    if (!input->active)
    {
        check_for_activating_event(input, event);
        return;
    }
*/

    auto now = timez.ui_time;
    if (event.type == EVENT_TEXT_INPUT)
    {
        // input->do_active_widget_deactivate_all = true;

        auto key = event.utf32;
        auto utf8_text = character_utf32_to_utf8(key);

        insert_text(input, utf8_text);
    }
    else if (event.type == EVENT_KEYBOARD)
    {
        // input->do_active_widget_deactivate_all = true;

        input->last_keypress_time = now;

        auto key     = event.key_code;
        auto pressed = static_cast<bool>(event.key_pressed);

        if (pressed)
        {
            if (event.ctrl_pressed)
            {
                switch (key)
                {
                    case CODE_A:      input_proc_select_all(input); break;
                    case CODE_C:
                    case CODE_INSERT: input_proc_copy_selection_to_clipboard(input, false); break;
                    case CODE_X:      input_proc_copy_selection_to_clipboard(input, true); break;
                    case CODE_V:      input_proc_insert_clipboard_at_cursor(input); break;
                }
            }

            if (event.shift_pressed)
            {
                switch (key)
                {
                    case CODE_DELETE: input_proc_copy_selection_to_clipboard(input, true); break;
                    case CODE_INSERT: input_proc_insert_clipboard_at_cursor(input); break;
                }
            }

            switch (key)
            {
                case CODE_ARROW_LEFT:  input_proc_move_cursor_left(input, event.shift_pressed); break;
                case CODE_ARROW_RIGHT: input_proc_move_cursor_right(input, event.shift_pressed); break;
                case CODE_HOME:        input_proc_move_cursor_to_point(input, event.shift_pressed, 0); break;
                case CODE_END:         input_proc_move_cursor_to_point(input, event.shift_pressed, input->text.count); break;
                // case CODE_ARROW_DOWN:  input_proc_navigate_history_forward(input); break;
                // case CODE_ARROW_UP:    input_proc_navigate_history_backward(input); break;
                case CODE_BACKSPACE:   input_proc_backspace_from_cursor(input); break;
                case CODE_DELETE:      input_proc_delete_from_cursor(input); break;
                case CODE_ESCAPE:      input->escaped = true; break;
                case CODE_ENTER:       input_proc_press_enter(input, event.shift_pressed); break;
//                case CODE_TAB:         input_proc_press_tab(input, event.shift_pressed); break;
                case CODE_MOUSE_LEFT: {
                    auto state = input->text_input_state;
                    assert(state);
                    auto status = get_status_flags(state->rect);
                    if (status & Status_Flags::OVER)
                    {
                        start_selecting(input);
                    }
                } break;
            }
        }
        else // If not pressed anything.
        {
            if (key == CODE_MOUSE_LEFT)
            {
                input->cursor_tracking_mouse = false;
            }
        }
    }
}





// @Incomplete Missing the auto complete.
#if 0

inline
i32 get_match_length(RArr<String> match_array, i32 n)
{
    if (!match_array.count)     return 0;
    if (match_array.count == 1) return match_array[0].count;

    n -= 1;
    while (true)
    {
        n += 1;
        if (n == match_array[0].count) break;
        auto c = match_array[0][n];

        // All the matches in match_array have the same from 0 to n-1
        auto all_match = true;

        for (auto cmd : match_array)
        {
            if ((n >= cmd.count) || (cmd[n] != c))
            {
                all_match = false;
                break;
            }
        }

        if (!all_match) break;
    }

    return n;
}

void reset_auto_complete(Text_Input *input)
{
    array_reset(&input->match_array); // Avoid leaking.

    if (input->auto_complete)
    {
        if (input->text)
        {
            auto longest_match = input->auto_complete(input->text, input->auto_complete_data, &input->match_array);
            input->longest_match = longest_match;

            input->match_length = get_match_length(input->match_array, input->text.count);
            input->match_selection = 0;

            // If our text is an exact match for array[0], then one of the completions
            // is our exact string .. but don't use that as the first completion, since
            // we want to give the user some visual information that other completions
            // exists. So just rotate our selection to the next one.
            if ((input->match_array.count > 1) && (input->text == input->match_array[0]))
            {
                input->match_selection += 1;
            }
        }
    }
}

// Update matches while trying to preserve the current selection.
void update_auto_complete(Text_Input *input)
{
    input->tab_pressed = false;

    if (input->match_array)
    {
        // Try to preserve previous selection.
        auto last_match = input->match_array[input->match_selection];

        reset_auto_complete(input);

        // Restore previous selection.
        i64 it_index = 0;
        for (auto it : input->match_array)
        {
            if (it == last_match) input->match_selection = it_index;
            it_index += 1;
        }
    }
    else
    {
        // There was no selection, just auto complete.
        reset_auto_complete(input);
    }
}

void set_input(Text_Input *input, String s);

// Tab auto completes up to match_length.
void advance_auto_complete(Text_Input *input, bool reverse)
{
    if (input->match_array)
    {
        auto new_text = input->match_array[input->match_selection];
        new_text.count -= new_text.count - input->match_length;  // @Investigate :Autocomplete

        if (input->text != new_text)
        {
            set_input(input, new_text);
        }
        else
        {
            if (reverse)
            {
                input->match_selection -= 1;
                if (input->match_selection < 0) input->match_selection = input->match_array.count;
            }
            else
            {
                input->match_selection += 1;
                if (input->match_selection == input->match_array.count) input->match_selection = 0;
            }
        }
    }
}

// Accept the currently selected match. Returns false if there's nothing to accept, or already accepted.
bool accept_auto_complete(Text_Input *input)
{
    if (!input->match_array) return false;
    if (input->match_array[input->match_selection] == input->text) return false;

    set_input(input, input->match_array[input->match_selection]);

    return true;
}

void set_input(Text_Input *input, String s)
{
    if (!input->initted) init(input);

    if (s.count >= input->input_buffer.count) s.count = input->input_buffer.count - 1; // Should we even do the -1 for the cursor

    u8 *dest = input->input_buffer.data;
    u8 *src  = s.data;
    memcpy(dest, src, s.count);

    input->text.count   = s.count;
    input->insert_point = s.count;

    update_auto_complete(input);
}

void refresh_input_from_history(Text_Input *input)
{
    Clamp(&input->command_history_index, 0, static_cast<i32>(input->command_history.count));

    if (input->command_history_index == input->command_history.count)
    {
        set_input(input, input->last_saved_input);
    }
    else
    {
        set_input(input, input->command_history[input->command_history_index]);
    }
}

void activate(Text_Input *input)
{
    input->active = true;
}

void deactivate(Text_Input *input)
{
    input->active = false;
}

// bool is_active(Text_Input *input) return input->active;
// String get_result(Text_Input *input) return input->text;

void reset(Text_Input *input)
{
    deselect(&input->selection);
    init(input);

    activate(input);
    input->entered = false;
    input->escaped = false;
    input->shift_plus_enter_was_pressed = false;
    input->cursor_tracking_mouse = false;
}

void append_text(Text_Input *input, String text)
{
    auto bytes_available = input->input_buffer.count - input->text.count;
    auto nbytes = std::min(bytes_available, text.count);

    u8 *dest = input->input_buffer.data;
    u8 *src  = text.data;
    memcpy(dest + input->text.count, src, nbytes);
    input->text.count   += nbytes;
    input->insert_point += nbytes; // @Bug @Hack: Assume insert_point is at the end. If we want this to be more robust, we could just insert at the point here as we do when pasting.

    update_auto_complete(input);
}

String get_selection(Text_Input *input); // Temporary Storage
void replace_selection(Text_Input *input, String s);
bool non_empty(Selection selection);

// @Cleanup or @Fixme: Unused...
void start_text_input(Text_Input *input, String text)
{
    // reset_all_text_inputs(input); // @Incomplete: Will be from editor.cpp
    reset(input);
    input->active             = true;
    input->auto_complete      = NULL;
    input->auto_complete_data = NULL;
    // array_reset(&input.match_array); // ????

    if (text != String("(null)")) // @Hardcoded:
    {
        append_text(input, text);
    }
}

inline
void input_proc_navigate_history_backward(Text_Input *input)
{
    // Handle auto complete navigation.
    input->selection.active = false;
    if (input->command_history_index == input->command_history.count)
    {
        if (input->last_saved_input) free_string(&input->last_saved_input);
        input->last_saved_input = copy_string(input->text);
    }

    input->command_history_index -= 1;
    refresh_input_from_history(input);
}

inline
void input_proc_navigate_history_forward(Text_Input *input)
{
    // Handle auto complete navigation.
    input->selection.active = false;

    if (input->command_history_index < input->command_history.count)
    {
        // Only do this if we're not at the end, because
        // we only save your input text when you leave
        // the last line.
        input->command_history_index += 1;
        refresh_input_from_history(input);
    }
}

inline
void input_proc_press_tab(Text_Input *input, bool shift_pressed = false)
{
    input->completion_change_time = timez.ui_time;

    if (!input->tab_pressed)
    {
        input->tab_pressed = true; // @Fixme: I don't see where we reset the tab_pressed ??????
    }
    else
    {
        auto reverse = shift_pressed;
        advance_auto_complete(input, reverse);
        input->tab_pressed = true; // This must happen after advance_auto_complete since that resets it indirectly.
    }
}

void using_state_to_check_for_activating_text_input(Text_Input_State *text_input_state, Event event)
{
    auto text_input = text_input_state->input;
    if (!text_input) return;

    check_for_activating_event(text_input, event);
}

void check_for_activating_event(Text_Input *input, Event event)
{
    if (!input->initted) return; // Nothing to handle.

    auto state = input->text_input_state;
    if (!state) return;

    //
    // We get here when we are not active, but need to check for a click
    // to see if we should become active.
    //
    if (event.type != EVENT_KEYBOARD) return;
    if (!event.key_pressed) return;
    if (event.key_code != CODE_MOUSE_LEFT) return;

    auto rect = state->rect;
    auto status = get_status_flags(rect);

    if (status & Status_Flags::OVER)
    {
        if (!input->active) // Deactivate everyone, including us if we are not active.
        {
            active_widget_deactivate_all();
        }

        activate(input);
        
        start_selecting(input);

        input->do_active_widget_add = true; // Add us to be the active widget.
    }
}

/*
Selection make_forward_selection(Selection old)
{
    auto swap = false;
    if (old.start_line > old.end_line)
    {
        swap = true;
    }
    else if (old.start_line == old.end_line)
    {
        if (old.start_character > old.end_character)
        {
            swap = true;
        }
    }

    if (!swap) return old;

    auto result = old;
    swap_elements(&result.start_line, &result.end_line);
    swap_elements(&result.start_character, &result.end_character);

    return result;
}
*/

void set_auto_complete(Text_Input *input, Auto_Complete proc, void *auto_complete_data)
{
    if (!input->initted) init(input);

    input->auto_complete      = proc;
    input->auto_complete_data = auto_complete_data;

    reset_auto_complete(input);
}

// Despite having rougly the same functionalities as the other similarly named procedures, these
// are not supposed to be called at the user-level. (They are used in ui_states.h)
void this_deactivate_is_not_the_same_as_the_normal_text_input_deactivate(Active_Widget *widget)
{
    auto state = CastDown_Widgets<Text_Input_State>(widget);

    auto input = state->input;
    if (input) input->active = false;
}

void this_handle_event_is_not_the_same_as_the_normal_text_input_handle_event(Active_Widget *widget, Event event)
{
    auto state = CastDown_Widgets<Text_Input_State>(widget);

    auto input = state->input;

    if (input)
    {
        handle_event(input, event);
    }
}

#endif
