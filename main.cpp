#include "common.h"
#include "main.h"
#include "file_utils.h"
#include "path_utils.h"
#include "events.h"
#include "hotloader.h"
#include "time_info.h"
#include "invaders.h"
#include "mixer.h"

// OpenGL
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>

const f32    DT_MAX = 0.15f;
const String PROGRAM_NAME("Invaders");
i32          BIG_FONT_SIZE = 32; // @Note: This font size changes depending on the window's size
const String FONT_FOLDER("data/fonts");

// @Hack:
constexpr auto STARTING_WIDTH  = 600;
constexpr auto STARTING_HEIGHT = 900;

// @Hack:
i32          resized_width  = STARTING_WIDTH;
i32          resized_height = STARTING_HEIGHT;

bool         should_quit = false;
bool         window_dimension_set = false; // @Fixme: unhandled
Display     *x_global_display = NULL;
bool         was_window_resized_this_frame = true; // Set to true to resize on first frame
f32          windowed_aspect_ratio_h_over_w = 0.0f;

String dir_of_running_exe;

RArr<Catalog_Base*>     all_catalogs;
Shader_Catalog          shader_catalog;
Texture_Catalog         texture_catalog;

void init_context()
{
    global_context.allocator.proc    = __default_allocator;
    global_context.allocator.data    = NULL; // Since regular heap malloc doesn't have a pointer to memory.

    global_context.temporary_storage = &__default_temporary_storage;

    global_context.current_level_set = NULL;
}

void glfw_error_callback(i32 error, const char *description)
{
    logprint("GLFW ERROR", "[%d]: %s", error, description);
    exit(1);
}

void my_hotloader_callback(Asset_Change *change, bool handled)
{
    if (handled) return;

    auto full_name = change->full_name;
    auto [short_name, ext] = chop_and_lowercase_extension(change->short_name);

    logprint("hotloader_callback", "Unhandled non-catalog asset change: %s\n", temp_c_string(full_name));
}

void glfw_window_size_callback(GLFWwindow *window, i32 new_width, i32 new_height)
{
    glViewport(0, 0, new_width, new_height);

    resized_width  = new_width;
    resized_height = new_height;
    was_window_resized_this_frame = true;
}

void init_gl(i32 render_target_width, i32 render_target_height, bool vsync = true, bool windowed = true)
{
    // Set the error callback first before doing anything
    glfwSetErrorCallback(glfw_error_callback);

    // Handle error
    assert(glfwInit() == GLFW_TRUE);

    // Hints the about-to-created window's properties using:
    // glfwWindowHint(i32 hint, i32 value);
    // to reset all the hints to their defaults:
    // glfwDefaultWindowHints();
    // ^ good idea to call this BEFORE setting any hints BEFORE creating any window

#ifdef NON_RESIZABLE_MODE
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
#endif

    glfwWindowHint(GLFW_SCALE_TO_MONITOR, 1); // Make window DPI aware (scaling the game accordingly to the monitor).

    {
        i32 width, height;
        width  = render_target_width;
        height = render_target_height;
        if (height < 1) height = 1;
        windowed_aspect_ratio_h_over_w = height / (f32)width;

        if (!window_dimension_set)
        {
            i32 limit_w, limit_h;

            // @Note: X11 way to get the dimension of the screen
            {
                x_global_display = glfwGetX11Display();
                assert((x_global_display != NULL));

                auto display = x_global_display;
                auto snum    = DefaultScreen(display);

                i32 desktop_height = DisplayHeight(display, snum);
                i32 desktop_width  = DisplayWidth(display, snum);

                // @Fixme: The screen query here is actually wrong because it merges both monitors into one.
                printf("              -----> Desktop width %d, height %d\n", desktop_width, desktop_height);

                limit_h = (i32)desktop_height;
                limit_w = (i32)desktop_width;
            }

            i32 other_limit_h = (i32)(limit_w * windowed_aspect_ratio_h_over_w);
            i32 limit = limit_h < other_limit_h ? limit_h : other_limit_h; // std::min(limit_h, other_limit_h);

            if (height > limit)
            {
                f32 ratio = limit / (f32)height;
                height    = (i32)(height * ratio);
                width     = (i32)(width  * ratio);
            }

            render_target_height = height;
            render_target_width  = width;
        }
    }

    // Creates both the window and context with which to render into
    if (windowed) glfw_window = glfwCreateWindow(render_target_width, render_target_height, (char*)PROGRAM_NAME.data, NULL, NULL);
    else          glfw_window = glfwCreateWindow(render_target_width, render_target_height, (char*)PROGRAM_NAME.data,
                                                 glfwGetPrimaryMonitor(), NULL);

    // Before we can use the context, we need to make it current
    glfwMakeContextCurrent(glfw_window);

    if (!vsync) glfwSwapInterval(0);
    else glfwSwapInterval(1);

    hook_our_input_event_system_to_glfw(glfw_window);

    // Properties that can be controlled after creating the window:
    // - glfwSetWindowSize(GLFWwindow *window, i32 width, i32 height);
    // - glfwSetWindowPos(GLFWwindow *window, i32 x_pos, i32 y_pos);
    // similarly, we can:
    // - glfwGetWindowSize(GLFWwindow *window, i32 *width, i32 *height);
    // - glfwGetWindowPos(GLFWwindow *window, i32 *x_pos, i32 *y_pos);
    // or if you want to set a callback to the size and position of the window when it is changed, do:
    // - glfwSetWindowSizeCallback(...);
    // - glfwSetWindowPosCallback(...);
    glfwSetWindowSizeCallback(glfw_window, glfw_window_size_callback);

    // glfwGetWindowSize() returns the size of the window in pixels, which is skewed if the window system
    // uses scaling.
    // To retrieve the actual size of the framebuffer, use
    // glfwGetFrambuffersize(GLFWwindow *window, i32 *width, i32 *height);
    // you can also do
    // glfwSetFramebuffersizeCallback(...);


    // GLFW provides a mean to associate your own data with a window:
    // void *glfwGetWindowUserPointer(GLFWwindow *window);
    // glfwSetWindowUserPointer(GLFWwindow *window, void *pointer);


    // @Important: NOW COMES THE OPENGL GLUE THAT ALLOWS THE USE OF OPENGL FUNCTIONS
    // this is where we use the "glad.h" lib
    // we must set this up before using any OpenGL functions
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    // check for DSA support
    if (!GLAD_GL_ARB_direct_state_access)
    {
        fprintf(stderr, "GLAD: DSA is not supported\n");
        exit(1);
    }

    {
        auto result = create_texture_rendertarget(STARTING_WIDTH, STARTING_HEIGHT);
        the_offscreen_buffer = result.first;

        // @Fixme: So many similar variable names for sizes
        auto back_buffer_width  = render_target_width;
        auto back_buffer_height = render_target_height;
        
        the_back_buffer = New<Texture_Map>();
        the_back_buffer->width  = back_buffer_width;
        the_back_buffer->height = back_buffer_height;
    }

    object_to_world_matrix = Matrix4(1.0);
    world_to_view_matrix   = Matrix4(1.0);
    view_to_proj_matrix    = Matrix4(1.0);
    object_to_proj_matrix  = Matrix4(1.0);

    num_immediate_vertices = 0;
    // should_vsync           = vsync;
    // in_windowed_mode       = windowed;

    glGenVertexArrays(1, &opengl_is_stupid_vao);
    glGenBuffers(1, &immediate_vbo);
    glGenBuffers(1, &immediate_vbo_indices);

    // @Temporary: Clearing the initial color of the game when loading
    glClearColor(.03, .10, .13, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glfwSwapBuffers(glfw_window);
}

void do_one_frame()
{
    reset_temporary_storage();
    update_time(DT_MAX);

    update_linux_events();
    glfwPollEvents();

    if (was_window_resized_this_frame)
    {
        the_back_buffer->width  = resized_width;
        the_back_buffer->height = resized_height;
        
        deinit_all_font_stuff_on_resize(); // Because it they depends on the height of the render target

        BIG_FONT_SIZE = resized_height * .08f;
    }

    read_input();

    simulate();

    if (game_mode == Game_Mode::SCORE_OVERVIEW)
    {
        show_os_cursor(glfw_window);
    }
    else
    {
        hide_os_cursor(glfw_window);
    }

    draw_game();

    glfwSwapBuffers(glfw_window);

    while (hotloader_process_change()) {}

    if (was_window_resized_this_frame) was_window_resized_this_frame = false; // @Speed
}

int main()
{
    init_context();
    init_time();

    //
    // Getting the directory of the executable.
    //
    auto exe_directory = get_executable_path();

    auto last_slash = find_index_from_right(exe_directory, '/');
    exe_directory.count = last_slash; // Up to but not including the last slash.

    assert(exe_directory.count);

    dir_of_running_exe = copy_string(exe_directory);
    setcwd(dir_of_running_exe);

    reset_temporary_storage();

    //
    // Init all the catalogs.
    //
    init_shader_catalog(&shader_catalog);
    init_texture_catalog(&texture_catalog);

    array_add(&all_catalogs, &shader_catalog.base);
    array_add(&all_catalogs, &texture_catalog.base);

    //
    // Then, we init OpenGL.
    // 
    init_gl(STARTING_WIDTH, STARTING_HEIGHT);

    //
    // Registering the files to the catalogs.
    //
    catalog_loose_files(String("data"), &all_catalogs);

    // white_texture = catalog_find(&texture_catalog, String("white"));

    // We init the shaders (after the catalog_loose_files). For draw.cpp
    init_shaders();

    init_mixer();
    init_game();

    hotloader_init(); 
    hotloader_register_callback(my_hotloader_callback);

    while (!glfwWindowShouldClose(glfw_window))
    {
        if (should_quit) break;

        do_one_frame();
    }

    save_high_score_leader_board();

    shutdown_mixer();

    glfwDestroyWindow(glfw_window);
    glfwTerminate();

    hotloader_shutdown();

    return 0;
}
