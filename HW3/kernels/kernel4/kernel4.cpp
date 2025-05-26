#include "kernel4.h"

#define PARTITION_FACTOR 4
void kernel4(int array[ARRAY_SIZE], int index[ARRAY_SIZE], int offset)
{
#pragma HLS ARRAY_PARTITION variable=array type=cyclic factor=PARTITION_FACTOR
#pragma HLS ARRAY_PARTITION variable=index type=cyclic factor=PARTITION_FACTOR
    int tmp = 0;
    for (int i=offset+1; i<ARRAY_SIZE-1; ++i)
    {
#pragma HLS PIPELINE
#pragma HLS UNROLL factor=PARTITION_FACTOR
        tmp = tmp + index[i] * (array[i+1] - array[i]);
    }
    array[offset] = array[offset] + tmp;
}
