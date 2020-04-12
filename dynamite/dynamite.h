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

#ifndef __DYNAMITE_H
#define __DYNAMITE_H

#include <linux/cdev.h>

#define internal_dev_info(dev, format, arg ...) pr_info(YELLOW_COLOR "%s %s: " format, dev_driver_string(dev), dev_name(dev) , ##arg)
#define internal_dev_err(dev, format, arg ...) pr_err(YELLOW_COLOR "%s %s: " format, dev_driver_string(dev), dev_name(dev) , ##arg)
#define internal_dev_dbg(dev, format, arg ...) pr_debug(YELLOW_COLOR "%s %s: " format, dev_driver_string(dev), dev_name(dev) , ##arg)
#define internal_dev_info_red(dev, format, arg ...) pr_cont(YELLOW_COLOR "%s %s: " RED_COLOR format, dev_driver_string(dev), dev_name(dev) , ##arg)
#define internal_dev_info_green(dev, format, arg ...) pr_cont(YELLOW_COLOR "%s %s: " GREEN_COLOR format, dev_driver_string(dev), dev_name(dev) , ##arg)
#define internal_dev_info_yellow(dev, format, arg ...) pr_cont(YELLOW_COLOR "%s %s: " YELLOW_COLOR format, dev_driver_string(dev), dev_name(dev) , ##arg)
#define internal_dev_info_blue(dev, format, arg ...) pr_cont(YELLOW_COLOR "%s %s: " BLUE_COLOR format, dev_driver_string(dev), dev_name(dev) , ##arg)

#define WRITE_FUNCTIONS_INTERNAL

#define DYNAMITE_VENDOR_ID      0x0547
#define DYNAMITE_PRODUCT_ID     0x1010

#define DYNAMITE_PLUS_VENDOR_ID 0x04b4
#define DYNAMITE_PLUS_PRODUCT_ID 0x1112

#define CPUCS_RESET              0x1
#define CPUCS_RUN                0x0

#define NO_RESET_CPU 0
#define RESET_CPU 1

#define WAIT_FOR_FW 2000

typedef enum {
	NOFW		= 0,
	READY		= 1,
	VEND_AX 	= 2,
	START	 	= 3,
	MOUSE_PHOENIX	= 4,
	PHOENIX_357 	= 5,
	PHOENIX_368 	= 6,
	PHOENIX_400 	= 7,
	PHOENIX_600 	= 8,
	SMARTMOUSE_357 	= 9,
	SMARTMOUSE_368 	= 10,
	SMARTMOUSE_400 	= 11,
	SMARTMOUSE_600 	= 12,
	CARDPROGRAMMER 	= 13,
} dynamite_t;

typedef enum {
	START_LOAD_VEND_AX_FW		= 0,
	FINISH_LOAD_VEND_AX_FW		= 1,
	START_LOAD_START_FW		= 2,
	FINISH_LOAD_START_FW		= 3,
	START_LOAD_MOUSE_PHOENIX_FW	= 4,
	FINISH_LOAD_MOUSE_PHOENIX_FW	= 5,
	START_LOAD_CARDPROGRAMMER_FW	= 6,
	FINISH_LOAD_CARDPROGRAMMER_FW	= 7,
} state_t;

#define MIN(a,b) (((a) <= (b)) ? (a) : (b))
#define MAX_PKT_SIZE 64

/* structure to hold all of our device specific stuff */
struct usb_dynamite {
	struct device *device;
	struct cdev cdev;
	struct usb_device *udevice;	 /* save off the usb device pointer */
	struct usb_interface *uinterface; /* the interface for this device */
	struct usb_class_driver uclass;
	char *buf[MAX_PKT_SIZE];
	int status;
	struct mutex lock;
	int state;
	unsigned char *bulk_in_buffer;		/* the buffer to receive data */
	size_t bulk_in_size;		/* the size of the receive buffer */
	__u8 bulk_in_endpointAddr;	/* the address of the bulk in endpoint */
	__u8 bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
	struct kref kref;
};

/* Struct used for firmware - increased size of data section
   to allow Keyspan's 'C' firmware struct to be used unmodified */
struct ezusb_hex_record {
	__u16 address;
	__u8 data_size;
	__u8 data[64];
};

struct dynamite_hex_record {
	__u8 data_size;
	__u8 data[64];
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

#include "card_programmer.h"
#include "start.h"
#include "vend_ax.h"
#include "mouse_phoenix.h"

#define NORMAL_COLOR  "\x1B[0m"
#define RED_COLOR  "\x1B[31m"
#define GREEN_COLOR  "\x1B[32m"
#define YELLOW_COLOR  "\x1B[33m"
#define BLUE_COLOR  "\x1B[34m"
#define MAGNETA_COLOR  "\x1B[35m"
#define CYAN_COLOR  "\x1B[36m"
#define WHITE_COLOR  "\x1B[37m"
#define RESET_COLOR "\x1B[0m"

#endif
