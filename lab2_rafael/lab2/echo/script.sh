#!/bin/sh
#Testing Script
if [ $# "<" 1 ] ; then
    echo "Usage: $0 <module_name>"
    exit -1
else
    module="$1"
    #echo "Module Device: $1"
fi
gcc -Wall ${module}_test.c -o ${module}_test
make
(sudo ./unload.sh echo && sudo ./load.sh ${module}) || sudo ./load.sh ${module}
ls -l /dev/echo*
./${module}_test /dev/${module}0
sudo ./unload.sh echo
exit 0
