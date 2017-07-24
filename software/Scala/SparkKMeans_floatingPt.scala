/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
= * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */



import breeze.linalg.{Vector, DenseVector, squaredDistance}
import org.apache.spark.{SparkConf, SparkContext}
import org.apache.spark.SparkContext._

/* LIBRARIES FOR FILE MMAP READ AND WRITE */
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.ByteOrder;

import java.io._
import java.nio.ByteBuffer;
import scala.tools.nsc.io._
import scala.util.control._



class util extends java.io.Serializable {


      var dimension = 4
      var K = 3
      var num_pts = 100
      var iterations = 10
      var fd = 0 // FILE DESCRIPTOR

      @native def initialize () // IOCTL function
      @native def update_centres (len: Int)
      @native def mmap_write (len: Long,num_pts: Long) : ByteBuffer // Open file and pass the buffer
      @native def mmap_read (len: Int) : ByteBuffer // Open file and pass the buffer
      @native def write ()
      @native def read (len: Int)
      @native def cleanup()

      def mmap (rdd: Iterator [breeze.linalg.Vector[Float]] ) /*: Iterator [Int]*/  = {
        System.loadLibrary("util")

	// INITIALIZE VARIABLES
	val K  = this.K
	val dimension = this.dimension
	val pts = rdd.toArray

	// COMPUTE BUFFER SIZE
	val len = pts.size*dimension*4 // 32 is sizeof(Float)

	val write_buff = mmap_write(len,pts.size)
	write_buff.order(ByteOrder.LITTLE_ENDIAN)

		
	for ( pt <- pts)
	    pt.foreach(dim => write_buff.putFloat(dim))
	
	//return Iterator (0)
	// JNI CALL TO CLOSE FILE DESCRIPTOR

      }

      def kmeans (id : Int, centres: Array [Vector[Float]]) : Array[(Int, (Vector[Float],Int))] = {

      // CALCULATE READ SIZE
      val K = this.K
      val dimension  = this.dimension

      // OPEN A FILE CHANNEL FOR READ AND WRITE      

      // The intermediate sums are 32 bits along with K 32-bit value
      // n for the counters for each dimension
      val len = 4*K*dimension+4*K // 4 for 32bits 4 for 32-bits 
      // REINIT THE CENTRES
      val read_buff = mmap_read(len)
      read_buff.order(ByteOrder.LITTLE_ENDIAN)

      centres.foreach(ctr => ctr.foreach( dim => read_buff.putFloat(dim)))

      // INVOKE IOCTL TO UPDATE CENTRES
      update_centres(K*dimension*4) // 4 is sizeofFloat

      // INVOKE THE WRITE FUNCTION OF THE DEVICE DRIVER
      write()
 	 
      // INVOKE THE READ FUNCTION OF THE DEVICE DRIVER
      read(len)
 
      // REWIND BYTEBUFFER TO ORIGINAL POSITION
      read_buff.rewind();
      //read_buff.order(ByteOrder.LITTLE_ENDIAN)

      // PARSE THE OUTPUT
      val ret = new Array[Tuple2[Int,(breeze.linalg.Vector[Float],Int)]] (K)
      var pts =  new Array[breeze.linalg.Vector[Float]] (K)
      var point = new Array[Float] (dimension)
      var matrix = Array.ofDim[Float](K,dimension)
      
      for (i <- 0 until K){
      	  for ( j <- 0 until dimension){
	      matrix(i)(j) = read_buff.getFloat()
	  }
      }	 
      
      for (i <- 0 until K )
	  ret(i) = (i,(new DenseVector(matrix(i)),read_buff.getInt()))
      // JNI TO CLOSE FD

     // 	  println (ret(i))
     ret.foreach(println)
      // RETURN
      return ret
      }
      


}


/**
 * K-means clustering.
 *
 * This is an example implementation for learning how to use Spark-FPGA.
 */
object SparkKMeans {

  def parseVector(line: String): Vector[Float] = {
    DenseVector(line.split(' ').map(_.toFloat))
  }

  def main(args: Array[String]) {

    if (args.length < 5) {
      System.err.println("Usage: SparkKMeans <file> <dim> <k> <nodes> <iterations>  ")
      System.exit(1)
    }

    
    // System.loadLibrary("util")  
    
    val sparkConf = new SparkConf().setAppName("SparkKMeans")
    val sc = new SparkContext(sparkConf)
    val lines = sc.textFile(args(0))
    val pts = lines.map(parseVector _)//.cache()
    val dimension = args(1).toInt // or pts.first().size	
    val K = args(2).toInt
    val slaves = args(3).toInt
    var iteration = args(4).toInt
    val km = new util // new instance of util class
    km.dimension = dimension
    km.K = K

   

    /******
    // CONVERTING THE INPUT VALUES TO 16 BIT FIXEDPOINT FORMAT of F10.6
    // THE VALUES ARE CONVERTED TO CHAR AS IT'S THE UNSIGNED SHORT TYPE IN SCALA
    ******/
    

    val kPoints = pts.take(K).toArray
    val pts_write = pts.foreachPartition(part => km.mmap(part))

    val partitions = (for (i <- 0 until slaves) yield i).toArray
    val nodes = sc.parallelize(partitions,slaves) // N instead if 1
    
    while(iteration > 0) {
      
      val intermed = nodes.flatMap(nd => km.kmeans(nd,kPoints) )
      val pointStats = intermed.reduceByKey{case ((x1, y1), (x2, y2)) => (x1 + x2, y1 + y2)}

      val newPoints = pointStats.map {pair => (pair._1, pair._2._1 / pair._2._2.toFloat)}.collectAsMap()

      val points = Array.ofDim[Float](K,dimension)
         
      for (newP <- newPoints) {
      	for ( j <- 0 until dimension){
	    points(newP._1)(j) = newP._2(j)
	}
        kPoints(newP._1) = new DenseVector(points(newP._1))
      }
      
      iteration = iteration - 1
      println("Finished iteration " + iteration )
    }
    
    println("Final centers:")

     var final_centres = new Array[Float] (dimension)

     for (newP <- kPoints) {
      for ( i <- 0 until dimension){
	  final_centres(i) = newP(i)

	  print(final_centres(i) + " ")
      }
      println("")
    
    }
 

    
    sc.stop()
  }
}
