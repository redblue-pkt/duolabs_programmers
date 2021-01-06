/*
 *   Copyright (C) redblue 2021
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _CAS_IOCTL_H
#define _CAS_IOCTL_H

#include <linux/ioctl.h>

typedef enum {
	NOFW		= 0,
	READY		= 1,
	VEND_AX		= 2,
	START		= 3,
	CAM		= 4,
	MM		= 5,
	JTAG		= 6,
	MOUSE_PHOENIX	= 7,
	PHOENIX_357	= 8,
	PHOENIX_368	= 9,
	PHOENIX_400	= 10,
	PHOENIX_600	= 11,
	SMARTMOUSE_357	= 12,
	SMARTMOUSE_368	= 13,
	SMARTMOUSE_400	= 14,
	SMARTMOUSE_600	= 15,
	PROGRAMMER	= 16,
	DREAMBOX	= 17,
	DIABLO		= 18,
	DRAGON		= 19,
	EXTREME		= 20,
	JOKER		= 21,
	XCAM		= 22,
} cas_device_status_t;

static const char *cas_device_status[] = {
	"nofw",
	"ready",
	"vend_ax",
	"start",
	"cam",
	"mm",
	"jtag",
	"mouse_phoenix",
	"phoenix357",
	"phoenix368",
	"phoenix400",
	"phoenix600",
	"smartmouse357",
	"smartmouse368",
	"smartmouse400",
	"smartmopuse600",
	"programmer",
	"dreambox",
	"diablo",
	"dragon",
	"extreme",
	"joker",
	"xcam",
};

typedef enum {
	NONE_DEVICE = 0,
	CAS2_DEVICE = 1,
	CAS2_PLUS_DEVICE = 2,
	CAS2_PLUS2_DEVICE = 3,
	CAS2_PLUS2_CRYPTO_DEVICE = 4,
} cas_device_list_t;

static const char *cas_device_list[] = {
	"nodevice",
	"cas2",
	"cas2plus",
	"cas2plus2",
	"cas2pluscrypto",
};

struct cas_bulk_command {
	short length;
	void *buffer;
};

struct cas_vendor_command {
	short length;
	int request;
	int address;
	int index;
	void *buffer;
};

struct cas_device_information_command {
	int device;
	int status;
	int vid;
	int pid;
};

typedef enum {
	IOCTL_SET_CAM = 0x000000c0,
	IOCTL_SET_MM =  0x000000c1,
	IOCTL_SET_JTAG = 0x000000c2,
	IOCTL_SET_PHOENIX_357 = 0x000000c3,
	IOCTL_SET_PHOENIX_368 = 0x000000c4,
	IOCTL_SET_PHOENIX_400 = 0x000000c5,
	IOCTL_SET_PHOENIX_600 = 0x000000c6,
	IOCTL_SET_SMARTMOUSE_357 = 0x000000c7,
	IOCTL_SET_SMARTMOUSE_368 = 0x000000c8,
	IOCTL_SET_SMARTMOUSE_400 = 0x000000c9,
	IOCTL_SET_SMARTMOUSE_600 = 0x00000c10,
	IOCTL_SET_PROGRAMMER = 0x00000c11,
	IOCTL_SET_DREAMBOX = 0x00000c12,
	IOCTL_SET_EXTREME = 0x00000c13,
	IOCTL_SET_DIABLO = 0x00000c14,
	IOCTL_SET_DRAGON = 0x00000c15,
	IOCTL_SET_XCAM = 0x00000c16,
	IOCTL_SET_JOKER = 0x00000c17,
	IOCTL_SEND_BULK_COMMAND = 0x00000c18,
	IOCTL_RECV_BULK_COMMAND = 0x00000c19,
	IOCTL_SEND_VENDOR_COMMAND = 0x00000c20,
	IOCTL_RECV_VENDOR_COMMAND = 0x00000c21,
	IOCTL_DEVICE_INFORMATION_COMMAND = 0x00000c22,
} _cas_ioctl_command_t;

#define IOCTL_DIR_OUT 0x0
#define IOCTL_DIR_IN 0x1

#endif
