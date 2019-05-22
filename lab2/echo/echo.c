/*                                                     
 * $Id: echo.c,v 1.5 2004/10/26 03:32:21 corbet Exp $ 
 */                                                    
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>

#define BUFFERSIZE 20

struct my_device{
	struct cdev *chard;
	int count;
	dev_t dev;
	int major;
	int minor;
};

struct my_device device;

int res = 0;

static struct file_operations fops = {
	.owner		= THIS_MODULE,
	.llseek 	= no_llseek,
	.open 		= dev_open,
	.release	= dev_release,
	.write		= dev_write,
	.read 		= dev_read,
};

static int echo_init(void)
{
	printk(KERN_ALERT "Hello echo\n");
	res = alloc_chrdev_region(&device.dev, 0, 1, "echo");

	if (res < 0){
		return res;
	}

	device.major = MAJOR(device.dev);
	device.minor = MINOR(device.dev);

	device.chard = cdev_alloc();
	device.chard->ops = &fops;
	device.chard->owner=THIS_MODULE;

	if(cdev_add(chard, device.dev, 1) <0){
		printk(KERN_ERR "Error in cdev-add\n");
		return res;
	}

	printk(KERN_NOTICE "Echo major ---> %d\n", device.major);
	return res;
}

static void echo_exit(void)
{
	printk(KERN_ALERT "Exiting Echo and freeing major: %d\n", device.major);
	unregister_chrdev_region(device.dev, 1);
	cdev_del(device.chard);
}

module_init(echo_init);
module_exit(echo_exit);
