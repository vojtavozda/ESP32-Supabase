#include "ESP32_Supabase.h"

Supabase *globalSupabase = nullptr;

// Define static variables here
String Supabase::realtimeConfigJson;
String Supabase::realtimeHeartbeatJson;
WebSocketsClient Supabase::webSocket;
esp_timer_handle_t Supabase::heartbeat_timer;
RealtimeTXTHandler Supabase::realtimeTXTHandler;

void hexdump(const void *mem, uint32_t len, uint8_t cols = 16)
{
    const uint8_t *src = (const uint8_t *)mem;
    Serial.printf("\n[HEXDUMP] Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
    for (uint32_t i = 0; i < len; i++)
    {
        if (i % cols == 0)
        {
            Serial.printf("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
        }
        Serial.printf("%02X ", *src);
        src++;
    }
    Serial.printf("\n");
}

void Supabase::_check_last_string()
{
    unsigned int last = url_query.length() - 1;
    if (url_query[last] != '?')
    {
        url_query += "&";
    }
}

int Supabase::_login_process()
{
    int httpCode;
    JsonDocument doc;
    Serial.println("Beginning to login..");

    if (https.begin(client, hostname + "/auth/v1/token?grant_type=password"))
    {
        https.addHeader("apikey", key);
        https.addHeader("Content-Type", "application/json");

        String query = "{\"" + loginMethod + "\": \"" + phone_or_email + "\", \"password\": \"" + password + "\"}";
        httpCode = https.POST(query);

        if (httpCode > 0)
        {
            String data = https.getString();
            deserializeJson(doc, data);
            if (doc.containsKey("access_token") && !doc["access_token"].isNull() && doc["access_token"].is<String>() && !doc["access_token"].as<String>().isEmpty())
            {
                USER_TOKEN = doc["access_token"].as<String>();
                authTimeout = doc["expires_in"].as<int>() * 1000;
                Serial.println("Login Success");
                Serial.println(USER_TOKEN);
                Serial.println(data);
            }
            else
            {
                Serial.println("Login Failed: Invalid access token in response");
            }
        }
        else
        {
            Serial.println(phone_or_email);
            Serial.println(password);

            Serial.print("Login Failed : ");
            Serial.println(httpCode);
        }

        https.end();
        loginTime = millis();
    }
    else
    {
        return -100;
    }

    return httpCode;
}

Supabase::Supabase()
{
    initialized = false;
    realtimeInitialized = false;
    realtimeStarted = false;
    realtimeTXTHandler = nullptr;
    globalSupabase = this;
}

void Supabase::begin(String hostname_a, String key_a)
{
    hostname = hostname_a;
    key = key_a;
    WiFi.onEvent(std::bind(&Supabase::onWiFiEvent, this, std::placeholders::_1, std::placeholders::_2));
    initialized = true;

    if (WiFi.status() == WL_CONNECTED) {
        connect();
    }
}

void Supabase::onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
    switch (event)
    {
    case SYSTEM_EVENT_STA_GOT_IP:
        connect();
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        disconnect();
        break;
    default:
        break;
    }
}

void Supabase::connect() {
    if (initialized) {
        client.setInsecure();
    }
    if (realtimeInitialized) {
        subscribeToRealtime();
    }
}

void Supabase::disconnect() {
    client.stop();

    if (realtimeStarted) {
        unsubscribeFromRealtime();
    }
}


void Supabase::beginRealtime(int port, String table, String id)
{
    realtimePort = port;
    realtimeTable = table;
    realtimeId = id;

    realtimeConfigJson = 
    "{"
        "\"event\": \"phx_join\","
        "\"topic\": \"realtime:[channel-name]\","
        "\"payload\": {"
            "\"config\": {"
                "\"broadcast\": {"
                    "\"self\": false"
                "},"
                "\"presence\": {"
                    "\"key\": \"\""
                "},"
                "\"postgres_changes\": ["
                    "{"
                        "\"event\": \"*\","
                        "\"schema\": \"public\","
                        "\"table\": \"" + table + "\","
                        "\"filter\": \"id=eq." + id + "\""
                    "}"
                "]"
            "}"
        "},"
        "\"ref\": \"sentRef\""
    "}";

    realtimeHeartbeatJson = 
    "{"
        "\"event\": \"heartbeat\","
        "\"topic\": \"phoenix\","
        "\"payload\": {},"
        "\"ref\": \"\""
    "}";

    realtimeInitialized = true;

    if (initialized && WiFi.status() == WL_CONNECTED) {
        subscribeToRealtime();
    }
    
}

void Supabase::subscribeToRealtime() {
    if (!realtimeInitialized) {
        Serial.println("Realtime not initialized! Call `beginRealtime` first");
        return;
    }
    String pureHostname = hostname;
    if (pureHostname.startsWith("https://")) {
        // Remove "https://" string from the `hostname` (if exists)
        pureHostname = pureHostname.substring(8);
    }
    webSocket.beginSSL(
        pureHostname,
        realtimePort,
        "/realtime/v1/websocket?apikey=" + key + "&vsn=1.0.0");
    webSocket.onEvent(webSocketEvent);
    realtimeStarted = true;
}

void Supabase::unsubscribeFromRealtime()
{
    webSocket.disconnect();
    realtimeStarted = false;
    
    if (heartbeat_timer != NULL) {
        esp_timer_stop(heartbeat_timer);
        esp_timer_delete(heartbeat_timer);
        heartbeat_timer = NULL;
    }
}

void Supabase::webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{

    switch (type)
    {
    case WStype_DISCONNECTED:
        // Serial.printf("[WSc] Disconnected!\n");
        // Stop the timer
        if (heartbeat_timer != NULL)
        {
            esp_timer_stop(heartbeat_timer);
            esp_timer_delete(heartbeat_timer);
            heartbeat_timer = NULL;
        }
        break;
    case WStype_CONNECTED:
        // Serial.printf("[WSc] Connected to url: %s\n", payload);
        // Create periodic timer to send heartbeat message
        esp_timer_create_args_t timer_args;
        timer_args.callback = &heartbeat;
        timer_args.arg = nullptr;
        timer_args.dispatch_method = ESP_TIMER_TASK;
        timer_args.name = "heartbeat_timer";
        esp_timer_create(&timer_args, &heartbeat_timer);
        esp_timer_start_periodic(heartbeat_timer, 30 * 1000000);
        // send message to server when Connected
        webSocket.sendTXT(realtimeConfigJson);
        break;
    case WStype_TEXT:
        // Serial.printf("[WSc] get text: %s\n", payload);
        if (realtimeTXTHandler != nullptr)
        {
            realtimeTXTHandler(payload, length);
        }
        break;
    default:
        // Serial.printf("[WSc] unknown type: %s\n", payload);
        break;
    }
}

void Supabase::heartbeat(void *arg)
{
    Serial.println("[WS] Sending Heartbeat message");
    
    webSocket.sendTXT(realtimeHeartbeatJson);
}

void Supabase::realtimeLoop()
{
    if (realtimeStarted) {
        webSocket.loop();
    }
}

String Supabase::getQuery()
{
    String temp = url_query;
    urlQuery_reset();
    return hostname + "/rest/v1/" + temp;
}
// query reset
void Supabase::urlQuery_reset()
{
    url_query = "";
}
// membuat Query Builder
Supabase &Supabase::Supabase::from(String table)
{
    url_query += (table + "?");
    return *this;
}

int Supabase::insert(String table, String json, bool upsert)
{
    int httpCode;
    if (https.begin(client, hostname + "/rest/v1/" + table))
    {
        https.addHeader("apikey", key);
        https.addHeader("Content-Type", "application/json");

        String preferHeader = "return=representation";
        if (upsert)
        {
            preferHeader += ",resolution=merge-duplicates";
        }
        https.addHeader("Prefer", preferHeader);

        if (useAuth)
        {
            unsigned long t_now = millis();
            if (t_now - loginTime >= authTimeout)
            {
                _login_process();
            }
            https.addHeader("Authorization", "Bearer " + USER_TOKEN);
        }
        httpCode = https.POST(json);
        https.end();
    }
    else
    {
        return -100;
    }
    return httpCode;
}

Supabase &Supabase::select(String colls)
{
    url_query += ("select=" + colls);
    return *this;
}
Supabase &Supabase::update(String table)
{
    url_query += (table + "?");
    return *this;
}
// Supabase& Supabase::drop(String table){
//   url_query += (table+"?");
//   return *this;
// }

// Comparison Operator
Supabase &Supabase::eq(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=eq." + conditions);
    return *this;
}
Supabase &Supabase::gt(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=gt." + conditions);
    return *this;
}
Supabase &Supabase::gte(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=gte." + conditions);
    return *this;
}
Supabase &Supabase::lt(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=lt." + conditions);
    return *this;
}
Supabase &Supabase::lte(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=lte." + conditions);
    return *this;
}
Supabase &Supabase::neq(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=neq." + conditions);
    return *this;
}
Supabase &Supabase::in(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=in.(" + conditions + ")");
    return *this;
}
Supabase &Supabase::is(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=is." + conditions);
    return *this;
}
Supabase &Supabase::cs(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=cs.{" + conditions + "}");
    return *this;
}
Supabase &Supabase::cd(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=cd.{" + conditions + "}");
    return *this;
}
Supabase &Supabase::ov(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=cd.{" + conditions + "}");
    return *this;
}
Supabase &Supabase::sl(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=sl.(" + conditions + ")");
    return *this;
}
Supabase &Supabase::sr(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=sr.(" + conditions + ")");
    return *this;
}
Supabase &Supabase::nxr(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=nxr.(" + conditions + ")");
    return *this;
}
Supabase &Supabase::nxl(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=nxl.(" + conditions + ")");
    return *this;
}
Supabase &Supabase::adj(String coll, String conditions)
{
    _check_last_string();
    url_query += (coll + "=adj.(" + conditions + ")");
    return *this;
}
// Supabase& Supabase::logic(String mylogic){
//   url_query += (mylogic);
// }

// Ordering
Supabase &Supabase::order(String coll, String by, bool nulls = 1)
{
    String subq[] = {"nullsfirst", "nullslast"};
    _check_last_string();
    url_query += ("order=" + coll + "." + by + "." + subq[(int)nulls]);
    return *this;
}
Supabase &Supabase::limit(unsigned int by)
{
    _check_last_string();
    url_query += ("limit=" + String(by));
    return *this;
}
Supabase &Supabase::offset(int by)
{
    _check_last_string();
    url_query += ("offset=" + String(by));
    return *this;
}
// do select. execute this after building your query
String Supabase::doSelect()
{
    https.begin(client, hostname + "/rest/v1/" + url_query);
    https.addHeader("apikey", key);
    https.addHeader("Content-Type", "application/json");

    if (useAuth)
    {
        unsigned long t_now = millis();
        if (t_now - loginTime >= authTimeout)
        {
            _login_process();
        }
        https.addHeader("Authorization", "Bearer " + USER_TOKEN);
    }

    int httpCode = 0;
    while (httpCode <= 0)
    {
        httpCode = https.GET();
    }

    if (httpCode > 0)
    {
        data = https.getString();
    }
    https.end();
    urlQuery_reset();
    return data;
}
// do update. execute this after querying your update
int Supabase::doUpdate(String json)
{
    int httpCode;
    if (https.begin(client, hostname + "/rest/v1/" + url_query))
    {
        https.addHeader("apikey", key);
        https.addHeader("Content-Type", "application/json");
        if (useAuth)
        {
            unsigned long t_now = millis();
            if (t_now - loginTime >= authTimeout)
            {
                _login_process();
            }
            https.addHeader("Authorization", "Bearer " + USER_TOKEN);
        }
        unsigned long t0 = millis();
        httpCode = https.PATCH(json);
        // Serial.printf("PATCH took %d ms\n",millis()-t0);
        https.end();
    }
    else
    {
        return -100;
    }
    urlQuery_reset();
    return httpCode;
}

int Supabase::login_email(String email_a, String password_a)
{
    useAuth = true;
    loginMethod = "email";
    phone_or_email = email_a;
    password = password_a;

    int httpCode = 0;
    while (httpCode <= 0)
    {
        httpCode = _login_process();
    }
    return httpCode;
}

int Supabase::login_phone(String phone_a, String password_a)
{
    useAuth = true;
    loginMethod = "phone";
    phone_or_email = phone_a;
    password = password_a;

    int httpCode = 0;
    while (httpCode <= 0)
    {
        httpCode = _login_process();
    }
    return httpCode;
}

String Supabase::rpc(String func_name, String json_param)
{

    int httpCode;

    if (!https.begin(client, hostname + "/rpc/" + func_name))
    {
        return String(-100);
    }
    https.addHeader("apikey", key);
    https.addHeader("Content-Type", "application/json");

    if (useAuth)
    {
        unsigned long t_now = millis();
        if (t_now - loginTime >= authTimeout)
        {
            _login_process();
        }
        https.addHeader("Authorization", "Bearer " + USER_TOKEN);
    }

    httpCode = https.POST(json_param);
    if (httpCode > 0)
    {
        data = https.getString();
        https.end();
        return data;
    }

    https.end();
    return String(httpCode);
}

void Supabase::asyncUpdateTask(void *pvParameters)
{
    // int i = *((int*)pvParameters);
    JsonDocument doc = *((JsonDocument *)pvParameters);

    // TODO: Everything has to be static!
    // int httpCode;
    // if (https.begin(client, hostname + "/rest/v1/" + url_query))
    // {
    //   https.addHeader("apikey", key);
    //   https.addHeader("Content-Type", "application/json");
    //   if (useAuth)
    //   {
    //     unsigned long t_now = millis();
    //     if (t_now - loginTime >= authTimeout)
    //     {
    //       _login_process();
    //     }
    //     https.addHeader("Authorization", "Bearer " + USER_TOKEN);
    //   }
    //   unsigned long t0 = millis();
    //   httpCode = https.PATCH(json);
    //   Serial.printf("PATCH took %d ms\n",millis()-t0);
    //   https.end();
    // }
    // urlQuery_reset();
    vTaskDelete(NULL);
}

void Supabase::asyncUpdate(String json)
{
    xTaskCreate(
        asyncUpdateTask,
        "asyncUpdateTask",
        10000,
        &json,
        1,
        &asyncUpdateTaskHandle);
}