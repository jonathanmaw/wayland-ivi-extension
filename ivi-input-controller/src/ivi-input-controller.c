/*
 * Copyright 2015 Codethink Ltd
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>

#include "weston/compositor.h"

struct seat_ctx {
    struct weston_keyboard_grab keyboard_grab;
    struct weston_pointer_grab pointer_grab;
    struct weston_touch_grab touch_grab;
    struct wl_listener updated_caps_listener;
    struct wl_listener destroy_listener;
};

static struct wl_listener g_seat_create_listener;

static void
keyboard_grab_key(struct weston_keyboard_grab *grab, uint32_t time,
                  uint32_t key, uint32_t state)
{
}

static void
keyboard_grab_modifiers(struct weston_keyboard_grab *grab, uint32_t serial,
                        uint32_t mods_depressed, uint32_t mods_latched,
                        uint32_t mods_locked, uint32_t group)
{
}

static void
keyboard_grab_cancel(struct weston_keyboard_grab *grab)
{
}

static struct weston_keyboard_grab_interface keyboard_grab_interface = {
    keyboard_grab_key,
    keyboard_grab_modifiers,
    keyboard_grab_cancel
};

static void
pointer_grab_focus(struct weston_pointer_grab *grab)
{
}

static void
pointer_grab_motion(struct weston_pointer_grab *grab, uint32_t time,
                    wl_fixed_t x, wl_fixed_t y)
{
}

static void
pointer_grab_button(struct weston_pointer_grab *grab, uint32_t time,
                    uint32_t button, uint32_t state)
{
}

static void
pointer_grab_cancel(struct weston_pointer_grab *grab)
{
}

static struct weston_pointer_grab_interface pointer_grab_interface = {
    pointer_grab_focus,
    pointer_grab_motion,
    pointer_grab_button,
    pointer_grab_cancel
};

static void
touch_grab_down(struct weston_touch_grab *grab, uint32_t time, int touch_id,
                wl_fixed_t sx, wl_fixed_t sy)
{
}

static void
touch_grab_up(struct weston_touch_grab *grab, uint32_t time, int touch_id)
{
}

static void
touch_grab_motion(struct weston_touch_grab *grab, uint32_t time, int touch_id,
                  wl_fixed_t sx, wl_fixed_t sy)
{
}

static void
touch_grab_frame(struct weston_touch_grab *grab)
{
}

static void
touch_grab_cancel(struct weston_touch_grab *grab)
{
}

static struct weston_touch_grab_interface touch_grab_interface = {
    touch_grab_down,
    touch_grab_up,
    touch_grab_motion,
    touch_grab_frame,
    touch_grab_cancel
};

static void
handle_seat_updated_caps(struct wl_listener *listener, void *data)
{
    struct weston_seat *seat = data;
    struct seat_ctx *ctx = wl_container_of(listener, ctx,
                                           updated_caps_listener);
    if (seat->keyboard && seat->keyboard != ctx->keyboard_grab.keyboard) {
        weston_keyboard_start_grab(seat->keyboard, &ctx->keyboard_grab);
    }
    if (seat->pointer && seat->pointer != ctx->pointer_grab.pointer) {
        weston_pointer_start_grab(seat->pointer, &ctx->pointer_grab);
    }
    if (seat->touch && seat->touch != ctx->touch_grab.touch) {
        weston_touch_start_grab(seat->touch, &ctx->touch_grab);
    }
}

static void
handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct seat_ctx *ctx = wl_container_of(listener, ctx, destroy_listener);

    if (ctx->keyboard_grab.keyboard)
        keyboard_grab_cancel(&ctx->keyboard_grab);
    if (ctx->pointer_grab.pointer)
        pointer_grab_cancel(&ctx->pointer_grab);
    if (ctx->touch_grab.touch)
        touch_grab_cancel(&ctx->touch_grab);

    free(ctx);
}

static void
handle_seat_create(struct wl_listener *listener, void *data)
{
    struct weston_seat *seat = data;
    struct seat_ctx *ctx = calloc(1, sizeof *ctx);
    if (ctx == NULL) {
        weston_log("%s: Failed to allocate memory\n", __FUNCTION__);
        return;
    }

    ctx->keyboard_grab.interface = &keyboard_grab_interface;
    ctx->pointer_grab.interface = &pointer_grab_interface;
    ctx->touch_grab.interface= &touch_grab_interface;

    ctx->destroy_listener.notify = &handle_seat_destroy;
    wl_signal_add(&seat->destroy_signal, &ctx->destroy_listener);

    ctx->updated_caps_listener.notify = &handle_seat_updated_caps;
    wl_signal_add(&seat->updated_caps_signal, &ctx->updated_caps_listener);
}

WL_EXPORT int
controller_module_init(struct weston_compositor *ec, int* argc, char *argv[],
                       const struct ivi_controller_interface *interface,
                       size_t interface_version)
{
    struct weston_seat *seat;

    /* Listen to seat creation */
    g_seat_create_listener.notify = &handle_seat_create;
    wl_signal_add(&ec->seat_created_signal, &g_seat_create_listener);

    /* Handle all pre-existing seats */
    wl_list_for_each(seat, &ec->seat_list, link) {
        handle_seat_create (NULL, seat);
        wl_signal_emit(&seat->updated_caps_signal, seat);
    }
    weston_log("ivi-input-controller module loaded successfully!\n");
    return 0;
}
