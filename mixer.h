#pragma once

#include "common.h"

struct Audio_Buffer;
struct Audio_Stream
{
    Audio_Buffer* buffer = NULL; // Pointer to internal data used by audio system.
    i64 sample_rate = 0;         // Frequency (samples per second)
    i64 sample_size = 0;         // Bits per sample: 8, 16, 32
    i64 channels    = 0;         // (1-mono, 2-stereo, ...)
};

enum class Audio_File_Type
{
    UNINITIALIZE = 0,
    WAV,
    OGG,
    MP3,
};

struct Sound
{
    Audio_Stream stream;
    i64 frames_count = 0; // Total number of frames across all channels.
};

struct Music
{
    Audio_Stream    stream;
    i64             frame_count = 0;     // Total number of frames (considering channels)
    bool            looping = true;
    Audio_File_Type context_type;        // Type of music context (audio filetype)
    void*           context_data = NULL; // Audio context data, depends on type.
};

void init_mixer();
void shutdown_mixer();

bool load_music(Music *music, String full_path);
void destroy_music(Music *music);
void play_music(Music *music);
void stop_music(Music *music);
void update_music(Music *music);

bool load_sound(Sound *sound, String full_path);

void destroy_sound(Sound *sound);
void play_sound(Sound *sound, bool perturb = true);
void stop_sound(Sound *sound);

