#!/bin/sh

this_dir=`pwd`
export CROSS_COMPILE=
export CROSS_SYSROOT=
export PROJECT_ROOT=$this_dir

LIB_DIR=$PROJECT_ROOT/image/lib
BIN_DIR=$PROJECT_ROOT/image/bin

export LD_LIBRARY_PATH=$LIB_DIR:$LD_LIBRARY_PATH
export PATH=$BIN_DIR:$PATH

function buildall() {
	make
	install -m 644 $PROJECT_ROOT/co/libco.so                   -D $LIB_DIR/libco.so
	install -m 644 $PROJECT_ROOT/ipc/libipc.so                 -D $LIB_DIR/libipc.so
	install -m 644 $PROJECT_ROOT/timer/libtmr.so               -D $LIB_DIR/libtmr.so
	install -m 644 $PROJECT_ROOT/memory-pool/libmem-pool.so    -D $LIB_DIR/libmem-pool.so
	install -m 644 $PROJECT_ROOT/thread-pool/libthread-pool.so -D $LIB_DIR/libthread-pool.so
	install -m 644 $PROJECT_ROOT/api/libapi.so                 -D $LIB_DIR/libapi.so
	
	install -m 755 -d $BIN_DIR
	install -m 755 $PROJECT_ROOT/sample/mc/mcd                 -D $BIN_DIR/
	install -m 755 $PROJECT_ROOT/sample/broker/broker          -D $BIN_DIR/
	install -m 755 $PROJECT_ROOT/sample/*-sample/*-sample      -D $BIN_DIR/
}

function buildclean() {
	make clean
	rm -rvf $LIB_DIR/*
	rm -rvf $BIN_DIR/*
}
