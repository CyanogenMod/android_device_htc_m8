#!/system/bin/sh
export PATH=/system/xbin:$PATH

multisim=`getprop persist.radio.multisim.config`

if [ "$multisim" = "dsds" ]; then
    start ril-daemon2
fi
