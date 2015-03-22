#!/sbin/sh

set -e

# Detect variant and create symlinks to its specific-blobs
modelid=`getprop ro.boot.mid`

case $modelid in
    "0P6B20000") variant="vzw" ;;
    "0P6B70000") variant="spr" ;;
    "0P6B13000") variant="gsm"; tmo="true" ;;
    *)           variant="gsm" ;;
esac

basedir="/system/blobs/$variant/"
cd $basedir
chmod 755 bin/*
find . -type f | while read file; do ln -s $basedir$file /system/$file ; done


# Create modem firmware links based on the currently installed modem
mkdir -p /firmware/radio
busybox mount -o shortname=lower -t vfat /dev/block/platform/msm_sdcc.1/by-name/radio /firmware/radio

# Prefer an mba.* pair if one exists
# If not, find the a7b* pair with the highest "number" (unless you're T-Mo)
if [ "$tmo" == "true" ] && [ -f "/firmware/radio/a7b80e1.mdt" ]; then
  base="/firmware/radio/a7b80e1"
elif [ -f "/firmware/radio/mba.mdt" ]; then
  base="/firmware/radio/mba"
elif ls /firmware/radio/a7b*.mdt 1> /dev/null 2>&1; then
  base=`ls /firmware/radio/a7b*.mdt | sort -r | head -n 1 | sed "s|.mdt||g"`
fi

ln -s $base.mdt /system/vendor/firmware/mba.mdt
ln -s $base.b00 /system/vendor/firmware/mba.b00

if [ ! -f "/system/vendor/firmware/mba.mdt" ]; then
  exit 1
fi

busybox umount /firmware/radio
exit 0
