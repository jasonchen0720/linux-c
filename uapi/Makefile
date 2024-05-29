include $(PROJECT_ROOT)/config.mk
include $(PROJECT_ROOT)/cflags.mk

# executable program name, e.g. myprog 
EXECUTABLES :=

UAPI := uapi

BROKER := broker

# static lib name, e.g. libmylib.a
STATIC_LIBS := libuapi.a

# shared lib name, e.g. libmylib.so
SHARED_LIBS :=

C_SRCS :=

CFLAGS += -fPIC

# Every subdirectory with source files must be described here
IFLAGS := \
-I.\
-I$(PROJECT_ROOT)/uapi/include \
-I$(PROJECT_ROOT)/include \
-I$(PROJECT_ROOT)/ipc

# All of the sources participating in the build are defined here
include subdir.mk

OBJS := $(C_SRCS:.c=.o)
C_DEPS := $(C_SRCS:.c=.d)

UAPI_FILES = uapi.c
UAPI_OBJS = $(UAPI_FILES:.c=.o)
C_DEPS += $(UAPI_FILES:.c=.d)

BROKER_FILES = broker.c
BROKER_OBJS = $(BROKER_FILES:.c=.o)
C_DEPS += $(BROKER_FILES:.c=.d)

USER_OBJS :=

UAPI_USER_OBJS := \
$(PROJECT_ROOT)/uapi/libuapi.a \
$(PROJECT_ROOT)/ipc/libipc.a \
$(PROJECT_ROOT)/common/libbst.a \
$(PROJECT_ROOT)/common/libcommon.a \
$(PROJECT_ROOT)/common/liblog.a

LIBS := 

SHARE_LIBS :=
SHARE_LDFLAGS :=

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

# All Target
all: $(EXECUTABLES) $(STATIC_LIBS) $(SHARED_LIBS) $(UAPI) $(BROKER)

$(UAPI): $(STATIC_LIBS)

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

	# uapi
$(UAPI): Makefile $(UAPI_OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	$(CC) $(LDFLAGS) -o $@ $(UAPI_OBJS) $(USER_OBJS) $(UAPI_USER_OBJS) -lpthread -ljson-c
	@echo 'Finished building target: $@'
	@echo ' '

	# broker
$(BROKER): Makefile $(BROKER_OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	$(CC) $(LDFLAGS) -o $@ $(BROKER_OBJS) $(USER_OBJS) $(UAPI_USER_OBJS) -lpthread
	@echo 'Finished building target: $@'
	@echo ' '
	
clean:
	-$(RM) $(OBJS) $(C_DEPS) $(EXECUTABLES) $(STATIC_LIBS) $(SHARED_LIBS) $(UAPI) $(UAPI_OBJS) $(BROKER) $(BROKER_OBJS)
	-@echo ' '

.PHONY: all clean
.SECONDARY:
