#ifndef __BROKER_CLIENT_H__
#define __BROKER_CLIENT_H__

/* prototype: event_callback(int msg_id, void *data, int size, void *arg); */
typedef int (*event_callback)(int, void *, int, void *);
int broker_client_publish(unsigned long mask, int msg_id, void *data, int size);
int broker_client_request(int msg_id, void *data, int size, void *response, int rsplen);
int broker_client_register(unsigned long mask, event_callback cb, void *arg);
void broker_client_unregister();
#endif

