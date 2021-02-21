C_SRCS += \
./libipc/ipc_client.c \
./libipc/ipc_server.c \
./libipc/ipc_log.c \
./libipc/ipc_base.c \
./libipc/client.c

%.o: ./%.c
	@echo 'Building file: $<'
	$(CC) $(CFLAGS) $(IFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '
