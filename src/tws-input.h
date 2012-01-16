#ifndef __TWS_INPUT_H__
#define __TWS_INPUT_H__

#include <wayland-server.h>

typedef struct _TwsInputDevice TwsInputDevice;

TwsInputDevice *
tws_input_device_new (struct wl_display *display);

void
tws_input_device_handle_event (TwsInputDevice *input_device,
                               const ClutterEvent *event);

void
tws_input_device_repick (TwsInputDevice *input_device,
                         uint32_t time,
                         ClutterActor *actor);

#endif /* __TWS_INPUT_H__ */
