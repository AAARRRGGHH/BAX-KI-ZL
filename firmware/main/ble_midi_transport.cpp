#include "ble_midi_transport.hpp"
#include "ble_midi_protocol.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
extern "C" {
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
void ble_store_config_init(void);
}
namespace {
constexpr char tag[]="ble-midi",name[]="BAX:KI:ZL";
// BLE UUID byte arrays are little endian (the printed standard UUID is reversed).
const ble_uuid128_t service_uuid=BLE_UUID128_INIT(0x00,0xc7,0xc4,0x4e,0xe3,0x6c,0x51,0xa7,0x33,0x4b,0xe8,0xed,0x5a,0x0e,0xb8,0x03);
const ble_uuid128_t characteristic_uuid=BLE_UUID128_INIT(0xf3,0x6b,0x10,0x9d,0x66,0xf2,0xa9,0xa1,0x12,0x41,0x68,0x38,0xdb,0xe5,0x72,0x77);
sm::BleMidiOutbox outbox;
sm::BleMidiRealtimeDecoder decoder;
std::array<sm::MidiRealtime,128> incoming{};
size_t input_head=0,input_count=0;
void received(sm::MidiRealtime event);
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
void received(sm::MidiRealtime event) {
 portENTER_CRITICAL(&mux);
 if(input_count==incoming.size()) {
   input_head=0; input_count=1; incoming[0]={0xff,event.time_us};
 } else { incoming[(input_head+input_count)%incoming.size()]=event; ++input_count; }
 portEXIT_CRITICAL(&mux);
}
uint16_t connection=BLE_HS_CONN_HANDLE_NONE,value_handle=0;
uint8_t address_type=0;
ble_npl_callout pump_callout;
uint32_t stalled_at=0;
// Host-task-only diagnostics; no logging or allocation for individual clock pulses.
uint32_t stats_at=0,tx_packets=0,tx_messages=0,tx_retries=0,rx_clocks=0,max_queue_ms=0;
uint32_t rx_age_max_ms=0,rx_same_stamp=0;
uint64_t last_rx_clock=0;
bool was_ready=false,advertise_retry=false;
bool output_enabled=true,subscribed=false;
SemaphoreHandle_t delivery_gate=nullptr;
struct DeliveryLock {
 DeliveryLock() { if(delivery_gate) xSemaphoreTake(delivery_gate,portMAX_DELAY); }
 ~DeliveryLock() { if(delivery_gate) xSemaphoreGive(delivery_gate); }
};
uint32_t now_ms() { return uint32_t(esp_timer_get_time()/1000); }
void session(bool enabled) {
 portENTER_CRITICAL(&mux); subscribed=enabled; outbox.session(enabled && output_enabled); portEXIT_CRITICAL(&mux);
 stalled_at=0; was_ready=false;
}
int gap_event(ble_gap_event*,void*);
void advertise() {
 ble_hs_adv_fields fields{};
 fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
 fields.uuids128=const_cast<ble_uuid128_t*>(&service_uuid); fields.num_uuids128=1; fields.uuids128_is_complete=1;
 int rc=ble_gap_adv_set_fields(&fields);
 ble_hs_adv_fields response{}; response.name=reinterpret_cast<const uint8_t*>(name);
 response.name_len=sizeof(name)-1; response.name_is_complete=1;
 if(!rc) rc=ble_gap_adv_rsp_set_fields(&response);
 ble_gap_adv_params parameters{}; parameters.conn_mode=BLE_GAP_CONN_MODE_UND;
 parameters.disc_mode=BLE_GAP_DISC_MODE_GEN; parameters.itvl_min=160; parameters.itvl_max=240;
 if(!rc) rc=ble_gap_adv_start(address_type,nullptr,BLE_HS_FOREVER,&parameters,gap_event,nullptr);
 advertise_retry=rc!=0;
 if(rc) ESP_LOGW(tag,"Advertising failed: %d; retrying",rc);
 else ESP_LOGI(tag,"Advertising: %s (BLE MIDI 1.0)",name);
}
void pump(ble_npl_event*) {
 if(advertise_retry && connection==BLE_HS_CONN_HANDLE_NONE) advertise();
 if(connection!=BLE_HS_CONN_HANDLE_NONE) {
   for(int i=0;i<8;++i) {
     DeliveryLock gate;
     sm::BleMidiOutbox::Batch pending;
     portENTER_CRITICAL(&mux); bool available=output_enabled && outbox.peek_batch(now_ms(),pending); portEXIT_CRITICAL(&mux);
     if(!available) break;
     os_mbuf* buffer=ble_hs_mbuf_from_flat(pending.packet.data(),pending.length);
     // notify_custom consumes its mbuf even when it fails. Keep the FIFO head for retry.
     const int rc=buffer?ble_gatts_notify_custom(connection,value_handle,buffer):BLE_HS_ENOMEM;
     if(rc) {
       ++tx_retries;
       if(!stalled_at) { stalled_at=now_ms(); ESP_LOGW(tag,"Notification backpressure: %d",rc); }
       if(uint32_t(now_ms()-stalled_at)>1500) {
         ESP_LOGE(tag,"Notification stalled; resetting connection, no stale-note replay");
         session(false); ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM);
       }
       break;
     }
     stalled_at=0;
     ++tx_packets; tx_messages+=pending.token.count;
     max_queue_ms=std::max(max_queue_ms,pending.age_ms);
     portENTER_CRITICAL(&mux); outbox.delivered(pending.token); portEXIT_CRITICAL(&mux);
   }
   portENTER_CRITICAL(&mux); bool ready=outbox.ready(); portEXIT_CRITICAL(&mux);
   if(ready && !was_ready) ESP_LOGI(tag,"MIDI READY: notifications subscribed, velocity 100; channel from saved setup");
   was_ready=ready;
   if(uint32_t(now_ms()-stats_at)>=5000) {
     stats_at=now_ms();
     if(tx_packets || rx_clocks || tx_retries) {
       int8_t rssi=0; const int rc=ble_gap_conn_rssi(connection,&rssi);
       ESP_LOGI("ble-timing","tx_packets=%lu tx_messages=%lu retries=%lu queue_max_ms=%lu rx_clock=%lu rx_age_max_ms=%lu rx_same_stamp=%lu rssi=%d valid=%d",
         (unsigned long)tx_packets,(unsigned long)tx_messages,(unsigned long)tx_retries,
         (unsigned long)max_queue_ms,(unsigned long)rx_clocks,(unsigned long)rx_age_max_ms,(unsigned long)rx_same_stamp,int(rssi),rc==0);
     }
     tx_packets=tx_messages=tx_retries=rx_clocks=max_queue_ms=0;
     rx_age_max_ms=rx_same_stamp=0;
   }
 }
 ble_npl_callout_reset(&pump_callout,ble_npl_time_ms_to_ticks32(advertise_retry?1000:1));
}
int access(uint16_t,uint16_t,ble_gatt_access_ctxt* context,void*) {
 if(context->op==BLE_GATT_ACCESS_OP_READ_CHR) return 0;
 if(context->op==BLE_GATT_ACCESS_OP_WRITE_CHR) {
   std::array<uint8_t,512> bytes{}; uint16_t length=0;
   if(OS_MBUF_PKTLEN(context->om)>bytes.size() || ble_hs_mbuf_to_flat(context->om,bytes.data(),bytes.size(),&length)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
   const auto arrival=uint64_t(esp_timer_get_time());
   return decoder.parse(bytes.data(),length,arrival,[arrival](sm::MidiRealtime event){
     if(event.status==0xf8){
       ++rx_clocks;if(last_rx_clock && last_rx_clock==event.time_us)++rx_same_stamp;last_rx_clock=event.time_us;
       if(arrival>=event.time_us)rx_age_max_ms=std::max(rx_age_max_ms,uint32_t((arrival-event.time_us)/1000));
     }else last_rx_clock=0;
     received(sm::ble_midi_clock_playout(event));
   })?0:BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
 }
 return BLE_ATT_ERR_UNLIKELY;
}
int gap_event(ble_gap_event* event,void*) {
 switch(event->type) {
 case BLE_GAP_EVENT_CONNECT:
   if(event->connect.status) { advertise(); break; }
   connection=event->connect.conn_handle; session(false);
   ESP_LOGI(tag,"Connected; waiting for MIDI notification subscription");
   {
     ble_gap_upd_params parameters{}; parameters.itvl_min=6; parameters.itvl_max=6;
     parameters.latency=0; parameters.supervision_timeout=200;
     int rc=ble_gap_update_params(connection,&parameters);
     ESP_LOGI(tag,"Requested interval 7.5 ms: rc=%d (central chooses actual interval)",rc);
   }
   break;
 case BLE_GAP_EVENT_DISCONNECT:
   decoder.reset();
   last_rx_clock=0;
   portENTER_CRITICAL(&mux); input_head=input_count=0; portEXIT_CRITICAL(&mux);
   received({0xff,uint64_t(esp_timer_get_time())});
   connection=BLE_HS_CONN_HANDLE_NONE; session(false);
   ESP_LOGW(tag,"Disconnected: reason=%d; local notes cleared",event->disconnect.reason);
   advertise(); break;
 case BLE_GAP_EVENT_SUBSCRIBE:
   if(event->subscribe.attr_handle==value_handle) {
     session(event->subscribe.cur_notify!=0);
     ESP_LOGI(tag,"MIDI notifications %s",event->subscribe.cur_notify?"enabled; resetting notes":"disabled");
   }
   break;
 case BLE_GAP_EVENT_CONN_UPDATE: {
   ble_gap_conn_desc desc{};
   if(!ble_gap_conn_find(connection,&desc)) ESP_LOGI(tag,"Actual interval %.2f ms; latency=%u",desc.conn_itvl*1.25,desc.conn_latency);
   break;
 }
 case BLE_GAP_EVENT_ENC_CHANGE: ESP_LOGI(tag,"Encryption status=%d",event->enc_change.status); break;
 case BLE_GAP_EVENT_REPEAT_PAIRING: {
   // Official bleprph recovery: the PC may have forgotten its old key.
   // Replace only this peer's BLE bond; application NVS remains untouched.
   ble_gap_conn_desc desc{};
   if(ble_gap_conn_find(event->repeat_pairing.conn_handle,&desc)) return BLE_GAP_REPEAT_PAIRING_IGNORE;
   int rc=ble_store_util_delete_peer(&desc.peer_id_addr);
   ESP_LOGW(tag,"Peer requested re-pairing; replacing its previous bond: rc=%d",rc);
   return rc?BLE_GAP_REPEAT_PAIRING_IGNORE:BLE_GAP_REPEAT_PAIRING_RETRY;
 }
 case BLE_GAP_EVENT_ADV_COMPLETE: if(connection==BLE_HS_CONN_HANDLE_NONE) advertise(); break;
 default: break;
 }
 return 0;
}
void synced() {
 int rc=ble_hs_util_ensure_addr(0);
 if(!rc) rc=ble_hs_id_infer_auto(0,&address_type);
 if(rc) { ESP_LOGE(tag,"Address initialization failed: %d",rc); return; }
 advertise(); ble_npl_callout_reset(&pump_callout,ble_npl_time_ms_to_ticks32(4));
}
void reset(int reason) {
 connection=BLE_HS_CONN_HANDLE_NONE; session(false);
 ble_npl_callout_stop(&pump_callout);
 ESP_LOGE(tag,"Host reset: %d; awaiting resynchronization",reason);
}
}
esp_err_t BleMidiTransport::start() {
 delivery_gate=xSemaphoreCreateMutex(); if(!delivery_gate) return ESP_ERR_NO_MEM;
 esp_err_t result=nimble_port_init(); if(result!=ESP_OK) return result;
 ble_hs_cfg.sync_cb=synced; ble_hs_cfg.reset_cb=reset;
 ble_hs_cfg.store_status_cb=ble_store_util_status_rr;
 ble_hs_cfg.sm_io_cap=BLE_HS_IO_NO_INPUT_OUTPUT;
 ble_hs_cfg.sm_bonding=1; ble_hs_cfg.sm_sc=1;
 ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
 ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
 ble_svc_gap_init(); ble_svc_gatt_init();
 static ble_gatt_chr_def characteristics[2]{};
 characteristics[0].uuid=&characteristic_uuid.u; characteristics[0].access_cb=access;
 characteristics[0].val_handle=&value_handle;
 characteristics[0].flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_NOTIFY;
 static ble_gatt_svc_def services[2]{};
 services[0].type=BLE_GATT_SVC_TYPE_PRIMARY; services[0].uuid=&service_uuid.u;
 services[0].characteristics=characteristics;
 int rc=ble_gatts_count_cfg(services);
 if(!rc) rc=ble_gatts_add_svcs(services);
 if(!rc) rc=ble_svc_gap_device_name_set(name);
 if(rc) { ESP_LOGE(tag,"GATT initialization failed: %d",rc); return ESP_FAIL; }
 ble_store_config_init();
 rc=ble_npl_callout_init(&pump_callout,nimble_port_get_dflt_eventq(),pump,nullptr);
 if(rc) return ESP_FAIL;
 nimble_port_freertos_init([](void*) { nimble_port_run(); nimble_port_freertos_deinit(); });
 return ESP_OK;
}
bool BleMidiTransport::send(sm::MidiEvent event) {
 portENTER_CRITICAL(&mux); const bool accepted=output_enabled?outbox.enqueue(event,now_ms()):event.kind!=sm::MidiKind::NoteOn; portEXIT_CRITICAL(&mux);
 return accepted;
}
bool BleMidiTransport::connected() {
 portENTER_CRITICAL(&mux); const bool ready=subscribed; portEXIT_CRITICAL(&mux); return ready;
}
void BleMidiTransport::set_enabled(bool enabled) {
 if(!enabled && delivery_gate) {
   // Let already queued Note Off / CC123 leave before muting the transport.
   const auto began=now_ms();
   while(uint32_t(now_ms()-began)<100) {
     sm::BleMidiOutbox::Pending pending;
     portENTER_CRITICAL(&mux); const bool available=outbox.peek(now_ms(),pending); portEXIT_CRITICAL(&mux);
     if(!available) break;
     vTaskDelay(pdMS_TO_TICKS(1));
   }
 }
 DeliveryLock gate;
 portENTER_CRITICAL(&mux);
 // Even when muted, finish a cleanup session on a still-connected old link.
 // This survives backpressure instead of discarding the final Note Offs.
 if(output_enabled!=enabled) { output_enabled=enabled; outbox.session(subscribed); }
 portEXIT_CRITICAL(&mux);
}
bool BleMidiTransport::reset_requested(uint32_t& generation) {
 portENTER_CRITICAL(&mux); bool pending=outbox.reset_pending(); generation=outbox.generation(); portEXIT_CRITICAL(&mux);
 return pending;
}
void BleMidiTransport::acknowledge_reset(uint32_t generation) {
 portENTER_CRITICAL(&mux); outbox.acknowledge_reset(generation); portEXIT_CRITICAL(&mux);
}
bool BleMidiTransport::receive_realtime(sm::MidiRealtime& event) {
 portENTER_CRITICAL(&mux);
 const bool available=input_count!=0;
 if(available) { event=incoming[input_head]; input_head=(input_head+1)%incoming.size(); --input_count; }
 portEXIT_CRITICAL(&mux); return available;
}
