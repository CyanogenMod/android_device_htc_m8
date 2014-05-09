#!/sbin/sh

set -e

modelid=`getprop ro.boot.mid`

case $modelid in
    "0P6B20000") variant="vzw" ;;
    "0P6B70000") variant="spr" ;;
    *)           variant="gsm" ;;
esac

basedir="/system/blobs/$variant/"
chmod 755 $basedir/bin/*
chmod 644 $basedir/vendor/lib/*
find $basedir -type f -print0 | while read file; do ln -s $basedir$file /system/$file ; done
