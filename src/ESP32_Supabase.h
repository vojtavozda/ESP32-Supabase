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

/** Need this for the trampoline */
class Supabase;
extern Supabase* globalSupabase;

typedef void (*RealtimeTXTHandler)(uint8_t * payload, size_t length);
typedef void (*WebSocketEventHandler)(WStype_t type, uint8_t * payload, size_t length);

class Supabase
{

private:

    Stream* debugSerial;

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
    bool realtimeInitialized;
    bool realtimeStarted;
    int realtimePort;
    String realtimeTable;
    String realtimeId;
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

    /** This function is connected to WiFi events
     * It ensures that client and realtime connections are stopped and
     * re-established when WiFi is lost/reconnected
    */
    void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

    void debugPrint(const String& message) {
        if (debugSerial) {
            debugSerial->print(message);
        }
    }

    void debugPrintln(const String& message) {
        if (debugSerial) {
            debugSerial->println(message);
        }
    }

    void debugPrintf(const char* format, ...) {
        if (debugSerial) {
            va_list args;
            va_start(args, format);
            char buf[256]; // Buffer to hold the formatted string
            vsnprintf(buf, sizeof(buf), format, args); // Format the string
            debugSerial->print(buf); // Print the formatted string
            va_end(args);
        }
    }

public:

    /** Supabase initialization status:
     * `false` as default, `true` after `begin()`
     * Use also to check if you want to call some supabase methods
     * */
    bool initialized;

    Supabase();
    ~Supabase() {};

    /** Initialize supabase. Call this first. When WiFi is connected, it
     * directly creates supabase client. Otherwise, it will wait for WiFi
     * @param hostname_a Supabase URL
     * @param key_a Supabase anon key
     * @param debugSerial_a Optional debug serial (`begin("h","k", &Serial);`)
     * */
    void begin(String hostname_a, String key_a, Stream* debugSerial_a = nullptr);

    /** Start both supabase client and realtime (if initialized) */
    void connect();
    /** Stop both supabase client and realtime */
    void disconnect();

    /** Init Supabase realtime. Port = 443 */
    void beginRealtime(int port, String table, String id);
    /** Subscribe to realtime */
    void subscribeToRealtime();
    /** Unsubscribe (and stop the periodic heartbeat timer) */
    void unsubscribeFromRealtime();
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