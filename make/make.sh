#!/bin/bash

#export PATH="/home/molson5/.local/bin:$PATH"
#export LD_LIBRARY_PATH="/home/molson5/.local/lib:/home/molson5/.local/lib64:$LD_LIBRARY_PATH"

export JAVA_HOME=/opt/jdk1.9.0
export ALT_BOOTDIR=/opt/jdk1.9.0
export ALT_JAVA_HOME=/opt/jdk1.9.0
export HOTSPOT_BUILD_JOBS=8
export ARCH_DATA_MODEL=64

#make clean
make objprofile2 #debug coloredtlabs optimized coloredtlabs objaddr objprofile2
