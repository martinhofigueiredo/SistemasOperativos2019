#!/bin/sh
#Loading code
if [ $# "<" 1 ] ; then
    echo "Usage: $0 <module_name>"
    exit -1
else
    module="$1"
    #echo "Module Device: $1"
fi
insmod -f ./${module}.ko || exit 1
rm -f /dev/${module}[0-3]
major=`cat /proc/devices | awk "\\$2==\"$module\" {print \\$1}"| head -n 1`
mknod /dev/${module}0 c $major 0
mknod /dev/${module}1 c $major 1
mknod /dev/${module}2 c $major 2
mknod /dev/${module}3 c $major 3
chmod a+rw /dev/${module}[0-3]
exit 0
