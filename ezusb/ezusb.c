// SPDX-License-Identifier: GPL-2.0
/*
 * EZ-USB specific functions used by some of the USB to Serial drivers.
 *
 * Copyright (C) 1999 - 2002 Greg Kroah-Hartman (greg@kroah.com)
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/firmware.h>
#include <linux/ihex.h>
#include "ezusb.h"

static const struct ezusb_fx_type ezusb_fx1 = {
	.cpucs_reg = 0x7F92,
	.max_internal_adress = 0x1B3F,
};

static int ezusb_writememory(struct usb_device *dev, int address,
				unsigned char *data, int length, __u8 request)
{
	int result;
	unsigned char *transfer_buffer;

	if (!dev)
		return -ENODEV;

	transfer_buffer = kmemdup(data, length, GFP_KERNEL);
	if (!transfer_buffer) {
		dev_err(&dev->dev, "%s - kmalloc(%d) failed.\n",
							__func__, length);
		return -ENOMEM;
	}
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), request,
				 USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				 address, 0, transfer_buffer, length, 3000);

	kfree(transfer_buffer);
	return result;
}

static int ezusb_set_reset(struct usb_device *dev, unsigned short cpucs_reg,
			   unsigned char reset_bit)
{
	int response = ezusb_writememory(dev, cpucs_reg, &reset_bit, 1, WRITE_INT_RAM);
	if (response < 0)
		dev_err(&dev->dev, "%s-%d failed: %d\n",
						__func__, reset_bit, response);
	return response;
}

int ezusb_fx1_set_reset(struct usb_device *dev, unsigned char reset_bit)
{
	return ezusb_set_reset(dev, ezusb_fx1.cpucs_reg, reset_bit);
}
EXPORT_SYMBOL_GPL(ezusb_fx1_set_reset);

static int ezusb_ihex_firmware_download(struct usb_device *dev,
					struct ezusb_fx_type fx,
					const char *firmware_path)
{
	int ret = -ENOENT;
	const struct firmware *firmware = NULL;
	const struct ihex_binrec *record;

	if (request_ihex_firmware(&firmware, firmware_path,
				  &dev->dev)) {
		dev_err(&dev->dev,
			"%s - request \"%s\" failed\n",
			__func__, firmware_path);
		goto out;
	}

	ret = ezusb_set_reset(dev, fx.cpucs_reg, 0);
	if (ret < 0)
		goto out;

	record = (const struct ihex_binrec *)firmware->data;
	for (; record; record = ihex_next_binrec(record)) {
		if (be32_to_cpu(record->addr) > fx.max_internal_adress) {
			ret = ezusb_writememory(dev, be32_to_cpu(record->addr),
						(unsigned char *)record->data,
						be16_to_cpu(record->len), WRITE_EXT_RAM);
			if (ret < 0) {
				dev_err(&dev->dev, "%s - ezusb_writememory "
					"failed writing internal memory "
					"(%d %04X %p %d)\n", __func__, ret,
					be32_to_cpu(record->addr), record->data,
					be16_to_cpu(record->len));
				goto out;
			}
		}
	}

	ret = ezusb_set_reset(dev, fx.cpucs_reg, 1);
	if (ret < 0)
		goto out;
	record = (const struct ihex_binrec *)firmware->data;
	for (; record; record = ihex_next_binrec(record)) {
		if (be32_to_cpu(record->addr) <= fx.max_internal_adress) {
			ret = ezusb_writememory(dev, be32_to_cpu(record->addr),
						(unsigned char *)record->data,
						be16_to_cpu(record->len), WRITE_INT_RAM);
			if (ret < 0) {
				dev_err(&dev->dev, "%s - ezusb_writememory "
					"failed writing external memory "
					"(%d %04X %p %d)\n", __func__, ret,
					be32_to_cpu(record->addr), record->data,
					be16_to_cpu(record->len));
				goto out;
			}
		}
	}
	ret = ezusb_set_reset(dev, fx.cpucs_reg, 0);
out:
	release_firmware(firmware);
	return ret;
}

int ezusb_fx1_ihex_firmware_download(struct usb_device *dev,
				     const char *firmware_path)
{
	return ezusb_ihex_firmware_download(dev, ezusb_fx1, firmware_path);
}
EXPORT_SYMBOL_GPL(ezusb_fx1_ihex_firmware_download);

int ezusb_fx1_writememory(struct usb_device *dev, int address,
				unsigned char *data, int length, __u8 request)
{
	return ezusb_writememory(dev, address, data, length, request);
}
EXPORT_SYMBOL_GPL(ezusb_fx1_writememory);

static struct ezusb_fx_type ezusb_fx2 = {
	.cpucs_reg = 0xE600,
	.max_internal_adress = 0x3FFF,
};

int ezusb_fx2_set_reset(struct usb_device *dev, unsigned char reset_bit)
{
	return ezusb_set_reset(dev, ezusb_fx2.cpucs_reg, reset_bit);
}
EXPORT_SYMBOL_GPL(ezusb_fx2_set_reset);

int ezusb_fx2_ihex_firmware_download(struct usb_device *dev,
				     const char *firmware_path)
{
	return ezusb_ihex_firmware_download(dev, ezusb_fx2, firmware_path);
}
EXPORT_SYMBOL_GPL(ezusb_fx2_ihex_firmware_download);

int ezusb_fx2_writememory(struct usb_device *dev, int address,
				unsigned char *data, int length, __u8 request)
{
	return ezusb_writememory(dev, address, data, length, request);
}
EXPORT_SYMBOL_GPL(ezusb_fx2_writememory);

MODULE_LICENSE("GPL");
