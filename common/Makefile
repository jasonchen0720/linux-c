include $(PROJECT_ROOT)/config.mk
include $(PROJECT_ROOT)/cflags.mk

# executable program name, e.g. myprog 
EXECUTABLES :=

# static lib name, e.g. libmylib.a
STATIC_LIBS := libcommon.a

# shared lib name, e.g. libmylib.so
SHARED_LIBS :=

SRCS :=

CFLAGS +=

# Every subdirectory with source files must be described here
IFLAGS := \
-I.\
-I$(PROJECT_ROOT)/include \
-I$(PROJECT_ROOT)/tree \

# All of the sources participating in the build are defined here
LOCK_SRCS = ./lock.c
RBT_SRCS = ./rb_tree.c 
BST_SRCS = ./bs_tree.c

CFG_SRCS = \
./gcl.c \
./scl.c

LOG_SRCS = ./generic_log.c

GEN_SRCS += \
./generic_bit.c \
./generic_file.c \
./generic_proc.c

SRCS = $(LOCK_SRCS) $(RBT_SRCS) $(BST_SRCS) $(CFG_SRCS) $(LOG_SRCS) $(GEN_SRCS)


%.o: ./%.c
	@echo 'Building file: $<'
	$(CC) $(CFLAGS) $(IFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

LOCK_OBJS := $(LOCK_SRCS:.c=.o)
LOCK_SLIB := liblock.a

RBT_OBJS := $(RBT_SRCS:.c=.o)
RBT_SLIB := librbt.a

BST_OBJS := $(BST_SRCS:.c=.o)
BST_SLIB := libbst.a

CFG_OBJS := $(CFG_SRCS:.c=.o)
CFG_SLIB := libcfg.a

LOG_OBJS := $(LOG_SRCS:.c=.o)
LOG_SLIB := liblog.a

GEN_OBJS := $(GEN_SRCS:.c=.o)
GEN_SLIB := libutils.a

OBJS := $(SRCS:.c=.o)
DEPS := $(SRCS:.c=.d)

USER_OBJS :=

LIBS :=

SHARE_LIBS :=

SHARE_LDFLAGS :=

MINI_LIBS = $(LOCK_SLIB) $(RBT_SLIB) $(BST_SLIB) $(CFG_SLIB) $(LOG_SLIB) $(GEN_SLIB)

# All Target
all: $(EXECUTABLES) $(STATIC_LIBS) $(SHARED_LIBS) $(MINI_LIBS)

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

# Mini invocations
$(LOCK_SLIB): $(LOCK_OBJS)
	@echo 'Building target: $@'
	$(AR) rcs $(LOCK_SLIB) $(LOCK_OBJS)
	@echo 'Finished building target: $@'
	@echo ' '

# Mini invocations
$(RBT_SLIB): $(RBT_OBJS)
	@echo 'Building target: $@'
	$(AR) rcs $(RBT_SLIB) $(RBT_OBJS)
	@echo 'Finished building target: $@'
	@echo ' '

# Mini invocations
$(BST_SLIB): $(BST_OBJS)
	@echo 'Building target: $@'
	$(AR) rcs $(BST_SLIB) $(BST_OBJS)
	@echo 'Finished building target: $@'
	@echo ' '

# Mini invocations
$(CFG_SLIB): $(CFG_OBJS)
	@echo 'Building target: $@'
	$(AR) rcs $(CFG_SLIB) $(CFG_OBJS)
	@echo 'Finished building target: $@'
	@echo ' '

# Mini invocations
$(LOG_SLIB): $(LOG_OBJS)
	@echo 'Building target: $@'
	$(AR) rcs $(LOG_SLIB) $(LOG_OBJS)
	@echo 'Finished building target: $@'
	@echo ' '

# Mini invocations
$(GEN_SLIB): $(GEN_OBJS)
	@echo 'Building target: $@'
	$(AR) rcs $(GEN_SLIB) $(GEN_OBJS)
	@echo 'Finished building target: $@'
	@echo ' '
	
clean:
	-$(RM) $(OBJS) $(DEPS) $(EXECUTABLES) $(STATIC_LIBS) $(SHARED_LIBS) $(MINI_LIBS)
	-@echo ' '

.PHONY: all clean
.SECONDARY:
