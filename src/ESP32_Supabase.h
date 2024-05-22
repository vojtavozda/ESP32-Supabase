#ifndef ESP32_Supabase_h
#define ESP32_Supabase_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <esp_timer.h>

#if defined(ESP8266)
#include <ESP8266HTTPClient.h>
#elif defined(ESP32)
#include <HTTPClient.h>
#else
#error "This library is not supported for your board! ESP32 and ESP8266"
#endif

typedef void (*RealtimeTXTHandler)(uint8_t * payload, size_t length);
typedef void (*WebSocketEventHandler)(WStype_t type, uint8_t * payload, size_t length);

class Supabase
{

private:
    String hostname;
    String key;
    String USER_TOKEN;

    String url_query;

    WiFiClientSecure client;
    HTTPClient https;

    bool useAuth;
    unsigned long loginTime;
    String phone_or_email;
    String password;
    String data;
    String loginMethod;
    String filter;

    unsigned int authTimeout = 0;

    // Websocktes
    bool realtimeStarted;
    static String realtimeConfigJson;
    static String realtimeHeartbeatJson;
    static WebSocketsClient webSocket;
    static void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
    static esp_timer_handle_t heartbeat_timer;
    static void heartbeat(void *arg);

    void _check_last_string();
    int _login_process();

    // Asynchronous functions
    static void asyncUpdateTask(void *pvParameters);
    TaskHandle_t asyncUpdateTaskHandle;


public:
    Supabase();
    ~Supabase() {};

    void begin(String hostname_a, String key_a);

    /** Init Supabase realtime. Port = 443 */
    void subscribeToRealtime(int port, String table, String id);
    /** Call this function periodically within loop() to listen to responses */
    void realtimeLoop();

    String getQuery();
    // query reset
    void urlQuery_reset();

    // membuat Query Builder
    Supabase &from(String table);
    int insert(String table, String json, bool upsert);
    Supabase &select(String colls);
    Supabase &update(String table);

    // Comparison Operator
    Supabase &eq(String coll, String conditions);
    Supabase &gt(String coll, String conditions);
    Supabase &gte(String coll, String conditions);
    Supabase &lt(String coll, String conditions);
    Supabase &lte(String coll, String conditions);
    Supabase &neq(String coll, String conditions);
    Supabase &in(String coll, String conditions);
    Supabase &is(String coll, String conditions);
    Supabase &cs(String coll, String conditions);
    Supabase &cd(String coll, String conditions);
    Supabase &ov(String coll, String conditions);
    Supabase &sl(String coll, String conditions);
    Supabase &sr(String coll, String conditions);
    Supabase &nxr(String coll, String conditions);
    Supabase &nxl(String coll, String conditions);
    Supabase &adj(String coll, String conditions);

    // Ordering
    Supabase &order(String coll, String by, bool nulls);
    Supabase &limit(unsigned int by);
    Supabase &offset(int by);

    // do select. execute this after building your query
    String doSelect();

    // do update. execute this after querying your update
    int doUpdate(String json);

    // Asynchronous update which does not block the code
    void asyncUpdate(String json);

    int login_email(String email_a, String password_a);
    int login_phone(String phone_a, String password_a);

    static RealtimeTXTHandler realtimeTXTHandler;

    String rpc(String func_name, String json_param = "");
};

#endif