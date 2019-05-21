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
};

dev_t dev;
int major = 0, counter = 0, minor = 0, res = 0;


static int echo_init(void)
{
	printk(KERN_ALERT "Hello echo\n");
	res = alloc_chrdev_region(*dev, 0, 1, "echo");

	if (res < 0){
		return res;
	}

	major = MAJOR(dev);
	minor = MINOR(dev);

	chard = cdev_alloc();
	chard->ops = &fops;
	chard->owner=THIS_MODULE;

	if(cdev_add(chard, devno, 1) <0){
		printk(KERN_ERR "Error in cdev-add\n");
		return res;
	}

	printk(KERN_NOTICE "Echo major ---> %d\n", major);
	return res;
}

static void echo_exit(void)
{
	printk(KERN_ALERT "Exiting Echo and freeing major: %d\n", major);
	unregister_chrdev_region(dev, 1);
	cdev_del(chard);
}

module_init(echo_init);
module_exit(echo_exit);
