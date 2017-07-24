#include <stdio.h>
#include <stdlib.h>

#include "../srcs/mapred.h"


// key_in the index of the cluster centre
// key_out the memory index inside the reducer for reducer use
// request is the request bits of the reducers for arbiter use
void partitioner (k_t *key_in, req_t *request,  k_t *key_out){

	// NEED TO ALSO OUTPUT THE MEMORY INDEX FOR THE REDUCER
	// *KEY % NUM_OF_MEMORY_INDEXS
	*request =  1 << (*key_in % NUM_REDUCERS);
	*key_out = *key_in / NUM_REDUCERS;
	//	return *key;
	// out = 1 << *key % NUM_REDUCERS;
}
