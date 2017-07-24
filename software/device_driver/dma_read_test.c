#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <stdint.h>

#define BILLION 1000000000L

int main (int argc, char *argv[]){
  
  if (argc <=  1){
    //    printf("please input the size of the buffer \n");
    return 1;
  }
  else {
    
    int size = atoi(argv[1]);
    //FILE *fp;
    char  *buff = (char *) malloc(size*sizeof(char));
    int i = 0;
    int ret = 0;
    int fp;
    clock_t begin, end;
    //uint64_t diff;
    unsigned long diff;
    struct timespec start,stop;
    double time_spent;
    double throughput;
    
    
    fp = open("/dev/xil_dma_read",O_RDONLY,S_IRUSR);
    

    
    if (fp < 0){

      printf("error opening the file");
      return -1;
    }

  
    //begin = clock();
    clock_gettime(CLOCK_MONOTONIC,&start);
    ret = read(fp,buff,size);
    clock_gettime(CLOCK_MONOTONIC,&stop);
    
    diff = BILLION * (stop.tv_sec - start.tv_sec) + stop.tv_nsec - start.tv_nsec;
    //    printf("elapsed time = %llu nanoseconds\n", (long long unsigned int) diff);

    //end = clock();
    if (ret < 0){

      printf("error opening the file");
      return -1;
    }

    time_spent = (double)(end-begin) / CLOCKS_PER_SEC;
    printf ("Execution time = %f Sec\n", time_spent);
    time_spent = (double) diff / BILLION ;
    
    
    throughput = (double)size/time_spent ;
    printf ("Throuput = %f Bytes/Sec\n",throughput);
    throughput = throughput / (1024.0*1024.0);
    printf ("Throuput = %f MB/Sec\n",throughput);

    for (i = 0 ; i< size; i++){

      //printf("index = %d val = %d\n",(char *) &i,buff[i]);
      if (buff[i] != *(char *) &i){
	printf ("data mismatch at index %d buff= %d\n",i,buff[i]);
	return 1;
      }
    }
    close(fp);
  
    free(buff);
    //printf("hello world\n");
    return 0;
  }
}
