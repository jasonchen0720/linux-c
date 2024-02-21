include $(JC_WORK_DIR)/config.mk
include $(JC_WORK_DIR)/cflags.mk

all clean:
ifeq ($(CONFIG_IPC),y)
	$(MAKE) -C ./ipc  $@
endif
ifeq ($(CONFIG_TREE),y)
	$(MAKE) -C ./tree  $@
endif
ifeq ($(CONFIG_TMR),y)
	$(MAKE) -C ./timer  $@
endif
	$(MAKE) -C ./co  $@
