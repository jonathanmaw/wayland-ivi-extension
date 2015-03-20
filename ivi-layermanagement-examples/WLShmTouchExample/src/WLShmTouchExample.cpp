/***************************************************************************
 *
 * Copyright (c) 2014 Codethink Limited
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <string.h>

#include <string>
#include <fstream>
#include <sstream>

#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include "ilm_client.h"
#include "ilm_control.h"
#include "ilm_types.h"


struct display {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_list seats;
};

struct seat {
    struct wl_list link;
    struct wl_seat *seat;
    struct wl_touch *touch;
    struct display *display;
};

struct window {
    struct display *display;
    int width;
    int height;
    struct wl_buffer *buffer;
    void *shm_data;
    struct wl_surface *surface;
    bool drawGreen;
};

static void paint_pixels(struct window *window)
{
    uint32_t red = 0xFFFF0000;
    uint32_t green = 0xFF00FF00;

    for (uint32_t *pixel = (uint32_t *)window->shm_data;
         pixel < (uint32_t *)window->shm_data + window->width * window->height;
         pixel++)
    {
        *pixel = window->drawGreen ? green : red;
    }
}

static void touch_handle_down(void *data, struct wl_touch *wl_touch,
                              uint32_t serial, uint32_t time,
                              struct wl_surface *surface, int32_t id,
                              wl_fixed_t x_w, wl_fixed_t y_w)
{
    struct window *window = (struct window *)wl_touch_get_user_data(wl_touch);

    window->drawGreen = !window->drawGreen;
    paint_pixels(window);
    wl_surface_attach(window->surface, window->buffer, 0, 0);
    wl_surface_damage(window->surface, 0, 0, window->width, window->height);
    wl_surface_commit(window->surface);
}

static void touch_handle_up(void *data, struct wl_touch *wl_touch,
                            uint32_t serial, uint32_t time, int32_t id)
{
}

static void touch_handle_motion(void *data, struct wl_touch *wl_touch,
                                uint32_t time, int32_t id, wl_fixed_t x_w,
                                wl_fixed_t y_w)
{
}

static void touch_handle_frame(void *data, struct wl_touch *wl_touch)
{
}

static void touch_handle_cancel(void *data, struct wl_touch *wl_touch)
{
}

static const struct wl_touch_listener touch_listener = {
    touch_handle_down,
    touch_handle_up,
    touch_handle_motion,
    touch_handle_frame,
    touch_handle_cancel
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    struct seat *s = (struct seat *)data;

    if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !s->touch)
    {
        printf("Adding touch\n");
        s->touch = wl_seat_get_touch(seat);
        wl_touch_add_listener(s->touch, &touch_listener, data);
    }
    else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && s->touch)
    {
        printf("Removing touch\n");
        wl_touch_destroy(s->touch);
        s->touch = NULL;
    }
}

static const struct wl_seat_listener seat_listener = {
    seat_handle_capabilities,
    NULL
};

static void shm_format(void *data, struct wl_shm* wlShm, uint32_t format)
{
    printf("SHM format supported: 0x%x\n", format);
}

struct wl_shm_listener shm_listener = {
    shm_format
};

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t id, const char *interface,
                                   uint32_t version)
{
    struct display *d = (struct display *)data;
    if (strcmp(interface, "wl_compositor") == 0)
    {
        d->compositor =
            (struct wl_compositor *)wl_registry_bind(registry, id,
                                                     &wl_compositor_interface, 1);
    }
    else if (strcmp(interface, "wl_shm") == 0)
    {
        d->shm =
            (struct wl_shm *)wl_registry_bind(registry, id, &wl_shm_interface, 1);
        wl_shm_add_listener(d->shm, &shm_listener, data);
    }
    else if (strcmp(interface, "wl_seat") == 0)
    {
        printf("Received seat\n");
        struct seat *seat = (struct seat *)calloc(1, sizeof *seat);
        seat->display = d;
        wl_list_insert(&d->seats, &seat->link);
        seat->seat =
            (struct wl_seat *)wl_registry_bind(registry, id,
                                               &wl_seat_interface, 1);
        wl_seat_add_listener(seat->seat, &seat_listener, seat);
    }
}

static void handle_global_remove(void *data, struct wl_registry *registry, uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    handle_global_remove
};

static bool gRunning = true;

static void sigint_handler(int signum)
{
    gRunning = false;
}

static int create_shm_file(off_t size)
{
    static const std::string path_template("/weston-shared-XXXXXX");
    const char *path = getenv("XDG_RUNTIME_DIR");

    int fd = -1;

    if (path == NULL)
    {
        fprintf(stderr, "Failed to get XDG_RUNTIME_DIR\n");
        return -1;
    }
    std::string full_template = std::string(path) + path_template;

    fd = mkstemp((char *)full_template.c_str());
    if (fd < 0)
    {
        fprintf(stderr, "mkstemp failed for path %s: %m\n", full_template.c_str());
        return -1;
    }

    if (ftruncate(fd, size) < 0)
    {
        fprintf(stderr, "ftruncate failed: %m\n");
        close(fd);
        return -1;
    }

    return fd;
}

static void destroy_window(struct window *window)
{
    wl_surface_destroy(window->surface);
    wl_buffer_destroy(window->buffer);
}

static struct window * create_window(struct display* display, int width,
                                     int height)
{
    struct window *window;
    struct wl_shm_pool *pool;
    void *data;
    int stride, size, fd;
    unsigned int depth;

    if (display == NULL)
    {
        fprintf(stderr, "display is NULL in create_window\n");
        return NULL;
    }

    window = (struct window *)calloc(1, sizeof *window);
    if (window == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for window\n");
        return NULL;
    }

    window->display = display;
    window->width = width;
    window->height = height;
    window->surface = wl_compositor_create_surface(display->compositor);
    if (window->surface == NULL)
    {
        fprintf(stderr, "Failed to create surface from compositor\n");
        goto fail;
    }

    depth = 4; // Only care about format WL_SHM_FORMAT_ARGB8888
    stride = width * depth;
    size = stride * height;

    fd = create_shm_file(size);
    if (fd < 0)
    {
        fprintf(stderr, "Creating a buffer file of size %d B failed: %m\n",
                size);
    }

    data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
    {
        fprintf(stderr, "mmap failed: %m\n");
        goto fail_fd;
    }

    pool = wl_shm_create_pool(display->shm, fd, size);
    if (pool == NULL)
    {
        fprintf(stderr, "Failed to create pool: %m\n");
        goto fail_fd;
    }

    window->buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
                                               stride, WL_SHM_FORMAT_ARGB8888);
    if (window->buffer == NULL)
    {
        fprintf(stderr, "Failed to create SHM buffer: %m\n");
        goto fail_pool;
    }

    wl_shm_pool_destroy(pool);
    close(fd);

    window->shm_data = data;

    return window;

    fail_pool:
        wl_shm_pool_destroy(pool);
    fail_fd:
        close(fd);
    fail:
        free(window);
        return NULL;
}

static void destroy_display(struct display *display)
{
    if (display->shm)
        wl_shm_destroy(display->shm);

    if (display->compositor)
        wl_compositor_destroy(display->compositor);

    wl_registry_destroy(display->registry);
    wl_display_flush(display->display);
    wl_display_disconnect(display->display);
    free(display);
}

static bool touch_exists(struct display *display)
{
    struct seat *seat;
    wl_list_for_each(seat, &display->seats, link) {
        if (seat->touch)
            return true;
    }
    return false;
}

static struct display* create_display()
{
    struct display *display;
    display = (struct display *)calloc(1, sizeof *display);
    if (display == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for display\n");
        return NULL;
    }

    wl_list_init(&display->seats);

    display->display = wl_display_connect(NULL);
    if (display->display == NULL)
    {
        fprintf(stderr, "display->display is NULL\n");
        goto cleanup;
    }

    display->registry = wl_display_get_registry(display->display);
    if (display->registry == NULL)
    {
        fprintf(stderr, "display->registry is NULL\n");
        goto cleanup;
    }

    wl_registry_add_listener(display->registry, &registry_listener, display);

    wl_display_roundtrip(display->display);
    if (display->shm == NULL)
    {
        fprintf(stderr, "display->shm is NULL\n");
        goto cleanup;
    }

    wl_display_roundtrip(display->display); //TODO: Understand why wl_display_roundtrip is called twice

    if (!touch_exists(display))
    {
        fprintf(stderr, "No touch devices\n");
        goto cleanup;
    }

    return display;

    cleanup:
        free(display);
        return NULL;
}



void usage(char *program, FILE *stream)
{
        fprintf(stream,
                "Usage: %s [OPTIONS]\n"
                "Where options are:\n"
                "  -layer=[LAYER]\n"
                "  -surface=[SURFACE]\n"
                "  -width=[WIDTH]\n"
                "  -height=[HEIGHT]\n"
                "  -x=[POS_X]\n"
                "  -y=[POS_Y]\n"
                "  -help\n",
                program);
}

void
set_touch_user_data(struct display *display, void *data)
{
    struct seat *seat;
    wl_list_for_each(seat, &display->seats, link)
        if (seat->touch)
            wl_touch_set_user_data(seat->touch, data);
}

int main(int argc, char **argv)
{
    int exit_code = EXIT_SUCCESS;
    int ret = 0;
    ilmErrorTypes error = ILM_FAILED;

    t_ilm_surface surfaceID = 2655;
    t_ilm_layer layerID = 2350;
    int x = 0;
    int y = 0;
    int width = 1024;
    int height = 768;

    struct display *display;
    struct window *window;

    struct sigaction sigint;

    // parse args

    for (int i=1; i < argc; i++)
    {
        
        std::string argstr(argv[i]);
        size_t mid_point = argstr.find('=');

        std::string key(argstr, 0, mid_point);
        std::string value(argstr, mid_point + 1);
        std::istringstream iss(value);

        if (key.compare("-x") == 0)
            iss >> x;
        else if (key.compare("-y") == 0)
            iss >> y;
        else if (key.compare("-width") == 0)
            iss >> width;
        else if (key.compare("-height") == 0)
            iss >> height;
        else if (key.compare("-surface") == 0)
            iss >> surfaceID;
        else if (argstr.compare("-help") == 0)
        {
            usage(argv[0], stdout);
            goto exit;
        }
        else
        {
            fprintf(stderr, "Unknown option %s\n", argv[i]);
            usage(argv[0], stderr);
            exit_code = EXIT_FAILURE;
            goto exit;
        }
    }

    // Set the signal handler
    sigint.sa_handler = sigint_handler;
    sigemptyset(&sigint.sa_mask);
    sigint.sa_flags = SA_RESETHAND;
    sigaction(SIGINT, &sigint, NULL);
    sigaction(SIGTERM, &sigint, NULL);

    // Initialize things
    error = ilm_init();
    if (error != ILM_SUCCESS)
    {
        fprintf(stderr, "Failed to create surface: %s\n",
                ILM_ERROR_STRING(error));
        exit_code = EXIT_FAILURE;
        goto exit;
    }

    display = create_display();
    if (display == NULL)
    {
        fprintf(stderr, "Failed to create display\n");
        exit_code = EXIT_FAILURE;
        goto exit_ilm;
    }

    window = create_window(display, width, height);
    if (window == NULL)
    {
        fprintf(stderr, "Failed to create window\n");
        exit_code = EXIT_FAILURE;
        goto exit_display;
    }

    // Make sure wl_surface exists before creating layout surface.
    wl_display_roundtrip(display->display);

    set_touch_user_data(display, (void *)window);
    // Set various bits of layermanager information

    printf("*** %s: nativehandle=%p\n", __FUNCTION__, window->surface);
    error = ilm_surfaceCreate((t_ilm_nativehandle)window->surface, width, height, ILM_PIXELFORMAT_RGBA_8888, &surfaceID);
    if (error != ILM_SUCCESS)
    {
        fprintf(stderr, "Failed to create surface: %s\n",
                ILM_ERROR_STRING(error));
        exit_code = EXIT_FAILURE;
        goto exit_window;
    }

    error = ilm_surfaceSetDestinationRectangle(surfaceID, x, y, width, height);
    if (error != ILM_SUCCESS)
    {
        fprintf(stderr, "Failed to set surface destination rectangle: %s\n",
                ILM_ERROR_STRING(error));
        exit_code = EXIT_FAILURE;
        goto exit_surface;
    }

    error = ilm_surfaceSetSourceRectangle(surfaceID, 0, 0, width, height);
    if (error != ILM_SUCCESS)
    {
        fprintf(stderr, "Failed to set surface source rectangle: %s\n",
                ILM_ERROR_STRING(error));
        exit_code = EXIT_FAILURE;
        goto exit_surface;
    }

    error = ilm_surfaceSetVisibility(surfaceID, ILM_TRUE); // see ilm_control.h
    if (error != ILM_SUCCESS)
    {
        fprintf(stderr, "Failed to set surface opacity: %s\n",
                ILM_ERROR_STRING(error));
        exit_code = EXIT_FAILURE;
        goto exit_surface;
    }

    error = ilm_commitChanges();
    if (error != ILM_SUCCESS)
    {
        fprintf(stderr, "Failed to commit changes: %s\n",
                ILM_ERROR_STRING(error));
        exit_code = EXIT_FAILURE;
        goto exit_surface;
    }

    // Paint once
    paint_pixels(window);

    wl_surface_attach(window->surface, window->buffer, 0, 0);
    wl_surface_damage(window->surface, 0, 0, window->width, window->height);
    wl_surface_commit(window->surface);

    // Enter run loop
    printf("Entering run loop\n");
    while (gRunning && ret != -1)
        ret = wl_display_dispatch(display->display);


    exit_surface:
        error = ilm_surfaceRemove(surfaceID);
        if (error != ILM_SUCCESS)
        {
            fprintf(stderr, "Failed to remove surface: %s\n",
                    ILM_ERROR_STRING(error));
            exit_code = -1;
        }

        error = ilm_commitChanges();
        if (error != ILM_SUCCESS)
        {
            fprintf(stderr, "Failed to commit changes: %s\n",
                    ILM_ERROR_STRING(error));
            exit_code = -1;
        }
    exit_window:
        destroy_window(window);
    exit_display:
        destroy_display(display);
    exit_ilm:
        ilm_destroy();
    exit:
        return exit_code;
}
