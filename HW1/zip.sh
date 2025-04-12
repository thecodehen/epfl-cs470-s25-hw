#!/bin/bash

rm hw1.zip
zip -r hw1.zip ./ -x "__pycache__/*" "build/*" "tests/*" zip.sh .DS_Store
