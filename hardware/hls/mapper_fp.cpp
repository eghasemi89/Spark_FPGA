#include "../srcs/mapred_fp.h"
#include "hls_math.h"
#include "limits"


/*
 *
 *
 * pt:
 * 	 pass by value making the array inside the structure
 * 	 to be partitioned into separate elements
 * centres:
 * 	 array that will be flattened in order to have direct
 * 	 access to all the centres at once
 *
 */
k_t mapper (pt_t pt[DIMENSION], ctr_t centres[K][DIMENSION]/*, KV_t * ret*/){
#pragma HLS ARRAY_RESHAPE variable=pt dim=1
#pragma HLS ARRAY_RESHAPE variable=centres dim=2

	unsigned short min_index;
	float min_dist = std::numeric_limits<float>::max();
	float total_dist = 0.0f;
	//unsigned int cntr;
	//ap_uint<32> cntr;
	//ap_uint<16> data_pt;
	//KV_t ret;wer
	//kv_t var;

	clusters: for (int i = 0; i < K; i++ ){
#pragma HLS PIPELINE
		dimensions: for ( int j = 0; j < DIMENSION; j++){
 #pragma HLS UNROLL

				//total_dist += sqrd((float)centres[i]>>j - (float) pt>>j);
				//if (centres[i][j] > pt [j])
				    total_dist += sqrd(centres[i][j] - pt[j]);
				//else
					//total_dist += pt[j] - centres[i][j] ;
		}

		if 	(total_dist < min_dist){
			min_dist = total_dist;
			min_index = i;

		}
		total_dist = 0;
	}
	//ret->key = min_index;
	//ret->value = pt;
	return min_index;
	//return ret;
}
