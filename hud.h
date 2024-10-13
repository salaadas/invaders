#pragma once

#include "common.h"

#include "font.h"

struct Fader
{
    String  text; // Const string, not allocated!
    Vector4 color;
    Vector4 bg_color;
    Vector2 dx_dt;

    Dynamic_Font *font = NULL;

    f32 time_started = 0.0f;
    // @Cleanup: rename these *_t to *_time variable names;
    f32 pre_fade_t = 0.0f;
    f32 fade_in_t  = 0.6f;
    f32 fade_out_t = 1.0f;

    f32 y = 0.0f; //    Of baseline, in unit coordinates.
};

extern Dynamic_Font *fader_font;
extern Dynamic_Font *fps_font;

void clear_faders();

Fader *game_report(String text);
Fader *short_game_report(String text);

Fader *do_report(String text, RArr<Fader*> *faders);

//#include "entity_manager.h"
void draw_hud(/*Entity_Manager *manager*/);
