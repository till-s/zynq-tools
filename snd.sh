#!/bin/sh
ADDR=0x1a
I2CM=/nfs/host/i2cm
if [ $# -eq 1 ] ; then
	$I2CM -a $ADDR -o $(( $1 << 1 )) -l 2 | awk '/:/{printf("0x%s%s\n",$3,$2);}'
elif [ $# -eq 2 ] ; then
	OFF=$(( ( $1 << 1 ) | (( $2 >> 8 ) & 1) ))
	VAL=$(( $2 & 0xff ))
	$I2CM -a $ADDR -o $OFF $VAL
else
  echo "Usage: $0 offset [val]"
  exit 1
fi
