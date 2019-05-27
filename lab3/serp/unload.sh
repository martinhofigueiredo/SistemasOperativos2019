#!/bin/sh 
#Removal code 
if [ $# "<" 1 ] ; then 
	echo "Usage: $0 <module_name>" 
	exit -1 
else 
	module="$1" 
	echo "Module Device: $1" 
fi 
sudo rmmod $module || exit 1 
# remove nodes 
sudo rm -f /dev/${module} /dev/${module}? 
exit 0