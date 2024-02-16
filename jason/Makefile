export JASON_WORK_DIR=$(shell pwd)

all clean:
	$(MAKE) -C ./lib $@
	$(MAKE) -C ./lib-jc $@
	$(MAKE) -C ./test $@
	$(MAKE) -C ./broker $@
	$(MAKE) -C ./mc $@

install:

uninstall:
