#include "kernel2.h"

void kernel2( int array[ARRAY_SIZE] )
{
    int i;
    int a = array[0], b = array[1], c = array[2];
    int tmp = a * b, res;
    for(i=3; i<ARRAY_SIZE; i++)
    {
#pragma HLS PIPELINE
        res = c + tmp;
        array[i] = res;
        tmp = b * c;
        b = c;
        c = res;
    }
}