#!/bin/sh

set -e

if [ $# -eq 0 ]; then
  SRC=adb
else
  if [ $# -eq 1 ]; then
    SRC=$1
  else
    echo "$0: bad number of arguments"
    echo ""
    echo "usage: $0 [PATH_TO_EXPANDED_ROM]"
    echo ""
    echo "If PATH_TO_EXPANDED_ROM is not specified, blobs will be extracted from"
    echo "the device using adb pull."
    exit 1
  fi
fi

BASE=../../../vendor/$VENDOR/$DEVICE/proprietary
rm -rf $BASE/*

for FILE in `egrep -v '(^#|^$)' ../$DEVICE/device-proprietary-files.txt`; do
  echo "Extracting /system/$FILE ..."
  DIR=`dirname $FILE`
  if [ ! -d $BASE/$DIR ]; then
    mkdir -p $BASE/$DIR
  fi
  if [ "$SRC" = "adb" ]; then
    adb pull /system/$FILE $BASE/$FILE
  else
    cp $SRC/system/$FILE $BASE/$FILE
  fi
done

for FILE in `egrep -v '(^#|^$)' ../msm8960-common/common-proprietary-files.txt`; do
  echo "Extracting /system/$FILE ..."
  DIR=`dirname $FILE`
  if [ ! -d $BASE/$DIR ]; then
    mkdir -p $BASE/$DIR
  fi
  if [ "$SRC" = "adb" ]; then
    adb pull /system/$FILE $BASE/$FILE
  else
    cp $SRC/system/$FILE $BASE/$FILE
  fi
done

./../msm8960-common/setup-makefiles.sh
