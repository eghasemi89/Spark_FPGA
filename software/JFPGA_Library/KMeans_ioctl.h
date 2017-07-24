#ifndef KMEANS_IOCTL_H
#define KMEANS_IOCTL_H

#include <linux/ioctl.h>

typedef struct {

  int iterations, num_pts, init_size;
} kmeans_arg_t;

typedef struct {
  unsigned long size;
  unsigned long num_pts;

} init_t;

#define IOCTL_MAGIC_NUM 'q'

#define CENTRE_UPDATE _IOR('q',1, int *)

#define INITIALIZE  _IOR('q',2, init_t *)

#define CLEAN_UP _IO('q',3)

#define AXI_DMA_WR _IO('q',4)

#define AXI_DMA_RD _IOR('q',5,int *)


#endif
