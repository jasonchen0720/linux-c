SRCS += \
./client/mc_client.c \
./client/broker_client.c

ifeq ($(CONFIG_IPC),y)
SRCS += ./client/ipcc.c
endif

client/%.o: ./client/%.c
	@echo 'Building file: $<'
	$(CC) $(CFLAGS) $(IFLAGS) -I. -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

