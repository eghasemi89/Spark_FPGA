#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define BILLION 1000000000L


int main (int argc, char *argv[]){

  if (argc <= 1){
    printf("please input the size of the buffer\n");
    return 1;
  }
  else{
    int size = atoi(argv[1]);
    FILE *fp;
    //char  buff[size];// = "aaaaaaaaa\n";
    int i = 0;
    struct timespec start,stop;

    unsigned long diff;
    double time_spent;
    double throughput;

    char *buff = (char *) malloc(size*sizeof(char));  
    for (i = 0 ; i< size ; i++){
      buff[i] = i;
    }


    fp = fopen("/dev/xil_dma_write","w");
  
    if (fp == NULL){

      printf("error opening the file\n");
      return -1;
    }
   
    clock_gettime(CLOCK_MONOTONIC,&start);
    fwrite(buff,sizeof(char),size,fp);
    clock_gettime(CLOCK_MONOTONIC,&stop);
    
    diff = BILLION * (stop.tv_sec - start.tv_sec) + stop.tv_nsec - start.tv_nsec;
    time_spent = (double) diff / BILLION ;

    throughput = (double)size/time_spent ;
    printf ("Throuput = %f Bytes/Sec\n",throughput);
    throughput = throughput / (1024.0*1024.0);
    printf ("Throuput = %f MB/Sec\n",throughput);

    fclose(fp);
    free(buff);
    return 0;
  }
}
