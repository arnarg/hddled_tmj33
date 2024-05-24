/*
 * Kernel module to control HDD LEDs on Terramaster NAS that run on Intel Celeron J33xx
 *
 * I got the source code for module named led_drv_TMJ33 that was running on my F2-221's
 * TOS 4.1 by emailing their support email. That module gives the ability to control the
 * two HDD LEDs on that NAS (up to 5 on F5-221).
 * I didn't like TOS and wanted to run any Linux distro on the NAS which is why I asked
 * for the source code.
 *
 * The led_drv_TMJ33 module (the one provided by Terramaster support) creates char devices
 * /dev/leddrv[1-10] that you can pipe into "led[1-10]on" and "led[1-10]off". However
 * they don't do any tracking on which char device the user is writing into, meaning
 * `echo led2on > /dev/leddrv5` will actually turn on led2, so will `echo led2on > /dev/leddrv3`.
 * I didn't like that interface so I wrote my own with the original one as reference on how
 * to interface with the hardware.
 *
 * It finds the base address of the device that has the relevant GPIO pins that control
 * the LEDs. This is done in function read_base. That address is stored in `base` in the
 * following explanation.
 *
 * The module ioremaps address with hardcoded offset from base as follows.
 *
 * led1   =  (volatile unsigned int *)ioremap(base+0xC505B8, 1);    //GPIO23
 * led3   =  (volatile unsigned int *)ioremap(base+0xC505C0, 1);    //GPIO24
 * led5   =  (volatile unsigned int *)ioremap(base+0xC505C8, 1);    //GPIO25
 * led7   =  (volatile unsigned int *)ioremap(base+0xC505D0, 1);    //GPIO26
 * led9   =  (volatile unsigned int *)ioremap(base+0xC505D8, 1);    //GPIO27
 * led2   =  (volatile unsigned int *)ioremap(base+0xC505E0, 1);    //GPIO28
 * led4   =  (volatile unsigned int *)ioremap(base+0xC505E8, 1);    //GPIO29
 * led6   =  (volatile unsigned int *)ioremap(base+0xC505F0, 1);    //GPIO30
 * led8   =  (volatile unsigned int *)ioremap(base+0xC505F8, 1);    //GPIO31
 * led10  =  (volatile unsigned int *)ioremap(base+0xC50600, 1);    //GPIO32
 *
 * Green color of physical HDD LED[1-5] is connected to the first 5 in the list above
 * (odd numbers in 1-9).
 * Red color of physical HDD LED[1-5] is connected to the last 5 in the list above (even
 * numbers in 2-10).
 *
 * So the green color of the physical HDD LEDs are offset of base in 0x8 intervals (starting
 * at base+0xC505B8) and the red color are offset +0x28 from the green color of the same
 * physical LED.
 *
 * This module creates char devices /dev/hddled[1-5]. Each device can only control a single
 * LED. You can write [0-3] into them to control the LED in question.
 *
 * 0 - OFF
 * 1 - GREEN
 * 2 - RED
 * 3 - BOTH (orange)
 *
 * `echo 1 > /dev/hddled1`
 */

#include <linux/init.h>           // Macros used to mark up functions e.g. __init __exit
#include <linux/module.h>         // Core header for loading LKMs into the kernel
#include <linux/device.h>         // Header to support the kernel Driver Model
#include <linux/kernel.h>         // Contains types, macros, functions for the kernel
#include <linux/fs.h>             // Header for the Linux file system support
#include <linux/uaccess.h>        // Required for the copy to user function
#include <linux/slab.h>           // For kmalloc/kfree
#include <linux/io.h>             // Added because the module would not compile under Kernel 5.6 without it

#ifndef HDDLED_TMJ33_VERSION
#define HDDLED_TMJ33_VERSION "0.3"
#endif

#define DEVICE_NAME "hddled"
#define CLASS_NAME  "hddled"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arnar Gauti Ingason");
MODULE_DESCRIPTION("A char driver for controlling HDD LEDs on Terramaster devices based on J33xx");
MODULE_VERSION(HDDLED_TMJ33_VERSION);

struct hddled {
	volatile unsigned int *green;
	volatile unsigned int *red;
};

struct private_data {
	bool read_done;
};

static int    majorNumber;
static struct class  *hddledClass = NULL;
static struct device *hddledDevices[5] = { NULL };
static struct hddled *hddleds[5] = { NULL };


static int     dev_open(struct inode*, struct file*);
static int     dev_release(struct inode*, struct file*);
static ssize_t dev_read(struct file*, char*, size_t, loff_t*);
static ssize_t dev_write(struct file*, const char*, size_t, loff_t*);

static struct hddled* create_hddled(unsigned int);

static struct file_operations fops = {
	.owner   = THIS_MODULE,
	.open    = dev_open,
	.read    = dev_read,
	.write   = dev_write,
	.release = dev_release
};

// Copied from Terramaster module
#define PCI_CFG_DATA    0xcfc
#define PCI_CFG_CTRL    0xcf8
static unsigned  int read_base(unsigned char offset) {
        unsigned char fun = 0;
        unsigned char bus = 0;
        unsigned char dev = 0x0d;
        outl((0x80000000 | ((bus)<<16) |((dev)<<11) | ((fun)<<8) | (offset)), PCI_CFG_CTRL);
        unsigned  int  val = inl(PCI_CFG_DATA);
        if(val == 0xffffffff)
                return 0xD0000000;
        return val&0xfffff000;
}

static int __init hddled_init(void) {
	int i, offset;
	unsigned int base = read_base(0x10);

	majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
	if (majorNumber < 0) {
		printk(KERN_ALERT "HDDLed failed to register a major number\n");
		return majorNumber;
	}
	printk(KERN_INFO "HDDLed: registered correctly with major number %d\n", majorNumber);

	hddledClass = class_create(CLASS_NAME);
	if (IS_ERR(hddledClass)) {
		unregister_chrdev(majorNumber, "hddled");
		printk(KERN_ALERT "Failed to register device class\n");
		return PTR_ERR(hddledClass);
	}
	printk(KERN_INFO "HDDLed: device class registered correctly\n");

	// Create char devices
	for (i = 0; i < sizeof(hddledDevices)/sizeof(struct device*); ++i) {
		hddledDevices[i] = device_create(hddledClass, NULL, MKDEV(majorNumber, i), NULL, "%s%d", DEVICE_NAME, i+1);
	}

	// Create hddled iomaps
	for (i = 0, offset = 0xC505B8; i < sizeof(hddleds)/sizeof(struct hddled*); ++i, offset += 0x8) {
		hddleds[i] = create_hddled(base+offset);
		// Turn off LEDs
		*hddleds[i]->green |= 0x1;
		*hddleds[i]->red &= 0xfffffffe;
	}

	printk(KERN_INFO "HDDLed: initialized\n");

	return 0;
}

static void __exit hddled_exit(void) {
	int minor;
	for (minor = 0; minor < sizeof(hddledDevices)/sizeof(struct device*); ++minor) {
		// Destroy char devices
		device_destroy(hddledClass, MKDEV(majorNumber, minor));
		// iounmap red and green led address in each hddled
		iounmap(hddleds[minor]->green);
		iounmap(hddleds[minor]->red);
		// free hddled structrs
		kfree(hddleds[minor]);
		hddleds[minor] = NULL;
	}
	class_unregister(hddledClass);
	class_destroy(hddledClass);
	unregister_chrdev(majorNumber, "hddled");
	printk(KERN_INFO "HDDLed: exited\n");
}

static int dev_open(struct inode *inodep, struct file *filep) {
	// Allocate private_data struct to keep track of if the read function is done reading
	struct private_data *pd = kmalloc(sizeof(struct private_data), GFP_KERNEL);
	pd->read_done = false;
	filep->private_data = (void*)pd;
	return 0;
}

static int dev_release(struct inode *inodep, struct file *filep) {
	// Free private_data struct
	kfree(filep->private_data);
	filep->private_data = NULL;
	return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
	int err = 0, ret = 0;
	size_t ret_len = 0;
	char out[4] = { 0 };
	int minor = iminor(filep->f_inode);
	struct hddled* led = hddleds[minor];
	struct private_data* pd = (struct private_data*)filep->private_data;

	// If we already returned the value to the user he should close the file
	if (pd->read_done) return 0;

	// Calculate current state
	ret = ((*led->green & 0x1) ^ 0x1) | ((*led->red & 0x1) << 1);
	sprintf(out, "%d", ret);
	ret_len = strlen(out);

	err = copy_to_user(buffer, out, ret_len);
	if (err == 0) {
		pd->read_done = true;
		return ret_len;
	} else {
		printk(KERN_ALERT "HDDLed: failed to send %d characters to the user\n", err);
		return err;
	}
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
	int err, val;
	int minor = iminor(filep->f_inode);
	struct hddled* led = hddleds[minor];

	err = kstrtoint_from_user(buffer, len, 10, &val);
	if (err < 0) {
		printk(KERN_ALERT "HDDLed: failed to read %d characters from the user\n", err);
		return err;
	}

	// Green LED
	if (val & 0x1) {
		// Turning on
		*led->green &= 0xfffffffe;
	} else {
		// Turning off
		*led->green |= 0x1;
	}

	// Red LED
	if (((val >> 1) & 0x1)) {
		// Turning on
		*led->red |= 0x1;
	} else {
		// Turning off
		*led->red &= 0xfffffffe;
	}

	return len;
}

static struct hddled* create_hddled(unsigned int addr) {
	struct hddled *led = kmalloc(sizeof(struct hddled), GFP_KERNEL);
	led->green = (volatile unsigned int *)ioremap(addr, 1);
	led->red = (volatile unsigned int *)ioremap(addr+0x28, 1);
	return led;
}

module_init(hddled_init);
module_exit(hddled_exit);
