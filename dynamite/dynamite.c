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

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mman.h>
#include <linux/kref.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/usb.h>

#include <linux/string.h>

#include "dynamite.h"

#include "../ezusb/ezusb.h"

#include "dynamite_ioctl.h"
#include "dynamite_init.h"
#include "dynamiteplus_init.h"
#include "dynamite_commands.h"

static int debug = DEBUG_NONE;
static int load_fx1_fw = 0;
static int load_fx2_fw = 0;

#define to_dynamite_dev(d) container_of(d, struct usb_dynamite, kref)

static struct usb_driver dynamite_driver;
static const struct file_operations dynamite_fops;

/* local function prototypes */
static int dynamite_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void dynamite_disconnect(struct usb_interface *interface);

static void wait_for_finish(struct usb_dynamite *dynamite, unsigned long usecs)
{
	int result;

	dev_dbg(&dynamite->uinterface->dev, "%s: time %lu\n", __func__, usecs);

	udelay(usecs);
}

static inline char printable(char c)
{
	return (c < ' ') || (c > '~') ? '-' : c;
}

static void dump_buffer(struct usb_dynamite *dynamite, unsigned char *buffer, char *name, int len)
{
	int n, i;
	int j = 16 - len;

	for (n = 0; n < len; n += i) {
		if (!strncmp(name, "data_out", 8))
			internal_dev_info_green(&dynamite->uinterface->dev, "%s ", name);
		else if (!strncmp(name, "data_in", 7))
			internal_dev_info_blue(&dynamite->uinterface->dev, "%s ", name);
		else
			internal_dev_info(&dynamite->uinterface->dev, "");
		for (i = 0; (i < 16) && (n + i < len); i++)
			pr_cont("0x%02x ", buffer[n + i]);
		if (debug == FULL_DEBUG_ALL || debug == FULL_DEBUG_IN || debug == FULL_DEBUG_OUT) {
			for (i = 0; i < j; i++)
				pr_cont("%1s", "     ");
			for (i = 0; (i < 16) && (n + i < len); i++)
				pr_cont(" %c ", printable(buffer[n + i]));
		}
		pr_cont("\n");
	}
}

static int vendor_command_snd(struct usb_dynamite *dynamite, unsigned char request, int address, int index, const char *buf, int size)
{
	int result;
	unsigned char *buffer = kmemdup(buf, size, GFP_KERNEL);

	if (!buffer) {
		dev_err(&dynamite->uinterface->dev, "%s: kmalloc(%d) failed\n", __func__, size);
		return -ENOMEM;
	}

	mutex_lock(&dynamite->lock);

	if ((debug != DEBUG_NONE && debug != FULL_DEBUG_IN && debug != SIMPLE_DEBUG_IN) && buffer != NULL)
		dump_buffer(dynamite, buffer, "data_out", size);

	result = usb_control_msg(dynamite->udevice, usb_sndctrlpipe(dynamite->udevice, 0), request, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, address, 0, buffer, size, 3000);

	mutex_unlock(&dynamite->lock);

	if (buffer)
        	kfree (buffer);
        return result;
}

static int vendor_command_rcv(struct usb_dynamite *dynamite, unsigned char request, int address, int index, char *buf, int size)
{
	int result;

	mutex_lock(&dynamite->lock);

	result = usb_control_msg(dynamite->udevice, usb_rcvctrlpipe(dynamite->udevice, 0), request, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, address, 0, buf, size, 3000);

	if ((debug != DEBUG_NONE && debug != FULL_DEBUG_OUT && debug != SIMPLE_DEBUG_OUT) && buf != NULL)
		dump_buffer(dynamite, buf, "data_in", size);

	mutex_unlock(&dynamite->lock);

	return result;
}

static int bulk_command_snd(struct usb_dynamite *dynamite, const char *buf, int size, int count)
{
	int result;
	unsigned char tmp[64];
	unsigned char *buffer;

	if (size < MAX_PKT_SIZE) {
		memset(tmp, 0x00, MAX_PKT_SIZE);
		memcpy(tmp, buf, size);
		buffer = kmemdup(tmp, MAX_PKT_SIZE, GFP_KERNEL);
	} else
		buffer = kmemdup(buf, size, GFP_KERNEL);

	if (!buffer) {
		dev_err(&dynamite->uinterface->dev, "%s: kmalloc(%d) failed\n", __func__, size);
		return -ENOMEM;
	}

	mutex_lock(&dynamite->lock);

	if ((debug != DEBUG_NONE && debug != FULL_DEBUG_IN && debug != SIMPLE_DEBUG_IN) && buffer != NULL)
		dump_buffer(dynamite, buffer, "data_out", MAX_PKT_SIZE);

	result = usb_bulk_msg(dynamite->udevice, usb_sndbulkpipe(dynamite->udevice, dynamite->bulk_out_endpointAddr), buffer, size, NULL, 1000);

	mutex_unlock(&dynamite->lock);

	if (buffer)
		kfree (buffer);
	return result;
}

static int bulk_command_rcv(struct usb_dynamite *dynamite, char *buf, int size, int count)
{
	int result;

	mutex_lock(&dynamite->lock);

	result = usb_bulk_msg(dynamite->udevice, usb_rcvbulkpipe(dynamite->udevice, dynamite->bulk_in_endpointAddr), buf, size, NULL, 1000);

	if ((debug != DEBUG_NONE && debug != FULL_DEBUG_OUT && debug != SIMPLE_DEBUG_OUT) && buf != NULL)
		dump_buffer(dynamite, buf, "data_in", MAX_PKT_SIZE);

	mutex_unlock(&dynamite->lock);

	return result;
}

static int read_eeprom(struct usb_dynamite *dynamite,unsigned char *buf, int len, int offset)
{
	int i, result;
	char *v, br[2];
	for (i=0; i < len; i++) {
		v = offset + i;
		result = vendor_command_rcv(dynamite, 0xA2, 0, 0, v, 2);
		if (result)
			return result;

		buf[i] = br[1];
	}
	dev_info(&dynamite->uinterface->dev, "read eeprom (offset: 0x%x, len: %d) : ", offset, i);
	return result;
}

static int send_command(struct usb_dynamite *dynamite, int id)
{
	int i, result;
	const struct dynamite_hex_record *record = NULL;

	if (id == START) {
		if (dynamite->device_running == DYNAMITE_DEVICE)
			record = &dynamite_init_code[0];
		else if (dynamite->device_running == DYNAMITE_PLUS_DEVICE)
			record = &dynamiteplus_init_code[0];
	} else if (id == PHOENIX_357)
		record = &phoenix_357_code[0];
	else if (id == PHOENIX_368)
		record = &phoenix_368_code[0];
	else if (id == PHOENIX_400)
		record = &phoenix_400_code[0];
	else if (id == PHOENIX_600)
		record = &phoenix_600_code[0];
	else if (id == SMARTMOUSE_357)
		record = &smartmouse_357_code[0];
	else if (id == SMARTMOUSE_368)
		record = &smartmouse_368_code[0];
	else if (id == SMARTMOUSE_400)
		record = &smartmouse_400_code[0];
	else if (id == SMARTMOUSE_600)
		record = &smartmouse_600_code[0];

	while(record->data_size != 0) {
		result = bulk_command_snd(dynamite, (unsigned char *)record->data, record->data_size, 0);
		result = bulk_command_rcv(dynamite, dynamite->bulk_in_buffer, MAX_PKT_SIZE, 0);

		if (result < 0)
			goto out;

		record++;
	}

	return 0;
out:
	return result;
}

static int send_init_command(struct usb_dynamite *dynamite)
{
	return send_command(dynamite, START);
}

static int send_phoenix_357_command(struct usb_dynamite *dynamite)
{
	return send_command(dynamite, PHOENIX_357);
}

static int send_phoenix_368_command(struct usb_dynamite *dynamite)
{
	return send_command(dynamite, PHOENIX_368);
}

static int send_phoenix_400_command(struct usb_dynamite *dynamite)
{
	return send_command(dynamite, PHOENIX_400);
}

static int send_phoenix_600_command(struct usb_dynamite *dynamite)
{
	return send_command(dynamite, PHOENIX_600);
}

static int send_smartmouse_357_command(struct usb_dynamite *dynamite)
{
	return send_command(dynamite, SMARTMOUSE_357);
}

static int send_smartmouse_368_command(struct usb_dynamite *dynamite)
{
	return send_command(dynamite, SMARTMOUSE_368);
}

static int send_smartmouse_400_command(struct usb_dynamite *dynamite)
{
	return send_command(dynamite, SMARTMOUSE_400);
}

static int send_smartmouse_600_command(struct usb_dynamite *dynamite)
{
	return send_command(dynamite, SMARTMOUSE_600);
}

static int dynamite_firmware_load(struct usb_dynamite *dynamite, int id, int reset_cpu)
{
	int response = -ENOENT;
	const char *fw_name;

	dev_dbg(&dynamite->uinterface->dev, "%s: sending %s...", __func__, fw_name);

	if (reset_cpu != NO_RESET_CPU) {
		dev_dbg(&dynamite->uinterface->dev, "%s reset cpu\n", dynamite->device_name);
		if (dynamite->device_running == DYNAMITE_DEVICE)
			response = ezusb_fx1_set_reset(dynamite->udevice, 1);
		else if (dynamite->device_running == DYNAMITE_PLUS_DEVICE)
			response = ezusb_fx2_set_reset(dynamite->udevice, 1);
	}

	if (response < 0)
		goto out;

	if (0) { ; }
	else if (le16_to_cpu(id) == VEND_AX) {
		if (dynamite->device_running == DYNAMITE_DEVICE)
			fw_name = "dynamite/vend_ax.fw";
		dynamite->state = START_LOAD_VEND_AX_FW;
	} else if (le16_to_cpu(id) == START) {
		if (load_fx1_fw)
			fw_name = "fx1/start.fw";
		else if (load_fx2_fw)
			fw_name = "fx2/start.fw";
		else {
			if (dynamite->device_running == DYNAMITE_DEVICE)
				fw_name = "dynamite/start.fw";
			else if (dynamite->device_running == DYNAMITE_PLUS_DEVICE)
				fw_name = "dynamiteplus/start.fw";
		}
		dynamite->state = START_LOAD_START_FW;
	} else if (le16_to_cpu(id) == MOUSE_PHOENIX) {
		if (dynamite->device_running == DYNAMITE_DEVICE)
			fw_name = "dynamite/phoenix.fw";
		else if (dynamite->device_running == DYNAMITE_PLUS_DEVICE)
			fw_name = "dynamiteplus/phoenix.fw";
		dynamite->state = START_LOAD_MOUSE_PHOENIX_FW;
	} else if (le16_to_cpu(id) == CARDPROGRAMMER) {
		if (dynamite->device_running == DYNAMITE_DEVICE)
			fw_name = "dynamite/programmer.fw";
		else if (dynamite->device_running == DYNAMITE_PLUS_DEVICE)
			fw_name = "dynamiteplus/programmer.fw";
		dynamite->state = START_LOAD_CARDPROGRAMMER_FW;
	} else {
		dev_err(&dynamite->uinterface->dev, "%s: unknown fw request, aborting\n",
			__func__);
		goto out;
	}

	if (dynamite->device_running == DYNAMITE_DEVICE) {
		if (ezusb_fx1_ihex_firmware_download(dynamite->udevice, fw_name) < 0) {
			dev_err(&dynamite->uinterface->dev, "failed to load firmware \"%s\"\n",
				fw_name);
			return -ENOENT;
		}
	}
	else if (dynamite->device_running == DYNAMITE_PLUS_DEVICE)
	{
		if (ezusb_fx2_ihex_firmware_download(dynamite->udevice, fw_name) < 0) {
			dev_err(&dynamite->uinterface->dev, "failed to load firmware \"%s\"\n",
				fw_name);
			return -ENOENT;
		}
	}

	if (reset_cpu != NO_RESET_CPU) {
		dev_dbg(&dynamite->uinterface->dev, "Dynamite Programmer reset cpu\n");
		if (dynamite->device_running == DYNAMITE_DEVICE)
			response = ezusb_fx1_set_reset(dynamite->udevice, 0);
		else if (dynamite->device_running == DYNAMITE_PLUS_DEVICE)
			response = ezusb_fx2_set_reset(dynamite->udevice, 0);
	}

	if (response < 0)
		goto out;

	if (dynamite->state == START_LOAD_VEND_AX_FW)
		dynamite->state = FINISH_LOAD_VEND_AX_FW;
	else if (dynamite->state == START_LOAD_START_FW)
		dynamite->state = FINISH_LOAD_START_FW;
	else if (dynamite->state == START_LOAD_MOUSE_PHOENIX_FW)
		dynamite->state = FINISH_LOAD_MOUSE_PHOENIX_FW;
	else if (dynamite->state == START_LOAD_CARDPROGRAMMER_FW)
		dynamite->state = FINISH_LOAD_CARDPROGRAMMER_FW;

	return 1;
out:
	return response;
}

static int dynamite_set_init_fw(struct usb_dynamite *dynamite)
{
	int result;

	if (dynamite->device_running == DYNAMITE_DEVICE) {
		result = dynamite_firmware_load(dynamite, VEND_AX, RESET_CPU);
		if (result < 0)
			dev_info(&dynamite->uinterface->dev, "%s error load VEND_AX\n", dynamite->device_name);

		wait_for_finish(dynamite, WAIT_FOR_FW);
	}

	result = dynamite_firmware_load(dynamite, START, RESET_CPU);
	if (result < 0)
		dev_info(&dynamite->uinterface->dev, "%s error load START\n", dynamite->device_name);

	wait_for_finish(dynamite, WAIT_FOR_FW);

#if 0
	result = send_init_command(dynamite);
	if (result < 0)
		dev_info(&dynamite->uinterface->dev, "%s error send init command\n", dynamite->device_name);

	wait_for_finish(dynamite, WAIT_FOR_FW);
#endif

	return result;
}

static int dynamite_set_phoenix_357_fw(struct usb_dynamite *dynamite)
{
	int result;

	if (dynamite->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = dynamite_firmware_load(dynamite, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(dynamite, WAIT_FOR_FW);
	}

	result = send_phoenix_357_command(dynamite);
	if (result >= 0)
		dev_info(&dynamite->uinterface->dev, "%s set to phoenix mode 357 mhz\n", dynamite->device_name);

	return result;
}

static int dynamite_set_phoenix_368_fw(struct usb_dynamite *dynamite)
{
	int result;

	if (dynamite->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = dynamite_firmware_load(dynamite, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(dynamite, WAIT_FOR_FW);
	}

	result = send_phoenix_368_command(dynamite);
	if (result >= 0)
		dev_info(&dynamite->uinterface->dev, "%s set to phoenix mode 368 mhz\n", dynamite->device_name);

	return result;
}

static int dynamite_set_phoenix_400_fw(struct usb_dynamite *dynamite)
{
	int result;

	if (dynamite->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = dynamite_firmware_load(dynamite, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(dynamite, WAIT_FOR_FW);
	}

	result = send_phoenix_400_command(dynamite);
	if (result >= 0)
		dev_info(&dynamite->uinterface->dev, "%s set to phoenix mode 400 mhz\n", dynamite->device_name);


	return result;
}

static int dynamite_set_phoenix_600_fw(struct usb_dynamite *dynamite)
{
	int result;

	if (dynamite->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = dynamite_firmware_load(dynamite, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(dynamite, WAIT_FOR_FW);
	}

	result = send_phoenix_600_command(dynamite);
	if (result >= 0)
		dev_info(&dynamite->uinterface->dev, "%s set to phoenix mode 600 mhz\n", dynamite->device_name);

	return result;
}

static int dynamite_set_smartmouse_357_fw(struct usb_dynamite *dynamite)
{
	int result;

	if (dynamite->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = dynamite_firmware_load(dynamite, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(dynamite, WAIT_FOR_FW);
	}

	result = send_smartmouse_357_command(dynamite);
	if (result >= 0)
		dev_info(&dynamite->uinterface->dev, "%s set to smartmouse mode 357 mhz\n", dynamite->device_name);

	return result;
}

static int dynamite_set_smartmouse_368_fw(struct usb_dynamite *dynamite)
{
	int result;

	if (dynamite->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = dynamite_firmware_load(dynamite, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(dynamite, WAIT_FOR_FW);
	}

	result = send_smartmouse_368_command(dynamite);
	if (result >= 0)
		dev_info(&dynamite->uinterface->dev, "%s set to smartmouse mode 368 mhz\n", dynamite->device_name);

	return result;
}

static int dynamite_set_smartmouse_400_fw(struct usb_dynamite *dynamite)
{
	int result;

	if (dynamite->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = dynamite_firmware_load(dynamite, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(dynamite, WAIT_FOR_FW);
	}

	result = send_smartmouse_400_command(dynamite);
	if (result >= 0)
		dev_info(&dynamite->uinterface->dev, "%s set to smartmouse mode 400 mhz\n", dynamite->device_name);

	return result;
}

static int dynamite_set_smartmouse_600_fw(struct usb_dynamite *dynamite)
{
	int result;

	if (dynamite->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = dynamite_firmware_load(dynamite, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(dynamite, WAIT_FOR_FW);
	}

	result = send_smartmouse_600_command(dynamite);
	if (result >= 0)
		dev_info(&dynamite->uinterface->dev, "%s set to smartmouse mode 600 mhz\n", dynamite->device_name);

	return result;
}

static int dynamite_set_cardprogrammer_fw(struct usb_dynamite *dynamite)
{
	int result;

        result = dynamite_firmware_load(dynamite, CARDPROGRAMMER, RESET_CPU);
	wait_for_finish(dynamite, WAIT_FOR_FW);
	if (result >= 0)
		dev_info(&dynamite->uinterface->dev, "%s set to card programmer mode\n", dynamite->device_name);

        return result;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,12,0)
static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *interface = usb_find_interface(&dynamite_driver, 0);
	struct usb_dynamite *dynamite = usb_get_intfdata(interface);

	return sprintf(buf, "%s", dynamite_device_status[dynamite->status]);
}

static ssize_t status_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *interface = usb_find_interface(&dynamite_driver, 0);
	struct usb_dynamite *dynamite = usb_get_intfdata(interface);

	if (!strncmp(buf, "phoenix357", 10)) {
		dynamite->status = PHOENIX_357;
		dynamite_set_phoenix_357_fw(dynamite);
	} else if (!strncmp(buf, "phoenix368", 10)) {
		dynamite->status = PHOENIX_368;
		dynamite_set_phoenix_368_fw(dynamite);
	} else if (!strncmp(buf, "phoenix400", 10)) {
		dynamite->status = PHOENIX_400;
		dynamite_set_phoenix_400_fw(dynamite);
	} else if (!strncmp(buf, "phoenix600", 10)) {
		dynamite->status = PHOENIX_600;
		dynamite_set_phoenix_600_fw(dynamite);
	} else if (!strncmp(buf, "smartmouse357", 13)) {
		dynamite->status = SMARTMOUSE_357;
		dynamite_set_smartmouse_357_fw(dynamite);
	} else if (!strncmp(buf, "smartmouse368", 13)) {
		dynamite->status = SMARTMOUSE_368;
		dynamite_set_smartmouse_368_fw(dynamite);
	} else if (!strncmp(buf, "smartmouse400", 13)) {
		dynamite->status = SMARTMOUSE_400;
		dynamite_set_smartmouse_400_fw(dynamite);
	} else if (!strncmp(buf, "smartmouse600", 13)) {
		dynamite->status = SMARTMOUSE_600;
		dynamite_set_smartmouse_600_fw(dynamite);
        } else if (!strncmp(buf, "cardprogrammer", 14)) {
                dynamite->status = CARDPROGRAMMER;
                dynamite_set_cardprogrammer_fw(dynamite);
	}

	return count;
}

static DEVICE_ATTR_RW(status);
#else
static ssize_t show_status(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *interface = usb_find_interface(&dynamite_driver, 0);
	struct usb_dynamite *dynamite = usb_get_intfdata(interface);

	return sprintf(buf, "%s", dynamite_device_status[dynamite->status]);
}

static ssize_t store_status(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *interface = usb_find_interface(&dynamite_driver, 0);
        struct usb_dynamite *dynamite = usb_get_intfdata(interface);

        if (!strncmp(buf, "phoenix357", 10)) {
                dynamite->status = PHOENIX_357;
                dynamite_set_phoenix_357_fw(dynamite);
        } else if (!strncmp(buf, "phoenix368", 10)) {
                dynamite->status = PHOENIX_368;
                dynamite_set_phoenix_368_fw(dynamite);
        } else if (!strncmp(buf, "phoenix400", 10)) {
                dynamite->status = PHOENIX_400;
                dynamite_set_phoenix_400_fw(dynamite);
        } else if (!strncmp(buf, "phoenix600", 10)) {
                dynamite->status = PHOENIX_600;
                dynamite_set_phoenix_600_fw(dynamite);
        } else if (!strncmp(buf, "smartmouse357", 13)) {
                dynamite->status = SMARTMOUSE_357;
                dynamite_set_smartmouse_357_fw(dynamite);
        } else if (!strncmp(buf, "smartmouse368", 13)) {
                dynamite->status = SMARTMOUSE_368;
                dynamite_set_smartmouse_368_fw(dynamite);
        } else if (!strncmp(buf, "smartmouse400", 13)) {
                dynamite->status = SMARTMOUSE_400;
                dynamite_set_smartmouse_400_fw(dynamite);
        } else if (!strncmp(buf, "smartmouse600", 13)) {
                dynamite->status = SMARTMOUSE_600;
                dynamite_set_smartmouse_600_fw(dynamite);
        } else if (!strncmp(buf, "cardprogrammer", 14)) {
                dynamite->status = CARDPROGRAMMER;
                dynamite_set_cardprogrammer_fw(dynamite);
        }

        return count;
}

static DEVICE_ATTR(status, S_IWUSR | S_IRUGO, show_status, store_status);
#endif

static long dynamite_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int result;

	struct usb_dynamite *dynamite = (struct usb_dynamite *)file->private_data;
	struct usb_interface *interface = usb_find_interface(&dynamite_driver, 0);

	struct dynamite_bulk_command dynamite_bulk_cmd;
	struct dynamite_vendor_command dynamite_vendor_cmd;
	struct dynamite_device_information_command dynamite_info_cmd;

	void *data;
	unsigned char *buffer;

        if (!dynamite || !dynamite->udevice)
                return -ENODEV;

	switch (cmd)
	{
		case IOCTL_SET_PHOENIX_357:
			dynamite->status = PHOENIX_357;
			result = dynamite_set_phoenix_357_fw(dynamite);
			if (result < 0)
				dev_err(&dynamite->uinterface->dev, "Error executing IOCTL_SET_PHOENIX_357 ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&dynamite->uinterface->dev, "Executed IOCTL_SET_PHOENIX_357 ioctl, result = %d", le32_to_cpu(result));
			break;
		case IOCTL_SET_PHOENIX_368:
			dynamite->status = PHOENIX_368;
			result = dynamite_set_phoenix_368_fw(dynamite);
			if (result < 0)
				dev_err(&dynamite->uinterface->dev, "Error executing IOCTL_SET_PHOENIX_368 ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&dynamite->uinterface->dev, "Executed IOCTL_SET_PHOENIX_368 ioctl, result = %d", le32_to_cpu(result));
			break;
		case IOCTL_SET_PHOENIX_400:
			dynamite->status = PHOENIX_400;
			result = dynamite_set_phoenix_400_fw(dynamite);
			if (result < 0)
				dev_err(&dynamite->uinterface->dev, "Error executing IOCTL_SET_PHOENIX_400 ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&dynamite->uinterface->dev, "Executed IOCTL_SET_PHOENIX_400 ioctl, result = %d", le32_to_cpu(result));
			break;
		case IOCTL_SET_PHOENIX_600:
			dynamite->status = PHOENIX_600;
			result = dynamite_set_phoenix_600_fw(dynamite);
			if (result < 0)
				dev_err(&dynamite->uinterface->dev, "Error executing IOCTL_SET_PHOENIX_600 ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&dynamite->uinterface->dev, "Executed IOCTL_SET_PHOENIX_600 ioctl, result = %d", le32_to_cpu(result));
			break;
		case IOCTL_SET_SMARTMOUSE_357:
			dynamite->status = SMARTMOUSE_357;
			result = dynamite_set_smartmouse_357_fw(dynamite);
			if (result < 0)
				dev_err(&dynamite->uinterface->dev, "Error executing IOCTL_SET_SMARTMOUSE_357 ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&dynamite->uinterface->dev, "Executed IOCTL_SET_SMARTMOUSE_357 ioctl, result = %d", le32_to_cpu(result));
			break;
		case IOCTL_SET_SMARTMOUSE_368:
			dynamite->status = SMARTMOUSE_368;
			result = dynamite_set_smartmouse_368_fw(dynamite);
			if (result < 0)
				dev_err(&dynamite->uinterface->dev, "Error executing IOCTL_SET_SMARTMOUSE_368 ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&dynamite->uinterface->dev, "Executed IOCTL_SET_SMARTMOUSE_368 ioctl, result = %d", le32_to_cpu(result));
			break;
		case IOCTL_SET_SMARTMOUSE_400:
			dynamite->status = SMARTMOUSE_400;
			result = dynamite_set_smartmouse_400_fw(dynamite);
			if (result < 0)
				dev_err(&dynamite->uinterface->dev, "Error executing IOCTL_SET_SMARTMOUSE_400 ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&dynamite->uinterface->dev, "Executed IOCTL_SET_SMARTMOUSE_400 ioctl, result = %d", le32_to_cpu(result));
			break;
		case IOCTL_SET_SMARTMOUSE_600:
			dynamite->status = SMARTMOUSE_600;
			result = dynamite_set_smartmouse_600_fw(dynamite);
			if (result < 0)
				dev_err(&dynamite->uinterface->dev, "Error executing IOCTL_SET_SMARTMOUSE_600 ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&dynamite->uinterface->dev, "Executed IOCTL_SET_SMARTMOUSE_600 ioctl, result = %d", le32_to_cpu(result));
			break;
		case IOCTL_SET_CARDPROGRAMMER:
			dynamite->status = CARDPROGRAMMER;
			result = dynamite_set_cardprogrammer_fw(dynamite);
			if (result < 0)
				dev_err(&dynamite->uinterface->dev, "Error executing IOCTL_SET_CARDPROGRAMMER ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&dynamite->uinterface->dev, "Executed IOCTL_SET_CARDPROGRAMMER ioctl, result = %d", le32_to_cpu(result));
			break;
		case IOCTL_RECV_VENDOR_COMMAND:
			data = (void *) arg;
			if (data == NULL)
				break;
			if (copy_from_user(&dynamite_vendor_cmd, data, sizeof(struct dynamite_vendor_command))) {
				result = -EFAULT;
				goto err_out;
			}
			if (dynamite_vendor_cmd.length < 0 || dynamite_vendor_cmd.length > PAGE_SIZE) {
				result = -EINVAL;
				goto err_out;
			}
			buffer = (unsigned char *) __get_free_page(GFP_KERNEL);
			if (!buffer) {
				result = -ENOMEM;
				goto err_out;
			}
			if (copy_from_user(buffer, dynamite_vendor_cmd.buffer, dynamite_vendor_cmd.length)) {
				result = -EFAULT;
				free_page((unsigned long) buffer);
				goto err_out;
			}
			result = vendor_command_rcv(dynamite, dynamite_vendor_cmd.request, dynamite_vendor_cmd.address, dynamite_vendor_cmd.index, buffer, dynamite_vendor_cmd.length);
			if (result < 0)
				dev_err(&dynamite->uinterface->dev, "Error executing IOCTL_RECV_VENDOR_COMMAND ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&dynamite->uinterface->dev, "Executed IOCTL_RECV_VENDOR_COMMAND ioctl, result = %d", le32_to_cpu(result));
			if (copy_to_user(dynamite_vendor_cmd.buffer, buffer, dynamite_vendor_cmd.length)) {
				free_page((unsigned long) buffer);
				result = -EFAULT;
				goto err_out;
			}
			free_page((unsigned long) buffer);
			break;
		case IOCTL_SEND_VENDOR_COMMAND:
			data = (void *) arg;
			if (data == NULL)
				break;
			if (copy_from_user(&dynamite_vendor_cmd, data, sizeof(struct dynamite_vendor_command))) {
				result = -EFAULT;
				goto err_out;
			}
			if (dynamite_vendor_cmd.length < 0 || dynamite_vendor_cmd.length > PAGE_SIZE) {
				result = -EINVAL;
				goto err_out;
			}
			buffer = (unsigned char *) __get_free_page(GFP_KERNEL);
			if (buffer == NULL) {
				result = -ENOMEM;
				goto err_out;
			}
			if (copy_from_user(buffer, dynamite_vendor_cmd.buffer, dynamite_vendor_cmd.length)) {
				free_page((unsigned long)buffer);
				result = -EFAULT;
				goto err_out;
			}
			result = vendor_command_snd(dynamite, dynamite_vendor_cmd.request, dynamite_vendor_cmd.address, dynamite_vendor_cmd.index, buffer, dynamite_vendor_cmd.length);
			if (result < 0)
				dev_err(&dynamite->uinterface->dev, "Error executing IOCTL_SEND_VENDOR_COMMAND ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&dynamite->uinterface->dev, "Executed IOCTL_SEND_VENDOR_COMMAND ioctl, result = %d", le32_to_cpu(result));
			free_page((unsigned long) buffer);
			break;
		case IOCTL_RECV_BULK_COMMAND:
			data = (void *) arg;
			if (data == NULL)
				break;
			if (copy_from_user(&dynamite_bulk_cmd, data, sizeof(struct dynamite_bulk_command))) {
				result = -EFAULT;
				goto err_out;
			}
			if (dynamite_bulk_cmd.length < 0 || dynamite_bulk_cmd.length > PAGE_SIZE) {
				result = -EINVAL;
				goto err_out;
			}
			buffer = (unsigned char *) __get_free_page(GFP_KERNEL);
			if (!buffer) {
				result = -ENOMEM;
				goto err_out;
			}
			if (copy_from_user(buffer, dynamite_bulk_cmd.buffer, dynamite_bulk_cmd.length)) {
				result = -EFAULT;
				free_page((unsigned long) buffer);
				goto err_out;
			}
			result = bulk_command_rcv(dynamite, buffer, dynamite_bulk_cmd.length, 0);
			if (result < 0)
				dev_err(&dynamite->uinterface->dev, "Error executing IOCTL_RECV_BULK_COMMAND ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&dynamite->uinterface->dev, "Executed IOCTL_RECV_BULK_COMMAND ioctl, result = %d", le32_to_cpu(result));
			if (copy_to_user(dynamite_bulk_cmd.buffer, buffer, dynamite_bulk_cmd.length)) {
				free_page((unsigned long) buffer);
				result = -EFAULT;
				goto err_out;
			}
			free_page((unsigned long) buffer);
			break;
		case IOCTL_SEND_BULK_COMMAND:
			data = (void *) arg;
			if (data == NULL)
				break;
			if (copy_from_user(&dynamite_bulk_cmd, data, sizeof(struct dynamite_bulk_command))) {
				result = -EFAULT;
				goto err_out;
			}
			if (dynamite_bulk_cmd.length < 0 || dynamite_bulk_cmd.length > PAGE_SIZE) {
				result = -EINVAL;
				goto err_out;
			}
			buffer = (unsigned char *) __get_free_page(GFP_KERNEL);
			if (buffer == NULL) {
				result = -ENOMEM;
				goto err_out;
			}
			if (copy_from_user(buffer, dynamite_bulk_cmd.buffer, dynamite_bulk_cmd.length)) {
				free_page((unsigned long)buffer);
				result = -EFAULT;
				goto err_out;
			}
			result = bulk_command_snd(dynamite, buffer, dynamite_bulk_cmd.length, 0);
			if (result < 0)
				dev_err(&dynamite->uinterface->dev, "Error executing IOCTL_SEND_BULK_COMMAND ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&dynamite->uinterface->dev, "Executed IOCTL_SEND_BULK_COMMAND ioctl, result = %d", le32_to_cpu(result));
			free_page((unsigned long) buffer);
			break;
		case IOCTL_DEVICE_INFORMATION_COMMAND:
			dynamite_info_cmd.device = dynamite->device_running;
			dynamite_info_cmd.status = dynamite->status;
			dynamite_info_cmd.vid = dynamite->udevice->descriptor.idVendor;
			dynamite_info_cmd.pid = dynamite->udevice->descriptor.idProduct;

			if (copy_to_user((void *)arg, &dynamite_info_cmd, sizeof(dynamite_info_cmd))) {
				result = -EFAULT;
				goto err_out;
			}
			break;
		default:
			dev_info(&dynamite->uinterface->dev, "Unknown ioctl command 0x%x\n", cmd);
			result = -ENOTTY;
			break;
	}
	return 0;
err_out:
	return result;
}

static ssize_t dynamite_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	int result;
	struct usb_dynamite *dynamite = (struct usb_dynamite *)file->private_data;
	struct usb_interface *interface = usb_find_interface(&dynamite_driver, 0);

	result = bulk_command_rcv(dynamite, dynamite->bulk_in_buffer, min(dynamite->bulk_in_size, count), count);

	if (!result) {
		if (copy_to_user(buffer, dynamite->bulk_in_buffer, count))
			result = -EFAULT;
		else
			result = count;
	}

	return result;
}

static void dynamite_write_bulk_callback(struct urb *urb)
{
	struct usb_dynamite *dynamite = urb->context;

	/* sync/async unlink faults aren't errors */
	if (urb->status && !(urb->status == -ENOENT || urb->status == -ECONNRESET || urb->status == -ESHUTDOWN)) {
		dev_dbg(&dynamite->uinterface->dev, "nonzero write bulk status received: %d", urb->status);
	}

	/* free up our allocated buffer */
	usb_free_coherent(urb->dev, urb->transfer_buffer_length, urb->transfer_buffer, urb->transfer_dma);
}

static ssize_t dynamite_write(struct file *file, const char __user *user_buffer, size_t count, loff_t *ppos)
{
	struct usb_dynamite *dynamite;
	int result;
	struct urb *urb = NULL;
	char *buf = NULL;

	dynamite = (struct usb_dynamite *)file->private_data;

	/* verify that we actually have some data to write */
	if (count == 0)
		goto exit;

	/* create a urb, and a buffer for it, and copy the data to the urb */
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		result = -ENOMEM;
		goto error;
	}

	buf = usb_alloc_coherent(dynamite->udevice, count, GFP_KERNEL, &urb->transfer_dma);
	if (!buf) {
		result = -ENOMEM;
		goto error;
	}
	if (copy_from_user(buf, user_buffer, count)) {
		result = -EFAULT;
		goto error;
	}

	/* initialize the urb properly */
	usb_fill_bulk_urb(urb, dynamite->udevice, usb_sndbulkpipe(dynamite->udevice, dynamite->bulk_out_endpointAddr), buf, count, dynamite_write_bulk_callback, dynamite);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* send the data out the bulk port */
	result = usb_submit_urb(urb, GFP_KERNEL);
	if (result) {
		dev_err(&dynamite->uinterface->dev, "failed submitting write urb, error %d", result);
		goto error;
	}

	if (debug)
		dump_buffer(dynamite, buf, "data_out", MAX_PKT_SIZE);

	/* release our reference to this urb, the USB core will eventually free it entirely */
	usb_free_urb(urb);

exit:
	return count;

error:
	usb_free_coherent(dynamite->udevice, count, buf, urb->transfer_dma);
	usb_free_urb(urb);
	kfree(buf);

	return result;
}

static int dynamite_open(struct inode *inode, struct file *file)
{
	int result;
	struct usb_interface *interface = usb_find_interface(&dynamite_driver, 0);
	struct usb_dynamite *dynamite = usb_get_intfdata(interface);

	kref_get(&dynamite->kref);

	file->private_data = dynamite;

	dev_dbg(&dynamite->uinterface->dev, "%s Reader/Programmer device opened\n", dynamite->device_name);

	return result;
}

static void dynamite_delete(struct kref *kref)
{
	struct usb_dynamite *dynamite = to_dynamite_dev(kref);

	usb_put_dev(dynamite->udevice);
	if (dynamite->bulk_in_buffer)
		kfree (dynamite->bulk_in_buffer);
	if (dynamite)
		kfree (dynamite);
}

static int dynamite_release(struct inode *inode, struct file *file)
{
	int result;

	struct usb_dynamite *dynamite;

	dynamite = (struct usb_dynamite *)file->private_data;
	if (dynamite == NULL)
		return -ENODEV;

	/* decrement the count on our device */
	kref_put(&dynamite->kref, dynamite_delete);

	dev_dbg(&dynamite->uinterface->dev, "%s Reader/Programmer device closed\n", dynamite->device_name);

	return result;
}

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(DYNAMITE_VENDOR_ID, DYNAMITE_PRODUCT_ID) },
	{ USB_DEVICE(DYNAMITE_PLUS_VENDOR_ID, DYNAMITE_PLUS_PREENUMERATION_PRODUCT_ID) },
	{ USB_DEVICE(DYNAMITE_PLUS_VENDOR_ID, DYNAMITE_PLUS_PRODUCT_ID) },
	{},
};
MODULE_DEVICE_TABLE(usb, id_table);

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver dynamite_driver = {
	.name		= "dynamite",
	.probe		= dynamite_probe,
	.disconnect	= dynamite_disconnect,
	.id_table	= id_table,
};

static const struct file_operations dynamite_fops = {
	.unlocked_ioctl	= dynamite_ioctl,
	.read		= dynamite_read,
	.write		= dynamite_write,
	.open		= dynamite_open,
	.release	= dynamite_release,
};

static struct usb_class_driver dynamite_class = {
	.name =		"dynamite_programmer",
	.fops =		&dynamite_fops,
};

static int dynamite_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	struct usb_dynamite *dynamite;
	int i, result = -ENOMEM;

	int init;

	dynamite = kzalloc(sizeof(struct usb_dynamite), GFP_KERNEL);
	if (!dynamite)
		goto error_mem;

	kref_init(&dynamite->kref);

	dynamite->udevice = usb_get_dev(interface_to_usbdev(interface));
	dynamite->uinterface = interface;

	if ((dynamite->udevice->descriptor.idVendor == DYNAMITE_VENDOR_ID) && (dynamite->udevice->descriptor.idProduct == DYNAMITE_PRODUCT_ID)) {
		dynamite->device_name = DYNAMITE;
		dynamite->device_running = DYNAMITE_DEVICE;
	} else if ((dynamite->udevice->descriptor.idVendor == DYNAMITE_PLUS_VENDOR_ID) && ((dynamite->udevice->descriptor.idProduct == DYNAMITE_PLUS_PREENUMERATION_PRODUCT_ID) || (dynamite->udevice->descriptor.idProduct == DYNAMITE_PLUS_PRODUCT_ID))) {
		dynamite->device_name = DYNAMITE_PLUS;
		dynamite->device_running = DYNAMITE_PLUS_DEVICE;
	} else {
		dynamite->device_running = NONE_DEVICE;
	}

	/* set up the endpoint information */
	/* use only the first bulk-in and bulk-out endpoints */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!dynamite->bulk_in_endpointAddr &&
		    (endpoint->bEndpointAddress & USB_DIR_IN) &&
		    ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
					== USB_ENDPOINT_XFER_BULK)) {
			/* we found a bulk in endpoint */
			buffer_size = endpoint->wMaxPacketSize;
			dynamite->bulk_in_size = buffer_size;
			dynamite->bulk_in_endpointAddr = endpoint->bEndpointAddress;
			dynamite->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
			if (!dynamite->bulk_in_buffer) {
				dev_err(&interface->dev, "Could not allocate bulk_in_buffer\n");
				goto error;
			}
		}

		if (!dynamite->bulk_out_endpointAddr &&
		    !(endpoint->bEndpointAddress & USB_DIR_IN) &&
		    ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
					== USB_ENDPOINT_XFER_BULK)) {
			/* we found a bulk out endpoint */
			dynamite->bulk_out_endpointAddr = endpoint->bEndpointAddress;
		}
	}

	if (!(dynamite->bulk_in_endpointAddr && dynamite->bulk_out_endpointAddr) && (dynamite->status != NOFW)) {
		dev_err(&interface->dev, "Could not find both bulk-in and bulk-out endpoints\n");
		goto error;
	}

#if 1
	if ((dynamite->udevice->descriptor.iManufacturer != NULL) && (dynamite->udevice->descriptor.iProduct != NULL)) {
		dev_info(&dynamite->uinterface->dev, "%s send init command\n", dynamite->device_name);
		result = send_init_command(dynamite);
		if (result < 0)
			dev_err(&dynamite->uinterface->dev, "%s error send init command\n", dynamite->device_name);
	}
#endif
	usb_set_intfdata(interface, dynamite);

	mutex_init(&dynamite->lock);

	result = device_create_file(&interface->dev, &dev_attr_status);
	if (result < 0)
		goto error;

	/* we can register the device now, as it is ready */
	result = usb_register_dev(interface, &dynamite_class);
	if (result < 0) {
		dev_err(&interface->dev, "Not able to get a minor for this device\n");
		goto error;
	}

	if ((dynamite->udevice->descriptor.iManufacturer == NULL) && (dynamite->udevice->descriptor.iProduct == NULL)) {
		dynamite->status = NOFW;
		dynamite_set_init_fw(dynamite);
	} else {
		dynamite->status = READY;
		//unsigned char buf[64];
		//read_eeprom(dynamite, buf, 64, 0);
	}

	dev_info(&interface->dev, "%s Reader/Programmer now attached\n", dynamite->device_name);

	return 0;
error:
	device_remove_file(&interface->dev, &dev_attr_status);
	usb_set_intfdata (interface, NULL);

	if (dynamite)
		kref_put(&dynamite->kref, dynamite_delete);

	return result;

error_mem:
	return result;
}

static void dynamite_disconnect(struct usb_interface *interface)
{
	struct usb_dynamite *dynamite;
	dynamite = usb_get_intfdata(interface);

	usb_deregister_dev(interface, &dynamite->uclass);
	device_remove_file(&interface->dev, &dev_attr_status);

	/* first remove the files, then NULL the pointer */
	usb_set_intfdata (interface, NULL);
	mutex_destroy(&dynamite->lock);
	kref_put(&dynamite->kref, dynamite_delete);
	dev_info(&interface->dev, "%s Reader/Programmer now disconnected\n", dynamite->device_name);
}


static int __init dynamite_usb_init(void)
{
	int result;

	/* register this driver with the USB subsystem */
	result = usb_register(&dynamite_driver);

	return result;
}

static void __exit dynamite_usb_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&dynamite_driver);
}

module_init(dynamite_usb_init);
module_exit(dynamite_usb_exit);

#define DRIVER_AUTHOR "redblue"
#define DRIVER_DESC "Duolabs Dynamite Programmer driver"

module_param(debug, int, 0660);
module_param(load_fx1_fw, int, 0660);
module_param(load_fx2_fw, int, 0660);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
