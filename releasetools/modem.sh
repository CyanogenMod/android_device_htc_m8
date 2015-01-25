#!/sbin/sh

set -e

mkdir -p /firmware/radio
busybox mount -o shortname=lower -t vfat /dev/block/platform/msm_sdcc.1/by-name/radio /firmware/radio

if [ -f "/firmware/radio/mba.mdt" ]; then
  ln -s /firmware/radio/mba.mdt /system/vendor/firmware/mba.mdt
  ln -s /firmware/radio/mba.b00 /system/vendor/firmware/mba.b00
elif [ -f "/firmware/radio/a7b80e1.mdt" ]; then
  ln -s /firmware/radio/a7b80e1.mdt /system/vendor/firmware/mba.mdt
  ln -s /firmware/radio/a7b80e1.b00 /system/vendor/firmware/mba.b00
fi

if [ ! -f "/system/vendor/firmware/mba.mdt" ]; then
  echo "ERROR: Could not find a valid firmware image to symlink. Please ensure you have a vaild baseband."
  exit 1
fi

busybox umount /firmware/radio
exit 0
