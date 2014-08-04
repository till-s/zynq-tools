#!/bin/sh
SND=/nfs/host/snd.sh
$SND 0xf 0  # RESET
sleep 1
$SND 6 0x72 # power-on essential parts (except OUT)

# ADC
$SND 0 0x17 # unmute + vol left
$SND 1 0x17 # unmute + vol right


# DAC
$SND 5 0x00 # disable DAC mute
$SND 4 0x12 # enable DAC to mixer

# SAMPLING
#$SND 8 0x01 # enable USB mode 48khz
$SND 8 0x23 # USB, BOSR, 41kHz
$SND 7 0x02 # 16-bit samples
sleep 1
$SND 9 0x01 # activate
$SND 6 0x62 # power-on OUT
