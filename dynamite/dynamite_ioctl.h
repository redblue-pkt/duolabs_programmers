/*
 *   Copyright (C) redblue 2020
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

#ifndef _DYNAMITE_IOCTL_H
#define _DYNAMITE_IOCTL_H

#include <linux/ioctl.h>

typedef enum {
	NOFW		= 0,
	READY		= 1,
	VEND_AX		= 2,
	START		= 3,
 	MOUSE_PHOENIX	= 4,
	PHOENIX_357	= 5,
	PHOENIX_368	= 6,
	PHOENIX_400	= 7,
	PHOENIX_600	= 8,
	SMARTMOUSE_357	= 9,
	SMARTMOUSE_368	= 10,
	SMARTMOUSE_400	= 11,
	SMARTMOUSE_600	= 12,
	CARDPROGRAMMER	= 13,
} dynamite_device_status_t;

static const char *dynamite_device_status[] = {
	"nofw",
	"ready",
	"vend_ax",
	"start",
	"mouse_phoenix",
	"phoenix357",
	"phoenix368",
	"phoenix400",
	"phoenix600",
	"smartmouse357",
	"smartmouse368",
	"smartmouse400",
	"smartmopuse600",
	"cardprogrammer",
};

typedef enum {
	NONE_DEVICE = 0,
	DYNAMITE_DEVICE = 1,
	DYNAMITE_PLUS_DEVICE = 2,
	DYNAMITE_TINY_DEVICE = 3,
} dynamite_device_list_t;

static const char *dynamite_device_list[] = {
	"nodevice",
	"dynamite",
	"dynamiteplus",
	"dynamitetiny",
};

struct dynamite_bulk_command {
	short length;
	void *buffer;
};

struct dynamite_vendor_command {
	short length;
	int request;
	int address;
	int index;
	void *buffer;
};

struct dynamite_device_information_command {
	int device;
	int status;
	int vid;
	int pid;
};

typedef enum {
	IOCTL_SET_PHOENIX_357 = 0x000000c1,
	IOCTL_SET_PHOENIX_368 = 0x000000c2,
	IOCTL_SET_PHOENIX_400 = 0x000000c3,
	IOCTL_SET_PHOENIX_600 = 0x000000c4,
	IOCTL_SET_SMARTMOUSE_357 = 0x000000c5,
	IOCTL_SET_SMARTMOUSE_368 = 0x000000c6,
	IOCTL_SET_SMARTMOUSE_400 = 0x000000c7,
	IOCTL_SET_SMARTMOUSE_600 = 0x000000c8,
	IOCTL_SET_CARDPROGRAMMER = 0x00000c10,
	IOCTL_SEND_BULK_COMMAND = 0x00000c11,
	IOCTL_RECV_BULK_COMMAND = 0x00000c12,
	IOCTL_SEND_VENDOR_COMMAND = 0x00000c13,
	IOCTL_RECV_VENDOR_COMMAND = 0x00000c14,
	IOCTL_DEVICE_INFORMATION_COMMAND = 0x00000c15,
} _dynamite_ioctl_command_t;

#define IOCTL_DIR_OUT 0x0
#define IOCTL_DIR_IN 0x1

#endif
