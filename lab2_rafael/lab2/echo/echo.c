#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include "echo.h"
MODULE_LICENSE("Dual BSD/GPL");

#define N_DEVICES 4

int echo_nr_devs = N_DEVICES;
int echo_major;
int echo_minor = 0;

static dev_t dev;
struct echo_dev *echo_devices;
static char *buffer;
struct echo_dev {
    struct cdev cdev;   // struct cdev for this echo device
    int         cnt;    // number of characters written to device
};

static int echo_open(struct inode *inodep, struct file *filp);
static int echo_release(struct inode *inodep, struct file *filp);
static ssize_t echo_read(struct file *filep, char __user *buff, size_t count, loff_t *offp);
static ssize_t echo_write(struct file *filep, const char __user *buff, size_t count, loff_t *offp);

static struct file_operations fops = {
        .owner =    THIS_MODULE,
        .open =     echo_open,
        .release =  echo_release,
				.read =     echo_read,
				.write =    echo_write,
        .llseek =   no_llseek,
};

static void echo_setup_cdev(struct echo_dev *dev, int index){
  int err, devno = MKDEV(echo_major, echo_minor + index);
  cdev_init(&dev->cdev, &fops);
  dev->cdev.owner = THIS_MODULE;
  dev->cdev.ops = &fops;
  err = cdev_add (&dev->cdev, devno, 1);
  /* Fail gracefully if need be */
  if (err)
    printk(KERN_NOTICE "Error %d allocating /dev/echo%d with a %d major device number! \n", err, MINOR(devno), MAJOR(devno));
  else
    printk(KERN_NOTICE "Successfully allocated /dev/echo%d with a %d major device number!\n", MINOR(devno), MAJOR(devno));
}
static int echo_open(struct inode *inodep, struct file *filp){
  struct echo_dev *dev;
  nonseekable_open(inodep, filp);
  dev = container_of(inodep->i_cdev, struct echo_dev, cdev);
  filp->private_data = dev;

	printk(KERN_ALERT "\nopen() invoked!\n\n");
	return 0;
}
static int echo_release(struct inode *inodep, struct file *filp){
	printk(KERN_ALERT "\nrelease() invoked!\n");
	return 0;
}
static ssize_t echo_read(struct file *filep, char __user *buff, size_t count, loff_t *offp){

  int res, bytes_r;

  res = copy_to_user(buff, buffer, count);
  if (res < 0){
    printk(KERN_WARNING "Error reading from user-space!\n");
  }

  bytes_r = count - res;

  printk(KERN_ALERT "Read %d bytes from the string '%s'\n", bytes_r, buff);
  return bytes_r;
}
static ssize_t echo_write(struct file *filep, const char __user *buff, size_t count, loff_t *offp){

  int res, bytes_w;
  buffer = kmalloc(count * sizeof(char), GFP_KERNEL);
  if(buffer == NULL){
    printk(KERN_WARNING "Error allocating space!\n");
  }

  res = copy_from_user(buffer, buff, count);
  if (res < 0){
    printk(KERN_WARNING "Error reading from user-space!\n");
  }
  bytes_w = count - res;

  printk(KERN_ALERT "Wrote %d bytes from the string '%s'\n", bytes_w, buffer);
  return bytes_w;
}

static int echo_init(void){
  int ret_alloc, i;
  echo_devices = kmalloc(echo_nr_devs * sizeof (struct echo_dev), GFP_KERNEL);

  if (echo_major) {
   dev = MKDEV(echo_major, 0);
   ret_alloc = register_chrdev_region(dev, echo_nr_devs, "echo");
  }
  else {
   ret_alloc = alloc_chrdev_region(&dev, 0, echo_nr_devs, "echo");
   echo_major = MAJOR(dev);
  }
  if (ret_alloc < 0) {
   printk(KERN_WARNING "Error allocating major %d!\n", echo_major);
   return ret_alloc;
  }

  printk("\n");
  for (i = 0; i < echo_nr_devs; i++){
    echo_setup_cdev(echo_devices + i, i);
  }
  printk("\n");

	return 0;
}
static void echo_cleanup(void){
  int i;
  for (i = 0; i < echo_nr_devs; i++){
    cdev_del(&echo_devices[i].cdev);
  }
  kfree(echo_devices);
  kfree(buffer);
  unregister_chrdev_region(MKDEV(echo_major, 0), echo_nr_devs);

	printk(KERN_ALERT "\nSuccessfully removed major device %d with %d devices in /dev/echo[0-%d]\n\n", MAJOR(dev), echo_nr_devs, echo_nr_devs-1);
}

module_init(echo_init);
module_exit(echo_cleanup);
