/*
 * File Name: Xilinx_Spark.c
 * Author: Ehsan Ghasemi
 * Description:
 *   This module is a character device driver for controlling the Hardware
 * implemented inside the FPGA in order to accelerate KMeans algorithm
 * inside FPGA. 
 *   The character device driver includes open, close, read, write, mmap, and ioctl 
 * system calls.
 *  The MMAP function call provide direct access to the memory buffer allocated for 
 * the DMAs to the user application. The user is required to memory map the address 
 * using the mmap function call and read and write the data based on the mmap
 * buffer provided by calling the mmap system call.
 *  The READ and WRITE system call in the module only controls the DMA engines 
 * insude the FPGA to move data from a preallocated buffer that is assumed to 
 * be memory mapped by the user application, to send and receive the data to 
 * and from FPGA.
 *  IOCTL function call handles initialization of the KMeans core inside the FPGA.  
 * This includes providing number of data points, number of iterations and 
 * sending the initial values of the centres. The initial values of the centres
 * are set based on he first set of values in the MMAP buffer region. Therefore,
 * the points are required to be written to the MMAP region before the IOCTL 
 * system call.
 *
 * Revision History:
 * - IOCTL added in order to reinitialize the centres for every run
 * May 15
 * - The Hardware architecture inside FPGA has changed to allow the processor
 *   access the PL memory region directly. 
 *   To support that, the addresses for MM2S and S2MM buffers are changed accordingly
 *
 * May 24
 * - The device driver is modified to support the new job deployment through Scala.
 *   This consists of a global variable which keeps track of the data
 *   offset for the memory mapped section of the file.
 *   size value is changed using SET_SIZE IOCTL function
 *   SET_SIZE has to be invoked after the mmap as to update previous memory location
 *   CLEAN_UP IOCTL is added to set the size back to initial state i.e. 0 
 *   for the next run.
 *   KMEANS_SET writes the updated centres to the FPGA starting from READ_BUFADDR
 *   dma_write function is modified so that it would write 
 *   SIZE bytes of data in every call.
 *
 * June 9
 * - A set of IOCTL functions were added for the system to work directly with Spark Core
 *   The driver is functional and test with Spark driver program for both floating and fixed point.
 * June 12
 * - In order to remove the requirement for repartitioning the 
     data into number of nodes in the cluster in Spark, data_offset and num_pts
     variables are used so to keep track of the offset in the buffer so multiple
     RDD partitions can one after another be copied into the buffer.
   - To make the system functional, GET_OFFSET, CLEAN_UP and INITIALIZE function 
     calls are also implemented to keep track of offset for the JNI calls.
 * 
 *
 *
 *
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h> // include the dev_t structure
#include <linux/kdev_t.h> // include MAJOR and MINOR macros to obtain major,minor number
#include <linux/fs.h> // include characture device registartion API
#include <linux/cdev.h> //include the cdev (kernel represetation of character device) structure
#include <asm/io.h> // API for memory mapped io components  i.e. ioremap
#include <linux/slab.h> // for dynamic memory allocation
#include <asm/uaccess.h> // allow protected access to user buffer
#include <linux/device.h> // for creating class in /sys/class for dynamic node creatin in /dev
#include <linux/interrupt.h> 
#include <linux/sched.h>
#include <linux/wait.h> // to declare a wait_queue_head_t struct
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/of_platform.h>
#include <linux/dma-mapping.h> // to declare io_remap_pfn_range
#include "KMeans_ioctl.h"
#include "xaxiethernet_hw.h"
// General definition
#define NUM_DMA    2
#define MM2S_BURSTSIZE 0x400000 // 4M --> dependent on the fifo size and dma setup
#define S2MM_BURSTSIZE 0x400000 // 4M 

#define KMEANS_BASEADDR 0x43C00000
#define KMEANS_SIZE  64*1024
#define KMEANS_ITERATIONS 0x4
#define KMEANS_PTS 0x0
//#define GPIO_BASEADDR 0x41210000
//#define GPIO2_BASEADDR 0x41200000
//#define GPIO_SIZE    64*1024
#define DMA_BASEADDR 0x40400000
#define DMA_SIZE     0x10000


#define TRANS_BASEADDR 0x43C10000
#define TRANS_SIZE 0x10000
#define TX_SRC_MAC_UP		0x00    /** Source MAC Upper Address */
#define TX_SRC_MAC_DOWN		0x04    /** Source MAC Lower Address */
#define TX_DST_MAC_UP		0x08    /** Destination MAC Upper Address */
#define TX_DST_MAC_DOWN		0x0C    /** Destination MAC Lower Address */
#define TX_RST			0x10    /** Reset Register */


#define RECV_BASEADDR 0x43C20000
#define RECV_SIZE 0x10000
#define RX_RST				0x10    /** Reset Register */


#define ETHERNET_BASEADDR 0x43C40000
#define ETHERNET_SIZE 0x10000
/* IEEE PHY Specific definitions */
#define PHY_R0_CTRL_REG		0
#define PHY_R3_PHY_IDENT_REG	3

#define PHY_R0_RESET         0x8000
#define PHY_R0_LOOPBACK      0x4000
#define PHY_R0_ANEG_ENABLE   0x1000
#define PHY_R0_DFT_SPD_MASK  0x2040
#define PHY_R0_DFT_SPD_10    0x0000
#define PHY_R0_DFT_SPD_100   0x2000
#define PHY_R0_DFT_SPD_1000  0x0040
#define PHY_R0_ISOLATE       0x0400

#define DMA_WRITE 1
#define DMA_READ 0

#define READBUF_BASEADDR  1016*1024*1024//0x80000000//1008*1024*1024
#define READBUF_SIZE      8*1024*1024
#define WRITEBUF_BASEADDR 0x90000000//1016*1024*1024
#define WRITEBUF_SIZE     320*1024*1024


//#define MM2S_IRQ    62
#define MM2S_IRQ    61
#define MM2S_DMACR  0x00
#define MM2S_DMASR  0x04
#define MM2S_SA     0x18
#define MM2S_LENGTH 0x28

//#define S2MM_IRQ    63
#define S2MM_IRQ    62
#define S2MM_DMACR  0x30
#define S2MM_DMASR  0x34
#define S2MM_DA     0x48
#define S2MM_LENGTH 0x58

#define KMEANS_IRQ  63
#define BILLION 1000000000L

struct mm2s_dev {
  void __iomem *buf;
  wait_queue_head_t queue;
  volatile int done;
  volatile int err;
  
};

struct s2mm_dev {
  void __iomem *buf;
  wait_queue_head_t queue;
  volatile int done;
  volatile int err;

};

struct dma_dev {
  void __iomem *dma_mm; //dma memory mapped
  struct s2mm_dev * s2mm;
  struct mm2s_dev * mm2s;

};

struct dma_struct {
  struct cdev c_dev;
  struct class *cl;
  //  void __iomem *gpio;
  //void __iomem *dma;
  struct dma_dev *dma;

};

struct kmeans_struct {
  void __iomem *mmap;
  wait_queue_head_t queue;
  volatile int ready;

};

dev_t dt; // device type to hold major minor number


struct dma_struct *dma_device;
struct kmeans_struct *kmeans_device;
void __iomem *ethernet_device;
void __iomem *trans_device;
void __iomem *recv_device;
//kmeans_arg_t km;
int iterations;
unsigned long data_offset;
unsigned long num_pts;

struct timespec start_dma,stop_dma;
unsigned long diff_dma;

// VARIABLES FOR TRANSMITTER
char AxiEthernetMAC[6] = { 0x00, 0x0A, 0x35, 0x01, 0x02, 0x03 };

irqreturn_t mm2s_interrupt(int irq,  void *dev_id, struct pt_regs *regs){
  
  // printk(KERN_INFO "Inside MM2S interrupt, done = %d \n",dma_device->dma[1].mm2s->done);

  iowrite32(0x1000,dma_device->dma[1].dma_mm+MM2S_DMASR);

  //setting the done flag and wake up the proccesses waiting in the queue
  dma_device->dma[1].mm2s->done = 0x1;
  wake_up_interruptible(&dma_device->dma[1].mm2s->queue);

  return IRQ_HANDLED;
}

irqreturn_t s2mm_interrupt(int irq, void *dev_id, struct pt_regs *regs){
  // printk(KERN_INFO "Inside S2MM interrupt, done = %d \n",dma_device->dma[1].s2mm->done);

  iowrite32(0x1000,dma_device->dma[1].dma_mm+S2MM_DMASR);
 
  dma_device->dma[1].s2mm->done = 0x1;
  wake_up_interruptible(&dma_device->dma[1].s2mm->queue);
  
  return IRQ_HANDLED;
}

irqreturn_t kmeans_interrupt(int irq, void *dev_id, struct pt_regs *regs){
  // printk(KERN_INFO "Inside S2MM interrupt, done = %d \n",dma_device->dma[1].s2mm->done);

  //iowrite32(0x1000,dma_device->dma[1].dma_mm+S2MM_DMASR);
 
  //dma_device->dma[1].s2mm->done = 0x1;
  kmeans_device->ready = 0x1;
  wake_up_interruptible(&kmeans_device->queue);

  return IRQ_HANDLED;
}



static int dma_open(struct inode *i, struct file *f)
{
 
  // INITIALIZE HARDWARE  i.e. check the idle bit and set the start bit

  // if xilinx_dma_read open
  if (iminor(i) == DMA_READ){
    printk(KERN_INFO "Driver: read open()\n");
    dma_device->dma[1].s2mm->done = 0;
    dma_device->dma[1].s2mm->err = 0;

  }
  else{
    printk(KERN_INFO "Driver: write open()\n");
    dma_device->dma[1].mm2s->done = 0;
    dma_device->dma[1].mm2s->err = 0;
  }
  
 
  return 0;
}
static int dma_close(struct inode *i, struct file *f)
{
  // UNREGISTER INTERRUPTS FOR BOTH CHANNELS
  if (iminor(i) == DMA_READ)
    printk(KERN_INFO "Driver: read close()\n");
  else
    printk(KERN_INFO "Driver: write close()\n");
  return 0;
}
static ssize_t dma_read(struct file *f, char __user *buf, size_t
			len, loff_t *off)
{
  int ret = 0;
  int num_of_packets = 0;
  int packet_remainder = 0;
  int count = 0; 
  int packet_size = S2MM_BURSTSIZE;
  // int offset = 0;
  //  struct timespec start,stop;
  //  unsigned long diff;
  if ( iminor (f->f_path.dentry->d_inode) == DMA_READ){
    // printk(KERN_INFO "Read buffer length  %d \n",len);
    num_of_packets = len / S2MM_BURSTSIZE;
    packet_remainder = len % S2MM_BURSTSIZE;
    printk(KERN_INFO "packets read: %d remainder %d \n", num_of_packets, packet_remainder);
    if (packet_remainder)
      num_of_packets++;
    // printk(KERN_INFO "packets read : %d remainder %d \n", num_of_packets, packet_remainder);

    // SPECIAL CASE WHERE THERE IS ONLY ONE PACKET AND THE SIZE IS LESS THAN A FULL PACKET
    if (num_of_packets == 1 && packet_remainder !=0)
      packet_size = packet_remainder;

    // READ THE FIRST PACKET
    iowrite32(0x5001,dma_device->dma[1].dma_mm+S2MM_DMACR);
    iowrite32(READBUF_BASEADDR+count*S2MM_BURSTSIZE,dma_device->dma[1].dma_mm+S2MM_DA);
    iowrite32(packet_size,dma_device->dma[1].dma_mm+S2MM_LENGTH);

    while (num_of_packets != 0 ){

      num_of_packets--;


      ret = wait_event_interruptible(dma_device->dma[1].s2mm->queue,
				     (dma_device->dma[1].s2mm->done == 1) || 
				     (dma_device->dma[1].s2mm->err == 1));
      if (ret){
	return -ERESTARTSYS;
      }
    
      // set the flag back to 0 for the next transfer
      dma_device->dma[1].s2mm->done = 0;
      if (num_of_packets == 0)
	break;

      count++;
   
      // configure S2MM channel of DMA
      // set the read base address
      // set the size of the transfer
      iowrite32(0x5001,dma_device->dma[1].dma_mm+S2MM_DMACR);
      iowrite32(READBUF_BASEADDR+count*S2MM_BURSTSIZE,dma_device->dma[1].dma_mm+S2MM_DA);
      iowrite32(packet_size,dma_device->dma[1].dma_mm+S2MM_LENGTH);
      //getrawmonotonic(&start);
    
      if (num_of_packets == 1){
	if(packet_remainder !=0 )
	  packet_size = packet_remainder;
      }
    
    }

   
    ret = len;
  }
  else
    ret = 0;
  
  return ret;
}

static ssize_t dma_write(struct file *f, const char __user *buf,
			 size_t len, loff_t *off)
{
  unsigned int ret = 0;
  int num_of_packets = 0;
  int packet_remainder = 0;
  int packet_size = MM2S_BURSTSIZE;
  int count = 0;
  int it;
  //  int offset = 0;


  // if the xil_dma_write file was accessed do transfer 
  // else return zero
  if ( iminor (f->f_path.dentry->d_inode) == DMA_WRITE){
    //printk(KERN_INFO "Write buffer length %d \n", len);
    printk(KERN_INFO "Write buffer length %lu \n", data_offset);
    printk(KERN_INFO " ITERATION: %d \n", iterations);

    packet_size = MM2S_BURSTSIZE;
    //num_of_packets = data_offset/MM2S_BURSTSIZE;
    num_of_packets = len/MM2S_BURSTSIZE;
    //packet_remainder = data_offset % MM2S_BURSTSIZE;
    packet_remainder = len % MM2S_BURSTSIZE;
 
    // increase num_of_packet if there's less than a packet data
    if (packet_remainder)
      num_of_packets++; 

    printk(KERN_INFO "packets write: %d remainder %d iteration %d\n", num_of_packets, packet_remainder,it);

    // in case the data to be sent is less than a packet size
    if (num_of_packets == 1 && packet_remainder !=0)
      packet_size = packet_remainder;

    while (num_of_packets != 0){

     

      // configure the dma device for MM2S channel
      // enable dma plus IOC and IERR
      // provide write address
      // provide length
      iowrite32(0x5001,dma_device->dma[1].dma_mm); 
      iowrite32(WRITEBUF_BASEADDR+count*MM2S_BURSTSIZE,dma_device->dma[1].dma_mm+MM2S_SA); 
      iowrite32(packet_size,dma_device->dma[1].dma_mm+MM2S_LENGTH); 
      printk(KERN_INFO "packets size: %d iteration %d\n", packet_size ,it);
      num_of_packets--;
      count++;
      
      
      if (num_of_packets == 1){
	if(packet_remainder !=0 )
	  packet_size = packet_remainder;
      }
    
      // wait for the data transfer to finish
      ret = wait_event_interruptible(dma_device->dma[1].mm2s->queue,
				     (dma_device->dma[1].mm2s->done == 1) || 
				     (dma_device->dma[1].mm2s->err == 1));
      
      if (ret){
	return -ERESTARTSYS;
      }

      // set the flag back to 0 for the next transfer
      // reduece num of packet and increase count
      dma_device->dma[1].mm2s->done = 0;

     

      // FOR 1 LESS THAN NUMBER OF ITERATIONS
      // WAIT FOR THE READY SIGNAL TO RE-STREAM THE DATA
      //if (it != iterations-1){
      //	ret =  wait_event_interruptible(kmeans_device->queue,
      //				(kmeans_device->ready == 1));

      kmeans_device->ready = 0;
    }
    // RESET VARIABLES
    count = 0;

    //}
  
   
    ret = len;
  }
  else
    ret = 0;
  
  return ret;
}



// MMAP OPEN AND CLOSE FUNCTION
// THESE FUNCTIONS ARE CALLED BY THE OS WHEN 
// MMAP FUNCTION IS CALLED
void dma_vma_open(struct vm_area_struct *vma)
{
  printk(KERN_NOTICE "DMA VMA open, virt %lx, phys %lx\n",
	 vma->vm_start, vma->vm_pgoff << PAGE_SHIFT);
  return;
}
void dma_vma_close(struct vm_area_struct *vma)
{ 
  // printk(KERN_NOTICE "SETTING NUM_PTS %lu and SIZE %lu\n", num_pts,data_offset);
  printk(KERN_NOTICE "DMA VMA close.\n");
  return;
}



static struct vm_operations_struct dma_remap_ops = {
  .open =  dma_vma_open,
  .close = dma_vma_close,
};

// DEFINITION OF MMAP FUNCTION PASSED TO THE FILE_OPERATIONS STRUCTRE
static int dma_mmap(struct file *filp, struct vm_area_struct *vma)
{
  vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    
  if ( iminor (filp->f_path.dentry->d_inode) == DMA_WRITE) {
    // size +=  vma->vm_end - vma->vm_start;
    if (remap_pfn_range(vma, vma->vm_start, (WRITEBUF_BASEADDR/*+size*/+(vma->vm_pgoff<<PAGE_SHIFT)) >> PAGE_SHIFT, vma->vm_end - vma->vm_start, vma->vm_page_prot))
      return -EAGAIN;
  }
  else {
    if (remap_pfn_range(vma, vma->vm_start, (READBUF_BASEADDR/*+size*/+(vma->vm_pgoff<<PAGE_SHIFT)) >> PAGE_SHIFT, vma->vm_end - vma->vm_start, vma->vm_page_prot))
      return -EAGAIN;
  }
  // run mmap_open sys call for error checking mostly
  vma->vm_ops = &dma_remap_ops;
  dma_vma_open(vma);
  return 0;
}


int dma_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
  //kmeans_arg_t km;
  int ctr_size;
  int ret;
  init_t init;

  // AXI_DMA VARIABLES TO WRITE TO AXI_DMA
  int write_num_of_packets = 0;
  int write_packet_remainder = 0;
  int write_packet_size = MM2S_BURSTSIZE;
  int write_count = 0;

  // AXI_DMA VARIABLES TO READ FROM AXI_DMA
  int read_num_of_packets = 0;
  int read_packet_remainder = 0;
  int read_packet_size = MM2S_BURSTSIZE;
  int read_count = 0;
  int read_len = 0;

  switch (cmd){
  case CENTRE_UPDATE:
    
    if (copy_from_user(&ctr_size, (int *)arg, sizeof(int))){
      return -EACCES;
    }
    //iowrite32(km.num_pts,kmeans_device->mmap+KMEANS_PTS);
    //iowrite32(km.iterations,kmeans_device->mmap+KMEANS_ITERATIONS);
    //iterations = km.iterations;

    printk(KERN_NOTICE "REINITIALIZING THE CENTRES \n");
   
    iowrite32(0x5001,dma_device->dma[1].dma_mm); 
    // UPDATED CENTRES ARE IN READBUF_BASEADDR
    iowrite32(READBUF_BASEADDR/*WRITEBUF_BASEADDR*/,dma_device->dma[1].dma_mm+MM2S_SA);
    //iowrite32(WRITEBUF_BASEADDR,dma_device->dma[1].dma_mm+MM2S_SA);
    iowrite32(ctr_size/*48*/,dma_device->dma[1].dma_mm+MM2S_LENGTH); 

    // wait for the data transfer to finish
    ret = wait_event_interruptible(dma_device->dma[1].mm2s->queue,
				   (dma_device->dma[1].mm2s->done == 1) || 
				   (dma_device->dma[1].mm2s->err == 1));
    
    if (ret){
      return -ERESTARTSYS;
    }

    dma_device->dma[1].mm2s->done = 0;

    break;
    // CALL THIS FUNCTION TO UPDATE THE OFFSET AFTER WRITING NEW DATA POINT
  case INITIALIZE:
    if (copy_from_user(&init, (init_t *)arg, sizeof(init_t))){
      return -EACCES;
    }
    
    data_offset += init.size;
    num_pts += init.num_pts;
    iowrite32(num_pts,kmeans_device->mmap+KMEANS_PTS);
    printk(KERN_NOTICE "SETTING NUM_PTS %lu and SIZE %lu\n", num_pts,data_offset);

    break;
  case GET_OFFSET:
    if (copy_to_user ((int *) arg, &data_offset, sizeof(int))){
	return -EACCES;
      }
    break;
  case CLEAN_UP:
    data_offset = 0;
    num_pts = 0;
    break;
  case AXI_DMA_WR:
    if ( iminor (f->f_path.dentry->d_inode) == DMA_WRITE){
      printk(KERN_INFO "Write buffer length %lu \n", data_offset);
      
      write_packet_size = MM2S_BURSTSIZE;
      write_num_of_packets = data_offset/MM2S_BURSTSIZE;
      write_packet_remainder = data_offset % MM2S_BURSTSIZE;
      write_count = 0;

      // increase num_of_packet if there's less than a packet data
      if (write_packet_remainder)
	write_num_of_packets++; 
      
      printk(KERN_INFO "packets write: %d remainder %d \n", write_num_of_packets, write_packet_remainder);
      
      // in case the data to be sent is less than a packet size
      if (write_num_of_packets == 1 && write_packet_remainder !=0)
	write_packet_size = write_packet_remainder;
    
      while (write_num_of_packets != 0){

     

	// configure the dma device for MM2S channel
	// enable dma plus IOC and IERR
	// provide write address
	// provide length
	iowrite32(0x5001,dma_device->dma[1].dma_mm); 
	iowrite32(WRITEBUF_BASEADDR+write_count*MM2S_BURSTSIZE,dma_device->dma[1].dma_mm+MM2S_SA); 
	iowrite32(write_packet_size,dma_device->dma[1].dma_mm+MM2S_LENGTH); 
	printk(KERN_INFO "packets size: %d \n", write_packet_size);
	write_num_of_packets--;
	write_count++;
      
      
	if (write_num_of_packets == 1){
	  if(write_packet_remainder !=0 )
	    write_packet_size = write_packet_remainder;
	}
    
	// wait for the data transfer to finish
	ret = wait_event_interruptible(dma_device->dma[1].mm2s->queue,
				       (dma_device->dma[1].mm2s->done == 1) || 
				       (dma_device->dma[1].mm2s->err == 1));
      
	if (ret){
	  return -ERESTARTSYS;
	}

	// set the flag back to 0 for the next transfer
	// reduece num of packet and increase count
	dma_device->dma[1].mm2s->done = 0;

     	//kmeans_device->ready = 0;
      }
      //write_count = 0;
    }
    break;
  case AXI_DMA_RD:
    
    if ( iminor (f->f_path.dentry->d_inode) == DMA_READ){
      if (copy_from_user(&read_len, (int *)arg, sizeof(int))){
	return -EACCES;
      }
      read_packet_size = S2MM_BURSTSIZE;
      read_num_of_packets = read_len / S2MM_BURSTSIZE;
      read_packet_remainder = read_len % S2MM_BURSTSIZE;
      read_count = 0;
      printk(KERN_INFO "packets read: %d remainder %d \n", read_num_of_packets, read_packet_remainder);
      if (read_packet_remainder)
	read_num_of_packets++;
    
      // SPECIAL CASE WHERE THERE IS ONLY ONE PACKET AND THE SIZE IS LESS THAN A FULL PACKET
      if (read_num_of_packets == 1 && read_packet_remainder !=0)
	read_packet_size = read_packet_remainder;
    
      // READ THE FIRST PACKET
      iowrite32(0x5001,dma_device->dma[1].dma_mm+S2MM_DMACR);
      iowrite32(READBUF_BASEADDR+read_count*S2MM_BURSTSIZE,dma_device->dma[1].dma_mm+S2MM_DA);
      iowrite32(read_packet_size,dma_device->dma[1].dma_mm+S2MM_LENGTH);

      while (read_num_of_packets != 0 ){

	read_num_of_packets--;


	ret = wait_event_interruptible(dma_device->dma[1].s2mm->queue,
				       (dma_device->dma[1].s2mm->done == 1) || 
				       (dma_device->dma[1].s2mm->err == 1));
	if (ret){
	  return -ERESTARTSYS;
	}
    
	// set the flag back to 0 for the next transfer
	dma_device->dma[1].s2mm->done = 0;
	if (read_num_of_packets == 0)
	  break;

	read_count++;
   
	// configure S2MM channel of DMA
	// set the read base address
	// set the size of the transfer
	iowrite32(0x5001,dma_device->dma[1].dma_mm+S2MM_DMACR);
	iowrite32(READBUF_BASEADDR+read_count*S2MM_BURSTSIZE,dma_device->dma[1].dma_mm+S2MM_DA);
	iowrite32(read_packet_size,dma_device->dma[1].dma_mm+S2MM_LENGTH);
	//getrawmonotonic(&start);
    
	if (read_num_of_packets == 1){
	  if(read_packet_remainder !=0 )
	    read_packet_size = read_packet_remainder;
	}
    
      }
    }
    break;
  default:
    return -EINVAL;

  }


  return 0;
}

static struct file_operations dma_fops =
  {
    .owner = THIS_MODULE,
    .open = dma_open,
    .release = dma_close,
    .read = dma_read,
    .write = dma_write,
    .mmap = dma_mmap,
    .unlocked_ioctl = dma_ioctl
  };

static void dma_exit(void){


  // FREE INTERRUPT LINES
  free_irq(MM2S_IRQ,NULL);
  free_irq(S2MM_IRQ,NULL);
  free_irq(KMEANS_IRQ,NULL);

  // UNMAP MEMORY MAPPED REGIONS
  if (kmeans_device->mmap != NULL)
    iounmap(kmeans_device->mmap);
  if (dma_device->dma[1].dma_mm != NULL)
    iounmap(dma_device->dma[1].dma_mm);
  if (dma_device->dma[1].mm2s->buf != NULL)
    iounmap(dma_device->dma[1].mm2s->buf);
  if (dma_device->dma[1].s2mm->buf != NULL)
    iounmap(dma_device->dma[1].s2mm->buf);
  if (ethernet_device != NULL)
    iounmap(ethernet_device);
  if (trans_device != NULL)
    iounmap(trans_device);
  if (recv_device != NULL)
    iounmap(recv_device);

  if (dma_device)
    cdev_del (&dma_device->c_dev);
  
  // DESTROY DEVVICES
  device_destroy(dma_device->cl,dt);
  device_destroy(dma_device->cl, MKDEV(MAJOR(dt),MINOR(dt)+1));

  class_destroy(dma_device->cl);
  
  // FREE THE DYNAMICALLY ALLOCATED VARIABLES
  if (kmeans_device)
    kfree(kmeans_device);
  if (dma_device->dma[1].mm2s)
    kfree(dma_device->dma[1].mm2s);
  if (dma_device->dma[1].s2mm)
    kfree(dma_device->dma[1].s2mm);
  if (dma_device->dma)
    kfree(dma_device->dma);
  if(dma_device) // if dynamic allocation of memory for device was successful 
    kfree(dma_device); 
  
  unregister_chrdev_region(dt,2);

  printk(KERN_ALERT "Unregister dma driver\n");

}
// aux function
void PhyWrite(unsigned int BaseAddress, unsigned int PhyAddress,	unsigned int RegisterNum, u16 PhyData)
{
	unsigned int MdioCtrlReg = 0;

	/*
	 * Wait till the MDIO interface is ready to accept a new transaction.
	 */
	while (!(ioread32(BaseAddress+XAE_MDIO_MCR_OFFSET) & XAE_MDIO_MCR_READY_MASK)) {
		;
	}

	MdioCtrlReg =   ((PhyAddress << XAE_MDIO_MCR_PHYAD_SHIFT) &
			XAE_MDIO_MCR_PHYAD_MASK) |
			((RegisterNum << XAE_MDIO_MCR_REGAD_SHIFT) &
			XAE_MDIO_MCR_REGAD_MASK) |
			XAE_MDIO_MCR_INITIATE_MASK |
			XAE_MDIO_MCR_OP_WRITE_MASK;

	iowrite32(PhyData, BaseAddress + XAE_MDIO_MWD_OFFSET);

	iowrite32(MdioCtrlReg, BaseAddress + XAE_MDIO_MCR_OFFSET);

	/*
	 * Wait till the MDIO interface is ready to accept a new transaction.
	 */
	while (!(ioread32(BaseAddress + XAE_MDIO_MCR_OFFSET) & XAE_MDIO_MCR_READY_MASK)) {
		;
	}

}

// aux function
void PhyRead(unsigned int BaseAddress, unsigned int PhyAddress, unsigned int RegisterNum, u16 *PhyDataPtr)
{
	unsigned int  MdioCtrlReg = 0;

	/*
	 * Wait till MDIO interface is ready to accept a new transaction.
	 */
	while (!(ioread32(BaseAddress+XAE_MDIO_MCR_OFFSET) & XAE_MDIO_MCR_READY_MASK));
	
	
	printk(KERN_ALERT "IN PHYWRITE, FIRST WHILE LOOP PASSED\n" );

	MdioCtrlReg =   ((PhyAddress << XAE_MDIO_MCR_PHYAD_SHIFT) &
			XAE_MDIO_MCR_PHYAD_MASK) |
			((RegisterNum << XAE_MDIO_MCR_REGAD_SHIFT)
			& XAE_MDIO_MCR_REGAD_MASK) |
			XAE_MDIO_MCR_INITIATE_MASK |
			XAE_MDIO_MCR_OP_READ_MASK;

	iowrite32(MdioCtrlReg, BaseAddress + XAE_MDIO_MCR_OFFSET);


	/*
	 * Wait till MDIO transaction is completed.
	 */
	while (!(ioread32(BaseAddress + XAE_MDIO_MCR_OFFSET) & XAE_MDIO_MCR_READY_MASK));



	/* Read data */
	*PhyDataPtr = (unsigned short) ioread32(BaseAddress+XAE_MDIO_MRD_OFFSET);

}


/*
 * Write Source MAC address to Transmitter internal register
 */
void InitTransceiver (char address[6])
{

  	unsigned int MacAddr = 0;
	unsigned int TimeoutLoops = 10000;
	unsigned int Reg = 0;
	unsigned int RegRcw1 = 0;
	unsigned int RegTc = 0;
	unsigned int PhyReg = 0;
	unsigned int PhyAddr = 1;


	printk(KERN_ALERT "XAE_IS_OFFSET VALUE = %x\n" , ioread32(ethernet_device+XAE_IS_OFFSET));

	// reset receiver
	iowrite32(0xA5,recv_device+RX_RST);
	// reset transmitter
	iowrite32(0xA5,trans_device+TX_RST);
	// Write to hardware
	MacAddr = address[0] << 8;
	MacAddr |= address[1];
	//AXI_ETHERNET_TRANSMITTER_mWriteReg(InstancePtr->BaseAddress, TX_SRC_MAC_UP, MacAddr);
	iowrite32(MacAddr,trans_device+TX_SRC_MAC_UP);
  	MacAddr = address[2] << 24;
	MacAddr |= address[3] << 16;
	MacAddr |= address[4] << 8;
	MacAddr |= address[5];
	iowrite32(MacAddr,trans_device+TX_SRC_MAC_DOWN);
	//AXI_ETHERNET_TRANSMITTER_mWriteReg(InstancePtr->BaseAddress, TX_SRC_MAC_DOWN, MacAddr);
	iowrite32(0x0000000A,trans_device+TX_DST_MAC_UP);
	iowrite32(0x35000000,trans_device+TX_DST_MAC_DOWN);

	printk(KERN_ALERT "PASSED TRANSMITTER AND RECEIVER INITIALIZATION\n");
	// INITIALIZING AXI ETHERNET BLOCK

	while (TimeoutLoops > 0) {
	  TimeoutLoops--;
	}
	TimeoutLoops = 10000;
	while (TimeoutLoops && (! (ioread32(ethernet_device+XAE_IS_OFFSET) & XAE_INT_MGTRDY_MASK))){
	  TimeoutLoops--;
	}

	// RESET ETHERNET / INIT HW
	// Disable the receiver
	Reg = ioread32(ethernet_device + XAE_RCW1_OFFSET);
	Reg &= ~XAE_RCW1_RX_MASK;
	iowrite32(Reg, ethernet_device + XAE_RCW1_OFFSET);

	// Clear interrupt
	Reg = ioread32(ethernet_device +  XAE_IP_OFFSET);
	if (Reg & XAE_INT_RXRJECT_MASK) {
		iowrite32(XAE_INT_RXRJECT_MASK, ethernet_device + XAE_IS_OFFSET);
		}
	// Set/Clear Options
	RegRcw1 = ioread32(ethernet_device +  XAE_RCW1_OFFSET);
	RegTc = ioread32(ethernet_device +   XAE_TC_OFFSET);
	RegRcw1 &= ~XAE_RCW1_FCS_MASK;
	RegTc &= ~XAE_TC_FCS_MASK;
	RegTc |= XAE_TC_TX_MASK;
	RegRcw1 |= XAE_RCW1_RX_MASK;
	RegTc &= ~XAE_TC_JUM_MASK;
	RegRcw1 &= ~XAE_RCW1_JUM_MASK;
	RegTc &= ~XAE_TC_VLAN_MASK;
	RegRcw1 &= ~XAE_RCW1_VLAN_MASK;
	RegRcw1 |= XAE_RCW1_LT_DIS_MASK;
	iowrite32(RegTc, ethernet_device + XAE_TC_OFFSET);
	iowrite32(RegRcw1, ethernet_device + XAE_RCW1_OFFSET);

	// set Phy Mdio Divisor
	/**
	* XAxiEthernet_PhySetMdioDivisor sets the MDIO clock divisor in the
	* Axi Ethernet,specified by <i>InstancePtr</i> to the value, <i>Divisor</i>.
	* This function must be called once after each reset prior to accessing
	* MII PHY registers.
	*
	* From the Virtex-6(TM) and Spartan-6 (TM) Embedded Tri-Mode Ethernet
	* MAC User's Guide, the following equation governs the MDIO clock to the PHY:
	*
	* <pre>
	* 			f[HOSTCLK]
	*	f[MDC] = -----------------------
	*			(1 + Divisor) * 2
	* </pre>
	*
	* where f[HOSTCLK] is the bus clock frequency in MHz, and f[MDC] is the
	* MDIO clock frequency in MHz to the PHY. Typically, f[MDC] should not
	* exceed 2.5 MHz. Some PHYs can tolerate faster speeds which means faster
	* access.*/
	iowrite32(29| XAE_MDIO_MC_MDIOEN_MASK, ethernet_device + XAE_MDIO_MC_OFFSET);


	// setting the mac address
	MacAddr = address[0];
	MacAddr |= address[1] << 8;
	MacAddr |= address[2] << 16;
	MacAddr |= address[3] << 24;
	iowrite32(MacAddr,ethernet_device+XAE_UAW0_OFFSET);
	MacAddr = ioread32(ethernet_device+XAE_UAW1_OFFSET);
	MacAddr &= ~XAE_UAW1_UNICASTADDR_MASK;
	MacAddr |= address[4];
	MacAddr |= address[5] << 8;
	iowrite32(MacAddr,ethernet_device+XAE_UAW1_OFFSET);



	printk(KERN_ALERT "PASSED THE ETHERNET INITIALIZATION\n");

	// reject broadcast & multicast
	unsigned int temp = ioread32(ethernet_device) | 0x6;
	iowrite32(temp,ethernet_device);

	// Config PHY
	// PHY = XAE_PHY_TYPE_1000BASE_X , AXIETHERNET_SPEED_1G
	// PHY address
	iowrite32(0x1000000,ethernet_device+XAE_MDIO_MCR_OFFSET);

	// Put the PHY in reset
	PhyWrite(ethernet_device, PhyAddr, PHY_R0_CTRL_REG,	PhyReg | PHY_R0_RESET);

	// delay
	int i = 0;
	TimeoutLoops = 100000000;

	// originally was: sleep 4s
	for(i=0;i<TimeoutLoops;i++);
	for(i=0;i<TimeoutLoops;i++);
	for(i=0;i<TimeoutLoops;i++);
	for(i=0;i<TimeoutLoops;i++);

	//PhyRead(ethernet_device,PhyAddr, PHY_R0_CTRL_REG, &PhyReg);

	printk(KERN_ALERT "I'M HERE\n");

	// configure internal phy

	/* Clear the PHY of any existing bits by zeroing this out */
	PhyReg = 0;
	//PhyRead(ethernet_device, PhyAddr, PHY_R0_CTRL_REG, &PhyReg);

	PhyReg &= (~PHY_R0_ANEG_ENABLE);
	PhyReg &= (~PHY_R0_ISOLATE);
	PhyReg |= PHY_R0_DFT_SPD_1000;

	// originally was: sleep 1s
	for(i=0;i<TimeoutLoops;i++);

	PhyWrite(ethernet_device, PhyAddr, PHY_R0_CTRL_REG, PhyReg);

	// originally was: sleep 1s
	for(i=0;i<TimeoutLoops;i++);

	// Set TEMAC speed
	Reg = ioread32(ethernet_device + XAE_EMMC_OFFSET) & ~XAE_EMMC_LINKSPEED_MASK;
	Reg |= XAE_EMMC_LINKSPD_1000;

	// originally was: sleep 1s
	for(i=0;i<TimeoutLoops;i++);

	PhyWrite(ethernet_device, PhyAddr, PHY_R0_CTRL_REG, PhyReg);

	// originally was: sleep 2s
	for(i=0;i<TimeoutLoops;i++);
	for(i=0;i<TimeoutLoops;i++);

	// clear interrupt
	iowrite32(XAE_INT_RXRJECT_MASK, ethernet_device + XAE_IS_OFFSET);

	// originally was: sleep 1s
	for(i=0;i<TimeoutLoops;i++);
	

}
static int dma_init(void){

  int result; 

  if (alloc_chrdev_region(&dt,0,2,"xilinx_dma") < 0 )
    return -1;
  printk(KERN_ALERT "Register DMA driver with major number %d \n", MAJOR(dt));


  // DYNAMIC MEMORY ALLOCATION FOR DEVICE VARIABLES 
  dma_device = kmalloc (sizeof(struct dma_struct),GFP_KERNEL);
  if (!dma_device){
    result = -ENOMEM;
    goto fail;
  }
  dma_device->dma = kmalloc(NUM_DMA*sizeof(struct dma_dev),GFP_KERNEL);
  if(!dma_device->dma[1].mm2s){
    result = -ENOMEM;
    goto fail;
  }
  dma_device->dma[1].mm2s = kmalloc(sizeof(struct mm2s_dev),GFP_KERNEL);
  if(!dma_device->dma[1].mm2s){
    result = -ENOMEM;
    goto fail;
  }
  dma_device->dma[1].s2mm = kmalloc(sizeof(struct s2mm_dev),GFP_KERNEL);
  if(!dma_device->dma[1].s2mm){
    result = -ENOMEM;
    goto fail;
  }
  kmeans_device = kmalloc (sizeof(struct kmeans_struct),GFP_KERNEL);
  if(!kmeans_device){
    result = -ENOMEM;
    goto fail;
  }

  // CREATE A CLASS TYPE IN /SYS/CLASS
  if ((dma_device->cl = class_create(THIS_MODULE, "xilinx_dma")) == NULL){
    kfree(dma_device);
    unregister_chrdev_region(dt, 2);
    return -1;
  }

  // CReATE THE DEVICES in /DEV DIRECTORY 
  if (device_create(dma_device->cl, NULL, dt, NULL, "xil_dma_read") == NULL){
    class_destroy(dma_device->cl);
    kfree(dma_device);
    unregister_chrdev_region(dt, 2);
    return -1;
  }
  if (device_create(dma_device->cl, NULL, MKDEV(MAJOR(dt),MINOR(dt)+1), NULL, "xil_dma_write") == NULL){
    class_destroy(dma_device->cl);
    kfree(dma_device);
    unregister_chrdev_region(dt, 2);
    return -1;
  }


  // INITIALIZE CHAR DEVICE IN /PROC/DEVICES
  cdev_init(&dma_device->c_dev,&dma_fops);
  if (cdev_add(&dma_device->c_dev,dt,2) == -1 ){
    result = -1;
    goto fail;

  }

  // IOMAP DMA_MM2S,DMA_S2MM, KMEANS BUFFERS
  if ((dma_device->dma[1].dma_mm = ioremap(DMA_BASEADDR,DMA_SIZE)) == NULL){
    printk(KERN_ERR "Mapping DMA ADDRESS failed\n");
    result =  -1;
    goto fail;
  }
  if ((dma_device->dma[1].mm2s->buf = ioremap(WRITEBUF_BASEADDR,WRITEBUF_SIZE)) == NULL){
    printk(KERN_ERR "Mapping READ BUFFER failed\n");
    result =  -1;
    goto fail;
  }
  if ((dma_device->dma[1].s2mm->buf = ioremap(READBUF_BASEADDR,READBUF_SIZE)) == NULL){
    printk(KERN_ERR "Mapping WRITE BUFFER failed\n");
    result =  -1;
    goto fail;
  }
  
  if ((kmeans_device->mmap = ioremap(KMEANS_BASEADDR,KMEANS_SIZE)) == NULL){
    printk(KERN_ERR "Mapping GPIO ADDRESS failed\n");
    result =  -1;
    goto fail;
  }

  if ((ethernet_device = ioremap(ETHERNET_BASEADDR,ETHERNET_SIZE)) == NULL){
    printk(KERN_ERR "Mapping ETHERNET ADDRESS failed\n");
    result =  -1;
    goto fail;
  }

 if ((trans_device = ioremap(TRANS_BASEADDR,TRANS_SIZE)) == NULL){
    printk(KERN_ERR "Mapping ETHERNET ADDRESS failed\n");
    result =  -1;
    goto fail;
  }

 if ((recv_device = ioremap(RECV_BASEADDR,RECV_SIZE)) == NULL){
    printk(KERN_ERR "Mapping ETHERNET ADDRESS failed\n");
    result =  -1;
    goto fail;
  }
 
  // REGISTER INTERRUPT LINES AS RISING TRIGGER INTERRUPT
  if((request_irq(MM2S_IRQ,mm2s_interrupt,IRQF_TRIGGER_RISING,"MM2S_IRQ",NULL))){
    result = -EBUSY;
    goto fail;

  }
  if((request_irq(S2MM_IRQ,s2mm_interrupt,IRQF_TRIGGER_RISING,"S2MM_IRQ",NULL))){
    result = -EBUSY;
    goto fail;

  }
  if((request_irq(KMEANS_IRQ,kmeans_interrupt,IRQF_TRIGGER_RISING,"KMEANS_IRQ",NULL))){
    result = -EBUSY;
    goto fail;
  }
  // INITIALIZE WAIT QUEUE FOR MM2S S2MM and KMEANS INTERRUPTS
  init_waitqueue_head(&dma_device->dma[1].mm2s->queue);
  init_waitqueue_head(&dma_device->dma[1].s2mm->queue);
  init_waitqueue_head(&kmeans_device->queue);

  iowrite32(0x10007,dma_device->dma[1].dma_mm); // soft reset the channel
  iowrite32(0x10007,dma_device->dma[1].dma_mm+S2MM_DMACR); // soft reset the channel

  // INITIALIZING TRANSMITTER
  InitTransceiver(AxiEthernetMAC);

  return 0;



 fail:
  dma_exit();
  return result;
}

module_init(dma_init);
module_exit(dma_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Ehsan Ghasemi");
MODULE_DESCRIPTION("DMA DRIVER USING MMAP");
