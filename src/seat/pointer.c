#include "pointer.h"
#include "client.h"

#include "compositor/view.h"
#include "compositor/surface.h"
#include "compositor/compositor.h"

#include <stdlib.h>
#include <assert.h>

#include <wayland-server.h>

static bool
view_exists_in_list(struct wlc_view *needle, struct wl_list *list)
{
   struct wlc_view *view;
   wl_list_for_each(view, list, link) {
      if (view == needle)
         return true;
   }
   return false;
}

static bool
is_valid_view(struct wlc_view *view)
{
   return (view && view->client && view->client->input[WLC_POINTER] && view->surface && view->surface->resource);
}

void
wlc_pointer_focus(struct wlc_pointer *pointer, uint32_t serial, struct wlc_view *view, int32_t x,  int32_t y)
{
   assert(pointer);

   if (pointer->focus == view)
      return;

   // FIXME: hack
   // We need better resource management.
   if (pointer->focus && !view_exists_in_list(pointer->focus, pointer->views))
      pointer->focus = NULL;

   if (is_valid_view(pointer->focus))
      wl_pointer_send_leave(pointer->focus->client->input[WLC_POINTER], serial, pointer->focus->surface->resource);

   if (is_valid_view(view))
      wl_pointer_send_enter(view->client->input[WLC_POINTER], serial, view->surface->resource, wl_fixed_from_int(x), wl_fixed_from_int(y));

   pointer->focus = view;
   pointer->grabbing = false;
   pointer->action = WLC_GRAB_ACTION_NONE;
   pointer->action_edges = 0;
}

void
wlc_pointer_button(struct wlc_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, enum wl_pointer_button_state state)
{
   assert(pointer);

   if (!is_valid_view(pointer->focus))
      return;

   if (state == WL_POINTER_BUTTON_STATE_PRESSED && !pointer->grabbing) {
      pointer->grabbing = true;
      pointer->gx = pointer->x;
      pointer->gy = pointer->y;
   } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
      pointer->grabbing = false;
      pointer->action = WLC_GRAB_ACTION_NONE;
      pointer->action_edges = 0;
   }

   wl_pointer_send_button(pointer->focus->client->input[WLC_POINTER], serial, time, button, state);
}

void
wlc_pointer_motion(struct wlc_pointer *pointer, uint32_t serial, uint32_t time, int32_t x, int32_t y)
{
   assert(pointer);

   pointer->x = wl_fixed_from_int(x);
   pointer->y = wl_fixed_from_int(y);

   struct wlc_view *focused = NULL;
   if (pointer->focus && pointer->grabbing) {
      focused = pointer->focus;
   } else {
      struct wlc_view *view;
      wl_list_for_each_reverse(view, pointer->views, link) {
         struct wlc_geometry b;
         wlc_view_get_bounds(view, &b);
         if (x >= b.x && x <= b.x + b.w &&
             y >= b.y && y <= b.y + b.h) {
            focused = view;
            break;
         }
      }
   }

   if (!focused) {
      wlc_pointer_focus(pointer, 0, NULL, 0, 0);
      return;
   }

   struct wlc_geometry b;
   wlc_view_get_bounds(focused, &b);
   int32_t dx = (x - b.x) * focused->surface->width / b.w;
   int32_t dy = (y - b.y) * focused->surface->height / b.h;
   wlc_pointer_focus(pointer, serial, focused, dx, dy);

   if (!is_valid_view(focused))
      return;

   wl_pointer_send_motion(focused->client->input[WLC_POINTER], time, wl_fixed_from_int(dx), wl_fixed_from_int(dy));

   if (pointer->grabbing) {
      int32_t dx = x - wl_fixed_to_int(pointer->gx);
      int32_t dy = y - wl_fixed_to_int(pointer->gy);

      if (pointer->action == WLC_GRAB_ACTION_MOVE) {
         const int32_t x = focused->geometry.x + dx, y = focused->geometry.y + dy;
         if (focused->surface->compositor->interface.view.move) {
            focused->surface->compositor->interface.view.move(focused->surface->compositor, focused, x, y);
         } else {
            wlc_view_position(focused, x, y);
         }
      } else if (pointer->action == WLC_GRAB_ACTION_RESIZE) {
         int32_t w = focused->geometry.w, h = focused->geometry.h;

         if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_LEFT) {
            w -= dx;
            focused->geometry.x += dx;
         } else if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_RIGHT) {
            w += dx;
         }

         if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_TOP) {
            h -= dy;
            focused->geometry.y += dy;
         } else if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_BOTTOM) {
            h += dy;
         }

         if (focused->surface->compositor->interface.view.resize) {
            focused->surface->compositor->interface.view.resize(focused->surface->compositor, focused, w, h);
         } else {
            wlc_view_resize(focused, w, h);
         }
      }

      pointer->gx = pointer->x;
      pointer->gy = pointer->y;
   }
}

void
wlc_pointer_remove_client_for_resource(struct wlc_pointer *pointer, struct wl_resource *resource)
{
   assert(pointer && resource);

   struct wlc_view *view;
   wl_list_for_each(view, pointer->views, link) {
      if (view->client->input[WLC_POINTER] != resource)
         continue;

      if (pointer->focus && pointer->focus->client && pointer->focus->client->input[WLC_POINTER] == resource) {
         view->client->input[WLC_POINTER] = NULL;
         wlc_pointer_focus(pointer, 0, NULL, 0, 0);
      } else {
         view->client->input[WLC_POINTER] = NULL;
      }
      break;
   }
}

void
wlc_pointer_free(struct wlc_pointer *pointer)
{
   assert(pointer);

   if (pointer->clients) {
      struct wlc_client *client;
      wl_list_for_each(client, pointer->clients, link)
         client->input[WLC_POINTER] = NULL;
   }

   free(pointer);
}

struct wlc_pointer*
wlc_pointer_new(struct wl_list *clients, struct wl_list *views)
{
   assert(views);

   struct wlc_pointer *pointer;
   if (!(pointer = calloc(1, sizeof(struct wlc_pointer))))
      return NULL;

   pointer->clients = clients;
   pointer->views = views;
   return pointer;
}
