#!/bin/bash

source /opt/Xilinx/Vitis_HLS/2022.2/settings64.sh

vitis_hls run_hls.tcl

# replace the number with the kernel number you are working on
vitis_hls -p prj_kernel1
