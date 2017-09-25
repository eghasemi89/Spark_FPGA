Apache Spark has become a powerful platform for big data processing over the last few years. This project was an initial attempt to study the integration of FPGAs in Spark platform. To prototype this idea, we created a cluster of Zynq-7000 All Programmable System-On-Chip Mini-ITX Development boards. You could find more details about the project in the following journal paper: 


http://onlinelibrary.wiley.com/doi/10.1002/cpe.4222/epdf?author_access_token=9mzjRGswfcUUxPK1uLN_I04keas67K9QMdWULTWMo8Pa2BEmaT7WXvEstpy4lijVT9ddeSM9OB5fa4uLZUfsibaphXzjEA3k1Jkxmvq5xr4pqYkObsTlYIVen9XC6ITT


The files in this project has been divided into two main sections: hardware and software.

- Hardware files include all the source files related to the design and implementation of the FPGA accelerator. This incorporates the Verilog HDL files for MapReduce custom hardware implementation, in addition to the MapReduce High-Level Synthesis (HLS) kernels to accelerate k-means clustering algorithm, as described in the journal paper.

- Software files include the driver program for Spark platform, The source files to create the JFPGA library, and a character device driver that handles data transfer between software and hardware. The character device driver uses Xilinx AXI DMA IP block to initiate data transfer to and from FPGA. Today, an easier and more generic way of transferring data between CPU and FPGA can be achieved by using Xilinx SDAccel Platform: https://www.xilinx.com/products/design-tools/software-zone/sdaccel.html