#!/bin/sh
#Loading code
 if [ $# "<" 1 ] ; then 
 	echo "Usage: $0 <module_name>" 
 	exit -1 
else 
	module="$1" 
	echo "Module Device: $1" 
fi 
sudo insmod -f ./${module}.ko || exit 1 
major=`cat /proc/devices | awk "\\$2==\"$module\" {print \\$1}"| head -n 1` 
sudo mknod /dev/${module} c $major 0 
sudo chmod a+rw /dev/${module} 
exit 0