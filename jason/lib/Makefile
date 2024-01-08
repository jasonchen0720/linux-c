include $(JASON_WORK_DIR)/config.mk

all clean:
	$(MAKE) -C ./common  $@
	$(MAKE) -C ./cfg  $@
ifeq ($(CONFIG_IPC),y)
	$(MAKE) -C ./ipc  $@
endif
ifeq ($(CONFIG_TREE),y)
	$(MAKE) -C ./tree  $@
endif
ifeq ($(CONFIG_THREAD_POOL),y)
	$(MAKE) -C ./thread-pool  $@
endif
ifeq ($(CONFIG_TMR),y)
	$(MAKE) -C ./timer  $@
endif
	$(MAKE) -f ./jason.mk $@
