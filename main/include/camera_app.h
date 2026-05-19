#ifndef CAMERA_APP_H
#define CAMERA_APP_H

#include "esp_err.h"
#include "esp_camera.h"

esp_err_t camera_app_init(void);
camera_fb_t *camera_app_capture(void);
void camera_app_return(camera_fb_t *fb);

#endif
