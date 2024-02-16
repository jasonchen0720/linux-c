include $(JC_WORK_DIR)/config.mk
include $(JC_WORK_DIR)/cflags.mk

# executable program name, e.g. myprog 
EXECUTABLES := tree-sample

# static lib name, e.g. libmylib.a
STATIC_LIBS :=

# shared lib name, e.g. libmylib.so
SHARED_LIBS :=

SRCS :=

CFLAGS +=

# Every subdirectory with source files must be described here
IFLAGS := \
-I.\
-I$(JC_WORK_DIR)/lib/include \
-I$(JC_WORK_DIR)/lib/tree

#ld
LDFLAGS :=

# All of the sources participating in the build are defined here
SRCS += tree_sample.c

%.o: ./%.c
	@echo 'Building file: $<'
	$(CC) $(CFLAGS) $(IFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

OBJS := $(SRCS:.c=.o)
DEPS := $(SRCS:.c=.d)

USER_OBJS := \
$(JC_WORK_DIR)/lib/common/libcommon.a

USER_OBJS += $(JC_WORK_DIR)/lib/cfg/libcfg.a $(JC_WORK_DIR)/lib/tree/libtree.a

LIBS :=

SHARE_LIBS :=

SHARE_LDFLAGS :=

# All Target
all: $(EXECUTABLES) $(STATIC_LIBS) $(SHARED_LIBS)

# Tool invocations
$(EXECUTABLES): $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	$(CC) $(LDFLAGS) -o $(EXECUTABLES) $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Tool invocations
$(STATIC_LIBS): $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	$(AR) rcs $(STATIC_LIBS) $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Tool invocations
$(SHARED_LIBS): $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	$(CC) $(SHARE_LDFLAGS) -shared -o $(SHARED_LIBS) $(OBJS) $(USER_OBJS) $(SHARE_LIBS)
	@echo 'Finished building target: $@'
	@echo  ' '

clean:
	-$(RM) $(OBJS) $(DEPS) $(EXECUTABLES) $(STATIC_LIBS) $(SHARED_LIBS)
	-@echo ' '

.PHONY: all clean
.SECONDARY:
