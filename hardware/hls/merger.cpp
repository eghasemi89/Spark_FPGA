#include <stdlib.h>
#include <stdio.h>
#include "../srcs/mapred.h"


int num_red = NUM_REDUCERS;

void merger(sum_t sum[MEM_SIZE][DIMENSION], count_t counter[K],ctr_t centres[K][DIMENSION]){
#pragma HLS RESOURCE variable=sum core=ROM_1P_BRAM
//#pragma HLS ARRAY_PARTITION variable=sum cyclic factor=4 dim=1
#pragma HLS ARRAY_RESHAPE variable=centres dim=2
#pragma HLS ARRAY_RESHAPE variable=sum dim=2


	outer_loop: for (int i = 0 ; i < K ; i++){
    #pragma HLS PIPELINE

		inner_loop: for (int j = 0 ; j < DIMENSION; j++){
        #pragma HLS UNROLL
			centres[i][j] = sum[i][j] / counter[i] ;

		}
	}



}
