#include "kernel1.h"

#define PARTITION_FACTOR 8
void kernel1( int array[ARRAY_SIZE] )
{
#pragma HLS ARRAY_PARTITION variable=array type=cyclic factor=PARTITION_FACTOR
    int i;
    for(i=0; i<ARRAY_SIZE; i++)
    {
#pragma HLS PIPELINE
#pragma HLS UNROLL factor=PARTITION_FACTOR
        array[i] = array[i] * 5;
    }
}