obj-m +=xilinx_spark.o
KDIR = ~/Zynq_Mini_ITX/linux/Software/linux-xlnx


all:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	rm -rf *.o *.ko *.mod.* *.symvers *.order *.*~ *~
