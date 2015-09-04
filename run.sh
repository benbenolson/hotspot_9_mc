#!/bin/bash

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

eval $DEBUG
eval $OPTIMIZED
eval $COLOREDTLABS
eval $OBJADDR
