#!/bin/bash

rm hw1.zip
zip -r hw1.zip ./ -x "build/*" "tests/*" zip.sh .DS_Store
