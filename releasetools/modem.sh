#!/sbin/sh

set -e

mkdir -p /firmware/radio
busybox mount -o shortname=lower -t vfat /dev/block/platform/msm_sdcc.1/by-name/radio /firmware/radio

radiover=`cat /firmware/radio/radiover.cfg | sed 's/SSD://g'`

# Valid radios should break here
case $radiover in
    "1.05.20.0227_2")      break;;  # Sprint 4.4.2
    "1.05.20.0321_2")      break;;  # Sprint 4.4.2
    "1.08.20.0610")        break;;  # Sprint 4.4.2
    "1.08.20.0612_4")      break;;  # Sprint 4.4.3
    "1.08.20.0916")        break;;  # Sprint 4.4.4

    "0.89.20.0222")        break;;  # Verizon 4.4.3
    "0.89.20.0321")        break;;  # Verizon 4.4.3
    "1.09.20.0702")        break;;  # Verizon 4.4.3
    "1.09.20.0926")        break;;  # Verizon 4.4.4

    "1.21.213311491.A04G") break;;  # GPE 5.0.1
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
  echo "ERROR: Could not find a valid firmware image to symlink. Please ensure you have a vaild baseband."
  exit 1
fi

busybox umount /firmware/radio
exit 0
