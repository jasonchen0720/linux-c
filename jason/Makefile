export JC_WORK_DIR=$(shell pwd)

all clean:
	$(MAKE) -C ./lib $@
	$(MAKE) -C ./lib-jc $@
	$(MAKE) -C ./sample $@
	$(MAKE) -C ./broker $@
	$(MAKE) -C ./mc $@

install:

uninstall:
