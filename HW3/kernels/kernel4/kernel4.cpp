#include "kernel4.h"

void kernel4(int array[ARRAY_SIZE], int index[ARRAY_SIZE], int offset)
{
    int tmp = 0;
    for (int i=offset+1; i<ARRAY_SIZE-1; ++i)
    {
#pragma HLS PIPELINE
        tmp = tmp + index[i] * (array[i+1] - array[i]);
    }
    array[offset] = array[offset] + tmp;
}