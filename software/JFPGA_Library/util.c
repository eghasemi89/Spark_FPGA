#include <stdlib.h>
#include <jni.h>
#include "util.h"
#include <ctype.h>
#include <string.h>
#include <fcntl.h> // for open/close syscalls
#include <sys/mman.h>
#include "KMeans_ioctl.h"


static void setInt(JNIEnv * env, jobject obj, const char * name, jint value)
{
    jclass cls = (*env)->GetObjectClass(env, obj);
    jfieldID fid = (*env)->GetFieldID(env, cls, name, "I");
    
    (*env)->SetIntField(env, obj, fid, value);
}

static jint getInt(JNIEnv * env, jobject obj, const char * name)
{
    jclass cls = (*env)->GetObjectClass(env, obj);
    jfieldID fid = (*env)->GetFieldID(env, cls, name, "I");
    
    return (*env)->GetIntField(env, obj, fid);
}

static void throwIOException(JNIEnv * env, jobject obj, const char * message)
{
    jclass cls = (*env)->FindClass(env, "java/io/IOException");
    
    (*env)->ThrowNew(env, cls, message);
}

JNIEXPORT jobject JNICALL Java_util_mmap_1write
(JNIEnv * env, jobject obj, jlong len, jlong num_pts){

  init_t init;
  int file = open ("/dev/xil_dma_write", O_RDWR);
  int i;

  if (file < 0 )
    throwIOException(env, obj, "Could not open file for reading.");

  //char *buff  = (char *) malloc(10*sizeof(char));
  //  char buff [10];

  // MEMORY MAP THE BUFFER
  char *buff = mmap(0,len,PROT_READ | PROT_WRITE,MAP_SHARED,file,0);
  //for (i = 0 ; i < 10; i++)
  // buff[i] = i;

  // SETTING THE INITIAL VALUES
  init.size = len;
  init.num_pts = num_pts;
  if (ioctl(file,INITIALIZE ,&init) == -1){
    perror("KMEANS_SET ioctl");
  }

  // PASSING THE BYTEBUFFER TO JAVA
  setInt(env, obj, "fd", (jint)file);
  jobject ret = (*env)->NewDirectByteBuffer(env,/*(void*)*/buff,len/**sizeof(unsigned int)*/);
  
  // for (i = 0 ; i < len; i++)
  // printf("%d \n", buff[i]);
  close (file);
  return ret;

}

JNIEXPORT jobject JNICALL Java_util_mmap_1read
(JNIEnv * env, jobject obj, jint len){

  int file = open ("/dev/xil_dma_read", O_RDWR);

  if (file < 0 )
    throwIOException(env, obj, "Could not open file for reading.");

  //char *buff  = (char *) malloc(10*sizeof(char));
  //  char buff [10];
  char *buff = mmap(0,len,PROT_READ | PROT_WRITE,MAP_SHARED,file,0);
  int i;
  //for (i = 0 ; i < 10; i++)
  // buff[i] = i;
  setInt(env, obj, "fd", (jint)file);
  jobject ret = (*env)->NewDirectByteBuffer(env,/*(void*)*/buff,len/**sizeof(char)*/);
  
  //for (i = 0 ; i < len; i++)
  // printf("%d \n", buff[i]);
  close(file);

  return ret;

}

JNIEXPORT void JNICALL Java_util_write
(JNIEnv * env , jobject obj){
  int fd = open ("/dev/xil_dma_write", O_RDWR);
  
  if (fd < 0 )
    throwIOException(env, obj, "Could not open file for reading.");
  
  if (ioctl(fd,AXI_DMA_WR) == -1){
    perror("KMEANS_SET ioctl");
  }

  close(fd);
}

JNIEXPORT void JNICALL Java_util_read
(JNIEnv * env, jobject obj, jint len){
  int fd = open ("/dev/xil_dma_read", O_RDWR);
  
  if (fd < 0 )
    throwIOException(env, obj, "Could not open file for reading.");
  int size = len;

  if (ioctl(fd,AXI_DMA_RD,&size) == -1){
    perror("KMEANS_SET ioctl");
  }

  
  close(fd);

}

JNIEXPORT void JNICALL Java_util_update_1centres
(JNIEnv * env, jobject obj, jint size){
  
  //int fd = (int) getInt(env,obj,"fd");
  int fd = open ("/dev/xil_dma_read", O_RDWR);
  
  if (fd < 0 )
    throwIOException(env, obj, "Could not open file for reading.");
  
  int ctr_size = size;
  
  if (ioctl(fd,CENTRE_UPDATE,&ctr_size) == -1){
    perror("KMEANS_SET ioctl");
  }

  close(fd);
}

JNIEXPORT jint JNICALL Java_mmap_osclose
(JNIEnv * env, jobject obj){

  int file = (int) getInt(env,obj,"fd");
  int i;
  if (file < 0)
    return 1;
  
  char *buff = mmap(0,10,PROT_READ | PROT_WRITE,MAP_SHARED,file,0);

  for ( i = 0 ; i < 10; i++)
    printf("%d \n", buff[i]);

  close(file);

  setInt(env,obj,"fd",0);
  
  return 0;

}
