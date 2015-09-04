#!/bin/bash

export JAVA_HOME=/opt/jdk1.9.0
export ALT_BOOTDIR=/opt/jdk1.9.0
export ALT_JAVA_HOME=/opt/jdk1.9.0
export ALT_JDK_IMPORT_PATH=/opt/jdk1.9.0
export HOTSPOT_BUILD_JOBS=8
export ARCH_DATA_MODEL=64

FAIL=0
TOTAL=0
ITERS=$1
DEBUG="build/linux/linux_amd64_compiler2/debug/hotspot"
OPTIMIZED="build/linux/linux_amd64_compiler2/optimized/hotspot"
COLOREDTLABS="build/linux/linux_amd64_compiler2/coloredtlabs/hotspot"
OBJPROFILE2="build/linux/linux_amd64_compiler2/objprofile2/hotspot"
OBJADDR="build/linux/linux_amd64_compiler2/objaddr/hotspot"

echo "DEBUG:"
for i in $(seq 1 $ITERS);
do
  if eval $DEBUG 2>&1 | grep -q "fatal error"; then
    let FAIL=FAIL+1
    let TOTAL=TOTAL+1
  else
    let TOTAL=TOTAL+1
  fi
done
echo "$FAIL/$TOTAL"

FAIL=0
TOTAL=0

echo "OPTIMIZED:"
for i in $(seq 1 $ITERS);
do
  if eval $OPTIMIZED 2>&1 | grep -q "fatal error"; then
    let FAIL=FAIL+1
    let TOTAL=TOTAL+1
  else
    let TOTAL=TOTAL+1
  fi
done
echo "$FAIL/$TOTAL"

FAIL=0
TOTAL=0

echo "COLOREDTLABS:"
for i in $(seq 1 $ITERS);
do
  if eval $COLOREDTLABS 2>&1 | grep -q "fatal error"; then
    let FAIL=FAIL+1
    let TOTAL=TOTAL+1
  else
    let TOTAL=TOTAL+1
  fi
done
echo "$FAIL/$TOTAL"

FAIL=0
TOTAL=0

echo "OBJPROFILE2:"
for i in $(seq 1 $ITERS);
do
  if eval $OBJPROFILE2 2>&1 | grep -q "fatal error"; then
    let FAIL=FAIL+1
    let TOTAL=TOTAL+1
  else
    let TOTAL=TOTAL+1
  fi
done
echo "$FAIL/$TOTAL"

FAIL=0
TOTAL=0

echo "OBJADDR:"
for i in $(seq 1 $ITERS);
do
  if eval $OBJADDR 2>&1 | grep -q "fatal error"; then
    let FAIL=FAIL+1
    let TOTAL=TOTAL+1
  else
    let TOTAL=TOTAL+1
  fi
done
echo "$FAIL/$TOTAL"
