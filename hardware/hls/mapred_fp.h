#ifndef __MAPRED_FP_H__
#define __MAPRED_FP_H__

#include <cmath>
#include "ap_fixed.h"
#include "ap_int.h"
//using namespace std;


#define DIMENSION 4
#define K 30
#define NUM_PTS 100
#define MEM_DEPTH 8
#define NUM_REDUCERS 8 /* when this value is changed the factor variable in
 	 	 	 	 	 	  merger.cpp source file for partitioning sum array
 	 	 	 	 	 	  also needs to be changed manually                */
#define MEM_SIZE MEM_DEPTH*NUM_REDUCERS
#define sqrd(x) (x)*(x)

// MAPPER
typedef float ctr_t;
typedef float pt_t;
typedef unsigned short k_t; //key type


// REDUCER
typedef unsigned int count_t;
typedef float sum_t;
typedef ap_uint<NUM_REDUCERS> req_t;
// Prototype of top level function for C-synthesis

k_t mapper (pt_t pt[DIMENSION], ctr_t centres[K][DIMENSION]);
void partitioner (k_t *key_in, req_t *request, k_t *key_out);
void reducer (k_t key, pt_t pt[DIMENSION], sum_t sum[MEM_DEPTH][DIMENSION], count_t counter[MEM_DEPTH]);
void merger(sum_t sum[MEM_SIZE][DIMENSION], count_t counter[K],ctr_t centres[K][DIMENSION]);



#endif // __MATRIXMUL_H__ not defined
