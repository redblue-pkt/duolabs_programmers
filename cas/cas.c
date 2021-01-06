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

#include <linux/kernel.h>
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

#include "cas.h"

#include "../ezusb/ezusb.h"

#include "cas_ioctl.h"
#include "cas_init.h"
#include "cas_commands.h"

static int debug = DEBUG_NONE;
static int load_fx1_fw = 0;
static int load_fx2_fw = 0;

#define to_cas_dev(d) container_of(d, struct usb_cas, kref)

static struct usb_driver cas_driver;
static const struct file_operations cas_fops;

/* local function prototypes */
static int cas_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void cas_disconnect(struct usb_interface *interface);

static void wait_for_finish(struct usb_cas *cas, unsigned long usecs)
{
	int result;

	dev_dbg(&cas->uinterface->dev, "%s: time %lu\n", __func__, usecs);

	udelay(usecs);
}

static inline char printable(char c)
{
	return (c < ' ') || (c > '~') ? '-' : c;
}

static void dump_buffer(struct usb_cas *cas, unsigned char *buffer, char *name, int len)
{
	int n, i;
	int j = 16 - len;

	for (n = 0; n < len; n += i) {
		if (!strncmp(name, "data_out", 8))
			internal_dev_info_green(&cas->uinterface->dev, "%s ", name);
		else if (!strncmp(name, "data_in", 7))
			internal_dev_info_blue(&cas->uinterface->dev, "%s ", name);
		else
			internal_dev_info(&cas->uinterface->dev, "");
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

static int vendor_command_snd(struct usb_cas *cas, unsigned char request, int address, int index, const char *buf, int size)
{
	int result;
	unsigned char *buffer = kmemdup(buf, size, GFP_KERNEL);

	if (!buffer) {
		dev_err(&cas->uinterface->dev, "%s: kmalloc(%d) failed\n", __func__, size);
		return -ENOMEM;
	}

	mutex_lock(&cas->lock);

	if ((debug != DEBUG_NONE && debug != FULL_DEBUG_IN && debug != SIMPLE_DEBUG_IN) && buffer != NULL)
		dump_buffer(cas, buffer, "data_out", size);

	result = usb_control_msg(cas->udevice, usb_sndctrlpipe(cas->udevice, 0), request, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, address, 0, buffer, size, 3000);

	mutex_unlock(&cas->lock);

	if (buffer)
        	kfree (buffer);
        return result;
}

static int vendor_command_rcv(struct usb_cas *cas, unsigned char request, int address, int index, char *buf, int size)
{
	int result;

	mutex_lock(&cas->lock);

	result = usb_control_msg(cas->udevice, usb_rcvctrlpipe(cas->udevice, 0), request, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, address, 0, buf, size, 3000);

	if ((debug != DEBUG_NONE && debug != FULL_DEBUG_OUT && debug != SIMPLE_DEBUG_OUT) && buf != NULL)
		dump_buffer(cas, buf, "data_in", size);

	mutex_unlock(&cas->lock);

	return result;
}

static int bulk_command_snd(struct usb_cas *cas, const char *buf, int size, int count)
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
		dev_err(&cas->uinterface->dev, "%s: kmalloc(%d) failed\n", __func__, size);
		return -ENOMEM;
	}

	mutex_lock(&cas->lock);

	if ((debug != DEBUG_NONE && debug != FULL_DEBUG_IN && debug != SIMPLE_DEBUG_IN) && buffer != NULL)
		dump_buffer(cas, buffer, "data_out", MAX_PKT_SIZE);

	result = usb_bulk_msg(cas->udevice, usb_sndbulkpipe(cas->udevice, cas->bulk_out_endpointAddr), buffer, size, NULL, 1000);

	mutex_unlock(&cas->lock);

	if (buffer)
		kfree (buffer);
	return result;
}

static int bulk_command_rcv(struct usb_cas *cas, char *buf, int size, int count)
{
	int result;

	mutex_lock(&cas->lock);

	result = usb_bulk_msg(cas->udevice, usb_rcvbulkpipe(cas->udevice, cas->bulk_in_endpointAddr), buf, size, NULL, 1000);

	if ((debug != DEBUG_NONE && debug != FULL_DEBUG_OUT && debug != SIMPLE_DEBUG_OUT) && buf != NULL)
		dump_buffer(cas, buf, "data_in", MAX_PKT_SIZE);

	mutex_unlock(&cas->lock);

	return result;
}

static int read_eeprom(struct usb_cas *cas,unsigned char *buf, int len, int offset)
{
	int i, result;
	char *v, br[2];
	for (i=0; i < len; i++) {
		v = offset + i;
		result = vendor_command_rcv(cas, 0xA2, 0, 0, v, 2);
		if (result)
			return result;

		buf[i] = br[1];
	}
	dev_info(&cas->uinterface->dev, "read eeprom (offset: 0x%x, len: %d) : ", offset, i);
	return result;
}

static int device_verification(struct usb_cas *cas, int type)
{
	int result;

	if (cas->device_running == CAS2_DEVICE)
		/* todo */
		result = -1;
	else if (cas->device_running == CAS2_PLUS_DEVICE)
		/* todo */
		result = -1;
	else if (cas->device_running == CAS2_PLUS2_DEVICE)
		/* todo */
		result = -1;
	else if (cas->device_running == CAS2_PLUS2_CRYPTO_DEVICE) {
		switch (type) {
			case CAM:
			case MM:
			case JTAG:
			case PROGRAMMER:
			case DIABLO:
			case DRAGON:
			case EXTREME:
			case JOKER:
			case XCAM:
			case HOST:
				result = 1;
				break;
			default:
				result = -1;
				break;
		}
	}

	if (result < 0)
		dev_err(&cas->uinterface->dev, "Error, not allowed set to %s mode for %s device\n", cas_device_status[type], cas->device_name);

	return result;
}

static int send_command(struct usb_cas *cas, int id)
{
	int i, result;
	const struct cas_hex_record *record = NULL;

	if (id == START) {
		if (cas->device_running == CAS2_DEVICE)
			record = &cas2_init_code[0];
		else if (cas->device_running == CAS2_PLUS_DEVICE)
			record = &cas2_plus_init_code[0];
                else if (cas->device_running == CAS2_PLUS2_DEVICE)
                        record = &cas2_plus2_init_code[0];
                else if (cas->device_running == CAS2_PLUS2_CRYPTO_DEVICE)
                        record = &cas2_plus2_crypto_plus_init_code[0];
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
	else if (id == HOST)
		record = &cam_host_code[0];

	while(record->data_size != 0) {
		result = bulk_command_snd(cas, (unsigned char *)record->data, record->data_size, 0);
		result = bulk_command_rcv(cas, cas->bulk_in_buffer, MAX_PKT_SIZE, 0);

		if (result < 0)
			goto out;

		record++;
	}

	return 0;
out:
	return result;
}

static int send_init_command(struct usb_cas *cas)
{
	return send_command(cas, START);
}

static int send_phoenix_357_command(struct usb_cas *cas)
{
	return send_command(cas, PHOENIX_357);
}

static int send_phoenix_368_command(struct usb_cas *cas)
{
	return send_command(cas, PHOENIX_368);
}

static int send_phoenix_400_command(struct usb_cas *cas)
{
	return send_command(cas, PHOENIX_400);
}

static int send_phoenix_600_command(struct usb_cas *cas)
{
	return send_command(cas, PHOENIX_600);
}

static int send_smartmouse_357_command(struct usb_cas *cas)
{
	return send_command(cas, SMARTMOUSE_357);
}

static int send_smartmouse_368_command(struct usb_cas *cas)
{
	return send_command(cas, SMARTMOUSE_368);
}

static int send_smartmouse_400_command(struct usb_cas *cas)
{
	return send_command(cas, SMARTMOUSE_400);
}

static int send_smartmouse_600_command(struct usb_cas *cas)
{
	return send_command(cas, SMARTMOUSE_600);
}

static int send_host_command(struct usb_cas *cas)
{
	return send_command(cas, HOST);
}

static int cas_firmware_load(struct usb_cas *cas, int id, int reset_cpu)
{
	int response = -ENOENT;
	const char *fw_name;

	dev_dbg(&cas->uinterface->dev, "%s: sending %s...", __func__, fw_name);

	if (reset_cpu != NO_RESET_CPU) {
		dev_dbg(&cas->uinterface->dev, "%s reset cpu\n", cas->device_name);
		if (cas->device_running == CAS2_DEVICE)
			response = ezusb_fx1_set_reset(cas->udevice, 1);
		else if (cas->device_running == CAS2_PLUS_DEVICE || cas->device_running == CAS2_PLUS2_DEVICE || cas->device_running == CAS2_PLUS2_CRYPTO_DEVICE)
			response = ezusb_fx2_set_reset(cas->udevice, 1);
	}

	if (response < 0)
		goto out;

	if (0) { ; }
	else if (le16_to_cpu(id) == VEND_AX) {
		if (cas->device_running == CAS2_DEVICE)
			fw_name = "cas2/vend_ax.fw";
		cas->state = START_LOAD_VEND_AX_FW;
	} else if (le16_to_cpu(id) == START) {
		if (load_fx1_fw)
			fw_name = "fx1/start.fw";
		else if (load_fx2_fw)
			fw_name = "fx2/start.fw";
		else {
			if (cas->device_running == CAS2_DEVICE)
				fw_name = "cas2/start.fw";
			else if (cas->device_running == CAS2_PLUS_DEVICE)
				fw_name = "cas2plus/start.fw";
			else if (cas->device_running == CAS2_PLUS2_DEVICE)
				fw_name = "cas2plus2/start.fw";
			else if (cas->device_running = CAS2_PLUS2_CRYPTO_DEVICE)
				fw_name = "cas2pluscrypto/start.fw";
		}
		cas->state = START_LOAD_START_FW;
	} else if (le16_to_cpu(id) == CAM) {
		/* todo cam2 interface */
		if (cas->device_running == CAS2_PLUS_DEVICE)
			fw_name = "cas2plus/cam.fw";
		else if (cas->device_running == CAS2_PLUS2_DEVICE)
			fw_name = "cas2plus2/cam.fw";
		else if (cas->device_running == CAS2_PLUS2_CRYPTO_DEVICE)
			fw_name = "cas2pluscrypto/cam.fw";
		cas->state = START_LOAD_CAM_FW;
	} else if (le16_to_cpu(id) == HOST) {
		/* todo this work in other devices ?? */
		if (cas->device_running == CAS2_PLUS2_CRYPTO_DEVICE)
			fw_name = "cas2pluscrypto/cam.fw";
		cas->state = START_LOAD_HOST_FW;
	} else if (le16_to_cpu(id) == MM) {
		if (cas->device_running == CAS2_DEVICE)
			fw_name = "cas2/mm.fw";
		else if (cas->device_running == CAS2_PLUS_DEVICE)
			fw_name = "cas2plus/mm.fw";
		else if (cas->device_running == CAS2_PLUS2_DEVICE)
			fw_name = "cas2plus2/mm.fw";
		else if (cas->device_running == CAS2_PLUS2_CRYPTO_DEVICE)
			fw_name = "cas2pluscrypto/mm.fw";
		cas->state = START_LOAD_MM_FW;
	} else if (le16_to_cpu(id) == JTAG) {
		if (cas->device_running == CAS2_DEVICE)
			fw_name = "cas2/jtag.fw";
		else if (cas->device_running == CAS2_PLUS_DEVICE)
			fw_name = "cas2plus/jtag.fw";
		else if (cas->device_running == CAS2_PLUS2_DEVICE)
			fw_name = "cas2plus2/jtag.fw";
		else if (cas->device_running == CAS2_PLUS2_CRYPTO_DEVICE)
			fw_name = "cas2pluscrypto/jtag.fw";
		cas->state = START_LOAD_JTAG_FW;
	} else if (le16_to_cpu(id) == MOUSE_PHOENIX) {
		if (cas->device_running == CAS2_DEVICE)
			fw_name = "cas2/phoenix.fw";
		cas->state = START_LOAD_MOUSE_PHOENIX_FW;
	} else if (le16_to_cpu(id) == PROGRAMMER) {
		if (cas->device_running == CAS2_DEVICE)
			fw_name = "cas2/programmer.fw";
		cas->state = START_LOAD_PROGRAMMER_FW;
	} else if (le16_to_cpu(id) == DREAMBOX) {
		if (cas->device_running == CAS2_DEVICE)
			fw_name = "cas2/dreambox.fw";
		cas->state = START_LOAD_DREAMBOX_FW;
	} else if (le16_to_cpu(id) == EXTREME) {
		if (cas->device_running == CAS2_DEVICE)
			fw_name = "cas2/extreme.fw";
		cas->state = START_LOAD_EXTREME_FW;
	} else if (le16_to_cpu(id) == DIABLO) {
		if (cas->device_running == CAS2_DEVICE)
			fw_name = "cas2/diablo.fw";
		else if (cas->device_running == CAS2_PLUS_DEVICE)
			fw_name = "cas2plus/diablo.fw";
		else if (cas->device_running == CAS2_PLUS2_DEVICE)
			fw_name = "cas2plus2/diablo.fw";
		else if (cas->device_running == CAS2_PLUS2_CRYPTO_DEVICE)
			fw_name = "cas2pluscrypto/diablo.fw";
		cas->state = START_LOAD_DIABLO_FW;
	} else if (le16_to_cpu(id) == DRAGON) {
		if (cas->device_running == CAS2_DEVICE)
			fw_name = "cas2/dragon.fw";
		else if (cas->device_running == CAS2_PLUS_DEVICE)
			fw_name = "cas2plus/cam.fw";
		else if (cas->device_running == CAS2_PLUS2_DEVICE)
			fw_name = "cas2plus2/cam.fw";
		else if (cas->device_running == CAS2_PLUS2_CRYPTO_DEVICE)
			fw_name = "cas2pluscrypto/cam.fw";
		cas->state = START_LOAD_DRAGON_FW;
	} else if (le16_to_cpu(id) == XCAM) {
		if (cas->device_running == CAS2_DEVICE)
			fw_name = "cas2/xcam.fw";
		else if (cas->device_running == CAS2_PLUS_DEVICE)
			fw_name = "cas2plus/cam.fw";
		else if (cas->device_running == CAS2_PLUS2_DEVICE)
			fw_name = "cas2plus2/cam.fw";
		else if (cas->device_running == CAS2_PLUS2_CRYPTO_DEVICE)
			fw_name = "cas2pluscrypto/cam.fw";
		cas->state = START_LOAD_XCAM_FW;
	} else if (le16_to_cpu(id) == JOKER) {
		if (cas->device_running == CAS2_DEVICE)
			fw_name = "cas2/joker.fw";
		else if (cas->device_running == CAS2_PLUS_DEVICE)
			fw_name = "cas2plus/joker.fw";
		else if (cas->device_running == CAS2_PLUS2_DEVICE)
			fw_name = "cas2plus2/joker.fw";
		else if (cas->device_running == CAS2_PLUS2_CRYPTO_DEVICE)
			fw_name = "cas2pluscrypto/joker.fw";
		cas->state = START_LOAD_JOKER_FW;
	} else {
		dev_err(&cas->uinterface->dev, "%s: unknown fw request, aborting\n",
			__func__);
		goto out;
	}

	if (cas->device_running == CAS2_DEVICE) {
		if (ezusb_fx1_ihex_firmware_download(cas->udevice, fw_name) < 0) {
			dev_err(&cas->uinterface->dev, "failed to load firmware \"%s\"\n",
				fw_name);
			return -ENOENT;
		}
	}
	else if (cas->device_running == CAS2_PLUS_DEVICE || cas->device_running == CAS2_PLUS2_DEVICE || cas->device_running == CAS2_PLUS2_CRYPTO_DEVICE)
	{
		if (ezusb_fx2_ihex_firmware_download(cas->udevice, fw_name) < 0) {
			dev_err(&cas->uinterface->dev, "failed to load firmware \"%s\"\n",
				fw_name);
			return -ENOENT;
		}
	}

	if (reset_cpu != NO_RESET_CPU) {
		dev_dbg(&cas->uinterface->dev, "Cas Programmer reset cpu\n");
		if (cas->device_running == CAS2_DEVICE)
			response = ezusb_fx1_set_reset(cas->udevice, 0);
		else if (cas->device_running == CAS2_PLUS_DEVICE || cas->device_running == CAS2_PLUS2_DEVICE || cas->device_running == CAS2_PLUS2_CRYPTO_DEVICE)
			response = ezusb_fx2_set_reset(cas->udevice, 0);
	}

	if (response < 0)
		goto out;

	if (cas->state == START_LOAD_VEND_AX_FW)
		cas->state = FINISH_LOAD_VEND_AX_FW;
	else if (cas->state == START_LOAD_START_FW)
		cas->state = FINISH_LOAD_START_FW;
	else if (cas->state == START_LOAD_CAM_FW)
		cas->state = FINISH_LOAD_CAM_FW;
	else if (cas->state == START_LOAD_MM_FW)
		cas->state = FINISH_LOAD_MM_FW;
	else if (cas->state == START_LOAD_JTAG_FW)
		cas->state = FINISH_LOAD_JTAG_FW;
	else if (cas->state == START_LOAD_MOUSE_PHOENIX_FW)
		cas->state = FINISH_LOAD_MOUSE_PHOENIX_FW;
	else if (cas->state == START_LOAD_PROGRAMMER_FW)
		cas->state = FINISH_LOAD_PROGRAMMER_FW;
	else if (cas->state == START_LOAD_DREAMBOX_FW)
		cas->state = FINISH_LOAD_DREAMBOX_FW;
	else if (cas->state == START_LOAD_EXTREME_FW)
		cas->state = FINISH_LOAD_EXTREME_FW;
	else if (cas->state == START_LOAD_DIABLO_FW)
		cas->state = FINISH_LOAD_DIABLO_FW;
	else if (cas->state == START_LOAD_DRAGON_FW)
		cas->state = FINISH_LOAD_DRAGON_FW;
	else if (cas->state == START_LOAD_XCAM_FW)
		cas->state = FINISH_LOAD_XCAM_FW;
	else if (cas->state == START_LOAD_JOKER_FW)
		cas->state = FINISH_LOAD_JOKER_FW;
	else if (cas->state == START_LOAD_HOST_FW)
		cas->state = FINISH_LOAD_HOST_FW;

	return 1;
out:
	return response;
}

static int cas_set_init_fw(struct usb_cas *cas)
{
	int result;

	if (cas->device_running == CAS2_DEVICE) {
		result = cas_firmware_load(cas, VEND_AX, RESET_CPU);
		if (result < 0)
			dev_info(&cas->uinterface->dev, "%s error load VEND_AX\n", cas->device_name);

		wait_for_finish(cas, WAIT_FOR_FW);
	}

	result = cas_firmware_load(cas, START, RESET_CPU);
	if (result < 0)
		dev_info(&cas->uinterface->dev, "%s error load START\n", cas->device_name);

	wait_for_finish(cas, WAIT_FOR_FW);

#if 0
	result = send_init_command(cas);
	if (result < 0)
		dev_info(&cas->uinterface->dev, "%s error send init command\n", cas->device_name);

	wait_for_finish(cas, WAIT_FOR_FW);
#endif

	return result;
}

static int cas_set_cam_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_CAM_FW) {
		result = cas_firmware_load(cas, CAM, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	//result = send_cam_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to cam mode\n", cas->device_name);

	return result;
}

static int cas_set_mm_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_MM_FW) {
		result = cas_firmware_load(cas, MM, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	//result = send_mm_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to mm mode\n", cas->device_name);

	return result;
}

static int cas_set_jtag_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_JTAG_FW) {
		result = cas_firmware_load(cas, JTAG, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	//result = send_jtag_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to jtag mode\n", cas->device_name);

	return result;
}

static int cas_set_phoenix_357_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = cas_firmware_load(cas, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	result = send_phoenix_357_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to phoenix mode 357 mhz\n", cas->device_name);

	return result;
}

static int cas_set_phoenix_368_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = cas_firmware_load(cas, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	result = send_phoenix_368_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to phoenix mode 368 mhz\n", cas->device_name);

	return result;
}

static int cas_set_phoenix_400_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = cas_firmware_load(cas, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	result = send_phoenix_400_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to phoenix mode 400 mhz\n", cas->device_name);


	return result;
}

static int cas_set_phoenix_600_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = cas_firmware_load(cas, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	result = send_phoenix_600_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to phoenix mode 600 mhz\n", cas->device_name);

	return result;
}

static int cas_set_smartmouse_357_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = cas_firmware_load(cas, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	result = send_smartmouse_357_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to smartmouse mode 357 mhz\n", cas->device_name);

	return result;
}

static int cas_set_smartmouse_368_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = cas_firmware_load(cas, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	result = send_smartmouse_368_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to smartmouse mode 368 mhz\n", cas->device_name);

	return result;
}

static int cas_set_smartmouse_400_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = cas_firmware_load(cas, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	result = send_smartmouse_400_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to smartmouse mode 400 mhz\n", cas->device_name);

	return result;
}

static int cas_set_smartmouse_600_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_MOUSE_PHOENIX_FW) {
		result = cas_firmware_load(cas, MOUSE_PHOENIX, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	result = send_smartmouse_600_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to smartmouse mode 600 mhz\n", cas->device_name);

	return result;
}

static int cas_set_programmer_fw(struct usb_cas *cas)
{
	int result;

        result = cas_firmware_load(cas, PROGRAMMER, RESET_CPU);
	wait_for_finish(cas, WAIT_FOR_FW);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to programmer mode\n", cas->device_name);

        return result;
}

static int cas_set_dreambox_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_DREAMBOX_FW) {
		result = cas_firmware_load(cas, DREAMBOX, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	//result = send_dreambox_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to dreambox mode\n", cas->device_name);

	return result;
}

static int cas_set_extreme_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_EXTREME_FW) {
		result = cas_firmware_load(cas, EXTREME, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	//result = send_extreme_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to extreme mode\n", cas->device_name);

	return result;
}

static int cas_set_diablo_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_DIABLO_FW) {
		result = cas_firmware_load(cas, DIABLO, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	//result = send_diablo_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to diablo mode\n", cas->device_name);

	return result;
}

static int cas_set_dragon_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_DRAGON_FW) {
		result = cas_firmware_load(cas, DRAGON, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	//result = send_dragon_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to dragon mode\n", cas->device_name);

	return result;
}

static int cas_set_xcam_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_XCAM_FW) {
		result = cas_firmware_load(cas, XCAM, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	//result = send_xcam_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to xcam mode\n", cas->device_name);

	return result;
}

static int cas_set_joker_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_JOKER_FW) {
		result = cas_firmware_load(cas, JOKER, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	//result = send_joker_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to joker mode\n", cas->device_name);

	return result;
}

static int cas_set_host_fw(struct usb_cas *cas)
{
	int result;

	if (cas->state != FINISH_LOAD_HOST_FW) {
		result = cas_firmware_load(cas, HOST, RESET_CPU);
		wait_for_finish(cas, WAIT_FOR_FW);
	}

	result = send_host_command(cas);
	if (result >= 0)
		dev_info(&cas->uinterface->dev, "%s set to host mode\n", cas->device_name);

	return result;
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *interface = usb_find_interface(&cas_driver, 0);
	struct usb_cas *cas = usb_get_intfdata(interface);

	return sprintf(buf, "%s", cas_device_status[cas->status]);
}

static ssize_t status_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *interface = usb_find_interface(&cas_driver, 0);
	struct usb_cas *cas = usb_get_intfdata(interface);

	if (!strncmp(buf, "cam", 3)) {
		if (device_verification(cas, CAM)) {
			cas->status = CAM;
			cas_set_cam_fw(cas);
		}
	} else if (!strncmp(buf, "mm", 2)) {
		if (device_verification(cas, MM)) {
			cas->status = MM;
			cas_set_mm_fw(cas);
		}
	} else if (!strncmp(buf, "jtag", 4)) {
		if (device_verification(cas, JTAG)) {
			cas->status = JTAG;
			cas_set_jtag_fw(cas);
		}
	} else if (!strncmp(buf, "phoenix357", 10)) {
		if (device_verification(cas, PHOENIX_357)) {
			cas->status = PHOENIX_357;
			cas_set_phoenix_357_fw(cas);
		}
	} else if (!strncmp(buf, "phoenix368", 10)) {
		if (device_verification(cas, PHOENIX_368)) {
			cas->status = PHOENIX_368;
			cas_set_phoenix_368_fw(cas);
		}
	} else if (!strncmp(buf, "phoenix400", 10)) {
		if (device_verification(cas, PHOENIX_400)) {
			cas->status = PHOENIX_400;
			cas_set_phoenix_400_fw(cas);
		}
	} else if (!strncmp(buf, "phoenix600", 10)) {
		if (device_verification(cas, PHOENIX_600)) {
			cas->status = PHOENIX_600;
			cas_set_phoenix_600_fw(cas);
		}
	} else if (!strncmp(buf, "smartmouse357", 13)) {
		if (device_verification(cas, SMARTMOUSE_357)) {
			cas->status = SMARTMOUSE_357;
			cas_set_smartmouse_357_fw(cas);
		}
	} else if (!strncmp(buf, "smartmouse368", 13)) {
		if (device_verification(cas, SMARTMOUSE_368)) {
			cas->status = SMARTMOUSE_368;
			cas_set_smartmouse_368_fw(cas);
		}
	} else if (!strncmp(buf, "smartmouse400", 13)) {
		if (device_verification(cas, SMARTMOUSE_400)) {
			cas->status = SMARTMOUSE_400;
			cas_set_smartmouse_400_fw(cas);
		}
	} else if (!strncmp(buf, "smartmouse600", 13)) {
		if (device_verification(cas, SMARTMOUSE_600)) {
			cas->status = SMARTMOUSE_600;
			cas_set_smartmouse_600_fw(cas);
		}
        } else if (!strncmp(buf, "programmer", 14)) {
		if (device_verification(cas, PROGRAMMER)) {
			cas->status = PROGRAMMER;
			cas_set_programmer_fw(cas);
		}
	} else if (!strncmp(buf, "dreambox", 8)) {
		if (device_verification(cas, DREAMBOX)) {
			cas->status = DREAMBOX;
			cas_set_dreambox_fw(cas);
		}
	} else if (!strncmp(buf, "diablo", 6)) {
		if (device_verification(cas, DIABLO)) {
			cas->status = DIABLO;
			cas_set_diablo_fw(cas);
		}
	} else if (!strncmp(buf, "dragon", 6)) {
		if (device_verification(cas, DRAGON)) {
			cas->status = DRAGON;
			cas_set_dragon_fw(cas);
		}
	} else if (!strncmp(buf, "extreme", 7)) {
		if (device_verification(cas, EXTREME)) {
			cas->status = EXTREME;
			cas_set_extreme_fw(cas);
		}
	} else if (!strncmp(buf, "xcam", 4)) {
		if (device_verification(cas, XCAM)) {
			cas->status = XCAM;
			cas_set_xcam_fw(cas);
		}
	} else if (!strncmp(buf, "joker", 5)) {
		if (device_verification(cas, JOKER)) {
			cas->status = JOKER;
			cas_set_programmer_fw(cas);
		}
	} else if (!strncmp(buf, "host", 4)) {
		if (device_verification(cas, HOST)) {
			cas->status = HOST;
			cas_set_host_fw(cas);
		}
	}

	return count;
}
static DEVICE_ATTR_RW(status);

static long cas_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int result;

	struct usb_cas *cas = (struct usb_cas *)file->private_data;
	struct usb_interface *interface = usb_find_interface(&cas_driver, 0);

	struct cas_bulk_command cas_bulk_cmd;
	struct cas_vendor_command cas_vendor_cmd;
	struct cas_device_information_command cas_info_cmd;

	void *data;
	unsigned char *buffer;

        if (!cas || !cas->udevice)
                return -ENODEV;

	switch (cmd)
	{
		case IOCTL_SET_CAM:
			if (device_verification(cas, CAM)) {
				cas->status = CAM;
				result = cas_set_cam_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_CAM ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_CAM ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_MM:
			if (device_verification(cas, MM)) {
				cas->status = MM;
				result = cas_set_mm_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_MM ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_MM ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_JTAG:
			if (device_verification(cas, JTAG)) {
				cas->status = JTAG;
				result = cas_set_jtag_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_JTAG ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_JTAG ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_PHOENIX_357:
			if (device_verification(cas, PHOENIX_357)) {
				cas->status = PHOENIX_357;
				result = cas_set_phoenix_357_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_PHOENIX_357 ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_PHOENIX_357 ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_PHOENIX_368:
			if (device_verification(cas, PHOENIX_368)) {
				cas->status = PHOENIX_368;
				result = cas_set_phoenix_368_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_PHOENIX_368 ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_PHOENIX_368 ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_PHOENIX_400:
			if (device_verification(cas, PHOENIX_400)) {
				cas->status = PHOENIX_400;
				result = cas_set_phoenix_400_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_PHOENIX_400 ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_PHOENIX_400 ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_PHOENIX_600:
			if (device_verification(cas, PHOENIX_600)) {
				cas->status = PHOENIX_600;
				result = cas_set_phoenix_600_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_PHOENIX_600 ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_PHOENIX_600 ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_SMARTMOUSE_357:
			if (device_verification(cas, SMARTMOUSE_357)) {
				cas->status = SMARTMOUSE_357;
				result = cas_set_smartmouse_357_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_SMARTMOUSE_357 ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_SMARTMOUSE_357 ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_SMARTMOUSE_368:
			if (device_verification(cas, SMARTMOUSE_368)) {
				cas->status = SMARTMOUSE_368;
				result = cas_set_smartmouse_368_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_SMARTMOUSE_368 ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_SMARTMOUSE_368 ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_SMARTMOUSE_400:
			if (device_verification(cas, SMARTMOUSE_400)) {
				cas->status = SMARTMOUSE_400;
				result = cas_set_smartmouse_400_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_SMARTMOUSE_400 ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_SMARTMOUSE_400 ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_SMARTMOUSE_600:
			if (device_verification(cas, SMARTMOUSE_600)) {
				cas->status = SMARTMOUSE_600;
				result = cas_set_smartmouse_600_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_SMARTMOUSE_600 ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_SMARTMOUSE_600 ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_PROGRAMMER:
			if (device_verification(cas, PROGRAMMER)) {
				cas->status = PROGRAMMER;
				result = cas_set_programmer_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_PROGRAMMER ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_PROGRAMMER ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_DREAMBOX:
			if (device_verification(cas, DREAMBOX)) {
				cas->status = DREAMBOX;
				result = cas_set_dreambox_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_DREAMBOX ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_DREAMBOX ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_EXTREME:
			if (device_verification(cas, EXTREME)) {
				cas->status = EXTREME;
				result = cas_set_extreme_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_EXTREME ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_EXTREME ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_DIABLO:
			if (device_verification(cas, DIABLO)) {
				cas->status = DIABLO;
				result = cas_set_diablo_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_DIABLO ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_DIABLO ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_DRAGON:
			if (device_verification(cas, DRAGON)) {
				cas->status = DRAGON;
				result = cas_set_dragon_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_DRAGON ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_DRAGON ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_XCAM:
			if (device_verification(cas, XCAM)) {
				cas->status = XCAM;
				result = cas_set_xcam_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_XCAM ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_XCAM ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_JOKER:
			if (device_verification(cas, JOKER)) {
				cas->status = JOKER;
				result = cas_set_joker_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_JOKER ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_JOKER ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_SET_HOST:
			if (device_verification(cas, HOST)) {
				cas->status = HOST;
				result = cas_set_host_fw(cas);
				if (result < 0)
					dev_err(&cas->uinterface->dev, "Error executing IOCTL_SET_HOST ioctrl, result = %d", le32_to_cpu(result));
				else
					dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SET_HOST ioctl, result = %d", le32_to_cpu(result));
			}
			break;
		case IOCTL_RECV_VENDOR_COMMAND:
			data = (void *) arg;
			if (data == NULL)
				break;
			if (copy_from_user(&cas_vendor_cmd, data, sizeof(struct cas_vendor_command))) {
				result = -EFAULT;
				goto err_out;
			}
			if (cas_vendor_cmd.length < 0 || cas_vendor_cmd.length > PAGE_SIZE) {
				result = -EINVAL;
				goto err_out;
			}
			buffer = (unsigned char *) __get_free_page(GFP_KERNEL);
			if (!buffer) {
				result = -ENOMEM;
				goto err_out;
			}
			if (copy_from_user(buffer, cas_vendor_cmd.buffer, cas_vendor_cmd.length)) {
				result = -EFAULT;
				free_page((unsigned long) buffer);
				goto err_out;
			}
			result = vendor_command_rcv(cas, cas_vendor_cmd.request, cas_vendor_cmd.address, cas_vendor_cmd.index, buffer, cas_vendor_cmd.length);
			if (result < 0)
				dev_err(&cas->uinterface->dev, "Error executing IOCTL_RECV_VENDOR_COMMAND ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&cas->uinterface->dev, "Executed IOCTL_RECV_VENDOR_COMMAND ioctl, result = %d", le32_to_cpu(result));
			if (copy_to_user(cas_vendor_cmd.buffer, buffer, cas_vendor_cmd.length)) {
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
			if (copy_from_user(&cas_vendor_cmd, data, sizeof(struct cas_vendor_command))) {
				result = -EFAULT;
				goto err_out;
			}
			if (cas_vendor_cmd.length < 0 || cas_vendor_cmd.length > PAGE_SIZE) {
				result = -EINVAL;
				goto err_out;
			}
			buffer = (unsigned char *) __get_free_page(GFP_KERNEL);
			if (buffer == NULL) {
				result = -ENOMEM;
				goto err_out;
			}
			if (copy_from_user(buffer, cas_vendor_cmd.buffer, cas_vendor_cmd.length)) {
				free_page((unsigned long)buffer);
				result = -EFAULT;
				goto err_out;
			}
			result = vendor_command_snd(cas, cas_vendor_cmd.request, cas_vendor_cmd.address, cas_vendor_cmd.index, buffer, cas_vendor_cmd.length);
			if (result < 0)
				dev_err(&cas->uinterface->dev, "Error executing IOCTL_SEND_VENDOR_COMMAND ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SEND_VENDOR_COMMAND ioctl, result = %d", le32_to_cpu(result));
			free_page((unsigned long) buffer);
			break;
		case IOCTL_RECV_BULK_COMMAND:
			data = (void *) arg;
			if (data == NULL)
				break;
			if (copy_from_user(&cas_bulk_cmd, data, sizeof(struct cas_bulk_command))) {
				result = -EFAULT;
				goto err_out;
			}
			if (cas_bulk_cmd.length < 0 || cas_bulk_cmd.length > PAGE_SIZE) {
				result = -EINVAL;
				goto err_out;
			}
			buffer = (unsigned char *) __get_free_page(GFP_KERNEL);
			if (!buffer) {
				result = -ENOMEM;
				goto err_out;
			}
			if (copy_from_user(buffer, cas_bulk_cmd.buffer, cas_bulk_cmd.length)) {
				result = -EFAULT;
				free_page((unsigned long) buffer);
				goto err_out;
			}
			result = bulk_command_rcv(cas, buffer, cas_bulk_cmd.length, 0);
			if (result < 0)
				dev_err(&cas->uinterface->dev, "Error executing IOCTL_RECV_BULK_COMMAND ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&cas->uinterface->dev, "Executed IOCTL_RECV_BULK_COMMAND ioctl, result = %d", le32_to_cpu(result));
			if (copy_to_user(cas_bulk_cmd.buffer, buffer, cas_bulk_cmd.length)) {
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
			if (copy_from_user(&cas_bulk_cmd, data, sizeof(struct cas_bulk_command))) {
				result = -EFAULT;
				goto err_out;
			}
			if (cas_bulk_cmd.length < 0 || cas_bulk_cmd.length > PAGE_SIZE) {
				result = -EINVAL;
				goto err_out;
			}
			buffer = (unsigned char *) __get_free_page(GFP_KERNEL);
			if (buffer == NULL) {
				result = -ENOMEM;
				goto err_out;
			}
			if (copy_from_user(buffer, cas_bulk_cmd.buffer, cas_bulk_cmd.length)) {
				free_page((unsigned long)buffer);
				result = -EFAULT;
				goto err_out;
			}
			result = bulk_command_snd(cas, buffer, cas_bulk_cmd.length, 0);
			if (result < 0)
				dev_err(&cas->uinterface->dev, "Error executing IOCTL_SEND_BULK_COMMAND ioctrl, result = %d", le32_to_cpu(result));
			else
				dev_dbg(&cas->uinterface->dev, "Executed IOCTL_SEND_BULK_COMMAND ioctl, result = %d", le32_to_cpu(result));
			free_page((unsigned long) buffer);
			break;
		case IOCTL_DEVICE_INFORMATION_COMMAND:
			cas_info_cmd.device = cas->device_running;
			cas_info_cmd.status = cas->status;
			cas_info_cmd.vid = cas->udevice->descriptor.idVendor;
			cas_info_cmd.pid = cas->udevice->descriptor.idProduct;

			if (copy_to_user((void *)arg, &cas_info_cmd, sizeof(cas_info_cmd))) {
				result = -EFAULT;
				goto err_out;
			}
			break;
		default:
			dev_info(&cas->uinterface->dev, "Unknown ioctl command 0x%x\n", cmd);
			result = -ENOTTY;
			break;
	}
	return 0;
err_out:
	return result;
}

static ssize_t cas_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	int result;
	struct usb_cas *cas = (struct usb_cas *)file->private_data;
	struct usb_interface *interface = usb_find_interface(&cas_driver, 0);

	result = bulk_command_rcv(cas, cas->bulk_in_buffer, min(cas->bulk_in_size, count), count);

	if (!result) {
		if (copy_to_user(buffer, cas->bulk_in_buffer, count))
			result = -EFAULT;
		else
			result = count;
	}

	return result;
}

static void cas_write_bulk_callback(struct urb *urb)
{
	struct usb_cas *cas = urb->context;

	/* sync/async unlink faults aren't errors */
	if (urb->status && !(urb->status == -ENOENT || urb->status == -ECONNRESET || urb->status == -ESHUTDOWN)) {
		dev_dbg(&cas->uinterface->dev, "nonzero write bulk status received: %d", urb->status);
	}

	/* free up our allocated buffer */
	usb_free_coherent(urb->dev, urb->transfer_buffer_length, urb->transfer_buffer, urb->transfer_dma);
}

static ssize_t cas_write(struct file *file, const char __user *user_buffer, size_t count, loff_t *ppos)
{
	struct usb_cas *cas;
	int result;
	struct urb *urb = NULL;
	char *buf = NULL;

	cas = (struct usb_cas *)file->private_data;

	/* verify that we actually have some data to write */
	if (count == 0)
		goto exit;

	/* create a urb, and a buffer for it, and copy the data to the urb */
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		result = -ENOMEM;
		goto error;
	}

	buf = usb_alloc_coherent(cas->udevice, count, GFP_KERNEL, &urb->transfer_dma);
	if (!buf) {
		result = -ENOMEM;
		goto error;
	}
	if (copy_from_user(buf, user_buffer, count)) {
		result = -EFAULT;
		goto error;
	}

	/* initialize the urb properly */
	usb_fill_bulk_urb(urb, cas->udevice, usb_sndbulkpipe(cas->udevice, cas->bulk_out_endpointAddr), buf, count, cas_write_bulk_callback, cas);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* send the data out the bulk port */
	result = usb_submit_urb(urb, GFP_KERNEL);
	if (result) {
		dev_err(&cas->uinterface->dev, "failed submitting write urb, error %d", result);
		goto error;
	}

	if (debug)
		dump_buffer(cas, buf, "data_out", MAX_PKT_SIZE);

	/* release our reference to this urb, the USB core will eventually free it entirely */
	usb_free_urb(urb);

exit:
	return count;

error:
	usb_free_coherent(cas->udevice, count, buf, urb->transfer_dma);
	usb_free_urb(urb);
	kfree(buf);

	return result;
}

static int cas_open(struct inode *inode, struct file *file)
{
	int result;
	struct usb_interface *interface = usb_find_interface(&cas_driver, 0);
	struct usb_cas *cas = usb_get_intfdata(interface);

	kref_get(&cas->kref);

	file->private_data = cas;

	dev_dbg(&cas->uinterface->dev, "%s Reader/Programmer device opened\n", cas->device_name);

	return result;
}

static void cas_delete(struct kref *kref)
{
	struct usb_cas *cas = to_cas_dev(kref);

	usb_put_dev(cas->udevice);
	if (cas->bulk_in_buffer)
		kfree (cas->bulk_in_buffer);
	if (cas)
		kfree (cas);
}

static int cas_release(struct inode *inode, struct file *file)
{
	int result;

	struct usb_cas *cas;

	cas = (struct usb_cas *)file->private_data;
	if (cas == NULL)
		return -ENODEV;

	/* decrement the count on our device */
	kref_put(&cas->kref, cas_delete);

	dev_dbg(&cas->uinterface->dev, "%s Reader/Programmer device closed\n", cas->device_name);

	return result;
}

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(CAS2_PLUS2_CRYPTO_VENDOR_ID, CAS2_PLUS2_CRYPTO_PRODUCT_ID) },
	{},
};
MODULE_DEVICE_TABLE(usb, id_table);

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver cas_driver = {
	.name		= "cas",
	.probe		= cas_probe,
	.disconnect	= cas_disconnect,
	.id_table	= id_table,
};

static const struct file_operations cas_fops = {
	.unlocked_ioctl	= cas_ioctl,
	.read		= cas_read,
	.write		= cas_write,
	.open		= cas_open,
	.release	= cas_release,
};

static struct usb_class_driver cas_class = {
	.name =		"cas_programmer",
	.fops =		&cas_fops,
};

static int cas_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	struct usb_cas *cas;
	int i, result = -ENOMEM;

	int init;

	cas = kzalloc(sizeof(struct usb_cas), GFP_KERNEL);
	if (!cas)
		goto error_mem;

	kref_init(&cas->kref);

	cas->udevice = usb_get_dev(interface_to_usbdev(interface));
	cas->uinterface = interface;

	if ((cas->udevice->descriptor.idVendor == CAS2_PLUS2_CRYPTO_VENDOR_ID) && (cas->udevice->descriptor.idProduct == CAS2_PLUS2_CRYPTO_PRODUCT_ID)) {
		cas->device_name = CAS2_PLUS2_CRYPTO;
		cas->device_running = CAS2_PLUS2_CRYPTO_DEVICE;
	} else {
		cas->device_running = NONE_DEVICE;
	}

	/* set up the endpoint information */
	/* use only the first bulk-in and bulk-out endpoints */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!cas->bulk_in_endpointAddr &&
		    (endpoint->bEndpointAddress & USB_DIR_IN) &&
		    ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
					== USB_ENDPOINT_XFER_BULK)) {
			/* we found a bulk in endpoint */
			buffer_size = endpoint->wMaxPacketSize;
			cas->bulk_in_size = buffer_size;
			cas->bulk_in_endpointAddr = endpoint->bEndpointAddress;
			cas->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
			if (!cas->bulk_in_buffer) {
				dev_err(&interface->dev, "Could not allocate bulk_in_buffer\n");
				goto error;
			}
		}

		if (!cas->bulk_out_endpointAddr &&
		    !(endpoint->bEndpointAddress & USB_DIR_IN) &&
		    ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
					== USB_ENDPOINT_XFER_BULK)) {
			/* we found a bulk out endpoint */
			cas->bulk_out_endpointAddr = endpoint->bEndpointAddress;
		}
	}

	if (!(cas->bulk_in_endpointAddr && cas->bulk_out_endpointAddr) && (cas->status != NOFW)) {
		dev_err(&interface->dev, "Could not find both bulk-in and bulk-out endpoints\n");
		goto error;
	}

#if 1
	if ((cas->udevice->descriptor.iManufacturer != NULL) && (cas->udevice->descriptor.iProduct != NULL)) {
		dev_info(&cas->uinterface->dev, "%s send init command\n", cas->device_name);
		result = send_init_command(cas);
		if (result < 0)
			dev_err(&cas->uinterface->dev, "%s error send init command\n", cas->device_name);
	}
#endif
	usb_set_intfdata(interface, cas);

	mutex_init(&cas->lock);

	result = device_create_file(&interface->dev, &dev_attr_status);
	if (result < 0)
		goto error;

	/* we can register the device now, as it is ready */
	result = usb_register_dev(interface, &cas_class);
	if (result < 0) {
		dev_err(&interface->dev, "Not able to get a minor for this device\n");
		goto error;
	}

	if ((cas->udevice->descriptor.iManufacturer == NULL) && (cas->udevice->descriptor.iProduct == NULL)) {
		cas->status = NOFW;
		cas_set_init_fw(cas);
	} else {
		cas->status = READY;
		//unsigned char buf[64];
		//read_eeprom(cas, buf, 64, 0);
	}

	dev_info(&interface->dev, "%s Reader/Programmer now attached\n", cas->device_name);

	return 0;
error:
	device_remove_file(&interface->dev, &dev_attr_status);
	usb_set_intfdata (interface, NULL);

	if (cas)
		kref_put(&cas->kref, cas_delete);

	return result;

error_mem:
	return result;
}

static void cas_disconnect(struct usb_interface *interface)
{
	struct usb_cas *cas;
	cas = usb_get_intfdata(interface);

	usb_deregister_dev(interface, &cas->uclass);
	device_remove_file(&interface->dev, &dev_attr_status);

	/* first remove the files, then NULL the pointer */
	usb_set_intfdata (interface, NULL);
	mutex_destroy(&cas->lock);
	kref_put(&cas->kref, cas_delete);
	dev_info(&interface->dev, "%s Reader/Programmer now disconnected\n", cas->device_name);
}


static int __init cas_usb_init(void)
{
	int result;

	/* register this driver with the USB subsystem */
	result = usb_register(&cas_driver);

	return result;
}

static void __exit cas_usb_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&cas_driver);
}

module_init(cas_usb_init);
module_exit(cas_usb_exit);

#define DRIVER_AUTHOR "redblue"
#define DRIVER_DESC "Duolabs Cas Programmer driver"

module_param(debug, int, 0660);
module_param(load_fx1_fw, int, 0660);
module_param(load_fx2_fw, int, 0660);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
