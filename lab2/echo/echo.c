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
int bc=0;
char buf[BS];

int dev_open(struct inode *inode, struct file *filp ){
	bc	= 0;

	filp->private_data = &devno;
	printk(KERN_NOTICE "Open finished\n");
	return nonseekable_open( inode, filp);
}

int dev_release(struct inode *inode, struct file *filp ){
	bc = 0;

	printk(KERN_NOTICE "Released finished\n");
	return 0;
}

ssize_t dev_write(struct file *filep, const char __user *buff, size_t count, loff_t *offp){
	int c=0;
	if(count >BS)
		return -1;
	if(!copy_from_user((void*) buf, (char *) buff, count)){
		c=count;
	}
	*offp+=count;
	bc += count;

	printk(KERN_NOTICE "Writing finished %d: %s\n",c,  buff);
	return c;
}

ssize_t dev_read(struct file *filep, char __user *buff, size_t count, loff_t *offp){
	int c=count, error_count=0;
	printk(KERN_NOTICE "Reading starting %d\n", bc);
	if(count > bc)
		c= bc;
	error_count = copy_to_user((void *) buff,(char *) buf, c);

	*offp+=c;
	bc -= c;
	printk(KERN_NOTICE "Reading finished %d: %s\n",c,  buf);

	return error_count;	
}

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
