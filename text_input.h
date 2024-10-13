#pragma once

#include "common.h"
// #include "source_location.h"

struct Rect
{
    f32 x = 0, y = 0;
    f32 w = 0, h = 0;
};

struct Selection
{
    bool active = false;

    // These are all indices:
    i32 start_line      = -1;
    i32 start_character = 0;

    i32 end_line      = -1;
    i32 end_character = 0;
};

struct Text_Input;
struct Text_Input_State
{
    f32 over_duration = 0.0f;
    f32 over_effect_t = 0.0f;

    f32 down_duration = 0.0f;
    f32 down_effect_t = 0.0f;

    f32 action_duration = -1.0f; // Set to 0 when an action first happens.
    f32 action_duration_2 = -1.0f; // Set to 0 when an action first happens.

    // bool pressed  = false;  // @Cleanup: This flag handling stuff need not be this complicated.
    // bool released = false;  // @Cleanup: This flag handling stuff need not be this complicated.    

    // In text input, I think of the text input region as a camera that slides left/right
    // to follow the movement of the cursor. This is because we want to have an easy notion
    // to visualize the movement of the text input and how to clamp things to the margin.
    bool camera_initted = false;
    f32 camera_x = 0.0f; // This is the left edge of the camera slider pane.

    f32 shrunken_deadzone_margin = -1.0f;
    f32 last_displayed_text_x = FLT_MAX; // FLT_MAX means disabled.

    f32 active_t = 0.0f;

    Text_Input *input = NULL; // Becase we are handling all events of text input within ui_handle_event, we need a way to convert from the states in the states table to the Text_Input themselves, so we have to store a field of Text_Input inside its state. This may not be the best solution but it is what I'm thinking of.

    Rect rect; // The region of the text input in order to handle events (see ui_handle_event).
};

struct Text_Input
{
    String text;

    bool entered       = false;
    bool escaped       = false;
    bool shift_plus_enter_was_pressed = false;

    Text_Input_State *text_input_state = NULL;

    static constexpr i64 MAX_BUFFER_SIZE = 8000;
    SArr<u8> input_buffer = NewArray<u8>(MAX_BUFFER_SIZE); // @Leak

    // RArr<String> command_history;

    bool initted = false;
    bool active  = false;

    f32 last_keypress_time = 0.0f;

    i32 insert_point = 0; // @Note: This is a i32 for now because the max capacity is 8000.
    // i32 command_history_index = 0; // @Incomplete: Missing auto complete for high score names!

    // String last_saved_input;
    bool cursor_tracking_mouse = false;

    // @Temporary: Move this to somewhere else
    bool did_initial_mouse_click_so_check_for_deadzone_change = false;

    // bool do_active_widget_add            = false; // @Hack: while we figure out how to structure stuff.
    // bool do_active_widget_deactivate_all = false; // @Hack: while we figure out how to structure stuff.

    Selection selection;
};

struct Text_Input_Theme
{
    // These factors (being similar to those in Button_Theme), are in here because we
    // are not including a Button_Theme inside us since we don't need all the fields
    // there. So we make another set of them on our own.
    f32 press_duration        = 0.35f; // The duration of the flash when you press the input.
    f32 over_fade_in_speed    = 14.0f; // Higher is faster; 14 == 1/14 second for full fade in.
    f32 over_fade_out_speed   = 6.0f;  // e.g. 4 == 1/4 second for full fade out.
    f32 active_fade_in_speed  = 12.0f;
    f32 active_fade_out_speed = 6.0f;

    f32 down_fade_in_speed    = 12.0f; // @Cleanup: Do we need these?
    f32 down_fade_out_speed   = 6.0f;

    Vector4 text_color        = Vector4(.30, .70, .70, 1.0);
    Vector4 text_color_over   = Vector4(.99, .70, .70, 1.0);
    Vector4 text_color_active = Vector4(.55, .85, .85, 1.0);

    Vector4 text_color_auto_complete        = Vector4(.20, .79, .90, .40);
    Vector4 text_color_auto_complete_failed = Vector4(.99, .23, .10, .40);

    Vector4 background_color        = Vector4(.09, .07, .25, 1.0);
    Vector4 background_color_over   = Vector4(.12, .12, .40, 1.0);
    Vector4 background_color_active = Vector4(.40, .15, .65, 1.0);
    Vector4 background_color_flash  = Vector4(.60, .77, .62, 1.0);

    Vector4 selection_color = Vector4(.15, .09, .75, 1.0);
    Vector4 cursor_color    = Vector4(.99, .80, .73, 1.0);

    f32 text_baseline_vertical_position = 0.225f; // How high up the rectangular area of the text, relative to rect height.

    f32 alignment_pad_ems = 1.0f; // How much space on the left of the rect before text begins.

    f32 text_insert_margin = 3.7f; // How much you want to be able to see when typing near the edge of a text input, in ems. The resulting pixel value is clamped at 49% of the width of the text input region at render at (otherwise it will be undefined when you want the insert point to be, unless we had two different margin).

    f32 cursor_width_inside_text  = 0.1f; // In ems.
    f32 cursor_width_outside_text = 0.6f; // In ems.
};

void init_high_score_input();
// void activate(Text_Input *input);
// void deactivate(Text_Input *input);
// void reset(Text_Input *input);
void set_high_score_text(String to_set); // This does not make the insert_point jumps to the end.
// void add_text(Text_Input *input, String to_add);

struct Dynamic_Font;
void draw_high_score_name_input(Rect r, Dynamic_Font *font);

struct Event;
void handle_high_score_input_event(Event event);

extern Text_Input high_score_name_input;

// struct Event;
// void handle_event(Text_Input *input, Event event);

// void using_state_to_check_for_activating_text_input(Text_Input_State *text_input_state, Event event);
// void check_for_activating_event(Text_Input *input, Event event);

// struct Text_Input_Theme;
// void draw(Text_Input *input, Rect r, Text_Input_Theme *theme = NULL, i64 identifier = 0, Source_Location loc = Source_Location::current());

// struct Active_Widget;
// void this_deactivate_is_not_the_same_as_the_normal_text_input_deactivate(Active_Widget *widget);
// void this_handle_event_is_not_the_same_as_the_normal_text_input_handle_event(Active_Widget *widget, Event event);
