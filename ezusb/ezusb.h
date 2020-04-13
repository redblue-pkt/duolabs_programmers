/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __EZUSB_H
#define __EZUSB_H

/* Commands for writing to memory */
#define WRITE_INT_RAM 0xA0
#define WRITE_EXT_RAM 0xA3

struct ezusb_fx_type {
	/* EZ-USB Control and Status Register.  Bit 0 controls 8051 reset */
	unsigned short cpucs_reg;
	unsigned short max_internal_adress;
};

extern int ezusb_fx1_set_reset(struct usb_device *dev, unsigned char reset_bit);
extern int ezusb_fx1_ihex_firmware_download(struct usb_device *dev,
					    const char *firmware_path);
extern int ezusb_fx1_writememory(struct usb_device *dev, int address,
					    unsigned char *data, int length, __u8 request);

extern int ezusb_fx2_set_reset(struct usb_device *dev, unsigned char reset_bit);
extern int ezusb_fx2_ihex_firmware_download(struct usb_device *dev,
					    const char *firmware_path);
extern int ezusb_fx2_writememory(struct usb_device *dev, int address,
					    unsigned char *data, int length, __u8 request);

#endif /* __EZUSB_H */
