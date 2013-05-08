/*
 * Copyright © 2008 Kristian Høgsberg
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "config.h"

#include <string.h>

#include "wl-input.h"

static void
lose_pointer_focus (struct wl_listener *listener, void *data)
{
  struct cwl_pointer *pointer =
    wl_container_of (listener, pointer, focus_listener);

  pointer->focus_resource = NULL;
}

static void
lose_keyboard_focus (struct wl_listener *listener, void *data)
{
  struct cwl_keyboard *keyboard =
    wl_container_of (listener, keyboard, focus_listener);

  keyboard->focus_resource = NULL;
}

static void
lose_touch_focus (struct wl_listener *listener, void *data)
{
  struct cwl_touch *touch =
    wl_container_of (listener, touch, focus_listener);

  touch->focus_resource = NULL;
}

static void
default_grab_focus (struct cwl_pointer_grab *grab,
                    struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
  struct cwl_pointer *pointer = grab->pointer;

  if (pointer->button_count > 0)
    return;

  cwl_pointer_set_focus (pointer, surface, x, y);
}

static void
default_grab_motion (struct cwl_pointer_grab *grab,
                     uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
  struct wl_resource *resource;

  resource = grab->pointer->focus_resource;
  if (resource)
    wl_pointer_send_motion (resource, time, x, y);
}

static void
default_grab_button (struct cwl_pointer_grab *grab,
                     uint32_t time, uint32_t button, uint32_t state_w)
{
  struct cwl_pointer *pointer = grab->pointer;
  struct wl_resource *resource;
  uint32_t serial;
  enum wl_pointer_button_state state = state_w;

  resource = pointer->focus_resource;
  if (resource)
    {
      struct wl_display *display = wl_client_get_display (resource->client);
      serial = wl_display_next_serial (display);
      wl_pointer_send_button (resource, serial, time, button, state_w);
    }

  if (pointer->button_count == 0 && state == WL_POINTER_BUTTON_STATE_RELEASED)
    cwl_pointer_set_focus (pointer, pointer->current,
                           pointer->current_x, pointer->current_y);
}

static const struct cwl_pointer_grab_interface default_pointer_grab_interface = {
  default_grab_focus,
  default_grab_motion,
  default_grab_button
};

static void
default_grab_touch_down (struct cwl_touch_grab *grab,
                         uint32_t time,
                         int touch_id, wl_fixed_t sx, wl_fixed_t sy)
{
  struct cwl_touch *touch = grab->touch;
  uint32_t serial;

  if (touch->focus_resource && touch->focus)
    {
      struct wl_display *display =
        wl_client_get_display (touch->focus_resource->client);
      serial = wl_display_next_serial (display);
      wl_touch_send_down (touch->focus_resource, serial, time,
                          &touch->focus->resource, touch_id, sx, sy);
    }
}

static void
default_grab_touch_up (struct cwl_touch_grab *grab,
                       uint32_t time, int touch_id)
{
  struct cwl_touch *touch = grab->touch;
  uint32_t serial;

  if (touch->focus_resource)
    {
      struct wl_display *display =
        wl_client_get_display (touch->focus_resource->client);
      serial = wl_display_next_serial (display);
      wl_touch_send_up (touch->focus_resource, serial, time, touch_id);
    }
}

static void
default_grab_touch_motion (struct cwl_touch_grab *grab,
                           uint32_t time,
                           int touch_id, wl_fixed_t sx, wl_fixed_t sy)
{
  struct cwl_touch *touch = grab->touch;

  if (touch->focus_resource)
    {
      wl_touch_send_motion (touch->focus_resource, time, touch_id, sx, sy);
    }
}

static const struct cwl_touch_grab_interface default_touch_grab_interface = {
  default_grab_touch_down,
  default_grab_touch_up,
  default_grab_touch_motion
};

static void
default_grab_key (struct cwl_keyboard_grab *grab,
                  uint32_t time, uint32_t key, uint32_t state)
{
  struct cwl_keyboard *keyboard = grab->keyboard;
  struct wl_resource *resource;
  uint32_t serial;

  resource = keyboard->focus_resource;
  if (resource)
    {
      struct wl_display *display = wl_client_get_display (resource->client);
      serial = wl_display_next_serial (display);
      wl_keyboard_send_key (resource, serial, time, key, state);
    }
}

static struct wl_resource *
find_resource_for_surface (struct wl_list *list, struct wl_surface *surface)
{
  struct wl_resource *r;

  if (!surface)
    return NULL;

  wl_list_for_each (r, list, link)
  {
    if (r->client == surface->resource.client)
      return r;
  }

  return NULL;
}

static void
default_grab_modifiers (struct cwl_keyboard_grab *grab, uint32_t serial,
                        uint32_t mods_depressed, uint32_t mods_latched,
                        uint32_t mods_locked, uint32_t group)
{
  struct cwl_keyboard *keyboard = grab->keyboard;
  struct cwl_pointer *pointer = keyboard->seat->pointer;
  struct wl_resource *resource, *pr;

  resource = keyboard->focus_resource;
  if (!resource)
    return;

  wl_keyboard_send_modifiers (resource, serial, mods_depressed,
                              mods_latched, mods_locked, group);

  if (pointer && pointer->focus && pointer->focus != keyboard->focus)
    {
      pr = find_resource_for_surface (&keyboard->resource_list,
                                      pointer->focus);
      if (pr)
        {
          wl_keyboard_send_modifiers (pr,
                                      serial,
                                      keyboard->modifiers.mods_depressed,
                                      keyboard->modifiers.mods_latched,
                                      keyboard->modifiers.mods_locked,
                                      keyboard->modifiers.group);
        }
    }
}

static const struct cwl_keyboard_grab_interface
  default_keyboard_grab_interface = {
  default_grab_key,
  default_grab_modifiers,
};

void
cwl_pointer_init (struct cwl_pointer *pointer)
{
  memset (pointer, 0, sizeof *pointer);
  wl_list_init (&pointer->resource_list);
  pointer->focus_listener.notify = lose_pointer_focus;
  pointer->default_grab.interface = &default_pointer_grab_interface;
  pointer->default_grab.pointer = pointer;
  pointer->grab = &pointer->default_grab;
  wl_signal_init (&pointer->focus_signal);

  /* FIXME: Pick better co-ords. */
  pointer->x = wl_fixed_from_int (100);
  pointer->y = wl_fixed_from_int (100);
}

void
cwl_pointer_release (struct cwl_pointer *pointer)
{
  /* XXX: What about pointer->resource_list? */
  if (pointer->focus_resource)
    wl_list_remove (&pointer->focus_listener.link);
}

void
cwl_keyboard_init (struct cwl_keyboard *keyboard)
{
  memset (keyboard, 0, sizeof *keyboard);
  wl_list_init (&keyboard->resource_list);
  wl_array_init (&keyboard->keys);
  keyboard->focus_listener.notify = lose_keyboard_focus;
  keyboard->default_grab.interface = &default_keyboard_grab_interface;
  keyboard->default_grab.keyboard = keyboard;
  keyboard->grab = &keyboard->default_grab;
  wl_signal_init (&keyboard->focus_signal);
}

void
cwl_keyboard_release (struct cwl_keyboard *keyboard)
{
  /* XXX: What about keyboard->resource_list? */
  if (keyboard->focus_resource)
    wl_list_remove (&keyboard->focus_listener.link);
  wl_array_release (&keyboard->keys);
}

void
cwl_touch_init (struct cwl_touch *touch)
{
  memset (touch, 0, sizeof *touch);
  wl_list_init (&touch->resource_list);
  touch->focus_listener.notify = lose_touch_focus;
  touch->default_grab.interface = &default_touch_grab_interface;
  touch->default_grab.touch = touch;
  touch->grab = &touch->default_grab;
  wl_signal_init (&touch->focus_signal);
}

void
cwl_touch_release (struct cwl_touch *touch)
{
  /* XXX: What about touch->resource_list? */
  if (touch->focus_resource)
    wl_list_remove (&touch->focus_listener.link);
}

void
cwl_seat_init (struct cwl_seat *seat)
{
  memset (seat, 0, sizeof *seat);

  wl_signal_init (&seat->destroy_signal);

  seat->selection_data_source = NULL;
  wl_list_init (&seat->base_resource_list);
  wl_signal_init (&seat->selection_signal);
  wl_list_init (&seat->drag_resource_list);
  wl_signal_init (&seat->drag_icon_signal);
}

void
cwl_seat_release (struct cwl_seat *seat)
{
  wl_signal_emit (&seat->destroy_signal, seat);

  if (seat->pointer)
    cwl_pointer_release (seat->pointer);
  if (seat->keyboard)
    cwl_keyboard_release (seat->keyboard);
  if (seat->touch)
    cwl_touch_release (seat->touch);
}

static void
seat_send_updated_caps (struct cwl_seat *seat)
{
  struct wl_resource *r;
  enum wl_seat_capability caps = 0;

  if (seat->pointer)
    caps |= WL_SEAT_CAPABILITY_POINTER;
  if (seat->keyboard)
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  if (seat->touch)
    caps |= WL_SEAT_CAPABILITY_TOUCH;

  wl_list_for_each (r, &seat->base_resource_list, link)
    wl_seat_send_capabilities (r, caps);
}

void
cwl_seat_set_pointer (struct cwl_seat *seat, struct cwl_pointer *pointer)
{
  if (pointer && (seat->pointer || pointer->seat))
    return;                     /* XXX: error? */
  if (!pointer && !seat->pointer)
    return;

  seat->pointer = pointer;
  if (pointer)
    pointer->seat = seat;

  seat_send_updated_caps (seat);
}

void
cwl_seat_set_keyboard (struct cwl_seat *seat, struct cwl_keyboard *keyboard)
{
  if (keyboard && (seat->keyboard || keyboard->seat))
    return;                     /* XXX: error? */
  if (!keyboard && !seat->keyboard)
    return;

  seat->keyboard = keyboard;
  if (keyboard)
    keyboard->seat = seat;

  seat_send_updated_caps (seat);
}

void
cwl_seat_set_touch (struct cwl_seat *seat, struct cwl_touch *touch)
{
  if (touch && (seat->touch || touch->seat))
    return;                     /* XXX: error? */
  if (!touch && !seat->touch)
    return;

  seat->touch = touch;
  if (touch)
    touch->seat = seat;

  seat_send_updated_caps (seat);
}

void
cwl_pointer_set_focus (struct cwl_pointer *pointer, struct wl_surface *surface,
                      wl_fixed_t sx, wl_fixed_t sy)
{
  struct cwl_keyboard *kbd = pointer->seat->keyboard;
  struct wl_resource *resource, *kr;
  uint32_t serial;

  resource = pointer->focus_resource;
  if (resource && pointer->focus != surface)
    {
      struct wl_display *display = wl_client_get_display (resource->client);
      serial = wl_display_next_serial (display);
      wl_pointer_send_leave (resource, serial, &pointer->focus->resource);
      wl_list_remove (&pointer->focus_listener.link);
    }

  resource = find_resource_for_surface (&pointer->resource_list, surface);
  if (resource &&
      (pointer->focus != surface || pointer->focus_resource != resource))
    {
      struct wl_display *display = wl_client_get_display (resource->client);
      serial = wl_display_next_serial (display);
      if (kbd)
        {
          kr = find_resource_for_surface (&kbd->resource_list, surface);
          if (kr)
            {
              wl_keyboard_send_modifiers (kr,
                                          serial,
                                          kbd->modifiers.mods_depressed,
                                          kbd->modifiers.mods_latched,
                                          kbd->modifiers.mods_locked,
                                          kbd->modifiers.group);
            }
        }
      wl_pointer_send_enter (resource, serial, &surface->resource, sx, sy);
      wl_signal_add (&resource->destroy_signal, &pointer->focus_listener);
      pointer->focus_serial = serial;
    }

  pointer->focus_resource = resource;
  pointer->focus = surface;
  pointer->default_grab.focus = surface;
  wl_signal_emit (&pointer->focus_signal, pointer);
}

void
cwl_keyboard_set_focus (struct cwl_keyboard *keyboard,
                       struct wl_surface *surface)
{
  struct wl_resource *resource;
  uint32_t serial;

  if (keyboard->focus_resource && keyboard->focus != surface)
    {
      struct wl_display *display;

      resource = keyboard->focus_resource;
      display = wl_client_get_display (resource->client);
      serial = wl_display_next_serial (display);
      wl_keyboard_send_leave (resource, serial, &keyboard->focus->resource);
      wl_list_remove (&keyboard->focus_listener.link);
    }

  resource = find_resource_for_surface (&keyboard->resource_list, surface);
  if (resource &&
      (keyboard->focus != surface || keyboard->focus_resource != resource))
    {
      struct wl_display *display;

      display = wl_client_get_display (resource->client);
      serial = wl_display_next_serial (display);
      wl_keyboard_send_modifiers (resource, serial,
                                  keyboard->modifiers.mods_depressed,
                                  keyboard->modifiers.mods_latched,
                                  keyboard->modifiers.mods_locked,
                                  keyboard->modifiers.group);
      wl_keyboard_send_enter (resource, serial, &surface->resource,
                              &keyboard->keys);
      wl_signal_add (&resource->destroy_signal, &keyboard->focus_listener);
      keyboard->focus_serial = serial;
    }

  keyboard->focus_resource = resource;
  keyboard->focus = surface;
  wl_signal_emit (&keyboard->focus_signal, keyboard);
}

void
cwl_keyboard_start_grab (struct cwl_keyboard *keyboard,
                        struct cwl_keyboard_grab *grab)
{
  keyboard->grab = grab;
  grab->keyboard = keyboard;

  /* XXX focus? */
}

void
cwl_keyboard_end_grab (struct cwl_keyboard *keyboard)
{
  keyboard->grab = &keyboard->default_grab;
}

void
cwl_pointer_start_grab (struct cwl_pointer *pointer,
                       struct cwl_pointer_grab *grab)
{
  const struct cwl_pointer_grab_interface *interface;

  pointer->grab = grab;
  interface = pointer->grab->interface;
  grab->pointer = pointer;

  if (pointer->current)
    interface->focus (pointer->grab, pointer->current,
                      pointer->current_x, pointer->current_y);
}

void
cwl_pointer_end_grab (struct cwl_pointer *pointer)
{
  const struct cwl_pointer_grab_interface *interface;

  pointer->grab = &pointer->default_grab;
  interface = pointer->grab->interface;
  interface->focus (pointer->grab, pointer->current,
                    pointer->current_x, pointer->current_y);
}

static void
current_surface_destroy (struct wl_listener *listener, void *data)
{
  struct cwl_pointer *pointer =
    wl_container_of (listener, pointer, current_listener);

  pointer->current = NULL;
}

void
cwl_pointer_set_current (struct cwl_pointer *pointer,
                         struct wl_surface *surface)
{
  if (pointer->current)
    wl_list_remove (&pointer->current_listener.link);

  pointer->current = surface;

  if (!surface)
    return;

  wl_signal_add (&surface->resource.destroy_signal,
                 &pointer->current_listener);
  pointer->current_listener.notify = current_surface_destroy;
}

void
cwl_touch_start_grab (struct cwl_touch *touch, struct cwl_touch_grab *grab)
{
  touch->grab = grab;
  grab->touch = touch;
}

void
cwl_touch_end_grab (struct cwl_touch *touch)
{
  touch->grab = &touch->default_grab;
}
