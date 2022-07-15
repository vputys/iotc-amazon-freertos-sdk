//
// Copyright: Avnet 2022
// Created by Nik Markovic <nikola.markovic@avnet.com> on 6/15/22.
//


#ifndef IOTCONNECT_SYNC_H
#define IOTCONNECT_SYNC_H

#ifdef __cplusplus
extern   "C" {
#endif
const char* iotc_sync_get_iothub_host();
const char* iotc_sync_get_username(void);
const char* iotc_sync_get_client_id(void);
const char* iotc_sync_get_pub_topic(void);
const char* iotc_sync_get_sub_topic(void);
const char* iotc_sync_get_dtg(void);

int iotc_sync_obtain_response(void);
void iotc_sync_free_response(void);


#ifdef __cplusplus
}
#endif

#endif // IOTCONNECT_SYNC_H