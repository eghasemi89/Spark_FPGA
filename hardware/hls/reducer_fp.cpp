#include <stdio.h>
#include <stdlib.h>

//#include "reducer.h"
#include "../srcs/mapred_fp.h"


void reducer (k_t key, pt_t pt[DIMENSION], sum_t sum[MEM_DEPTH][DIMENSION], count_t counter[MEM_DEPTH] ){
#pragma HLS ARRAY_RESHAPE variable=sum complete dim=2 reshape
#pragma HLS ARRAY_RESHAPE variable=pt complete dim=1 reshape




	update:for ( int i = 0 ; i < DIMENSION; i++){
    #pragma HLS UNROLL
			sum[(key)][i] += pt[i];

	}


	counter[key] += 1;
}
