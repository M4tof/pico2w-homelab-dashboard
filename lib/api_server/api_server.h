#ifndef API_SERVER_H
#define API_SERVER_H

#include <stdbool.h>
#include <stddef.h>

void start_api_server(void);

extern int LIGHT_THRESHOLD;
extern char currentCustomMsgForDisplay[64];

void set_light_threshold(int val);
int  get_light_threshold(void);

void set_custom_display_msg(const char *msg);
void get_custom_display_msg(char *out_buf, size_t max_len);

#endif