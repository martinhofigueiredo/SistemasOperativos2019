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
#include <linux/kfifo.h>
#include <asm/semaphore.h>
#include <linux/interrupt.h>


#define FIFO_SIZE 4096
#define BASE_PORT 0x3f8
#define LAST_PORT 0x3ff

MODULE_LICENSE("Dual BSD/GPL");

struct my_device{
	dev_t devno;
	struct cdev *chard;
	int irq;
	int count;
	int dev_count;
	int timer_state;
	char *data;
	char *devname;
	struct semaphore sem; 
	struct kfifo *dev_fifo; 
	spinlock_t lock;   
	wait_queue_head_t r_queue, w_queue; // the read_queue and write_queue decs
	int wq_flag, rq_flag;
};

dev_t devno;
struct my_device *mydev;

struct timer_list read_timer;

void config_uart_dev(void);
void timer_end(unsigned long data);
int write_work(void);
int read_work(void);

irqreturn_t handler_i(int irq, void * dev_id);
// File Operations funtions 

// Non_seekable Open / Release
int dev_open(struct inode *inode, struct file *filp ){
	
	struct my_device *dev;
	
	//finds the address of the struct my_device which has in the member chard the address of inode->i_cdev
	dev = container_of(&inode->i_cdev, struct my_device, chard);

	
	filp->private_data = dev;

	printk(KERN_NOTICE "Open finished\n");

	return nonseekable_open( inode, filp);
}

int dev_release(struct inode *inode, struct file *filp ){
	//delete timer
	del_timer(&read_timer);
	printk(KERN_NOTICE "Released finished\n");
	return 0;
}

// Write / Read
ssize_t dev_write(struct file *filep, const char __user *buff, size_t count, loff_t *offp){
	int c=0;
	char first;
	//char *buf;
	
	mydev->data = kzalloc(sizeof(char)*count+1, GFP_KERNEL);

	mydev->count = count;
	
	c = copy_from_user((void*) mydev->data, (char *) buff, count);
	if(c){
		printk(KERN_ALERT "Error in Copy from user\n");
	}
	first = mydev->data[0];
	down_interruptible(&(mydev->sem));
    c = kfifo_put(mydev->dev_fifo, (mydev->data), count);
    up(&(mydev->sem));
    outb(first, BASE_PORT+ UART_TX);
    if(c!=count){
    	printk(KERN_NOTICE "c != count!\n");
    }
    //outb((mydev->data[0]), BASE + UART_TX); // Gotta write the first char here

	*offp+=c;

	printk(KERN_NOTICE "Writing finished %d: %s\n",c,  buff);
	kfree(mydev->data);
	
	return c;
}

ssize_t dev_read(struct file *filep, char __user *buff, size_t count, loff_t *offp){
	
	int c=count, error_count=0, i=0;
	char *rbuf;
	char aux=0;
	
	//max time to wait for input data
	mydev->timer_state = 0;
	rbuf = kzalloc(sizeof(char)*count+1, GFP_KERNEL);
	
	i=0;
	do{
		msleep_interruptible(3);
		aux=kfifo_len(mydev->dev_fifo);
		if(aux>0){

			down_interruptible(&(mydev->sem));
			error_count = kfifo_get(mydev->dev_fifo, &(rbuf[i]), count);
			up(&(mydev->sem));
			i += error_count;
			
			if(strlen(rbuf)>=0)
				aux = rbuf[strlen(rbuf)-1];
			printk(KERN_NOTICE "%d  - - - - error_count\n",error_count);
		}
		
		
			//printk(KERN_ALERT "ERROR in kfifo_get\n");
			//return error_count;
		//}

	}
	while( aux != 10) ;
	
	printk(KERN_NOTICE "%d .... %ld\n", aux, (strlen(rbuf)+1 <count));
	//copy buffer to user
	c = error_count;
	error_count = copy_to_user((void *) buff,(char *) rbuf, strlen(rbuf)+1);
	//print data
	if(rbuf[strlen(rbuf)-1] == 10)
		printk(KERN_NOTICE "Reading finished %d: %s",strlen(rbuf),  rbuf);	
	else
		printk(KERN_NOTICE "Reading finished %d: %s\n",strlen(rbuf),  rbuf);

	kfree(rbuf);

	//return
	if(error_count)
		return error_count;	

	return i;
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
static int seri_init(void)
{
	int res=0, fd=0;
	int major=0;

	printk(KERN_ALERT "Seri, world\n");
	//alloc struct my_device and its fields
	mydev=kzalloc(sizeof(struct my_device), GFP_KERNEL);
	mydev->dev_count=1;
	mydev->chard = kzalloc(sizeof(struct cdev), GFP_KERNEL);
	mydev->devname = "seri";
	mydev->irq = 4;
	
	spin_lock_init(&(mydev->lock));

	init_waitqueue_head(&(mydev->r_queue));
	init_waitqueue_head(&(mydev->w_queue));
	mydev->dev_fifo = kfifo_alloc(FIFO_SIZE, GFP_KERNEL, &mydev->lock);
	init_MUTEX(&mydev->sem);  


	//Request port addresses from 0x3f8-0x3ff
	if(request_region(BASE_PORT, LAST_PORT - BASE_PORT+1, "seri") == NULL){
		printk(KERN_ALERT "Region Request for seri failed!\n");		
		return -1;
	}     

	//call to function to configure the uart
	config_uart_dev();

	//Register device region
	res = alloc_chrdev_region(&(mydev->devno), 0, 1, mydev->devname);
	if (res < 0)
    {
        printk(KERN_ERR "Major number allocation failed!\n");
        return res;
    }
	major = MAJOR((mydev->devno));
	mydev->dev_count=1;
	
	printk(KERN_ALERT "cdev_init...\n");
	//initialise device
	cdev_init(mydev->chard, &fops);	
	printk(KERN_ALERT "cdev_init sucessfull\n");

	//add device
	res = cdev_add(mydev->chard, mydev->devno, 1);
	if(res < 0){
		printk(KERN_ERR "Error in cdev-add\n");
		return res;
	}

	printk(KERN_NOTICE "Echo major ---> %d\n", major);
	
	res = request_irq(mydev->irq, handler_i, SA_INTERRUPT, mydev->devname, &mydev);
	if (res < 0)
    {
        printk(KERN_ERR "Failed allocating interrupt line!\n");
        return res;
    }
	//initialize timmer
	setup_timer(&read_timer, timer_end, 0);
    mydev->timer_state = 0;

    //fd=dev_open("/dev/seri", O_RDWR );

	return res;
}

static void seri_exit(void)
{
	int major = MAJOR((mydev->devno));
	//free irq
	free_irq(mydev->irq, &mydev);
	//delete device
	cdev_del(mydev->chard);
	//unregister device region
	unregister_chrdev_region( mydev->devno , 1);
	//release port region
	release_region(BASE_PORT, LAST_PORT - BASE_PORT +1);

	//free mydev and its members
	mydev->dev_count =0;
	kfree(mydev->chard);
	kfifo_free(mydev->dev_fifo);
	kfree(mydev);

	printk(KERN_ALERT "Goodbye, cruel world\nFreeing major: %d\n", major);
}

module_init(seri_init);
module_exit(seri_exit);

//help funtions
void config_uart_dev(void){

	char LCR=0;
	
	//enable Receiver Interrrupt and Transmitter holding register interrupt
	LCR = UART_IER_RDI | UART_IER_THRI;// | UART_RLSI; // Enable receiver interrupt and
                                        //Transmitter holding register interrupt
    outb(LCR, BASE_PORT + UART_IER);         // write to it

	//build and set 8bit character, 2 stop bits, even pairity
	LCR |= UART_LCR_WLEN8 | UART_LCR_STOP| UART_LCR_EPAR;
	//if((inb(BASE_PORT + UART_LSR) & UART_LSR_THRE) != 0)
		outb(LCR, BASE_PORT + UART_LCR);

	//set bitrate
	LCR |= UART_LCR_DLAB;
	
	//set DLAB
	outb(LCR, BASE_PORT + UART_LCR);
	//else
	//	schedule();
	
	//lowest byte to DLL
	//if((inb(BASE_PORT + UART_LSR) & UART_LSR_THRE) != 0)
	outb(UART_DIV_1200, BASE_PORT + UART_DLL);
	//else
	//	schedule();

	//highest byto do DLM
	//if((inb(BASE_PORT + UART_LSR) & UART_LSR_THRE) != 0)
	outb(UART_DIV_1200 >> 8, BASE_PORT + UART_DLM);
	//else
	//	schedule();

	LCR &= ~UART_LCR_DLAB;

	//reset DLAB
	//if((inb(BASE_PORT + UART_LSR) & UART_LSR_THRE) != 0)
	outb(LCR, BASE_PORT + UART_LCR);
	//else
	//	schedule();
}

void timer_end(unsigned long data){
	//when timmer ends it changes mydev->timmer_state to 1
	mydev->timer_state =1;
}

irqreturn_t handler_i(int irq, void * dev_id){

	//Write interrupt
	char iir = inb(BASE_PORT + UART_IIR) ;
	printk(KERN_NOTICE "IIR: %d\n", iir);

    if ((iir  & UART_IIR_THRI) != 0)
    {
    	printk(KERN_NOTICE "Handling Write Interrrupt\n");
        write_work(); // Routine for writing data
    }
    else if((iir & UART_IIR_RDI) != 0)
    {
    	printk(KERN_NOTICE "Handling Read Interupt\n");
    	read_work();
    }

	printk(KERN_NOTICE "Handler Func!\n");
	return 0;
    
}
int write_work(void){
	int i=0, wr_c=0;
	char wr_b[mydev->count];

	down_interruptible(&(mydev->sem));
	wr_c = kfifo_get(mydev->dev_fifo, wr_b, mydev->count);
	up(&(mydev->sem));
	if(wr_c>0){
		for(i=1; i<mydev->count; i++){
			outb(wr_b[i], BASE_PORT+ UART_TX);
		}
	}
	return wr_c;
}

int read_work(void)
{
	char rbuf, rd_c=0;
	rbuf = inb(BASE_PORT + UART_RX);
	down_interruptible(&(mydev->sem));
    rd_c=kfifo_put(mydev->dev_fifo, &rbuf, sizeof(unsigned char));
    up(&(mydev->sem));
    return rd_c;
}
