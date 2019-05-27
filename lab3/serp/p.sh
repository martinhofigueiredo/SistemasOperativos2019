#!/bin/sh 
sudo sh disableserial.sh
cd serp/
sudo sh unload.sh serp
make
sudo sh load.sh serp

