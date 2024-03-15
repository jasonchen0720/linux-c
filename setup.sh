#!/bin/sh

this_dir=`pwd`
export CROSS_COMPILE=
export CROSS_SYSROOT=
export PROJECT_ROOT=$this_dir
export LD_LIBRARY_PATH=$PROJECT_ROOT/sample/lib:$LD_LIBRARY_PATH
