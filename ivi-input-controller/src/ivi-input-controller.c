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
#include "ilm_types.h"

#include "ivi-controller-server-protocol.h"
#include "ivi-layout-export.h"

struct seat_ctx {
    struct input_context *input_ctx;
    struct weston_keyboard_grab keyboard_grab;
    struct weston_pointer_grab pointer_grab;
    struct weston_touch_grab touch_grab;
    struct wl_listener updated_caps_listener;
    struct wl_listener destroy_listener;
};

struct surface_ctx {
    struct wl_list link;
    ilmInputDevice focus;
    struct ivi_layout_surface *layout_surface;
};

struct input_controller {
    struct wl_list link;
    struct wl_resource *resource;
    struct wl_client *client;
    uint32_t id;
    struct input_context *input_context;
};

struct input_context {
    struct wl_listener seat_create_listener;
    struct wl_list controller_list;
    struct wl_list surface_list;
    struct weston_compositor *compositor;
};

static void
send_input_focus(struct input_context *ctx, t_ilm_surface surface_id,
                 ilmInputDevice device, t_ilm_bool enabled)
{
    struct input_controller *controller;
    wl_list_for_each(controller, &ctx->controller_list, link) {
        ivi_controller_input_send_input_focus(controller->resource, surface_id,
                                              device, enabled);
    }
}

static void
keyboard_grab_key(struct weston_keyboard_grab *grab, uint32_t time,
                  uint32_t key, uint32_t state)
{
    struct seat_ctx *seat_ctx = wl_container_of(grab, seat_ctx, keyboard_grab);
    struct surface_ctx *surf_ctx;
    struct wl_display *display = grab->keyboard->seat->compositor->wl_display;

    wl_list_for_each(surf_ctx, &seat_ctx->input_ctx->surface_list, link) {
        struct weston_surface *surface;
        struct wl_resource *resource;
        struct wl_client *surface_client;
        uint32_t serial;
        if (!(surf_ctx->focus & ILM_INPUT_DEVICE_KEYBOARD))
            continue;

        /* TODO: Seat acceptance */

        surface = ivi_layout_surfaceGetWestonSurface(surf_ctx->layout_surface);
        surface_client = wl_resource_get_client(surface->resource);
        serial = wl_display_next_serial(display);

        wl_resource_for_each(resource, &grab->keyboard->resource_list) {
            if (wl_resource_get_client(resource) != surface_client)
                continue;
            wl_keyboard_send_key(resource, serial, time, key, state);
        }

        wl_resource_for_each(resource, &grab->keyboard->focus_resource_list) {
            if (wl_resource_get_client(resource) != surface_client)
                continue;
            wl_keyboard_send_key(resource, serial, time, key, state);
        }
    }
}

static void
keyboard_grab_modifiers(struct weston_keyboard_grab *grab, uint32_t serial,
                        uint32_t mods_depressed, uint32_t mods_latched,
                        uint32_t mods_locked, uint32_t group)
{
    struct seat_ctx *seat_ctx = wl_container_of(grab, seat_ctx, keyboard_grab);
    struct surface_ctx *surf_ctx;
    struct wl_display *display = grab->keyboard->seat->compositor->wl_display;

    wl_list_for_each(surf_ctx, &seat_ctx->input_ctx->surface_list, link) {
        struct weston_surface *surface;
        struct wl_resource *resource;
        struct wl_client *surface_client;
        uint32_t serial;

        /* Keyboard modifiers go to surfaces with pointer focus as well */
        if (!(surf_ctx->focus
              & (ILM_INPUT_DEVICE_KEYBOARD | ILM_INPUT_DEVICE_POINTER)))
            continue;

        /* TODO: Seat acceptance */

        surface = ivi_layout_surfaceGetWestonSurface(surf_ctx->layout_surface);
        surface_client = wl_resource_get_client(surface->resource);
        serial = wl_display_next_serial(display);

        wl_resource_for_each(resource, &grab->keyboard->resource_list) {
            if (wl_resource_get_client(resource) != surface_client)
                continue;
            wl_keyboard_send_modifiers(resource, serial, mods_depressed,
			               mods_latched, mods_locked, group);
        }

        wl_resource_for_each(resource, &grab->keyboard->focus_resource_list) {
            if (wl_resource_get_client(resource) != surface_client)
                continue;
            wl_keyboard_send_modifiers(resource, serial, mods_depressed,
			               mods_latched, mods_locked, group);
        }
    }
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
    struct seat_ctx *seat = wl_container_of(grab, seat, pointer_grab);
    struct surface_ctx *surf_ctx;

    weston_pointer_move(grab->pointer, x, y);

    /* Get coordinates relative to the surface the pointer is in.
     * This might cause weirdness if there are multiple surfaces
     * that are accepted by this pointer's seat and have focus */

    /* For each surface_ctx, check for focus and send */
    wl_list_for_each(surf_ctx, &seat->input_ctx->surface_list, link) {
        struct weston_surface *surf;
        struct wl_resource *resource;
        struct wl_client *surface_client;
        struct weston_view *view;
        wl_fixed_t sx, sy;

        if (!(surf_ctx->focus & ILM_INPUT_DEVICE_POINTER))
            continue;

        /* Assume one view per surface */
        surf = ivi_layout_surfaceGetWestonSurface(surf_ctx->layout_surface);
        view = wl_container_of(surf->views.next, view, surface_link);

        /* Do not send motion events for coordinates outside the surface */
        weston_view_from_global_fixed(view, x, y, &sx, &sy);
        if (!pixman_region32_contains_point(&surf->input, wl_fixed_to_int(sx),
                                            wl_fixed_to_int(sy), NULL))
            continue;

        surface_client = wl_resource_get_client(surf->resource);
        wl_resource_for_each(resource, &grab->pointer->resource_list) {
            if (wl_resource_get_client(resource) != surface_client)
                continue;

            wl_pointer_send_motion(resource, time, sx, sy);
        }

        wl_resource_for_each(resource, &grab->pointer->focus_resource_list) {
            if (wl_resource_get_client(resource) != surface_client)
                continue;

            wl_pointer_send_motion(resource, time, sx, sy);
        }
    }
}

static void
pointer_grab_button(struct weston_pointer_grab *grab, uint32_t time,
                    uint32_t button, uint32_t state)
{
    struct seat_ctx *seat = wl_container_of(grab, seat, pointer_grab);
    struct weston_pointer *pointer = grab->pointer;
    struct weston_compositor *compositor = pointer->seat->compositor;
    struct wl_display *display = compositor->wl_display;
    struct surface_ctx *surf_ctx;
    wl_fixed_t sx, sy;
    struct weston_view *view;

    view = weston_compositor_pick_view(compositor, pointer->x, pointer->y,
                                       &sx, &sy);
    if (view == NULL)
        return;

    /* For each surface_ctx, check for focus and send */
    wl_list_for_each(surf_ctx, &seat->input_ctx->surface_list, link) {
        struct weston_surface *surf;
        struct wl_resource *resource;
        struct wl_client *surface_client;
        uint32_t serial;

        surf = ivi_layout_surfaceGetWestonSurface(surf_ctx->layout_surface);

        /* TODO: Filter by seat acceptance */

        /* Send to surfaces that have pointer focus */
        if (surf_ctx->focus & ILM_INPUT_DEVICE_POINTER) {

            surface_client = wl_resource_get_client(surf->resource);
            serial = wl_display_next_serial(display);
            wl_resource_for_each(resource, &grab->pointer->resource_list) {
                if (wl_resource_get_client(resource) != surface_client)
                    continue;

                wl_pointer_send_button(resource, serial, time, button, state);
            }

            wl_resource_for_each(resource, &grab->pointer->focus_resource_list) {
                if (wl_resource_get_client(resource) != surface_client)
                    continue;

                wl_pointer_send_button(resource, serial, time, button, state);
            }
        }

        /* If a button release, set pointer focus to this surface */
        if (pointer->button_count == 0
            && state == WL_POINTER_BUTTON_STATE_RELEASED) {
            if (view->surface == surf) {
                surf_ctx->focus |= ILM_INPUT_DEVICE_POINTER;
                send_input_focus(seat->input_ctx,
                                 ivi_layout_getIdOfSurface(surf_ctx->layout_surface),
                                 ILM_INPUT_DEVICE_POINTER, ILM_TRUE);
            } else {
                surf_ctx->focus &= ~ILM_INPUT_DEVICE_POINTER;
                send_input_focus(seat->input_ctx,
                                 ivi_layout_getIdOfSurface(surf_ctx->layout_surface),
                                 ILM_INPUT_DEVICE_POINTER, ILM_FALSE);
            }
        }
    }
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
    struct seat_ctx *seat = wl_container_of(grab, seat, touch_grab);
    struct wl_display *display = grab->touch->seat->compositor->wl_display;
    struct surface_ctx *surf_ctx;

    /* For each surface_ctx, check for focus and send */
    wl_list_for_each(surf_ctx, &seat->input_ctx->surface_list, link) {
        struct weston_surface *surf;
        struct wl_resource *resource;
        struct wl_client *surface_client;
        uint32_t serial;

        surf = ivi_layout_surfaceGetWestonSurface(surf_ctx->layout_surface);

        /* TODO: Exclude surfaces that don't accept this seat */

        /* Touches set touch focus */
        if (grab->touch->num_tp == 1) {
            if (surf == grab->touch->focus->surface) {
                surf_ctx->focus |= ILM_INPUT_DEVICE_TOUCH;
                send_input_focus(seat->input_ctx,
                                 ivi_layout_getIdOfSurface(surf_ctx->layout_surface),
                                 ILM_INPUT_DEVICE_TOUCH, ILM_TRUE);
            } else {
                surf_ctx->focus &= ~ILM_INPUT_DEVICE_TOUCH;
                send_input_focus(seat->input_ctx,
                                 ivi_layout_getIdOfSurface(surf_ctx->layout_surface),
                                 ILM_INPUT_DEVICE_TOUCH, ILM_FALSE);
            }
        }

        /* This code below is slightly redundant, since we have already
         * decided only one surface has touch focus */
        if (!(surf_ctx->focus & ILM_INPUT_DEVICE_TOUCH))
            continue;

        surface_client = wl_resource_get_client(surf->resource);
        serial = wl_display_next_serial(display);
        wl_resource_for_each(resource, &grab->touch->resource_list) {
            if (wl_resource_get_client(resource) != surface_client)
                continue;

            wl_touch_send_down(resource, serial, time, surf->resource,
                               touch_id, sx, sy);
        }
        wl_resource_for_each(resource, &grab->touch->focus_resource_list) {
            if (wl_resource_get_client(resource) != surface_client)
                continue;

            wl_touch_send_down(resource, serial, time, surf->resource,
                               touch_id, sx, sy);
        }
    }
    

}

static void
touch_grab_up(struct weston_touch_grab *grab, uint32_t time, int touch_id)
{
    struct seat_ctx *seat = wl_container_of(grab, seat, touch_grab);
    struct wl_display *display = grab->touch->seat->compositor->wl_display;
    struct surface_ctx *surf_ctx;

    /* For each surface_ctx, check for focus and send */
    wl_list_for_each(surf_ctx, &seat->input_ctx->surface_list, link) {
        struct weston_surface *surf;
        struct wl_resource *resource;
        struct wl_client *surface_client;
        uint32_t serial;
        
        /* TODO: Exclude surfaces that don't accept this seat */

        if (!(surf_ctx->focus & ILM_INPUT_DEVICE_TOUCH))
            continue;

        surf = ivi_layout_surfaceGetWestonSurface(surf_ctx->layout_surface);
        surface_client = wl_resource_get_client(surf->resource);
        serial = wl_display_next_serial(display);
        wl_resource_for_each(resource, &grab->touch->resource_list) {
            if (wl_resource_get_client(resource) != surface_client)
                continue;

            wl_touch_send_up(resource, serial, time, touch_id);
        }

        wl_resource_for_each(resource, &grab->touch->focus_resource_list) {
            if (wl_resource_get_client(resource) != surface_client)
                continue;

            wl_touch_send_up(resource, serial, time, touch_id);
        }

        /* Touches unset touch focus */
        if (grab->touch->num_tp == 0) {
            if (surf == grab->touch->focus->surface)
                surf_ctx->focus &= ~ILM_INPUT_DEVICE_TOUCH;
                send_input_focus(seat->input_ctx,
                                 ivi_layout_getIdOfSurface(surf_ctx->layout_surface),
                                 ILM_INPUT_DEVICE_TOUCH, ILM_FALSE);
        }
    }
}

static void
touch_grab_motion(struct weston_touch_grab *grab, uint32_t time, int touch_id,
                  wl_fixed_t sx, wl_fixed_t sy)
{
    struct seat_ctx *seat = wl_container_of(grab, seat, touch_grab);
    struct surface_ctx *surf_ctx;
    
    /* For each surface_ctx, check for focus and send */
    wl_list_for_each(surf_ctx, &seat->input_ctx->surface_list, link) {
        struct weston_surface *surf;
        struct wl_resource *resource;
        struct wl_client *surface_client;
        
        if (!(surf_ctx->focus & ILM_INPUT_DEVICE_TOUCH))
            continue;

        surf = ivi_layout_surfaceGetWestonSurface(surf_ctx->layout_surface);
        surface_client = wl_resource_get_client(surf->resource);
        wl_resource_for_each(resource, &grab->touch->resource_list) {
            if (wl_resource_get_client(resource) != surface_client)
                continue;

            wl_touch_send_motion(resource, time, touch_id, sx, sy);
        }

        wl_resource_for_each(resource, &grab->touch->focus_resource_list) {
            if (wl_resource_get_client(resource) != surface_client)
                continue;

            wl_touch_send_motion(resource, time, touch_id, sx, sy);
        }
    }
}

static void
touch_grab_frame(struct weston_touch_grab *grab)
{
    struct seat_ctx *seat = wl_container_of(grab, seat, touch_grab);
    struct surface_ctx *surf_ctx;

    /* For each surface_ctx, check for focus and send */
    wl_list_for_each(surf_ctx, &seat->input_ctx->surface_list, link) {
        struct weston_surface *surf;
        struct wl_resource *resource;
        struct wl_client *surface_client;
        
        if (!(surf_ctx->focus & ILM_INPUT_DEVICE_TOUCH))
            continue;

        surf = ivi_layout_surfaceGetWestonSurface(surf_ctx->layout_surface);
        surface_client = wl_resource_get_client(surf->resource);
        wl_resource_for_each(resource, &grab->touch->resource_list) {
            if (wl_resource_get_client(resource) != surface_client)
                continue;

            wl_touch_send_frame(resource);
        }

        wl_resource_for_each(resource, &grab->touch->focus_resource_list) {
            if (wl_resource_get_client(resource) != surface_client)
                continue;

            wl_touch_send_frame(resource);
        }
    }
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

static uint32_t
get_seat_capabilities(const struct weston_seat *seat)
{
    uint32_t caps = 0;
    if (seat->keyboard_device_count > 0)
        caps |= ILM_INPUT_DEVICE_KEYBOARD;
    if (seat->pointer_device_count > 0)
        caps |= ILM_INPUT_DEVICE_POINTER;
    if (seat->touch_device_count > 0)
        caps |= ILM_INPUT_DEVICE_TOUCH;
    return caps;
}

static void
handle_seat_updated_caps(struct wl_listener *listener, void *data)
{
    struct weston_seat *seat = data;
    struct seat_ctx *ctx = wl_container_of(listener, ctx,
                                           updated_caps_listener);
    struct input_controller *controller;
    if (seat->keyboard && seat->keyboard != ctx->keyboard_grab.keyboard) {
        weston_keyboard_start_grab(seat->keyboard, &ctx->keyboard_grab);
    }
    if (seat->pointer && seat->pointer != ctx->pointer_grab.pointer) {
        weston_pointer_start_grab(seat->pointer, &ctx->pointer_grab);
    }
    if (seat->touch && seat->touch != ctx->touch_grab.touch) {
        weston_touch_start_grab(seat->touch, &ctx->touch_grab);
    }

    wl_list_for_each(controller, &ctx->input_ctx->controller_list, link) {
        ivi_controller_input_send_seat_capabilities(controller->resource,
                                                    seat->seat_name,
                                                    get_seat_capabilities(seat));
    }
}

static void
handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct seat_ctx *ctx = wl_container_of(listener, ctx, destroy_listener);
    struct weston_seat *seat = data;
    struct input_controller *controller;

    if (ctx->keyboard_grab.keyboard)
        keyboard_grab_cancel(&ctx->keyboard_grab);
    if (ctx->pointer_grab.pointer)
        pointer_grab_cancel(&ctx->pointer_grab);
    if (ctx->touch_grab.touch)
        touch_grab_cancel(&ctx->touch_grab);

    wl_list_for_each(controller, &ctx->input_ctx->controller_list, link) {
        ivi_controller_input_send_seat_destroyed(controller->resource,
                                                 seat->seat_name);
    }

    free(ctx);
}

static void
handle_seat_create(struct wl_listener *listener, void *data)
{
    struct weston_seat *seat = data;
    struct input_context *input_ctx = wl_container_of(listener, input_ctx,
                                                      seat_create_listener);
    struct input_controller *controller;
    struct seat_ctx *ctx = calloc(1, sizeof *ctx);
    if (ctx == NULL) {
        weston_log("%s: Failed to allocate memory\n", __FUNCTION__);
        return;
    }

    ctx->input_ctx = input_ctx;

    ctx->keyboard_grab.interface = &keyboard_grab_interface;
    ctx->pointer_grab.interface = &pointer_grab_interface;
    ctx->touch_grab.interface= &touch_grab_interface;

    ctx->destroy_listener.notify = &handle_seat_destroy;
    wl_signal_add(&seat->destroy_signal, &ctx->destroy_listener);

    ctx->updated_caps_listener.notify = &handle_seat_updated_caps;
    wl_signal_add(&seat->updated_caps_signal, &ctx->updated_caps_listener);

    wl_list_for_each(controller, &input_ctx->controller_list, link) {
        ivi_controller_input_send_seat_created(controller->resource,
                                               seat->seat_name,
                                               get_seat_capabilities(seat));
    }
}

static void
handle_surface_destroy(struct ivi_layout_surface *layout_surface, void *data)
{
    struct input_context *ctx = data;
    struct surface_ctx *surf, *next;
    int surface_removed = 0;

    wl_list_for_each_safe(surf, next, &ctx->surface_list, link) {
        if (surf->layout_surface == layout_surface) {
            wl_list_remove(&surf->link);
            free(surf);
            surface_removed = 1;
            break;
        }
    }

    if (!surface_removed) {
        weston_log("%s: Warning! surface %d already destroyed\n", __FUNCTION__,
                   ivi_layout_getIdOfSurface(layout_surface));
    }
}

static void
handle_surface_create(struct ivi_layout_surface *layout_surface, void *data)
{
    struct input_context *input_ctx = data;
    struct surface_ctx *ctx;

    wl_list_for_each(ctx, &input_ctx->surface_list, link) {
        if (ctx->layout_surface == layout_surface) {
            weston_log("%s: Warning! surface context already created for"
                       " surface %d\n", __FUNCTION__,
                       ivi_layout_getIdOfSurface(layout_surface));
            break;
        }
    }

    ctx = calloc(1, sizeof *ctx);
    if (ctx == NULL) {
        weston_log("%s: Failed to allocate memory\n", __FUNCTION__);
        return;
    }
    ctx->layout_surface = layout_surface;

    wl_list_insert(&input_ctx->surface_list, &ctx->link);
}

static void
unbind_resource_controller(struct wl_resource *resource)
{
    struct input_controller *controller = wl_resource_get_user_data(resource);
    
    wl_list_remove(&controller->link);

    free(controller);
}

static void
controller_input_set_input_focus(struct wl_client *client,
                                 struct wl_resource *resource,
                                 uint32_t surface, uint32_t device,
                                 int32_t enabled)
{
    struct input_controller *controller = wl_resource_get_user_data(resource);
    struct input_context *ctx = controller->input_context;
    int found_surface = 0;
    struct surface_ctx *surf;
    wl_list_for_each(surf, &ctx->surface_list, link) {
        if (ivi_layout_getIdOfSurface(surf->layout_surface) != surface)
            continue;

        if (enabled == ILM_TRUE)
            surf->focus |= device;
        else
            surf->focus &= ~device;
        send_input_focus(ctx, surface, device, enabled);
        found_surface = 1;
    }

    if (!found_surface) {
        weston_log("%s: surface %d was not found\n", __FUNCTION__, surface);
    }
}

static const struct ivi_controller_input_interface input_implementation = {
    controller_input_set_input_focus
};

static void
bind_ivi_input(struct wl_client *client, void *data,
               uint32_t version, uint32_t id)
{
    struct input_context *ctx = data;
    struct input_controller *controller;
    struct weston_seat *seat;
    struct surface_ctx *surface_ctx;
    controller = calloc(1, sizeof *controller);
    if (controller == NULL) {
        weston_log("%s: Failed to allocate memory for controller\n",
                   __FUNCTION__);
        return;
    }

    controller->input_context = ctx;
    controller->resource =
        wl_resource_create(client, &ivi_controller_input_interface, 1, id);
    wl_resource_set_implementation(controller->resource,
                                   &input_implementation,
                                   controller, unbind_resource_controller);

    controller->client = client;
    controller->id = id;

    wl_list_insert(&ctx->controller_list, &controller->link);

    /* Send seat events for all known seats to the client */
    wl_list_for_each(seat, &ctx->compositor->seat_list, link) {
        ivi_controller_input_send_seat_created(controller->resource,
                                               seat->seat_name,
                                               get_seat_capabilities(seat));
    }
    /* Send focus events for all known surfaces to the client */
    wl_list_for_each(surface_ctx, &ctx->surface_list, link) {
        ivi_controller_input_send_input_focus(controller->resource,
            ivi_layout_getIdOfSurface(surface_ctx->layout_surface),
            surface_ctx->focus, ILM_TRUE);
    }
}

static struct input_context *
create_input_context(struct weston_compositor *ec)
{
    struct input_context *ctx = NULL;
    struct weston_seat *seat;
    ctx = calloc(1, sizeof *ctx);
    if (ctx == NULL) {
        weston_log("%s: Failed to allocate memory for input context\n",
                   __FUNCTION__);
        return NULL;
    }

    ctx->compositor = ec;
    wl_list_init(&ctx->controller_list);
    wl_list_init(&ctx->surface_list);

    /* Add signal handlers for ivi surfaces. Warning: these functions leak
     * memory. */
    ivi_layout_addNotificationCreateSurface(handle_surface_create, ctx);
    ivi_layout_addNotificationRemoveSurface(handle_surface_destroy, ctx);

    ctx->seat_create_listener.notify = &handle_seat_create;
    wl_signal_add(&ec->seat_created_signal, &ctx->seat_create_listener);

    wl_list_for_each(seat, &ec->seat_list, link) {
        handle_seat_create(&ctx->seat_create_listener, seat);
        wl_signal_emit(&seat->updated_caps_signal, seat);
    }

    return ctx;
}

WL_EXPORT int
module_init(struct weston_compositor *ec, int* argc, char *argv[])
{
    struct input_context *ctx = create_input_context(ec);
    if (ctx == NULL) {
        weston_log("%s: Failed to create input context\n", __FUNCTION__);
        return -1;
    }

    if (wl_global_create(ec->wl_display, &ivi_controller_input_interface, 1,
                         ctx, bind_ivi_input) == NULL) {
        return -1;
    }
    weston_log("ivi-input-controller module loaded successfully!\n");
    return 0;
}
