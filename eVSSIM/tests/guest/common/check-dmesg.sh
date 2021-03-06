#!/bin/bash

for i in "$@"
do
case $i in
    -s=*|--string=*)
    STRING="${i#*=}"
    shift # past argument=value
    ;;
    -l=*|--loglevel=*)
    LEVEL="${i#*=}"
    shift # past argument=value
    ;;
    -C|--clear)
    echo " "
    echo "Clearing dmesg ring buffer "
    dmesg -C
    echo " "
    exit
    ;;
    *)
    echo " "
    echo " "
    echo "usage:   $0 -l=[log level (default:err)] -s=[searched string] "
    echo "         $0 -C"
    echo " "
    echo "         returns 0 if found or 1 if not found"
    echo " "
    echo 'example:  $0 -l=warn,err -s="cgroup\|BIOS"'
    echo " "
    echo " "
    exit -1
    ;;
esac
done

dmesg -x --level=${LEVEL:-err} | grep ${STRING}
