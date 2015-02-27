/**************************************************************************
 *
 * Copyright 2015 Codethink Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ivi-controller-input-client-protocol.h"
#include "ilm_control_input.h"
#include "ilm_control_platform.h"


/* GCC visibility */
#if defined(__GNUC__) && __GNUC__ >= 4
#define ILM_EXPORT __attribute__ ((visibility("default")))
#else
#define ILM_EXPORT
#endif

extern struct ilm_control_context ilm_context;

ILM_EXPORT ilmErrorTypes
ilm_setInputAcceptanceOn(t_ilm_surface surfaceID, t_ilm_uint num_seats,
                         t_ilm_string *seats)
{
    return ILM_FAILED;
}

ILM_EXPORT ilmErrorTypes
ilm_getInputAcceptanceOn(t_ilm_surface surfaceID, t_ilm_uint *num_seats,
                         t_ilm_string **seats)
{
    return ILM_FAILED;
}

ILM_EXPORT ilmErrorTypes
ilm_getInputDevices(ilmInputDevice bitmask, t_ilm_uint *num_seats,
                    t_ilm_string **seats)
{
    ilmErrorTypes returnValue = ILM_FAILED;
    struct ilm_control_context *ctx = sync_and_acquire_instance();
    struct seat_context *seat;
    int max_seats = wl_list_length(&ctx->wl.list_seat);
    int seats_added = 0;

    *seats = calloc(max_seats, sizeof **seats);

    if (*seats == NULL) {
        fprintf(stderr, "Failed to allocate memory for input device list\n");
        release_instance();
        return ILM_FAILED;
    }

    wl_list_for_each(seat, &ctx->wl.list_seat, link) {
        returnValue = ILM_SUCCESS;

        if ((seat->capabilities & bitmask) == 0)
            continue;

        *seats[seats_added] = strdup(seat->seat_name);
        if (*seats[seats_added] == NULL) {
            int j;
            fprintf(stderr, "Failed to duplicate seat name %s\n",
                    seat->seat_name);
            for (j = 0; j < seats_added; j++)
                free(*seats[j]);
            free(*seats);
            *seats = NULL;
            returnValue = ILM_FAILED;
            break;
        }

        seats_added++;
    }
    *num_seats = seats_added;
    release_instance();
    return returnValue;
}

ILM_EXPORT ilmErrorTypes
ilm_getInputDeviceCapabilities(t_ilm_string seat_name, ilmInputDevice *bitmask)
{
    ilmErrorTypes returnValue = ILM_FAILED;
    struct ilm_control_context *ctx = sync_and_acquire_instance();
    struct seat_context *seat;

    wl_list_for_each(seat, &ctx->wl.list_seat, link) {
        if (strcmp(seat_name, seat->seat_name) == 0) {
            *bitmask = seat->capabilities;
            returnValue = ILM_SUCCESS;
        }
    }

    release_instance();
    return returnValue;
}

ILM_EXPORT ilmErrorTypes
ilm_setInputFocus(t_ilm_surface *surfaceIDs, t_ilm_uint num_surfaces,
                  ilmInputDevice bitmask, t_ilm_bool is_set)
{
    struct ilm_control_context *ctx = sync_and_acquire_instance();
    int i;

    for (i = 0; i < num_surfaces; i++) {
        struct surface_context *ctx_surf;
        int found_surface = 0;
        wl_list_for_each(ctx_surf, &ctx->wl.list_surface, link) {
            if (ctx_surf->id_surface == surfaceIDs[i]) {
                found_surface = 1;
                break;
            }
        }

        if (!found_surface) {
            fprintf(stderr, "Surface %d was not found\n", surfaceIDs[i]);
            continue;
        }

        ivi_controller_input_set_input_focus(ctx->wl.input_controller,
                                             ctx_surf->id_surface,
                                             bitmask, is_set);
    }
    release_instance();
    return ILM_SUCCESS;
}

ILM_EXPORT ilmErrorTypes
ilm_getInputFocus(t_ilm_surface **surfaceIDs, ilmInputDevice **bitmasks,
                  t_ilm_uint *num_ids)
{
    struct ilm_control_context *ctx = sync_and_acquire_instance();
    int i = 0;
    struct surface_context *ctx_surf;

    *num_ids = wl_list_length(&ctx->wl.list_surface);
    *surfaceIDs = calloc(*num_ids, sizeof **surfaceIDs);

    if (*surfaceIDs == NULL) {
        fprintf(stderr, "Failed to allocate memory for surface ID list\n");
        release_instance();
        return ILM_FAILED;
    }

    *bitmasks = calloc(*num_ids, sizeof **bitmasks);
    if (*bitmasks == NULL) {
        fprintf(stderr, "Failed to allocate memory for bitmask list\n");
        free(*surfaceIDs);
        release_instance();
        return ILM_FAILED;
    }

    wl_list_for_each(ctx_surf, &ctx->wl.list_surface, link) {
        (*surfaceIDs)[i] = ctx_surf->id_surface;
        (*bitmasks)[i] = ctx_surf->prop.focus;
        i++;
    }

    release_instance();
    return ILM_SUCCESS;

}
