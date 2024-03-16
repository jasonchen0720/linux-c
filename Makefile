include $(PROJECT_ROOT)/config.mk

all clean:
	$(MAKE) -C ./common  $@
	
ifeq ($(CONFIG_IPC),y)
	$(MAKE) -C ./ipc  $@
endif
ifeq ($(CONFIG_THREAD_POOL),y)
	$(MAKE) -C ./thread-pool  $@
endif

ifeq ($(CONFIG_TMR),y)
	$(MAKE) -C ./timer  $@
endif

	$(MAKE) -C ./memory-pool  $@
	$(MAKE) -C ./co  $@
	$(MAKE) -C ./api $@
	$(MAKE) -C ./broker $@
	$(MAKE) -C ./mc $@
	$(MAKE) -C ./sample $@
	@echo -e '\n\033[32mMake $@ done!\033[0m\n'
install:
	install -m 644 ipc/libipc.so                 -D sample/lib/libipc.so
	install -m 644 timer/libtmr.so               -D sample/lib/libtmr.so
	install -m 644 memory-pool/libmem-pool.so    -D sample/lib/libmem-pool.so
	install -m 644 thread-pool/libthread-pool.so -D sample/lib/libthread-pool.so
	install -m 644 api/libapi.so                 -D sample/lib/libapi.so
uninstall:
	rm -rfv sample/lib/*
	rm -rfv sample/bin/*
