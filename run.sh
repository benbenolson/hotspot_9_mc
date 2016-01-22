#!/bin/bash

set -x
export JAVA_HOME=/opt/jdk1.9.0
export ALT_BOOTDIR=/opt/jdk1.9.0
export ALT_JAVA_HOME=/opt/jdk1.9.0
export ALT_JDK_IMPORT_PATH=/opt/jdk1.9.0
export HOTSPOT_BUILD_JOBS=8
export ARCH_DATA_MODEL=64

DEBUG="build/linux/linux_amd64_compiler2/debug/hotspot"
OPTIMIZED="build/linux/linux_amd64_compiler2/optimized/hotspot"
COLOREDTLABS="build/linux/linux_amd64_compiler2/coloredtlabs/hotspot"
OBJADDR="build/linux/linux_amd64_compiler2/objaddr/hotspot"

numactl --cpunodebind=1 --preferred=1 sudo -E /home/molson5/projects/memcolor/hotspot_9_mc/build/linux/linux_amd64_compiler2/coloredtlabs/hotspot -XX:+UseParallelGC -XX:ParallelGCThreads=1 -XX:+PrintHeapAtGC -XX:+PrintGCDetails -XX:-UseAdaptiveSizePolicy -XX:+DisableMajorGC -XX:+PreventTenuring -XX:+GCTimers -XX:+CompileSlowAllocations -XX:+SlowAllocations -XX:TLABSize=32768 -XX:-ResizeTLAB -XX:+UseColoredSpaces -XX:+ColorObjectAllocations -XX:+RedMemoryInterleave -XX:RedMemoryTrays=4 -XX:BlueMemoryTrays=5-7 -XX:UnknownAPColor=0 -XX:UnknownObjectColor=0 -XX:NewSize=66571993088 -Xms90g -Xmx90g -XX:CompileCommandFile=/home/molson5/projects/memcolor/hotspot_9_mc/results/StoreBench-d300/membench_color/.apc-cmds.txt -classpath /home/molson5/projects/memcolor/benchmarks/custom/StoreBench/ StoreBench 12 104 183 2.3125 16.1875
echo $?
