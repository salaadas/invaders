#pragma once

#include "common.h"

struct Invader;
struct Texture_Map;
struct Particle_Emitter;

typedef void(*Update_Proc_Type)(Invader *invader);

//
// @Speed: Make a entity array so that we don't New stuff everywhere.
// 

enum class Invader_Action
{
    FALLING_IN = 0,
    SPLINE_FLYING_IN, // The flying in from the sides that curls.
    SLEEPING,
    STRAFING,
};

struct Path_Cursor
{
    i32 waypoint_index = 0;
};

enum class Strafe_Type : u8
{
    DOWNWARD = 0,
    SIDEWAYS,
    KAMIKAZE_STRAIGHT,
    KAMIKAZE_DRUNKEN,
    NUM_STRAFE_TYPE
};

enum Path_Flag
{
    ENDPOINT_IS_IN_FORMATION   = 0x1,
    ENDPOINT_IS_OFF_THE_BOTTOM = 0x2
};

struct Path
{
    RArr<Vector2> waypoints;

    Path_Cursor cursor;

    u32 path_flags = 0;

    Strafe_Type strafe_type;
};

struct Invader
{
    // :Entity
    Vector2 position;
//    Vector2 velocity;
    Texture_Map *map = NULL;

    f32 shot_cooldown = -1.0f;

    f32 theta = 0.0f; // In degrees.
    Vector2 spawn_position;
//    Update_Proc_Type update_proc = NULL;  // Could be used for some fancy spline stuff like in Gaplus later on.

    i32 line_index = 0;
    i32 slot_index = 0;

    Invader_Action action = Invader_Action::FALLING_IN;

    Path strafe_path;
};

struct Bullet
{
    // :Entity
    Vector2 velocity;
    Vector2 position;
    Texture_Map *map = NULL;

    Particle_Emitter *emitter = NULL;
    bool player_friendly = true;
    bool can_pierce = false;
};

enum class Pickup_Type
{
    UNINITIALIZE = 0,
    EXTRA_BULLETS,
    V_SHOT,
    SHIELD,
    PIERCING
};

struct Pickup
{
    // :Entity
    Vector2 velocity;
    Vector2 position;
    Texture_Map *map = NULL;

    Pickup_Type type = Pickup_Type::UNINITIALIZE;
};

struct Particle
{
    Vector2 position;
    Vector2 velocity;

    f32 size     = .007f;
    f32 elapsed  = 0.0f;
    f32 lifetime = 2.0f;
    f32 drag     = 1.0f;
};

struct Particle_Emitter
{
    Vector2 position;
    Vector2 velocity;

    RArr<Particle> particles;
    f32 fadeout_period = 0.01f;

    f32 theta0 = 0.0f;
    f32 theta1 = TAU;

    f32 drag0 = .89f; // The higher the drag, the lesser the entropy that the particle will make.
    f32 drag1 = .99f;

    f32 size0 = .010f;
    f32 size1 = .015f;

    f32 speed0 = 0.0f;
    f32 speed1 = .10f;

    f32 lifetime0 = .40f;
    f32 lifetime1 = 1.0f;

    f32 emitter_lifetime = -1.0f;

    Vector4 color0;
    Vector4 color1;

    f32 elapsed   = .0f;
    f32 remainder = .0f;

    bool producing = true;
};

enum class Shot_Type
{
    STRAIGHT_SINGLE = 0,
    STRAIGHT_DOUBLE,
    STRAIGHT_TRIPLE,
};

struct Level_Config
{
    i32 num_lines = 3;
    i32 invaders_per_line = 3;
    f32 enemy_scale = 1.0f;
};

struct Layout_Slot
{
    Vector2 position;
};

struct Layout_Line
{
    i64 line_index = 0;
    f32 spawn_cooldown = .1f;

    f32 horizontal_spacing = 1.0f;
    
    RArr<Layout_Slot> invader_slots;
};

enum class Game_Mode
{
    IN_GAME = 0,
    WAITING_LOUNGE,
    SCORE_OVERVIEW, // If your score is in the top 10, we ask you to type in the your name.
    LEADER_BOARD
};

enum class Transition_Mode
{
    NOTHING = 0,
    NEXT_STAGE,
    CHANGE_GAME_MODE
};

struct High_Score_Record
{
    String name;
    i64 score = 0;
    // @Incomplete: Maybe add date and time for tie breaker? Right now whoevs got it first in the record is the first.
};

extern Game_Mode game_mode;

void read_input();
void simulate();
void init_game();
void draw_game();

void save_high_score_leader_board();

//
// Draw stuff:
//

struct Shader;
extern Shader *shader_argb_and_texture;
extern Shader *shader_argb_no_texture;
extern Shader *shader_text;

void init_shaders();

void rendering_2d_right_handed();
void rendering_2d_right_handed_unit_scale();

struct Dynamic_Font;
void draw_prepared_text(Dynamic_Font *font, i64 x, i64 y, Vector4 color);
i64 draw_text(Dynamic_Font *font, i64 x, i64 y, String text, Vector4 color);
