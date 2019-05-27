#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include "serial_reg.h"
#include  <linux/delay.h>
#include <linux/timer.h>
#include <linux/sched.h>

#define BS 20
#define BASE_PORT 0x3f8
#define LAST_PORT 0x3ff

MODULE_LICENSE("Dual BSD/GPL");

struct my_device{
	dev_t devno;
	struct cdev *chard;
	char *devname;
	int count;
	int timer_state;
	char *rbuf;

	int rcount;
	int filercount;
	int filewcount;
};

struct my_device *mydev;

struct timer_list read_timer;

void config_uart_dev(void);
void timer_end(unsigned long data);

// File Operations funtions 

// Non_seekable Open / Release
int dev_open(struct inode *inode, struct file *filp ){
	struct my_device *dev;
	//finds the address of the struct my_device which has in the member chard the address of inode->i_cdev
	dev = container_of(&inode->i_cdev, struct my_device, chard);
	
	filp->private_data = dev;
	dev->filewcount=0;
	dev->filercount=0;
	printk(KERN_NOTICE "Open finished\n");

	return nonseekable_open( inode, filp);
}

int dev_release(struct inode *inode, struct file *filp ){
	//delete timer
	del_timer(&read_timer);
	printk(KERN_NOTICE "Released finished\nChars read=%d Chars written=%d\n", mydev->filercount, mydev->filewcount);
	return 0;
}

// Write / Read
ssize_t dev_write(struct file *filep, const char __user *buff, size_t count, loff_t *offp){

	int c=0, i=0;
	char *buf;
	buf = kzalloc(sizeof(char)*count+1, GFP_KERNEL);

	c = copy_from_user((void*) buf, (char *) buff, count);
	
	//if copy_from_user goes well
	if(c<0 || c >count){
		printk(KERN_ALERT "Copy from user error!\n");
		c=0;
	}
	else if(c == 0){
		c=count;
		//sends byte by byte to the UART_TX
		for(i=0; i<c ; i++){
			//check if can send another byte
			if( ( inb(BASE_PORT + UART_LSR) & UART_LSR_THRE )!= 0)
				//if transmitter is not empty
				outb(buf[i], BASE_PORT+ UART_TX);
			else
				schedule();
		}
	}
	else{
		printk(KERN_NOTICE "Partial Copy from user - %d\n", c);
	}

	*offp+=c;
	mydev->filewcount +=c;
	printk(KERN_NOTICE "Writing finished %d: %s\n",c,  buff);
	kfree(buf);
	return c;
}

ssize_t dev_read(struct file *filep, char __user *buff, size_t count, loff_t *offp){
	
	int c=count, error_count=0, i=0, beg_idle =jiffies;
	char rec=0;
	
	
	//max time to wait for input data
	mydev->timer_state = 0;
	mydev->rbuf = kzalloc(sizeof(char)*count+1, GFP_KERNEL);

	i=0;
	//read cycle
	do{
		//if Data Ready in LSR register
		if((inb(BASE_PORT+UART_LSR) & UART_LSR_DR ) == 0)
		{
			//if it's not supposed to block
			if (filep->f_flags & O_NONBLOCK)
            {
            	kfree(mydev->rbuf);
				return EAGAIN;
			}
		}
		else if( (inb(BASE_PORT+UART_LSR)) & (UART_LSR_PE | UART_LSR_OE | UART_LSR_FE) ) 
		{
			//Some unknown error has occured
			printk(KERN_ALERT "Error in read\n");
			return EIO;
		}
		else{
			//read char
			printk(KERN_NOTICE "Data ready\n");
			mydev->rbuf[i]= inb(BASE_PORT+UART_RX);
			rec = mydev->rbuf[i];
			i++;
			//reset timmer
			mod_timer(&read_timer, jiffies + msecs_to_jiffies(3000));
			printk(KERN_NOTICE "former time %d, actual time %ld\n",beg_idle,  (long int)jiffies);
			beg_idle = jiffies;
			//schedule to give time to the program to run anything it might need
			set_current_state(TASK_INTERRUPTIBLE); 
			schedule_timeout(1);
			
		}
	}
	while(i <count && rec != 10 && mydev->timer_state !=1);
	 
	//copy buffer to user
	error_count = copy_to_user((void *) buff,(char *) mydev->rbuf, strlen(mydev->rbuf)+1);
	
	if(error_count <  0 && error_count >count){
		printk(KERN_ALERT "Copy from user error!\n");
		error_count=0;
	}
	else if(error_count == 0){
		error_count = count;		
	}
	else{
		printk(KERN_NOTICE "Partial Copy from user - %d\n", c);
	}

	
	//print data
	if(mydev->rbuf[strlen(mydev->rbuf)-1] == 10)
		printk(KERN_NOTICE "Reading finished %ld: %s",strlen(mydev->rbuf)+1,  mydev->rbuf);	
	else
		printk(KERN_NOTICE "Reading finished %ld: %s\n",strlen(mydev->rbuf)+1,  mydev->rbuf);

	kfree(mydev->rbuf);
	mydev->filercount += error_count;
	//return
	if(error_count)
		return error_count;	
	return error_count;
}
// struct file operations
static struct file_operations fops = {
	.owner		= THIS_MODULE,
	.llseek 	= no_llseek,
	.open 		= dev_open,
	.release	= dev_release,
	.write		= dev_write,
	.read 		= dev_read,
};

// Basic functions init/exit
static int serp_init(void)
{
	int res=0, major=0;

	printk(KERN_ALERT "Serp, world\n");
	
	//Request port addresses from 0x3f8-0x3ff
	if(request_region(BASE_PORT, LAST_PORT - BASE_PORT+1, "serp") == NULL){
		printk(KERN_ALERT "Region Request for serp failed!\n");		
		return -1;
	}     

	//call to function to configure the uart
	config_uart_dev();

	//alloc struct my_device and its fields
	mydev=kzalloc(sizeof(struct my_device), GFP_KERNEL);
	mydev->count=1;
	mydev->chard = kzalloc(sizeof(struct cdev), GFP_KERNEL);
	mydev->devname = "serp";
	//get a devnumber according to major and minor(0)
	//mydev->devno = MKDEV(major, 0);

	//Register device region
	res = alloc_chrdev_region(&(mydev->devno), 0, 1, mydev->devname);
	if(res < 0)
		return res;
	
	major = MAJOR(mydev->devno);
	printk(KERN_ALERT "cdev_init...\n");
	
	//initialise device
	cdev_init(mydev->chard, &fops);	
	printk(KERN_ALERT "cdev_init sucessfull\n");

	//add device
	if(cdev_add(mydev->chard, mydev->devno, 1) <0){
		printk(KERN_ERR "Error in cdev-add\n");
		return res;
	}

	printk(KERN_NOTICE "Echo major ---> %d\n", major);
	
	//initialize timmer
	setup_timer(&read_timer, timer_end, 0);
    mydev->timer_state = 0;

	return res;
}

static void serp_exit(void)
{
	int major=MAJOR(mydev->devno);

	//delete device
	cdev_del(mydev->chard);
	//unregister device region
	unregister_chrdev_region( mydev->devno , 1);
	//release port region
	release_region(BASE_PORT, LAST_PORT - BASE_PORT +1);

	//free mydev and its members
	mydev->count =0;
	kfree(mydev->chard);
	kfree(mydev);

	printk(KERN_ALERT "Goodbye, cruel world\nFreeing major: %d\n", major);
}

module_init(serp_init);
module_exit(serp_exit);

//help funtions
void config_uart_dev(void){

	char LCR=0;
	
	//disable interrupts
	if((inb(BASE_PORT + UART_LSR) & UART_LSR_THRE) != 0)
		outb(0, BASE_PORT + UART_IER);
	else
		schedule();
	
	//build and set 8bit character, 2 stop bits, even pairity
	LCR |= UART_LCR_WLEN8 | UART_LCR_STOP| UART_LCR_EPAR;
	if((inb(BASE_PORT + UART_LSR) & UART_LSR_THRE) != 0)
		outb(LCR, BASE_PORT + UART_LCR);
	else
		schedule();
	
	//set bitrate
	LCR |= UART_LCR_DLAB;
	
	//set DLAB
	if((inb(BASE_PORT + UART_LSR) & UART_LSR_THRE) != 0)
		outb(LCR, BASE_PORT + UART_LCR);
	else
		schedule();
	
	//lowest byte to DLL
	if((inb(BASE_PORT + UART_LSR) & UART_LSR_THRE) != 0)
		outb(UART_DIV_1200, BASE_PORT + UART_DLL);
	else
		schedule();

	//highest byto do DLM
	if((inb(BASE_PORT + UART_LSR) & UART_LSR_THRE) != 0)
		outb(UART_DIV_1200 >> 8, BASE_PORT + UART_DLM);
	else
		schedule();

	LCR &= ~UART_LCR_DLAB;

	//reset DLAB
	if((inb(BASE_PORT + UART_LSR) & UART_LSR_THRE) != 0)
		outb(LCR, BASE_PORT + UART_LCR);
	else
		schedule();
}

void timer_end(unsigned long data){
	//when timmer ends it changes mydev->timmer_state to 1
	mydev->timer_state =1;
}