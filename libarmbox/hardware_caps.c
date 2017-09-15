/*
 * determine the capabilities of the hardware.
 * part of libstb-hal
 *
 * (C) 2010-2012 Stefan Seyfried
 *
 * License: GPL v2 or later
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <hardware_caps.h>

#define FP_DEV "/dev/dbox/oled0"
static int initialized = 0;
static hw_caps_t caps;

hw_caps_t *get_hwcaps(void)
{
	if (initialized)
		return &caps;

	memset(&caps, 0, sizeof(hw_caps_t));

	initialized = 1;
	caps.has_CI = 0;
	caps.can_cec = 1;
	caps.can_shutdown = 1;
	caps.display_type = HW_DISPLAY_LINE_TEXT;
	caps.can_set_display_brightness = 0;
	caps.has_HDMI = 1;
	caps.display_xres = 4;
	strcpy(caps.boxvendor, "armbox");
	strcpy(caps.boxname, "armbox");
	strcpy(caps.boxarch,caps.boxname);
	return &caps;
}