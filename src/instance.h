#ifndef _INSTANCE_H_
#define _INSTANCE_H_

#include "deca_types.h"
#include "deca_device_api.h"

#define RX_BUF_LEN 128
#define UUS_TO_DWT_TIME 65536
#define POLL_RX_TO_RESP_TX_DLY_UUS 600
#define TX_ANT_DLY 16436
#define RX_ANT_DLY 16436
#define POLL_TX_TO_RESP_RX_DLY_UUS 140
#define RESP_RX_TIMEOUT_UUS 210
#define SPEED_OF_LIGHT 299702547
#define RESP_MSG_TS_LEN 4
#define RESP_MSG_POLL_RX_TS_IDX 9
#define RESP_MSG_RESP_TX_TS_IDX 13

#define STATUS_IDLE 0
#define STATUS_POLL 1


#ifdef __cplusplus
extern "C" {
#endif

void set_status(const uint8 s);
void set_mac(uint16 dmac);
void set_pan(uint16 dpan);
void set_src(uint8 *buf);
int check_addr(uint8 *buf);
void instance_rxcallback(const dwt_callback_data_t *rxd);
void instance_txcallback(const dwt_callback_data_t *txd);
void instance_init();
uint64 get_rx_timestamp_u64(void);
void resp_msg_get_ts(uint8 *ts_field, uint32 *ts);
void resp_msg_set_ts(uint8 *ts_field, const uint64 ts);
void calculate_distance();
void response_poll();
void usart_handle(void);

#ifdef __cplusplus
}
#endif

#endif
