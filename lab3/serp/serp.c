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
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/sched.h>

#define LPORT 0x3ff
#define BPORT 0x3f8

struct device{
	dev_t dev;
	struct cdev *chard;
	char *devname;
	int count;
	int tstate;
	char *rbuf;
	int countread;
	int countwrite;
};

struct device *mdevice;

struct timer_list rtimer;

void config_uart_dev(void){

	char LCR=0;
	
	//disable interrupts
	if((inb(BPORT + UART_LSR) & UART_LSR_THRE) != 0)
		outb(0, BPORT + UART_IER);
	else
		schedule();
	
	//build and set 8bit character, 2 stop bits, even pairity
	LCR |= UART_LCR_WLEN8 | UART_LCR_STOP| UART_LCR_EPAR;
	if((inb(BPORT + UART_LSR) & UART_LSR_THRE) != 0)
		outb(LCR, BPORT + UART_LCR);
	else
		schedule();
	
	//set bitrate
	LCR |= UART_LCR_DLAB;
	
	//set DLAB
	if((inb(BPORT + UART_LSR) & UART_LSR_THRE) != 0)
		outb(LCR, BPORT + UART_LCR);
	else
		schedule();
	
	//lowest byte to DLL
	if((inb(BPORT + UART_LSR) & UART_LSR_THRE) != 0)
		outb(UART_DIV_1200, BPORT + UART_DLL);
	else
		schedule();

	//highest byto do DLM
	if((inb(BPORT + UART_LSR) & UART_LSR_THRE) != 0)
		outb(UART_DIV_1200 >> 8, BPORT + UART_DLM);
	else
		schedule();

	LCR &= ~UART_LCR_DLAB;

	//reset DLAB
	if((inb(BPORT + UART_LSR) & UART_LSR_THRE) != 0)
		outb(LCR, BPORT + UART_LCR);
	else
		schedule();
}


void timer_end(unsigned long data){
		
		mdevice->tstate =1;
}

// File Operations funtions 

int deviceopen(struct inode *inode, struct file *filep ){
	struct device *mdev;
	mdev = container_of(&inode->i_cdev, struct device, chard);
	
	filep->private_data = mdev;
	mdev->countwrite=0;
	mdev->countread=0;
	printk(KERN_NOTICE "OPEN SUCESSFULL\n");

	return nonseekable_open( inode, filep);
}

int devicerelease(struct inode *inode, struct file *filep ){
	//delete timer
	del_timer(&rtimer);
	printk(KERN_NOTICE "REALEASED SUCESSFULL\nCharacters read is %d and written is %d\n", mdevice->countread, mdevice->countwrite);
	return 0;
}

ssize_t devicewrite(struct file *filep, const char __user *buff, size_t count, loff_t *offp){

	int c=0, i=0;
	char *buf;
	buf = kzalloc(sizeof(char)*count+1, GFP_KERNEL);

	c = copy_from_user((void*) buf, (char *) buff, count);
	

	if(c<0 || c >count){
		printk(KERN_ALERT "Copy from user error!\n");
		c=0;
	}
	else if(c == 0){
		c=count;
		//sends byte by byte to the UART_TX
		for(i=0; i<c ; i++){
			//check if can send another byte
			if( ( inb(BPORT + UART_LSR) & UART_LSR_THRE )!= 0)
				//if transmitter is not empty
				outb(buf[i], BPORT+ UART_TX);
			else
				schedule();
		}
	}
	else{
		printk(KERN_NOTICE "Partial Copy from user - %d\n", c);
	}

	*offp+=c;
	mdevice->countwrite +=c;
	printk(KERN_NOTICE "Writing finished %d: %s\n",c,  buff);
	kfree(buf);
	return c;
}

ssize_t deviceread(struct file *filep, char __user *buff, size_t count, loff_t *offp){
	
	int c=count;
	int errcount=0;
	int i=0;
	int beg_idle = jiffies;
	char rec=0;
	
	
	//max time to wait for input data
	mdevice->tstate = 0;
	mdevice->rbuf = kzalloc(sizeof(char)*count+1, GFP_KERNEL);

	i=0;
	//read cycle
	do{
		//if Data Ready in LSR register
		if((inb(BPORT+UART_LSR) & UART_LSR_DR ) == 0)
		{
			//if it's not supposed to block
			if (filep->f_flags & O_NONBLOCK)
            {
            	kfree(mdevice->rbuf);
				return EAGAIN;
			}
		}
		else if( (inb(BPORT+UART_LSR)) & (UART_LSR_PE | UART_LSR_OE | UART_LSR_FE) ) 
		{
			//Some unknown error has occured
			printk(KERN_ALERT "Error in read\n");
			return EIO;
		}
		else{
			//read char
			printk(KERN_NOTICE "Data ready\n");
			mdevice->rbuf[i]= inb(BPORT+UART_RX);
			rec = mdevice->rbuf[i];
			i++;
			//reset timmer
			mod_timer(&rtimer, jiffies + msecs_to_jiffies(5000));
			printk(KERN_NOTICE "former time %d, actual time %ld\n",beg_idle,  (long int)jiffies);
			beg_idle = jiffies;
			//schedule to give time to the program to run anything it might need
			set_current_state(TASK_INTERRUPTIBLE); 
			schedule_timeout(1);
			
		}
	}
	while(i <count && rec != 10 && mdevice->tstate !=1);
	 
	//copy buffer to user
	errcount = copy_to_user((void *) buff,(char *) mdevice->rbuf, strlen(mdevice->rbuf)+1);
	
	if(errcount <  0 && errcount >count){
		printk(KERN_ALERT "Copy from user error!\n");
		errcount=0;
	}
	else if(errcount == 0){
		errcount = count;		
	}
	else{
		printk(KERN_NOTICE "Partial Copy from user - %d\n", c);
	}

	
	//print data
	if(mdevice->rbuf[strlen(mdevice->rbuf)-1] == 10)
		printk(KERN_NOTICE "Reading finished %ld: %s",strlen(mdevice->rbuf)+1,  mdevice->rbuf);	
	else
		printk(KERN_NOTICE "Reading finished %ld: %s\n",strlen(mdevice->rbuf)+1,  mdevice->rbuf);

	kfree(mdevice->rbuf);
	mdevice->countread += errcount;
	//return
	if(errcount)
		return errcount;	
	return errcount;
}
// struct file operations
static struct file_operations fops = {
	.owner		= THIS_MODULE,
	.open 		= deviceopen,
	.write		= devicewrite,
	.read 		= deviceread,
	.release	= devicerelease,
	.llseek 	= no_llseek,
};

// Basic functions init/exit
static int serp_init(void)
{
	int res=0, major=0;

	printk(KERN_ALERT "Serp, world\n");
	
	//Request port addresses from 0x3f8-0x3ff
	if(request_region(BPORT, LPORT - BPORT+1, "serp") == NULL){
		printk(KERN_ALERT "Region Request for serp failed!\n");		
		return -1;
	}     

	//call to function to configure the uart
	config_uart_dev();

	//alloc struct device and its fields
	mdevice=kzalloc(sizeof(struct device), GFP_KERNEL);
	mdevice->count=1;
	mdevice->chard = kzalloc(sizeof(struct cdev), GFP_KERNEL);
	mdevice->devname = "serp";
	//get a devnumber according to major and minor(0)
	//mdevice->dev = MKDEV(major, 0);

	//Register device region
	res = alloc_chrdev_region(&(mdevice->dev), 0, 1, mdevice->devname);
	if(res < 0)
		return res;
	
	major = MAJOR(mdevice->dev);
	printk(KERN_ALERT "cdev_init...\n");
	
	//initialise device
	cdev_init(mdevice->chard, &fops);	
	printk(KERN_ALERT "cdev_init sucessfull\n");

	//add device
	if(cdev_add(mdevice->chard, mdevice->dev, 1) <0){
		printk(KERN_ERR "Error in cdev-add\n");
		return res;
	}

	printk(KERN_NOTICE "Echo major ---> %d\n", major);
	
	//initialize timmer
	setup_timer(&rtimer, timer_end, 0);
    mdevice->tstate = 0;

	return res;
}

static void serp_exit(void)
{
	int major=MAJOR(mdevice->dev);

	//delete device
	cdev_del(mdevice->chard);
	//unregister device region
	unregister_chrdev_region( mdevice->dev , 1);
	//release port region
	release_region(BPORT, LPORT - BPORT +1);

	//free mdevice and its members
	mdevice->count =0;
	kfree(mdevice->chard);
	kfree(mdevice);

	printk(KERN_ALERT "Goodbye, cruel world\nFreeing major: %d\n", major);
}

module_init(serp_init);
module_exit(serp_exit);



