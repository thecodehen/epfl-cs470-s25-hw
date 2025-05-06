#!/bin/bash

rm hw2.zip
zip -r hw2.zip ./ -x "__pycache__/*" "build/*" "tests/*" zip.sh .DS_Store
