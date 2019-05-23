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

#define BS 20

MODULE_LICENSE("Dual BSD/GPL");

/*struct my_device{
	struct cdev *chard;
	int count;
};*/

int major= 0;
dev_t devno;
struct cdev *chard;
struct my_device *mydev;
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
	int res=0;

	printk(KERN_ALERT "Echo, world\n");
	devno = MKDEV(major, 0);
	 

	if (major)
		res = register_chrdev_region(devno, 1, "echo");
	else {
		res = alloc_chrdev_region(&devno, 0, 1, "echo");
		major = MAJOR(devno);
	}
	if (res < 0)
		return res;

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
	printk(KERN_ALERT "Goodbye, cruel world\nFreeing major: %d\n", major);
	unregister_chrdev_region( devno , 1);
	cdev_del(chard);
}

module_init(echo_init);
module_exit(echo_exit);
