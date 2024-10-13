// @Fixme: Remove the mutex in the callback, this is bad when alt-tab, also it causes problems like random seg-faults...

// @Fixme: This seems to be bugging us when we allocate input_buffer using New().
// @Fixme: Why is this bugging.

// Cleanup the stupid heap shit that is happening in here.
// Cleanup the linked list for audio buffer.
// What is this magic number 8 that is used everywhere for division of the sample size?

#include "mixer.h"

#include <miniaudio.h>
#include <dr_wav.h>
#include <dr_mp3.h>
#include <stb_vorbis.c>

RArr<Sound*> loaded_sounds;
RArr<Music*> loaded_musics;

enum class Audio_Buffer_Usage
{
    STATIC = 0,
    STREAM
};

constexpr auto AUDIO_SUB_BUFFERS_SIZE = 2;

typedef void(*Audio_Callback)(void *buffer_data, u32 frames);
struct Audio_Buffer
{
    ma_data_converter converter;

    f32 volume = 1.0f;
    f32 pitch  = 1.0f;
    f32 pan    = 0.5f;

    bool playing = false;
    bool paused  = false;
    bool looping = false;

    Audio_Buffer_Usage usage;

    bool is_sub_buffer_processed[AUDIO_SUB_BUFFERS_SIZE];

    i64 size_in_frames        = 0;
    i64 frame_cursor_position = 0;
    i64 frames_processed      = 0;

    u8 *data = NULL; 

    // @Speed:
    Audio_Buffer *next = NULL;
    Audio_Buffer *prev = NULL;
}; 

// @Hack @Hardcoded to stereo currently, we should be querying the system to see what works best.
constexpr auto NUM_CHANNELS    = 2;
constexpr auto PLAYBACK_FORMAT = ma_format_f32;
constexpr auto SAMPLE_RATE     = 44100;

ma_context global_miniaudio_context;
ma_mutex   global_miniaudio_mutex;
ma_device  global_miniaudio_device;

bool initted = false;

Audio_Buffer *audio_buffer_first = NULL;
Audio_Buffer *audio_buffer_last  = NULL;

u64 audio_pcm_buffer_size = 0;
void *audio_pcm_buffer = NULL;

void miniaudio_log(void *user_data, ma_uint32 level, const char *message)
{
    // @Note: All log messages from miniaudio are errors.
    logprint("miniaudio_internal", "%s\n", message);
}

bool is_audio_buffer_playing_in_locked_mutex(Audio_Buffer *buffer)
{
    return (buffer != NULL) && buffer->playing && !buffer->paused;
}

bool is_audio_buffer_playing(Audio_Buffer *buffer)
{
    bool result = false;

    ma_mutex_lock(&global_miniaudio_mutex);

    result = is_audio_buffer_playing_in_locked_mutex(buffer);

    ma_mutex_unlock(&global_miniaudio_mutex);

    return result;
}

void stop_audio_buffer_in_locked_mutex(Audio_Buffer *buffer)
{
    if (buffer == NULL) return;

    if (is_audio_buffer_playing_in_locked_mutex(buffer))
    {
        buffer->playing = false;
        buffer->paused  = false;
        buffer->frame_cursor_position = 0;
        buffer->frames_processed      = 0;
        buffer->is_sub_buffer_processed[0] = true;
        buffer->is_sub_buffer_processed[1] = true;
    }
}

void stop_audio_buffer(Audio_Buffer *buffer)
{
    ma_mutex_lock(&global_miniaudio_mutex);

    stop_audio_buffer_in_locked_mutex(buffer);

    ma_mutex_unlock(&global_miniaudio_mutex);
}

// Eventhough this has the argument 'frames_ouptut' which has the same as as the *_in_mixing_format,
// the provided frames_output is actually the input of the mixing format.
u64 read_audio_buffer_frames_in_internal_format(Audio_Buffer *buffer, u8 *frames_output, i64 frames_count)
{
    i64 sub_size_in_frames;
    if (buffer->size_in_frames > 1) sub_size_in_frames = buffer->size_in_frames / AUDIO_SUB_BUFFERS_SIZE;
    else                            sub_size_in_frames = buffer->size_in_frames;

    i64 current_sub_buffer_index = buffer->frame_cursor_position / sub_size_in_frames;
    if (current_sub_buffer_index > 1) return 0;

    // Another thread can update the processed state of buffers, so
    // we just take a copy here to try and avoid potential synchronization problems.
    bool is_sub_processed[2] = {0};
    is_sub_processed[0] = buffer->is_sub_buffer_processed[0];
    is_sub_processed[1] = buffer->is_sub_buffer_processed[1];

    auto converter = &buffer->converter;
    auto frame_size_in_bytes = ma_get_bytes_per_frame(converter->formatIn, converter->channelsIn);

    // Fill out every frame until we find a buffer that's marked as processed.
    // Then fill the remainder with 0.
    u64 total_frames_read = 0;
    while (true)
    {
        // We break from this loop differently depending on the buffer's usage
        //  - For static buffers, we simply fill as much data as we can
        //  - For streaming buffers we only fill half of the buffer that are processed
        //    Unprocessed halves must keep their audio data in-tact.
        if (buffer->usage == Audio_Buffer_Usage::STATIC)
        {
            if (total_frames_read >= frames_count) break;
        }
        else
        {
            assert(buffer->usage == Audio_Buffer_Usage::STREAM);
            if (is_sub_processed[current_sub_buffer_index]) break;
        }

        auto frames_remaining = frames_count - total_frames_read;
        if (frames_remaining <= 0) break;

        i64 frames_remaining_in_output_buffer;
        if (buffer->usage == Audio_Buffer_Usage::STATIC)
        {
            // Fill as much as possible.
            frames_remaining_in_output_buffer = buffer->size_in_frames - buffer->frame_cursor_position;
        }
        else
        {
            auto first_frame_index_of_this_sub_buffer = sub_size_in_frames * current_sub_buffer_index;
            frames_remaining_in_output_buffer = sub_size_in_frames - (buffer->frame_cursor_position - first_frame_index_of_this_sub_buffer);
        }

        auto frames_to_read = frames_remaining;
        if (frames_to_read > frames_remaining_in_output_buffer) frames_to_read = frames_remaining_in_output_buffer;

        auto dest = frames_output + (total_frames_read * frame_size_in_bytes);
        auto src  = buffer->data + buffer->frame_cursor_position * frame_size_in_bytes;
        auto nbytes = frames_to_read * frame_size_in_bytes;

        // @Fixme: This seems to be bugging us when we allocate input_buffer using New().
        memcpy(dest, src, nbytes);

        buffer->frame_cursor_position = (buffer->frame_cursor_position + frames_to_read) % buffer->size_in_frames;

        total_frames_read += frames_to_read;

        // If we've read to the end of the buffer, mark it as processed
        if (frames_to_read == frames_remaining_in_output_buffer)
        {
            buffer->is_sub_buffer_processed[current_sub_buffer_index] = true;
            is_sub_processed[current_sub_buffer_index] = true;

            current_sub_buffer_index = (current_sub_buffer_index + 1) % 2;

            if (!buffer->looping)
            {
                stop_audio_buffer_in_locked_mutex(buffer);
                break;
            }
        }
    }

    return frames_count;


    // Fill the remainder with 0.
    auto total_frames_remaining = (frames_count - total_frames_read);
    if (total_frames_remaining > 0)
    {
        auto dest = frames_output + (total_frames_read * frame_size_in_bytes);
        memset(dest, 0, total_frames_read * frame_size_in_bytes);

        // For static buffers we can fill the remaining frames with silence for safety, but we don't want
        // to report those frames as "read". The reason for this is that the caller uses the return value
        // to know whether a non-looping sound has finished playback.
        if (buffer->usage != Audio_Buffer_Usage::STATIC) total_frames_read += total_frames_remaining;
    }

    return total_frames_read;
}

u64 read_audio_buffer_frames_in_mixing_format(Audio_Buffer *buffer, f32 *frames_output, i64 frames_count)
{
    // We continuously convert the data from the Audio_Buffer's internal format to the universal mixing
    // format. We do this until the a total of 'frames_count' frames has been outputted.
    // Also, we never try to read more input data than required.

    constexpr auto INPUT_BUFFER_COUNT = 2048;
    u8 input_buffer[INPUT_BUFFER_COUNT] = {0};

    // @Fixme: Why is this bugging.
    // constexpr auto INPUT_BUFFER_COUNT = 4096;
    // auto input_buffer = New<u8>(INPUT_BUFFER_COUNT);
    // defer {my_free(input_buffer);};

    auto converter = &buffer->converter;
    auto bytes_per_frame = ma_get_bytes_per_frame(converter->formatIn, converter->channelsIn);

    auto input_buffer_frame_cap = INPUT_BUFFER_COUNT * sizeof(input_buffer[0]) / bytes_per_frame;

    u64 total_output_frames_processed = 0;
    while (total_output_frames_processed < frames_count)
    {
        long long unsigned int output_frames_to_process = frames_count - total_output_frames_processed;
        long long unsigned int input_frames_to_process  = 0;

        ma_data_converter_get_required_input_frame_count(converter, output_frames_to_process, &input_frames_to_process);

        if (input_frames_to_process > input_buffer_frame_cap) input_frames_to_process = input_buffer_frame_cap;

        auto running_frames_output = frames_output + (total_output_frames_processed * converter->channelsOut);

        // Now we convert the data to our own mixing format.
        long long unsigned int input_frames_just_processed = read_audio_buffer_frames_in_internal_format(buffer, input_buffer, input_frames_to_process);

        long long unsigned int output_frames_just_processed = output_frames_to_process;

        ma_data_converter_process_pcm_frames(converter, input_buffer, &input_frames_just_processed, running_frames_output, &output_frames_just_processed);

        total_output_frames_processed += output_frames_just_processed;

        // If we run out of data, we break.
        if (input_frames_just_processed < input_frames_to_process) break;

        // Ensure we don't infinite loop.
        if (!input_frames_just_processed && !output_frames_just_processed) break;
    }

    return total_output_frames_processed;
}

//
// This is our main mixing function.
// It's just an accumulation.
//
void mix_audio_frames(f32 *frames_output, f32 *frames_input, i64 frames_count, Audio_Buffer *buffer)
{
    auto local_volume = buffer->volume;

    auto channels = global_miniaudio_device.playback.channels;

    if (channels == 2) // Consider panning.
    {
        auto left = buffer->pan;
        auto right = 1.0f - left;

        //
        // Fast sine approximation in [0..1] for pan law: y = 0.5 * x * (3 - x*x);
        //
        f32 levels[2] = { local_volume * .5f * left * (3.f - left*left),
                          local_volume * .5f * right * (3.f - right*right) };

        auto f_out = frames_output;
        auto f_in  = frames_input;

        for (i64 i = 0; i < frames_count; ++i)
        {
            f_out[0] += (f_in[0] * levels[0]); // Left.
            f_out[1] += (f_in[1] * levels[1]); // Right.

            f_out += 2;
            f_in  += 2;
        }
    }
    else  // We do not consider panning
    {
        for (i64 i = 0; i < frames_count; ++i)
        {
            for (i64 c = 0; c < channels; ++c)
            {
                auto f_out = frames_output + (i*channels);
                auto f_in  = frames_input  + (i*channels);

                // Output accumulates input multiplied by volume to provided output (usually 0).
                f_out[c] += (f_in[c] * local_volume);
            }
        }
    }
}

//
// It is called when miniaudio needs to get some data.
//
void audio_data_callback(ma_device *audio_device, void *frames_output, const void *frames_input, ma_uint32 frames_to_read)
{
    // Mixing is basically just an accumulation, we need to initialize the output buffer to 0.
    auto playback_dev = &audio_device->playback;
    auto bytes_per_sample = ma_get_bytes_per_sample(playback_dev->format);

    memset(frames_output, 0, frames_to_read * playback_dev->channels * bytes_per_sample);

    // @Cleanup: Using a mutex here for thread-safety which makes things not real-time.
    ma_mutex_lock(&global_miniaudio_mutex);

    for (auto audio_buffer = audio_buffer_first; audio_buffer != NULL; audio_buffer = audio_buffer->next)
    {
        // Ignore stopped or paused sounds.
        if (!audio_buffer->playing || audio_buffer->paused) continue;

        i64 frames_read = 0;

        while (true)
        {
            if (frames_read >= frames_to_read) break;

            // Just read as much data as we can from the stream.
            auto df = (frames_to_read - frames_read);

            while (df > 0)
            {
                constexpr auto STEREO_DATA_COUNT = 1024;
                f32 stereo_data[STEREO_DATA_COUNT] = {0}; // Frames data.

                auto frames_to_read_right_now = df;

                if (frames_to_read_right_now > STEREO_DATA_COUNT/NUM_CHANNELS)
                {
                    frames_to_read_right_now = STEREO_DATA_COUNT/NUM_CHANNELS;
                }

                auto frames_just_read = read_audio_buffer_frames_in_mixing_format(audio_buffer, stereo_data, frames_to_read_right_now);

                if (frames_just_read > 0)
                {
                    auto f_out = reinterpret_cast<f32*>(frames_output) + (frames_read * playback_dev->channels);
                    auto f_in  = reinterpret_cast<f32*>(stereo_data);

                    mix_audio_frames(f_out, f_in, frames_just_read, audio_buffer);

                    df -= frames_just_read;
                    frames_read += frames_just_read;
                }

                if (!audio_buffer->playing)
                {
                    frames_read = frames_to_read;
                    break;
                }

                // If we weren't able to read all the frames we requested, bail.
                if (frames_just_read < frames_to_read_right_now)
                {
                    if (!audio_buffer->looping)
                    {
                        stop_audio_buffer_in_locked_mutex(audio_buffer);
                        break;
                    }
                    else
                    {
                        // Should not get here.
                        assert(0);

                        audio_buffer->frame_cursor_position = 0;
                        continue;
                    }
                }
            }

            // If for some reason we weren't able to read every frame we'll need to break from the loop
            // Not doing this could theoretically put us into an infinite loop.
            if (df > 0) break;
        }
    }

    ma_mutex_unlock(&global_miniaudio_mutex);
}

void init_mixer()
{
    auto ma_context_config = ma_context_config_init();
    ma_log_callback_init(miniaudio_log, NULL);

    auto success = ma_context_init(NULL, 0, &ma_context_config, &global_miniaudio_context);
    if (success != MA_SUCCESS)
    {
        logprint("miniaudio", "Failed to init miniaudio's context!\n");
        assert(0);
        return;
    }

    // @Note: Using the default device. The format is f32 because it simplifies mixing.
    {
        auto config = ma_device_config_init(ma_device_type_playback);

        config.playback.pDeviceID = NULL; // set NULL to use the default playback.
        config.playback.format    = PLAYBACK_FORMAT;
        config.playback.channels  = NUM_CHANNELS; // @Hardcoded: Using stereo right now....
        config.capture.pDeviceID  = NULL; // set NULL for the default capture AUDIO
        config.capture.format     = ma_format_s16; // using signed 16 bits format (enforce every file data to this).
        config.capture.channels   = 1;
        config.sampleRate         = SAMPLE_RATE;
        config.dataCallback       = audio_data_callback;
        config.pUserData          = NULL;

        success = ma_device_init(&global_miniaudio_context, &config, &global_miniaudio_device);
        if (success != MA_SUCCESS)
        {
            logprint("miniaudio", "Failed to init miniaudio's playback device!\n");
            ma_context_uninit(&global_miniaudio_context);
            assert(0);
            return;
        }
    }

    // Mixing happens on a separate thread which means we need to synchronize.
    // @Note: Using a mutex here to make things simple, but may want to look at something
    // a bit smarter later on to keep everything real-time, if that's necessary.
    auto mutex_success = ma_mutex_init(&global_miniaudio_mutex);
    if (mutex_success != MA_SUCCESS)
    {
        logprint("miniaudio", "Failed to create mutex for audio mixing!\n");
        ma_device_uninit(&global_miniaudio_device);
        ma_context_uninit(&global_miniaudio_context);
        assert(0);
        return;
    }

    // @Fixme: CURRENTLY KEEP THE DEVICE RUNNING THE WHOLE TIME.
    // We might want to only run the device if there is at least one sound being played.
    auto start_mixing_success = ma_device_start(&global_miniaudio_device);
    if (start_mixing_success != MA_SUCCESS)
    {
        logprint("miniaudio", "Failed to start playback device for mixer!\n");
        ma_device_uninit(&global_miniaudio_device);
        ma_context_uninit(&global_miniaudio_context);
        assert(0);
        return;
    }

    auto dev = &global_miniaudio_device;

    logprint("miniaudio", "Backend: %s\n", ma_get_backend_name(global_miniaudio_context.backend));
    logprint("miniaudio", "Format: source = %s, internal = %s\n", ma_get_format_name(dev->playback.format), ma_get_format_name(dev->playback.internalFormat));
    logprint("miniaudio", "Channels: source = %d, internal = %d\n", dev->playback.channels, dev->playback.internalChannels);
    logprint("miniaudio", "Sample rate: source = %d, internal = %d\n", dev->sampleRate, dev->playback.internalSampleRate);
    logprint("miniaudio", "Periods size %d\n", dev->playback.internalPeriodSizeInFrames * dev->playback.internalPeriods);

    initted = true;
}

void shutdown_mixer()
{
    assert(initted);
    if (!initted) return;

    initted = false;

    for (auto it : loaded_musics) destroy_music(it);
    for (auto it : loaded_sounds) destroy_sound(it);

    ma_mutex_uninit(&global_miniaudio_mutex);
    ma_device_uninit(&global_miniaudio_device);
    ma_context_uninit(&global_miniaudio_context);

    my_free(audio_pcm_buffer);
    audio_pcm_buffer = NULL;
    audio_pcm_buffer_size = 0;
}

// Create a new audio buffer that is filled with silence.
Audio_Buffer *create_audio_buffer(ma_format input_format, u32 channels, u32 sample_rate, u32 size_in_frames, Audio_Buffer_Usage usage)
{
    auto audio_buffer = New<Audio_Buffer>();

    if (size_in_frames > 0)
    {
        auto bytes_per_sample = ma_get_bytes_per_sample(input_format);
        audio_buffer->data = reinterpret_cast<u8*>(my_alloc(size_in_frames * channels * bytes_per_sample));
    }

    auto converter_config = ma_data_converter_config_init(input_format, PLAYBACK_FORMAT, channels, NUM_CHANNELS, sample_rate, global_miniaudio_device.sampleRate);

    converter_config.allowDynamicSampleRate = true;

    auto success = ma_data_converter_init(&converter_config, NULL, &audio_buffer->converter);

    if (success != MA_SUCCESS)
    {
        logprint("miniaudio", "Failed to create data conversion pipeline.\n");

        my_free(audio_buffer);
        return NULL;
    }

    audio_buffer->usage = usage;
    audio_buffer->size_in_frames = size_in_frames;

    audio_buffer->is_sub_buffer_processed[0] = true;
    audio_buffer->is_sub_buffer_processed[1] = true;

    // @Cleanup: Track audio buffer from linked list.
    {
        ma_mutex_lock(&global_miniaudio_mutex);

        // @Speed:
        if (!audio_buffer_first)
        {
            audio_buffer_first = audio_buffer;
        }
        else
        {
            audio_buffer_last->next = audio_buffer;
            audio_buffer->prev = audio_buffer_last;
        }

        audio_buffer_last = audio_buffer;

        ma_mutex_unlock(&global_miniaudio_mutex);
    }

    return audio_buffer;
}

// Create audio stream for streaming pcm data.
Audio_Stream create_audio_stream(u32 sample_rate, u32 sample_size, u32 channels)
{
    Audio_Stream stream;

    stream.sample_rate = sample_rate;
    stream.sample_size = sample_size;
    stream.channels    = channels;

    ma_format input_format;
    if      (sample_size == 8)  input_format = ma_format_u8;
    else if (sample_size == 16) input_format = ma_format_s16;
    else                        input_format = ma_format_f32;

    // Size of a streaming buffer must be at least double the size of a period.
    auto period_size = global_miniaudio_device.playback.internalPeriodSizeInFrames;

    // If the buffer is not set, compute one that would give us
    // a buffer good enough for a decent frame rate.
    constexpr auto SUBSTITUTE_FRAME_RATE = 30;

    constexpr auto AUDIO_BUFFER_DEFAULT_SIZE = 0; // @Investiage: @Cleanup

    u32 sub_buffer_size;
    if (AUDIO_BUFFER_DEFAULT_SIZE == 0) sub_buffer_size = global_miniaudio_device.sampleRate / SUBSTITUTE_FRAME_RATE;
    else                                sub_buffer_size = AUDIO_BUFFER_DEFAULT_SIZE;

    if (sub_buffer_size < period_size) sub_buffer_size = period_size;

    stream.buffer = create_audio_buffer(input_format, stream.channels, stream.sample_rate, sub_buffer_size * 2, Audio_Buffer_Usage::STREAM); // @Hardcoded to creating 2 buffer for stereo and mono.

    if (!stream.buffer)
    {
        logprint("create_audio_stream", "Failed to load audio stream buffer, panic!\n");
        assert(0);
    }
    else
    {
/*
        logprint("create_audio_stream", "Stream initialized successfully (%ld Hz, %ld bit, %s)\n",
                 stream.sample_rate, stream.sample_size,
                 stream.channels == 1 ? "Mono" : "Stereo");
*/

        stream.buffer->looping = true; // Always loop for streaming buffers.
    }

    return stream;
}

#include "catalog.h" // For get_extension().

bool load_music(Music *music, String full_path)
{
    assert(initted);

    auto ext = get_extension(full_path);
    auto c_path = reinterpret_cast<char*>(temp_c_string(full_path));

    auto music_loaded = false;

    if (ext == String("wav"))
    {
        auto context = New<drwav>(false);
        auto success = drwav_init_file(context, c_path, NULL);

        if (success)
        {
            music->context_type = Audio_File_Type::WAV;
            music->context_data = context;

            i32 sample_size = context->bitsPerSample;

            if (sample_size == 24) sample_size = 16; // Forced conversion to s16 in update_music().

            music->stream = create_audio_stream(context->sampleRate, sample_size, context->channels);
            music->frame_count = context->totalPCMFrameCount;

            music_loaded = true;
        }
        else
        {
            logprint("load_music", "Failed to init drwav while loading music from path '%s'!\n", c_path);
        }
    }
    else if (ext == String("ogg"))
    {
        auto context_data = stb_vorbis_open_filename(c_path, NULL, NULL);

        if (music->context_data != NULL)
        {
            music->context_type = Audio_File_Type::OGG;
            music->context_data = context_data;

            auto info = stb_vorbis_get_info(context_data);

            // @Note: OGG bit rate defaults to 16 bit,
            // this is enough for compressed format (which is s16).
            music->stream = create_audio_stream(info.sample_rate, 16, info.channels);

            // @Investiage: re-read this part of the code...
            music->frame_count = reinterpret_cast<u32>(stb_vorbis_stream_length_in_samples(context_data));

            music_loaded = true;
        }
        else
        {
            logprint("load_music", "stb_vorbis failed to load ogg music file from path '%s'!\n", c_path);
        }
    }
    else if (ext == String("mp3"))
    {
        auto context = New<drmp3>(false);
        auto result = drmp3_init_file(context, c_path, NULL);

        if (result > 0)
        {
            music->context_type = Audio_File_Type::MP3;
            music->context_data = context;

            music->stream = create_audio_stream(context->sampleRate, 32, context->channels);
            music->frame_count = static_cast<u32>(drmp3_get_pcm_frame_count(context));

            music_loaded = true;
        }
        else
        {
            logprint("load_music", "Failed to init dr_mp3 while loading music from '%s'!\n", c_path);
        }
    }
    else
    {
        logprint("load_music", "File format %s of music path '%s' is not supported!\n", temp_c_string(ext), c_path);
    }

    if (!music_loaded)
    {
        switch (music->context_type)
        {
            case Audio_File_Type::WAV: {
                drwav_uninit(reinterpret_cast<drwav*>(music->context_data));
                my_free(music->context_data);
            } break;
            case Audio_File_Type::OGG: {
                stb_vorbis_close(reinterpret_cast<stb_vorbis*>(music->context_data));
            } break;
            case Audio_File_Type::MP3: {
                drmp3_uninit(reinterpret_cast<drmp3*>(music->context_data));
                my_free(music->context_data);
            } break;
        }

        music->context_data = NULL;
        logprint("load_music", "Music file %s could not be loaded\n", c_path);
    }
    else
    {
/*
        logprint("miniaudio", "Music file %s loaded successfully\n", c_path);
        logprint("miniaudio", "    > Sample rate:   %ld Hz\n", music->stream.sample_rate);
        logprint("miniaudio", "    > Sample size:   %ld bits\n", music->stream.sample_size);
        logprint("miniaudio", "    > Channels:      %ld (%s)\n", music->stream.channels,
                 (music->stream.channels == 1) ? "Mono"
                 : (music->stream.channels == 2) ? "Stereo" : "Multi");
        logprint("miniaudio", "    > Total frames:  %ld\n", music->frame_count);
*/
    }

    array_add(&loaded_musics, music);

    return music_loaded;
}

void untrack_audio_buffer(Audio_Buffer *buffer)
{
    ma_mutex_lock(&global_miniaudio_mutex);

    if (buffer->prev == NULL) audio_buffer_first = buffer->next;
    else                      buffer->prev->next = buffer->next;

    if (buffer->next == NULL) audio_buffer_last  = buffer->prev;
    else                      buffer->next->prev = buffer->prev;

    buffer->prev = NULL;
    buffer->next = NULL;

    ma_mutex_unlock(&global_miniaudio_mutex);
}

void destroy_audio_buffer(Audio_Buffer *buffer)
{
    if (buffer == NULL) return;

    untrack_audio_buffer(buffer);

    ma_data_converter_uninit(&buffer->converter, NULL);
    my_free(buffer->data);
    my_free(buffer);
}

void destroy_music(Music *music)
{
    destroy_audio_buffer(music->stream.buffer);
    music->stream.buffer = NULL;

    if (!music->context_data) return;

    switch (music->context_type)
    {
        case Audio_File_Type::WAV: {
            drwav_uninit(reinterpret_cast<drwav*>(music->context_data));
            my_free(music->context_data);
        } break;
        case Audio_File_Type::OGG: {
            stb_vorbis_close(reinterpret_cast<stb_vorbis*>(music->context_data));
        } break;
        case Audio_File_Type::MP3: {
            drmp3_uninit(reinterpret_cast<drmp3*>(music->context_data));
            my_free(music->context_data);
        } break;
        default: assert(0);
    }

    music->context_data = NULL;
}

// Update audio stream, assuming the audio system mutex has been locked.
void update_audio_stream_in_locked_state(Audio_Stream *stream, void *data, i64 num_frames_to_write)
{
    auto audio_buffer = stream->buffer;

    if (!audio_buffer)
    {
        logprint("update_audio_stream_in_locked_state", "Stream does not contain audio buffer, bail!\n");
        return;
    }

    // Check if buffer is processed.
    auto sub_left_processed  = audio_buffer->is_sub_buffer_processed[0];
    auto sub_right_processed = audio_buffer->is_sub_buffer_processed[1];

    if (!sub_left_processed && !sub_right_processed)
    {
        logprint("update_audio_stream_in_locked_state", "Neither audio buffer left/right is available for updating audio stream!\n");
        return;
    }

    u32 which_sub_to_update = 0;

    if (sub_left_processed && sub_right_processed)
    {
        // Both buffers are available for updating.
        // Update the first one and make sure the cursor is moved back to the front.

        which_sub_to_update = 0;
        audio_buffer->frame_cursor_position = 0;
    }
    else
    {
        // Just update whichever sub-buffer is processed.
        which_sub_to_update = (sub_left_processed) ? 0 : 1;
    }

    u32 sub_size_in_frames = audio_buffer->size_in_frames / 2;

    auto offset = sub_size_in_frames * stream->channels * (stream->sample_size/8);
    u8 *sub_buffer = audio_buffer->data + offset * which_sub_to_update;

    // Total frames processed in buffer is always the complete size,
    // filled with 0 if required.
    audio_buffer->frames_processed += sub_size_in_frames;

    // @Cleanup: The current API expect a whole buffer to be updated in one go...
    if (sub_size_in_frames < num_frames_to_write)
    {
        logprint("update_audio_stream_in_locked_state", "Attempted to write too many frames to the audio buffer, bail!\n");
        return;
    }
    
    u32 frames_to_write = num_frames_to_write;
    u32 bytes_to_write  = frames_to_write * stream->channels * (stream->sample_size/8);

    memcpy(sub_buffer, data, bytes_to_write);

    // Any leftover frames should be filled with zeros.
    u32 num_left_over_frames = sub_size_in_frames - frames_to_write;

    if (num_left_over_frames > 0)
    {
        auto dest   = sub_buffer + bytes_to_write;
        auto nbytes = num_left_over_frames * stream->channels * (stream->sample_size/8);

        memset(dest, 0, nbytes);
    }

    audio_buffer->is_sub_buffer_processed[which_sub_to_update] = false;
}

void stop_music(Music *music)
{
    stop_audio_buffer(music->stream.buffer);

    switch (music->context_type)
    {
        case Audio_File_Type::WAV: {
            drwav_seek_to_pcm_frame(reinterpret_cast<drwav*>(music->context_data), 0);
        } break;
        case Audio_File_Type::OGG: {
            stb_vorbis_seek_start(reinterpret_cast<stb_vorbis*>(music->context_data));
        } break;
        case Audio_File_Type::MP3: {
            drmp3_seek_to_start_of_stream(reinterpret_cast<drmp3*>(music->context_data));
        } break;

        default: {
            logprint("stop_music", "Unknown music context type %d, panic!\n", (i32)music->context_type);
            assert(0);
        }
    }
}

// Re-fill music's audio buffers if the data is already processed.
void update_music(Music *music)
{
    if (!music->stream.buffer) return;

    ma_mutex_lock(&global_miniaudio_mutex);

    auto sub_buffer_size_in_frames = music->stream.buffer->size_in_frames / AUDIO_SUB_BUFFERS_SIZE;

    // On the first call of this function, we lazily pre-allocated a temporary
    // bufer to read audio files or audio memory data in.
    auto frame_size = music->stream.channels * music->stream.sample_size / 8; // Size of one frame.
    auto pcm_size   = sub_buffer_size_in_frames * frame_size;

    if (audio_pcm_buffer_size < pcm_size)
    {
        my_free(audio_pcm_buffer);
        audio_pcm_buffer      = my_alloc(pcm_size);
        audio_pcm_buffer_size = pcm_size;
    }

    // Check both sub-buffers to see if they need to be refilled.
    auto audio_buffer = music->stream.buffer;
    auto pcm_buffer = reinterpret_cast<i8*>(audio_pcm_buffer);

    for (auto sub_buffer_index = 0; sub_buffer_index < AUDIO_SUB_BUFFERS_SIZE; ++sub_buffer_index)
    {
        if (audio_buffer && !audio_buffer->is_sub_buffer_processed[sub_buffer_index])
        {
            // No refilling required, move to next sub-buffer.
            continue;
        }

        i64 frames_left = music->frame_count - audio_buffer->frames_processed;
        i64 frames_to_stream = 0;

        if ((frames_left >= sub_buffer_size_in_frames) || music->looping)
        {
            frames_to_stream = sub_buffer_size_in_frames;
        }
        else
        {
            frames_to_stream = frames_left;
        }

        auto frame_count_still_needed = frames_to_stream;
        auto frame_count_read_total   = 0;

        switch (music->context_type)
        {
            case Audio_File_Type::WAV: {
                auto context_data = reinterpret_cast<drwav*>(music->context_data);

                if (music->stream.sample_size == 16)
                {
                    while (true)
                    {
                        auto output_buffer = reinterpret_cast<i16*>(pcm_buffer + frame_count_read_total*frame_size);

                        auto frame_count_read = static_cast<i64>(drwav_read_pcm_frames_s16(context_data, frame_count_still_needed, output_buffer));

                        frame_count_read_total   += frame_count_read;
                        frame_count_still_needed -= frame_count_read;

                        if (frame_count_still_needed <= 0) break;

                        drwav_seek_to_pcm_frame(context_data, 0);
                    }
                }
                else if (music->stream.sample_size == 32)
                {
                    while (true)
                    {
                        auto output_buffer = reinterpret_cast<f32*>(pcm_buffer + frame_count_read_total*frame_size);

                        auto frame_count_read = static_cast<i64>(drwav_read_pcm_frames_f32(context_data, frame_count_still_needed, output_buffer));

                        frame_count_read_total   += frame_count_read;
                        frame_count_still_needed -= frame_count_read;

                        if (frame_count_still_needed <= 0) break;

                        drwav_seek_to_pcm_frame(context_data, 0);
                    }
                }
            } break;


            case Audio_File_Type::OGG: {
                auto context_data = reinterpret_cast<stb_vorbis*>(music->context_data);

                while (true)
                {
                    auto output_buffer = reinterpret_cast<i16*>(pcm_buffer + frame_count_read_total*frame_size);

                    auto frame_count_read = static_cast<i64>(stb_vorbis_get_samples_short_interleaved(context_data,
                                                                                                      music->stream.channels,
                                                                                                      output_buffer,
                                                                                                      frame_count_still_needed*music->stream.channels));

                    frame_count_read_total   += frame_count_read;
                    frame_count_still_needed -= frame_count_read;

                    if (frame_count_still_needed <= 0) break;

                    stb_vorbis_seek_start(context_data);
                }
            } break;


            case Audio_File_Type::MP3: {
                auto context_data = reinterpret_cast<drmp3*>(music->context_data);

                while (true)
                {
                    auto output_buffer = reinterpret_cast<f32*>(pcm_buffer + frame_count_read_total*frame_size);

                    auto frame_count_read = static_cast<i64>(drmp3_read_pcm_frames_f32(
                                                                 context_data,
                                                                 frame_count_still_needed,
                                                                 output_buffer));

                    frame_count_read_total   += frame_count_read;
                    frame_count_still_needed -= frame_count_read;

                    if (frame_count_still_needed <= 0) break;

                    drmp3_seek_to_start_of_stream(context_data);
                }
            } break;


            default: break;
        }

        update_audio_stream_in_locked_state(&music->stream, audio_pcm_buffer, frames_to_stream);

        audio_buffer->frames_processed = audio_buffer->frames_processed % music->frame_count;

        if (frames_left <= sub_buffer_size_in_frames)
        {
            if (!music->looping)
            {
                // If not looping, end the stream by filling the latest frames from input.
                ma_mutex_unlock(&global_miniaudio_mutex);
                stop_music(music);
                return;
            }
        }
    }

    ma_mutex_unlock(&global_miniaudio_mutex);
}

void play_audio_buffer(Audio_Buffer *buffer)
{
    if (!buffer) return;

    ma_mutex_lock(&global_miniaudio_mutex);

    buffer->playing = true;
    buffer->paused  = false;
    buffer->frame_cursor_position = 0;

    ma_mutex_unlock(&global_miniaudio_mutex);
}

// @Incomplete: Add option for volume as arguments.
void play_music(Music *music)
{
    auto audio_buffer = music->stream.buffer;
    if (!audio_buffer) return;

    // @Note: For music streams, we need to maintain the frame cursor position.
    auto frame_cursor_position = audio_buffer->frame_cursor_position;
    defer {audio_buffer->frame_cursor_position = frame_cursor_position;};

    play_audio_buffer(audio_buffer); // This function changes the frame_cursor_position.
}

#include "file_utils.h"

// Used as an intermediate struct to process the Sound.
struct Sound_Wave_Data
{
    void *data = NULL;
    i64 sample_size  = -1;
    i64 num_channels = -1;
    i64 sample_rate  = -1;
    i64 frames_count = -1;
};

bool process_sound_from_wave_data(Sound *sound, Sound_Wave_Data wave)
{
    // When using miniaudio we need to do our own mixing
    // To simplify this we need convert the format of each sound to be consistent with
    // the format used to open the playback 'global_miniaudio_device'. We can do this two ways:
    //
    //   1) Convert the whole sound in one go at load time (here)
    //   2) Convert the audio data in chunks at mixing time
    //
    // First option has been selected, format conversion is done on the loading stage
    // The downside is that it uses more memory if the original sound is u8 or s16.

    ma_format in_format;
    if      (wave.sample_size == 8)  in_format = ma_format_u8;
    else if (wave.sample_size == 16) in_format = ma_format_s16;
    else                             in_format = ma_format_f32;

    auto out_sample_rate = global_miniaudio_device.sampleRate;

    auto frames_count = ma_convert_frames(NULL, 0, PLAYBACK_FORMAT, NUM_CHANNELS, out_sample_rate, NULL,
                                          wave.frames_count, in_format, wave.num_channels, wave.sample_rate);

    if (!frames_count)
    {
        logprint("load_sound", "Failed to get the miniaudio's frames count for format conversion!\n");
        return false;
    }

    auto audio_buffer = create_audio_buffer(PLAYBACK_FORMAT, NUM_CHANNELS, out_sample_rate, frames_count, Audio_Buffer_Usage::STATIC);

    if (!audio_buffer)
    {
        logprint("load_sound", "Failed to creata audio buffer!\n");
        return false;
    }

    frames_count = ma_convert_frames(audio_buffer->data,
                                     frames_count, PLAYBACK_FORMAT, NUM_CHANNELS, out_sample_rate, wave.data,
                                     wave.frames_count, in_format, wave.num_channels, wave.sample_rate);

    if (!frames_count)
    {
        logprint("load_sound", "Failed to do format conversion!\n");
        return false;
    }

    sound->frames_count = frames_count;

    auto stream = &sound->stream;
    stream->sample_rate = out_sample_rate;
    stream->sample_size = 32;
    stream->channels    = NUM_CHANNELS;
    stream->buffer      = audio_buffer;

    return true;
};

bool load_sound(Sound *sound, String full_path)
{
    assert(initted);

    auto [content, success] = read_entire_file(full_path);

    if (!success)
    {
        logprint("load_sound", "Failed to read file content of sound from path '%s'!\n", temp_c_string(full_path));
        return false;
    }

    auto orig_content = content;
    defer {free_string(&orig_content);};

    auto ext = get_extension(full_path);

    if (ext == String("wav") || ext == String("WAV"))
    {
        // @Incomplete:
        assert(0);
    }
    else if (ext == String("ogg") || ext == String("OGG"))
    {
        // @Incomplete:
        assert(0);
    }
    else if (ext == String("mp3") || ext == String("MP3"))
    {
        drmp3_config config = {0};

        long long unsigned int in_total_frames = 0;

        auto c_content = to_c_string(content);
        defer {my_free(c_content);};
        auto data = drmp3_open_memory_and_read_pcm_frames_f32(c_content, content.count, &config, &in_total_frames, NULL);
        if (!data)
        {
            logprint("load_sound:dr_mp3", "Failed to read pcm data from sound '%s'!\n", temp_c_string(full_path));
            return false;
        }

        Sound_Wave_Data wave;
        wave.data         = data;
        wave.sample_size  = 32;
        wave.num_channels = config.channels;
        wave.sample_rate  = config.sampleRate;
        wave.frames_count = in_total_frames;

        auto succes = process_sound_from_wave_data(sound, wave);

        if (!success) return false;
    }

    array_add(&loaded_sounds, sound);

    return true;
}

void set_audio_buffer_volume(Audio_Buffer *buffer, f32 volume_scale)
{
    if (!buffer) return;

    ma_mutex_lock(&global_miniaudio_mutex);

    buffer->volume = volume_scale;

    ma_mutex_unlock(&global_miniaudio_mutex);
}

//
// Pitch is just an adjustment of the sample rate.
// @Note: When you change the pitch, you also change the duration
// of the sound:
// - Higher pitches will make the sound faster.
// - Lower pitches will make the sound slower.
void set_audio_buffer_pitch(Audio_Buffer *buffer, f32 pitch)
{
    if (!buffer || (pitch <= 0)) return;

    ma_mutex_lock(&global_miniaudio_mutex);

    auto converter = &buffer->converter;

    auto new_sample_rate = static_cast<f32>(converter->sampleRateOut) / pitch;
    ma_data_converter_set_rate(converter, converter->sampleRateIn, new_sample_rate);

    buffer->pitch = pitch;

    ma_mutex_unlock(&global_miniaudio_mutex);
}

/*
Sound create_sound_alias(Sound *sound)
{
    Sound alias;

    auto src_buffer = sound->stream.buffer;
    if (!src_buffer->data) return alias;

    auto buffer = create_audio_buffer(PLAYBACK_FORMAT, NUM_CHANNELS, global_miniaudio_device.sampleRate, 0, Audio_Buffer_Usage::STATIC);

    if (!buffer)
    {
        logprint("create_sound_alias", "Failed to create audio buffer for sound alias!\n");
        return alias; // Early bail so that we don't NULL dereference the audio_buffer.
    }

    buffer->size_in_frames = src_buffer->size_in_frames;
    buffer->volume         = src_buffer->volume;
    buffer->data           = src_buffer->data;

    alias.frames_count = sound->frames_count;

    auto stream = &alias.stream;
    stream->sample_rate = global_miniaudio_device.sampleRate;
    stream->sample_size = 32;
    stream->channels    = NUM_CHANNELS;
    stream->buffer      = buffer;

    return alias;
}

void destroy_sound_alias(Sound *alias)
{
    auto buffer = alias->stream.buffer;
    if (!buffer) return;

    untrack_audio_buffer(buffer);

    ma_data_converter_uninit(&buffer->converter, NULL);
    my_free(buffer);
    // @Note: We do not free the data of the buffer because we do not own it.
}
*/

void play_sound(Sound *sound, bool perturb)
{
    if (perturb)
    {
        auto random_volume_scale = get_random_within_range(.62f, .8f); // @Hardcoded: Should have a tweaks file...
        set_audio_buffer_volume(sound->stream.buffer, random_volume_scale);

        auto random_pitch_scale = get_random_within_range(.85f, 1.1f); // @Hardcoded: Should have a tweaks file...
        set_audio_buffer_pitch(sound->stream.buffer, random_pitch_scale);

        play_audio_buffer(sound->stream.buffer);
    }
    else
    {
        play_audio_buffer(sound->stream.buffer);
    }
}

void destroy_sound(Sound *sound)
{
    destroy_audio_buffer(sound->stream.buffer);
    sound->stream.buffer = NULL;
}

void stop_sound(Sound *sound)
{
    stop_audio_buffer(sound->stream.buffer);
}
