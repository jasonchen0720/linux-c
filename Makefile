include $(PROJECT_ROOT)/config.mk

all clean:
	$(MAKE) -C ./tree  $@
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
	$(MAKE) -C ./lib $@
	$(MAKE) -C ./broker $@
	$(MAKE) -C ./mc $@
	$(MAKE) -C ./sample $@
	@echo -e '\n\033[32mMake $@ done!\033[0m\n'
install:
	install -m 644 ipc/libipc.so -D sample/lib/libipc.so
	install -m 644 lib/libjay.so -D sample/lib/libjay.so
uninstall:
	rm -rfv sample/lib/*