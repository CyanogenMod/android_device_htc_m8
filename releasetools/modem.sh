#!/sbin/sh

set -e

mkdir -p /firmware/radio
busybox mount -o shortname=lower -t vfat /dev/block/platform/msm_sdcc.1/by-name/radio /firmware/radio

radiover=`cat /firmware/radio/radiover.cfg | sed 's/SSD://g'`

# Supported radios should break here
case $radiover in
    "1.12.20.1205")        break;;  # VZW
    "1.21.213311491.A04G") break;;  # GPE
    *)
        echo "ERROR: Baseband version $radiover is not valid! Please update your baseband."
        exit 1;;
esac

if [ -f "/firmware/radio/mba.mdt" ]; then
  ln -s /firmware/radio/mba.mdt /system/vendor/firmware/mba.mdt
  ln -s /firmware/radio/mba.b00 /system/vendor/firmware/mba.b00
elif [ -f "/firmware/radio/a7b40e1.mdt" ]; then
  ln -s /firmware/radio/a7b40e1.mdt /system/vendor/firmware/mba.mdt
  ln -s /firmware/radio/a7b40e1.b00 /system/vendor/firmware/mba.b00
fi

if [ ! -f "/system/vendor/firmware/mba.mdt" ]; then
  echo "ERROR: Could not find a valid firmware image to symlink. Please ensure you have a supported baseband."
  exit 1
fi

busybox umount /firmware/radio
exit 0
