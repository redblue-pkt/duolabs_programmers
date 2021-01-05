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

#define DYNAMITE_VENDOR_ID      0x0547
#define DYNAMITE_PRODUCT_ID     0x1010

#define DYNAMITE_PLUS_VENDOR_ID 0x04b4
#define DYNAMITE_PLUS_PREENUMERATION_PRODUCT_ID 0x1112
#define DYNAMITE_PLUS_PRODUCT_ID 0x1111

#define DYNAMITE "Dynamite"
#define DYNAMITE_PLUS "Dynamite Plus"

#define SET_EE_VALUE                      0x10
#define GET_EE_VALUE                      0x11

#define NO_RESET_CPU 0
#define RESET_CPU 1

#define WAIT_FOR_FW 2000

typedef enum {
	START_LOAD_VEND_AX_FW		= 0,
	FINISH_LOAD_VEND_AX_FW		= 1,
	START_LOAD_START_FW		= 2,
	FINISH_LOAD_START_FW		= 3,
	START_LOAD_MOUSE_PHOENIX_FW	= 4,
	FINISH_LOAD_MOUSE_PHOENIX_FW	= 5,
	START_LOAD_CARDPROGRAMMER_FW	= 6,
	FINISH_LOAD_CARDPROGRAMMER_FW	= 7,
} dynamite_fimware_status_t;

typedef enum {
	DEBUG_NONE		= 0,
	FULL_DEBUG_ALL		= 1,
	SIMPLE_DEBUG_ALL	= 2,
	FULL_DEBUG_IN		= 3,
	FULL_DEBUG_OUT		= 4,
	SIMPLE_DEBUG_IN		= 5,
	SIMPLE_DEBUG_OUT	= 6,
} dynamite_debug_t;

#define MIN(a,b) (((a) <= (b)) ? (a) : (b))
#define MAX_PKT_SIZE 64

/* structure to hold all of our device specific stuff */
struct usb_dynamite {
	struct device *device;
	struct cdev cdev;
	struct usb_device *udevice;	 /* save off the usb device pointer */
	struct usb_interface *uinterface; /* the interface for this device */
	struct usb_class_driver uclass;
	const char *device_name;
	int device_running;
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

struct dynamite_hex_record {
	__u8 data_size;
	__u8 data[64];
};

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
